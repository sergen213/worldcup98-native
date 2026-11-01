// wc98/byte_reader.hpp — bounds-checked little-endian reader used by all
// file-format parsers (BIGF archives, FSH textures, FCDB databases, BNK banks,
// LAY layouts, ...). The 1998 EA engine is little-endian x86; every format in
// the project is parsed through this reader so that malformed/truncated
// original files fail loudly instead of reading out of bounds.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace wc98 {

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data) noexcept
        : data_(data) {}

    std::size_t size() const noexcept { return data_.size(); }
    std::size_t pos() const noexcept { return pos_; }
    bool eof() const noexcept { return pos_ >= data_.size(); }

    // Returns false and leaves pos unchanged if count bytes are unavailable.
    bool skip(std::size_t count) noexcept {
        if (data_.size() - pos_ < count) return false;
        pos_ += count;
        return true;
    }

    bool seek(std::size_t offset) noexcept {
        if (offset > data_.size()) return false;
        pos_ = offset;
        return true;
    }

    [[nodiscard]] std::uint8_t u8() noexcept { return read<1>(); }
    [[nodiscard]] std::uint16_t u16() noexcept { return read<2>(); }
    [[nodiscard]] std::uint32_t u32() noexcept { return read<4>(); }
    [[nodiscard]] std::uint64_t u64() noexcept { return read<8>(); }

    [[nodiscard]] std::int8_t i8() noexcept {
        return static_cast<std::int8_t>(u8());
    }
    [[nodiscard]] std::int16_t i16() noexcept {
        return static_cast<std::int16_t>(u16());
    }
    [[nodiscard]] std::int32_t i32() noexcept {
        return static_cast<std::int32_t>(u32());
    }

    // Bounds-checked raw bytes; returns an empty span at EOF.
    [[nodiscard]] std::span<const std::uint8_t> bytes(std::size_t count) noexcept {
        if (data_.size() - pos_ < count) return {};
        std::span<const std::uint8_t> out = data_.subspan(pos_, count);
        pos_ += count;
        return out;
    }

    // Fixed-length magic check without moving the read position.
    [[nodiscard]] bool has_at(std::size_t offset, std::string_view magic) const noexcept {
        if (offset + magic.size() > data_.size()) return false;
        const auto span = std::span<const char>(
            reinterpret_cast<const char*>(data_.data() + offset), magic.size());
        return std::string_view(span.data(), magic.size()) == magic;
    }
    [[nodiscard]] bool has_magic(std::string_view magic) const noexcept {
        return has_at(pos_, magic);
    }

    // ASCII string up to 'max' bytes, NUL-terminated within the range.
    // Returns nullopt-like empty view on truncation/EOF.
    [[nodiscard]] std::string_view fixed_string(std::size_t max) {
        const auto raw = bytes(max);
        if (raw.size() != max) return {};
        const std::string_view sv(
            reinterpret_cast<const char*>(raw.data()), max);
        const auto nul = sv.find('\0');
        return sv.substr(0, nul);
    }

private:
    template <std::size_t N>
    [[nodiscard]] std::uint64_t read() noexcept {
        if (data_.size() - pos_ < N) {
            pos_ = data_.size();  // clamp; parsers detect via eof()/failed()
            failed_ = true;
            return 0;
        }
        std::uint64_t v = 0;
        std::memcpy(&v, data_.data() + pos_, N);  // x86: LE by construction
        pos_ += N;
        return v;
    }

public:
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
    bool failed_ = false;
};

}  // namespace wc98
