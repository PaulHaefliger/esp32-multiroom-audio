#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


typedef struct {
    uint8_t *buf;
    size_t capacity;

    volatile size_t write_idx;   /* written only by the producer */
    volatile size_t read_idx;    /* written only by the consumer */
} ring_buffer_t;

size_t rb_available_read(const ring_buffer_t *rb);

size_t rb_available_write(const ring_buffer_t *rb);

void rb_init(ring_buffer_t *rb, uint8_t *backing, size_t capacity);

/* Non-blocking. Both return the number of bytes actually copied. */
size_t rb_write(ring_buffer_t *rb, const uint8_t *src, size_t len);

size_t rb_read(ring_buffer_t *rb, uint8_t *dst, size_t len);

void rb_clear(ring_buffer_t *rb);

#endif
