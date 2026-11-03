#pragma once
// Teams, formations and team AI (owner: TeamAI agent).
//
// TeamState mirrors the team block 0x65A2D0 + t*0x3C8 field for field (offsets static_asserted in
// team.cpp); exe player pointers are player ids (team*11 + index, -1 = none), team pointers are team
// indices. FormationState holds the formation module globals (0x58B108.., slot caches 0x675BA4 /
// 0x676364, slot offsets 0x6764DC, record pointers 0x6765F4/0x6765FC).
#include <cstdint>
#include "wc98/sim/fixed.hpp"

namespace wc98::sim {

struct World;

inline constexpr int kSlots = 17; // formation slots: 0 GK, 1 sweeper, 2-6 defence, 7-11 midfield, 12-16 attack
inline constexpr int kSlotLinks = kSlots * 4; // 0x44 bytes at team+0x2CC

// Role class per slot, 0x58B028 (0 GK, 1 sweeper, 2 defender, 3 midfielder, 4 forward).
inline constexpr std::int32_t kSlotRole[kSlots] = {0, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4};
// Left/right mirror slot, 0x58B06C (i32 table).
inline constexpr std::int32_t kSlotMirror[kSlots] = {0, 1, 6, 5, 4, 3, 2, 11, 10, 9, 8, 7, 16, 15, 14, 13, 12};

struct TeamState {
    std::int32_t index = 0;    // +0x000 team index
    std::int32_t side = 0;     // +0x004 attack direction; side 1 mirrors x/z
    std::int32_t opponent = 0; // +0x008 opponent team (exe: pointer)
    // +0x00C..+0x183: the 0x178-byte front-end tactics block copied in by Team_initPlayers 0x451210
    // (memcpy 0x44EA50 from *(0x65EF5C + t*0x134), built by the FE in FUN_0052168C). Byte layout
    // (team offsets): +0x0F total players of the active formation (written by 0x4CEE60), +0x10
    // active formation record index 0..2, +0x16 + i*0x6E formation record i: [0] mentality,
    // [1..7] descriptor {gk, sweeper, def, mid, fwd, wideFlagA, wideFlagB}, [8 + s*6] slot s
    // {u8 used, i8 dx, i8 dz, u8, u16 player tag}.
    std::uint8_t blk[0x178] = {};
    std::int32_t players[11] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}; // +0x184
    std::int32_t possWinner = -1; // +0x1B0 player who took possession (Ball_takePossession)
    std::int32_t f1b4 = -1;       // +0x1B4 player id (deref'd by State003 0x5338FC); set -> attack participation uses mentality 0
    std::int32_t f1b8 = -1;       // +0x1B8 player id (IGT pass-back target, 0x4E2684)
    std::int32_t f1bc = -1;       // +0x1BC nearest to the own goal line goalLineZ[side] (slot 0 excluded), 0x4536A8
    std::int32_t f1c0 = -1;       // +0x1C0 nearest to the far goal line goalLineZ[!side] (slot 0 excluded), 0x4536A8
    std::int32_t f1c4 = -1;       // +0x1C4 nearest to the ball reference point (slot 0 excluded), 0x4536A8
    std::int32_t f1c8 = -1;       // +0x1C8 nearest eligible to the reference point (0x452728, any slot)
    std::int32_t f1cc = -1;       // +0x1CC chaser: nearest eligible, switched when > 0x80 from the old one
    std::int32_t f1d0 = -1;       // +0x1D0 ahead-of-ball pick (0x452578) or f1c4
    std::int32_t oneTouch = -1;   // +0x1D4 one-touch kicker (Player_queueOneTouch 0x4E2244)
    std::int32_t f1d8 = -1;       // +0x1D8 player id
    std::int32_t f1dc = -1;       // +0x1DC player id
    std::int32_t f1e0 = -1;       // +0x1E0 player id (kick receiver, Kick_setRequestParams)
    std::int32_t f1e4 = -1;       // +0x1E4 goalkeeper (slot 0) player id, set by 0x49EBCC
    std::int32_t f1e8 = -1;       // +0x1E8 player id (always marked in 0x4D39E4)
    std::int32_t f1ec = -1;       // +0x1EC player id (opponent man-marked by f1e8, 0x4D39E4)
    std::int32_t f1f0 = -1;       // +0x1F0 player id (slot-swap partner of possWinner, 0x4D2F24)
    std::int32_t ctrl1f4 = 0;     // +0x1F4 0..2 (0x45159C)
    std::int32_t ctrl1f8 = 0;     // +0x1F8 0..2 (0x45159C)
    std::int32_t ctrl1fc = 0;     // +0x1FC 0..2 (0x45159C, read by Player_setSpeedScale)
    std::int32_t f200 = 0;        // +0x200
    std::int32_t f204 = 0;        // +0x204 (FUN_00454448)
    std::int32_t mentality = 0;   // +0x208 0..4 (Team_setMentality 0x451750); block zeroed at setup (0x4545E4)
    std::int32_t formStyle = 0;   // +0x20C 0x58AE34[teamDB +0x6F]
    std::int32_t f210 = 0;        // +0x210
    std::int32_t line214 = 0;     // +0x214 (= halfLength at period start)
    std::int32_t line218 = 0;     // +0x218
    std::int32_t order[11] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}; // +0x21C players by slot priority 0x587170 (0x4A047C)
    Vec3 kickPos[11];             // +0x248..+0x2CB line-up points built by 0x451DDC (two lines of 5 + 1)
    std::uint8_t links[kSlotLinks] = {}; // +0x2CC [base slot][4] team-mate index or 0xFF (0x4A047C)
    std::int32_t w310[12] = {};   // +0x310..+0x33F
    std::int32_t oneTouchTimer = 0; // +0x340 frames (Player_queueOneTouch sets 100)
    std::int32_t oneTouchCode = 0;  // +0x344 kick code
    std::int32_t playerCount = 11;  // +0x348
    std::int32_t humans = 0;        // +0x34C human controllers on this team
    std::int32_t f350 = 0, f354 = 0; // +0x350 / +0x354
    std::int32_t line358 = 0;     // +0x358 formation line parameter (defence+mid count)
    std::int32_t line35c = 0;     // +0x35C formation line parameter (midfield count)
    std::int32_t w360[5] = {};    // +0x360..+0x373
    std::int32_t f374 = 0;        // +0x374
    std::int32_t f378 = 0;        // +0x378
    std::int32_t w37c[17] = {};   // +0x37C..+0x3BF
    std::int32_t timer3c0 = 0;    // +0x3C0
    std::uint32_t flags = 0;      // +0x3C4 u16 flags (bit 1 possession, 0x80, 0x200 ...); upper half unused
    // The per-team setup block 0x65EF58 + t*0x134 (front end -> match) fields used by team code.
    std::int32_t setupAuto = 0;   // 0x65F088 + t*0x134: AI manages tactics of a human team
    std::int32_t possessionThird[3] = {}; // 0x65E21C/220/224 + t*0x60C: frames in own/middle/attacking third
    std::int32_t supportToggle = 0; // 0x57F9AC[t] alternates every Team_updateSupportTargets call
    std::int32_t g57f9a4 = 0;       // 0x57F9A4[t] (cleared by Team_startPeriod 0x4513FC; reader not traced)
    // (score: w.match.score[t], 0x65E050, owned by Rules)

    std::uint8_t* rec(int i) { return blk + (0x16 - 0x0C) + i * 0x6E; }
    const std::uint8_t* rec(int i) const { return blk + (0x16 - 0x0C) + i * 0x6E; }
    std::uint8_t& formIdx() { return blk[0x10 - 0x0C]; }
    std::uint8_t formIdx() const { return blk[0x10 - 0x0C]; }
};

// One 0x1C-byte slot cache entry (0x675BF4.. / 0x675FAC..).
struct SlotCacheEntry {
    std::uint8_t state = 0;   // +0 0 idle, 1 fresh, 2 queued for recompute
    std::uint8_t flag = 0;    // +1 zone follows the current ball zone
    std::uint16_t val = 0;    // +2 attack participation / lerp argument
    std::int16_t zone = -1;   // +4
    std::uint16_t pad = 0;    // +6
    std::int32_t fn = 0;      // +8 positioning callback (exe address, see Formation_callPos)
    std::int32_t player = -1; // +0xC
    Vec3 pos;                 // +0x10 result (integer world units)
};

// Cache header (0x675BA4 current slots / 0x676364 base slots, + t*0x28).
struct SlotCache {
    std::uint32_t dirty = 0; // +0x00 bit s = slot s needs recompute
    std::int32_t line358 = 0, line35c = 0; // +0x04/+0x08 line parameters used while evaluating
    std::int32_t poss = 0;   // +0x0C possession bit snapshot
    // (+0x10 entry pointer: implicit)
    std::uint8_t queue[kSlots] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                                  0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}; // +0x14
    std::uint8_t readIdx = 0, writeIdx = 0, pad = 0; // +0x25 / +0x26
    SlotCacheEntry e[kSlots];
};

struct FormationState {
    Fixed scaleX = 0, scaleZ = 0;         // 0x58B108 / 0x58B10C: 32*W'/0xB2C, 32*L'/0x1504 (PL/KO/FK units)
    Fixed pkScaleX = 0, pkScaleZ = 0;     // 0x58B110 / 0x58B114: W'/0xB2C, L'/0x1504 (PK units)
    Fixed colPerUnit = 0, rowPerUnit = 0; // 0x58B118 / 0x58B11C: 11/W, 21/L
    std::int32_t cellW = 0, cellL = 0;    // 0x58B120 / 0x58B124: W/11, L/21
    std::int32_t col = 0, row = 0;        // 0x58B128 / 0x58B12C last ball zone cell (hysteresis)
    SlotCache cur[2];                     // 0x675BA4 + t*0x28 (keyed by current slot +0xC)
    SlotCache base[2];                    // 0x676364 + t*0x28 (keyed by base slot +0x8)
    std::int32_t slotOff[2][kSlots][2] = {}; // 0x6764DC + t*0x8C: per-slot (x, z) offsets in table units
    std::int32_t adapt[2] = {};           // 0x6764DC + t*0x8C + 0x88: score-adaptation state (0x4CEA9C)
    std::int32_t selRec[2] = {};          // record bound by Formation_selectTeamFormation (0x6765F4/FC)
    std::int32_t g57f9a0 = 0;             // DAT_0057F9A0 (cleared by Team_startPeriod 0x4513FC; reader not traced)
};

// ---- Formation module (formation.cpp) ----
void Formation_initZoneScales(World& w);          // 0x4CE5EC (called from Pitch_setDimensions 0x455930)
void Formation_resetCaches(World& w);             // 0x4CE510 (Match_startPeriod 0x454B1D)
void Formation_init(World& w);                    // tail of Formation_loadTables 0x4CE374 (after the file loads)
void Formation_selectTeamFormation(World& w, int team, int rec); // 0x4CE2E0
// 0x4CF230: EAX = pos (16.16), EDX = out (2 dwords: zone, 298 - zone; may be null), EBX = hysteresis.
void Formation_ballZone(World& w, const Vec3& pos16, std::int32_t* out, int hysteresis);
// 0x4CF358: positioning callback cb (index into table 0x58B0B0) for player pid; zone < 0 = current
// ball zone of the player's side; stores into the current-slot cache when `store`. Returns the
// callback (exe address; players keep it at +0x30).
std::int32_t Formation_evalSlotCallback(World& w, int cb, int pid, Vec3& out, int zone, int val, int store);
// 0x4CF450: cached formation target with attack participation; fn = callback exe address, base = 0
// current-slot cache (+0xC/+0x1C) / 1 base-slot cache (+0x8/+0x18).
void Formation_attackParticipation(World& w, std::int32_t fn, int pid, Vec3& out, int zone, int base);
// 0x4CF5F8: out = b if team in possession else b + (a - b) * DF[mentality] / 128 (x/z).
void Formation_lerpByMentality(World& w, Vec3& out, int team, const Vec3& a, const Vec3& b);
// Calls a positioning callback by exe address (the cached `fn` values): EAX = player, EDX = out,
// EBX = zone, ECX = val.
void Formation_callPos(World& w, std::int32_t fn, int pid, Vec3& out, int zone, int val);
void Formation_slotPosition(World& w, int pid, Vec3& out, int zone, int val); // 0x4CF678
Fixed Formation_lineAdjust(const World& w, int pid);                          // 0x4CE7C8
void Formation_flushSlotCache(World& w, int base);                            // 0x4CE0D0
void Formation_applyDescriptor(World& w, int team, const std::uint8_t desc[7]); // 0x4CEE60
void Formation_adaptToPlayers(World& w, int team);                            // 0x4CEDAC

// ---- Teams (team.cpp) ----
void Team_setMentality(World& w, int team, int m); // 0x451750
void Team_buildSupportLinks(World& w, int team);   // 0x4A047C (order[] / links[] by mentality)
void Team_setFormation(World& w, int team, int f); // 0x4A0730
void Team_buildTacticsBlock(World& w, int team);   // FE default tactics block (0x52168C default path) -> team.blk
int Team_dbByte6f(const World& w, int team);       // team DB record byte +0x6F = raw[5] bits 10..14 (0x4ABE84)
std::int32_t Formation_styleForTeam(int dbByte6f); // 0x4CF350 (table 0x58AE34)
void Team_bindFormationSlots(World& w, int team);  // 0x49EBCC
void Team_startPeriod(World& w, int team, int side); // 0x4513FC
void Team_buildKickPositions(World& w, int team);  // 0x451DDC (team.kickPos)
void Team_setControlLevels(World& w, int team);    // 0x45159C
void Team_afterGoal(World& w, int team);           // 0x4517B4
void Team_lateGameFormation(World& w, int team);   // 0x451B18
void Team_setPlayersFlag80(World& w, int team, int on); // 0x451D38
void Team_resetPlayersPeriod(World& w, int team, int flag); // 0x451D94
void Team_initMatch(World& w, int team);           // 0x4555D4 (setupAuto from controllers)
int Team_playerBySquad(const World& w, int team, int squadIdx); // 0x4529F8 (-1 none)
void Team_lineUp(World& w, int team, int cb);      // 0x453628
int Team_nearestToLineZ(const World& w, std::int32_t z, int team, int exclSlot);    // 0x452360
int Team_nearestToPointFree(const World& w, const Vec3& p, int team, int exclSlot); // 0x452728
int Team_aheadOfBallPick(const World& w, int team, int exclSlot);                   // 0x452578
int Team_opponentInBox(World& w, int pid, std::int32_t x0, std::int32_t x1, std::int32_t z0, std::int32_t z1,
                       std::int32_t maxDist, int exclSlot);                         // 0x453AB0
void Player_chooseRunTarget(World& w, int pid, int a, int b, std::int32_t dist, Vec3& out); // 0x4DB320
void Player_runAround(World& w, int pid, int q, std::int32_t dist, int flag, Vec3& out);    // 0x4DB260

// Contract used by Sim_frame (sim.cpp):
void Formation_drainQueue(World& w);                // FUN_004CE254 (before each time slice)
void Team_updateSupportTargets(World& w, int team); // 0x453B5C
void Team_lateGameTactics(World& w);                // FUN_00455730
void Team_autoMentality(World& w, int team);        // 0x451860 (EAX = DAT_0065f240 = team)
void Team_updateTactics(World& w, int team);        // 0x4536A8

// ---- Phase set-up (team_phase.cpp) ----
void Team_onPhaseChange(World& w, int team); // 0x452D10 (Match_setPhase, team.playerCount != 0)
void Team_setPlayersState(World& w, int team, int state, int exceptState, int exceptSlot); // 0x452A90
void Team_setPlayersStateFiltered(World& w, int team, int state, int exceptSlot);         // 0x452B10
int Team_playerForSlotTag(const World& w, int team, int k); // 0x452BD8 u16 squad tag at team+0x160+k*2
void Team_resetAnimFlag4(World& w, int team);               // 0x452C5C
void Team_resetAnimFlag1(World& w, int team, int keeperAnim, int outfieldAnim); // 0x4532D0
void Team_lineUpWall(World& w, int team);                   // 0x453360 free-kick wall
// 0x4524B0: player whose move target is nearest pt (skips +0x22C bits 0/1 and curSlot == skipSlot).
int Team_nearestTargetToPoint(World& w, const Vec3& pt, int team, int skipSlot, std::int32_t* outDist);
int Team_nearestTargetToPointAll(World& w, const Vec3& pt, int team, int skipSlot); // 0x4527E8 (no bit-0 test)
void Input_controlNearestToBall(World& w);                  // 0x466ED0

// ---- Team AI decisions (ai.cpp) ----
int Team_decidePossession(World& w, int team);      // 0x4D3DBC -> new team.flags bit 1
void Team_onPossessionChange(World& w, int team);   // 0x4D37AC clear all marks of the team
void Team_attackAi_4D2644(World& w, int team);      // 0x4D2644 support spots (team+0x310) / player+0x14
void Team_attackAi_4D2F24(World& w, int team);      // 0x4D2F24 slot swap with possWinner, line push-ups
void Team_attackAi_4D35AC(World& w, int team);      // 0x4D35AC counts ahead / behind the ball (team+0x360)
void Team_attackAi_4D2A44(World& w, int team);      // 0x4D2A44 defensive line z (+0x214/+0x218), marking
void Team_defendAi_4D39E4(World& w, int team);      // 0x4D39E4 man marking
void Team_ai_4DA3C4(World& w, int team);            // 0x4DA3C4 team+0x378 mode: 1 offside trap, 4 press
void Team_armOffsideTrap(World& w, int team);       // 0x4DA670
void Team_clearPressFlags(World& w, int team);      // 0x4DA190 (player flag 0x200000)
void Team_startPress(World& w, int team);          // 0x4DABC8 (IGT action: mode 4, press the ball winner)
bool Team_beatsDefender_4DBA34(World& w, int pid, std::int32_t* vec = nullptr); // 0x4DBA34 (EDX vector out)
void Team_mark(World& w, int marker, int marked);   // 0x4D3734
void Team_unmark(World& w, int marked);             // 0x4D3760
void Sort_shellDesc(int n, std::int32_t* keys, std::int32_t* idx); // 0x481560 (descending)
int Team_nearestPlayer_452650(const World& w, const Vec3& pos, int team, int exclSlot); // 0x452650
void Player_resetIdle_4BD308(World& w, int pid);    // 0x4BD308 default state when idle (from 0x4536A8)

// ---- Squad / line-up module (squad.cpp; line-up sheet w.pl.lineup = 0x66E564 + t*0x2C) ----
int Squad_subsLeft_49ED4C(const World& w, int team);            // left - pending - queued
void Squad_bindPlayer_49FA9C(World& w, int team, int k, int squad);
void Squad_substitute_49F03C(World& w, int team, int squadA, int squadB); // request a <-> b
void Squad_applyLineup_49F140(World& w, int team);
bool Squad_nextSub_49F630(World& w, int* team, int* idx);      // next pending substitution
int Squad_firstPending_49FA48(const World& w, int team);
int Squad_pendingCount_66E586(const World& w, int team);       // u16 0x66E586 + t*0x2C
void Team_removeInjured_49F684(World& w, int team);            // carry out the first pending substitution
int Squad_pickReplacement_4A0260(const World& w, int team, int outSquad, int mode);
int Squad_pickInjurySub_49FA00(World& w, int team, int index, int mode); // bench squad index or 0xFF
void Team_aiSubstitutions_453E58(World& w);                    // Sim_frame 0x455293
void Team_assignPositions_49EB10(World& w, int team);          // player +0x04 = slot of his tag
void Team_allocateTakers_4A0AA0(World& w, int team);           // set-piece takers team+0x160
void Team_reapplyRecords_4A08FC(World& w, int team);           // tail of 0x49EBCC

} // namespace wc98::sim
