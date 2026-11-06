#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace wc98::formats {

// EA DCT video (.DCT): SCHl container of mTCD frame chunks, each carrying a
// PlayStation MDEC (SRE) bitstream. docs/asset-formats/VIDEO.md.
struct DctFrameHeader {
    int width = 0;
    int height = 0;
    std::uint32_t size = 0;   // full mTCD chunk size
    std::size_t offset = 0;   // chunk start in file
};

std::vector<DctFrameHeader> dctScan(std::span<const std::uint8_t> data);

struct DctFrame {
    int width = 0, height = 0;
    std::vector<std::uint8_t> y;  // w*h
    std::vector<std::uint8_t> u;  // (w/2)*(h/2), 128 = neutral
    std::vector<std::uint8_t> v;
};

// Decode one mTCD chunk to YUV420 (JPEG full range). Throws on corrupt stream.
DctFrame dctDecode(std::span<const std::uint8_t> data, const DctFrameHeader& hdr);

void dctToRgba(const DctFrame& f, std::vector<std::uint8_t>& rgba); // w*h*4

} // namespace wc98::formats
