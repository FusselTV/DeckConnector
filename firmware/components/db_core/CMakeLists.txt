set(srcs db_device.c db_driver.c db_json.c db_proto.c db_regions.c drivers/elgato_gen2.c db_cora.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${srcs} INCLUDE_DIRS include)
else()
    add_library(db_core STATIC ${srcs})
    target_include_directories(db_core PUBLIC include)
endif()
