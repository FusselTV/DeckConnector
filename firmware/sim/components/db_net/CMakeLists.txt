idf_component_register(SRCS net.c timer.c INCLUDE_DIRS ../../../components/db_net/include REQUIRES esp_common mdns
                       PRIV_REQUIRES db_config esp_event esp_netif_linux esp_timer)
