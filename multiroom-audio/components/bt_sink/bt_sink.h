#pragma once
#include <stdint.h>
#include "ring_buffer.h"

void bt_sink_start(ring_buffer_t *rb);

uint32_t bt_sink_dropped_bytes(void);
