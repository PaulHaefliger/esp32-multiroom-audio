idf_component_register(SRCS "bt_sink.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt ring_buffer nvs_flash i2s_output)