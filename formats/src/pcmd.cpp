// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/pcmd.hpp"

#include "wc98/reader.hpp"

#include <algorithm>
#include <cstring>
#include <format>

namespace wc98::formats {
namespace {

float fixed(Reader& r) { return static_cast<float>(r.i32le()) / 65536.0f; }

std::array<std::uint8_t, 4> rgba(Reader& r) {
    auto b = r.bytes(4);
    return {b[0], b[1], b[2], b[3]};
}

void expectTag(Reader& r, const char* tag) {
    auto b = r.bytes(4);
    if (std::memcmp(b.data(), tag, 4) != 0) throw FormatError(std::format("PCMD: expected '{}' at {}", tag, r.pos() - 4));
}

// Element counts are bounded by what could possibly fit in the remaining bytes.
std::uint32_t count(Reader& r, std::size_t elemSize, const char* what) {
    std::uint32_t n = r.u32le();
    if (n > r.remaining() / elemSize) throw FormatError(std::format("PCMD: {} count {} exceeds data", what, n));
    return n;
}

} // namespace

bool PcmdModel::isPcmd(std::span<const std::uint8_t> d) { return d.size() >= 4 && std::memcmp(d.data(), "PCMD", 4) == 0; }

PcmdModel PcmdModel::parse(std::span<const std::uint8_t> data) {
    Reader r(data);
    expectTag(r, "PCMD");
    PcmdModel m;
    m.version = r.u32le();
    m.exporter = r.fixedString(20);
    m.headerFlag = r.u32le();

    expectTag(r, "MATR");
    m.materials.resize(count(r, 16, "material"));
    for (auto& mat : m.materials) {
        mat.type = r.u32le();
        mat.color = rgba(r);
        mat.texture = r.fixedString(8);
    }

    expectTag(r, "NODE");
    m.nodes.resize(count(r, 76, "node"));
    for (auto& n : m.nodes) {
        std::uint32_t type = r.u32le();
        if (type > 2) throw FormatError(std::format("PCMD: unknown node type {} at {}", type, r.pos() - 4));
        n.type = static_cast<PcmdNodeType>(type);
        n.name = r.fixedString(20);
        n.parent = r.i32le();
        n.sibling = r.i32le();
        n.child = r.i32le();
        for (auto& v : n.scale) v = fixed(r);
        for (auto& v : n.rotation) v = fixed(r);
        for (auto& v : n.translation) v = fixed(r);
        if (n.type == PcmdNodeType::Joint) {
            n.weights.resize(count(r, 24, "weight"));
            for (auto& w : n.weights) {
                for (auto& v : w.position) v = fixed(r);
                w.meshNode = r.u32le();
                w.vertex = r.u32le();
                w.percent = fixed(r);
            }
        } else if (n.type == PcmdNodeType::Mesh) {
            n.vertices.resize(count(r, 12, "vertex"));
            for (auto& v : n.vertices)
                for (auto& c : v) c = fixed(r);
            n.polygons.resize(count(r, 16, "polygon"));
            for (auto& p : n.polygons) {
                p.textured = r.u32le() != 0;
                p.vertexColors = r.u32le() != 0;
                p.material = r.u32le();
                p.corners.resize(count(r, 16, "corner"));
                if (p.material >= m.materials.size()) throw FormatError(std::format("PCMD: material {} out of range in '{}'", p.material, n.name));
                for (auto& c : p.corners) {
                    c.vertex = r.u32le();
                    c.u = fixed(r);
                    c.v = fixed(r);
                    c.color = rgba(r);
                }
            }
        }
    }

    // Optional tool trailer on loose files: "\0cRC" + 32-bit value.
    if (r.remaining() == 8 && std::memcmp(data.data() + r.pos(), "\0cRC", 4) == 0) m.crcTrailer = true;
    else if (r.remaining() != 0) throw FormatError(std::format("PCMD: {} unexpected trailing bytes", r.remaining()));

    // Cross-reference checks: skin influences must point at mesh nodes; corner indices at vertices.
    for (const auto& n : m.nodes)
        for (const auto& w : n.weights)
            if (w.meshNode >= m.nodes.size() || m.nodes[w.meshNode].type != PcmdNodeType::Mesh)
                throw FormatError(std::format("PCMD: weight on '{}' references node {} which is not a mesh", n.name, w.meshNode));
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        std::size_t nv = m.vertexCount(i);
        for (const auto& p : m.nodes[i].polygons)
            for (const auto& c : p.corners)
                if (c.vertex >= nv) throw FormatError(std::format("PCMD: corner vertex {} >= {} in '{}'", c.vertex, nv, m.nodes[i].name));
    }
    return m;
}

std::size_t PcmdModel::vertexCount(std::size_t meshNode) const {
    const auto& n = nodes.at(meshNode);
    if (!n.vertices.empty()) return n.vertices.size();
    std::size_t count = 0;
    for (const auto& j : nodes)
        for (const auto& w : j.weights)
            if (w.meshNode == meshNode) count = std::max<std::size_t>(count, w.vertex + 1);
    return count;
}

std::size_t PcmdModel::cornerCount() const {
    std::size_t c = 0;
    for (const auto& n : nodes)
        for (const auto& p : n.polygons) c += p.corners.size();
    return c;
}

CmapFile CmapFile::parse(std::span<const std::uint8_t> data) {
    Reader r(data);
    auto tag = r.bytes(4);
    if (std::memcmp(tag.data(), "CMAP", 4) != 0) throw FormatError("CMAP: bad magic");
    std::uint32_t size = r.u32le();
    CmapFile c;
    c.version = r.fixedString(4);
    c.sets = r.u32le();
    c.count = r.u32le();
    std::uint64_t n = std::uint64_t(c.sets) * c.count;
    if (size != data.size() || 20 + n * 4 != size) throw FormatError(std::format("CMAP: size {} does not match {} sets x {} colours", size, c.sets, c.count));
    c.colors.resize(n);
    for (auto& col : c.colors) col = rgba(r);
    return c;
}

} // namespace wc98::formats
