# World Cup 98 — Native Linux Preservation Project

A native Linux reimplementation of **EA Sports World Cup 98** (1998 PC
release) that runs on your own copy of the original game files, with no Wine,
Proton or Windows runtime.

## Status

Early development. Nothing in the gameplay pipeline works yet. What exists
today:

- Parsers and tools for the original asset formats (`BIGF` archives, RefPack,
  `FSH` textures, `FFN` fonts, `FCDB` team/player database, `BNK`/`SCHl`/`CRD`
  audio, `.LAY` front-end layouts, `.MDL` models, animations).
- An engine foundation: SDL3 window, Vulkan renderer, INI settings and an
  OpenAL mixer.
- `wc98-native`, a window that presents an empty Vulkan frame. It proves the
  presentation path; no game scenes are ported yet.
- Offscreen demos that render real front-end screens, the trophy and the
  stadium from the original data.

Features are only marked done when a test has exercised them. See
[docs/STATUS.md](docs/STATUS.md) for confirmed findings and open questions, and
[CHANGELOG.md](CHANGELOG.md) for what changed.

## You need the original game

This repository contains **no game data**: no executables, assets, music,
commentary, textures or ISO images. You need your own legally obtained copy of
the original game. The original files are read only; they are never modified,
redistributed or needed at build time. World Cup 98 and its content are © their
respective owners. This project is not affiliated with or endorsed by EA.

Reverse-engineering findings in `docs/` are our own analysis, documented with
evidence (offsets, strings, captures). Format knowledge from community sources
is cited.

## Download

Releases ship a self-contained x86_64 AppImage, `wc98-native-linux-x86_64-<version>.AppImage`
with a `.sha256` sidecar. It needs only a Vulkan-capable driver on the host — the loader and
GPU drivers are deliberately never bundled.

```sh
chmod +x wc98-native-linux-x86_64-*.AppImage
./wc98-native-linux-x86_64-*.AppImage --smoke 3   # windowed self-check
```

Self-update checks this repository's GitHub releases, verifies SHA-256 against the sidecar,
smoke-tests the new build, swaps it in atomically and keeps the previous file as `.prev` for
rollback; it never touches game files, saves or `~/.config/wc98-native`:

```sh
packaging/update.sh --check ~/apps/wc98-native-linux-x86_64-0.1.0.AppImage
packaging/update.sh         ~/apps/wc98-native-linux-x86_64-0.1.0.AppImage
```

Repackage locally after a build with `packaging/linux/build-appimage.sh` (downloads pinned
appimagetool 1.9.1 and verifies its SHA-256; release AppImages are built on Ubuntu 24.04 for
distribution reach). Until the first release exists, build from source below.

## Requirements

- Linux on x86_64.
- A Vulkan 1.3 capable GPU and driver.
- To build: CMake ≥ 3.28, Ninja, a C++26 compiler (GCC 16 or Clang 23; a C++23
  preset exists for older toolchains), SDL3, Vulkan headers and loader with
  `glslc`, GLM, OpenAL, libpng and OpenSSL.

## Usage

```sh
build/debug/app/wc98-native --help
build/debug/app/wc98-native --windowed --width 1280 --height 960
```

Options: `--width N`, `--height N`, `--fullscreen`, `--windowed`, `--smoke N`
(present N frames and exit), `--version`. Settings are read from
`$XDG_CONFIG_HOME/wc98-native/settings.ini` (default `~/.config`; override the
file with `WC98_CONFIG`).

Asset tools (each prints its usage when run without arguments):

| Tool | Purpose |
| --- | --- |
| `wc98-inventory <game-dir> [report.json]` | Inventory the game files |
| `wc98-extract <game-dir-or-file> <output-dir>` | Unpack archives; convert textures, fonts and databases to PNG/JSON |
| `wc98-audio-extract <wc98-dir> [out-dir]` | Decode music, speech and sound banks |
| `wc98-dbexplorer <FE-dir> --teams` | Browse the team and player database |
| `wc98-exeinfo info <file>` | Inspect the original PE executables |
| `wc98-render-demo <png> [tree]` | Render a front-end screen offscreen to PNG |
| `wc98-model-view out.png <mdl-file>` | Render an `.MDL` model offscreen to PNG |
| `wc98-model-export MODEL --out DIR` | Export a model (with animation) to glTF |
| `wc98-anim-inspect list ARCHIVE` | List animation clips |

## Build from source

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset default
```

Unit tests need no game data. To also run the data-driven checks, configure
with `-DWC98_TREE=<path to your extracted game>`. The CI recipe, including how
SDL3 is built on Ubuntu 24.04, is in
[.github/workflows/build.yml](.github/workflows/build.yml).

## Documentation

- [docs/STATUS.md](docs/STATUS.md) — confirmed findings, hypotheses, unknowns.
- [docs/development/DEVELOPMENT_LOG.md](docs/development/DEVELOPMENT_LOG.md) — development history.
- [docs/development/MILESTONE4.md](docs/development/MILESTONE4.md) — rendering milestone notes.
- [docs/architecture/RENDERER.md](docs/architecture/RENDERER.md) — renderer design.
- [docs/asset-formats/](docs/asset-formats/) — asset formats, inventory and the team database.
- [docs/reverse-engineering/](docs/reverse-engineering/) — executable, audio, rendering and simulation analysis.
- [docs/research/](docs/research/) — external sources and asset research.
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) — third-party components.

## Layout

```text
core/       foundation library (byte I/O, logging)
formats/    asset format parsers
platform/   SDL3 window, XDG paths
renderer/   Vulkan renderer and shaders
config/     INI settings
audio/      OpenAL mixer
app/        wc98-native executable
tools/      standalone inspectors and extractors
tests/      unit, Vulkan and data-driven tests
docs/       research, reverse engineering, formats, architecture
```

## Contributing

Changes are accepted only as pull requests to this repository, reviewed and
merged by the maintainer. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Source-available, all rights reserved (code only — see [LICENSE](LICENSE)).
Original game data remains under its own copyright and is never redistributed
here. Third-party components keep their own licenses (see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
