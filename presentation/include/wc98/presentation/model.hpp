#pragma once
// PCMD models -> renderer meshes (one mesh per material) and player skeleton palettes.
#include "wc98/formats/anim.hpp"
#include "wc98/formats/pcmd.hpp"
#include "wc98/renderer/renderer.hpp"

#include <array>
#include <functional>
#include <span>
#include <vector>

namespace wc98::presentation {

struct ModelPart {
    std::uint32_t material = 0; // PcmdModel::materials index
    render::MeshData mesh;
};

using NodeFilter = std::function<bool(std::size_t node)>;
using CornerColors = std::span<const std::array<std::uint8_t, 4>>; // one per corner in file order
using CornerLights = std::span<const std::array<std::uint32_t, 4>>; // packed RGBA x 4 light directions per corner (.lm)

// Static model: node transforms applied (stadium meshes are already in world space). Corner colours
// come from `colors` when given, else the prelit corner RGB (vertexColors polygons) or white.
std::vector<ModelPart> buildStaticParts(const formats::PcmdModel& m, const NodeFilter& filter = {}, CornerColors colors = {});

// Skinned player model: every corner gets a render::SkinVertex whose joint indices are rig joint
// indices (0..14), i.e. palette slots of jointPalette().
std::vector<ModelPart> buildSkinnedParts(const formats::PcmdModel& m, const formats::PlayerRig& rig, const NodeFilter& filter = {},
                                         CornerColors colors = {}, CornerLights lights = {});

using LocalPose = std::array<formats::JointPose, formats::PlayerRig::kJoints>;

struct PoseOptions {
    bool mirror = false;     // AISEQS flag 0x8000: left/right swapped (Anim_poseFromKeys 0x501B58 mirror branch)
    bool quantize = false;   // original look: key fraction cut to 16 steps (frac >> 12 in 0x501B58)
    bool rootTravel = false; // keep the clip's ground travel (ch3/ch5) in the root; off when the
                             // simulation already moved the player (Anim_updateRoot 0x45FD24)
};
// Pose at a fractional key index: translations lerped, quaternions lerped component-wise and
// normalised (the exe lerps without normalising), clamped to the last key.
LocalPose samplePose(const formats::PlayerRig& rig, const formats::AnimClip& clip, float key, const PoseOptions& o = {});
// Key position for a playback time in 60 Hz frames: header flags bit 0 selects 4 ticks/key (ANIM.MO
// and MOAI.VIV clips) vs 2 (INGAME/MISC/ANIM.BIG clips), Anim_selectKeys 0x45FC30. `loop` wraps over
// the clip length. WC98: the sim engine tracks instance time in 8.8 frames; keyA/keyB + frac
// (AnimInstance, wc98/sim/anim.hpp) give the same position directly when an instance is active.
float clipKey(const formats::AnimClip& clip, float frames, bool loop);
// World-from-joint matrices (model space of the player) for the 15 rig joints.
std::array<glm::mat4, formats::PlayerRig::kJoints> jointPalette(const LocalPose& local);

glm::mat4 toMat4(const formats::JointPose& p);

} // namespace wc98::presentation
