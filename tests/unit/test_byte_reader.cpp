// Unit tests for wc98::ByteReader. Plain C++ asserts, no test framework.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "wc98/byte_reader.hpp"

using namespace wc98;

int main() {
    const std::vector<std::uint8_t> data{
        'B', 'I', 'G', 'F',                          // 0..3  magic
        0x78, 0x56, 0x34, 0x12,                      // 4..7  u32 LE 0x12345678
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 8..15 u64
        0x00,                                        // 16    i8
        0x22, 0x33,                                  // 17..18 i16 LE 0x3322
        0x44, 0x00, 0x7A, 0x00, 0x00,                // 19..23 "D"+NULs
    };
    ByteReader r(data);

    assert(r.has_magic("BIGF"));
    assert(r.skip(4));  // past the magic
    assert(r.u32() == 0x12345678u);
    assert(r.u64() == 0xFFFFFFFFFFFFFFFFull);
    assert(r.i8() == 0);
    assert(r.i16() == 0x3322);
    assert(r.pos() == 19);
    assert(!r.eof());

    assert(r.skip(2));
    assert(r.pos() == 21);
    assert(!r.seek(100));      // out of range: rejected, position unchanged
    assert(r.pos() == 21);

    assert(r.skip(3));         // pos -> 24
    assert(r.eof());
    assert(!r.skip(1));
    assert(r.u32() == 0);      // EOF read: 0 + failure latch
    assert(r.failed());

    // fixed_string stops at NUL inside the fixed field.
    ByteReader s(std::span<const std::uint8_t>(data.data() + 19, 5));
    assert(s.fixed_string(5) == std::string_view("D"));

    // has_at peeks without moving.
    assert(r.has_at(0, "BIGF"));
    assert(r.pos() == 24);

    // Empty input.
    ByteReader t(std::span<const std::uint8_t>{});
    assert(t.eof());
    assert(!t.skip(1));
    assert(t.bytes(4).empty());

    std::puts("test_byte_reader: all assertions passed");
    return 0;
}
