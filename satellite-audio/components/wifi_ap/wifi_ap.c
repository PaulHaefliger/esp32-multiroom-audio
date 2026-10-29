#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "audio_proto.h"
#include "wifi_ap.h"

/* The satellite (no Bluetooth) hosts the network, which avoids the unstable
   SoftAP + Bluetooth coexistence case on central. */

static const char *TAG = "wifi_ap";

static void wifi_ap_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
    switch (event_id) {
    case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *e =
            (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "Client joined: " MACSTR ", AID=%d",
                 MAC2STR(e->mac), e->aid);
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *e =
            (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "Client left: " MACSTR ", AID=%d",
                 MAC2STR(e->mac), e->aid);
        break;
    }
    default:
        break;
    }
}

void wifi_ap_start(void)
{
    /* No bt_sink here, so NVS (needed by the WiFi driver) is set up here. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_ap_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = NET_SSID,
            .ssid_len = strlen(NET_SSID),
            .channel = NET_CHANNEL,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* 20 MHz: half the spectrum of 40 MHz, fewer collisions with central's Bluetooth. */
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20));

    ESP_LOGI(TAG, "SoftAP started, SSID: %s (open), channel %d", NET_SSID, NET_CHANNEL);
}
