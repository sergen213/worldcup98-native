// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/presentation/model.hpp"

#include <algorithm>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <map>
#include <stdexcept>

namespace wc98::presentation {

using namespace wc98::formats;

namespace {

std::uint32_t cornerRgba(const PcmdPolygon& p, const PcmdCorner& c, CornerColors colors, std::size_t corner) {
    if (!colors.empty()) {
        const auto& k = colors[corner];
        return render::packRgba(k[0], k[1], k[2], 255);
    }
    return p.vertexColors ? render::packRgba(c.color[0], c.color[1], c.color[2], 255) : 0xFFFFFFFFu;
}

// Walks every polygon in file order (the CMAP corner order), emitting corners into per-material parts.
template <class EmitCorner>
std::vector<ModelPart> walk(const PcmdModel& m, const NodeFilter& filter, CornerColors colors, EmitCorner&& emit) {
    if (!colors.empty() && colors.size() != m.cornerCount()) throw std::invalid_argument("corner colour count does not match the model");
    std::vector<ModelPart> parts;
    std::map<std::uint32_t, std::size_t> byMat;
    std::size_t corner = 0;
    for (std::size_t ni = 0; ni < m.nodes.size(); ++ni) {
        const auto& n = m.nodes[ni];
        if (n.type != PcmdNodeType::Mesh) continue;
        bool keep = !filter || filter(ni);
        for (const auto& poly : n.polygons) {
            if (!keep) {
                corner += poly.corners.size();
                continue;
            }
            auto [it, fresh] = byMat.try_emplace(poly.material, parts.size());
            if (fresh) parts.push_back({poly.material, {}});
            render::MeshData& md = parts[it->second].mesh;
            auto base = std::uint32_t(md.vertices.size());
            for (const auto& c : poly.corners) {
                emit(md, ni, poly, c, cornerRgba(poly, c, colors, corner), corner);
                ++corner;
            }
            for (std::uint32_t k = 1; k + 1 < poly.corners.size(); ++k) md.indices.insert(md.indices.end(), {base, base + k, base + k + 1});
        }
    }
    return parts;
}

glm::vec3 v3(const Vec3& v) { return {v[0], v[1], v[2]}; }

} // namespace

glm::mat4 toMat4(const JointPose& p) {
    glm::quat q(p.rotation[3], p.rotation[0], p.rotation[1], p.rotation[2]);
    glm::mat4 m = glm::mat4_cast(q) * p.scale;
    m[3] = glm::vec4(v3(p.translation), 1.0f);
    return m;
}

std::vector<ModelPart> buildStaticParts(const PcmdModel& m, const NodeFilter& filter, CornerColors colors) {
    std::vector<JointPose> world = pcmdNodeWorld(m);
    const bool vUp = m.exporter.starts_with("SOFTIMAGE"); // Softimage UVs have v = 0 at the image bottom
    return walk(m, filter, colors, [&](render::MeshData& md, std::size_t ni, const PcmdPolygon&, const PcmdCorner& c, std::uint32_t rgba, std::size_t) {
        const auto& n = m.nodes[ni];
        if (c.vertex >= n.vertices.size()) throw std::invalid_argument("static model: skinned mesh node " + n.name);
        Vec3 p = compose(world[ni], JointPose{{0, 0, 0, 1}, n.vertices[c.vertex], 1}).translation;
        md.vertices.push_back({v3(p), {0, 1, 0}, {c.u, vUp ? 1.0f - c.v : c.v}, rgba});
    });
}

std::vector<ModelPart> buildSkinnedParts(const PcmdModel& m, const PlayerRig& rig, const NodeFilter& filter, CornerColors colors, CornerLights lights) {
    if (!lights.empty() && lights.size() != m.cornerCount()) throw std::invalid_argument("corner light count does not match the model");
    std::map<std::size_t, std::uint32_t> jointOfNode;
    for (std::size_t j = 0; j < PlayerRig::kJoints; ++j) jointOfNode[rig.node[j]] = std::uint32_t(j);
    // Influences per (mesh node, vertex), at most 3 (the maximum in every player model).
    std::map<std::size_t, std::vector<render::SkinVertex>> skins;
    for (std::size_t j = 0; j < m.nodes.size(); ++j)
        for (const auto& w : m.nodes[j].weights) {
            auto& list = skins[w.meshNode];
            if (list.empty()) list.resize(m.vertexCount(w.meshNode));
            render::SkinVertex& s = list[w.vertex];
            int slot = s.weight.x == 0 ? 0 : s.weight.y == 0 ? 1 : s.weight.z == 0 ? 2 : -1;
            if (slot < 0) throw std::invalid_argument("skinned model: more than 3 influences on a vertex");
            auto jt = jointOfNode.find(j);
            if (jt == jointOfNode.end()) throw std::invalid_argument("skinned model: weight on a non-rig node");
            s.pos[std::size_t(slot)] = v3(w.position);
            s.weight[slot] = w.percent / 100.0f;
            s.joints |= jt->second << (8 * slot);
        }
    // Normalise (low.xxx carries two vertices whose weights do not sum to 100 %).
    for (auto& [node, list] : skins)
        for (auto& s : list)
            if (float sum = s.weight.x + s.weight.y + s.weight.z; sum > 0) s.weight /= sum;
    return walk(m, filter, colors, [&](render::MeshData& md, std::size_t ni, const PcmdPolygon&, const PcmdCorner& c, std::uint32_t rgba, std::size_t corner) {
        auto it = skins.find(ni);
        if (it == skins.end() || c.vertex >= it->second.size()) throw std::invalid_argument("skinned model: corner without skin weights");
        md.vertices.push_back({{}, {0, 1, 0}, {c.u, c.v}, rgba});
        md.skin.push_back(it->second[c.vertex]);
        if (!lights.empty()) md.skin.back().light = lights[corner];
    });
}

LocalPose samplePose(const PlayerRig& rig, const AnimClip& clip, float key, const PoseOptions& o) {
    if (clip.keyCount == 0) throw std::invalid_argument("samplePose: empty clip");
    key = std::clamp(key, 0.0f, float(clip.keyCount - 1));
    auto k0 = std::size_t(key);
    std::size_t k1 = std::min<std::size_t>(k0 + 1, clip.keyCount - 1);
    float t = key - float(k0);
    if (o.quantize) t = std::floor(t * 16.0f) / 16.0f;
    LocalPose a = rig.localPose(clip, k0);
    if (t > 0 && k1 != k0) {
        LocalPose b = rig.localPose(clip, k1);
        for (std::size_t j = 0; j < a.size(); ++j) {
            Quat& q = a[j].rotation;
            float len = 0;
            for (int i = 0; i < 3; ++i) a[j].translation[i] += (b[j].translation[i] - a[j].translation[i]) * t;
            for (int i = 0; i < 4; ++i) q[i] += (b[j].rotation[i] - q[i]) * t, len += q[i] * q[i];
            len = std::sqrt(len);
            for (int i = 0; i < 4; ++i) q[i] /= len;
        }
    }
    if (!o.rootTravel) { // localPose() adds the ground travel channels 3/5 to the root
        auto travel = [&](std::size_t k, int c) { return float(clip.keys.at(k)[std::size_t(c)]) / 4.0f; };
        a[0].translation[0] -= travel(k0, 3) + (travel(k1, 3) - travel(k0, 3)) * t;
        a[0].translation[2] -= travel(k0, 5) + (travel(k1, 5) - travel(k0, 5)) * t;
    }
    if (o.mirror) {
        // 0x501B58 mirror branch on the stored values: root x and the other triplets' z negated,
        // quaternions (x,y,z,w) -> (-x,-y,z,w), left/right limb slots swapped (table 0x58E4D8).
        // Our rotations are the conjugates of the stored ones, which maps to the same sign pattern.
        for (auto& p : a) p.rotation = {-p.rotation[0], -p.rotation[1], p.rotation[2], p.rotation[3]};
        a[0].translation[0] = -a[0].translation[0];
        for (std::size_t j : {5u, 7u, 9u, 12u}) a[j].translation[2] = -a[j].translation[2];
        for (auto [l, r] : {std::pair{5, 7}, {6, 8}, {12, 9}, {13, 10}, {14, 11}}) std::swap(a[std::size_t(l)], a[std::size_t(r)]);
    }
    return a;
}

float clipKey(const AnimClip& clip, float frames, bool loop) {
    float key = frames / ((clip.flags & 1) ? 4.0f : 2.0f);
    float keys = float(clip.keyCount);
    if (loop && keys > 0) key = std::fmod(std::fmod(key, keys) + keys, keys);
    return key;
}

std::array<glm::mat4, PlayerRig::kJoints> jointPalette(const LocalPose& local) {
    std::array<JointPose, PlayerRig::kJoints> world;
    std::array<glm::mat4, PlayerRig::kJoints> out;
    for (std::size_t j = 0; j < PlayerRig::kJoints; ++j) {
        int p = PlayerRig::kParent[j];
        world[j] = p < 0 ? local[j] : compose(world[std::size_t(p)], local[j]);
        out[j] = toMat4(world[j]);
    }
    return out;
}

} // namespace wc98::presentation
