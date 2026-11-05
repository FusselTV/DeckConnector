idf_component_register(SRCS db_web.c auth.c api.c events.c INCLUDE_DIRS include EMBED_TXTFILES index.html
                       REQUIRES esp_common
                       PRIV_REQUIRES db_config db_core db_deck db_ui esp_app_format db_log db_net db_ota esp_http_server esp_timer lwip mbedtls)

idf_build_get_property(python PYTHON)
set(page_gz ${CMAKE_CURRENT_BINARY_DIR}/index.html.gz)
add_custom_command(OUTPUT ${page_gz}
                   COMMAND ${python} -c "import gzip, sys; open(sys.argv[2], 'wb').write(gzip.compress(open(sys.argv[1], 'rb').read(), 9, mtime=0))"
                           ${CMAKE_CURRENT_SOURCE_DIR}/index.html ${page_gz}
                   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/index.html VERBATIM)
add_custom_target(db_web_page_gz DEPENDS ${page_gz})
target_add_binary_data(${COMPONENT_LIB} ${page_gz} BINARY DEPENDS db_web_page_gz)
