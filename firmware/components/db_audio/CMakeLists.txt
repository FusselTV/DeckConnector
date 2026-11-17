# The simulator has no speaker: the API answers as on a board without one.
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(SRCS db_audio_none.c INCLUDE_DIRS include REQUIRES db_core esp_common)
else()
    idf_component_register(SRCS db_audio.c INCLUDE_DIRS include
                           REQUIRES db_core esp_common
                           PRIV_REQUIRES db_board db_config esp_driver_i2c esp_driver_i2s)
endif()
