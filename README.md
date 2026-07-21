# ImIRC

Lightweight desktop IRC client (Dear ImGui + GLFW + Boost.Asio).

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
2. `config/` next to the binary when that path is writable (local builds, portable tar), else
3. `$XDG_CONFIG_HOME/imirc` or `~/.config/imirc` (AppImage and system packages)

## Packaging

Generate **portable tar.gz**, **.deb**, **.rpm** (if `rpmbuild` is available), and **AppImage**:

```bash
./scripts/package.sh
```

Artifacts land in `dist/`:

| Artifact | Notes |
|---|---|
| `ImIRC-<ver>-linux-<arch>.tar.gz` | Portable: extract and run `./imirc` |
| `imirc_<ver>_amd64.deb` | Debian/Ubuntu (`/opt/imirc`, `/usr/bin/imirc`) |
| `imirc-<ver>-*.rpm` | Fedora/RHEL (needs `rpmbuild`, or `alien`) |
| `ImIRC-<ver>-<arch>.AppImage` | Needs network once to fetch `linuxdeploy` |

Environment knobs:

- `BUILD_DIR` — CMake build dir (default `./build`)
- `DIST_DIR` — output dir (default `./dist`)
- `VCPKG_ROOT` — vcpkg checkout
- `SKIP_APPIMAGE=1` — skip AppImage
- `JOBS` — parallel build jobs

Individual CPack generators after a Release build:

```bash
cmake --build build --config Release
cd build && cpack -G DEB
cd build && cpack -G RPM   # requires rpmbuild
```

## License

Unlicense (public domain) — see [LICENSE](LICENSE).
