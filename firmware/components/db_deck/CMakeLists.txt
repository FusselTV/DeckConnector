# On Linux (firmware/simdeck) the USB port is emulated and libjpeg does the JPEG work.
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(SRCS db_deck.c jpeg_port.c INCLUDE_DIRS include
                           REQUIRES db_core esp_common
                           PRIV_REQUIRES db_log db_ota esp_timer nvs_flash)
    # static: CI builds in the ESP-IDF image (libjpeg.so.8) and runs on Debian (libjpeg.so.62)
    target_link_libraries(${COMPONENT_LIB} PUBLIC -l:libjpeg.a)
else()
    idf_component_register(SRCS db_deck.c usb_port.c jpeg_port.c INCLUDE_DIRS include
                           REQUIRES db_core esp_common
                           PRIV_REQUIRES db_log db_ota esp_driver_jpeg esp_timer nvs_flash)
endif()
