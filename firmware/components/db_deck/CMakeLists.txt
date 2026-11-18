# On Linux (firmware/simdeck) the USB port is emulated and libjpeg does the JPEG work.
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(SRCS db_deck.c jpeg_port.c INCLUDE_DIRS include
                           REQUIRES db_core esp_common
                           PRIV_REQUIRES db_log db_ota esp_timer nvs_flash)
    target_link_libraries(${COMPONENT_LIB} PUBLIC jpeg)
else()
    idf_component_register(SRCS db_deck.c usb_port.c jpeg_port.c INCLUDE_DIRS include
                           REQUIRES db_core esp_common
                           PRIV_REQUIRES db_log db_ota esp_driver_jpeg esp_timer nvs_flash)
endif()
