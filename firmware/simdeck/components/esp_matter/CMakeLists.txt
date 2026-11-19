idf_component_register(SRCS fake.cpp INCLUDE_DIRS include REQUIRES esp_event esp_netif_linux PRIV_REQUIRES freertos)
# what newlib and esp-matter's flags give db_matter.cpp on the chip
target_compile_options(${COMPONENT_LIB} INTERFACE
                       $<$<COMPILE_LANGUAGE:CXX>:-D_Static_assert=static_assert -Wno-missing-field-initializers>)
