# Milestone 8 — launcher stack port (config / assets / launcher)

Ported the FIFA 99 sibling's launcher stack (`fifa99-native` `config/`, `assets/`, `launcher/`)
into this repo, adapted to World Cup 98 realities. Executed 2026-11-03 by agent LauncherPort;
verification evidence below is from real runs on this machine (GCC 16.2.1, Qt 6.12, SDL 3.4.18).

## What was ported

### config/ — no changes needed (already in the repo)
`wc98_config` was landed earlier (commits `ded0424`, `a347f75`: INI settings store + control-binding
layer from the 1998 manual table). The contract's option keys (stadium, dayNight, gameSpeed,
detailFields/detailStadium, halfLength, clockStyle, volumes, cameraModes, lang) ride the existing
`[gameplay]` pass-through (`Settings::gameplay`, "passed through to the game options untouched").
The launcher's Match options page writes those keys; audio volumes are typed `AudioSettings` fields.
Note: parse lower-cases INI keys (settings.cpp), so `[gameplay]` keys are canonical lowercase
(`daynight`, `gamespeed`, `cameramode0`, …) — the page uses exactly those spellings.

### assets/ → `wc98_assets` (new, this milestone)
- `include/wc98/assets/install.hpp` + `src/install.cpp` — WC98 tree discovery: `detect()` classifies a
  directory as `GameTree` when it holds `FIFAWC.EXE` + one of AUDIO/FE/INGAME/COMMON (f99's DATA/-based
  CdTree/InstalledTree/DataTree/PatchDir layouts do not exist in WC98); common-location search
  (`~/Games`, `/run/media/$USER`, `$XDG_DATA_HOME/wc98-native/game`), `normalizeGamePath`,
  `isExecutableName`, streaming SHA-256 (OpenSSL EVP, same provider as tools/inventory).
- `include/wc98/assets/vfs.hpp` + `src/vfs.cpp` — layered case-insensitive read-only VFS. WC98 delta: the
  tree root IS the data root; layers are Base/User (f99's Patch layer dropped); BIG-member paths
  (`INGAME/STADIUM/PARI___D.BIG:stad_n`) and RefPack payloads resolve through `wc98_formats`.
- `include/wc98/assets/manifest.hpp` + `src/manifest.cpp` + `src/wc98_manifest.inc` — embedded manifest of
  **3,596 Win95 CD game-data files** (path, size, SHA-256; 371 MiB) with feature-group classification and
  a playable/incomplete report. f99's Base/Patch `Origin` collapsed to one edition.
- `src/sha256.hpp` — private streaming EVP wrapper (verbatim port).

### launcher/ → `wc98-launcher` + `wc98_launcher_ui` (new, this milestone)
Qt 6 Widgets port of the f99 launcher: `main.cpp` (CLI modes `--self-test`, `--page NAME`,
`--screenshot-pages DIR`, `--version` preserved), `mainwindow.{hpp,cpp}` (nav + status bar + Save/Play;
Play launches the `wc98-native` executable found next to the launcher or in PATH, logs under
`$XDG_STATE_HOME/wc98-native/log`), `common.{hpp,cpp}` (paths, dlopen'd Vulkan device probe, OpenAL device
list, SDL version, diagnostics report), `keymap.hpp` (**verbatim** — Qt key press → SDL3 scancode name; the
WC98 input layer (`input/`, `wc98::input`) consumes exactly f99's `config::Action` + binding syntax, so no
adaptation was needed), `controls.cpp` (per-device binding table, gamepad hot-plug, capture dialog —
verbatim rename; help text matches the repo's 1998-manual defaults).
Pages: **Game files** (folder list, auto-detect, Validate → manifest report tree + playable summary),
**Match options** (the pass-7 OPTIONS.SAV fields as raw numerics mirrored to `[gameplay]`, plus
Import/Export of `USER/OPTIONS.SAV` through `wc98::formats::OptionsSave`, preserving all unnamed bytes
verbatim on export), **Graphics**, **Audio**, **Logs**, **Troubleshooting** (ported; language/patch widgets
removed as WC98 has no such surface).

## Dropped (and why)

- **ISO/ZIP reading + extraction (`iso.cpp`, libarchive)** — not trivially reusable: it needs libarchive, a
  dependency this project does not carry ("no new external deps"). Detection of CD001/PK signatures is kept so
  the launcher can *tell* the user to extract; all authoring/extraction UI and `ExtractResult` code dropped.
- **Patch 1.1 surface** (`patchDataFiles`, `mountPatch`, `Origin::Patch`, patch widgets) — WC98 has no official
  patch stream in scope.
- **Language selection** (f99's `game.language`, FCDB_<lang>.DBI picks) — wc98_config deliberately has no language
  field; the OPTIONS.SAV `lang` byte is exposed on the Match options page instead.
- **Saves page / mltset.dat import** — f99-specific save set; WC98's writable state (OPTIONS.SAV) is handled by the
  Match options page, tournament saves live in `USER/` per docs/reverse-engineering/SAVES.md and need no launcher UI.
- **f99 `europe_manifest.inc`** — replaced by `wc98_manifest.inc`, generated from this project's verified dev sample
  tree (see regeneration below).

## Manifest regeneration

From a verified extracted tree (`$WC98_TREE`): walk the game-data dirs AUDIO COMMON FE FE2 FEPACK INGAME VIDEO,
skip names with an executable/script extension (`isExecutableName` list), emit sorted lines
`{"<UPPER generic path>", <size>u, "<sha256-hex>"},`. The dev sample used for 0.1.0 was
`$WC98_TREE` (the extracted dev sample tree on the workstation; 3,596 entries / 371 MiB; totals cross-check
the 3,624-game-file / 378.1 MiB share in docs/research/ASSET_INVENTORY.md §Overview minus executables and redist trees).

## Verification evidence (all on this machine)

- `-fsyntax-only` with `-std=c++26 -Wall -Wextra -Werror`: all 8 new TUs clean
  (`wc98_launcher_ui` needs the same `-Wno-missing-field-initializers` f99 uses for the `Vk*Info{sType}` idiom).
- **Full link** of `wc98-launcher` outside CMake (all launcher+config+assets+formats TUs, Qt6/SDL3/OpenAL/libpng/crypto/dl):
  binary at `/tmp/lport-build/wc98-launcher`. Note: objects need `-fPIC` for the GCC-16 + Qt protected-symbol
  copy-reloc rule; CMake's Qt integration sets this.
- `WC98_TREE=… ./wc98-launcher --self-test` → **exit 0, all PASS**: settings serialize/parse round trip incl.
  `[gameplay]` keys and an unknown section, atomic save+reload, OPTIONS.SAV synthetic magic/lang parse + field
  sync round trip, `detect(WC98_TREE) = game-tree (FIFAWC.EXE + AUDIO/)`, Vulkan/OpenAL/SDL diagnostics.
- `QT_QPA_PLATFORM=offscreen ./wc98-launcher --screenshot-pages shots` → all 7 pages rendered (00-game-files …
  06-troubleshooting), exit 0.
- `assets::validate($WC98_TREE)` → **3,596 files: 0 missing, 0 mismatch,
  0 unknown-variant, playable=true, variant "World Cup 98 (Win95 CD)"** (~0.2 s wall).
- Synthetic tamper classify (drop `COMMON/FCDB.DBI`, flip one FE hash byte) → Missing=1, Mismatch=1,
  playable=false, correct variant string.

## Integration lines for Main (apply verbatim; LauncherPort did not touch these files)

Root `CMakeLists.txt` — two additions:

```cmake
add_subdirectory(assets)   # after add_subdirectory(formats): wc98_assets (discovery, manifest, VFS)
...
add_subdirectory(launcher) # after add_subdirectory(app): wc98-launcher; skipped with a message when Qt6 Widgets is absent
```

`tests/CMakeLists.txt` — register the launcher self-test (no new test TU needed; `--self-test` passes without a tree,
so no 77-skip required):

```cmake
# M8: launcher settings round trip + OPTIONS.SAV codec + game detection + env report, offscreen.
if(TARGET wc98-launcher)
    add_test(NAME launcher_selftest COMMAND wc98-launcher --self-test)
    set_tests_properties(launcher_selftest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
```

`CHANGELOG.md` — under `## [Unreleased]` → `### Added`:

```markdown
- M8 launcher stack from the FIFA 99 sibling: `wc98_assets` (game-tree discovery, layered VFS with
  BIG/RefPack member reads, SHA-256 validation manifest of 3,596 Win95 CD data files) and
  `wc98-launcher` (Qt6 GUI: game-files validation report, Match options page with USER/OPTIONS.SAV
  import/export bound to `[gameplay]` settings keys, SDL3 controls/keymap editor, graphics/audio/
  logs/troubleshooting pages, Play launching `wc98-native`; CI modes --self-test / --screenshot-pages).
```

## Follow-ups (not blocking)

- Real OPTIONS.SAV samples: the extracted tree's USER/OPTIONS.SAV is a 54-byte ASCII extraction artifact
  (OPTIONS_SAV_WC98.md), so Import on that file correctly reports "bad size/magic" in the GUI; the Wine
  oracle pass should re-run the self-test with an authentic block.
- `wc98_launcher_ui` is a static lib so a future Qt UI test TU can drive pages offscreen (f99 pattern);
  none ported — the screenshot-pages mode already covers visual CI in-tree.
