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
  research/EXTERNAL_PROJECTS.md (sibling-project tooling audit).
