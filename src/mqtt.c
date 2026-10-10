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
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MQTT_DEFAULT_KEEPALIVE_S 60
#define MQTT_PUB_TIMEOUT_S 60

#define MQTT_MAX_FAILURES 5         // consecutive failures before the modem is hard reset
#define MQTT_BACKOFF_MIN_S 5
#define MQTT_BACKOFF_MAX_S 300

typedef enum {
    MQTT_IDLE,              // modem not ready
    MQTT_NOTREADY,          // modem ready, client not started
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
    "IDLE", "NOTREADY", "STARTING", "ACQUIRING_CLIENT", "CONNECTING", "CONNECTED",
    "TOPIC_PROMPT", "TOPIC_SENT", "PAYLOAD_PROMPT", "PAYLOAD_SENT", "PUBLISHING",
    "CLEANUP_DISC", "CLEANUP_REL", "CLEANUP_STOP", "BACKOFF"
};

// seconds to wait in each state before giving up, 0 = no timeout
static const uint8_t stateTimeouts[MQTT_STATE_COUNT] = {
    0, 0, 15, 10, 30, 0,
    5, 5, 5, 5, MQTT_PUB_TIMEOUT_S + 10,
    5, 5, 5, 0
};

static mqtt_config config;
static mqtt_state mqttState = MQTT_IDLE;
static uint32_t stateEnteredAt = 0;
static uint8_t failureCount = 0;
static uint32_t backoffSeconds = 0;
static char publishingTopic[MQTT_MAX_TOPIC];
static char publishingData[MQTT_MAX_PAYLOAD];

static void onModemReady(void);
static void onModemLine(const char* line);
static void onModemReset(void);
static void mqttPoll(void);

static const sim7600_client modemClient = {
    .onReady = onModemReady,
    .onLine = onModemLine,
    .onReset = onModemReset,
    .poll = mqttPoll
};

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

static void setState(mqtt_state s) {
    mqttState = s;
    stateEnteredAt = sim7600Seconds();
    // hold the modem while a command sequence is in flight
    if (s == MQTT_IDLE || s == MQTT_NOTREADY || s == MQTT_CONNECTED || s == MQTT_BACKOFF) {
        sim7600Release(&modemClient);
    }
}

static bool startsWith(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

static bool isFinalError(const char* resp) {
    return strcmp(resp, "ERROR") == 0 || startsWith(resp, "+CME ERROR");
}

// copies src into dest, returns false if it did not fit
static bool copyString(char* dest, const char* src, size_t size) {
    size_t len = strlen(src);
    if (len >= size) {
        return false;
    }
    memcpy(dest, src, len + 1);
    return true;
}

// parses "<prefix><code>" and stores code. returns false if resp does not match
static bool parseResultCode(const char* resp, const char* prefix, uint32_t* code) {
    const char* p;
    if (!startsWith(resp, prefix)) {
        return false;
    }
    p = resp + strlen(prefix);
    if (*p < '0' || *p > '9') {
        return false;
    }
    *code = strtoul(p, NULL, 10);
    return true;
}

static void sendCmdWithLength(const char* prefix, size_t len) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "%s%u", prefix, (unsigned)len);
    sim7600SendCommand(cmd);
}

static void mqttFail(const char* reason) {
    uint32_t i;
    sim7600Log("[MQTT] Failure in state %s: %s", mqttStateName(), reason);

    if (failureCount < 255) {
        failureCount++;
    }
    if (failureCount >= MQTT_MAX_FAILURES) {
        sim7600Log("[MQTT] Too many failures, resetting modem");
        failureCount = 0;
        setState(MQTT_IDLE);
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
    sim7600SendCommand("AT+CMQTTDISC=0,10");
    setState(MQTT_CLEANUP_DISC);
}

static void startMqttClient(void) {
    if (!sim7600Acquire(&modemClient)) {
        return;     // another client is using the modem, try again on the next poll
    }
    sim7600SendCommand("AT+CMQTTSTART");
    setState(MQTT_STARTING);
}

static void mqttAcquireClient(void) {
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\"", config.clientId);
    sim7600SendCommand(cmd);
    setState(MQTT_ACQUIRING_CLIENT);
}

static void mqttConnect(void) {
    char cmd[160];
    int len;
    if (config.user != NULL) {
        len = snprintf(cmd, sizeof(cmd), "AT+CMQTTCONNECT=0,\"tcp://%s:%u\",%u,1,\"%s\",\"%s\"",
                       config.host, config.port, config.keepAliveS, config.user,
                       config.pass ? config.pass : "");
    }
    else {
        len = snprintf(cmd, sizeof(cmd), "AT+CMQTTCONNECT=0,\"tcp://%s:%u\",%u,1",
                       config.host, config.port, config.keepAliveS);
    }
    if (len < 0 || len >= (int)sizeof(cmd)) {
        mqttFail("connect command too long");
        return;
    }
    sim7600Log("[MQTT] Connecting to %s:%u as %s", config.host, config.port,
               config.user ? config.user : "(anonymous)");
    sim7600SendSecretCommand(cmd);  // contains the password, keep it out of the log
    setState(MQTT_CONNECTING);
}

//-----------------------------------------------------------------------------
// sim7600 client callbacks
//-----------------------------------------------------------------------------

static void onModemReady(void) {
    setState(MQTT_NOTREADY);
}

static void onModemReset(void) {
    setState(MQTT_IDLE);
}

static void onModemLine(const char* resp) {
    uint32_t code;

    // unsolicited: connection to the broker or the network was lost
    if (startsWith(resp, "+CMQTTCONNLOST") || startsWith(resp, "+CMQTTNONET")) {
        if (mqttState >= MQTT_STARTING && mqttState <= MQTT_PUBLISHING) {
            mqttFail(resp);
        }
        return;
    }

    switch (mqttState) {
    case MQTT_STARTING:
        if (parseResultCode(resp, "+CMQTTSTART: ", &code)) {
            if (code == 0) {
                mqttAcquireClient();
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
        if (strcmp(resp, "OK") == 0) {
            mqttConnect();
        }
        else if (isFinalError(resp) || startsWith(resp, "+CMQTTACCQ: ")) {
            mqttFail(resp);
        }
        break;
    case MQTT_CONNECTING:
        if (parseResultCode(resp, "+CMQTTCONNECT: 0,", &code)) {
            if (code == 0) {
                sim7600Log("[MQTT] Connected to %s:%u", config.host, config.port);
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
        if (strcmp(resp, ">") == 0) {
            sim7600SendRaw(publishingTopic); // modem reads exactly the announced length
            setState(MQTT_TOPIC_SENT);
        }
        else if (isFinalError(resp)) {
            mqttFail(resp);
        }
        break;
    case MQTT_TOPIC_SENT:
        if (strcmp(resp, "OK") == 0) {
            sendCmdWithLength("AT+CMQTTPAYLOAD=0,", strlen(publishingData));
            setState(MQTT_PAYLOAD_PROMPT);
        }
        else if (isFinalError(resp)) {
            mqttFail(resp);
        }
        break;
    case MQTT_PAYLOAD_PROMPT:
        if (strcmp(resp, ">") == 0) {
            sim7600SendRaw(publishingData);
            setState(MQTT_PAYLOAD_SENT);
        }
        else if (isFinalError(resp)) {
            mqttFail(resp);
        }
        break;
    case MQTT_PAYLOAD_SENT:
        if (strcmp(resp, "OK") == 0) {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+CMQTTPUB=0,%u,%u", config.qos, MQTT_PUB_TIMEOUT_S);
            sim7600SendCommand(cmd);
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
        if (strcmp(resp, "OK") == 0 || isFinalError(resp)) {
            sim7600SendCommand("AT+CMQTTREL=0");
            setState(MQTT_CLEANUP_REL);
        }
        break;
    case MQTT_CLEANUP_REL:
        if (strcmp(resp, "OK") == 0 || isFinalError(resp)) {
            sim7600SendCommand("AT+CMQTTSTOP");
            setState(MQTT_CLEANUP_STOP);
        }
        break;
    case MQTT_CLEANUP_STOP:
        if (strcmp(resp, "OK") == 0 || isFinalError(resp)) {
            setState(MQTT_BACKOFF);
        }
        break;
    default:
        break;
    }
}

// called by sim7600Handler() while the modem is ready
static void mqttPoll(void) {
    uint32_t elapsed = sim7600Seconds() - stateEnteredAt;
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
    case MQTT_CLEANUP_REL:
    case MQTT_CLEANUP_STOP:
        if (elapsed >= timeout) {
            onModemLine("ERROR");
        }
        return;
    default:
        break;
    }
    if (timeout != 0 && elapsed >= timeout) {
        mqttFail("timeout");
    }
}

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------

bool mqttInit(const mqtt_config* cfg) {
    if (cfg == NULL || cfg->host == NULL || cfg->clientId == NULL || cfg->qos > 2) {
        return false;
    }
    config = *cfg;
    if (config.keepAliveS == 0) {
        config.keepAliveS = MQTT_DEFAULT_KEEPALIVE_S;
    }
    setState(sim7600IsReady() ? MQTT_NOTREADY : MQTT_IDLE);
    return sim7600RegisterClient(&modemClient);
}

bool mqttIsConnected(void) {
    return mqttState == MQTT_CONNECTED;
}

// starts publishing data to topic. returns false if not connected or a publish is in progress
bool mqttPublish(const char* topic, const char* data) {
    if (mqttState != MQTT_CONNECTED) {
        return false;
    }
    if (topic[0] == 0 || data[0] == 0
            || !copyString(publishingTopic, topic, sizeof(publishingTopic))
            || !copyString(publishingData, data, sizeof(publishingData))) {
        sim7600Log("[MQTT] Publish rejected: empty or too long");
        return false;
    }
    if (!sim7600Acquire(&modemClient)) {
        return false;   // another client is using the modem
    }
    sendCmdWithLength("AT+CMQTTTOPIC=0,", strlen(publishingTopic));
    setState(MQTT_TOPIC_PROMPT);
    return true;
}

const char* mqttStateName(void) {
    return mqttState < MQTT_STATE_COUNT ? stateNames[mqttState] : "?";
}
