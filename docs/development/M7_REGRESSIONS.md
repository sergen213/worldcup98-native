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
