# Ghidra Pass 5 — In-Game Match Simulation Core (2026-11-02)

Headless PyGhidra runs: scripts `ghidra-scripts/GhidraWc98Ig{1..13}.py`; decompile dumps in
`analysis/pass5/FIFAWC.EXE_ig{1..10}.txt` + inventories `FIFAWC.EXE_funcs.txt`,
`FIFAWC.EXE_density.txt`, `FIFAWC.EXE_imports.txt`, `FIFAWC.EXE_datuse.txt`,
`FIFAWC.EXE_bands.txt`, `FIFAWC.EXE_api.txt`. Every claim cites an address; confidence per item as
Confirmed (decompile + xref evidence) / Hypothesis / Unknown.

## Per-tick match chain (Confirmed, ig2/ig3/ig5/ig6/ig7)

FE tick `FUN_0047a8f4` → UI interpreter `FUN_00444b28` per frame:
- `FUN_0046add4` (118 instrs, ig3): gate `DAT_0057f648 == 8` → calls `FUN_004b163c` + `FUN_004c8f30`;
  else `FUN_004c9580`. `DAT_0057d538` is the enable flag (users: FUN_0046ac30/acec/ad54/ad8c/add4).
- Screen handlers `FUN_0043e010/0x43e61c/0x43eb5c` → `FUN_0043bc94` (137, ig3): gate
  `DAT_0057fdd4 == '\b'` (state 8) → `FUN_004b163c` + `FUN_004c8f30`; else `FUN_004b3ce0`.
- `FUN_004b163c` (21, ig3) is NOT sim: copies 0x40 dwords EAX→`DAT_00581ad4` (settings/state push).
- Scheduler path: frame driver `FUN_0049d900` (485, ig7; **no static callers** — BSS function-pointer
  registration) → budget dispatcher `FUN_0050f310` (228, ig6: time-slice FIFO at `DAT_00656ae8`,
  stride 0xc records) → **per-tick match update `FUN_0049daf0`** (814, ig5).

### `FUN_0049daf0` = the in-match frame tick (Confirmed, ig5.txt)
Fixed-point time: `DAT_00641230 += dt<<16`; carry → integer sim ticks into `DAT_00641250`, delta
into `DAT_0064122c` (per-frame tick quantum used by all consumers below). Substep loop: while
`(uVar1>>2)` iterations `{ FUN_004fcc80(); FUN_004fd1a0(); FUN_0049b0a0() }` (`FUN_004fcc80` →
`FUN_004a4230`, 13 instrs). Then, in order: `FUN_004a9650`, `FUN_004944e0` (camera pan),
`FUN_004a8520` (state step via slot `PTR_FUN_00581ad0` = `FUN_004b12f0`, raw-verified),
**ball tick `FUN_004b3320`**, **camera director `FUN_004b4b60`**, cam functions
(`FUN_0049c330/c1a0/c5b0/e200`), replay-mode gate (`DAT_00640fb4 != 10 && != 0xc` → `FUN_0049c900`),
focus selection (cam mode `DAT_0064015c == 9`: focus = ball object `[DAT_00646a28+0xa0]` vs
`&DAT_0064691c` written to `DAT_006401d8`), then tail `(*(code *)PTR_FUN_00581acc)()` =
`FUN_004b1610` (raw slot 0x581acc → 0x4b1610 verified host-side; also 0x581ad0 → 0x4b12f0).

### `FUN_004944e0` camera pan (Confirmed, ig6): view at `DAT_006401e8/01f0` moves by focus record
`DAT_006401f4` fields `[+0x20]>>3*dt*?`, `[+0x24]`, flags `[+0x3c]&0xf` nibble-decoded; clamped to
pitch bounds `±(DAT_00641310-0x20)`, `±(DAT_00641314-0x20)` (pitch half-extents).

## Ball state + physics (Confirmed, ig5/ig9 dumps)

Ball lives in a DGROUP global block `DAT_00645c74–0x646a40` plus an object pointer chain.

**Ball tick `FUN_004b3320`** (347, ig5): snapshot fine pos
`DAT_00645cc8/cc/cd0 ← DAT_006469e8/ec/f0`; branch on flags:
- `(DAT_00646a24 & 0x40) == 0 || DAT_006469d4 != 0` → if `(DAT_00646918 & 2)==0`: `FUN_004b2a40`(1342)
  + `FUN_004b2090`(2190); else spin path `FUN_004b1f30`(337).
- attached branch (`&0x40` and no carry counter): prev-pos copy `DAT_0064691c/20/24 ← DAT_00646928/2c/30`,
  `FUN_004b1d40(0,0)`, `FUN_004b2f80()`, `FUN_004b2090()`, clear bit7 of `DAT_00646a24`.
- timers `DAT_00645ca8/0x645cac` decrement by `DAT_0064122c`; at zero, target triple
  `DAT_00645c98/a0 → DAT_00646a00/04/08`; pos latch `DAT_0064697c/80/84 ← 0x64691c/20/24`.

**Ball physics kernel `FUN_004b2090` (2190 instrs, ig2)** and **`FUN_004b2a40` (1342, ig2)** —
called only from the ball tick. All-integer 16.16 fixed-point (zero `(float)` casts in the whole
IG cluster; Watcom math statically linked: no sin/cos/sqrt DLL imports exist at all — import list
in `FIFAWC.EXE_imports.txt` is 35 slots, DirectDraw/joy/user only).

**Drag/carrier coupling `FUN_004b2f80` (923, ig5)**: relative velocity between ball fine pos
(`[obj+0xd9c/da0/da4]<<16`) and carrier anchor record fields `[rec 0x2e9..0xeb]<<16`, scaled by
`0x100000000/(dt<<16)`, stored to rec `0x2e6/e7/e8`, clamped per-axis to ±`0xf0000/0xa0000/0xf0000`;
then sum-of-squares (speed²). Evidence: ig5 body lines.

**Spin/friction `FUN_004b1f30` (337, ig9)**: piecewise linear drag on velocity `[+0x30]`:
if `< 0x20000` → `×0x61c8`; else `×0xeab + 0xc391` (all `(a*b+0x8000)>>16` Watcom fixed-point idiom);
accumulated into spin phase `[+0xd38]`, coarse copy `[+0xd3c]=[+0xd38]>>16`; angular velocity from
`FUN_004f3db4` → `[+0xd40]`; per-object time bookkeeping at `[+0x44]/[+0x48]`, `[+0xd1c/d2c/d30]`.

**Possession**:
- `FUN_004b1a60` (469, ig5) = possession transfer: clears bit5 (`&~0x20`) then sets bit5 from arg;
  rotates holder chain `DAT_00646a28 → DAT_00646a2c → DAT_00646a30`; stamps
  `DAT_006469d0 = DAT_00641220` (last-change sim time); object record links: `[obj+0x20]` type record,
  `[type+0xe0] = obj` backlink, `[type+0xec]`, `+0x108/+0x114` last-taker slots; side check via
  `**[a+0x20] != **[b+0x20]` clearing focus fields.
- `FUN_004b1a40` (30, ig9) = set/clear possession bit5 from EAX&1.
- `FUN_004b1ce0` (81, ig9) = snap ball to carrier: if ball==focus==lock (`DAT_00646a28 == DAT_006401c8
  == DAT_00646a0c`) shift pos→prev and load new triple from param, set bit `0x40` (attached).
- `FUN_004b1780` (196, ig9) = ball reset: zeroes holder chain + copies pos triples, clears flags
  `DAT_00646a24 & 0xfb`.
- `FUN_004b1850` (486, ig4) = full object-state init (zeroes `0x645c74–0x646a2c`, dual-scale pos
  write: coarse `DAT_00646964/68/6c` and fine `DAT_006469e8/ec/f0 = v<<16` when EBX==0, swapped when
  EBX!=0 — confirms two coordinate scales).

### Ball field-offset table (global block; all Confirmed from cited bodies)
| DAT | role | evidence |
|---|---|---|
| 0x64691c/20/24 | pos coarse x,y,z (camera focus target `&DAT_0064691c`) | FUN_0049daf0, FUN_004b3320, FUN_004b1ce0 |
| 0x646928/2c/30 | pos previous (per-tick shift) | FUN_004b3320, FUN_004b1ce0 |
| 0x646964/68/6c | coarse pos copy | FUN_004b1850 |
| 0x6469e8/ec/f0 | fine pos (16.16) | FUN_004b3320 first line, FUN_004b1850 |
| 0x645cc8/cc/cd0 | fine pos snapshot for this tick | FUN_004b3320 |
| 0x64697c/80/84 | latched pos when timers zero | FUN_004b3320 |
| 0x646a00/04/08 | target triple on timer expiry (from 0x645c98/a0) | FUN_004b3320 |
| 0x645ca8 / 0x645cac | event timers, decrement by dt `DAT_0064122c` | FUN_004b3320 |
| 0x6469d0 | last possession-change time (`= DAT_00641220`) | FUN_004b1a60 |
| 0x6469d4 | carry counter (≠0 forces physics branch) | FUN_004b3320, FUN_004b1850 |
| 0x646a24 | ball flags: bit0 in-play(cam gate), bit2 side-mismatch, bit4 init, bit5 possession, bit6 attached-to-holder (0x40), bit7 clear-on-attach | FUN_004b4b60(bit0), FUN_004c08d0(|4), FUN_004b1a60(0x20), FUN_004b1ce0(0x40), FUN_004b3320 |
| 0x646918 | ball mode bits (bit2 = spin-only path) | FUN_004b3320 |
| 0x646a28 / a2c / a30 | ball object ptr / previous holder / chain link | FUN_004b1a60, FUN_004b1780 |
| 0x646a0c | focus-lock object (with 0x6401c8) | FUN_004b1ce0, FUN_004b1a60 |

Object record fields (`DAT_00646a28` target): `+0x20` type-record ptr, `+0xa0` position array
(camera focus), `+8` counter; type record: `+0xe0` holder backlink, `+0xec`, `+0x108/+0x114` last-taker.
Ball big record (from FUN_004b2f80/004b1f30): `+0x30` velocity 16.16, `+0x44/48` time bookkeeping,
`+0xd9c/da0/da4` fine pos<<16 source, `+0xd1c/d2c/d30` spin anchor, `+0xd38` spin phase 16.16,
`+0xd3c` coarse spin, `+0x40 d40` angular velocity, carrier anchor record: `0x2d1/0x2d2` time base,
`0x2e6/e7/e8` drag velocity out, `0x2e9/ea/eb` anchor pos in, `+0x310*4` flag byte.

## Player movement / motion update (Confirmed)

**Motion kernel `FUN_004b2090`** doubles as per-entity keyframed movement update (ig2 body):
record base EAX: target pos `[+0xd74/78/d7c]`, prev pos `[+0xd80/84/88]`, coarse grid
`[+0xca8..cbc] = pos>>16`, delta/velocity vector `[+0x24/28/2c] = target - prev`, interpolation
weight from `FUN_004f3190(dt)` → `[+0x30]`; keyframe array at `[+0x64 + idx*0x10]` with
`{x@+0x64, y@+0x68, z@+0x6c, flags@+0x70}`, index at `[+0xba4]`, active keyframe copy at `[+0xd58..]`;
flag bits at `+0xdb0` (1: last-frame marker, 2: uniform-assigned), `+0xc3c` short id (-1 sentinel).
Called every ball tick from `FUN_004b3320`; shared by camera transform `FUN_004b61d0`(1571) and REPLAY
entry `FUN_004b89f0` (first function after the 0x4b7510 gap).

**Set-piece/placement machine `FUN_004c09a0`** (2350, ig5): switch(param_1=1..9) per-player record:
pos coarse fields `[+0xba4/ba8/bac]` 16.16; velocity x/z `[+0xb98]/[+0xba0]` accelerated by
`×0xc000>>16`, clamped ±`0x50000` when ball flag `DAT_00646a24&0x40`; target triple `[+0xbb0/bb4/bb8]`;
state `[+0xb5c]=2`, phase `[+0xb60]=1|4`; id/uniform `[+0xb54]` copied to `[+0xbe0]`; coarse grid
`[+0xccc/cd0/cd4] = pos>>16`. Pitch constants: half-length `DAT_006412f8>>1`, width anchor
`DAT_00641314`, offsets `DAT_006412e8/0x641324`, `DAT_00641310`. Offside/side helper `FUN_004c08d0`
(165, ig7): `[+0xbda/bdc/bde]` sentinels (-1), sets ball bit2 (`|4`) on goal-side mismatch via
`[*(DAT_00645cb0)+0x20]+4`.

**Match clock `FUN_004b5290`** (460, ig7): frame counter `DAT_0064121c += DAT_0064123c`, mod 0x3c →
seconds `DAT_00641210/0x641214`; running-time accumulator `DAT_00641238` (caps at 600) / `DAT_00641234`,
minutes `DAT_00641218 = DAT_00641234/0x3c` capped by `DAT_0064120c>>2`; half-length seconds at
`DAT_0064120c`; time-up sets flag bit4 of `DAT_00641274`, calls `FUN_004bedb0()` (whistle/stoppage —
Hypothesis) and the tick-end slot. Keeper-cam countdown: `(DAT_00641011 & 0x20) && cammode==9` →
`DAT_00641260--`.

**Camera director `FUN_004b4b60`** (555, ig6): mode gates (`DAT_00640fb4==10` replay → `FUN_004b89f0`;
`DAT_0064126c==3`; cam modes `DAT_0064015c==9`), ball flag bit0 gate into cut logic `FUN_004b58e0`
(1188, ig2 — touches 0x646a28/2c ball pointers + flags 17×), zoom per cam type in `FUN_004b5e60`
(571, ig7): distances `0xa0000`=10.0, `0x60000`=6.0, `0x30000`=3.0 (16.16) → `DAT_00646bb8`.

## AI decision entry (Confirmed chain + module-location negative)

- Per-frame player entries: `FUN_004c8f30` (178, ig3): reads control-class byte
  `(&DAT_00580200)[idx&0x7f]`; class 8 (user-controlled; evidence: kit-conversion branch) + state
  `DAT_0057fdd4==0x10` → shape/CLUT conversion (`FUN_004a3410`, `FUN_004a2d30`) then **motion-window
  update `FUN_00518250`** (1117); class queue slot `(&DAT_006419e4)[idx]`.
- `FUN_004c9580` (118, ig3): same shape pair for non-user path → `FUN_00518250`.
  Wrapper `FUN_004cbfa0` (13) called from `FUN_0043a424` (FE frame driver).
- **Decision-execution engine = `FUN_00518250`** (ig4): Watcom asserts name it:
  file string `s_win_movdfl_c_0056d178`, messages `"movdfl - INVALID SHAPE TYPE"` @0x56d188,
  `"INVALID TARGET WINDOW"` @0x56d1b0 (ig4 body lines). It computes per-object target windows from
  clip rect globals `DAT_0057fdc4/c8/cc/d0`, state byte `DAT_0057fdd4` (window tiers: `<0xf`, `>3`,
  shape selectors via `param[7]`, class-scaled speed `(class+1)&~2 * v >>3`), deferral slot
  `DAT_00656e28`, window fns `FUN_004a3790/0x4a3900`. This is the motion-deferral (target-window)
  planner that converts AI/placement decisions into keyframed movement consumed by FUN_004b2090.

**Negative results (AI band):**
- The functions around getter `FUN_004c9710` ("AI-ver V13-6-14") are **joystick/input**, not AI:
  `FUN_004c9720` calls `joyGetNumDevs/joyGetDevCapsA`; `FUN_004c9770` reads registry
  `\System\CurrentControlSet\Control\…CurrentJoystickSettings` (@0x56a15c/0x56a144); `FUN_004c99f0`
  = `joyGetPosEx` wrapper (ig10). Module getters anchor string leaves only — bands are NOT
  getter-adjacency (pass-4 methodology note holds; CONFIRMED negative).
- The dense 0x4c3xxx–0x4c82xx cluster is FE options/settings UI (callers are menu screens
  FUN_00431528/0x432fd4/0x437ed0…, ig8 `TBLUSR 585f50` → `FUN_004c8210` difficulty setter: fields
  +0x24 = skill/16, +0x28/2c = skill/10 from `DAT_0057f690`; role-callback tables at raw slots
  0x585d10/0x585d38/0x585f50-fbc have consumers only in options-screen code).
- No function in that cluster is invoked from the per-tick match chain; CPU-player decision core
  was not located statically — it is reached through BSS-registered task pointers (frame driver
  `FUN_0049d900` has zero static callers, proving pointer registration exists).

## Goalkeeper logic (Negative + adjacent evidence)

- Assert string `"Save type not handled in GOALIETASK_genericVerticalSave"` @VA 0x56ea48: **zero
  consumers** — no Ghidra xref, no code immediate, no DGROUP pointer slot to [0x56ea38–0x56ec48]
  (exhaustive raw scans in run ig9 + host u32 scan; pool neighbours are CRT fatal strings:
  "Floating-point support not loaded" @0x56ea24, FP-exception texts @0x56eb07+). The GOALIETASK_*
  function names exist only as stripped-assert residue → keeper save code cannot be anchored by name.
- `"team has no goalie in TEAM_findgoalie"` @VA 0x5683d4: same — zero consumers (run Ig9 scan, hits=0).
- Adjacent confirmed goalkeeper-adjacent logic: goal-side record `DAT_00645cb0` (+0x20 side bit at
  `[+4]`, FUN_004c08d0 ig7), keeper-camera gates `(DAT_00641011 & 0x20)` with cam mode 9 in
  FUN_004b5290/FUN_004b4b60, `FUN_004c09a0` cases 3/4 (keeper velocity clamp ×0xc000 on +0xb98/+0xba0).

## Player record (motion) field table — proposed merged view
| Offset | role | evidence |
|---|---|---|
| +0x24/28/2c | delta vector (target−prev), used as velocity | FUN_004b2090 ig2 |
| +0x30 | interpolation weight / speed 16.16 | FUN_004b2090, FUN_004b1f30 (friction input) |
| +0x64+idx*0x10 | keyframe {x,y,z,flags@+0x70} | FUN_004b2090 |
| +0xba4 | active keyframe index (motion view) / pos.x 16.16 (set-piece writes) — two record views over the same big record, see caveats | FUN_004b2090 vs FUN_004c09a0 ig5 |
| +0xb98/+0xba0 | vel x/z 16.16, clamp ±0x50000 in-play | FUN_004c09a0 case3/4 |
| +0xbb0/bb4/bb8 | target pos triple | FUN_004c09a0 case5 (→ +0xb8c/90/94) |
| +0xb54 / +0xbe0 | player id/uniform; assigned uniform (-1 sentinel) | FUN_004c09a0, FUN_004c08d0 |
| +0xb5c/+0xb60 | state (2=setpiece) / phase (1 kick-off, 4) | FUN_004c09a0 case5/7 |
| +0xca8..cbc | coarse pos = fine>>16 | FUN_004b2090, FUN_004b1f30 |
| +0xccc/cd0/cd4 | coarse grid pos copy | FUN_004c09a0 case5, FUN_004b1f30 |
| +0xc3c | id short (-1) | FUN_004b2090/004b1f30 |
| +0xd74..d88 | target / prev world pos 16.16 | FUN_004b2090 |
| +0xd58.. | active keyframe record copy (flags +0xd5a bit decode) | FUN_004b2090 |
| +0xdb0 | flags bit1/bit2 (last-frame, uniform-assigned) | FUN_004b2090 |
| +0x580200[idx&0x7f] | control class byte: 0 invalid(assert), 4 = CPU?, 8 = user-controlled (kit-convert branch) — 4 mapping is Hypothesis | FUN_004c8f30, FUN_00518250 assert |

## Function map
| Addr | Size | Role | Confidence |
|---|---|---|---|
| FUN_0049daf0 | 814 | per-tick match update (dt carry, substeps, pan, ball tick, cam director) | Confirmed |
| FUN_0050f310 | 228 | scheduler time-slice dispatcher → 0x49daf0 | Confirmed |
| FUN_0049d900 | 485 | frame driver (BSS-registered; speed factor DAT_00640fbc) | Confirmed entry, Unk. caller |
| FUN_004b3320 | 347 | ball tick (branches to physics/spin/attach) | Confirmed |
| FUN_004b2090 | 2190 | motion/physics kernel (keyframe interp, delta vel) | Confirmed |
| FUN_004b2a40 | 1342 | ball physics companion (kick branch) | Confirmed callee; role Hypothesis |
| FUN_004b1f30 | 337 | spin + piecewise friction integrator | Confirmed |
| FUN_004b2f80 | 923 | carrier-drag velocity clamp (ball↔anchor) | Confirmed |
| FUN_004b1a60/4b1a40/4b1ce0/4b1780/4b1850 | — | possession transfer / bit set / snap-attach / reset / init | Confirmed |
| FUN_004b1d40 | 423 | ball pos setter (ig5) | Confirmed callee |
| FUN_004b5290 | 460 | match clock + whistle gate FUN_004bedb0 | Confirmed |
| FUN_004b4b60 | 555 | camera director (cuts, keeper-cam, replay) | Confirmed |
| FUN_004b5e60 | 571 | cam zoom table per mode | Confirmed |
| FUN_004b58e0 | 1188 | goal-cam cut logic (ball ptrs+flags heavy) | Hypothesis |
| FUN_004944e0 | 484 | camera pan integrator | Confirmed |
| FUN_004c09a0 | 2350 | set-piece placement machine (9 cases) | Confirmed |
| FUN_004c08d0 | 165 | goal-side/offside bit setter | Confirmed |
| FUN_004b61d0 | 1571 | world→view transform shared by camera/REPLAY/motion | Hypothesis (transform) |
| FUN_004c8f30 / FUN_004c9580 | 178/118 | per-frame player update entries (class-gated user/CPU) → movdfl | Confirmed chain, class-4 Hypothesis |
| FUN_00518250 | 1117 | win\movdfl.c target-window motion planner (decision execution) | Confirmed identity via asserts |
| FUN_0046add4 / FUN_0043bc94 | 118/137 | FE→IG per-frame bridges, state gates 0x57f648/0x57fdd4 == 8 | Confirmed |
| FUN_004b163c | 21 | 0x40-dword settings copy → DAT_00581ad4 (NOT tick) | Confirmed negative |
| FUN_00490c48 getters region | — | version manifest leaves; bands not getter-adjacent | Confirmed negative |

## Methodology facts learned (Confirmed, this pass)
- Simulation is **pure integer 16.16 fixed-point**: zero float casts in FUN_004b2090/4b3320/4b1f30/
  4b2f80/4c09a0 bodies; `(lo>>16)+hi*0x10000+(bit15)` = Watcom `(longlong a*b + 0x8000)>>16`.
- No trig imports exist (35-slot import table, `FIFAWC.EXE_imports.txt`); math is statically linked.
- Frame driver with zero static callers (`FUN_0049d900`) proves BSS function-pointer task
  registration; per-player AI tasks are reached only through those invisible slots → dynamic capture
  (Wine) is the remaining route for CPU decision code and keeper saves.

## Next steps (prioritized)
1. Wine dynamic capture of DGROUP 0x645c74–0x646a40 + object ptr 0x646a28 during kickoff to confirm
   attached/possession bit semantics and dump the ball record layout live.
2. Enumerate BSS slot writers around DAT_00632a80-aac / PTR slots 0x581acc/ad0 (RUNTIME writes via
   code scan for `mov [abs],imm` with targets in band) to recover per-player task pointers → AI core.
3. Map the object-type record (`[obj+0x20]`) fields +0xe0/+0xec/+0x108/+0x114 via FUN_004f63b0 /
   FUN_00501910 bodies (ball/keeper team slots).
4. Diff against FIFA99 (`/mnt/2nd4TB/FIFA99`) — same lineage likely yields named symbols for the
   GOALIETASK/movdfl family that WC98 stripped.
