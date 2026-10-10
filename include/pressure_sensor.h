// Pressure Sensor Library
// Jennifer Hernandez

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL
// Target uC:       TM4C123GH6PM
// System Clock:    -

// Hardware configuration:
// 16 MHz external crystal oscillator

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef PRESSURE_H_ //CLOCK_H will only be defined once
#define PRESSURE_H_

#define PRESSURE PORTE, 3      // AIN0, pressure sensor input
#define PRESSURE_ADC_ERROR 0xFFFFFFFF

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void initPressure();
uint32_t getPressureRaw();
float getPressurePSI();

#endif
