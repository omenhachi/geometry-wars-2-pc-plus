# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(geometry_wars_retro_evolved_2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where tools/codegen.ps1 unpacks the pinned SDK.
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")

include(generated/rexglue.cmake)

# TRG Launcher: the third_party/trg-launcher submodule.
set(_trg_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/trg-launcher")
if(NOT EXISTS "${_trg_dir}/CMakeLists.txt")
    message(FATAL_ERROR "TRG Launcher not found: run `git submodule update --init third_party/trg-launcher`")
endif()

# The launcher draws inside the runtime's own Dear ImGui frame, so it must use
# the ImGui compiled into rexruntime (one context), never a downloaded copy.
add_library(gw2_imgui INTERFACE)
target_link_libraries(gw2_imgui INTERFACE rex::runtime)
set(TRG_LAUNCHER_IMGUI_TARGET gw2_imgui CACHE STRING "" FORCE)
set(TRG_LAUNCHER_STANDALONE OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_FETCH_DEPS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_trg_dir}" trg-launcher)
# Written for C++17: in C++20 and later u8string() is a different type.
set_target_properties(trg_launcher PROPERTIES CXX_STANDARD 17)
# TRG defines this only for cl.exe; clang++ needs it too for the MSVC CRT.
target_compile_definitions(trg_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
get_directory_property(GW2_TRG_VERSION DIRECTORY "${_trg_dir}" DEFINITION PROJECT_VERSION)

add_executable(geometry_wars_retro_evolved_2 WIN32
    src/main.cpp
    src/debug_saulo_fault.cpp
    src/profile_fix.cpp
    src/live_fix.cpp
    src/achievements_overlay.cpp
    src/live_stats.cpp
    src/local_stats.cpp
    src/game_files.cpp
    src/stfs.cpp
    src/frame_counter.cpp
    src/frame_limiter.cpp
    src/launcher.cpp
    src/launcher_achievements.cpp
    src/launcher_cvars.cpp
    src/launcher_options.cpp
    src/launcher_platform.cpp
    src/launcher_settings.cpp
    src/window_host.cpp
)

rexglue_setup_target(geometry_wars_retro_evolved_2 GPU_PLUGINS xenos)
target_link_libraries(geometry_wars_retro_evolved_2 PRIVATE trg::launcher bcrypt shcore)
target_compile_definitions(geometry_wars_retro_evolved_2 PRIVATE
    GW2_TRG_LAUNCHER_VERSION="${GW2_TRG_VERSION}")

# The runtime reads its cvars from <exe dir>/geometry_wars_retro_evolved_2.toml.
add_custom_command(TARGET geometry_wars_retro_evolved_2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/geometry_wars_retro_evolved_2.toml"
        "$<TARGET_FILE_DIR:geometry_wars_retro_evolved_2>"
    VERBATIM
)
