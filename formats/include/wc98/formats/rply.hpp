#pragma once
// WC98 replay container: user\rply%04d.dat + user\rplyindx.dat (docs/asset-formats/RPLY.md).
// String VAs 0x1604A4 / 0x1604B8; symbols FEREPLAYBUFF 0x1604D4, replayfile 0x1604E4.
// Layout CONFIRMED from Ghidra pass 8 (analysis/pass8/rply_decomp_*.txt): loader FUN_00495d10
// copies a 142-dword (loop 0x8e) header table; the record walk FUN_004963b0/FUN_00496400 advances
// by a big-endian u16 total size at each record start (= size includes those two bytes);
// FUN_004962d0 decodes the exe payload as 71 control bytes (record+2, loop < 0x47) of 2-bit codes
// (0x80 -> BE16 int16 delta, 0x40 -> i8 delta) over an int16 channel array with data at record+0x49.
// This codec models the CONTAINER framing exactly; the exe payload is a delta state stream, and our
// native recorder writes its own dialect payload [u32 LE tick][sim ControllerState image] (app/replay.cpp).
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "wc98/reader.hpp" // FormatError

namespace wc98::formats {

inline constexpr std::size_t kRplyTableDwords = 142;            // FUN_00495d10 loop count 0x8e
inline constexpr std::size_t kRplyHeaderBytes = 4 + 4 * kRplyTableDwords; // record area starts @572 (FUN_00495e60 +0x23c)
inline constexpr std::size_t kRplyIndexEntries = 100;           // browser scan cap, FUN_0043c4f0
inline constexpr std::size_t kRplyIndexEntryBytes = 25;         // entry stride 0x19

// One record including its BE16 size prefix (bytes [0..1] = total record length).
using RplyRecord = std::vector<std::uint8_t>;

struct RplyFile {
    std::uint32_t recordCount = 0;                       // u32 @ file offset 0x00
    std::array<std::uint32_t, kRplyTableDwords> table{}; // dwords @ +0x04 (dialect fields: RPLY.md)
    std::vector<RplyRecord> records;                     // size-prefixed sequence

    static RplyFile parse(std::span<const std::uint8_t> fileBytes); // FormatError on framing failure
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;      // round-trips byte-identical
};

// Exe record framing: BE16 total size (= payload.size()+2) followed by the payload bytes.
RplyRecord makeRplyRecord(std::span<const std::uint8_t> payload);
[[nodiscard]] std::size_t rplyRecordSize(const RplyRecord& r); // BE16 prefix value

struct RplyIndexEntry {
    std::uint32_t field0 = 0; // +0x00: unproven use in the exe browser (native writes 0)
    std::uint8_t number = 0;  // +0x04 replay slot number (browser sprintf source, *(entry+4))
    std::string name;         // +0x05 char[20] filename, NUL-padded
};

// Fixed 100 x 25-byte table. parse FormatError on size mismatch; serialize() emits exactly 2500 bytes.
std::vector<RplyIndexEntry> parseRplyIndex(std::span<const std::uint8_t> fileBytes);
std::vector<std::uint8_t> serializeRplyIndex(std::span<const RplyIndexEntry> entries);

} // namespace wc98::formats
