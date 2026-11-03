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

## Verification

- `cmake --preset debug && cmake --build build/debug -j16 && ctest --test-dir build/debug`:
  19/19 pass (16 pre-existing + test_sim_core, test_sim_players, test_sim_team; the two FCDB/PL data
  suites run against WC98_TREE=<extracted game tree>).
- No stubs: `grep -rniE "todo|not implemented|stub" simulation/` is empty; the only upstream gap
  (section 5) is closed with a real WC98 implementation.
