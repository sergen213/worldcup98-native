#pragma once
// Ball (owner: Ball agent). BallState mirrors the ball object B = 0x668430 (trajectory block
// T = B+0x64) plus the ball globals around it; every field names its exe address. Player
// pointers are player ids (-1 = none). See docs/reverse/SIMULATION.md §4.
#include <cstdint>
#include "wc98/sim/fixed.hpp"

namespace wc98::sim {

struct World;

inline constexpr int kRingSize = 180; // 0xB4 trajectory frames (3 s)

// Ring entry flags (u32 at entry+0xC).
enum BallEntryFlag : std::uint32_t {
    kEntGround = 0x40,          // ground contact (bounce) this frame
    kEntOut = 0x10,             // beyond goal line / touchline
    kEntGoal = 0x30,            // goal (Ball_markGoal)
    kEntPenBox = 0x80,          // inside a penalty area
    kEntGoalArea = 0x100,       // inside a goal area
    kEntPost = 0x1000,          // post hit (Ball_resolveCollision case 1)
    kEntBar = 0x2000,           // crossbar hit (case 2)
    kEntWall = 0x8000,          // fun-mode side wall (cases 3/4)
    kEntDescending = 0x200,     // vy <= 0 after the step
    kEntCross142 = 0x400, kEntCross85 = 0x800, // crossed height 142 / 85 (+0x8000000)
    kEntApex = 0x02000000,      // vy turned from > 0 to <= 0
    kEntRolling = 0x04000000,   // rolling on the ground
    kEntInNet = 0x01000000,     // contact mode 2 (ball in the net)
};

struct BallEntry {
    Vec3 pos;                // 16.16
    std::uint32_t flags = 0;
};

struct BallState {
    // --- ball object B = 0x668430 ---
    Fixed spin = 0;              // B+0x00 0x668430 spin parameter of the last kick
    std::int32_t f04[3] = {};    // B+0x04 (zeroed by Ball_placeAt; readers untraced)
    std::int32_t f14[3] = {};    // B+0x14 (zeroed by Ball_placeAt; readers untraced)
    Vec3 vel;                    // B+0x24 0x668454 per-frame displacement (kick velocity on a kick)
    Fixed speed = 0;             // B+0x30 0x668460 octLength(vel.x, vel.z)
    std::int32_t timer34 = 0;    // B+0x34 0x668464 countdown (Ball_update)
    std::int32_t timer38 = 0;    // B+0x38 0x668468 countdown; 0 -> posI copied to posI_d78
    std::int32_t lastToucher = -1; // B+0x3C 0x66846C last player to kick/touch
    std::int32_t apexHeight = -1;  // B+0x40 0x668470 height (int) of the next apex, 0 after the last
    std::int32_t landingFrame = -1; // B+0x44 0x668474 frame of the first bounce (kick wrappers preset it)
    std::int32_t consumed = 0;   // B+0x48 0x668478 frames popped since the kick
    std::int32_t cross142Frame = -1; // B+0x4C 0x66847C first descent through y=142
    std::int32_t cross85Frame = -1;  // B+0x50 0x668480 first descent through y=85
    Vec3 framePos16;             // B+0x54 0x668484 pos16 at the start of Ball_update
    std::int32_t kickCount = 0;  // B+0x60 0x668490 ++ per Ball_kick
    // --- trajectory T = B+0x64 = 0x668494 ---
    BallEntry ring[kRingSize];   // T+0x000
    std::int32_t readIdx = 0;    // T+0xB40 playback index
    std::int32_t writeIdx = -1;  // T+0xB44
    std::int32_t freeSlots = kRingSize; // T+0xB48
    std::int32_t avail = 0;      // T+0xB4C entries ready to pop
    std::int32_t kickTime = 0;   // T+0xB50 total sim frames at the kick
    std::int32_t step = 0;       // T+0xB54 frames integrated since the kick
    std::int32_t contactTimer = 0; // T+0xB58
    std::int32_t contactMode = 0;  // T+0xB5C 2 = in net, 1 = leaving the net (counts down)
    std::int32_t netCase = 0;    // T+0xB60 which net surface holds the ball (1..8)
    Vec3 kickPos;                // T+0xB64 pos16 at the kick
    Vec3 kickVel;                // T+0xB70
    Fixed kickSpin = 0;          // T+0xB7C
    Vec3 netPos;                 // T+0xB80 net contact point
    Vec3 netPrev;                // T+0xB8C pos before the net contact
    Vec3 tvel;                   // T+0xB98 integrator velocity
    Vec3 tpos;                   // T+0xBA4 integrator position
    Vec3 tprev;                  // T+0xBB0 integrator previous position
    Fixed absX = 0, absZ = 0, absPrevX = 0, absPrevZ = 0; // T+0xBBC..0xBC8
    Fixed tspin = 0;             // T+0xBCC curve parameter (decays)
    // First-event step stamps (int16, -1 none), T+0xBD0..0xBE6:
    // [0] vx reversed, [1] vz reversed, [2] stopped, [3] (unset by traced code), [4] first bounce,
    // [5] over a goal line, [6] goal, [7] stadium wall, [8] post/bar, [9] penalty area, [10] goal
    // area, [11] first roll.
    std::int16_t stamp[12] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::int16_t apexCount = 0;    // T+0xBE8
    std::int16_t bounceCount = 0;  // T+0xBEA
    std::int16_t cross142Count = 0; // T+0xBEC
    std::int16_t cross85Count = 0;  // T+0xBEE
    std::int16_t crossCount = 0;    // T+0xBF0
    std::int16_t apexAt[12] = {};   // T+0xBF2
    std::int16_t bounceAt[12] = {}; // T+0xC0A
    std::int16_t cross142At[8] = {}; // T+0xC22
    std::int16_t cross85At[8] = {};  // T+0xC32
    std::int16_t crossAt[8] = {};    // T+0xC42
    std::int16_t tc52[9] = {};       // T+0xC52..0xC63 (unused by traced code)
    std::uint32_t trajFlags = 0;     // T+0xC64 0x6690F8: 1 at rest, 2 frozen (0x47561C path)
    // --- B+0xCCC.. ---
    std::int32_t framesThisStep = -1; // B+0xCCC 0x6690FC min(frames, 6)
    Vec3 posHist[6];             // B+0xCD0 integer position per popped frame
    Vec3 posI;                   // B+0xD18 0x669148 integer ball position
    Vec3 prevPosI;               // B+0xD24 0x669154
    Vec3 posI_d30;               // B+0xD30 0x669160 (Ball_placeAt copy chain)
    Vec3 landing;                // B+0xD3C 0x66916C predicted landing point / kick target (int)
    Vec3 posAtTouch;             // B+0xD48 0x669178 where the ball left play (x, y, z+-1)
    Vec3 cross142Pos;            // B+0xD54 0x669184
    Vec3 cross85Pos;             // B+0xD60 0x669190
    Vec3 restartPos;             // B+0xD6C 0x66919C restart spot (written by rules)
    Vec3 posI_d78;               // B+0xD78 0x6691A8 posI when timer38 is 0
    std::int32_t kickerByTeam[2] = {-1, -1}; // B+0xD84 0x6691B4[team] last kicker per team
    std::int32_t zone[2] = {};   // B+0xD8C 0x6691BC ball zone (Formation_ballZone), mirrored
    std::int32_t fkZone[2] = {}; // B+0xD94 0x6691C4 free-kick zone (rules)
    std::int32_t prevZone = 0;   // B+0xD9C 0x6691CC
    std::int32_t zoneFrames = 0; // B+0xDA0 0x6691D0 frames in the current zone
    std::int32_t radius = 5;     // B+0xDA4 0x6691D4
    Fixed rollAngle = 0;         // B+0xDA8 0x6691D8 visual roll accumulator (high word & 3)
    std::int32_t rollPhase = 0;  // B+0xDAC 0x6691DC rollAngle >> 16
    std::int32_t heading = 0;    // B+0xDB0 0x6691E0 atan2(vel)
    std::int32_t held = 0;       // B+0xDB4 0x6691E4 (cleared by Ball_init; no roll while set)
    std::int32_t bounces = 0;    // B+0xDB8 0x6691E8 bounces popped (Ball_consumeTrajectory)
    Fixed restitution = 0;       // B+0xDBC 0x6691EC (Ball_setSurface)
    Fixed bounceRetain = 0;      // B+0xDC0 0x6691F0
    Fixed rollFriction = 0;      // B+0xDC4 0x6691F4
    std::uint32_t lastFlags = 0; // B+0xDC8 0x6691F8 flags of the entry popped last
    std::int32_t possTime = 0;   // B+0xDCC 0x6691FC total frames when possession was taken
    std::int32_t outKind = 0;    // B+0xDD0 0x669200 0 none, 1 out, 2 in net (stops the held path)
    std::int32_t netKind = 0;    // B+0xDD4 0x669204 first set bit of entry flags byte 2 (1..8)
    Vec3 pos;                    // B+0xDE4 0x669214 current position 16.16
    Vec3 prevPos;                // B+0xDF0 0x669220 previous position 16.16
    Vec3 velCopy;                // B+0xDFC 0x66922C vel copy (Ball_update)
    std::int32_t dribbler = -1;  // B+0xE08 0x669238 pending kicker / dribbling player
    Vec3 dribbleTarget;          // B+0xE0C 0x66923C (int) target set by Ball_setDribblePosition
    std::int32_t kickResult = 0; // B+0xE18 0x669248 (kick wrappers write 0 / spin arg)
    std::int32_t lastKickKind = 0; // B+0xE1C 0x66924C
    std::uint32_t flags = 0;     // B+0xE20 0x669250: 1 in play, 2 goal event, 4 goal scored, 8,
                                 //   0x10 placed, 0x20 possessed, 0x40 dribble pos pending, 0x80 lob
    std::int32_t possessor = -1; // B+0xE24 0x669254
    std::int32_t prevPossessor = -1;  // 0x669258
    std::int32_t prevPossessor2 = -1; // 0x66925C
    // --- other ball globals ---
    std::int32_t lastKicker = -1;     // DAT_0065F28C (cleared by Ball_setKicker when another player kicks)
    std::uint32_t lastEntryFlags = 0; // DAT_0066BA3C flags of the last integrated entry
    Fixed net[4] = {};                // DAT_0066BA2C..38 net box bounds (FUN_00481B48)
    std::int32_t stadiumWalls = 1;    // DAT_00582E8C != 0: stand-wall handler 0x41DF38 installed
                                      //   by the stadium setup 0x41CEDF (condition untraced; on)
    Fixed flightSet[4] = {};          // DAT_00590290..9C ground-roll constants (FUN_0050605C)
    std::int32_t lob[48] = {};        // DAT_00588F38 lob table rows {vy, range, firstRoll+7, range/9}
    std::uint16_t flightTime[320] = {}; // DAT_00672A24 (Ball_buildFlightTimeTable 0x4BA91C)
    std::uint16_t spinDrift[320] = {};  // DAT_00672CA4 (FUN_004BAA5C)
};

// --- Contract used by Sim_frame / rules ---
void Ball_init(World& w);           // 0x474E58 (called from Match_init 0x4546F8)
void Ball_update(World& w);         // 0x476A80
void Ball_resetForPeriod(World& w); // 0x474EB4

// --- Ball core (ball.cpp) ---
void Ball_setSurface(World& w, int column); // 0x474DC0 (EDX = column)
void Ball_buildNetBounds(World& w);         // 0x481B48
// 0x474F78: EAX = pos (16.16 when fixed16 / EBX != 0), EDX != 0 keeps the dribble state.
void Ball_placeAt(World& w, const Vec3& p, bool fixed16, int keepDribble);
inline void Ball_placeAt(World& w, const Vec3& intPos) { Ball_placeAt(w, intPos, false, 0); }
// 0x475430: velocity (16.16 per frame), spin, frames to precompute (-1 -> 60, else <= 180).
void Ball_kick(World& w, Fixed vx, Fixed vy, Fixed vz, Fixed spin, int frames);
void Ball_integrateTrajectory(World& w, int n); // 0x4761A4
void Ball_consumeTrajectory(World& w, int n);   // 0x47576C
void Ball_takePossession(World& w, int pid, int edx); // 0x475194
void Ball_releaseFromPossessor(World& w, int pid);   // 0x47533C
void Ball_setDribblePosition(World& w, const Vec3& posI); // 0x4753DC
void Ball_setKicker(World& w, int pid, int kind); // 0x4B956C
int Ball_ringIndex(const World& w, int k);         // 0x4760B4 (-1 when not in the ring)
// 0x476154 / 0x4760E8: entry for step `frame` since the kick; rel = frame - consumed. False if
// frame == -1, rel < 0. intPos variant returns integer coordinates.
bool Ball_forecast(const World& w, int frame, Vec3* pos16, int* rel, std::uint32_t* flags);
bool Ball_forecastInt(const World& w, int frame, Vec3* posI, int* rel, std::uint32_t* flags);
bool Ball_markerInForecast(const World& w, int k); // stamp[6 + 2k] still ahead in the ring

// --- Kick velocity wrappers and solvers (flight.cpp) ---
void Ball_kickLob(World& w, int kicker, const OctDist& d);                                   // 0x4B9BC0 mode 0
void Ball_kickWithVelocity(World& w, int kicker, const Vec3& vel, Fixed spin, int kind, std::int32_t result = 0); // 0x4B9DFC mode 1 (ECX -> kickResult)
void Ball_stopDead(World& w); // 0x4BA1EC
void Ball_kickGroundPass(World& w, int kicker, const Vec3& target, std::int32_t lift, int kind); // 0x4B95E8 mode 2
void Ball_kickTimed(World& w, int kicker, const Vec3& target, std::int32_t frames, Fixed spin, int kind); // 0x4B96C0 mode 3
void Ball_kickLofted(World& w, int kicker, const Vec3& target, Fixed spin, std::int32_t useHigh, std::int32_t lift, int kind); // 0x4B9784 mode 4
void Ball_kickDriven(World& w, int kicker, const Vec3& target, std::int32_t lift, Fixed spin, int kind); // 0x4B9918 mode 5
void Ball_kickToTarget(World& w, int kicker, const OctDist& d, std::int32_t targetY, std::int32_t lift, Fixed spin, int kind); // 0x4B9A1C mode 6
void Ball_reflectOffPlayer(World& w, int pid, const Vec3& n, Fixed scale, std::int32_t result); // 0x4B9E94 (EDX n, EBX scale, ECX -> kickResult)
void Ball_pushInDirection(World& w, int pid, Angle a, Fixed speed); // 0x4BA0A4
void Kick_volleyLimits(const World& w, int kicker, Fixed* maxVy, Fixed* maxH); // 0x4B9854
// Solvers: from/to 16.16 positions, out = velocity. Return flags (1 clamped, 2 speed mismatch).
int Ball_solveGroundPass(const World& w, const Vec3& from, const Vec3& to, std::int32_t lift, Fixed limit, Vec3& out); // 0x4BA7F8
int Ball_solveTimedFlight(const World& w, const Vec3& from, const Vec3& to, Fixed spin, int frames, Vec3& out); // 0x4BA21C
int Ball_solveFlightLowArc(const World& w, const Vec3& from, const Vec3& to, Fixed spin, Fixed speed, Fixed maxVy, Vec3& out); // 0x4BA388
int Ball_solveFlightToTarget(const World& w, const Vec3& from, const Vec3& to, Fixed spin, Fixed lift, int useHigh, Fixed maxH, Fixed maxVy, Vec3& out); // 0x4BA524
int Ball_findFlightTime(const World& w, Fixed dist, Fixed speed); // 0x4BAB90
void Ball_buildFlightTimeTable(World& w); // 0x4BA91C
void Ball_buildSpinTable(World& w, Fixed drag, Fixed decay, Fixed gain); // 0x4BAA5C
void Ball_buildLobTable(World& w);        // 0x4BAC5C
void Flight_selectSet(World& w, int i);   // 0x50605C
void Ball_solveKick(World& w, Angle a, Fixed vh, Fixed vy, Fixed spin, std::int32_t out[4], std::int32_t* lift); // 0x4BA89C

// --- Forecast readers (k frames ahead; extend != 0 integrates more when needed; return 2 ok / 0) ---
int Ball_predictVelRaw(World& w, int k, int extend, Vec3* out); // 0x476BD0
int Ball_predictPosRaw(World& w, int k, int extend, Vec3* out); // 0x476CE0 (integer pos)
int Ball_predictVyRaw(World& w, int k, int extend, Fixed* out); // 0x476DAC (apex height after k)
int Ball_predictPos(World& w, int k, int extend, Vec3* out);    // 0x476E6C (k limited to the stop frame)
int Ball_predictVel(World& w, int k, int extend, Vec3* out);    // 0x476EFC
int Ball_predictVy(World& w, int k, int extend, Fixed* out);    // 0x476F8C
int Ball_firstStampAfter(const std::int16_t* arr, int count, int after); // 0x47701C

// --- Math helpers used by ball and kick code ---
std::int32_t Math_length(std::int32_t x, std::int32_t z); // 0x479570 Euclidean length via atan2 + sin
OctDist Math_vecTo(const Vec3& from, const Vec3& to);      // 0x4BAD70 {Math_length, dx, dz}
Fixed Math_sqrtFx(std::uint32_t v);                        // inline bsr/table sqrt (tables 0x5902F0/0x590374)
std::uint32_t Math_isqrt(std::uint32_t v);                 // 0x485800 integer sqrt (table 0x592A80)
std::int32_t Math_atan2_16(std::int32_t x, std::int32_t z); // 0x461A10, 0x10000 per turn
Fixed Math_sin16(std::int32_t a);                          // 0x44F300
Fixed Math_cos16(std::int32_t a);                          // 0x44F370
void Math_sinCos16(std::int32_t a, Fixed* s, Fixed* c);    // 0x460BD0

// Closed-form flight helpers (flight.cpp, integer 16.16, no x87).
Fixed Flight_dragPow(std::int32_t x);                        // 0x505CD0
Fixed Flight_dragLog(std::int32_t x);                        // 0x505D4C
Fixed Flight_groundSpeedForDistance(const World& w, Fixed dist, Fixed lift); // 0x5060E8
Fixed Flight_vyForTime(Fixed y0, Fixed y1, Fixed t);         // 0x50621C
Fixed Flight_vyForApexTime(Fixed t);                         // 0x506190 vy0 reaching the apex after t frames
Fixed Flight_rollTime(const World& w, Fixed dist, Fixed speed); // 0x506094
Fixed Flight_timeFromVy(Fixed dy);                           // 0x506148 frames to apex from vy
Fixed Flight_heightAt(Fixed y0, Fixed t);                    // 0x5061CC apex height
Fixed Flight_timeForApex(Fixed y0, Fixed apexY);             // 0x506394 (-1 when unreachable)
int Flight_rootsForHeight(Fixed y0, Fixed y1, Fixed vy, Fixed* tLow, Fixed* tHigh); // 0x5062A8

} // namespace wc98::sim
