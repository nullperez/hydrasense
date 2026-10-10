// Timer Service Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// Timer 4

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "tm4c123gh6pm.h"
#include "timer.h"

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------



typedef struct {
    _tim_callback_t callback;
    void* context;
    uint32_t exp_time;
    uint32_t rem_time;
    bool reload;
    bool active;
} timer_t;

timer_t timers[NUM_TIMERS];
static volatile uint32_t uptimeSeconds = 0;

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void initTimer() {
    uint8_t i;
    // Enable clocks
    SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R4;
    _delay_cycles(3);
    // Configure Timer 4 for 1 sec tick
    TIMER4_CTL_R &= ~TIMER_CTL_TAEN;                 // turn-off timer before reconfiguring
    TIMER4_CFG_R = TIMER_CFG_32_BIT_TIMER;           // configure as 32-bit timer (A+B)
    TIMER4_TAMR_R = TIMER_TAMR_TAMR_PERIOD;          // configure for periodic mode (count down)
    TIMER4_TAILR_R = 40000000 - 1;                   // set load value (1 Hz rate)
    for (i = 0; i < NUM_TIMERS; i++) {               // clear table before the ISR can run
        timers[i].callback = NULL;
        timers[i].context = NULL;
        timers[i].exp_time = 0;
        timers[i].rem_time = 0;
        timers[i].reload = false;
        timers[i].active = false;
    }
    TIMER4_CTL_R |= TIMER_CTL_TAEN;                  // turn-on timer
    TIMER4_IMR_R |= TIMER_IMR_TATOIM;                // turn-on interrupt
    NVIC_EN2_R |= 1 << (INT_TIMER4A-80);             // turn-on interrupt 86 (TIMER4A)
}

uint8_t startOneshotTimer(_tim_callback_t callback, uint32_t seconds, void* context) {
    uint8_t i = 0;
    if (callback == NULL || seconds == 0) {
        return INVALID_TIMER;
    }
    for (i = 0; i < NUM_TIMERS; i++) {
        if (!timers[i].active) {
            timers[i].exp_time = seconds;
            timers[i].rem_time = seconds;
            timers[i].callback = callback;
            timers[i].context = context;
            timers[i].reload = false;
            timers[i].active = true;
            return i;
        }
    }
    return INVALID_TIMER;
}

uint8_t startPeriodicTimer(_tim_callback_t callback, uint32_t seconds, void* context) {
    uint8_t i = 0;
    if (callback == NULL || seconds == 0) {
        return INVALID_TIMER;
    }
    for (i = 0; i < NUM_TIMERS; i++) {
        if (!timers[i].active) {
            timers[i].exp_time = seconds;
            timers[i].rem_time = seconds;
            timers[i].callback = callback;
            timers[i].context = context;
            timers[i].reload = true;
            timers[i].active = true;
            return i;
        }
    }
    return INVALID_TIMER;
}

//stops timer by ID, returns success status
bool stopTimer(uint8_t id) {
    if (id < NUM_TIMERS) {
        if (timers[id].active) {
            timers[id].rem_time = 0;
            timers[id].active = 0;
            return true;
        }
    }
    return false;
}

bool restartTimer(uint8_t id) {
    if (id < NUM_TIMERS) {
        if (timers[id].active) {
            timers[id].rem_time = timers[id].exp_time;
            return true;
        }
    }
    return false;
}

/*bool restartTimer(_callback callback) {
     uint8_t i = 0;
     bool found = false;
     while (i < NUM_TIMERS && !found) {
         found = fn[i] == callback;
         if (found) {
             ticks[i] = period[i];
         }
         i++;
     }
     return found;
}*/

// seconds since initTimer(). wraps after ~136 years, subtract timestamps to get elapsed time
uint32_t getUptimeSeconds() {
    return uptimeSeconds;
}

// 1 second ticks. callbacks run in interrupt context: keep them short (e.g. set a flag)
void tickIsr() {
    uint8_t i;
    uptimeSeconds++;
    for (i = 0; i < NUM_TIMERS; i++) {
        if (timers[i].active && timers[i].rem_time != 0) {
            timers[i].rem_time--;
            if (timers[i].rem_time == 0) {
                if (timers[i].reload) {
                    timers[i].rem_time = timers[i].exp_time;
                }
                else {
                    timers[i].active = false;    // free the slot for one-shot timers
                }
                if (timers[i].callback) {
                    (*timers[i].callback)(timers[i].context);
                }
            }
        }
    }
    TIMER4_ICR_R = TIMER_ICR_TATOCINT;
}

static uint32_t lcg_seed = 1;

void seed_rng() {
    uint32_t t = getTicks();
    lcg_seed = t;
}
// Placeholder random number function
uint32_t getTicks() {
    return TIMER4_TAV_R;
}

uint32_t rand32() {
    lcg_seed = (1103515245 * lcg_seed + 12345) & 0x7FFFFFFF;
    return lcg_seed;
}
