# Project overlay: static libraries + static MinGW runtime (libgcc/libstdc++/winpthread).
# Used by scripts/package-windows.sh via VCPKG_OVERLAY_TRIPLETS.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_ENV_PASSTHROUGH PATH)
set(VCPKG_CMAKE_SYSTEM_NAME MinGW)
