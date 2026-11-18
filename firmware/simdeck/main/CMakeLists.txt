idf_component_register(SRCS ../../main/main.c
                       PRIV_REQUIRES db_audio db_board db_config db_core db_deck db_dock db_link db_log db_net db_ota
                                     db_serial db_ui db_web esp_app_format esp_timer nvs_flash sim_usb)
