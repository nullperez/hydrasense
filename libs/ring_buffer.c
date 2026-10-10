#include "ring_buffer.h"
#include <stdint.h>
#include <stdbool.h>

//returns false (and drops the byte) if the buffer is full
bool ring_buffer_put(ring_buffer* rb, uint8_t data) {
    if (ring_buffer_full(rb)) {
        return false;
    }
    rb->buffer[rb->head] = data;
    rb->head++;
    if (rb->head == rb->size) {
        rb->head = 0;
    }
    return true;
}

//caller must check ring_buffer_empty() first
uint8_t ring_buffer_get(ring_buffer* rb) {
    const uint8_t data = rb->buffer[rb->tail];
    rb->tail++;

    if (rb->tail == rb->size) {
        rb->tail = 0;
    }
    return data;
}

uint8_t ring_buffer_peek(const ring_buffer* rb) {
    return rb->buffer[rb->tail];
}

bool ring_buffer_empty(const ring_buffer* rb) {
    return rb->head == rb->tail;
}

bool ring_buffer_full(const ring_buffer* rb) {
    uint8_t i_after_h = rb->head + 1;
    if (i_after_h == rb->size) {
        i_after_h = 0;
    }
    return i_after_h == rb->tail;
}
