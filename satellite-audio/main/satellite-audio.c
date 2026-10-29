#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ring_buffer.h"
#include "i2s_output.h"
#include "wifi_ap.h"
#include "udp_rx.h"

/* Deep buffer to absorb bursty delivery. 44.1 kHz x 4 B = 176.4 kB/s. */
#define RB_SIZE        65536          /* ~370 ms */
#define PREFILL_BYTES  (RB_SIZE / 2)  /* ~185 ms buffered before playback starts */

static const char *TAG = "satellite";

static ring_buffer_t rb;
static uint8_t rb_storage[RB_SIZE];

void app_main(void)
{
    rb_init(&rb, rb_storage, sizeof(rb_storage));

    wifi_ap_start();       /* host the network central joins */
    udp_rx_start(&rb);     /* producer: UDP -> rb */

    /* Wait for headroom before playing, or the first WiFi hiccup underruns. */
    while (rb_available_read(&rb) < PREFILL_BYTES) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "prefilled %d bytes, starting playback", PREFILL_BYTES);
    i2s_output_start(&rb); /* consumer: rb -> I2S */

    /* Health log every 2 s. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        udp_rx_stats_t st;
        udp_rx_get_stats(&st);
        ESP_LOGI(TAG, "rx=%lu lost=%lu bad=%lu trimmed=%lu overflow=%luB",
                 (unsigned long)st.packets, (unsigned long)st.lost,
                 (unsigned long)st.bad, (unsigned long)st.trimmed,
                 (unsigned long)st.overflow_bytes);
    }
}
