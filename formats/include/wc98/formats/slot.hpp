#pragma once
// USER/SLOT%.2d.SAV game-slot codec (docs/reverse-engineering/SAVESLOT_WC98.md).
// The on-disk file is the live DGROUP block image verbatim (FUN_0047b474 writes it through);
// the slot summary record carries format id 0x10000 at rec+0x04 and six u32 fields from
// rec+0x08, cached per slot in DAT_00616890 (stride 0x38) for the browser UI. Field semantics
// are Wine-oracle hypotheses; this codec keeps them as raw u32s plus a pass-through image so
// round-trip is byte-safe without samples (options.hpp pattern).
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "wc98/reader.hpp"  // FormatError

namespace wc98::formats {

constexpr std::uint32_t kSlotFormatId = 0x10000u;  // rec+0x04 header check (FUN_0047b67c)
constexpr std::uint32_t kSlotChunkTag = 0x544F4C53u;  // fourcc 'SLOT' (chunk tag invented here)
inline constexpr int kSlotCount = 20;  // FUN_0047b67c cap: slots iterate to min(dir count, 0x14)
inline constexpr std::size_t kSlotSummaryBytes = 6 * sizeof(std::uint32_t);

// Slot filename mirroring the native string "USER\SLOT%.2d.SAV" (@0x565fc8).
std::string slotFileName(int slot);

struct SlotSave {
    std::array<std::uint32_t, 6> summary{};  // rec+0x08..0x18 browser-cache fields (raw)
    std::vector<std::uint8_t> image;         // block tail / app payload, verbatim

    // Framed-stream parse (SaveFrameStream); FormatError on framing/CRC failure, missing SLOT
    // chunk, or version != kSlotFormatId.
    static SlotSave parse(std::span<const std::uint8_t> fileBytes);
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;  // single 'SLOT' record stream
};

struct SlotScanEntry {
    int slot = -1;
    bool valid = false;            // DAT_00616890 +0x00 flag: file parses and header matches
    std::array<std::uint32_t, 6> summary{};
};

// FUN_0047b67c mirror: scan slots 0..kSlotCount-1 under `dir` (USER/ directory), parse each,
// mark invalid on any failure.
std::vector<SlotScanEntry> scanSlots(const std::filesystem::path& dir);

}  // namespace wc98::formats
