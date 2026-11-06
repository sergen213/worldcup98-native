#pragma once
// Generic WC98 save-block framing (docs/reverse-engineering/SAVESLOT_WC98.md,
// docs/reverse-engineering/OPTIONS_SAV_WC98.md §Read path). All three save kinds
// (options.sav, USER\SLOT%.2d.SAV, Pdat%02d.sav) share the reader chain
// FUN_004336d8/0043371c (open+size probe) -> FUN_00439870 / FUN_004398d0 (block read/write,
// on-disk image == live DGROUP block verbatim) -> chunk loop FUN_004c2ed0 + record verifier
// FUN_00439684. Evidence: analysis/pass7/slots_framing.txt, slots_framing2.txt.
//
// Record framing reconstruction (FUN_004c2ed0 loop + FUN_00439684): a save file is a sequence of
// records { u32 tag, u32 version/id, payload }, each verified by the CRC-16 walk FUN_004c8070:
// reflected polynomial 0xA001 (standard CRC-16/ARC table found in the image at DAT_00585d50),
// seed 0xFBEA, stored per record at descriptor+8 and checked by FUN_00439684.
//
// Documented deviation: native derives each record's payload size from a type registry (the
// strings "CCSJRPCCSSM" table consumed near FUN_00439620) which is not fully decoded; this codec
// therefore carries an explicit u32 payload length per record and trails the CRC after the
// payload. Round-trip identity: parse() + serialize() reproduces accepted bytes exactly.
#include <cstdint>
#include <span>
#include <vector>

#include "wc98/reader.hpp"  // FormatError

namespace wc98::formats {

constexpr std::uint32_t fourcc(char a, char b, char c, char d) {
    return std::uint32_t(std::uint8_t(a)) | (std::uint32_t(std::uint8_t(b)) << 8) |
           (std::uint32_t(std::uint8_t(c)) << 16) | (std::uint32_t(std::uint8_t(d)) << 24);
}

// FUN_004c8070: `for(i..){ u = (u >> 8) ^ tab[(u ^ byte) & 0xff]; }` with initial u = 0xFBEA.
constexpr std::uint16_t kCrcSeed = 0xFBEAu;
std::uint16_t crc16SaveFormat(std::span<const std::uint8_t> data, std::uint16_t seed = kCrcSeed);

struct SaveChunk {
    std::uint32_t tag = 0;
    std::uint32_t version = 0;
    std::vector<std::uint8_t> payload;
};

class SaveFrameStream {
public:
    // FormatError on truncation, length overflow, or CRC mismatch (native marks the slot invalid).
    static SaveFrameStream parse(std::span<const std::uint8_t> data);
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    void add(SaveChunk c) { chunks_.push_back(std::move(c)); }
    [[nodiscard]] const std::vector<SaveChunk>& chunks() const { return chunks_; }

private:
    std::vector<SaveChunk> chunks_;
};

}  // namespace wc98::formats
