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

#include <stdint.h>
#include "clock.h"
#include "gpio.h"
#include "pressure_sensor.h"
#include "tm4c123gh6pm.h"

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void initPressure()
{
    enablePort(PORTE);
    SYSCTL_RCGCADC_R |= SYSCTL_RCGCADC_R0;
    _delay_cycles(3);

    // Configure the analog input pin
    selectPinAnalogInput(PRESSURE);  // Ensure PRESS is mapped to the correct AINx pin

    // ADC0 Configuration for Single Sample Mode
    ADC0_ACTSS_R &= ~ADC_ACTSS_ASEN1;  // Disable SS1 while configuring
    ADC0_SAC_R = ADC_SAC_AVG_16X;      // 16x hardware averaging to reduce noise
    ADC0_EMUX_R = (ADC0_EMUX_R & ~ADC_EMUX_EM1_M) | ADC_EMUX_EM1_PROCESSOR; // Software trigger

    ADC0_SSMUX1_R = 0;  // Select AIN0 (Adjust this if your AINx is different)
    ADC0_SSCTL1_R = ADC_SSCTL1_END0 | ADC_SSCTL1_IE0; // Mark sample as end and enable interrupt flag

    ADC0_ACTSS_R |= ADC_ACTSS_ASEN1;  // Enable SS1
}

// returns the 12-bit ADC reading, or PRESSURE_ADC_ERROR if the conversion never completed
uint32_t getPressureRaw()
{
    uint32_t timeout = 100000;
    uint32_t data;

    // Start ADC Conversion
    ADC0_PSSI_R |= ADC_PSSI_SS1;

    // Wait for conversion to complete (a few us normally), but never hang
    while ((ADC0_RIS_R & ADC_RIS_INR1) == 0) {
        if (--timeout == 0) {
            return PRESSURE_ADC_ERROR;
        }
    }

    // Read 12-bit ADC result from correct FIFO
    data = ADC0_SSFIFO1_R & ADC_SSFIFO1_DATA_M;

    // Clear interrupt flag
    ADC0_ISC_R = ADC_ISC_IN1;
    return data;
}

// returns pressure clamped to 0-30 PSI, or a negative value if the ADC failed
float getPressurePSI()
{
    uint32_t data = getPressureRaw();
    float volt, psi;
    if (data == PRESSURE_ADC_ERROR) {
        return -1.0f;
    }
    volt = (data * 3.3f) / 4095;
    psi = ((volt - 0.365f) / (3.3f - 0.365f)) * 30;
    if (psi < 0) {
        psi = 0;    // below the sensor's 0 PSI offset (noise or disconnected)
    }
    else if (psi > 30) {
        psi = 30;
    }
    return psi;
}
