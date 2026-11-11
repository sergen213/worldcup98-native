# Development Log

Newest entries first. One entry per meaningful milestone (project rule).

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
