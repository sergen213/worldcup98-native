#pragma once
// The match random number generator of FIFA99.EXE v1.1 (Rng_next 0x4BB000, Rng_seed 0x4BAFD8).
// A lagged add-with-carry generator over six 16-bit words kept in dwords at 0x589038..0x58904C,
// with a 96-bit counter folded in. Ported instruction by instruction (the seed may leave high
// bits in the words; the first sums use full 32-bit adds exactly like the original).
#include <array>
#include <cstdint>

namespace wc98::sim {

struct Rng {
    std::array<std::uint32_t, 6> s{}; // s[0] = 0x589038 ... s[5] = 0x58904C
    std::uint32_t last = 0;           // DAT_0065f2e0, last result

    // Rng_seed 0x4BAFD8 (called from Match_init 0x4546F8 with EAX = seed): word i = (signed char)
    // "ArCaDe"[i] (string 0x5748D0, "ArCaDe_CoInOp") + seed << 25. The match seed comes from the
    // input module's generator 0x466FD0 and replays re-seed with seed + 12 (0x40A46E).
    void seed(std::uint32_t seedValue) {
        static constexpr char kKey[6] = {'A', 'r', 'C', 'a', 'D', 'e'};
        for (int i = 0; i < 6; ++i) s[i] = static_cast<std::uint32_t>(static_cast<int>(kKey[i])) + (seedValue << 25);
    }

    // Rng_next 0x4BB000: returns 0..0xFFFF (EAX & 0x7FFFFFFF of a 16-bit value).
    std::uint32_t next() {
        std::uint32_t sum = s[5] + s[4];
        std::uint32_t r = sum & 0xffff;
        std::uint32_t carry = sum >> 16;
        s[4] = r;
        for (int k = 3; k >= 0; --k) {
            sum = r + carry + s[k];
            r = sum & 0xffff;
            carry = sum >> 16;
            s[k] = r;
        }
        s[5] = (s[5] + 1) & 0xffff;
        // 0x4BB041..: counter ripple s5 -> s4 -> s3 -> s2 -> s1; when s1 wraps, s0 is bumped and
        // the result is incremented (0x4BB0AD), whether or not s0 itself wraps.
        bool wrapped = s[5] == 0;
        for (int k = 4; k >= 1 && wrapped; --k) {
            s[k] = (s[k] + 1) & 0xffff;
            wrapped = s[k] == 0;
        }
        if (wrapped) {
            s[0] = (s[0] + 1) & 0xffff;
            r = (r + 1) & 0xffff;
        }
        last = r & 0x7fffffff;
        return last;
    }
};

} // namespace wc98::sim
