// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Minimal JSON string escaping shared by the wc98 tools (header-only, no dependency).
#include <cstdio>
#include <string>
#include <string_view>

namespace wc98::tools {
// Quotes and escapes `s` (expected UTF-8) as a JSON string literal.
inline std::string jstr(std::string_view s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
            else if (c >= 0x80) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } // EA name bytes are Latin-1
            else o += char(c);
        }
    }
    return o + "\"";
}
} // namespace wc98::tools
