#!/usr/bin/env bash
# Cross-build Windows packages with MinGW-w64 + vcpkg (x64-mingw-static):
#   - portable zip
#   - NSIS setup.exe (if makensis is installed)
#
# MSI is not produced here (WiX is Windows-only). NSIS is the usual Linux-friendly installer.
#
# Host deps (Ubuntu/Debian):
#   sudo apt install g++-mingw-w64-x86-64-posix mingw-w64-x86-64-dev cmake zip nsis
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-mingw}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
VERSION="${IMIRC_VERSION:-}"
TOOLCHAIN="${ROOT}/packaging/cmake/mingw-vcpkg-toolchain.cmake"

VCPKG_ROOT="${VCPKG_ROOT:-${HOME}/dev/vcpkg}"
if [[ ! -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]]; then
	VCPKG_ROOT="${HOME}/vcpkg"
fi
[[ -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]] || {
	echo "error: vcpkg not found (set VCPKG_ROOT)" >&2
	exit 1
}
export VCPKG_ROOT
export IMIRC_VCPKG_ROOT="${VCPKG_ROOT}"

log() { printf '==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "missing required tool: $1"; }

need cmake
need zip
need x86_64-w64-mingw32-g++
need x86_64-w64-mingw32-gcc
need x86_64-w64-mingw32-strip
[[ -f "$TOOLCHAIN" ]] || die "missing toolchain: $TOOLCHAIN"

mkdir -p "$DIST_DIR" "$BUILD_DIR"

# Drop a stale native (Linux) cache if present — that yields host c++ + MinGW headers
# and the winapifamily.h error.
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
	cached_cxx="$(grep -E '^CMAKE_CXX_COMPILER:(FILEPATH|STRING)=' "$BUILD_DIR/CMakeCache.txt" | head -1 | cut -d= -f2- || true)"
	if [[ -n "$cached_cxx" && "$cached_cxx" != *mingw* ]]; then
		log "Clearing non-MinGW CMake cache (was: $cached_cxx)"
		rm -f "$BUILD_DIR/CMakeCache.txt"
		rm -rf "$BUILD_DIR/CMakeFiles"
	fi
fi

log "Configuring MinGW Release in $BUILD_DIR"
cmake -S "$ROOT" -B "$BUILD_DIR" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
	-DIMIRC_VCPKG_ROOT="${VCPKG_ROOT}" \
	-DOPENSSL_USE_STATIC_LIBS=ON \
	-DBoost_USE_STATIC_LIBS=ON

if [[ -z "$VERSION" ]]; then
	VERSION="$(cmake -S "$ROOT" -B "$BUILD_DIR" -N 2>/dev/null | sed -n 's/.*CMAKE_PROJECT_VERSION:STATIC=//p' | head -1)"
	VERSION="${VERSION:-0.2.0}"
fi
log "Version ${VERSION} (windows-x64)"

cxx="$(grep -E '^CMAKE_CXX_COMPILER:(FILEPATH|STRING)=' "$BUILD_DIR/CMakeCache.txt" | head -1 | cut -d= -f2- || true)"
[[ "$cxx" == *mingw* ]] || die "CMake did not select a MinGW compiler (got: ${cxx:-empty})"
log "Compiler: $cxx"

log "Building"
cmake --build "$BUILD_DIR" --config Release -j"$JOBS" --target client

BIN=""
for cand in \
	"$BUILD_DIR/bin/imirc.exe" \
	"$BUILD_DIR/bin/Release/imirc.exe" \
	"$BUILD_DIR/client/src/imirc.exe"
do
	if [[ -f "$cand" ]]; then
		BIN="$cand"
		break
	fi
done
[[ -n "$BIN" ]] || die "imirc.exe not found under $BUILD_DIR"

log "Stripping $BIN"
x86_64-w64-mingw32-strip --strip-unneeded "$BIN" || true

# ---------------------------------------------------------------------------
# Portable zip
# ---------------------------------------------------------------------------
PORTABLE_NAME="ImIRC-${VERSION}-windows-x64"
PORTABLE_DIR="$DIST_DIR/$PORTABLE_NAME"
rm -rf "$PORTABLE_DIR"
mkdir -p "$PORTABLE_DIR/fonts"
cp -a "$BIN" "$PORTABLE_DIR/imirc.exe"
if [[ -d "$BUILD_DIR/bin/fonts" ]]; then
	cp -a "$BUILD_DIR/bin/fonts/." "$PORTABLE_DIR/fonts/"
elif [[ -d "$ROOT/client/src/fonts" ]]; then
	cp -a "$ROOT/client/src/fonts/." "$PORTABLE_DIR/fonts/"
fi
cp -a "$ROOT/LICENSE" "$PORTABLE_DIR/"
[[ -f "$ROOT/README.md" ]] && cp -a "$ROOT/README.md" "$PORTABLE_DIR/" || true

ZIP_OUT="$DIST_DIR/${PORTABLE_NAME}.zip"
rm -f "$ZIP_OUT"
(cd "$DIST_DIR" && zip -r -9 "$ZIP_OUT" "$PORTABLE_NAME")
log "Wrote $ZIP_OUT"

# ---------------------------------------------------------------------------
# NSIS installer
# ---------------------------------------------------------------------------
if command -v makensis >/dev/null 2>&1; then
	log "Building NSIS installer"
	SETUP_OUT="$DIST_DIR/ImIRC-${VERSION}-windows-x64-setup.exe"
	makensis -V2 \
		-DIMIRC_VERSION="${VERSION}" \
		-DIMIRC_STAGE="${PORTABLE_DIR}" \
		-DIMIRC_OUTFILE="${SETUP_OUT}" \
		"$ROOT/packaging/imirc.nsi"
	log "Wrote $SETUP_OUT"
else
	log "makensis not found — skipping NSIS setup.exe (sudo apt install nsis)"
fi

log "Done. Windows artifacts in $DIST_DIR:"
ls -lh "$DIST_DIR"/*windows* 2>/dev/null | sed 's/^/  /' || ls -lh "$DIST_DIR" | sed -n '1,30p' | sed 's/^/  /'
