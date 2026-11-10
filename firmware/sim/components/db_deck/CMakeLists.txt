idf_component_register(SRCS fakes.c
                       INCLUDE_DIRS ../../../components/db_deck/include
                       PRIV_INCLUDE_DIRS ../../../components/db_dock/include ../../../components/db_ui/include
                                         ../../../components/db_ota/include ../../../components/db_log/include
                                         ../../../components/db_net/include
                                         ../../../components/db_link/include
                       REQUIRES db_core db_config esp_timer esp_http_server nvs_flash)
