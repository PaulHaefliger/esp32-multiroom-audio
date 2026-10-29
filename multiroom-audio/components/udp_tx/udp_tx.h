#pragma once

#include <stdint.h>
#include "ring_buffer.h"

typedef struct {
    uint32_t sent;
    uint32_t enomem;
    uint32_t gave_up;
} udp_tx_stats_t;

void udp_tx_start(ring_buffer_t *rb);

void udp_tx_get_stats(udp_tx_stats_t *out);
