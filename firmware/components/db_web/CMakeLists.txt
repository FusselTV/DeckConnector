# On Linux (firmware/sim) there is no HTTPS server and no chip: no cert.c and metrics.c, no TLS, no mDNS.
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(SRCS http.c api.c auth.c events.c INCLUDE_DIRS include
                           REQUIRES esp_common
                           PRIV_REQUIRES nvs_flash db_config db_core db_deck db_dock db_ui db_link db_log db_net db_ota esp_app_format esp_http_server esp_timer mbedtls)
else()
    idf_component_register(SRCS http.c api.c metrics.c auth.c events.c cert.c INCLUDE_DIRS include
                           REQUIRES esp_common
                           PRIV_REQUIRES app_update bootloader_support espcoredump esp_partition nvs_flash db_config db_core db_deck db_dock db_ui esp_app_format db_link db_log db_net db_ota esp_http_server esp_https_server esp_http_client esp_timer lwip mbedtls esp_driver_tsens)
endif()
# Test builds only: a route that crashes on purpose, for the coredump test (tools/tests/test_coredump.py).
if(DB_DEBUG_CRASH)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DB_DEBUG_CRASH)
endif()
idf_build_get_property(python PYTHON)
# The React build. Checked when CMake runs.
set(page ${CMAKE_CURRENT_SOURCE_DIR}/../../../web/dist/index.html)
if(NOT EXISTS ${page})
    message(FATAL_ERROR "web/dist/index.html is missing: run `cd web && npm ci && npm run build` first")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../../web/dist)
set(page_gz ${CMAKE_CURRENT_BINARY_DIR}/index.html.gz)
add_custom_command(OUTPUT ${page_gz}
                   COMMAND ${python} -c "import gzip, sys; open(sys.argv[2], 'wb').write(gzip.compress(open(sys.argv[1], 'rb').read(), 9, mtime=0))"
                           ${page} ${page_gz}
                   DEPENDS ${page} VERBATIM)
add_custom_target(db_web_page_gz DEPENDS ${page_gz})
target_add_binary_data(${COMPONENT_LIB} ${page_gz} BINARY DEPENDS db_web_page_gz)
