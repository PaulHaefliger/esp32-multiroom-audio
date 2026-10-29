#pragma once

#include <stdint.h>
#include "ring_buffer.h"

typedef struct {
    uint32_t sent;      /* packets handed to the WiFi driver */
    uint32_t enomem;    /* TX-queue-full hits (each costs a 10 ms retry) */
    uint32_t gave_up;   /* packets dropped after 5 failed tries */
} udp_tx_stats_t;

/* Sends audio from rb to whichever satellite last sent a hello.
   Call after wifi_sta_start(). */
void udp_tx_start(ring_buffer_t *rb);

void udp_tx_get_stats(udp_tx_stats_t *out);
