// pass-108 venue fly-in lane mirror; evidence analysis/pass108venue/NOTES.md.
#include "wc98/presentation/venue.hpp"

#include "wc98/file.hpp"
#include "wc98/formats/layout.hpp" // formats::iequals (case-insensitive tag compare, exe FUN_004df695 lane)
#include "wc98/presentation/data.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>

namespace wc98::presentation {

namespace {
// FSH tags are space-padded; compare trimmed case-insensitively (exe lookup FUN_004df695).
std::string trimTag(std::string_view tag) {
    const std::size_t end = tag.find_last_not_of(" \t\0");
    return std::string(tag.substr(0, end == std::string_view::npos ? tag.size() : end + 1));
}
constexpr float kPi = 3.14159265358979323846f;
} // namespace

glm::mat4 venueMatrix(const VenueTransform& t) {
    // FUN_0047d2f0: each euler angle is multiplied by the 2*pi constant before sin/cos — the
    // entry rotation fields are turn units. Compose order translate -> euler -> scale matches
    // the FUN_00483fd8 frame (translate lane 0x47d7c0/0x47d7f4/0x47d820, euler 0x47d2f0,
    // scale 0x47cf1c, store 0x47d828). Axis order X,Y,Z [H] (multiply order lost in register args).
    const float rad = 2.0f * kPi; // _DAT_00566324 (double 2*pi)
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(t.pos[0], t.pos[1], t.pos[2]));
    m = glm::rotate(m, t.rotTurns[0] * rad, glm::vec3(1, 0, 0));
    m = glm::rotate(m, t.rotTurns[1] * rad, glm::vec3(0, 1, 0));
    m = glm::rotate(m, t.rotTurns[2] * rad, glm::vec3(0, 0, 1));
    m = glm::scale(m, glm::vec3(t.scale));
    return m;
}

BuiltVenue buildVenueMeshes(const formats::MdlFile& mdl, const formats::FshFile& pak,
                            const formats::FshFile& fsh) {
    BuiltVenue out;
    out.valid = !mdl.groups.empty();

    // Loader material lane (FUN_00483418): the name-table record maps to a material slot —
    // "0000"/flag==0 = packed RGB colour (type 0), named = white + case-insensitive lookup
    // (type 1). The exe loads both tables and resolves through them: the venue .PAK first, then
    // the companion .FSH (same key: MDL tag must match the FSH tag, the test_mdl venueChecks
    // binding).
    std::vector<const formats::FshBitmap*> bound;
    out.materials.reserve(mdl.textures.size());
    for (const auto& t : mdl.textures) {
        VenueMaterialSpec m;
        m.tag = trimTag(t.name);
        if (m.tag.empty() || m.tag == "0000" || t.flag == 0) {
            m.tag.clear();
            m.rgba = render::packRgba(std::uint8_t(t.color & 0xFF), std::uint8_t((t.color >> 8) & 0xFF),
                                      std::uint8_t((t.color >> 16) & 0xFF));
        } else {
            for (const auto* table : {&pak, &fsh}) {
                for (const auto& e : table->entries) {
                    if (!e.bitmap) continue;
                    if (formats::iequals(trimTag(e.tag), m.tag)) {
                        m.texW = e.bitmap->width;
                        m.texH = e.bitmap->height;
                        bound.push_back(&e.bitmap.value());
                        break;
                    }
                }
                if (m.texW) break;
            }
        }
        out.materials.push_back(std::move(m));
    }
    std::sort(bound.begin(), bound.end());
    bound.erase(std::unique(bound.begin(), bound.end()), bound.end());
    out.resolvedBitmaps = bound.size();

    for (const auto& group : mdl.groups) {
        // One mesh per (group, texture) pair; the exe rasterizer draws faces grouped per material.
        std::map<std::uint32_t, VenuePartData> byTex;
        for (const auto& face : group.faces) {
            const std::size_t mid = out.materials.empty() ? 0 : std::min<std::size_t>(face.texture, out.materials.size() - 1);
            auto& part = byTex[face.texture];
            part.material = mid;
            const VenueMaterialSpec& mat = out.materials.empty()
                ? VenueMaterialSpec::emptyFlat()
                : out.materials[mid];
            for (int c = 0; c < 3; ++c) {
                const auto& p = face.vertices[c] < group.positions.size() ? group.positions[face.vertices[c]]
                                                                          : group.positions.front();
                // File uv is already [0,1] (same convention as tools/model-view). The exe load lane
                // multiplies it by the texture extent for its texel-space software sampler
                // (FUN_00483418); the GPU sampler keeps the normalized file uv.
                part.mesh.vertices.push_back({
                    {p[0], p[1], p[2]},
                    {0.0f, 1.0f, 0.0f},
                    {face.uv[2 * c], face.uv[2 * c + 1]},
                    mat.rgba,
                });
            }
            const std::uint32_t base = std::uint32_t(part.mesh.vertices.size()) - 3u;
            part.mesh.indices.insert(part.mesh.indices.end(), {base, base + 1, base + 2});
            ++part.triangles;
            out.triangles += 1;
        }
        for (auto& [tex, part] : byTex) {
            for (const auto& v : part.mesh.vertices) {
                out.bboxMin[0] = std::min(out.bboxMin[0], v.pos.x);
                out.bboxMin[1] = std::min(out.bboxMin[1], v.pos.y);
                out.bboxMin[2] = std::min(out.bboxMin[2], v.pos.z);
                out.bboxMax[0] = std::max(out.bboxMax[0], v.pos.x);
                out.bboxMax[1] = std::max(out.bboxMax[1], v.pos.y);
                out.bboxMax[2] = std::max(out.bboxMax[2], v.pos.z);
            }
            out.parts.push_back(std::move(part));
        }
    }
    return out;
}

bool loadVenueFromTree(const std::filesystem::path& treeRoot, std::size_t venueIndex, VenueFiles& files) {
    const auto& table = formats::venueTable();
    if (venueIndex >= table.size()) return false;
    const auto& entry = table[venueIndex];
    auto upper = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    };
    // Same resolution as the test_mdl venueChecks gate: exe lowercase paths -> upper-case CD dirs.
    const std::string mdlName = upper(std::string(entry.mdlPath)).substr(3); // "FE/PDPF.MDL"
    const std::string pakName = upper(std::string(entry.pakPath)).substr(8);  // "FE/PACK/PDPF.PAK"
    const std::string fshName = upper(std::string(entry.mdlPath)).substr(3);  // companion "FE/<TAG>.FSH"
    std::filesystem::path fshName8 = fshName;
    fshName8 = fshName8.parent_path() / (fshName8.stem().string() + ".fsh");
    const std::filesystem::path mdlFile = wc98::findCaseInsensitive(treeRoot / "FE", mdlName);
    const std::filesystem::path pakFile = wc98::findCaseInsensitive(treeRoot / "FE" / "PACK", pakName);
    if (mdlFile.empty() || pakFile.empty()) return false;
    files.venueIndex = venueIndex;
    files.mdl = formats::MdlFile::parse(wc98::readFile(mdlFile));
    files.pak = formats::parseVenuePak(std::span<const std::uint8_t>{wc98::readFile(pakFile)});
    // Companion table (exe FUN_00483418 loads fe\<tag>.fsh alongside the pak); optional: the
    // tree stores it next to the .MDL for every venue slot. It is a plain SHPI file (own tag
    // table), so parse as FshFile, not as a pak container.
    const std::filesystem::path fshFile = wc98::findCaseInsensitive(treeRoot / "FE", fshName8.filename().string());
    files.fsh = formats::FshFile{};
    if (!fshFile.empty()) {
        try {
            files.fsh = formats::FshFile::parse(std::span<const std::uint8_t>{wc98::readFile(fshFile)});
        } catch (const std::exception&) {
            files.fsh = formats::FshFile{}; // [H] malformed companion: pak-only resolution
        }
    }
    return true;
}

VenueAsset uploadVenue(render::Renderer& r, const BuiltVenue& built, const VenueFiles& files,
                       std::string_view cacheKeyPrefix) {
    VenueAsset out;
    out.cpu = built;
    out.materials.reserve(built.materials.size());
    for (const auto& m : built.materials) {
        render::Material mat;
        mat.doubleSided = true; // exe software lane rasterizes polygons without winding cull [H]
        if (!m.tag.empty() && m.texW && m.texH) {
            for (const formats::FshFile* table : {&files.pak, &files.fsh}) {
                for (const auto& e : table->entries) {
                    if (!e.bitmap) continue;
                    if (formats::iequals(trimTag(e.tag), m.tag)) {
                        mat.texture = r.createTexture(toImage(*e.bitmap), render::Wrap::Clamp,
                                                      std::string(cacheKeyPrefix) + ":" + m.tag);
                        break;
                    }
                }
                if (mat.texture) break;
            }
        }
        out.materials.push_back(mat);
    }
    out.meshes.reserve(built.parts.size());
    for (const auto& part : built.parts)
        out.meshes.push_back(r.createMesh(part.mesh, std::string(cacheKeyPrefix) + ":" + std::to_string(out.meshes.size())));
    return out;
}

std::vector<render::DrawItem> VenueFrame::drawItems() const {
    std::vector<render::DrawItem> items;
    const glm::mat4 model = venueMatrix(transform);
    for (std::size_t i = 0; i < asset.cpu.parts.size(); ++i) {
        render::DrawItem it;
        it.mesh = asset.meshes[i];
        it.material = asset.materials[asset.cpu.parts[i].material];
        it.model = model;
        items.push_back(it);
    }
    return items;
}

render::Camera VenueFrame::framedCamera() const {
    const auto& b0 = asset.cpu.bboxMin;
    const auto& b1 = asset.cpu.bboxMax;
    glm::vec3 center{(b0[0] + b1[0]) * .5f, (b0[1] + b1[1]) * .5f, (b0[2] + b1[2]) * .5f};
    // exe bbox offsets (+0x50/+0x54) are added to projected 2D coords; the port frames with them
    // added to the world center instead [H] (same offset values, port-side framing).
    center.x += float(transform.bboxOff[0]);
    center.y += float(transform.bboxOff[1]);
    const glm::vec3 size{std::max(1.f, b1[0] - b0[0]), std::max(1.f, b1[1] - b0[1]), std::max(1.f, b1[2] - b0[2])};
    const float radius = 0.5f * std::sqrt(size.x * size.x + size.y * size.y + size.z * size.z);
    const float fov = 45.0f;
    const float dist = (radius / std::tan(fov * 0.5f * kPi / 180.0f)) * 1.05f * std::max(1.0f, transform.scale);
    render::Camera cam;
    cam.eye = center + glm::vec3(0, 0, dist);
    cam.target = center;
    cam.nearZ = std::max(0.01f, dist - radius * 2.f - 1.f);
    cam.farZ = dist + radius * 2.f + 1.f;
    return cam;
}

} // namespace wc98::presentation
