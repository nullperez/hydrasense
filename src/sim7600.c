/*
 * sim7600.c
 *
 * Portable SIM7600 modem driver, see sim7600.h. Depends only on the C library.
 */

#include "sim7600.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    SIM_UNINITIALIZED,
    SIM_RESET_ASSERT,       // hard reset sequence: reset pin low,
    SIM_RESET_RELEASE,      //   high,
    SIM_RESET_POWER_ON,     //   low again to power on, then high
    SIM_INITIALIZING,       // waiting for PB DONE after power up / reset
    SIM_DISABLING_ECHO,     // sent ATE0, waiting for OK
    SIM_CHECKING_SIM,       // sent AT+CPIN?, waiting for +CPIN: READY
    SIM_WAITING_NETWORK,    // polling AT+CGREG? until registered
    SIM_RUNNING,
    SIM_STATE_COUNT
} sim_state;

static const char* const stateNames[SIM_STATE_COUNT] = {
    "UNINITIALIZED", "RESET_ASSERT", "RESET_RELEASE", "RESET_POWER_ON",
    "INITIALIZING", "DISABLING_ECHO", "CHECKING_SIM", "WAITING_NETWORK", "RUNNING"
};

// seconds spent in each state before acting (reset phases) or retrying a command,
// and how many tries before a hard reset. 0 = no timeout
static const uint8_t stateTimeouts[SIM_STATE_COUNT] = { 0, 5, 2, 3, 45, 5, 5, 3, 0 };
static const uint8_t stateMaxTries[SIM_STATE_COUNT] = { 0, 0, 0, 0, 1, 3, 6, 60, 0 };

static sim7600_config config;
static const sim7600_client* clients[SIM7600_MAX_CLIENTS];
static uint8_t clientCount = 0;
static sim_state moduleState = SIM_UNINITIALIZED;
static uint32_t stateEnteredAt = 0;
static uint8_t tries = 0;
static bool resetRequested = false;
static uint32_t resetCount = 0;
static const void* lockOwner = NULL;

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

uint32_t sim7600Seconds(void) {
    return config.getSeconds ? config.getSeconds() : 0;
}

void sim7600Log(const char* fmt, ...) {
    char msg[96];
    va_list args;
    if (config.log == NULL) {
        return;
    }
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    config.log(msg);
}

static void setState(sim_state s) {
    if (s != moduleState) {
        tries = 0;
    }
    moduleState = s;
    stateEnteredAt = sim7600Seconds();
}

static void setResetPin(bool level) {
    if (config.setResetPin) {
        config.setResetPin(level);
    }
}

// strips CR/LF and surrounding spaces in place
static void cleanResponse(char* str) {
    int i, j = 0;
    for (i = 0; str[i] != '\0'; i++) {
        if (str[i] != '\r' && str[i] != '\n' && !(j == 0 && str[i] == ' ')) {
            str[j++] = str[i];
        }
    }
    while (j > 0 && str[j-1] == ' ') {
        j--;
    }
    str[j] = '\0';
}

static bool startsWith(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

// +CGREG: <n>,<stat>[,...]  stat 1 = registered home, 5 = registered roaming
static bool isRegistered(const char* resp) {
    const char* comma;
    if (!startsWith(resp, "+CGREG: ")) {
        return false;
    }
    comma = strchr(resp, ',');
    return comma != NULL && (comma[1] == '1' || comma[1] == '5') && (comma[2] == 0 || comma[2] == ',');
}

static void notifyReset(void) {
    uint8_t i;
    lockOwner = NULL;
    for (i = 0; i < clientCount; i++) {
        if (clients[i]->onReset) {
            clients[i]->onReset();
        }
    }
}

static void notifyReady(void) {
    uint8_t i;
    for (i = 0; i < clientCount; i++) {
        if (clients[i]->onReady) {
            clients[i]->onReady();
        }
    }
}

// sends the command for the current bring-up state
static void sendStateCommand(void) {
    switch (moduleState) {
    case SIM_DISABLING_ECHO:
        sim7600SendCommand("ATE0");
        break;
    case SIM_CHECKING_SIM:
        sim7600SendCommand("AT+CPIN?");
        break;
    case SIM_WAITING_NETWORK:
        sim7600SendCommand("AT+CGREG?");
        break;
    default:
        break;
    }
    tries++;
}

static void enterState(sim_state s) {
    setState(s);
    sendStateCommand();
}

// starts the hard reset sequence, sim7600Handler() steps through it
static void startReset(void) {
    sim7600Log("[SIM7600] Resetting modem");
    resetCount++;
    notifyReset();
    sim7600SendCommand("AT+CRESET");   // soft reset as well, in case the pin reset has no effect
    if (config.setResetPin) {
        setResetPin(false);
        setState(SIM_RESET_ASSERT);
    }
    else {
        setState(SIM_INITIALIZING);
    }
}

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------

bool sim7600Init(const sim7600_config* cfg) {
    if (cfg == NULL || cfg->uartWrite == NULL || cfg->getSeconds == NULL) {
        return false;
    }
    config = *cfg;
    sim7600Log("[SIM7600] Initializing");
    startReset();
    return true;
}

bool sim7600RegisterClient(const sim7600_client* client) {
    if (client == NULL || clientCount >= SIM7600_MAX_CLIENTS) {
        return false;
    }
    clients[clientCount++] = client;
    return true;
}

void sim7600SendCommand(const char* cmd) {
    if (config.uartWrite == NULL) {
        return;
    }
    sim7600Log("[SIM7600] > %s", cmd);
    config.uartWrite(cmd);
    config.uartWrite("\r");
}

void sim7600SendSecretCommand(const char* cmd) {
    if (config.uartWrite == NULL) {
        return;
    }
    sim7600Log("[SIM7600] > (command hidden)");
    config.uartWrite(cmd);
    config.uartWrite("\r");
}

void sim7600SendRaw(const char* data) {
    if (config.uartWrite == NULL) {
        return;
    }
    sim7600Log("[SIM7600] > %s", data);
    config.uartWrite(data);
}

bool sim7600IsReady(void) {
    return moduleState == SIM_RUNNING;
}

void sim7600RequestReset(void) {
    resetRequested = true;
}

bool sim7600Acquire(const void* owner) {
    if (lockOwner == NULL || lockOwner == owner) {
        lockOwner = owner;
        return true;
    }
    return false;
}

void sim7600Release(const void* owner) {
    if (lockOwner == owner) {
        lockOwner = NULL;
    }
}

const char* sim7600StateName(void) {
    return moduleState < SIM_STATE_COUNT ? stateNames[moduleState] : "?";
}

uint32_t sim7600ResetCount(void) {
    return resetCount;
}

void sim7600FeedLine(char* line) {
    uint8_t i;
    cleanResponse(line);
    if (line[0] == 0) { // if empty response
        return;
    }
    sim7600Log("[SIM7600] < %s", line);

    // the modem rebooted on its own (brown-out, crash): start over
    if (moduleState > SIM_INITIALIZING && strcmp(line, "RDY") == 0) {
        sim7600Log("[SIM7600] Unexpected modem restart");
        notifyReset();
        setState(SIM_INITIALIZING);
        return;
    }

    switch (moduleState) {
    case SIM_INITIALIZING:
        if (strcmp(line, "PB DONE") == 0) {
            enterState(SIM_DISABLING_ECHO);
        }
        break;
    case SIM_DISABLING_ECHO:
        if (strcmp(line, "OK") == 0) { // the "ATE0" echo line (if echo was on) is ignored
            enterState(SIM_CHECKING_SIM);
        }
        break;
    case SIM_CHECKING_SIM:
        if (strcmp(line, "+CPIN: READY") == 0) {
            enterState(SIM_WAITING_NETWORK);
        }
        // anything else (SIM PIN, ERROR, ...) is retried on timeout
        break;
    case SIM_WAITING_NETWORK:
        if (isRegistered(line)) {
            sim7600Log("[SIM7600] Registered on network");
            setState(SIM_RUNNING);
            notifyReady();
        }
        break;
    case SIM_RUNNING:
        for (i = 0; i < clientCount; i++) {
            if (clients[i]->onLine) {
                clients[i]->onLine(line);
            }
        }
        break;
    default:
        break;  // lines during the reset sequence are ignored
    }
}

void sim7600Handler(void) {
    uint8_t i;
    uint32_t elapsed;
    if (moduleState == SIM_UNINITIALIZED) {
        return;
    }
    if (resetRequested) {
        resetRequested = false;
        startReset();
        return;
    }
    if (moduleState == SIM_RUNNING) {
        for (i = 0; i < clientCount; i++) {
            if (clients[i]->poll) {
                clients[i]->poll();
            }
        }
        return;
    }
    elapsed = sim7600Seconds() - stateEnteredAt;
    if (elapsed < stateTimeouts[moduleState]) {
        return;
    }
    switch (moduleState) {
    case SIM_RESET_ASSERT:
        setResetPin(true);
        setState(SIM_RESET_RELEASE);
        break;
    case SIM_RESET_RELEASE:
        setResetPin(false);
        setState(SIM_RESET_POWER_ON);
        break;
    case SIM_RESET_POWER_ON:
        setResetPin(true);
        setState(SIM_INITIALIZING);
        break;
    case SIM_INITIALIZING:
        // missed PB DONE (or the modem was already up): probe it with ATE0
        sim7600Log("[SIM7600] No PB DONE from modem, probing");
        enterState(SIM_DISABLING_ECHO);
        break;
    default:
        if (tries >= stateMaxTries[moduleState]) {
            sim7600Log("[SIM7600] Not responding in state %s", sim7600StateName());
            startReset();
        }
        else {
            stateEnteredAt = sim7600Seconds();
            sendStateCommand(); // retry
        }
        break;
    }
}
