#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_coexist.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "ring_buffer.h"
#include "bt_sink.h"
#include "wifi_sta.h"
#include "udp_tx.h"

#define TEST_TONE 0

#define RB_SIZE  32768

static ring_buffer_t rb;
static uint8_t rb_storage[RB_SIZE];

#if TEST_TONE
#define FRAMES_PER_TICK 441

static void tone_producer(void *arg)
{
    static int16_t buf[FRAMES_PER_TICK * 2];
    float phase = 0.0f;
    const float step = 2.0f * (float)M_PI * 440.0f / 44100.0f;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        for (int i = 0; i < FRAMES_PER_TICK; i++) {
            int16_t s = (int16_t)(0.2f * INT16_MAX * sinf(phase));
            buf[2 * i] = s;
            buf[2 * i + 1] = s;
            phase += step;
            if (phase >= 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        if (rb_available_write(&rb) >= sizeof(buf)) {   /* whole chunks only */
            rb_write(&rb, (uint8_t *)buf, sizeof(buf));
        }
        vTaskDelayUntil(&last, 1);
    }
}
#endif

void app_main(void)
{
    rb_init(&rb, rb_storage, sizeof(rb_storage));

#if TEST_TONE
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_sta_start();
    udp_tx_start(&rb);
    xTaskCreate(tone_producer, "tone", 4096, NULL, 5, NULL);
#else
    bt_sink_start(&rb);
    wifi_sta_start();

    esp_coex_preference_set(ESP_COEX_PREFER_WIFI);

    udp_tx_start(&rb);
#endif

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        udp_tx_stats_t st;
        udp_tx_get_stats(&st);
        size_t fill = rb_available_read(&rb);
        ESP_LOGI("central", "sent=%lu enomem=%lu gave_up=%lu rb=%u%% bt_drop=%luB heap=%lu heap_min=%lu",
                 (unsigned long)st.sent, (unsigned long)st.enomem,
                 (unsigned long)st.gave_up, (unsigned)(fill * 100 / RB_SIZE),
                 (unsigned long)bt_sink_dropped_bytes(),
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
    }
}
