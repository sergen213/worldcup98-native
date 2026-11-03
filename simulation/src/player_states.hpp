#pragma once
// Player state machine internals (owner: SpStates, Simulation.SimPlayers.SpStates). Private to
// simulation/src: state handlers (table 0x589068), their helpers in 0x532000..0x546000,
// 0x557000..0x558000, 0x4DA000..0x4DC000, 0x4E0474..0x4E0900, and declarations of functions
// owned by other agents that are not yet declared in their public headers ("pending externals":
// remove an entry once its owner declares it with the same signature).
//
// Conventions: pid = team*11 + index (player pointer 0x65AA60 + pid*0x26C); Watcom args EAX, EDX,
// EBX, ECX then stack. Every function cites its exe address.
#include "wc98/sim/world.hpp"

namespace wc98::sim {

inline PlayerState& P(World& w, int pid) { return w.players[static_cast<std::size_t>(pid)]; }
inline const PlayerState& P(const World& w, int pid) { return w.players[static_cast<std::size_t>(pid)]; }

// --- State handlers (named by the first state id using them; see player_states.cpp) ---
void State000(World& w, int pid); // 0x532c34
void State001(World& w, int pid); // 0x532d18
void State002(World& w, int pid); // 0x5335ec
void State003(World& w, int pid); // 0x533898
void State004(World& w, int pid); // 0x533c4c
void State080(World& w, int pid); // 0x5340d4
void State081(World& w, int pid); // 0x5341a0
void State082(World& w, int pid); // 0x534214
void State083(World& w, int pid); // 0x534454
void State084(World& w, int pid); // 0x5344e8
void State085(World& w, int pid); // 0x53467c
void State086(World& w, int pid); // 0x5348dc
void State100(World& w, int pid); // 0x534990
void State180(World& w, int pid); // 0x535bec
void State181(World& w, int pid); // 0x536064
void State182(World& w, int pid); // 0x536e04
void State183(World& w, int pid); // 0x53756c
void State185(World& w, int pid); // 0x5367e0
void State186(World& w, int pid); // 0x53636c
void State187(World& w, int pid); // 0x5365d0
void State188(World& w, int pid); // 0x5382c4
void State18A(World& w, int pid); // 0x557cf0
void State18C(World& w, int pid); // 0x538040
void State18D(World& w, int pid); // 0x557ad4
void State200(World& w, int pid); // 0x538760
void State201(World& w, int pid); // 0x539280
void State202(World& w, int pid); // 0x539134
void State203(World& w, int pid); // 0x539b30
void State204(World& w, int pid); // 0x53a264
void State205(World& w, int pid); // 0x53903c
void State206(World& w, int pid); // 0x53b8f4
void State207(World& w, int pid); // 0x53c214
void State208(World& w, int pid); // 0x538d58
void State209(World& w, int pid); // 0x53b11c
void State20C(World& w, int pid); // 0x53a654
void State280(World& w, int pid); // 0x53d424
void State281(World& w, int pid); // 0x53d988
void State300(World& w, int pid); // 0x53df6c
void State301(World& w, int pid); // 0x53ec9c
void State302(World& w, int pid); // 0x53e630
void State303(World& w, int pid); // 0x53f354
void State304(World& w, int pid); // 0x53f7f4
void State305(World& w, int pid); // 0x53fd08
void State306(World& w, int pid); // 0x53fd50
void State307(World& w, int pid); // 0x53fe00
void State308(World& w, int pid); // 0x540658
void State309(World& w, int pid); // 0x541468
void State30A(World& w, int pid); // 0x5426cc
void State30B(World& w, int pid); // 0x542d78
void State30C(World& w, int pid); // 0x4e0814
void State30D(World& w, int pid); // 0x4e0474
void State30E(World& w, int pid); // 0x4e07f8
void State380(World& w, int pid); // 0x4da6b4
void State381(World& w, int pid); // 0x4da90c
void State382(World& w, int pid); // 0x4dafc0
void State383(World& w, int pid); // 0x4dac18

// --- Helpers: outfield 0x000-0x100 (SpStates, player_states_outfield.cpp) ---

// --- Helpers: outfield 0x180-0x18F, 0x280-0x281, 0x380-0x383 (player_states_run.cpp) ---

// --- Helpers: keeper 0x200-0x22F + keeper block 0x543390 (keeper*.cpp) ---
// Byte-exact keeper data read by exe address (keeper_data.cpp): 0x5892A0..0x589D10 (0x580FA0 +
// state*0x40 for 0x20C..0x22F, 0x589BA0/0x589C20/0x589C30 rows), 0x59F440..0x59F600, 0x5A04CC asin.
std::uint8_t Keeper_d8(std::uint32_t exeAddr);
std::int32_t Keeper_d32(std::uint32_t exeAddr); // little-endian dword (unaligned like the exe reads)
void Keeper_keepOutOfGoal(World& w, int pid);   // 0x53b04c possessor keeper: push back to z = +-(halfLength-0x20) (keeper_dive.cpp)
void Keeper_parryBall(World& w, int pid);       // 0x53b5dc deflect the ball with the dive row's vector (keeper_dive.cpp)
// 0x53c4e0 (EDX = anim, EBX/ECX/stack outs) anim window flags for the current dive anim (keeper_hold.cpp)
void Keeper_animPhase(World& w, int pid, int anim, std::int32_t* a, std::int32_t* b, std::int32_t* c);
void Keeper_holdPhase(World& w, int pid, std::int32_t* a, std::int32_t* b, std::int32_t* c); // 0x53c5bc (EDX, EBX, ECX outs) (keeper_hold.cpp)
bool Keeper_ballMarkersGone(const World& w, std::int32_t k); // 0x53c764 k <= -3, no goal flag, stamps[6]/[8] not ahead (keeper_hold.cpp)
// 0x53c62c anim-chain callback passed by exe address (EBX of Anim_start 0x4CCCFC) from 0x53ab10 / State206:
// EAX = finished anim, EDX = default next anim, EBX = out (zeroed); next anim from the keeper rows (keeper_hold.cpp)
std::int32_t Keeper_nextAnimCb(World& w, std::int32_t anim, std::int32_t dflt, std::int32_t* out);
// Marking helpers 0x53c7e0..0x53d424 used by State280/281/381 (keeper_hold.cpp):
bool Mark_rollTackle(World& w, int pid);                           // 0x53c7e0 rng&0x1f < 0x59F500[team.ctrl1f4]
std::int32_t Mark_engageScore(World& w, int pid, const Vec3& pt, int idx); // 0x53c808 (EDX pt, EBX idx) possession/zone score
std::int32_t Mark_zoneValue(World& w, int pid, int idx);           // 0x53c990 (EDX idx) per-zone table 0x59F548..0x59F5A8
std::int32_t Mark_tackleOdds(World& w, int pid, int opp, std::int32_t dist); // 0x53ca88 (EDX opp, EBX dist)
void Mark_setDefendLine(World& w, int pid, int opp, int idx, Angle a); // 0x53cd1c (EDX opp, EBX idx, ECX) -> player+0x228
// 0x53ce90 (EDX opp, EBX out, ECX angle, stack s0 pushed last, s1): jockey point in out; 1 = in range
int Mark_jockeyTarget(World& w, int pid, int opp, Vec3& out, Angle a, std::int32_t s0, std::int32_t s1);

// --- Helpers: actions 0x300-0x30E (player_states_action.cpp) ---
bool Action_humanPassReady(World& w, int pid, bool execute); // 0x53dbc4 team+0x1B8 human with team+0x200 kick code; execute -> Player_executeKick
bool Action_canShootAtGoal(World& w, int pid, const Vec3& p, std::int32_t reach); // 0x53dc10 (EDX = point, EBX = reach)
bool Action_turnToOpponentTarget(World& w, int pid, std::int32_t n); // 0x53deb0

// --- Pending externals (owner in comment; append only, grep before adding) ---
// 0x4524B0 owner: Simulation.SimTeamAI (pending) — EAX point, EDX team, EBX slot to skip, ECX out dist:
// team player (not flag 2/1 in +0x22C, curSlot != skip) whose target vector is nearest to the point.
int Team_nearestTargetToPoint(World& w, const Vec3& pt, int team, int skipSlot, std::int32_t* outDist);
int Kick_execute(World& w, int pid);        // 0x4D07B0 owner: Simulation.SimPlayers.SpKick (pending) — EAX = player+0x190; release the armed kick, nonzero if kicked
void Kick_arm(World& w, int pid);           // 0x4D0BB4 owner: Simulation.SimPlayers.SpKick (pending) — set pending bit, start kick anim (+0x1D0)
void Kick_clearPending(World& w, int pid);  // 0x4D0770 owner: Simulation.SimPlayers.SpKick (pending) — clear +0x1E4 bits 2/3, drop ball.dribbler if it is pid
void Player_executeKick(World& w, int pid, int code, std::int32_t ballY); // 0x4E3478 owner: Simulation.SimPlayers.SpKick (pending) — EDX code, EBX ball height
bool Player_hasPriorityOver(const World& w, int pid, int other); // 0x4BD0D0 owner: Simulation.SimPlayers (pending) — EDX other: other possessor / nearer / pid.countdown -> 1
int Ball_firstStampAfter(const std::int16_t* stamps, int count, int frame); // 0x47701C owner: Simulation.SimBall (pending) — first stamps[i] > frame, else -1
void Player_refreshSpeedCap(World& w, int pid);      // 0x4BBE30 owner: Simulation.SimPlayers (pending) — speedCap: human -> 0x4BBD84, AI by attr/phase
bool Player_canReachCross85(World& w, int pid);      // 0x4BEBA8 owner: Simulation.SimPlayers (pending) — can reach ball's y=85 descent point (0x669190) in time
std::int32_t Player_claimRange(const World& w, int pid); // 0x4BE034 owner: Simulation.SimPlayers (pending) — table 0x5890A8[team.ctrl1f8]
// 0x4BD9AC owner: Simulation.SimPlayers (pending) — intercept solver: EAX from, EDX speed, EBX ball pos, ECX vel.x,
// stack arg0 (pushed last) vel.z, arg1 halfWidth, arg2 halfLength, arg3 out point; returns frames (< 0 none). ret 0x10.
std::int32_t Player_solveIntercept(World& w, const Vec3& from, Fixed speed, const Vec3& ballPos, Fixed velX, Fixed velZ,
                                   std::int32_t halfW, std::int32_t halfL, Vec3& out);
void TeamAI_onBallWon_4DC614(World& w, int pid, int edx);   // 0x4DC614 (team_ai_ball.cpp) — AI kick after taking the ball (EDX kick code)
bool TeamAI_tryHeaderPass_4DF2CC(World& w, int pid, std::int32_t edx); // 0x4DF2CC (team_ai_ball.cpp) — EDX height for the kick spec; nonzero -> caller enters 0x30A
bool Motion_turnSettled(World& w, int pid);          // 0x4CBC6C owner: Simulation.SimPlayers (pending) — EAX player+0xB8; facing reached (clamped +-0x72) target
bool Motion_facingBall(World& w, int pid);           // 0x4CBA30 owner: Simulation.SimPlayers (pending) — |angleBall - facing| <= 0x55
// 0x4D0BF0 owner: Simulation.SimPlayers.SpKick (pending) — EAX = player+0x190 (req.r00 = player), EDX = {_, x, z}
// offset used when EBX (target, x/z read) is null, ECX, stack arg0 (pushed last) = y, arg1..arg4, arg5 (as int8);
// -> Kick_plan 0x4D0CFC mode 6 (ret 0x18).
void Kick_planToPoint(World& w, int pid, const std::int32_t* rel, const Vec3* target, std::int32_t ecx, std::int32_t y,
                      std::int32_t a1, std::int32_t a2, std::int32_t a3, std::int32_t a4, std::int32_t a5);
Angle Player_inputAimAngle(const World& w, int pid); // 0x4E0448 owner: Simulation (pending) — human pad (+0x10,+0x14) != 0 -> atan2i, else facingDesired (+0x134)
void Player_skillMoveBall(World& w, int animId, Angle angle, int pid); // 0x4E01EC owner: Simulation (pending) — EAX anim, EDX angle, EBX player; per-skill ball push (ECX preserved)
void Ball_stopDead(World& w); // 0x4BA1EC owner: Simulation.SimBall (pending) — Ball_kickWithVelocity(EAX null, EDX &{0,0,0}, EBX 0, ECX 1, stack 0)
void Math_polarRecord(std::int32_t r, Angle a, std::int32_t out[4]); // 0x4BB1A8 owner: Simulation.SimPlayers (pending) — out = {r, fx(r, sin a), fx(r, cos a), a} (EAX r, EDX a, EBX out)
void Math_normalize3(const Vec3& in, Vec3& out); // 0x4BB820 owner: Simulation.SimPlayers (pending) — out = in doubled until a |comp| >= 1.0, then / 0x4BB70C length (16.16)
// 0x4BB1F4 owner: Simulation.SimPlayers (pending) — closest approach of lines p0 + t0*d0 and p1 + t1*d1 (unit
// dirs): EAX p0, EDX d0, EBX p1, ECX d1, stack arg0 radius, arg1 t0 out, arg2 t1 out, arg3 normal out; 1 if
// distance >> 16 <= radius >> 16 (ret 0x10).
int Math_closestApproach(const Vec3& p0, const Vec3& d0, const Vec3& p1, const Vec3& d1, Fixed radius, Fixed* t0,
                         Fixed* t1, Vec3* normal);
// 0x460ACC owner: Simulation.SimPlayers.SpAnim (pending) — EAX = anim instance (player+0x74), EDX = out 16 bone
// records of 0x30 bytes, EBX = player+0x78: world-space skeleton; only record +0x24 (Vec3 16.16) is read.
void Anim_evalSkeleton(const World& w, int pid, Vec3 bones[16]);

// 0x4D1708 owner: Simulation.SimPlayers.SpKick (pending) — EAX player+0x190: if !(flags & 4): u16 +0x40 = anim,
// u16 +0x4E = frames (EBX), +0x44 = dist (ECX), u16 +0x4C = angle (stack); returns 1, else 0 (ret 4).
int Kick_setAnimParams(World& w, int pid, int animId, std::int32_t frames, std::int32_t dist, std::int32_t angle);
void TeamAI_planAirKick_4DCAC8(World& w, int pid); // 0x4DCAC8 (team_ai_ball.cpp) — AI header/volley kick plan

void Team_noteTouch_4D61BC(World& w, int pid); // 0x4D61BC owner: Simulation.SimTeamAI (pending) — EAX player: DAT_00667298 = DAT_00667294, DAT_00667294 = 0, then event-chain checks (0x50510C / 0x4D6364 / 0x4D66BC / 0x505188)
int TeamAI_onBallAct_4DD324(World& w, int pid); // 0x4DD324 (team_ai_ball.cpp) — AI on-ball decision; nonzero = acted
void Player_dribbleVector(World& w, int pid, std::int32_t dist, Angle a, std::int32_t out[4]); // 0x4BDCAC owner: Simulation.SimPlayers (pending) — EDX dist (<= 0x20, x2 if attr+0x10 && !(attr+0x40 & 0x40)), EBX angle, ECX out -> Math_polarRecord

// --- SimPorter2 (player_helpers.cpp, player_states_idle.cpp, ...) ---
// Squad module 0x49E000.. (pending, unassigned): injury substitution used by State085.
void Input_clearEdges(World& w, int c);                                 // 0x466888 owner: Simulation (input.cpp)
Fixed Math_vecLength3(const Vec3& v);                                   // 0x4BB70C
void Motion_faceVector(World& w, int pid, std::int32_t dx, std::int32_t dz); // 0x4CBCEC head aim toward (dx, dz)
void Motion_facePoint(World& w, int pid, const Vec3& pt);                  // 0x4CBD30 head aim toward a point
void Player_holdOffsideLine_4BCA4C(World& w, int pid);                      // 0x4BCA4C
bool Player_clearOfRestart_4BD05C(const World& w, const Vec3& pos);         // 0x4BD05C
bool Player_pointLeftOf_4BE080(const World& w, int pid, const Vec3& pt);    // 0x4BE080
void Player_meetPointCore_4BE168(World& w, int pid, std::int32_t dist, std::int32_t cang, std::int32_t yaw,
                                 const Vec3& ball, std::int32_t c, std::int32_t s0, const Vec3* from, Vec3& out);
// 0x4BE0FC (EDX anim, EBX ball, ECX c, stack s0, from, out): stand point for anim contact with the ball
void Player_meetPoint_4BE0FC(World& w, int pid, int anim, const Vec3& ball, std::int32_t c, std::int32_t s0,
                             const Vec3* from, Vec3& out);
// 0x4BE26C (EAX anim, EDX from, EBX to, ECX ox, stack oz, out): contact point; false without contact mark
bool Player_animContactPoint_4BE26C(const World& w, int anim, const Vec3& from, const Vec3& to, std::int32_t ox,
                                    std::int32_t oz, Vec3& out);
void Player_clearTeamRefs_4BE6D0(World& w, int pid);                        // 0x4BE6D0
// 0x4BE890 (EDX target, EBX lookAt, ECX n, stack out, angle out): 1 if the step covers >= 0.98
int Player_stepToward_4BE890(const World& w, int pid, const Vec3& target, const Vec3& lookAt, std::int32_t n,
                             Vec3& out, Angle* angle);
bool Player_ballAtContact_4BEA94(World& w, int pid);                        // 0x4BEA94
void Player_predictPos_4BEB5C(const World& w, int pid, std::int32_t frames, Vec3& out); // 0x4BEB5C
Angle Player_knockAngle_534090(World& w, int a, int o, std::int32_t h);     // 0x534090
// Tackle move table 0x58B340 (layout in player_helpers.cpp).
std::uint8_t Tackle_u8(const World& w, int row, int off);                   // byte accessors 0x4D2320..0x4D24D0
std::int32_t Tackle_anim_4D227C(const World& w, int row, int sel);
void Tackle_motion_4D23F0(const World& w, int row, int sel, std::int32_t* frames, std::int32_t* dist,
                          std::int32_t* angle);
std::int32_t Tackle_height_4D22C4(const World& w, int row);
std::int32_t Tackle_flag_4D22E4(const World& w, int row, int bit);          // 0x4D22E4 bit 0, 0x4D2378 bit 1, 0x4D24EC bit 2
int Tackle_findRow_4D20E0(const World& w, int kind, int cls);
int Tackle_approachClass_4D2128(World& w, int pid, int o);
int Tackle_target_4D21C4(World& w, int pid, int mode);
int Tackle_kindOf_4D221C(const World& w, int pid);
bool Tackle_ownTeamBall_4D2250(const World& w, int pid);
void Tackle_ballAfter_4D1C64(World& w, std::int32_t frames, Vec3& out);
int Tackle_pickMove_4D1A68(World& w, int pid, int o, int kind);
int Player_tryTackle(World& w, int pid, int kind);                          // 0x4D19C8
bool Player_dodgesTackle_4E03E4(const World& w, int pid, int tackler);       // 0x4E03E4
int Team_nearestToBall_4522FC(const World& w, int team, int skipSlot);      // 0x4522FC
// Pending externals owned by SimPorter3 (squad / cut-scene / set-piece / input / kick aim):
int CutScene_playerReady(const World& w, int pid);                       // 0x465294 (nonzero = ready)
int CutScene_active663420(const World& w);                               // DAT_00663420
int Training_ctrl66741C(const World& w);                                 // DAT_0066741C training controller index
void SetPiece_begin(World& w, int pid, Angle a, int flag);               // 0x44C3E0 (EDX angle, EBX flag)
void SetPiece_reset(World& w);                                           // 0x44C5B4
void SetPiece_updateHuman(World& w, int pid);                            // 0x44C7F4
void SetPiece_updatePower(World& w, int pid);                            // 0x44C9D8
void Input_giveControlToNth(World& w, int n, int pid);                   // 0x466E94 (EAX n, EDX pid)
void Input_action56(World& w, int pid, int edx);                         // 0x4E2D50
// 0x4E3D88 (EDX vec out, EBX/ECX outs, stack s0 (pushed last), s1)
void Kick_aimSetPiece(World& w, int pid, KickVec* v, std::int32_t* b, std::int32_t* c, std::int32_t s0, std::int32_t s1);
// 0x4E4D8C (EDX vec out, EBX, ECX, stack s0, s1 = player) -> receiver
int Kick_aimTo(World& w, int pid, KickVec* v, std::int32_t b, std::int32_t c, std::int32_t s0, int s1);
// SimPorter2 run states (player_states_run.cpp):
int Seq_status_535870(const World& w, int pid);
void Seq_start_535A38(World& w, int pid, Angle heading, std::uint32_t table, std::int32_t count);
int Team_nearestToLine_535B0C(const World& w, int team, std::int32_t z, int skipSlot);
void Restart_spotFromGoal_5374D8(const World& w, int side, std::int32_t dist, Vec3& out);
void Penalty_pickRunUp_536774(World& w);
void Penalty_skipRunUp_536D74(World& w, int pid);
int Player_walkOff_557A24(World& w, int pid, std::int32_t dz, std::int32_t speed, int alt);
void Player_randomPointInHalf_4BF358(World& w, int pid, KickVec& out);
void Team_startOverlap_4DA758(World& w, int team);  // 0x4DA758 (input action -> mode 2 / state 0x381)
void Team_startWideRun_4DADE8(World& w, int team);  // 0x4DADE8 (mode 3 / state 0x382)

// --- Simulation (kick.cpp) ---
int Kick_plan(World& w, int pid, std::int32_t vec[4], int mode, int coordMode, const Vec3* target, std::int32_t lift,
              std::int32_t useHigh, Fixed spin, int kind, int animClass, std::int32_t a6, int animArg); // 0x4D0CFC
void Player_executeKickDeferred(World& w, int pid, int code, std::int32_t ballY); // 0x4E3380 (untraced: pending)
void TeamAI_replanShot_4DCE94(World& w, int pid); // 0x4DCE94 AI shot re-plan at contact (team_ai_ball.cpp)

// SimPorter2 keeper (player_states_keeper.cpp; planners 0x4BF830..0x4C2970 in keeper_ai.cpp).
bool Keeper_restartGuard_4C2DD8(const World& w, int pid);   // 0x4C2DD8
bool Keeper_attackLost_4C2D80(const World& w, int pid);     // 0x4C2D80
int Keeper_sideCode_4C2658(std::int32_t code);              // 0x4C2658
bool Keeper_freeKickAgainst_4BF7D4(const World& w, int pid); // 0x4BF7D4
void Keeper_position_539FCC(World& w, int pid, Vec3& out);  // 0x539FCC
void Team_keeperTouch_4D7218(World& w, int pid, int code, int b); // 0x4D7218 commentary only (no-op)
int Keeper_planShot_4C182C(World& w, int pid, int state, Vec3* a, std::int32_t* frames, Vec3* b);
bool Keeper_catchPlan_4BFD60(World& w, int pid, Vec3* pos, std::int32_t* frames);
int Keeper_divePlan_4C0688(World& w, int pid, std::int32_t* state);
bool Keeper_rushOut_4C03F4(World& w, int pid);
int Keeper_crossPlan_4BF830(World& w, int pid, std::int32_t* t1, Vec3* p1, std::int32_t* t2, Vec3* p2, std::int32_t* anim);
bool Keeper_lowShot_4C2688(World& w, int pid, Vec3* pos, std::int32_t* frames);
int Keeper_divePlan20C_4C16D0(World& w, int pid, int state, Vec3* a, std::int32_t* frames, Vec3* b);
bool Keeper_edgeIntersect_4C2970(World& w, int pid, std::int32_t* frames, Vec3& e);

} // namespace wc98::sim
