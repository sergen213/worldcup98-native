// Derived from fifa99-native (same author), ported for World Cup 98
// FCDB: synthetic byte-level round trips for both record layouts (FIFA 99 80 B team
// record and World Cup 98 68 B team record), the STR0/STR1 name tables, the FCLG string
// table, plus a WC98-tree gate (24 leagues / 56 teams / 1782 players, matrix section 5).
#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/fcdb.hpp"
#include <cstring>
#include <filesystem>
#include <map>
#include <numeric>
#include <string>
#include <vector>

using namespace wc98::formats;
using Bytes = std::vector<std::uint8_t>;

static void le32(Bytes& b, std::uint32_t v) { for (int s = 0; s < 32; s += 8) b.push_back(std::uint8_t(v >> s)); }
static void put32(Bytes& b, std::size_t at, std::uint32_t v) { std::memcpy(&b[at], &v, 4); }

static void syntheticFifa99() {
    // FCDB: 1 league (id 7, 1 team), 1 team (id 5, budget 1234, club, roster [42]), 1 player (id 42).
    Bytes b = {'F', 'C', 'D', 'B'};
    le32(b, 501); le32(b, 0); le32(b, 0); le32(b, 0);
    put32(b, 8, std::uint32_t(b.size()));
    le32(b, 1); le32(b, 0); le32(b, 12);           // league table, offset to record
    le32(b, 7u | 1u << 26); b.push_back(5); b.push_back(0);
    put32(b, 12, std::uint32_t(b.size()));
    le32(b, 1); le32(b, 80);
    std::size_t team = b.size();
    b.resize(team + 80);
    put32(b, team, 5u | 1234u << 11);
    put32(b, team + 16, 1u << 30);
    put32(b, team + 20, 1u << 15);
    b[team + 28] = 42;
    put32(b, 16, std::uint32_t(b.size()));
    le32(b, 1); le32(b, 20);
    le32(b, 42u | 13u << 14 | 96u << 25); le32(b, 777); le32(b, 0x76543210); le32(b, 0xFEDCBA98); le32(b, 0xC | 3u << 4 | 5u << 15);
    auto db = FcdbDatabase::parse(b, FcdbSchema::Fifa99);
    CHECK(db.schema == FcdbSchema::Fifa99);
    CHECK(db.leagues.size() == 1 && db.leagues[0].id == 7 && db.leagues[0].teamIds == std::vector<std::uint16_t>{5});
    CHECK(db.teams[0].id == 5 && db.teams[0].budget == 1234 && db.teams[0].isClub && db.teams[0].roster == std::vector<std::uint16_t>{42});
    const auto& p = db.players[0];
    CHECK(p.id == 42 && p.nationality == 13 && p.overall == 96 && p.value == 777 && p.shirtNumber == 3 && p.position == 5);
    CHECK(p.attributes[0] == 0 && p.attributes[1] == 2 && p.attributes[14] == 1 && p.attributes[13] == 0xC && p.attributes[12] == 0xF);
    Bytes bad = b; put32(bad, 4, 500);
    CHECK_THROWS(FcdbDatabase::parse(bad, FcdbSchema::Fifa99));
    Bytes trunc(b.begin(), b.end() - 4);
    CHECK_THROWS(FcdbDatabase::parse(trunc, FcdbSchema::Fifa99));
    // Cross-schema guard: the 80 B team record is not the WC98 layout (matrix section 5).
    CHECK_THROWS(FcdbDatabase::parse(b, FcdbSchema::WorldCup98));

    // STR0: one league name, one team entry with fixed abbreviation/short fields.
    Bytes s = {'S', 'T', 'R', '0'};
    le32(s, 501); le32(s, 20); le32(s, 0); le32(s, 0);
    le32(s, 1); le32(s, 0); le32(s, 12); for (char c : std::string("LIGA")) s.push_back(std::uint8_t(c)); s.push_back(0);
    put32(s, 12, std::uint32_t(s.size()));
    le32(s, 1); le32(s, 0); le32(s, 12);
    const char t[] = "Abc\0\0\0Abcdef\0\0\0\0\0Abcdef Town";
    s.insert(s.end(), t, t + sizeof t);
    auto n = FcdbNames::parse(s);
    CHECK(n.leagues == std::vector<std::string>{"LIGA"} && n.teams.size() == 1);
    CHECK(n.teams[0].abbreviation == "Abc" && n.teams[0].shortName == "Abcdef" && n.teams[0].fullName == "Abcdef Town");

    const char l[] = "FCLG\0V1.7\0one\0\0two\0\0\0\0";
    auto fl = FclgStrings::parse(std::span(reinterpret_cast<const std::uint8_t*>(l), sizeof l - 1));
    CHECK(fl.strings == (std::vector<std::string>{"FCLG", "V1.7", "one", "", "two"}));
    CHECK(cp1252ToUtf8("\xD6l\x80") == "\xC3\x96l\xE2\x82\xAC");
}

// WC98 (1998) layout: 68-byte team record, 17 words, roster = 20 big-endian u16 at byte 24,
// kit / bank / stadium / tactics / strategy / nation bits per fifa98edit (MIT, matrix 12.1).
static void syntheticWc98() {
    Bytes b = {'F', 'C', 'D', 'B'};
    le32(b, 501); le32(b, 0); le32(b, 0); le32(b, 0);
    put32(b, 8, std::uint32_t(b.size()));
    le32(b, 1); le32(b, 0); le32(b, 12);
    le32(b, 7u | 1u << 26); b.push_back(3); b.push_back(0);       // league 7 -> team id 3
    put32(b, 12, std::uint32_t(b.size()));
    le32(b, 1); le32(b, 68);
    std::size_t team = b.size();
    b.resize(team + 68);
    b[team + 0] = 5;                    // id 5
    b[team + 1] = 0x38;                 // bank bits: 7
    b[team + 3] = 0x20;                 // stadium = 2
    b[team + 4] = 4;                    // home shirt colour 1 = 4
    b[team + 5] = 2 << 2;               // home shirt style = 2
    b[team + 21] = 1 << 2;              // tactics = 1
    b[team + 17] = 3 << 5;              // strategy = 3, shirt low bits 0
    b[team + 22] = 0x3A;                // roster size = 10 * 2 = 20 (low nibble), nation high nibble 3
    b[team + 23] = 0;                   // nation low nibble: nationality = (b23&0xF)<<4 | (b22&0xF0)>>4 = 3
    b[team + 24] = 42;                  // roster[0] as little-endian u16 = 42 (WC98 1998 layout)
    put32(b, 16, std::uint32_t(b.size()));
    le32(b, 1); le32(b, 20);
    std::size_t pl = b.size();
    b.resize(pl + 20);
    b[pl + 0] = 42;                     // id 42
    b[pl + 2] = 3;                      // nation 12 (nibble << 2)
    b[pl + 3] = 1;                      // starting slot
    b[pl + 4] = 9; b[pl + 5] = 3;       // price 0x0309 = 777
    b[pl + 6] = 0x56;                   // hair 5 / colour 1 / skin 2
    b[pl + 7] = 0x4B;                   // face 9 / beard 3
    for (int i = 8; i < 16; ++i) b[pl + i] = 0xFF;
    b[pl + 16] = 0x0F;                  // tackle nibble 15
    b[pl + 17] = 0x31;                  // shirt 16, role 3
    auto db = FcdbDatabase::parse(b, FcdbSchema::WorldCup98);
    CHECK(db.schema == FcdbSchema::WorldCup98);
    CHECK(db.leagues.size() == 1 && db.leagues[0].id == 7 && db.leagues[0].teamIds == std::vector<std::uint16_t>{3});
    const auto& t = db.teams[0];
    CHECK(t.id == 5 && t.bank == 7 && t.stadium == 2 && t.tactics == 1 && t.strategy == 3);
    CHECK(t.rosterSize == 1 && t.nationality == 3 && t.roster == std::vector<std::uint16_t>{42}); // rosterSize = filled slots (one synthetic id)
    CHECK(t.home.shirtStyle == 2 && t.home.shirtColor1 == 4);
    CHECK(!t.isClub && t.budget == 0);  // WC98 has no club flag and no budget field
    const auto& p = db.players[0];
    CHECK(p.id == 42 && p.nationality == 12 && p.startingSlot == 1 && p.price == 777 && p.value == 777);
    CHECK(p.hairStyle == 5 && p.hairColor == 1 && p.skinTone == 2 && p.faceStyle == 9 && p.beardStyle == 3);
    CHECK(p.shirtNumber == 16 && p.role == 3);
    for (auto a : p.attributes) { CHECK(a == 15); CHECK(wc98SkillValue(a) == 99); }  // 39..99 step 4
    CHECK(p.overall == 99);
    CHECK_THROWS(FcdbDatabase::parse(b, FcdbSchema::Fifa99));
}

static void original() {
    auto root = treeDirOrSkip();
    auto fe = [&](const std::string& n) { return wc98::readFile(wc98::findCaseInsensitive(root, "COMMON/" + n)); };
    // WC98 gate: entity counts 24 leagues / 56 teams / 1782 players (matrix section 5).
    auto db = FcdbDatabase::parse(fe("FCDB.DBI"));
    CHECK(db.schema == FcdbSchema::WorldCup98);
    CHECK(db.leagues.size() == 24 && db.teams.size() == 56 && db.players.size() == 1782);
    auto names = FcdbNames::parse(fe("FCDB_ENG.DBI"));
    auto players = FcdbNames::parse(fe("FCDBPENG.DBI"));
    CHECK(names.leagues.size() == 24 && names.teams.size() == 56 && players.players.size() == 1782);
    std::map<std::uint16_t, std::size_t> idx;
    for (std::size_t i = 0; i < db.players.size(); ++i) idx[db.players[i].id] = i;
    int unresolved = 0, shirtSentinel = 0, skillOut = 0;
    for (const auto& t : db.teams) {
        CHECK(t.roster.size() <= 20);
        CHECK(t.rosterSize == t.roster.size());
        CHECK(t.home.shirtStyle <= 63 && t.away.socksStyle <= 3); // shirtStyle is a 6-bit field (measured max 32 across 56 teams)
        for (auto id : t.roster) unresolved += !idx.contains(id);
    }
    CHECK(unresolved == 0);
    for (const auto& p : db.players) {
        shirtSentinel += p.shirtNumber == 255; // "no number" sentinel (measured 720 players)
        for (auto a : p.attributes) skillOut += a > 99; // 39..99 scaled values (matrix section 5)
    }
    CHECK(shirtSentinel == 720 && skillOut == 0);
    std::fprintf(stderr, "WC98 FCDB: %zu leagues, %zu teams, %zu players, %d roster slots\n", db.leagues.size(), db.teams.size(),
                 db.players.size(), int(std::ranges::fold_left(db.teams, 0, [](int s, const FcdbTeam& t) { return s + int(t.roster.size()); })));
    // All nine UI string tables parse and agree on the string count (WC98 adds Danish).
    auto eng = FclgStrings::parse(fe("FCENG.BIN"));
    CHECK(eng.strings.size() > 1000);
    for (const char* l : {"FCGER.BIN", "FCFRE.BIN", "FCITA.BIN", "FCSPA.BIN", "FCDUT.BIN", "FCSWE.BIN", "FCDAN.BIN"})
        CHECK(FclgStrings::parse(fe(l)).strings.size() == eng.strings.size());
    // Measured: the Japanese UI table carries 1932 NUL-delimited entries (44,387 B),
    // not the 2,031 of the European tables — shift-JIS layout difference, not a parse bug.
    CHECK(FclgStrings::parse(fe("FCJAP.BIN")).strings.size() == 1932);
    // All eight team-name languages parse.
    for (const char* l : {"GER", "FRE", "ITA", "SPA", "DUT", "SWE", "DAN", "JAP"})
        CHECK(FcdbNames::parse(fe("FCDB_" + std::string(l) + ".DBI")).teams.size() == 56);
}

int main() {
    syntheticFifa99();
    syntheticWc98();
    original();
    return 0;
}
