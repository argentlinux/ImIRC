#!/usr/bin/env bash
# Build Linux aarch64 packages inside Docker (qemu when host is x86_64):
#   portable tar.gz, .deb, .rpm
#
# Host deps:
#   docker + buildx; arm64 emulation via:
#     docker run --privileged --rm tonistiigi/binfmt --install arm64
#
# Usage:
#   ./scripts/package-linux-arm64.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
IMAGE="${IMIRC_DOCKER_IMAGE:-imirc-build:linux}"
PLATFORM="${IMIRC_DOCKER_PLATFORM:-linux/arm64}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
# AppImage needs FUSE and is flaky under qemu — off by default in Docker.
SKIP_APPIMAGE="${SKIP_APPIMAGE:-1}"
VCPKG_VOLUME="${IMIRC_VCPKG_VOLUME:-imirc-vcpkg-arm64}"
VCPKG_CACHE_VOLUME="${IMIRC_VCPKG_CACHE_VOLUME:-imirc-vcpkg-cache-arm64}"

log() { printf '==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "missing required tool: $1"; }

need docker
docker buildx version >/dev/null 2>&1 || die "docker buildx is required"

ensure_arm64_emu() {
	if docker run --rm --platform linux/arm64 "$IMAGE" true >/dev/null 2>&1; then
		return 0
	fi
	# Image may not exist yet — probe with ubuntu.
	if docker run --rm --platform linux/arm64 ubuntu:24.04 uname -m 2>/dev/null | grep -q aarch64; then
		return 0
	fi
	log "Installing qemu binfmt for arm64 (privileged one-shot)"
	docker run --privileged --rm tonistiigi/binfmt --install arm64 >/dev/null
	docker run --rm --platform linux/arm64 ubuntu:24.04 uname -m | grep -q aarch64 \
		|| die "arm64 emulation still unavailable after binfmt install"
}

mkdir -p "$DIST_DIR"

log "Ensuring arm64 emulation"
ensure_arm64_emu

log "Building Docker image ${IMAGE} (${PLATFORM})"
docker buildx build \
	--platform "${PLATFORM}" \
	--load \
	-t "${IMAGE}" \
	-f "${ROOT}/packaging/docker/Dockerfile.linux" \
	"${ROOT}/packaging/docker"

docker volume create "${VCPKG_VOLUME}" >/dev/null
docker volume create "${VCPKG_CACHE_VOLUME}" >/dev/null

log "Running arm64 package build in container (first vcpkg install is slow under qemu)"
# Mount source read-write so build-arm64/ and dist/ appear on the host.
# Keep host build/ untouched via BUILD_DIR=build-arm64.
docker run --rm \
	--platform "${PLATFORM}" \
	-e JOBS="${JOBS}" \
	-e SKIP_APPIMAGE="${SKIP_APPIMAGE}" \
	-e BUILD_DIR=/src/build-arm64 \
	-e DIST_DIR=/src/dist \
	-e VCPKG_ROOT=/opt/vcpkg \
	-e VCPKG_DEFAULT_BINARY_CACHE=/opt/vcpkg-cache \
	-e VCPKG_FORCE_SYSTEM_BINARIES=1 \
	-v "${ROOT}:/src" \
	-v "${VCPKG_VOLUME}:/opt/vcpkg" \
	-v "${VCPKG_CACHE_VOLUME}:/opt/vcpkg-cache" \
	-w /src \
	"${IMAGE}" \
	bash -lc '
		set -euo pipefail
		echo "==> container arch: $(uname -m)"
		[[ "$(uname -m)" == "aarch64" ]] || { echo "error: expected aarch64, got $(uname -m)" >&2; exit 1; }

		if [[ ! -x /opt/vcpkg/vcpkg ]]; then
			echo "==> Bootstrapping vcpkg in volume"
			if [[ ! -d /opt/vcpkg/.git ]]; then
				rm -rf /opt/vcpkg/*
				git clone --depth 1 https://github.com/microsoft/vcpkg /opt/vcpkg
			fi
			/opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
		fi
		mkdir -p /opt/vcpkg-cache /src/dist /src/build-arm64
		./scripts/package.sh
	'

log "Arm64 artifacts in $DIST_DIR:"
ls -lh "$DIST_DIR"/*aarch64* "$DIST_DIR"/*arm64* 2>/dev/null | sed 's/^/  /' || \
	ls -lh "$DIST_DIR" | sed -n '1,30p' | sed 's/^/  /'
