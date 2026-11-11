#pragma once
// Animation engine of the EA Gen3 match sim (FIFA99.EXE v1.1 disassembly, ported to World Cup 98;
// GHIDRA_PASS5_IG.md confirms the same architecture). All player movement is root motion read from
// ANIM.MO clips through the AISEQS.TBI sequence table. The engine (0x45F000..0x461000, state block
// 0x65FFAC) plays clip *instances*; a player's AnimCtl (player+0x64, player.hpp) owns a short list of them.
// The sim only reads key channels 0..8 of a clip (stored raw in every key block, compressed or not):
//   ch0 foot flags, ch1 yaw (1024/turn), ch2 gait phase, ch3..5 root x/y/z, ch6..8 ball bone x/y/z.
// Clip header (u16 unless noted): +2 header size, +6 key count, +8/+10/+12 i16 marks (event
// ticks, -1 none), +0xE flags (bit 0: 4 ticks/key else 2, bit 15 raw keys), +0x10 channel count.
// Engine time is 8.8 frames: a key spans 0x200 << (flags & 1); positions are key << 12 (16.16).
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

 #include "wc98/sim/fixed.hpp"
#include "wc98/formats/anim.hpp"
namespace wc98::sim {

struct World;
struct MatchData;

// ---- loaded data (MatchData::anim, read-only) ----

// AISEQS.TBI name table entry (0x18 bytes at file +u32[1]: char name[10], u16 index, u8 +0xD
// flags 0x80 = not loaded, i32 +0x10 = -1 not loaded / 0 loaded, +0x14 clip ptr). The clip is the
// ANIM.MO member of that name (case-insensitive; WC98 layout delta in SIM_PORT_NOTES.md section 5).
struct AnimClip {
    std::string name;
    bool loaded = false;           // !(flags & 0x80) && slot != -1  (0x500AAC)
    std::uint16_t keyCount = 0;    // clip +6
    std::uint16_t flags = 0;       // clip +0xE
    std::int16_t marks[3] = {-1, -1, -1}; // clip +8, +10, +12
    std::vector<std::array<std::int16_t, 9>> keys; // channels 0..8 of every key block
    // WC98 adaptation (SIM_PORT_NOTES.md section 5): the formats parser's full-channel decode.
    // The root-motion engine above reads only ch0..8; Anim_evalSkeleton (keeper body capsules)
    // FK-evaluates the triplet/quaternion channels through wc98::formats::PlayerRig.
    std::shared_ptr<formats::AnimClip> fmt;
};

// AISEQS.TBI record (0x14 bytes from file +8, u32[0] = 0x1C4 records): the "anim id" every
// state handler plays.
struct AnimSeq {
    std::uint16_t id = 0;          // +0x00
    std::uint16_t next = 0;        // +0x02 follow-up anim (0x4CC408)
    std::uint16_t fallback = 0;    // +0x04 used when the clip is missing (0x4CC984)
    std::int16_t angle = 0;        // +0x06 start yaw offset (1024/turn)
    std::int32_t clip = -1;        // +0x08 name ptr -> name entry (index into AnimData::clips), -1 none
    std::uint16_t blendIn = 0;     // +0x0C crossfade frames (lowered at load by 0x4CBDD4)
    std::uint16_t blendOut = 0;    // +0x0E
    std::uint32_t flags = 0;       // +0x10: 1 locomotion (re-selected by 0x4CC028), 2/0x10/1/4 -> ctl +0x37
                                   // bits 0/2/1/3, 8 keep yaw, 0x40 has marks (set at load) / sync callback,
                                   // 0x80 always valid, 0x8000.. >> 15 = instance flags (1 mirror, 4 loop)
};

struct AnimData {
    std::vector<AnimSeq> seqs;      // 0x1C4
    std::vector<AnimClip> clips;    // AISEQS name table order
    // Locomotion tables, exe values resolved at load by 0x4CBEA0 (copied in anim.cpp, exe address there).
    std::uint8_t gaitType[14][18] = {};   // 0x59F81C anim type by speed (rows 11..13: 0x59F8E2/F4/906 turning)
    std::uint16_t locoBlock[264] = {};   // WC98 DAT_00591698..DAT_005918A8 (D-63 canonical): flat u16
                                        // {id, rate<<5} pair view — header trio @+0x0, central loco rows
                                        // stride 0x2C @+0xC [reqClass][gaitType col], turnL grid u16 ids
                                        // (row stride 0x16) @+0xE8, turnR grid @+0x156. The exe builds the
                                        // live tables at runtime via FUN_004FC2C0 (D-61); static dump =
                                        // kLoco + kTurnL + kTurnR contiguous (anim_tables.inc).
    std::uint16_t skillA[6][11] = {};     // 0x59FB28 (Anim_trySkill, 0x1C4 none)
    std::uint16_t skillB[6][11] = {};     // 0x59FBAC (req bit 0x800)
    // WC98 adaptation: the PCMD player rig (skeleton offsets + hierarchy) for Anim_evalSkeleton,
    // loaded from INGAME/PLAYER/MODELS/HIG_30.PC by loadMatchAnimData.
    formats::PlayerRig rig{};
    bool rigLoaded = false;
};

// Reads COMMON/AISEQS.TBI and the COMMON/ANIM.MO BIGF under gameDir (case-insensitive) into d.anim,
// loads the PCMD player rig from INGAME/PLAYER/MODELS/HIG_30.PC, and runs the load-time passes
// 0x4CBD9C / 0x45F7E8 / 0x45F8C0 / 0x4CBDD4 / 0x4CBEA0. False on failure (SIM_PORT_NOTES.md section 5).
bool loadMatchAnimData(MatchData& d, const std::filesystem::path& gameDir);
bool parseAnimSeqTable(AnimData& a, const std::vector<std::uint8_t>& tbi); // layout only (no clips)

// ---- engine state (World::anim, hashed) ----

inline constexpr int kAnimInstances = 0x45; // 0x4CC6B0 -> 0x45F710(0x45)

// Engine instance (0x438 bytes in the exe pool; only the fields the simulation reads are kept —
// +0xAC..+0x437 is the per-instance key decode cache 0x5034F0 and pose data, results identical).
struct AnimInstance {
    std::int32_t next;      // +0x00 newer instance -> index + 1 (0 none)
    std::int32_t prev;      // +0x04 older (fading out) instance / free list link -> index + 1
    std::int32_t headYaw;   // +0x08 (0x45FB3C, renderer)
    std::int32_t headPitch; // +0x0C (0x45FB40, renderer)
    std::int32_t f10, f14;
    std::uint32_t cache;    // +0x18 1 root, 2 bone, 4 yaw, 8 keys valid
    std::int32_t f1c;       // +0x1C
    std::uint32_t flags;    // +0x20 1 mirror, 4 loop, 0x10 ended, 0x20 fading-out partner, 0x40 fading out,
                            // 0x80 fading in, 0x200/0x400 lock x/z, 0x1000 fixed yaw, 0x2000 no mark snap,
                            // 0x4000|0x10000 mark0 passed, 0x8000|0x40000 mark1 passed, 0x20000 looped,
                            // 0x80000 mark rearm (bit 19)
    std::int32_t savedTime; // +0x24
    std::int32_t time;      // +0x28 8.8 frames
    std::int32_t key;       // +0x2C current key index (-1 none)
    std::int32_t frac;      // +0x30 0..0xFFFF between key and key + 1
    std::int32_t stamp;     // +0x34 engine clock at the last advance
    std::int32_t f38;       // +0x38 = 0x100
    std::int32_t duration;  // +0x3C (keys - 1) << 9|10, scaled by rate at start
    std::int32_t markBase;  // +0x40
    Fixed weight;           // +0x44 crossfade weight
    Fixed weightRate;       // +0x48 per 8.8 frame >> 8
    Fixed rate;             // +0x4C playback rate (0x45FB44)
    std::int32_t angle;     // +0x50 placement yaw
    Fixed cosA, sinA;       // +0x54 cos(-angle), +0x58 sin(-angle)
    std::int32_t f5c;
    std::int32_t yaw;       // +0x60 interpolated ch1 (0x45FC9C)
    Vec3 bone;              // +0x64 ball bone in world (0x45FEBC)
    Vec3 root;              // +0x70 accumulated root position (world)
    Vec3 prevRoot;          // +0x7C last interpolated root (clip space)
    Vec3 curRoot;           // +0x88 interpolated root (clip space)
    std::int32_t clip;      // +0x94 clip entry -> AnimData::clips index, -1 none
    std::int32_t animId;    // +0x98
    std::int32_t pid;       // +0x9C owner ctl -> player id
    std::uint32_t callback; // +0xA0 event callback -> exe address (0x4CC2A4/0x4CC408/0x4CC520/0x4CC608)
    std::int32_t keyA;      // +0xA4 key ptr -> key index
    std::int32_t keyB;      // +0xA8
};

struct AnimShared {
    std::int32_t clock = 0;     // 0x65FFAC+0x1C engine clock (8.8 frames)
    std::int32_t freeCount = 0; // +0x04
    std::int32_t freeHead = 0;  // +0x08 -> index + 1
    std::int32_t aimRate = 0;   // DAT_006757C8 = (simDt >> 16) * 9 (head aim step)
    std::int32_t motionPid = -1; // DAT_006757F8 movement object of the player last animated -> player id
    std::array<AnimInstance, kAnimInstances> inst{};
};

// ---- engine (instance level, index 0..0x44) ----
void Anim_initEngine(World& w);                       // 0x4CC6B0 state part: 0x45F710 pool, 0x45F7CC clock = 0
void Players_animate(World& w, Fixed simDt);          // 0x4CC8F4 -> 0x45F7D8 (declared in player.hpp too)

// ---- player level (pid; ctl = player+0x64) ----
int Anim_resolveId(const World& w, int animId);                       // 0x4CC984
int Anim_play(World& w, int pid, int animId, std::int32_t ecx, std::uint32_t chooser, std::int32_t angleOfs); // 0x4CC9BC
int Anim_start(World& w, int pid, int animId, std::int32_t ebx, std::int32_t ecx); // 0x4CCCFC (EBX chooser, ECX heading)
int Anim_selectLocomotion(World& w, int pid);                         // 0x4CC028
bool Anim_trySkill(World& w, int pid, int row, std::int32_t heading); // 0x4CCD14 (EDX row, EBX heading)
void Anim_updateScripted(World& w, int pid);                          // 0x4CCFC0 (EDX = M)
bool Anim_setDrift(World& w, int pid, std::int32_t dx, std::int32_t dz, std::int32_t frames); // 0x4CD2F0 (EDX, EBX, ECX)
bool Anim_clipUnavailable(const World& w, int animId);                // 0x4CD384
// 0x4CD3B0: EAX anim, EDX from-time selector, ECX from point (0 root / 1 bone), EBX to-time selector,
// stack s1 to point, out44 distance, out48 angle. Selectors: -100 mark0, -99 mark1 - 2 frames,
// -98 mark2 (else mark0), -97 end, else frames. Returns (to - from) frames, -1 if invalid.
int Anim_contactInfo(const World& w, int anim, int edx, int ecx, int ebx, int s1, std::int32_t* out44,
                     std::int32_t* out48);
Vec3 Anim_predictEndPos(World& w, int pid);                           // 0x4CD60C (integer position)
int Anim_boneHeightAt(const World& w, int animId, int sel);           // 0x4CD70C (-1 invalid)
int Anim_clipFrames(const World& w, int animId);                      // 0x4CD7D8
int Anim_markFrame0(const World& w, int animId);                      // 0x4CD810 (no resolve, -1 none)
int Anim_markFrame1(const World& w, int animId);                      // 0x4CD858 (mark1 - 2)
int Anim_markFrame2(const World& w, int animId);                      // 0x4CD8A4 (mark2, else mark0)
Vec3 Anim_contactPos(World& w, int pid);                              // 0x4CD8F8 (x, max(y - 8, 0), z) ints
int Anim_frame(const World& w, int pid);                              // 0x4CD944
int Anim_lengthFrames(const World& w, int pid);                       // 0x4CD950
int Anim_currentMark2(const World& w, int pid);                       // 0x4CD95C
int Anim_yawAt(const World& w, int animId, int sel);                  // 0x4CD998 (0x400 invalid)
int Anim_playScripted(World& w, int pid, int animId, std::int32_t startFrame, std::int32_t angle,
                      std::int32_t blend, bool mirror);               // 0x4CDB70 (ECX, EBX, stack)
// 0x4CDD54 (via 0x4CDA80 -> 0x500AC8 -> 0x500710 -> 0x501B58): full skeleton pose of an anim to
// find the per-frame hand/foot displacement. Needs the 14-quaternion skeleton evaluation of the
// renderer (compressed channels 9+, 0x54CDE4 bit reader); not ported.
void untraced_Anim_poseExtent(const World& w, int animId, int dir, std::int8_t* outX, std::int8_t* outY,
                              std::int8_t* outZ);

} // namespace wc98::sim
