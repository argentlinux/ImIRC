# MinGW-w64 + vcpkg toolchain for cross-building ImIRC on Linux.
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=packaging/cmake/mingw-vcpkg-toolchain.cmake ...
#
# Requires: g++-mingw-w64-x86-64-posix, mingw-w64-x86-64-dev, vcpkg

if(IMIRC_MINGW_VCPKG_TOOLCHAIN_LOADED)
	return()
endif()
set(IMIRC_MINGW_VCPKG_TOOLCHAIN_LOADED TRUE)

# --- locate vcpkg ---
if(NOT IMIRC_VCPKG_ROOT)
	if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
		set(IMIRC_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
	elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../vcpkg/scripts/buildsystems/vcpkg.cmake")
		get_filename_component(IMIRC_VCPKG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../vcpkg" ABSOLUTE)
	elseif(EXISTS "$ENV{HOME}/dev/vcpkg/scripts/buildsystems/vcpkg.cmake")
		set(IMIRC_VCPKG_ROOT "$ENV{HOME}/dev/vcpkg")
	elseif(EXISTS "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake")
		set(IMIRC_VCPKG_ROOT "$ENV{HOME}/vcpkg")
	else()
		message(FATAL_ERROR "vcpkg not found; set IMIRC_VCPKG_ROOT or VCPKG_ROOT")
	endif()
endif()

# Prefer POSIX-threaded MinGW (Boost.Thread).
if(EXISTS "/usr/bin/x86_64-w64-mingw32-g++-posix")
	set(_IMIRC_MINGW_CC "/usr/bin/x86_64-w64-mingw32-gcc-posix")
	set(_IMIRC_MINGW_CXX "/usr/bin/x86_64-w64-mingw32-g++-posix")
else()
	set(_IMIRC_MINGW_CC "x86_64-w64-mingw32-gcc")
	set(_IMIRC_MINGW_CXX "x86_64-w64-mingw32-g++")
endif()

set(CMAKE_SYSTEM_NAME Windows CACHE STRING "" FORCE)
set(CMAKE_SYSTEM_PROCESSOR x86_64 CACHE STRING "" FORCE)
set(CMAKE_C_COMPILER "${_IMIRC_MINGW_CC}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${_IMIRC_MINGW_CXX}" CACHE FILEPATH "" FORCE)
set(CMAKE_RC_COMPILER "x86_64-w64-mingw32-windres" CACHE FILEPATH "" FORCE)

set(CMAKE_FIND_ROOT_PATH "/usr/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Static Boost/OpenSSL + static MinGW runtime (libgcc/libstdc++/winpthread).
set(VCPKG_TARGET_TRIPLET "x64-mingw-static" CACHE STRING "" FORCE)
set(VCPKG_TARGET_ARCHITECTURE "x64")
set(VCPKG_CRT_LINKAGE "static")
set(VCPKG_LIBRARY_LINKAGE "static")
set(VCPKG_CMAKE_SYSTEM_NAME "MinGW")

get_filename_component(_IMIRC_TRIPLET_DIR "${CMAKE_CURRENT_LIST_DIR}/../vcpkg-triplets" ABSOLUTE)
set(VCPKG_OVERLAY_TRIPLETS "${_IMIRC_TRIPLET_DIR}" CACHE STRING "" FORCE)

# Skip vcpkg's chainload — this file already configured the MinGW compilers.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "" CACHE STRING "" FORCE)

# Static builds have nothing to applocal-copy; avoid a post-build powershell.exe call.
set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "" FORCE)
set(X_VCPKG_APPLOCAL_DEPS_INSTALL OFF CACHE BOOL "" FORCE)

include("${IMIRC_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
