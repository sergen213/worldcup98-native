// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Player animation clips (magic 01 0B; entries of DATA/CMN/AI/MOAI.VIV and DATA/BE/ANIM.BIG) and the
// 15-joint player rig they drive (PCMD .XXX models). See docs/formats/ANIM.md.
#include "wc98/formats/pcmd.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace wc98::formats {

using Vec3 = std::array<float, 3>;
using Quat = std::array<float, 4>; // x, y, z, w

struct AnimClip {
    std::uint16_t headerSize = 0;   // 116 (bit-width table present) or 36/160
    std::uint16_t ticks = 0;        // duration; 4 ticks per key in practice
    std::uint16_t keyCount = 0;
    std::array<std::int16_t, 3> marks{}; // -1 or tick positions (meaning unverified)
    std::uint16_t flags = 0;        // bit 15: keys stored uncompressed
    std::uint16_t channelCount = 0; // 9 + 3*triplets + 4*quaternions
    std::uint16_t field18 = 0;      // always 5 (unknown)
    std::uint16_t tripletCount = 0;
    std::uint16_t quatCount = 0;
    std::vector<std::uint8_t> bitWidths; // per channel, only for compressed clips
    std::vector<std::vector<std::int32_t>> keys; // decoded channel values per key

    [[nodiscard]] static bool isAnim(std::span<const std::uint8_t> data);
    static AnimClip parse(std::span<const std::uint8_t> data); // throws FormatError
    [[nodiscard]] bool compressed() const { return !(flags & 0x8000); }
    // ticks/keys is 4 in MOAI.VIV (+2 on 127 clips) and 2 in ANIM.BIG. ponytail: 60 Hz tick is a
    // hypothesis (running speed fits); see ANIM.md.
    [[nodiscard]] float secondsPerKey() const { return keyCount ? float(std::lround(float(ticks) / keyCount)) / 60.0f : 0.0f; }

    // Channel layout (confirmed): 0 flags, 1..8 scalar, then triplets, then quaternions (x,y,z,w * 256).
    static constexpr std::size_t kFirstTriplet = 9;
    [[nodiscard]] std::size_t tripletChannel(std::size_t i) const { return kFirstTriplet + 3 * i; }
    [[nodiscard]] std::size_t quatChannel(std::size_t i) const { return kFirstTriplet + 3 * tripletCount + 4 * i; }
};

struct JointPose {
    Quat rotation{0, 0, 0, 1};
    Vec3 translation{};
    float scale = 1; // uniform (one PCMD node uses 1.216, all others 1)
};

// The fixed FIFA 99 player skeleton: TorsoChn, LowerTorso, UpperTorso, Neck, Head, L/R arms, R/L legs.
struct PlayerRig {
    static constexpr std::size_t kJoints = 15;
    static constexpr std::array<std::string_view, kJoints> kNames = {
        "TorsoChn", "LowerTorso", "UpperTorso", "Neck", "Head", "LUpperArm", "LLowArm", "RUpperArm", "RLowArm",
        "RThigh", "RLowLeg", "RFoot", "LThigh", "LLowLeg", "LFoot"};
    // Animation hierarchy; differs from the PCMD links: the thighs hang off LowerTorso, not TorsoChn.
    static constexpr std::array<int, kJoints> kParent = {-1, 0, 1, 2, 3, 2, 5, 2, 7, 1, 9, 10, 1, 12, 13};

    std::array<std::size_t, kJoints> node{}; // PCMD node index of each joint
    std::array<Vec3, kJoints> offset{};      // static offsets in the parent frame (shoulders/hips/root come from keys)

    [[nodiscard]] static bool isPlayerModel(const PcmdModel& m);
    static PlayerRig fromModel(const PcmdModel& m); // throws FormatError if the model is not a player rig
    // Local (parent-relative) joint transforms for one key of a 9 + 5*3 + 14*4 channel clip.
    [[nodiscard]] std::array<JointPose, kJoints> localPose(const AnimClip& clip, std::size_t key) const;
};

Vec3 rotate(const Quat& q, const Vec3& v);
Quat multiply(const Quat& a, const Quat& b);
// parent * child; also maps a point: compose(pose, {identity, p}).translation.
JointPose compose(const JointPose& parent, const JointPose& child);

// Positions of every vertex of a skinned mesh node for the given world joint poses (indexed like
// PcmdModel::nodes; non-joint entries ignored). Vertices without influences stay at the origin.
std::vector<Vec3> skinMesh(const PcmdModel& m, std::size_t meshNode, std::span<const JointPose> nodeWorld);

// World pose of every PCMD node from its stored TRS and parent links (static models, arrow rig).
std::vector<JointPose> pcmdNodeWorld(const PcmdModel& m);

} // namespace wc98::formats
