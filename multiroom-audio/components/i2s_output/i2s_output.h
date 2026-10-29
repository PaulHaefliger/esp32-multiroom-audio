#pragma once

#include "ring_buffer.h"

void i2s_output_start(ring_buffer_t *rb);

void i2s_output_set_rate(uint32_t sample_rate);

void i2s_output_set_volume(uint8_t vol);

uint8_t i2s_output_get_volume(void);