idf_component_register(SRCS db_ui.c menu.c canvas.c font_data.c jpeg_port.c INCLUDE_DIRS include
                       REQUIRES esp_common
                       PRIV_REQUIRES db_core db_config db_deck db_link db_net esp_driver_jpeg esp_timer
                                     esp_app_format lwip)
