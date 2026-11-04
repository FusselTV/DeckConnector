idf_component_register(SRCS main.c PRIV_REQUIRES db_config db_core db_deck db_dock db_link db_log db_net db_ota db_ui db_web nvs_flash esp_timer esp_app_format)

# Test image for the rollback check in tools/dbtools/test_ota.py: crashes right after boot.
if(DB_TEST_CRASH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DB_TEST_CRASH)
endif()
