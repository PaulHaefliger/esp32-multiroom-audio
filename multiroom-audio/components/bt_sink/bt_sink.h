#pragma once
#include <stdint.h>
#include "ring_buffer.h"

/* Starts the Bluetooth A2DP sink ("ESP32-Multiroom"); decoded PCM goes into rb.
   Also initializes NVS, so call it first. */
void bt_sink_start(ring_buffer_t *rb);

/* Total PCM bytes dropped because rb was full. Diagnostic. */
uint32_t bt_sink_dropped_bytes(void);
