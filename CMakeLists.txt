cmake_minimum_required(VERSION 3.20)

project(ChessProject VERSION 1.1.0 LANGUAGES C)

# ------------------------------------------------------------
# Global toolchain settings
# ------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CHESS_FETCH_RAYLIB "Fetch raylib automatically when not installed" ON)
option(CHESS_ENABLE_WARNINGS "Enable common warning flags" ON)
option(CHESS_BUILD_LEGACY_RELAY_SERVER "Build legacy standalone relay server binary" OFF)
option(CHESS_BUILD_APP "Build GUI application target" ON)
option(CHESS_BUILD_ENGINE_BENCH "Build engine benchmark CLI target" ON)
option(CHESS_ENABLE_ENGINE_TESTS "Register engine bench tests in CTest" ON)
set(CHESS_RELEASE_VERSION "${PROJECT_VERSION}" CACHE STRING "Version label used for release package naming")
set(_chess_default_release_arch "x64")
if(CMAKE_SYSTEM_PROCESSOR)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _chess_proc_lower)
    if(_chess_proc_lower MATCHES "^(x86_64|amd64)$")
        set(_chess_default_release_arch "x64")
    elseif(_chess_proc_lower MATCHES "^(aarch64|arm64)$")
        set(_chess_default_release_arch "arm64")
    else()
        set(_chess_default_release_arch "${_chess_proc_lower}")
    endif()
endif()
set(CHESS_RELEASE_ARCH "${_chess_default_release_arch}" CACHE STRING "Architecture label used for release package naming")

find_package(Threads REQUIRED)

set(CHESS_ENGINE_CORE_SOURCES
    src/engine/bitboard.c
    src/engine/movegen.c
    src/engine/search.c
)

# ------------------------------------------------------------
# Raylib dependency resolution
# ------------------------------------------------------------
if(CHESS_BUILD_APP)
    find_package(raylib QUIET)
    if(NOT raylib_FOUND)
        if(CHESS_FETCH_RAYLIB)
            include(FetchContent)
            set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

            FetchContent_Declare(
                raylib
                GIT_REPOSITORY https://github.com/raysan5/raylib.git
                GIT_TAG 5.5
            )
            FetchContent_MakeAvailable(raylib)
        else()
            message(FATAL_ERROR "raylib not found. Install it or set CHESS_FETCH_RAYLIB=ON")
        endif()
    endif()
endif()

# ------------------------------------------------------------
# Application target
# ------------------------------------------------------------
if(CHESS_BUILD_APP)
    set(CHESS_SOURCES
        src/main.c
        src/core/audio.c
        src/core/game_state.c
        src/core/main_loop.c
        ${CHESS_ENGINE_CORE_SOURCES}
        src/gui/font.c
        src/gui/renderer.c
        src/gui/ui_widgets.c
        src/gui/screens/menu_screen.c
        src/gui/screens/play_screen.c
        src/gui/screens/lobby_screen.c
        src/gui/screens/settings_screen.c
        src/network/client.c
        src/network/matchmaker.c
        src/data/secure_io.c
    )

    add_executable(chess_app ${CHESS_SOURCES})

    target_include_directories(chess_app PRIVATE include src/network)
    target_link_libraries(chess_app PRIVATE raylib Threads::Threads)

    if(CHESS_ENABLE_WARNINGS)
        if(MSVC)
            target_compile_options(chess_app PRIVATE /W4)
        else()
            target_compile_options(chess_app PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endif()

# Explicit Windows system libraries for MinGW/clang toolchains.
if(WIN32 AND CHESS_BUILD_APP)
    # Embed icon in chess_app.exe so Explorer/Desktop shows custom file icon.
    set(CHESS_ICON_PNG "${CMAKE_SOURCE_DIR}/assets/icons/app_icon.png")
    set(CHESS_ICON_ICO "${CMAKE_BINARY_DIR}/generated/chess_app_icon.ico")
    set(CHESS_ICON_RC "${CMAKE_BINARY_DIR}/generated/chess_app_icon.rc")

    if(EXISTS "${CHESS_ICON_PNG}")
        execute_process(
            COMMAND powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/cmake/png_to_ico.ps1"
                -InputPng "${CHESS_ICON_PNG}"
                -OutputIco "${CHESS_ICON_ICO}"
            RESULT_VARIABLE _icon_conv_result
            OUTPUT_VARIABLE _icon_conv_stdout
            ERROR_VARIABLE _icon_conv_stderr
        )

        if(_icon_conv_result EQUAL 0 AND EXISTS "${CHESS_ICON_ICO}")
            file(TO_CMAKE_PATH "${CHESS_ICON_ICO}" CHESS_ICON_ICO_RC_PATH)
            file(WRITE "${CHESS_ICON_RC}" "IDI_APP_ICON ICON \"${CHESS_ICON_ICO_RC_PATH}\"\n")
            target_sources(chess_app PRIVATE "${CHESS_ICON_RC}")
        else()
            message(WARNING
                "Could not generate embedded EXE icon from ${CHESS_ICON_PNG}.\n"
                "stdout:\n${_icon_conv_stdout}\n"
                "stderr:\n${_icon_conv_stderr}")
        endif()
    else()
        message(WARNING "Window icon PNG not found for EXE embedding: ${CHESS_ICON_PNG}")
    endif()

    # Build as a GUI app (no console window) on Windows.
    set_target_properties(chess_app PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(chess_app PRIVATE ws2_32 gdi32 winmm opengl32 crypt32)
endif()

# ------------------------------------------------------------
# One-file portable package target (Windows self-extracting EXE)
# ------------------------------------------------------------
if(WIN32 AND CHESS_BUILD_APP)
    find_program(CHESS_7Z_EXE
        NAMES 7z 7z.exe
        HINTS
            "$ENV{ProgramFiles}/7-Zip"
            "$ENV{ProgramW6432}/7-Zip"
    )

    if(NOT CHESS_7Z_EXE)
        message(WARNING "7z.exe was not found. Install 7-Zip (example: winget install --id 7zip.7zip).")
    endif()

    add_custom_target(chess_onefile
        COMMAND ${CMAKE_COMMAND}
            -DCHESS_APP_EXE=$<TARGET_FILE:chess_app>
            -DCHESS_SOURCE_ASSETS=${CMAKE_SOURCE_DIR}/assets
            -DCHESS_OUT_DIR=${CMAKE_BINARY_DIR}/release
            -DCHESS_7Z_EXE=${CHESS_7Z_EXE}
            -DCHESS_RELEASE_VERSION=${CHESS_RELEASE_VERSION}
            -P ${CMAKE_SOURCE_DIR}/cmake/build_onefile.cmake
        DEPENDS chess_app
        COMMENT "Building standalone one-file Chess executable"
        VERBATIM
    )
endif()

# ------------------------------------------------------------
# Linux release package target (.run + .tar.gz)
# ------------------------------------------------------------
if(UNIX AND NOT APPLE AND CHESS_BUILD_APP)
    add_custom_target(chess_linux_release
        COMMAND ${CMAKE_COMMAND}
            -DCHESS_APP_EXE=$<TARGET_FILE:chess_app>
            -DCHESS_SOURCE_ASSETS=${CMAKE_SOURCE_DIR}/assets
            -DCHESS_OUT_DIR=${CMAKE_BINARY_DIR}/release
            -DCHESS_RELEASE_VERSION=${CHESS_RELEASE_VERSION}
            -DCHESS_RELEASE_ARCH=${CHESS_RELEASE_ARCH}
            -P ${CMAKE_SOURCE_DIR}/cmake/build_linux_release.cmake
        DEPENDS chess_app
        COMMENT "Building Linux release bundles (.run + .tar.gz)"
        VERBATIM
    )
endif()

# ------------------------------------------------------------
# Engine benchmark target (no GUI/raylib dependency)
# ------------------------------------------------------------
if(CHESS_BUILD_ENGINE_BENCH)
    add_executable(chess_engine_bench
        tools/engine_bench.c
        ${CHESS_ENGINE_CORE_SOURCES}
    )
    target_include_directories(chess_engine_bench PRIVATE include)
    target_link_libraries(chess_engine_bench PRIVATE Threads::Threads)

    if(CHESS_ENABLE_WARNINGS)
        if(MSVC)
            target_compile_options(chess_engine_bench PRIVATE /W4)
        else()
            target_compile_options(chess_engine_bench PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endif()

include(CTest)
if(BUILD_TESTING AND CHESS_BUILD_ENGINE_BENCH AND CHESS_ENABLE_ENGINE_TESTS)
    add_test(
        NAME engine_bench_quick
        COMMAND chess_engine_bench --quick
    )
endif()

# ------------------------------------------------------------
# Optional legacy relay server executable (disabled by default)
# ------------------------------------------------------------
if(CHESS_BUILD_LEGACY_RELAY_SERVER)
    add_executable(chess_relay_server
        src/network/relay_server.c
        src/network/matchmaker.c
    )
    target_include_directories(chess_relay_server PRIVATE include src/network)
    if(WIN32)
        target_link_libraries(chess_relay_server PRIVATE ws2_32)
    endif()
endif()
