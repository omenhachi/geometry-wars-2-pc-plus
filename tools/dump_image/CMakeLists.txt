cmake_minimum_required(VERSION 3.25)
project(geometry_wars_retro_evolved_2_dump_image LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)

find_package(rexglue 0.10.0 REQUIRED CONFIG)

add_executable(dump_image main.cpp)
target_link_libraries(dump_image PRIVATE rex::runtime)

add_custom_command(TARGET dump_image POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:dump_image> $<TARGET_FILE_DIR:dump_image>
    COMMAND_EXPAND_LISTS)
