if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(SRCS improv.c INCLUDE_DIRS include
                           REQUIRES esp_common
                           PRIV_REQUIRES db_config db_core db_net db_web esp_app_format esp_timer)
else()
    idf_component_register(SRCS improv.c INCLUDE_DIRS include
                           REQUIRES esp_common
                           PRIV_REQUIRES db_config db_core db_net db_web esp_app_format esp_driver_uart esp_timer)
endif()
