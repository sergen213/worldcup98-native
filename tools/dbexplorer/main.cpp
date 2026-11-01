// Derived from fifa99-native (same author), ported for World Cup 98
// wc98-dbexplorer: joins FCDB.DBI with the STR0/STR1 name tables of one language and
// prints / exports leagues, teams and players, plus a consistency validation.
// The FCDB lives in FE/ in FIFA 99 and in COMMON/ in World Cup 98.
//   wc98-dbexplorer <db-dir (WC98: COMMON)> [--lang ENG] [--leagues] [--teams] [--team <index|name>]
//                  [--json out.json] [--csv out-dir] [--validate]
#include "common/json.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/fcdb.hpp"
#include "wc98/formats/gen4tbls.hpp"
#include "wc98/formats/refpack.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <ranges>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace wc98;
using namespace wc98::formats;
using tools::jstr;

namespace {

struct Db {
    FcdbDatabase db;
    FcdbNames names;        // STR0 of the selected language
    FcdbNames playerNames;  // STR1 (only ENG and JAP exist; JAP is empty)
    std::map<std::uint16_t, std::size_t> playerIndex, teamIndex;

    std::string player(std::uint16_t id) const {
        auto it = playerIndex.find(id);
        return it != playerIndex.end() && it->second < playerNames.players.size() ? cp1252ToUtf8(playerNames.players[it->second]) : "?";
    }
    std::string team(std::uint16_t id) const {
        auto it = teamIndex.find(id);
        return it != teamIndex.end() && it->second < names.teams.size() ? cp1252ToUtf8(names.teams[it->second].fullName) : "?";
    }
};

std::vector<std::uint8_t> load(const fs::path& dir, const std::string& name) {
    auto p = findCaseInsensitive(dir, name);
    if (p.empty()) throw std::runtime_error("missing " + (dir / name).string());
    return readFile(p);
}

std::string csvField(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) o += c == '"' ? std::string("\"\"") : std::string(1, c);
    return o + "\"";
}

// WC98 delta (matrix 5 + 12.1): the 1998 schema renames the 17 skill fields and maps
// each nibble n to the game value n*4+39 (the 1998 range 39..99, step 4).
std::string_view attrName(const FcdbDatabase& db, std::size_t i) {
    return db.schema == FcdbSchema::WorldCup98 ? kFcdbAttributeNames1998[i] : kFcdbAttributeNames[i];
}
int attrValue(const FcdbDatabase& db, std::uint8_t nibble) {
    return db.schema == FcdbSchema::WorldCup98 ? wc98SkillValue(nibble) : nibble;
}

std::string kitJson(const FcdbKit1998& k) {
    return std::format(R"({{"shirtStyle":{},"shirtColors":[{},{},{}],"shortsStyle":{},"shortsColors":[{},{}],"socksStyle":{},"socksColors":[{},{}]}})",
                       k.shirtStyle, k.shirtColor1, k.shirtColor2, k.shirtColor3, k.shortsStyle, k.shortsColor1, k.shortsColor2, k.socksStyle,
                       k.socksColor1, k.socksColor2);
}
std::string kitText(const FcdbKit1998& k) {
    return std::format("shirt {} [{} {} {}] shorts {} [{} {}] socks {} [{} {}]", k.shirtStyle, k.shirtColor1, k.shirtColor2, k.shirtColor3,
                       k.shortsStyle, k.shortsColor1, k.shortsColor2, k.socksStyle, k.socksColor1, k.socksColor2);
}

int validate(const Db& d) {
    int problems = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        problems += !ok;
    };
    const auto& db = d.db;
    const bool wc98 = db.schema == FcdbSchema::WorldCup98; // WC98 delta: 68 B team record (matrix 5)
    check(d.names.leagues.size() == db.leagues.size(), std::format("league names {} == league records {}", d.names.leagues.size(), db.leagues.size()));
    check(d.names.teams.size() == db.teams.size(), std::format("team names {} == team records {}", d.names.teams.size(), db.teams.size()));
    check(d.playerNames.players.size() == db.players.size(), std::format("player names {} == player records {}", d.playerNames.players.size(), db.players.size()));
    check(d.teamIndex.size() == db.teams.size(), "team ids unique");
    bool asc = std::ranges::adjacent_find(db.players, [](auto& a, auto& b) { return a.id >= b.id; }) == db.players.end();
    check(asc, "player ids strictly ascending (binary-searched by the game)");
    int badRoster = 0, rosterRefs = 0, clubOut = 0, natMismatch = 0, rosterSizeMismatch = 0, oversized = 0;
    for (const auto& t : db.teams) {
        for (auto id : t.roster) { ++rosterRefs; badRoster += !d.playerIndex.contains(id); }
        if (wc98) {
            // WC98: the record's own roster-size field (byte 22 nibble * 2) must match the
            // 20 big-endian u16 slots, and 1998 rosters cap at 20 (fifa98edit, MIT).
            rosterSizeMismatch += t.rosterSize != t.roster.size();
            oversized += t.roster.size() > 20;
        } else {
            if (t.isClub && (t.roster.size() < 16 || t.roster.size() > 25)) ++clubOut;
            if (!t.isClub)
                for (auto id : t.roster)
                    if (auto it = d.playerIndex.find(id); it != d.playerIndex.end() && db.players[it->second].nationality != t.nationality) ++natMismatch;
        }
    }
    check(badRoster == 0, std::format("{} roster references resolve to players ({} unresolved)", rosterRefs, badRoster));
    if (wc98) {
        check(rosterSizeMismatch == 0, std::format("team roster-size field matches the roster list ({} mismatches)", rosterSizeMismatch));
        check(oversized == 0, std::format("WC98 rosters fit the 20 u16 slots ({} outside)", oversized));
    } else {
        check(clubOut == 0, std::format("club rosters within the manual's 16..25 players ({} outside)", clubOut));
        check(natMismatch == 0, std::format("national-team players carry the team's nationality code ({} mismatches)", natMismatch));
    }
    int badLeague = 0;
    for (const auto& l : db.leagues)
        for (auto id : l.teamIds) badLeague += !d.teamIndex.contains(id);
    check(badLeague == 0, std::format("league team references resolve ({} unresolved)", badLeague));
    int gk = 0, skillOut = 0, badShirt = 0;
    for (const auto& p : db.players) {
        gk += p.position == 0;
        for (auto a : p.attributes) skillOut += a > 15; // WC98 nibbles decode as n*4+39 (39..99)
        if (wc98) badShirt += p.shirtNumber > 99;
    }
    check(skillOut == 0, std::format("all 17 skill fields are 4-bit nibbles ({} out of range)", skillOut));
    if (wc98) check(badShirt == 0, std::format("jersey numbers within 0..99 ({} outside)", badShirt));
    std::printf("info %s schema: %zu leagues, %zu teams, %zu players (%d goalkeepers)\n", wc98 ? "World Cup 98" : "FIFA 99",
                db.leagues.size(), db.teams.size(), db.players.size(), gk);
    return problems;
}

std::string playerJson(const Db& d, const FcdbPlayer& p, std::size_t index) {
    const bool wc98 = d.db.schema == FcdbSchema::WorldCup98;
    std::string j = std::format("{{\"id\":{},\"name\":{},\"nationality\":{},\"overall\":{},\"value\":{},\"position\":{},\"position2\":{},\"shirt\":{},\"attributes\":{{",
                                p.id, jstr(index < d.playerNames.players.size() ? cp1252ToUtf8(d.playerNames.players[index]) : ""), p.nationality,
                                p.overall, p.value, p.position, p.position2, p.shirtNumber);
    for (std::size_t k = 0; k < p.attributes.size(); ++k) j += std::format("{}{}:{}", k ? "," : "", jstr(attrName(d.db, k)), attrValue(d.db, p.attributes[k]));
    j += "}}";
    if (wc98) // 1998-only fields (fifa98edit, MIT): look + role + transfer price
        j += std::format(",\"role\":{},\"startingSlot\":{},\"price\":{},\"hair\":[{},{}],\"skinTone\":{},\"face\":[{},{}]", p.role, p.startingSlot,
                         p.price, p.hairStyle, p.hairColor, p.skinTone, p.faceStyle, p.beardStyle);
    return j + "}";
}


// WC98 (matrix section 12.1): GEN4TBLS lives in COMMON/GEN4TBLS.VIV as a plain BIGF archive
// whose members are the G98*.FMT column-oriented delta tables (17 team attributes). FIFA 99
// shipped the same module as row-oriented 67-byte format records; the G98HD member is a
// small text header.
fs::path findGen4(const fs::path& dir) {
    for (const auto& cand : {dir / "GEN4TBLS.VIV", dir.parent_path() / "GEN4TBLS.VIV"})
        if (fs::exists(cand)) return cand;
    for (const auto& e : fs::recursive_directory_iterator(dir.parent_path()))
        if (e.is_regular_file() && e.path().filename() == "GEN4TBLS.VIV") return e.path();
    return {};
}

void dumpGen4(const fs::path& dir, const std::string& sel) {
    const fs::path p = sel.empty() ? findGen4(dir) : fs::path(sel);
    if (p.empty() || !fs::exists(p)) {
        std::fprintf(stderr, "no GEN4TBLS archive found near %s\n", dir.string().c_str());
        return;
    }
    auto arc = refpack::decompressIfNeeded(readFile(p));
    const std::span<const std::uint8_t> sp{arc};
    const auto big = BigArchive::parse(sp);
    const auto names = Gen4FmtTable::attributeNames();
    std::printf("%s: %zu members\n", p.filename().c_str(), big.entries.size());
    for (const auto& e : big.entries) {
        const auto mem = refpack::decompressIfNeeded(BigArchive::payload(sp, e));
        if (!Gen4FmtTable::isFmtMember(e.name)) {
            std::string head;
            for (auto b : mem | std::views::take(12)) head += char(std::isprint(b) ? b : '.');
            std::printf("  %-12s %7u B  header text: %s\n", e.name.c_str(), e.size, head.c_str());
            continue;
        }
        const auto t = Gen4FmtTable::parse(mem, e.name);
        std::printf("  %-12s %7u B  %zu attribute rows x %zu %s deltas\n", e.name.c_str(), e.size, t.attributes, t.columns,
                    t.sixteenBit ? "i16be" : "i8");
        for (std::size_t a = 0; a < t.attributes; ++a) {
            const auto r = t.row(a);
            const auto [mn, mx] = std::ranges::minmax(r);
            std::printf("    %2zu %-10s n=%-5zu first=[", a, a < names.size() ? names[a] : "?", r.size());
            for (std::size_t c = 0; c < std::min<std::size_t>(6, r.size()); ++c) std::printf("%s%d", c ? "," : "", r[c]);
            std::printf("] min=%d max=%d\n", mn, mx);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: wc98-dbexplorer <FE-dir> [--lang ENG] [--leagues] [--teams] [--team <index|name>] [--json f] [--csv dir] [--validate] [--gen4 [VIV]]\n");
        return 2;
    }
    fs::path dir = argv[1];
    std::string lang = "ENG", jsonOut, csvOut, teamSel, gen4Sel;
    bool leagues = false, teams = false, doValidate = false, doGen4 = false;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&] { if (i + 1 >= argc) throw std::runtime_error(a + " needs a value"); return std::string(argv[++i]); };
        if (a == "--lang") lang = next();
        else if (a == "--leagues") leagues = true;
        else if (a == "--teams") teams = true;
        else if (a == "--team") teamSel = next();
        else if (a == "--json") jsonOut = next();
        else if (a == "--csv") csvOut = next();
        else if (a == "--validate") doValidate = true;
        else if (a == "--gen4") { doGen4 = true; if (i + 1 < argc && argv[i + 1][0] != '-') gen4Sel = next(); }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    Db d;
    d.db = FcdbDatabase::parse(load(dir, "FCDB.DBI"));
    d.names = FcdbNames::parse(load(dir, "FCDB_" + lang + ".DBI"));
    d.playerNames = FcdbNames::parse(load(dir, "FCDBP" + std::string(lang == "JAP" ? "JAP" : "ENG") + ".DBI"));
    for (std::size_t i = 0; i < d.db.players.size(); ++i) d.playerIndex[d.db.players[i].id] = i;
    for (std::size_t i = 0; i < d.db.teams.size(); ++i) d.teamIndex[d.db.teams[i].id] = i;

    if (leagues)
        for (std::size_t i = 0; i < d.db.leagues.size(); ++i)
            std::printf("%3zu id=%-4u %-28s %zu teams\n", i, d.db.leagues[i].id,
                        i < d.names.leagues.size() ? cp1252ToUtf8(d.names.leagues[i]).c_str() : "?", d.db.leagues[i].teamIds.size());
    if (teams)
        for (std::size_t i = 0; i < d.db.teams.size(); ++i) {
            const auto& t = d.db.teams[i];
            if (d.db.schema == FcdbSchema::WorldCup98)
                std::printf("%3zu id=%-4u nat=%-3u %-24s %2zu players bank=%u stadium=%u tactics=%u strategy=%u\n", i, t.id, t.nationality,
                            i < d.names.teams.size() ? cp1252ToUtf8(d.names.teams[i].fullName).c_str() : "?", t.roster.size(), t.bank, t.stadium,
                            t.tactics, t.strategy);
            else
                std::printf("%3zu id=%-4u %-5s %-24s %2zu players budget=%u\n", i, t.id, t.isClub ? "club" : "nat",
                            i < d.names.teams.size() ? cp1252ToUtf8(d.names.teams[i].fullName).c_str() : "?", t.roster.size(), t.budget);
        }
    if (!teamSel.empty()) {
        std::size_t ti = d.db.teams.size();
        for (std::size_t i = 0; i < d.names.teams.size(); ++i)
            if (cp1252ToUtf8(d.names.teams[i].fullName) == teamSel || std::to_string(i) == teamSel) ti = i;
        if (ti >= d.db.teams.size()) { std::fprintf(stderr, "no team %s\n", teamSel.c_str()); return 1; }
        const auto& team = d.db.teams[ti];
        std::printf("%s\n", cp1252ToUtf8(d.names.teams[ti].fullName).c_str());
        if (d.db.schema == FcdbSchema::WorldCup98)
            std::printf("  home %s\n  away %s\n", kitText(team.home).c_str(), kitText(team.away).c_str());
        for (auto id : team.roster) {
            const auto& p = d.db.players[d.playerIndex.at(id)];
            std::printf("  #%-2u %-20s pos=%-2u ovr=%-2u nat=%-3u", p.shirtNumber, d.player(id).c_str(), p.position, p.overall, p.nationality);
            for (std::size_t k = 0; k < p.attributes.size(); ++k)
                std::printf(" %.5s=%d", attrName(d.db, k).data(), attrValue(d.db, p.attributes[k]));
            std::printf("\n");
        }
    }
    if (!jsonOut.empty()) {
        std::string j = "{\"language\":" + jstr(lang) + ",\"leagues\":[";
        for (std::size_t i = 0; i < d.db.leagues.size(); ++i) {
            const auto& l = d.db.leagues[i];
            j += std::format("{}\n{{\"index\":{},\"id\":{},\"name\":{},\"teams\":[", i ? "," : "", i, l.id,
                             jstr(i < d.names.leagues.size() ? cp1252ToUtf8(d.names.leagues[i]) : ""));
            for (std::size_t k = 0; k < l.teamIds.size(); ++k) j += std::format("{}{}", k ? "," : "", l.teamIds[k]);
            j += "]}";
        }
        j += "],\"teams\":[";
        const bool wc98 = d.db.schema == FcdbSchema::WorldCup98;
        for (std::size_t i = 0; i < d.db.teams.size(); ++i) {
            const auto& t = d.db.teams[i];
            const FcdbTeamName none;
            const auto& n = i < d.names.teams.size() ? d.names.teams[i] : none;
            j += std::format("{}\n{{\"index\":{},\"id\":{},\"name\":{},\"shortName\":{},\"abbreviation\":{},\"nationality\":{}", i ? "," : "", i, t.id,
                             jstr(cp1252ToUtf8(n.fullName)), jstr(cp1252ToUtf8(n.shortName)), jstr(cp1252ToUtf8(n.abbreviation)), t.nationality);
            if (wc98) // 1998 team fields (fifa98edit, MIT)
                j += std::format(",\"bank\":{},\"stadium\":{},\"tactics\":{},\"strategy\":{},\"rosterSize\":{},\"kits\":{{\"home\":{},\"away\":{}}}", t.bank,
                                 t.stadium, t.tactics, t.strategy, t.rosterSize, kitJson(t.home), kitJson(t.away));
            else
                j += std::format(",\"isClub\":{},\"budget\":{},\"rivalTeamId\":{}", t.isClub, t.budget, t.rivalTeamId);
            j += ",\"players\":[";
            for (std::size_t k = 0; k < t.roster.size(); ++k) j += std::format("{}{}", k ? "," : "", t.roster[k]);
            j += "]}";
        }
        j += "],\"players\":[";
        for (std::size_t i = 0; i < d.db.players.size(); ++i) j += (i ? ",\n" : "\n") + playerJson(d, d.db.players[i], i);
        j += "]}\n";
        writeFile(jsonOut, std::span(reinterpret_cast<const std::uint8_t*>(j.data()), j.size()));
    }
    if (!csvOut.empty()) {
        const bool wc98 = d.db.schema == FcdbSchema::WorldCup98;
        std::string p = wc98 ? "id,name,nationality,overall,price,position,position2,shirt,role,startingSlot,hairStyle,hairColor,skinTone,faceStyle,beardStyle,teams"
                             : "id,name,nationality,overall,value,position,position2,shirt,teams";
        for (std::size_t k = 0; k < 17; ++k) p += "," + std::string(attrName(d.db, k));
        p += "\n";
        std::map<std::uint16_t, std::string> memberOf;
        for (std::size_t i = 0; i < d.db.teams.size(); ++i)
            for (auto id : d.db.teams[i].roster) {
                auto& s = memberOf[id];
                s += (s.empty() ? "" : ";") + (i < d.names.teams.size() ? cp1252ToUtf8(d.names.teams[i].fullName) : std::to_string(i));
            }
        for (std::size_t i = 0; i < d.db.players.size(); ++i) {
            const auto& pl = d.db.players[i];
            if (wc98)
                p += std::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}", pl.id, csvField(d.player(pl.id)), pl.nationality, pl.overall,
                                 pl.price, pl.position, pl.position2, pl.shirtNumber, pl.role, pl.startingSlot, pl.hairStyle, pl.hairColor,
                                 pl.skinTone, pl.faceStyle, pl.beardStyle, csvField(memberOf[pl.id]));
            else
                p += std::format("{},{},{},{},{},{},{},{},{}", pl.id, csvField(d.player(pl.id)), pl.nationality, pl.overall, pl.value, pl.position,
                                 pl.position2, pl.shirtNumber, csvField(memberOf[pl.id]));
            for (auto a : pl.attributes) p += std::format(",{}", attrValue(d.db, a));
            p += "\n";
        }
        std::string t = wc98 ? "index,id,name,short,abbreviation,nationality,bank,stadium,tactics,strategy,rosterSize,players,homeKit,awayKit\n"
                             : "index,id,name,short,abbreviation,club,budget,nationality,rival,players\n";
        for (std::size_t i = 0; i < d.db.teams.size(); ++i) {
            const auto& tm = d.db.teams[i];
            const FcdbTeamName none;
            const auto& n = i < d.names.teams.size() ? d.names.teams[i] : none;
            if (wc98)
                t += std::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n", i, tm.id, csvField(cp1252ToUtf8(n.fullName)),
                                 csvField(cp1252ToUtf8(n.shortName)), csvField(cp1252ToUtf8(n.abbreviation)), tm.nationality, tm.bank, tm.stadium,
                                 tm.tactics, tm.strategy, tm.rosterSize, tm.roster.size(), csvField(kitText(tm.home)), csvField(kitText(tm.away)));
            else
                t += std::format("{},{},{},{},{},{},{},{},{},{}\n", i, tm.id, csvField(cp1252ToUtf8(n.fullName)), csvField(cp1252ToUtf8(n.shortName)),
                                 csvField(cp1252ToUtf8(n.abbreviation)), tm.isClub ? 1 : 0, tm.budget, tm.nationality, csvField(d.team(tm.rivalTeamId)), tm.roster.size());
        }
        std::string l = "index,id,name,teams\n";
        for (std::size_t i = 0; i < d.db.leagues.size(); ++i) {
            std::string ts;
            for (auto id : d.db.leagues[i].teamIds) ts += (ts.empty() ? "" : ";") + d.team(id);
            l += std::format("{},{},{},{}\n", i, d.db.leagues[i].id, csvField(i < d.names.leagues.size() ? cp1252ToUtf8(d.names.leagues[i]) : ""), csvField(ts));
        }
        auto w = [&](const char* f, const std::string& s) { writeFile(fs::path(csvOut) / f, std::span(reinterpret_cast<const std::uint8_t*>(s.data()), s.size())); };
        w("players.csv", p);
        w("teams.csv", t);
        w("leagues.csv", l);
    }
    if (doGen4) dumpGen4(dir, gen4Sel);
    if (doValidate) return validate(d) ? 1 : 0;
    return 0;
}
