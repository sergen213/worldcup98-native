# M7 gameplay regression scenarios — harness spec (2026-11-03)

Target: `tests/unit/test_sim_scenarios.cpp` (registered in tests/CMakeLists.txt; pure simulation, no
tree/env gate). API basis read today: `SimRunner{MatchData,Options,seed}` + `setController` + `tick()`
(simulation/include/wc98/sim/sim.hpp L30-46); event stream `SimEvent{frame,type,team,player,a,b}` +
`EventType` (events.hpp — kickoff/pass/cross/shot/header/goal/own_goal/post/save/catch/tackle/
slide_tackle/foul/cards/throw_in/corner/goal_kick/free_kick/penalty/offside/shootout_*/possession/
substitution); rules hooks `Match_dispatchEvent/Match_onEvent` (rules.cpp L529/L847, exe PTR 0x58104C).

## Method per scenario
Deterministic seed; drive via pad words where the behavior is player-initiated, force via
`Match_dispatchEvent(code)` only where M6 proved the natural trigger unreachable (each forcing marked);
assert on the SimEvent stream (type order + team/player fields) and World state (score, clock, period).

| # | Scenario | Setup seed/initial | Drive | Assert | Notes |
|---|---|---|---|---|---|
| 1 | Kickoff opens play | any; fresh MatchData {0,26} | ticks to t=1..N + `dispatchEvent(0xb)` after 5 s (M6 gap: SetPiece_begin unreachable from pad) | KickOff at start frame, Possession follows, ball z moves | reuse match_demo team ids |
| 2 | Pass chain raises possession events | seed 0x2E7A | pad: pass button pattern to holder pid (from world possessor), alternate targets | ≥2 Pass events, Possession a/b pair matches players | player fields valid 0..21 |
| 3 | Shot → Save/Goal | place attacker near box via initial state injection hook `sim::placeBall` if exists; else seed search (probe seeds for early Shot ≤600 ticks) | pad shoot word at holder | Shot raised; outcome ∈ {Goal,Save,PostHit,BarHit,Clearance} consistent with score delta | Goal ⇒ a = game seconds |
| 4 | Offside fires | choose seed where through-ball is played to last-defender line (probe top-20 seeds for first Offside) | natural sim, no pads (AI vs AI) | Offside event team = attacking side; free-kick restart follows | pure-AI scenario — no forcing |
| 5 | Set pieces: corner / throw-in / goal kick | seed-driven boundaries: ball out over goal line (Corner/GoalKick), sideline (ThrowIn) | natural AI ticks, scan first occurrence per type ≤20k ticks | each type appears with plausible team flip at restart | assert ≥1 Corner AND ≥1 ThrowIn in one long run = engine reachability proof |
| 6 | Foul → card ladder | same long run | natural | Foul raised; YellowCard/RedCard b field = foul class (events.hpp comment) | Injury rare — count only, no gate |
| 7 | HT / FT transitions | short half length via Options | pad idle (AI vs AI) | PeriodStart→PeriodEnd at clock bounds ×2, MatchEnd, periodIndex() 1..4 incl. stoppage? verify against rules.cpp period code points | assert score persists across periods |
| 8 | Shootout reaches after knockout draw | Options knockout + idle pads to full time | natural + shootout dispatch codes (rules.cpp L1003 onShootoutEvent) | ShootoutKick ≥1, ShootoutGoal/Miss consistent | only if knockout mode exists in Options; check first |

## Known gaps this harness must document honestly
- AI holder stalls open play ~t≈130-150 s with silent pad (M6Match seed 0x2E7A): scenario 4/5 seeds must
  be picked from runs that survive the stall (probe script, keep winning seeds as constants).
- Kickoff strike forcing (#1) is a sim-layer slice TODO (SetPiece_begin reachability) — harness pins the
  forced path so the future natural trigger flips one line.
- Substitution: no clock-driven sub observed in demo runs; assert only if long-run seed shows one.

## Acceptance for the implementation slice
All 8 scenario tests run <60 s total (tick budgets capped), deterministic across machines (integer sim,
no FP-order-sensitive asserts — compare event types/teams, not exact frames, unless stable); registration
lines for tests/CMakeLists.txt; failures print seed + frame.

## As built (2026-11-05, `tests/unit/test_sim_scenarios.cpp`, gate green 0.25 s)
- All 8 scenarios shipped; seed 0x2E7A pinned throughout; per-scenario terminal-event tick caps
  (kickoff t<=30, pass ~182, shot 155 -> goal 163 score+1, offside 154 restartType=5, corner/throw-in
  each in its own seeded run at ~250 - post-corner restart cycles livelock in the synthetic world,
  foul 262 -> yellow 300 (red via class 4), PeriodEnd#1 ~7380 + #2 a=15 + finished() periodIndex()==2,
  shootout gameMode=3: ShootoutKick x4 @160/200/240/280).
- Field-semantics corrections to the table above (measured): **cards carry foul class in `a`** (not b);
  **Pass carries kind in `a`, mode in `b`**; Goal `a` = game seconds (confirmed); offside predicate is
  driven via public `Rules_checkOffside` (the engine's own post-kick call site), and `teams[t].side`
  = the goal that team DEFENDS.
- No gtest in repo: scenarios are plain functions under tests/check.hpp CHECK style; "TEST_CASE" above
  maps to that. Kick forcing recipe = Player_executeKick + animId 0x10/contactFrame -1 override +
  Kick_arm + Player_onAnimContact (synthetic worlds have no clip contact frames); kickoff via forced
  `Match_dispatchEvent(0xb)` — every site cites this doc in-code, natural trigger flips one line.

### Extra time + golden goal (2026-11-06, scenarios 9-10)
- Scenarios `extra-time` / `golden-goal` added; seed 0x2E7A pinned; no simulation change needed — the
  native flow is complete: with `opt.tieBreak != 0` (DAT_0065EEEC) a tied FT runs
  `Match_decidePeriodOutcome` -> `requestPeriodEnd(1, 0xe)` and SimRunner starts periods[2] = {type 2,
  half\*20} (period table "normal" mode, `Match_buildPeriodTable` 0x454888); a goal in periodType 2 goes
  `Match_onEvent` case 6 -> `Match_endPeriodSuddenDeath` — WC98 knockout extra time IS golden goal
  (first goal ends the match; periods[3] never starts, MatchEnd endTeam = scoring side). The
  gameMode-6/periodType-4 `goldenGoalTarget` check (DAT_0065EEF0) is the separate single-goal-race mode.
- Measured: `opt.halfLengthMinutes = 1` keeps runtime comparable; extra-time entry PeriodStart(a=2,b=2)
  at sim-tick ~4008 with score 0-0, preceded by PeriodEnd(a=0xe,b=1); golden-goal Goal event (team 1,
  player 13, forced scenario-3 shot recipe in ET open play) at tick ~4176, `finished()` within the seq-9
  full-time sequence (<400 ticks), score 0-1 endTeam=1. Forcing = same dispatch-0xb kickoff + kick recipe.
