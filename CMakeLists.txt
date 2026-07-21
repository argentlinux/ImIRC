cmake_minimum_required(VERSION 3.16)

set(PROJECT "imirc")
project(${PROJECT} VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMIRC_VERSION "${PROJECT_VERSION}")

if(MSVC)
  add_definitions(/MP)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
    add_definitions(-DLINUX)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(OpenGL)
find_package(OpenSSL REQUIRED)

set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME    OFF)
find_package(Boost REQUIRED COMPONENTS
    system
    thread
    log
    log_setup
    date_time
    program_options
)

set(GLAD_PREFIX
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/glad"
    CACHE STRING "Path to glad"
)

set(GLFW_PREFIX
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/glfw"
    CACHE STRING "Path to GLFW"
)
set(DEAR_IMGUI_PREFIX
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui"
    CACHE STRING "Path to Dear ImGui"
)

add_library(glad "${GLAD_PREFIX}/glad.c")
include_directories(
    "${GLAD_PREFIX}/"
)

set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)
set(GLFW_INSTALL OFF)
add_subdirectory(${GLFW_PREFIX})
include_directories(
    "${GLFW_PREFIX}/include"
)

list(APPEND imguiSrc
    ${DEAR_IMGUI_PREFIX}/imgui.cpp
    ${DEAR_IMGUI_PREFIX}/misc/cpp/imgui_stdlib.cpp
    ${DEAR_IMGUI_PREFIX}/imgui_draw.cpp
    ${DEAR_IMGUI_PREFIX}/imgui_tables.cpp
    ${DEAR_IMGUI_PREFIX}/imgui_widgets.cpp
    ${DEAR_IMGUI_PREFIX}/backends/imgui_impl_glfw.cpp
    ${DEAR_IMGUI_PREFIX}/backends/imgui_impl_opengl3.cpp
)
include_directories(
    "${DEAR_IMGUI_PREFIX}"
)

if (MSVC)
    add_compile_definitions(PLATFORM_MSVC)
    add_compile_definitions(_WIN32_WINNT=0x0603)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
endif (MSVC)

if (UNIX)
    add_compile_definitions(PLATFORM_UNIX)
endif (UNIX)

# Prefer std::filesystem with Boost.DLL (avoids needing boost-filesystem as a direct dep).
add_compile_definitions(BOOST_DLL_USE_STD_FS)

include_directories(${Boost_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/util/src)

add_subdirectory(util)
add_subdirectory(client)

# Install + CPack (deb / rpm / tar.gz). AppImage is built by scripts/package.sh.
include("${CMAKE_SOURCE_DIR}/packaging/CPackConfig.cmake")
