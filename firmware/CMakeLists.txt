cmake_minimum_required(VERSION 3.22)

execute_process(COMMAND git describe --tags --always --dirty WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE PROJECT_VER OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deckbridge)

# Third party code: GCC 15 flags a harmless strncpy in mdns. Our own components keep -Werror.
idf_component_get_property(mdns_lib espressif__mdns COMPONENT_LIB)
target_compile_options(${mdns_lib} PRIVATE -Wno-error=stringop-truncation)
