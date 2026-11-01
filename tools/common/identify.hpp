// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Magic-number based identification of original FIFA 99 files (header-only, shared by
// wc98-inventory and wc98-extract). Names are short format ids used in reports.
#include "wc98/formats/big.hpp"
#include "wc98/formats/fcdb.hpp"
#include "wc98/formats/refpack.hpp"
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace wc98::tools {

inline std::string magicString(std::span<const std::uint8_t> d) {
    std::string s;
    bool printable = d.size() >= 4;
    for (std::size_t i = 0; i < 4 && i < d.size(); ++i) printable = printable && d[i] >= 0x20 && d[i] < 0x7F;
    static const char* hex = "0123456789abcdef";
    for (std::size_t i = 0; i < 4 && i < d.size(); ++i) {
        if (printable) s += char(d[i]);
        else { s += hex[d[i] >> 4]; s += hex[d[i] & 15]; }
    }
    return printable ? s : "0x" + s;
}

// WC98 delta (matrix section 3): WC98 archives put BIGF at offset 0 (majority) *or* behind a
// 48-byte prefix (the .PBK / .SMP / ANIM.BIG family). Returns the BIGF offset, or npos.
inline std::size_t bigBase(std::span<const std::uint8_t> d) {
    if (formats::BigArchive::isBig(d)) return 0;
    if (d.size() > 0x34 && formats::BigArchive::isBig(d.subspan(0x30))) return 0x30;
    return std::string::npos;
}

// Returns the format id of an (already RefPack-decompressed) payload, "unknown" otherwise.
inline std::string identify(std::span<const std::uint8_t> d) {
    auto is = [&](const char* m) { return d.size() >= std::strlen(m) && std::memcmp(d.data(), m, std::strlen(m)) == 0; };
    if (d.empty()) return "empty";
    if (bigBase(d) != std::string::npos) return "BIG";
    if (is("SHPI")) return "FSH";
    if (is("FNTF")) return "FFN";
    if (formats::isDbi(d)) return "DBI";
    if (is("FCLG")) return "FCLG";
    if (is("BNKl")) return "BNKl";
    if (is("SCHl")) return "SCHl";
    if (is("MADk")) return "MADk";
    if (is("MIDx")) return "MIDx";
    if (is("PCMD")) return "PCMD";
    if (is("CMAP")) return "CMAP";
    if (is("CRDl")) return "CRDl";
    if (is("RIFF")) return "RIFF";
    if (is("TREE")) return "TREE";
    if (is("\x7f" "ELF")) return "ELF";
    if (is("MZ")) return "MZ";
    if (is("?_\x03")) return "WinHelp";
    if (is("BM") && d.size() >= 6 && (d[2] | d[3] << 8 | d[4] << 16 | std::uint32_t(d[5]) << 24) == d.size()) return "BMP";
    if (d.size() >= 2 && d[0] == 0x01 && d[1] == 0x0B) return "EA-010B"; // ANIM.BIG / MOAI.VIV members
    // Text: no control bytes other than TAB/CR/LF/EOF in the first 4 KiB.
    std::size_t n = std::min<std::size_t>(d.size(), 4096);
    for (std::size_t i = 0; i < n; ++i)
        if (d[i] < 0x20 && d[i] != '\t' && d[i] != '\r' && d[i] != '\n' && d[i] != 0x1A) return "unknown";
    return "text";
}

// True when a loose CD file ends with the 8-byte "\0cRC" + u16 + u16 trailer (see docs/ASSET_INVENTORY.md).
inline bool hasCrcTrailer(std::span<const std::uint8_t> d) {
    return d.size() >= 8 && std::memcmp(d.data() + d.size() - 8, "\0cRC", 4) == 0;
}

} // namespace wc98::tools
