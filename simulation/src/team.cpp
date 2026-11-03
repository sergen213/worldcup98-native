// Team block functions (0x451210..0x455730 team region, 0x49EBCC slot binding, 0x4A047C/0x4A0730
// tactics, 0x4DB260/0x4DB320 run targets). Ported from the FIFA99.EXE v1.1 disassembly.
#include <cstddef>
#include <cstring>

#include "wc98/sim/data.hpp"
#include "wc98/sim/world.hpp"

namespace wc98::sim {

static_assert(offsetof(TeamState, blk) == 0x0C);
static_assert(offsetof(TeamState, players) == 0x184);
static_assert(offsetof(TeamState, possWinner) == 0x1B0);
static_assert(offsetof(TeamState, f1e4) == 0x1E4);
static_assert(offsetof(TeamState, mentality) == 0x208);
static_assert(offsetof(TeamState, order) == 0x21C);
static_assert(offsetof(TeamState, kickPos) == 0x248);
static_assert(offsetof(TeamState, links) == 0x2CC);
static_assert(offsetof(TeamState, oneTouchTimer) == 0x340);
static_assert(offsetof(TeamState, playerCount) == 0x348);
static_assert(offsetof(TeamState, line358) == 0x358);
static_assert(offsetof(TeamState, f378) == 0x378);
static_assert(offsetof(TeamState, timer3c0) == 0x3C0);
static_assert(offsetof(TeamState, flags) == 0x3C4);

namespace {

// 0x587170: slot priority order for team.order (0x4A047C).
constexpr std::int8_t kSlotOrder[17] = {14, 13, 15, 12, 16, 9, 8, 10, 7, 11, 6, 2, 5, 3, 4, 1, 0}; // exe 0x587170
// 0x586E60: per-mentality support link tables (ptrs 0x58714C = {0x586E60, 0x586EA4, 0x586EA4, 0x586EE8,
// 0x586EE8}), [slot][4] preferred link slot (-1 none).
constexpr std::int8_t kLinkTab[204] = { // exe 0x586e60
    -1, 6, 2, -1, 9, 5, 3, 4, 8, 3, 7, -1, 8, 4, 2, -1, 9, 5, 3, -1, 10, 6, 4, -1,
    10, 11, 5, -1, 8, 3, 2, 4, 9, 4, 2, 3, 14, 5, 3, 4, 9, 6, 4, 5, 10, 6, 5, 4,
    -1, 8, 7, 9, -1, 9, 7, 8, -1, 10, 8, 9, -1, 11, 9, 10, -1, 11, 10, 9, -1, 6, 2, -1,
    9, 5, 3, 4, 8, 3, 7, -1, 8, 4, 2, -1, 9, 5, 3, -1, 10, 6, 4, -1, 10, 11, 5, -1,
    13, 8, 12, 3, 13, 9, 7, 3, 14, 10, 8, 4, 15, 11, 9, 5, 15, 16, 10, 5, 13, 8, 7, 9,
    14, 9, 7, 8, -1, 10, 8, 9, 14, 11, 9, 10, 15, 11, 10, 9, -1, 6, 2, -1, 9, 5, 3, 4,
    8, 3, 7, -1, 8, 4, 2, -1, 9, 5, 3, -1, 10, 6, 4, -1, 10, 11, 5, -1, 13, 8, 12, 3,
    13, 9, 7, 3, 14, 10, 8, 4, 15, 11, 9, 5, 15, 16, 10, 5, -1, 13, 7, 8, -1, 14, 12, 8,
    -1, 15, 13, 9, -1, 16, 14, 10, -1, 11, 15, 10,
};
constexpr int kLinkTabOff[5] = {0, 0x44, 0x44, 0x88, 0x88}; // 0x58714C - 0x586E60
// 0x586F2C: fallback candidates per link column (ptrs 0x587160 = +0, +0x88, +0x110, +0x198), [slot][8].
constexpr std::int8_t kLinkCand[544] = { // exe 0x586f2c
    0, 0, 0, 0, 0, 0, 0, 0, 4, 5, 3, 9, -1, -1, -1, -1, 3, 7, 8, -1, -1, -1, -1, -1,
    4, 2, 8, 9, 7, -1, -1, -1, 5, 3, 9, 10, 8, -1, -1, -1, 4, 6, 10, 9, 11, -1, -1, -1,
    5, 11, 10, -1, -1, -1, -1, -1, 8, 12, 13, -1, -1, -1, -1, -1, 9, 7, 13, 14, 12, -1, -1, -1,
    10, 8, 14, 15, 13, -1, -1, -1, 9, 11, 15, 14, 16, -1, -1, -1, 10, 16, 15, -1, -1, -1, -1, -1,
    13, 7, 8, 14, -1, -1, -1, -1, 14, 12, 9, -1, -1, -1, -1, -1, 15, 13, 10, 8, -1, -1, -1, -1,
    14, 12, 9, -1, -1, -1, -1, -1, 15, 11, 10, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0,
    4, 5, 9, 10, -1, -1, -1, -1, 3, 4, 7, 8, 9, -1, -1, -1, 4, 5, 8, 9, 10, -1, -1, -1,
    5, 6, 9, 10, 11, -1, -1, -1, 6, 10, 11, -1, -1, -1, -1, -1, 5, 11, 10, -1, -1, -1, -1, -1,
    8, 9, 12, 13, 14, 2, 3, 4, 9, 10, 13, 14, 15, 3, 4, 5, 10, 11, 14, 15, 16, 4, 5, 6,
    11, 15, 16, 5, 6, -1, -1, -1, 10, 16, 15, 6, 5, -1, -1, -1, 13, 14, 7, 8, 9, -1, -1, -1,
    14, 15, 8, 9, 10, -1, -1, -1, 15, 16, 9, 10, 11, -1, -1, -1, 16, 10, 11, -1, -1, -1, -1, -1,
    15, 9, 10, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 4, 3, 9, 8, -1, -1, -1, -1,
    3, 7, 8, -1, -1, -1, -1, -1, 2, 8, 7, -1, -1, -1, -1, -1, 3, 2, 9, 8, 7, -1, -1, -1,
    4, 3, 10, 9, 8, -1, -1, -1, 5, 4, 11, 10, 9, -1, -1, -1, 8, 12, 13, 2, 3, -1, -1, -1,
    7, 13, 12, 3, 2, -1, -1, -1, 8, 7, 14, 13, 12, 4, 3, 2, 9, 8, 15, 14, 13, 5, 4, 3,
    10, 9, 16, 15, 14, 6, 5, 4, 13, 7, 8, -1, -1, -1, -1, -1, 12, 8, 7, -1, -1, -1, -1, -1,
    13, 12, 9, 8, 7, -1, -1, -1, 14, 13, 10, 9, 8, -1, -1, -1, 15, 14, 11, 10, 9, -1, -1, -1,
    0, 0, 0, 0, 0, 0, 0, 0, 4, 5, 3, -1, -1, -1, -1, -1, 3, 4, 7, 8, -1, -1, -1, -1,
    4, 2, 8, 9, 7, -1, -1, -1, 5, 3, 9, 10, 8, -1, -1, -1, 4, 6, 10, 9, 11, -1, -1, -1,
    5, 4, 11, 10, -1, -1, -1, -1, 2, 3, 8, -1, -1, -1, -1, -1, 3, 4, 2, 9, 7, -1, -1, -1,
    4, 5, 3, 10, 8, -1, -1, -1, 5, 4, 6, 9, 11, -1, -1, -1, 6, 5, 10, -1, -1, -1, -1, -1,
    7, 8, 13, -1, -1, -1, -1, -1, 8, 9, 7, 14, 12, -1, -1, -1, 9, 10, 8, 15, 13, -1, -1, -1,
    10, 9, 11, 14, 16, -1, -1, -1, 11, 10, 15, -1, -1, -1, -1, -1,
};
// 0x58AE34 (Formation_styleForTeam 0x4CF350): 15 identity entries followed by unrelated data (the
// 0x58AE70 descriptors read as dwords); index = 5-bit DB byte, so all 32 are reachable.
constexpr std::int32_t kStyleMap[32] = { // exe 0x58ae34
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 67371265, 16777217, 33752065, 16842752, 66819,
    50397440, 516, 50528513, 16777219, 33817600, 65536, 197380, 50331904, 517, 67305473, 16777219, 17105920,
};
// 0x592C4C: FE default formation descriptors {gk, sweeper, def, mid, fwd, wideA, wideB}; entries 15
// (0x592CB5) and 16 (0x592CBC) are the fixed descriptors of records 1 and 2 (FUN_0052168C).
constexpr std::uint8_t kFeDesc[17][7] = { // exe 0x592c4c
    {1, 1, 4, 4, 1, 0, 0}, {1, 1, 4, 3, 2, 0, 0}, {1, 1, 3, 5, 1, 0, 0}, {1, 1, 3, 4, 2, 0, 0},
    {1, 1, 3, 3, 3, 0, 0}, {1, 0, 4, 4, 2, 0, 0}, {1, 0, 4, 3, 3, 0, 0}, {1, 0, 3, 5, 2, 0, 0},
    {1, 0, 3, 4, 3, 0, 0}, {1, 0, 5, 4, 1, 0, 0}, {1, 0, 5, 3, 2, 0, 0}, {1, 0, 3, 3, 4, 0, 0},
    {1, 0, 3, 2, 5, 0, 0}, {1, 0, 4, 2, 4, 0, 0}, {1, 0, 2, 1, 1, 0, 0}, {1, 0, 5, 4, 1, 0, 0},
    {1, 0, 3, 4, 3, 0, 0},
};

int opp(int t) { return t == 0 ? 1 : 0; }

// FUN_00524C8C: slot used flags of a record from its descriptor (same rule as 0x4CEE60).
void deriveUsed(std::uint8_t* rec) {
    const std::uint8_t* d = rec + 1;
    std::uint8_t* s = rec + 8;
    s[0] = d[0] > 0;
    s[6] = d[1] > 0;
    for (int i = 0; i < 3; ++i) {
        const std::uint8_t n = d[2 + i];
        std::uint8_t* l = s + (2 + 5 * i) * 6;
        l[0] = n >= 4;
        l[6] = n >= 2;
        l[12] = n & 1;
        l[18] = n >= 2;
        l[24] = n >= 4;
    }
}


} // namespace

// ---------------------------------------------------------------------------------------------
// Tactics block / formation records

int Team_dbByte6f(const World& w, int team) {
    // WC98 (SIM_PORT_NOTES.md): the 68-byte team record has no FIFA 99 word-5 formation field; the
    // parsed fifa98edit tactics index (byte 21 >> 2) is the 1998 equivalent, passed through.
    return static_cast<int>(w.data->sheets[team].team.tactics & 0x1f);
}

std::int32_t Formation_styleForTeam(int b) { return kStyleMap[b & 31]; }

// FE default path of FUN_0052168C (no saved custom tactics): team id, three formation records
// (mentality 2/4/0; descriptors 0x592C4C[style], 0x592CB5, 0x592CBC), slot used flags (0x524C8C),
// total player count. The squad-size reduction (0x524890/0x524DD4) and the squad tag assignment
// (0x5246EC/0x522EA4, FE positional matching) are not traced: tags = k for the k-th used slot.
void Team_buildTacticsBlock(World& w, int team) {
    TeamState& t = w.teams[team];
    std::memset(t.blk, 0, sizeof t.blk);
    const std::uint16_t id = w.data->sheets[team].team.id;
    std::memcpy(t.blk, &id, 2); // block +0 (team+0x0C)
    int style = Team_dbByte6f(w, team);
    if (style > 16) style = 0; // untraced: table 0x592C4C only has 17 entries
    const std::uint8_t* src[3] = {kFeDesc[style], kFeDesc[15], kFeDesc[16]};
    const std::uint8_t ment[3] = {2, 4, 0};
    for (int r = 0; r < 3; ++r) {
        std::uint8_t* rec = t.rec(r);
        rec[0] = ment[r];
        std::memcpy(rec + 1, src[r], 7);
        deriveUsed(rec);
        std::uint16_t k = 0;
        for (int s = 0; s < kSlots; ++s) {
            std::uint8_t* sr = rec + 8 + s * 6;
            if (sr[0]) {
                std::memcpy(sr + 4, &k, 2);
                ++k;
            }
        }
    }
    const std::uint8_t* d = t.rec(0) + 1;
    t.blk[3] = static_cast<std::uint8_t>(d[0] + d[1] + d[2] + d[3] + d[4]); // 0x524C70 -> team+0x0F
    // untraced_FeTakerIds_52183A: the FE copies 9 set-piece taker DB ids into block +0x154 (team+0x160)
    // from a front-end team struct (+0x30..+0x50) that is not the 80-byte FCDB record; 0x49E970
    // converts them to squad indices, unmatched ids becoming 0xFF/0xFFFF, which 0x4A0AA0 then
    // re-allocates. Without that struct every tag is left unmatched (0xFF).
    for (int k = 0; k < 9; ++k) t.blk[0x154 + k * 2] = 0xff;
}

// 0x4A047C: team.order (players by slot priority) and team.links (4 support team-mates per slot).
void Team_buildSupportLinks(World& w, int team) {
    TeamState& t = w.teams[team];
    const int n = t.playerCount;
    std::int32_t slotIdx[17];
    for (int& v : slotIdx) v = 0xff;
    for (int i = 0; i < n; ++i) slotIdx[w.players[t.players[i]].slot] = i;
    int o = 0;
    for (int k = 0; k < 17; ++k) {
        const int idx = slotIdx[kSlotOrder[k]];
        if (idx != 0xff) t.order[o++] = t.players[idx];
    }
    const std::int8_t* mt = kLinkTab + kLinkTabOff[t.mentality];
    for (int i = 0; i < n; ++i) {
        const int slot = w.players[t.players[i]].slot;
        std::int32_t used[11] = {};
        used[i] = 1;
        for (int j = 0; j < 4; ++j) {
            int c = mt[slot * 4 + j];
            std::int32_t e;
            if (c < 0) {
                e = 0xff;
            } else {
                const std::int8_t* cand = kLinkCand + j * 0x88 + c * 8;
                e = slotIdx[c];
                int k = 0;
                for (;;) {
                    if (e != 0xff && !used[e] && c != slot) break;
                    c = cand[k];
                    if (c < 0) {
                        e = 0xff;
                        break;
                    }
                    ++k;
                    e = slotIdx[c];
                    if (k >= 8) break;
                }
            }
            if (e != 0xff) used[e] = 1;
            t.links[slot * 4 + j] = static_cast<std::uint8_t>(e);
        }
    }
}

// 0x451750 (EAX = team, EDX = mentality).
void Team_setMentality(World& w, int team, int m) {
    TeamState& t = w.teams[team];
    if (m == t.mentality) return;
    if (m < 0 || m >= 5) m = 2;
    t.mentality = m;
    Team_buildSupportLinks(w, team);
}

// 0x4A0730 (EAX = team, EDX = record).
void Team_setFormation(World& w, int team, int f) {
    TeamState& t = w.teams[team];
    t.formIdx() = static_cast<std::uint8_t>(f);
    Formation_selectTeamFormation(w, team, f);
    Team_setMentality(w, team, t.rec(f)[0]);
    Formation_applyDescriptor(w, team, t.rec(f) + 1);
    Team_humanAfterSendOff(w, team, 0);
}

// 0x49EBCC. untraced_FeTagConvert_49E970: the FE tag conversion (DB ids -> squad indices, team+0x0E
// flag) is not needed: Team_buildTacticsBlock already writes squad indices.
void Team_bindFormationSlots(World& w, int team) {
    TeamState& t = w.teams[team];
    std::uint8_t* sheet = w.pl.lineup[team];
    const std::uint16_t zero = 0, left = static_cast<std::uint16_t>(w.opt.maxSubs);
    std::memcpy(sheet + 0x22, &zero, 2); // 0x66E586 pending
    std::memcpy(sheet + 0x26, &zero, 2); // 0x66E58A queued
    std::memcpy(sheet + 0x24, &left, 2); // 0x66E588 = DAT_0065EEE8
    if (w.data->sheets[team].squad.empty()) return; // 0x65F044 squad size
    const std::uint8_t* rec = t.rec(t.formIdx());
    Formation_applyDescriptor(w, team, rec + 1);
    int k = 0;
    for (int s = 0; s < kSlots; ++s) {
        const std::uint8_t* sr = rec + 8 + s * 6;
        if (!sr[0]) continue;
        const int pid = t.players[k];
        PlayerState& p = w.players[pid];
        p.slot = p.curSlot = p.slotCopy = s;
        p.baseRole = p.role = kSlotRole[s];
        if (s == 0) t.f1e4 = pid;
        std::uint16_t tag;
        std::memcpy(&tag, sr + 4, 2);
        Player_bindSquadMember(w, team, k, tag);
        ++k;
    }
    Team_buildSupportLinks(w, team);
    Team_allocateTakers_4A0AA0(w, team);
    Team_assignPositions_49EB10(w, team);
    Team_reapplyRecords_4A08FC(w, team);
}

// ---------------------------------------------------------------------------------------------
// Period / match set-up

// 0x451D94 (EDX unused by the loop).
void Team_resetPlayersPeriod(World& w, int team, int) {
    const TeamState& t = w.teams[team];
    for (int i = 0; i < t.playerCount; ++i) Player_clearArrivalCb(w, t.players[i]);
}

// 0x451D38.
void Team_setPlayersFlag80(World& w, int team, int on) {
    const TeamState& t = w.teams[team];
    for (int i = 0; i < t.playerCount; ++i) {
        PlayerState& p = w.players[t.players[i]];
        p.flags22c = (p.flags22c & ~0x80u) | (static_cast<std::uint32_t>(on & 1) << 7);
    }
}

// 0x451DDC: kick-off / restart line-up points (two lines of five and one), random depth jitter.
void Team_buildKickPositions(World& w, int team) {
    TeamState& t = w.teams[team];
    const Pitch& P = w.pitch;
    const std::int32_t k = ((P.goalAreaHalfWidth + P.penAreaHalfWidth) >> 1) / 2;
    std::int32_t z0 = P.halfLength - P.goalAreaDepth;
    std::int32_t d = (P.penAreaDepth - P.goalAreaDepth - 0x90) / 2;
    if (t.side == 1) {
        z0 = -z0;
        d = -d;
    }
    const std::uint32_t ad = static_cast<std::uint32_t>(d < 0 ? -d : d);
    const std::int32_t base0 = z0 - d - (d >> 1);
    for (int i = 0; i < 5; ++i) t.kickPos[i].z = static_cast<std::int32_t>(w.rng.next() % ad) + base0;
    t.kickPos[0].x = -2 * k;
    t.kickPos[3].x = -k;
    t.kickPos[1].x = 2 * k;
    t.kickPos[4].x = k;
    t.kickPos[2].x = 0;
    const std::int32_t base1 = t.kickPos[0].z + d - (d >> 1);
    for (int i = 0; i < 5; ++i) {
        t.kickPos[5 + i].z = static_cast<std::int32_t>(w.rng.next() % ad) + base1;
        t.kickPos[5 + i].x = t.kickPos[i].x;
    }
    t.kickPos[10].x = 0;
    t.kickPos[10].z = z0 - 2 * d;
}

// 0x4513FC (EAX = team, EDX = side).
void Team_startPeriod(World& w, int team, int side) {
    TeamState& t = w.teams[team];
    t.f1b4 = -1;
    t.f1b8 = -1;
    t.f1bc = -1;
    t.f1c0 = -1;
    t.oneTouch = -1;
    t.f1e0 = -1;
    t.f1f0 = -1;
    t.timer3c0 = 0;
    t.f200 = 0;
    t.f204 = 0;
    t.line358 = 0;
    t.line35c = 0;
    t.f350 = 0;
    t.f354 = 0;
    t.f378 = 0;
    t.side = side;
    t.possWinner = t.players[0];
    t.flags &= 0xfb81;
    t.f1c8 = t.f1cc = t.f1d0 = t.possWinner;
    t.line214 = t.line218 = w.pitch.halfLength;
    Team_resetPlayersPeriod(w, team, 0);
    Team_autoMentality(w, team);
    Team_buildKickPositions(w, team);
    if (w.opt.gameMode != 1) {
        for (int i = 0; i < 11; ++i) {
            const int pid = t.players[i];
            // ECX holds a pointer into the team block here (always non-zero): faceBall = true.
            if (w.opt.gameMode == 3) {
                Player_reset(w, pid, nullptr, 6, true);
            } else if (w.match.periodIndex == 0 ||
                       (w.match.periodIndex == 1 && (w.match.periodType == 0 || w.match.periodType == 1))) {
                const Vec3 e{w.pitch.entryPos[t.side][0], w.pitch.entryPos[t.side][1], w.pitch.entryPos[t.side][2]};
                Player_reset(w, pid, &e, 8, true);
            } else {
                Player_reset(w, pid, nullptr, 0, true);
            }
        }
    }
    w.formation.g57f9a0 = 0;
    t.g57f9a4 = 0;
    t.supportToggle = 0;
}

// 0x45159C: control assistance levels team+0x1F4/0x1F8/0x1FC from human counts and DAT_0065EEFC.
void Team_setControlLevels(World& w, int team) {
    TeamState& t = w.teams[team];
    const TeamState& o = w.teams[opp(team)];
    std::int32_t c = 1, esi = 1, eax = 1;
    std::uint32_t edx = static_cast<std::uint32_t>(w.opt.aiAssist);
    if (t.humans == 0 && o.humans == 0) edx = 1;
    if (t.humans == 0) {
        if (edx == 0) {
            eax = 0;
            esi = 0;
        } else if (edx == 2) {
            esi = 2;
            eax = 2;
        }
        const std::uint32_t gm = static_cast<std::uint32_t>(w.opt.gameMode);
        if (gm >= 0xd && gm <= 0x11) {
            --c;
            if (gm >= 0xe && gm <= 0x10) ++eax;
        }
    } else if (edx == 0) {
        --c;
        esi = 0;
        eax = o.humans == 0 ? 2 : 0;
    } else if (edx == 2) {
        esi = 0;
        ++c;
        eax = o.humans == 0 ? 0 : 2;
    }
    auto clamp2 = [](std::int32_t v) { return v < 0 ? 0 : (v > 2 ? 2 : v); };
    t.ctrl1f4 = clamp2(esi);
    t.ctrl1f8 = clamp2(c);
    t.ctrl1fc = clamp2(eax);
    for (int i = 0; i < t.playerCount; ++i) Player_setSpeedScale(w, t.players[i]);
}

// 0x4517B4 (after a goal): re-balance the opponent when only one side is human.
void Team_afterGoal(World& w, int team) {
    const TeamState& t = w.teams[team];
    const TeamState& o = w.teams[opp(team)];
    if (t.humans == 0) {
        if (o.humans == 0) return;
        if (o.ctrl1f4 != 0) return;
    } else {
        if (o.humans != 0) return;
        if (t.ctrl1f4 != 2) return;
    }
    PlayerAttr_raiseSpecial_478CBC(w, opp(team), 1);
    Team_setControlLevels(w, opp(team));
}

// 0x451B18 (Match_onEvent tail): AI formation re-derivation late in a period while not level.
void Team_lateGameFormation(World& w, int team) {
    const TeamState& t = w.teams[team];
    if (t.humans != 0 && t.setupAuto == 0) return;
    const std::int32_t diff = w.match.score[team] - w.match.score[opp(team)];
    const bool late = w.match.periodType == 1 &&
                      static_cast<std::uint32_t>(w.match.durationSec - w.match.elapsedSec) < 0x4650;
    if (diff == 0 || !late) return;
    Formation_adaptToPlayers(w, team);
    Team_humanAfterSendOff(w, team, 0);
}

// 0x4555D4: setupAuto = 1 unless a controller (flag 0x20 clear) drives a player of this team.
void Team_initMatch(World& w, int team) {
    TeamState& t = w.teams[team];
    t.setupAuto = 1;
    for (const ControllerState& c : w.controllers) {
        if (c.player == 0) continue;
        if (w.players[c.player - 1].team != team) continue;
        if (c.flags1a0 & 0x20) continue;
        t.setupAuto = 0;
    }
}

// 0x4529F8 (EAX = team, EDX = squad index).
int Team_playerBySquad(const World& w, int team, int squadIdx) {
    const TeamState& t = w.teams[team];
    for (int i = 0; i < t.playerCount; ++i) {
        const int pid = t.players[i];
        if (w.attrs[w.players[pid].attr].squadIndex == squadIdx) return pid;
    }
    return -1;
}

// 0x453628 (EAX = team, EDX = positioning callback index).
void Team_lineUp(World& w, int team, int cb) {
    const TeamState& t = w.teams[team];
    for (int i = 0; i < t.playerCount; ++i) {
        const int pid = t.players[i];
        Vec3 pos;
        w.players[pid].posCb = Formation_evalSlotCallback(w, cb, pid, pos, -1, 0x40, 1);
        Player_moveToward_4BE804(w, pid, pos);
        Player_lineUp_4BCDE0(w, pid);
    }
}

// ---------------------------------------------------------------------------------------------
// Mentality

// 0x451860 (EAX = team).
void Team_autoMentality(World& w, int team) {
    const TeamState& t = w.teams[team];
    if (t.humans != 0 && t.setupAuto == 0) return;
    const std::int32_t diff = w.match.score[team] - w.match.score[opp(team)];
    const bool p1 = w.match.periodType == 1;
    const bool late = p1 && static_cast<std::uint32_t>(w.match.durationSec - w.match.elapsedSec) < 0x4b0;
    const int side = t.side;
    const std::int32_t bz = w.ball.landing.z; // 0x669174
    const int otherSide = side == 0 ? 1 : 0;
    const bool b10 = side == (bz >= 0 ? 1 : 0);
    const bool b0c = side == (bz - w.pitch.thirdLineZ[side] >= 0 ? 1 : 0);
    const int pos = w.ball.possessor;
    int m = 2;
    if (diff < 0) {
        if (late) {
            m = 0;
        } else if (diff <= -3) {
            if (b10) m = 4;
            else if (p1) m = 1;
        } else {
            if (b10) m = 3;
            if (p1 && side != (bz - w.pitch.thirdLineZ[otherSide] >= 0 ? 1 : 0)) m = 1;
        }
    } else if (diff == 0) {
        if (pos < 0 || w.players[pos].team == team) {
            if (!b0c || (t.flags & 0x200) || t.f1b4 >= 0) m = 1;
        } else if (b0c) {
            m = 3;
        }
    } else if (diff < 2) {
        if (late) m = 4;
    } else if (late) {
        m = 3;
    } else if (!p1 && !b10) {
        m = 1;
    }
    if (pos >= 0 && w.players[pos].team != team && m <= 2 &&
        side != (bz - w.pitch.thirdLineZ[otherSide] >= 0 ? 1 : 0)) {
        m = 3;
    }
    Team_setMentality(w, team, m);
}

// FUN_00455730 (DAT_0065EF54 & 8): late in the match the leading team gets handicapped
// (PlayerAttr_applyHandicap 0x478E60 + 0x45159C, team flag 0x400); cleared when close again.
void Team_lateGameTactics(World& w) {
    if (!(w.opt.rules & 8)) return;
    if (w.opt.gameMode == 6) return;
    if (w.match.periodType != 1) return;
    if (w.match.elapsedSec & 0xf) return;
    const int esi = w.opt.handicapMode == 0 ? w.match.periodType : 0;
    const std::int32_t u = static_cast<std::int32_t>(static_cast<std::uint32_t>(w.match.durationSec * 13) >> 8);
    const std::int32_t rem = w.match.durationSec - w.match.elapsedSec;
    std::int32_t ad = w.match.score[0] - w.match.score[1];
    if (ad < 0) ad = -ad;
    bool lead = ad >= 3 || (ad >= 2 && rem > 2 * u && rem < 4 * u) || (ad >= 1 && rem > u && rem < 2 * u);
    if (!lead) {
        for (int t = 0; t < 2; ++t) {
            if (w.teams[t].flags & 0x400) {
                PlayerAttr_applyHandicap(w, t, esi, 0, w.opt.skillLevel);
                Team_setControlLevels(w, t);
                w.teams[t].flags &= ~0x400u;
            }
        }
        return;
    }
    bool zeroLeads;
    if (w.match.periodType == 3)
        zeroLeads = static_cast<std::uint32_t>(w.match.so.goals[0]) > static_cast<std::uint32_t>(w.match.so.goals[1]);
    else
        zeroLeads = static_cast<std::uint32_t>(w.match.score[0]) > static_cast<std::uint32_t>(w.match.score[1]);
    const int t = zeroLeads ? 0 : 1;
    if (w.teams[t].flags & 0x400) return;
    PlayerAttr_applyHandicap(w, t, esi, 1, w.opt.skillLevel);
    Team_setControlLevels(w, t);
    w.teams[t].flags |= 0x400;
}

// ---------------------------------------------------------------------------------------------
// Key-player pickers and tactics

namespace {
bool pickable(const PlayerState& p, int exclSlot) {
    return !(p.flags22c & 2) && p.curSlot != exclSlot && !(p.flags22c & 1);
}
} // namespace

// 0x452360 (EAX = z, EDX = team, EBX = excluded current slot).
int Team_nearestToLineZ(const World& w, std::int32_t z, int team, int exclSlot) {
    const TeamState& t = w.teams[team];
    std::uint32_t best = 0x7fff8000;
    int r = -1;
    for (int i = 0; i < t.playerCount; ++i) {
        const PlayerState& p = w.players[t.players[i]];
        if (!pickable(p, exclSlot)) continue;
        std::int32_t d = z - p.posI.z;
        if (d < 0) d = -d;
        if (static_cast<std::uint32_t>(d) < best) {
            best = static_cast<std::uint32_t>(d);
            r = t.players[i];
        }
    }
    return r;
}

// 0x452728 (EAX = pos, EDX = team, EBX = excluded slot): nearest, not the last kicker, not in a
// state of group 1 (0x80..0xFF).
int Team_nearestToPointFree(const World& w, const Vec3& pos, int team, int exclSlot) {
    const TeamState& t = w.teams[team];
    std::uint32_t best = 0x7fff8000;
    int r = -1;
    for (int i = 0; i < t.playerCount; ++i) {
        const int pid = t.players[i];
        const PlayerState& p = w.players[pid];
        if (!pickable(p, exclSlot)) continue;
        if (pid == w.ball.lastKicker) continue;
        if ((static_cast<std::uint32_t>(p.state) >> 7) == 1) continue;
        const std::uint32_t d = static_cast<std::uint32_t>(octLength(pos.x - p.posI.x, pos.z - p.posI.z));
        if (d < best) {
            best = d;
            r = pid;
        }
    }
    return r;
}

// 0x452578 (EAX = team, EDX = excluded slot): nearest to the ball among players within 0x60 of it or
// whose angle to the ball is within 0xAB of the own-goal -> ball direction.
int Team_aheadOfBallPick(const World& w, int team, int exclSlot) {
    const TeamState& t = w.teams[team];
    const std::int32_t gz = t.side == 1 ? w.pitch.halfLength : -w.pitch.halfLength;
    const Angle a = atan2i(w.ball.posI.x, w.ball.posI.z - gz);
    std::uint32_t best = 0x7fff8000;
    int r = -1;
    for (int i = 0; i < t.playerCount; ++i) {
        const int pid = t.players[i];
        const PlayerState& p = w.players[pid];
        if (!pickable(p, exclSlot)) continue;
        const std::uint32_t d = static_cast<std::uint32_t>(p.distBall);
        if (d > 0x60) {
            std::int32_t df = (p.angleBall - a) & 0x3ff;
            if (df > 0x200) df = 0x400 - df;
            if (df > 0xab) continue;
        }
        if (d < best) {
            best = d;
            r = pid;
        }
    }
    return r;
}

// 0x4536A8.
void Team_updateTactics(World& w, int team) {
    TeamState& t = w.teams[team];
    const int n = t.playerCount;
    if (n <= 0) return;
    const std::uint32_t fr = static_cast<std::uint32_t>(w.time.simFrames);
    if (t.oneTouch >= 0) {
        if (static_cast<std::uint32_t>(t.oneTouchTimer) > fr) {
            t.oneTouchTimer -= static_cast<std::int32_t>(fr);
        } else {
            t.oneTouchTimer = 0;
            t.oneTouch = -1;
        }
    }
    if (t.timer3c0 != 0) {
        if (static_cast<std::uint32_t>(t.timer3c0) > fr) t.timer3c0 -= static_cast<std::int32_t>(fr);
        else t.timer3c0 = 0;
    }
    if (team == w.time.teamOrder) {
        t.f1bc = Team_nearestToLineZ(w, w.pitch.goalLineZ[t.side], team, 0);
        t.f1c0 = Team_nearestToLineZ(w, w.pitch.goalLineZ[t.side == 0 ? 1 : 0], team, 0);
        std::int32_t h = w.ball.apexHeight;
        if (h <= w.ball.posI.y) h = w.ball.posI.y;
        const BallState& B = w.ball;
        const Vec3 ref = (h > 0x8e && B.cross142Frame > 0 && (B.cross142Frame >> 1) > B.consumed) ? B.cross142Pos
                                                                                                  : B.landing;
        t.f1c4 = Team_nearestPlayer_4523E4(w, ref, team, 0, nullptr);
        t.f1c8 = Team_nearestToPointFree(w, ref, team, 0xff);
        if (t.f1c8 < 0) t.f1c8 = Team_nearestPlayer_4523E4(w, ref, team, 0xff, nullptr);
        int c = Team_nearestToPointFree(w, ref, team, 0);
        if (c < 0) c = t.f1c8;
        if (t.f1cc < 0) t.f1cc = c;
        if (t.f1cc >= 0 && c >= 0 && c != t.f1cc &&
            static_cast<std::uint32_t>(Player_cachedDistance(w, c, t.f1cc)) > 0x80)
            t.f1cc = c;
        int a = Team_aheadOfBallPick(w, team, 0);
        if (a < 0) a = t.f1c4;
        if (t.f1d0 < 0 || (a >= 0 && a != t.f1d0)) t.f1d0 = a;
        // 0x453A34
        const int pos = w.ball.possessor;
        t.possWinner = pos >= 0 ? (w.players[pos].team == team ? pos : t.f1d0) : t.f1cc;
        const int bit = Team_decidePossession(w, team);
        if (bit != static_cast<int>((t.flags >> 1) & 1) || w.match.phase != 9) {
            Team_onPossessionChange(w, team);
            t.flags = (t.flags & ~2u) | (static_cast<std::uint32_t>(bit & 1) << 1);
        }
        if (t.flags & 2) Team_attackAi_4D2644(w, team);
        else if (w.match.phase == 9) Team_defendAi_4D39E4(w, team);
        Team_attackAi_4D2F24(w, team);
        Team_attackAi_4D35AC(w, team);
        Team_attackAi_4D2A44(w, team);
    }
    if (t.f378 != 0) Team_ai_4DA3C4(w, team);
    for (int i = 0; i < n; ++i) {
        const int pid = t.players[i];
        PlayerState& p = w.players[pid];
        p.stateTimer += static_cast<std::int32_t>(fr);
        const std::uint32_t lo = (static_cast<std::uint32_t>(p.timer200) + fr) & 0xffff;
        p.timer200 = static_cast<std::int32_t>((static_cast<std::uint32_t>(p.timer200) & 0xffff0000u) | lo);
        p.countdown -= static_cast<std::int32_t>(fr);
        if (p.countdown < 0) p.countdown = 0;
        if ((p.flags22c & 0x200) && p.countdown == 0) {
            if (p.slot != 0) Player_resetIdle_4BD308(w, pid);
            else if (w.ball.possessor == pid) w.teams[p.team].possWinner = pid;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Support runs

// 0x453AB0 (EAX = player, EDX/EBX = x range, ECX = z0, stack z1, max distance, excluded slot):
// nearest opponent strictly inside the box within maxDist.
int Team_opponentInBox(World& w, int pid, std::int32_t x0, std::int32_t x1, std::int32_t z0, std::int32_t z1,
                       std::int32_t maxDist, int exclSlot) {
    const TeamState& o = w.teams[w.teams[w.players[pid].team].opponent];
    std::uint32_t best = 0xffff;
    int r = -1;
    for (int i = 0; i < o.playerCount; ++i) {
        const int q = o.players[i];
        const PlayerState& P = w.players[q];
        if (P.curSlot == exclSlot) continue;
        if (P.flags22c & 3) continue;
        const std::uint32_t d = static_cast<std::uint32_t>(Player_cachedDistance(w, pid, q));
        if (d > static_cast<std::uint32_t>(maxDist) || d >= best) continue;
        if (P.posI.x <= x0 || P.posI.x >= x1 || P.posI.z <= z0 || P.posI.z >= z1) continue;
        r = q;
        best = d;
    }
    return r;
}

namespace {
void stepToward(Vec3& out, const Vec3& from, std::int32_t dist, Angle a) { // 0x4BB0D0 (x/z only)
    const Vec3 r = offsetInDirection(from, dist, a);
    out.x = r.x;
    out.z = r.z;
}
} // namespace

// 0x4DB260 (EAX = player, EDX = opponent, EBX = dist, ECX = side flag, stack = out).
void Player_runAround(World& w, int pid, int q, std::int32_t dist, int flag, Vec3& out) {
    const PlayerState& p = w.players[pid];
    const std::int32_t d = Player_cachedDistance(w, pid, q);
    const std::int32_t c = 0x100 - Math_asinRatio(d, dist * 2);
    const Angle aq = Player_cachedAngle(w, pid, q);
    std::int32_t a = flag ? aq - c : aq + c;
    a &= 0x3ff;
    if (a > 0x200) a -= 0x400;
    stepToward(out, p.posI, dist, a);
    if (flag) a = -a;
    const int side = w.teams[p.team].side;
    if ((side == 1 && a > 0) || (side == 0 && a < 0)) {
        out.x = p.posI.x;
        out.z = p.posI.z + (side == 1 ? -dist : dist);
    }
}

// 0x4DB320 (EAX = player, EDX = opponent A, EBX = opponent B, ECX = dist, stack = out).
void Player_chooseRunTarget(World& w, int pid, int a, int b, std::int32_t dist, Vec3& out) {
    PlayerState& p = w.players[pid];
    const int side = w.teams[p.team].side;
    const std::int32_t limX = w.pitch.halfWidth - 0x240, limZ = w.pitch.halfLength - 0x120;
    enum { kDefault, kChosen, kAngle } path = kDefault;
    Angle ang = 0;
    if ((a >= 0 || b >= 0) && dist != 0) {
        path = kChosen;
        if (a < 0) {
            Player_runAround(w, pid, b, dist, 1, out);
        } else if (b < 0) {
            Player_runAround(w, pid, a, dist, 0, out);
        } else {
            const std::int32_t h = dist >> 1;
            std::int32_t e = w.pitch.penAreaLineZ + 0x60;
            bool done = false;
            if (side == 1) {
                e = -e;
                if (e > p.posI.z && Team_opponentInBox(w, pid, -w.pitch.halfWidth, w.pitch.halfWidth,
                                                       -w.pitch.halfLength, e, dist, 0) < 0) {
                    dist = h;
                    out.x = p.posI.x;
                    out.z = p.posI.z + h;
                    done = true;
                }
            } else if (e < p.posI.z && Team_opponentInBox(w, pid, -w.pitch.halfWidth, w.pitch.halfWidth, e,
                                                          w.pitch.halfLength, dist, 0) < 0) {
                dist = h;
                out.x = p.posI.x;
                out.z = p.posI.z - h;
                done = true;
            }
            if (!done) {
                const Angle aA = Player_cachedAngle(w, pid, a);
                const Angle aB = Player_cachedAngle(w, pid, b);
                const std::int32_t df = aB - aA;
                if (df > 0x2b) {
                    path = kAngle;
                    ang = aA + (df >> 1);
                } else if ((p.posI.z + w.pitch.thirdLineZ[side] >= 0 ? 1 : 0) == side) {
                    if (w.rng.next() & 1) Player_runAround(w, pid, b, dist, 0, out);
                    else Player_runAround(w, pid, a, dist, 1, out);
                } else {
                    path = kDefault;
                }
            }
        }
    }
    if (path == kDefault) {
        out.z = -w.pitch.goalLineZ[side];
        out.x = p.posI.x > 0 ? w.pitch.goalHalfWidth : -w.pitch.goalHalfWidth;
        ang = atan2i(out.x - p.posI.x, out.z - p.posI.z);
        path = kAngle;
    }
    if (path == kAngle) stepToward(out, p.posI, dist, ang);
    // 0x4DB3BF
    out.y = 0;
    p.flags22c |= 0x200;
    const int q = Team_nearestPlayer_4523E4(w, out, p.team, 0, nullptr);
    p.flags22c &= ~0x200u;
    if (q >= 0 && Player_cachedDistance(w, pid, q) < 0x120) {
        const int rnd = static_cast<int>(w.rng.next() & 1);
        Player_runAround(w, pid, q, dist, rnd, out);
    }
    std::int32_t df = atan2i(out.x - p.posI.x, out.z - p.posI.z) & 0x3ff;
    if (df > 0x200) df = 0x400 - df;
    if (df >= 0xc7) stepToward(out, p.posI, dist >> 1, p.facing);
    if (-limZ > out.z || out.z > limZ) {
        out = p.posI;
        if (-limX > out.x || out.x > limX) {
            if ((p.posI.z >= 0 ? 1 : 0) != side) out.x += p.posI.x > 0 ? -dist : dist;
            else out.z += side == 1 ? -dist : dist;
        }
    } else if (-limX > out.x || out.x > limX) {
        out = p.posI;
        out.z += side == 1 ? -dist : dist;
    }
    if (-limX > out.x) out.x = -limX;
    else if (out.x > limX) out.x = limX;
    if (-limZ > out.z) out.z = -limZ;
    else if (out.z > limZ) out.z = limZ;
}

// 0x453B5C.
void Team_updateSupportTargets(World& w, int team) {
    TeamState& t = w.teams[team];
    if (!(t.flags & 2)) return;
    if ((w.time.teamOrder ^ team) != 0) return;
    int ref = w.ball.possessor;
    if (ref < 0 || w.players[ref].team != team) {
        ref = t.f1e0;
        if (ref < 0) return;
    }
    const int n = t.playerCount;
    const std::int32_t third = w.pitch.thirdLineZ[t.side];
    t.supportToggle ^= 1;
    const bool toggle = t.supportToggle != 0;
    const std::int32_t hw = w.pitch.halfWidth;
    const bool inPlay = w.match.phase == 9;
    if (t.side == 1) {
        const bool flag = !(inPlay && !(third > w.ball.posI.z));
        const std::int32_t hl = -w.pitch.halfLength;
        for (int i = 0; i < n; ++i) {
            const int pid = t.players[i];
            PlayerState& p = w.players[pid];
            if (!(third >= p.posI.z) && !flag) {
                p.runTarget = p.posI;
                continue;
            }
            std::int32_t r = (Player_cachedDistance(w, ref, pid) * 3) >> 3;
            if (r > 0x3c0) r = 0x3c0;
            int A = -1, Bq = -1;
            if (toggle && (i & 1)) {
                A = Team_opponentInBox(w, pid, p.posI.x, hw, hl, p.posI.z, r, 0);
                Bq = Team_opponentInBox(w, pid, -hw, p.posI.x, hl, p.posI.z, r, 0);
            }
            Player_chooseRunTarget(w, pid, A, Bq, r >> 1, p.runTarget);
        }
    } else {
        const bool flag = !(inPlay && !(third < w.ball.posI.z));
        const std::int32_t hl = w.pitch.halfLength;
        for (int i = 0; i < n; ++i) {
            const int pid = t.players[i];
            PlayerState& p = w.players[pid];
            if (third > p.posI.z && !flag) {
                p.runTarget = p.posI;
                continue;
            }
            std::int32_t r = (Player_cachedDistance(w, ref, pid) * 3) >> 3;
            if (r > 0x3c0) r = 0x3c0;
            int A = -1, Bq = -1;
            if (toggle && (i & 1)) {
                A = Team_opponentInBox(w, pid, -hw, p.posI.x, p.posI.z, hl, r, 0);
                Bq = Team_opponentInBox(w, pid, p.posI.x, hw, p.posI.z, hl, r, 0);
            }
            Player_chooseRunTarget(w, pid, A, Bq, r >> 1, p.runTarget);
        }
    }
}

} // namespace wc98::sim

namespace wc98::sim {

// 0x451C2C (EAX = team, EDX = frames, ECX = excluded player (vs +0x24C / +0x254), EBX = skip keeper):
// player countdown = ((20 - attr[+0x35]) >> 1) + frames.
void Team_holdPlayers_451C2C(World& w, int team, int frames, int ecx, int ebx) {
    const TeamState& t = w.teams[team];
    for (int i = 0; i < t.playerCount; ++i) {
        const int pid = t.players[i];
        PlayerState& p = w.players[pid];
        if (p.slot == 0 && ebx != 0) continue;
        if (ecx >= 0 && (ecx + 1 == p.markTarget || ecx + 1 == p.f254)) continue;
        p.countdown = ((0x14 - static_cast<std::int32_t>(Player_attr(w, pid, 0x35))) >> 1) + frames;
    }
}

// 0x451CA0: any of the team's players with byte +0x22D bit 0 (flags22c 0x100) set.
int Team_playersBusy(const World& w, int team) {
    const TeamState& t = w.teams[team];
    for (int k = 0; k < t.playerCount; ++k)
        if (w.players[t.players[k]].flags22c & 0x100) return 1;
    return 0;
}

// 0x451CEC: any of the team's players with +0x22C bit 7 set.
int Team_playersWalking(const World& w, int team) {
    const TeamState& t = w.teams[team];
    for (int k = 0; k < t.playerCount; ++k)
        if (w.players[t.players[k]].flags22c & 0x80) return 1;
    return 0;
}

} // namespace wc98::sim
