// Derived from fifa99-native (same author), ported for World Cup 98
// wc98-inventory: recursive scan of a World Cup 98 directory tree -> JSON report
// (path, size, sha256, magic, identified format, RefPack flag, BIG member tree, unknowns).
#include "common/identify.hpp"
#include "common/json.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/refpack.hpp"
#include <openssl/evp.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace wc98;
using tools::jstr;

namespace {

std::string sha256(std::span<const std::uint8_t> d) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_Digest(d.data(), d.size(), md, &len, EVP_sha256(), nullptr);
    std::string s;
    char b[3];
    for (unsigned i = 0; i < len; ++i) { std::snprintf(b, sizeof b, "%02x", md[i]); s += b; }
    return s;
}

struct Stats {
    std::map<std::string, int> files, members; // format -> count
    std::map<std::string, int> magicAt0;       // WC98 inventory check: raw 4-byte magic at offset 0 -> count
    std::vector<std::string> unknown;          // "path (magic)"
    int refpack = 0, crcTrailer = 0, errors = 0;
};

// Appends the JSON description of one payload (file or member) without the enclosing braces.
std::string describe(std::span<const std::uint8_t> raw, const std::string& path, int depth, Stats& st, bool member) {
    std::string j;
    bool rp = formats::refpack::isCompressed(raw);
    std::vector<std::uint8_t> unpacked;
    std::span<const std::uint8_t> d = raw;
    std::string err;
    if (rp) {
        ++st.refpack;
        try { unpacked = formats::refpack::decompress(raw); d = unpacked; }
        catch (const std::exception& e) { err = e.what(); }
    }
    std::string fmt = err.empty() ? tools::identify(d) : "refpack-error";
    (member ? st.members : st.files)[fmt]++;
    if (fmt == "unknown") st.unknown.push_back(path + " (" + tools::magicString(d) + ")");
    j += ",\"magic\":" + jstr(tools::magicString(raw)) + ",\"format\":" + jstr(fmt) + ",\"refpack\":" + (rp ? "true" : "false");
    if (rp && err.empty()) j += ",\"unpackedSize\":" + std::to_string(d.size()) + ",\"unpackedMagic\":" + jstr(tools::magicString(d));
    if (fmt == "BIG" && depth < 8) {
        try {
            auto big = formats::BigArchive::parse(d);
            j += ",\"bigDeclaredSize\":" + std::to_string(big.declaredSize) + ",\"members\":[";
            for (std::size_t i = 0; i < big.entries.size(); ++i) {
                const auto& e = big.entries[i];
                std::string mp = path + "::" + e.name;
                j += std::string(i ? "," : "") + "{\"name\":" + jstr(e.name) + ",\"offset\":" + std::to_string(e.offset) +
                     ",\"size\":" + std::to_string(e.size) +
                     describe(formats::BigArchive::payload(d, e), mp, depth + 1, st, true) + "}";
            }
            j += "]";
        } catch (const std::exception& e) {
            err = e.what();
        }
    }
    if (!err.empty()) { ++st.errors; j += ",\"error\":" + jstr(err); }
    return j;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: wc98-inventory <game-dir> [report.json]\n");
        return 2;
    }
    fs::path root = argv[1];
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) files.push_back(e.path());
    std::ranges::sort(files);

    Stats st;
    std::string j = "{\"root\":" + jstr(root.string()) + ",\"files\":[";
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        auto d = readFile(files[i]);
        total += d.size();
        ++st.magicAt0[tools::magicString(d)]; // raw magic histogram (WC98_ASSET_INVENTORY.md cross-check)
        std::string rel = fs::relative(files[i], root).generic_string();
        bool crc = tools::hasCrcTrailer(d);
        st.crcTrailer += crc;
        j += std::string(i ? ",\n" : "\n") + "{\"path\":" + jstr(rel) + ",\"size\":" + std::to_string(d.size()) +
             ",\"sha256\":" + jstr(sha256(d)) + ",\"crcTrailer\":" + (crc ? "true" : "false") + describe(d, rel, 0, st, false) + "}";
    }
    auto counts = [](const std::map<std::string, int>& m) {
        std::string s = "{";
        for (const auto& [k, v] : m) s += (s.size() > 1 ? "," : "") + jstr(k) + ":" + std::to_string(v);
        return s + "}";
    };
    j += "\n],\"summary\":{\"fileCount\":" + std::to_string(files.size()) + ",\"totalBytes\":" + std::to_string(total) +
         ",\"filesByFormat\":" + counts(st.files) + ",\"magicAt0\":" + counts(st.magicAt0) +
         ",\"bigMembersByFormat\":" + counts(st.members) +
         ",\"refpackPayloads\":" + std::to_string(st.refpack) + ",\"crcTrailerFiles\":" + std::to_string(st.crcTrailer) +
         ",\"errors\":" + std::to_string(st.errors) + ",\"unknown\":[";
    for (std::size_t i = 0; i < st.unknown.size(); ++i) j += (i ? "," : "") + jstr(st.unknown[i]);
    j += "]}}\n";

    if (argc > 2) writeFile(argv[2], std::span(reinterpret_cast<const std::uint8_t*>(j.data()), j.size()));
    else std::fwrite(j.data(), 1, j.size(), stdout);
    std::fprintf(stderr, "%zu files, %llu bytes, %d refpack payloads, %zu unknown, %d errors\n", files.size(),
                 static_cast<unsigned long long>(total), st.refpack, st.unknown.size(), st.errors);
    std::fprintf(stderr, "-- raw magic at offset 0 --\n");
    for (const auto& [k, v] : st.magicAt0) std::fprintf(stderr, "  %-14s %5d files\n", k.c_str(), v);
    std::fprintf(stderr, "-- identified format (after RefPack unpack) --\n");
    for (const auto& [k, v] : st.files) std::fprintf(stderr, "  %-14s %5d files\n", k.c_str(), v);
    return 0;
}
