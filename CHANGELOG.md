# Changelog

All notable changes. Versioning: `0.x.y` pre-1.0; 1.0 = full World Cup
tournament playable natively, per the definition of success in the
project goals.


## [Unreleased]

### Added
- Team-select is the authentic `FEFLOW11.LAY` pick grid (`app/fe_flow.cpp drawTeamsGrid`):
  compositor composite of map background, flags, group cells + dynamic ZWCUP pool rows
  (F<id>.FSH shape `f000`, FCDB names, scrollbar thumb in the LAY's `sbar` track); scene registry
  evidence `analysis/pass7/grid{5,6,7}.txt`, doc LAY_COMPOSITOR_NOTES.md; gate
  `fe_render_tournament` (--fe-team) + existing fe_nav/teamselect gates unchanged.

### Changed
- Commentary pick model is now the faithful consume-on-use predicate, not round-robin
  (`app/commentary.cpp`): pass-7c RE closed `FUN_0048d0dc` = qfind over the class-keyed ready list
  with slot-tag predicate `FUN_0048cf10`, activation `FUN_0048d1f8` removes each picked record;
  null pick -> event silent. Port pool consumed in storage order (per-event tag table unmeasured,
  marked in code). Evidence `analysis/pass7/pick{,.2,.3,.4}.txt`, doc COMMENTARY_WC98.md §pick;
  gate: `test_commentary` consumption asserts (pool exhausted -> silent).

## [0.2.0] - 2026-11-06 — tournament playable end-to-end (native FE -> bracket -> slot save)

### Added

- FE team-select screen (FE usability, `app/team_select.hpp` + `app/fe_flow.{hpp,cpp}`): Enter on the
  Tournament menu entry opens a paging list of the 40 ZWCUP pool teams (FCDB STR0 fullName names from
  FCDB.DBI + FCDB_ENG.DBI; arrows/pad cursor, Esc back). Confirming launches the tournament driver with
  the pick: `runTournamentDemo(userTeam)` records `TournamentState.userTeam` and asserts group-standings
  membership (+ bracket R16 node when qualified); slot payload gains a trailing additive `u32 userTeam`
  (round-trip-safe). Gate: `test_teamselect` paging model + `fe_nav_tournament{,_save,_load}` scenarios
  with scripted `--fe-pick <id>` (Scotland=35; load-verify asserts the pick survives slot round trip).

- Speech master-gain model (`app/commentary.{hpp,cpp}`): `SpeechGain` mirrors the EACSND speech
  volume chain (SNDspeechinit vol domain [0,0x7f], SNDspeechautovol linear 16.16 ramp with
  overshoot clamp at target, state-cb per-tick apply; pass-7b RE in COMMENTARY_WC98.md §gain,
  evidence `analysis/pass7/gain*.txt`). Commentary pump now ticks the master ramp and re-applies
  `class gain * master` to live voices each frame, with sentence start/stop autovol seam edges
  (sites 0x48fcb7/0x491125). Gate: `test_commentary` exact-tick ramp asserts + wiring.
- Gamepad support in the platform + app layers (`platform window.{hpp,cpp}`, `app/main.cpp`): SDL3
  gamepad subsystem init (failure-tolerant: pad-less startup never fatal), hot-plug tracking, per-poll
  `Events.padButtons`/`connectedCount`, canonical cross-platform button enum (`Window::PadButton`,
  values mirror `SDL_GamepadButton`) so app code stays SDL-free; d-pad-as-buttons plus left-stick/d-pad
  navigation surfaced as repeat-throttled nav events (`PadNavThrottle`, ~0.25 s) driving the same FE
  menu actions as the arrow keys (South/Start = confirm, East/Back = Esc parity). Match input unchanged:
  gamepads already map through `wc98::input` config bindings. Gates `test_gamepad` (mapping + throttle +
  virtual-pad poll glue), `gamepad_smoke_dummy` (`--pad-smoke`, SDL dummy driver, exits 0 pad-less headless).
- FE screen text objects (`FeText` in `presentation fe_screen.{hpp,cpp}`): LAY `textid` -> FC<lang>.BIN
  ("FCLG") string index (symbol form via the FEMSG table where present) -> `eras14bi.ffn` glyph quads
  through the HUD layout API (`layoutText`, new additive `textWidthPx`); LAY offsets/alignment/colour
  keys, colour indices into the global FE palette (additive `formats::globalPalette()` accessor).
  Unresolvable TEXTID keeps counting `noArt`. Gate `fe_flow_render` asserts menu text quads > 0 on FEFLOW05.
- Save framing + `USER/SLOT%.2d.SAV` codec (`formats/src/saveframe.cpp`, `slot.cpp`; SAVESLOT_WC98.md):
  record stream with the FUN_004c8070 CRC-16/ARC walk (seed 0xFBEA, table @DAT_00585d50), slot summary
  header id `0x10000`, FUN_0047b67c scan loop mirror. App-level tournament save/load (`app/saveload.{hpp,cpp}`):
  FE New game persists a slot after every completed round (XDG user dir, `WC98_SAVE_SLOT`), Load entry
  restores and verifies identical state. Gates `test_slot`, `tournament_saveload`, `fe_nav_tournament_save/_load`.
- In-match HUD overlay (`presentation/src/hud.cpp` + `match_scene` Frame::ui seam): scoreboard strip,
  clock (`DAT_0065F1C8`), radar from `MISC8.FSH` art at the exe-recovered rect (float block
  `0x55a9a8` = 533,67 / 180×120), FCDB STR0 team abbreviations, `eras14bi.ffn` glyphs, shirt-number
  labels; Hypothesis geometry behind `[gameplay] hud_*` knobs. Gate `test_hud`; HUD_WC98.md.
- Tournament module spec (`TOURNAMENT_MODULE_WC98.md`): bracket record stride 0x300 five-slot name
  layout, mode bytes (draw-mode has no writer; game mode from FE dispatcher), ZWCUP.RES C(40,2) pair
  matrix bijection (131 played pairs), group standings block closure, knockout node graph, per-screen
  implementation checklist — 61 cited claims.
- Tournament module `wc98::tournament` (`tournament/`): ZWCUP.RES parser + C(40,2) pair-matrix bijection
  gate (780/780), group standings w/ head-to-head tiebreak, seed-pinned schedule generator, knockout
  bracket R16→QF→SF→F + third-place playoff propagation; gates `test_tournament` + headless progression
  demo `wc98-tournament-demo` (gate `tournament_progression`, champion recorded).
- Speech record queue plumbing closed: pick = gate DAT_0063241c + qfind predicate, drain pops all;
  phrase-record 0x04/0x08 = cmn_queue link slots (`analysis/pass7/rec1.txt`).

## [0.1.0] - 2026-11-05 — first pre-release

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
- M8 launcher stack from the FIFA 99 sibling: `wc98_assets` (game-tree discovery, layered VFS with
  BIG/RefPack member reads, SHA-256 validation manifest of 3,596 Win95 CD data files) and
  `wc98-launcher` (Qt6 GUI: game-files validation report, Match options page with USER/OPTIONS.SAV
  import/export bound to `[gameplay]` settings keys, SDL3 controls/keymap editor, graphics/audio/
  logs/troubleshooting pages, Play launching `wc98-native`; CI modes --self-test / --screenshot-pages).
- Commentary pipeline: `PhraseBank` phrase-record index over PHRS.PBK (1,290 members / 4,665 phrases,
  RefPack-transparent), tolerant BIGF mirror parsing for TYPE.PBK stubs (byte-exact prefix equality
  gated), `MIDx`/empty/raw decode dispatch (CRWD banks live), engine-ABI `SpeechQueue`, and the
  exhibition-loop integration `app/commentary.{hpp,cpp}` (FUN_004893ec trigger model -> queue ->
  segment fetch/decode -> OpenAL Mixer; gates `test_phrs`/`test_speechqueue`/`test_commentary`).
- Team-history/trivia parsers `.HIS`/`.TRI` (`formats/src/{his,tri}.cpp`, gate `test_histtri`;
  language-invariant 33-id participant run at +0x04; trivia string pool) — tournament data layer.
- OPTIONS.SAV codec port (`save::loadOptions/saveToBytes`, byte-exact original round-trip gate
  `test_options_codec`) + launcher import/export wiring to `[gameplay]` keys.
- Release packaging v2: `packaging/linux/deploy.sh` (linuxdeploy, pinned SHA-256; game + Qt6 launcher
  in one AppImage with xcb/wayland/offscreen plugins, two desktop entries; host Vulkan never bundled);
  CI package job runs it with game (`--smoke 3`) and offscreen launcher self-test smokes.
