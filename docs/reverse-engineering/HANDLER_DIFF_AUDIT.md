# FIFA-99 handler -> FIFAWC.EXE diff audit (pass 11)

Handler-by-handler code audit of the `simulation/` citations (FIFA 99 v1.1 exe addresses in the
source comments, kept valid for the shared engine architecture per `SIM_PORT_NOTES.md`) against
WC98 `FIFAWC.EXE`. Method + raw decompile pairs live in `analysis/pass11/handlers/FINDINGS.md`
(per-handler dumps under `pairs/<idx>_<label>/fifa99.txt|wc98.txt`, machine evidence in
`classification.json`). The data-table half of the parity audit is closed separately
(`SIM_TABLES_WC98.md`); this doc covers handler code semantics only.

## 1. Per-handler verdict table

| FIFA-99 | handler | WC98 | verdict | evidence (byte = prologue byte lane, consts = constant-signature lane) |
|---|---|---|---|---|
| `0x4bb0d0` | offsetXZ_W2(root+dist x/z) | `0x4f34b0` | IDENTICAL | byte 0.961 |
| `0x4bb1a8` | Motion_applyTurnAndRootPos | `0x4f3590` | IDENTICAL | byte 0.947 |
| `0x4bdcac` | Player_dribbleVector | `0x500200` | IDENTICAL | byte 0.932 |
| `0x4ce5ec` | Formation_initZoneScales | `0x500e60` | IDENTICAL | byte 0.843 |
| `0x4cea24` | Formation_applyDescriptor | `0x5012a0` | IDENTICAL | byte 1.000 |
| `0x4e0d90` | Input_bindGestureTables | `0x511be0` | IDENTICAL | byte 1.000 |
| `0x4e1a78` | Input_runAction | `0x512800` | IDENTICAL | byte 1.000 |
| `0x466d40` | Input_updateControllers | `0x4a9650` | DATA-DELTA | byte 0.632 |
| `0x476ce0` | Ball_predictPosRaw | `0x4b3590` | DATA-DELTA | byte 0.760 |
| `0x476e6c` | Ball_predictPos | `0x4b3720` | DATA-DELTA | no candidate |
| `0x482ab0` | Ball_collideStep | `0x4c0150` | DATA-DELTA | byte 0.925; consts 26; lanes agree |
| `0x4cbd30` | Motion_faceVector | `0x4f5530` | DATA-DELTA | byte 0.917 |
| `0x4cc984` | Anim_resolveId/seqResolve | `0x4fccd0` | DATA-DELTA | byte 0.821 |
| `0x4d0bb4` | Kick_arm | `0x502f40` | DATA-DELTA | byte 0.867 |
| `0x4e03e4` | Player_dodgesTackle_4E03E4 | `0x5108b0` | DATA-DELTA | byte 0.860; consts 5 |
| `0x452d10` | Team_phaseSetUp | `0x4e11f0` | STRUCTURAL-DIFF | byte 0.197; consts 4 |
| `0x460acc` | Anim_evalSkeleton | `0x4a5c50` | STRUCTURAL-DIFF | byte 0.857 |
| `0x4664f8` | Input_readPadBits | `0x4a9280` | STRUCTURAL-DIFF | byte 0.314 |
| `0x4832ec` | Ball_resolveCollision | `0x4c0997` | STRUCTURAL-DIFF | byte 0.832; consts 32; lanes agree |
| `0x483ef0` | Ball_collideGoalMouth | `0x4c159c` | STRUCTURAL-DIFF | byte 0.763; consts 6 |
| `0x4bbd84` | Motion_facePoint | `0x4fe1e3` | STRUCTURAL-DIFF | byte 0.517; consts 5; lanes agree |
| `0x4bcde0` | Player_lineUp_4BCDE0 | `0x4ff250` | STRUCTURAL-DIFF | byte 0.308; consts 4; lanes agree |
| `0x4cab40` | Motion_rootMotionPre | `0x4f3e60` | STRUCTURAL-DIFF | consts 8 |
| `0x4caed0` | Motion_turnSettled2 | `0x4f4380` | STRUCTURAL-DIFF | consts 8 |
| `0x4cb778` | Motion_setTarget | `0x4f4e70` | STRUCTURAL-DIFF | byte 0.204; consts 4 |
| `0x4cb7fc` | Motion_setTargetAlt | `0x4f4ee0` | STRUCTURAL-DIFF | byte 0.309; consts 4 |
| `0x4cb884` | Motion_warpTo | `0x4f4f80` | STRUCTURAL-DIFF | byte 0.359 |
| `0x4cba60` | Motion_predictDistance/Init | `0x4f4380` | STRUCTURAL-DIFF | byte 0.300; consts 9 |
| `0x4cccfc` | Anim_start | `0x52e394` | STRUCTURAL-DIFF | consts 4 |
| `0x4ccd80` | Anim_moveStep | `0x52e394` | STRUCTURAL-DIFF | consts 9 |
| `0x4cd60c` | Anim_predictEndPos | `0x50cf50` | STRUCTURAL-DIFF | consts 4 |
| `0x4cee60` | Formation_drainQueue/slotUsed | `0x5016e0` | STRUCTURAL-DIFF | byte 0.772; consts 12; lanes agree |
| `0x4d0c68` | Kick_setRequestParams | `0x4fdd10` | STRUCTURAL-DIFF | consts 6 |
| `0x4d0cfc` | Kick_plan | `0x502fc0` | STRUCTURAL-DIFF | consts 9 |
| `0x4d24ec` | Tackle_flag family | `0x40f188` | STRUCTURAL-DIFF | byte 0.500 |
| `0x4dce94` | TeamAI_replanShot_4DCE94 | `0x4f4380` | STRUCTURAL-DIFF | consts 5 |
| `0x4dd324` | TeamAI_onBallAct_4DD324 | `0x50cf50` | STRUCTURAL-DIFF | consts 16 |
| `0x4df2cc` | TeamAI_tryHeaderPass_4DF2CC | `0x50e740` | STRUCTURAL-DIFF | byte 0.205; consts 10 |
| `0x4e3478` | Kick_aimTo | `0x5146c0` | STRUCTURAL-DIFF | consts 16 |
| `0x466bcd` | Input_processController | `-` | MISSING | no candidate |
| `0x476d40` | Ball_kick(flags in-play) / FUN_00476D40 | `-` | MISSING | no candidate |
| `0x4b9560` | Ball_buildLobTable | `-` | MISSING | no candidate |
| `0x4ba1ec` | Ball_stopDead/kickWithVelocity | `-` | MISSING | byte 0.188 |
| `0x4bdac3` | Player_solveIntercept | `-` | MISSING | no candidate |
| `0x4bdb22` | Player_solveIntercept2 | `-` | MISSING | no candidate |
| `0x4be804` | Player_moveToward_4BE804 | `-` | MISSING | no candidate |
| `0x4cb9f8` | Motion_snapTo | `-` | MISSING | no candidate |
| `0x4ccfc0` | Anim_updateScripted | `-` | MISSING | no candidate |
| `0x4cf5f8` | Formation_lerpByMentality | `-` | MISSING | no candidate |
| `0x4d0770` | Kick_clearPending | `-` | MISSING | byte 0.406 |
| `0x4d07b0` | Kick_execute | `-` | MISSING | byte 0.073 |
| `0x4d0bf0` | Kick_planToPoint | `-` | MISSING | no candidate |
| `0x4d1708` | Kick_setAnimParams | `-` | MISSING | byte 0.350 |
| `0x4d19c8` | Tackle_pickMove_4D1A68/tryTackle | `-` | MISSING | no candidate |
| `0x4da190` | Team_unmark(ai.cpp) | `-` | MISSING | byte 0.250 |
| `0x4dc614` | TeamAI_onBallWon_4DC614 | `-` | MISSING | no candidate |
| `0x4dcac8` | TeamAI_planAirKick_4DCAC8 | `-` | MISSING | byte 0.171 |
| `0x4e0ede` | Input_selectContext | `-` | MISSING | no candidate |
| `0x4e262e` | Player_requestKick | `-` | MISSING | no candidate |
| `0x4e3380` | Player_executeKick | `-` | MISSING | no candidate |

## 2. Real differences (ranked; porting work)

P1 = affects live-match behavior now, P2 = affects derived/secondary behavior, P3 = cosmetic/codegen.
Evidence: `pairs/<idx>...` dumps in the pass-11 analysis tree.

- **P1** `0x4cb778` Motion_setTarget/Alt/warpTo family -> `0x4f4e70`: WC98 motion record layout shifted: target x/y/z `+0x34/0x38/0x3c` (y zeroed), &target `+0x4c`, flag byte `+0x5e` bit 1, dx/dz `+0x64/+0x68`, octagonal dist `+0x60`. FIFA-99: x/z `+0x34/+0x3c`, flag `+0x50`, dx/dz `+0x58/+0x5c`, dist `+0x54`. Ported code uses the FIFA-99 offsets; semantics (algorithm) identical, layout not. [pairs/00,01,02]
- **P1** `0x4d0bb4` kick family (arm/plan/aimTo) -> `0x502f40 + 0x502fc0 + 0x5146c0`: Kick-state flag field is WC98 `+0x3c` bit 4 vs FIFA-99 `+0x54`; Kick_plan/K_aimTo fully rewritten in WC98 (located by constant signature, byte lane nCands=0). Charge/anim-id constants need re-extraction from the WC98 bodies. [pairs/21,24,27]
- **P1** `0x4cc984` Anim seqResolve flag bit -> `0x4fccd0`: WC98 anim-record ready flag is **bit 0x40**; FIFA-99 tests **0x80** (stride 0x14 unchanged, table DAT_0058b864). Ported `anim.cpp` gates on the FIFA-99 bit. [pairs/49]
- **P1** `0x4dd324` TeamAI decision family (onBallAct, tryHeaderPass) -> `0x50cf50 / 0x50e740`: WC98 AI bodies rewritten (byte ratio .20); located by constant signature only. Ported `team_ai_ball.cpp`/`ai.cpp` logic must be re-verified against the WC98 decompiles; constants 0x20/0x64/0xb4 shared. [pairs/43,45]
- **P1** `0x4e03e4` Player_dodgesTackle / player-record field shift -> `0x5108b0`: Field offsets shifted (+0x66 -> +0x5c family) with identical shape; all player-record constants in the ported state machines need an offset audit against WC98. [pairs/17]
- **P2** `0x4cbd30` Motion face family heading fields -> `0x4f5530 / 0x4fe1e3`: WC98 stores headings at `+0x7c/+0x80/+0x90` (three slots) where FIFA-99 keeps one; facePoint is inlined into FUN_004fe1a0. Motion_faceVector shape identical modulo layout. [pairs/10,09]
- **P2** `0x466d40` input family -> `0x4a9650 / 0x4a9280`: WC98 controller slot record is `0x15c` bytes (FIFA-99 layout `0x20d0`); pad-bit decode is inlined into the per-slot handler FUN_004a9280, no standalone readPadBits function. Input_runAction itself is byte-identical at 0x512800. [pairs/36..41]
- **P2** `0x4d24ec` tackle flag family -> `0x40f188`: Byte ratio .50 with maxRun 10 = code rewritten around the same set/clear bit protocol. [pairs/18,19]
- **P3** `0x452d10` Team_phaseSetUp / formation init -> `0x4e11f0`: Byte ratio .197 (fully rewritten phase setup); Formation_applyDescriptor is byte-identical at 0x5012a0, initZoneScales epilogue-only. [pairs/55..58]

## 3. MISSING (not located; explicit not-found evidence)

Byte-lane candidate count = 0 and signature-lane score < 3 across the decompiled region corpus
(1562 functions, 0x4a0000-0x530000, `wc98all/`). FIFA-99 bodies are preserved under `pairs/`.
Expected causes: WC98 inlining (handler merged into a caller) or coverage outside the region.

- `0x466bcd` Input_processController: laneA=None laneB=None consts=0
- `0x476d40` Ball_kick(flags in-play) / FUN_00476D40: laneA=None laneB=None consts=0
- `0x4b9560` Ball_buildLobTable: laneA=None laneB=None consts=0
- `0x4ba1ec` Ball_stopDead/kickWithVelocity: laneA=[['0x4cafa7', 2]] laneB=[] consts=0
- `0x4bdac3` Player_solveIntercept: laneA=None laneB=None consts=0
- `0x4bdb22` Player_solveIntercept2: laneA=None laneB=None consts=0
- `0x4be804` Player_moveToward_4BE804: laneA=[] laneB=[] consts=0
- `0x4cb9f8` Motion_snapTo: laneA=[] laneB=[] consts=0
- `0x4ccfc0` Anim_updateScripted: laneA=[] laneB=[] consts=0
- `0x4cf5f8` Formation_lerpByMentality: laneA=[] laneB=[] consts=0
- `0x4d0770` Kick_clearPending: laneA=[['0x502ca0', 2]] laneB=[] consts=0
- `0x4d07b0` Kick_execute: laneA=[] laneB=[['0x502ca8', 4]] consts=0
- `0x4d0bf0` Kick_planToPoint: laneA=[] laneB=[] consts=0
- `0x4d1708` Kick_setAnimParams: laneA=[['0x5038d0', 2]] laneB=[] consts=0
- `0x4d19c8` Tackle_pickMove_4D1A68/tryTackle: laneA=[] laneB=[] consts=0
- `0x4da190` Team_unmark(ai.cpp): laneA=[['0x50a848', 2], ['0x50a83f', 2]] laneB=[] consts=0
- `0x4dc614` TeamAI_onBallWon_4DC614: laneA=[] laneB=[] consts=0
- `0x4dcac8` TeamAI_planAirKick_4DCAC8: laneA=[] laneB=[['0x50cc35', 5]] consts=0
- `0x4e0ede` Input_selectContext: laneA=None laneB=None consts=0
- `0x4e262e` Player_requestKick: laneA=None laneB=None consts=0
- `0x4e3380` Player_executeKick: laneA=[] laneB=[] consts=0

## 4. Method + caveats

- Two independent locating lanes: (a) prologue byte-sequence scan with masking of direct
  absolute operands (Watcom `mov reg,imm32`, call/jmp rel32 to image targets); (b) FIFA-99
  numeric-literal multiset scored over the WC98 decompile corpus (field offsets + constants).
- Verdict rules: IDENTICAL = aligned function-start hit with zero real deltas; DATA-DELTA = same
  shape, differences are immediate/field-offset/data-slot operands (constants recorded in
  `classification.json`); STRUCTURAL-DIFF = control flow / instruction selection differs.
- Ratio < 1.0 at aligned starts with zero deltas means code identical modulo build-local operands.
- Ghidra function-start table used for alignment; region corpus built by `GhidraWc98Hdl2.py`.
- Read-only audit: no simulation/ code, tests, or docs other than this file were changed.
