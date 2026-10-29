#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ring_buffer.h"
#include "i2s_output.h"
#include "wifi_ap.h"
#include "udp_rx.h"

#define RB_SIZE        65536
#define PREFILL_BYTES  (RB_SIZE / 2)

static const char *TAG = "satellite";

static ring_buffer_t rb;
static uint8_t rb_storage[RB_SIZE];

void app_main(void)
{
    rb_init(&rb, rb_storage, sizeof(rb_storage));

    wifi_ap_start();
    udp_rx_start(&rb);

    while (rb_available_read(&rb) < PREFILL_BYTES) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "prefilled %d bytes, starting playback", PREFILL_BYTES);
    i2s_output_start(&rb);

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
