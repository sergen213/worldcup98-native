// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// RGBA8 -> PNG encoding (libpng), used by the texture/font exporters.
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats {
// `rgba` holds width*height*4 bytes, top-down. Throws std::runtime_error on failure.
std::vector<std::uint8_t> encodePng(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba);
}
