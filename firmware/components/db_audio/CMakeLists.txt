# The simulator's speaker is silent, and it has no microphone for the self-test.
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(SRCS db_audio_none.c stream.c INCLUDE_DIRS include
                           REQUIRES db_core esp_common esp_http_server PRIV_REQUIRES freertos)
else()
    idf_component_register(SRCS db_audio.c stream.c INCLUDE_DIRS include
                           REQUIRES db_core esp_common esp_http_server
                           PRIV_REQUIRES db_board db_config esp_driver_gpio esp_driver_i2c esp_driver_i2s)
endif()
