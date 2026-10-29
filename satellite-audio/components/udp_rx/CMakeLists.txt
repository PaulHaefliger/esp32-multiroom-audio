idf_component_register(SRCS "udp_rx.c"
                       INCLUDE_DIRS "."
                       REQUIRES ring_buffer
                       PRIV_REQUIRES audio_proto i2s_output lwip)
