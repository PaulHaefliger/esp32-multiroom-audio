#pragma once

#include <stdint.h>
#include "ring_buffer.h"

typedef struct {
    uint32_t packets;
    uint32_t lost;
    uint32_t bad;
    uint32_t overflow_bytes;
    uint32_t trimmed;
} udp_rx_stats_t;

void udp_rx_start(ring_buffer_t *rb);

void udp_rx_get_stats(udp_rx_stats_t *out);
