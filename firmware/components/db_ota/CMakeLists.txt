idf_component_register(SRCS db_ota.c ota_pubkey.c restart.c INCLUDE_DIRS include
                       REQUIRES esp_common
                       PRIV_REQUIRES db_log app_update bootloader_support db_core esp_partition spi_flash esp_http_client mbedtls esp_app_format nvs_flash esp_timer esp_hw_support riscv soc)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_restart_noos")
