# WC98 task-handler family map (pass 12 SCI-5)

Semantic dossier for the 30 `tabB` task-handler vectors of `FIFAWC.EXE` — the per-scene native
handlers that drive match presentation/match-flow tasks. Companion to
`analysis/pass12sci/TASK_HANDLERS.md` (scheduler model, loader chain; not re-derived here),
`HANDLER_DIFF_AUDIT.md` (pass-11 FIFA-99 code audit) and `GAMEPLAY_PARITY.md` §5.1 rank-1 work.

Evidence base: all 30 handler bodies decompiled (`analysis/pass12sci/sci4.txt`); shared-primitive
decompiles `/tmp/sci5.txt` (scripts `ghidra-scripts/GhidraWc98Sci5.py`, `GhidraWc98Sci6.py`,
`GhidraWc98Sci7.py`); static tables read from image bytes with the data-segment mapping
raw = VA − 0x401A00; payload grammar parsed from on-disk `COMMON/*.SCI` files.
Claim tiers: **[P]** proven by decompiled code/bytes, **[H]** hypothesis, **[C]** convention.

## 1. Scheduler and task-record layout [P]

Per `TASK_HANDLERS.md` §1-2 (recap): frame dispatcher `FUN_004a0e50` iterates the object array
`[&DAT_00641594 + i*4]`, count `DAT_0057fb7c`; for each task record with flag bit 2 clear it writes
rec+4 = scene tick word, runs tick `FUN_004a0d80` (tabA), and when flag bit 4 clear dispatches
`*(code*)(&PTR_FUN_0057fc40[rec+8])` = **tabB handler** (sci6 re-decompile of `0x4a0e50`).

Record field glossary (observed consumers, sci4/sci5 decompiles):

| offset | role | evidence |
|---|---|---|
| +0/+1 flags | bit2 = "frozen" (skip tick+dispatch), bit4 = "tabB suppressed", bit0 (+1) = follow-task active, bits 7/8 of word at +0 → cleared by `FUN_004a0f40` kill path (`*param_3 &= 0xfc7f`) | sci2/sci5 |
| +4 | scene tick word (dispatcher writes each frame); used as rotation source in h26: `rec+0x100 += (rec+4)<<16 / 0x880000` | [P] |
| +8 | tabA/tabB handler index | sci2 |
| +0xc | follow-task progress counter; h19 indirect-dispatch selector `PTR_FUN_00586014[rec+0xc]` | [P] |
| +0x10 | sub-mode (h13 case 3 reads `rec+0x10==3`) | [P] |
| +0x14 | state/active flag; set 1 by trio-tail `FUN_00516550` | [P] |
| +0x60 | preset row index (h12/h14: table row = `(rec+0x60)*10`) | [P] |
| +0xa0..+0xbc | snapshot A (camera start): 10×u32 {pos xyz, rot/zoom fields} copied by `FUN_00517e30/fb0` | sci5 |
| +0xd0..+0xfc | snapshot B (camera live): same layout; commit trio `FUN_00517730/760/790` copies from the scene global block | [P] |
| +0x100 | angle u16 (0x4000 quarter-turn units) | h6/h26/h8 |
| +0x102, +0x106.. | spare/zero writes in commit paths | h26 |
| +0x104 | speed u16 | all camera handlers |
| +0x108/+0x10c/+0x110 | world target x/y/z (game units) | all |
| +0x114 | camera height; default from packed table `DAT_0057FB82[2*idx]` via tabA_pre `FUN_004a0790` (sentinels 0xfffc/0xfffb/0xffffffff = mode-dependent) | [P] |
| +0x118..+0x128 | five zeroed "event" slots (h12/h14/h28 init) | [P] |
| +0x190/+0x198/+0x1a0/+0x1a4/+0x1b0 | timer B block: end, live, end, step=0x10000/end | `FUN_00517b50/cc0` |
| +0x19c/+0x190/+0x1a8/+0x1c0 | timer C block: live, end, stepX, stepZ | `FUN_00517e30/f20/be0/d60` |
| +0x1b4 bit0 / +0x1bc bit0 / (+0x1b8) | timer C / timer B / timer D active | `FUN_00517df0/e20/be0/c50` |
| +0x1c4..+0x1d8 | h6 placement-task private state (timer, side byte, direction, target) | [P] |

Task pool: alloc `FUN_004a09a0` (reserves via `FUN_0049f5a0`, initializes via `FUN_004a0810`, appends
to `[&DAT_00641594]++DAT_0057fb7c`); free `FUN_004a0ab0`. Pool init `FUN_004a0810` [P, sci7/sci8]:
writes rec+0/+2 flag word = `0x34800000` (lower halfword 0, scene tag 0x3480), **rec+8 = rec+0xc =
handler index** (register-passthrough arg [C]), defaults +0x7c=0x4000, +0x80=0x3480, +0x84=0x1900,
+0x8c=0x3c0 (the 960-unit polar speed constant), +0x6c=0/+0x70=0x460/+0x74=0x500, +0x78=0xe28
(player-height default), +0xb4/+0xe4 = tabA_pre camera height (`FUN_004a0790`), clears the stack
slots (+0x38..+0x4c region). Only caller of init is alloc itself; **alloc/free are called only from
the FE scene-loop driver `FUN_00409f78`** (sites 0x409fee alloc → `DAT_0059bd70`, 0x40a10d free on
scene exit; sci7) — one resident task per scene entry. Xrefs to the pool array/count outside the
scheduler are a single unanalyzed site, raw **0x4AA432** (sci6); xrefs of mode selector
`DAT_580DF0` land in the same unanalyzed scene-setup region raw 0x4AA148/1BA/267/2FC/485 (sci8).
=> **producer sites for rec+4/rec+8 = the scene-module install code at raw ~0x4AA000-0x4AA600**
(functions not analyzed by Ghidra's function starts; they write the mode selector and create task
records) [P]. Scene registry lane: wrapper xref `FUN_0047aa48` (scene-handler family, cf.
`FUN_0047aeb0`) calls the driver at 0x47aa8e [P].

## 2. Frame snapshot block `DAT_006447xx` [P]

The handlers never touch sim objects directly; they read a per-frame snapshot filled by the frame
prelude `FUN_004aa640` (called first in `FUN_004a0e50`) and refreshed on demand:

- `_DAT_00644760 = DAT_0063c0e8`, `DAT_006447f8 = DAT_00640178` (kit/side index),
  `DAT_006447fc = DAT_0064015c` (**match phase**; h6 gates on `== 9`) [P]
- ball triple broadcast: `FUN_004aacf0(DAT_006447d4/7d8/7dc)` fills a 60-slot stride-12 array at
  `&DAT_00644668` + copies to `DAT_006447bc/c0/c4`. This is the "loader hook" of
  `TASK_HANDLERS.md` §1 — it is a **world-position broadcast**, not a file loader [P] (sci5).
- object pointers → position triples: `DAT_00644818/81c/820` (from `DAT_006401e8` = ball),
  `DAT_00644828..` (from `DAT_006401fc` = controlled player), `DAT_0064483c..` (`DAT_006401f8`,
  +0xa0/+0xa4/+0xa8 pos, +0x108 extra), `DAT_00644870/874/878` (from `DAT_00646970`),
  `DAT_00644858/85c/860` (from `DAT_0064691c`), `DAT_00644880` (`DAT_00645cb0+0x20→+4`) [P]
- pitch bounds: `DAT_0064477c` (half-z, ±0x140/−0x140 clamps), `DAT_00644780` (half-x; mirror
  comparisons `-DAT_00644780`) [P]
- side flags: `DAT_00644774` (attack-side selector in h12/h14), `DAT_00644814/880` + gate
  `DAT_00644800/87c` (team side flip in h6) [P]
- camera live state: `DAT_00644894..0xac` filled by `FUN_004aa880` per frame for the follow-task:
  two player pointers `DAT_00640230/234`, kit/formation flag from
  `&DAT_0064046c[team*0x584 + idx*0x44] & 0x3f >> 5` (formation table, SIM_TABLES lane) [P]
- h25_cb extra: `DAT_006448ac` flag byte bits 1/2/3 bit7 from player states + `(DAT_00640218&3)<<4`
  + `_DAT_006448ac |= (_DAT_00641010 & 0x7f) >> 6` [P]

Scene/mode selector `DAT_00580df0` drives per-mode branches in h13, h27 and the tabA_pre height
constants {0x1280, 0x440, 0x1980, 0x1100, 0x1540} [P].

## 3. Module → handler binding [P]

**Correction (pass-110sci, §10):** the "descriptor array" read at VA `0x580A9C` (stride 32
`{u32 nameptr, u32 id, 0xffffffff, u32 idx}`) is a **shifted window over the exe script registry**,
not a separate table. The real structure is 22 stride-0x20 records at VA `0x580A80` (raw `0x17F080`)
`{u32 flags, u32 ctxId, u32 count, u32×5 zeros/byte@0xc, u32 poolVa@+0x1c}` + an 86-entry name pool
at VA `0x580520` `{char name[12], u16 zero, u16 class@+0xe}`. The old `id` column values are record
`flags` fields (506 = 0x1fa, 508 = 0x1fc, 970/972 = 0x3ca/0x3cc); `nameptr`/`0xffffffff`/`tag` were
`poolVa`/`count`/`ctxId` reads offset by one field. Because the window starts at record 0's `+0x1c`
slot, **each old row straddles two records**: the printed name is record N's segment name, while the
`id`/`0xffffffff`/`idx` values belong to record **N+1** (rows 0..14 pair names of records 0..14 with
flags/ctxId/count of records 1..15; module records 16..21 = genanc/genans 01..03, ctxId
{0x30, 0x82, 0x2e}). The table keeps the old columns, relabeled by their true field identity:

| module (rec N segment name) | old `id` = rec N+1 `flags` | old `idx` = rec N+1 `count` | handler |
|---|---|---|---|
| gengol00.sci | 506 | 5 | FUN_00517460 (h5) |
| gen2nc00.sci | 508 | 7 | FUN_00517520 (h7) |
| gen2ns00 / genanc00 / clains00 / genwis44 | 458/460/482/190 | 2 | FUN_00517560 (h2) |
| genans00 / clainc00 / geninc00 / genins00 / genwin41 / genloc00 | 466/468/484/126/318/252 | 4 | FUN_00517420 (h4) |
| genwin00 / genwic45 / genlos00 (+genanc/genans 01..03) | 122/124/970 | 1 | FUN_005174e0 (h1) |
| genskw04.sci | 250 | 3 | FUN_005175a0 (h3) |

Name grammar `{gen|cla}{scene}{c|s}{NN}`: scene ∈ goal, win, los, loc, skew(skw), anc(attract),
inc(in-match), 2n(neutral second); `c`/`s` = context; NN = arena/module variant. The old
`{id → idx}` pair misread the record fields (`id` = `flags`, `idx` = `count` segment length), so the
"tabB slot" column is a window artifact, not a binding [P, pass-110sci]; the handler-side evidence
stands — every slot-5/7/2/4/1/3 handler in tabB is one of the 0x516xxx/0x517xxx trio bodies (below),
i.e. all `.SCI` modules share the same native body family; scene identity lives in the payload
records, not in code [P].

tabA tick table VA `0x57FBC8` — **correction to `TASK_HANDLERS.md` §2** ("all entries = stub"):
byte-verified dump shows 12 non-stub tick vectors: slots {12,28}=`FUN_004c9e60`, 13=`FUN_004ca870`,
{14,15,22}=`FUN_004ca100`, 16=`FUN_004cb280`, 19=`FUN_004ca060`, 21=`FUN_004ca610`,
24=`FUN_004caee0`, 25=`FUN_004caf80`, 26=`FUN_00515fc0`, 27=`FUN_004ca2e0`, 29=`FUN_005160b0`;
all other slots = stub `FUN_004a0d30`. The stub prelude `FUN_004a0790` reads the packed default
camera-height word `*(u32*)(&DAT_0057FB82 + idx*2) >> 16` (byte dump: heights {0xcc0, 0xa00, 0x880,
0xf00, 0xac0, 0x1100, 0x1280, 0x1540, 0xe40, 0x1a80, 0x8fc}) with sentinels 0xfffb→rec+0x78,
0xfffc→rec+0x114, 0xffffffff→mode constants; on mismatch vs the player's live height it starts
timer B via `FUN_00517b50` [P].

## 4. Shared primitives (all [P], sci5)

- `FUN_00497740/4977b0` = fixed-point **cos/sin** of u16 angle (quadrant-folded table
  `DAT_00585898`, 16.16). The `dist*trig(x)*0x3c0` idiom in handlers = polar move by 960-unit speed.
- `FUN_004a6fa0` = **atan2(dz,dx) → u16 angle** (quadrant table `DAT_0058ec6c..70`, +0x4000 bias).
- `FUN_004c2a40` = integer sqrt (byte-table bisection `DAT_00593d87/88`).
- `FUN_004a5d50` = **rotate vector by angle** (`FUN_004af940` + `0x6487e>>9` sin/cos scale).
- `FUN_004c9dd0` = scaled projection length (distance term for camera height).
- `FUN_00518060` = perspective/speed curve: piecewise table (`DAT_00585898`, 0x100..0x200 fold) →
  `(iVar<<0xf)/curve`; used as camera scale factor [H].
- `FUN_005166f0` = **pitch-mesh sampler**: ball world (DAT_006447bc/c4) → cell indices
  (x: clamp<3, z: clamp<7; grid stride 0x5a per x-cell × 10 per z-cell at `EBX`, 20-byte cells of
  packed u16 {height?, angle, speed} pairs), bilinear interp, writes rec+0x108/0x10c/0x110 target +
  +0x100 angle (+0xc000 wrap when `rec+8==2`) + +0x104 speed; scale constants
  `DAT_00656e04..e20` (grid origin/cell size/scale) [P].
- `FUN_00516680` = clamp camera target to pitch bounds (`DAT_0064477c/780`) + boundary-event
  callback `PTR_FUN_00580df8`, zeroing the velocity triple on hit [P].
- `FUN_00516cc0` = ball-follow drift: ramps rec+0x1d4/0x1d8 toward ±0xa0/±0xc0 by proximity 0x80
  and integrates camera target (used by h6) [P].
- Camera commit trio `FUN_00517730/760/790` = copy live scene block → rec snapshot B (+0xd0..+0xfc),
  each followed by flag-clear helpers `FUN_00517670/6b0/6f0`; trio-tail `FUN_00516550` also sets
  rec+0x14 = 1; short tail `FUN_004c9d70` = {730,760} only [P].
- Snapshot-start pair: `FUN_00517e30(Δ, B→rec, ...)` writes snapshot A (+0xa0..+0xbc from a second
  block) + starts timer C (steps = |Δ|/end); `FUN_00517f20` advances timer C producing interpolated
  products (the camera dolly); `FUN_00517fb0` snaps both snapshots from the live block [P].
- Timers: B = fade/zoom ramp (`FUN_00517b50` start, step 0x10000/end; advance `FUN_00517cc0`;
  active test `FUN_00517e20`); C = position dolly (start `FUN_00517e30`, advance
  `FUN_00517be0/f20`, remaining `FUN_00517d60`, active test `FUN_00517df0`); D (+0x1b8) advance
  `FUN_00517c50`. Completion callbacks clear the active bits [P].
- `FUN_004a0f40` = **player-follow task step**: big FSM over a player record (stride 0x2c dwords /
  0xb words, fields `>>0x10` packed): computes target/angle/speed into rec+0x82..0x96, starts/
  advances timer C (`FUN_00517e30/f20`) or B trio commit; on completion clears flags word bits
  7/8 (`*= 0xfc7f`) = task kill. `FUN_004aa880` refreshes its live player block (sci5) [P].

## 5. Handler sections (tabB slots h0..h29, unique bodies + tabA tick pairs)

Scene binding per §3; "module trio" = {advance, mesh-sample, commit+state}. All bodies sci4/sci6.

**h0 `FUN_005174a0`, h1 `FUN_005174e0`, h2 `FUN_00517560`, h3 `FUN_005175a0`, h4 `FUN_00517420`,
h5 `FUN_00517460`, h7 `FUN_00517520`, h9 `FUN_005175e0` — module presentation tick (8 slots, 1 body).**
Body = {timerB advance `FUN_00517cc0`; pitch-mesh sample `FUN_005166f0`; trio-tail
`FUN_00516550`}. All `.SCI` modules bind to handler-slot bodies {1,2,3,4,5,7} = this family [P
handler side]; the per-module slot assignments (goal celebration →5, win/loss result flow →1,
in-match/attract/localization →2/4, neutral-second + scoreboard-skew →7/3) came from the artifact
`idx` column (§3 correction) and are [H], not bindings. The body
is scene-agnostic: it advances the camera fade timer, re-samples the ball onto the pitch mesh every
frame and commits the snapshot; the per-scene identity is data (§6) + rec+0x114 height default (§3).

**h6 `FUN_00516f40` — kickoff/placement ball task.** Gate: phase byte `DAT_006447fc == 9` (kickoff
phase [H]); init side byte from `&DAT_00644760 + DAT_006447f8*4`. Path A (`rec+0x1c4 < 0x40001`):
polar ball move — atan target via `FUN_004a6fa0`, sin/cos×960, writes rec+0x1d0 (angle), +0x108/10c
(ball x/y), +0x110 (+0x4000 spin bias), +0x104 = 64000 velocity cap, +0x100 angle; commits trio.
Timeout (`> 0x27fff`): side-flip compare `DAT_00644814/880`, then dolly via timer C
(`FUN_00517f20`) + spin adjust ±`DAT_00656e08` (attack sign) with pitch clamp (`FUN_00516680`) and
follow drift (`FUN_00516cc0`). Semantics: place ball at the kickoff spot, camera-dolly to it,
side-aware [P].

**h8/h17 `FUN_004cb3c0` — ball-centred broadcast camera (in-match framing).** Target = ball triple
(`DAT_006447d4/7dc`) + rotated offset (rec+0x70 radius × sin/cos, y from rec+0x10c scale via
`FUN_00518060`), pitch-clamped to ±(bounds±0x80/0x140) with bisection rescale; writes +0x108/10c/110,
+0x104 = atan angle of ball delta×rec+0x70, +0x100 = live zoom field `rec+0x6c`; commits; if timer B
idle: rec+0x114 = player height field (+0x78), zero rec+300, commit tail. tabA tick pair
`FUN_004cb3a0/...` (slot 8 shared) [P].

**h12 `FUN_004c9e70` — fixed preset camera from mode table.** Row = `(DAT_00649338>0)*10` into
static u16 rows at `DAT_00585FEA/EC/FEE/FF0` (angle, speed, offsets; byte dump: 0x7d...); base
target = ball origin (`DAT_006447d4/7dc`) if side flag `DAT_00644774==0`, else carrier
(`DAT_00644870` + ±0x70 z). Offset via sin/cos² (field-squared scaling [H]); yaw = atan + row+6.
Zeroes rec+300/+0x118..; commits trio. tabA tick `FUN_004c9e60` (slot 28 shares) [P].

**h13 `FUN_004caa70` — mode-switch carrier camera.** Switch `DAT_00580df0`: case 1: dolly
timer-C-active → kill via follow-task; else target = ball-carrier record (block +100: pos
`>>0x10`) rotated by angle band clamp (0x7fa6/0xbfff window, `FUN_004ca7b0` yaw source per mode),
writes +0x108/10c/110 + angle/speed, tail {730,760}. case 2/5: timer-C → kill (+ mode5 extra:
callback `PTR_FUN_00580e00` when ball z gate `DAT_00644854/860`). case 3: direct copy of carrier
pos/angle/speed from block +100 (+0x1c/+0x20). tabA tick `FUN_004ca870` [P].

**h14=h15=h22 `FUN_004ca140` — per-slot preset camera (3 slots, 1 body).** Row = `(rec+0x60)*10`
into static table `DAT_00586046/48/4A/4C` (byte-verified rows: angles {0xf000…}, speeds
{0x7b8,0xc000…}); zeroes rec+300 + event slots; base = ball (`DAT_006447d4/7dc`) if side 0 else
`DAT_00649324` pair (row index <2 forces away-base); offset sin/cos²; yaw = atan + row+8; special:
idx<2 && home → angle/speed via atan pair (+0x4000). Commits trio. tabA tick `FUN_004ca100` [P].

**h16 `FUN_004cb2a0` — camera-transition terminator.** If timer C inactive and flag bit0 clear →
commit tail only; else kill the follow-task (`FUN_004a0f40(rec)`). tabA tick `FUN_004cb280`. One-shot
fade-out task [P].

**h18 `FUN_004cb600` — pitch-edge side camera.** z target = bounds edge: `DAT_0064477c+0x140` /
mirror `-0x140` (keeps rec+0x110 in-band), y = `DAT_00644794 − 0x10`; angle/speed via atan pair;
commits. Out-of-bounds/touchline presentation cam [H] [P]. tabA tick stub.

**h19 `FUN_004ca0e0` — event trampoline.** Single instruction: dispatch
`PTR_FUN_00586014[rec+0xc]`. Table VA `0x586014` byte-verified (sci8): slots 0..11 =
`{0x5174a0, 0x5174e0, 0x517560, 0x5175a0, 0x517420, 0x517460, 0x516f40, 0x517520, 0x4cb3c0,
0x5175e0, 0x517620, 0x517660}` — **identical to tabB slots 0..11 (h0-h11)**; slot ≥12 are preset
data constants (`0x3c0`, `0xf0007fa6` = the h13 angle-clamp band, ...), i.e. valid event indices
are 0..11 and the trampoline re-enters this handler family through a second index register
(rec+0xc). tabA tick `FUN_004ca060`. Scene-event posting lane [P].

**h20 `FUN_004c9fd0` — overhead camera.** z = −`DAT_00644780` (mirror x-edge), y = 0x90 fixed
(height 144 units), angle/speed atan pair; commits. Tactics/overhead view [H] [P]. tabA stub.

**h21 `FUN_004ca690` — ball-follow TV camera.** Source triple pointer: flag bit2 (+1) → carrier
`DAT_00644870`; else bit4 (+0) → player `DAT_00644828`, else ball `DAT_00644818`. Offset =
(−z-edge, −x-edge) bisection-clamped; writes +0x104/+0x105 bytes {0x58,0xeb} = packed angle/speed
constant (0xeb58), +0x100 = live zoom `rec+0x68`; target via sin/cos²; yaw atan; commits + timer B
advance. tabA tick `FUN_004ca610` [P].

**h23 `FUN_004cafe0` — scene-level camera tier.** Tier from quality word `DAT_00644778`: ≤1 →
{Δx 0x71, Δz 0x3d}, 2 → {0x65,0x31}, 3 → {0x59,0x25} (×0x10 units); z source by flags:
`DAT_00644830` / `DAT_00644860` (bit4) / `DAT_00644820` (bit5), clamped to pitch ±(−0x140, +0x760);
+0x10c bytes {0x40,6} = 0x640 angle/speed constant; atan pair. tabA stub [P].

**h24 `FUN_004caf20` — player-follow tick.** Body: `FUN_004a0f40(rec)` only (follow FSM §4).
tabA tick `FUN_004caee0`. The pure "entity follow" task used by ball-cam and presentation [P].

**h25 `FUN_004cafa0` — snapshot refresh + follow.** Body: `FUN_004aa880()` (player live block,
kit/formation flag from formation table) then follow tick. tabA tick `FUN_004caf80` [P].

**h26=h29 `FUN_00516070` — rotating presentation camera.** If timer C active: advance trio
(`be0/c50/cc0`). Else integrate the angle: rec+0x100 += (rec+4)<<16 / 0x880000 (scene tick word →
fixed rotation rate), zero +0x102, commit {790,760,730}. Celebration orbit / post-goal spin [H]
[P]. tabA ticks `FUN_00515fc0` (slot 26) / `FUN_005160b0` (slot 29 = self).

**h27 `FUN_004ca460` — close/bench camera.** Source: `DAT_00644870 ≥ 1` → carrier pair
(+0x120 height, dist 800), else player pair `DAT_00644828` (+0x140, dist 960); target = pos +
sin/cos×dist; angle/speed atan pair; commits. If timer C active → advance trio (`be0/cc0/c50`).
tabA tick `FUN_004ca2e0` [P].

**h28 `FUN_004cb170` — fixed singleton preset camera.** Preset from singletons
`DAT_005862D2/2D4/2D6/2D8` (byte dump: {0x445c, 0xf3e8, 0x08fc, 0x0030}): zeroes rec+300 + event
slots; base = ball (`DAT_006447d4/7dc`); offset sin/cos²; yaw = atan + D2D8. tabA tick `FUN_004c9e60`
(slot 12 pair). Kickoff/preset cam [H] [P].

**tabA stub `FUN_004a0d30` (slots 0-11, 17, 18, 20, 23).** Default-height prelude §3 + on player
height mismatch start timer B (`FUN_00517b50`) else commit tail. This is the per-scene tick default
[PER of sci4].

## 6. `.SCI` payload grammar [P bytes]

Parsed from `WorldCup98_Extracted/COMMON/*.SCI` (87 files; examples GENGOL00.SCI 2308 B,
CLAINC00.SCI 4316 B, GEN2NC00.SCI 2500 B, GENANC00.SCI 4476 B). Layout:

1. **Header 0x00..0x2F.** `u32 count` (GENGOL00 = 0x20, CLAINC00/GENANC00 = 0x11, GEN2NC00 =
   0x61); then three u16 pairs `{0x35, val}` {size?}: GENGOL 0xc9=201, CLAINC 0xe7=231, GEN2NC
   0x123=291, GENANC 0x222=546 — monotone with file size [H: payload word/record count];
   `{2|6, offset}`; `{0x16(22), magic@0x0E}` — full-tree survey across all 87 files: magic ∈
   {0x201×65, 0x202×5, 0x203×15, 0x204×2} (`formats/sci.cpp` enforces). Six DGROUP VA relocation
   pointers at +0x10..+0x24: `0x580054 … 0x5a31e4` (identical in all four sampled files) [P].
2. **Record stream @0x30, stride 16 = 8×i16.** Verified aligned runs; observed record types:
   {0,1,4,5,6,7,9,0xa,0xb,0xc,0xd,0x10,0x13,0x15,0x19} (CLAINC00 @0x40 fields
   `{0x15,0,-0x14,6,0xa0}`; GENGOL00 @0x80 `{0x15,0,0x3c,5}` [P bytes]).
   Triple groups `0xb/0xa/0xd`, pairs `0x4/0x9` interleave in CLAINC00. Fields are i16 offsets/
   speeds/angles — the same units as the handler writes (§5) [P, byte dumps above].
3. **Tail VA table.** Main entries stride-12 = `{u32 DGROUP VA, u16[4]}` (words observed {0,0,count,
   index}); e.g. GENGOL00 @0x7D8 `0x582054, 0x58c054 …` (per-arena bases, stride 0x1000), CLAINC00
   @0xF98; then a trailing packed stride-8 list of `{u32 VA, u32 val}` pairs ending at EOF: GENGOL00/
   CLAINC00 = `0x5a30cc/0x5a3150/0x5a2054` (BSS relocation bases; boundary = first stride-12 view
   with nonzero words[0..1]) [P, all 87 files parse via `formats/sci.cpp`, gate test_sci].

Relation to the runtime record format: data consumer `FUN_004aaf00` (sci2) reads a serialized
buffer at `DAT_0063697c`: packed u16 quads stride 8 (`{+0,+2,+4,+6}` via `>>0x10`) at **+0x170**
(count nibble `[+0x232]&0xf`), a second table at **+0x1c8** (count `([+0x232]>>4)&0xff`), scalars
**+0x220/222/224** and packed u32 pairs **+0x226/228/22a**, count word **+0x232**. The 16-byte .SCI
records decompose exactly as four u32 words = eight i16 fields, i.e. the stream is the serialized
form of these packed quads. Loader-side field splitting **is** statically proven (pass-110sci): the
install consumer `FUN_004a75d0` (§10) rewrites the loaded header's u32 slots @0x10..0x20 into
section-boundary pointers — `@0x10 = buffer+0x24`, `@0x14 = @0x10 + u16@0x0A*0x10` (the tail-anchor
×16 field), `@0x18 = @0x14 + byte@0xE*0xC`, `@0x1C = @0x18 + u16@0x0C*0xC` (`0x16` = 22 tail
entries), `@0x20 = @0x1C + byte@0xF*8` (trailer count) — and patches the stride-12 tail entries'
`+4/+8` slots (entries defined from `@0x14`/`@0x18`) with `iVar6 = iVar2*0x10 + param_2[4]`,
stepping by the entry+0 i16 link [P bytes, sc4]. The boundary pointers **reconcile with the
`SciFile.parse` spans tree-wide** (all 87 files):
`@0x14 = tailStart − 4`. In the loader frame the patched entries are defined from `@0x14`
(`magic & 0xff` of them) and `@0x18` (`u16@0x0C = 22` more): each stride-12 entry = `{i16 link
@+0, record ptr @+4, ptr @+8}`. The `SciFile.parse` view is +4-shifted (`tailStart = @0x14 + 4`),
so the observed views' `+0/+4` slots carry the record pointers and the link i16 lands in the
previous view's `+8` word — on disk `{u32 DGROUP VA, u16[4]}` = `{0,0,count,index}` style. The
trailer = the last `(magic >> 8) + 1` stride-8 `{VA,val}` pairs at `@0x1C + 4 = EOF − 24` — gate
`test_sci` (`tail.size() == (magic&0xff) + 0x16`, `trailer.size() == (magic>>8) + 1`, `size ==
0x24 + anchor*16 + (magic&0xff)*0xC + 0x16*0xC + 4 + trailer*8`, all 87 files). Patch value =
pointer to record `iVar2` in the loaded buffer: `param_2[4]` is set to `buffer + 9` uints =
`buffer + 0x24`, the same basis as `@0x10`; the loop-2 write and the trailer `@0x20` patch share
it. So the on-disk `{u32 DGROUP VA}` tail/trailer slots (`0x582054 … 0x5a1054`, `0x5a30cc/0x5a3150/
0x5a2054`) are **stale pre-relocation pointer slots** the install overwrites, not VA tables [P
sc4]. What stays [H]: the meaning of the per-entry index/link values (`iVar2` seeds from the high
u16 of the `u32@0x06` word (`tagB` {2|6}), steps by the entry+0 i16); the pointers themselves are
into the loaded buffer. The pass-17 "descriptor `id` (gengol00=506) selects
which scene record receives the payload" claim is RETRACTED — the old `id` column was record
`flags` (§3 correction). Producer sites write rec+8 from pool init `FUN_004a0810` [P sci7].

## 7. Parity delta vs port sim

Family → component mapping (HANDLER_DIFF_AUDIT.md style; components per GAMEPLAY_PARITY §5):

| handler family | WC98 behavior | port sim component | verdict |
|---|---|---|---|
| module trio h0-h5,h7,h9 | fade-timer + ball→mesh re-sample + snapshot commit, scene = payload data | `presentation/match_scene.cpp` fixed camera loop (no task layer) | MISSING-lane: no per-scene presentation tasks; match flow is direct-state |
| h6 kickoff placement | side-aware ball dolly + spin at phase 9 | `simulation/rules.cpp` `KickoffPlace_init/step` (h6 model), `app/livematch` pose note consumed via `kickoffPlace.bearing` | PORTED (gate `scenarioKickoffPlacement`; spin slot value [H] = z-grid origin derivation) |
| h8/h13/h21/h27 camera follow | polar offset cam, pitch clamp, mode switch `DAT_00580df0`, carrier/block +100 source | `presentation/camera.cpp` FUN_00501910 grid box port + `CameraTask` dolly (delta-1) + h21 `presentation::followCamera` (source-triple select, inset-edge clamps ±(halfW−0x1e0)/(halfL−0x140), 0xeb58 constant) | MODEL-PORTED: framing math + task data lanes modeled, gate in `test_presentation`; tick uses the grid-box port for live framing |
| h12/h14/h28 presets | static u16 preset rows (DAT_00585Fxx/5860xx/5862Dx) + sin/cos² offsets | `presentation::cameraPresetH12/H14/H28` (delta-2 install) | PORTED: byte-verified rows installed, gate in `test_presentation` |
| h18/h20/h23 edge/tier cams | pitch-edge + quality-tier cameras (DAT_00644778 tiers {0x59,0x65,0x71}) | `presentation::tierCamera` (h23) + `MatchCamera::setQualityTier`, `--quality N`; h18 `sideCamera` (lane thresholds 0x1e0/-0x1df, far/near ±(zEdge+0x140), rec+0x110 mid band, forced-near guard, DAT_00644794-0x10 polar lane); h20 `overheadCamera` (-xEdge polar, 0x90) | MODEL-PORTED: all three modeled with gates in `test_presentation`; live tick uses the grid-box port framing |
| h24/h25 follow FSM `FUN_004a0f40` | player-follow task FSM (snapshot + timer C dolly) | `player_states_*` per-tick state machines (semantic port) | IDENTICAL-semantics via different structure [C] |
| h19 trampoline / event tasks | `PTR_FUN_00586014[rec+0xc]` callback table | commentary/event queue lane (`app/commentary.cpp`) | partial: no task-id registry |
| payload grammar §6 | .SCI record streams feed per-scene params | `decodeOpcodeStream` + `parseExeScriptRegistry` (`test_sci` tree + exe gates) | mirrored (data-level; no runtime consumer in port sim) |

Ranked actionable deltas:

1. **Camera dolly model** — **DONE (2026-11-08)**: `presentation::CameraTask` implements the timer
   B/C snapshot-swap (`FUN_00517b50/e30/f20/cc0`, bit-15 carry rounding) + commit trio snaps;
   `MatchCamera::glideTo/gliding` expose it, gate in `test_presentation`. Unblocks §5.1 rank-1.
2. **Preset table installation** — **DONE (2026-11-08)**: byte-verified rows `DAT_00585FEA`
   (h12), `DAT_00586046` (h14), `DAT_005862D2` (h28) + height table `DAT_0057FB82` (sentinels
   −5/−4/−1 lanes, FUN_004a0790 prelude) installed in camera.cpp; sine `DAT_00585898` embedded.
3. **Kickoff placement task** — **DONE (2026-11-08)**: h6 side-flip + spin ±`DAT_00656e08` dolly
   modeled as `simulation::KickoffPlace_init/step` (`rules.cpp`, replaces the direct-set kick placement;
   per-frame hook in `Match_updateRules`), gate `scenarioKickoffPlacement` (`test_sim_scenarios`);
   "poses" cosmetic note closed (GAMEPLAY_PARITY §5.6) — livematch pose path follows the dolly bearing.
4. **.SCI loader in tooling** (not runtime) — **DONE (2026-11-08)**: grammar refined across all 87
   files (`formats/sci.cpp`, gate `test_sci`: header/magic survey, stride-12 tail + stride-8 trailer).
   Loader-splitting [H] stays open — Win9x-env capture lane (SQF EULZ family blocker).
5. **Quality-tier camera gate** (h23 `DAT_00644778` tiers) — **DONE (2026-11-08)**:
   `presentation::tierCamera` models the h23 geometry (tier deltas {0x71,0x3d}/{0x65,0x31}/
   {0x59,0x25}, x/z pitch-edge clamps, flags bit4/bit5 z-source lanes, polar dz<<4 + 0x640 constant),
   `MatchCamera::setQualityTier` mirrors the quality word and `wc98-native --quality N` wires the
   live option; gate in `test_presentation`. h18/h20 edge/overhead cams stay open (tabA stubs).

## 8. Open items

- Loader field-splitting proof (`FUN_004aaf00` producer of `DAT_0063697c`) — **CLOSED at code level
  (sci9, `analysis/pass12sci/sci9_scene_setup.txt`)**: the producer fills the caller's param blocks
  from the scene block base `DAT_0063697c`: u16 count word @+0x232 (low nibble = triple count A,
  high nibble count B), stride-8 entry arrays at +0x170 (positions broadcast to `{x,y,z}` with
  `>>10` fixed-point scaling) and +0x1c8 (second array → camera vectors), u16 pairs @+0x220..+0x22e,
  flags byte via `FUN_004b1ef0`. The runtime-capture half stays blocked (SQF/EULZ negative result,
  memory rule: do not re-attempt wine EULZ lanes).
- h19 trampoline table contents — CLOSED (sci8 dump, §5 h19).
- `DAT_00580df0` producer sites per scene — **CLOSED (sci12, function-level)**: the unanalyzed
  scene-install functions are now created + decompiled
  (`analysis/pass12sci/sci12_scene_install.txt`): `FUN_004aa148/1ba/267/2fc/485` write
  `DAT_00580df0` (mode selector, arg) + `DAT_00580de8` and gate task kill (`FUN_004a0c90`) on scene
  ids {7, 0xd, 0xf, 0x15, 0x17} via `FUN_004a0be0`; `FUN_004aa2fc` also gates the kit/side table
  `(&DAT_0063c858)[side*0x1fd]` (with `DAT_00640160 == 1`) to zero the live camera triple
  `DAT_00580de0/4`; `FUN_004aa432` sweeps every task record clearing flag-byte bit1 (`& 0xfd`,
  unfreeze-all on scene switch) and sets `DAT_00580df4 = 1`. Scene registry lane unchanged
  (wrapper `FUN_0047aa48` → driver 0x47aa8e).
- tabA tick bodies (`FUN_004c9e60/4ca870/...`) share handler semantics; individual decompiles not
  included here — follow-up if a slot's tick behavior matters for parity.

## 9. Interpreter lane — D-29 closed as data model (pass-17sci) [P]

Full dossier: `analysis/pass17sci/INTERPRETER.md` (probes, descriptor-table decode, grammar table;
pseudocode dump `analysis/pass17sci/DECOMPILE/all_functions.txt`, 5320 functions). Summary:

- **No .SCI VM exists in FIFAWC.EXE.** Exhaustive probes (magic/tag/anchor-reader grep over the full
  pseudocode, switch/case sweep on every observed record type, `.sci` string scan of .text — zero;
  fileopen `_fsize` consumer enumeration = loader + modem lanes only) find no bytecode fetch/dispatch
  structure. The scripted-task "interpreter" is this document's §1 scheduler pair with tabA/tabB
  vector dispatch, consuming `.SCI` payload rows as **data tables** through the preset-row model of
  §3/§5 (h12/h14/h28 row = `(rec+0x60)*10`).
- The pass-12 "descriptor table at VA 0x580A9C" is the exe script registry read as a shifted
  window (corrected pass-110sci, §10): records @0x580A80 stride 0x20 `{flags, ctxId, count,
  poolVa@+0x1c}`; pool @0x580520 with 86 entries `{name[12], class u16@+0xe}` (class histogram =
  {0:24, 1:5, 2:2, 3:7, 4:20, 5:5, 6:4, 7:7, 8:8, 9:2, 10:2}); slots @0x580D3C/40/44 = pool-end
  sentinel / record count / table VA. Registration `FUN_004a7090` pushes record segments;
  selection `FUN_004a7330` buckets by class with tick-gated skip counts; boot loader
  `FUN_00409bc0` loads survivors through `common\%s` (§10).
- Payload grammar shipped as an opcode decoder: `decodeOpcodeStream` in `formats/src/sci.cpp` +
  header; 12 observed types {0x1,0x2,0x4,0x6,0x9,0xa,0xb,0xc,0xd,0x10,0x13,0x15} with tree-wide
  invariants (f1/f5 always zero, f2 pitch delta ±0x384, f3 dur ≤ 0x214) — `test_sci` decodes all
  15,305 records across the 87 tree files and pins head sequences (GENGOL00 {0x10,0x13×4,0x15,0xb,
  0xa}, CLAINC00 {0x10,0x15,0xb,0xa,0x4,0xd,0xb,0xa}).
- Residual: per-handler row-capture parity (runtime capture blocked by the SQF/EULZ negative lane),
  not an interpreter implementation. D-29 flipped to **Resolved (data model)**.

## 10. Exe script registry + selection lane (pass-110sci) [P bytes]

Byte-pinned from retail `FIFAWC.EXE` (DGROUP raw base: VA = file offset + 0x401A00). This is the
boot-time lane that opens `COMMON\*.SCI` files — the pass-12 "descriptor array" (§3 correction) is
this structure read with a shifted window. Evidence scripts/outputs:
`analysis/pass110sci/sc1..sc8.txt` + `GhidraWc98Sc1..Sc8.py`.

Static table (DGROUP image, not .bss):
- Record table VA `0x580A80` (raw `0x17F080`), **22 records**, stride `0x20`:
  `{u32 flags, u32 ctxId, u32 count, u32 zeros incl. byte@0xc=0, u32 poolVa@+0x1c}`.
  Slots: `0x580D3C` = pool-end sentinel (VA of last entry), `0x580D40` = 22, `0x580D44` = table VA.
- Name pool VA `0x580520` (raw `0x17EB20`), **86 entries**, stride 16: `{char name[12], u16 zero,
  u16 class@+0xe}`; class ∈ 0..10, histogram {0:24, 1:5, 2:2, 3:7, 4:20, 5:5, 6:4, 7:7, 8:8, 9:2,
  10:2}; record segments are contiguous (`sum(count)=86`), first entry `gengol00.sci` (class 4),
  last `genans03.sci` (class 0). Tree ships 87 `COMMON/*.SCI` files; `gengol31.sci` is NOT in the
  registry (unregistered file).
- Records 0..14: ctxId `0xffffffff`, counts 31/5/7/2/2/4/4/2/4/4/1/1/2/4/3 (goal/win/loc/anc/ans/
  inc/skew families). Records 16..21: flags `0x3ca/0x3cc`, ctxId {0x30, 0x82, 0x2e}, count 1 each
  (genanc/genans 01..03 module pairs); byte@+0xc is statically zero on all 22.

Runtime lane (DGROUP .bss):
- Registration `FUN_004a7090` (driver `FUN_004a7120` walks the 22-record table via slots): pushes
  `count` consecutive pool entry pointers into the pointer array `DAT_00641EBc` (len `DAT_00641DEe`),
  bumping per-class counters `u16 @0x641E06 + class*2`. If record byte@+0xc != 0 the record goes to
  a module table `DAT_00641E1C + DAT_00641DEA*4` instead — statically never taken (all 22
  bytes zero; CLOSED pass-111sci: zero references to any record +0xc/+0x10 in the image and slot
  `0x580D44` read only at driver site 0x4a7175, so no rewrite lane exists — the branch is
  unreachable).
- Selection builder `FUN_004a7330` (per class 0..10): registered count vs aux skip word
  `u16 @0x641DF0 + class*2` (`0xffff` = unconditional); scan start = `FUN_004c321c() % totalEntries`
  (64-bit tick counter, pass-56 lane), skip consumption gated on `(tick>>32)&3 == 0`; survivors
  copied as 16-byte entries into the runtime registry `DAT_00641FFC` (len `DAT_00641DEC`). Aux word
  source CLOSED (pass-111sci): `FUN_004a7470` fills all eleven slots with `0xffff` at boot
  (`FUN_004a7100` in the `FUN_004AB520` boot chain); no other write site — skip consumption never
  happens (see §10 boot-mode note).
- Boot loader `FUN_00409BC0` (gate: scene selector `DAT_00640FB4 == 10`, skipped in other scenes):
  `FUN_004a7220` returns (registry ptr, len); for each entry
  `sprintf("common\%s", name@+0)` → loader `FUN_00439810`, handle written to slot
  `DAT_0059CC38 + idx*4`, then install `FUN_004a75d0`: gate = loaded `.SCI` header tag
  `u16@0x04 == 0x35` (`SciFile.tagA` nibble check), bucket nibble from `in_EAX` (falls back to
  header `count@0x00` when `0xffffffff`); install into bucket array
  `DAT_006426F0 + (nibble-1)*100*4` — when the low nibble is 0 the entry appends at the fill
  counter `DAT_006426EC + (nibble-1)*0x19` (capped at 23, `-2` = bucket-full sentinel), otherwise
  it overwrites slot 0 directly (`*(DAT_006426F0 + (nibble-1)*100) = file buffer`) — the
  pass-17sci "installs one of slots" lane, now with exact bucket arithmetic. Install also
  rewrites the loaded header's u32 slots @0x10..0x20 into
  section-boundary pointers (`@0x10 = buffer+0x24`, `@0x14 = @0x10 + u16@0x0A*0x10`, `@0x18 =
  @0x14 + byte@0xE*0xC`, `@0x1C = @0x18 + u16@0x0C*0xC` with `0x16` = 22 tail entries, `@0x20 =
  @0x1C + byte@0xF*8`) and patches each stride-12 tail entry (entries defined from `@0x14`/`@0x18`)
  `+4/+8` slots with record pointers `iVar2*0x10 + param_2[4]` (`= @0x10 + index*0x10` basis)
  stepping by the entry+0 i16 link; loop-2 and the trailer `@0x20` write share the basis [P bytes,
  sc4]. See §6 for the boundary identity gate (`test_sci`).
- Gate in port: `formats::parseExeScriptRegistry` (`formats/src/sci.cpp`) + `test_sci` exe registry
  block pin 22/86, table/pool VAs, record 0/1/16/21 fields, class histogram, first/last names, and
  the unregistered `gengol31.sci`.

Boot-mode gate CLOSED (pass-111sci): driver `FUN_004a7120` walks the 22 records (u16 gate word)
and registers each under the mode word built by boot handler `FUN_00409CAC` — venue flag bit
(`FUN_0041a098` = u32 @ `0x571514 + venue*0x20` — the row-`+0x1c` word of a venue table based at
`0x5714f8`; re-based pass-116ctx: `0x419f2c` writes the venue index `ECX` into `DAT_005716b8`
and `0x419f45/0x419f50` then `esi = venue<<5; esi = [0x5714f8+esi]` = the row-`+0x0` pakptr,
copied as a name string at `0x419f5c` — **venue 0 = `pari___x.big`**, name slot
`0x55aa48 + venue*0x10`, 14 rows `{pakptr, 1, 0, lane, 0x23f0, 0x2d50, 0, flag}` for
`{pari, stet, stde, lens, lyon, mars, mont, toul, bord, nant, prac, semi, fina, clas}`. The
earlier pass-111 base (13 rows from `0x571514`, venue 0 = `stet`, pari = pre-venue row) was
off-by-one; the flag column is `{pari:1,stet:1,stde:0,lens:1,lyon:1,mars:0,mont:1,toul:1,
bord:1,nant:1,prac:1,semi:1,fina:0,clas:1}` and the lane column `{pari..bord:1, nant:0, prac:1,
semi:1, fina:0, clas:0}` — same numbers as the old `{stet:1,stde:1,lens:0,…}` listing, venue
labels shifted by one. When the venue global keeps its default `0xffffffff` (-1), `FUN_0041a098`
reads the default row at `0x5714d8` (pakptr sentinel `0xffffffff`, flag 0); `pari` is venue 0,
not a pre-venue row. All `venue<<5` consumers are same-row (replaces pass-115ctx prev-row
mapping): `0x41a020/0x41a0a0` row `+0x1c` flag; `0x41a680 cmp $0,[0x571504+venue<<5]` = row
`+0x0c` lane gate — nonzero and `(DAT_0057d2bc & word [0x60cc06]) == 0` allocates
`DAT_00571710` (`0x409af8/0x40de38`), calls `0x41da7c` (ebx=1, edx=`0x571718`) and registers
handler `0x41be20` via `0x4bf1d0`; `0x41a3c0 cmp $0,[0x571500+venue<<5]` = row `+0x08`
(all-zero in the image) skips the fly-around block; `0x41a0b2/0x41a0c4` read row `+0x10/+0x14`
(`0x23f0/0x2d50`) into caller cells. Flag/lane semantics stay [H]):
`venueFlag == 0 → bit 0x2, == 1 → bit 0x4`, any other value sets no venue bit (strict
`==0/==1` branches in the decompile; the table rows hold only 0/1): scene bucket B (`DAT_00642508`: selector {0,11}→2, 9→1,
else 0) → `0x8/0x10/0x20`; scene bucket C (`DAT_0064250C`: 6→1, {8,9}→2, else 0) → `0x40/0x80/`
`0x100` (bucket C==2 fills the u16 high byte). Predicate: bit 0 unconditional; `flags & 0x200`
records require `ctxId == DAT_006424FC = (uint)DAT_0059B2E6` (one value → at most one ctx pair of
records 16..21 joins); `DAT_006424FC` is a zero-extended u16 boot copy (`66 a1` at `0x409d4a`) of
a u16 field at base `0x59b2e6` (team id, [H] name): `FUN_0040F404` tests `short [0x59b2e6 +
off]` (off stepping by `0x40`) against `0x35d`/`0x35e` = **861/862** (legend team ids 861..876)
and maps hits to `{3,2}` slot writes; `FUN_004110EC` reads the field absolute (`xor esi,esi` →
`mov ax,[0x59b2e6]`) and compares against `861`; `DAT_00642500 ← 0x59b70e` = same field at
base+`0x428`. No absolute store to `0x59b2e6`/`0x59b70e` exists (stores at adjacent
`0x59b27c..0x59b290` in the init lane `0x4029xx` are other fields); producer lane [H]
(pointer-relative fill, unproven, pass-112ctx; pass-116ctx: 2 of the 5 reads are
register-relative — `mov ax,[0x59b2e6+EBP]` at `0x40f6bc` with `EBP = [esp+0x30]`, and
`mov dx,[0x59b2e6+ESI]` at `0x411110` with `ESI` stepping by `0x428` until `0x850` — while
`0x409d4a` (copy), `0x41117e` (compare) and `0x414340` are absolute; `0x414340`
(`xor eax,eax; mov edx,1; mov ax,[0x59b2e6]`) feeds `FUN_00414168` with EAX = the field
(team id, zero-extended), EDX = 1, and stores the result to `DAT_00571180`, the slot-index
byte table read at `0x414118`): else the masked word must keep all triples non-zero (`&0x6`, `&0x38`,
`&0x1c0`). Gate `test_sci` boot-mode block pins per-scene register sets (scene 10 = 6 with the ctx
global unmatched, 7 with a matched ctx value; scene 6 = 5/6; scene 8 = 4/5; scene 9 = 4). Membership
is fully deterministic from (scene, venue flag, ctx value) — the aux slots are all `0xffff` after
`FUN_004a7470`, so nothing is skipped; only the scan START (`FUN_004c321c() % totalEntries`) and
the selection ORDER are tick-dependent [H]. Aux skip
table producer CLOSED (pass-111sci): `FUN_004a7470` fills the 11 class slots `0x641DF0..0x641E04`
with `0xffff` (unconditional), called from `FUN_004a7100` in the boot chain (`FUN_004AB520` site
0x4ab541) — no other write site, so the tick-gated skip consumption of `FUN_004A7330` never
consumes. Byte@+0xc module rewrite CLOSED: zero references to any record `+0xc`/`+0x10` in the
whole image and table-pointer slot `0x580D44` is read only at driver site 0x4a7175 — no store lane
exists, so the module-table branch of `FUN_004a7090` is statically unreachable (earlier "a scene
handler could rewrite byte@+0xc at runtime" claim retracted). Raw imm32 cross-scan corroborates
both negatives: the only 4-byte-LE occurrences of `0x641DF0..0x641E04`, the module table
`0x641E1C` and slot `0x580D44` in the image sit inside `FUN_004a7470`/`FUN_004a7090`/
`FUN_004a7220`/the driver itself, and the record-table base `0x580A80` appears only as data in the
registry block. The subset of the 86 pool entries loaded on a given boot equals the registered
membership (deterministic per scene/venue/ctx); selection ORDER is tick-dependent [H]. No bytecode
VM — §9 conclusion unchanged.
