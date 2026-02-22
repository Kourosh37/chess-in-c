cmake_minimum_required(VERSION 3.20)

project(ChessProject LANGUAGES C)

# ------------------------------------------------------------
# Global toolchain settings
# ------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CHESS_FETCH_RAYLIB "Fetch raylib automatically when not installed" ON)
option(CHESS_ENABLE_WARNINGS "Enable common warning flags" ON)

find_package(Threads REQUIRED)

# ------------------------------------------------------------
# Raylib dependency resolution
# ------------------------------------------------------------
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

# ------------------------------------------------------------
# Application target
# ------------------------------------------------------------
set(CHESS_SOURCES
    src/main.c
    src/core/audio.c
    src/core/game_state.c
    src/core/main_loop.c
    src/engine/bitboard.c
    src/engine/movegen.c
    src/engine/search.c
    src/gui/font.c
    src/gui/renderer.c
    src/gui/ui_widgets.c
    src/gui/screens/menu_screen.c
    src/gui/screens/play_screen.c
    src/gui/screens/lobby_screen.c
    src/gui/screens/settings_screen.c
    src/network/client.c
    src/network/matchmaker.c
    src/data/profile_mgr.c
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

# Explicit Windows system libraries for MinGW/clang toolchains.
if(WIN32)
    target_link_libraries(chess_app PRIVATE ws2_32 gdi32 winmm opengl32)
endif()

# ------------------------------------------------------------
# Optional relay server executable (TCP room relay)
# ------------------------------------------------------------
add_executable(chess_relay_server
    src/network/relay_server.c
    src/network/matchmaker.c
)
target_include_directories(chess_relay_server PRIVATE include src/network)
if(WIN32)
    target_link_libraries(chess_relay_server PRIVATE ws2_32)
endif()
