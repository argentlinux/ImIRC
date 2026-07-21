#!/usr/bin/env bash
# Build ImIRC packages: portable tar.gz, .deb, .rpm (if rpmbuild exists), AppImage.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
VERSION="${IMIRC_VERSION:-}"
ARCH="$(uname -m)"

VCPKG_ROOT="${VCPKG_ROOT:-${HOME}/dev/vcpkg}"
if [[ ! -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]]; then
	VCPKG_ROOT="${HOME}/vcpkg"
fi
TOOLCHAIN=""
if [[ -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]]; then
	TOOLCHAIN="-DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
fi

log() { printf '==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "missing required tool: $1"; }

need cmake
need cpack
need strip

mkdir -p "$DIST_DIR" "$BUILD_DIR"

log "Configuring Release build in $BUILD_DIR"
cmake -S "$ROOT" -B "$BUILD_DIR" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=/usr \
	${TOOLCHAIN}

if [[ -z "$VERSION" ]]; then
	VERSION="$(cmake -S "$ROOT" -B "$BUILD_DIR" -N 2>/dev/null | sed -n 's/.*CMAKE_PROJECT_VERSION:STATIC=//p' | head -1)"
	VERSION="${VERSION:-0.1.0}"
fi
log "Version ${VERSION} (${ARCH})"

log "Building"
cmake --build "$BUILD_DIR" --config Release -j"$JOBS" --target client

BIN="$BUILD_DIR/bin/imirc"
[[ -x "$BIN" ]] || die "binary not found at $BIN"

log "Stripping $BIN"
strip --strip-unneeded "$BIN" || true

# ---------------------------------------------------------------------------
# Portable tar.gz (self-contained folder)
# ---------------------------------------------------------------------------
PORTABLE_NAME="ImIRC-${VERSION}-linux-${ARCH}"
PORTABLE_DIR="$DIST_DIR/$PORTABLE_NAME"
rm -rf "$PORTABLE_DIR"
mkdir -p "$PORTABLE_DIR/fonts"
cp -a "$BIN" "$PORTABLE_DIR/imirc"
if [[ -d "$BUILD_DIR/bin/fonts" ]]; then
	cp -a "$BUILD_DIR/bin/fonts/." "$PORTABLE_DIR/fonts/"
elif [[ -d "$ROOT/client/src/fonts" ]]; then
	cp -a "$ROOT/client/src/fonts/." "$PORTABLE_DIR/fonts/"
fi
cp -a "$ROOT/LICENSE" "$PORTABLE_DIR/"
[[ -f "$ROOT/README.md" ]] && cp -a "$ROOT/README.md" "$PORTABLE_DIR/" || true
chmod +x "$PORTABLE_DIR/imirc"

log "Creating portable archive"
tar -C "$DIST_DIR" -czf "$DIST_DIR/${PORTABLE_NAME}.tar.gz" "$PORTABLE_NAME"
log "Wrote $DIST_DIR/${PORTABLE_NAME}.tar.gz"

# ---------------------------------------------------------------------------
# DEB / RPM via CPack (/opt/imirc + /usr/bin symlink + desktop files)
# ---------------------------------------------------------------------------
log "Running CPack (DEB)"
(cd "$BUILD_DIR" && cpack -G DEB -C Release)
if command -v rpmbuild >/dev/null 2>&1; then
	log "Running CPack (RPM)"
	(cd "$BUILD_DIR" && cpack -G RPM -C Release) || log "RPM packaging failed (continuing)"
else
	log "rpmbuild not found — skipping RPM (install rpm-build to enable)"
	if command -v alien >/dev/null 2>&1; then
		DEB="$(ls -1t "$BUILD_DIR"/imirc_*.deb "$BUILD_DIR"/*.deb 2>/dev/null | head -1 || true)"
		if [[ -n "${DEB:-}" ]]; then
			log "Converting DEB → RPM with alien"
			(cd "$DIST_DIR" && alien -r --scripts "$DEB") || true
		fi
	fi
fi

# Move CPack artifacts into dist/
shopt -s nullglob
for f in "$BUILD_DIR"/*.deb "$BUILD_DIR"/*.rpm "$BUILD_DIR"/*.tar.gz; do
	# Don't overwrite our portable tarball name collision carelessly
	base="$(basename "$f")"
	if [[ "$base" == "${PORTABLE_NAME}.tar.gz" ]]; then
		continue
	fi
	mv -f "$f" "$DIST_DIR/"
	log "Moved $base → dist/"
done
shopt -u nullglob

# ---------------------------------------------------------------------------
# AppImage (linuxdeploy)
# ---------------------------------------------------------------------------
TOOLS_DIR="$DIST_DIR/tools"
mkdir -p "$TOOLS_DIR"
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage"
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"

fetch_linuxdeploy() {
	if [[ -x "$LINUXDEPLOY" ]]; then
		return 0
	fi
	need curl
	log "Downloading linuxdeploy"
	curl -fsSL -o "$LINUXDEPLOY" "$LINUXDEPLOY_URL"
	chmod +x "$LINUXDEPLOY"
}

build_appimage() {
	fetch_linuxdeploy || return 1
	local APPDIR="$DIST_DIR/AppDir"
	rm -rf "$APPDIR"
	mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
		"$APPDIR/usr/share/icons/hicolor/256x256/apps" \
		"$APPDIR/opt/imirc/fonts"

	# Keep binary + fonts together (program_location()/fonts)
	cp -a "$BIN" "$APPDIR/opt/imirc/imirc"
	if [[ -d "$PORTABLE_DIR/fonts" ]]; then
		cp -a "$PORTABLE_DIR/fonts/." "$APPDIR/opt/imirc/fonts/"
	fi
	ln -sfn ../../opt/imirc/imirc "$APPDIR/usr/bin/imirc"

	cp -a "$ROOT/packaging/imirc.desktop" "$APPDIR/usr/share/applications/"
	cp -a "$ROOT/packaging/imirc.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/imirc.png"
	# AppImage root icon + desktop
	cp -a "$ROOT/packaging/imirc.png" "$APPDIR/imirc.png"
	cp -a "$ROOT/packaging/imirc.desktop" "$APPDIR/imirc.desktop"
	# Fix Exec for AppImage desktop
	sed -i 's|^Exec=.*|Exec=imirc|' "$APPDIR/imirc.desktop"
	echo 'AppRun' > /dev/null
	ln -sfn usr/bin/imirc "$APPDIR/AppRun"
	chmod +x "$APPDIR/AppRun"

	log "Building AppImage"
	export OUTPUT="$DIST_DIR/ImIRC-${VERSION}-${ARCH}.AppImage"
	# linuxdeploy may try to bundle system libs; our binary is mostly static.
	if "$LINUXDEPLOY" --appdir "$APPDIR" \
		--executable "$APPDIR/usr/bin/imirc" \
		--desktop-file "$APPDIR/imirc.desktop" \
		--icon-file "$APPDIR/imirc.png" \
		--output appimage; then
		# linuxdeploy writes into cwd sometimes
		shopt -s nullglob
		for img in ./*.AppImage "$BUILD_DIR"/*.AppImage "$DIST_DIR"/*.AppImage; do
			[[ -f "$img" ]] || continue
			base="$(basename "$img")"
			if [[ "$img" != "$DIST_DIR/$base" ]]; then
				mv -f "$img" "$DIST_DIR/"
			fi
		done
		shopt -u nullglob
		# Normalize name if linuxdeploy used its own
		if [[ ! -f "$OUTPUT" ]]; then
			found="$(ls -1t "$DIST_DIR"/*ImIRC*.AppImage "$DIST_DIR"/*imirc*.AppImage 2>/dev/null | head -1 || true)"
			if [[ -n "${found:-}" && "$found" != "$OUTPUT" ]]; then
				mv -f "$found" "$OUTPUT"
			fi
		fi
		log "Wrote ${OUTPUT}"
	else
		log "linuxdeploy failed — creating a simple AppImage-like payload skipped"
		return 1
	fi
}

if [[ "${SKIP_APPIMAGE:-0}" != "1" ]]; then
	build_appimage || log "AppImage step skipped/failed"
else
	log "SKIP_APPIMAGE=1 — skipping AppImage"
fi

log "Done. Artifacts in $DIST_DIR:"
# Keep AppDir only when debugging
if [[ "${KEEP_APPDIR:-0}" != "1" ]]; then
	rm -rf "$DIST_DIR/AppDir"
fi
ls -lh "$DIST_DIR" | sed -n '1,20p' | sed 's/^/  /'
