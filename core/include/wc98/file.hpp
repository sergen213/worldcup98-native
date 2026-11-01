// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// File helpers (header-only in wc98 so core stays an INTERFACE target).
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace wc98 {

// Reads a whole file; throws std::runtime_error with the path on failure.
inline std::vector<std::uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    f.seekg(0, std::ios::end);
    std::vector<std::uint8_t> v(static_cast<std::size_t>(f.tellg()));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(v.size()));
    if (!f) throw std::runtime_error("read failed " + p.string());
    return v;
}

// Writes atomically (temp file + rename), creating parent directories.
inline void writeFile(const std::filesystem::path& p, std::span<const std::uint8_t> data) {
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    std::filesystem::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("cannot write " + tmp.string());
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!f) throw std::runtime_error("write failed " + tmp.string());
    }
    std::filesystem::rename(tmp, p);
}

// Case-insensitive lookup of a relative path ("ingame/stadium/trophy.big")
// under root. The WC98 CD/extracted copies mix upper and lower case names.
inline std::filesystem::path findCaseInsensitive(const std::filesystem::path& root,
                                                 const std::filesystem::path& rel) {
    auto lower = [](std::string s) {
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    };
    std::filesystem::path cur = root;
    for (const auto& part : rel) {
        std::filesystem::path exact = cur / part;
        if (std::filesystem::exists(exact)) {
            cur = exact;
            continue;
        }
        const std::string want = lower(part.string());
        bool found = false;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(cur, ec)) {
            if (lower(e.path().filename().string()) == want) {
                cur = e.path();
                found = true;
                break;
            }
        }
        if (!found) return {};
    }
    return cur;
}

}  // namespace wc98
