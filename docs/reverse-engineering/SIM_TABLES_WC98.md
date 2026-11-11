# Simulation tables re-verified against WC98 FIFAWC.EXE (closes GAMEPLAY_PARITY §3(a))

Date: 2026-11-08. Evidence: `analysis/pass10/simtables/` (scan scripts, raw dumps, FINDINGS.md).
Reference binary: `WorldCup98_Extracted/FIFAWC.EXE`. FIFA99.EXE v1.1 is not on disk; verification = locate the equivalent structures in FIFAWC.EXE by byte-signature scan of the file-backed sections + structural scans + capstone consumer xrefs, then byte-diff against the ported `.inc` arrays.

## Method

1. **Byte-signature exact scan.** Rebuild each table's expected bytes from the `.inc` array (masked for `kButtonMask`: entry = u32 + 8 zero pad bytes, dev stride 0x60) and search every file-backed byte of FIFAWC.EXE; map raw offset -> VA through the Watcom section map (AUTO vaddr 0x401000/raw 0x400/0x157000; DGROUP vaddr 0x55a000/raw 0x158600/0x3d600).
2. **Structural scans** for tables whose payload is indirect: kCtxTable entries `{u32=8, u32 action_ptr, u32 ctx}` (stride 0xC x14 per dev, stride 0xA8 per dev); {0,1}-byte window scan of the queue arrays under the button-mask base.
3. **Consumer xrefs.** Capstone prefilter: every DGROUP-base displacement (`va - 0x55a000`, Watcom flat model data-seg base reg) and absolute VA encoding sampled at element stride across AUTO code, each hit confirmed by a local linear disasm window. Confirmed sites (FINDINGS.md) include `mov ebp, 0x591698` (loco), `mov al, [ebp+0x59159c]` (gait), `mov dx, [edx+0x5918e0]/[edx+0x591964]` (skillA/B + 0x38), `mov dl, [edx+eax+0x58d8fc]` (ctxkeep), `mov reg, 0x58d314` (ctxtable), `mov ah, [edx+0x58c750]` (queue block) — the tables are live-referenced by match input/locomotion code.

Layout note: WC98's DGROUP layout mirrors FIFA99's exactly for these families — button-mask base + 0x360 = action table, + 0xB60 = ctx table, + 0x1148 = ctx keep (FIFA99 deltas), and the anim block anchor kGaitType is contiguous with loco/turn/skill at FIFA99 deltas (+0xFC, +0x204, +0x288, +0x30C, +0x390).

## Results

| Table | WC98 VA | Verdict | Action taken in .inc |
|---|---|---|---|
| kButtonMask | 0x58c7b4 | byte-identical (masked scan) | header cites WC98 VA; data unchanged |
| kCtxTable | 0x58d314 | byte-identical index lists; ptrs = action base + idx*0x80 verified | header cites WC98 VA; data unchanged |
| kActionTable | 0x58cb14 | **differs: 162/2048 entries** | data re-dumped from WC98 bytes; FIFA99 0x58BE48 kept as fallback provenance line |
| kCtxKeep | 0x58d8fc | byte-identical (exact scan) | header cites WC98 VA; data unchanged |
| kActionQueueAllowed | 0x58c752 (= btnmask − 0x62, same FIFA99 delta) | **differs: 48/98 entries** | data re-dumped from WC98 bytes |
| kActionQueueable | — (FIFA99 slot = 0x58c6f0) | **WC98 has no equivalent byte array**: the mirrored slot holds dword-structured data, not a 0/1 array; WC98 ships a single queue block (above). Evidence: FINDINGS.md hexdump + {0,1}-window scan. | FIFA99 dump kept with explicit absent note |
| kGaitType | 0x59159c | byte-identical (exact scan) | header cites WC98 VA; data unchanged |
| kLoco | 0x591698 | **differs: 74/132** (WC98 clip ids; e.g. first row 321,322,323,458,532,234 vs FIFA99 358,442,235,324,357,67) | data re-dumped from WC98 bytes |
| kTurnL / kTurnR | 0x5917a0 / 0x591824 | **differ: 66/66 each** | data re-dumped from WC98 bytes |
| kSkillA / kSkillB | 0x5918a8 / 0x59192c | **differ: 66/66 each** (WC98 uses clip 534 filler where FIFA99 used 452) | data re-dumped from WC98 bytes |

All FIFA99 addresses are retained in the header comments as fallback provenance. The anim clip ids index the WC98 `COMMON/ANIM.MO` clip set (max id ~534, ANIM.MO member range), consistent with `wc98::formats::AnimClip`, not the FIFA 99 MOAI set — this is exactly why values differ and gait-shape tables do not.

## Consequences for GAMEPLAY_PARITY.md §3(a)

- The "WC98 re-capture pending" row for anim_tables (kGaitType/kLoco/kTurnL/kTurnR/kSkillA/kSkillB): **closed** — kGaitType confirmed byte-identical; the five u16 tables re-dumped from WC98 and installed in `simulation/src/anim_tables.inc`.
- The input_tables.inc row ("pending" WC98 re-dump): **closed** — kButtonMask/kCtxTable/kCtxKeep verified byte-identical at 0x58c7b4 / 0x58d314 / 0x58d8fc; kActionTable + kActionQueueAllowed re-dumped from WC98 (0x58cb14, 0x58c752); kActionQueueable documented as absent-equivalent.
- §3(b) "cross-verified" class can now include these tables; the FIFA-99-era provenance flag in §3(a) applies only to ball_tables.inc kick constants (unchanged by this pass).

## WC98 ↔ port motion-record offset map (pass-15 S2, register D-62)

WC98's motion record (M = player+0xB8 agent object) equals the FIFA-99-derived port layout with the
`targetDist..flags` block shifted **+0xC**. Site-by-site proof: `analysis/pass15motion/MOTION_CLAIMS.md` §0.

| WC98 M offset | field | port field (`player.hpp`) |
|---|---|---|
| +0x60 / +0x64 / +0x68 | targetDist, dx, dz (oct-dist) | `targetDist/Dx/Dz` @M+0x54/58/5C |
| +0x9C | turn class index | `turnClass` @M+0x90 |
| +0xA0 | speed (16.16) | `speed` @M+0x94 |
| +0xB0 | desired/cap speed | `speedCap` @M+0xA4 |
| +0xB4 / +0xB8 / +0xBC | sprintMax, runMax, penalty value | `sprintMax` @A8, `runMax` @AC, `mB0` @B0 |
| +0xC0 / +0xC4 / +0xC8 | decel Δv, accelRun, accelSprint | `decel/accel/sprintAccel` @B4/B8/BC |
| +0xCC / +0xD0 / +0xD4 | recipRun, recipSprint, recipDecel (multiply) | `accelRecip/sprintAccelRecip/decelRecip` @C0/C4/C8 |
| +0xD8 | motion flags dword (cascade 0x1c0 bits 0x40/0x80/0x100, sprint 0x100000, penalty 0x6000000, collapse bit 4, class-eval bit 2) | `moveReq` @C(+0xD8) |
| +0xDC | motion status byte (bits set by rootstep l.77/97/101) | `moveStatus` @D0 |
| +0x84 / +0x88 / +0x94 | facing, facingDesired, dir8 | `facing/facingDesired/dir8` @0x78/0x7C/0x88 |

Unshifted blocks (identical to the port's comments): pos/target +0x1C..+0x4C and the anim-facing pair
0x10/+0x14. Tables: speed caps `DAT_0058aee4` stride 4, turn thresholds `DAT_0058aef0` stride 8
(both pinned by `test_motion_tables`; installed rows in `simulation/include/wc98/sim/motion_tables.inc`).

## Follow-ups (ranked)

1. kActionQueueable: WC98's queueability gate lives on the dword block at 0x58c6f0 — decode its per-action dword semantics and replace the kept FIFA99 byte array with a faithful WC98 model (input.cpp:352 currently consumes the FIFA99 table).
2. Diff-shape analysis of kActionTable rows (which contexts/actions WC98 renamed) for the input gesture docs; 162 changed bytes are in `kActionTable.wc98.bin`.
3. Skill-table filler value survey: confirm 534 = "no skill clip" sentinel semantics from the locoPass/selectLocomotion functions at 0x4fc2xx/0x4fd0xx (Ghidra lane).
