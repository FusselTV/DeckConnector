idf_component_register(SRCS db_config.c INCLUDE_DIRS include
                       REQUIRES esp_common
                       PRIV_REQUIRES db_core nvs_flash esp_hw_support lwip)
