// Derived from fifa99-native (same author), ported for World Cup 98.
// NOT ported from FIFA 99: the exe_tables.inc palette machinery (Kit_applyPlayerPalette 0x413A9C,
// Kit_buildColourRamp 0x413724, paljer/shorts/socks/cleat/jnum aliasing and the kit-colour clash
// tables). World Cup 98 ships per-team PRE-COLOURED GIMX atlases INGAME/PLAYER/TEXTURES/PK###A/H.FSH;
// no runtime kit recolour exists in this data set (measured: PK026A palettes are baked, A and H tag
// sets identical, md5 differs). Face compositing (Face_composite 0x413F0C family) survives.
#include "wc98/presentation/kits.hpp"

#include "wc98/formats/refpack.hpp"

#include <algorithm>
#include <format>
#include <span>
#include <stdexcept>

namespace wc98::presentation {

using formats::FshBitmap;
using formats::FshFile;

namespace {

std::string_view trimTag(std::string_view t) {
    while (!t.empty() && (t.back() == ' ' || t.back() == '\0')) t.remove_suffix(1);
    return t;
}

const FshBitmap* shape(const FshFile& f, std::string_view tag) {
    for (const auto& e : f.entries) {
        if (e.bitmap && trimTag(e.tag) == tag) return &*e.bitmap;
    }
    return nullptr;
}
const FshBitmap& need(const FshFile& f, std::string_view tag, std::string_view file) {
    if (const FshBitmap* b = shape(f, tag)) return *b;
    throw std::runtime_error(std::format("{}: shape '{}' missing", file, tag));
}

// Transparency key for the fa/hr/fh overlay strips, THE knob (fixable in one place).
// Measured on disk (indices decoded from the type-0xFB compressed 8-bit records; each strip carries
// its own attached 0x2A BGRA palette record):
//   HHIG_30.FSH fh00 bitmap record @0x10100: index 0xFF covers 95.2 % of the 64x84 pixels
//     (clean-shaven style), fh01 98.7 %, fh05 97.8 %; hr00 @0x7bd8 has 35.6 % 0xFF (hair is a keyed
//     overlay too); fa00..fa08 and HMED_70 fa## contain ZERO 0xFF pixels -> face layer fully opaque.
// Palette[0xFF] of fh05's attached palette reads (255,0,0): a classic red-key marker colour.
constexpr std::uint8_t kOverlayKey = 0xFF;

struct FaceLayer {
    const FshBitmap* b = nullptr; // decoded strip (nullptr layer is skipped)
};

// Composites layers bottom-to-top into a w x h RGBA canvas. Pixels equal to kOverlayKey are
// transparent; every other pixel replaces the destination with that layer's own palette colour.
// Layers smaller than the canvas are nearest-neighbour scaled: measured fa## has no key pixels, so
// the COM head base (hhdc 64x32 under HHIG 64x84) never shows through; the scaling keeps the
// documented hhdc-base contract for a future exe trace of Face_composite's WC98 twin.
render::Image compositeLayers(int w, int h, std::span<const FaceLayer> layers) {
    render::Image img{std::uint32_t(w), std::uint32_t(h), std::vector<std::uint8_t>(std::size_t(w) * h * 4)};
    for (const FaceLayer& l : layers) {
        const FshBitmap* b = l.b;
        if (!b || b->indices.empty() || b->rgba.size() < std::size_t(b->width) * b->height * 4) continue;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int sy = std::min(h - 1, y * b->height / h), sx = std::min(w - 1, x * b->width / w);
                const std::size_t s = std::size_t(sy) * b->width + sx;
                if (b->indices[s] == kOverlayKey) continue;
                std::uint8_t* d = img.rgba.data() + (std::size_t(y) * w + x) * 4;
                const std::uint8_t* src = b->rgba.data() + s * 4;
                d[0] = src[0]; d[1] = src[1]; d[2] = src[2]; d[3] = 255; // head textures are opaque
            }
    }
    return img;
}

// fa<face % 9> + hr<hairStyle % 13> + fh<beardStyle % 6> over the COM head base (measured strip
// counts in HHIG_30.FSH: fa00..fa08, hr00..hr12, fh00..fh05; FIFA 99 Face_composite 0x413F0C order).
render::Image compositeStrips(const FshFile& head, std::string_view baseTag, const FshFile& strips,
                              std::string_view stripsBase, const Look& look) {
    const FshBitmap* base = shape(head, baseTag); // hhdc (HIG 64x32) / mhdc (MED 32x16) lives in COM_<n>
    const FshBitmap& fa = need(strips, std::format("fa{:02d}", ((look.face % kFaceCount) + kFaceCount) % kFaceCount), stripsBase);
    const FshBitmap& hr = need(strips, std::format("hr{:02d}", ((look.hairStyle % kHairCount) + kHairCount) % kHairCount), stripsBase);
    const FshBitmap& fh = need(strips, std::format("fh{:02d}", ((look.facialHair % kBeardCount) + kBeardCount) % kBeardCount), stripsBase);
    return compositeLayers(fa.width, fa.height, std::array<FaceLayer, 4>{FaceLayer{base}, FaceLayer{&fa}, FaceLayer{&hr}, FaceLayer{&fh}});
}

// Shape_mirrorX 0x407F7C family: horizontal flip of a decoded image (jersey backs).
render::Image mirrorX(const render::Image& in) {
    render::Image out{in.width, in.height, std::vector<std::uint8_t>(in.rgba.size())};
    for (std::uint32_t y = 0; y < in.height; ++y)
        for (std::uint32_t x = 0; x < in.width; ++x) {
            const std::size_t s = (std::size_t(y) * in.width + x) * 4, d = (std::size_t(y) * in.width + (in.width - 1 - x)) * 4;
            for (int k = 0; k < 4; ++k) out.rgba[d + k] = in.rgba[s + k];
        }
    return out;
}

constexpr std::string_view kTexturesDir = "INGAME/PLAYER/TEXTURES/";

std::string texPath(const std::string& fileBase) { return std::string(kTexturesDir) + fileBase + ".FSH"; }

} // namespace

Look lookOf(const formats::FcdbPlayer& p) {
    // WC98 1998-schema decode (formats/src/fcdb.cpp: byte 6 = hairStyle<<4 | hairColor<<2 | skinTone,
    // byte 7 = faceStyle<<3 | beardStyle); the decoded struct fields are used directly.
    return {int(p.skinTone), int(p.hairColor), int(p.hairStyle), int(p.beardStyle), int(p.faceStyle), p.shirtNumber};
}

KitTextures::KitTextures(const GameData& data) : data_(data) {}

std::string KitTextures::kitFile(std::uint16_t teamId, bool away) const {
    // Away kits: PK###A (exists for every FCDB id; measured PK026A). Home: PK###H is a distinct
    // pre-coloured file (measured md5 differs from A); fall back to A when absent.
    if (!away) {
        std::string h = std::format("PK{:03d}H", teamId);
        if (data_.exists(texPath(h))) return h;
    }
    return std::format("PK{:03d}A", teamId);
}

std::string KitTextures::officialKitFile(int which) const {
    // Referee/shared single-set atlases. Measured PK999B..E.FSH exist with the identical GIMX tag
    // set as PK###A (24 tags hclt hrsv hulg hllg hslv hlsv hjbk hjfr mjbk mjfr mslv mllg mulg mrsv
    // mlsv mclt ljer llar lgar luga luar lulg lllg hair, all type-0x7B records; probe of
    // INGAME/PLAYER/TEXTURES/PK999B.FSH). Fall back to a real team atlas if one is missing.
    static constexpr std::array<const char*, 4> kOfficials{"PK999B", "PK999C", "PK999D", "PK999E"};
    std::string base = kOfficials[std::size_t(which) % 4];
    if (data_.exists(texPath(base))) return base;
    return "PK026A";
}

std::string KitTextures::gkKitFile(int setIndex, int lodSuffix) const {
    // The contract header says 18 GK sets; measured on disk only GK00..GK05 x {_30,_70,_90} (18
    // files), and no _10 variant -> keepers at LOD 10 use the _30 set. Tags equal the PK layout
    // (probed GK00_30.FSH: tag set is a superset check against PK026A, identical).
    if (lodSuffix == 10) lodSuffix = 30;
    return std::format("GK{:02d}_{:d}", ((setIndex % 6) + 6) % 6, lodSuffix);
}

const FshFile& KitTextures::fshAt(const std::string& fileBase) const {
    auto it = files_.find(fileBase);
    if (it == files_.end()) {
        auto bytes = data_.file(texPath(fileBase));
        // Tree FSH files may be RefPack-wrapped; the SHPI container itself is then plain.
        it = files_.emplace(fileBase, FshFile::parse(formats::refpack::decompressIfNeeded(bytes))).first;
    }
    return it->second;
}

const FshFile& KitTextures::kitFsh(const std::string& fileBase) const { return fshAt(fileBase); }

render::Image KitTextures::faceComposite(int lodSuffix, const Look& look) const {
    // HIG family (SHI_10 + HIG_* pair with HHIG_<n>): 64x84 strips over the COM_<n> hhdc head base.
    const std::string com = "COM_" + std::to_string(lodSuffix);
    return compositeStrips(fshAt(com), "hhdc", fshAt("HHIG_" + std::to_string(lodSuffix)), "HHIG", look);
}

std::map<std::string, render::Image> KitTextures::imagesFor(const std::string& fileBase, int lodSuffix, const Look& look) const {
    std::map<std::string, render::Image> out;
    auto addAll = [&](const FshFile& f) {
        for (const auto& e : f.entries)
            if (e.bitmap && !e.bitmap->indices.empty()) out.try_emplace(std::string(trimTag(e.tag)), toImage(*e.bitmap));
    };
    addAll(fshAt(fileBase)); // team / GK / official atlas: hjfr hjbk hslv ... ljer hair

    const std::string com = "COM_" + std::to_string(lodSuffix); // shared skin/head set (measured: all four exist)
    if (data_.exists(texPath(com))) addAll(fshAt(com)); // hgvo harm hhdc mhdc ...

    auto tryFace = [&](const char* tag, const std::string& strips, const char* baseTag) {
        if (!data_.exists(texPath(strips))) return; // LOW lods have no face strips (atlas 'hair' only)
        out[tag] = compositeStrips(fshAt(com), baseTag, fshAt(strips), strips, look);
    };
    tryFace("hifa", "HHIG_" + std::to_string(lodSuffix), "hhdc"); // HIG materials sample hifa: no file carries the tag
    tryFace("mdfa", "HMED_" + std::to_string(lodSuffix), "mhdc"); // MED materials sample mdfa from HMED_<n> 32x42 strips

    // hjbx/mjbx are referenced by the HIG/MED model materials (matrix notes: hifa/hjbx/mdfa) but no
    // atlas tag contains them; FIFA 99 generates the mirrored jersey backs (R3DJBACK_updateJerseyBacksAI
    // 0x407C50, Shape_mirrorX 0x407F7C). Hypothesis carried over: mirror hjbk/mjbk. Shirt-number
    // glyphs are pre-baked in WC98's per-team atlases (no jnum digit fonts exist in the tree).
    for (auto [src, dst] : {std::pair{"hjbk", "hjbx"}, std::pair{"mjbk", "mjbx"}})
        if (auto img = fshImage(kitFsh(fileBase), src); img && !out.contains(dst)) out[dst] = mirrorX(*img);

    return out;
}

} // namespace wc98::presentation
