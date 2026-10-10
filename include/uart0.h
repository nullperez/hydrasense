// UART0 Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL
// Target uC:       TM4C123GH6PM
// System Clock:    -

// Hardware configuration:
// UART Interface:
//   U0TX (PA1) and U0RX (PA0) are connected to the 2nd controller
//   The USB on the 2nd controller enumerates to an ICDI interface and a virtual COM port

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef UART0_H_
#define UART0_H_

#include <stdint.h>
#include <stdbool.h>

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------
#ifndef UART_CALLBACK_T
#define UART_CALLBACK_T
typedef void (*uart_callback)(char* str);
#endif
//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void enableUart0RxInt(void);
void disableUart0RxInt(void);
void setUart0Callback(uart_callback fn);
void initUart0(void);
void setUart0BaudRate(uint32_t baudRate, uint32_t fcyc);
void putcUart0(char c);
void putsUart0(const char* str);
void putcUart0_int(char c);
void putsUart0_int(const char* str);
char getcUart0(void);
bool isUart0RxFifoEmpty(void);
void uart0Isr();
void processUart0Lines(void);
uint32_t getUart0RxErrorCount(void);


#endif
