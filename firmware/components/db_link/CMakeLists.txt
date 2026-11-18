idf_component_register(SRCS db_link.c ws.c sat.c INCLUDE_DIRS include
                       REQUIRES esp_common esp_timer db_core
                       PRIV_REQUIRES db_log db_board db_config db_deck db_net db_ota esp_timer esp_app_format lwip mbedtls
                                     nvs_flash)
