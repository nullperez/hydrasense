/*
 * shell.c
 *
 *  Created on: Sep 6, 2024
 *      Author:
 */

//-----------------------------------------------------------------------------
// Device includes
//-----------------------------------------------------------------------------

#include "shell.h"
#include "uart0.h"
#include "uart1.h"
#include "nvic.h"
#include "clock.h"
#include "tm4c123gh6pm.h"
#include "strlib.h"
#include "sim7600.h"
#include "wait.h"
#include "mqtt.h"
#include "timer.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SHELL_MODE,
    AT_MODE
} shell_mode;

shell_mode uartMode = SHELL_MODE;

void processShellCmd(char* line) {
    //putsUart0("Received string: ");
    //putsUart0(line);
    //putsUart0("\n");
    shell_cmd data;
    int valid = 0;
    if (!str_ncopy(data.buffer, line, sizeof(data.buffer))) {
        putsUart0("Command too long\n>");
        return;
    }
    parseFields(&data);
    if (isCommand(&data, "mode", 1)) {
        valid = 1;
        char* mode = getFieldString(&data, 1);
        if (str_equal(mode, "at")) {
            uartMode = AT_MODE;
            putsUart0("Switched to AT command mode.\n");
        }
        else if (str_equal(mode, "shell")) {
            uartMode = SHELL_MODE;
            putsUart0("Switched to MCU Shell mode.\n");
        }
        else {
            putsUart0("Usage: mode at|shell\n");
        }
        putcUart0('>');
        return;
    }
    if (uartMode == AT_MODE) {
        //process raw AT commands
        sendATCommand(line);
    }
    else if (uartMode == SHELL_MODE) {
        //process commands for MCU
        if (isCommand(&data, "sms", 2)) {
            valid = 1;
            putsUart0("sms: not implemented\n");
        }
        if (isCommand(&data, "status", 0)) {
            char num[11];
            valid = 1;
            putsUart0("uptime s: ");
            to_string(getUptimeSeconds(), num, 10);
            putsUart0(num);
            putsUart0("\nmodem: ");
            putsUart0(getSim7600StateName());
            putsUart0(", resets: ");
            to_string(getSim7600ResetCount(), num, 10);
            putsUart0(num);
            putsUart0("\nmqtt: ");
            putsUart0(getMqttStateName());
            putsUart0("\nuart1 rx errors: ");
            to_string(getUart1RxErrorCount(), num, 10);
            putsUart0(num);
            putsUart0("\n");
        }
        if (isCommand(&data, "preempt", 1)) {
            valid = 1;
            putsUart0("Preempt called\n");
        }
        if (!valid) {
            putsUart0("Invalid command\n");
        }
    }
    putcUart0('>');
}

void initShell() {
    initUart0();
    setUart0BaudRate(115200, 40e6);
    setUart0Callback(processShellCmd);
    enableUart0RxInt();
    enableNvicInterrupt(INT_UART0);
    printShell("Initializing UART0\n");
}

void printShell(const char* str) {
    putsUart0("[TM4C]: ");
    putsUart0(str);
}

/*void readLine(shell_cmd* data) { //blocks until return or max chars detected
    int count = 0;
    //while (1) {
    if (kbhitUart0()) {
        char c = getcUart0();
        if ((c == 8 || c == 127) & (count > 0)) {
            count--;
        }
        else if (c == 13) {
            data->buffer[count] = '\0';
            return;
        }
        else if (c >= 32) {
            data->buffer[count] = c;
            count++;
        }
        if (count == MAX_CHARS) {
            data->buffer[count] = '\0';
            return;
        }
    }
    else {
        //yield();
    }
    //}
}*/

void parseFields(shell_cmd* data) {
    int hasCounted = 0;
    int i;
    data->fieldCount = 0;
    for (i = 0; data->buffer[i] != '\0'; i++) {
        char c = data->buffer[i];
        if ((c >= 48 && c <= 57) || c == 95) {
            //is number OR '_'
            if (!hasCounted) {
                hasCounted = 1;
                data->fieldType[data->fieldCount] = 'n';
                data->fieldPosition[data->fieldCount] = i;
                data->fieldCount++;
            }
        }
        else if ((c >= 65 && c <= 90) || (c >= 97 && c <= 122)) {
            //is alphabet
            if (!hasCounted) {
                hasCounted = 1;
                data->fieldType[data->fieldCount] = 'a';
                data->fieldPosition[data->fieldCount] = i;
                data->fieldCount++;
            }
        }
        else {
            // is delimiter
            hasCounted = 0;
            data->buffer[i] = 0;
        }
        if (data->fieldCount == MAX_FIELDS) {
            return;
        }
    }
    return;
}

char* getFieldString(shell_cmd* data, uint8_t fieldNumber) {
    if (fieldNumber < data->fieldCount) {
        return &data->buffer[data->fieldPosition[fieldNumber]];
    }
    else {
        return NULL;
    }
}

// returns 0 if the field does not exist or is not numeric
int32_t getFieldInteger(shell_cmd* data, uint8_t fieldNumber) {
    if (fieldNumber < data->fieldCount && data->fieldType[fieldNumber] == 'n') {
        return to_uint32((const char*)&(data->buffer[data->fieldPosition[fieldNumber]]), 10);
    }
    return 0;
}

// hex digits a-f parse as alpha fields, so accept either field type here
uint32_t getFieldHexInteger(shell_cmd* data, uint8_t fieldNumber) {
    if (fieldNumber < data->fieldCount) {
        return to_uint32((const char*)&(data->buffer[data->fieldPosition[fieldNumber]]), 16);
    }
    return 0;
}

bool isCommand(shell_cmd* data, const char strCommand[], uint8_t minArguments) {
    int i;
    int arg1_len = 0;;
    for (; data->buffer[arg1_len] != ' ' && data->buffer[arg1_len] != 0; arg1_len++);
    if (str_length(strCommand) != arg1_len) {
        return false;
    }
    for (i = 0; data->buffer[i] != 0 && strCommand[i] != 0; i++) {
        if (data->buffer[i] != strCommand[i]) {
            return false;
        }
    }
    if (data->fieldCount-1 >= minArguments) {
        return true;
    }
    return false;
}

/*void processShell() {
    shell_cmd data;
    putsUart0(">");
    while (1) {
        if (kbhitUart0()) {
            readLine(&data);
            bool valid = false;
            parseFields(&data);
            // Command evaluation
            if (isCommand(&data, "reboot", 0)) { //reboot
                valid = true;
                reboot();
            }
            if (isCommand(&data, "ps", 0)) {
                valid = true;
            }
            if (isCommand(&data, "preempt", 1)) {
                valid = true;
            }
            if (!valid) {
                putsUart0("Invalid command\n");
            }
            putsUart0(">");
        }
        else {

        }
    }
}*/
