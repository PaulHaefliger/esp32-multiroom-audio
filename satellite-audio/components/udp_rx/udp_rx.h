#pragma once

#include <stdint.h>
#include "ring_buffer.h"

typedef struct {
    uint32_t packets;         /* valid audio packets received */
    uint32_t lost;            /* gaps in the sequence numbers */
    uint32_t bad;             /* wrong magic/length, or late/duplicate */
    uint32_t overflow_bytes;  /* PCM dropped because rb was full */
    uint32_t trimmed;         /* packets dropped by the latency clamp */
} udp_rx_stats_t;

/* Broadcasts hellos and writes received PCM into rb. Call after wifi_ap_start(). */
void udp_rx_start(ring_buffer_t *rb);

void udp_rx_get_stats(udp_rx_stats_t *out);
