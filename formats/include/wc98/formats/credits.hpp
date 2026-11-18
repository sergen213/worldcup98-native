#pragma once
// CRED.BIN credits-roll cell stream (FE/CRED.BIN family: USCRED/UKCRED/FR.., per-language
// variants). Grammar [C] analysis/pass34fe94, measured on all six shipped variants:
//   header {u32 version = 3, u32 count = 1, u32 pool = 0x129f}, then cells until EOF:
//     kind 1 {u32 kind = 1, u32 fcWidgetId} -> caption row (widget id via FC remap table)
//     kind 2 {u32 kind = 2, u32 len, bytes} -> literal name row; len includes the NUL,
//                                            no padding (cells pack byte-exact)
//     kind 3 {u32 kind = 3}                 -> blank row (18-px grid spacer)
// Row grid: 18 px stride; scene draw slot FUN_0046986c adds +2 px per tick into
// _DAT_0057d270 and scrolls one 18-px row at the 0x12 wrap; scene tick slot FUN_00469904
// arms the end-of-roll blank beat DAT_0057d280 = 0x1b ticks; ticks pace every 4 frames [H].
// Details in analysis/pass34fe94/NOTES.md.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wc98::formats {

struct CredRow {
    int kind = 3;         // 1 caption id, 2 literal name, 3 blank
    std::uint32_t id = 0; // kind 1: FC-bank index (FeText::stringAt)
    std::string text;     // kind 2: literal author/staff name
};

// Decodes the cell stream; stops at the first unknown kind or truncated payload.
// Returns empty for a short buffer or a version != 3 header.
inline std::vector<CredRow> parseCreditsBlob(std::string_view blob) {
    std::vector<CredRow> rows;
    if (blob.size() < 12) return rows;
    const auto u32 = [&](std::size_t p) {
        const auto b = reinterpret_cast<const std::uint8_t*>(blob.data());
        return std::uint32_t(b[p]) | (std::uint32_t(b[p + 1]) << 8) | (std::uint32_t(b[p + 2]) << 16) |
               (std::uint32_t(b[p + 3]) << 24);
    };
    if (u32(0) != 3) return rows; // observed version = 3
    std::size_t p = 12;            // {version, count, pool} header
    while (p + 4 <= blob.size()) {
        const auto kind = u32(p);
        p += 4;
        if (kind == 1) {
            if (p + 4 > blob.size()) break;
            rows.push_back({1, u32(p), {}});
            p += 4;
        } else if (kind == 2) {
            if (p + 4 > blob.size()) break;
            const auto len = u32(p);
            p += 4;
            if (len > blob.size() - p) break; // truncated payload
            std::string s(blob.substr(p, len));
            if (const auto nul = s.find('\0'); nul != std::string::npos) s.resize(nul);
            rows.push_back({2, 0, std::move(s)});
            p += len; // byte-exact cell: len includes the trailing NUL, no padding
        } else if (kind == 3) {
            rows.push_back({3, 0, {}});
        } else {
            break; // unknown kind: stop (stream is exhausted in practice at EOF)
        }
    }
    return rows;
}

} // namespace wc98::formats
