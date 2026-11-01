// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// EA RefPack (QFS) LZ77 compression, header 0x10FB / 0x11FB / 0x90FB. See docs/formats/REFPACK.md.
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats::refpack {
[[nodiscard]] bool isCompressed(std::span<const std::uint8_t> data);
// Declared uncompressed size from the header (throws if not RefPack).
[[nodiscard]] std::uint32_t uncompressedSize(std::span<const std::uint8_t> data);
// Decompresses a complete RefPack stream; throws FormatError on malformed input.
std::vector<std::uint8_t> decompress(std::span<const std::uint8_t> data);
// Returns data unchanged when not compressed, otherwise decompressed.
std::vector<std::uint8_t> decompressIfNeeded(std::span<const std::uint8_t> data);
}
