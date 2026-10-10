#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "tm4c123gh6pm.h"
#include "clock.h"
#include "uart0.h"
#include "uart1.h"
#include "wait.h"
#include "sim7600.h"
#include "mqtt.h"
#include "shell.h"
#include "sensors.h"
#include "gpio.h"
#include "strlib.h"
#include "nvic.h"
#include "timer.h"
#include "watchdog.h"
#include "secrets.h"   // MQTTBROKER, MQTTPORT, MQTTUSER, MQTTPASS (copy secrets.example.h)

#define PUBLISH_PERIOD_S 60
#define WATCHDOG_TIMEOUT_S 15   // MCU resets after 2x this without a feed

#define MODEM_RST PORTA,7

static volatile bool publishPending = false;

//-----------------------------------------------------------------------------
// Board glue for the portable SIM7600 driver
//-----------------------------------------------------------------------------

static void modemSetResetPin(bool level) {
    setPinValue(MODEM_RST, level);
}

static void consoleLog(const char* msg) {
    putsUart0(msg);
    putcUart0('\n');
}

static const sim7600_config modemConfig = {
    .uartWrite = putsUart1,
    .getSeconds = getUptimeSeconds,
    .setResetPin = modemSetResetPin,
    .log = consoleLog
};

static const mqtt_config mqttConfig = {
    .host = MQTTBROKER,
    .port = MQTTPORT,
    .clientId = "TM4C",
    .user = MQTTUSER,
    .pass = MQTTPASS,
    .keepAliveS = 60,
    .qos = 2
};

static void initModem() {
    printShell("Initializing UART1 (SIM7600A-H)\n");
    initUart1();
    setUart1BaudRate(115200, 40e6);
    setUart1Callback(sim7600FeedLine);
    enableUart1RxInt();
    enableNvicInterrupt(INT_UART1);

    enablePort(PORTA);
    selectPinPushPullOutput(MODEM_RST);

    if (!sim7600Init(&modemConfig) || !mqttInit(&mqttConfig)) {
        printShell("Modem/MQTT config invalid\n");
    }
}

//-----------------------------------------------------------------------------
// Application
//-----------------------------------------------------------------------------

// runs in the timer ISR: only flag the work, the main loop does it
static void periodicCallback(void* context) {
    publishPending = true;
}

static void publishSensors() {
    uint8_t v = readValvePosition();
    float f = readFlowMeter();
    float p = readPressure();
    char pubBuffer[MQTT_MAX_PAYLOAD];
    int len = snprintf(pubBuffer, sizeof(pubBuffer), "%d,%.2f,%.2f", v, f, p);
    if (len < 0 || len >= (int)sizeof(pubBuffer)) {
        printShell("Sensor payload did not fit, skipping\n");
        return;
    }
    printShell("Publishing data: \"");
    putsUart0(pubBuffer);
    putsUart0("\"\n");
    if (!mqttPublish("sensors", pubBuffer)) {
        printShell("Publish failed to start\n");
    }
}

static void printResetCause() {
    uint32_t cause = SYSCTL_RESC_R;
    SYSCTL_RESC_R = 0;
    printShell("Reset cause:");
    if (cause & SYSCTL_RESC_POR) putsUart0(" power-on");
    if (cause & SYSCTL_RESC_EXT) putsUart0(" external");
    if (cause & SYSCTL_RESC_BOR) putsUart0(" brown-out");
    if (cause & SYSCTL_RESC_WDT0) putsUart0(" WATCHDOG");
    if (cause & SYSCTL_RESC_SW) putsUart0(" software");
    putcUart0('\n');
}

int main(void) {
    initSystemClockTo40Mhz();
    initWatchdog(WATCHDOG_TIMEOUT_S, 40e6);
    initTimer();
    initShell();
    printResetCause();
    initSensors();
    initModem();
    if (startPeriodicTimer(periodicCallback, PUBLISH_PERIOD_S, 0) == INVALID_TIMER) {
        printShell("Could not start publish timer\n");
    }
    while (1) {
        feedWatchdog();
        processUart0Lines();
        processUart1Lines();
        sim7600Handler();
        // stays pending while disconnected or busy, so a reading goes out as soon as we can send
        if (publishPending && mqttIsConnected()) {
            publishPending = false;
            publishSensors();
        }
    }
}
