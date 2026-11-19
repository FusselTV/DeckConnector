idf_component_register(SRCS db_log.c evlog.c INCLUDE_DIRS include REQUIRES db_core PRIV_REQUIRES log esp_timer nvs_flash esp_partition)
