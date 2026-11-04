idf_component_register(SRCS db_web.c INCLUDE_DIRS include EMBED_TXTFILES index.html
                       REQUIRES esp_common
                       PRIV_REQUIRES db_config db_deck db_log db_net db_ota esp_http_server lwip mbedtls)
