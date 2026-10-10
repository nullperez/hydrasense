// Watchdog Library
//
// WDT0 resets the MCU if feedWatchdog() is not called for 2 x seconds
// (the first timeout raises the interrupt flag, the second one resets).
// This also recovers from the infinite loops in the startup fault handlers.

#include <stdint.h>
#include "tm4c123gh6pm.h"
#include "watchdog.h"

void initWatchdog(uint32_t seconds, uint32_t fcyc) {
    SYSCTL_RCGCWD_R |= SYSCTL_RCGCWD_R0;
    _delay_cycles(3);
    WATCHDOG0_LOCK_R = WDT_LOCK_UNLOCK;
    WATCHDOG0_LOAD_R = seconds * fcyc;                  // must fit in 32 bits (max ~107 s at 40 MHz)
    WATCHDOG0_TEST_R |= WDT_TEST_STALL;                 // pause while halted in the debugger
    WATCHDOG0_CTL_R = WDT_CTL_RESEN | WDT_CTL_INTEN;    // enable reset, start counting
    WATCHDOG0_LOCK_R = WDT_LOCK_LOCKED;                 // protect against stray writes
}

void feedWatchdog(void) {
    WATCHDOG0_LOCK_R = WDT_LOCK_UNLOCK;
    WATCHDOG0_ICR_R = 0;                                // any write reloads the counter
    WATCHDOG0_LOCK_R = WDT_LOCK_LOCKED;
}
