cmake_minimum_required(VERSION 3.22)

execute_process(COMMAND git describe --tags --always --dirty WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE PROJECT_VER OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
# Re-run the configure step (9 s) whenever HEAD moves, so the version inside the image stays right. Not on the index:
# git status rewrites it after every edit. --git-path, because .git is a file in a worktree. "-dirty" is as of the
# last configure.
execute_process(COMMAND git rev-parse --path-format=absolute --git-path logs/HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} OUTPUT_VARIABLE head_log OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${head_log})

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

# ldgen takes 5 s and reruns on every edit, because it depends on every library. ldgen_cache.py skips it when its
# output cannot change. CMake sets a launcher only per directory, so every custom command here passes through it.
set_property(DIRECTORY PROPERTY RULE_LAUNCH_CUSTOM ${CMAKE_CURRENT_LIST_DIR}/ldgen_launch.sh)
