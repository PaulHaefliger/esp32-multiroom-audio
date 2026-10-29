/* Lock-free single-producer / single-consumer ring buffer.
   Only the producer writes write_idx and only the consumer writes read_idx,
   so no locks are needed; fences order the data copies against the index updates. */
#include "ring_buffer.h"
#include <string.h>

size_t rb_available_read(const ring_buffer_t *rb)
{
    return (rb->write_idx - rb->read_idx + rb->capacity) % rb->capacity;
}
/* One slot stays empty, so "full" and "empty" are distinguishable. */
size_t rb_available_write(const ring_buffer_t *rb)
{
    return rb->capacity - rb_available_read(rb) - 1;
}
void rb_init(ring_buffer_t *rb, uint8_t *backing, size_t capacity)
{
    rb->buf = backing; rb->capacity = capacity;
    rb->write_idx = 0; rb->read_idx = 0;
}
size_t rb_write(ring_buffer_t *rb, const uint8_t *src, size_t len)
{
    size_t avail = rb_available_write(rb);   /* read once: a second read could differ */
    size_t n = (len < avail ? len : avail);
    size_t tail = rb->capacity - rb->write_idx;
    if (tail < n) {   /* wraps past the end: copy in two parts */
        memcpy(rb->buf + rb->write_idx, src, tail);
        memcpy(rb->buf, src + tail, n - tail);
        /* Data must be visible to the other core before the new index is. */
        __atomic_thread_fence(__ATOMIC_RELEASE);
        rb->write_idx = n - tail;
    } else {
        memcpy(rb->buf + rb->write_idx, src, n);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        rb->write_idx = (rb->write_idx + n) % rb->capacity;
    }
    return n;
}
size_t rb_read(ring_buffer_t *rb, uint8_t *dst, size_t len)
{
    size_t avail = rb_available_read(rb);
    /* Pairs with the release in rb_write: don't read data before seeing the index. */
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    size_t n = (len < avail ? len : avail);
    size_t tail = rb->capacity - rb->read_idx;
    if (tail < n) {
        memcpy(dst, rb->buf + rb->read_idx, tail);
        memcpy(dst + tail, rb->buf, n - tail);
        rb->read_idx = n - tail;
    } else {
        memcpy(dst, rb->buf + rb->read_idx, n);
        rb->read_idx = (rb->read_idx + n) % rb->capacity;
    }
    return n;
}
void rb_clear(ring_buffer_t *rb) { rb->read_idx = 0; rb->write_idx = 0; }
