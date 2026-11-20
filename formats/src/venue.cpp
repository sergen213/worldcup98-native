// WC98-specific decode (pass-105), no fifa99-native lineage.
#include "wc98/formats/venue.hpp"
#include "wc98/formats/refpack.hpp"
#include "wc98/reader.hpp"

#include <format>

namespace wc98::formats {

namespace {
// Exe venue table raw 0x17c4c8 (VA 0x57dec8), entry stride 0x5c; paths verbatim from the
// DGROUP string pool raw 0x16494a-0x164ba8 (VA 0x56634a+). Callback column: entry +0x0c —
// only slot 11 (`lpolybf`, callback 0x47b9a4 = scene-handler frame FUN_0047b9a4) is
// non-NULL in the shipped image.
constexpr VenueEntry kVenues[] = {
    {"fe\\pack\\pdpf.pak", "fe\\pdpf.mdl", "fe\\pdpf.fsh", false},
    {"fe\\pack\\stetf.pak", "fe\\stetf.mdl", "fe\\stetf.fsh", false},
    {"fe\\pack\\stdenisf.pak", "fe\\stdenisf.mdl", "fe\\stdenisf.fsh", false},
    {"fe\\pack\\lensf.pak", "fe\\lensf.mdl", "fe\\lensf.fsh", false},
    {"fe\\pack\\lyonf.pak", "fe\\lyonf.mdl", "fe\\lyonf.fsh", false},
    {"fe\\pack\\marsf.pak", "fe\\marsf.mdl", "fe\\marsf.fsh", false},
    {"fe\\pack\\mtpelf.pak", "fe\\mtpelf.mdl", "fe\\mtpelf.fsh", false},
    {"fe\\pack\\tlsef.pak", "fe\\tlsef.mdl", "fe\\tlsef.fsh", false},
    {"fe\\pack\\brdxf.pak", "fe\\brdxf.mdl", "fe\\brdxf.fsh", false},
    {"fe\\pack\\nantesf.pak", "fe\\nantesf.mdl", "fe\\nantesf.fsh", false},
    {"fe\\pack\\wcupf.pak", "fe\\wcupf.mdl", "fe\\wcupf.fsh", false},
    {"fe\\pack\\lpolybf.pak", "fe\\lpolybf.mdl", "fe\\lpolybf.fsh", true},
};
} // namespace

std::span<const VenueEntry> venueTable() {
    return kVenues;
}

FshFile parseVenuePak(std::span<const std::uint8_t> pak) {
    return FshFile::parse(refpack::decompressIfNeeded(pak));
}

namespace {
// DGROUP image range of the retail FIFAWC.EXE: VA 0x55A000 at file raw 0x158600
// (VA = raw + 0x401A00), same binding as the SCI registry parse (sci.cpp).
constexpr std::uint32_t kDgroupVa = 0x55a000u;
constexpr std::uint32_t kDgroupRaw = 0x158600u;
constexpr std::uint32_t kFlyaroundTableVa = 0x5714f8u; // 14 rows, stride 0x20
constexpr std::size_t kPaletteEntries = 12;
} // namespace

VenueFogLane parseVenueFogLane(std::span<const std::uint8_t> exeBytes) {
    const auto raw = [&](std::uint32_t va) {
        return static_cast<std::size_t>(va - kDgroupVa + kDgroupRaw);
    };
    const auto at = [&](std::uint32_t va, std::size_t n) {
        if (raw(va) + n > exeBytes.size())
            throw FormatError(std::format("venue fog lane: VA {0:x} outside image", va));
        return exeBytes.data() + raw(va);
    };
    const auto rgb = [&](std::uint32_t va, std::size_t idx) {
        const std::uint8_t* p = at(va + static_cast<std::uint32_t>(idx * 3), 3);
        return VenueRgb{p[0], p[1], p[2]};
    };

    VenueFogLane lane;
    for (std::size_t i = 0; i < lane.enable.size(); ++i)
        lane.enable[i] = at(kFlyaroundTableVa + i * 0x20 + 0x0c, 1)[0];
    for (std::size_t i = 0; i < kPaletteEntries; ++i) {
        lane.fogColors[i] = rgb(0x571e48u, i);
        lane.whiteColors[i] = rgb(0x571e6cu, i);
        lane.pitchColors[i] = rgb(0x571e90u, i);
    }
    return lane;
}

} // namespace wc98::formats
