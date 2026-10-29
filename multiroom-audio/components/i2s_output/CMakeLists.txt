idf_component_register(SRCS "i2s_output.c"
                       INCLUDE_DIRS "."
                       REQUIRES ring_buffer
                       PRIV_REQUIRES esp_driver_i2s)
