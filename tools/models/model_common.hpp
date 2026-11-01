// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Shared code of wc98-model-export and wc98-anim-inspect: input loading ("file" or "archive:member"),
// PCMD -> triangle scene (static or posed player), glTF 2.0 writer and an orthographic CPU preview.
#include "common/json.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/anim.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/fsh.hpp"
#include "wc98/formats/pcmd.hpp"
#include "wc98/formats/png.hpp"
#include "wc98/formats/refpack.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace wc98::tools {
namespace fs = std::filesystem;
using namespace wc98::formats;

// ---------------------------------------------------------------- input

struct Archive {
    std::vector<std::uint8_t> data; // RefPack-decompressed BIG/VIV bytes
    // WC98 delta (matrix section 3): a minority of WC98 archives (DATA/BE/ANIM.BIG) carry a
    // 48-byte prefix before the BIGF magic; MOAI.VIV and the .VIV/.BIG majority start at 0.
    std::size_t base = 0;
    BigArchive dir;
    Archive() = default;
    explicit Archive(const fs::path& p) : data(refpack::decompressIfNeeded(readFile(p))) {
        if (!BigArchive::isBig(data) && data.size() > 0x34 &&
            BigArchive::isBig(std::span(data.data() + 0x30, data.size() - 0x30)))
            base = 0x30;
        dir = BigArchive::parse(body());
    }
    [[nodiscard]] std::span<const std::uint8_t> body() const { return std::span(data.data() + base, data.size() - base); }
    [[nodiscard]] std::vector<std::uint8_t> member(const BigEntry& e) const { return refpack::decompressIfNeeded(BigArchive::payload(body(), e)); }
};

// "path", "path:member" or "path:member:member..." (BIG/VIV/PAK members, case-insensitive,
// RefPack handled at every level). WC98 nests one more level than FIFA 99: BEART.BIG members
// are themselves BIG archives of FSH textures, e.g. "BEART.BIG:PC0001.BIG:0001".
inline std::vector<std::uint8_t> loadInput(const std::string& spec) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        auto c = spec.find(':', start);
        parts.push_back(spec.substr(start, c == std::string::npos ? c : c - start));
        if (c == std::string::npos) break;
        start = c + 1;
    }
    if (parts.size() == 1 && fs::is_regular_file(parts[0])) return refpack::decompressIfNeeded(readFile(parts[0]));
    Archive a(parts[0]);
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const BigEntry* e = a.dir.find(parts[i]);
        if (!e) throw std::runtime_error("no member '" + parts[i] + "' in " + spec);
        auto payload = a.member(*e);
        if (i + 1 == parts.size()) return payload;
        a = Archive{};                       // descend: re-parse the member as a nested archive
        a.data = std::move(payload);
        if (!BigArchive::isBig(a.data) && a.data.size() > 0x34 &&
            BigArchive::isBig(std::span(a.data.data() + 0x30, a.data.size() - 0x30)))
            a.base = 0x30;
        a.dir = BigArchive::parse(a.body());
    }
    return {};
}

inline std::string specStem(const std::string& spec) {
    std::string s = spec.substr(spec.find_last_of("/:\\") + 1);
    return s.substr(0, s.find('.'));
}

// ---------------------------------------------------------------- textures

struct Texture {
    std::uint32_t w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
    bool alpha = false; // any texel with alpha < 255
};

// Opens a BIG/VIV archive from already-decompressed bytes; WC98 prefix handling applies
// (see Archive). Returns nullopt when the payload is not an archive.
inline std::optional<Archive> archiveFromBytes(std::vector<std::uint8_t> bytes) {
    Archive a;
    a.data = std::move(bytes);
    if (!BigArchive::isBig(a.data) && a.data.size() > 0x34 &&
        BigArchive::isBig(std::span(a.data.data() + 0x30, a.data.size() - 0x30)))
        a.base = 0x30;
    if (!BigArchive::isBig(a.body())) return std::nullopt;
    a.dir = BigArchive::parse(a.body());
    return a;
}

// Tag -> texture from the given specs; the first file defining a tag wins.
// WC98: a spec may be an FSH, a RefPack .PAK that unpacks to one, or an *archive* —
// WC98 nests FSH textures inside nested BIG archives (BEART.BIG:PC0001.BIG:0001),
// so archives are harvested recursively (matrix section 2, 3D-texture layout).
inline void harvestTextures(const std::vector<std::uint8_t>& payload, std::map<std::string, Texture>& out, int depth);

inline void harvestFsh(const FshFile& f, std::map<std::string, Texture>& out) {
    for (auto& e : f.entries) {
        std::string tag = e.tag.substr(0, e.tag.find_last_not_of(" \0") + 1); // FSH tags are space padded ("cl  ")
        if (!e.bitmap || out.contains(tag)) continue;
        Texture t{e.bitmap->width, e.bitmap->height, e.bitmap->rgba, false};
        for (std::size_t i = 3; i < t.rgba.size(); i += 4) t.alpha = t.alpha || t.rgba[i] < 255;
        out.emplace(tag, std::move(t));
    }
}

inline void harvestTextures(const std::vector<std::uint8_t>& payload, std::map<std::string, Texture>& out, int depth) {
    if (FshFile::isFsh(payload)) {
        try { harvestFsh(FshFile::parse(payload), out); } catch (const std::exception&) {}
        return;
    }
    if (depth >= 6) return;
    auto un = refpack::decompressIfNeeded(payload);
    if (auto a = archiveFromBytes(std::move(un)))
        for (const auto& e : a->dir.entries) harvestTextures(a->member(e), out, depth + 1);
}

inline std::map<std::string, Texture> loadTextures(const std::vector<std::string>& specs) {
    std::map<std::string, Texture> out;
    for (const auto& s : specs) {
        // WC98 convenience: a directory spec harvests every FSH / RefPack .PAK inside it
        // (player textures live as 141 loose .FSH files in INGAME/PLAYER/TEXTURES).
        if (fs::is_directory(s)) {
            std::vector<fs::path> files;
            for (const auto& e : fs::directory_iterator(s))
                if (e.is_regular_file()) files.push_back(e.path());
            std::ranges::sort(files);
            for (const auto& p : files) harvestTextures(refpack::decompressIfNeeded(readFile(p)), out, 0);
            continue;
        }
        harvestTextures(loadInput(s), out, 0);
    }
    return out;
}

// ---------------------------------------------------------------- scene

struct Vertex {
    Vec3 pos{};
    float u = 0, v = 0;
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
    std::array<std::uint16_t, 4> joints{};
    std::array<float, 4> weights{};
};

struct Primitive {
    std::uint32_t material = 0;
    std::vector<Vertex> verts;
    std::vector<std::uint32_t> indices;
};

struct Part {
    std::string name;
    std::vector<Primitive> prims;
};

struct Scene {
    const PcmdModel* model = nullptr;
    std::vector<Part> parts;
    std::optional<PlayerRig> rig;                         // set for skinned player models
    std::array<JointPose, PlayerRig::kJoints> bindLocal{}; // pose the vertices were skinned with
};

// Default player parts: torso, first hair style, closed hands (the rest are alternatives).
inline bool defaultPlayerPart(const PcmdModel& m, std::size_t node) {
    auto first = [&](std::string_view prefix) {
        for (std::size_t i = 0; i < m.nodes.size(); ++i)
            if (m.nodes[i].type == PcmdNodeType::Mesh && m.nodes[i].name.starts_with(prefix)) return i == node;
        return false;
    };
    bool hasTorso = false;
    for (const auto& n : m.nodes) hasTorso = hasTorso || n.name.starts_with("torso");
    if (!hasTorso) return true;
    return first("torso") || first("hair") || first("closed_right") || first("closed_left");
}

// Builds triangles for every mesh node (or the default player subset). For player models `bind`
// (local joint poses) positions the skin; other models use their stored node transforms.
inline Scene buildScene(const PcmdModel& m, const std::array<JointPose, PlayerRig::kJoints>* bind, bool allParts) {
    Scene s;
    s.model = &m;
    std::vector<JointPose> world = pcmdNodeWorld(m);
    std::vector<std::vector<std::pair<std::uint16_t, float>>> influence; // per vertex of the current mesh
    if (PlayerRig::isPlayerModel(m)) {
        if (!bind) throw std::runtime_error("player model needs a pose (--pose ARCHIVE:CLIP)");
        s.rig = PlayerRig::fromModel(m);
        s.bindLocal = *bind;
        for (std::size_t j = 0; j < PlayerRig::kJoints; ++j) {
            int p = PlayerRig::kParent[j];
            world[s.rig->node[j]] = p < 0 ? s.bindLocal[j] : compose(world[s.rig->node[std::size_t(p)]], s.bindLocal[j]);
        }
    }
    for (std::size_t ni = 0; ni < m.nodes.size(); ++ni) {
        const auto& n = m.nodes[ni];
        if (n.type != PcmdNodeType::Mesh || n.polygons.empty()) continue;
        if (s.rig && !allParts && !defaultPlayerPart(m, ni)) continue;
        std::vector<Vec3> pos;
        if (!n.vertices.empty()) {
            for (const auto& v : n.vertices) pos.push_back(compose(world[ni], JointPose{{0, 0, 0, 1}, v, 1}).translation);
        } else {
            pos = skinMesh(m, ni, world);
            influence.assign(pos.size(), {});
            for (std::size_t j = 0; j < m.nodes.size(); ++j)
                for (const auto& w : m.nodes[j].weights)
                    if (w.meshNode == ni) influence[w.vertex].emplace_back(std::uint16_t(j), w.percent / 100.0f);
        }
        Part part{n.name, {}};
        std::map<std::uint32_t, std::size_t> byMat;
        for (const auto& poly : n.polygons) {
            auto [it, fresh] = byMat.try_emplace(poly.material, part.prims.size());
            if (fresh) part.prims.push_back({poly.material, {}, {}});
            auto& prim = part.prims[it->second];
            auto base = std::uint32_t(prim.verts.size());
            for (const auto& c : poly.corners) {
                Vertex v;
                v.pos = pos[c.vertex];
                v.u = c.u;
                v.v = c.v;
                if (poly.vertexColors) v.color = {c.color[0], c.color[1], c.color[2], 255};
                if (s.rig && n.vertices.empty()) {
                    const auto& inf = influence[c.vertex];
                    float sum = 0;
                    for (std::size_t k = 0; k < inf.size() && k < 4; ++k) sum += inf[k].second;
                    for (std::size_t k = 0; k < inf.size() && k < 4; ++k) {
                        v.joints[k] = inf[k].first; // node index == rig joint index for player models
                        v.weights[k] = inf[k].second / sum; // glTF needs sum 1; low.xxx has two 25 %/75 % vertices
                    }
                }
                prim.verts.push_back(v);
            }
            for (std::uint32_t k = 1; k + 1 < poly.corners.size(); ++k) prim.indices.insert(prim.indices.end(), {base, base + k, base + k + 1});
        }
        s.parts.push_back(std::move(part));
    }
    return s;
}

// ---------------------------------------------------------------- glTF

namespace detail {
struct Bin {
    std::vector<std::uint8_t> bytes;
    std::vector<std::string> views, accessors;
    std::size_t view(const void* p, std::size_t n, int target = 0) {
        while (bytes.size() % 4) bytes.push_back(0);
        std::size_t off = bytes.size();
        bytes.insert(bytes.end(), static_cast<const std::uint8_t*>(p), static_cast<const std::uint8_t*>(p) + n);
        views.push_back(std::format(R"({{"buffer":0,"byteOffset":{},"byteLength":{}{}}})", off, n, target ? std::format(R"(,"target":{})", target) : ""));
        return views.size() - 1;
    }
    std::size_t accessor(std::size_t view, int comp, std::size_t count, const char* type, std::string extra = "") {
        accessors.push_back(std::format(R"({{"bufferView":{},"componentType":{},"count":{},"type":"{}"{}}})", view, comp, count, type, extra));
        return accessors.size() - 1;
    }
};
inline std::string fmtf(float f) { return std::format("{}", std::isfinite(f) ? f : 0.0f); }
inline std::string join(const std::vector<std::string>& v) {
    std::string o;
    for (std::size_t i = 0; i < v.size(); ++i) o += (i ? "," : "") + v[i];
    return o;
}
inline std::array<float, 16> matrix(const JointPose& p) { // column-major, uniform scale
    auto [x, y, z, w] = p.rotation;
    float s = p.scale;
    return {s * (1 - 2 * (y * y + z * z)), s * 2 * (x * y + z * w), s * 2 * (x * z - y * w), 0,
            s * 2 * (x * y - z * w), s * (1 - 2 * (x * x + z * z)), s * 2 * (y * z + x * w), 0,
            s * 2 * (x * z + y * w), s * 2 * (y * z - x * w), s * (1 - 2 * (x * x + y * y)), 0,
            p.translation[0], p.translation[1], p.translation[2], 1};
}
inline JointPose inverse(const JointPose& p) {
    Quat ci{-p.rotation[0], -p.rotation[1], -p.rotation[2], p.rotation[3]};
    float is = 1 / p.scale;
    Vec3 t = rotate(ci, p.translation);
    return {ci, {-t[0] * is, -t[1] * is, -t[2] * is}, is};
}
} // namespace detail

struct NamedClip {
    std::string name;
    AnimClip clip;
};

// Writes <dir>/<base>.gltf + .bin and one PNG per used texture. Player scenes get a skin over the
// 15-joint rig and one glTF animation per clip.
inline void writeGltf(const fs::path& dir, const std::string& base, const Scene& s, const std::map<std::string, Texture>& tex,
                      const std::vector<NamedClip>& clips) {
    using detail::fmtf;
    detail::Bin bin;
    const auto& m = *s.model;
    std::vector<std::string> nodes, meshes, materials, textures, images, skins, anims;
    std::map<std::string, std::size_t> texIndex;

    for (const auto& mat : m.materials) {
        auto t = tex.find(mat.texture);
        std::string pbr;
        if (t != tex.end()) {
            auto [it, fresh] = texIndex.try_emplace(mat.texture, textures.size());
            if (fresh) {
                auto png = encodePng(t->second.w, t->second.h, t->second.rgba);
                writeFile(dir / (mat.texture + ".png"), png);
                images.push_back(std::format(R"({{"uri":{}}})", jstr(mat.texture + ".png")));
                textures.push_back(std::format(R"({{"source":{},"sampler":0}})", images.size() - 1));
            }
            pbr = std::format(R"("baseColorTexture":{{"index":{}}},)", it->second);
        } else {
            pbr = std::format(R"("baseColorFactor":[{},{},{},1],)", fmtf(mat.color[0] / 255.0f), fmtf(mat.color[1] / 255.0f), fmtf(mat.color[2] / 255.0f));
        }
        bool alpha = t != tex.end() && t->second.alpha;
        materials.push_back(std::format(R"({{"name":{},"pbrMetallicRoughness":{{{}"metallicFactor":0,"roughnessFactor":1}},"doubleSided":true{}}})",
                                        jstr(mat.texture), pbr, alpha ? R"(,"alphaMode":"MASK")" : ""));
    }

    const bool skinned = s.rig.has_value();
    std::vector<std::size_t> meshNodes;
    for (const auto& part : s.parts) {
        std::vector<std::string> prims;
        for (const auto& pr : part.prims) {
            std::vector<float> pos, uv, col, wts;
            std::vector<std::uint16_t> jnt;
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (const auto& v : pr.verts) {
                for (int a = 0; a < 3; ++a) {
                    pos.push_back(v.pos[a]);
                    lo[a] = std::min(lo[a], v.pos[a]);
                    hi[a] = std::max(hi[a], v.pos[a]);
                }
                uv.insert(uv.end(), {v.u, v.v});
                for (auto c : v.color) col.push_back(c / 255.0f);
                jnt.insert(jnt.end(), v.joints.begin(), v.joints.end());
                wts.insert(wts.end(), v.weights.begin(), v.weights.end());
            }
            std::size_t n = pr.verts.size();
            auto aPos = bin.accessor(bin.view(pos.data(), pos.size() * 4, 34962), 5126, n, "VEC3",
                                     std::format(R"(,"min":[{},{},{}],"max":[{},{},{}])", fmtf(lo[0]), fmtf(lo[1]), fmtf(lo[2]), fmtf(hi[0]), fmtf(hi[1]), fmtf(hi[2])));
            auto aUv = bin.accessor(bin.view(uv.data(), uv.size() * 4, 34962), 5126, n, "VEC2");
            auto aCol = bin.accessor(bin.view(col.data(), col.size() * 4, 34962), 5126, n, "VEC4");
            auto aIdx = bin.accessor(bin.view(pr.indices.data(), pr.indices.size() * 4, 34963), 5125, pr.indices.size(), "SCALAR");
            std::string attrs = std::format(R"("POSITION":{},"TEXCOORD_0":{},"COLOR_0":{})", aPos, aUv, aCol);
            if (skinned) {
                auto aJ = bin.accessor(bin.view(jnt.data(), jnt.size() * 2, 34962), 5123, n, "VEC4");
                auto aW = bin.accessor(bin.view(wts.data(), wts.size() * 4, 34962), 5126, n, "VEC4");
                attrs += std::format(R"(,"JOINTS_0":{},"WEIGHTS_0":{})", aJ, aW);
            }
            prims.push_back(std::format(R"({{"attributes":{{{}}},"indices":{},"material":{}}})", attrs, aIdx, pr.material));
        }
        meshes.push_back(std::format(R"({{"name":{},"primitives":[{}]}})", jstr(part.name), detail::join(prims)));
        meshNodes.push_back(nodes.size());
        nodes.push_back(std::format(R"({{"name":{},"mesh":{}{}}})", jstr(part.name), meshes.size() - 1, skinned ? R"(,"skin":0)" : ""));
    }

    std::vector<std::size_t> roots = meshNodes;
    if (skinned) {
        const auto& rig = *s.rig;
        std::size_t first = nodes.size();
        std::array<JointPose, PlayerRig::kJoints> world;
        std::vector<float> ibm;
        for (std::size_t j = 0; j < PlayerRig::kJoints; ++j) {
            int p = PlayerRig::kParent[j];
            world[j] = p < 0 ? s.bindLocal[j] : compose(world[std::size_t(p)], s.bindLocal[j]);
            auto mm = detail::matrix(detail::inverse(world[j]));
            ibm.insert(ibm.end(), mm.begin(), mm.end());
            std::vector<std::string> kids;
            for (std::size_t c = 0; c < PlayerRig::kJoints; ++c)
                if (PlayerRig::kParent[c] == int(j)) kids.push_back(std::to_string(first + c));
            const auto& b = s.bindLocal[j];
            nodes.push_back(std::format(R"({{"name":{},"rotation":[{},{},{},{}],"translation":[{},{},{}]{}}})", jstr(m.nodes[rig.node[j]].name),
                                        fmtf(b.rotation[0]), fmtf(b.rotation[1]), fmtf(b.rotation[2]), fmtf(b.rotation[3]), fmtf(b.translation[0]),
                                        fmtf(b.translation[1]), fmtf(b.translation[2]), kids.empty() ? "" : R"(,"children":[)" + detail::join(kids) + "]"));
        }
        auto aIbm = bin.accessor(bin.view(ibm.data(), ibm.size() * 4), 5126, PlayerRig::kJoints, "MAT4");
        std::vector<std::string> jl;
        for (std::size_t j = 0; j < PlayerRig::kJoints; ++j) jl.push_back(std::to_string(first + j));
        skins.push_back(std::format(R"({{"joints":[{}],"inverseBindMatrices":{},"skeleton":{}}})", detail::join(jl), aIbm, first));
        roots.push_back(first);

        for (const auto& nc : clips) {
            std::size_t nk = nc.clip.keys.size();
            if (nk == 0) continue;
            std::vector<float> times(nk);
            for (std::size_t k = 0; k < nk; ++k) times[k] = float(k) * nc.clip.secondsPerKey();
            auto aT = bin.accessor(bin.view(times.data(), times.size() * 4), 5126, nk, "SCALAR",
                                   std::format(R"(,"min":[0],"max":[{}])", fmtf(times.back())));
            std::vector<std::array<JointPose, PlayerRig::kJoints>> poses;
            for (std::size_t k = 0; k < nk; ++k) poses.push_back(rig.localPose(nc.clip, k));
            std::vector<std::string> samplers, channels;
            for (std::size_t j = 0; j < PlayerRig::kJoints; ++j) {
                bool moves = j == 0 || j == 5 || j == 7 || j == 9 || j == 12; // translation channels driven by keys
                std::vector<float> r, t;
                for (const auto& p : poses) {
                    r.insert(r.end(), p[j].rotation.begin(), p[j].rotation.end());
                    t.insert(t.end(), p[j].translation.begin(), p[j].translation.end());
                }
                if (j > 0) {
                    samplers.push_back(std::format(R"({{"input":{},"output":{},"interpolation":"LINEAR"}})", aT, bin.accessor(bin.view(r.data(), r.size() * 4), 5126, nk, "VEC4")));
                    channels.push_back(std::format(R"({{"sampler":{},"target":{{"node":{},"path":"rotation"}}}})", samplers.size() - 1, first + j));
                }
                if (moves) {
                    samplers.push_back(std::format(R"({{"input":{},"output":{},"interpolation":"LINEAR"}})", aT, bin.accessor(bin.view(t.data(), t.size() * 4), 5126, nk, "VEC3")));
                    channels.push_back(std::format(R"({{"sampler":{},"target":{{"node":{},"path":"translation"}}}})", samplers.size() - 1, first + j));
                }
            }
            anims.push_back(std::format(R"({{"name":{},"samplers":[{}],"channels":[{}]}})", jstr(nc.name), detail::join(samplers), detail::join(channels)));
        }
    }

    std::vector<std::string> rootStr;
    for (auto r : roots) rootStr.push_back(std::to_string(r));
    std::string json = std::format(R"({{"asset":{{"version":"2.0","generator":"wc98-model-export, FIFA 99 PCMD"}},"scene":0,"scenes":[{{"nodes":[{}]}}],)"
                                   R"("nodes":[{}],"meshes":[{}],"materials":[{}],"buffers":[{{"uri":{},"byteLength":{}}}],"bufferViews":[{}],"accessors":[{}])",
                                   detail::join(rootStr), detail::join(nodes), detail::join(meshes), detail::join(materials), jstr(base + ".bin"),
                                   bin.bytes.size(), detail::join(bin.views), detail::join(bin.accessors));
    if (!textures.empty())
        json += std::format(R"(,"textures":[{}],"images":[{}],"samplers":[{{"magFilter":9728,"minFilter":9728,"wrapS":10497,"wrapT":10497}}])", detail::join(textures), detail::join(images));
    if (!skins.empty()) json += std::format(R"(,"skins":[{}])", detail::join(skins));
    if (!anims.empty()) json += std::format(R"(,"animations":[{}])", detail::join(anims));
    json += "}\n";
    writeFile(dir / (base + ".bin"), bin.bytes);
    writeFile(dir / (base + ".gltf"), std::span(reinterpret_cast<const std::uint8_t*>(json.data()), json.size()));
}

// ---------------------------------------------------------------- preview

// Front (x,y), side (z,y) and top (x,z) orthographic views, z-buffered, flat-lit, textured. Returns PNG.
inline std::vector<std::uint8_t> renderPreview(const Scene& s, const std::map<std::string, Texture>& tex, int size) {
    const auto& m = *s.model;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& p : s.parts)
        for (const auto& pr : p.prims)
            for (const auto& v : pr.verts)
                for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], v.pos[a]), hi[a] = std::max(hi[a], v.pos[a]);
    float ext = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 1e-6f});
    const int W = size * 3, H = size;
    std::vector<std::uint8_t> img(std::size_t(W) * H * 4);
    for (std::size_t i = 0; i < img.size(); i += 4) img[i] = 32, img[i + 1] = 32, img[i + 2] = 40, img[i + 3] = 255;
    std::vector<float> zbuf(std::size_t(W) * H, -1e30f);
    // view: screen-x axis (sign), screen-up axis (sign), depth axis (sign: larger = nearer)
    struct View { int ax, ay, az; float sx, sy, sz; };
    const View views[3] = {{0, 1, 2, 1, 1, 1}, {2, 1, 0, -1, 1, 1}, {0, 2, 1, 1, -1, 1}};
    for (int vi = 0; vi < 3; ++vi) {
        const View& V = views[vi];
        auto proj = [&](const Vec3& p) {
            float c[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
            return std::array<float, 3>{vi * size + size / 2.0f + V.sx * (p[V.ax] - c[V.ax]) / ext * size * 0.92f,
                                        size / 2.0f - V.sy * (p[V.ay] - c[V.ay]) / ext * size * 0.92f, V.sz * p[V.az]};
        };
        for (const auto& part : s.parts)
            for (const auto& pr : part.prims) {
                const auto& mat = m.materials[pr.material];
                auto ti = tex.find(mat.texture);
                const Texture* t = ti == tex.end() ? nullptr : &ti->second;
                for (std::size_t i = 0; i + 2 < pr.indices.size(); i += 3) {
                    const Vertex* v[3] = {&pr.verts[pr.indices[i]], &pr.verts[pr.indices[i + 1]], &pr.verts[pr.indices[i + 2]]};
                    Vec3 e1{v[1]->pos[0] - v[0]->pos[0], v[1]->pos[1] - v[0]->pos[1], v[1]->pos[2] - v[0]->pos[2]};
                    Vec3 e2{v[2]->pos[0] - v[0]->pos[0], v[2]->pos[1] - v[0]->pos[1], v[2]->pos[2] - v[0]->pos[2]};
                    Vec3 n{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                    float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    float shade = nl > 0 ? 0.35f + 0.65f * std::abs((n[0] * 0.3f + n[1] * 0.8f + n[2] * 0.52f) / nl) : 0.35f;
                    auto p0 = proj(v[0]->pos), p1 = proj(v[1]->pos), p2 = proj(v[2]->pos);
                    float area = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p2[0] - p0[0]) * (p1[1] - p0[1]);
                    if (std::abs(area) < 1e-9f) continue;
                    int x0 = std::max(vi * size, int(std::floor(std::min({p0[0], p1[0], p2[0]}))));
                    int x1 = std::min(vi * size + size - 1, int(std::ceil(std::max({p0[0], p1[0], p2[0]}))));
                    int y0 = std::max(0, int(std::floor(std::min({p0[1], p1[1], p2[1]}))));
                    int y1 = std::min(H - 1, int(std::ceil(std::max({p0[1], p1[1], p2[1]}))));
                    for (int y = y0; y <= y1; ++y)
                        for (int x = x0; x <= x1; ++x) {
                            float px = x + 0.5f, py = y + 0.5f;
                            float b0 = ((p1[0] - px) * (p2[1] - py) - (p2[0] - px) * (p1[1] - py)) / area;
                            float b1 = ((p2[0] - px) * (p0[1] - py) - (p0[0] - px) * (p2[1] - py)) / area;
                            float b2 = 1 - b0 - b1;
                            if (b0 < 0 || b1 < 0 || b2 < 0) continue;
                            float z = b0 * p0[2] + b1 * p1[2] + b2 * p2[2];
                            std::size_t zi = std::size_t(y) * W + x;
                            if (z <= zbuf[zi]) continue;
                            float rgb[3] = {mat.color[0] / 255.0f, mat.color[1] / 255.0f, mat.color[2] / 255.0f};
                            if (t) {
                                float u = b0 * v[0]->u + b1 * v[1]->u + b2 * v[2]->u, vv = b0 * v[0]->v + b1 * v[1]->v + b2 * v[2]->v;
                                long tx = long(std::floor(u * t->w)) % long(t->w), ty = long(std::floor(vv * t->h)) % long(t->h);
                                if (tx < 0) tx += t->w;
                                if (ty < 0) ty += t->h;
                                const std::uint8_t* c = &t->rgba[(std::size_t(ty) * t->w + std::size_t(tx)) * 4];
                                if (c[3] < 128) continue;
                                rgb[0] = c[0] / 255.0f, rgb[1] = c[1] / 255.0f, rgb[2] = c[2] / 255.0f;
                            }
                            for (int a = 0; a < 3; ++a) rgb[a] *= (b0 * v[0]->color[a] + b1 * v[1]->color[a] + b2 * v[2]->color[a]) / 255.0f;
                            zbuf[zi] = z;
                            for (int a = 0; a < 3; ++a) img[zi * 4 + a] = std::uint8_t(std::clamp(rgb[a] * shade, 0.0f, 1.0f) * 255);
                        }
                }
            }
    }
    return encodePng(W, H, img);
}

} // namespace wc98::tools
