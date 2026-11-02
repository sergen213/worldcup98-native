// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// EA front-end "GEOM"/"SKEL" polygon model (.MDL, the 12 files in FE/) — a 1998-only format
// (FIFA 99 has none; research-inbox/F99_REUSABILITY_MATRIX.md section 6). Layout evidence and
// confidence for every field: docs/reverse-engineering/MDL_GEOMETRY_NOTES.md.
//
// File = magic + LE u32 counts, a texture-name table of [u32 colour][char[4] FSH tag][u32 flag]
// records (stride 12), per-object configuration blocks (opaque to this parser — skipped), then
// one or more draw-list groups. Each group header (stride 36) stores its own geometry:
//   [u32 next?][f32 ?][u32 0][u32 faceCount][u32 vertexCount]
//   [u32 facesOff == header+36][u32 vertsOff == facesOff + 40*faceCount]
//   [u32 vertsEnd == vertsOff + 16*vertexCount][u32 end]
// Faces are triangles with per-corner UV and a texture-name index; positions are float xyz + w=0.
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wc98::formats {

struct MdlTextureRef { // name-table record (stride 12)
    std::uint32_t color = 0; // RGB in bytes 0..2 (e.g. 0x00b2b2b2 on untextured "0000" entries)
    std::string name;        // FSH entry tag, e.g. "wc07"; "0000" = no texture
    std::uint32_t flag = 0;  // 1 on most named textures, 0 otherwise (meaning unverified)
};

struct MdlFace { // draw-list record (stride 40)
    std::uint32_t texture = 0;            // index into MdlFile::textures
    std::array<std::uint32_t, 3> vertices{}; // indices into the owning group's positions
    std::array<float, 6> uv{};            // (u,v) per corner, texture space (origin top-left)
};

struct MdlGroup { // one draw-list: header at `headerOffset`, faces immediately after it
    std::uint32_t headerOffset = 0;
    std::vector<MdlFace> faces;
    std::vector<std::array<float, 3>> positions;
};

struct MdlFile {
    std::string magic;                     // "GEOM" or "SKEL" (LPOLYBF.MDL)
    std::uint32_t objectCount = 0;         // header field @8: per-object blocks live between names and
                                           // the first group; count ≠ groups on SKEL files (role
                                           // unverified — not parsed here)
    std::vector<MdlTextureRef> textures;   // name table, indexed by MdlFace::texture
    std::vector<MdlGroup> groups;

    [[nodiscard]] static bool isMdl(std::span<const std::uint8_t> data);
    // Parses names and every draw-list group; throws FormatError on malformed input.
    static MdlFile parse(std::span<const std::uint8_t> data);
};

} // namespace wc98::formats
