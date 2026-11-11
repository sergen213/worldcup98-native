# Gameplay Validation Record — second-pass forensic pass (2026-11-10/11)

Companion to [GAMEPLAY_DISCREPANCIES.md](GAMEPLAY_DISCREPANCIES.md): this file records the
comparison environment, the **baseline measurements** ("before"), and per-fix before/after records.
Objective §9 (synchronized reference testing), §11 (regression record) and §13 (self-test
requirements) live here; register rows own their evidence citations.

## 1. Reference and port sides of the comparison

| Side | Artifact | State used |
|---|---|---|
| Original Windows release | `WorldCup98_Extracted/FIFAWC.EXE` (Watcom PE32, built 1998-04-13), game tree `WorldCup98_Extracted/` | Ghidra analyzed project `ghidra/wc98` (4,720 functions) + raw byte lane |
| Port under test | repo HEAD `d623df3` at pass start; register commit `ec091f3` | release build `build/release`, suite 114 gates |

Byte-lane mapping used for direct exe reads (no disassembler dependency): data-segment VA →
file offset = `VA - 0x401a00`; `.text` VA → file offset = `VA - 0x400000`. Verified by exact-match:
the port's WC98 dump of the queue-allowed table reproduces byte-for-byte at data VA `0x58c752`
(raw `0x18ad52`) — see D-01 evidence below.

## 2. Comparison lanes (what can and cannot be measured, with proof)

| Lane | Tooling | What it measures | Status |
|---|---|---|---|
| Static exe semantics | PyGhidra lane `.venv-ghidra/bin/python -m pyghidra --project-path ghidra --project-name wc98` (scripts `ghidra-scripts/GhidraWc98*.py`) | handler bodies, constants, table rows, branch structure at cited VAs | active |
| Raw byte lane | direct file reads via the mapping above | exact table contents, record strides, presence/absence of structures | active |
| Port telemetry | `wc98-native --self-test FILE.json` (fixed seed 0x2E7A) + fixed-seed scenario gates in `test_sim_scenarios` | per-tick internal sim state: velocity ladders, ball gaps, keeper displacement, phase/possession transitions | active |
| Presentation capture | `wc98-native --fe-smoke/--live-match-smoke N --windowed --out-prefix P`, Xvfb offscreen | presented frames (PNG), HUD/camera geometry | active |
| Original runtime telemetry | winedbg launch-under-debugger under Xvfb 16bpp (`~/.wine-wc98`) | FE-boot-scope probes only: file-open stream, register triples, DGROUP reads at boot | **in-match scope BLOCKED**: wined3d 24 bpp AV path never reaches the match presentation (`WINE_ORACLE_WC98.md`); DirectDraw capture lane dead. No row in this pass claims a live in-match original capture. |

Consequence for §9: synchronized comparisons are **event/state-based, not video-frame based** —
the exe tick cadence is fixed by the frame clock (`FUN_004b5290`: `DAT_0064121c += DAT_0064123c`,
mod `0x3c` at 60 Hz; `kTickDt = 0x20000` = 2 frames, SIM_PORT_NOTES §7), so port gates compare
per-tick state ladders against exe-derived replicas (e.g. root-ladder replica step-sum) rather than
wall-clock or frame numbers. Frame-rate/tick-rate alignment is therefore implicit and documented:
port sim ticks == exe sim ticks; render frames are a separate rate and never used as the comparison
axis.

## 3. Baseline record (before any pass-15 correction)

Suite at HEAD `d623df3`: **114/114 ctest gates passed** (`ctest -j4`, real time 216 s, exit 0).
Self-test lane (`--self-test /tmp/wc98port-out/selftest.json`): **13/13 steps passed**, exit 0.

| Step | Measured (before) |
|---|---|
| movement | carrier path 1141 units, net=(1,1141), phz=9 seq=0 f278=0 stat=0 pt=0 poss=-1 st=4 spd=0 f=18890 |
| sprint | segment covered 1524 units; dribble gap avg 57.84 |
| turning | heading bend up→right = 89 deg |
| dribbling | ball-gap mean 57.84 over 119 dribble ticks |
| passing | possessor pid=-1 recv=15, nearest=(pid 5, gap 555 team 2) at ball=(-1537,-509) |
| shooting | released=true velocity=(0,0) units/tick dist=2522 apex=368 |
| physics | ball arc apex=368 minGround=0 |
| ai | closest opponent 419 → 555 ball-anchored; opponents moved max 2573 (n=9) |
| goalkeeper | keeper pid=0 displacement 417 units, phase 9 |
| match-state | phase 9 kickoffKicked=true elapsed 0→284 s |
| foul-card | penalty award=true restartType=4 phase 10 seq 5 |
| penalty | shot=true goal=true score 0→1 taker-placed=true |
| stop-distance | release=455336 decay=160424 thr=65536 ladderTicks=9 vEnd=294912 stopDistFx=5151332 replicaSfx=4827776 tol=1355500 sprintOkFx=1821344 runMax=465304 |

Baseline byte-lane finding (D-01 root cause, measured 2026-11-10): WC98 data VA `0x58c752` =
the queue-allowed byte array exactly as the port dumped it; the region below
(`0x58c6c0..0x58c74f`) is a dword-record structure (stride 0x20, fields incl. `0x155`, `0x20f`,
`0x20e`, `0x71`, negative masks `0xfffffe00/0xfeab/0xffffff80`), i.e. gesture/action parameter
records, **not** a 0/1 array; the FIFA-99-only `kActionQueueable` predicate at
`simulation/src/input.cpp:352` therefore tests a table WC98 does not use.

Section-delta note (measured): `.text` VA → file offset = `VA - 0x400c00` (proven by the CD-probe
anchor cited in STATUS.md: file `0x38440` == VA `0x439040`); data delta `VA - 0x401a00`.

### 3.1 Fresh byte-lane measurements (2026-11-10, pass-15)

- **Friction/roll pair confirmed by unique sigil:** dword `0xeab` occurs exactly **once** in the
  whole image (raw `0xb13c8` = VA `0x4b1fc8`) and `0xc391` exactly once (VA `0x4b1fd7`) — both inside
  `FUN_004b1f30`; the threshold `0x20000` at VA `0x4b1fbb` and low-speed rate `0x61c8` at
  VA `0x4b2053`. Port `ball.cpp:754/771` uses exactly this pair → D-11 verified independently.
- **No per-tick gravity integration in WC98:** dword immediate `0x2f5c0` (the FIFA-99 "gravity-sized"
  value used illustratively in `test_sim_core.cpp:19`) occurs **zero** times in the image; the physics
  kernel region `FUN_004b2090..0x4b2a40` contains no fx-range immediates except `0x61c8`. Original
  airborne motion is therefore a closed-form solve + keyframe stepping, not per-tick gravity
  integration (objective §5 model question). Port's arc model matches that shape; its solver constants
  remain to be re-verified against the WC98 kick-solver region (`0x505cd0..0x506400`, no `0xeab`
  there — confirmed by scan) in slice S1.
- **Ball companion/kick-branch constants observed** (region `0x4b2a40..0x4b3790`, imm-scan):
  `0xf9db` (= 63963 ≈ 0.977 in 16.16, a damping factor) at VA `0x4b2c4c`, `0x4e200` (320000) at
  `0x4b2c0a/0x4b2c2f`, `0x2710` at `0x4b2e50/0x4b2e75`; plus record-offset immediates
  (`0xb48..0xbb0`). These are handed to slice S1 for semantic mapping against the port's collision /
  kick-application model (D-13/D-14).
- **Runtime table provenance closed at measured VAs (pass-15, `analysis/pass15ball/TABLES.md`):** trig
  sin quarter-wave == exe rows at `DAT_00585898`, atan octant byte table == `DAT_00594CAC`; ball tables
  kRestitution/kBounceRetain/kRollFriction/kLobInit/kMaxKickSpeed/kSqrtSlope/kSqrtBase/kAtan16/kIsqrt ==
  exe rows at `0x581bd8/0x581c00/0x581c28/0x58ad8c/0x59490c/0x591490/0x591514/0x58ec6c/0x593d88`,
  zero mismatches. The FIFA-99-era cites in the `.inc` files (`0x582EB4`, `0x59F104`, …) point at a zeros
  region and a float table — every cite corrected to the measured VA; gates `test_trig_tables`,
  `test_ball_tables`.
- **Closed-form flight constants proven absent:** 13 of 18 `kFlightConst` rows (incl. gravity `-0x2f5c00`)
  occur zero times in the image, and slot `DAT_005913e8` has no ball-band consumer (only odd-address FE
  branch-target xrefs); `kFlightData[626]` absent as a contiguous array. The pool slots with real ball
  consumers are the sqrt pair `0x591490/0x591514` and kick-power builder `FUN_004f1e50@0x4f1ee3` — map in
  `analysis/pass15ball2/TABLES.md`. Register rows D-47..D-54 carry this; D-49 is the open correction.


## 4. Tolerance policy (objective §13)

No arbitrary pass thresholds. Existing tolerances and their derivation, kept as the floor for new
gates:
- stop-distance ladder: ±1 fx unit/tick position rounding + release-boundary floor + 5 % on the
  average-velocity branch (`self_test_gameplay` step 13 detail above).
- team-shape slot targets: grid-cell max step (1776 x-units / 2845 z-units) per sample.
- keeper band: row-derived worst-in-band 204/291.
- camera trig: documented last-ulp drift at the 16-bit angle from libm vs exe quarter-wave table
  `0x582EB4` (`presentation/src/camera.cpp` l.8-10) — gates must not mask this with looser bounds;
  a real fix replaces libm with the extracted table rows (D-38).
New gates introduced by pass-15 corrections state their numeric basis in-gate (fx rounding,
table stride, per-tick integration quantum), never a percentage chosen to make the gate pass.

## 5. Per-row before/after record (appended as fixes land)

| Row | Before (this baseline) | After | Gate | Result |
|---|---|---|---|---|
| D-01 | `input.cpp:352` predicate uses FIFA-99-only `kActionQueueable`; WC98 single-table shape measured above | pending | `test_input` + new queue step | — |
| D-10..D-16 (ball) | self-test physics/shooting rows above; BounceRoll/NetRest/spin gates green on FIFA-99-shaped model | pending per constant map | named ball gates | — |
| D-27 (set pieces) | SetPieces/PenaltyKick/scenarioKickoffPlacement green on FIFA-99 phase body | pending case-map | per-restart placement gates | — |
| D-47/D-48 | `.inc` cites were FIFA-99 addresses; runtime rows unproven at any WC98 VA | trig + 9 ball tables byte-exact at measured VAs (list in §3.1); cites rewritten | `test_trig_tables`, `test_ball_tables` | **Fixed** — parity rows 21/22 PV |
| D-49 | closed-form kick/flight solvers use FIFA-99 constants (13/18 `kFlightConst` rows, all of `kFlightData`) | constants absent from image; consumer map built (`pass15ball2/TABLES.md`) | after fix: `self_test_gameplay` kick/lob + new scenario | **Open** — correction pending re-derivation from `FUN_004f1e50`/sqrt-pair consumers |
| D-50 | dribble cadence claims (`& 7`, `* 0x5000`) asserted in dossier | `FUN_004f3590` = multiply helper; claims REFUTED, WC98 touch gate not located | after locating: `scenarioDribbleCadence` | **Open** — no divergence claim retained; xref lane on `B+0xE0C` writers |
| D-51 | net reflect/damp claimed Critical/High numeric defects | raw dumps match port exactly (`ball.cpp:195-209`, `275-294`) | existing `scenarioNetRest`, `scenarioSpinNetSurface` | **Non-defect** — claims withdrawn, no code change |

## 6. What this pass will never claim

A build, a launch, a completed match, or visual similarity is not fidelity evidence. Parity claims
require: exe-address-cited behavior + a named gate asserting the port reproduces it. Rows where the
original side cannot be measured are marked Blocked with their technical explanation (D-28 referee
positioning: no gameplay referee tick in static lanes; D-35 EULZ: expander installed by a runtime
mechanism absent from shipped binaries). The register keeps those claims at "documented", not
"verified".
