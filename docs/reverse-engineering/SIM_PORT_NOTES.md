# simulation/ port notes (FIFA 99 -> World Cup 98)

`simulation/` is the M5 module: the match-simulation reconstruction from the sibling project
the sibling fifa99-native `simulation/` module (`f99_sim`, namespace `f99::sim`) ported to this repo as
`wc98_simulation` / `wc98::sim`. The engine family is identical (EA Gen3, Watcom 16.16 fixed-point);
the exe addresses in the source comments are FIFA 99 v1.1 addresses and remain valid for the shared
architecture. Every delta made during the port is recorded below with its evidence.

## 1. Mechanical rewrite (no semantic change)

| Item | From | To |
|---|---|---|
| Namespace | `f99::sim` | `wc98::sim` |
| Includes | `"f99/sim/X"`, `"f99/formats/Y"`, `"f99/file.hpp"` | `"wc98/sim/X"`, `"wc98/formats/Y"`, `"wc98/file.hpp"` |
| Library target | `f99_sim` (links `f99_formats f99_core`) | `wc98_simulation` + alias `wc98::simulation` (links `wc98_formats wc98::core`) |
| Game tree paths | `DATA/CMN/FE/FCDB.DBI`, MOAI container | `COMMON/FCDB.DBI`, `COMMON/ANIM.MO`, `COMMON/GEN4TBLS.VIV` (`findCaseInsensitive`) |
| Generated tables | `ball_tables.inc`, `anim_tables.inc`, `trig_tables.inc` | copied verbatim (byte-exact exe dumps, `// exe 0xADDR` kept) |
| Test skip gate | `gameDirOrSkip()` / `F99_TREE` | `treeDirOrSkip()` / `WC98_TREE` (exit 77 = CTest Skipped) |

Dead code removed during the port: `mt()`/`dif()` accessors in `src/team_ai_ball.cpp` were defined
but referenced nowhere upstream; GCC with `-Werror=unused-function` rejects them, so they are deleted.

## 2. Formats symbols used by simulation (checked against wc98 headers)

| FIFA 99 symbol | World Cup 98 header | status |
|---|---|---|
| `f99::file.hpp: readFile/findCaseInsensitive` | `wc98/file.hpp` (header-only, core INTERFACE) | identical API |
| `f99/formats/big.hpp: BigfFile` | `wc98/formats/big.hpp` | identical (BE BIGF + RefPack) |
| `f99/formats/refpack.hpp` | `wc98/formats/refpack.hpp` | identical |
| `f99::formats::FcdbDatabase::parse` | `wc98/formats/fcdb.hpp` | schema-aware: WC98 layout auto-detected (section 3) |
| `f99/formats/gen4tbls.hpp` (`GEN4TBLS.T`, members G99*) | `wc98/formats/gen4tbls.hpp` (`COMMON/GEN4TBLS.VIV`, members G98*.FMT) | section 6 |
| MOAI animation container (f99 only) | `wc98/formats/anim.hpp: PlayerRig + AnimClip` (ANIM.MO BIGF, 1998 key-block variant) | sections 5 |

## 3. FCDB record layout (`src/data.cpp`, `src/player.cpp`)

Evidence: `docs/reverse-engineering/FCDB.md`, fifa98edit (MIT) layout verification, measured in
`formats/src/fcdb.cpp`.

| Field | FIFA 99 | World Cup 98 | Port handling |
|---|---|---|---|
| Team record | 80 B / 20 LE words, roster base byte 28 (BE u16 slots) | **68 B / 17 words**, roster = **20 little-endian u16 player ids at byte 24** | `FcdbDatabase::parse` schema split; `TeamSheet` built from parsed fields, semantics unchanged: first 11 roster entries start. |
| Player record | bit-packed words (`FUN_005202B0` bitfields) | **byte-packed**: id bytes 0..1 (14 bits), nation 10 bits at bytes 1..2, skills = 17 nibbles in bytes 8..16 (game value `n*4+39`), price bytes 4..5, jersey/role bytes 16..17 | parsed by the wc98 parser; simulation consumes the raw 0..15 **nibbles** - identical scale to what FIFA 99 read from w2/w3/w4, so `Team_buildMatchPlayerAttrs` and all sim consumers are unchanged. |
| Attribute names | per-word bit slots | `kFcdbAttributeNames1998` (aggression, acceleration, attackBias, agility, ballControl, awareness, fitness, creativity, passing, heading, reaction, passBias, shotPower, shotBias, speed, shotAccuracy, tackle) | `expandFcdbPlayer` fills each FIFA 99 runtime byte slot by matching the attribute **name** to its WC98 nibble (player.cpp lines 78-104). Unnamed 1997-era slots keep their FIFA 99 A-offsets. |
| Speed class (`b[0x0a]` -> `A[0x36]`, animation rate) | w4 bits 30..31 stored in the record | **absent** in the 1998 schema | derived from the speed nibble: `>=12 -> 3, >=8 -> 2, >=4 -> 1, else 0` (matches the FIFA 99 rating bands; unclamped like upstream). |
| Alternative position (`b[0x12]`) | w4 bits 10..14 | **absent** | `p.role` passthrough (the 1998 record has one role byte, GK = 0); the club-flag alternation branch is inert. |
| Overall rating (`b[0x27]`, squad.cpp best-rating comparator) | w0 bits 25..31 (0..127 scale) | mean of the 17 skill values (39..99) | passed through: monotonic in both scales, only ordering is consumed. |
| `A[0x39]` | fixed 0x0e | - | kept as the constant 0x0e (no 1998 counterpart). |

## 4. Data loading (`src/data.cpp`, `src/data_formations.cpp`, `src/data_anim.cpp`)

- FCDB: single file `COMMON/FCDB.DBI` (FIFA 99 splits FE/CMN tables); names STR0/STR1 in the same
  DBI set, language suffix ENG. Roster -> squad sheet join unchanged after section 3's parse split.
- Formation tables: `COMMON/GEN4TBLS.VIV` (Vivendi container wrapping one BE BIGF of G98*.FMT
  members) replaces FIFA 99's `DATA/CMN/GEN4TBLS.T`; decompressed by the existing
  `Gen4Tables::parse`. Section sizes verified: PL = 17 slots x 299 zones x {ax,az,dx,dz} int8.
- Animation data: FIFA 99 reads MOAI.VIV (indexed stream container). World Cup 98 stores the same
  keyframed motion in **`COMMON/ANIM.MO`: a BE BIGF archive** parsed by `wc98::formats`
  (`AnimClip`, full-channel decode kept on `AnimClip::fmt`) with the 1998 rig from `PlayerRig`.
  The simulation keeps FIFA 99's instance bookkeeping (clip table, keyA/keyB/frac, flags) and feeds
  it from the wc98 parser; clip lookup goes through the BIGF member name.

## 5. Anim_evalSkeleton (declared at FIFA 99 exe 0x460ACC) - implemented here

Upstream `f99_sim` **does not link**: `Anim_evalSkeleton` was left unported, and its tests are gated
behind `F99_SIM_LINKS=OFF`. World Cup 98 has the full rig data in ANIM.MO + `PlayerRig`, so this
port implements it (src/anim.cpp, end of file) as FK over `wc98::formats`:

- current instance pose = `rig.localPose(clip, keyA/keyB)` blended by `frac/0x10000`
  (translation lerp + quaternion nlerp, shortest arc), mirroring on instance flag bit 1;
- bones[16] slots follow the keeper capsule topology that the ported
  `Keeper_skeletonBlockNormal` tests: pelvis/spine/chest/head, arm A/B shoulder-elbow-tip, leg
  A/B hip-knee-toe (rig joints TorsoChn..LLowLeg); the four distal tips are extrapolated
  (0.55 segment lengths) - the exe's exact distal bone placement is untraced;
- placement anchors the rig root at the player position, rotates clip x/z by the instance yaw
  (cosA/sinA, the `root()` idiom), keeps pose y as height; crossfades with the fading partner
  instance (flag 0x40) by weight +0x44;
- fallback: standing skeleton (head at model unit 84 ~ 1.7 m) whenever no posed clip exists, so
  capsules never degenerate.

Consequence: all three sim test suites build and run in this repo; the `F99_SIM_LINKS` gate is not
ported.

## 6. Formation tables without AT/DF (src/formation.cpp + tests)

The WC98 G98*.FMT members are **column-oriented delta tables** (17 team attributes x columns, parsed
into `Gen4Tables::g98`); the classic PL/FK/PK/KO byte tables decode into the FIFA 99 layouts. The
G99AT/G99DF mentality-blend arrays of FIFA 99 have **no WC98 member**: `Gen4Tables::at/df` stay all
zero, so `Formation_lerpByMentality` (0x4CF5F8) degenerates to endpoint B. The ported
`test_sim_team` asserts exactly that (with the FIFA 99 35/128 mix noted in a comment), and its
slot-target expectations are computed in-test from the loaded G98PL bytes with an fx replica, since
the hard-coded FIFA 99 numbers encode G99PL contents.

## 7. Named constants <-> World Cup 98 exe evidence (GHIDRA_PASS5_IG.md)

| simulation constant / idiom | FIFA 99 address in code | WC98 evidence (pass 5) |
|---|---|---|
| `fx()` rounding `(a*b+0x8000)>>16` | throughout | "all `(a*b+0x8000)>>16` Watcom fixed-point idiom", `FUN_004b1f30` (IG9) |
| ball roll drag pair: threshold `0x20000`, slope `0xeab`, base `0xc391`, low-speed rate `0x61c8` (`ball.cpp` rollUpdate 754/771) | ball tick tables | `FUN_004b1f30`: `< 0x20000 -> x0x61c8; else x0xeab + 0xc391` (spin/friction, pass 5 l.61) |
| ball flags bit5 possession (`flags & ~0x20`, `setPossessed`) | `Player_setBallOwner` path | `FUN_004b1a60` clears/sets bit5, `FUN_004b1a40` set/clear from EAX&1; flags word `DAT_00646a24` bit5 (l.93) |
| ball flags bit6 0x40 attached-to-holder (`rollUpdate` gate, ball tick branch) | ball tick | `FUN_004b1ce0` snap-attach sets 0x40; attached branch prev-pos copy `DAT_0064691c/20/24 <- DAT_00646928/2c/30` in ball tick `FUN_004b3320` (l.45) |
| `kTickDt = 0x20000` (2 frames @ 60 Hz), frame clock mod 0x3c (`sim.hpp:11`) | 0x41B7FC / MatchLoop_stepTick 0x41B8AC | match clock `FUN_004b5290`: `DAT_0064121c += DAT_0064123c`, mod 0x3c @ 60 Hz (l.125) |
| motion predictor / target-window planner (`Motion_predictDistance` 0x4CBA60, movdfl windows in player_states_run) | 0x4CBA60 | `FUN_00518250` = win\movdfl.c target-window motion planner, identity via assert string `s_win_movdfl_c_0056d178` (l.146/220) |
| keyframed motion kernel (`Anim_*` instance update, skeleton eval 0x460ACC) | 0x460ACC family | pass 5 "keyframed motion kernel" architecture confirmation; WC98 data = `COMMON/ANIM.MO` BIGF + rig (section 5) |


## 8. Fast-forward bits, sequence pump, and the presentation-driven restart lane (pass 14)

`Sim_advance` (0x454ED8) handles `flags278 & 0xc0` by draining time slices with a fixed due-date
(0x1e0000) until phase 9 clears the bits. Two facts make this lane fragile in simulation-only runs:

- **The sequence pump only advances an active sequence** (`rules.cpp`: `if (m.seqId != 0) { seqFrames
  += simFrames; MatchSeq_run(...); }`). With an empty `MatchSeq` queue, booking's
  `MatchSeq_startNext` pops id 0, so no handler runs and the FF loop never observes a phase-9 exit:
  it spins forever (measured >5.8M slices in one tick).
- **The PK lineup (`rt=4`) is queued by the presentation/replay-complete pump in the exe**, not by
  the foul handler; `Match_foulInBox` only queues booking (`push(5)`). A simulation-only lane must
  queue the lineup itself: `MatchSeq_push(w, 4)` immediately behind the booking entry (the same
  ring slot mechanics the replay-complete path uses), so booking's `startNext` pops it and the FF
  drain terminates. The penalty scenario gate + self-test `penalty` step do exactly this.
- `scriptIdle` (`scriptState & 1`) and the `seqFrames > 0x5DC0` escape both require an active
  sequence; a cut-scene lane entered with seqId 0 cannot time out in-sim (presentation state, not
  modelled — SIMULATION_STATUS.md scope).

Verified by gates: `test_sim_scenarios` penalty scenario (award -> lineup -> spot kick -> goal) and
`self_test_gameplay` steps `foul-card` + `penalty`; the FF spin was reproduced first (>5.8M slices,
seq 0/0, phase 0xa, status 0x5) before the queue fix terminated it.

## Kick family WC98 deltas (HANDLER_DIFF_AUDIT pair 24, pass 11)

FIFA-99 `Kick_plan` 0x4D0CFC was re-extracted against WC98 `FUN_00502fc0`; the port in
`simulation/src/kick.cpp` now carries the WC98 constants. Kick-state record layout shifts (flags at
WC98 `+0x3c` vs FIFA-99 `+0x1e4`, i.e. bit 4 pending / clear 0x10) are semantic-model only, never
ported verbatim. Mapping as implemented:

| WC98 (0x502FC0) | Port state |
|---|---|
| `local_2c` clamp: > DAT_0056d148 (= 1243.2, double read from the exe image) -> `0x534`, stored as short at record +0x14 (KickVec angle slot) | implemented (`kParamClampMax`) |
| Power base: DAT_0064015c == 2 -> 0x960, else 0xb40; kind==1 + FUN_00500660 != 0 keeps DAT_00641314 (half-length) | implemented (`kPowerPhase2`/`kPowerBase`) |
| Power +50%: `>>1` bump when DAT_00641011 bit 0x40 (side 0) / 0x80 (side 1) matches the team side byte `**(player+0x20)` | implemented via new `w.opt.kickAssist` field |
| Facing ladder `uStack_18 - [player+0x108] & 0x3ff`, wrap abs<0x200, thresholds 0x55/199/0x180 -> classes 0..3; sign sets/clears flag bit 2 | identical to FIFA-99 port (annotated) |
| `param_6 == 0x10` early branch: uStack_30 = 0, iStack_28 = -1, flags `\|= 4` **without** clearing bits 0/1 (FIFA-99 cleared them via `& 0xfa`) | WC98 semantics applied |
| DAT_0064015c == 9 switch gates incl. cVar1 = player byte +0x82 in {2,3,4} -> special-kick re-map param_5 = 0xe (case 0/2, side class 3 && dist < 0x1e0) | identical to FIFA-99 port (annotated; WC98 reads the byte at +0x82 vs FIFA-99 +0x9A) |
| kind==3 && animClass==2 && posI.z sign (+0xa8) == team side -> animClass = 1 | identical (annotated) |

**OPEN items (not derivable from the decompile alone; do not fake):**

- Power *consumption*: `if (power < dist && target)` calls FUN_004f3590 / FUN_004f34b0 (offsetXZ,
  sin/cos scale via FUN_004af940) with register args hidden by Ghidra. The vector clamp is computed
  in the port (`power` local) but NOT applied; extract call-site register state to close.
- FUN_00500660 predicate: needs `player+8 == 0`, `FUN_0049eb60(dist, ...)` (undumped) and team+0x138 == 0.
  The port approximates the observable effect as slot-0 && kind==1 -> half-length power.
- DAT_00641011 is a live (front-end-written) options global; `w.opt.kickAssist` defaults to 0, so no
  boost fires in sim runs until the front end sets it (FE lane owns that write).
- WC98 record +0x19 bit 0x80 = "target pointer present" sticky flag (tail copies target from EBX when
  set); the port models the ternary only, not the sticky flag.
- Kick_planToPoint 0x4D0BF0 has no WC98 candidate (audit MISSING); kick Arm/execute WC98 bodies are
  pairs 21/27 (`0x502f40`, `0x5146c0`), not this slice.


## TeamAI decision-family WC98 deltas (HANDLER_DIFF_AUDIT pairs 43-47, 16, pass 11)

`TeamAI_onBallAct_4DD324` -> WC98 `FUN_0050cf50` and `TeamAI_tryHeaderPass_4DF2CC` -> WC98
`FUN_0050e740` were reconciled constant-by-constant; the ports in `simulation/src/team_ai_ball.cpp`
now carry the WC98 constants (FIFA-99 citations kept, WC98 address cited where semantics changed).
Record-layout offsets (WC98 player +0xd8 flags vs FIFA-99 record fields) are semantic-model only,
never ported verbatim. Mapping as implemented:

| WC98 constant / gate | Port state |
|---|---|
| Pass thresholds loaded RAW from DAT_0058bfd1 / bfd6 / bfdb (byte, mentality-indexed); FIFA-99's `+ DAT_0058b6a0` formation-base addition is gone | implemented: onBallAct f(0x40)/f(0x44)/f(0x48) and tryHeaderPass k66/k62/t96 load without `adj` |
| Dribble budget (pair 46 L_4dd6ad): first FUN_004f33e0() draw discarded, second low half used: `budget = table[attr0x33] + (rand & 0xf)`; no difficulty scaling; `[team+0x144]==1 && [team+0x13c]!=0` -> budget `>>=2`, scratch draw `>>=3` | implemented (FIFA-99: conditional 0x1f/0xf draws, `<< (dif==2?0:1)`, f210==1 -> `>>=3`) |
| Difficulty-0 sector-reach clamp `- reach>>2` absent in WC98 | implemented (clamp removed from f(0x0c)) |
| Near-chaser dribble gate LAB_0050d...: 0xc0 / 0x180 (FIFA-99 0x150 / 0x1b0) | implemented |
| Difficulty-0 random gate: `[team+0x124]==0 && (rand & 3) != 0` -> dribble lane (FIFA-99 `dif==0 && !(rand&1)`) | implemented (mask widened, test polarity per WC98) |
| Wing-clear chaser gate LAB_0050dff3: dist < 0xc0 selects pass lanes (FIFA-99 0xf0) | implemented |
| L_4de946 entry: budget + **0xb4** `< frames` (FIFA-99 `budget + 0x1e`) | implemented |
| Facing/frames fused gate: `(facing wrap-abs < 0x55) || frames > 0x21c` -> select lane; the FIFA-99 separate `frames <= 0x5a && !flag` skip is gone | implemented (constants 0x55 / 0x21c) |
| L_4deb5b gate: `(reach + attr [+half]) >> 1 < distBall \|\| (rand & 0xf)` replaces the kSectorClose[sector] table compare | implemented (`kSectorClose` kept for reference, marked superseded) |
| Shot-lane goal-side probe offset +/- **0x40** (FIFA-99 +/- 0x80); gate global DAT_0059fd18 -> WC98 DAT_00595230 | implemented |
| Pending skill code: `== 0x71` selects the skill branch (FIFA-99 0x73; WC98 anim-id family shifts -2, cf. pair 16) | implemented in L_4dd814 |
| tryHeaderPass passback gate: `[team+0x14] in {5, 6}` (FIFA-99 `== 6`) writes player +0xEC = target | implemented (`p.f014 == 5 \|\| p.f014 == 6`) |
| tryHeaderPass k5e adjust `(2 - [team+0x124]) * -8` and cross lift `(rel[0]>>3)+0x72`, clamp `< 0xe4 -> 0xe4` | identical to FIFA-99 port (annotated) |
| Player_dodgesTackle (pair 16, WC98 FUN_005108b0): flag DAT_0064019e bit 0; dodge anim ids {0x6C,0x71,0x7C,0x7D} stand / {0x71,0x72,0x73} slide (FIFA-99 {0x6C,0x73,0x7E,0x7F}/{0x73,0x74,0x75}) | implemented in `simulation/src/player_helpers.cpp` |

**OPEN items (not derivable from the decompile alone; do not fake):**

- Pair 43 (`TeamAI_onBallWon_4DC614`) and pair 44 (`TeamAI_planAirKick_4DCAC8`): audit laneA/laneB
  empty / only a 5-const laneB candidate at 0x50CC35; no WC98 body was extractable. The ports stay
  at FIFA-99 semantics (audit rows "TeamAI_onBallWon: laneA=[] laneB=[] consts=0").
- Pair 45 (`TeamAI_replanShot_4DCE94`): the audit candidate `0x4F4380` is a velocity-clamp/aim
  family routine (reads record +0xd8 flags, clamps 0x68000/0x40000/0x18000, writes kick vectors at
  +0x1C..+0x30) whose structure bears no relation to the FIFA-99 body (team+0x1E0 lookup ->
  Kick_planToPoint/Kick_plan). The match is a false positive; `TeamAI_replanShot_4DCE94` in
  `simulation/src/ai.cpp` is unchanged, WC98 location unknown.
- onBallAct flag detection: FIFA-99's wrapAbs + mask-draw block (`(1<<((2-dif)<<2-2))-1`) plus the
  `frames <= 0x5a && !flag` skip became a budget/counter lane in WC98 (framePasses outputs
  local_3c/local_50 pointer args, compared against DAT_006561dc counter < local_84/local_78) with
  register args hidden by Ghidra. The port keeps the FIFA-99 mask-draw model; constant gates in the
  fused gate row above are implemented.
- WC98 fuses the give-and-go guard at L_4de3b8 into one condition that adds a `[team+0xf0] != 0`
  term (f1bc pointer set) and retargets to the shot-select lane LAB_0050d9c8; the extra term is not
  expressible in the port's label model without re-modelling that lane. Gates left at FIFA-99 shape.
- tryHeaderPass tail: WC98 calls Kick_plan (FUN_00502fc0) with `(uVar8, 0, local_20, local_2c,
  0 < iVar5)` register args hidden; the port keeps the FIFA-99 kickSpec + planToPoint model.
- RNG stream divergence: WC98 consumes an extra discarded draw in the budget block and drops the
  conditional 0x1f draw, so fixed-seed scenario gates seeded from the FIFA-99 stream diverge. No
  new deterministic scenario gate is added for these deltas (documented instead).
- Runtime-global mapping used by onBallAct via `aiD32`: FIFA-99 DAT_0059fc5c -> WC98 DAT_00595180,
  DAT_0059fd18 -> DAT_00595230 (shot-lane gate), DAT_0059fd24 / DAT_0059fd28 -> unmapped (values
  live; port reads through the shared runtime global lane).

## Verification

- `cmake --preset debug && cmake --build build/debug -j16 && ctest --test-dir build/debug`:
  19/19 pass (16 pre-existing + test_sim_core, test_sim_players, test_sim_team; the two FCDB/PL data
  suites run against WC98_TREE=<extracted game tree>).
- No stubs: `grep -rniE "todo|not implemented|stub" simulation/` is empty; the only upstream gap
  (section 5) is closed with a real WC98 implementation.
