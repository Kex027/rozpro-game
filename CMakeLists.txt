cmake_minimum_required(VERSION 3.16)
project(GoldFever CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source directories
include_directories(common)

# ==============================================================================
# TARGET 1: Authoritative Game Server
# ==============================================================================
add_executable(GoldFeverServer server/server.cpp)

if(WIN32)
    # Link Winsock library for socket APIs on Windows
    target_link_libraries(GoldFeverServer ws2_32)
else()
    # Link pthread for threading on POSIX systems (Linux/macOS)
    target_link_libraries(GoldFeverServer pthread)
endif()

# ==============================================================================
# TARGET 2: Graphical Client (Raylib)
# ==============================================================================
if(WIN32)
    set(BUILD_CLIENT_DEFAULT ON)
else()
    set(BUILD_CLIENT_DEFAULT OFF) # Headless server by default on Linux/WSL
endif()

option(BUILD_CLIENT "Build the graphical client (requires graphics headers)" ${BUILD_CLIENT_DEFAULT})

if(BUILD_CLIENT)
    # We fetch and build Raylib directly from the release tarball to make setup painless.
    include(FetchContent)

    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

    message(STATUS "Fetching Raylib 5.0 source package...")
    FetchContent_Declare(
      raylib
      URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz
    )
    FetchContent_MakeAvailable(raylib)

    add_executable(GoldFeverClient client/client.cpp client/network_client.cpp)

    # Link Raylib library
    target_link_libraries(GoldFeverClient raylib)

    # Link platform-specific graphics and system dependencies
    if(WIN32)
        # Windows system dependencies
        target_link_libraries(GoldFeverClient ws2_32 winmm gdi32 user32 shell32 opengl32)
    else()
        # Linux system dependencies (usually requires X11 / OpenGL headers)
        find_package(Threads REQUIRED)
        target_link_libraries(GoldFeverClient Threads::Threads dl m)
    endif()
endif()
