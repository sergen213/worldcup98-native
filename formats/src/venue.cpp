// WC98-specific decode (pass-105), no fifa99-native lineage.
#include "wc98/formats/venue.hpp"
#include "wc98/formats/refpack.hpp"

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

} // namespace wc98::formats
