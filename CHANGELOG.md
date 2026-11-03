# Changelog

All notable changes. Versioning: `0.x.y` pre-1.0; 1.0 = full World Cup
tournament playable natively, per the definition of success in the
project goals.

## [Unreleased]

### Added
- CMake/Ninja C++26 project skeleton with CTest harness and CI.
- `wc98::core` — bounds-checked little-endian `ByteReader`.
- Development log, legal/positioning docs, issue templates.
- Engine foundation ported from the FIFA 99 sibling (wc98 namespace): `wc98_platform`
  (SDL3 window, XDG paths), `wc98_renderer` + `wc98_renderer_vk` (Vulkan 1.3,
  Original/Enhanced presentation, offscreen readback), `wc98_config` (INI settings),
  `wc98_audio` (OpenAL mixer); `wc98-native` app skeleton; `wc98-render-demo`
  offscreen PNG demo. CTest suites: platform paths, renderer layout, Vulkan
  offscreen pixel checks + PNG round-trip on disk.
- `.PBK` speech banks fully decoded: members are 20 B phrase records + `PT` v2 play
  tracks; `decodePlayTrack` routes by codec tag (MicroTalk 6,851 / EA-XA 113) — the test
  gate moved from the old 249/4,652 framing artifact to 6,964/6,964 members, verified as
  real speech (voiced F0 ~ 175 Hz). Pass-2 audio RE doc corrected: its claimed decoder
  tables do not exist in the executable (see GHIDRA_PASS2_AUDIO.md banner).
- M4 slice 1: `wc98-render-demo <png> [tree]` presents a real FE bitmap
  (FE/FEFLOW02/FEFLOW02.FSH, 640x480) through the Vulkan renderer; ctest gate
  `render_demo_fsh` when WC98_TREE is set. New docs: AUDIO_RUNTIME.md (streaming model),
  docs/research/EXTERNAL.md (sibling-project tooling notes).
- M4 slices 2+3: `.LAY` front-end screens composite through the renderer
  (`wc98-render-demo --lay ARCHIVE.BIG@SCREEN.LAY`; FEFLOW05 main menu + CONTROL1 verified
  visually, gate `render_demo_lay`); FE `GEOM/SKEL` `.MDL` parser (offset-validated draw-lists,
  per-corner UV faces) renders the original trophy and stadium bowl textured via
  `wc98-model-view` (gate `model_view_wcupf`). Notes: LAY_COMPOSITOR_NOTES.md, MDL_GEOMETRY_NOTES.md.
- **M5 match simulation port complete**: `wc98_simulation` (47 files, ~27.6k lines) — ball
  flight/possession, player state machine incl. keeper saves, motion planner, formations from
  G98PL, FCDB-driven squads (WC98 68 B team records, 17 skill nibbles), rig FK skeleton posing
  implemented for the upstream linking gap (docs/reverse-engineering/SIM_PORT_NOTES.md maps every
  constant to its FIFAWC.EXE address). Gates `test_sim_core/players/team`.
- Input layer `wc98_input`: SDL3 keyboard/mouse/gamepad -> exe pad words (bit layout from the
  sibling FIFA 99 device readers), per-tick transition queues (short taps survive a tick), virtual
  gamepad hot-plug with stable ids, controller select. Gate `test_input`.
- M4 slice 1b: `wc98-render-demo --fsh <path>[@tag]` decodes any SHPI member (kit atlases PK/GK,
  FE sponsor art) with attached/global palettes; gates `render_demo_fsh_kit`,
  `render_demo_fsh_fe`; PNG proofs show original France kit cloth and Danish sponsor typography.
- Ghidra pass 4: module version manifest map (IG/AI/CAMERA/MOTION/TOURNAMENT getters),
  `win\key.c` input pump + shared key-poll primitive, in-exe EA sound-service cluster
  (streamservice/MIDI/speech) identified; DGROUP zero-fill scan pitfall documented.
- `wc98-model-export` renders real CD player art end-to-end: skinned `PCMD` models
  (`HIG/MED/LOW_*.PC`) posed by `COMMON/ANIM.MO` clips, kit textures bound from
  `PK###A/H.FSH` atlases (10/13 materials; `hjbx`↔`hjbk` tag quirk documented), plus
  stadium stands and the fully textured trophy. Evidence in MILESTONE4.md "Models & kits".
- Linux distribution packaging: `packaging/linux/build-appimage.sh` (pinned appimagetool 1.9.1,
  SHA-256 verified; bundles non-system libs only — host Vulkan/GL stack stays untouched),
  desktop entry + generic icon, `packaging/update.sh` self-updater (GitHub releases check,
  `.sha256` sidecar verification, `--smoke 3` pre-commit test, atomic swap with `.prev`
  rollback; never touches user game files), and a CI `package` job uploading the AppImage
  artifact on main pushes.
- **M6 exhibition-match vertical slice**: `presentation::MatchScene` renders a live match from original
  art — skinned player LOD chain (`SHI_10/HIG_30/MED_70/LOW_90.PC`) posed by sim anim instances, kit
  atlases per FCDB appearance (face composites + GK/referee sets), tiled `TURFS0P<col>.PAK` grass with
  law-scaled line markings, ball billboard from `MISC16.FSH` bl00-bl11 roll frames, planar player shadows
  (`SHADOW.PC`, FIFA 99 shear matrix), goal nets/posts + corner flags from `MODELS2.PAK`, and the
  four-sided stadium bowl (`STDE___D.BIG`). New headless driver `wc98-match-demo <prefix> [--tree DIR]`:
  real seeded simulation, keyboard controller through `wc98_input` (W = sprint, pad bit 0x100), three
  640x480 PNG captures (kickoff / mid-play / sprint). Gate `match_demo_frames` (skips without a tree).
  Contract: docs/development/MILESTONE6.md.
