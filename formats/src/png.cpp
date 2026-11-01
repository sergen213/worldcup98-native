// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/png.hpp"
#include <png.h>
#include <stdexcept>

namespace wc98::formats {

std::vector<std::uint8_t> encodePng(std::uint32_t w, std::uint32_t h, std::span<const std::uint8_t> rgba) {
    if (w == 0 || h == 0 || rgba.size() != std::size_t(w) * h * 4) throw std::runtime_error("encodePng: bad image size");
    png_image img{};
    img.version = PNG_IMAGE_VERSION;
    img.width = w;
    img.height = h;
    img.format = PNG_FORMAT_RGBA;
    png_alloc_size_t size = 0;
    if (!png_image_write_get_memory_size(img, size, 0, rgba.data(), 0, nullptr))
        throw std::runtime_error(std::string("encodePng: ") + img.message);
    std::vector<std::uint8_t> out(size);
    if (!png_image_write_to_memory(&img, out.data(), &size, 0, rgba.data(), 0, nullptr))
        throw std::runtime_error(std::string("encodePng: ") + img.message);
    out.resize(size);
    return out;
}

} // namespace wc98::formats
