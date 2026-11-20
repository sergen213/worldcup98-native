// pass-108: FE venue fly-in render lane — the port mirror of the exe venue loader/transform
// frame (FUN_00483418 load + FUN_00483fd8 compose). Authority: analysis/pass105venue/NOTES.md,
// analysis/pass106venue/NOTES.md, analysis/pass108venue/NOTES.md. Field roles and constants come
// from exe bytes/decompile; where a detail is not recovered from the exe it is marked [H] and the
// port-side choice is stated. The exe draws venues with a software polygon rasterizer
// (FUN_00482d7c); the port reproduces the same semantics — material slots, UV texel scaling, entry
// transform fields, mode enum and animation accumulator — through the existing GPU mesh pipeline.
#pragma once
#include "wc98/formats/fsh.hpp"
#include "wc98/formats/mdl.hpp"
#include "wc98/formats/venue.hpp"
#include "wc98/renderer/renderer.hpp"

#include <glm/mat4x4.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wc98::presentation {

// Exe entry transform fields (pass-106): f32 position[3] entry+0x28..0x30, f32 rotation[3]
// entry+0x38..0x40 (turns: FUN_0047d2f0 multiplies each angle by _DAT_00566324, a double 2*pi,
// so the field is in 0..1 turn units), f32 scale entry+0x4c (FUN_0047cf1c), i32 bbox offset pair
// entry+0x50/+0x54, mode enum entry+0x20.
struct VenueTransform {
    std::array<float, 3> pos{0.f, 0.f, 0.f};
    std::array<float, 3> rotTurns{0.f, 0.f, 0.f};
    float scale = 1.0f;
    std::array<std::int32_t, 2> bboxOff{0, 0}; // entry+0x50/+0x54 (exe: added to projected 2D coords)
    std::uint32_t mode = 0;                   // 0 static; 1/2 animated accumulator lane; >2 silhouette collector (exe-only)
};

// Animation accumulator constants (FUN_00483fd8: `DAT_0057e340 += (float)_DAT_005665d4` for
// mode 1, `_DAT_005665cc` for mode 2 while the animation flag `DAT_0057e318` is 1). Byte-verified
// DGROUP doubles: 0x5665d4 = 0x3f689374bc6a7efa = 0.003, 0x5665cc = 0x3f9eb851eb851eb8 = 0.03.
inline constexpr float kVenueAnimDeltaMode1 = 0.003f; // _DAT_005665d4
inline constexpr float kVenueAnimDeltaMode2 = 0.03f;  // _DAT_005665cc

// model->world = translate(pos) * rotate(rotTurns * 2*pi) * scale, the FUN_0047d2f0/47cf1c/47d828
// compose lane (identity -> euler -> scale -> matrix-stack store). The exact euler multiply order
// is lost in the Watcom register args of FUN_0047cea8/FUN_0047cf84 [H]; X,Y,Z order assumed.
glm::mat4 venueMatrix(const VenueTransform& t);

// CPU-side material slot from the loader material lane (FUN_00483418): the MDL name-table record
// is either a packed colour (`tag` empty, rgba from bytes 0..2) or a FSH texture named by tag
// (exe case-insensitive name lookup FUN_004df695). Texture dimensions drive the load-lane UV
// multiply (uv *= texture extent -> texel space); the port stores normalized [0,1] uv. The exe
// texture record width/height field order (+2 vs +4) is not recovered [H]; port: width->u,
// height->v.
struct VenueMaterialSpec {
    std::uint32_t rgba = 0xFFFFFFFFu; // vertex colour: flat slot colour, or white for textured slots
    std::string tag;                  // FSH entry tag; empty = flat colour slot
    std::uint32_t texW = 0, texH = 0; // bitmap extent when `tag` resolved in the venue .PAK
    static VenueMaterialSpec emptyFlat() { return {}; }
};

struct VenuePartData { // one draw-list group, split per material
    render::MeshData mesh;
    std::size_t material = 0;
    std::uint32_t triangles = 0;
};

// Parses MDL + venue .PAK (+ companion .FSH) into meshes/material slots (no GPU). Every face
// references a material; texture-named materials resolve a bitmap from the pak first, then the
// companion fsh (exe loader FUN_00483418 loads both tables); unresolved tags keep white vertex
// colour and no extent. `resolvedBitmaps` counts distinct bitmaps actually bound to a material.
struct BuiltVenue {
    std::vector<VenueMaterialSpec> materials; // one per MdlTextureRef, file order
    std::vector<VenuePartData> parts;
    std::array<float, 3> bboxMin{0.f, 0.f, 0.f}, bboxMax{0.f, 0.f, 0.f};
    std::uint32_t triangles = 0;
    std::size_t resolvedBitmaps = 0; // distinct pak/fsh bitmaps bound to a material
    bool valid = false;
};
BuiltVenue buildVenueMeshes(const formats::MdlFile& mdl, const formats::FshFile& pak,
                            const formats::FshFile& fsh);

// Tree loader for the exe entry paths (FUN_00483418 resolution): "fe\<tag>.mdl" -> FE/<TAG>.MDL,
// "fe\pack\<tag>.pak" -> FE/PACK/<TAG>.PAK, plus the companion "fe\<tag>.fsh" the loader loads as
// a second table, resolved case-insensitively against the extracted tree (the CD stores upper
// case, the exe strings are lowercase). Returns false when the mdl/pak files are absent (the
// companion fsh is optional); malformed mdl/pak files throw (same exceptions as the formats
// parsers).
struct VenueFiles {
    formats::MdlFile mdl;
    formats::FshFile pak;
    formats::FshFile fsh; // companion fe\<tag>.fsh; empty when the file is absent
    std::size_t venueIndex = 0;
};
bool loadVenueFromTree(const std::filesystem::path& treeRoot, std::size_t venueIndex, VenueFiles& files);

// GPU upload of BuiltVenue; textures resolve from `files.pak` first, then `files.fsh` (exe loader
// loads both tables); `cacheKeyPrefix` keys textures/meshes ("venue:lensf").
struct VenueAsset {
    std::vector<render::DrawItem> draws; // material/mesh pairs, model matrix set per frame
    std::vector<render::Material> materials;
    std::vector<render::MeshId> meshes;  // parallel to BuiltVenue::parts
    BuiltVenue cpu;
};
VenueAsset uploadVenue(render::Renderer& r, const BuiltVenue& built, const VenueFiles& files,
                       std::string_view cacheKeyPrefix);

// One venue slot's runtime state: loaded asset + exe entry transform + animation accumulator
// (DAT_0057e340). advance() mirrors the FUN_00483fd8 lane: modes 1/2 add the exe delta per frame
// while `animated` (DAT_0057e318 == 1). The consumption site of the accumulator is inside the
// matrix-stack lane, invisible in the decompile [H] — the value stays observable state.
class VenueFrame {
public:
    VenueAsset asset;
    VenueTransform transform;
    float accumulator = 0.0f; // DAT_0057e340

    void advance(bool animated) {
        if (!animated) return;
        if (transform.mode == 1) accumulator += kVenueAnimDeltaMode1;
        else if (transform.mode == 2) accumulator += kVenueAnimDeltaMode2;
    }
    // DrawItems with the composed venueMatrix(transform).
    std::vector<render::DrawItem> drawItems() const;
    // Framing mirror: a camera that fits the venue bbox (+ offset) into the 4:3 view. The exe
    // lane fits projected vertex extents against the screen rect (FUN_00482d7c clamp arithmetic);
    // the port frames the world bbox instead — documented port-side framing [H].
    render::Camera framedCamera() const;
};

} // namespace wc98::presentation
