idf_component_register(SRCS db_web.c auth.c api.c events.c INCLUDE_DIRS include EMBED_TXTFILES index.html
                       REQUIRES esp_common
                       PRIV_REQUIRES db_config db_core db_deck db_ui esp_app_format db_log db_net db_ota esp_http_server esp_timer lwip mbedtls)
