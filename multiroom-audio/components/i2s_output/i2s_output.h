#pragma once

#include "ring_buffer.h"

/* Plays audio from rb out of I2S. Only the satellite calls this; central only
   uses the volume functions. */
void i2s_output_start(ring_buffer_t *rb);

void i2s_output_set_rate(uint32_t sample_rate);

void i2s_output_set_volume(uint8_t vol);

uint8_t i2s_output_get_volume(void);
