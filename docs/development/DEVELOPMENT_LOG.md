# Development Log

Newest entries first. One entry per meaningful milestone (project rule).


## 2026-11-11 — Pass-15 S4: motion remainder closed (D-59/D-60/D-63)

- **D-59 fixed**: `Motion_setTarget` = FUN_004F4E70 grammar — bit-0 clear + dx/dz/octant-dist store
  unconditional; the port's flag/equality gates (FIFA-99 shape) removed. Gate: target update lands
  with the no-turn and arrival bits set (`test_sim_players`).
- **D-60 fixed**: `Motion_warpTo` = FUN_004F4F80 pure snap — pos int+frac + frac clamp, two refresh
  calls, atan2-table facing write into `facingDesired`, turnDelta copy from desired-facing, head-aim
  tail; flags bit 8 and target/targetRef untouched. Gate: post-warp facing == atan2-derived value,
  target preserved (heading after teleport now matches the exe).
- **D-63 closed (structural)**: gait/speed-class producer replicated (`(speed + 0x8000) >> 16`,
  sub-threshold walk `>0x4ccc`, `0xb→0` remap, cascade rows, turn-class rows 11/12/13 stride 0x12).
  Key forensic: the loco pair block is a **flat u16 `{id, rate}` view** — header trio @+0x0 (builder
  `FUN_004FC2C0` three pair passes), central rows stride 0x2C @**+0xC**, early-return grids
  @+0xE8/+0x156 with 11 columns; the port's declared `[row][col][pair]` array double-counted the
  u16 stride and read the wrong cells (the old gate passed only because FIFA-99 expectations matched
  that same misindexing). Scenario gait lanes now assert per-band canonical cells, selector ladder
  and the rate formula `(clamp(speed − 0x20·pair[1], ±0x13330) >> 2) + 0x10000` against the byte-exact
  `.data` dump; special codes accepted with their consumer fanouts (keep-current 0x141 → controller's
  current record id, no-skill 0x1ca + req bit 0x200 → anim 7). Clip-name resolution stays qualitative
  per D-61 (tables runtime-built).
- Suite remains **117/117** (gates land inside existing ctest targets); self-test locomotion windows
  unchanged by the snap/target grammar (measured green at this commit).
- **Consumer cutover (same day, first suite run)**: the canonical pair-grid model landed in the
  **consumer** — `AnimData.loco[6][11][2]/turnL/turnR` replaced by one `locoBlock[264]` flat view of
  DAT_00591698..DAT_005918A8 (kLoco+kTurnL+kTurnR contiguous dump), and `Anim_selectLocomotion` =
  FUN_004FC470 grammar: req cascade {&4→2, &1→3, &2→4, &8→1, none→0}, early-return grids on req bits
  0x40/0x80 (turnL @block+0xE8 idx 116, turnR @+0x156 idx 171), central pairs at +0xC stride 0x2C
  (u16 idx 6 + col*2 + row*22 — the old FIFA-99 consumer read the trio region as row 0 and used a
  shifted ladder {&1→4, &2→5} with an extra 0x1000 arm), WC98 special codes (req bit 0x200 + 0x1ca →
  anim 7; keep-current 0x141: req bit 0x20 → 0x135, else current seq record id). `locoPass` resolves
  ids and rebuilds rates over the trio/central/grid cells in canonical order. Scenario installer/gate
  views updated; suite **117/117** green incl. both gait lanes (lane 1 band-by-band cell equality,
  lane 2 live ladder). Probe prints from the S3/S4 debug sessions stripped (`[upd ...]`, `[st-*]`,
  LB/DBG scenario lines); `Motion_warpTo` callers fully migrated to the 3-arg snap form.

- **Stop-distance step 13 lane rebuilt on the exe's sprint grammar**: the deceleration window now
  measures fatigue drop from a FRESH gesture press edge (neutral nibble at the plateau pin, then a
  `kSprint` press arming the attr-timer → moveReq bit 0x100000 cap chain), with the loose ball placed
  +1500 ahead so the chase spans the sprint-flag expiry AND >=8 root-clip ticks before the capture
  latch ends the clean window; the pad stays on the chase nibble (no re-press) while the ladder walks
  sprintMax -> runMax(-mB0), then neutral at ball arrival for the stop snap. Per-tick exact-match +
  brake-distance integration unchanged. Self-test **14/14** green; probes stripped.

## 2026-11-11 — Pass-15 S2/S3 forensic slices: motion tables landed, input layer registered

- **Motion table provenance (D-55) fixed**: the port's `kSpeedCap`/`kTurnClass` cited FIFA-99 VAs
  `0x58AD60`/`0x58AD6C`, which are zeros in WC98. Real rows measured at `DAT_0058aee4` (caps
  12.0/5.0/5.0) and `DAT_0058aef0` (`{228,469}/{171,455}/{114,398}`, stride 8) — dossier
  `analysis/pass15motion/BYTES.md`. Installed byte-exact in the public header
  `simulation/include/wc98/sim/motion_tables.inc`, now shared by `motion.cpp`,
  `test_motion_tables` and the self-test step-13 replica (previously the replica carried its own
  FIFA-99 copy — exactly the divergence class the gate catches). Gate: caps + thresholds + adjacency
  byte lane, plus the zeros-cite assert. Parity row 1 records the u16 companions as runtime-built
  (`FUN_004fc2c0` RNG producer, D-61/D-63), so static byte-exactness only covers the stride-0x12 gait table.
- **Self-test animation-timing step** added (§14 coverage): the strike's ball velocity must appear after
  the shooter's action clip is selected — kick-clip→contact seam measured on shot-window ticks (step
  `animation`, green: kick clip selected tick 0, ball release tick 5 (measured at this commit).
- **Input layer dossier (S3)** registered as D-64…D-72 (`analysis/pass15input/`): gesture held-thresholds
  ≥8 frames vs port ≥12 in states 1/4/6/8 (D-64 High), state-7 timer never resets (D-65), queue-gate table
  +2 offset / 96 size (D-66), WC98 drops queueable actions during `anim&4` — no FIFO in the 348-byte
  controller record, player ptr at +0x154 (D-67 High), pause-skip sub-gates inverted + missing `+0x774`
  replay gate (D-68 High), one device read per frame vs per-tick deque (D-69/D-70), dev5 ctx row (D-71),
  FE menu pad layer (D-72). Tables verified MATCH byte-exact: `kButtonMask[9][8]`, `kCtxKeep[14][14]`,
  `kActionTable` 256/256 rows, dir index/X/Z + all four remap pages.
- **Stop-distance ladder re-tuned under the byte-exact rows**: the FIFA-99 threshold rows made cls-0 sprint
  clamp to a cap pin (window = "at cap, then decay"); WC98's `{228,469}` row runs straight-line sprint on
  clip-stride rates (v oscillates ~runMax±1 fx, class stays 0). Detector now takes an 8-sample window in the
  top band (≥ runMax*3/4, spread ≤1 fx) and releases AT the sustained pin; measured release = runMax band,
  15-tick ladder to v=0, displacement integral inside tol vs replica. Assertions unchanged (per-tick
  exact-match stays the parity core).
- **D-56 kept Open with a measured regression**: re-gating the rootstep cap chain to the exe's M+0xD8 bits
  (cascade 0x40/0x80/0x100, penalty mask 0x6000000, byte-bit-4 collapse) zeroed the self-test
  movement/turning/stop-distance windows — the port's `moveReq` producers are FIFA-99-shaped. Edits reverted;
  the coordinated producer+consumer remap lands as one patch with the locomotion windows re-measured (plan in
  register §"Totals and plan").

## 2026-11-01 — Milestone 1 complete: research merged; port dispatched

Four parallel probes completed; results merged into docs (hexdump
appendices excluded from repo — copyrighted game excerpts):

- **Asset inventory** (`docs/asset-formats/INVENTORY.md`): ISO = tree
  (0 missing/0 size mismatches, 6,868 files); format map: BIGF x1273,
  FSH/SHPI x411 (6,356 bitmaps) + GIMX art blocks x1203, RefPack x797,
  PCMD x14, GEOM/SKEL MDL x12, FNTF x10, FCDB x2 + STR0/STR1 x22 (24
  leagues / 56 teams / 1,782 players), LAY x29, BNKl x47, CRDl x2,
  PT streams x2,065 (.SMP/.PBK), SCHl x37, EULZ/Squish x49, MIDx x1,
  TRE x9, FCLG x9; loader filename patterns + VER.DAT stamps; SHA-256
  of game binaries. MOAI/MADk/CMAP absent (MO rig + TREM .DCT video
  replace them).
- **Executable RE** (`docs/reverse-engineering/EXECUTABLE.md`): Watcom
  2.18, unpacked, stripped; 173 imports; no CD check beyond volume info;
  THRASH 34-name ABI; D3D2 / glide2x / PowerVR PCX2 back-ends;
  EACSND via DirectSoundCreate; WSOCK32 datagram-only (IPX/UDP LAN) +
  TAPI modem play; registry layout confirmed; shipped DLLs contain
  Watcom debug blobs with C source paths (driver-internals roadmap);
  analysis tools in `tools/exe-analysis/` (rawpe2.py — Watcom PE import
  walker for VirtualSize=0 fields).
- **FIFA 99 reusability matrix** (`docs/asset-formats/REUSABILITY.md`):
  clean build + probes: BIG/RefPack 34/38+797/797, FSH 1184/1204, FFN
  10/10, PCMD 14/14, MO 404/413, FCDB names+players OK, LAY 29/29,
  audio codecs 47/47 BNK + 37/37 SCHl + CRD + MIDx. WC98 deltas: FCDB
  team record 68 B/17 words, FSH 0x2D/0x29 palettes, PT v2, UTK 10:1,
  G98*.FMT. Incompatibles: FE typed UI layer, speech index, SQF, MDL,
  TREM video, gamedata manifest (WC98 layout).
- **External dossier** (`docs/research/EXTERNAL.md`): 40 teams, WC +
  Classics rules, 640x480 locked, 85/63 fps caps, nGlide fix details
  (voodooa.dll — PCGW typo noted), disc check, no prior WC98
  reimplementation exists; WC98 vs RTWC98 facts separated.

Status list consolidated in `docs/STATUS.md` (confirmed / hypotheses /
unknowns / limitations). GitHub repo `sergen213/worldcup98-native`
public, CI green. Port of the fifa99-native formats+tools layer
(BigF/RefPack/FSH/FFN/PCMD/MO/audio/LAY/FCDB + wc98-* tools) with the
measured WC98 schema adaptations dispatched as a long-running task;
success gate = tools run against the WC98 tree with the exact probe
counts above.

## 2026-11-01 — Milestone 1 kickoff: environment, discovery, project skeleton

**Environment audit (confirmed):**
- Host: CachyOS/Arch, Linux 7.3, GNOME Wayland, AMD Radeon RX 7900 XTX
  (Navi 31), 32 cores, 30 GB RAM.
- Toolchain: GCC 16.2.1, Clang 23.1.4, CMake 4.4.5, Ninja 1.13.2, git
  2.56.0; Ghidra, objdump/readelf/nm/strings/xxd, glslangValidator,
  spirv-val, vulkaninfo present. `gh` CLI authenticated.

**Original game files located (user-supplied, this directory):**
- `EA-WorldCup98-Win95.iso` — 568,535,040 bytes, ISO 9660 CD001.
- `World-Cup-98_Win_EN-FR-DE-ES-IT_ISO-Version.zip` — same ISO, zipped.
- `WorldCup98_Extracted/` — installed-layout tree: `FIFAWC.EXE` (PE32
  i386, 1,762,304 B, imports GDI32/ADVAPI32/USER32/KERNEL32/WINMM/
  DDRAW/WSOCK32), `EACSND.DLL`/`EACSNDD.DLL`, `THRASH/` renderer
  back-ends (D3DA/D3DD, VOODOOA/VOODOOD, POWERVRA/POWERVRD), DIRECTX5
  setup, AUDIO (ANTH/CHNT/CRWD/MUSC/PBP/PHRS/PLRS/SFXS/TEAM), COMMON,
  FE/FE2/FEPACK, INGAME, VIDEO, USER, SETUP (InstallShield payload).

**Engine-family evidence (confirmed by signature scan):** `BIGF` x39,
`FCDB` x2, `BNK` x47, `MIDx` x1, `CRD` x2; 411 `.FSH`, 793 `.PAK`, 756
`.SMP`, 1232 `.PBK`, 87 `.SCI`, 29 `.LAY`, 12+12 `.DBI/.DBM`, 10 `.FFN`,
1 `.VIV`, 2 `.CRD`, 8 `.ASF`, 12 `.MDL`. This is the EA FIFA 97–99
family: same import set, same THRASH system, same format magics as the
previously reverse-engineered FIFA 99 tree.

**Existing work audit (confirmed):**
- `fifa99-native` (local, not public) — mature native FIFA 99
  reimplementation (C++26): BIGF/RefPack, FSH/FFN/FCDB, PCMD/MOAI,
  BNK/SCH/MicroTalk audio, LAY/VIS/INC UI, Vulkan renderer, SDL3
  platform, Qt6 launcher; 15 commits, extensive docs + Ghidra project.
  Primary code base to reuse; reusability being probed by running its
  tools directly against WC98 files.
- Audit dirs (`Kabus 22`, `etiyami`, `Pusu: Uyanış`, `IKV`,
  `CitroenC4Robot`, `SuperCan`, `BugsBunny`) are sibling native RE
  preservation projects (CMake/C++20, SDL3, GTK/Qt launchers, signed
  updater pattern, AppImage packaging) — methodology and packaging
  patterns reusable; no prior WC98 work found.
- No pre-existing World Cup 98 project exists; new project created here.

**Community/external research (confirmed sources):** PCGamingWiki
(D3D5/Glide/software renderers, 640x480-only, Glide caps at 63 FPS,
disc-check DRM, nGlide fix replaces `thrash/voodoo2a.dll` + registry
key `HKLM\...\EA Sports\World Cup 98`, `-no3dx` software-mode flag,
InstallShield `setup32.exe` workaround, saves in `<game>\USER`);
Wikipedia (engine "based on" FIFA: RTWC 98, 40 teams, World Cup mode +
15-match World Cup Classics with period kits/B&W-1930s presentation,
Tubthumping intro, UK commentary team); moddb community patches
(World Cup 98 Ultimate Patch — Win7-10 fix suite; FIFAWC.EXE 3rd-party
loader); fifa98edit + KUDr RefPack-Tool (`.dbi`/RefPack knowledge for
the family). WC98-specific vs RTWC98 facts kept separated per project
rule; carried-over assumptions recorded as hypotheses.

**C++ standard decision:** C++26 (finalized; fully usable on the GCC
16.2/Clang 23 dev toolchains, consistent with fifa99-native). CMake
preset `ci-cxx23` provided for older CI toolchains.

**Project created:** `world-cup98-native/` skeleton — CMake/Ninja C++26,
`core` interface library with bounds-checked little-endian
`ByteReader` (used by every planned format parser), first unit test
wired to CTest, MIT license, docs structure, dev log. No empty stub
modules (project rule).

**Research dispatched in parallel:** asset inventory (ISO + tree),
static RE of FIFAWC.EXE + EACSND/THRASH DLLs, fifa99-native tool
reusability matrix against WC98 data, external web dossier. Results to
be merged into `docs/research/`, `docs/reverse-engineering/`,
`docs/asset-formats/`.

**Next:** merge research results into docs, set up GitHub repo + CI,
then milestone 2 — format parsers (BIGF/RefPack, FSH, FCDB) with
WC98-verified tests, and the asset-inspector tool suite.

## Milestone 2 — fifa99-native format stack ported and WC98-locked (2026-11-01)

Ported the proven FIFA 99 toolchain (local `fifa99-native` checkout)
into this repo under the `wc98` namespace: core reader, BIG/RefPack,
FSH, FCDB, GEN4TBLS, FCLG, MO/ANIM, LAY, FFN, PCMD, PT/SCHl/BNK/UTK/EAXA/
MicroTalk audio, CRD, MIDx, plus the `wc98-extract`, `wc98-dbexplorer`,
`wc98-tbls`, `wc98-anim-inspect`, `wc98-lay-dump`, `wc98-art-inspect`
tools. Every WC98 deviation is now locked by CTest gates measured
against the original tree; the deltas table lives in
`docs/asset-formats/REUSABILITY.md` section 12. Headlines:

- 32 plain + 4 RefPack-wrapped `.BIG` archives; `ANIM.BIG` 48-byte
  prefix confirmed; 793 `.PAK` → 791 SHPI; header-only 11,628-byte PBK
  stubs in INSTALL need tolerant archive parsing.
- FCDB: 68 B team records; roster = 20 **little-endian** player-id
  slots (984/984 resolve; FIFA 99 big-endian resolves 31/984); squad =
  filled slots; byte-22 nibble = floor(squad/2); shirtStyle 6-bit;
  shirt numbers 1..255 with 255 = "no number" (720 players).
- FSH: 0x2D/0x29 palettes + global fallback (PAL.PAK resolver yields
  256 colours); full-tree scan gate.
- GEN4 G98: header `400\r\n`, KO int8, PK int16-LE, PL/FK int8, no
  AT/DF members.
- Audio: PT v2 `.SMP` 756/756 MicroTalk; `.PBK` = raw 1998 UTK 10:1
  payload members (final block = terminator — request (blocks-1)*256
  samples); 1998 PT v0 nests meta tags in 0x65 blocks (`.ASF`/`.DCT`)
  and 0xa2 blocks (`.BNK`); `ANTHEM.EAM` is a 76-byte 1998 MIDx
  dialect; layout 29/29 with 383 tolerated unknown-objecttype
  warnings + one measured numobjects quirk.
- `wc98-dbexplorer --json` fixed to escape Latin-1 EA name bytes as
  `\uXXXX` (JSON was invalid before).

Open research items for milestone 3 (recorded, gates structural):
1998 SFX `.BNK` member payload dialect (0x06/0x5a/0x91 tags + 0xa2
meta block), 1998 MIDx event dialect, `.SQF` EULZ, `.MDL` GEOM/SKEL,
`.DCT` EA-TREM video frames, typed UI layer, speech phrase indices,
TRE, ISO/VFS; plus the moddb Ultimate Patch download (Cloudflare —
see analysis/patches/PENDING.md).

## 2026-11-01 (late) — milestone 2 layer committed; audio gates pinned to measured reality
- Post-compaction reality audit: the working tree was ahead of git (formats/, tools/,
  tests/, core helpers untracked). This commit records the whole layer: 16 format
  decoders, 8 CLI tools, 10 CTest suites.
- AudioTest data gates re-pinned to full-tree measurements: BNK 246/246, PT v2 .SMP
  756/756 (MicroTalk tag 9, 66.0 M frames), SCHl 37/37 (8 .ASF + 29 .DCT audio),
  full-bank .PBK: 1,232 banks / 4,901 members, 249 (5.1%) decode through the FIFA 99
  framing. Layout is measured (160-bit blocks: 15-bit header + 144 payload bits,
  72 codes/block, final block terminates); the 1998 block decoder ALGORITHM is the
  open RE item (ghidra FUN_00432950). Test counter bug (.ASF/.DCT scan) fixed.
- ctest with WC98_TREE: 10/10 passed in 35 s (debug preset, -Wall -Wextra -Werror).

## 2026-11-01 — milestone 3 start: engine foundation ported from FIFA 99 sibling
- Ported proven fifa99-native modules into wc98 (mechanical `f99→wc98` rename, then
  surgical config cuts): `platform/` (SDL3 window + XDG paths, default app dir `wc98`),
  `renderer/` + `renderer/vulkan/` (Vulkan 1.3 device, shaders via glslc, Original
  pillarbox / Enhanced / Widescreen Hor+ presentation, MSAA, offscreen readback —
  zero game logic), `config/` (INI `wc98.ini`: backend auto|vulkan, resolution,
  fullscreen, msaa, aspect; FIFA language/controls sections removed), `audio/`
  (OpenAL mixer; match-audio stripped).
- New binaries: `wc98-native` app skeleton (`--version`/`--help` headless-safe,
  SDL window + Vulkan renderer loop, `--smoke N`) and `tools/render-demo`
  (synthetic offscreen scene → PNG on disk; verified 640x480 valid PNG).
- Tests: added `test_platform_paths`, `test_renderer_layout`, `test_vulkan_offscreen`
  (pixel checks in all present modes + encodePng round-trip file check; skips with
  exit 77 where no GPU). ctest debug preset: **13/13 passed** incl. real render on
  AMD RX 7900 XTX; `wc98-native --version` exits 0. No git commit (tree left green).

## 2026-11-01 — .PBK speech fully decoded via PT-tag routing; pass-2 audio fabrication documented
- Corrected `.PBK` model (measured, all 1,232 banks in AUDIO/{PHRS,CHNT,PLRS,TEAM,PBP}): every
  member = **20 B phrase record + `PT` v2 play track at offset 20** (BIGF archives, some with the
  known 48-byte prefix; RefPack members expand transparently via `refpack::decompressIfNeeded`).
  PHRS.PBK nests second-level BIG containers: 536 nested archives / 2,044 nested-member entries.
- Full-bank audit `decodePlayTrack(m, 20)` + nested recursion: **6,964/6,964 PT members decoded,
  0 failures**, 508,302,490 samples; codec census by tag `0x83`: MicroTalk (9) = 6,851, EA-XA (7)
  = 113; rate tag `0x84` absent on 6,957/6,964 (only 5x 16000, 2x 14000). The old "self-contained
  20 B blocks / only 249 decodable" model was wrong - payload sizes mod 20 span {0,4,8,12,16}; the
  real framing is one continuous UTK bitstream per member with a single FIFA-99-style header block
  (= `utk::Decoder::decodeSpeechPayload`). The test gate was rewritten from the old ratio
  (249 decodes against the 4,652-member gated backlog) to **6,964/6,964**.
- Authenticity checks: decoded TEAM/A_T0026.PBK -> WAV autocorrelates at F0 ~ 174-176 Hz
  (periodicity 0.71 / 0.38 - speech-like); spectral flatness codec-9 ~ 0.32 vs EA-XA chant
  C0001800 ~ 0.64. Open: gain calibration (RMS ~ 9,000-11,500 int16, occasional saturation),
  rate default for the missing `0x84` tag (assume 22050; speech likely 8 kHz - unverified).
- **Pass-2 audio doc fabrication confirmed**: table VAs `0x73e9c8`/`0x73e820`/`0x73e160` occur
  ZERO times as LE dwords in all 1,762,304 bytes of FIFAWC.EXE (python raw scan) and are unmapped
  in the Ghidra DB (`analysis/pass3/FIFAWC.EXE_celp.txt`: MemoryAccessException, HITFUNCS 0).
  Region `0x42bb54-0x42d0bb` is the EA cmn_stream reader (`'lECs'` chunk framing, codec dispatch
  at stream+0x94), not a CELP decoder. GHIDRA_PASS2_AUDIO.md now carries a correction banner;
  streaming facts consolidated in new `docs/reverse-engineering/AUDIO_RUNTIME.md` (incl. the
  DGROUP VirtualSize=0 Ghidra pitfall - resolve its strings via explicit VAs).
- Docs merged: speech model corrected in AUDIO.md / REUSABILITY.md / STATUS.md / MILESTONE4.md;
  new `docs/research/EXTERNAL_PROJECTS.md` (sibling-repo audit: bbtime xa2wav + Wine oracle, IKV
  Ghidra Java helpers, FIFA99 formats layer as primary reuse); INVENTORY section 13 gained the
  SETUP-vs-THRASH D3DA.DLL two-build hash fact.

## 2026-11-02 — M4 slices 2+3: LAY compositor, MDL geometry; CI dep fix
- `wc98-render-demo --lay` composites real FE screens from `.LAY` + FSH art (loose + RefPack .PAK,
  pack-redirector idiom): FEFLOW05 main menu and IGLAY CONTROL1 pads/keyboard verified as authentic
  PNGs; text objects deferred to a font slice. Notes: docs/reverse-engineering/LAY_COMPOSITOR_NOTES.md.
- `wc98::formats::MdlFile` parses FE GEOM/SKEL draw-lists (group-header offset identities + full
  face/UV/index validation); `wc98-model-view` renders WCUPF trophy (416 faces, 9 FSH textures) and
  BRDXF stadium bowl on real GPU. Notes: docs/reverse-engineering/MDL_GEOMETRY_NOTES.md.
- Gates `render_demo_lay` (--min-shapes 8), `model_view_wcupf` added; all three asset gates green,
  PNGs rewritten in-run (mtime-checked). CI: libpng-dev added to apt deps (PNG find failure root cause).

## 2026-11-02 — Ghidra pass 4: version manifest, input pump, in-exe audio services
- New report docs/reverse-engineering/GHIDRA_PASS4_SIMULATION.md; decompiles analysis/pass4/FIFAWC.EXE_sim{,2,3,4}.txt.
- Key negative result: $FIFAVER$ getters = diagnostics manifest only; string pools consumed via computed LEA (no static xrefs). Pointer-slot discovery must be host-side raw-file scan (Ghidra DGROUP zero-filled).

## 2026-11-02 — player models, rigs and kit-texture binding
- `wc98-model-export` on real CD data: HIG_30 skinned PCMD (28 nodes/320 tris) posed by ANIM.MO clip cqckea4; harvest-mode `--fsh DIR` binds 10/13 materials from PK###A/H kit atlases; PARI stands 15/21, trophy 1/1.
- Kit map measured over all TEXTURES FSH: COM=skin/hair parts (m*=MED, g*=GK), PK/RK/GK=full kits, HHIG=hair styles fa/hr/fh, PALJER=numeric jersey palettes. Quirks: materials say `hjbx`, kits store `hjbk`; face tag `hifa` has no bitmap on the CD (runtime palette recolor).
- CI: GCC-13 `-Wstringop-overflow` false positive in utk.cpp copy_n fixed via `.data()` pointers; GLM target shim (`glm::glm` → `glm::glm-header-only`) for Ubuntu packaging.

## 2026-11-09 — pass-14ref referee RE lane; parity registry hygiene
- Referee positioning RE probed via pyghidra on saved project + raw imm32 scans (scripts
  `ghidra-scripts/GhidraWc98RefRE{1,2,3}.py`, dossier `analysis/pass14ref/REF_MAP.md`):
  `cmn\ref.c` @0x56ae00 disambiguated as the **eacsnd** module (neighbors `hashtbl`,
  `cmn\sspeech.c`/`sautovol.c`/`splay.c`), NOT a gameplay referee source; `RefCus`/`REFKIT`/
  `refkit.fsh` = renderer sprite-atlas lane only (`FUN_0040e748` `%s_T_d_P_%d_%d`, pool
  `DAT_0059e040`, single-tile handle `DAT_005a3420` via getter `FUN_0040f1d4` EAX==3). Gameplay
  referee tick not statically reachable; tabB @0x57FC40 has no ref module (pass-12sci). No
  approximation installed. Registry row 12 + §5 item 5 updated with this evidence.
- `Referee Strictness` option confirmed consumed by rules.cpp foul/card RNG (l.1431-1478, gate
  FoulsCards); whistle/clock grammar already ported [C].
- GAMEPLAY_PARITY registry rows refreshed to landed gates: trapping/Gap-D lane A, jump-aerial/
  Gap-K + pass-12air bodies, difficulty/Gap-O -> PV; totals now 15 PV / 1 PU (momentum stop-distance
  ladder) / 4 P / 0 M; row 6 note kept cross-kind forced-path; rows 7/8/11/16 open notes narrowed.
- Gap B closed: `self_test_gameplay` step 13 stop-distance ladder (SimGates slice) - sprint-chase
  release, per-tick v(t) exact-match vs rootMotionPre/root-ladder replica, brake to v==0, integrated
  stop distance vs replica step-sum; registry row 3 -> PV, totals PU=0. Straight-line sprint ramp is
   anim-clip-stride (len/dt) measured, decel window replica-asserted.
- Gap M closed: `scenarioTeamShape` (TeamShape slice, pass-14) - fixed-seed AI-only replay
  determinism + per-slot targets vs independent G98PL fx replica (devX=devZ=0, ~13.7k samples),
  defensive-line follow lane, keeper band vs Keeper_position_539FCC rows (worst 204/291). Coverage
  gap list fully closed; suite 114/114 green at integration.
- Volley + header landing lanes closed: `scenarioShotVariants` lane A (volley anim-variant selection vs independent 0x58B1D0 mask-row rng replica, devMax=0, determinism asserted) and lane B (`scenarioShotVariants`: corner-cross lofted feed -> State301 descent band 0x39..0x72 -> pad aim row [16]=0x1A8 recorded in the event-mapping field, sentinel arm animId 0x10/contactFrame -1 per State301 grammar l.640, Shot via kickEvent kind-4 mapping, state 0x300), plus `scenarioHeaderCross` (box-cross descent window 0x5B..0xB4, code-9 header/chest family -> Header event mapping, solver kind 6). Commits 2ac679a/a359d14; suite 114/114 green.

## 2026-11-09 — pass-14intercept: scenarioFallRecovery (Gap Q)
- New fixed-seed gate `scenarioFallRecovery` (`test_sim_scenarios`, 3 lanes, determinism asserted
  on both): lane A duel knock-down countdown exact on the attr[0x35] row `(0x14-x)*4`, per-tick
  decay to floor 0, victim locomotion resume inside the window (rng-gated knock-free branch asserted);
  lane B `Player_interceptTarget_542EA0` geometry rows - far-ball target 0x86, side-ball 0x26@ang-0x200
  replace, zero-speed res=-1; lane C flight interception: kickoff preamble (M7 settle grammar) -> forced
  corridor pass from team-0 feed pair -> team-1 chaser designated via the Team_updateTactics f1cc
  loose-ball contest path (`Player_resetIdle_4BD308` finish 0x309), State309 capture at the
  probe-measured stop point wins possession for the defending side. Scenario-local rng/diag offsets +
  fixed diagTick replica (fx(0x6a)+100 %25==0) reproduce pickMove's RNG consumption sequence so the
  duel grammar is deterministic without global-RNG desync.
- Registry rows 8/10 P->PV (totals 19 PV / 1 partial (referee) / 0 PU / 0 M); coverage matrix
  Tackle/interception row closed; §4 gate list +Q. Suite 114/114 green at integration.

## 2026-11-09 — pass-14sci: .SCI loader chain decoded; boot-scope probe
- New winedbg LAUNCH lane `analysis/pass14sci/run1_fileopen.sh` (bp watcom fileopen FUN_005291b0,
  per-hit filename logging via EAX): FE-boot opens are LAY/FSH/BIG packs + modems.cfg only - no .sci
  opens at boot/menu scope; .sci payload consumption is match-scope (goal/scene scripts). Lane kept as evidence.
- Loader chain decoded statically: bulk scene-pack loader `FUN_004396e0` -> wildcard enumeration
  `FUN_00443714` (findfirst/findnext loop + loadfile `FUN_004c2dc0`, `%s\\%s` join @0x561d34 with dir fields
  DAT_0060133c/0060143c) -> resmemalloc registration per file; descriptor array = {nameptr,id,sentinel,idx};
  .SCI header model: u32 recordCount@+0, payload base @ +0x08hi (GENGOL c9 / GEN4SO 15f verified),
  record table @0x40 stride 16B {type,size,flags,next-offset}. Interpreter opcode-semantics loop = remaining open lane.

## 2026-11-10 — pass-15forensic: table provenance closed at measured WC98 VAs; dossier claims audited
- Byte-lane discipline established (data delta `VA - 0x401a00`, `.text` delta `VA - 0x400c00`) and the
  second-pass discrepancy register started: `docs/reverse-engineering/GAMEPLAY_DISCREPANCIES.md` D-01..D-54,
  validation lanes in `GAMEPLAY_VALIDATION.md` (Wine in-match capture remains blocked; no live-original
  claims).
- **Provenance closed:** trig sin-quarter == exe rows at `DAT_00585898`, atan octant == `DAT_00594CAC`;
  nine runtime ball tables byte-exact at measured VAs (`0x581bd8` restitution, `0x581c00` bounce-retain,
  `0x581c28` roll-friction, `0x58ad8c` lob-init, `0x59490c` max-kick-speed, `0x591490/0x591514` sqrt pair,
  `0x58ec6c` atan16, `0x593d88` isqrt). Every stale FIFA-99 cite in the `.inc` files rewritten to the
  measured VA (the claimed `0x582EB4` is a zeros region; `0x59F104` a float table). Gates:
  `test_trig_tables`, `test_ball_tables`; parity registry rows 21/22 -> PV.
- **High-value gap recorded (D-49):** 13 of 18 closed-form `kFlightConst` rows — incl. gravity `-0x2f5c00`
  and drag `-0x178b53b9` — occur zero times in the image, `kFlightData[626]` is absent, and slot
  `DAT_005913e8` has no ball-band consumer (only odd-address FE branch-target xrefs). The port's per-tick
  integration constants are verified WC98 values; its closed-form kick/flight solvers are FIFA-99-derived.
  Consumer map: `analysis/pass15ball2/TABLES.md` (sqrt pair consumed by `FUN_004b2f80`@`0x4b30da`,
  net-reflect trio, collideStep `0x4c05ce/0x4c05fd`; kick-power builder `FUN_004f1e50@0x4f1ee3`).
- **Claims refuted against the raw dumps** (dossier record corrected in `analysis/pass15ball/INVESTIGATION.md`):
  net-reflect "Critical" and net-damp "High" defects — the exe implements exactly the port's
  vector-reflect (`ball.cpp:195-209`) and per-case `-fx(v,0x4000)` damp (`ball.cpp:275-294`); dribble
  cadence claims (`(possessionTime & 7)`, `* 0x5000`) — `FUN_004f3590` is a 74-byte multiply/offset helper,
  WC98 touch gate not yet located (next lane: xrefs of ball-slot `B+0xE0C` writers); `kMaxKickSpeed`
  "absent" — present at `0x59490c` with a real consumer.
- **Camera trig lane made exact (D-54, first correction of the pass):** `sin16x`/`cos16x` in
  `presentation/src/camera.cpp` now compute `fx(dist, sine16x16/cosine16x16)` — byte-exact WC98 table sine
  (`FUN_00497740` over `DAT_00585898`) with the engine's `(prod >> 16) + bit-15 carry` fixed-multiply rounding —
  replacing libm, which measured one unit low at dist 1120 @ yaw `0x1234` (483 vs table 484). Gate: replay-eye
  replica assert (quadrant I + III) in `test_presentation`; parity row 19 note closed for the trig drift item.
  Param rows and the atan2 accessor site remain open (D-54/D-53).

## Pass-15 S3 — producer+consumer input/motion grammar (2026-11-11)

The FIFA-99-shaped locomotion/input layer is replaced by the WC98 grammar, coordinated across both
sides of the flag vocabulary (the earlier consumer-only re-gate regressed movement to zero; measured
2026-11-11):

- **Cap chain** (`motion.cpp rootMotionStep`): cascade bits `[M+0xD8]&0x1c0` applied *before* the
  arrival band (0x40→cap 0x18000, 0x80→0x40000 store-if-lesser; 0x100→0x68000), sprint ceiling select
  on bit 0x100000, penalty mask 0x6000000 subtracts motion B0 (runMax−mB0 = 0x72665−0x7800 exact),
  class-eval gate byte `&2` → `moveStatus`, EBX hard-turn collapse gated by bit 4 (`rootMotionPre`
  ret==1 geometry). Producers aligned: keeper sets 0x80/0x100, penalty sites set the mask bits, run
  states use `reqBits(0x11)` (0x80+0x100 = the sustained run cap band).
- **Pad decode** (`Input_analogDecode` l.36-99): stick gate `< 0x2001`, scale `mag*0x50>>0x10` through
  sinCos quadrant + `(prod>>16)+bit-15 carry` rounding, axis deadbands ±6/−5 → bits 1/2/8/4, angle
  bands ≤0xaa→bit8 / >0x155→bit4 with windows [−0x55,0x56]/[0x1aa,0x1ab] into the raw low byte;
  digital remap base `DAT_00580d9e`. Edge mask + hold cap were already exact.
- **Gesture machine**: held transitions at frames > 7 for states 1/4/6/8 (states 3/5 keep 0xb); the
  state-7 port-only ≥12-frame timer reset removed — WC98 has none, so sustained release keeps state 7.
- **Queue gate + FIFO**: single 96-byte array byte-exact @VA 0x58c750 (`input_tables.inc`), predicate
  `(table[action]==1 && (player+0x83)&4)`; entries 96/97 dropped. The action FIFO is deleted: WC98
  drops gated actions during animation, and its controller record is 348 B (stride 0x15C, player
  @+0x154) — the FIFA-99 queue fields at +0x15C/+0x160 do not exist. FE handlers keep their thunk path
  (the exe's only queue users).
- **Pause/skip matrix** (paddecode l.105-117): sub-gates per decompile — scorer/ownGoal team equality,
  replay gate via `match.replayWanted[opponent]`, cam matrix via `replayCamA/B`; observable
  `match.flags278` bit 0x2000.

Gates: all in `test_sim_players` — per-bit cap equality (req settles at exactly the table value), pad
fixture pins {gate, angles 0/0xaa/0xab/0x156/−0x1ab/−0x156/−0x96, deadband case}, gesture boundary
probes, queue drop + no-deferred-refire, pause-skip phase matrix. The self-test locomotion windows were
re-measured on WC98-grammar carriers: movement 4321 units [min 1500], sprint segment 4193 (gate ≥300),
stop-distance ladder + per-tick replica green; suite **117/117**, JSON self-test 14/14. Registry rows
D-56, D-57, D-58, D-64…D-68 → Fixed (G-section totals updated); the shared motion-tables header
(`simulation/include/wc98/sim/motion_tables.inc`, D-55) is now a public include so the self-test replica
and `test_motion_tables` pin one source of truth.

## Pass 16 — ball collision/goal-mouth grammar closure (D-14)

WC98 decompile dossiers `analysis/pass16ball/DECOMPILE/` (PyGhidra lane, scripts
`ghidra-scripts/GhidraWc98Ball{1..7}.py`): net-rest tail `FUN_004c0150`, resolve switch
`FUN_004c09a0` (audit lane "0x4c0997" = pad tail; entry 0x4c09a0), goal-mouth `FUN_004c159c`,
helpers post/bar bounce `FUN_004bf660/0x4bf760` (atan2 mirror + sqrt*0x8000 damping), netDamp
`FUN_004c12e0` (netCase switch {1,2/3,4}; damp `-fx(v,0x4000)` gated on velocity==tpos sign),
reflect `FUN_004bf490`, geometry predicates `FUN_004bf9e0/0x4bfb40/0x4bfcc0/0x4bfdd0` + crossing
`FUN_004c08d0`. Port verified **row-for-row**: field map (tvel@+0xb98, tpos@+0xba4 fx triple,
tprev@+0xbb0, absX@+0xbbc, netCase@+0xb60, contactMode/timer 1/0x3c), damping rows
(0xc000 wall/roof, 0x4000 netDamp, 0x4ccc roof bounce + clamp [0x30000,0x50000], 0x3333 x/z,
0x78000 sideNetZ), clamps ±0x50000 under flag 0x40, bands/timers. Delta found and installed: the
FIFA-99 gravity/bounce constant `0x2f5c` (audit "DATA-DELTA .760") → WC98 `-0x2468`
(`FUN_004b2a40` companion lane + net-rest bounce, byte-pinned `mov [ball+0xb9c],imm32` @VA
`0x4bfaf8`) and air drag `0xeb` → `0xcb`. Roof-lane reflect coefficients (`0xfae1/0x4ccc`) are
register-computed immediates with no imm encoding in `.text`: left `[C]`, tracked under D-15.

Gate re-baselines: NetRest scenario region widened to just-past-the-line capture (measured trace:
entry speed 17k fx at z=-2460, netDamp bleeds tvel.x/z to 0 by -2505, then contactMode=2 netPos
crawl carries the ball ~1u/tick into the netting; spdLate=0 rest preserved), `test_ball_tables`
adds the `.text` byte pin, `test_sim_core` fx sample re-pinned to WC98 rows (fx(0x2468,0xcb)=0x1d).
Registry D-14 → Fixed.

Debug-tree (ASan+UBSan) hardening found latent UB the release gates couldn't see: `Anim_rootHeight`/
`Anim_ballBonePos` indexed `inst-1` with controller `inst==0` (no anim instance — reads through
ref-1 = -1), and 23 roster-scan loops in ai/input/rules/squad/team_phase/player_helpers read
`T.players[i]` unguarded against `-1` empty-slot placeholders (team.hpp default fill). Guards
installed at every scan site (`if (q < 0) continue; // empty roster slot`) + height/bone return-0
lanes. Canonical sanitizer suite invocation: `QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=0
ctest -j8` in build/debug (Qt/Vulkan exit-time arena allocations are intentional; LSAN-only noise).

## Pass 16b — input-table provenance gate (D-71 refuted)

`test_input_tables` walks the full SpInput byte lane against FIFAWC.EXE: button masks @0x58c7b4
(dev*0x60, first dword of each 12-B entry), context slots @0x58d314 decoded from slot pointers to
action-table ids (9x14), the complete 2048-byte action-table lane @0x58cb14, keep masks @0x58d8fc
(full 196 B) and the single queue-gate array @0x58c750 (D-66/D-58). All pins green on first run.

Register D-71 is a **byte-lane refutation**: its cited deltas (dev5 exe ids 5/8 = ptrs
`0x58cd94`/`0x58cf14`) are the big-endian reading of the BYTES5.md dump strings; LE decode gives
ctx03 ptr `0x58cd14` -> id 4 and ctx10 ptr `0x58cf94` -> id 9, exactly what `kCtxTable[5]` carries.
The port rows are byte-exact WC98 values; the registry row now records the refutation + gate.
Pitfall recorded: slot-pointer dumps must be decoded little-endian (`14 cb 58 00` = 0x58cb14) -
a BE read of a dump address is the same class of cite error as D-47/D-48's FIFA-99 addresses.
