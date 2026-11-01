// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/formats/gen4tbls.hpp"
#include <format>
#include <string_view>
#include "wc98/reader.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/refpack.hpp"
#include <cstring>
#include <format>

namespace wc98::formats {

Gen4Tables Gen4Tables::parse(std::span<const std::uint8_t> raw) {
    // WC98 (matrix section 3): archives are plain BIGF or RefPack-wrapped.
    const auto data = refpack::decompressIfNeeded(raw);
    const auto big = BigArchive::parse(data);
    // WC98 delta (matrix section 11): the 1998 AISCRIPT table set is named G98*, FIFA 99
    // G99* — COMMON/GEN4TBLS.VIV carries G98HD, G98KO.FMT, G98PL.FMT, G98PK.FMT,
    // G98FK.FMT. The KO/PL/PK/FK byte layouts are identical in both games.
    const BigEntry* hd = big.find("G98HD");
    Gen4Tables t;
    t.prefix = hd ? "G98" : "G99";
    if (!hd) hd = big.find("G99HD");
    if (!hd) throw FormatError("GEN4TBLS: member G98HD / G99HD missing");
    const auto h = BigArchive::payload(data, *hd);
    t.header.assign(h.begin(), h.end());

    // Formation_loadTables (FIFA 99 exe 0x4CE374) builds "G99%s.FMT" names with a ".FMM"
    // fallback; the 1998 exe uses the same scheme with the G98 prefix.
    const auto member = [&](std::string_view name, std::size_t size) {
        const BigEntry* e = big.find(std::format("{}{}.FMT", t.prefix, name));
        if (!e) e = big.find(std::format("{}{}.FMM", t.prefix, name));
        if (!e) throw FormatError(std::format("GEN4TBLS: member {}{}.FMT missing", t.prefix, name));
        if (e->size != size) throw FormatError(std::format("GEN4TBLS: {} is {} bytes, expected {}", e->name, e->size, size));
        return BigArchive::payload(data, *e);
    };
    // The exe uses the members in place; tables are little-endian like the host.
    const auto copy = [&](auto& dst, std::string_view name) {
        std::memcpy(dst.data(), member(name, sizeof dst).data(), sizeof dst);
    };
    copy(t.ko, "KO");
    copy(t.pl, "PL");
    copy(t.pk, "PK");
    copy(t.fk, "FK");
    // WC98 has no AT/DF members (matrix section 11: "AT/DF absent"); they stay zeroed.
    t.haveAtDf = big.find(std::format("{}AT.FMT", t.prefix)) != nullptr || big.find(std::format("{}DF.FMT", t.prefix)) != nullptr;
    if (t.haveAtDf) {
        copy(t.at, "AT");
        copy(t.df, "DF");
    }
    return t;
}


bool Gen4FmtTable::isFmtMember(std::string_view n) {
    return n.size() >= 9 && n.rfind("G98", 0) == 0 && n.ends_with(".FMT");
}

Gen4FmtTable Gen4FmtTable::parse(std::span<const std::uint8_t> d, std::string_view memberName) {
    Gen4FmtTable t;
    t.name = std::string(memberName);
    if (d.empty() || d.size() % kAttributes != 0)
        throw FormatError(std::format("G98 fmt table '{}' size {} is not a multiple of {}", t.name, d.size(), kAttributes));
    const std::size_t rowBytes = d.size() / kAttributes;
    t.sixteenBit = rowBytes % 2 == 0 && rowBytes >= 256; // documented heuristic (see header)
    t.columns = t.sixteenBit ? rowBytes / 2 : rowBytes;
    t.attributes = kAttributes;
    t.cells.reserve(t.attributes * t.columns);
    for (std::size_t r = 0; r < kAttributes; ++r) {
        const std::size_t base = r * rowBytes;
        for (std::size_t c = 0; c < t.columns; ++c) {
            if (t.sixteenBit) {
                const std::uint16_t v = std::uint16_t(std::uint16_t(d[base + c * 2]) << 8 | d[base + c * 2 + 1]);
                t.cells.push_back(std::int16_t(v > 0x7fffu ? std::int32_t(v) - 0x10000 : std::int32_t(v)));
            } else {
                t.cells.push_back(std::int8_t(d[base + c]));
            }
        }
    }
    return t;
}

std::array<const char*, Gen4FmtTable::kAttributes> Gen4FmtTable::attributeNames() {
    // Family attribute columns (fifa98edit, MIT: https://github.com/ma-akad/fifa98edit).
    return {"Aggression", "Acceleration", "Attack Bias", "Agility", "Ball Control",
            "Awareness", "Fitness", "Creativity", "Passing", "Heading", "Reaction",
            "Pass Bias", "Shot Power", "Shot Bias", "Speed", "Shot Accuracy", "Tackle"};
}

} // namespace wc98::formats
