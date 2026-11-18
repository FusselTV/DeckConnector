cmake_minimum_required(VERSION 3.22)

include(common.cmake)
project(deckconnector)

# ldgen takes 5 s and reruns on every edit, because it depends on every library. ldgen_cache.py skips it when its
# output cannot change. CMake sets a launcher only per directory, so every custom command here passes through it.
set_property(DIRECTORY PROPERTY RULE_LAUNCH_CUSTOM ${CMAKE_CURRENT_LIST_DIR}/ldgen_launch.sh)
