set(emu ../../../host_test)
idf_component_register(SRCS usb_port.c chip.c ${emu}/deck_emu.c ${emu}/mirabox_emu.c
                       PRIV_INCLUDE_DIRS ../../../components/db_deck ${emu}
                       PRIV_REQUIRES db_config db_core db_deck esp_partition esp_timer freertos log)
target_link_libraries(${COMPONENT_LIB} PUBLIC z)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=db_config_load")
