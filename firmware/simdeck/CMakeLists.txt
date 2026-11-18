# The firmware's own main, deck stack, dock, servers and Satellite on Linux, on emulated USB decks
# (DB_SIM_DECK_MODELS). firmware/sim's fakes stand in for the network, updates and the log. `idf.py build`,
# then `build/deckbridge_simdeck.elf` serves on DB_SIM_PORT (default 8080).
cmake_minimum_required(VERSION 3.22)
set(ENV{IDF_COMPONENT_CHECK_NEW_VERSION} 0)
set(CCACHE_ENABLE 1)
set(EXTRA_COMPONENT_DIRS ../components/db_audio ../components/db_board ../components/db_config ../components/db_core
                         ../components/db_deck ../components/db_dock ../components/db_link ../components/db_serial
                         ../components/db_ui ../components/db_web ../sim/components/db_log ../sim/components/db_net
                         ../sim/components/db_ota)
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_CURRENT_LIST_DIR}/components/sim_usb/include" APPEND)
project(deckbridge_simdeck)
idf_component_get_property(deck db_deck COMPONENT_LIB)
idf_component_get_property(usb sim_usb COMPONENT_LIB)
target_link_libraries(${deck} PUBLIC ${usb})
