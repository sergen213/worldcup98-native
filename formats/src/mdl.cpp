// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/mdl.hpp"

#include "wc98/reader.hpp"

#include <cmath>
#include <format>
#include <string_view>

namespace wc98::formats {

namespace {

constexpr std::size_t kNameRecordSize = 12; // [u32 colour][char[4] name][u32 flag]
constexpr std::size_t kGroupHeaderSize = 36;
constexpr std::size_t kFaceRecordSize = 40;  // [u32 tex][3×u32 vert][6×f32 uv]
constexpr std::size_t kPositionRecordSize = 16; // [f32 x][f32 y][f32 z][f32 w=0]

bool saneUv(const MdlFace& f) {
    for (const float c : f.uv)
        if (!std::isfinite(c) || std::abs(c) > 64.0f) return false;
    return true;
}

} // namespace

bool MdlFile::isMdl(std::span<const std::uint8_t> data) {
    const std::string_view head(reinterpret_cast<const char*>(data.data()), // NOLINT: byte span
                                data.size() < 4 ? data.size() : 4);
    return data.size() >= 16 && (head == "GEOM" || head == "SKEL");
}

MdlFile MdlFile::parse(std::span<const std::uint8_t> data) {
    Reader r(data);
    if (!isMdl(data)) throw FormatError("not a GEOM/SKEL model");
    MdlFile m;
    m.magic.assign(reinterpret_cast<const char*>(data.data()), 4); // NOLINT: isMdl checked the bytes
    r.skip(4);
    const std::uint32_t nameCount = r.u32le();
    m.objectCount = r.u32le();
    (void)r.u32le(); // @0x0c: constant 20 on all 12 FE files (meaning unverified)
    (void)r.u32le(); // @0x10: 0x14 + 12*nameCount — derived, not trusted
    (void)r.u32le(); // @0x14: always 0
    if (nameCount == 0 || nameCount > 4096 || kNameRecordSize * nameCount + 0x18 > data.size())
        throw FormatError(std::format("MDL name count {} implausible for a {} byte file", nameCount, data.size()));

    m.textures.reserve(nameCount);
    for (std::uint32_t i = 0; i < nameCount; ++i) {
        MdlTextureRef t;
        t.color = r.u32le();
        auto tag = r.bytes(4);
        t.name.assign(tag.begin(), tag.end());
        while (!t.name.empty() && (t.name.back() == '\0' || t.name.back() == ' ')) t.name.pop_back();
        t.flag = r.u32le();
        m.textures.push_back(std::move(t));
    }

    // Draw-list groups: located by their internal offset identities (header+36 == facesOff, etc.)
    // plus full face/position validation — no heuristic count guessing. Object configuration
    // blocks between the name table and the first group carry no geometry and are skipped; a
    // candidate inside them would have to reproduce all three offsets AND pass every index, UV
    // and position check below.
    std::size_t q = 0x18 + kNameRecordSize * nameCount;
    while (q + kGroupHeaderSize <= data.size()) {
        Reader h(data.subspan(q));
        (void)h.u32le(); // group 0: file offset of the animation/skeleton block; garbage elsewhere
        (void)h.f32le(); // unverified per-group float (1.7 on WCUPF, 68.4 on BRDXF)
        (void)h.u32le(); // always 0 in validated groups
        const std::uint32_t faceCount = h.u32le();
        const std::uint32_t vertexCount = h.u32le();
        const std::uint64_t facesOff = h.u32le();   // absolute file offsets
        const std::uint64_t vertsOff = h.u32le();
        const std::uint64_t vertsEnd = h.u32le();
        (void)h.u32le(); // end: >= vertsEnd by 0 or vertexCount*16 (stride hint, unverified)

        // The header describes its own placement exactly; that identity plus full record
        // validation below is what locates groups (no count fields elsewhere in the file).
        const bool chainOk = faceCount > 0 && vertexCount > 0 && faceCount <= 1u << 20 && vertexCount <= 1u << 20 &&
                             facesOff == q + kGroupHeaderSize && vertsOff == facesOff + kFaceRecordSize * faceCount &&
                             vertsEnd == vertsOff + kPositionRecordSize * vertexCount && vertsEnd <= data.size();
        if (!chainOk) { q += 4; continue; }

        MdlGroup g;
        g.headerOffset = std::uint32_t(q);
        bool ok = true;
        Reader f(data.subspan(std::size_t(facesOff)));
        for (std::uint32_t i = 0; i < faceCount && ok; ++i) {
            MdlFace face;
            face.texture = f.u32le();
            for (auto& v : face.vertices) v = f.u32le();
            for (auto& c : face.uv) c = f.f32le();
            ok = face.texture < m.textures.size() &&
                 face.vertices[0] < vertexCount && face.vertices[1] < vertexCount && face.vertices[2] < vertexCount &&
                 saneUv(face);
            if (ok) g.faces.push_back(face);
        }
        Reader p(data.subspan(std::size_t(vertsOff)));
        for (std::uint32_t i = 0; i < vertexCount && ok; ++i) {
            std::array<float, 3> pos{};
            for (auto& c : pos) c = p.f32le();
            const float w = p.f32le(); // padding zero on every position of the 12 FE files
            ok = std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]) && w == 0.0f;
            if (ok) g.positions.push_back(pos);
        }
        if (!ok || g.faces.size() != faceCount) { q += 4; continue; }
        m.groups.push_back(std::move(g));
        q = std::size_t(vertsEnd); // skip past this group's records; scanning resumes from there
    }

    if (m.groups.empty()) throw FormatError("MDL has no draw-list groups");
    return m;
}

} // namespace wc98::formats
