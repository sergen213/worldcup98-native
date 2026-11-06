#include "saveload.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <random>
#include <span>

#include "wc98/file.hpp"
#include "wc98/formats/slot.hpp"
#include "wc98/platform/paths.hpp"
#include "wc98/reader.hpp"

namespace wc98::app {
namespace {
using namespace wc98::tournament;
constexpr std::uint32_t kScenarioSeed = 0x98D19A8u;  // fixture-order stand-in (§5.3 mt19937)
constexpr std::uint32_t kResultSeed = 0xC0FFEE98u;   // score stream, fixed seed

struct Score { int a, b; bool pkHome; };
Score play(std::mt19937& rng) {
    std::uniform_int_distribution<int> goals(0, 3);
    return {goals(rng), goals(rng), (rng() & 1u) != 0};
}

void putU32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(std::uint8_t(x));
    v.push_back(std::uint8_t(x >> 8));
    v.push_back(std::uint8_t(x >> 16));
    v.push_back(std::uint8_t(x >> 24));
}

class LeReader {
public:
    explicit LeReader(std::span<const std::uint8_t> d) : d_(d) {}
    std::uint32_t u32() {
        if (pos_ + 4 > d_.size()) throw FormatError("saveload: state image truncated");
        const std::uint32_t v = std::uint32_t(d_[pos_]) | (std::uint32_t(d_[pos_ + 1]) << 8) |
                                (std::uint32_t(d_[pos_ + 2]) << 16) | (std::uint32_t(d_[pos_ + 3]) << 24);
        pos_ += 4;
        return v;
    }
    std::uint16_t u16() { const std::uint32_t lo = u8(), hi = u8(); return std::uint16_t(lo | (hi << 8)); }
    std::uint8_t u8() {
        if (pos_ + 1 > d_.size()) throw FormatError("saveload: state image truncated");
        return d_[pos_++];
    }

private:
    std::span<const std::uint8_t> d_;
    std::size_t pos_ = 0;
};
}  // namespace

std::vector<std::uint8_t> encodeState(const TournamentState& st) {
    std::vector<std::uint8_t> v;
    for (const std::uint16_t t : st.groups) { v.push_back(std::uint8_t(t)); v.push_back(std::uint8_t(t >> 8)); }
    putU32(v, st.seed);
    putU32(v, static_cast<std::uint32_t>(st.groupResults.size()));
    for (const auto& m : st.groupResults) {
        v.push_back(std::uint8_t(m.teamA));
        v.push_back(std::uint8_t(m.teamA >> 8));
        v.push_back(std::uint8_t(m.teamB));
        v.push_back(std::uint8_t(m.teamB >> 8));
        v.push_back(m.goalsA);
        v.push_back(m.goalsB);
    }
    for (const std::uint32_t w : st.koWinner) putU32(v, w);
    for (const std::uint32_t l : st.koLoser) putU32(v, l);
    putU32(v, st.champion);
    return v;
}

TournamentState decodeState(std::span<const std::uint8_t> image) {
    LeReader r(image);
    TournamentState st;
    for (auto& t : st.groups) t = r.u16();
    st.seed = r.u32();
    const std::uint32_t n = r.u32();
    if (n > 64) throw FormatError("saveload: group match count out of range");
    st.groupResults.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        MatchResult m;
        m.teamA = r.u16();
        m.teamB = r.u16();
        m.goalsA = r.u8();
        m.goalsB = r.u8();
        st.groupResults.push_back(m);
    }
    for (auto& w : st.koWinner) w = r.u32();
    for (auto& l : st.koLoser) l = r.u32();
    st.champion = r.u32();
    return st;
}

std::array<std::uint32_t, 6> summaryFields(const TournamentState& st) {
    std::array<std::uint32_t, 6> s{};
    s[0] = 1;  // mode: tournament
    s[1] = static_cast<std::uint32_t>(st.groupResults.size());
    s[2] = 16u - static_cast<std::uint32_t>(std::ranges::count(st.koWinner, kNoTeam));
    s[3] = st.champion;
    s[4] = st.seed;
    s[5] = 0;
    return s;
}

std::filesystem::path defaultSlotDir() {
    return platform::userDirs("wc98-native").data / "user";
}

void saveTournament(const std::filesystem::path& path, const TournamentState& st) {
    formats::SlotSave s{summaryFields(st), encodeState(st)};
    wc98::writeFile(path, s.serialize());
}

TournamentState loadTournament(const std::filesystem::path& path) {
    const auto bytes = wc98::readFile(path);
    return decodeState(formats::SlotSave::parse(bytes).image);
}

int runTournamentSaveLoad(int slot, const std::filesystem::path& dir) {
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("saveload: FAIL %s\n", what);
            ++fails;
        }
    };

    TournamentState st;
    for (std::size_t i = 0; i < 32; ++i) st.groups[i] = static_cast<std::uint16_t>(i);
    st.seed = kScenarioSeed;
    st.koWinner.fill(kNoTeam);
    st.koLoser.fill(kNoTeam);

    const auto sched = generateSchedule(st.groups, st.seed);
    check(sched.size() == 64, "schedule size");

    std::mt19937 rng(kResultSeed);
    std::vector<MatchResult> groupMatches[8];  // schedule order per group (standings input)
    std::array<Fixture, 16> koFixtures{};       // index = bracketNode
    for (const auto& f : sched)
        if (f.group < 0 && f.bracketNode >= 0 && f.bracketNode < 16)
            koFixtures[std::size_t(f.bracketNode)] = f;
    const auto slotPath = dir / formats::slotFileName(slot);

    // Group stage: one save+load round trip per completed matchday.
    for (int round = 1; round <= 3; ++round) {
        for (const auto& f : sched)
            if (f.group >= 0 && f.matchday == round) {
                const Score s = play(rng);
                MatchResult m{st.groups[std::size_t(f.home.a)], st.groups[std::size_t(f.away.a)],
                              std::uint8_t(s.a), std::uint8_t(s.b)};
                groupMatches[f.group].push_back(m);
                st.groupResults.push_back(m);
            }
        saveTournament(slotPath, st);
        const auto back = loadTournament(slotPath);
        check(back == st, "group stage round state identical");
        std::printf("saveload: slot %02d round %d saved+loaded, matches=%zu identical=%d\n", slot,
                    round, st.groupResults.size(), int(back == st));
    }

    // Knockout: standings -> qualifiers -> bracket; persist per completed round.
    std::array<StandingsRow, 16> place{};
    for (int g = 0; g < 8; ++g) {
        std::vector<StandingsRow> seeds;
        for (int pos = 0; pos < 4; ++pos) {
            StandingsRow row;
            row.teamId = st.groups[std::size_t(g * 4 + pos)];
            row.seedRank = pos;
            seeds.push_back(row);
        }
        const auto table = computeStandings(seeds, groupMatches[g]);
        place[std::size_t(g * 2)] = table[0];
        place[std::size_t(g * 2 + 1)] = table[1];
    }
    auto placeTeam = [&](const SlotRef& r) {
        return place[std::size_t(r.a * 2 + r.b - 1)].teamId;
    };
    std::array<std::uint32_t, 16> seeds{};
    for (int n = 0; n < 8; ++n) {
        const Fixture& f = koFixtures[std::size_t(n)];
        seeds[std::size_t(2 * n)] = placeTeam(f.home);
        seeds[std::size_t(2 * n + 1)] = placeTeam(f.away);
    }
    KnockoutBracket bracket =
        KnockoutBracket::makeTree(std::vector<std::uint32_t>(seeds.begin(), seeds.end()), true);

    for (int n = 0; n < 16; ++n) {  // topological: R16, QF, SF, third-place, final
        const BracketNode& node = bracket.node(std::size_t(n));
        auto resolve = [&](const SlotRef& r) {
            return n == bracket.thirdPlaceNode() ? st.koLoser[std::size_t(r.a)]
                                                 : st.koWinner[std::size_t(r.a)];
        };
        const Fixture& kf = koFixtures[std::size_t(n)];
        const std::uint32_t h = kf.home.kind == SlotRef::kGroupPlace
                                    ? placeTeam(kf.home)
                                    : resolve(kf.home);
        const std::uint32_t a = kf.away.kind == SlotRef::kGroupPlace
                                    ? placeTeam(kf.away)
                                    : resolve(kf.away);
        check(h != kNoTeam && h == node.teamA, "bracket fixture home");
        check(a != kNoTeam && a == node.teamB, "bracket fixture away");
        const Score s = play(rng);
        const bool homeWin = s.a > s.b || (s.a == s.b && s.pkHome);
        st.koWinner[std::size_t(n)] = homeWin ? h : a;
        st.koLoser[std::size_t(n)] = homeWin ? a : h;
        check(bracket.setWinner(std::size_t(n), st.koWinner[std::size_t(n)]), "setWinner");
        if (node.round == KnockoutRound::kSemiFinal)
            check(bracket.setLoser(std::size_t(n), st.koLoser[std::size_t(n)]), "setLoser");
        if (n == 7 || n == 11 || n == 13 || n == 15) {  // R16 / QF / SF / third-place done
            saveTournament(slotPath, st);
            check(loadTournament(slotPath) == st, "knockout round state identical");
        }
    }
    st.champion = bracket.champion();
    check(st.champion != kNoTeam, "champion recorded");
    saveTournament(slotPath, st);
    const auto finalBack = loadTournament(slotPath);
    std::printf("saveload: slot %02d '%s' final saved+loaded champion=%u identical=%d\n", slot,
                slotPath.filename().string().c_str(), st.champion, int(finalBack == st));
    check(finalBack == st, "final state identical");
    return fails ? 1 : 0;
}

int runTournamentLoadVerify(int slot) {
    const auto dir = defaultSlotDir();
    const auto path = dir / formats::slotFileName(slot);
    if (!std::filesystem::exists(path)) {
        std::printf("fe-load: no save in %s, running new-game scenario first\n",
                    dir.string().c_str());
        if (runTournamentSaveLoad(slot, dir) != 0) return 1;
    }
    const auto bytes = wc98::readFile(path);
    const formats::SlotSave s = formats::SlotSave::parse(bytes);
    const TournamentState st = decodeState(s.image);
    const bool identical = decodeState(encodeState(st)) == st && (st.champion != kNoTeam);
    std::printf("fe-load: slot %02d summary=[%u,%u,%u,%u,%u,%u] matches=%zu champion=%u identical=%d\n",
                slot, s.summary[0], s.summary[1], s.summary[2], s.summary[3], s.summary[4],
                s.summary[5], st.groupResults.size(), st.champion, int(identical));
    return identical ? 0 : 1;
}

}  // namespace wc98::app
