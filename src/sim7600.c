#include <stddef.h>
#include "sim7600.h"
#include "shell.h"
#include "gpio.h"
#include "uart0.h"
#include "uart1.h"
#include "nvic.h"
#include "mqtt.h"
#include "strlib.h"
#include "timer.h"
#include "wait.h"
#include "watchdog.h"
#include "tm4c123gh6pm.h"

#define RST PORTA,7

typedef enum {
    SIM_INITIALIZING,       // waiting for PB DONE after power up / reset
    SIM_DISABLING_ECHO,     // sent ATE0, waiting for OK
    SIM_CHECKING_SIM,       // sent AT+CPIN?, waiting for +CPIN: READY
    SIM_WAITING_NETWORK,    // polling AT+CGREG? until registered
    SIM_RUNNING,
    SIM_STATE_COUNT
} sim_state;

static const char* const stateNames[SIM_STATE_COUNT] = {
    "INITIALIZING", "DISABLING_ECHO", "CHECKING_SIM", "WAITING_NETWORK", "RUNNING"
};

// seconds before a command is retried in each state, and how many tries before a hard reset
static const uint8_t stateTimeouts[SIM_STATE_COUNT] = { 45, 5, 5, 3, 0 };
static const uint8_t stateMaxTries[SIM_STATE_COUNT] = { 1, 3, 6, 60, 0 };

static sim_state moduleState = SIM_INITIALIZING;
static uint32_t stateEnteredAt = 0;
static uint8_t tries = 0;
static bool resetRequested = false;
static uint32_t resetCount = 0;

static void setState(sim_state s) {
    if (s != moduleState) {
        tries = 0;
    }
    moduleState = s;
    stateEnteredAt = getUptimeSeconds();
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

// sends the command for the current state
static void sendStateCommand() {
    switch (moduleState) {
    case SIM_DISABLING_ECHO:
        sendATCommand("ATE0");
        break;
    case SIM_CHECKING_SIM:
        sendATCommand("AT+CPIN?");
        break;
    case SIM_WAITING_NETWORK:
        sendATCommand("AT+CGREG?");
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

// +CGREG: <n>,<stat>[,...]  stat 1 = registered home, 5 = registered roaming
static bool isRegistered(const char* resp) {
    const char* comma;
    if (!str_starts_with(resp, "+CGREG: ")) {
        return false;
    }
    comma = str_char(resp, ',');
    return comma != NULL && (comma[1] == '1' || comma[1] == '5') && (comma[2] == 0 || comma[2] == ',');
}

void processATResponse(char* resp) {
    cleanResponse(resp);
    if (resp[0] == 0) { // if empty response
        return;
    }

    putsUart0("[SIM7600]: ");
    putsUart0(resp);
    putsUart0("\n");

    // the modem rebooted on its own (brown-out, crash): start over
    if (moduleState != SIM_INITIALIZING && str_equal(resp, "RDY")) {
        printShell("Unexpected modem restart\n");
        mqttReset();
        setState(SIM_INITIALIZING);
        return;
    }

    switch (moduleState) {
    case SIM_INITIALIZING:
        if (str_equal(resp, "PB DONE")) {
            enterState(SIM_DISABLING_ECHO);
        }
        break;
    case SIM_DISABLING_ECHO:
        if (str_equal(resp, "OK")) { // the "ATE0" echo line (if echo was on) is ignored
            enterState(SIM_CHECKING_SIM);
        }
        break;
    case SIM_CHECKING_SIM:
        if (str_equal(resp, "+CPIN: READY")) {
            enterState(SIM_WAITING_NETWORK);
        }
        // anything else (SIM PIN, ERROR, ...) is retried on timeout
        break;
    case SIM_WAITING_NETWORK:
        if (isRegistered(resp)) {
            printShell("Registered on network\n");
            mqttReset();
            setState(SIM_RUNNING);
        }
        break;
    case SIM_RUNNING:
        processMqttResponse(resp);
        break;
    default:
        break;
    }
}

// hard reset via the reset pin. blocks ~10 s, keeps the watchdog fed
static void resetSim7600() {
    printShell("Resetting SIM7600\n");
    sendATCommand("AT+CRESET");   // soft reset as well, in case the pin reset has no effect
    resetCount++;
    mqttReset();
    //turning off
    setPinValue(RST, 0);
    waitMicrosecond(5e6);
    feedWatchdog();
    setPinValue(RST, 1);
    //wait
    waitMicrosecond(2e6);
    feedWatchdog();
    //turning on
    setPinValue(RST, 0);
    waitMicrosecond(3e6);
    feedWatchdog();
    setPinValue(RST, 1);
    setState(SIM_INITIALIZING);
}

// safe to call from anywhere: the reset itself happens in sim7600Handler()
void sim7600RequestReset() {
    resetRequested = true;
}

void sendATCommand(const char* cmd) {
    putsUart0("[TM4C]: ");
    putsUart0(cmd);
    putsUart0("\n");

    putsUart1(cmd);
    putcUart1('\r');
}

// call from the main loop
void sim7600Handler() {
    uint32_t elapsed;
    if (resetRequested) {
        resetRequested = false;
        resetSim7600();
        return;
    }
    if (moduleState == SIM_RUNNING) {
        mqttHandler(); //call mqtt state machine
        return;
    }
    elapsed = getUptimeSeconds() - stateEnteredAt;
    if (elapsed < stateTimeouts[moduleState]) {
        return;
    }
    if (moduleState == SIM_INITIALIZING) {
        // missed PB DONE (or the modem was already up): probe it with ATE0
        printShell("No PB DONE from modem, probing\n");
        enterState(SIM_DISABLING_ECHO);
    }
    else if (tries >= stateMaxTries[moduleState]) {
        printShell("Modem not responding in state ");
        putsUart0(getSim7600StateName());
        putcUart0('\n');
        resetSim7600();
    }
    else {
        stateEnteredAt = getUptimeSeconds();
        sendStateCommand(); // retry
    }
}

const char* getSim7600StateName() {
    return moduleState < SIM_STATE_COUNT ? stateNames[moduleState] : "?";
}

uint32_t getSim7600ResetCount() {
    return resetCount;
}

void initSim7600() {
    initUart1();
    setUart1BaudRate(115200, 40e6);
    setUart1Callback(processATResponse);
    enableUart1RxInt();
    enableNvicInterrupt(INT_UART1);

    enablePort(PORTA);
    selectPinPushPullOutput(RST);

    printShell("Initializing UART1 (SIM7600A-H)\n");
    resetSim7600();
}
