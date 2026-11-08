// Derived from fifa99-native (same author), ported for World Cup 98
#include <wc98/formats/res.hpp>

namespace wc98::formats {

std::size_t ResFile::pairIndex(std::size_t n, std::size_t a, std::size_t b) {
    // FUN_0048cc58 formula (a<b): idx = ((2n - a - 3)*a)/2 + b - 1 over C(n,2).
    return ((2 * n - a - 3) * a) / 2 + b - 1;
}

ResTriple ResFile::headToHead(std::size_t slotA, std::size_t slotB) const {
    if (slotA == slotB || slotA >= n || slotB >= n)
        throw FormatError("RES: pair slot out of range");
    if (slotA < slotB) return pairs[pairIndex(n, slotA, slotB)];
    const ResTriple t = pairs[pairIndex(n, slotB, slotA)]; // directional swap byte0<->byte2
    return {t.goalsB, t.venueFlag, t.goalsA};
}

std::size_t ResFile::slotOf(std::uint16_t id) const {
    for (std::size_t i = 0; i < ids.size(); ++i)
        if (ids[i] == id) return i;
    throw FormatError(std::format("RES: team id {} not in id section", id));
}

ResFile ResFile::parse(std::span<const std::uint8_t> data) {
    if (data.size() < 2) throw FormatError("RES: file shorter than header");
    const auto* b = data.data();
    const std::size_t n = static_cast<std::size_t>(b[0]) | (static_cast<std::size_t>(b[1]) << 8);
    if (n < 2) throw FormatError(std::format("RES: count {} < 2", n));

    const std::size_t pairsCount = (n - 1) * n / 2;
    const std::size_t pairBytes = 3 * pairsCount;
    // Loader size predicate (FUN_0048cae8): filesize >= 2 + 2n + C(n,2)*3.
    if (data.size() < 2 + 2 * n + pairBytes)
        throw FormatError(std::format("RES: size {} < header+matrix for n={}", data.size(), n));
    ResFile f;
    f.n = static_cast<std::uint32_t>(n);
    f.ids.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        f.ids.push_back(static_cast<std::uint16_t>(b[2 + i * 2] | (b[3 + i * 2] << 8)));
    f.pairs.reserve(pairsCount);
    const std::size_t base = 2 + 2 * n;
    for (std::size_t p = 0; p < pairsCount; ++p) {
        ResTriple t{};
        t.goalsA = b[base + p * 3];
        t.venueFlag = b[base + p * 3 + 1];
        t.goalsB = b[base + p * 3 + 2];
        f.pairs.push_back(t);
    }
    return f;
}

} // namespace wc98::formats
