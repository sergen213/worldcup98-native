// Derived from fifa99-native (same author), ported for World Cup 98
// SQF container gate: synthetic header checks + census over the real FE/*.SQF tree.
#include "wc98/formats/sqf.hpp"
#include <check.hpp>
#include <wc98/file.hpp>

#include <algorithm>
#include <cstdint>



#include <cstdio>
#include <filesystem>
#include <span>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;

Bytes makeFile(std::size_t payload) {
    Bytes b(0x44 + payload, 0xA5);
    const char* head = "EULZ*RTFD\0"; // magic + kind signature through byte 9 (NUL)
    for (std::size_t i = 0; i < 10; ++i) b[i] = static_cast<std::uint8_t>(head[i]);
    auto put = [&](std::size_t off, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) b[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    put(0x08, 68);
    put(0x0C, 40);
    return b;
}

std::uint64_t fnv1a(std::span<const std::uint8_t> d) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const auto b : d) h = (h ^ b) * 0x100000001b3ULL;
    return h;
}

void syntheticChecks() {
    const auto good = makeFile(7);
    CHECK(wc98::formats::sqf::isSqf(good));
    const auto a = wc98::formats::sqf::parseSqf(good);
    CHECK(a.archiveCount == 68);
    CHECK(a.typeCount == 40);
    CHECK(a.payload.size() == 7);
    CHECK(a.ids.size() == 0x34);

    auto badKind = good; // '*RTFD\0' kind byte[7] must stay 'F' (loader check)
    badKind[7] = 'X';
    CHECK(!wc98::formats::sqf::isSqf(badKind));
    CHECK_THROWS(wc98::formats::sqf::parseSqf(badKind));

    auto noTypes = good; // type count zero -> loader-invalid
    noTypes[0x0C] = 0;
    CHECK_THROWS(wc98::formats::sqf::parseSqf(noTypes));

    const Bytes headerOnly = makeFile(0); // filesize - 0x44 == 0: no payload
    CHECK_THROWS(wc98::formats::sqf::parseSqf(headerOnly));

    Bytes junk(0x50, 0);
    CHECK(!wc98::formats::sqf::isSqf(junk));
    CHECK_THROWS(wc98::formats::sqf::parseSqf(junk));
}

void treeChecks() {
    const auto dir = treeDirOrSkip() / "FE";
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == ".SQF") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    CHECK(files.size() == 49);

    unsigned totalTypes = 0;
    for (const auto& f : files) {
        const auto data = wc98::readFile(f);
        CHECK(wc98::formats::sqf::isSqf(data));
        const auto a = wc98::formats::sqf::parseSqf(data);
        CHECK(a.archiveCount == 68); // constant on all 49 files
        CHECK(a.typeCount >= 16 && a.typeCount <= 280);
        CHECK(a.payload.size() == data.size() - 0x44);
        totalTypes += a.typeCount;
    }
    CHECK(totalTypes == 5493);

    // FNV-1a/64 of the raw payload stream. The stream is not decoded: the EULZ expander is
    // unavailable offline (hook slot NULL, see docs/asset-formats/SQF_FOOTIX.md) so the gate
    // pins container structure + stable stream bytes instead of image output.
    struct { const char* name; std::uint64_t hash; } expect[] = {
        {"FTX10_1.SQF", 0x5c3363fc09ad0cf2ULL},
        {"FTX7_2.SQF",  0xbca2732859187e04ULL},
        {"FTX13_2.SQF", 0x6af7f39411287da7ULL},
    };
    for (const auto& e : expect) {
        const auto data = wc98::readFile(dir / e.name);
        CHECK(fnv1a(wc98::formats::sqf::parseSqf(data).payload) == e.hash);
    }
}
} // namespace

int main() {
    syntheticChecks();
    treeChecks();
    std::printf("test_sqf ok\n");
    return 0;
}
