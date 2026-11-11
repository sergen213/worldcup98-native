# Project Status — Confirmed Findings, Hypotheses, Unknowns, Limitations

Conventions: **Confirmed** = evidence in this repo (offset/string/magic +
counts in `docs/asset-formats/INVENTORY.md`, `docs/reverse-engineering/EXECUTABLE.md`).
**Hypothesis** = strongly supported; **Unverified** = thin evidence;
**Unknown** = open RE items. Source URLs in `docs/research/EXTERNAL.md`.

## Confirmed — engine & platform

- Engine family: EA FIFA 97–99 codebase. FIFAWC.EXE = Watcom linker 2.18
  PE32 (AUTO/DGROUP segments), built 1998-04-13, **not packed**, no CD
  protection beyond `GetVolumeInformation` (unlike FIFA 99's Petite+CMS).
  `$FIFAVER$` module banners (RENDER/IG/AI/CAMERA/TOURNAMENT/FCDB...).
- Renderer: `!thrash\%s.dll` loader with 34-name `_THRASH_*` GetProcAddress
  ABI; shipped back-ends Direct3D 2 (`d3da/d3dd`), 3dfx Glide 2.x via
  runtime `glide2x.dll` (`voodoa/d`), PowerVR PCX1/2 (`powervra/d`);
  software renderer in the exe (`-no3dx`). Locked 640x480, 16-bit color.
- Registry: `HKLM\SOFTWARE\EA Sports\World Cup 98` (3D Card, Thrash
  Driver, Triple Buffer, Hardware Acceleration, Language, CD Drive).
- Audio: `eacsnd.dll` runtime-loaded, version-checked, 13
  `_iSNDdirect*@` exports, uses `DirectSoundCreate`; formats BNKl/EA-XA,
  SCHl, CRD, PT streams (.SMP/.PBK), MIDx; **no** waveOut/MIDI path.
- Networking: WSOCK32 all-ordinal datagram-only (sendto/recvfrom/select;
  no connect/listen) + "DOS IPX compatible"/GAMELIST/MACHINE strings;
  modem play fully implemented (TAPI, AT S95=47, modems.cfg).
- Shipped binaries carry Watcom debug blobs (ELF-magic stubs, C source
  paths `C:\3rash\d3d\*.c`, `D:\audio\lib\win\sdirect.c`) — driver
  internals recoverable from the DLLs themselves; the exe is stripped.

## Confirmed — data (counts verified by scanning the user's files)

- `BIGF` archives 1,273 (incl. 1,232 `.PBK`), RefPack 797 payloads (all
  793 loose `.PAK` = FSH textures, 791 unpack to SHPI), FSH 411 files /
  6,356 bitmaps, FFN 10, PCMD 14, GEOM/SKEL `.MDL` 12, MO rig 413 anim
  clips (`COMMON/ANIM.MO`), FCDB 2 + 22 STR0/STR1 `.DBI/.DBM` (24 leagues,
  56 teams, 1,782 players, 20-byte player records), LAY 29, FCLG 9,
  BNK 47, CRD 2, SCHl 37 (8 `.ASF` music incl. Tubthumping, 29 `.DCT`
  video), EULZ/Squish `.SQF` 49, TRE 9, `USER/OPTIONS.SAV` = `AFIFAFIF`.
- `.DCT` EA SRE video **decoded + playable** (2026-11-06, pass 8): SCHl/mTCD container, MPEG-1
  VLC + EA scan swap (zigzag pair 10/17), escape `000001`+run5+level8, EOB=`10`, 2-bit cell
  header / 6 blocks per 8x8 cell, luma 4x4 quadrants + V/U chroma 4x4 (b4=V, b5=U), float IDCT
  +128. Full-stream verification: INT_FIFA.DCT 1,017 frames deterministic checksums; all 29
  files (9,915 frames) scan clean. Gates `test_dct` + `video_play`; `wc98-native --video-play
  FILE.DCT` plays through the Vulkan renderer at the record cadence (56 ms → fps=17.9).
  `docs/asset-formats/VIDEO.md`, `docs/reverse-engineering/VIDEO_DCT_NOTES.md`.
- FIFA 99 native toolchain validated against WC98 (see REUSABILITY.md):
  core, BIG/RefPack, FSH (98.5%), FFN, PCMD, MO/ANIM (404/413),
  GEN4TBLS, FCDB names/players, FCLG, LAY grammar, all audio codecs
  (BNK 47/47, SCHl 37/37, CRD, MIDx), OpenAL mixer — COMPATIBLE.
- M4 rendering proven on real GPU offscreen (ctest gates green, PNGs visually verified
  2026-11-02): FEFLOW02 full-frame bitmap; `.LAY` composites of FE main menu (FEFLOW05) and
  CONTROL1 via `.FSH`/RefPack `.PAK` art resolution (LAYOUTS.BIG/IGLAY.BIG members); FE mouse cursor
  RESOLVED 2026-11-06 — engine hard-loads `.\fe\pack\maus.pak` (FIFAWC.EXE file offset 1451894), the
  28x26 `curs` arrow draws over every FE composite (gates `test_presentation`, `fe_render_tournament`);
  CONTROL1's `redleft.fsh#mous` is a shipped-asset gap (REDLEFT.PAK = keyb/joys/gpad/grav only, the
  original skips it), not a loader bug.
  trophy (`WCUPF.MDL`: 2 groups, 416 faces, 9 FSH textures) and stadium bowl (`BRDXF`) through
  the GEOM/SKEL parser — see reverse-engineering/{LAY_COMPOSITOR,MDL_GEOMETRY}_NOTES.md.
- Player art end-to-end via `wc98-model-export` (2026-11-02): skinned `PCMD` players
  (`HIG_30`: 28 nodes, 320 tris) posed by real `ANIM.MO` clips; kit atlases `PK###A/H.FSH`
  bind 10/13 materials (harvest mode); stands 15/21, trophy PCMD 1/1 textured.
  Kit/tag map + quirks (`hjbx`↔`hjbk`, palette-only face `hifa`) in MILESTONE4.md.
- HUD slice (2026-11-05, fidelity pass 2026-11-06): presentation overlay draws the match scoreboard strip, `DAT_0065F1C8` clock,
  radar from `MISC8.FSH` rpit/rsp*/rpl* art at exe-recovered rect (float block VA `0x55a9a8` = 533,67
  180×120), FCDB STR0 team abbreviations, strip face `FE/ERAS10.FFN` (font-table slot 0, FUN_00438670@0x4386de;
  eras14bi REFUTED for the strip) with clock/score text `"%02d:%02d"` (@0x43888c) and shirt-number
  labels; scoreboard strip rect + ball marker are Hypothesis behind 15 `[gameplay] hud_*` knobs. Gate
  `test_hud`; docs/reverse-engineering/HUD_WC98.md (items 2/4/5 RESOLVED pass-10; open: strip-rect recovery depth, radar kit recolor).
- WC98 deltas requiring adaptation (final, locked by port test gates —
  see REUSABILITY.md section 12): FCDB team record 68 B/17 words
  (FIFA 99: 80 B/20); roster = 20 little-endian u16 player-id slots
  (bytes 24..64; 984/984 slots resolve — FIFA 99's big-endian resolves
  31/984), squad size = filled slots, byte-22 nibble = floor(squad/2)
  (odd squads 11/13 measured; 40/56 teams slot-capped at 20); shirtStyle is 6-bit
  (measured max 32); FSH 0x2D/0x29 palette records + global fallback
  (PAL.PAK = RefPack→SHPI→type-0x01 palette, resolver yields 256
  colours); PT v2 .SMP streams are 756/756 MicroTalk (tag 9) with
  header-relative 0x88; .PBK = BIGF archives whose members each carry a
  20 B phrase record + `PT` v2 play track at offset 20 — all speech decodes
  via codec-tag routing, 6,964/6,964 members measured 2026-11-01 (gain chain closed 2026-11-05 —
  `COMMENTARY_WC98.md` §gain); 1998 PT v0 nests the
  standard meta tags (0x85 samples / 0x82 channels / 0x83 codec)
  inside 0x65 blocks (.ASF/.DCT audio) and 0xa2 meta blocks (.BNK
  SFX members, which also use a 1998 tag dialect 0x06/0x5a/0x91 —
  payload decoding open); 4 of 36 .BIG archives are RefPack-wrapped;
  ANIM.BIG carries the 48-byte prefix; INSTALL ships header-only
  11,628-byte PBK stubs (count field 1290) — archive parsing must be
  tolerant; G98*.FMT names (KO 68 B i8, PK 136 B i16-LE, PL/FK
  20,332 B i8, no AT/DF).

## Confirmed — product (external, cited)

- 40 teams (32 qualifiers + 8); World Cup mode with real 1998 groups,
  no ET/shootout in groups, golden goal/shootout in friendlies; 15-match
  World Cup Classics (unlocked progressively; B&W/sepia presentation
  for 1930–66 matches; pre-1970 matches no substitutions; Wolstenholme
  commentary in Classics; UK build Motson/Waddle + Lynam/Lineker).
- Disc check required; Glide mode 63 FPS cap; nGlide fix = replace
  `thrash/voodooa.dll` + registry fix (PCGW's "voodoo2a" is a typo).
- No prior World Cup 98 reimplementation exists (GitHub searched, 2026-11).

## Strong hypotheses

- Simulation is deterministic (netplay lockstep in FIFA 98/99 readmes;
  same engine) → enables replay/regression harnesses.
- LAN play is IPX-derived broadcast (datagram-only imports + GAMELIST
  broadcast strings); DirectPlay absent.

## Unverified / Unknowns (next RE targets, ranked)

1. FE typed UI layer: LAY object-type vocabulary RESOLVED 2026-11-07 (pass-10, commit da2e4a7) —
 registry driver FUN_00447910 -> registerClass FUN_00472fd8 with EAX imm32 = objecttype; all 10 former
 unknown ids proven (animseq 3, sbar 11, knobbar 19, tbb 32, flg 37, menu 42, box 43, kt 44, btn 45,
 sliderbtn 47; cited sites in docs/formats/LAYOUT.md), gaps 13/25-29/38/40/41 = no class (empty name,
 asserted). Remaining FE art pipeline: SQF EULZ sprites BLOCKED (pass-9eulz); MDL gated.
2. Speech/commentary indexing (decoder itself is COMPLETE: 6,964/6,964 `.PBK`
   members decode via PT-tag routing, measured 2026-11-01): PHRS.INF index semantics RESOLVED
   2026-11-06 — `PHRS.HDR` == byte-exact first 0x2d6c bytes of `PHRS.PBK` (the full BIGF TOC = 1,290 LE {u32 offset,u32 size} pairs at 12+8*i; member sizes == decompressed sizes, no RefPack), INF = {u32@4 count=1290, u32@8 0x146f0 value-unexplained, "V8-7-4"}; rate default when tag `0x84` is absent RESOLVED 2026-11-06 — 22,050 Hz confirmed
   from `EACSND.DLL@0xe01` (`mov [0x40aa10],0x5622`, driver global rate var; see AUDIO.md).
Gain calibration resolved 2026-11-05 (`analysis/pass7/gain*.txt`, `COMMENTARY_WC98.md` §gain):
EACSND linear vol chain (domain [0,0x7f], 16.16) saturating at 0x7f + byte curve table explains
the hot levels; port implements SNDspeechautovol ramp (`SpeechGain`, gate `test_commentary`).
3. `.SQF` EULZ sprites **BLOCKED** (pass-9eulz 2026-11-07): slot `0x593e94` has zero write sites in the
   whole image, no setter in any shipped DLL, stays NULL through all SQF loads under Wine; winedbg has
   no data-watch capability — expander is installed by a runtime mechanism absent from shipped binaries.
   Only remaining route: Win9x-era env (DOSBox-X debugger). `test_sqf` pins raw-payload FNV hashes.
   `.MDL` GEOM/SKEL gated (`test_mdl`: 12 FE models / 113 groups / 4,886 faces); `.DCT` EA SRE video RESOLVED
   2026-11-06 (`VIDEO_DCT_NOTES.md`, `docs/asset-formats/VIDEO.md`); inter-cell motion compensation = Hypothesis.
4. `.SCI` AI-script bytecode (VM unparsed, parity with FIFA 99), GEN4TBLS AT/DF. `AISEQS.TBI`
   index refined 2026-11-07: clip pool = stride-24 tagged records `[fourcc pair][seq<<16][TBA keyframe
   offset]`, 417 entries (`AI_SEQS_TBI.md`).
5. Save/replay/presentation files: see `docs/reverse-engineering/SAVES.md` (options.sav layout,
   `.his`/`.tri` Classics data, network.dat/dial.dat). Replay framing decoded 2026-11-06
   (`REPLAY_WC98.md`: 100 slots stride 0x19, snapshot records stride 0x23c = idx + 0x238 state block);
   file bodies stay Hypothesis (no live samples).
   Wine oracle pass 2026-11-05 (`WINE_ORACLE_WC98.md`): live `USER/options.sav` dumped (96 B, magic
   `AFIFAFIF`) = runtime write confirmed; task tables `0x57fbc8/0x57fc40/0x581ac0` byte-identical to
   image at all observed states (never runtime-patched); replay/network saves still not generated
   (FE/match presentation never entered under wined3d - GPU path AV documented).
6. Official patch lineage: **RESOLVED 2026-11-06** - `fifawc98_patch.zip`, Ultimate Patch and the
   3rd-party Loader all fetched + diffed (EXTERNAL.md §4/§5; details under Known limitations).


## Progress snapshot (2026-11-06, v0.3.0 released)
- Second-pass forensic audit in progress (pass-15, 2026-11-10): discrepancy register + validation doc
  landed (`GAMEPLAY_DISCREPANCIES.md` D-01..D-54, `GAMEPLAY_VALIDATION.md`); trig + nine runtime ball
  tables proven byte-exact at **measured WC98 VAs** and gated by new `test_trig_tables` /
  `test_ball_tables`; stale FIFA-99 cites corrected; closed-form kick/flight constants recorded as
  FIFA-99-derived (register D-49, open); net-contact "Critical" claims refuted against the raw dumps;
  camera trig lane now uses the byte-exact WC98 table sine with engine fixed-multiply rounding
  (`test_presentation` replay-eye replica), closing the documented last-ulp drift.
- Pass-15 S2/S3 (2026-11-11): motion caps + turn-threshold rows landed byte-exact at `DAT_0058aee4` /
  `DAT_0058aef0` as a shared public header (`simulation/include/wc98/sim/motion_tables.inc`) consumed by
  `motion.cpp`, the new `test_motion_tables` gate and the self-test step-13 replica (the replica had carried its
  own stale FIFA-99 copy). Self-test now runs 14 steps incl. an animation-timing seam assert (kick clip selected
  before ball velocity appears). Input layer mapped end-to-end from FIFAWC.EXE (20 controllers stride 0x15C, player
  ptr +0x154, ctx machine slots @0x58d314 → fused gesture+predict → dispatch): `kButtonMask[9][8]`, `kCtxKeep[14][14]`,
  `kActionTable` (256/256 rows) and the dir index/remap pages are byte-exact **MATCH**; divergences registered as
  D-64…D-72 (gesture held-threshold ≥8 vs port ≥12, queue-gate table +2 offset / 96 size, WC98 drops queueable actions
  during `anim&4` — no FIFO in the 348-byte controller record, pause-skip sub-gates inverted, one device read per frame).
  D-56 was Open after a measured zero-speed regression; **pass-15 S3 landed the coordinated
  producer+consumer patch (2026-11-11)**: cap chain cascade 0x40/0x80/0x100 applied before the arrival
  band + sprint ceiling bit 0x100000 (cap 0x68000) + penalty mask 0x6000000 minus B0 + class-eval byte
  `&2` + collapse gate bit 4, fed by aligned producers (keeper 0x80/0x100, penalty sites, run-state
  `reqBits(0x11)`); pad-decode constants (gate `< 0x2001`, scale `mag*0x50>>0x10` via sinCos + bit-15
  carry, deadbands → bits 1/2/8/4, angle bands 0xaa/0x155 windows, digital remap `DAT_00580d9e`);
  gesture held-transitions at frames > 7 (states 1/4/6/8) with the state-7 port timer removed; single
  96-byte queue gate @VA 0x58c750 `(table[action]==1 && (player+0x83)&4)` and **FIFO deleted**
  (drop-only semantics, 348 B controller record); pause-skip sub-gates + replay matrix
  (`match.flags278` bit 0x2000). D-56/D-57/D-58/D-64…D-68 → Fixed; gates in `test_sim_players`;
  self-test locomotion windows re-measured on WC98-grammar carriers (movement 4321, sprint segment
  4193, stop-distance ladder green); suite **117/117**, JSON self-test 14/14.
- Pass-15 S4 (2026-11-11): motion remainder closed — `Motion_setTarget` unconditional (FUN_004F4E70),
  `Motion_warpTo` pure snap (FUN_004F4F80: atan2 facing write + turnDelta copy, no flag bit 8 / target
  reset; post-teleport heading now exe-exact), gait/speed-class producer replicated with the canonical
  `{id, rate}` pair-grid model (loco block = flat u16 view: header trio +0x0, central rows stride 0x2C
  @+0xC, early-return grids @+0xE8/+0x156; the declared `[row][col][pair]` indexing double-counted the
  stride). Gates in `test_sim_players` (D-59/D-60) + scenario gait lanes (cells/ladder/rate formula vs
  byte-exact `.data` dump); D-59/D-60 → Fixed, D-63 → Closed (structural), suite remains **117/117**.
- Pass-16 input/physics closure (2026-11-11): D-71 refuted on the byte lane + `test_input_tables`
  gate; D-70 per-frame pad sampling seam in the live loop; **D-69 cutover** — platform per-bit event
  deque deleted, direct physical-word sampling (`sample()` = current level), `test_input` asserts the
  exe tap-invisibility grammar; **D-73** PK-celebration crash fixed (winner-lane `possWinner == -1`
  guard in `Player_frameMove`). Self-test windows re-baselined on pass-16 physics: turning phase-gated,
  AI closing range anchored on live-ball ticks, sprint band asserted at the byte-pinned class cap
  (kSpeedCap[1] = 0x50000). Suite **118/118**, JSON self-test **14/14** incl. PK goal->celebration.
- Pass-16cam camera provenance (2026-11-11): D-54 param rows partially closed — FOV/task-height table
  proven at **DAT_0057FB82** (port `kTaskHeights` byte-exact, accessor FUN_004a0790 instruction bytes),
  FIFA-99 cite `FOV i16 0x57FAD8` refuted, caminit FUN_004a0810 + h12/h14/h28 preset rows byte-proven;
  every FIFA-99 `kModes` zone signature = zero hits in the image (rows stay family carryover). New gate
  `test_cam_tables`; cites relabeled per row in `camera.cpp`. Suite **119/119**.

- Pass-16kick D-49 close (2026-11-11): closed-form flight/solve rebuilt on the WC98 arithmetic grammar — math band `FUN_0051e530..FUN_0051ecbb` decoded, solver pool `DAT_005913e8..0x59143c` installed byte-exact as `kKickPool`, drag pair over `DAT_00590a20` + lambert tables `DAT_00590fe8/0x590be8`, norm pair `kNormScale/kNormBias` @`DAT_00591490/0x591514`. FIFA-99 `kFlightConst`/`kFlightSets` cites deleted; 13 absence asserts in `test_ball_tables`. Self-test passing/shooting windows promoted (speed vs solve ceiling, apex = `Flight_heightAt(Flight_timeFromVy(vy))`, arc descent); `scenarioFallRecovery` lane C re-baselined as a flight stop-zone gate. Suite **119/119**, self-test **14/14**.
- Release: tag `v0.3.0` + GitHub release (AppImage + `.sha256`/`.version` sidecars): playable live match,
  authentic FE fonts/language knob, FEFLOW11 team-select grid, measured percent-table scene defaults;
  game smoke exit 0 under Xvfb, launcher offscreen self-test pass, `update.sh --check` -> "up to date";
  CI matrix green incl. linuxdeploy job (ctest now per-test `--timeout 300` guard).
- Release: tag `v0.2.0` + GitHub release (AppImage +`.sha256`/`.version` sidecars); AppImage game smoke
  exit 0 on RX 7900 XTX under Xvfb, launcher offscreen self-test pass, `update.sh --check` -> "up to
  date" against live endpoint. Prior `v0.1.0` flow identical (clean-clone ctest + CI matrix GCC-13/
  Clang C++23 + GCC-16 C++26 green incl. linuxdeploy job, single-entry passthrough proven).
- Suite (tree build): 47 gates incl. M7 regression harness `test_sim_scenarios` (10 scenarios:
  kickoff/pass/shot-goal/offside/set pieces/foul-cards/HT-FT/shootout/extra-time/golden-goal,
  seed-pinned, 0.25 s),
  commentary chain (`test_phrs` mirror equality 1290/1290, `test_speechqueue`, `test_commentary`
  incl. exact-tick 16.16 gain asserts), tournament + slot save/load gates, FE render/nav gates.
- Wine oracle (wine-11.21): original boots + runs under Xvfb 16bpp; live `options.sav` (AFIFAFIF)
  captured; task tables == static image (never runtime-patched); wined3d 24bpp AV documented - FE
  visuals not reached (black presentation), no fabricated captures. `WINE_ORACLE_WC98.md`.
- Landed (2026-11-06): map-grid team-select, fontID faces + lang knob. FontID -> face CONFIRMED via exe
  font pointer table `DAT_0057d2c0` (raw 0x17b8c0: eras10/12/20/36/11i/8i FEFONTS.BIG + BI faces
  IGFONTS.BIG, slot 9 null; loader code VA 0x46933a) - labels draw their own face at native line
  height (`FeText::kFontNames`); `--lang CODE` selects FC{DAN,DUT,ENG,FRE,GER,ITA,JAP,SPA,SWE}.BIN
  (registry Language key; gates fe_text_fonts/fe_text_lang_ger). Full 9-bank load gate + FCENG vs
  FCFRE label-220 diff in `test_presentation` (2026-11-07). LAY_COMPOSITOR_NOTES.md.
- Landed (2026-11-06): percent-table scene defaults MEASURED live pass 7e - winedbg launch-under-
  debugger capture: setter triples FUN_0046b074 idx4=65/idx3=80, FUN_0046b130 loop idx{0,2,1,5}=80;
  live table DAT_00632778 = {80,80,80,80,65,80,1}; speech scene level idx4 getter -> 65 %
  (`Commentary::kScenePct/kSceneSpeechPct`, gate duckModel; `analysis/pass7/percent1.txt`,
  COMMENTARY_WC98.md §"Scene defaults measured"). Duck/mix table model (`ChannelMix`, pass 7d).
  Landed earlier: scene-percent seam (`ChannelMix`, pass 7d, `analysis/pass7/duck*.txt`); tournament module + progression demo + team pick
  (`--fe-pick`), save/load, HUD, FE flow w/ drawn text, commentary pick chain closed + faithful port
  model (pass 7c); suite 43.
- Landed (2026-11-06): INTERACTIVE playable match (`app/livematch.{hpp,cpp}`, gate `interactive_match`
  via `--live-match-smoke N`): FE Enter on Exhibition now launches a live on-screen loop - sim ticks
  real-time at the 1/50 s engine cadence, scene + HUD drawn every frame with camera follow, commentary
  pump every 2 frames off the event trigger seam, Esc/window-close quits; keyboard/pad mapping (S/W/A
  D-F-C-Q-E + pad West/North/Left/Right/Back/Start) drives controller 0 through the ported pad word.
  Gate evidence: shapes=340 uiVerts=264, pass bit 0x20 reached controller 0, clock advanced, phrase 0
  started. MatchBoot/snapshotFromWorld shared with `wc98-match-demo` (de-duplicated bootstrap).
  Camera init snaps to the tele target with one `tick(init, 600)` (live smoothing climbs eye.y only
  +/-frames/tick; from 0 it crawls for seconds - kickoff viewed near ground level looked like a
  ragdoll). Observation: first ~1 s after kickoff all field players sample prone/fall anim poses
  (GK standClip); upright by demo capture time t=1 s - sim-state, not a snapshot-layer bug.
- Landed (2026-11-07): hub screen (`Feflow06.lay`) as the interactive main menu when present - 13 wedges with hover/push/disabled
  shape states, FRAMES anim, help pane FC captions, keyboard + mouse widgetHit; edges fe07/fe11/fe32 to existing flows, `quit`
  exits, fe33/fe90/fe04/fe02 documented no-ops. Hub scene graph: options (FEFLOW15 tabs tbb1..4 + row cursor, audio volumes ->
  settings.ini/mixer), save/load entry (Feflow89 -> WCT1 load), credits roll (feflow19). In-game pause: Esc opens the IGMENU.LAY
  composite over the live match (INGAME.FSH bt*n/bt*d art, {Resume,Quit}, IGQUESHN confirm; sim frozen). Gates `fe_hub`,
  `fe_flow_options`, `fe_flow_saveload`, `fe_credits`, `fe_igmenu` (`--ig-pause-smoke N`).
- Landed (2026-11-08, gameplay parity lanes): scenario gates E/F/G/H/K/M/N/P + difficulty A/B gate,
  keeper-save cascade guard, `--self-test` rebalanced to 12/12 deterministic steps (fixed seed
  0x2E7A; ctest suite 112/112). RE pass 12: commentary per-event speech preload table decoded
  (`analysis/pass12speech/SPEECH_PRELOAD_TABLE.md`), tournament schedule generation closed +
  ZWCUP/ZINTER triple semantics validated as pre-'98 finals H2H snapshot
  (`analysis/pass12tourn/{SCHEDULE_GEN,ZWCUP_TRIPLE}.md`; TOURNAMENT_MODULE §5 items 3/4 closed).
- Landed (2026-11-08, pass-12sci): task scheduler model decoded (`FUN_004a0e50` frame dispatch,
  `FUN_004a0d80` tick; rec+8 handler index, flags bit2/bit4) - the ".SCI runtime-install" premise
  FALSIFIED: tabA/tabB handler tables are static image data (tabB = 30 goal/win/loss/inc vectors),
  .SCI descriptor array maps module ids -> tabB idx; all 30 handlers decompiled
  (`analysis/pass12sci/TASK_HANDLERS.md`; GAMEPLAY_PARITY §5.1 re-ranked, no Win9x-env blocker).
- Landed (2026-11-08, pass-12sci integration): camera dolly model + byte-proven preset/height/sine
  tables installed (`presentation::CameraTask`, `MatchCamera::glideTo`; TASK_HANDLERS_WC98 §7
  delta-1/2 closed) and `.SCI` payload parser across all 87 COMMON files (`formats/sci.cpp`, gate
  `test_sci`; delta-4). Suite ctest = 113/113. Remaining §7 deltas: kickoff placement task (3),
  quality-tier camera gate (5, pending presentation quality option).
- Landed (2026-11-08, pass-12sci §7 delta-3): kickoff placement task modeled from h6 `FUN_00516f40`
  (`simulation::KickoffPlace_init/step` in rules.cpp - side-aware dolly to the centre spot, spin
  ±DAT_00656e08 at the placement-phase tick; replaces the direct-set kick placement; livematch pose
  path consumes `kickoffPlace.bearing`; gate `scenarioKickoffPlacement`). §7 delta-3 + GAMEPLAY_PARITY
  §5.6 closed (superseded by the delta-5 lane below; see 2026-11-08 h23 entry).
- Landed (2026-11-08): commentary per-event preload + scheduler parity models wired
  (`app/commentary` SpeechPreload/EventDispatch/Scheduler; gates a-d in test_commentary).
- Landed (2026-11-08, pass-12kick): kick-plan power clamp + slot-0 rect predicate ported
  (`FUN_00502fc0` 39-64 helpers incl. `DAT_00585898` sin table; gate scenarioKickPowerClamp);
  SIM_PORT_NOTES "Kick family" open items closed (dossier `analysis/pass12kick/`).
- Landed (2026-11-08, pass-12sci §7 delta-5): quality-tier camera modeled from h23 `FUN_004cafe0`
  (`presentation::tierCamera`: tier deltas {0x71,0x3d}/{0x65,0x31}/{0x59,0x25}, pitch-edge x/z clamps,
  flags bit4/bit5 z-source lanes, dz<<4 polar + 0x640 angle/speed constants [P]; producer = task pool
  init `FUN_004a0810` argument, quality word DAT_00644778). `MatchCamera::setQualityTier` mirrors the
  word; `wc98-native --quality N` live option. Gate in test_presentation; §7 deltas 1-5 all closed.
- Landed (2026-11-08, pass-12sci §7): h21 ball-follow TV camera `presentation::followCamera` modeled
  (flag source-triple select carrier/pair/ball, inset-edge clamps ±(halfW−0x1e0)/(halfL−0x140),
  rec+0x104 {0x58,0xeb} = 0xeb58 [P]); gate in test_presentation; §7 camera-follow row MODEL-PORTED.
- Landed (2026-11-08, pass-12sci §7): h18 `sideCamera` (lane thresholds 0x1e0/-0x1df, far/near
  ±(zEdge+0x140), rec+0x110 mid band + forced-near guard, DAT_00644794-0x10 polar lane) and h20
  `overheadCamera` (-xEdge polar, 0x90) modeled; gates in test_presentation; §7 edge/tier row closed.
- Landed (2026-11-08, pass-12ai): TeamAI decision-family WC98 divergences pairs 46-48 in
  `team_ai_ball.cpp`: unconditional shot eval + threshold (0x50cfab/0x50d137), rand mask &1
  (0x50d6dd), no FIFA-99 extra draw in LAB_0050d9c8, ball-y constant 0x72 (DAT_00646920<0x73),
  budget high-half scratch lane + dual shift, two flag-gated counter tests vs f(0x34)/f(0x24)
  (0x50ded2/def5), high-half draw threshold replacing the global lane (0x50dee6..def0),
  give-and-go arm set f014 {5,6}, header-pass skip-lane restructure (shot-score gate, local_50
  forced cross-lift, deep test on iabs(z), return test k26>0x3c0). Suite ctest = 114/114.
- Landed (2026-11-08, sci9/sci12): scene-install producer sites (FUN_004aa148/1ba/267/2fc/485
  mode-selector writes, kit/side-gated camera zeroing, task unfreeze sweep FUN_004aa432) and the
  loader field-split layout proven at code level (FUN_004aaf00); TASK_HANDLERS_WC98 §8 closed.

- Landed (2026-11-08, pass-12air): WC98 pair 43/44 bodies located via threshold-table xrefs
  (`FUN_0050c820` onBallWon, `FUN_0050cc70` planAirKick) and installed - raw thresholds (no formation
  base), gate id 0x281, clearance = rand+offsetXZ point (target/nudge/lift-scale/0x72 clamp GONE), tail
  code bands >=0x3c1/>=0x1e1 clamps-removed; planAirKick clearance atan2+offsetXZ rel only, receiver
  stays -1. Scenario gates A/D (`scenarioGaitTransitions` live gait ladder + `scenarioReceiverContest`
  duel lanes: uncontested completion transfer, contested aerial = Header deflection per pair-43/44)
  close GAMEPLAY_PARITY §4 gaps A and D; suite ctest = 114/114 (self-test 12/12). Dossier
  `analysis/pass12air/ai2_xref_tables.txt`.

## Known limitations

- Interactive playable match (M10 `app/livematch.cpp`): the key/pad action mapping is a port convention
  built on the ported input layer (bindings = config/src/settings.cpp defaults, pad-word assembly in
  input/src/input.cpp); the live loop reuses the M6 scene/HUD/camera as-is - no new presentation
  fidelity claimed.
- Live match first-second pose: mass-prone field-player poses at the kickoff whistle moment are sim
  data (open-play poses verified upright in late live captures); t<1 s demo probe closed 2026-11-06 -
  faithful exe data path, not a seeding bug: idle loco selector is speed-gait row col 0 = anim id 358
  (`kLoco` verbatim from exe 0x59F918), AISEQS.TBI id==index holds across all 534 records -> clip 278
  `bsppob1`, whose name-table entry is loaded (flags +0xD = 0, clip ptr 0x94ac a4) and FK-decodes as a
  fall/prone clip (head bone y 424->~90 across keys 0..128, span 0x200 => sampled mid-clip in the first
  ~1 s); GK keeps standClip via keeper state path. Instance advance/key math (`anim.cpp` 0x460050 /
  0x45FC30) matches the exe addresses; pose sampling in `snapshotFromWorld` tracks instance keyPos, so
  the snapshot layer is exonerated as claimed. No code change made.
- M6 vertical slice playable headless (`wc98-match-demo`, gate `match_demo_frames`); commentary
  integrated in the exhibition loop via `app/commentary.{hpp,cpp}` (trigger seam FUN_004893ec model ->
  SpeechQueue -> PHRS segment fetch/decode -> OpenAL Mixer, null-mixer headless; gates
  `test_commentary`). Event->phrase pick chain CLOSED (pass 7c, 2026-11-06): `FUN_0048d0dc` = qfind over
  the class-keyed ready list (`DAT_00632428 + [slot+0xC]*4`) with slot-tag predicate `FUN_0048cf10`;
  activation `FUN_0048d1f8` removes the picked record (consume-on-use), null pick -> silent. Port pool
  consumed in storage order, per-event tag table unmeasured (marked in code); COMMENTARY_WC98 §pick,
  evidence `analysis/pass7/pick*.txt`. FCLG cross-map probed negative (`analysis/pass7/fclg_probe.txt`);
  gain calibration + autovol ramp closed 2026-11-05 (`analysis/pass7/gain*.txt`, §gain; `SpeechGain`).
  duck/mix table model + scene-percent seam closed 2026-11-06 (`analysis/pass7/duck*.txt`, §pass 7d; `ChannelMix`).
- Record queue plumbing confirmed (2026-11-05, `analysis/pass7/rec1.txt`, GhidraWc98Rec1.py on saved wc98
  project): `FUN_004c4800` = cmn_queue push (qbottom, line 0x162), `FUN_004c4890` = pop (0x1cb),
  `FUN_004c4ce0` = qfind with predicate callback (0x431); node +0x00 = next link, queue object fields
  count/flags/head/tail. Phrase-record consumers of +0x04/+0x08 therefore are the linked-list slots
  written by push (`*prev->next = node`) - hypothesis "queue links" confirmed; pick `FUN_0048d0dc` is
  gate DAT_0063241c + qfind, drain `FUN_0048d1f8` pops all + per-item handler FUN_0048d0c0.
- Front-end flow (FEFLOW05 nav + team-select + fonts/lang) and in-match HUD landed; slot save/load
  landed (`SLOT%.2d.SAV` codec, tournament state persisted per round, `--tournament-saveload` gate).
- Gamepad FE nav landed (platform `Window.poll` padButtons/padNav + canonical `PadButton`, app FE loop
  South/Start confirm, d-pad/stick nav with 0.25 s repeat): CI gates cover mapping, throttle and virtual-pad
  poll (`test_gamepad`) plus pad-less dummy-driver startup (`gamepad_smoke_dummy`); real-hardware path is not
  CI-gateable — verify manually: plug a gamepad, run `wc98-native --fe --tree <tree>`, d-pad/left-stick moves
  the FEFLOW05 selection, South (Xbox A) launches.
- Raw hexdump appendices of game files are kept out of the repo
  (copyright); research-inbox/ is gitignored.
- Probe logs under /tmp are ephemeral; durable record = counts in docs.
- Community patch lineage VERIFIED 2026-11-06 (nGlide `fifawc98_patch.zip`, Ultimate Patch, 3rd-party Loader; EXTERNAL.md §4/§5): all three reduce to dgVoodoo/nGlide wrappers + the same SDB compat trio + registry voodoo forcing; **the community "patches" share one engine-level byte**: file offset 0x38440 (VA 0x439040, `FUN_00439028` CD-ROM FIFAWC.EXE probe) JNE→JMP = no-CD early-return; Ultimate/Loader additionally ship a community-modified `THRASH/voodooa.dll` and set `Thrash Resolution=1920x1080` — the confirmed registry key is a real hi-res hook, thrash output not hard-locked to 640x480.
