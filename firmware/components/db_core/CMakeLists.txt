set(srcs db_device.c db_driver.c db_json.c db_proto.c db_regions.c db_screen.c drivers/elgato_gen1.c drivers/elgato_gen2.c drivers/mirabox.c db_image.c db_quirks.c db_cora.c db_dns.c
         db_sat.c db_ptable.c db_improv.c db_version.c db_wake.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
else()
    add_library(db_core STATIC ${srcs})
    target_include_directories(db_core PUBLIC include)
endif()
