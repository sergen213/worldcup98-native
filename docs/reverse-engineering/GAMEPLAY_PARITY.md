# Gameplay Parity Inventory - FIFA: Road to World Cup 98 (Windows) -> native Linux port

Authoritative **gameplay-systems** inventory + parity checklist for the live-match simulation
(`simulation/`, `wc98::sim`). Companion docs: [SIM_PORT_NOTES.md](SIM_PORT_NOTES.md) (port deltas,
table provenance), [GHIDRA_PASS4_SIMULATION.md](GHIDRA_PASS4_SIMULATION.md) /
[GHIDRA_PASS5_IG.md](GHIDRA_PASS5_IG.md) (exe-side in-game architecture dossiers),
[GHIDRA_PASS6_AI.md](GHIDRA_PASS6_AI.md) (AI/camera task layer),
[AI_SEQS_TBI.md](AI_SEQS_TBI.md) (clip-sequence table), [HUD_WC98.md](HUD_WC98.md),
[AUDIO_RUNTIME.md](AUDIO_RUNTIME.md), [COMMENTARY_WC98.md](COMMENTARY_WC98.md).
Format precedent: MENU_PARITY.md.

Legend: **[C]** = CONFIRMED from cited evidence; **[H]** = HYPOTHESIS; **[INFERENCE]** = weak-
evidence line, derived not directly observed in a gate/capture.

## 1. Scope & reference statement

- **Reference binary:** `WorldCup98_Extracted/FIFAWC.EXE` (definitive reference for all gameplay
  claims). Watcom PE32, built 1998-04-13, not packed - docs/STATUS.md "Confirmed - engine &
  platform".
- **Port lineage:** `simulation/` is the M5 module = the fifa99-native `f99_sim` reconstruction
  ported to `wc98_simulation` / `wc98::sim`; same EA Gen3 Watcom engine family, 16.16 fixed-point.
  The `FUN_*`/`DAT_*` addresses in `simulation/src/*.cpp` comments are **FIFA 99 v1.1 addresses**
  valid for the shared architecture; per-file banners (e.g. camera.cpp l.1-4) keep them as family
  documentation, WC98 rederivation is a follow-up slice - SIM_PORT_NOTES.md preamble + §2 cross-map.
- **Table provenance:** generated tables are byte-exact dumps via the fifa99-native
  `tools/sim-tables/dump.py` lane; per SIM_PORT_NOTES.md §1 they were copied verbatim from
  FIFA99.EXE v1.1 DGROUP (table headers say "byte-exact copy of FIFA99.EXE v1.1 ... tools/sim-
  tables/dump.py"), with WC98-specific constants cross-verified against FIFAWC.EXE (section 3).
- **Playable lane:** `app/livematch.cpp` (`--live-match-smoke N` = gate `interactive_match`;
  `wc98-match-demo --demo --out-prefix` = gate `match_demo_frames`). Control bindings:
  config/src/settings.cpp `defaultKeyboard()` (Pass S, Shoot D, Sprint W, Lob A, Evade Q,
  Through E, tactics C/V/X/Z, Space IGM, Esc pause; 1998 PC manual). Pad-word bits in
  input/src/input.cpp (Lob=0x10 l.20, ThroughPass=0x200 l.25, Evade=0x80 l.23).
- **Suite state:** full `ctest -j4` = **111/111 passed** at HEAD v0.4.0 (MENU_PARITY.md §10;
  CHANGELOG release entry). Gameplay gates enumerated in section 4.

## 2. Gameplay systems inventory

Status values: **PV** = ported-verified (implemented + named gate asserts it), **PU** =
ported-unverified (implemented, evidence cited, no dedicated assertion), **P** = partial
(sub-functionality missing/weak), **M** = missing.

| # | System | Implementation (file:symbol) | St | Evidence (exe addr / doc) | Gate(s) | Open-difference notes |
|---|---|---|---|---|---|---|
| 1 | Locomotion: gait select, accel/decel curves, turning | motion.cpp `updatePlayerSpeed` accel/decel via `p.accel/accelRecip/decel/decelRecip` (l.143-194); anim.cpp `Anim_selectLocomotion_4CC028` (l.720), `locoPass_4CBEA0` (l.557) | PV | gait/turn tables exe 0x59f81c/0x59f918/0x59fa20/0x59faa4 (anim_tables.inc headers); movement params 0x4BBBF8 + runtime tables 0x4CFF94 (test_sim_players header) | `test_sim_players`, `test_anim` | WC98-verified via pass10 (SIM_TABLES_WC98.md): kGaitType byte-identical, the five u16 loco/turn/skill tables re-dumped from FIFAWC.EXE and installed; self-test dribble-lag calibration §5.2 |
| 2 | Stamina & attribute influence | player.cpp attr build 0x478B08 + FE expansion 0x5202B0 (WC98 nibble layout per SIM_PORT_NOTES §4); fatigue 0x479028; kSkillA/kSkillB tables exe 0x59faa4/0x59fbac | PV | test_sim_players asserts attr arithmetic + fatigue (header l.2-6) | `test_sim_players`, `test_fcdb` (FCDB attrs) | live in-match speed decay from fatigue not asserted by any gate [INFERENCE] |
| 3 | Momentum / inertia | motion.cpp turn-class speed caps `kSpeedCap[turnClass]` + decel integration (l.457-496); `Player_warpTo` (player_states_idle.cpp) | PU | kTurnClass[3][2] {171,455}/{114,455}/{114,398} (team_ai_ball.cpp l.~50 block); warp grammar SIM_PORT_NOTES §7 (M7 note 2) | none dedicated (`test_sim_scenarios` pass-warp uses it transitively) | no accel/stop-distance measurement gate (§4 gap B) |
| 4 | Dribbling & possession (ball flags bit5/bit6) | ball.hpp `kEntInPossession` bit5/bit6 (l.23-28, T+0x198 field); team_ai_ball.cpp `TeamAI_onBallWon_4DC614` (l.883), `dribbleDir_4DC0D8` (l.678); anim.cpp `dribbleBone` callback (l.671) | PV | GHIDRA_PASS5_IG.md (ball entity/possession grammar); exe 0x4DC614 | `test_sim_scenarios` DribbleOut, `interactive_match` | dribble-touch cadence vs original untouched; quality not asserted |
| 5 | Trapping | input/kick trap-request path (`Player_requestTrap`, kick.cpp requestParams l.~360); M7 note: trap->pass chain verified via forced path | PU | docs/development/M7_REGRESSIONS.md scenario notes | `test_sim_scenarios` Pass (chain only) | no dedicated first-touch control gate (§4 gap D) |
| 6 | Passing / through-balls / crosses / long balls | kick.cpp `Kick_aimTo` (l.607), `aimReceiver` (l.377), `passLead` (l.430), `freeKickDistance` (l.399); team_ai_ball.cpp `passKind_5455C0` (l.78), `evalPasses_54628C` (l.306), `framePasses_4DD2BC`; pad bits Lob 0x10 / ThroughPass 0x200 (input/src/input.cpp l.20-25) | PV | exe 0x4E1B54.. action dispatch (input.cpp l.670); GHIDRA_PASS5_IG.md IG grammar; WC98 power clamp + slot-0 rect gate (`FUN_00502fc0` 48-64, `FUN_0049eb60`) ported pass-12kick | `test_sim_scenarios` Pass + KickPowerClamp, `interactive_match`, `match_demo_frames` | cross/long-ball kind selection asserted only via forced code path (M7 note) |
| 7 | Shooting / volley / header / bicycle | team_ai_ball.cpp `evalShots_546B9C->evalShots_546B2C` (l.484), `TeamAI_replanShot_4DCE94` (l.1041); kick.cpp `Player_executeKick_4E3478` (l.646), deferred 0x... (l.783); input.cpp volley/header-at-landing 0x4E1B54 (l.670) | P | exe kShotThr table 0x58B68C + difficulty offsets (GHIDRA_PASS5_IG §6) | `test_sim_scenarios` ShotGoal, `interactive_match` | shot **power-level ladder** and bicycle/volley strike variants not asserted (§4 gap H); player-side volley landing kick PU |
| 8 | Tackling / sliding / interception / blocking | player_states_action.cpp steal/foul handler (foul emission 0x4D9F2C, foul code 0x4D9B38 per SIM_PORT_NOTES §7); rules.cpp `Rules_checkPlayerFouls_4A6918` (l.~) | P | exe 0x4D9F2C/0x4D9B38; GHIDRA_PASS5_IG.md | `test_sim_scenarios` FoulsCards (foul+card path) | no tackle-win / interception-timing gate (§4 gap J); block geometry (`player_states_action.cpp l.1989` blocker find) PU |
| 9 | Jumping / aerial challenges | input.cpp `airAction` cases 0x4E1BC0/0x4E1BF8 (l.910-918); team_ai_ball.cpp `TeamAI_planAirKick_4DCAC8` (l.978), `TeamAI_tryHeaderPass_4DF2CC` (l.1620) | PU | exe 0x4E1BC0/0x4E1BF8/0x4DCAC8 | none (§4 gap K) | aerial duel resolution vs keeper cross claiming not asserted |
| 10 | Player-player physical interaction / fal balance-fall-recovery | player_states_idle.cpp `knockDown_53547C` (l.559, challenge targets l.774-796); team.cpp 0x4517B4 post-goal side re-balance (l.378) | P | exe 0x53547C/0x4517B4 | `test_sim_scenarios` FoulsCards (foul outcomes only) | no collision/fall-recovery scenario gate; balance meter semantics [INFERENCE] |
| 11 | Goalkeeper: movement / positioning / saves / distribution | keeper_ai.cpp (positioning, rows exe 0x58929C..), keeper_data.cpp table rows, keeper_save.cpp save resolution (`w.emit(EventType::Save)` l.288); player_states_keeper.cpp Save emits (l.229, 616) | PV | GHIDRA_PASS5_IG §5 keeper grammar; exe 0x58929C.. rows | `test_sim_scenarios` ShotGoal asserts result in {Goal,Save,PostHit,BarHit}; `interactive_match` | GK distribution (throw/pickup-then-play) PU - no gate forces a possession-outlet assert |
| 12 | Referee behavior | rules.cpp clock/whistle grammar: `MatchClock_update`, period-end arm (`status \|= 8` + whistle=1, event 9); app/livematch.cpp l.1293-1296 (exe MatchClock grammar [C]); app/game_audio.cpp whistle slot 81 | P | GHIDRA_PASS5_IG §6 (whistle/clock events); CHANGELOG IGHALF half-whistle arm | `test_sim_scenarios` Periods, `fe_ighalf`, `fe_igscsum` | referee **positioning/advancing** is unreconstructed in the exe RE too - no address map exists; advantage-play and card-show timing open [INFERENCE] |
| 13 | Formations & tactical positioning | formation.cpp `Formation_update_4CF678`, ball-zone grid (colPerUnit 197/rowPerUnit 270 = (11<<16)/3648, (21<<16)/5088), `Formation_lerpByMentality` degenerate (G98DF absent) | PV | GEN4TBLS G98PL grid [C] (SIM_PORT_NOTES §6; test_sim_team header); exe 0x4CF230/0x4CF678/0x451860 | `test_sim_team`, `test_gen4tbls` | mentality lerp degenerate = faithful WC98 state, not a defect (§3) |
| 14 | Team AI: attacking / defensive / transitions | ai.cpp `Team_attackAi_4D2644/4D2A44/4D2F24/4D35AC` (l.344-612), `Team_decidePossession` (l.250), `Team_onPossessionChange`, press/offside-trap flags (`Team_startPress` l.195, `Team_armOffsideTrap` l.207); team_ai_ball.cpp ball-side decision layer | PV | exe 0x4D2644.. family; GHIDRA_PASS6_AI.md (task-handler tables open) | `test_sim_scenarios` (Kickoff/Pass/DribbleOut/SetPieces), `interactive_match`, `match_demo_frames` | per-formation attack variants keyed by data only; .SCI script layer not ported (§5 item 1) |
| 15 | Difficulty behavior | team.cpp `Team_setControlLevels_45159C` (l.343, invoked rules.cpp l.447-576); kShotThr difficulty offsets (GHIDRA_PASS5_IG §6); TBLUSR user-tuning members pass-5 | PU | exe 0x45159C; TBLUSR setter [C] GHIDRA_PASS5_IG.md | none (§4 gap O) | no gate compares CPU ctrl-level behaviour across difficulty settings |
| 16 | Match flow / stoppages / restarts (kickoff, goals, offside, fouls/free kicks, penalties, throw-ins, corners, goal kicks, cards, subs, clock/injury time/HT/ET/shootout/golden goal) | rules.cpp full seq grammar: `Match_onEvent` case 7 -> `Rules_checkBallOut` (restartType map), cases 0x12-0x15 offside + `Rules_checkOffside_4A6D8C`, seqSubstitution/`Team_removeInjured_4F9684`, yellow/red card code, injury-time added time in MatchClock_update; team_phase.cpp restart placement/taker (FUN_00452D10 family) | PV | exe 0x4A6D8C/0x4F9684/0x452D10..; M7_REGRESSIONS.md scenario list | `test_sim_scenarios` Kickoff, Offside, SetPieces, FoulsCards, Periods, Shootout, ExtraTimeGoldenGoal; `tournament_progression` (bracket flow) | in-match **penalty kick** (restartType 4 from box foul) implemented (`Rules_checkBallOut`/foul path) but no dedicated PK-kick gate (§4 gap N); clock-driven substitution not asserted (M7 "no substitutions observed" note); added-time value asserts pending (M7 §3 verify items) |
| 17 | Ball physics: mass/momentum, friction/roll drag pair, bounce/restitution, air drag/gravity, spin, deflections, post/crossbar/net | flight.cpp gravity+drag triple `fx(fx(g,t),kDrag)` constants 0x2f5c0/0xeab/0xc391/0x61c8 (header l.4-6); ball.cpp `rollUpdate` pair constants at l.754/771 (SIM_PORT_NOTES §7); net state fields ball.hpp `kEntInNet` 0x01000000, contactMode/netCase/netPos (l.28-68); PostHit/BarHit events | PV | WC98 cross-verified: gravity pair **FIFAWC FUN_004b1f30 l.37**, drag pair **FUN_004b1f30 l.25** (GHIDRA_PASS5_IG.md l.60-61) | `test_sim_core` (fx rounding), `test_sim_scenarios` ShotGoal (PostHit/BarHit in result set) | spin magnitude model + net-surface case table not asserted (§4 gap I); bounce restitution values inherited dump (§3) |
| 18 | Animation system: ANIM.MO clips, AISEQS.TBI sequences, instance/keyframe model, transitions/cancel, anim-ball sync | anim.cpp engine `alloc_45FA10`/`advance_460050`/`keys_45FC30`/`bone_45FEBC`/`playBlend_460674` (l.74-339), `parseAnimSeqTable` AISEQS.TBI (l.461), `seqResolve_4CC984` (l.497), blendPass 0x4CBDD4, `Players_animate_4CC8F4`; kick.cpp contact frames (`Player_onAnimContact` l.791; clip marks[0]/marks[1]); dribbleBone anim-ball callback | PV | engine range 0x45F000..0x461000 + layer 0x4CBD9C..0x4CDD54 (anim.cpp l.1); AISEQS.TBI stride-20 records [C] AI_SEQS_TBI.md; skeleton eval 0x460ACC | `test_anim` (ANIM.MO/TBI parse), `interactive_match` (players animate live) | transition/cancel matrix beyond blend frames not asserted; FIFA-era clip ids vs WC98 ANIM.MO data verified at parse level only |
| 19 | Camera: broadcast grid box, follow modes | presentation/src/camera.cpp per-mode init params (CamRot_init* 0x4C3BAC.., CamPos_init* 0x544FAC.., FOV i16 0x57FAD8) + height/lead tables; match_scene.cpp camera-follow snap (livematch l.1018, smoothing-per-render l.1314) | P | params read from **FIFA99.EXE v1.1** [C] camera.cpp l.5-7; WC98 grid box `FUN_00501910` 12x22 clamp, index x+y*13, complement 298-index, scale DAT_0058bcc4/bcc8 (GHIDRA_PASS6_AI.md l.47-48) **not ported** - no grid-box code in presentation/ | `match_demo_frames`, `interactive_match` render the follow camera; `fe_camera_pages` = IG camera-select board only | WC98 rederivation of cam init addresses is a documented follow-up (camera.cpp l.3); libm vs exe quarter-wave table 0x582EB4 last-ulp drift [C] camera.cpp l.8-10 |
| 20 | Audio events: whistle / commentary / crowd / kick sfx | app/game_audio.cpp `kWhistleSlot=81`, `kKickSlot=23` (l.17-19), CROWD.BNK slot 2/3 ambience loop; trigger seam on KickOff/PeriodEnd/MatchEnd + Shot (livematch l.1296 event 9); commentary SpeechQueue audio/speechqueue.cpp, phrs banks formats/phrs | PV | CHANGELOG.md l.183-186 (GAME00.BNK whistle #81 / kick slot 23; OpenAL loopback asserts audible >=1 s); AUDIO_RUNTIME.md + COMMENTARY_WC98.md gate grammar | `test_gameaudio`, `test_commentary`, `test_speechqueue`, `test_phrs` | commentary **trigger site-map** (45/46 gate sites) open - COMMENTARY_WC98.md "Open items"; scene percent levels [INFERENCE] (winedbg pass 7e; STATUS §Open commentary lane) |

## 3. Physics / animation constants provenance

Three provenance classes, by generated-table header comments and SIM_PORT_NOTES:

**(a' [CLOSED 2026-11-08]) Byte-exact exe dumps - WC98 re-dump COMPLETE.** The FIFA-99 v1.1
tables were located in WC98 FIFAWC.EXE DGROUP by exact byte-signature + structural scans and
xref-confirmed (capstone consumer sites); method, evidence files and per-table verdicts:
`docs/reverse-engineering/SIM_TABLES_WC98.md`, `analysis/pass10/simtables/`.
Byte-identical families (data unchanged; headers now cite WC98 VAs): kGaitType **0x59159c**,
kButtonMask **0x58c7b4**, kCtxTable **0x58d314**, kCtxKeep **0x58d8fc** - WC98's DGROUP mirrors
FIFA-99's layout deltas exactly. Differed families were re-dumped from WC98 bytes and installed:
kActionTable @ 0x58cb14 (162/2048 entries differ), kLoco @ 0x591698 (74/132 - WC98 clip ids match
the ANIM.MO set, max ~534), kTurnL/kTurnR/kSkillA/kSkillB @ 0x5917a0/…/0x59192c (66/66 each),
kActionQueueAllowed @ 0x58c752 (48/98). **kActionQueueable has no WC98 equivalent** (the mirrored
FIFA-99 slot holds dword-structured data); FIFA-99 bytes kept with an explicit absent note.
This closes the silent-drift risk for these arrays: gates `test_input`, `test_anim`,
`test_sim_players`, `test_gen4tbls` re-ran green on WC98 data.
Ball-tables (drag/gravity/kick constants) remain class-(b) verified; remaining per-handler
decompile diff audit is §5.2.

**(b) Cross-verified against WC98 FIFAWC.EXE** (values confirmed identical in the reference binary):

- gravity + air-drag pair: `fx(0x2f5c0, t*fx(v))` **FUN_004b1f30 l.37**; roll drag pair
  `(0x20000/0xeab)` / `(0xc391/0x61c8)` **FUN_004b1f30 l.25** - GHIDRA_PASS5_IG.md l.60-61; ball.cpp
  rollUpdate l.754/771 selects the pair (SIM_PORT_NOTES §7).
- tick dt: frame clock `mod 0x3c` seconds -> **FUN_004b5290** [C] (SIM_PORT_NOTES §7).
- formation grid format + pitch sizes 11x21 cols x rows, G98PL member in GEN4TBLS.VIV: SIM_PORT_NOTES
  §6, `test_gen4tbls` gate.
- FCDB attribute nibble layout (0x3A/0x2F/0x42/0x5B): SIM_PORT_NOTES §4; A[0x42] skill/falance/
  injury flags §5; gates `test_fcdb`, `test_sim_players`.
- 3-term kick constants: flight.cpp header (l.4-6) cites WC98 verification of the triple.

**(c) Measured via winedbg runtime probes** (live FIFAWC under Xvfb, launch-under-debugger lane):
speech/commentary percent table DAT_00632778 scene defaults {80,80,80,80,65,80,1} (idx4=speech) -
analysis/pass7 evidence scripts (outside repo), summarized in the memory notes; docs/STATUS.md §Open
keeps commentary as an open RE lane. Marked [INFERENCE] for parity purposes: not asserted by any
gate in this repo.

**G98AT/G98DF absence consequence:** GEN4TBLS.VIV on WC98 ships **only G98PL**; the FIFA-99-era
G98AT (attack) and G98DF (defence) mentality lerp tables are absent from disk - SIM_PORT_NOTES §6,
data_formations.cpp header. Therefore `Formation_lerpByMentality` is **degenerate by design**
(identity/no-lerp): slot targets move on the G98PL grid alone; `test_sim_team` computes expectations
from loaded G98PL bytes with an independent fx replica (header l.3-6). This is faithful to WC98, not
a port defect; a real mentality-driven positional shift requires community-data substitution and is
not planned [INFERENCE].

## 4. Test coverage matrix (goal §12 validation categories)

| Validation category | Gate(s) | Asserted content | Gap |
|---|---|---|---|
| Walk/sprint gait & movement params | `test_sim_players` | accel/decel/speed from attrs, fx arithmetic | live gait transitions unasserted (A) |
| Accel/stop distance | `self_test_gameplay` movement + sprint windows | carrier path / sprint segment covered under a live moveReq (pad->request->ramp), measured per tick on the real match | stop-distance value ladder open |
| Direction change / turn class | `self_test_gameplay` turning window; kSpeedCap[turnClass] code | heading bend between orthogonal pad segments >= 90 deg | - |
| Dribble control | `test_sim_scenarios` DribbleOut; `interactive_match` | possession kept under pressure, bit5/bit6 flags | touch cadence unasserted |
| Short / long pass | `test_sim_scenarios` Pass (+forced path note M7) | receiver arrival + kick window | - |
| Through-ball / cross | `test_sim_scenarios` Pass chain; `self_test_gameplay` passing step; pad bits input gate `test_input` | kick released, M7 landing placement on the facing side, possession left the taker | receiver capture vs opponent contest unasserted (D) |
| Shot power levels | `test_sim_scenarios` ShotGoal + ShotPower (Gap E) | goal result, Save/Post/Bar branch; kickPower scales solver D = fx(0x16, kickPower), speed-class ladder asserted | - |
| Bounce / roll physics | `test_sim_core` fx rounding; `self_test_gameplay` physics step; ShotGoal transitively; BounceRoll (Gap F) | shot arc apex + minGround measured on the live ball (flight + ground contact evidence); bounce count + roll decay asserted | - |
| Collisions (player-ball, post/crossbar/net) | `test_sim_scenarios` ShotGoal (PostHit/BarHit) + NetRest (Gap G) | event branch; net-case rest position + containment asserted | net-case sub-branch table open |
| Tackle / interception | `test_sim_scenarios` FoulsCards; TackleWin (Gap H) | foul emission; slide-tackle win timing via pickMove row gate, possession flip + card branch asserted | - |
| Header / aerial duel | `test_sim_scenarios` HeaderDuel (Gap K) | Header event emits on air-ball contact, ball lifted by header vector; planAirKick/tryHeaderPass WC98 re-verify open (§5.2) | - |
| GK saves + distribution | `test_sim_scenarios` ShotGoal + KeeperDistribution (Gap L); `self_test_gameplay` goalkeeper step | Save in {Goal,Save,PostHit,BarHit}; keeper displacement off the line during shot flight > 0; keeper throw-back re-enters play (rt/possessor) | - |
| AI positioning | `test_sim_team` (grid arithmetic), scenarios team shape transitively | ball-zone cell math, G98PL slot targets | realism/spacing-vs-original **no gate** (M) |
| Rules / restarts | `test_sim_scenarios` Kickoff, Offside, SetPieces (corner+throw-in), FoulsCards, Periods HT->FT, Shootout, ExtraTimeGoldenGoal; PenaltyKick (Gap N); AddedTime (Gap P) | event chains, restart types, cards, shootout, golden goal; in-box foul -> PK lineup -> strike -> Goal with score move; injury-sec clamp + status&4 board bit | difficulty behaviour closed via scenarioDifficulty (O) |

Gates: E/F/G/H/K/L/N/P are now fixed-seed scenarios in `test_sim_scenarios` (ShotPower,
BounceRoll, NetRest, TackleWin, HeaderDuel, KeeperDistribution, PenaltyKick, AddedTime); B/C/D
placement by the `self_test_gameplay` windows; O by scenarioDifficulty. Still open: A live gait
transitions, D receiver-capture contest, M AI spacing realism (qualitative vs original).

## 5. Open differences & ranked next steps

1. **.SCI layer - RE-RANKED (pass-12sci)**: the "runtime-installed task tables" premise is
   FALSIFIED: `tabA @0x57FBC8` = stub vectors, `tabB @0x57FC40` = 30 static handler pointers
   (goal/win/loss/inc/anc modules per descriptor map `{name,id,idx}`, array VA 0x580A9C);
   scheduler tick/dispatch = `FUN_004a0d80/0x4a0e50` (rec+8 index, flags bit2/bit4). All 30
   handlers decompiled (`analysis/pass12sci/TASK_HANDLERS.md`, `sci4.txt`). Wine captures were
   byte-identical because the tables are never patched. **LANDED (2026-11-08)**: camera dolly +
   preset/height tables installed (`presentation::CameraTask`, `MatchCamera::glideTo`; byte-verified
   rows DAT_00585FEA/586046/5862D2 + height DAT_0057FB82, sine DAT_00585898), `.SCI` payload grammar
   parsed across all 87 COMMON/*.SCI (`formats/sci.cpp`, gate `test_sci`). Remaining from §7: the
   kickoff placement task (delta-3) and quality-tier camera gate (delta-5, pending a presentation
   quality option); handler-family semantic diff = TASK_HANDLERS_WC98.md §7 delta table.
2. ~~Per-handler WC98 vs FIFA-99 decompile diff audit~~ **CLOSED** (pass 11): the code half is
   now audited handler-by-handler - verdict table + ranked real differences in
   HANDLER_DIFF_AUDIT.md (`analysis/pass11/handlers/` evidence). The data-table half was already
   closed (§3(a'), SIM_TABLES_WC98.md). The actionable P1 lanes opened from the audit are now
   **CLOSED** too: WC98 Kick_plan (0x502FC0) constants re-extracted into `kick.cpp` and TeamAI
   decision-family (0x50CF50/0x50E740) + pair-16 dodge ids installed per SIM_PORT_NOTES deltas.
   Player/motion/kick record *layout* shifts (+0x3c kick flags, +0x5c dodge
   fields, three heading slots) are layout-only: the port is a semantic model with named C++
   fields, so no verbatim offset work; anim seqResolve ready bit 0x40-vs-0x80 is likewise
   internal-bit naming (port gates on its own consistently-set producer/consumer bit).
3. ~~Camera WC98 rederivation + broadcast grid box~~ **CLOSED**: the `FUN_00501910` 12x22
   broadcast grid box (scale DAT_0058bcc4/bcc8, index x+y*13, complement 298-index) is ported in
   presentation/src/camera.cpp with camera-param re-verification against FIFAWC.EXE; the live-match
   framing gates cover the behavior. FIFA-99-exe param values remain [C] (shared family).
4. ~~Difficulty behavior verification~~ **CLOSED**: `scenarioDifficulty` (`test_sim_scenarios`)
   asserts the exact ctrl1f4/ctrl1f8/ctrl1fc triples of `Team_setControlLevels_45159C` at aiAssist
   0 vs 2 and that consumers follow (claim range kRange[ctrl1f8], fatigue predicate, keeper ctrl1fc).
5. **Referee positioning / advantage play**: exe-side string map (pass-12 refr): `refCard`
   descriptor VA 0x5712BC raw 0x16F914 = {name 'refCard', id=1, alias ptr 'card'} (isolated
   record; card-event registration), `RefCus` texture-pool tag consumed by tile-allocator
   `FUN_0040e748` (`%s_T_d_P_%d_%d`, pool `DAT_0059e040`); whistle/clock grammar ported [C].
   No referee handler code is statically reachable - task tables install at runtime (.SCI layer,
   item 1), so positioning RE stays blocked on the same blocker; rank 5.
6. **Kickoff first-second pose** (M7 scenario note: 1-2 s of "poses" at kickoff): documented as the
   faithful data path in docs/STATUS.md "Known limitations" (l.195-206) - no work expected; keep for
   cosmetic A/B once capture lane revives.
7. ~~Coverage gaps B/C/D/E/F/G/H/K/L/M/N/P~~ (section 4): all gates landed - `self_test_gameplay`
   (§14 lane) covers B, C and D-placement with measured internal-state assertions; E/F/G/H/K/L/N/P
   are fixed-seed scenarios in `test_sim_scenarios`. Remaining: A gait transitions, D capture
   contest, M AI spacing realism (qualitative).
8. **TeamAI decision-family re-verification** (pass 11 P1): WC98 rewrote the onBallAct/tryHeaderPass
   bodies (0x50CF50/0x50E740, byte ratio .20); `team_ai_ball.cpp`/`ai.cpp` logic re-verify against
   the pair dumps. Kick_plan WC98 constants are integrated (`kick.cpp` 0x502FC0, mapping table in
   SIM_PORT_NOTES.md "Kick family WC98 deltas"); open sub-items listed there.
9. Commentary trigger site-map (45/46 sites) - COMMENTARY_WC98.md Open items; percent-table scene
   levels [INFERENCE] from winedbg pass 7e; rank 9 = gate-able once site map lands.

## Totals registry

systems (20 rows): **ported-verified=11** (locomotion, stamina/attrs, dribble+possession, passing
family, GK saves, formations/tactics, team AI, match flow/restarts, ball physics, animation, audio
events), **ported-unverified=4** (momentum/inertia, trapping, jump/aerial, difficulty),
**partial=5** (shooting family, tackling family, physical interaction/fall-recovery, referee,
camera), **missing=0** - no system is entirely absent; open sub-items are listed per row and in §4.
Gates: named ctest gates cited = **21** (`test_sim_core`, `test_sim_players`, `test_sim_team`,
`test_sim_scenarios`, `match_demo_frames`, `replay_record_play`, `interactive_match`,
`self_test_gameplay`, `fe_igmenu`, `fe_ighalf`, `fe_igscsum`, `fe_igflsum`, `test_hud`,
`test_input`, `test_anim`, `test_gen4tbls`, `test_fcdb`, `test_phrs`, `test_speechqueue`,
`test_commentary`, `test_gameaudio`); suite total 111/111 green at v0.4.0 (HEAD 9e23838), plus the
goal §14 lane `wc98-native --self-test FILE.json` (`self_test_gameplay`) driving a real exhibition
match with scripted pad words: movement/sprint/turning/dribbling/passing/shooting/physics/AI/
goalkeeper/match-state all measured on internal sim state, JSON report written, exit code verdict.
