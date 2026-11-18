set(priv db_core db_config db_deck db_dock db_link db_log db_net db_ota esp_timer esp_app_format lwip)
if(NOT ${IDF_TARGET} STREQUAL "linux")
    list(APPEND priv espcoredump)
endif()
idf_component_register(SRCS db_ui.c screens.c render.c canvas.c font_data.c INCLUDE_DIRS include
                       REQUIRES db_deck esp_common
                       PRIV_REQUIRES ${priv})
