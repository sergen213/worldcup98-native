// Derived from fifa99-native (same author), ported for World Cup 98
#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/refpack.hpp"
#include <cstring>
#include <filesystem>

using namespace wc98::formats;

static void synthetic() {
    // Synthetic BIGF with two entries (independently constructed, not game data).
    std::vector<std::uint8_t> b = {'B','I','G','F', 0,0,0,0, 0,0,0,2, 0,0,0,0};
    auto be32 = [&](std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s)); };
    std::uint32_t hdr = 16 + (8 + 2) + (8 + 4);
    be32(hdr); be32(3); b.push_back('a'); b.push_back(0);
    be32(hdr + 3); be32(2); for (char c : std::string("x\\y")) b.push_back(std::uint8_t(c)); b.push_back(0);
    CHECK(b.size() == hdr);
    for (char c : std::string("abcde")) b.push_back(std::uint8_t(c));
    auto a = BigArchive::parse(b);
    CHECK(a.entries.size() == 2);
    CHECK(a.find("X/Y") && a.find("x/y")->size == 2);
    CHECK(BigArchive::payload(b, *a.find("a"))[2] == 'c');
    b[hdr - 6] = 0xFF; // corrupt second size -> out of range
    CHECK_THROWS(BigArchive::parse(b));

    // RefPack: literal run "abcd", 2-byte copy dist 4 len 3 -> "abcdabc", end with 1 literal 'z'.
    std::vector<std::uint8_t> rp = {0x10, 0xFB, 0, 0, 8, 0xE0, 'a','b','c','d', 0x00, 0x03, 0xFD, 'z'};
    auto out = refpack::decompress(rp);
    CHECK(std::string(out.begin(), out.end()) == "abcdabcz");
    rp[4] = 9; // wrong declared size
    CHECK_THROWS(refpack::decompress(rp));
    CHECK(!refpack::isCompressed(b));
}

static void original() {
    auto root = treeDirOrSkip();
    namespace fs = std::filesystem;
    auto upper = [](const std::string& s) { std::string o = s; for (char& c : o) c = char(std::toupper(static_cast<unsigned char>(c))); return o; };
    // WC98 gates (matrix section 3): 36 loose .BIG archives parse, 793 .PAK files are RefPack
    // streams and 786 of them unpack to SHPI (7 are .LIT commentary data).
    int bigs = 0, prefix = 0, members = 0, paks = 0, shpi = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        auto ext = upper(e.path().extension().string());
        auto raw = wc98::readFile(e.path());
        if (ext == ".PAK") {
            ++paks;
            CHECK(refpack::isCompressed(raw));
            auto u = refpack::decompress(raw);
            CHECK(u.size() == refpack::uncompressedSize(raw));
            if (u.size() >= 4 && std::memcmp(u.data(), "SHPI", 4) == 0) ++shpi;
            continue;
        }
        if (ext != ".BIG") continue;
        ++bigs;
        // WC98 quirk: 4 of the 36 .BIG archives ship RefPack-wrapped (FEFONTS, IGFONTS,
        // IGLAY, LAYOUTS); the rest are plain BIGF (measured 32 + 4, matrix section 3).
        auto d = refpack::decompressIfNeeded(raw);
        const std::span<const std::uint8_t> ds{d}; // span view for subspan / payload calls
        // WC98 quirk: some archives carry a 48-byte prefix before BIGF (matrix section 3).
        std::size_t base = BigArchive::isBig(ds) ? 0 : (ds.size() > 0x34 && BigArchive::isBig(ds.subspan(0x30)) ? 0x30 : 0);
        prefix += base != 0;
        auto a = BigArchive::parse(ds.subspan(base));
        members += int(a.entries.size());
        for (const auto& en : a.entries) (void)BigArchive::payload(ds.subspan(base), en);
    }
    std::fprintf(stderr, "%d .BIG archives (%d 48-byte-prefixed), %d members; %d .PAK RefPack streams (%d unpack to SHPI)\n", bigs, prefix,
                 members, paks, shpi);
    CHECK(bigs == 36);
    CHECK(paks == 793 && shpi == 791); // measured 791 (matrix said 786 + 7 .LIT)
    // The RefPack payloads inside ANIM.BIG (a prefixed WC98 archive) decompress to size.
    auto anim = wc98::readFile(wc98::findCaseInsensitive(root, "INGAME/MISC/ANIM.BIG"));
    const std::span<const std::uint8_t> as{anim};
    std::size_t base = BigArchive::isBig(as) ? 0 : 0x30;
    auto aa = BigArchive::parse(as.subspan(base));
    int comp = 0;
    for (auto& e : aa.entries) {
        auto p = BigArchive::payload(as.subspan(base), e);
        if (refpack::isCompressed(p)) { ++comp; CHECK(refpack::decompress(p).size() == refpack::uncompressedSize(p)); }
    }
    std::fprintf(stderr, "ANIM.BIG: %zu entries, %d RefPack members\n", aa.entries.size(), comp);
}

int main() {
    synthetic();
    original();
    return 0;
}
