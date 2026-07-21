# ImIRC

Lightweight desktop IRC client (Dear ImGui + GLFW + Boost.Asio).

**Current version:** 0.2.0

## Build

```bash
# With vcpkg (recommended)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/dev/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build -j$(nproc)
./build/bin/imirc
```

Runtime assets (`fonts/`) are copied next to the binary automatically.

## Config

Identity and server profiles are stored as JSON under:

1. `$IMIRC_CONFIG_DIR` if set, else
2. `config/` next to the binary when that path is writable (local builds, portable archives), else
3. Linux: `$XDG_CONFIG_HOME/imirc` or `~/.config/imirc` (AppImage / system packages)  
   Windows: `%APPDATA%\imirc`

## Packaging

| Script | Output |
|---|---|
| `./scripts/package.sh` | Linux native: tar.gz, deb, rpm, AppImage |
| `./scripts/package-linux-arm64.sh` | Linux arm64 via Docker: tar.gz, deb, rpm |
| `./scripts/package-windows.sh` | Windows via MinGW: zip + NSIS setup.exe |

Artifacts land in `dist/`.

### Linux (x86_64 / native)

```bash
./scripts/package.sh
```

| Artifact | Notes |
|---|---|
| `ImIRC-<ver>-linux-<arch>.tar.gz` | Portable: extract and run `./imirc` |
| `imirc_<ver>_amd64.deb` / `imirc_<ver>_arm64.deb` | Debian/Ubuntu (`/opt/imirc`, `/usr/bin/imirc`) |
| `imirc-<ver>-*.rpm` | Fedora/RHEL (needs `rpmbuild`, or `alien`) |
| `ImIRC-<ver>-<arch>.AppImage` | Needs network once to fetch `linuxdeploy` |

Environment knobs:

- `BUILD_DIR` — CMake build dir (default `./build`)
- `DIST_DIR` — output dir (default `./dist`)
- `VCPKG_ROOT` — vcpkg checkout
- `SKIP_APPIMAGE=1` — skip AppImage
- `JOBS` — parallel build jobs

### Linux arm64 (Docker)

```bash
# one-time (if arm64 containers fail with "exec format error")
docker run --privileged --rm tonistiigi/binfmt --install arm64

./scripts/package-linux-arm64.sh
```

Produces `ImIRC-*-linux-aarch64.tar.gz`, `imirc_*_arm64.deb`, and an aarch64 `.rpm`. AppImage is skipped by default under qemu (`SKIP_APPIMAGE=1`). First run bootstraps vcpkg into Docker volumes and is slow.

### Windows (MinGW cross-compile)

```bash
sudo apt install g++-mingw-w64-x86-64-posix mingw-w64-x86-64-dev cmake zip nsis
./scripts/package-windows.sh
```

| Artifact | Notes |
|---|---|
| `ImIRC-<ver>-windows-x64.zip` | Portable: extract and run `imirc.exe` |
| `ImIRC-<ver>-windows-x64-setup.exe` | NSIS installer (Start Menu + Desktop shortcuts) |

Static Boost/OpenSSL/MinGW runtime. MSI is not generated (use NSIS).

## License

Unlicense (public domain) — see [LICENSE](LICENSE).
