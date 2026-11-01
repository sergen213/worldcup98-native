// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// EA "PCMD" polygon model (.PC, .PC2, .XXX) and its companion "CMAP" per-corner colour map (.LM).
// See docs/formats/PCMD.md and docs/formats/CMAP.md for layouts and field confidence.
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wc98::formats {

struct PcmdMaterial {
    std::uint32_t type = 0;             // 1 = textured, 0 = flat colour (only seen with empty/"Zn" names)
    std::array<std::uint8_t, 4> color{}; // flat colour, byte order as stored (R,G,B,0 hypothesised)
    std::string texture;                 // FSH entry tag (4 chars in practice, field is 8 bytes)
};

struct PcmdCorner {
    std::uint32_t vertex = 0;            // index into the owning mesh's vertex list (or skin vertex list)
    float u = 0, v = 0;                  // 16.16 fixed in the file
    std::array<std::uint8_t, 4> color{}; // prelit RGB + flag byte; meaningful when PcmdPolygon::vertexColors
};

struct PcmdPolygon {
    bool textured = true;      // header word 0 (0 only on flat-colour shadow/pole polygons)
    bool vertexColors = false; // header word 1: corners carry prelit RGB
    std::uint32_t material = 0;
    std::vector<PcmdCorner> corners; // 3 or 4
};

// One skin influence stored on a joint node: the vertex position in that joint's frame.
struct PcmdWeight {
    std::array<float, 3> position{}; // 16.16 fixed
    std::uint32_t meshNode = 0;      // node index of the skinned mesh
    std::uint32_t vertex = 0;        // vertex index within that mesh
    float percent = 0;               // 16.16 fixed, influences of a vertex sum to 100
};

enum class PcmdNodeType : std::uint32_t { Null = 0, Mesh = 1, Joint = 2 };

struct PcmdNode {
    PcmdNodeType type = PcmdNodeType::Null;
    std::string name;
    std::int32_t parent = -1, sibling = -1, child = -1; // child = next joint of a Softimage chain for joints
    std::array<float, 3> scale{1, 1, 1};
    std::array<float, 4> rotation{0, 0, 0, 1}; // quaternion x,y,z,w (all zero on player joints)
    std::array<float, 3> translation{};
    std::vector<std::array<float, 3>> vertices; // Mesh: node-local positions (empty for skinned meshes)
    std::vector<PcmdPolygon> polygons;          // Mesh
    std::vector<PcmdWeight> weights;            // Joint
};

struct PcmdModel {
    std::uint32_t version = 0;    // 0x100 in every FIFA 99 file
    std::string exporter;         // "SOFTIMAGE" or "STADEX"
    std::uint32_t headerFlag = 0; // 1 on skinned models, 0 otherwise (exact meaning unverified)
    std::vector<PcmdMaterial> materials;
    std::vector<PcmdNode> nodes;
    bool crcTrailer = false;      // "\0cRC" + u32 appended to loose CD files

    [[nodiscard]] static bool isPcmd(std::span<const std::uint8_t> data);
    // Parses and validates indices (material, skin mesh node); throws FormatError.
    static PcmdModel parse(std::span<const std::uint8_t> data);
    // Vertex count of a mesh node: its own list, or 1 + highest skin-weight vertex index.
    [[nodiscard]] std::size_t vertexCount(std::size_t meshNode) const;
    // Total number of polygon corners in file order (the CMAP colour count).
    [[nodiscard]] std::size_t cornerCount() const;
};

// "CMAP" (.LM): `sets` groups of one RGBA colour per polygon corner of the matching .XXX model.
struct CmapFile {
    std::string version; // " 001"
    std::uint32_t sets = 0;
    std::uint32_t count = 0;
    std::vector<std::array<std::uint8_t, 4>> colors; // sets * count, set-major

    static CmapFile parse(std::span<const std::uint8_t> data);
    [[nodiscard]] std::span<const std::array<std::uint8_t, 4>> set(std::size_t i) const {
        return std::span(colors).subspan(i * count, count);
    }
};

} // namespace wc98::formats
