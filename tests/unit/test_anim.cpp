// Derived from fifa99-native (same author), ported for World Cup 98
// Player animation clips: synthetic bitstream decode + every WC98 clip (COMMON/ANIM.MO 413,
// INGAME/MISC/ANIM.BIG 5) with quaternion sanity; the 1998 key-block variant is a known gap.
#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/anim.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/refpack.hpp"

#include <cmath>
#include <map>
#include <vector>

using namespace wc98::formats;

namespace {

struct Out {
    std::vector<std::uint8_t> b;
    void u16(unsigned v) { b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8)); }
    void pad4(std::size_t start) { while ((b.size() - start) % 4) b.push_back(0xFE); }
};

// 9 scalar + 1 triplet + 1 quaternion = 16 channels.
std::vector<std::uint8_t> syntheticClip(bool compressed) {
    const std::uint8_t widths[16] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 3, 0, 5, 2, 4, 1, 6};
    const int base[16] = {6, 0, -13, 0, 1151, 0, -28, -981, 590, 9, 1151, -21, 10, -20, 30, 250};
    const int delta[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, -4, 0, 15, 1, -8, -1, 31};
    Out o;
    o.u16(0x0B01); o.u16(compressed ? 52 : 36); o.u16(8); o.u16(2);
    o.u16(0xFFFF); o.u16(0xFFFF); o.u16(0xFFFF); o.u16(compressed ? 3 : 0x8003);
    o.u16(16); o.u16(5); o.u16(1); o.u16(1);
    while (o.b.size() < 36) o.b.push_back(0);
    if (compressed) o.b.insert(o.b.end(), widths, widths + 16);
    for (int key = 0; key < 2; ++key) {
        std::size_t start = o.b.size();
        o.u16(0); // size, patched below
        if (key == 0 || !compressed) {
            for (int c = 0; c < 16; ++c) o.u16(unsigned(base[c] + (key ? delta[c] : 0)) & 0xFFFF);
        } else {
            o.u16(0x8000 | base[0]);
            for (int c = 1; c < 9; ++c) o.u16(unsigned(base[c]) & 0xFFFF);
            std::vector<int> bits; // MSB-first per value
            for (int c = 9; c < 16; ++c)
                for (int i = widths[c] - 1; i >= 0; --i) bits.push_back((delta[c] >> i) & 1);
            while (bits.size() % 16) bits.push_back(0);
            for (std::size_t w = 0; w < bits.size(); w += 16) {
                unsigned word = 0;
                for (int i = 0; i < 16; ++i) word = (word << 1) | unsigned(bits[w + i]);
                o.u16(word); // little-endian word, MSB first inside it
            }
        }
        o.pad4(start);
        o.b[start] = std::uint8_t(o.b.size() - start);
    }
    return o.b;
}

void syntheticTests() {
    for (bool comp : {true, false}) {
        auto c = AnimClip::parse(syntheticClip(comp));
        CHECK(c.keyCount == 2 && c.keys.size() == 2 && c.compressed() == comp && c.channelCount == 16);
        CHECK(c.tripletChannel(0) == 9 && c.quatChannel(0) == 12);
        CHECK(c.keys[0][0] == 6 && c.keys[1][0] == 6); // delta marker bit stripped
        CHECK(c.keys[0][2] == -13 && c.keys[1][7] == -981);
        CHECK(c.keys[1][9] == 5 && c.keys[1][10] == 1151 && c.keys[1][11] == -6 && c.keys[1][12] == 11);
        CHECK(c.keys[1][13] == -28 && c.keys[1][14] == 29 && c.keys[1][15] == 281);
    }
    auto bad = syntheticClip(true);
    bad[6] = 3; // header claims 3 keys
    CHECK_THROWS(AnimClip::parse(bad));
    bad = syntheticClip(true);
    bad[0] = 0;
    CHECK_THROWS(AnimClip::parse(bad));
}

} // namespace

int main() {
    syntheticTests();
    auto root = treeDirOrSkip();
    auto load = [&](const char* rel) { return refpack::decompressIfNeeded(wc98::readFile(wc98::findCaseInsensitive(root, rel))); };

    // WC98 gate (matrix section 6): the whole match animation set is one BIGF archive,
    // COMMON/ANIM.MO (413 six-char clips). 404 of 413 parse; the 9 failures share the
    // 1998 key-block variant (hdrSize 2817 -> "ANIM: bad key block size 164"), a known gap.
    auto mo = load("COMMON/ANIM.MO");
    const std::span<const std::uint8_t> ms{mo};
    std::size_t base = BigArchive::isBig(ms) ? 0 : 0x30; // WC98 48-byte prefix variant (matrix 3)
    auto big = BigArchive::parse(ms.subspan(base));
    std::size_t clips = 0, keys = 0, failed = 0;
    std::map<std::string, AnimClip> parsed;
    for (const auto& e : big.entries) {
        auto d = refpack::decompressIfNeeded(BigArchive::payload(ms.subspan(base), e));
        if (!AnimClip::isAnim(d)) continue;
        try {
            auto c = AnimClip::parse(d);
            ++clips;
            keys += c.keys.size();
            // Every decoded quaternion stays unit length (x256); wrong bit order/sign breaks this at once.
            for (const auto& k : c.keys)
                for (std::size_t q = 0; q < c.quatCount; ++q) {
                    double n = 0;
                    for (int i = 0; i < 4; ++i) n += double(k[c.quatChannel(q) + i]) * k[c.quatChannel(q) + i];
                    CHECK(std::abs(std::sqrt(n) - 256) < 4);
                }
            parsed.emplace(e.name, std::move(c));
        } catch (const std::exception&) { ++failed; }
    }
    std::printf("ANIM.MO: %zu members, %zu clips parsed, %zu keys decoded, %zu failed (1998 key-block variant)\n", big.entries.size(), clips,
                keys, failed);
    CHECK(big.entries.size() == 413 && clips == 404 && failed == 9);

    // ANIM.BIG (INGAME/MISC): a prefixed WC98 archive whose members are anim clips.
    auto animBig = load("INGAME/MISC/ANIM.BIG");
    const std::span<const std::uint8_t> abBytes{animBig};
    std::size_t base2 = BigArchive::isBig(abBytes) ? 0 : 0x30;
    auto ab = BigArchive::parse(abBytes.subspan(base2));
    CHECK(ab.entries.size() == 3); // ANIM.BIG ships 3 clips in this extraction
    for (const auto& e : ab.entries) CHECK(!AnimClip::parse(BigArchive::payload(abBytes.subspan(base2), e)).keys.empty());

    // Cross-game rig check (matrix section 6): WC98 clips pose the FIFA 99 player rig, i.e.
    // the 15-joint bit layout is identical. Measured ground truth: sspecz6 is a player-rig
    // clip (5 triplets / 14 quats); aaaaaa is a 16/16 show clip (160-byte header).
    CHECK(parsed.at("sspecz6").tripletCount == 5 && parsed.at("sspecz6").quatCount == 14);
    CHECK(parsed.at("aaaaaa").tripletCount == 16 && parsed.at("aaaaaa").quatCount == 16);
    CHECK(parsed.at("sspecz6").keys.size() >= 1 && parsed.at("aaaaaa").keys.size() >= 1);
    return 0;
}
