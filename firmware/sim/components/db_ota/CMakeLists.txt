idf_component_register(SRCS ota.c INCLUDE_DIRS ../../../components/db_ota/include REQUIRES db_core esp_common
                       PRIV_REQUIRES freertos)
