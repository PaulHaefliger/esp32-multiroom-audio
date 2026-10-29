#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include <stdint.h>
#include "freertos/task.h"
#include <string.h>
#include <stdbool.h>
#include "ring_buffer.h"
#include "i2s_output.h"

/* MAX98357A wiring. No MCLK needed: the amp runs off BCLK. */
#define I2S_BCLK_GPIO    GPIO_NUM_26
#define I2S_LRCLK_GPIO   GPIO_NUM_25
#define I2S_DOUT_GPIO    GPIO_NUM_27

static i2s_chan_handle_t tx_chan;
static ring_buffer_t *rb;
#define CHUNK_BYTES 1024   /* bytes per I2S write (256 stereo frames, ~5.8 ms) */

/* Volume as Q15 gain (32767 = 1.0). Written by the volume setter, read by the
   feed task: a lone scalar, so a relaxed atomic is enough. */
static uint16_t s_gain_q15 = 32767;

/* Raw AVRCP volume, kept so central can forward it in packet headers. */
static uint8_t s_vol = 127;

/* Diagnostic: logs ring buffer fill once per second. */
static void rb_monitor_task(void *arg) {
    uint32_t secs = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        size_t used = rb_available_read(rb);
        size_t cap  = rb->capacity;
        ESP_LOGI("rb_mon", "t=%lus  fill=%u/%u bytes (%u%%)",
                 secs++, (unsigned)used, (unsigned)cap,
                 cap ? (unsigned)(used * 100 / cap) : 0);
    }
}

/* Consumer: ring buffer -> volume -> I2S. i2s_channel_write blocks until DMA
   has room, so the I2S clock paces this loop. */
static void feed_task(void *arg) {
    uint8_t scratch[CHUNK_BYTES] __attribute__((aligned(4)));
    size_t n;
    size_t written;
    bool refilling = true;   /* start (and restart after underrun) with silence */

    for (;;) {
        /* After an underrun, play silence until the buffer is back to 50%,
           instead of limping along near empty. */
        if (refilling) {
            if (rb_available_read(rb) >= rb->capacity / 2) {
                refilling = false;
            } else {
                memset(scratch, 0, CHUNK_BYTES);
                i2s_channel_write(tx_chan, scratch, CHUNK_BYTES, &written, portMAX_DELAY);
                continue;
            }
        }

        n = rb_read(rb, scratch, CHUNK_BYTES);
        if (n < CHUNK_BYTES) {   /* underrun: pad with silence */
            memset(scratch + n, 0, CHUNK_BYTES - n);
            refilling = true;
        }
        /* Gain loaded once per chunk; gain <= 1.0, so the product never overflows. */
        uint16_t gain = __atomic_load_n(&s_gain_q15, __ATOMIC_RELAXED);
        int16_t *samples = (int16_t *)scratch;
        for (size_t i = 0; i < CHUNK_BYTES / 2; i++) {
            samples[i] = (int16_t)(((int32_t)samples[i] * gain) >> 15);
        }

        i2s_channel_write(tx_chan, scratch, CHUNK_BYTES, &written, portMAX_DELAY);
    }
}

/* AVRCP volume 0..127 -> Q15 gain. */
void i2s_output_set_volume(uint8_t vol) {
    if (vol > 127) vol = 127;
    uint16_t g = (uint16_t)(((uint32_t)vol * 32767u) / 127u);
    __atomic_store_n(&s_gain_q15, g, __ATOMIC_RELAXED);
    __atomic_store_n(&s_vol, vol, __ATOMIC_RELAXED);
}

uint8_t i2s_output_get_volume(void) {
    return __atomic_load_n(&s_vol, __ATOMIC_RELAXED);
}

/* Configures I2S (44.1 kHz, 16-bit stereo, Philips format) and starts the feed task. */
void i2s_output_start(ring_buffer_t *buffer) {
    rb = buffer;
    
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_GPIO,
            .ws   = I2S_LRCLK_GPIO,
            .dout = I2S_DOUT_GPIO,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    i2s_channel_init_std_mode(tx_chan, &std_cfg);
    i2s_channel_enable(tx_chan);
    xTaskCreate(feed_task, "feed", 4096, NULL, 5, NULL);
    xTaskCreate(rb_monitor_task, "rb_mon", 2048, NULL, 3, NULL);
}

/* Unused. Racy if called while the feed task is writing. */
void i2s_output_set_rate(uint32_t sample_rate) {
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    i2s_channel_disable(tx_chan);
    i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
    i2s_channel_enable(tx_chan);
}
