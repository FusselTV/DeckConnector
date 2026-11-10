cmake_minimum_required(VERSION 3.22)
project(deckbridge_host_test C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror -Wconversion -Wno-sign-conversion -fsanitize=address,undefined
                    -fno-omit-frame-pointer)
add_link_options(-fsanitize=address,undefined)

include(FetchContent)
FetchContent_Declare(unity GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git GIT_TAG v2.6.1)
FetchContent_MakeAvailable(unity)

add_subdirectory(../components/db_core db_core)

enable_testing()
foreach(t test_regions test_proto test_elgato test_image test_cora test_screen test_dns test_ptable test_improv)
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} db_core unity)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

add_executable(test_sat test_sat.c)
target_link_libraries(test_sat db_core unity z)
add_test(NAME test_sat COMMAND test_sat)

set(ui ../components/db_ui)
add_executable(test_ui test_ui.c ${ui}/screens.c ${ui}/render.c ${ui}/canvas.c ${ui}/font_data.c)
target_include_directories(test_ui PRIVATE ${ui})
target_compile_definitions(test_ui PRIVATE UI_OUT="${CMAKE_BINARY_DIR}/ui")
target_link_libraries(test_ui db_core unity m)
add_test(NAME test_ui COMMAND test_ui)

add_executable(test_decks test_decks.c deck_emu.c)
target_link_libraries(test_decks db_core)
add_test(NAME test_decks COMMAND test_decks)
