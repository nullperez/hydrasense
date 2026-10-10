/******************************************************************************
 * File:        strlib.c
 *
 * Author:      Giancarlo Perez
 *
 * Created:     12/2/24
 *
 * Description: -
 ******************************************************************************/

//=============================================================================
// INCLUDES
//=============================================================================

#include "strlib.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*uint8_t asciiToUint8(const char str[]) {
    uint8_t data;
    if (str[0] == '0' && tolower(str[1]) == 'x')
        sscanf(str, "%hhx", &data);
    else
        sscanf(str, "%hhu", &data);
    return data;
}*/

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

char* str_char(const char* str, char c) {
    while (*str != 0) {
        if (*str == c) {
            return (char*)str;
        }
        str++;
    }
    return NULL;
}

//src MUST be null terminated, dest MUST be large enough. Prefer str_ncopy.
void str_copy(char dest[], const char* src) {
    uint32_t i;
    for (i = 0; src[i] != 0; i++) {
        dest[i] = src[i];
    }
    dest[i] = 0;
}

//copies at most size-1 chars and always null terminates. returns false if src was truncated
bool str_ncopy(char dest[], const char* src, uint32_t size) {
    uint32_t i;
    if (size == 0) {
        return false;
    }
    for (i = 0; src[i] != 0 && i < size - 1; i++) {
        dest[i] = src[i];
    }
    dest[i] = 0;
    return src[i] == 0;
}

bool str_starts_with(const char* str, const char* prefix) {
    while (*prefix != 0) {
        if (*str++ != *prefix++) {
            return false;
        }
    }
    return true;
}


bool str_equal(const char* str1, const char* str2) {
    int i;
    if (str_length(str1) != str_length(str2)) {
        return false;
    }
    for (i = 0; str1[i] != 0; i++) {
        if (str1[i] != str2[i]) {
            return false;
        }
    }
    return true;
}

uint32_t str_length(const char str[]) {
    int c;
    for (c = 0; str[c] != 0; c++);
    return c;
}

char* str_tokenize(char* str, const char* delim) {
    static char *last = NULL;
    char* start;
    if (str == NULL) {
        str = last;
    }
    if (str == NULL) {
        return NULL;
    }
    while (*str && str_char(delim, *str)) {
        str++;
    }
    if (*str == '\0') {
        return NULL;
    }
    start = str;
    while (*str && !str_char(delim, *str)) {
        str++;
    }
    if (*str) {
        *str = '\0';
        last = str + 1;
    }
    else {
        last = NULL;
    }
    return start;
}

void str_to_lower(char* str) {
    for (; *str != 0; str++) {
        *str = to_lower(*str);
    }
}

void str_to_upper(char* str) {
    for (; *str != 0; str++) {
        *str = to_upper(*str);
    }
}

char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

char to_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (c - 32) : c;
}

//out must hold at least 33 chars for base 2, 11 for base 10. bases outside 2-36 output ""
void to_string(uint32_t n, char* out, uint32_t b) {
    char num[32];
    uint32_t i = 0;
    uint32_t c;
    if (b < 2 || b > 36) {
        out[0] = 0;
        return;
    }
    do {
        uint32_t r = n % b;
        num[i++] = (r < 10) ? ('0' + r) : ('A' + r - 10);
        n /= b;
    } while (n > 0);
    for (c = 0; c < i; c++) {
        out[c] = num[i-c-1];
    }
    out[c] = 0;
}

//parses digits until the first invalid char for the base. accepts upper/lower case hex digits
uint32_t to_uint32(const char* str, uint32_t base) {
    uint32_t n = 0;
    for (; *str != 0; str++) {
        char c = to_upper(*str);
        uint32_t digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        }
        else if (c >= 'A' && c <= 'Z') {
            digit = c - 'A' + 10;
        }
        else {
            break;
        }
        if (digit >= base) {
            break;
        }
        n = n*base + digit;
    }
    return n;
}
