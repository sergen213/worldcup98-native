# Ghidra pass 6 — match task dispatch, frame loop, object records (2026-11-03)

Method: PyGhidra headless runs (`ghidra-scripts/GhidraWc98Ai*.py`) + host-side raw scans.
Evidence files: `analysis/pass6/FIFAWC.EXE_{rawslots,immregistrars,ai1..ai9}*.txt` (untracked).
Pass-5 prerequisites apply: DGROUP is zero-filled in-file, so BSS function-pointer tables are
invisible statically; this pass maps the dispatch *mechanism* and every table VA.

## Confirmed — fixed-timestep frame loop with presentation slots
`FUN_0049d900` (size 485, zero static callers; sole xref = code at `0x419a1e` inside the game-mode
state machine `FUN_004199d8`, reached via jump table `PTR_LAB_004199c0[DAT_005714c4]`, modes 0..5):
- catch-up loop: while (sim ticks due) { `FUN_00500be0`; `FUN_0050f310` } — and `FUN_0050f310` is the
  only caller of the per-tick match update `FUN_0049daf0` (pass-5) ⇒ full chain
  frame driver → tick → world.
- presentation pair slots `PTR_FUN_00581acc` / `PTR_FUN_00581ad0` called around each step
  (begin/end-of-frame; return of the second gates loop exit — vsync/present result).
- flags: `_DAT_0064019c` bit 0x100 pause-active, bit 0x200 pause-requested, `&0x800` demo-mode skip;
  turbo speed divisor at `DAT_00640fbc` (shifts tick budget by `(4-c)*2`); replay gate
  `DAT_00641274 & 0x20`; frame counter `DAT_006401a4`.

## Confirmed — per-player task dispatch (the AI/action state machine)
- Task driver `FUN_004a0d80` (caller: player loop `FUN_004a0e50`): reads object fields (+0x20/+0x4c
  time-slice bookkeeping, +0x24 owner), on slice exhaustion runs
  `(*table_0x57fbc8)[obj[+0x38/8]]()` — **task handler table at DGROUP 0x57fbc8** indexed by the
  object's current task id; then compact-shifts per-player slot arrays (+0x20/+0x34/+0x4c triples).
- Player iteration `FUN_004a0e50`: player count `DAT_0057fb7c`, pointer array at `DAT_00641594`
  (stride 4, skip flag bit1), per-player calls into the task table **and a second table at
  0x57fc40** indexed by `obj[+8]` (animation/event queue handlers); global pre-tick hook
  `(*PTR_FUN_00580e00)()`; team-huddle reset via `FUN_004aacf0(DAT_006447d4..dc)`.
- Handler family: ~60 task handler functions dumped in `pass6/FIFAWC.EXE_ai8.txt` (cluster
  FUN_004a2ab0–FUN_004a3350 + FUN_004c9e60–FUN_004cb600) and match systems in `_ai9.txt`; jump-table
  resolution notes in `_ai4_jumptables.txt`. Task ids = table indices; contents require the runtime
  table capture (below).

## Confirmed — control class queue + set-piece hook
`FUN_004b3ce0` (motion update wrapper, callers include FE screens + match flows): class byte
`(DAT_00580200)[idx&0x7f]`; CPU (`\x04`) uses sentinel slot 0xff of the per-control queue
`DAT_006419e4`, human (`\b`) its own idx; when `DAT_0057fdd4==0x10` (set-piece mode) installs
callback `_DAT_00581c88 = &LAB_005205e0` + arg, then calls the motion planner `FUN_00518250`
(win\movdfl.c, pass-5). Confirms pass-5's control-class hypothesis (4=CPU, 8=user).

## Confirmed — object-type record fields (task list #4)
Via `FUN_004f63b0` (ball/contact handler; callers FUN_004b1a60/4b1c40 possession fns):
- `[obj+0x20]` → type record; `rec[+0x114]` = self backlink (skip-if-self check), `rec[+0x118]` =
  last-taker slot (set when zero, guarded by +0x34 contact flag); object `[+0x13c]` = uniform kit id
  from table `DAT_00594b3c[playerRecord[+0x3a]*4]` (player-record byte 0x3a indexes a 4-byte-entry
  global palette/kit table — feeds PK texture selection).
- `FUN_00501910` = broadcast-camera box: pitch point → grid cell clamped to 12×22, index
  `x + y*13`, complement `298-index`; scale factors `DAT_0058bcc4/bcc8`, box state
  `DAT_0058bcd4/bcd8` (feed camera-mode selection).

## Table VA inventory (runtime-capture targets)
| VA | role | evidence |
|---|---|---|
| 0x57fbc8 | per-player task handler table (idx = obj task id) | FUN_004a0d80 dispatch |
| 0x57fc40 | secondary queue handler table | FUN_004a0e50 |
| 0x581acc/ad0 | present begin/end slots | FUN_0049d900, pass-5 raw-verified |
| 0x580e00 | per-player-loop global hook | FUN_004a0e50 |
| 0x57fbcc.. region | task tables (60/128-slot families) | immregistrars + `_ai2_xrefs.txt` |
| PTR_LAB_004199c0 | game-mode jump table (modes 0..5) | FUN_004199d8 |

## Known gap — static discovery exhausted for slot *contents*
Host scan of the DGROUP init window (`_rawslots.txt`, 1,505 hits): every in-range u32 is ASCII text
("CLAC", "DIE", "ERA"…), zero function pointers ⇒ tables are built at runtime (Watcom C++ static
init / heap), not stored in the image. Next step (not done this pass): attach under Wine
(`FIFAWC.EXE` boots to FE; sibling projects have a working Wine-oracle harness) and dump
0x57fbc8/0x57fc40/0x581ac0-block + the 0x632a80 HAL slots from live memory after match start.

## Negative results
- `immregistrars` scan (mov [abs],imm32 over full AUTO): 178 sites, none write the task/present
  slot VAs ⇒ rules out direct immediate registration.
- `DAT_00632a9a` (BSSslotB): zero references anywhere — dead slot; `0x57fbf8/0x57fc38`: no static
  refs (runtime-only).
