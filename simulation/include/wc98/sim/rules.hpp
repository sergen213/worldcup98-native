#pragma once
// Match rules, clock, phase state machine, restart sequences, fouls/cards/offside, shoot-out and
// the match statistics block (owner: Rules agent). Exe globals are named in the field comments.
#include <cstdint>
#include "wc98/sim/fixed.hpp"

namespace wc98::sim {

struct World;

// Period table 0x65DFB8, 6 x 16 bytes (Match_buildPeriodTable 0x454888).
struct PeriodEntry {
    std::int32_t type = 0;        // +0 0 first half, 1 second half, 2 extra time, 3 shoot-out, 4 golden goal
    std::int32_t durationSec = 0; // +4 real seconds
    std::int32_t sideFlag = 0;    // +8 -> DAT_0065F2DC, side of team 0 (Team_startPeriod EDX)
    std::int32_t kickoffTeam = 0; // +C team of the kick-off event
};

// Team statistics in the 0x60C block 0x65E1F0 + t*0x60C (zeroed by FUN_004CFF94).
struct TeamStats {
    std::int32_t goals = 0;     // 0x65E1FC (goals credited to a player of this team, not own goals)
    std::int32_t f200 = 0;      // 0x65E200
    std::int32_t shotsWide = 0; // 0x65E204 (Rules_checkBallOut near-miss / Ball_consumeTrajectory)
    std::int32_t corners = 0;   // 0x65E208
    std::int32_t cards = 0;     // 0x65E20C (FUN_004CFFE4)
    std::int32_t f210 = 0;      // 0x65E210
    std::int32_t fouls = 0;     // 0x65E214 (FUN_004D0224)
    std::int32_t offsides = 0;  // 0x65E218 (Stats_countOffside 0x4D0274)
    std::uint32_t flags = 0;    // 0x65E228 bit 0 match abandoned against this team (FUN_004D03D0)
    std::int32_t handicap = 0;  // 0x65E22C (FUN_0045564C)
};

// Penalty shoot-out record 0x65EE14 (0x74 bytes, cleared by event 0 of dispatcher 0x4715F8).
struct ShootoutState {
    std::int32_t result[2][5] = {};   // +0x00 per round slot: 0 none, 1 missed, 2 scored, 3 to kick
    std::int32_t taker[2][5] = {};    // +0x28 exe stores the kicker's (player+0x24)+0x28 (name) -> player id
    std::int32_t goals[2] = {};       // +0x50 0x65EE64 / 0x65EE68
    std::int32_t remaining[2] = {};   // +0x58 0x65EE6C / 0x65EE70
    std::int32_t kicker[2] = {};      // +0x60 index into kickOrder (player index in team)
    std::int32_t round = 0;           // +0x68 0x65EE7C
    std::int32_t first = 0;           // +0x6C 0x65EE80 team that kicked first
    std::uint32_t flags = 0;          // +0x70 0x65EE84: 1 team to kick, 2 can win now, 4 must score, 0x20 decided
};

// h6 kickoff placement task (FUN_00516f40) private state, modeled per-frame in the port sim
// (TASK_HANDLERS_WC98 §4 glossary "rec+0x1c4..+0x1d8 h6 placement-task private state", §7 delta-3).
struct KickoffPlaceState {
    std::int32_t timer = 0;        // rec+0x1c4: placement timer (accumulates the scene tick word while ball y < 0x80)
    std::int32_t sideByte = -1;    // rec+0x1c8: init sentinel -1, then attack-side bit (& 1); spin sign source
    std::int32_t dollyActive = 0;  // rec+0x1ca: placement dolly armed (timer C started at FUN_00517e30)
    std::int32_t dir = 1;          // rec+0x1cc: side-aware dolly direction, ±1
    Angle bearing = 0;             // rec+0x1d0: live dolly bearing (port angle units, -0x200..0x1ff)
};

struct MatchState {
    // --- phase / state machine (0x65F228..) ---
    std::uint32_t status = 0;   // DAT_0065F228: 1 dead ball, 2 kick-off pending, 4 added time shown, 8 period
                                //   over, 0x10 match over, 0x20 sim halted (end-of-period sequence done)
    std::int32_t phase = 0;     // DAT_0065F22C (Match_setPhase 0x455418)
    std::int32_t restartType = 0; // DAT_0065F230 0 kick-off 1 throw-in 2 corner 3 goal kick 4 penalty 5 free
                                  //   kick 6 shoot-out kick 8 period end 0xE presentation intro
    std::int32_t nextPhase = 0;   // DAT_0065F234 (Match_requestPeriodEnd EDX)
    std::int32_t prevPhase = 0;   // DAT_0065F238
    std::int32_t f244 = 0;        // DAT_0065F244 (cleared by Match_resetPhase)
    std::int32_t restartSide = 0; // DAT_0065F248 side of the team taking the restart (Match_setPhase)
    std::int32_t phaseTeam = 0;   // DAT_0065F24C team of the current phase / restart (Match_setPhase 0x45543E, Match_onEvent)
    std::int32_t throwAnim = 0, f258 = 0; // DAT_0065F254 throw-in anim pick 0..3 (state 0x185) / 0x65F258
                                          //   (both cleared by Match_startPeriod)
    std::int32_t replayWanted[2] = {}; // DAT_0065F25C[team] (set by Rules_checkBallOut on goals)
    std::int32_t f26c = 0;        // DAT_0065F26C (zeroed by Ball_takePossession / release)
    std::int32_t endWhistle = 0;  // DAT_0065F270 1 half-time whistle (seq 1), 2 final whistle (seq 2)
    std::int32_t endTeam = 0;     // DAT_0065F274 winner / phase team of the period end
    std::uint32_t flags278 = 0;   // DAT_0065F278
    std::int32_t f27c = 0;        // DAT_0065F27C (= 0x8E after Match_init)
    std::int32_t f280 = 0;        // DAT_0065F280
    std::int32_t ownGoalPlayer = -1; // DAT_0065F284 defender credited with an own goal
    std::int32_t scorer = -1;     // DAT_0065F288 goal scorer (or own-goal player)
    std::int32_t subPlayer = -1;  // DAT_0065F290 player leaving for a substitute (0x471FD4)
    std::int32_t f294 = -1;       // DAT_0065F294 player shown by the foul / offside sequences
    std::int32_t igtRequester = -1; // DAT_0065F298
    std::int32_t offBallControl = -1; // DAT_0065F29C player id (IGT off-ball runner), cleared each frame
    std::int32_t player2a0 = -1;  // DAT_0065F2A0 player id (one-touch / action 6)
    std::int32_t shootoutKicker = -1; // DAT_0065F2A8 current shoot-out kicker
    std::int32_t restartTaker = -1;   // DAT_0065F2AC
    std::int32_t penaltyTaker = -1;   // DAT_0065F2B0
    std::int32_t setPieceMode = 0;    // DAT_0065F2B8 (Match_resetPhase sets 1)
    std::int32_t setPieceAimMode = 0; // DAT_0065F2BC
    std::int32_t setPieceKind = 0;    // DAT_0065F2C0
    Vec3 setPieceAim;                 // DAT_0065F2C4/C8/CC (training also uses it as the random ball spot)
    std::int32_t setPieceAimCtrl = 0; // DAT_0065F2D0 controller index + 1 (exe: pointer)
    std::int32_t setPieceTaker = -1;  // DAT_0065F2D4
    std::int32_t setPiecePartner = -1; // DAT_0065F2D8
    std::int32_t periodSideFlag = 0;  // DAT_0065F2DC
    std::int32_t setPieceArmed = 0;   // DAT_0065F2E4
    std::uint8_t f2e8[8] = {};        // DAT_0065F2E8..0x65F2ED (zeroed by Match_init)

    // --- clock (0x65F1C0..0x65F224) ---
    std::int32_t durationSec = 0;    // DAT_0065F1C0 period length in real seconds
    std::int32_t totalSec = 0;       // DAT_0065F1C4 seconds since kick-off of the match
    std::int32_t elapsedSec = 0;     // DAT_0065F1C8 seconds in this period
    std::int32_t injurySec = 0;      // DAT_0065F1CC added time (<= duration/4)
    std::int32_t clockFrames = 0;    // DAT_0065F1D0 frames of the current second
    std::int32_t stoppageFrames = 0; // DAT_0065F1E8 dead-ball frames counted as stoppage
    std::int32_t deadFrames = 0;     // DAT_0065F1EC dead-ball frames of the current stoppage (first 600 free)
    std::int32_t f1fc = 0, f200 = 0; // DAT_0065F1FC / 0x65F200
    std::int32_t subCheckSec = 0;    // DAT_0065F210 elapsed second of the last AI substitution check (0x453E58)
    std::int32_t possessionTimer = 0; // DAT_0065F214 fun-mode 0x10 possession timer (seconds)
    std::int32_t blockUntil = 0;     // DAT_0065F218 total-frame stamp: body blocks (0x543560) allowed while totalFrames <= it (unsigned)
    std::int32_t nextPeriod = 0;     // DAT_005AAB98 (Match_run 0x40A664: period index passed to Match_startPeriod, ++ after)
    std::int32_t keeperHoldUntil = 0; // DAT_0065F21C total-frame stamp: no blocks before it (Keeper_holdOff 0x543370, unsigned)
    std::int32_t periodType = 0;     // DAT_0065F220
    std::int32_t periodIndex = 0;    // DAT_0065F224
    std::int32_t displayOverflow = 0; // DAT_0057EC24 game seconds past the regular end (MatchClock_toGameSeconds)
    PeriodEntry periods[6];          // 0x65DFB8

    // --- restart sequences (0x6675B4..0x6675F4) ---
    std::int32_t seqRead = 0;        // DAT_006675B4
    std::int32_t seqWrite = 1;       // DAT_006675B8
    std::int32_t seqQueue[5] = {};   // 0x6675BC
    std::int32_t rulesMode = 0;      // DAT_006675D0 0 normal clock, 1 MatchClock_updateAlt, 2 shoot-out, 3 presentation
    std::int32_t dispatcher = 0;     // PTR 0x58104C: 0 Match_onEvent, 1 training 0x473B20, 2 shoot-out 0x4715F8,
                                     //   3 presentation 0x4741A0
    std::int32_t seqId = 0;          // DAT_006675D8 current sequence (handler table 0x581008)
    std::int32_t seqStep = 0;        // DAT_006675DC
    std::int32_t seqE0 = 0, seqE4 = 1, seqE8 = 1; // DAT_006675E0 / E4 / E8
    std::int32_t seqTrigger = 0;     // DAT_006675EC trigger id waited for by seq 5
    std::int32_t seqFrames = 0;      // DAT_006675F0 frames since the step started
    std::uint32_t seqFlags = 0;      // byte DAT_006675F4 bit 0 substitution pending
    std::int32_t cutScene = 0;       // DAT_0066341C (CutScene_playForEvent sets 1)
    std::int32_t scriptState = 1;    // DAT_006633D8 cut-scene engine state: 1 idle, 4 starting
    KickoffPlaceState kickoffPlace; // h6 placement task (rec+0x1c4.. state mirror; §7 delta-3)

    // --- foul state 0x65E018..0x65E04C (cleared by FUN_0046F39C) ---
    std::int32_t foulClass = 0;      // DAT_0065E018 0 none, 1 yellow-able, 2 red-able, 3 offside, 4 red,
                                     //   5 yellow, 6 no card
    std::int32_t card = 0;           // DAT_0065E01C 0 none, 1 yellow, 2 red
    Vec3 foulSpot;                   // DAT_0065E024
    std::int32_t foulInBox = 0;      // DAT_0065E030 -> penalty
    std::int32_t offender = -1;      // DAT_0065E034
    std::int32_t victim = -1;        // DAT_0065E038
    std::int32_t injuredPlayer = -1; // DAT_0065E03C
    std::int32_t injury = 0;         // DAT_0065E040 1..3
    std::int32_t cardRange = 0;      // DAT_0065E044 card chance denominator (100 / 50)
    std::int32_t offenderCtrl = 0;   // DAT_0065E048 (player+0x34)
    std::int32_t victimCtrl = 0;     // DAT_0065E04C

    // --- statistics block 0x65E050.. ---
    std::int32_t score[2] = {};      // DAT_0065E050
    std::int32_t goalCount = 0;      // DAT_0065E058
    std::int32_t cardCount = 0;      // DAT_0065E05C
    std::uint32_t goalLog[0x50] = {}; // 0x65E06C: byte0 bit0 team, 1 own goal / shoot-out miss, 2 penalty,
                                      //   3 shoot-out; byte1 squad number; u16 time (seconds)
    std::uint32_t cardLog[0x14] = {}; // 0x65E1AC: byte0 bit0 team, bit1 red; byte1 squad number; u16 time
    TeamStats stats[2];
    ShootoutState so;                // 0x65EE14
    std::int32_t kickOrder[2][11] = {}; // 0x66755C shoot-out takers (player index in team)

    // --- AI timers 0x6731F4.. (FUN_004BBB3C, called by Match_resetPhase) ---
    std::int32_t ai6731f4 = 0, ai673208 = 0, ai67320c = 0, ai673214 = 0, ai673218 = 0, ai67321c = 0;

    // --- training block 0x667410.. (training module 0x46D420.. untraced) ---
    std::int32_t trainDrill = 0;     // DAT_00667410
    std::int32_t trainResult = 0;    // DAT_00667428
    std::int32_t trainBusy = 0;      // DAT_00667430
    std::int32_t trainCtrl = 0;      // DAT_0066741C training controller index (training module only)
    std::int32_t trainGoal = 0;      // DAT_00667534
    // --- needed by SpStates (SpAction): ---
    std::int32_t skillPlayer = -1;   // DAT_00667274 player id that finished a skill move (State30D -> 0x308);
                                     //   cleared (0) by 0x46A58F/0x46AA11/0x46AAE1/0x46AB17, read by 0x4D9B1E
};

// Contract used by sim.cpp (match lifecycle, Match_run 0x40A664 / MatchLoop_runPeriod 0x41B480):
void Match_init(World& w, std::uint32_t seed); // Match_setupOptions 0x4544D0 + Match_init 0x4546F8 + period table
void Match_startPeriod(World& w);              // Match_startPeriod 0x454B14 (next period)
int Match_periodResult(const World& w);        // DAT_0057C604: 0 running, 1 period over, 2 match over
void Match_updateRules(World& w);              // Match_updateRules 0x46F878
void Match_frameFlags(World& w);               // Sim_frame tail: DAT_0065f278 & 8 -> FUN_0045564C

// Rules functions (exe address in the comment).
void Match_setupOptions(World& w);                              // 0x4544D0
void Match_buildPeriodTable(World& w, int index, int mode);     // 0x454888 (EAX, EDX)
void Match_startPeriodIndex(World& w, int index);               // 0x454B14 (EAX = period index)
void Match_resetPhase(World& w);                                // 0x454CFC
void Match_setPhase(World& w, int phase, int team);             // 0x455418
void Match_selectEventDispatcher(World& w, int periodType);     // 0x46F630
void Match_dispatchEvent(World& w, int event, int team);        // call through PTR 0x58104C
void Match_onEvent(World& w, int event, int team);              // 0x470F98
void Match_onShootoutEvent(World& w, int event, int team);      // 0x4715F8
void Match_onTrainingEvent(World& w, int event, int team);      // 0x473B20
void Match_onPresentationEvent(World& w, int event, int team);  // 0x4741A0
void Match_handicap(World& w, int level);                       // FUN_0045564C (EAX = DAT_0065EEF8)
void Match_lineUpRestart(World& w, int restartType);            // 0x46F3EC restart spot -> ball.restartPos
void Match_requestPeriodEnd(World& w, int whistle, int next, int team); // 0x471F30
void Match_decidePeriodOutcome(World& w);                       // 0x46FDE4
void Match_endPeriodSuddenDeath(World& w, int winner);          // 0x470EFC
int Match_checkAbandon(World& w);                               // 0x470E70
int Match_decideByTargets(int a, int b, const World& w);        // 0x46FD24 (2 = undecided)
void MatchClock_update(World& w);                               // 0x46FF60
void MatchClock_updateAlt(World& w);                            // 0x473A00
int MatchClock_canEndPeriod(World& w);                          // 0x46FB70 (1 = keep playing)
std::int32_t MatchClock_toGameSeconds(World& w, std::int32_t sec); // 0x437BF8
void Rules_checkBallOut(World& w);                              // 0x4705B4
int Rules_resolveFoul(World& w);                                // 0x4719BC
void Rules_registerFoul(World& w, int kind, int offender, int victim, const Vec3* spot, int injury); // 0x471BD4
void Rules_checkOffside(World& w, int passer, const std::int32_t pass[3]); // 0x4BC610 (EDX = {len, dx, dz})
std::int32_t Rules_isOffsidePosition(const World& w, int pid, int defender); // 0x4BC8F0 (distance beyond the line)
int Team_tooFewPlayers(const World& w, int team);               // 0x4D03A4 (< 7 players)
int Match_seqIsFoul(const World& w);                            // FUN_0046FB5C (seqId == 5)
int Team_nearestPlayer_4523E4(const World& w, const Vec3& pos, int team, int excludeSlot, std::int32_t* outDist);
int Team_findKeeper_45299C(World& w, int team);                 // 0x45299C (first slot-0 player, -1)
// Pitch predicates (integer positions).
bool Pitch_inPenaltyArea(const World& w, const Vec3& p);          // 0x455D98 either end
bool Pitch_inPenaltyBox(const World& w, int side, const Vec3& p); // 0x455DD0
bool Pitch_inGoalArea(const World& w, int side, const Vec3& p);   // 0x455E34
bool Pitch_inGoal(const World& w, int side, const Vec3& p);       // 0x455E98
void Pitch_clampToField(const World& w, Vec3& p);                 // 0x455F38
bool Pitch_onField(const World& w, const Vec3& p);                // 0x456094

// Restart sequences (queue 0x6675BC, handler table 0x581008).
void MatchSeq_push(World& w, int id);           // 0x471F78
void MatchSeq_clear(World& w);                  // 0x471FA8
void MatchSeq_startNext(World& w, int runNow);  // 0x472344
void MatchSeq_reset(World& w);                  // FUN_0046F2EC (sequences + referee motion params)
// Referee = players[kReferee] (movement object 0x66764C, world.hpp).
const Vec3& Referee_posI(const World& w);         // 0x667668
void Referee_onPhase_470C80(World& w, int kind); // speed cap + target for a phase / restart kind
void MatchSeq_run(World& w, int id);            // table 0x581008[id]
int SeqTrigger_busy(const World& w, int id);    // PTR 0x581BB4 (presentation triggers: never busy)
void CutScene_play(World& w, int id);           // CutScene_playForEvent 0x4643F0 (no-script path)
int CutScene_isReady(const World& w);           // 0x46520C

// ---------------------------------------------------------------------------------------------
// Functions of other subsystems called by the rules code. Declared here until they appear in the
// owner's header (an identical redeclaration there is harmless).
// TeamAI (team.hpp):
void Formation_initZoneScales(World& w);                        // 0x4CE5EC
void Formation_init(World& w);                                  // Formation_loadTables 0x4CE374 tail
void Formation_resetCaches(World& w);                           // 0x4CE510
void Formation_ballZone(World& w, const Vec3& pos16, std::int32_t* out, int hysteresis); // 0x4CF230
void Formation_adaptToPlayers(World& w, int team);              // 0x4CEDAC
void Team_onPhaseChange(World& w, int team);                    // 0x452D10
void Team_setPlayersFlag80(World& w, int team, int on);         // 0x451D38
void Team_lateGameFormation(World& w, int team);                // 0x451B18
void Team_setPlayersState(World& w, int team, int state, int exceptState, int exceptSlot); // 0x452A90
void Team_resetAnimFlag1(World& w, int team, int keeperAnim, int outfieldAnim);            // 0x4532D0
void Team_holdPlayers_451C2C(World& w, int team, int frames, int ecx, int ebx);            // 0x451C2C
void Team_afterGoal(World& w, int team);                        // 0x4517B4
void Team_resetPlayersPeriod(World& w, int team, int flag);     // 0x451D94
void Team_initMatch(World& w, int team);                        // 0x4555D4
int Team_playerBySquad(const World& w, int team, int squadIdx); // 0x4529F8
void Team_startPeriod(World& w, int team, int side);            // 0x4513FC (EBX = phase, read from w)
void Team_setControlLevels(World& w, int team);                 // 0x45159C
int Team_playersWalking(const World& w, int team);              // 0x451CEC
int Team_playersBusy(const World& w, int team);                 // 0x451CA0
void Team_sendOff(World& w, int team, int playerIndex);         // 0x49F804 (+ 0x504FBC)
void Team_humanAfterSendOff(World& w, int team, int flag);      // 0x4A0134
// Players (player.hpp):
void Team_initPlayers(World& w, int team, int playerCount);     // 0x451210
void Players_buildSpeedTables(World& w);                        // 0x4CFF94 (zeroes the stats block)
void PlayerAttr_resetTeam(World& w, int team);                  // 0x478D18
void PlayerAttr_newPeriod(World& w, int team);                  // 0x478F88
void PlayerAttr_applyHandicap(World& w, int team, int edx, int ebx, int level); // 0x478E60
std::int32_t PlayerAttr_teamRating(const World& w, int team);   // 0x478EDC
void PlayerAttr_raiseSpecial_478CBC(World& w, int team, std::int32_t delta); // 0x478CBC outfield skills += delta
void Player_injure(World& w, int pid);                          // 0x4BD998
void Keeper_release_4BF29C(World& w, int pid);                  // 0x4BF29C
bool Player_isState100Kind0(const World& w, int pid);           // 0x4D250C
bool Player_isState100Kind1(const World& w, int pid);           // 0x4D2578
bool Player_isState100Kind2(const World& w, int pid);           // 0x4D2540
void Tackle_buildMoves(World& w);                               // 0x4D18E0
// Init callees of Match_setupOptions / Match_init / Match_startPeriod whose owner has not declared a
// name yet (pending; integrator assigns): player arrays 0x4511D0, 0x4BB9E0 (0x672F24..), controllers
// 0x466D84, anim tables 0x4CC6B0, anim engine 0x45F648, AI table 0x4E073C (0x58B6EC), distance cache
// 0x4D9F70 (0x676604), player-AI block 0x4BEC34 (0x6734F0).
void Players_initArrays_4511D0(World& w);
void Players_init_4BB9E0(World& w);
void Anim_initTables_4CC6B0(World& w);
void Anim_resetEngine_45F648(World& w);
void Player_clearDistanceCache_4D9F70(World& w);
void PlayerAI_reset_4BEC34(World& w);
// Ball (ball.hpp): Ball_init, Ball_resetForPeriod, Ball_placeAt, Ball_releaseFromPossessor,
// Ball_markerInForecast are declared there.

// WC98 h6 kickoff placement task (FUN_00516f40) modeled per-frame in the port sim
// (TASK_HANDLERS_WC98 §5 "h6", §7 delta-3). KickoffPlace_init arms the dolly window on a kick
// restart (replacing the direct centre placement), KickoffPlace_step advances it every frame;
// kickoffSpinUnits is the ±DAT_00656e08 spin-adjust magnitude (see rules.cpp for provenance).
void KickoffPlace_init(World& w);
bool KickoffPlace_step(World& w); // true while the dolly window runs
std::int32_t kickoffSpinUnits(const World& w);

} // namespace wc98::sim
