#pragma once
// Player objects 0x65AA60 (22 x 0x26C), match attribute records 0x65E230 (0x44 each) and human
// controllers 0x664C5C (0x1A4 each) — owner: Players agent. Every struct mirrors the exe layout
// byte for byte (offsetof == exe offset is static_asserted in player.cpp); exe pointers are stored
// as indices (documented per field). Unnamed dwords keep their offset as name (fXXX).
#include <cstdint>
#include "wc98/sim/fixed.hpp"

namespace wc98::sim {

struct World;

inline constexpr int kMaxControllers = 4; // exe has 20 slots (0x664C5C); a match uses <= 4 humans
inline constexpr int kSquadMax = 22;      // attribute records per team (0x60C block, 0x44 each)

// Match attribute record, 0x65E230 + t*0x60C + i*0x44 (Team_buildMatchPlayerAttrs 0x478B08).
// Ratings (3..19 after PlayerAttr_clamp 0x478520) are bytes addressed by their record offset.
struct PlayerAttr {
    std::int32_t squadIndex;    // +0x00
    Fixed fatigueSpeed;         // +0x04 accumulator, each 1.0 lowers A[0x3A]
    Fixed fatigueStamina;       // +0x08 accumulator, each 1.5 lowers u16 A[0x18]
    std::int32_t fatigueFrames; // +0x0C (PlayerAttr_fatigueTick carry counter)
    std::int32_t f10;           // +0x10 (0xF set by 0x4BC124; zeroed by Player_setSpeedScale)
    std::int32_t f14;           // +0x14 zeroed by Player_setSpeedScale
    std::uint16_t stamina18;    // +0x18 u16 copy of DB +0x1B, lowered by fatigue (floor 4)
    std::uint16_t f1a;          // +0x1A
    std::uint16_t offsides;     // +0x1C u16 stats written by Rules: offsides (Stats_countOffside 0x4D0274)
    std::uint16_t fouls;        // +0x1E fouls committed
    std::uint16_t fouled;       // +0x20 times fouled
    std::uint16_t goals;        // +0x22
    std::uint16_t f24;          // +0x24
    std::uint16_t ownGoals;     // +0x26
    std::uint8_t r[0x1C];       // +0x28..+0x43 rating bytes, see at(); +0x40 status: 1 injured, 2 booked, 4 ?, 8|0x20 sent off
    std::uint8_t& at(int off) { return r[off - 0x28]; }
    std::uint8_t at(int off) const { return r[off - 0x28]; }
};

// Out vector of FUN_004BB1A8 {dist, dist*sin, dist*cos, angle}; Kick_plan fills dist with
// Math_length (0x479570). The kick request keeps one at +0x08.
struct KickVec {
    std::int32_t dist, dx, dz, angle;
};

// Kick request at player+0x190 (0x58 bytes, Kick_plan 0x4D0CFC / Kick_setRequestParams 0x4D0C68).
struct KickRequest {
    std::int32_t kicker;        // +0x00 kicker player ptr -> player id
    std::int32_t receiver;      // +0x04 team+0x1E0 (intended receiver, pid or -1)
    KickVec vec;                // +0x08 kick vector (Kick_plan EDX copy)
    std::int32_t mode;          // +0x18 solver 0..6 (Kick_execute jump table 0x4D0794)
    std::int32_t coordMode;     // +0x1C 1 absolute target, 0 offset from the ball
    Vec3 target;                // +0x20
    std::int32_t lift;          // +0x2C
    std::int32_t useHigh;       // +0x30
    Fixed spin;                 // +0x34 (0 = auto curl for kinds 1/3)
    std::int32_t deferredCode;  // +0x38 kick code re-aimed at contact (0x4E3380), -1 none
    std::int32_t aimAngle;      // +0x3C human aim angle kept for the re-aim, -99999 none (0x4E446C)
    std::uint16_t animId;       // +0x40 kick animation (Kick_plan)
    std::uint8_t animArg;       // +0x42 Kick_plan arg 8
    std::uint8_t r43;
    std::int32_t animOut44;     // +0x44 Anim_contactInfo out (0 when no contact frame)
    std::int32_t animHeading;   // +0x48 Anim_contactInfo out + kick heading, wrapped
    std::int16_t heading;       // +0x4C kick heading (Kick_arm passes it to the anim)
    std::int16_t contactFrame;  // +0x4E Anim_contactInfo result (-1 none)
    std::uint8_t kind;          // +0x50 1 pass 2 chip 3 cross 4 shot 9/10 header/chest 0xE back-heel
    std::uint8_t side;          // +0x51 facing-vs-target class 0..3, 4 special
    std::uint8_t r52, r53;
    std::uint32_t flags;        // +0x54 1 contact frame within clip, 2 target on the left (negates
                                //   spin), 4 pending, 8 planned, 0x10 executed
};

// Animation controller at player+0x64 (0x54 bytes; owner SpAnim, anim engine anim.cpp). Exe
// pointers as indices: seq = AISEQS record id, inst = AnimShared::inst index + 1, chooser /
// timedCb = exe code address used as an id.
struct AnimCtl {
    std::int32_t motion;        // +0x00 movement object ptr (always own player+0xB8; unused)
    std::int32_t seq;           // +0x04 current sequence record ptr -> record id + 1 (u16 id at rec+0), 0 none
    std::uint32_t chooser;      // +0x08 next-anim chooser fn (EBX of 0x4CC9BC) -> exe address, 0 none
    std::uint32_t timedCb;      // +0x0C callback fired when timedCount runs out (no writer in the exe)
    std::int32_t inst;          // +0x10 engine instance list head -> AnimShared::inst index + 1, 0 none
    std::int32_t pose;          // +0x14 = 0x6757FC pose scratch (unused)
    std::uint32_t req;          // +0x18 request flags read by Anim_selectLocomotion 0x4CC028 (bits 0-3, 0x80, 0x100, 0x800, 0x1000)
    Fixed rateReq;              // +0x1C pending instance rate (applied through 0x45FB44 at the next re-select)
    std::int16_t turnStep;      // +0x20 added to angle each frame by Anim_updateScripted 0x4CCFC0
    std::int16_t height;        // +0x22 player y (Anim_moveStep: M+0x08 = this)
    std::int16_t pitchLimit;    // +0x24 head pitch limit (init -14; Player_initMoveParams)
    std::int16_t headPitch;     // +0x26
    std::int16_t headPitchTarget; // +0x28 (used when req & 0x200, Motion_updateHeadAim)
    std::int16_t headYawTarget; // +0x2A
    std::int16_t headYaw;       // +0x2C head yaw (Motion_updateHeadAim); Anim_moveStep resets it to the body yaw
    std::int16_t timedCount;    // +0x2E frames until timedCb
    std::uint16_t stamp30;      // +0x30 totalFrames + 30 at play
    std::uint16_t stamp32;      // +0x32 totalFrames + 10: next locomotion re-select
    std::uint8_t b34[20];       // +0x34 s16 angle, +0x36 u8 type, +0x37 u8 flags, +0x39 int driftX, +0x41 int driftZ
                                // (unaligned; driftZ overlaps the low byte of the +0x44 dword, as in the exe)
    std::int32_t driftFrames;   // +0x48 frames of drift left (Anim_setDrift 0x4CD2F0)
    Fixed rateScale;            // +0x4C anim rate scale (Player_setSpeedScale 0x4BBB84)
    std::int32_t f50;           // +0x50 = 1 by Anim_init

    // +0x37 flags: 1 face-update, 2 no-drift, 4 input-block, 8 root motion, 0x10 ball at the dribble
    // bone, 0x20 re-select pending, 0x40 scripted, 0x80 synced locomotion.
    std::int32_t angle() const { return static_cast<std::int16_t>(b34[0] | (b34[1] << 8)); }
    void setAngle(std::int32_t v) { b34[0] = static_cast<std::uint8_t>(v); b34[1] = static_cast<std::uint8_t>(v >> 8); }
    std::uint8_t& type() { return b34[2]; }
    std::uint8_t type() const { return b34[2]; }
    std::uint8_t& flags() { return b34[3]; }
    std::uint8_t flags() const { return b34[3]; }
    std::int32_t get32(int off) const {
        const int i = off - 0x34;
        return static_cast<std::int32_t>(b34[i] | (b34[i + 1] << 8) | (b34[i + 2] << 16) |
                                         (static_cast<std::uint32_t>(b34[i + 3]) << 24));
    }
    void set32(int off, std::int32_t v) {
        for (int k = 0; k < 4; ++k)
            b34[off - 0x34 + k] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> (8 * k));
    }
    std::int32_t driftX() const { return get32(0x39); }
    std::int32_t driftZ() const { return get32(0x41); }
};

struct PlayerState {
    std::int32_t index;             // +0x000 index in team 0..10 (Team_initPlayers 0x451210)
    std::int32_t f004;
    std::int32_t slot;              // +0x008 base formation slot (0 = goalkeeper)
    std::int32_t curSlot;           // +0x00C current formation slot 0..16
    std::int32_t slotCopy;          // +0x010 slot copy
    std::int32_t f014;
    std::int32_t baseRole;          // +0x018 base role class 0 GK 1 SW 2 DEF 3 MID 4 FWD (0x58B028)
    std::int32_t role;              // +0x01C current role class
    std::int32_t team;              // +0x020 team block ptr -> team index 0/1
    std::int32_t dbPlayer;          // +0x024 expanded DB player ptr (0x65EF60[t*0x4D+i]) -> squad index
    std::int32_t attr;              // +0x028 match attribute record ptr -> index into World::attrs (team*kSquadMax + squad index)
    std::int32_t stateCb;           // +0x02C state handler ptr (0x589068[state>>7][state&0x7f]) -> state id, -1 = none
    std::int32_t posCb;             // +0x030 positioning callback = exe address of the callback (Formation_evalSlotCallback result)
    std::int32_t ctrl;              // +0x034 controlling controller ptr -> controller index + 1 (0 = AI)
    std::int32_t padOffX;           // +0x038 human pad move offset x (+-0xC0, Player_humanMoveTarget 0x4BC008)
    std::int32_t f03C;
    std::int32_t padOffZ;           // +0x040 human pad move offset z
    std::int32_t prevState;         // +0x044 previous state (Player_setState)
    std::int32_t state;             // +0x048 state id (table 0x589068/0x589088)
    std::int32_t stateSub;          // +0x04C state sub-step (cleared by Player_setState)
    std::int32_t stateSub2;         // +0x050 cleared by Player_setState
    std::int32_t stateTimer;        // +0x054 frames in state (+= dt)
    std::int32_t stateArg0;         // +0x058 Player_setState stack arg 1
    std::int32_t stateArg1;         // +0x05C Player_setState stack arg 2
    std::int32_t countdown;         // +0x060 -= dt, floor 0
    AnimCtl anim;                   // +0x064 animation controller (0x54 bytes, anim engine 0x45F000..0x461000)
    std::int32_t animRef;           // +0x0B8 movement object M+0x00: ptr to anim controller (always own +0x64; unused)
    Vec3 pos;                       // +0x0BC M+0x04 position 16.16
    std::int32_t animFacing;        // +0x0C8 M+0x10 anim facing
    std::int32_t animFacingDesired; // +0x0CC M+0x14 desired facing source
    std::int32_t turnAgility;       // +0x0D0 M+0x18 min(A[0x2A]>>2, 12)
    Vec3 posI;                      // +0x0D4 M+0x1C integer position (y = anim height - 0x50)
    Vec3 frac;                      // +0x0E0 M+0x28 fractional parts
    Vec3 target;                    // +0x0EC M+0x34 move target
    Vec3 altTarget;                 // +0x0F8 M+0x40 alternative target (Motion_setTargetAlt 0x4CB7FC, moveFlags 1)
    std::int32_t targetRef;         // +0x104 M+0x4C ptr to current target vec -> 0 = own +0xEC, 1 = alt +0xF8
    std::uint32_t moveFlags;        // +0x108 M+0x50 8 new target/warped, 1, 4
    std::int32_t targetDist;        // +0x10C M+0x54 octagonal distance to target
    std::int32_t targetDx;          // +0x110 M+0x58
    std::int32_t targetDz;          // +0x114 M+0x5C
    std::int32_t targetAngle;       // +0x118 M+0x60 cached heading to target (Motion_dirToTarget 0x4CB9F8, moveFlags 4)
    std::int32_t distBall;          // +0x11C M+0x64 octagonal distance to ball
    std::int32_t dxBall;            // +0x120 M+0x68
    std::int32_t dzBall;            // +0x124 M+0x6C
    std::int32_t angleBall;         // +0x128 M+0x70 angle to ball
    std::int32_t distBallPrev;      // +0x12C M+0x74 copy of +0x11C
    std::int32_t facing;            // +0x130 M+0x78 facing (stored -0x200..0x1ff)
    std::int32_t facingDesired;     // +0x134 M+0x7C desired facing
    std::int32_t turnDelta;         // +0x138 M+0x80 (zeroed by Motion_applyTurnAndRootPos)
    std::int32_t headAim;           // +0x13C M+0x84
    std::int32_t dir8;              // +0x140 M+0x88 ((facing+0x40)&0x3ff)>>7
    std::int32_t turnAmount;        // +0x144 M+0x8C
    std::int32_t turnClass;         // +0x148 M+0x90 0..2
    Fixed speed;                    // +0x14C M+0x94 current speed 16.16
    std::int32_t m98;               // +0x150 M+0x98 zeroed with speed
    std::int32_t m9c;               // +0x154 M+0x9C zeroed with speed
    std::int32_t velAngle;          // +0x158 M+0xA0 velocity heading (0x4BB1A8)
    Fixed speedCap;                 // +0x15C M+0xA4 current max-speed cap
    Fixed sprintMax;                // +0x160 M+0xA8 run + sprint bonus
    Fixed runMax;                   // +0x164 M+0xAC run max (0x590590 / keeper 0x5908B0)
    Fixed mB0;                      // +0x168 M+0xB0 (26 - A[0x32]) / 32
    Fixed decel;                    // +0x16C M+0xB4
    Fixed accel;                    // +0x170 M+0xB8 0x590540[A[0x2F]]
    Fixed sprintAccel;              // +0x174 M+0xBC sprint bonus / 2
    Fixed accelRecip;               // +0x178 M+0xC0 2^32 / accel
    Fixed sprintAccelRecip;         // +0x17C M+0xC4
    Fixed decelRecip;               // +0x180 M+0xC8
    std::uint32_t moveReq;          // +0x184 M+0xCC motion request flags (rebuilt each frame by 0x4BD104/0x4BF3CC)
    std::uint32_t moveStatus;       // +0x188 M+0xD0 motion result flags (Motion_update)
    std::int32_t arrivalCb;         // +0x18C M+0xD4 arrival callback ptr -> id (0 = none)
    KickRequest kick;               // +0x190 kick request (Kick_plan 0x4D0CFC)
    std::uint32_t pendingCode;      // +0x1E8 u16 +0x1E8 pending kick code / skill anim (0xffff none) | u16 +0x1EA << 16
    Fixed kickPower;                // +0x1EC Player_setKickPowerFromHold 0x4E26D4
    std::int32_t f1F0;
    std::int32_t f1F4;
    std::int32_t f1F8;
    std::int32_t f1FC;
    std::int32_t timer200;          // +0x200 u16 frame timer
    std::int32_t f204;              // +0x204 init 0x80000000
    Vec3 runTarget;                 // +0x208 run/support target
    std::int32_t markSpot;          // +0x214 ptr to own team +0x248 + k*0xC (TeamState::kickPos, set by 0x4D2644) -> k + 1, 0 = null
    std::int32_t f218;
    std::int32_t f21C;
    std::int32_t f220;
    std::int32_t f224;
    std::int32_t f228;
    std::uint32_t flags22c;         // +0x22C state flags (init (v&0xff9b9f70)|0x80030); byte 0x22D = bits 8..15
    std::int32_t f230;
    std::int32_t f234;
    std::int32_t f238;
    std::int32_t f23C;
    std::int32_t f240;
    std::int32_t f244;
    std::int32_t f248;              // +0x248 Player_frameMove 0x4BC124 result (intercept player ptr -> id, -1 none)
    std::int32_t markTarget;        // +0x24C marked opponent (player ptr, set by 0x4D3747) -> pid + 1, 0 = none
    std::int32_t markedBy;          // +0x250 player marking this one -> pid + 1, 0 = none
    std::int32_t f254;
    std::int32_t f258;
    std::int32_t f25C;
    std::int32_t f260;
    std::int32_t f264;
    std::int32_t f268;
};

// Gesture state machine of one button (Input_buttonGesture 0x4E1690), 0x18 bytes at
// controller + 0x5C + b*0x18. The snapshot part (+0x0C) is what action handlers receive (EDX).
struct GestureSnap {
    std::uint32_t event;        // +0x0C byte: last event 0..15 (0xFF after bind); upper bytes stay 0
    std::int32_t lastFrames;    // +0x10 frames in the previous step (hold length for releases)
    std::int32_t prevCount;     // +0x14 tap count when the event fired
};
struct GestureButton {
    std::uint32_t state;        // +0x00 byte: state 0..12 (12 = wait for release after a context switch)
    std::int32_t frames;        // +0x04 frames in the current step (+= dt)
    std::int32_t count;         // +0x08 taps in the current sequence
    GestureSnap snap;           // +0x0C
};

// One human pad (controller 0x664C5C + i*0x1A4) plus the raw pad word the platform layer feeds
// (padBits = the word Input_readPadBits 0x4664F8 assembles from the device record: bits 0-3 dpad
// 1 up 2 down 4 right 8 left, bits 4-13 buttons (0x10 lob, 0x20 pass, 0x40 shoot, 0x80 L1,
// 0x100 sprint, 0x200 R1, 0x400 L2, 0x800 R2, 0x1000 Space -> moved to 0x20000, 0x2000 pause),
// bits 14-15 dpad orientation (remap 0x580D8E), bits 16-23 keyboard extra keys (Z X C V F R ->
// 0x40000 .. 0x800000)). Owner: SpInput (input.cpp).
struct ControllerState {
    std::int32_t index;             // +0x000 controller index (Input_initControllers 0x466D84)
    std::int32_t dir;               // +0x004 direction index 0..8 of the remapped dpad (0x580D6C)
    std::int32_t rawDir;            // +0x008 direction index of the raw dpad (+0x40)
    std::int32_t devType;           // +0x00C device type 0..8 (button masks 0x58BAE8, contexts 0x58C648)
    std::int32_t dirX, dirZ;        // +0x010 / +0x014 remapped dpad vector -1..1 (0x580D7C / 0x580D85)
    std::int32_t rawDirX, rawDirZ;  // +0x018 / +0x01C raw dpad vector
    std::int32_t analogX;           // +0x020 analog x (only with an analog reader)
    std::int32_t analogZ;           // +0x024 analog z
    std::int32_t analogMag;         // +0x028 analog magnitude (deadzone 0x2001)
    std::uint32_t analogAngle;      // +0x02C u16 +0x2C relative angle / u16 +0x2E angle - 0x100
    std::int32_t analogDevice;      // +0x030 analog device flag (always 0 on PC: reader 0x580D64 reports no analog)
    std::int32_t holdFrames;        // +0x034 frames any button is held (+= dt while < 250)
    std::int32_t f038;              // +0x038 copied to the new player by Player_transferControl 0x452884
    std::uint32_t bits;             // +0x03C current bits (remapped dpad | buttons)
    std::uint32_t raw;              // +0x040 Input_readPadBits result (raw dpad, orientation)
    std::int32_t f044;              // +0x044 cleared by Input_clearEdges 0x466888
    std::uint32_t pressed;          // +0x048 pressed edges (button mask 0x3FE2FF0)
    std::uint32_t released;         // +0x04C released edges
    std::int32_t f050;              // +0x050 cleared by Input_clearEdges 0x466888
    std::int32_t f054;
    std::int32_t lastAction;        // +0x058 gesture block G+0: last action byte (0 none)
    GestureButton buttons[8];       // +0x05C G+0x04
    std::int32_t f11C, f120, f124, f128, f12C, f130, f134, f138, f13C, f140, f144, f148;
    std::int32_t maskDev;           // +0x14C G+0xF4 button-mask row ptr 0x58BAE8 + dev*0x60 -> dev
    std::int32_t gestureCtx;        // +0x150 G+0xF8 context entry ptr 0x58C648 + dev*0xA8 + ctx*0xC -> ctx (-1 null)
    // WC98's controller record (stride 0x1A4 @ 0x664C5C) has no FIFA-99 queue FIFO at +0x154..
    // +0x19B: the pad path drops gated actions outright (register D-67), and the exe exposes no
    // writer for this region; it stays reserved to keep the layout pins (player = +0x19C). The three
    // analog-feed slots inside the gap carry the stick reader stub's raw values (D-57).
    std::int32_t f154[15];          // +0x154..+0x18F reserved gap
    std::int32_t stickMag;          // +0x190 raw stick magnitude (gate < 0x2001 zeroes the vector)
    std::int32_t stickAngle;        // +0x194 raw stick angle, 10-bit
    std::int32_t stickPrevAngle;    // +0x198 previous angle for the +0x2C relative delta
    std::int32_t player;            // +0x19C controlled player ptr -> player id + 1 (0 = inactive)
    std::uint32_t flags1a0;         // +0x1A0 options: 2 skill contexts 1/2, 8 chip shots in the box, 0x10 IGT 1-2
    std::uint32_t padBits;          // platform input (not in the exe struct)
};

// Set-piece aim block 0x632F20.. (set-piece module 0x44C00C..0x44D3xx): written by the set-piece
// setup 0x44C3E0, the aim update 0x44C7F4 and SpInput's set-piece actions (w.pl.sp).
struct SetPieceAim {
    std::int32_t f20;           // DAT_00632F20 (set 0 by 0x44C3E0 / 0x44C5D4)
    std::int32_t arg;           // DAT_00632F24 0x44C3E0 EDX; angle given to Ball_solveKick (0x44C6D0)
    std::int32_t spin;          // DAT_00632F28 aim curl -0xAB..0xAB (0x44C7F4)
    std::int32_t loft;          // DAT_00632F2C loft angle (0x1C, 0x2B on corners)
    Fixed power;                // DAT_00632F30 power (0x4CCC)
    std::int32_t f34;           // DAT_00632F34
    std::int32_t powerRef;      // DAT_00632F38 ptr to a live gesture hold counter -> gesture ref (-1 null, see input.cpp)
};

// Per-team goalkeeper block 0x6734F0 + t*0x144 (init 0x4BEC3D; SpStates keeper.cpp, SimPlayers
// keeper per-frame 0x4BF3CC.., SpInput). Unnamed dwords keep their block offset as name.
struct KeeperBlock {
    std::int32_t ctrl;          // +0x000 controller taking keeper control (index + 1, 0 none; SpInput 0x4E0E24)
    std::int32_t k004, k008, k00c;
    std::int32_t k010;          // +0x010 init -1
    std::int32_t diveState;     // +0x014 keeper dive state id (0x20C..0x22F / 0x206..)
    std::uint32_t k018;         // +0x018 byte
    std::int32_t kickSeen;      // +0x01C ball kickCount (DAT_00668490) when the keeper last evaluated the shot (State209 0x53B1F5)
    std::int32_t kickSeenFlag;  // +0x020 1 = reaction planned for kickSeen, 0 = not reachable (State209 0x53B1FC/0x53B342)
    std::int32_t k024[31];      // +0x024..+0x09F
    std::int32_t plan[3][11];   // +0x0A0 + (state-0x209)*0x2C (states 0x209..0x20B, 0x4C1935)
    std::int32_t k124, k128, k12c, k130, k134, k138, k13c;
    std::int32_t busy;          // +0x140 busy flag (read by SpInput action 65)
};
static_assert(sizeof(KeeperBlock) == 0x144);

// Player-subsystem globals.
struct PlayerShared {
    std::int32_t current;       // DAT_0065f2a4 player being updated (id, -1 none)
    std::uint8_t lineup[2][0x2c]; // 0x66E564 + t*0x2C: squad index per pitch index (3 copies at
                                  // +0x00/+0x0B/+0x16, FUN_0049FA9C), +0x22.. counters (Team side)
    // Pair cache 0x676604 + i*0x10 {angle, dist, distFrame, angleFrame}, i = 22a + b - (a+1)(a+2)/2
    // (a <= b player ids, FUN_004D9F84); stored at [i + 1] because a == b == 0 maps to -1.
    std::int32_t pairCache[232][4];
    KeeperBlock keeper[2];         // 0x6734F0 + t*0x144
    // One-touch receive plan per team (State302 0x53E630): meeting point, ball frame, kick anim length.
    Vec3 oneTouchPos[2];           // DAT_006A3978 + t*0xC
    std::int32_t oneTouchFrame[2]; // DAT_006A3990 + t*4 ball frame (trajectory step) to meet the ball
    std::int32_t oneTouchLead[2];  // DAT_006A3998 + t*4 kick anim contact frame (Anim_markFrame0)
    std::int32_t passZone;         // DAT_006775E0 executeKick <-> Kick_findTarget: -1/0 in, 1/2 = shoot instead
    std::int32_t kickDeferred;     // DAT_0058CEE8 1 only inside Player_executeKickDeferred 0x4E3380
    // Tackle move table 0x58B340 (12 x 0x2C bytes): static bytes from the exe + anim-derived words
    // filled by Tackle_buildMoves 0x4D18E0 (kick.cpp has the layout).
    std::uint8_t tackleMoves[12][0x2c];
    SetPieceAim sp;                // 0x632F20.. set-piece aim block
    // Skill-move table 0x58B6EC (10 x 0x1C): {anim, dist(mark0, bone), angle, dist(mark0, root), angle,
    // frames, u32}; static rows from the exe, anim/dist/angle/frames rebuilt by SkillMoves_build 0x4E073C.
    std::int32_t skillMoves[10][7];
    // Shot-setup globals reset when leaving state 0x308 (0x4BBB3C); DAT_00673210 is FrameTime::timer673210.
    std::int32_t g673208, g67320c, g673214, g673218, g67321c, g6731f4;
    std::int32_t dribbleVec[4];    // DAT_006731F8..0x673204 {len, x, z, angle} human dribble push (State308 0x540658, Player_dribbleVector 0x4BDCAC)
    // --- SimPorter2 --- (player state handlers 0x18x / 0x20x)
    std::int32_t spWait185 = 0, spWait182 = 0; // DAT_0059F324 / 0x59F328 set-piece auto-take deadline (elapsedSec + 0x1E)
    std::int32_t spWait183 = 0, spWait188 = 0; // DAT_0059F33C / 0x59F370
    Vec3 subWalkPos;                           // DAT_006A3960 substitution walk-back point (State186)
    Vec3 takerSpot;                            // DAT_006A396C set-piece taker spot (State183)
    std::int32_t k201Walk = 0;                 // DAT_0059F464 State201 distance walked holding the ball
    Vec3 k201Last;                             // DAT_0059F46C State201 last position (0x59F468 frame stamp: never read)
    std::int32_t k201Deadline = 0;             // DAT_0059F478 State201 elapsedSec deadline
};

// Contract used by Sim_frame (sim.cpp):
void Players_animate(World& w, Fixed simDt);   // FUN_004CC8F4 -> anim engine FUN_0045F7D8
void Input_updateControllers(World& w);        // 0x466D40
void SetPiece_updateAim(World& w);             // FUN_0044C00C
void Team_updatePlayers(World& w, int team);   // FUN_00453A58 -> 0x4BD104 outfield / 0x4BF3CC keeper
void Team_applyFatigue(World& w, int team);    // 0x451BB4 (every 5 game minutes)

// Attributes / setup.
void Players_buildSpeedTables(World& w);                      // FUN_004CFF94 (Match_init 0x4547C7)
void Team_initPlayers(World& w, int team, int playerCount);   // 0x451210 (Match_init 0x4547D4/0x4547E4)
void PlayerAttr_clamp(PlayerAttr& a);                         // 0x478520
void Team_buildMatchPlayerAttrs(World& w, int team, bool fresh); // 0x478B08 (EDX = fresh)
bool PlayerAttr_fatigueTick(World& w, PlayerAttr& a, bool edx); // 0x479028
void PlayerAttr_setInjured(PlayerAttr& a);                    // 0x479268
Fixed PlayerAttr_runSpeed(const PlayerAttr& a);               // 0x47927C
bool PlayerAttr_tickTimers(World& w, PlayerAttr& a, std::int32_t edx); // 0x47914C
bool Player_isKeeperInOwnBox(const World& w, int pid);        // 0x4BE048
void Player_initMoveParams(World& w, int pid);                // 0x4BBBF8
void Player_setSpeedScale(World& w, int pid);                 // 0x4BBB84
void Player_injure(World& w, int pid);                        // 0x4BD998
void Player_bindSquadMember(World& w, int team, int k, int squadIdx); // FUN_0049FA9C
// Cross-subsystem accessors (Ball: Kick_volleyLimits / Ball_kickToTarget).
std::uint8_t Player_attr(const World& w, int pid, int off);   // (player+0x28)->byte[off]
std::uint8_t Player_dbByte(const World& w, int team, int squad, int off); // expanded DB player byte (0x65EF60)
int Player_animId(const World& w, int pid);                   // *(u16*)[player+0x68]

// State machine / per-frame update.
void Player_setState(World& w, int pid, int state, std::int32_t arg0, std::int32_t arg1, bool force = false,
                     bool run = false); // 0x4BDCD8 (EDX = state, ECX = force, EBX = run handler now, stack args)
void Player_reset(World& w, int pid, const Vec3* pos, int cbIndex, bool faceBall); // 0x4BBA2C (EDX pos, EBX cb, ECX)
void Player_clearArrivalCb(World& w, int pid);                // 0x4BBD78
void Player_updateSpeedCap(World& w, int pid);                // 0x4BBD84
void Player_requestSprint(World& w, int pid);                 // 0x4BBF28
void Player_dropBallAndReset(World& w, int pid);              // 0x4BDED0
void Player_moveToward_4BE804(World& w, int pid, const Vec3& v); // 0x4BE804
void Player_lineUp_4BCDE0(World& w, int pid);                 // 0x4BCDE0
// 0x4BC124 (EBX = head-aim mode = flags22c bit 4, ECX = speed-cap mode = bit 5, stack = keeper/intercept check).
std::int32_t Player_frameMove(World& w, int pid, int ebx, int ecx, int keeperCheck);
// 0x542EA0 (SpStates.SpAction): intercept target for the player being updated (DAT_0065F2A4).
bool Player_interceptTarget_542EA0(World& w, int pid, Vec3& out, std::int32_t& res);
void Player_onBallLost(World& w, int pid);                    // 0x4BF708 (Ball code)
void Keeper_reactToBall(World& w, int pid);                   // 0x4BF5A4 (Ball code)
void Keeper_release_4BF29C(World& w, int pid);                // 0x4BF29C
void Keeper_dropPossession_4BF788(World& w, int pid);         // 0x4BF788
std::int32_t Player_cachedDistance(World& w, int a, int b);   // 0x4D9FFC
Angle Player_cachedAngle(World& w, int from, int to);         // 0x4DA094
// Predicates on state 0x100 sub-steps (table 0x58B35A + stateSub*0x2C), used by Rules 0x46FAAC.
bool Player_isState100Kind0(const World& w, int pid);         // 0x4D250C
bool Player_isState100Kind1(const World& w, int pid);         // 0x4D2578
bool Player_isState100Kind2(const World& w, int pid);         // 0x4D2540

// External callees of Team_initPlayers / Player_setState awaiting their owners' headers
// (identical redeclarations are harmless; delete once team.hpp / SpKick's section has them).
void Player_assignControlToReceiver(World& w, int pid, int except); // SpKick 0x4528C4 (except -1 = null)

// Movement object (player+0xB8).
void Motion_init(World& w, int pid, const Vec3& pos);         // 0x4CB47C
const Vec3& Motion_targetVec(const World& w, int pid);        // *(player+0x104)
bool Player_arrivalCallback(World& w, int pid, std::int32_t cb); // call through M+0xD4 (cb = exe address)
void Motion_setTarget(World& w, int pid, const Vec3& t);      // 0x4CB778
bool Motion_setTargetAlt(World& w, int pid, const Vec3& t);   // 0x4CB7FC (alt target +0x40, flag 1)
void Motion_warpTo(World& w, int pid, const Vec3* pos); // WC98 FUN_004f4f80 (0x4CB904); no facing handling (D-60)
void Motion_snapTo(World& w, int pid, const Vec3& pos);       // 0x4CB884
Angle Motion_dirToTarget(World& w, int pid);                  // 0x4CB9F8
void Motion_update(World& w, int pid, Fixed dt);              // 0x4CB510
void Motion_applyTurnAndRootPos(World& w, int pid, Fixed dt, int ebx); // 0x4CB1E8
// 0x4CBA60 (EAX = M, EDX = speed, EBX = heading, ECX = frames, stack: sprint, ignoreTurnClass).
std::int32_t Motion_predictDistance(const World& w, int pid, Fixed speed, Angle heading, std::int32_t frames,
                                    int sprint, int ignoreTurnClass);

// --- SpStates ---
// Player state handlers: table 0x589068[state >> 7][state & 0x7F] (player_states.cpp), flags
// 0x589088 (bit 0 -> player+0x22C bit 9 in Player_setState). nullptr / 0 outside the table.
using StateFn = void (*)(World&, int pid);
StateFn Player_stateHandler(int state);
std::uint8_t Player_stateFlags(int state);
// Keeper block / save code (SpKeeper, keeper_save.cpp).
int Player_blockBall(World& w, int pid, int deflect);        // 0x543390 (EDX = deflect): 1 if the ball was blocked/caught
void Keeper_holdOff(World& w, std::int32_t frames);          // 0x543370 keeperHoldUntil = max(old, totalFrames + frames)
std::int32_t Math_asin1024(Fixed x);                         // 0x5432B0 asin(x) in 1/1024 turn (-0x100..0x100), table 0x5A04CC
std::int32_t Math_asinRatio(std::int32_t a, std::int32_t b); // 0x543280 asin(a / b), clamped to +-0x100
std::int32_t Math_acos1024(Fixed x);                         // 0x543350 0x100 - asin(x)
// Player helpers ported by SpStates (player_states_outfield.cpp).
void Player_humanMoveTarget(World& w, int pid, Vec3& out); // 0x4BC008 human pad move target (pos + pad offset, clamped)
void Player_supportTarget(World& w, int pid, Vec3& out);   // 0x4BCBE0 base-slot target pushed +-0x1B0, z clamped
void Player_adjustRunTarget(World& w, int pid, const Vec3& in, Vec3& out); // 0x4BCC80 EDX in, EBX out (may alias): offside-line hold
void Player_offBallRun(World& w, int pid);                 // 0x532DA0 AI off-ball run step machine
int Player_avoidPlayer(World& w, int pid, int other, const Vec3& target, Vec3& out); // 0x532900
void SkillMoves_build(World& w); // 0x4E073C (Match_init 0x454804): resolve skill anims of table 0x58B6EC + contact offsets

// --- SpAnim --- (engine + player wrappers: anim.hpp / anim.cpp)
void Anim_init(World& w, int pid, bool fresh);                // 0x4CC918 (EDX = fresh)
void Anim_moveStep(World& w, int pid);                        // 0x4CCD80 (EAX = player+0x64, EDX = M)
Fixed Anim_rootHeight(World& w, int pid);                     // 0x4607CC on the controller's instance
Vec3 Anim_ballBonePos(World& w, int pid);                     // 0x460778 (EBX = 0x10000), 16.16
void Motion_updateHeadAim(World& w, int pid, std::int32_t ebx, std::int32_t ecx = 0); // 0x4CD00C
std::int32_t Motion_rootMotionPre(World& w, int pid, Fixed dt);             // 0x4CAB40 -> EBX of 0x4CAED0
void Motion_rootMotionStep(World& w, int pid, Fixed dt, std::int32_t ebx);  // 0x4CAED0
void Motion_setVelocity(World& w, int pid, Fixed speed, Angle a);           // 0x4BB1A8
// Implemented elsewhere, called by the anim engine:
int Player_onAnimContact(World& w);                           // 0x4BDF38 (kick.cpp; via [0x675A08])
// Next-anim chooser stored in AnimCtl::chooser (exe address, e.g. 0x535894 / 0x537F98 / 0x53C62C;
// called EAX = current anim, EDX = sequence next id, EBX = &out). Returns the next anim id and
// writes the chooser for it to *nextChooser. Implemented with the state handlers (SpStates).
int Player_animChooser(World& w, int pid, std::uint32_t chooser, int curAnim, int nextAnim,
                       std::uint32_t* nextChooser);

// --- SpInput (input.cpp) ---
// Gesture refs: handlers get the gesture snapshot the exe passes in EDX as a ref = controller*16 +
// slot (slot 0..7 = buttons[slot].snap; WC98 has no queueSnap slots, register D-67); -1 = none.
GestureSnap* Input_snap(World& w, int ref);
std::uint32_t Input_readPadBits(std::uint32_t padBits);                  // 0x4664F8 sim part: Space 0x1000 -> 0x20000
std::uint32_t Input_analogDecode(World& w, int c);                       // 0x4A9280 analog branch (D-57)
int Input_dirToVector(std::uint32_t bits, std::int32_t* x, std::int32_t* z); // 0x466860 (tables 0x580D6C/7C/85)
void Input_clearEdges(World& w, int c);                                  // 0x466888
void Input_processController(World& w, int c, Fixed dt);                 // 0x4668AC
void Input_initControllers(World& w);                                    // 0x466D84
// 0x4554C0 (Match_run 0x40A6ED via 0x466600): bind controller c to the team (0/1; other = unbind)
// with device type devType (button masks 0x58BAE8 row).
void Input_assignController(World& w, int c, int team, int devType);
void Player_requestKick(World& w, int pid, int code);                    // 0x4E28CC (EDX code)
void Input_bindGestureTables(World& w, int c, int ctx, int devType);     // 0x4E0DAC
void Input_selectContext(World& w, int pid, Fixed dt, std::uint32_t buttons); // 0x4E0E24 per controlled player
void Input_setGestureContext(World& w, int c, int ctx);                  // 0x4E1598 (ctx -1 = none)
int Input_buttonGesture(World& w, int c, Fixed dt, std::uint32_t buttons, int* ref); // 0x4E1690 -> action
void Input_dispatchAction(World& w, int c, int action, int ref);         // 0x4E330C
void Input_runAction(World& w, int pid, int action, int ref);            // handler table 0x58CCF8[action]
bool Player_inShootingZone(const World& w, int pid);                     // 0x4E19F8
int Player_switchControl(World& w, int pid, int manual);                 // 0x4BD754: 0 owner, 1, 2 = picked itself
void Player_transferControl(World& w, int from, int to);                 // 0x452884
int Player_selectReceiver(World& w, const Vec3& origin, int pid, int team, Angle angle, int type,
                          std::int32_t dist);                            // 0x4BD4B8 (-1 none)
int Team_nearestFree_451F4C(const World& w, const Vec3& pos, int team, int excludeSlot);   // 0x451F4C
int Team_nearestPlayer_452018(const World& w, const Vec3& pos, int team, int excludeSlot, int ctrlSlot,
                              int needOutfield);                                         // 0x452018
int Team_bestFacing_452100(const World& w, int team, int excludeSlot);                   // 0x452100
int Team_nearestUncontrolled_4521EC(const World& w, const Vec3& pos, int team, int excludeSlot); // 0x4521EC
int Team_nearestPlayer_4522A0(const World& w, const Vec3& pos, int team, int ctrlSlot, int needOutfield); // 0x4522A0
int Team_nearestPlayer_4522B0(const World& w, const Vec3& pos, int team, int excludeSlot);  // 0x4522B0
int Team_nearestPlayer_4522D8(const World& w, const Vec3& pos, int team, int excludeSlot);  // 0x4522D8
int Team_findKeeper(const World& w, int team);                           // 0x45299C (-1 none)
void SetPiece_cycleMode(World& w, int pid);                              // 0x44C5D4
void SetPiece_takeAim_44C560(World& w, int pid);                         // 0x44C560
void SetPiece_begin(World& w, int pid, Angle a, int flag);               // 0x44C3E0 (EDX angle, EBX flag)
void SetPiece_reset(World& w);                                           // 0x44C5B4
void SetPiece_computeKick(World& w, KickVec* rel, std::int32_t* lift, Fixed* spin); // 0x44C6D0

} // namespace wc98::sim
