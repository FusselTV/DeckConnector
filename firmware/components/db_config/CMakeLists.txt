set(priv db_core nvs_flash esp_hw_support)
if(NOT ${IDF_TARGET} STREQUAL "linux")
    list(APPEND priv lwip) # arpa/inet.h; firmware/sim takes the host's
endif()
idf_component_register(SRCS db_config.c INCLUDE_DIRS include
                       REQUIRES esp_common
                       PRIV_REQUIRES ${priv})
