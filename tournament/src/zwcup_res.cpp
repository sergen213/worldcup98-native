// ZWCUP.RES / ZINTER.RES parser — spec §2.5 (FUN_0048cae8 size check, FUN_0048cc58 pair index).
#include "wc98/tournament/tournament.hpp"

#include <cstddef>

namespace wc98::tournament {

namespace {
std::uint16_t leU16(const std::byte* p) {
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[0]) |
                                      (static_cast<std::uint8_t>(p[1]) << 8));
}
} // namespace

std::int64_t ZwcupRes::pairByteOffset(std::uint16_t n, std::uint16_t a, std::uint16_t b) {
    if (a >= b || b >= n) return -1;
    // Exact arithmetic of FUN_0048cc58 (§2.5, host-verified bijection).
    const std::int64_t triple = (static_cast<std::int64_t>(2 * n - a - 3) * a) / 2 + b - 1;
    return 2 + 2 * static_cast<std::int64_t>(n) + 3 * triple;
}

bool ZwcupRes::findPair(std::uint16_t idA, std::uint16_t idB, ResPairRec& out) const {
    int a = -1, b = -1;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == idA) a = static_cast<int>(i);
        if (ids[i] == idB) b = static_cast<int>(i);
    }
    if (a < 0 || b < 0 || a == b) return false;
    const bool swapped = a > b;
    const std::uint16_t lo = static_cast<std::uint16_t>(swapped ? b : a);
    const std::uint16_t hi = static_cast<std::uint16_t>(swapped ? a : b);
    const std::int64_t off = pairByteOffset(poolSize, lo, hi);
    if (off < 2 + 2 * static_cast<std::int64_t>(poolSize)) return false;
    const std::size_t triple = static_cast<std::size_t>((off - 2 - 2 * poolSize) / 3);
    out = pairs[triple];
    if (swapped) { // directional access: byte0<->byte2 when team args flip (§2.5)
        ResPairRec r = out;
        out.goalsA = r.goalsB;
        out.goalsB = r.goalsA;
    }
    return true;
}

bool parseZwcupRes(std::span<const std::byte> data, ZwcupRes& out) {
    if (data.size() < 2) return false;
    const std::uint16_t n = leU16(data.data());
    if (n < 2) return false;
    // Loader size validation (§2.5): filesize >= 2 + 2n + 3*n*(n-1)/2; exact for the shipped files.
    const std::size_t expected = 2 + 2 * static_cast<std::size_t>(n) +
                                 3 * static_cast<std::size_t>(n) * (n - 1) / 2;
    if (data.size() < expected) return false;
    out.poolSize = n;
    out.ids.clear();
    out.pairs.clear();
    out.ids.reserve(n);
    for (std::uint16_t i = 0; i < n; ++i) out.ids.push_back(leU16(data.data() + 2 + 2 * i));
    const std::size_t triples = static_cast<std::size_t>(n) * (n - 1) / 2;
    out.pairs.reserve(triples);
    for (std::size_t t = 0; t < triples; ++t) {
        const std::byte* p = data.data() + 2 + 2 * static_cast<std::size_t>(n) + 3 * t;
        out.pairs.push_back({static_cast<std::uint8_t>(p[0]),
                             static_cast<std::uint8_t>(p[1]),
                             static_cast<std::uint8_t>(p[2])});
    }
    return true;
}

} // namespace wc98::tournament
