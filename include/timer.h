/******************************************************************************
 * File:        timer.h
 *
 * Author:      Giancarlo Perez
 *
 * Created:     12/7/24
 *
 * Description: -
 ******************************************************************************/

#ifndef TIMER_H_
#define TIMER_H_

//=============================================================================
// INCLUDES
//=============================================================================

#include <stdint.h>
#include <stdbool.h>

//=============================================================================
// DEFINES AND MACROS
//=============================================================================

#define NUM_TIMERS 25
#define INVALID_TIMER 255

//=============================================================================
// TYPEDEFS AND GLOBALS
//=============================================================================

typedef void (*_tim_callback_t)(void*);

//=============================================================================
// FUNCTION PROTOTYPES
//=============================================================================

void initTimer();
uint8_t startOneshotTimer(_tim_callback_t callback, uint32_t seconds, void* context);
uint8_t startPeriodicTimer(_tim_callback_t callback, uint32_t seconds, void* context);
bool stopTimer(uint8_t id);
bool restartTimer(uint8_t id);
void tickIsr();
uint32_t getUptimeSeconds();
void seed_rng();
uint32_t getTicks();
uint32_t rand32();

#endif
