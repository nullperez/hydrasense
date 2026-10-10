#ifndef SIM7600_H_
#define SIM7600_H_

#include <stdint.h>
#include <stdbool.h>

void initSim7600();
void sim7600Handler();
void sim7600RequestReset();
void sendATCommand(const char* cmd);
void processATResponse(char* resp);
const char* getSim7600StateName();
uint32_t getSim7600ResetCount();

#endif
