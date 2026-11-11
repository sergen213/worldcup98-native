# Changelog

All notable changes. Versioning: `0.x.y` pre-1.0; 1.0 = full World Cup
tournament playable natively, per the definition of success in the
project goals.


## [Unreleased]
### Added

- **Striker↔keeper contact resolution + D-30 premise refutation (pass-18keeper)**: RE pass over the
  brief's cited sites (`analysis/pass18keeper/DECOMPILE/keeper1.txt`, PyGhidra script
  `ghidra-scripts/GhidraWc98Keeper1.py`) shows the "contact branch" region `0x4c0980..0x4c0a20` is
  the `FUN_004c09a0` switch **jump table** (`JMP dword [EAX*4 + 0x4c0978]`; no function owns
  `0x4c0978..0x4c099c`) plus the resolver prologue, and the cases 3/4 "keeper velocity clamp" rows
  are ball wall bounces on integrator slots +0xb98/+0xba0 (×0xc000 negate, ±0x50000 clamp under
  `DAT_00646a24` bit 0x40) with zero player fields; the resolver's only xref is the sweep arbiter
  `FUN_004c1730`, and `FUN_004c08d0` stamps goal-line slots on the goal-side record `DAT_00645cb0`.
  **D-30's static premise (a player↔keeper collision table at these sites) is REFUTED**; body
  contact lives in the block lane already ported (`simulation/src/keeper_save.cpp`: 0x543390 rows,
  0x5437D4 skeleton capsules, 0x5434D4 reflection ×0x8000). New gate `scenarioKeeperContact`
  (`tests/unit/test_sim_scenarios.cpp`): each 0x543390 precondition row refused in isolation
  (keeper-hold window, possessor, height 0xE4, zero velocity, self-toucher, state 0x307); with all
  rows open the keeper deflect path resolves the contact — momentary take+release, loose-ball flag
  cleared, team claim slots `f1e0` (0x65A4B0/0x65A878) + DAT_0065F2A0 cleared, kickCount +2
  (take-consume row + reflection re-kick), reflected |vel| = half the incoming speed within the
  fixed-point reflect band; resolution is bit-identical across two seeded fixtures, and a live
  seeded shot at an on-line keeper keeps identical event streams run-to-run with outcome ∈ {Save,
  Goal} mutually exclusive (seed 0x2E7A resolves as a goal past the keeper). Registry: D-30 → Closed
  — premise refuted (residuals: cross-claim band qualitative, WC98 anchoring of the block chain
  Blocked); D-16 residual narrowed to the simultaneous-reach contest table with the keeper-deflect
  predicate rows now gated. Suite **123/123**.

- **SMIX voice-layer port (pass-18voice, D-54 audio residual)**: `audio/include/wc98/audio/smix_voice.hpp`
  extends the pass-18smix record model with the decoded residual grammar: ramp helper `FUN_00548a08`
  on the record +0x68 byte window (gate/mult byte +0x6B; signed-target midpoint store then
  `(gate*mid)>>7` overwrite, gate-zero clears the ramp-out flag), pan setter prev-pair compare +
  direction flags (`FUN_00548a58`), voice start/stop record rows with the serve-pump hook
  `(*DAT_0066eeac)` (`FUN_005483bc`/`FUN_00548960`), frequency clamp **0x80000** boundary rows, the
  centi-ratio pitch path `FUN_005492b0`/`FUN_005490b0` over the 13-entry power table DAT_00596144
  (`2^(2^k/1200)`, multiply/divide per set bit, ROUND(base*scale)), and the TLV sample-format parser
  `FUN_0052d290` as a pure function with sign-extension widths per `FUN_0052e160` + full tag-table
  round-trip (default waveformat **0x5622** = 22.05 kHz mono16; rate map {8000..44100}; loopEnd aux
  clamp `<-1 → -1`). App lane (`app/game_audio.cpp`) starts each voice through the FUN_005483bc
  record rows; default serve-pump slot is a no-op so v0.3.0 stream gains stay bit-identical (unity
  at 100% scene levels). Gates: new `test_voice_layer` — ramp convergence exact at i/127 fractions,
  clamp boundary rows, pitch table rows + replica sweep, TLV round-trip + truncation guards;
  `test_smix_grammar` gains the raw gate/midpoint/ramp byte rows. Registry: `docs/asset-formats/AUDIO.md`
  §Voice layer closes the D-54 audio residual (EACSND envelope/streamer dispatch and HW3D pos3d stay
  documented-open). Suite **123/123**.

- **Live set-piece restart cutover (pass-17restart, D-27 live-wired)**: `Match_setpieceLive`
  (`simulation/src/rules.cpp`) runs at seq4 step-0 per restart: band + box init from the stadium id,
  live trajectory record → `SetPieceRec` feed (driver_0x4c1730 fields), sweep arbiter accumulator time
  for the interpolated triple, and the placement machine on the restart→selector map {corner 1,
  throw-in 2, penalty 9, goal kick 10; ids outside {1..10} leave the lane inert}. The rounded triple is
  applied as a dedicated tasking record `MatchState::setPiecePlacement` (the exe ball-block +0xccd..
  slot alignment stays [P]; the flight-prediction landing field and input-lane aim/kind slots are not
  machine-owned). Mid-match band adjustment `FUN_004c0150` ported as `SetPiece_adjustNet` (netCase
  skip-gates, sweep-miss rows, netCase-5 damping ×0x3333 + clamp ≥0x30000, post-lane mouth flip row).
  Gates: test_setpiece_grammar section 13 (damping/miss/skip/flip/lane rows), scenario
  `live-restart-machine` (corner + throw-in through the live lane vs independent replica on seed
  0x2E7A), self-test step setpiece-live. Trajectory-record state-row application stays [P]
  (per-handler row-capture parity open, registry D-27). Suite 122/122, self-test 20/20.

- **Set-piece grammar port (pass-17, D-27 grammar-installed)**: new `simulation/src/setpieces.cpp` +
  `setpieces.hpp` replicate the WC98 restart grammar from the dossier (`/mnt/1st4TB/WorldCup98/analysis/pass17setpiece/`):
  pitch-band initializer `FUN_0049e400` (stadium rows {0:(0xff0,0x1620), 1:(0xf00,0x1500), 3:(0xe10,0x12c0),
  default:(0xe40,0x13e0)} + derived box/circle/half/quarter/fifth/third rows incl. DAT_006412f8=DAT_006412fc=6),
  box-band writer `FUN_004bf1e0` (goalNear/Far, crossNear/High; aspect [P] = trig-pair soup model), selector
  arbiter lane of `FUN_004c1730` (`SetPiece_arbitrate`: goal-line band→1, touchline→2, velocity bands→3/4 with
  FUN_004bf390 fixed-point time, box-band→9; earliest-interpolation accumulator local_1c=0x10001) incl. the
  cylinder-sweep quadratic helper chain (FUN_0049e16c square / FUN_0049e1d0 dot / FUN_004a19b8 / FUN_0049e13c sqrt /
  FUN_0049e220 split divide), and the placement machine `FUN_004c09a0` (`SetPiece_place`, cases 1/2/3/4/5/6-tail/
  7/8/9/10: velocity clamp ×0xc000 ±0x50000 under DAT_00646a24 bit 0x40, fx/fxt rounded triples, `>>0x10` ball-block
  writes, state/subState/flag bytes). Gates: `tests/unit/test_setpiece_grammar.cpp` (initializer table rows per
  stadium, box-band rows, six arbiter segments, per-case exact rounded-target asserts) + self-test steps
  setpiece-corner / setpiece-throwin / setpiece-goalkick / setpiece-penalty on the default-stadium band. Live
  restart cutover (rules spots -> machine) and FUN_004c0150 mid-match band adjustments stay open; registry D-27
  marked Partial. Suite 121/121, self-test 19/19.
- **Pass-lane dribble/pass grammar cutover (pass-17ai, D-22 closed)**: `team_ai_ball.cpp` onBallAct
  lane re-modeled on the WC98 passlane dossier (`analysis/pass17ai/DECOMPILE/passlane_0x50cf50.txt`) —
  fused retarget gate `L_4de3b8` = own-box flag || loose-ball designation (team +0xF0) || attacking-half
  flag → shot-select retarget (dossier L.410-412); budget ladder local_68 table base + scratch draw
  local_78 high-half semantics (L.310/313/557), near-chaser `distBall>>6` clamp [0x18..0x30] (L.362-372),
  counter DAT_006561dc exit increments consolidated net-0 (L.359-361, 378/383/390, 698/701); header-pass
  body drops the [0..0x640] lift clamp tail (belongs to pass-lane LAB_0050d7c3 only; hdrpass dossier
  L.252-275) with kickSpec kind/cls from fr codes. Self-test shooting/physics steps gate release
  attribution on `ball.lastToucher == shooter && towardGoal` and close the arc window when the keeper
  deflection takes the touch (driven apex obs/pred 23/22 MATCH). Registry D-22 closed; D-21/D-23 grammar
  rows marked installed. Suite 120/120, self-test 15/15.


- **Pass-17 ref — referee lane live capture bounded; motion-record array located (D-28 narrowed)**:
  the WC98 per-slot motion record array is decoded from the tick chain (`FUN_0049daf0` callees):
  handler `FUN_0049bd80` indexes base **0x63C000 stride 0x7F4** — pos x/y/z rec+0xA0/0xA4/0xA8,
  focus ptr +0x114, class +0x13C, clip array +0xB4 (count +0x770); pair-distance producer
  `FUN_0049c330` (motion-ptr array DAT_0063c98c). REFUTED: rules.cpp cites `0x46F2EC/0x46F39C/
  0x470C80/0x472244/0x473042` and globals `0x6675F8/0x66764C/0x65E018` have zero imm32 occurrences
  in FIFAWC.EXE — FIFA-99-note convention (scan pinned in dossier). Live winedbg LAUNCH lane proven
  boot-to-FE with fileopen landmarks (`bp *0x5291b0`) + XTEST key acceptance at FE; match scope not
  reached (nav grammar open, residuals R1-R4); per-slot sampler `run2_slots.py` ready for a future
  kickoff-nav capture. No simulation constants installed (none measured); suite untouched. Docs:
  D-28 row narrowed. Dossier `analysis/pass17ref/{NOTES.md,DECOMPILE/,logs/}`.

- **Camera preset-record grammar decoded + FIFA-99 `kModes` deleted (pass-17cam, D-54 closed)**: the
  live per-mode camera grammar is byte-derived from WC98 — 11 selector FUN_004ca880 records at
  DAT_00586{08c..248} (stride 0x2c) installed as port `kPresets` (camera.hpp: flags@+0x8, s16 radius/x
  lane +0xa, height +0xc, s16 z-lane +0xe, center pair +0x16/+0x1a, yaw word +0x1c, pitch word +0x20,
  smoothing word +0x26); consumers decoded (FUN_004caa70 case-3 direct eye-trio writes cam+0x108/
  0x10c/0x110; case-1 orbit anchors DAT_00649324/28 + side clamp ±(DAT_0064477c+0x140) + yaw window
  {0..0x7fa6} wrap-clamp; FUN_004a0f40 task-setup field reads). Live-patch inputs documented: case-3
  rec+0xe = ±(DAT_0064477c+0x210) / rec+0xa = DAT_00649330, case-5 (DAT_00586244·coord)>>16 round-pair
  + height 0x180 + pitch 63000. Gate `test_cam_tables` §12 pins all 11 records × 9 fields == image
  bytes, the case-5 constants, and mode dispatch bytes at 0x4ca88f/0x4ca7b5 (selector DAT_00580df0).
  Dossier `analysis/pass17cam/{NOTES.md,DECOMPILE/consumers.txt}`. Suite 120/120.

- **Pass-17 S5 — snap band, anim-rate closure, FE pad gate**: WC98 `FUN_004F5000` decoded as
  setPosAndTarget and installed as `Motion_setPosAndTarget` (pos store + `setTarget(pos)` insertion +
  atan2(ball) facing pair + dir8 quantize + zeroing block; gate `scenarioSetPosAndTarget`) — the band
  0x4f4exx contains no target-less snap, so `Motion_snapTo` is documented FIFA-99-derived (D-06 residual).
  D-09 closed: the FIFA-99 anim-rate switch `{e666,f777,10888,11999}` is removed from
  `Player_setSpeedScale` — f777/11999 have zero occurrences in the image; the `DAT_00581bd8..0x581c4f`
  ladder is FE config (single ref @VA 0x4b16b7, `FUN_004b1690`); gate byte pin + scans in
  `test_motion_tables`. D-61 closed (docs): u16 gait/turn grids are runtime-built by `FUN_004FC2C0`,
  byte-exactness = pre-fill only. D-72 closed measured: self-test step `fe-nav` drives the headless FE
  shell with scripted pad words (hub cycle → FEFLOW07 open/back). Anchors refuted: `DAT_00580200` is a
  character-class table at text-edit sites (D-02), `FUN_005108b0` is an FE keyboard-combo gate (D-08);
  D-07 heading-slot consumer enumeration landed. Suite 119/119, self-test 15/15.

- **Scene sound-level layer ported (pass-17s6audio)**: the exe's percent-table mix layer is decoded
  and installed — `DAT_00632778[6]` init 100 (`FUN_00490728`), setter + per-channel notifier dispatch
  over `DAT_004904b0[1..5]` (commentary/music/stream/EA-XA queue/CD, `FUN_0049067c`), Q7 gain
  `(pct*0x7f)/100` clamp-to-1 (`FUN_004906a4`) and signed scaled multiply (`FUN_004906d0`); runtime
  scene row `{80,80,80,80,65,80}` measured live in pass-7. Port: `audio/src/scene_levels.cpp`, music +
  crowd-stream Q7 wiring in `app/game_audio.cpp`, gate `test_scene_levels`. Headline refutation: the
  pass-16 "streamer/envelope" seeds are the **THRASH 2-D renderer** (`DAT_00632a68` =
  `_THRASH_drawline@8` import slot, string @0x567854, installed by `FUN_004916b0`; clip-rect float
  `DAT_005e6020`; drawline interpolation at 0x42a140); cited VAs `musicsetup 0x42b780` /
  `volenvelope 0x439c70` have no function, zero refs, zero byte hits. Dossier
  `analysis/pass17s6audio/{NOTES.md,DECOMPILE/}`; docs section in AUDIO.md. Suite 120/120.

- **Closed-form flight/solve rebuilt on WC98 grammar (pass-16kick, D-49)**: the FIFA-99 log/exp
  decomposition is deleted; `flight.cpp` now uses the decoded WC98 math band (fx dot / EA fast-norm
  over `DAT_00591490/0x591514` / cross-pair / rotate2d) with the solver pool `DAT_005913e8..0x59143c`
  installed byte-exact as `kKickPool`, drag pair over `DAT_00590a20` and lambert tables
  `DAT_00590fe8/0x590be8`. 13 FIFA-99-absence asserts pin the deleted cites in `test_ball_tables`;
  self-test passing/shooting windows promoted to measured flight-shape gates;
  `scenarioFallRecovery` lane C re-baselined as a probe-measured stop-zone gate.
- **D-71 refuted + gate (pass-16)**: WC98 dev5 input-context slots decode LE at 0x58d314 to ids
  4/9 — byte-exact with the port; the claimed deltas were big-endian reads. New `test_input_tables`
  lane pins all 9×14 slot decodes + queue gate @0x58c750 (D-66/D-58).
- **Input queue cutover to WC98 grammar (pass-16, D-69)**: per-bit event deque deleted — `recompute`
  assigns the physical pad word directly, `sample()` returns it; a tap opening and closing between
  two device reads produces no event, exactly as in the original. `test_input` asserts the grammar.

- **Camera parameter provenance (pass-16cam, D-54)**: the FOV/task-height lane is WC98-proven at
  **DAT_0057FB82** (port `kTaskHeights` byte-exact; accessor FUN_004a0790 reads `[cam+8]`-indexed
  dwords, instruction bytes asserted) and the FIFA-99 cite `FOV i16 0x57FAD8` is refuted (an
  ascending u16 pair table there). caminit FUN_004a0810 (FOV default 0xe28 at EBX[0x1e]=+0x78 +
  record defaults), the h12/h14/h28 preset rows and the case-5 preset pitch field (63000) all proven;
  every `kModes` FIFA-99 zone-table signature is absent from the image, so those numerics

- **PK-celebration crash fix (pass-16, D-73)**: `Player_frameMove` winner lane guarded for
  `possWinner == -1` (keeper branch = far mirror, steering skipped, head falls back to facing); the
  self-test PK goal->celebration tick survives end-to-end. Self-test windows re-baselined on pass-16
  physics: turning phase-gated, AI close-range anchored on live-ball ticks, sprint band asserted at
  the byte-pinned class cap (kSpeedCap[1] = 0x50000). Suite 118/118, self-test 14/14.

- **Live-loop pad sampling seam (pass-16, D-70)**: the live match loop now reads the device once
  per game frame (`in.fill(1, framePad)` after the event pump) and every sim tick drained from that
  frame consumes the shared word - the exe's `FUN_0049daf0` controller-update grammar. Multi-tick
  frames can no longer surface a fresh device word mid-frame. Gates: fe_* lanes, `interactive_match`,
  `match_demo_frames`, `replay_record_play` green on the new seam (68/68).

- **Input-table provenance gate + D-71 refuted (pass-16)**: `test_input_tables` pins the whole
  SpInput byte lane to FIFAWC.EXE - button masks @`0x58c7b4`, all 9x14 context slot pointers
  @`0x58d314` **decoded** to action-table ids (dev5 ctx03 -> id 4 @`0x58cd14`, ctx10 -> id 9
  @`0x58cf94`), the full 2048-byte action-table lane @`0x58cb14`, the keep-mask lane @`0x58d8fc`
  and the single queue-gate array @`0x58c750` (D-66/D-58). Registry D-71 **refuted**: the cited
  deltas `0x58cd94`/`0x58cf14` were big-endian reads of these little-endian slot bytes - the port's
  `kCtxTable` rows (incl. dev5) are byte-exact WC98 values; no code change needed, gate added.

- **Ball collision/goal-mouth grammar closure (pass-16, D-14)**: WC98 bodies decompiled and
  verified row-for-row — net-rest tail `FUN_004c0150`, resolve switch `FUN_004c09a0` (post/bar
  bounce helpers `FUN_004bf660/0x4bf760`, netDamp `FUN_004c12e0`, reflect `FUN_004bf490`), goal-mouth
  classifier `FUN_004c159c` (geometry predicates `FUN_004bf9e0/0x4bfb40/0x4bfcc0/0x4bfdd0`). The
  FIFA-99-shaped physics pair installed from measured rows: gravity `-0x2468` (was 0x2f5c), air drag
  `0xcb` (was 0xeb), net-rest bounce `0x2468` (byte-pinned `mov [ball+0xb9c],imm32` @VA `0x4bfaf8`).
  All ball.cpp cites rewritten to WC98 VAs. Gates: NetRest scenario re-baselined to the measured
  trace (17k fx entry speed bled to rest, netPos crawl), `test_ball_tables` byte pin, fx sample row
  in `test_sim_core`. Registry D-14 → Fixed; roof-lane reflect coefficients stay `[C]` (D-15).

- **Motion remainder batch (pass-15 S4)**: `Motion_setTarget` = FUN_004F4E70 unconditional store
  (bit-0 clear + dx/dz/oct-dist, no flag/equality gates), `Motion_warpTo` = FUN_004F4F80 pure snap
  (pos int+frac, refresh pair, atan2 facing write, turnDelta copy; flags/target untouched — post-warp
  heading now matches the exe). The gait/speed-class producer is replicated with its **canonical
  `{id, rate}` pair-grid model**: the loco block is a flat u16 view (header trio +0x0, central rows
  stride 0x2C @+0xC, early-return grids @+0xE8/+0x156), not `[row][col][pair]` triple-indexed —
  the declared array shape double-counted the stride and read the wrong cells. Gates: D-59/D-60
  probes in `test_sim_players`; scenario gait lanes assert per-band canonical cells, the selector
  ladder and the rate formula `(clamp(speed − 0x20·pair[1], ±0x13330) >> 2) + 0x10000` against the
  byte-exact `.data` dump (keep-current 0x141 fans to the controller's current record id). Speed caps
  + turn thresholds moved into shared `motion_tables.inc`, pinned by `test_motion_tables`. Registry
  D-59/D-60 → Fixed, D-63 → Closed (structural).
- **Canonical gait-selector consumer (pass-15 S4 cutover)**: `Anim_selectLocomotion` is now the
  FUN_004FC470 grammar over a single flat u16 view `AnimData.locoBlock[264]` of DAT_00591698..DAT_005918A8
  (trio pairs + central `{id,rate}` rows at +0xC stride 0x2C idx base **6** + early-return grids @+0xE8/+0x156);
  the FIFA-99-shaped `[row][col][pair]` arrays and its shifted req ladder {&1→4,&2→5,0x1000 arm} are removed.
  WC98 special codes wired: req bit 0x200 + 0x1ca → anim 7, keep-current 0x141 (req bit 0x20 → 0x135, else the
  controller's current sequence record id). `locoPass` resolves ids/rebuilds rates over trio/central/grid in
  canonical order. Scenario gait gates assert per-band canonical cells + live ladder; full suite green with this
  selector (self-test §14 windows recalibrated to the new runtime clip ids). Registry row 1 cites FUN_004FC470.
- **Producer+consumer input/motion grammar**: the WC98 cap chain (cascade bits 0x40→cap 0x18000,
  0x80→0x40000, 0x100→0x68000 applied **before** the arrival band; sprint ceiling select on bit
  0x100000; penalty mask 0x6000000 subtracting motion B0; class-eval gate byte `&2`; EBX-collapse
  gated by bit 4) and the pad-decode constants (stick gate `< 0x2001`, scale `mag*0x50>>0x10` through
  WC98 sinCos quadrant + bit-15-carry rounding, axis deadbands → bits 1/2/8/4, angle bands
  0xaa/0x155 with windows [−0x55,0x56]/[0x1aa,0x1ab], digital remap base `DAT_00580d9e`) are now fed
  by the exe's own producer sites (keeper 0x80/0x100, penalty mask sites, run-state `reqBits(0x11)`) —
  FIFA-99-shaped `moveReq` flags removed. Gesture held-transitions fire at frames > 7 for states
  1/4/6/8; the state-7 port-only timer reset is gone (WC98 has none); the action-queue gate re-dumped
  byte-exact as a single 96-entry array @VA 0x58c750 with predicate `(table[action]==1 &&
  (player+0x83)&4)`; the queue **FIFO is removed** — WC98 drops gated actions during animation, and
  the FIFA-99 tail fields (queue[4]@+0x160, count@0x15C) do not exist in its 348-byte controller
  record; pause-skip request sub-gates + replay gate per `paddecode` l.105-117 (observable
  `match.flags278` bit 0x2000 matrix). Gates: per-bit cap equality, pad-decode fixture pins at angles
  {0,0xaa,0xab,0x156,−0x1ab,−0x156,−0x96}, gesture boundary probes (7 no-fire / 8 fires), queue-drop
  + no-deferred-refire, pause-skip phase matrix — all `test_sim_players`. Self-test locomotion windows
  re-measured on WC98-grammar carriers (movement 4321 units, sprint segment 4193, stop-distance ladder
  green); suite **117/117**. Registry rows D-56/D-57/D-58/D-64…D-68 → Fixed.
- Ball/trig table provenance gate pair (pass-15): `tests/unit/test_trig_tables.cpp` asserts the port's
  sin-quarter and atan-octant rows are **byte-exact** against FIFAWC.EXE at measured VAs
  `DAT_00585898`/`DAT_00594CAC` (+ quadrant-boundary exactness), and `tests/unit/test_ball_tables.cpp`
  asserts the nine runtime ball tables byte-exact at their measured VAs (`0x581bd8`, `0x581c00`,
  `0x581c28`, `0x58ad8c`, `0x59490c`, `0x591490`, `0x591514`, `0x58ec6c`, `0x593d88`), flight-set row
  presence, and the **absence** of the 13 FIFA-99-derived closed-form `kFlightConst` rows (solver-constant
  gap, register D-49). All stale FIFA-99 cite addresses in `trig_tables.inc`/`ball_tables.inc`/`fixed.hpp`
  corrected to measured WC98 VAs; parity registry rows 21/22 -> PV.

- Motion table provenance gate + shared rows (pass-15 S2, register D-55): `kSpeedCap`/`kTurnClass` are now
  the byte-exact WC98 rows at **`DAT_0058aee4`** / **`DAT_0058aef0`** in a public header
  (`simulation/include/wc98/sim/motion_tables.inc`) shared by `motion.cpp`, `test_motion_tables` and the
  self-test step-13 replica — one source of truth. The gate pins caps + thresholds + block adjacency against
  FIFAWC.EXE and asserts the FIFA-99 cites `0x58AD60`/`0x58AD6C` are zeros; parity row 1 records the
  runtime-built u16 companions (D-61) as non-static.
- Self-test animation-timing step (§14 subsystem coverage): a strike's ball velocity must appear strictly
  **after** the shooter's action clip is selected (kick-clip → contact-frame seam), measured on ticks inside
  the shot window (`app/selftest.cpp`, step `animation`).
- Stop-distance ladder re-tuned for the byte-exact rows (pass-15 S2): the old detector leaned on the
  FIFA-99 threshold rows' cap-clamp pin; under WC98's cls-0 row {228,469} straight-line sprint runs on
  clip-stride rates that gait transitions re-rate. Top-band evidence is now an 8-sample window inside
  [runMax*3/4, ·] with spread <= 1 fx unit, and the release fires AT the sustained pin (pad released
  mid-sprint). Measured: release = runMax band, 15-tick ladder to v=0, displacement integral within tol
  vs replica. Assertions unchanged — per-tick exact-match against the shared rows stays the parity core.
- Pass-15 S3 input-layer dossier registered as rows **D-64…D-72**: gesture held-thresholds are `frames > 7`
  in the exe vs ≥12 in states 1/4/6/8 (D-64 High), state 7 has no timer reset (D-65), the queue-gate table is
  a single 96-byte array at `0x58c750` — the port's copy is shifted +2 and sized 98 (D-66), WC98 **drops**
  queueable actions during `anim&4` with no FIFO (the 348-byte controller record has none of the FIFA-99 tail
  fields; D-67 High), pause/skip-request sub-gates inverted + missing replay gate (D-68 High), one device read
  per frame vs the port's per-tick deque (D-69/D-70), dev5 ctx row value (D-71), FE menu pad layer shape (D-72).
- Register D-56 stays **Open** with a measured regression: consumer-only re-gating of the cap chain zeroed the
  self-test movement/turning/stop-distance windows, so the coordinated producer+consumer bit remap is queued
  behind the input dossier (D-57) instead of landing half-done.

- Camera trig lane made exact (pass-15, register D-54): orbit/lead polar offsets (`sin16x`/`cos16x`) now
  compute `fx(dist, sine16x16/cosine16x16)` — the byte-exact WC98 quarter-wave table sine (FUN_00497740 over
  `DAT_00585898`) with the engine's `(prod >> 16) + bit-15 carry` fixed-multiply rounding — replacing the libm
  path that drifted by one unit of the 16-bit angle (measured: dist 1120 @ yaw `0x1234` = 484 table vs 483 libm).
  Pinned by a replay-camera eye replica assert (quadrant I + III) in `test_presentation`.
- Gap-B stop-distance gate: `self_test_gameplay` step 13 (`app/selftest.cpp`) - sprint-chase
  release on a controlled carrier, then per-tick v(t) exact-match against an independent replica of
  `Motion_rootMotionPre` + root-ladder cap chain (speedCap -> sprintMax/runMax-mB0 -> turn-class
  kSpeedCap, arrival-band cap, overshoot collapse, standing-snap gate), brake to v==0 and integrated
  stop distance vs replica step-sum; parity registry row 3 momentum -> PV, PU=0.
- Gap-M team-shape scenario gate `scenarioTeamShape` (in `test_sim_scenarios`, pass-14): fixed-seed
  AI-only replay determinism; per-slot targets vs an independent fx replica of
  `Formation_slotPosition_4CF678` from loaded G98PL bytes (measured devX=devZ=0 over ~13.7k samples,
  tolerance = grid-cell max step 1776/2845); defensive-line ball-follow lane; keeper band vs
  `Keeper_position_539FCC` rows (worst 204/291 in-band). Coverage-gap list fully closed.
- Referee positioning RE lane (pass-14ref): `cmn\ref.c` string disambiguated as eacsnd module;
  `RefCus`/`REFKIT` = renderer sprite atlas (`FUN_0040e748` `%s_T_d_P_%d_%d`, pool DAT_0059e040,
  single-tile handle via getter FUN_0040f1d4 EAX==3); gameplay referee tick statically unreachable -
  documented open (analysis/pass14ref/REF_MAP.md), no approximation installed.
- `.RES` team-history pair-matrix parser `formats/res.cpp` + gate `test_res`: ZWCUP/ZINTER
  `[u16 n][ids][C(n,2) triples]` layout with the FUN_0048cc58 pair-index formula and directional
  head-to-head lookup; Brazil–Scotland (2,1,0) / Italy–Chile (1,0,1) dossier triples asserted
  (TOURNAMENT_MODULE_WC98 §2.5 marked [P via gate]).
- Gap-I assert gate `scenarioSpinNetSurface` (in `test_sim_scenarios`): spin sign-convention row,
  RNG magnitude ladder + 25*extent clamp, B+0x00/T+0xB7C/BCC triple write and 0xf9db/0x2710 decay
  rows, plus the net-surface case table kinds 5..10 -> netCase/contactMode/flag/clamp rows with the
  release/timer countdown (dossier constants cited; `Test_ballResolveCollision`/`Test_netContact`
  test-only wrappers export the file-local helpers). Gap A/D doc lines closed in GAMEPLAY_PARITY.
- Camera dolly model + byte-proven preset tables in `presentation::CameraTask` /
  `MatchCamera::glideTo`: timer B/C snapshot-swap (`FUN_00517b50/e30/f20/cc0`, bit-15 carry
  rounding), commit trio, preset rows `DAT_00585FEA/586046/5862D2`, height table `DAT_0057FB82`
  (sentinel lanes) and sine `DAT_00585898` installed; kickoff/goal camera moves no longer snap
  (TASK_HANDLERS_WC98 §7 delta-1/2, gate in `test_presentation`; GAMEPLAY_PARITY §5.1 rank-1).
- `.SCI` payload parser `formats/sci.cpp` + gate `test_sci`: header magic survey {0x201..0x204},
  stride-16 record streams, stride-12 tail tables and trailing stride-8 relocation-pair lists —
  all 87 `COMMON/*.SCI` files parse (TASK_HANDLERS_WC98 §6 grammar refined; delta-4).
- Kickoff placement task `simulation::KickoffPlace_init/step` from h6 `FUN_00516f40`: side-aware
  dolly to the centre spot (rec+0x1cc direction rule, 0x3c0/frame polar step), spin ±DAT_00656e08 at
  the placement-phase tick with per-side sign; replaces the direct-set kick placement and livematch
  pose follows `kickoffPlace.bearing` (gate `scenarioKickoffPlacement`; §7 delta-3, GAMEPLAY_PARITY §5.6).
- Quality-tier scene camera `presentation::tierCamera` from h23 `FUN_004cafe0`: tier deltas
  {0x71,0x3d}/{0x65,0x31}/{0x59,0x25} ×0x10 by quality word `DAT_00644778`, pitch-edge x/z clamps,
  flags bit4/bit5 z-source lanes (ball/block/player), dz<<4 polar + 0x640 constants; live option
  `wc98-native --quality N` (`MatchCamera::setQualityTier`) — gate in `test_presentation` (§7 delta-5).
- h21 ball-follow TV camera `presentation::followCamera`: task-flag source-triple select
 (carrier byte[1] bit2 / player-pair byte0 bit4 / ball default), inset-edge bisection clamps
±(halfW−0x1e0)/(halfL−0x140) and the rec+0x104 bytes {0x58,0xeb} = `0xeb58` angle/speed constant [P] - gate in
 `test_presentation`; §7 camera-follow row closed (MODEL-PORTED).
- h18 pitch-edge side camera `presentation::sideCamera` + h20 overhead camera
  `presentation::overheadCamera`: ball-x lane thresholds 0x1e0/-0x1df, far/near lanes
  ±(zEdge+0x140) with rec+0x110 mid-band constant and the forced-near bisection guard [P],
  DAT_00644794-0x10 polar-height lane; overhead = -xEdge polar distance + 0x90 angle/speed.
  Gates in `test_presentation`; §7 edge/tier row closed (all three MODEL-PORTED).
- Task-handler family dossier `docs/reverse-engineering/TASK_HANDLERS_WC98.md`: semantic map of the
  30 tabB task handlers (scene bindings via the `.SCI` descriptor table, camera/pitch-mesh/timer
  primitives, kickoff placement task), byte-proven `.SCI` payload grammar from `COMMON/*.SCI`, and a
  ranked parity-delta table vs the port simulation (GAMEPLAY_PARITY §5.1 rank-1 unblocked).
- Kick-plan power model closed to WC98 parity (`simulation/src/kick.cpp`): the `power < dist`
  displacement clamp (WC98 `FUN_00502fc0` lines 62-64) rescales the kick vector to length=power
  along the heading via the 16.16 fixed-point sin/cos builders (`FUN_004f3590`/`FUN_004f34b0`, table
  `DAT_00585898[i] = round(sin(2*pi*i/1024)*65536)`); the slot-0 half-length power gate now models
  the decoded `FUN_00500660` predicate (player+8==0, `FUN_0049eb60` pitch test: |dx|<=halfWidth,
  |dz|<=halfLength, attacking-direction line) instead of an approximation. New ctest gate
  `scenarioKickPowerClamp`; SIM_PORT_NOTES.md "Kick family WC98 deltas" open items closed
  (kickAssist writers located at VA 0x450ff5 FE handler + raw 0x6a9e2 block).
- Commentary adapter aligned to the pass-12 per-event speech architecture (`app/commentary.hpp`
  `SpeechPreload` / `SpeechEventDispatch` / `SpeechScheduleRecord` + `SpeechScheduler`, wired into
  `Commentary`): preload enable/disable gate semantics (scene-enabled precondition, once-only gate
  transition, `SNDbankcreate('BNkl')` bank handle as the phrase-id source), event-byte switch
  dispatch (`[obj+0x20]&0x3f`, cases 0..7) with per-record `SNDphraseadd(id, overlap=0)` + queue-slot
  write-back into record+6 (overflow -13 stored verbatim) and exit-label `SNDsentencestart`, and the
  RNG-gated match-clock scheduler (`t*100/60` conversions, `{sceneId, delay 0x7f/0x4a, t0, priority
  sceneBase+3, start/end}` fields, `%5==0` echo variant with `%200 + t0 + 400` jitter, per-frame sweep
  rewrite `(paceA*0x20 - paceB*7)*4/60`) — deterministic under a fixed seed. New ctest gates in
  `test_commentary`: `preloadGate`, `eventDispatchGates`, `schedulerGates`, `schedulerWiring`;
  COMMENTARY_WC98.md gains the port-side VA->symbol mapping table.
- Commentary per-event speech preload table decoded (pass 12 ghidra dossier
  `analysis/pass12speech/SPEECH_PRELOAD_TABLE.md`): event-byte switch dispatcher at
  `0x48f45c/0x48f4b0`, preload enable/disable family (`FUN_0048d378/0x48d3f4`) creating a
  `SNDbankcreate('BNkl')` bank handle as the phrase id, runtime RNG-gated scheduler
  `0x48d4f7..0x48e1xx`; SPEECH_PHRASES.md §5.1 open item closed.
- Automated gameplay self-test (goal §14): `wc98-native --self-test FILE.json` launches a real
  exhibition match from the FCDB tree, drives player 0 with scripted pad words (fixed tick windows),
  and measures every step on internal simulation state — movement/sprint/turning path & heading
  deltas, dribble possession retention, pass kick + M7 landing placement, shot arc apex/minGround,
  ball flight physics, opponent AI reaction to possession, goalkeeper displacement off the line,
  match-state progression. Structured JSON report written (version/steps/passed/detail), exit code
  verdicts; ctest gate `self_test_gameplay` (skip return code 2 when no tree/FCDB env).
- Broadcast camera grid box ported: `FUN_00501910` 12x22 box (scale DAT_0058bcc4/bcc8, index
  x+y*13, complement 298-index) drives live-match framing in presentation/src/camera.cpp with
  camera-param re-verification against FIFAWC.EXE (gate `test_presentation`). GAMEPLAY_PARITY §5.3 closed.
- Difficulty control-level A/B gate (`scenarioDifficulty` in `test_sim_scenarios`): asserts the exact
  ctrl1f4/ctrl1f8/ctrl1fc triples of `Team_setControlLevels_45159C` for aiAssist 0 vs 2 (human vs AI
  sides) and that consumers follow — claim range kRange[ctrl1f8] {0x55,0x42,0x2f}, fatigue-tick
  predicate, keeper ctrl1fc — closing gap O.
- WC98 simulation-table verification pass (pass10): the shipped FIFA-99 dumps of the locomotion and
  input action families were re-dumped from FIFAWC.EXE by byte-signature scan + consumer xrefs
  (`docs/reverse-engineering/SIM_TABLES_WC98.md`, `analysis/pass10/simtables/`): kButtonMask/kCtxTable/
  kCtxKeep proven byte-identical, kGaitType byte-identical; the five u16 loco/turn/skill tables and the
  action/context tables (162/2048 entries differ) installed in `simulation/src/anim_tables.inc` +
  `input_tables.inc`. `locoPass` normalizes the WC98 seq-table filler id (534, the TBI record count) to
  the 0x1c4 sentinel and bounds every resolved id inside the loaded seq table (fixed five live-match
  gate segfaults). Self-test sprint window re-anchors on the controlled carrier via M7 warp; dribble
  lag assertion recalibrated to the WC98-measured ~95-unit sprint ramp (was ~43 FIFA-99).
- `docs/reverse-engineering/GAMEPLAY_PARITY.md`: complete gameplay systems inventory (goal §1),
  constants provenance (§3), test coverage matrix (§4) and ranked open-differences list (§5);
  gaps B/C/D-placement closed by the self-test, remaining gaps E/F/G/H/K/M/N/P ranked for a
  one-batch scenario extension.
- Scenario-gates batch (GAMEPLAY_PARITY §4 gaps E/F/G/H/K/L/N/P closed): eight fixed-seed scenarios
  in `test_sim_scenarios` - ShotPower (kickPower scales solver D = fx(0x16, kickPower), speed-class
  ladder), BounceRoll (chip lob apex + bounce/roll decay on the live ball), NetRest (ball rests in
  netting past goal line), TackleWin (slide-tackle win timing via pickMove row gate, possession flip),
  HeaderDuel (Header event on air-ball contact + header lift), KeeperDistribution (keeper throw-back
  re-enters play), PenaltyKick (in-box foul -> booking + PK lineup through the State185 approach
  machine -> strike -> Goal with score move; driven-roll completion documents the port's ground-roll
  power gap vs exe PK power), AddedTime (stoppageFrames -> injurySec clamp at duration>>2, status&4
  added-time board bit). Self-test §14 evolves: steps 10/11 foul-card + penalty measured on live
  match state; State185 approach warps mirror the scenario lane.
- Pass-11 handler-diff audit integrated (`docs/reverse-engineering/HANDLER_DIFF_AUDIT.md`: 60-handler
  WC98-vs-FIFA-99 verdict table, ranked P1/P2/P3 deltas; evidence `analysis/pass11/handlers/`).
  WC98 Kick_plan (FUN_00502fc0) constants re-extracted into `simulation/src/kick.cpp`: param clamp
  kParamClampMax=1243 -> 0x534, power bases kPowerPhase2 0x960 / kPowerBase 0xb40 with +50% assist
  bump gated by the new `Options::kickAssist` (DAT_00641011 model), param_6==0x10 flag semantics;
  open items recorded in SIM_PORT_NOTES.md "Kick family WC98 deltas" (power-consumption clamp,
  FUN_00500660 predicate). `World::kickCode` array keeps the plan's kick code for event mapping.

- Pass-11 P1 lanes closed (TeamAI decision family): WC98 `FUN_0050cf50`/`FUN_0050e740` deltas installed in
  `team_ai_ball.cpp` — raw mentality-indexed pass thresholds, budget block rewrite (discarded first draw,
  `>>=2` team gate), difficulty-0 reach clamp removal, chaser gates 0xc0/0x180 + wing-clear 0xf0→0xc0, dif==0
  dribble `(rand&3)!=0`, fused facing/frames gate, `(reach+attr)>>1` close check, shot-lane probe ±0x40; pair 16
  dodge anim ids {0x6C,0x71,0x7C,0x7D}/{0x71,0x72,0x73} in `player_helpers.cpp`. OPEN items recorded (SIM_PORT_NOTES).

- Keeper capsule spin fix (`keeper_save.cpp` `Keeper_hitBone`): the exe-faithful `Math_normalize3` loops forever on a
  zero bone axis (documented exe behavior); headless lanes run clip frames whose skeleton bones coincide, spinning
  `Keeper_skeletonBlockNormal` inside every tick. Degenerate axes now return no-capsule (port guard, cited).
- TeamAI WC98 divergences pairs 46-48 (pass-12ai, dossier `analysis/pass12ai/`): unconditional shot
  eval + threshold lanes (0x50cfab/0x50d137), rand mask &1 (0x50d6dd), FIFA-99 extra draw removed in
  LAB_0050d9c8, ball-y constant 0x72, budget high-half scratch lane + dual shift, two flag-gated
  counter tests vs f(0x34)/f(0x24) (0x50ded2/def5), high-half draw threshold replacing the global
  lane (0x50dee6..def0), give-and-go arm set f014 {5,6}, header-pass skip-lane restructure (shot-score
  gate, local_50 forced cross-lift, deep test iabs(z), return test k26>0x3c0). ctest 114/114.
- RE closure (sci9/sci12): scene-install producer functions decompiled (`FUN_004aa148/1ba/267/2fc/485`
  mode-selector writes, kit/side-gated camera zeroing, unfreeze sweep `FUN_004aa432`) and the loader
  field-split layout proven at code level (`FUN_004aaf00`: count nibbles +0x232, stride-8 triple arrays
  +0x170/+0x1c8 >>10, u16 pairs +0x220..+0x22e); TASK_HANDLERS_WC98 §8 closed.
- WC98 pair 43/44 air-kick bodies (pass-12air): `TeamAI_onBallWon` = `FUN_0050c820`,
  `TeamAI_planAirKick` = `FUN_0050cc70` - located via xrefs into the mentality-threshold byte tables
  (`analysis/pass12air/ai2_xref_tables.txt`). Installed divergences: thresholds RAW (no formation/difficulty
  base add); onBallWon gate id 0x281, clearance lane = rand + offsetXZ point only (nearest-player target,
  dz+(dist>>4) nudge, >>3+>>4 lift scale and the 0x72 clamp GONE), tail code bands >=0x3c1 -> 8 / >=0x1e1
  -> 9 with the 0x72/0x8e lift clamps removed; planAirKick clearance = atan2+offsetXZ rel only, receiver
  stays -1, kick power from the rel vector. Scenario gates close §4 gaps A (`scenarioGaitTransitions` live
  pad-driven gait ladder) and D (`scenarioReceiverContest`: uncontested completion transfer + contested
  aerial resolves as Header deflection per pair-43/44, both deterministic); suite ctest 114/114, self-test 12/12.

## [0.4.0] - 2026-11-08 — Windows menu parity complete: popup/IG board families, highlights browser, management grids, LOAD/SAVE scenes, camera/practise/control pages
### Added
- IGCAMERA/IGCAMSEL camera popups + practise page cycle + CONTROL1 controller page (`IgBoards` kinds 16-20,
  `--ig-camera-smoke N`, `--ig-practise-smoke N`, `--fe-control N`, gates `fe_camera_pages`, `fe_practise_pages`,
  `fe_control_page`; MENU_PARITY §2b rows): live pause 'C' opens IGCAMERA (FC#353 CAMERA title, 5 key-assignment
  rows FC 1468/1011/1545/1546/1104 + binding strings, flg/nmb cursor, forw/menu nav), forw opens the IGCAMSEL grid
  (FC#379 'Camera Select', 7 camera names; Enter applies a MatchCamera mode, evidence "igcam: camera sideline").
  Pause 'P' opens PRACT1 over the live frame (FC#1249 'Number of Players' stepper via INGAME.FSH bt4n/bt5n art,
  User/CPU Keeper rows FC 1920/374 + On/Off 1267/1263): btn-nav RIGHT tabs IGPRACT1->IGPRACT2 (page-2 option grid),
  LEFT wraps, DOWN+Enter flips a keeper toggle, Esc opens IGPRAC1 (on/off rows). CONTROL1 rides the popup lane from
  the options screen: composite grv*/ls*/*rs* bar art + npll/nplr nav pair, FC#258 'Attacking' ctx caption, four
  Gravis percent bars from the exe scene-default table DAT_00632778 {80,80,80,80,65,80,1} with gold fill quads and a
  live gamepad axis readout when a pad is open (keyboard fallback 'kbd'), tmtl/tmtr FC#4594 captions. INGAME.LAY
  documented as a boundary: its frame is scene-handler code-drawn (raw shapes=0 legit, null.fsh placeholders).
  Captures /tmp/wc98port-out/{cam_igcamera,cam_igcamsel,prac_igpract1,prac_igpract2,control1_fe00}.png.


- Management grid + LOAD/SAVE list scenes (`FeFlow` FEFLOW43/41/42, `--fe-mgmt-grid N`, `--fe-load-game N`,
  `--fe-save-manager N`, gates `fe_mgmt_grid43`, `fe_loadsave41`, `fe_savemgr42`; MENU_PARITY rows 29-30):
  FEFLOW43 opens as a standalone board (not in the scene registry [H]) — composite paints the iNNn button chain +
  back collage via FE/PACK/FEFLOW43, overlays paint FCDB squad rows ("#shirt name" on vNNN + kit swatch), FC-bank
  position labels under each column (GK=810 SW=1801 RB=1531 LB=979 RM=1534 LM=982 RF=1533 LF=981 MF=1070 FW=751,
  header captions 1379-1381/1382), and the aNNN card-icon columns (redc/yell/but1..3) resolved from the
  FE/PACK/FEFLOW73 PAK like the feflow72 tile lane; UP/DOWN pans the four-row window, sbar thumb follows, Enter =
  Save via saveTournament. feflow41 LOAD lists real SLOT%.2d.SAV files scanned from defaultSlotDir (rows through
  paintListRows + sbbg/sbar page slider), feflow42 SAVE adds the tick/cross confirm: Enter writes the focused slot
  and flips cros(idle) -> nvicons tic1, evidence "fe: saved slot NN". Gate fixtures seed deterministic slots under a
  scratch XDG_DATA_HOME. Captures /tmp/wc98port-out/{mgmt43,load41,save42}.png.
- MANMARKS ratings board (`IgBoards` kind 15, `--ig-manmarks-smoke`, gate `fe_manmarks_grid`): igteam72 'M' opens the
  player-marker management screen over the frozen fixture match — pNNN squad grid rows painted from FCDB lineup/subs
  ("#shirt name" + tricolour kit swatch, hilite pane = row cursor; UP/DOWN pans a five-row window with the sbar thumb),
  mm00..mm02 pair captions pairing home defenders (lineup tail) with away attackers, FC-bank subtitle 1025 'Man
  Marking'; Esc -> IGMENU. Capture /tmp/wc98port-out/manmarks_manmarks.png.
- Single-player setup + stadium select (`FeFlow` FEFLOW07/08, `--fe-setup07 N`, `--fe-stadium08 N`, gates
  `fe_setup07`, `fe_stadium08`): the hub 'fe07' wedge opens the FEFLOW07 matchup screen — ltnm/rtnm team names
  and shdl/shdr FNNN shields over the lbNN/rbNN skill-bar pairs (bar art via HILITEDSHAPEIDON, FCENG captions
  Shooting/Passing/Speed/Defence/Overall @1691/1330/1741/573/1278); two-axis grammar = UP/DOWN moves the side
  cursor (yellow HILITEDSHAPEIDON box at ltrb/rtrb), LEFT/RIGHT cycles that side's team through the FCDB pool,
  Enter follows the 'fe09' forward edge into friendly config. FEFLOW08 stadium select opens from the friendly
  confirm ('fe08' tag @0x44C3D3): France-map mpNN ball-marker tiles (feflow08.fsh via the stem-index lane,
  `hbal` hilite), nslt/nsrt arrows walk the tile ring, stme runtime stadium name + hint pane strip show the
  FCENG stadium captions ("Parc des Princes"…), sint/wint/hint decoration row; Esc = back per the 'fe06' exit
  wedge. Scene-ownership whitelists exclude the chat/cha2/cha3 netplay surfaces and soup textids (429-433,
  1185-1207) per the f0f8109 rule; key slot = default thunk FUN_0044a210. Evidence analysis/pass13
  IG_FAMILIES_SPEC.md §B, MENU_NAV_WC98.md feflow07/08 rows; captures /tmp/wc98port-out/setup07_fe00.png,
  stad08_fe00.png.
- IGTEAM management mirrors (`IgBoards` kinds 8-14, `--ig-mgmt-smoke`, gate `fe_mgmt_pages`): the IGMENU
  'Management' row (five-row domain {Resume, Stats, Highlights, Management, Quit}, bt0-bt4 INGAME.FSH chrome)
  opens the §E page cycle over a deterministic seed-0x2E7A fixture match — IGTEAM72 pNNN pitch grid + tNNN
  formation tiles from FCDB lineup shirt numbers/positions; IGTEAM73 kt00..08 kit row + lin squad rows with
  the ab00/ab01 sub pair (Enter swaps starter<->bench, `igmgmt: swap` evidence line on real players);
  IGTEAM74/80 card rows (pr/red/yel) + vit stamina from sim booking state; IGTEAM75/79 s001 attack-bias
  slider with the UP/DOWN ±10 keyloop grammar painted live into the header pair. gobk/gofd (L/R) cycle
  {72,73,74,75,79,80}; 'B' (tmf3 icon) opens the IGTEAM89 hub whose b000..b005 rows are the FC-bank captions
  Formation/Substitutions/Positioning/Attack Bias/Man Marking/Kick Takers — Enter enters the mapped page,
  Esc returns to IGMENU (§E grammar). Scene-ownership whitelisted composite (soup slots excluded per the
  f0f8109 rule), FC captions via fcBankText; evidence analysis/pass13/IG_FAMILIES_SPEC.md §E, MENU_PARITY rows.
- Highlights browser + save list (`IgBoards` kinds 5-7, `--ig-hlite-smoke/--ig-hlsav-smoke`): the IGMENU
  'Highlights' row opens the IGHILITE browser over the shipped `user/rplyindx.dat` 100x25B index — FC#899
  "INSTANT REPLAY" title on the box3 video pane, selected slot filename + `SLOT NNNN` readouts in the white
  entry strips, btlt/btrt page arrows (10 rows/page), slol/slor slot cycling; Enter runs the app/replay.cpp
  re-drive of the selected `rplyNNNN.dat` (fixture slots seeded from a fixed seed + scripted pad stream:
  state-hash parity `match=1`). The browser 'save' button opens IGHILSAV: lst1 index rows, Norway/Denmark
  header pair (typ names + FNNN shields + nmb score digits painted after the flag quads), up/down arrows via
  the btnup->ingame alias; Enter writes a new slot through `recordToUserDir`. IGHILIT1 = shipped dummy
  hotkey-transition page, composite-only. Gates `fe_highlite_browser`, `fe_hilsav_list`; evidence
  analysis/pass13/IG_FAMILIES_SPEC.md §D, MENU_PARITY.md rows.
- IG presentation boards (`IgBoards` in livematch, `--ig-stats-smoke/--ig-half-smoke/--ig-full-smoke`): IGSTATS opens from the IGMENU 'Stats' row over the live frame — two-page L/R stat grid (Possession % / Corners Won / Caught Offside / Fouls Committed / CARDS + GOALS / Shots off Target) painted from `World.match.stats[2]`, goalLog and `time.possessionFrames`, with FC-bank captions (1382/516/392/758/354/781/1708, page buttons 1771 'Statistics' / 329 'Booking Summary'), team names from FCDB fullName + FNNN shields + score. IGHALF opens at the half whistle (period-end arm: `status |= 8` + `Match_requestPeriodEnd(whistle=1,next=0xe)` + event 9 — the exe's own MatchClock grammar [C]); full time chains IGSCSUM -> Enter -> IGFLSUM with Time/Player/Score rows from `goalLog`. Esc dismisses to caller (ESC=0x65 resume contract); scenes load via LAYOUTS.BIG/IGLAY.BIG/loose FE/INGAME search like the popup lane. Gates `fe_igstats`, `fe_ighalf`, `fe_igscsum`, `fe_igflsum` (captures `/tmp/wc98port-out/ig*_*.png`). Evidence: analysis/pass13/IG_FAMILIES_SPEC.md §C, MENU_PARITY.md §2b.
- Shared popup OK/CANCEL + YES/NO lane (`FeFlow::openPopup/movePopup/drawPopup`, `--fe-popup NAME`): FEFLOW10 (ok_1/canc) and FEFLOW37 (no/yes) modal confirms composite the blur backdrop (`FE/GLOBAL/BLUR.FSH`) + shared popp panel (`Feflow86.lay#back`, 368x230 @136,140) + the two-slot nvicons button pair with hover shape-chain; runtime captions resolve per focused widget `textID` via the FC bank ("Are you sure?", "Are you sure you want to Quit?" = index 220) and the help pane shows each button's `help=NNN` string. Two-axis cursor cycles the pair; Enter confirms / Esc cancels — both dismiss back to the caller (original: response callback + 'okcanc1'/'yesno' payload, FUN_00459220/FUN_004592f0). Hub 'quit' wedge now opens the FEFLOW37 confirm before exiting. IG boxes: `--fe-popup igconsel` paints the INGAME.FSH btnn/btnd "OK" chrome over the legit null.fsh region placeholders (§A rule); MESG/IGQUESHN share FUN_00440830/FUN_004425e0 grammar (livematch pause already wires IGQUESHN). Gates `fe_popup_ok`, `fe_popup_yesno`, `fe_popup_dismiss_enter`, `fe_popup_dismiss_esc`, `fe_popup_feflow86`, `fe_igconsel`, `fe_mesg`; evidence analysis/pass13/POPUP_LANE_SPEC.md.
- `ScreenArt` stem alias `button -> ingame`: IG popup layouts reference `..\global\button.fsh`, which exists nowhere on
  disk; the referenced tags (`btnn/btnd/btn2`) live in `FE/INGAME/INGAME.FSH`. IGSTATS-family buttons now render
  (raw lane shapes=0 -> 4). Evidence: analysis/pass13/IG_FAMILIES_SPEC.md.

### Changed

- `--fe-league` gate: hub `selectNav(LeaguePlay)` opens FEFLOW73 directly; the follow-up `enter()` call leaked
  into the default wedge (friendly setup) and preempted the league composite in `drawCurrent`. `enter()` now runs
  only on the FEFLOW05 fallback menu. `cfgTeam()` falls back to the FCDB head team when `poolIds_` is empty, so
  FEFLOW73 pn rows paint roster captions + kit swatches (capture: texts=85, "#8 Simeone" .. "#15 Zanetti").

### Added

- Objecttype-18 checkbox painter + feflow95 Cheats-screen parity: `presentation::paintCheckbox` renders the toggle glyph from the widget's
  HILITEDSHAPEIDON (`Xblak.fsh#blkx`) with OFF = `hilitedshapeidoff=null` (no glyph), over the objecttype-2 cheatbox `1box` frame painted by
  composite. Scene lane `FeFlow::drawCheats` (+ F9 dev key, Esc closes; arrows/Enter/click per row) paints the blur backdrop, cheatart pane,
  the eight rows and their captions installed by scene code from exe data ("Cheats" VA 0x56400c; "Take a Dive" .. "Alien Model"
  VA 0x564014..0x564074 — byte-verified in FIFAWC.EXE, NOT FC-bank indices); the help pane shows the focused row. Gates `fe_cheats` /
  `fe_cheats_off` (`--fe-cheats N` = row bitmask ON at open); label table pinned in `tests/unit/test_fescenes.cpp`; evidence MENU_PARITY.md §8.
- World-cup family interactive screens (FEFLOW18 finals-draw board, FEFLOW23/24 R16 bracket boards, FEFLOW35
  standings board): `--fe-wc-cfg N` gates (`fe_wc_cfg{18,23,24,35}`) open the screens with scene-ownership
  whitelists (dynamic tm/gm/qtm textID soup excluded), app overlays painting ZWCUP pool names + group letters
  on FEFLOW18 (deterministic re-shuffled zones per `rand` press, engine cap 99), R16 bracket pairs with kit-shield
  cards under dark-ink team names, and the 8-group standings page (`nglt`/`ngrt` = mod-8 group cursor). Decoded
  thunk semantics per tag: analysis/pass12/wcfamily_notes.txt (GhidraWc98WcFam1/2 lane).
- League configuration family (FEFLOW72/74/75/76/78/79/80): list-box (type=9) + slider (type=11) painters render
  `s001` rows with FC-bank captions, kit swatches from the fifa98edit hslCols palette, row cursor navigation and
  gofd/gobk screen-chain keys per decoded key loops. Gates `fe_league_cfg{72,74,75,76,78,79,80}` (--fe-league-cfg N).
  Scene-ownership whitelists keep engine textID soup slots (0000*, tt*/ad*, mt9x) out of the composite — captions
  collide otherwise; kitColor() palette now matches analysis/fifa98edit/__init__.py hslCols verbatim.
- Hub screen (Feflow06.lay) as the interactive main menu when present on the tree: 13 button wedges with hover/push/disabled
  shape states, FRAMES animation clock, help pane FC captions and keyboard + mouse (widgetHit) cursor; wedge edges fe07/fe11/fe32
  reach the existing setup/team flows, `quit` exits the app cleanly, and fe33/fe90/fe04/fe02 render DISABLEDSHAPE* with documented
  no-op Enter (modes without port simulation). FEFLOW05 fallback kept for trees without the layout.
- Scene-graph views from the hub: options (FEFLOW15 composite, tab pages tbb1..4, row cursor; music/menu-sfx volumes wired to
  settings.ini + live mixer gain), save/load entry (Feflow89 slot list -> WCT1 load when a file exists), credits roll (feflow19).
- In-game pause menu: Esc opens the IGMENU.LAY composite over the live match (INGAME.FSH bt*n/bt*d button art), {Resume,Quit}
  rows, Enter on quit opens the IGQUESHN confirm box; simulation frozen while open. `--ig-pause-smoke N` gate (`fe_igmenu`).
- Startup movies on the interactive FE path: `INT_EASP.DCT` (EA Sports logo) then `INT_FIFA.DCT`, played
  through the .DCT player in Windows-original order before the front-end opens; Esc skips one movie.
  `--no-intro` skips both, and headless gates (`--fe-smoke`, `--fe-nav`, `--fe-present`) never open a
  movie window. Verified on the real display: movies present, menu composite reached afterwards (intro
  log lines + desktop captures).
- Launcher Play button now passes explicit startup flags: `--fe --tree <first configured game folder>`.
  Bare `wc98-native` used to render an empty frame loop (the black screen from the desktop bug report);
  the shell now reaches the front-end with music, intro chain and menu.

### Added — World-Cup history boards + igoptns captions lane (pass 13)

- feflow90/feflow91 interactive scene (`FeFlow::openHistory/drawHistory`): hub wedge `fe33` Enter opens the World-Cup history board; bounded year cursor over
  15 finals 1930-1994 (original 'ysla'/'yslc'/'ysrb' tags, list length DAT_0057c970=9); finalist pair + scores painted from an app-side dataset with FCDB fullName
  captions; shields/flags composited from FE/GLOBAL `F0NN.FSH` (record-index rule) over the tmfg emblem. Enter ('fe91' chain, selected-index store FUN_004857a8 ->
  DAT_0062fab8) opens the feflow91 continuation board; Esc returns to the hub [C] analysis/pass13/CLASSICS_FE33.md. Gates `fe_history_select` /
  `fe_history_select_1994` / `fe_nav_history`; captures `/tmp/wc98port-out/hist*_fe00.png`. F040/F041 flag files decode byte-identical = original-data duplicate for
  Uruguay/Argentina 1930 (reproduced faithfully, never invented).
- In-game option popup captions lane `igaoptns/iggoptns/igmoptns` (`FeFlow::openIgOptns/drawIgOptns`, `--fe-igoptns a|g|m`): trio ships inside LAYOUTS.BIG; ROWS tuple
  parser reuses the FEFLOW15 grammar (label,mode,count,v...), list windowed to its box (ROWHEIGHT slots + scroll with the row cursor); tab captions trimmed into the
  button boxes (this media's ids are FC-bank dev strings [C]); ok/canc/defa `igicon.fsh#chck` art is PROVEN ABSENT on disk (analysis/pass13/IGOPTNS_ART.md) -> text-only
  captions, no invented placeholder art. Gates `fe_igoptns_{a,g,m}`; captures `/tmp/wc98port-out/ig{a,g,m}_fe00.png`.

## [0.3.2] - 2026-11-07 — AppImage launcher starts on desktop sessions
### Fixed
- AppImage launcher startup on desktop sessions: Qt resolved `qt.conf` relative to the *invoked*
  binary, and an AppImage starts through the root-level `AppRun` symlink, so `usr/bin/qt.conf` was
  invisible and the platform-plugin search path came out empty -> "Could not find the Qt platform
  plugin "wayland"/"xcb" in """ + SIGABRT. Launcher main() now seeds `QT_PLUGIN_PATH` from the
  symlink-resolved `/proc/self/exe`, and deploy.sh ships a root-level `qt.conf` (Prefix=usr) too.

## [0.3.1] - 2026-11-07 — authentic FE menus 1:1 (main-menu graph, friendly/practise setup screens, league select FEFLOW73)
### Added
- Main-menu composite scene-ownership whitelist (pass-10): `composite()` draws only objects owned by the
  shown scene (`FeFlow::menuVisible_` = back/titl/flag on FEFLOW05, which is MNMSETUP.LAY's netplay-setup
  layout); hidden edit/list widgets no longer paint their FC-bank soup. Gate `fe_flow_render` asserts
  the menu composite stats line (LAY_COMPOSITOR_NOTES.md "Main-menu scene").

- League Play select screen (pass-11): Enter on the "League Play" menu entry opens FEFLOW73.LAY —
  the authentic league list (`FeFlow::leagueView_`, pn00..pn08 row cursor, composite whitelist, Esc to
  menu). `--fe-league` captures it headlessly; gate `fe_league_select` (FE_MENU_SCREENS_WC98.md §8).

- Team-select fidelity (pass-11): pool pages from FCDB `teamdb` records (pool 3 = Europe B, 56 teams),
  names + flag file ids from the record fields; row flags drawn into the LAY icon slots (`stf0..stf7`,
  mixed zero-padding with `tt00..tt07` measured in FEFLOW11.LAY), current-team big shield over `tmfg`.
  Root cause of the black flag quads fixed: textured menu draws passed a degenerate UV rect `{}`
  (0,0,0,0) to `SpriteBatch::draw`, so each quad sampled texel(0,0) and painted a full-size dark-navy
  rectangle; app-side textured draws now use the full-rect UV, row icons scale into their LAY cell.
- FFN font atlas decode FIXED: FNTF glyph atlases are 4-bit INTENSITY maps (index = coverage), not palette
  lookups - they carry the shared FE palette, whose opaque entries made every texel alpha=255, so tinted
  label quads painted solid blocks. `decodeFshBitmap(..., intensityRamp=true)` decodes them as a white
  luminance ramp (alpha = index * 17); all FE/HUD text is legible again (eras10/11i/12/8i/11bi/9bi/14bi/
  20/36/20bi verified: transparent + mid alpha coverage present).

- HUD fidelity (pass-10): scoreboard strip face = `FE/ERAS10.FFN` (font-table slot 0 via
  FUN_00438670@0x4386de -> FUN_0046a2a8 copies `DAT_0060b704[0]`; eras14bi REFUTED for the strip,
  kept as fallback) and clock format `"%02d:%02d"` zero-padded minutes (@0x43888c sprintf,
  value via FUN_00438b48); gate `test_hud` updated. HUD_WC98.md items 2/4/5 RESOLVED.
- MDL geometry gate `test_mdl`: synthetic GEOM parse + FE/*.MDL tree census (12 files, 113 groups,
  4886 faces, 478 texture names; SKEL magic pinned on LPOLYBF.MDL).
- Community patch lineage fully closed: the moddb "FIFAWC.EXE 3rd-Party Loader" = gold master +
  the single no-CD byte at file offset 0x38440 (VA 0x439040 JNE->JMP); Ultimate Patch ships the
  nGlide `voodooa.dll` verbatim (md5 match), `dxa.dll` = EA D3DA renamed, plus Galahs' new
  `voodoo2a.dll` (`docs/research/EXTERNAL.md` §4/§5).
- AISEQS.TBI clip pool refined: stride-24 tagged records `[fourcc pair][seq<<16][TBA keyframe
  offset]`, 417 entries (AI_SEQS_TBI.md).
- FE mouse cursor drawn from MAUS.PAK (`presentation::loadFeCursor`, 28x26 `curs` arrow, engine-hardcoded path at FIFAWC.EXE offset 1451894) over every FE composite in the window loop; gates `test_presentation` tree checks (MAUS entries + REDLEFT shipped-gap assert), `fe_render_tournament` FAIL regex `cursors=0`.
- FE element colour state RESOLVED + implemented: hilite pane (HILITEPANE) draws HILITECOLOUR/HILITEDSHAPEID, plain panes NORMALCOLOUR (exe parse chain 0x46e0d9/0x46e10b fields +0x68/+0x6C; listbox draw FUN_00477f54 @0x477f54); no per-element blend mode exists. Gate `test_presentation` tree checks assert hilite/plain glyph rgba = pal[2]/pal[1].
- Runtime audio wired into the shell (`app/game_audio.{hpp,cpp}`): FE flow starts a looping music
  stream from AUDIO/MUSC `FE_*.ASF` (exe glob `FE*.ASF` @0x566b3e, queue FUN_0048ed9c + pump
  FUN_0048eb98); live match plays CROWD.BNK slot-2 ambience loop + GAME00.BNK whistle (#81) on
  KickOff/PeriodEnd/MatchEnd and ball-strike thud (#23) on Shot off the event seam. RE evidence
  pass 8 (AUDIO_RUNTIME.md §6); Mixer best-effort pattern kept (headless = counters only). Gate
  `test_gameaudio`: OpenAL loopback capture asserts music stream audible over >=1 s, whistle/kick/
  crowd slot decodes non-silent, scripted KickOff/PeriodEnd/Shot fire SFX voices.
- Replay system RE'd + implemented (Ghidra pass 8, `analysis/pass8/rply_*.txt`): `.dat` container =
  u32 record count + 142-dword state table (`FUN_00495d10`/`FUN_00495e60`, loop 0x8e) + BE16
  size-prefixed per-tick input records at file offset 0x23c (walk `FUN_00496110`/`FUN_004963b0`,
  FEREPLAYBUFF chain DAT_0057f83c/0x63699c/0x636994); `rplyindx.dat` = fixed 100 x 25-byte slot
  table (`FUN_0043c4f0`, slot byte entry+4, name at +5). Codec `formats/src/rply.cpp` (docs/asset-formats/RPLY.md),
  live-match `--record-replay` hook records the controller stream to `user/rplyNNNN.dat` + index;
  `wc98-replay` re-drives `SimRunner` from recorded seed + inputs, determinism gate = final world
  stateHash equality (seed-pinned 0x2E7A scripted scenario). Gates: `test_rply` (byte constants +
  writer round-trip), `replay_record_play` (record to disk -> re-drive -> hash MATCH; skip-2 w/o tree).
- Tournament presentation flow RE-confirmed + implemented (Ghidra pass 8, `analysis/pass8/tourney_*.txt`): scene registry FUN_004479d0 enumerates all 49 feflowNN scenes with handler VAs; CONFIRMED mapping = feflow19 groupings, feflow35 group standings (`ssnN` rows), feflow25 QF board, feflow26 SF board + trophy slots `wcup`/`wcps`, feflow27 full bracket + `chmp` champion + third-place `3mat`, feflow23 KO matchup preview. fe_flow composites FEFLOW35->25->26(+wcup)->27 between rounds with standings/bracket overlays at the LAY's own slot positions (`--fe-present`). Gate `fe_render_present` (stat lines shapes/texts nonzero per screen; 50 -> 51).
- `.DCT` EA SRE movie codec RE'd + implemented (Ghidra pass 8, `analysis/pass8/dct_*.txt`,
  docs/reverse-engineering/VIDEO_DCT_NOTES.md): SCHl/mTCD container (BE fourcc + LE self-size;
  dims LE16 at record +12/+14 = 320x240, BE-u16 delay `0x0038`=56 ms at +18); payload word-swapped,
  MSB-first, 64-bit preamble; 2-bit cell header (`11` = inter, 9-bit field skipped) / 6 blocks per
  8x8 cell with present flags — luma 4x4 quadrants + V(b4)/U(b5) chroma 4x4 (4:2:0); EA SRE VLC =
  MPEG-1 ff_mpeg1_vlc_table with EOB=`10` resolving before longer prefix codes, escape `000001`+run(5)+level(8 signed),
  EA scan table = zigzag direct with (10,17) pair swapped, dequant `level*matrix[i]/8` floor, float
  reference IDCT +128. Byte-verified on all 29 tree videos (9,915 frames; INT_FIFA.DCT 1,017-frame
  deterministic checksums, Python-oracle sample-exact). Codec `formats/src/dct.cpp` (+dct_tables.inc)
  docs/asset-formats/VIDEO.md; playback `wc98-native --video-play FILE.DCT` (`app/video.cpp`, Vulkan
  texture upload per frame at measured cadence). Gates: `test_dct` (29 files scan + full-stream decode,
  determinism, SEQ_OPEN smoke), `video_play` (headless decode+upload stat line; suite 51 -> 53).


## [0.3.0] - 2026-11-06 — playable live match + authentic FE fonts, team-select grid, measured scene defaults

### Added
- Interactive playable match (`app/livematch.{hpp,cpp}`, gate `interactive_match` via
  `wc98-native --live-match-smoke N`): FE Enter on Exhibition launches a live on-screen match - sim
  ticked from the wall clock in fixed 1/50 s engine steps, MatchScene + tele camera follow + HUD strip
  drawn every frame, commentary pumped every 2 frames off the World::events trigger seam (SpeechQueue
  -> PHRS decode; null mixer headless), Esc/window-close quits. Keyboard + gamepad drive controller 0
  through the ported input layer's pad word (bindings: pass S/pad A = bit 0x20, shoot D/B, sprint W/Y,
  lob A/X, evade Q/LB, through-ball E/RB, arrows/stick move; settings.ini overrides), proving the
  input->sim command path in-gate. Evidence: shapes=340 uiVerts=264, "pass bit 0x20 reached controller
  0", clock advanced 0 -> 1, events=3, commentary phrase 0 started. Shared `MatchBoot` bootstrap +
  `snapshotFromWorld` now live in livematch.{hpp,cpp} and are reused by `wc98-match-demo` (duplicated
  M6 preflight/palette/FCDB/sheet code deleted).
- Team-select is the authentic `FEFLOW11.LAY` pick grid (`app/fe_flow.cpp drawTeamsGrid`):
  compositor composite of map background, flags, group cells + dynamic ZWCUP pool rows
  (F<id>.FSH shape `f000`, FCDB names, scrollbar thumb in the LAY's `sbar` track); scene registry
  evidence `analysis/pass7/grid{5,6,7}.txt`, doc LAY_COMPOSITOR_NOTES.md; gate
  `fe_render_tournament` (--fe-team) + existing fe_nav/teamselect gates unchanged.
- FE text draws authentic fonts + language knob (`presentation/fe_screen.cpp FeText`): fontID -> face
  CONFIRMED from the exe font pointer table `DAT_0057d2c0` (raw 0x17b8c0, loader loop code VA
  0x46933a opens "%s.ffn"): eras10/eras12/eras20/eras36/eras11i/eras8i from FE/PACK/FEFONTS.BIG,
  BI faces (eras11bi/eras14bi/eras9bi/eras20bi) from IGFONTS.BIG; labels take their TEXTID "id,font"
  face at native line height (measured header lineHeights eras8i=18 … eras36=79 px). `--lang CODE`
  selects the FC{DAN,DUT,ENG,FRE,GER,ITA,JAP,SPA,SWE}.BIN UI-string bank (registry Language key);
  German FEFLOW05 visually verified (umlaut glyphs, EA's own "wirlich" string kept verbatim).
  Gates `fe_text_fonts` (faces=10), `fe_text_lang_ger`; doc LAY_COMPOSITOR_NOTES.md.
- Commentary scene defaults are measured, not inferred (`app/commentary.hpp`): live winedbg launch-under-
  debugger capture pass 7e recorded setter triples (FUN_0046b074 idx4=65 @0x46b095 + idx3=80 @0x46b0f8;
  FUN_0046b130 quintet loop idx{0,2,1,5}=80) and the post-boot table DAT_00632778 = {80,80,80,80,65,80,1};
  port encodes `kScenePct`/`kSceneSpeechPct` (speech scene level 65 % via getter FUN_0048ed9c). Evidence
  `analysis/pass7/percent1.txt`, COMMENTARY_WC98.md §"Scene defaults measured"; gate duckModel.

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
