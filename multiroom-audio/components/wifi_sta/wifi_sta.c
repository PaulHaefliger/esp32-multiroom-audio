#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_proto.h"
#include "wifi_sta.h"

/* Central joins the satellite's network as a station: Espressif rates
   station + Classic Bluetooth as stable, but SoftAP + Bluetooth as unstable. */

#define GOT_IP_BIT  BIT0

static const char *TAG = "wifi_sta";
static EventGroupHandle_t s_evt;

/* Connect on start, reconnect on drop, signal once an IP is assigned. */
static void wifi_sta_event_handler(void *arg, esp_event_base_t base,
                                   int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "disconnected (reason %d), retrying", e->reason);
        xEventGroupClearBits(s_evt, GOT_IP_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_evt, GOT_IP_BIT);
    }
}

void wifi_sta_start(void)
{
    /* Harmless if bt_sink already initialized NVS. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_evt = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_sta_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_sta_event_handler, NULL));

    wifi_config_t cfg = {
        .sta = {
            .ssid = NET_SSID,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* 20 MHz uses half the spectrum of 40 MHz: fewer collisions with Bluetooth. */
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20));

/* Power save adds receive latency, but the driver requires it when Bluetooth
   is on. Central mostly transmits, so it doesn't hurt here. */
#if !CONFIG_BT_ENABLED
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
#endif

    ESP_LOGI(TAG, "joining %s ...", NET_SSID);
    /* Block until we have an IP, so sockets can be opened right after. */
    xEventGroupWaitBits(s_evt, GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}
