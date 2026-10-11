# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(geometry_wars_retro_evolved_2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where tools/codegen.ps1 unpacks the pinned SDK.
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")

include(generated/rexglue.cmake)

add_executable(geometry_wars_retro_evolved_2 WIN32 src/main.cpp src/debug_saulo_fault.cpp)

rexglue_setup_target(geometry_wars_retro_evolved_2 GPU_PLUGINS xenos)

# The runtime reads its cvars from <exe dir>/geometry_wars_retro_evolved_2.toml.
add_custom_command(TARGET geometry_wars_retro_evolved_2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/geometry_wars_retro_evolved_2.toml"
        "$<TARGET_FILE_DIR:geometry_wars_retro_evolved_2>"
    VERBATIM
)
