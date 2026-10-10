/*
 * sim7600.h
 *
 * Portable SIM7600 modem driver. It has no hardware dependencies: the board
 * supplies UART output, the reset pin, a seconds clock and logging through
 * sim7600_config. It brings the modem up (echo off, SIM ready, network
 * registered), watches for failures and reboots, and resets the modem when
 * it stops responding. Every wait has a timeout and nothing blocks.
 *
 * Protocol modules (MQTT, SMS, HTTP, ...) register as clients. They receive
 * modem lines once the modem is ready and send through sim7600SendCommand().
 *
 * Board integration:
 *   1. Deliver every line received from the modem to sim7600FeedLine() from
 *      the main loop (not from an ISR), without the CR/LF terminator.
 *      The data prompt '>' has no terminator: the UART layer must deliver it
 *      as its own line when it is the first char of a line.
 *   2. Call sim7600Handler() from the main loop.
 */

#ifndef SIM7600_H_
#define SIM7600_H_

#include <stdint.h>
#include <stdbool.h>

#define SIM7600_MAX_CLIENTS 4

typedef struct {
    void     (*uartWrite)(const char* s);    // required: write a string to the modem UART
    uint32_t (*getSeconds)(void);            // required: monotonic seconds counter
    void     (*setResetPin)(bool level);     // optional: drives the modem reset pin, NULL = AT+CRESET only
    void     (*log)(const char* msg);        // optional: one message per call, no newline. NULL = silent
} sim7600_config;

typedef struct {
    void (*onReady)(void);                   // modem is registered on the network
    void (*onLine)(const char* line);        // every line from the modem while ready
    void (*onReset)(void);                   // modem is resetting or rebooted, drop all state
    void (*poll)(void);                      // called from sim7600Handler() while ready
} sim7600_client;                            // any member may be NULL

bool sim7600Init(const sim7600_config* cfg); // starts with a modem reset
bool sim7600RegisterClient(const sim7600_client* client); // client must stay valid (static)
void sim7600FeedLine(char* line);
void sim7600Handler(void);

void sim7600SendCommand(const char* cmd);    // sends cmd + CR
void sim7600SendSecretCommand(const char* cmd); // same, but not logged (credentials)
void sim7600SendRaw(const char* data);       // sends data as-is (after a '>' prompt)
bool sim7600IsReady(void);
void sim7600RequestReset(void);              // safe to call from a client callback

// Only one client may run a command sequence at a time, since final results
// (OK/ERROR) carry no owner. Acquire before sending, release once done.
bool sim7600Acquire(const void* owner);      // true if free or already held by owner
void sim7600Release(const void* owner);

uint32_t sim7600Seconds(void);
void sim7600Log(const char* fmt, ...);       // printf-style, through cfg.log
const char* sim7600StateName(void);
uint32_t sim7600ResetCount(void);

#endif
