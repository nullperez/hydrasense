/*
 * mqtt.c
 *
 *  Created on: Mar 22, 2025
 *      Author: gianc
 *
 * MQTT client state machine on top of the SIM7600 AT+CMQTT* commands.
 * Every state that waits on the modem has a timeout. Any failure tears the
 * client down (DISC/REL/STOP), backs off, and starts over. Repeated failures
 * escalate to a hard reset of the modem.
 */

#include "mqtt.h"
#include "sim7600.h"
#include "strlib.h"
#include "uart1.h"
#include "uart0.h"
#include "timer.h"
#include "shell.h"
#include "secrets.h"   // MQTTBROKER, MQTTPORT, MQTTUSER, MQTTPASS (copy secrets.example.h)
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define MQTT_CLIENT_ID "TM4C"
#define MQTT_KEEPALIVE_S 60
#define MQTT_QOS 2
#define MQTT_PUB_TIMEOUT_S 60

#define MQTT_MAX_FAILURES 5         // consecutive failures before the modem is hard reset
#define MQTT_BACKOFF_MIN_S 5
#define MQTT_BACKOFF_MAX_S 300

typedef enum {
    MQTT_NOTREADY,
    MQTT_STARTING,          // sent CMQTTSTART, waiting for +CMQTTSTART: 0
    MQTT_ACQUIRING_CLIENT,  // sent CMQTTACCQ, waiting for OK
    MQTT_CONNECTING,        // sent CMQTTCONNECT, waiting for +CMQTTCONNECT: 0,0
    MQTT_CONNECTED,
    MQTT_TOPIC_PROMPT,      // sent CMQTTTOPIC, waiting for '>'
    MQTT_TOPIC_SENT,        // sent topic, waiting for OK
    MQTT_PAYLOAD_PROMPT,    // sent CMQTTPAYLOAD, waiting for '>'
    MQTT_PAYLOAD_SENT,      // sent payload, waiting for OK
    MQTT_PUBLISHING,        // sent CMQTTPUB, waiting for +CMQTTPUB: 0,0
    MQTT_CLEANUP_DISC,      // tearing down after a failure, each step advances on OK/ERROR/timeout
    MQTT_CLEANUP_REL,
    MQTT_CLEANUP_STOP,
    MQTT_BACKOFF,           // waiting before retrying
    MQTT_STATE_COUNT
} mqtt_state;

static const char* const stateNames[MQTT_STATE_COUNT] = {
    "NOTREADY", "STARTING", "ACQUIRING_CLIENT", "CONNECTING", "CONNECTED",
    "TOPIC_PROMPT", "TOPIC_SENT", "PAYLOAD_PROMPT", "PAYLOAD_SENT", "PUBLISHING",
    "CLEANUP_DISC", "CLEANUP_REL", "CLEANUP_STOP", "BACKOFF"
};

// seconds to wait in each state before giving up, 0 = no timeout
static const uint8_t stateTimeouts[MQTT_STATE_COUNT] = {
    0, 15, 10, 30, 0,
    5, 5, 5, 5, MQTT_PUB_TIMEOUT_S + 10,
    5, 5, 5, 0
};

static mqtt_state mqttState = MQTT_NOTREADY;
static uint32_t stateEnteredAt = 0;
static uint8_t failureCount = 0;
static uint32_t backoffSeconds = 0;
static char publishingTopic[MQTT_MAX_TOPIC];
static char publishingData[MQTT_MAX_PAYLOAD];

static void setState(mqtt_state s) {
    mqttState = s;
    stateEnteredAt = getUptimeSeconds();
}

static bool isFinalError(const char* resp) {
    return str_equal(resp, "ERROR") || str_starts_with(resp, "+CME ERROR");
}

// parses "<prefix><code>" and stores code. returns false if resp does not match
static bool parseResultCode(const char* resp, const char* prefix, uint32_t* code) {
    const char* p;
    if (!str_starts_with(resp, prefix)) {
        return false;
    }
    p = resp + str_length(prefix);
    if (*p < '0' || *p > '9') {
        return false;
    }
    *code = to_uint32(p, 10);
    return true;
}

static void sendCmdWithLength(const char* prefix, uint32_t len) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "%s%lu", prefix, (unsigned long)len);
    sendATCommand(cmd);
}

static void sendData(const char* data) {
    printShell("> ");
    putsUart0(data);
    putcUart0('\n');
    putsUart1(data);    // modem reads exactly the announced length, no terminator
}

static void mqttFail(const char* reason) {
    uint32_t i;
    printShell("MQTT failure in state ");
    putsUart0(getMqttStateName());
    putsUart0(": ");
    putsUart0(reason);
    putcUart0('\n');

    if (failureCount < 255) {
        failureCount++;
    }
    if (failureCount >= MQTT_MAX_FAILURES) {
        printShell("Too many MQTT failures, resetting modem\n");
        failureCount = 0;
        setState(MQTT_NOTREADY);
        sim7600RequestReset();
        return;
    }
    backoffSeconds = MQTT_BACKOFF_MIN_S;
    for (i = 1; i < failureCount && backoffSeconds < MQTT_BACKOFF_MAX_S; i++) {
        backoffSeconds *= 2;
    }
    if (backoffSeconds > MQTT_BACKOFF_MAX_S) {
        backoffSeconds = MQTT_BACKOFF_MAX_S;
    }
    sendATCommand("AT+CMQTTDISC=0,10");
    setState(MQTT_CLEANUP_DISC);
}

static void startMqttClient() {
    sendATCommand("AT+CMQTTSTART");
    setState(MQTT_STARTING);
}

static void mqttAcquireClient(const char* cid) {
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\"", cid);
    sendATCommand(cmd);
    setState(MQTT_ACQUIRING_CLIENT);
}

static void mqttConnect(const char* server) {
    char cmd[128];
    int len = snprintf(cmd, sizeof(cmd), "AT+CMQTTCONNECT=0,\"tcp://%s:%s\",%d,1,\"%s\",\"%s\"",
                       server, MQTTPORT, MQTT_KEEPALIVE_S, MQTTUSER, MQTTPASS);
    if (len < 0 || len >= (int)sizeof(cmd)) {
        mqttFail("connect command too long");
        return;
    }
    // log without the password
    printShell("AT+CMQTTCONNECT=0,\"tcp://");
    putsUart0(server);
    putcUart0(':');
    putsUart0(MQTTPORT);
    putsUart0("\",...,\"");
    putsUart0(MQTTUSER);
    putsUart0("\",\"****\"\n");

    putsUart1(cmd);
    putcUart1('\r');
    setState(MQTT_CONNECTING);
}

// forget all client state, e.g. after the modem was reset
void mqttReset() {
    setState(MQTT_NOTREADY);
}

bool mqttIsConnected() {
    return mqttState == MQTT_CONNECTED;
}

// starts publishing data to topic. returns false if not connected or a publish is in progress
bool mqttPublish(const char* topic, const char* data) {
    uint32_t topicLen = str_length(topic);
    uint32_t dataLen = str_length(data);
    if (mqttState != MQTT_CONNECTED) {
        return false;
    }
    if (topicLen == 0 || dataLen == 0
            || !str_ncopy(publishingTopic, topic, sizeof(publishingTopic))
            || !str_ncopy(publishingData, data, sizeof(publishingData))) {
        printShell("MQTT publish rejected: empty or too long\n");
        return false;
    }
    sendCmdWithLength("AT+CMQTTTOPIC=0,", topicLen);
    setState(MQTT_TOPIC_PROMPT);
    return true;
}

uint8_t getMqttState() {
    return mqttState;
}

const char* getMqttStateName() {
    return mqttState < MQTT_STATE_COUNT ? stateNames[mqttState] : "?";
}

void processMqttResponse(const char* resp) {
    uint32_t code;

    // unsolicited: connection to the broker or the network was lost
    if (str_starts_with(resp, "+CMQTTCONNLOST") || str_starts_with(resp, "+CMQTTNONET")) {
        if (mqttState >= MQTT_STARTING && mqttState <= MQTT_PUBLISHING) {
            mqttFail(resp);
        }
        return;
    }

    switch (mqttState) {
    case MQTT_NOTREADY:
    case MQTT_CONNECTED:
    case MQTT_BACKOFF:
        break;
    case MQTT_STARTING:
        if (parseResultCode(resp, "+CMQTTSTART: ", &code)) {
            if (code == 0) {
                mqttAcquireClient(MQTT_CLIENT_ID);
            }
            else {
                mqttFail(resp);
            }
        }
        else if (isFinalError(resp)) {
            mqttFail(resp);
        }
        break;
    case MQTT_ACQUIRING_CLIENT:
        if (str_equal(resp, "OK")) {
            mqttConnect(MQTTBROKER);
        }
        else if (isFinalError(resp) || str_starts_with(resp, "+CMQTTACCQ: ")) {
            mqttFail(resp);
        }
        break;
    case MQTT_CONNECTING:
        if (parseResultCode(resp, "+CMQTTCONNECT: 0,", &code)) {
            if (code == 0) {
                printShell("Connected to MQTT Broker: ");
                putsUart0(MQTTBROKER);
                putcUart0(':');
                putsUart0(MQTTPORT);
                putcUart0('\n');
                failureCount = 0;
                setState(MQTT_CONNECTED);
            }
            else {
                mqttFail(resp); // connection rejected (bad credentials, broker unreachable, ...)
            }
        }
        else if (isFinalError(resp)) {
            mqttFail(resp);
        }
        break;
    case MQTT_TOPIC_PROMPT:
        if (str_equal(resp, ">")) {
            sendData(publishingTopic);
            setState(MQTT_TOPIC_SENT);
        }
        else if (isFinalError(resp)) {
            mqttFail(resp);
        }
        break;
    case MQTT_TOPIC_SENT:
        if (str_equal(resp, "OK")) {
            sendCmdWithLength("AT+CMQTTPAYLOAD=0,", str_length(publishingData));
            setState(MQTT_PAYLOAD_PROMPT);
        }
        else if (isFinalError(resp)) {
            mqttFail(resp);
        }
        break;
    case MQTT_PAYLOAD_PROMPT:
        if (str_equal(resp, ">")) {
            sendData(publishingData);
            setState(MQTT_PAYLOAD_SENT);
        }
        else if (isFinalError(resp)) {
            mqttFail(resp);
        }
        break;
    case MQTT_PAYLOAD_SENT:
        if (str_equal(resp, "OK")) {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+CMQTTPUB=0,%d,%d", MQTT_QOS, MQTT_PUB_TIMEOUT_S);
            sendATCommand(cmd);
            setState(MQTT_PUBLISHING);
        }
        else if (isFinalError(resp)) {
            mqttFail(resp);
        }
        break;
    case MQTT_PUBLISHING:
        // OK only means the command was accepted, +CMQTTPUB reports the actual result
        if (parseResultCode(resp, "+CMQTTPUB: 0,", &code)) {
            if (code == 0) {
                setState(MQTT_CONNECTED);
            }
            else {
                mqttFail(resp);
            }
        }
        else if (isFinalError(resp)) {
            mqttFail(resp);
        }
        break;
    case MQTT_CLEANUP_DISC:
        if (str_equal(resp, "OK") || isFinalError(resp)) {
            sendATCommand("AT+CMQTTREL=0");
            setState(MQTT_CLEANUP_REL);
        }
        break;
    case MQTT_CLEANUP_REL:
        if (str_equal(resp, "OK") || isFinalError(resp)) {
            sendATCommand("AT+CMQTTSTOP");
            setState(MQTT_CLEANUP_STOP);
        }
        break;
    case MQTT_CLEANUP_STOP:
        if (str_equal(resp, "OK") || isFinalError(resp)) {
            setState(MQTT_BACKOFF);
        }
        break;
    default:
        break;
    }
}

// call periodically from the main loop while the modem is running
void mqttHandler() {
    uint32_t elapsed = getUptimeSeconds() - stateEnteredAt;
    uint8_t timeout = mqttState < MQTT_STATE_COUNT ? stateTimeouts[mqttState] : 0;

    switch (mqttState) {
    case MQTT_NOTREADY:
        startMqttClient();
        return;
    case MQTT_BACKOFF:
        if (elapsed >= backoffSeconds) {
            setState(MQTT_NOTREADY);
        }
        return;
    // cleanup steps never fail, a timeout just moves on to the next step
    case MQTT_CLEANUP_DISC:
        if (elapsed >= timeout) {
            processMqttResponse("ERROR");
        }
        return;
    case MQTT_CLEANUP_REL:
        if (elapsed >= timeout) {
            processMqttResponse("ERROR");
        }
        return;
    case MQTT_CLEANUP_STOP:
        if (elapsed >= timeout) {
            setState(MQTT_BACKOFF);
        }
        return;
    default:
        break;
    }
    if (timeout != 0 && elapsed >= timeout) {
        mqttFail("timeout");
    }
}
