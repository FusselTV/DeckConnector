idf_component_register(SRCS db_ota.c ota_pubkey.c INCLUDE_DIRS include
                       REQUIRES esp_common
                       PRIV_REQUIRES app_update bootloader_support db_core esp_partition spi_flash esp_http_client mbedtls esp_app_format nvs_flash esp_timer)
