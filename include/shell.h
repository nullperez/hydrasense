#ifndef SHELL_H_
#define SHELL_H_

#include <stdint.h>
#include <stdbool.h>

#define MAX_OUT 63              // longest line UART0 delivers (buffer size - 1)
#define MAX_FIELDS      5

typedef struct _shell_cmd {
    char    buffer[MAX_OUT+1];
    uint8_t fieldCount;
    uint8_t fieldPosition[MAX_FIELDS];
    char    fieldType[MAX_FIELDS];
} shell_cmd;

void initShell();
void parseFields(shell_cmd* data);
char* getFieldString(shell_cmd* data, uint8_t fieldNumber);
int32_t getFieldInteger(shell_cmd* data, uint8_t fieldNumber);
uint32_t getFieldHexInteger(shell_cmd* data, uint8_t fieldNumber);
bool isCommand(shell_cmd* data, const char strCommand[], uint8_t minArguments);
void processShellCmd(char* line);
void printShell(const char* str);

#endif
