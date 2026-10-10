#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>


#define RING_BUFFER_CAPACITY 64

typedef struct _ring_buffer {
    uint8_t buffer[RING_BUFFER_CAPACITY]; // size must be <= RING_BUFFER_CAPACITY
    uint8_t size;
    uint8_t head;
    uint8_t tail;
} ring_buffer;

bool ring_buffer_put(ring_buffer* rb, uint8_t data);
uint8_t ring_buffer_get(ring_buffer* rb);
uint8_t ring_buffer_peek(const ring_buffer* rb);
bool ring_buffer_empty(const ring_buffer* rb);
bool ring_buffer_full(const ring_buffer* rb);

#endif
