# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(geometry_wars_retro_evolved_2 VERSION 0.7.0 LANGUAGES C CXX)
set(GW2_VERSION_SUFFIX "")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where tools/codegen.ps1, tools/codegen.sh and tools/package-linux.sh unpack the pinned SDK.
if(WIN32)
    set(GW2_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")
else()
    set(GW2_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/linux-amd64")
endif()
list(APPEND CMAKE_PREFIX_PATH "${GW2_SDK_DIR}")

# Release binaries must not carry the build machine's folders (__FILE__ in log calls).
add_compile_options("$<$<CONFIG:Release>:-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>")

include(generated/rexglue.cmake)

# TRG Launcher: the third_party/trg-launcher submodule, or a local checkout
# named by GW2_TRG_LAUNCHER_DIR while the submodule is not checked out.
set(GW2_TRG_LAUNCHER_DIR "$ENV{GW2_TRG_LAUNCHER_DIR}" CACHE PATH
    "Local TRG Launcher checkout, used when third_party/trg-launcher is empty")

# Upstream TRG is immutable. Every build compiles a patched copy under the
# binary directory; the submodule checkout must stay at the pinned revision.
set(GW2_TRG_LAUNCHER_REVISION "b384a28c712c9320eea8f9d50639dec49981b6c1")
set(GW2_TRG_LAUNCHER_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/patches/trg-launcher")
set(GW2_TRG_LAUNCHER_PATCH_STAMP_VERSION 2)
file(GLOB _trg_patch_names CONFIGURE_DEPENDS RELATIVE "${GW2_TRG_LAUNCHER_PATCH_DIR}"
    "${GW2_TRG_LAUNCHER_PATCH_DIR}/*.patch")
list(SORT _trg_patch_names)
if(NOT _trg_patch_names)
    message(FATAL_ERROR "TRG Launcher: no patches found in ${GW2_TRG_LAUNCHER_PATCH_DIR}")
endif()
foreach(_trg_patch_name IN LISTS _trg_patch_names)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${GW2_TRG_LAUNCHER_PATCH_DIR}/${_trg_patch_name}")
endforeach()

find_package(Git QUIET)
if(NOT Git_FOUND)
    message(FATAL_ERROR "TRG Launcher: git is required to apply patches/trg-launcher")
endif()

# Copies the source into output_dir and applies the patches there. The stamp
# covers the source state and every patch, so the copy is rebuilt only when one
# of them changes. GIT_CEILING_DIRECTORIES keeps git apply from discovering the
# parent repository when the generated copy has no .git of its own.
function(gw2_prepare_patched_trg_launcher source_dir output_dir require_pinned)
    set(_source_state "")
    if(EXISTS "${source_dir}/.git")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse HEAD
                        OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE
                        RESULT_VARIABLE _head_result ERROR_QUIET)
        if(NOT _head_result EQUAL 0)
            message(FATAL_ERROR "TRG Launcher: ${source_dir} is not a git checkout; run `git submodule update --init third_party/trg-launcher`")
        endif()
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" status --porcelain --untracked-files=normal
                        OUTPUT_VARIABLE _status OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" diff --binary HEAD
                        OUTPUT_VARIABLE _diff)
        string(SHA256 _source_state "${_head}\n${_status}\n${_diff}")
        if(require_pinned)
            if(NOT _head STREQUAL GW2_TRG_LAUNCHER_REVISION)
                message(FATAL_ERROR "TRG Launcher: ${source_dir} is at ${_head}, expected the pinned ${GW2_TRG_LAUNCHER_REVISION}. Run `git submodule update --init third_party/trg-launcher`.")
            endif()
            if(NOT _status STREQUAL "")
                message(FATAL_ERROR "TRG Launcher: ${source_dir} has local changes; run `git -C third_party/trg-launcher checkout .` and configure again.")
            endif()
        endif()
    else()
        file(GLOB_RECURSE _source_files LIST_DIRECTORIES false RELATIVE "${source_dir}" "${source_dir}/*")
        list(SORT _source_files)
        foreach(_file IN LISTS _source_files)
            if(_file MATCHES "(^|/)\\.git(/|$)")
                continue()
            endif()
            file(SHA256 "${source_dir}/${_file}" _file_hash)
            string(APPEND _source_state "${_file}\n${_file_hash}\n")
        endforeach()
        string(SHA256 _source_state "${_source_state}")
    endif()

    set(_fingerprint "${GW2_TRG_LAUNCHER_PATCH_STAMP_VERSION}\n${source_dir}\n${_source_state}\n")
    foreach(_patch_name IN LISTS _trg_patch_names)
        file(SHA256 "${GW2_TRG_LAUNCHER_PATCH_DIR}/${_patch_name}" _patch_hash)
        string(APPEND _fingerprint "${_patch_name}\n${_patch_hash}\n")
    endforeach()
    string(SHA256 _fingerprint "${_fingerprint}")

    set(_stamp "${output_dir}/.gw2-trg-patch-stamp")
    if(EXISTS "${_stamp}" AND EXISTS "${output_dir}/CMakeLists.txt")
        file(READ "${_stamp}" _stamp_value)
        if(_stamp_value STREQUAL _fingerprint)
            return()
        endif()
    endif()

    message(STATUS "TRG Launcher: preparing patched copy in ${output_dir}")
    file(REMOVE_RECURSE "${output_dir}")
    file(MAKE_DIRECTORY "${output_dir}")
    file(COPY "${source_dir}/" DESTINATION "${output_dir}" PATTERN ".git" EXCLUDE)

    # --ignore-whitespace: the submodule may be checked out with CRLF (core.autocrlf) while a patch has LF.
    get_filename_component(_ceiling "${output_dir}" DIRECTORY)
    foreach(_patch_name IN LISTS _trg_patch_names)
        set(_patch "${GW2_TRG_LAUNCHER_PATCH_DIR}/${_patch_name}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_ceiling}"
                        "${GIT_EXECUTABLE}" -C "${output_dir}" apply --check --ignore-whitespace "${_patch}"
                        RESULT_VARIABLE _can_apply ERROR_VARIABLE _apply_error)
        if(NOT _can_apply EQUAL 0)
            message(FATAL_ERROR "TRG Launcher: ${_patch} does not apply to ${source_dir} (${_apply_error}); the patch and the source revision are out of sync.")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_ceiling}"
                        "${GIT_EXECUTABLE}" -C "${output_dir}" apply --ignore-whitespace "${_patch}"
                        RESULT_VARIABLE _apply_result)
        if(NOT _apply_result EQUAL 0)
            message(FATAL_ERROR "TRG Launcher: failed to apply ${_patch}")
        endif()
    endforeach()
    file(WRITE "${_stamp}" "${_fingerprint}")
endfunction()

set(_trg_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/trg-launcher")
if(EXISTS "${_trg_source_dir}/CMakeLists.txt")
    set(_trg_require_pinned TRUE)
else()
    set(_trg_source_dir "${GW2_TRG_LAUNCHER_DIR}")
    set(_trg_require_pinned FALSE)
endif()
if(NOT EXISTS "${_trg_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR "TRG Launcher not found: run `git submodule update --init third_party/trg-launcher`")
endif()

set(_trg_patched_dir "${CMAKE_BINARY_DIR}/trg-launcher-patched")
gw2_prepare_patched_trg_launcher("${_trg_source_dir}" "${_trg_patched_dir}" ${_trg_require_pinned})

# Reconfigure when the patches, the upstream checkout or its HEAD change.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_trg_source_dir}/CMakeLists.txt")
if(EXISTS "${_trg_source_dir}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_trg_source_dir}" rev-parse --git-path HEAD
                    OUTPUT_VARIABLE _trg_head_path OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_trg_head_path)
        get_filename_component(_trg_head_path "${_trg_head_path}" ABSOLUTE BASE_DIR "${_trg_source_dir}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_trg_head_path}")
    endif()
endif()

# The launcher draws inside the runtime's own Dear ImGui frame, so it must use
# the ImGui compiled into rexruntime (one context), never a downloaded copy.
add_library(gw2_imgui INTERFACE)
target_link_libraries(gw2_imgui INTERFACE rex::runtime)
set(TRG_LAUNCHER_IMGUI_TARGET gw2_imgui CACHE STRING "" FORCE)
set(TRG_LAUNCHER_STANDALONE OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_FETCH_DEPS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_trg_patched_dir}" trg-launcher)
# Written for C++17: in C++20 and later u8string() is a different type.
set_target_properties(trg_launcher PROPERTIES CXX_STANDARD 17)
# TRG defines this only for cl.exe; clang++ needs it too for the MSVC CRT.
target_compile_definitions(trg_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
get_directory_property(GW2_TRG_VERSION DIRECTORY "${_trg_patched_dir}" DEFINITION PROJECT_VERSION)

set(GW2_SOURCES
    src/main.cpp
    src/active_log.cpp
    src/diagnostics.cpp
    src/guest_functions.cpp
    src/input_summary.cpp
    src/system_info.cpp
    src/profile_fix.cpp
    src/live_fix.cpp
    src/achievements_overlay.cpp
    src/aim_camera.cpp
    src/aspect_render.cpp
    src/keyboard_prompts.cpp
    src/title_logo.cpp
    src/hud_colour.cpp
    src/hud_colour_hooks.cpp
    src/online_board.cpp
    src/online_leaderboard.cpp
    src/cheat_menu.cpp
    src/cheat_hooks.cpp
    src/cheats.cpp
    src/bloom_fix.cpp
    src/crash_report.cpp
    src/rumble.cpp
    src/player_rumble_hooks.cpp
    src/king_rumble_hooks.cpp
    src/gravity_well_hooks.cpp
    src/gate_rumble_hooks.cpp
    src/crt_native.cpp
    src/gpu_fence_wait.cpp
    src/graphics_adapters.cpp
    src/imgui_gamepad.cpp
    src/live_stats.cpp
    src/local_stats.cpp
    src/main_menu_exit_game.cpp
    src/game_files.cpp
    src/stfs.cpp
    src/update_config.cpp
    src/updater/github_release.cpp
    src/updater/json.cpp
    src/updater/sha256.cpp
    src/updater/third_party/miniz/miniz.c
    src/updater/update_controller.cpp
    src/updater/update_job.cpp
    src/updater/update_paths.cpp
    src/updater/update_platform.cpp
    src/updater/update_ui.cpp
    src/updater/update_zip.cpp
    src/updater/version.cpp
    src/frame_counter.cpp
    src/frame_limiter.cpp
    src/frame_wait.cpp
    src/launcher.cpp
    src/launcher_achievements.cpp
    src/launcher_cvars.cpp
    src/launcher_options.cpp
    src/launcher_platform.cpp
    src/launcher_settings.cpp
    src/log_filter.cpp
    src/log_path.cpp
    src/session_log.cpp
    src/user_data.cpp
    src/visual_interpolation.cpp
    src/window_host.cpp
)
set_source_files_properties(src/updater/third_party/miniz/miniz.c PROPERTIES COMPILE_OPTIONS "-w")
if(WIN32)
    # Resource 1 is the first icon in the executable: Explorer, and SDL's window class, use it.
    enable_language(RC)
    set(GW2_APP_RC "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/geometry_wars_retro_evolved_2.rc")
    set_source_files_properties("${GW2_APP_RC}" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/app-icon.ico")
    list(APPEND GW2_SOURCES src/launcher_platform_win32.cpp "${GW2_APP_RC}")
    add_executable(geometry_wars_retro_evolved_2 WIN32 ${GW2_SOURCES})
    set(GW2_APP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/geometry_wars_retro_evolved_2.manifest")
    target_link_options(geometry_wars_retro_evolved_2 PRIVATE "LINKER:/MANIFESTINPUT:${GW2_APP_MANIFEST}")
    set_property(TARGET geometry_wars_retro_evolved_2 APPEND PROPERTY LINK_DEPENDS "${GW2_APP_MANIFEST}")
else()
    list(APPEND GW2_SOURCES src/launcher_platform_linux.cpp)
    add_executable(geometry_wars_retro_evolved_2 ${GW2_SOURCES})
endif()

rexglue_setup_target(geometry_wars_retro_evolved_2 GPU_PLUGINS xenos)
target_link_libraries(geometry_wars_retro_evolved_2 PRIVATE trg::launcher)
if(WIN32)
    target_link_libraries(geometry_wars_retro_evolved_2 PRIVATE advapi32 bcrypt dxgi shcore synchronization shell32 winhttp)
else()
    # The SDK stages the runtime beside the program only on Windows and macOS; the RPATH is $ORIGIN.
    add_custom_command(TARGET geometry_wars_retro_evolved_2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rex::runtime>"
            "$<TARGET_FILE_DIR:geometry_wars_retro_evolved_2>"
        VERBATIM
    )
endif()

# Every build checks that tools/rexglue holds the runtime and GPU plugin this branch's patches need, and that the
# copies next to the program are those; tools/build.ps1 and tools/build.sh prepare them when they are not.
if(WIN32)
    # Not cached, stable locations first: the versioned Microsoft Store PowerShell folder that PowerShell puts on PATH
    # is removed when the Store updates it.
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Microsoft/WindowsApps" _gw2_store_aliases)
    file(TO_CMAKE_PATH "$ENV{ProgramFiles}/PowerShell/7" _gw2_pwsh_install)
    unset(GW2_POWERSHELL CACHE)
    find_program(GW2_POWERSHELL NAMES pwsh powershell HINTS "${_gw2_store_aliases}" "${_gw2_pwsh_install}"
        NO_CACHE REQUIRED)
    set(GW2_REXGLUE_CHECK "${GW2_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-patched-rexruntime.ps1" -Check -Config $<CONFIG>)
    set(GW2_REXGLUE_STAGED -Staged)
else()
    find_program(GW2_BASH bash REQUIRED)
    set(GW2_REXGLUE_CHECK "${GW2_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-patched-rexruntime.sh"
        --check "${GW2_SDK_DIR}" --config $<CONFIG>)
    set(GW2_REXGLUE_STAGED --staged)
endif()
get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo|Release)$")
    message(FATAL_ERROR "Set CMAKE_BUILD_TYPE to Debug, RelWithDebInfo or Release (the presets, tools/build.ps1 "
        "and tools/build.sh do): the ReXGlue runtime and GPU plugin are prepared and checked per configuration")
endif()
add_custom_target(gw2_rexglue_check COMMAND ${GW2_REXGLUE_CHECK} VERBATIM)
add_dependencies(geometry_wars_retro_evolved_2 gw2_rexglue_check)
add_custom_target(gw2_rexglue_staging ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE:rex::gpu-xenos>"
        "$<TARGET_FILE_DIR:geometry_wars_retro_evolved_2>"
    COMMAND ${GW2_REXGLUE_CHECK} ${GW2_REXGLUE_STAGED} "$<TARGET_FILE_DIR:geometry_wars_retro_evolved_2>"
    VERBATIM
)
add_dependencies(gw2_rexglue_staging geometry_wars_retro_evolved_2)

# ThinLTO in Release and RelWithDebInfo; the runtime and GPU plugin are prebuilt. Debug compiles at -O0, which marks
# every function optnone, so LTO would optimize nothing there.
function(gw2_enable_thinlto target)
    set(_lto "$<CONFIG:Release,RelWithDebInfo>")
    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        # lld-link runs the LTO backend at -O2 unless told otherwise; Release compiles at -O3.
        set(_link -flto=thin "$<$<CONFIG:Release>:-Wl,/opt:lldlto=3>"
            "-Wl,/lldltocache:${CMAKE_BINARY_DIR}/lto-cache")
    elseif(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(_link -flto=thin -fuse-ld=lld "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/lto-cache")
    else()
        return()
    endif()
    target_compile_options(${target} PRIVATE "$<$<AND:${_lto},$<COMPILE_LANGUAGE:CXX>>:-flto=thin>")
    get_target_property(_type ${target} TYPE)
    if(NOT _type STREQUAL "OBJECT_LIBRARY")
        target_link_options(${target} PRIVATE "$<${_lto}:${_link}>")
    endif()
endfunction()
gw2_enable_thinlto(geometry_wars_retro_evolved_2)
if(TARGET geometry_wars_retro_evolved_2_recomp)
    gw2_enable_thinlto(geometry_wars_retro_evolved_2_recomp)
endif()

if(WIN32)
    add_executable(trg-updater WIN32
        src/updater/helper_main.cpp
        src/updater/install_transaction.cpp
        src/updater/sha256.cpp
        src/updater/update_job.cpp
        src/updater/update_paths.cpp
        src/updater/update_platform.cpp
    )
    target_include_directories(trg-updater PRIVATE src)
    target_link_libraries(trg-updater PRIVATE shell32)
    set_target_properties(trg-updater PROPERTIES OUTPUT_NAME "trg-updater")
    gw2_enable_thinlto(trg-updater)
endif()

target_compile_definitions(geometry_wars_retro_evolved_2 PRIVATE
    GW2_PORT_VERSION="${PROJECT_VERSION}${GW2_VERSION_SUFFIX}"
    GW2_BUILD_CONFIG="$<CONFIG>"
    GW2_TRG_LAUNCHER_VERSION="${GW2_TRG_VERSION}")

# The runtime reads its cvars from <exe dir>/geometry_wars_retro_evolved_2.toml.
add_custom_command(TARGET geometry_wars_retro_evolved_2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/geometry_wars_retro_evolved_2.toml"
        "$<TARGET_FILE_DIR:geometry_wars_retro_evolved_2>"
    VERBATIM
)

add_custom_command(TARGET geometry_wars_retro_evolved_2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:geometry_wars_retro_evolved_2>/assets/launcher"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/header-art.png"
        "$<TARGET_FILE_DIR:geometry_wars_retro_evolved_2>/assets/launcher/header-art.png"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:geometry_wars_retro_evolved_2>/assets/controls"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/controls/controller_kbm.tga"
        "$<TARGET_FILE_DIR:geometry_wars_retro_evolved_2>/assets/controls/controller_kbm.tga"
    VERBATIM
)

# Unit tests need no game files and no ReXGlue runtime. They are not part of the default build:
#   cmake --build <dir> --target gw2_tests
# and the programs land in <dir>/tests. On Windows tools/test.ps1 builds and runs the same set.
add_custom_target(gw2_tests)
function(gw2_add_test name)
    add_executable(${name} EXCLUDE_FROM_ALL tests/${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE src)
    target_include_directories(${name} SYSTEM PRIVATE "${GW2_SDK_DIR}/include" "${_trg_patched_dir}/include")
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS _SILENCE_ALL_CXX20_DEPRECATION_WARNINGS)
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(NOT WIN32)
        target_link_libraries(${name} PRIVATE Threads::Threads)
    endif()
    add_dependencies(gw2_tests ${name})
endfunction()
if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

gw2_add_test(live_tests src/local_stats.cpp src/live_stats.cpp)
gw2_add_test(achievements_overlay_rules_tests)
gw2_add_test(launcher_tests src/game_files.cpp src/stfs.cpp src/launcher_achievements.cpp)
gw2_add_test(window_host_tests)
target_compile_definitions(window_host_tests PRIVATE GW2_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
gw2_add_test(frame_counter_tests)
gw2_add_test(frame_limiter_tests src/frame_wait.cpp)
gw2_add_test(visual_interpolation_tests src/visual_interpolation.cpp)
gw2_add_test(launcher_placement_tests)
gw2_add_test(user_data_tests src/user_data.cpp)
gw2_add_test(cheats_tests)
target_compile_definitions(cheats_tests PRIVATE GW2_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
gw2_add_test(log_path_tests src/log_path.cpp)
gw2_add_test(dpi_tests)
gw2_add_test(online_tests src/online_board.cpp src/updater/json.cpp)
gw2_add_test(keyboard_tests)
gw2_add_test(title_logo_tests src/title_logo.cpp)
gw2_add_test(hud_colour_tests src/hud_colour.cpp)
target_link_libraries(keyboard_tests PRIVATE gw2_trg_test_support)
target_compile_definitions(keyboard_tests PRIVATE GW2_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
gw2_add_test(launcher_shortcut_tests)
target_compile_definitions(launcher_shortcut_tests PRIVATE
    GW2_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
    GW2_TRG_SRC_DIR="${_trg_patched_dir}/src")
gw2_add_test(lifecycle_tests)
target_compile_definitions(lifecycle_tests PRIVATE GW2_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
# TRG's settings and platform sources without its ImGui dependency; C++17 like the launcher library.
add_library(gw2_trg_test_support STATIC EXCLUDE_FROM_ALL
    "${_trg_patched_dir}/src/settings.cpp" "${_trg_patched_dir}/src/platform.cpp")
target_include_directories(gw2_trg_test_support PUBLIC "${_trg_patched_dir}/include")
set_target_properties(gw2_trg_test_support PROPERTIES CXX_STANDARD 17)
target_compile_definitions(gw2_trg_test_support PRIVATE _CRT_SECURE_NO_WARNINGS)
gw2_add_test(launcher_settings_tests src/launcher_settings.cpp src/launcher_options.cpp)
gw2_add_test(graphics_adapter_tests src/graphics_adapters.cpp)
gw2_add_test(diagnostics_tests src/diagnostics.cpp)
gw2_add_test(aspect_ratio_tests)
target_compile_definitions(aspect_ratio_tests PRIVATE GW2_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
gw2_add_test(update_tests
    src/updater/github_release.cpp
    src/updater/install_transaction.cpp
    src/updater/json.cpp
    src/updater/sha256.cpp
    src/updater/update_job.cpp
    src/updater/update_paths.cpp
    src/updater/update_platform.cpp
    src/updater/update_zip.cpp
    src/updater/version.cpp
    src/updater/third_party/miniz/miniz.c
)
target_compile_definitions(update_tests PRIVATE GW2_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(launcher_settings_tests PRIVATE gw2_trg_test_support)
target_compile_definitions(launcher_settings_tests PRIVATE GW2_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
if(WIN32)
    target_link_libraries(launcher_tests PRIVATE bcrypt)
    target_link_libraries(graphics_adapter_tests PRIVATE advapi32 dxgi d3d12)
    target_link_libraries(update_tests PRIVATE shell32)
    target_link_libraries(gw2_trg_test_support PUBLIC shell32 ole32 comdlg32 winmm user32)
else()
    target_link_libraries(gw2_trg_test_support PUBLIC Threads::Threads)
endif()

# Tests that need the ReXGlue runtime, not part of the default build: cmake --build <dir> --target <name>
add_executable(gw2_physical_heap_stress EXCLUDE_FROM_ALL tests/physical_heap_stress.cpp)
target_link_libraries(gw2_physical_heap_stress PRIVATE rex::runtime)
add_executable(gw2_restart_state_tests EXCLUDE_FROM_ALL tests/restart_state_tests.cpp src/launcher_cvars.cpp
    src/launcher_settings.cpp src/launcher_options.cpp src/graphics_adapters.cpp src/diagnostics.cpp
    src/system_info.cpp src/game_files.cpp src/stfs.cpp src/crash_report.cpp)
target_include_directories(gw2_restart_state_tests PRIVATE src)
target_compile_options(gw2_restart_state_tests PRIVATE -Wall -Wextra)
target_compile_definitions(gw2_restart_state_tests PRIVATE GW2_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gw2_restart_state_tests PRIVATE rex::runtime gw2_trg_test_support)
if(WIN32)
    target_link_libraries(gw2_restart_state_tests PRIVATE advapi32 bcrypt dxgi)
endif()
add_executable(gw2_title_thread_shutdown_tests EXCLUDE_FROM_ALL tests/title_thread_shutdown_tests.cpp)
target_link_libraries(gw2_title_thread_shutdown_tests PRIVATE rex::runtime)
add_executable(gw2_shutdown_order_tests EXCLUDE_FROM_ALL tests/shutdown_order_tests.cpp)
target_link_libraries(gw2_shutdown_order_tests PRIVATE rex::runtime)
target_compile_options(gw2_title_thread_shutdown_tests PRIVATE -Wall -Wextra)
target_compile_options(gw2_shutdown_order_tests PRIVATE -Wall -Wextra)
add_executable(gw2_active_log_tests EXCLUDE_FROM_ALL tests/active_log_tests.cpp src/active_log.cpp
    src/log_filter.cpp)
target_include_directories(gw2_active_log_tests PRIVATE src)
target_include_directories(gw2_active_log_tests SYSTEM PRIVATE "${GW2_SDK_DIR}/include")
target_compile_options(gw2_active_log_tests PRIVATE -Wall -Wextra)
target_link_libraries(gw2_active_log_tests PRIVATE rex::runtime)
add_executable(gw2_crash_report_tests EXCLUDE_FROM_ALL tests/crash_report_tests.cpp src/crash_report.cpp)
target_include_directories(gw2_crash_report_tests PRIVATE src)
target_compile_options(gw2_crash_report_tests PRIVATE -Wall -Wextra)
if(WIN32)
    target_link_libraries(gw2_crash_report_tests PRIVATE shell32)
endif()
add_executable(gw2_log_filter_tests EXCLUDE_FROM_ALL tests/log_filter_tests.cpp src/active_log.cpp
    src/crash_report.cpp src/log_filter.cpp src/session_log.cpp)
target_include_directories(gw2_log_filter_tests PRIVATE src)
target_include_directories(gw2_log_filter_tests SYSTEM PRIVATE "${GW2_SDK_DIR}/include")
target_compile_options(gw2_log_filter_tests PRIVATE -Wall -Wextra)
target_compile_definitions(gw2_log_filter_tests PRIVATE GW2_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(gw2_log_filter_tests PRIVATE rex::runtime)
if(WIN32)
    target_link_libraries(gw2_log_filter_tests PRIVATE shell32)
endif()
set(_runtime_tests gw2_physical_heap_stress gw2_restart_state_tests gw2_title_thread_shutdown_tests
    gw2_shutdown_order_tests gw2_active_log_tests gw2_log_filter_tests)
# The fake librenderdoc.so goes into renderdoc/, out of reach of the runtime's $ORIGIN RUNPATH.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(gw2_fake_renderdoc SHARED EXCLUDE_FROM_ALL tests/fake_renderdoc.cpp)
    set_target_properties(gw2_fake_renderdoc PROPERTIES OUTPUT_NAME renderdoc
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/renderdoc")
    add_executable(gw2_renderdoc_detect_tests EXCLUDE_FROM_ALL tests/renderdoc_detect_tests.cpp)
    target_link_libraries(gw2_renderdoc_detect_tests PRIVATE rex::runtime rex::renderdoc ${CMAKE_DL_LIBS})
    target_compile_options(gw2_renderdoc_detect_tests PRIVATE -Wall -Wextra)
    add_dependencies(gw2_renderdoc_detect_tests gw2_fake_renderdoc)
    add_executable(gw2_presenter_pacing_tests EXCLUDE_FROM_ALL tests/presenter_pacing_tests.cpp)
    target_link_libraries(gw2_presenter_pacing_tests PRIVATE rex::runtime rex::renderdoc)
    target_compile_options(gw2_presenter_pacing_tests PRIVATE -Wall -Wextra)
    list(APPEND _runtime_tests gw2_renderdoc_detect_tests gw2_presenter_pacing_tests)
endif()
foreach(_runtime_test ${_runtime_tests})
    add_custom_command(TARGET ${_runtime_test} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rex::runtime>"
            "$<TARGET_FILE_DIR:${_runtime_test}>"
        VERBATIM
    )
    if(NOT WIN32)
        set_target_properties(${_runtime_test} PROPERTIES BUILD_RPATH "$ORIGIN")
    endif()
endforeach()
