idf_component_register(SRCS log.c ../../../components/db_log/evlog.c ../../../components/db_log/trace.c INCLUDE_DIRS ../../../components/db_log/include
                       REQUIRES db_core PRIV_REQUIRES esp_http_server esp_partition esp_timer nvs_flash)
