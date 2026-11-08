# The API layer (db_web, db_config, db_core) on Linux with the real esp_http_server, and fakes for everything that
# needs the chip. `idf.py build`, then `build/deckbridge_sim.elf` serves on DB_SIM_PORT (default 8080).
cmake_minimum_required(VERSION 3.22)
set(ENV{IDF_COMPONENT_CHECK_NEW_VERSION} 0)
set(CCACHE_ENABLE 1)
set(EXTRA_COMPONENT_DIRS ../components/db_web ../components/db_config ../components/db_core ../components/db_serial components)
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deckbridge_sim)
