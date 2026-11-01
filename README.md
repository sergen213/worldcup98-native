# World Cup 98 — Native Linux Preservation Project

A native Linux reimplementation of **EA Sports World Cup 98** (1998 PC release):
an original-engine-faithful, standalone game that runs on modern Linux
(x86_64, GNOME/Wayland, AMD Radeon tested) using **the user's own legally
obtained original game files**. No Wine, no Proton, no Windows runtime.

- Engine stack: native C++ (C++26), CMake + Ninja, SDL3 windowing/input,
  Vulkan rendering, native Linux audio.
- The original PC game shares the EA FIFA 97–99 engine family: `THRASH/`
  renderer back-ends (Direct3D 5 / 3dfx Glide / PowerVR), `DDRAW` +
  `WSOCK32` + `WINMM` imports, `EACSND.DLL` audio, and the
  `BIGF`/RefPack/`FSH`/`FFN`/`FCDB`/`BNK`/`MIDx`/`CRD`/`LAY` asset
  formats — all confirmed by direct inspection of the original files
  (see `docs/research/` and `docs/reverse-engineering/`).
- Sister project: **FIFA 99** native reimplementation
  (`fifa99-native`), whose reverse-engineered formats and tooling this
  project builds upon, validated file-by-file against World Cup 98 data.

## Status

Early phase. Current scope: verified asset inventory, executable
analysis, format tooling, engine foundation. See
[docs/development/DEVELOPMENT_LOG.md](docs/development/DEVELOPMENT_LOG.md)
for progress and [docs/](docs/) for the research findings with evidence.

Nothing in the gameplay pipeline is claimed to work yet; features are
only marked done when a test (unit, integration, or captured-behavior
comparison) has exercised them.

## Legal

- This repository contains **no game data** — no executables, assets,
  music, commentary, textures, or ISO images of the original game.
- Original game files are the source of truth and are never modified,
  redistributed, or required at build time; end users supply their own
  legally obtained copy through the launcher's import/validation flow.
- Reverse-engineering findings are our own analysis, documented with
  evidence (offsets, strings, captures). Format knowledge derived from
  community sources is cited.

## Build (development)

Requirements: CMake ≥ 3.28, Ninja, a C++26 toolchain (GCC 16 / Clang 23;
C++23 fallback preset provided). SDL3/Vulkan dependencies arrive with
the platform/renderer modules and will be documented when they are used.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset default
```

## Layout

```text
core/       foundation library (byte I/O; format parsers arrive here)
tools/      standalone inspectors/extractors (asset formats)
tests/      unit / integration / asset / simulation / regression tests
docs/       research, reverse-engineering, asset formats, architecture,
            compatibility, development log
src/        game modules (added when implemented — no empty stubs)
```

## License

MIT (code only — see `LICENSE`). Original game data remains under its
own copyright and is never redistributed here.
