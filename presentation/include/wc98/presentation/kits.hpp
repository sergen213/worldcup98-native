#pragma once
// WC98 (1998) kit textures. Delta vs FIFA 99: the original does NOT recolour one shared atlas at
// run time; INGAME/PLAYER/TEXTURES ships per-team pre-coloured GIMX atlases PK###<letter>.FSH
// (### = 3-digit FCDB team id, e.g. PK026A) with the shape tags the HIG/MED/LOW player models'
// materials reference (hjfr/hjbx/hslv/hllg/hulg/hclt/hrsv/hlsv + m*/l* LOD twins, ljer/hair, ...).
// Head skin: COM_30.FSH hhdc/hhdo; face composite "hifa" is built at load by layering the
// HHIG/HMED/HLOW fa##/hr##/fh## strips (9 faces x 13 hairs x 6 beards) over the head base.
// Goalkeepers: GK##_<lod>.FSH (18 sets, same tag layout). Referees: the B..E single-set atlases.
#include "wc98/formats/fcdb.hpp"
#include "wc98/presentation/data.hpp"
#include "wc98/renderer/renderer.hpp"

#include <array>
#include <map>
#include <string>

namespace wc98::presentation {

// Model LODs: INGAME/PLAYER/MODELS/{SHI_10,HIG_30,HIG_70,HIG_90,MED_*,LOW_*}.PC; the texture-set
// suffix (COM_<n>, GK##_<n>, HHIG/HMED/HLOW_<n>) is the model's number.
inline constexpr std::array<int, 4> kLodSuffixes{10, 30, 70, 90};

// FCDB player appearance: the 1998 record carries hairStyle (byte 6 hi nibble), hairColor
// (bits 2..3), skinTone (bits 0..1), faceStyle (byte 7 high 5 bits) and beardStyle (low 3 bits).
struct Look {
    int skin = 0;        // 0..3, FcdbPlayer::skinTone
    int hairColour = 1;  // 0..3, FcdbPlayer::hairColor
    int hairStyle = 0;   // FcdbPlayer::hairStyle (fa/hr/fh strip index = style % count)
    int facialHair = 0;  // FcdbPlayer::beardStyle
    int face = 0;        // FcdbPlayer::faceStyle
    int number = 0;      // FcdbPlayer::shirtNumber
};
[[nodiscard]] Look lookOf(const formats::FcdbPlayer& p);

// Strip counts measured on disk (HHIG_30.FSH directory): fa00..fa08, hr00..hr12, fh00..fh05.
inline constexpr int kFaceCount = 9, kHairCount = 13, kBeardCount = 6;

class KitTextures {
public:
    explicit KitTextures(const GameData& data);

    // PK###A exists for every team; PK###H is a distinct file (measured md5 differs). The original's
    // A/H + home/away selection table is not traced; the slice maps Home -> H when present else A,
    // Away -> A (SIM_PORT_NOTES.md follow-up: trace FIFAWC.EXE kit-file selection).
    [[nodiscard]] std::string kitFile(std::uint16_t teamId, bool away) const;
    // Referee/shared atlas B..E (single files); falls back to the team's own atlas.
    [[nodiscard]] std::string officialKitFile(int which) const;
    [[nodiscard]] std::string gkKitFile(int setIndex, int lodSuffix) const;   // GK##_<lod>.FSH
    [[nodiscard]] const formats::FshFile& kitFsh(const std::string& fileBase) const;  // "PK026A"

    // The model's material tags resolved to images: every tag found in the team atlas, the LOD's
    // COM_<n> shared set (skin/head/arms), and the composite faces built from HHIG/HMED/HLOW_<n>.
    // `lodSuffix` selects GK##_<n> for keepers. Missing tags are absent from the map.
    [[nodiscard]] std::map<std::string, render::Image> imagesFor(const std::string& fileBase, int lodSuffix, const Look& look) const;

    // 64x84 (HIG) head texture: fa<face>, hr<hairStyle % kHairCount>, fh<facialHair % kBeardCount>
    // layers over the hhdc base. Layer alpha is measured from disk data by the implementer
    // (indexed bitmaps carry no alpha channel; overlay indices are decoded, base kept).
    [[nodiscard]] render::Image faceComposite(int lodSuffix, const Look& look) const;

private:
    const formats::FshFile& fshAt(const std::string& fileBase) const; // loads + caches "PK026A" etc.
    const GameData& data_;
    mutable std::map<std::string, formats::FshFile> files_;
};

} // namespace wc98::presentation
