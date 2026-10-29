idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES ring_buffer bt_sink wifi_sta udp_tx nvs_flash esp_coex)
