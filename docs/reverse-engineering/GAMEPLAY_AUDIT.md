# GAMEPLAY_AUDIT — Phase-1 subsystem audit (FIFAWC.EXE is the sole authority)

Audit date: 2026-11-20 (goal `local://paste-2.md` Phase 1). Derived from the live registers:
`GAMEPLAY_DISCREPANCIES.md` rows D-01..D-76, `GAMEPLAY_PARITY.md` (parity rows), `SIM_PORT_NOTES.md`,
gate inventory (`ctest --test-dir build/release -N` → 155 tests). Classes:

1 recovered-verified · 2 behaviorally-verified · 3 partial · 4 approximation · 5 incorrect · 6 missing · 7 not investigated.

## 1. Subsystem classification

| # | Subsystem | Class | Register rows | Evidence / gates |
|---|---|---|---|---|
| 1 | Input processing (pad word, sampling, analog decode) | 2 | D-03 fixed, D-57, D-69, D-70, D-71 refuted | `Input_analogDecode` constants byte-pinned; gate `test_input` tap/edge asserts; one-owner per-frame sampling (D-70) |
| 2 | Player controller / action queue | 2 (context selection open) | D-01 fixed (0x58c750 table), D-67, D-68, D-72; D-02 open | gate `test_sim_players` byte pin vs exe bytes + suppression truth-table; context selection unlocated (`analysis/pass17s5/NOTES.md`) |
| 3 | Player movement (target/warp/turn/snap) | 2 (snapTo/solveIntercept open) | D-04, D-05 fixed (caps ladder exact), D-06 open, D-62, D-66 | `test_sim_players` target-under-bits + post-warp facing + per-bit cap equality; step-13 replica |
| 4 | Facing / heading | 3 | D-07 (consumers enumerated, feed graph pending), D-08 | `analysis/pass17s5/DECOMPILE` consumer list |
| 5 | Player attributes | 3 | D-39 stamina fixed, D-40 balance open, D-46 knock-down row exact | FCDB nibble attrs recovered (FIFA98EDIT lanes); `PlayerAttr+0x18` stamina seed; knock-down `(0x14-x)*4` row gated |
| 6 | Ball physics | 2 | D-10..D-15 fixed/closed, D-41..D-49 closed (gravity/air-drag, flight constants) | `test_sim_core`, `test_ball_tables`, BounceRoll/NetRest/scenarioSpin gates |
| 7 | Player-ball interactions (dribble cadence) | 3 | D-12 open (S1), D-50 cadence not located | contest table only; no cadence evidence |
| 8 | Player collisions / tackles | 3 | D-16, D-25 open (S5), D-30 keeper band fixed | scenario `scenarioKeeperContact` determinism on seed 0x2E7A |
| 9 | Animation system | 3 (grammar closed, scripts open) | D-09 closed, D-17/D-18/D-19 open (S4), D-61/D-63/D-64/D-65 closed | `Anim_select` gait/speed-class replica; transition matrix unlocated |
| 10 | Goalkeepers | 3 | D-30 fixed; keeper behavior rows in GAMEPLAY_PARITY | cross-claim band gates |
| 11 | Referees | 7 blocked | D-28 blocked (pass-17ref narrowed; residuals R1-R4) | sampler `run2_slots.py` ready; runtime lane dead (Wine DirectDraw black) |
| 12 | Team AI / decisions | 3 | D-21/D-23 grammar installed, gate pending; D-24/D-25/D-26 open (S5); D-22, D-45 fixed | difficulty triples `ctrl1f4/ctrl1fc` gates at aiAssist 0 vs 2 |
| 13 | Formations / team shape | 2 | D-44 replica dev=0 over ~13.7k samples, D-76 FE default descriptors open | scenarioTeamShape; DB-derived kits/lineups |
| 14 | Team tactics | 2 | FCDB team record b212 + GEN4TBLS.VIV recovered (pass-46 forensics) | `docs/asset-formats/TEAM_AND_FORMATION_REFERENCE.md`, no embedded default bytes (.bss empty pool) |
| 15 | Match rules / state | 3 | D-36 kickoff pose non-defect; set-piece D-27 partial | live restart cutover + FUN_004c0150 gated (pass-17restart) |
| 16 | Set pieces | 3 | D-27 partial [P] residuals (ball-block slot alignment, state-row application) | grammar + restart gates |
| 17 | Camera | 3 | D-38 open, D-54 provenance-closed presets, D-47 trig tables | preset FOV lanes provenance-closed |
| 18 | Replays | 3 | D-34 open — port owns its format; exe `.rep` bodies unverified | `replay_record_play` + `fe_replay57_enter` |
| 19 | HUD / strips | 3 | D-33 open (15-knob calibration rect) | `test_hud`; font table `DAT_0057d2c0` confirmed |
| 20 | Tournament / progression | 2 | D-75 history tables recovered (seed `kWcFinals`) | tournament wheel captures verified from DB |
| 21 | .SCI scripted tasks | 2 (data model) | D-29 resolved data model; residual per-handler row-capture | descriptor array raw 0x17f09c; VM unparsed (blocked) |

## 2. Impact ranking (highest first) — correction priorities

1. **S1 gameplay slice** — kick/possession handlers, carrier coupling, player-ball contact, kick
   contact frames (D-12/D-13/D-16/D-20/D-31/D-50): the live-match interaction core is the largest
   class-3 block and gates audio event sync (goal Phase 7).
2. **Commentary per-event phrase content** (D-32 + AUDIO A7): exe pick chain is class-1 decoded;
   port seam is sequential [H] — wrong selection distribution.
3. **Animation slice S4** (D-17/D-18/D-19 + contact frames D-20): animation/audio sync requires it.
4. **AI slice S5** (D-21..D-26): decisions/marking/RNG divergence change match events themselves.
5. **Presentation announcer scene wiring** — lane ported (pass-77 `app::Announcer`:
   `AUDIO/TEAM` 95 PBK + `AUDIO/PBP/SCHEDULE|STANDING` 756 SMP load/play mirror, gate in
   `test_gameaudio`); no tournament presentation scene feeds slots yet (exe scene→id mapping
   [H], AUDIO_RUNTIME Pass-77).
6. **Set-piece residuals + tactics application** (D-27 [P]).
7. **Camera/HUD/FE data lanes** (D-33/D-38, D-76): geometry correctness.
8. **Blocked lanes** — referee (D-28), replay bodies (D-34), EULZ (D-35): documented, need a
   Win9x-era env or live runtime capture.
