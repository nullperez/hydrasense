#ifndef WATCHDOG_H_
#define WATCHDOG_H_

#include <stdint.h>

void initWatchdog(uint32_t seconds, uint32_t fcyc);
void feedWatchdog(void);

#endif
