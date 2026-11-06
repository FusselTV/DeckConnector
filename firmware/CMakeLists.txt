cmake_minimum_required(VERSION 3.22)

execute_process(COMMAND git describe --tags --always --dirty WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE PROJECT_VER OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
# Re-run the configure step after every commit so the version inside the image stays right.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../.git/HEAD
             ${CMAKE_CURRENT_LIST_DIR}/../.git/index)

# esp_hosted stays on 3.0.10 (idf_component.yml). The check for newer components reads the manifest of 3.1.0,
# whose Kconfig options 3.0.10 lacks, and that fails the configure step.
set(ENV{IDF_COMPONENT_CHECK_NEW_VERSION} 0)

# ccache when it is installed. A plain variable, because idf.py always sets the cache entry, to off by default.
set(CCACHE_ENABLE 1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deckbridge)

# Third party code: GCC 15 flags a harmless strncpy in mdns. Our own components keep -Werror.
idf_component_get_property(mdns_lib espressif__mdns COMPONENT_LIB)
target_compile_options(${mdns_lib} PRIVATE -Wno-error=stringop-truncation)
