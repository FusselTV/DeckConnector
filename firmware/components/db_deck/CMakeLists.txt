idf_component_register(SRCS db_deck.c usb_port.c jpeg_port.c INCLUDE_DIRS include
                       REQUIRES db_core esp_common
                       PRIV_REQUIRES db_log db_ota esp_driver_jpeg esp_timer)
