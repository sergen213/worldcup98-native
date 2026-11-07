// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// WC98 FE overlay archives (.SQF): 'EULZ' container wrapping a compressed Mac resource fork
// ('*RTFD') of FOOTIX front-end resources. Measured on all 49 FE/*.SQF files; layout, the
// runtime loader contract and the open EULZ-expansion item: docs/asset-formats/SQF_FOOTIX.md.
//
// Container (all little-endian):
//   +0x00  'EULZ'          magic
//   +0x04  '*RTFD\0'       payload kind signature (Mac resource fork), byte[7] == 'F'
//   +0x08  u32             constant 68 on every file (semantics Open)
//   +0x0C  u32             resource-type count of this archive (census 16..280; non-zero checked by loader)
//   +0x10  16 bytes        UUID-ish id; creation date as u32be at +0x18 (ends 0x0139ed38 family)
//   +0x28  16 bytes        second UUID-ish id; date u32be at +0x38
//   +0x44  payload         EULZ-compressed stream, size = filesize - 0x44
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats::sqf {

struct SqfArchive {
    std::uint32_t archiveCount = 68; // u32@+0x08, constant on all 49 files (semantics Open)
    std::uint32_t typeCount = 0;     // u32@+0x0C: resource types in this archive
    std::vector<std::uint8_t> ids;   // 48 id/date bytes at +0x10 (two UUIDs + two BE dates)
    std::span<const std::uint8_t> payload; // compressed stream at +0x44

    // Raw compressed member view. The EULZ expander is NOT implemented offline: the runtime
    // loader calls a function pointer at [DAT_00593e94] that is NULL in FIFAWC.EXE (zero static
    // xrefs to the slot; SQF loads fail under Wine), so no decompressed sample exists anywhere.
    // See docs/asset-formats/SQF_FOOTIX.md §"Open items".
};

// Parses the fixed header; throws FormatError on magic/count/payload-length violations.
[[nodiscard]] SqfArchive parseSqf(std::span<const std::uint8_t> data);

// True when the buffer starts with an EULZ SQF container (magic + '*RTFD\0' kind).
[[nodiscard]] bool isSqf(std::span<const std::uint8_t> data);

} // namespace wc98::formats::sqf
