idf_component_register(SRCS db_net.c INCLUDE_DIRS include
                       REQUIRES esp_common
                       PRIV_REQUIRES db_log db_board db_config esp_netif esp_eth esp_event esp_timer esp_wifi lwip)
