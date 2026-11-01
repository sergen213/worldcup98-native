// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Parser-facing throwing reader.
//
// wc98's bounds-checked reader is wc98::ByteReader (wc98/byte_reader.hpp):
// non-throwing, EOF-latching. The ported FIFA-98/99 format parsers (f99's
// ByteReader) rely on out-of-range access throwing FormatError, so this
// adapter maps that contract onto ByteReader: every accessor bounds-checks
// via need(), then delegates the actual read to ByteReader. Big-endian,
// 24-bit and float accessors are built on the same little-endian primitives,
// so all parsing in the project flows through one reader.
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <stdexcept>
#include <string>

#include "wc98/byte_reader.hpp"

namespace wc98 {

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data, std::size_t pos = 0) : data_(data), base_(data) {
        seek(pos);
    }

    [[nodiscard]] std::size_t pos() const { return pos_; }
    [[nodiscard]] std::size_t size() const { return data_.size(); }
    [[nodiscard]] std::size_t remaining() const { return pos_ <= data_.size() ? data_.size() - pos_ : 0; }
    [[nodiscard]] std::span<const std::uint8_t> data() const { return data_; }

    void seek(std::size_t p) {
        if (p > data_.size())
            throw FormatError(std::format("seek to {} past end ({})", p, data_.size()));
        base_.seek(p);
        pos_ = p;
    }
    void skip(std::size_t n) { seek(pos_ + n); }

    std::uint8_t u8() {
        need(1);
        const auto v = base_.u8();
        pos_ = base_.pos();
        return v;
    }
    std::uint16_t u16le() {
        need(2);
        const auto v = base_.u16();
        pos_ = base_.pos();
        return v;
    }
    std::uint32_t u32le() {
        need(4);
        const auto v = base_.u32();
        pos_ = base_.pos();
        return v;
    }
    std::uint16_t u16be() { return std::byteswap(u16le()); }
    std::uint32_t u32be() { return std::byteswap(u32le()); }
    std::uint32_t u24le() {
        need(3);
        const auto b = base_.bytes(3);
        pos_ = base_.pos();
        return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16);
    }
    std::uint32_t u24be() {
        need(3);
        const auto b = base_.bytes(3);
        pos_ = base_.pos();
        return (std::uint32_t(b[0]) << 16) | (std::uint32_t(b[1]) << 8) | std::uint32_t(b[2]);
    }
    std::int16_t i16le() { return static_cast<std::int16_t>(u16le()); }
    std::int32_t i32le() { return static_cast<std::int32_t>(u32le()); }
    float f32le() { return std::bit_cast<float>(u32le()); }

    std::span<const std::uint8_t> bytes(std::size_t n) {
        need(n);
        auto s = base_.bytes(n);
        pos_ = base_.pos();
        return s;
    }
    std::string fixedString(std::size_t n) {
        auto s = bytes(n);
        std::size_t len = 0;
        while (len < n && s[len] != 0) ++len;
        return std::string(reinterpret_cast<const char*>(s.data()), len);
    }
    std::string cString() {
        std::string out;
        for (;;) {
            auto c = u8();
            if (!c) break;
            out.push_back(char(c));
        }
        return out;
    }

    // Bounds check mirroring f99's semantics: out-of-range throws FormatError.
    void need(std::size_t n) const {
        if (pos_ > data_.size() || n > data_.size() - pos_)
            throw FormatError(std::format("read of {} bytes at {} past end ({})", n, pos_, data_.size()));
    }

private:
    std::span<const std::uint8_t> data_;
    ByteReader base_;
    std::size_t pos_ = 0;
};

}  // namespace wc98
