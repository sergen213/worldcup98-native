// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/anim.hpp"

#include "wc98/reader.hpp"

#include <cmath>
#include <format>
#include <functional>

namespace wc98::formats {
namespace {

// MSB-first bit reader over little-endian 16-bit words (confirmed against all 623 compressed MOAI clips).
class WordBits {
public:
    explicit WordBits(std::span<const std::uint8_t> d) : d_(d) {}
    std::uint32_t read(unsigned n) {
        std::uint32_t v = 0;
        for (unsigned i = 0; i < n; ++i, ++bit_) {
            std::size_t word = bit_ / 16;
            if (word * 2 + 1 >= d_.size()) throw FormatError("ANIM: delta key bitstream overruns its block");
            unsigned w = d_[word * 2] | (d_[word * 2 + 1] << 8);
            v = (v << 1) | ((w >> (15 - bit_ % 16)) & 1);
        }
        return v;
    }

private:
    std::span<const std::uint8_t> d_;
    std::size_t bit_ = 0;
};

std::int32_t signExtend(std::uint32_t v, unsigned bits) {
    return bits == 0 ? 0 : static_cast<std::int32_t>(v << (32 - bits)) >> (32 - bits);
}

Vec3 operator+(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }

} // namespace

bool AnimClip::isAnim(std::span<const std::uint8_t> d) { return d.size() >= 36 && d[0] == 0x01 && d[1] == 0x0B; }

AnimClip AnimClip::parse(std::span<const std::uint8_t> data) {
    if (!isAnim(data)) throw FormatError("ANIM: bad magic (expected 01 0B)");
    Reader r(data, 2);
    AnimClip c;
    c.headerSize = r.u16le();
    c.ticks = r.u16le();
    c.keyCount = r.u16le();
    for (auto& m : c.marks) m = r.i16le();
    c.flags = r.u16le();
    c.channelCount = r.u16le();
    c.field18 = r.u16le();
    c.tripletCount = r.u16le();
    c.quatCount = r.u16le();
    const std::size_t nch = c.channelCount;
    if (nch != AnimClip::kFirstTriplet + 3u * c.tripletCount + 4u * c.quatCount)
        throw FormatError(std::format("ANIM: {} channels != 9 + 3*{} + 4*{}", nch, c.tripletCount, c.quatCount));
    if (c.compressed()) {
        if (c.headerSize < 36 + nch) throw FormatError("ANIM: header too small for bit-width table");
        r.seek(36);
        auto w = r.bytes(nch);
        c.bitWidths.assign(w.begin(), w.end());
        for (auto b : c.bitWidths)
            if (b > 16) throw FormatError("ANIM: bit width > 16");
    }
    r.seek(c.headerSize);
    while (r.remaining() > 0) {
        std::size_t start = r.pos();
        std::uint16_t size = r.u16le();
        if (size < 4 || size > r.remaining() + 2) throw FormatError(std::format("ANIM: bad key block size {} at {}", size, start));
        auto blk = data.subspan(start + 2, size - 2);
        Reader b(blk);
        std::vector<std::int32_t> v(nch);
        v[0] = b.u16le() & 0x7FFF; // bit 15 marks delta keys in MOAI clips (not in ANIM.BIG)
        bool full = c.keys.empty() || !c.compressed();
        if (full) {
            for (std::size_t i = 1; i < nch; ++i) v[i] = b.i16le();
        } else {
            for (std::size_t i = 1; i < AnimClip::kFirstTriplet; ++i) v[i] = b.i16le();
            WordBits bits(blk.subspan(2 * AnimClip::kFirstTriplet));
            const auto& prev = c.keys.back();
            for (std::size_t i = AnimClip::kFirstTriplet; i < nch; ++i) v[i] = prev[i] + signExtend(bits.read(c.bitWidths[i]), c.bitWidths[i]);
        }
        c.keys.push_back(std::move(v));
        r.seek(start + size);
    }
    if (c.keys.size() != c.keyCount) throw FormatError(std::format("ANIM: decoded {} keys, header says {}", c.keys.size(), c.keyCount));
    return c;
}

Quat multiply(const Quat& a, const Quat& b) {
    return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}

Vec3 rotate(const Quat& q, const Vec3& v) {
    // v + 2w(u x v) + 2u x (u x v)
    Vec3 u{q[0], q[1], q[2]};
    Vec3 t{2 * (u[1] * v[2] - u[2] * v[1]), 2 * (u[2] * v[0] - u[0] * v[2]), 2 * (u[0] * v[1] - u[1] * v[0])};
    return {v[0] + q[3] * t[0] + (u[1] * t[2] - u[2] * t[1]), v[1] + q[3] * t[1] + (u[2] * t[0] - u[0] * t[2]),
            v[2] + q[3] * t[2] + (u[0] * t[1] - u[1] * t[0])};
}

JointPose compose(const JointPose& p, const JointPose& c) {
    Vec3 s{c.translation[0] * p.scale, c.translation[1] * p.scale, c.translation[2] * p.scale};
    return {multiply(p.rotation, c.rotation), p.translation + rotate(p.rotation, s), p.scale * c.scale};
}

bool PlayerRig::isPlayerModel(const PcmdModel& m) {
    if (m.nodes.size() < kJoints) return false;
    for (std::size_t i = 0; i < kJoints; ++i)
        if (m.nodes[i].type != PcmdNodeType::Joint || !m.nodes[i].name.starts_with(kNames[i])) return false;
    return true;
}

PlayerRig PlayerRig::fromModel(const PcmdModel& m) {
    if (!isPlayerModel(m)) throw FormatError("player rig: model does not start with the 15 player joints");
    PlayerRig rig;
    for (std::size_t i = 0; i < kJoints; ++i) {
        rig.node[i] = i;
        rig.offset[i] = m.nodes[i].translation;
    }
    // The exporter stored no position for the Neck chain root (translation 0). A rigid fit of the
    // vertices shared by UpperTorso/Head and Neck/Head in hig.xxx and shi.xxx places it at exactly
    // (64.77, 0, 0) in the UpperTorso frame; every LOD shares the joint lengths. Verified by test_anim.
    rig.offset[3] = {64.77f, 0, 0};
    return rig;
}

std::array<JointPose, PlayerRig::kJoints> PlayerRig::localPose(const AnimClip& clip, std::size_t key) const {
    if (clip.tripletCount != 5 || clip.quatCount != kJoints - 1)
        throw FormatError(std::format("player rig: clip has {} triplets / {} quaternions, need 5 / 14", clip.tripletCount, clip.quatCount));
    const auto& k = clip.keys.at(key);
    auto triplet = [&](std::size_t i) {
        std::size_t c = clip.tripletChannel(i);
        return Vec3{k[c] / 4.0f, k[c + 1] / 4.0f, k[c + 2] / 4.0f}; // positions are stored x4
    };
    std::array<JointPose, kJoints> p;
    // Root: pelvis triplet plus ground travel (channels 3 and 5); channel 4 equals the triplet's y.
    p[0].translation = triplet(0) + Vec3{k[3] / 4.0f, 0, k[5] / 4.0f};
    for (std::size_t j = 1; j < kJoints; ++j) {
        std::size_t c = clip.quatChannel(j - 1);
        // Stored quaternions are the transpose (conjugate) of the parent-relative rotation.
        Quat q{-float(k[c]), -float(k[c + 1]), -float(k[c + 2]), float(k[c + 3])};
        float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (n == 0) throw FormatError("player rig: zero quaternion");
        p[j].rotation = {q[0] / n, q[1] / n, q[2] / n, q[3] / n};
        p[j].translation = offset[j];
    }
    p[5].translation = triplet(1);  // LUpperArm (shoulder)
    p[7].translation = triplet(2);  // RUpperArm
    p[12].translation = triplet(3); // LThigh (hip joint)
    p[9].translation = triplet(4);  // RThigh
    return p;
}

std::vector<Vec3> skinMesh(const PcmdModel& m, std::size_t meshNode, std::span<const JointPose> nodeWorld) {
    std::vector<Vec3> v(m.vertexCount(meshNode), Vec3{});
    for (std::size_t j = 0; j < m.nodes.size() && j < nodeWorld.size(); ++j)
        for (const auto& w : m.nodes[j].weights) {
            if (w.meshNode != meshNode) continue;
            Vec3 p = compose(nodeWorld[j], JointPose{{0, 0, 0, 1}, w.position, 1}).translation;
            float f = w.percent / 100.0f;
            for (int a = 0; a < 3; ++a) v[w.vertex][a] += p[a] * f;
        }
    return v;
}

std::vector<JointPose> pcmdNodeWorld(const PcmdModel& m) {
    std::vector<JointPose> world(m.nodes.size());
    std::vector<int> state(m.nodes.size(), 0); // 0 todo, 1 in progress, 2 done
    std::function<const JointPose&(std::size_t)> get = [&](std::size_t i) -> const JointPose& {
        if (state[i] == 2) return world[i];
        if (state[i] == 1) throw FormatError("PCMD: node parent cycle");
        state[i] = 1;
        const auto& n = m.nodes[i];
        Quat q{n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]};
        float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        q = len == 0 ? Quat{0, 0, 0, 1} : Quat{q[0] / len, q[1] / len, q[2] / len, q[3] / len}; // player joints store all zeros
        JointPose local{q, n.translation, n.scale[0]};
        bool hasParent = n.parent >= 0 && std::size_t(n.parent) < m.nodes.size();
        world[i] = hasParent ? compose(get(std::size_t(n.parent)), local) : local;
        state[i] = 2;
        return world[i];
    };
    for (std::size_t i = 0; i < m.nodes.size(); ++i) get(i);
    return world;
}

} // namespace wc98::formats
