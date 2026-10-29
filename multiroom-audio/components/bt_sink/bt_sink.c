#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "ring_buffer.h"
#include "bt_sink.h"
#include "i2s_output.h"
#include "esp_avrc_api.h"

static const char *TAG = "bt_sink";
static const char local_device_name[] = "ESP32-Multiroom";

static ring_buffer_t *rb;

static uint8_t s_avrc_volume = 127;
static uint32_t s_dropped_bytes = 0;

static void a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG, "A2DP connection state: %d", param->conn_stat.state);
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "A2DP audio state: %d", param->audio_stat.state);
        break;
    case ESP_A2D_AUDIO_CFG_EVT: {
        esp_a2d_mcc_t *mcc = &param->audio_cfg.mcc;
        if (mcc->type == ESP_A2D_MCT_SBC) {
            int sample_rate = 16000;
            if      (mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_16K) sample_rate = 16000;
            else if (mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_32K) sample_rate = 32000;
            else if (mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_44K) sample_rate = 44100;
            else if (mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_48K) sample_rate = 48000;
            ESP_LOGI(TAG, "Negotiated sample rate: %d", sample_rate);
        }
        break;
    }
    default:
        break;
    }
}

static void a2d_data_cb(const uint8_t *data, uint32_t len)
{
    static uint32_t count = 0;
    if (count++ % 200 == 0) {
        ESP_LOGI(TAG, "PCM len=%lu", len);
    }
    if (rb_available_write(rb) < len) {
        s_dropped_bytes += len;
        return;
    }
    rb_write(rb, data, len);
}

uint32_t bt_sink_dropped_bytes(void)
{
    return s_dropped_bytes;
}

void bt_av_hdl_avrc_tg_evt(esp_avrc_tg_cb_event_t event,
                           esp_avrc_tg_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
        break;

    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT: {
        uint8_t vol = param->set_abs_vol.volume;
        s_avrc_volume = vol;
        i2s_output_set_volume(vol);
        ESP_LOGI(TAG, "Absolute volume set: %d", vol);
        break;
    }

    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
        if (param->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
            esp_avrc_rn_param_t rn = { .volume = s_avrc_volume };
            esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE,
                                    ESP_AVRC_RN_RSP_INTERIM, &rn);
        }
        break;

    default:
        break;
    }
}

void bt_sink_start(ring_buffer_t *buffer)
{
    rb = buffer;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_avrc_tg_init();
    esp_avrc_tg_register_callback(bt_av_hdl_avrc_tg_evt);

    esp_avrc_rn_evt_cap_mask_t evt_set = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET,
                                        &evt_set,
                                        ESP_AVRC_RN_VOLUME_CHANGE);
    esp_avrc_tg_set_rn_evt_cap(&evt_set); 

    esp_bt_gap_register_callback(gap_cb);
    esp_a2d_register_callback(a2d_cb);
    esp_a2d_sink_register_data_callback(a2d_data_cb);
    esp_a2d_sink_init(); 

    esp_bt_gap_set_device_name(local_device_name);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    ESP_LOGI(TAG, "BT sink started, discoverable as \"%s\"", local_device_name);
}