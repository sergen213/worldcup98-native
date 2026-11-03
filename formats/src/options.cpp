// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/options.hpp"
#include "wc98/reader.hpp"
#include <algorithm>
#include <format>

namespace wc98::formats {
namespace {
constexpr std::uint32_t rd32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint32_t(b[at]) | (std::uint32_t(b[at + 1]) << 8) | (std::uint32_t(b[at + 2]) << 16) |
           (std::uint32_t(b[at + 3]) << 24);
}
constexpr void wr32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = std::uint8_t(v >> (8 * i));
}
} // namespace

OptionsSave OptionsSave::parse(std::span<const std::uint8_t> data) {
    Reader r(data);
    if (data.size() < kOptionsBlockSize)
        throw FormatError(std::format("options.sav: {} bytes, need {}", data.size(), kOptionsBlockSize));
    const auto m0 = r.u32le();
    const auto m1 = r.u32le();
    // FUN_0046b350 re-validates both magic dwords and defaults the whole block on mismatch.
    if (m0 != kOptionsMagic || m1 != kOptionsMagic)
        throw FormatError(std::format("options.sav: bad magic {:08x}/{:08x}", m0, m1));
    OptionsSave o;
    o.raw.assign(data.begin(), data.begin() + kOptionsBlockSize);
    o.syncFromRaw();
    if (o.lang >= 9) // loader gate: DAT_0060cbcb in [0,8) plus the installer's 9th slot check
        throw FormatError(std::format("options.sav: language byte {} out of range", o.lang));
    return o;
}

void OptionsSave::syncFromRaw() {
    const auto& b = raw;
    stadium = std::uint16_t(rd32(b, 0x08) & 0xFFFF);
    dayNight = b[0x0A];
    gameSpeed = rd32(b, 0x14);
    detailFields = rd32(b, 0x18);
    detailStadium = rd32(b, 0x1C);
    halfLength = rd32(b, 0x24);
    clockStyle = rd32(b, 0x28);
    lang = b[kOptionsLangOffset];
    volumeBlock = rd32(b, 0x60);
    for (int i = 0; i < 6; ++i) cameraModes[std::size_t(i)] = rd32(b, 0x64 + 4 * i);
}

void OptionsSave::syncToRaw() {
    raw[0x08] = std::uint8_t(stadium & 0xFF);
    raw[0x09] = std::uint8_t(stadium >> 8); // u16 pair only; +0x0A gets dayNight below
    raw[0x0A] = dayNight;
    wr32(raw, 0x14, gameSpeed);
    wr32(raw, 0x18, detailFields);
    wr32(raw, 0x1C, detailStadium);
    wr32(raw, 0x24, halfLength);
    wr32(raw, 0x28, clockStyle);
    raw[kOptionsLangOffset] = lang;
    wr32(raw, 0x60, volumeBlock);
    for (int i = 0; i < 6; ++i) wr32(raw, 0x64 + 4 * i, cameraModes[std::size_t(i)]);
}

OptionsSave OptionsSave::defaults(std::uint8_t lang) {
    OptionsSave o;
    o.raw.assign(kOptionsBlockSize, 0);
    wr32(o.raw, 0x00, kOptionsMagic);
    wr32(o.raw, 0x04, kOptionsMagic);
    o.lang = lang;
    o.syncToRaw(); // writes lang back at +0x23
    return o;
}

std::vector<std::uint8_t> OptionsSave::serialize() const { return raw; }

} // namespace wc98::formats
