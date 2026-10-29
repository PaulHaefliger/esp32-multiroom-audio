idf_component_register(SRCS "satellite-audio.c"
                       INCLUDE_DIRS "."
                       REQUIRES ring_buffer i2s_output wifi_ap udp_rx)
