// Squad / line-up module 0x49E000..0x4A1000 (owner: SimPorter3): substitutions (AI 0x453E58 and the
// queue consumed by the restart sequences), injuries, sending-off, slot re-assignment and set-piece
// taker allocation. The line-up sheet 0x66E564 + t*0x2C is w.pl.lineup[t]:
//   +0x00 cur[11]  squad index of each pitch index (team+0x184 order)
//   +0x0B sel[11]  confirmed line-up
//   +0x16 req[11]  requested line-up (IGM / AI edits)
//   +0x22 u16 pending substitutions (0x66E586), +0x24 u16 substitutions left (0x66E588, starts at
//   DAT_0065EEE8), +0x26 u16 queued edits (0x66E58A), +0x28 dword injury-substitution flag (0x66E58C).
// Every function is transcribed from the disassembly (Watcom regcall: EAX, EDX, EBX, ECX, stack).
#include <cstring>

#include "wc98/sim/data.hpp"
#include "wc98/sim/world.hpp"
#include "player_states.hpp"

namespace wc98::sim {

namespace {

using u32 = std::uint32_t;

std::uint8_t* sheet(World& w, int t) { return w.pl.lineup[t]; }
const std::uint8_t* sheet(const World& w, int t) { return w.pl.lineup[t]; }
std::uint16_t get16(const std::uint8_t* s, int off) {
    std::uint16_t v;
    std::memcpy(&v, s + off, 2);
    return v;
}
void set16(std::uint8_t* s, int off, std::uint16_t v) { std::memcpy(s + off, &v, 2); }
std::int32_t get32(const std::uint8_t* s, int off) {
    std::int32_t v;
    std::memcpy(&v, s + off, 4);
    return v;
}
void set32(std::uint8_t* s, int off, std::int32_t v) { std::memcpy(s + off, &v, 4); }

// 0x65F044 + t*0x134: squad size handed over by the front end.
int squadCount(const World& w, int t) {
    const std::size_t n = w.data->sheets[t].squad.size();
    return static_cast<int>(n < kSquadMax ? n : kSquadMax);
}
PlayerAttr& attrRec(World& w, int t, int squad) { return w.attrs[t * kSquadMax + squad]; }
const PlayerAttr& attrRec(const World& w, int t, int squad) { return w.attrs[t * kSquadMax + squad]; }
// Formation record slot s of the team's active record: {u8 used, i8 dx, i8 dz, u8, u16 tag}.
std::uint8_t* slotRec(World& w, int t, int rec, int s) { return w.teams[t].rec(rec) + 8 + s * 6; }
const std::uint8_t* slotRec(const World& w, int t, int rec, int s) { return w.teams[t].rec(rec) + 8 + s * 6; }

// exe 0x586B74, 17 rows x 11 i32: {slot, 5 DB-position masks (0x49FDC8 / 0x4A0260 use 6..10 and
// 1..5, see callers), 5 on-pitch position masks}.
constexpr std::int32_t kSlotFill[187] = {
    0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    2, 126, 1792, 131070, 0, 2, 58, 126, 1792, 3968, 4, 16,
    126, 1792, 131070, 0, 16, 58, 126, 1792, 3968, 5, 16, 126,
    1792, 131070, 0, 32, 58, 126, 1792, 3968, 3, 16, 126, 1792,
    131070, 0, 8, 58, 126, 1792, 3968, 14, 16384, 126976, 69632, 131070,
    0, 16384, 57344, 126976, 1792, 3968, 15, 16384, 126976, 69632, 131070, 0,
    32768, 57344, 126976, 1792, 3968, 13, 16384, 126976, 69632, 131070, 0, 8192,
    57344, 126976, 1792, 3968, 9, 1792, 3968, 126, 131070, 0, 0, 0,
    0, 512, 57402, 10, 1792, 3968, 126, 131070, 0, 0, 0, 0,
    1024, 57402, 8, 1792, 3968, 126, 131070, 0, 0, 0, 0, 256,
    57402, 16, 65536, 126976, 2176, 131070, 0, 65536, 34816, 16384, 126976, 3968,
    12, 4096, 126976, 2176, 131070, 0, 4096, 8320, 16384, 126976, 3968, 11,
    2048, 3968, 126976, 131070, 0, 0, 0, 0, 2048, 65600, 7, 128,
    3968, 126976, 131070, 0, 0, 0, 0, 128, 4100, 6, 64, 126,
    1792, 131070, 0, 64, 48, 2048, 58, 3968, 2, 4, 126, 1792,
    131070, 0, 4, 24, 128, 58, 3968,
};
const std::int32_t* fillRow(int r) { return kSlotFill + r * 11; }
// exe 0x586AEC / 0x586B30, 17 x i32: slot preference orders of set-piece takers 1 / 0 (0x4A0F3C).
constexpr std::int32_t kTakerOrder1[17] = {12, 7, 8, 2, 3, 13, 14, 1, 9, 4, 15, 10, 5, 16, 11, 6, 0};
constexpr std::int32_t kTakerOrder0[17] = {16, 11, 10, 6, 5, 15, 14, 1, 9, 4, 13, 8, 3, 12, 7, 2, 0};

// 0x49F5E8 (EAX count, EDX array, BL x, CL y, stack out1, out2; ret 8).
void findTwo(u32 count, const std::uint8_t* a, std::uint8_t x, std::uint8_t y, int* fx, int* fy) {
    *fx = 0;
    *fy = 0;
    for (u32 i = 0; i < count; ++i) {
        if (x == a[i]) *fx = 1;
        if (y == a[i]) *fy = 1;
    }
}

// 0x4D0704 (EAX team, EDX squad): 0 if sent off (0x20) or substituted (0x10).
int eligible(const World& w, int t, int squad) {
    const std::uint8_t f = attrRec(w, t, squad).at(0x40);
    return (f & 0x20) || (f & 0x10) ? 0 : 1;
}

// 0x4D06B8 (EAX team, EDX squad): mark substituted (not in training, after kick-off, limited subs).
void markSubstituted(World& w, int t, int squad) {
    if (w.opt.gameMode == 1 || static_cast<u32>(w.match.totalSec) == 0 || w.opt.maxSubs == 9999) return;
    attrRec(w, t, squad).at(0x40) |= 0x10;
}

// 0x49F454 (EAX team, EDX squad, EBX cur index, ECX req index, stack sel index, flag, out; ret 0xC).
int classify(const World& w, int t, int squad, int curIdx, int reqIdx, int selIdx, int flag, int* out) {
    const bool c = curIdx != 0xff, r = reqIdx != 0xff, s = selIdx != 0xff;
    *out = 1;
    if (c && r && c != s) return 0;
    if (r == s && c != s) {
        *out = 2;
        return 0;
    }
    const MatchState& m = w.match;
    if (c && m.phase == 10 && m.card != 0 && m.offender >= 0 && t == m.offender / kPlayersPerTeam &&
        squad == static_cast<int>(w.attrs[w.players[m.offender].attr].squadIndex)) {
        *out = 5;
        return 0;
    }
    if (c && r && s) {
        if (flag == 0) return 3;
    } else if (!c && !r && !s) {
        if (flag == 0) return 2;
        if (eligible(w, t, squad)) {
            if (Squad_subsLeft_49ED4C(w, t) == 0) {
                *out = 4;
                return 0;
            }
            return 4;
        }
        *out = 3;
        return 0;
    } else if (c && !r && s) {
        return flag == 0 ? 2 : 6;
    } else if (c || !r || s) {
        return 0;
    } else if (flag == 0) {
        return 5;
    }
    return 1;
}

// 0x49ED84 (EAX team, EDX a, EBX b, ECX out kind): nonzero = refused (code), else *kind 1..5.
int checkSwap(const World& w, int t, int a, int b, int* kind) {
    *kind = 0;
    const int nSquad = squadCount(w, t);
    if (a == b || static_cast<u32>(nSquad) <= static_cast<u32>(a) || static_cast<u32>(nSquad) <= static_cast<u32>(b))
        return 1;
    const std::uint8_t* S = sheet(w, t);
    int aCur = 0xff, bCur = 0xff, aReq = 0xff, bReq = 0xff, aSel = 0xff, bSel = 0xff;
    const int n = w.teams[t].playerCount;
    for (int i = 0; i < n; ++i) {
        if (a == S[i]) aCur = i;
        if (b == S[i]) bCur = i;
        if (a == S[0x16 + i]) aReq = i;
        if (b == S[0x16 + i]) bReq = i;
        if (a == S[0xb + i]) aSel = i;
        if (b == S[0xb + i]) bSel = i;
    }
    int fa, fb;
    if ((aReq != 0xff) == (bReq != 0xff)) {
        fa = fb = aReq != 0xff ? 1 : 0;
    } else {
        fa = aReq == 0xff ? 1 : 0;
        fb = bReq == 0xff ? 1 : 0;
    }
    int r1, r2;
    const int i1 = classify(w, t, a, aCur, aReq, aSel, fa, &r1);
    const int i2 = classify(w, t, b, bCur, bReq, bSel, fb, &r2);
    if (i1 == 0) return r1;
    if (i2 == 0) return r2;
    if (i1 == 1) {
        *kind = 4;
        return 0;
    }
    if (i1 == 2) {
        *kind = 5;
        return 0;
    }
    const int A = (i1 == 3 || i1 == 4 || i2 == 3 || i2 == 4) ? 1 : 0;
    const int B = (i1 == 5 || i1 == 6 || i2 == 5 || i2 == 6) ? 1 : 0;
    *kind = A - B > 0 ? 2 : (A == B ? 1 : 3);
    return 0;
}

// 0x49FB38 (EAX team, EDX DB-position mask, EBX used[squad]): best rated (DB +0x27) free squad
// member whose DB position (+0x11) is in the mask; 0xFF none.
int pickSquad(const World& w, int t, std::int32_t mask, const std::int32_t* used) {
    int list[kSquadMax], n = 0;
    const int nSquad = squadCount(w, t);
    for (int i = 0; i < nSquad; ++i)
        if (used[i] == 0 && (static_cast<u32>(mask) & (1u << (Player_dbByte(w, t, i, 0x11) & 0x1f)))) list[n++] = i;
    if (n == 1) return list[0];
    if (n == 0) return 0xff;
    int best = list[0];
    std::uint8_t r = Player_dbByte(w, t, best, 0x27);
    for (int k = 1; k < n; ++k)
        if (Player_dbByte(w, t, list[k], 0x27) > r) {
            r = Player_dbByte(w, t, list[k], 0x27);
            best = list[k];
        }
    return best;
}

// 0x49FCBC (EAX team, EDX position mask, EBX used[player index]): best rated on-pitch player whose
// position (+0x04, 0x49EB10) is in the mask; returns its index (+0x00), 0xFF none.
int pickOnPitch(const World& w, int t, std::int32_t mask, const std::int32_t* used) {
    const TeamState& T = w.teams[t];
    int list[kPlayersPerTeam], n = 0;
    for (int i = 0; i < T.playerCount; ++i) {
        const PlayerState& p = w.players[T.players[i]];
        if (used[p.index] == 0 && (static_cast<u32>(mask) & (1u << (p.f004 & 0x1f)))) list[n++] = i;
    }
    if (n == 0) return 0xff;
    const PlayerState* q = &w.players[T.players[list[0]]];
    int res = q->index;
    if (n == 1) return res;
    std::uint8_t r = Player_dbByte(w, t, q->dbPlayer, 0x27);
    for (int k = 1; k < n; ++k) {
        const PlayerState& p = w.players[T.players[list[k]]];
        const std::uint8_t v = Player_dbByte(w, t, p.dbPlayer, 0x27);
        if (v > r) {
            r = v;
            res = p.index;
        }
    }
    return res;
}

void setSlot(PlayerState& p, int s) {
    p.slot = p.curSlot = p.slotCopy = s;
    p.baseRole = p.role = kSlotRole[s];
}

// 0x49FDC8 (EAX pass, EDX team, EBX k, ECX slotUsed[17], stack squadUsed[22]; ret 4): fill the
// active record's free slots from the bench in table order.
int fillFromSquad(World& w, int pass, int t, int k, std::int32_t* slotUsed, std::int32_t* squadUsed) {
    TeamState& T = w.teams[t];
    if (static_cast<u32>(k) >= static_cast<u32>(T.playerCount)) return k;
    for (int r = 0; r < kSlots; ++r) {
        const std::int32_t* row = fillRow(r);
        const int s = row[0];
        if (slotUsed[s] != 0 || slotRec(w, t, w.formation.selRec[t], s)[0] == 0) continue;
        const std::int32_t mask = pass >= 5 ? -1 : row[1 + pass];
        const int sq = pickSquad(w, t, mask, squadUsed);
        if (sq == 0xff) continue;
        const int pid = T.players[k];
        setSlot(w.players[pid], s);
        Squad_bindPlayer_49FA9C(w, t, k, sq);
        if (s == 0) T.f1e4 = pid;
        slotUsed[s] = 1;
        squadUsed[sq] = 1;
        std::uint8_t* sr = slotRec(w, t, T.formIdx(), s);
        sr[1] = 0;
        sr[2] = 0;
        set16(sr, 4, static_cast<std::uint16_t>(sq));
        ++k;
        if (static_cast<u32>(T.playerCount) <= static_cast<u32>(k)) return k;
    }
    return k;
}

// 0x49FF44 (EAX pass, EDX team, EBX k, ECX slotUsed[17], stack playerUsed[22]; ret 4): re-assign
// the players on the pitch to the active record's slots.
int fillFromPitch(World& w, int pass, int t, int k, std::int32_t* slotUsed, std::int32_t* playerUsed) {
    TeamState& T = w.teams[t];
    if (static_cast<u32>(k) >= static_cast<u32>(T.playerCount)) return k;
    for (int r = 0; r < kSlots; ++r) {
        const std::int32_t* row = fillRow(r);
        const int s = row[0];
        if (slotUsed[s] != 0 || slotRec(w, t, w.formation.selRec[t], s)[0] == 0) continue;
        const std::int32_t mask = pass >= 5 ? -1 : row[6 + pass];
        const int idx = pickOnPitch(w, t, mask, playerUsed);
        if (idx == 0xff) continue;
        const int pid = T.players[idx];
        PlayerState& p = w.players[pid];
        setSlot(p, s);
        if (s == 0) T.f1e4 = pid;
        slotUsed[s] = 1;
        playerUsed[idx] = 1;
        set16(slotRec(w, t, T.formIdx(), s), 4, static_cast<std::uint16_t>(w.attrs[p.attr].squadIndex));
        ++k;
        if (static_cast<u32>(k) >= static_cast<u32>(T.playerCount)) return k;
    }
    return k;
}

// 0x4CF0AC (EAX slot, EDX record descriptor, EBX record slot table): remove a slot from its line and
// re-pack the remaining players of that line into the line's standard pattern.
void removeSlot(int s, std::uint8_t* desc, std::uint8_t* slots) {
    std::int32_t tags[5] = {-1, -1, -1, -1, -1};
    const int cls = kSlotRole[s];
    int first, count;
    switch (cls) {
    case 0: first = 0, count = 1; break;
    case 1: first = 1, count = 1; break;
    case 2: first = 2, count = 5; break;
    case 3: first = 7, count = 5; break;
    default: first = 12, count = 5; break;
    }
    std::uint8_t* g = slots + first * 6;
    int n = 0;
    for (int i = 0; i < count; ++i) {
        std::uint8_t* e = g + i * 6;
        if (e[0] != 0 && first + i != s) tags[n++] = get16(e, 4);
        e[0] = 0;
    }
    desc[cls] = static_cast<std::uint8_t>(n);
    if (cls <= 1) {
        g[0] = n > 0 ? 1 : 0;
        return;
    }
    int k = 0;
    auto put = [&](std::uint8_t* e) {
        e[0] = 1;
        set16(e, 4, static_cast<std::uint16_t>(tags[k++]));
    };
    if (n >= 4) put(g);
    if (n >= 2) put(g + 6);
    if (n & 1) put(g + 12);
    if (n >= 2) put(g + 18);
    if (n >= 4) put(g + 24);
}

// 0x4A0F3C (EAX slot table, EDX order[17], EBX blocked[17]): first free slot tag in preference order.
std::uint16_t takerByOrder(const std::uint8_t* slots, const std::int32_t* order, const std::int32_t* blocked) {
    for (int i = 0; i < kSlots; ++i) {
        const std::uint8_t* e = slots + order[i] * 6;
        if (e[0] != 0 && blocked[order[i]] == 0) return get16(e, 4);
    }
    for (int i = 0; i < kSlots; ++i) {
        const std::uint8_t* e = slots + order[i] * 6;
        if (e[0] != 0) return get16(e, 4);
    }
    return 0;
}

// 0x4A0F0C (EAX n, EDX values): index of the first maximum.
int argMax(int n, const std::int32_t* v) {
    std::int32_t best = v[0];
    int k = 0;
    for (int i = 1; i < n; ++i)
        if (best < v[i]) {
            best = v[i];
            k = i;
        }
    return k;
}

// 0x4A0FA8 (EAX value, EDX count, EBX u16 array): index of value, -1 none.
int findTag(std::int32_t v, int n, const std::uint16_t* a) {
    for (int i = 0; i < n; ++i)
        if (a[i] == v) return i;
    return -1;
}

// 0x4A0888 (EAX team, EDX squad): base slot of the player wearing that squad index, -1 none.
int slotOfSquad(const World& w, int t, int squad) {
    const std::uint8_t* S = sheet(w, t);
    for (int k = 0; k < w.teams[t].playerCount; ++k)
        if (S[k] == squad) return w.players[w.teams[t].players[k]].slot;
    return -1;
}

// team+0x160 + k*2: set-piece taker squad tags.
std::uint16_t taker(const TeamState& T, int k) { return get16(T.blk, 0x160 - 0x0C + k * 2); }
void setTaker(TeamState& T, int k, std::uint16_t v) { set16(T.blk, 0x160 - 0x0C + k * 2, v); }

// 0x4A0E24 (EAX team, EDX squad): drop a squad index from the taker list team+0x160, re-allocate.
void dropTaker(World& w, int t, int squad) {
    TeamState& T = w.teams[t];
    for (int k = 0; k < 9; ++k)
        if (taker(T, k) == squad) setTaker(T, k, 0xff);
    Team_allocateTakers_4A0AA0(w, t);
}

} // namespace

// 0x49ED4C (EAX team): substitutions still available = left - pending - queued.
int Squad_subsLeft_49ED4C(const World& w, int t) {
    const std::uint8_t* S = sheet(w, t);
    return static_cast<int>(static_cast<u32>(get16(S, 0x24)) - get16(S, 0x22) - get16(S, 0x26));
}

// 0x49FA9C (EAX team, EDX pitch index, EBX squad).
void Squad_bindPlayer_49FA9C(World& w, int t, int k, int squad) { Player_bindSquadMember(w, t, k, squad); }

// 0x49F03C (EAX team, EDX squad a, EBX squad b): request swapping a for b in the requested line-up.
void Squad_substitute_49F03C(World& w, int t, int a, int b) {
    int kind;
    if (checkSwap(w, t, a, b, &kind) != 0) return;
    if (kind == 5) return;
    std::uint8_t* S = sheet(w, t);
    int ai = 0xff, bi = 0xff;
    for (int i = 0; i < w.teams[t].playerCount; ++i) {
        if (S[0x16 + i] == a) ai = i;
        if (S[0x16 + i] == b) bi = i;
    }
    if (kind == 4) {
        std::swap(S[0x16 + ai], S[0x16 + bi]);
        return;
    }
    if (ai == 0xff) S[0x16 + bi] = static_cast<std::uint8_t>(a);
    else S[0x16 + ai] = static_cast<std::uint8_t>(b);
    if (kind == 2) set16(S, 0x26, static_cast<std::uint16_t>(get16(S, 0x26) + 1));
    else if (kind == 3) set16(S, 0x26, static_cast<std::uint16_t>(get16(S, 0x26) - 1));
}

// 0x49F140 (EAX team): apply the requested line-up: on-pitch swaps rebind at once, bench entries
// become pending substitutions (made at the next restart, 0x471FD4).
void Squad_applyLineup_49F140(World& w, int t) {
    TeamState& T = w.teams[t];
    std::uint8_t* S = sheet(w, t);
    const int n = T.playerCount;
    for (int i = 0; i < n; ++i) {
        if (S[0x16 + i] == S[i]) continue;
        int fSel, fReq;
        findTwo(static_cast<u32>(n), S, S[0xb + i], S[0x16 + i], &fSel, &fReq);
        if (!fSel || !fReq) continue;
        int j = 0;
        while (j < n && S[j] != S[0x16 + i]) ++j;
        const std::uint8_t saved = S[0x16 + j], x = S[i], y = S[0x16 + i];
        Squad_bindPlayer_49FA9C(w, t, j, x);
        Squad_bindPlayer_49FA9C(w, t, i, y);
        S[0x16 + j] = saved;
        for (int r = 0; r < 3; ++r)
            for (int s = 0; s < kSlots; ++s) {
                std::uint8_t* e = slotRec(w, t, r, s);
                if (!e[0]) continue;
                if (get16(e, 4) == x) set16(e, 4, y);
                else if (get16(e, 4) == y) set16(e, 4, x);
            }
        if (w.players[T.players[i]].slot == 0) dropTaker(w, t, S[i]);
        if (w.players[T.players[j]].slot == 0) dropTaker(w, t, S[j]);
    }
    for (int i = 0; i < n; ++i) {
        if (S[0x16 + i] == S[i]) continue;
        int fSel, fReq;
        findTwo(static_cast<u32>(n), S, S[0xb + i], S[0x16 + i], &fSel, &fReq);
        if (fSel) {
            S[0xb + i] = S[0x16 + i];
            set16(S, 0x22, static_cast<std::uint16_t>(get16(S, 0x22) + 1));
        }
    }
    set16(S, 0x26, 0);
}

// 0x49F630 (EAX out team, EDX out index): next pending substitution, team 0 first.
bool Squad_nextSub_49F630(World& w, int* team, int* idx) {
    *idx = 0xff;
    if (get16(sheet(w, 0), 0x22) != 0) {
        *team = 0;
        *idx = Squad_firstPending_49FA48(w, 0);
    } else if (get16(sheet(w, 1), 0x22) != 0) {
        *team = 1;
        *idx = Squad_firstPending_49FA48(w, 1);
    }
    return *idx != 0xff;
}

// 0x49FA48 (EAX team): first pitch index whose confirmed line-up differs from the current one.
int Squad_firstPending_49FA48(const World& w, int t) {
    const std::uint8_t* S = sheet(w, t);
    for (int i = 0; i < w.teams[t].playerCount; ++i)
        if (S[i] != S[0xb + i]) return i;
    return 0xff;
}

int Squad_pendingCount_66E586(const World& w, int t) { return get16(sheet(w, t), 0x22); }

// 0x49F684 (EAX team): carry out the first pending substitution (State186 0x536491 / 0x536643).
void Team_removeInjured_49F684(World& w, int t) {
    TeamState& T = w.teams[t];
    std::uint8_t* S = sheet(w, t);
    if (get16(S, 0x22) == 0) return;
    for (int k = 0; k < T.playerCount; ++k) {
        const std::uint8_t outSq = S[k], inSq = S[0xb + k];
        if (outSq == inSq) continue;
        if (w.attrs[w.players[T.players[k]].attr].at(0x40) & 1) --T.w360[4]; // team+0x370 injured on pitch
        markSubstituted(w, t, outSq);
        Squad_bindPlayer_49FA9C(w, t, k, inSq);
        set16(S, 0x22, static_cast<std::uint16_t>(get16(S, 0x22) - 1));
        if (w.opt.maxSubs != 9999) set16(S, 0x24, static_cast<std::uint16_t>(get16(S, 0x24) - 1));
        for (int r = 0; r < 3; ++r)
            for (int s = 0; s < kSlots; ++s) {
                std::uint8_t* e = slotRec(w, t, r, s);
                if (e[0] && get16(e, 4) == outSq) set16(e, 4, inSq);
            }
        dropTaker(w, t, S[k]);
        w.emit(EventType::Substitution, t, T.players[k], outSq, inSq);
        return;
    }
}

// 0x4A0260 (EAX team, EDX squad leaving, EBX mode 0 same position / 1 attacker / 2 defender):
// replacement from the bench; 0xFF none.
int Squad_pickReplacement_4A0260(const World& w, int t, int outSquad, int mode) {
    std::int32_t used[kSquadMax] = {};
    const int nSquad = squadCount(w, t);
    for (int i = 0; i < nSquad; ++i) {
        // 0x65EFB8 status struct +0xB (career suspension) is not part of MatchData: zero (see
        // Team_buildMatchPlayerAttrs), so only the eligibility and injury bits decide.
        if (!eligible(w, t, i) || (attrRec(w, t, i).at(0x40) & 1)) used[i] = 1;
    }
    const std::uint8_t* S = sheet(w, t);
    for (int k = 0; k < w.teams[t].playerCount; ++k) used[S[k]] = 1;
    const TeamState& T = w.teams[t];
    int s;
    if (mode == 1) {
        s = 12;
        while (s <= 16 && slotRec(w, t, T.formIdx(), s)[0] != 0) ++s;
        if (s > 16) return 0xff;
    } else if (mode == 2) {
        s = 2;
        while (s <= 6 && slotRec(w, t, T.formIdx(), s)[0] != 0) ++s;
        if (s > 6) return 0xff;
    } else {
        s = Player_dbByte(w, t, outSquad, 0x11);
    }
    int r = 0;
    while (r < kSlots && fillRow(r)[0] != s) ++r; // (exe reads row 17 past the table if absent)
    if (r == kSlots) return 0xff;
    int res = 0xff;
    for (int k = 0; k < 5; ++k) {
        res = pickSquad(w, t, fillRow(r)[6 + k], used);
        if (res != 0xff) break;
    }
    return res;
}

// 0x49FA00 (EAX team, EDX pitch index, EBX mode): replacement for the player at the index.
int Squad_pickInjurySub_49FA00(World& w, int t, int index, int mode) {
    if (get16(sheet(w, t), 0x24) == 0) return 0xff;
    const int r = Squad_pickReplacement_4A0260(w, t, sheet(w, t)[index], mode);
    if (r < 0 || static_cast<u32>(r) >= static_cast<u32>(squadCount(w, t))) return 0xff;
    return r;
}

// 0x453FD8 (EAX team, EDX in/out mode): AI choice of a player to take off (0 = -1 none).
static int aiPickTired(World& w, int t, int* mode) {
    const MatchState& m = w.match;
    const int left = Squad_subsLeft_49ED4C(w, t);
    int res = -1, wantTired = 0, wantBooked = 0;
    const TeamState& T = w.teams[t];
    const std::int32_t used = w.opt.maxSubs - left - T.w360[3]; // team+0x36C injuries
    const std::int32_t injuryFlag = get32(sheet(w, t), 0x28);
    if (injuryFlag == 0) {
        bool late = false;
        if (m.periodIndex == 1) {
            // fild of the u32 pair as int64, fdivp, compare with 0.9 (0x56F4E4)
            late = static_cast<double>(static_cast<u32>(m.elapsedSec)) / static_cast<double>(static_cast<u32>(m.durationSec)) > 0.9;
        }
        if (late || m.periodIndex == 2 || m.periodIndex == 3) {
            const int diff = m.score[t] - m.score[t == 0 ? 1 : 0];
            if (diff < 0) {
                if (diff >= -2) *mode = 1;
            } else if (diff > 0 && diff <= 2) {
                *mode = 2;
            }
        }
    }
    switch (m.periodIndex) {
    case 0: // 0x454110
        if (used != 0) return -1;
        wantBooked = 1;
        break;
    case 1: // 0x454324
        if (!(used == 0 || (used == 1 && injuryFlag != 0))) return -1;
        wantBooked = wantTired = 1;
        break;
    case 2: case 3: // 0x454364
        if (!(used <= 1 || (used == 2 && injuryFlag != 0))) return -1;
        wantTired = 1;
        break;
    default: return -1;
    }
    const int n = T.playerCount;
    std::int32_t fit[kPlayersPerTeam];
    std::int32_t sum = 0;
    for (int i = 0; i < n; ++i) {
        const PlayerAttr& a = w.attrs[w.players[T.players[i]].attr];
        std::int32_t f = 0;
        for (int off = 0x2f; off <= 0x35; ++off) f += a.at(off);
        for (int off = 0x37; off <= 0x3a; ++off) f += a.at(off);
        f += a.stamina18;
        for (int off = 0x28; off <= 0x2d; ++off) f += a.at(off);
        fit[i] = f;
        sum += f;
    }
    const std::int32_t avg = sum / n;
    const std::int32_t thr = 4 - static_cast<std::int32_t>(w.rng.next() & 1);
    int list[3 * kPlayersPerTeam], cnt = 0;
    for (int i = 0; i < n; ++i) {
        if (!(avg > fit[i])) continue;
        const PlayerState& p = w.players[T.players[i]];
        if (p.slot == 0) continue;
        if (*mode == 1) {
            if (p.role == 2 || p.curSlot == 1) list[cnt++] = i;
            else continue;
        } else if (*mode == 2) {
            if (p.role == 4) list[cnt++] = i;
            else continue;
        }
        const PlayerAttr& a = w.attrs[p.attr];
        if (wantTired && a.stamina18 <= static_cast<u32>(thr)) list[cnt++] = i;
        if (wantBooked && (attrRec(w, t, a.squadIndex).at(0x40) & 2)) list[cnt++] = i;
    }
    if (cnt >= 1) res = T.players[list[w.rng.next() % static_cast<u32>(cnt)]];
    return res;
}

// 0x453E58 (Sim_frame 0x455293): every >= 20 seconds of a period, an AI team (rand & 3: 0 or 1)
// without a pending substitution may bring on a substitute for a tired, booked or injured player.
void Team_aiSubstitutions_453E58(World& w) {
    MatchState& m = w.match;
    const u32 e = static_cast<u32>(m.elapsedSec);
    if (e < 0x14) return;
    if (!(e < static_cast<u32>(m.subCheckSec) || e - static_cast<u32>(m.subCheckSec) >= 0x14)) return;
    m.subCheckSec = m.elapsedSec;
    const u32 t = w.rng.next() & 3;
    if (t >= 2) return;
    const TeamState& T = w.teams[t];
    if (static_cast<u32>(T.humans) > 0 && T.setupAuto == 0) return;
    if (get16(sheet(w, t), 0x22) != 0) return;
    int mode = 0;
    int pid = aiPickTired(w, static_cast<int>(t), &mode);
    if (pid < 0 && static_cast<u32>(T.w360[4]) > 0 && static_cast<u32>(Squad_subsLeft_49ED4C(w, t)) > 0) {
        for (int i = 0; i < T.playerCount; ++i) {
            if (w.attrs[w.players[T.players[i]].attr].at(0x40) & 1) {
                pid = T.players[i];
                mode = 0;
                break;
            }
        }
    }
    if (pid < 0) return;
    const PlayerState& p = w.players[pid];
    const int r = Squad_pickInjurySub_49FA00(w, static_cast<int>(t), p.index, mode);
    if (r == 0xff) return;
    Squad_substitute_49F03C(w, static_cast<int>(t), static_cast<int>(w.attrs[p.attr].squadIndex), r);
    Squad_applyLineup_49F140(w, static_cast<int>(t));
    if (mode != 0) set32(sheet(w, t), 0x28, 1); // 0x453F85..0x453F9D
}

// 0x49F804 (EAX team, EDX pitch index): send a player off: the team list shrinks, the player moves
// past the end, every record loses his slot.
void Team_sendOff(World& w, int t, int idx) {
    TeamState& T = w.teams[t];
    std::uint8_t* S = sheet(w, t);
    --T.playerCount;
    const int pid = T.players[idx];
    const std::uint8_t a = S[idx];
    std::uint8_t b = S[0xb + idx];
    const int n = T.playerCount;
    if (b != a) {
        S[0x16 + idx] = a;
        S[0xb + idx] = a;
        set16(S, 0x22, static_cast<std::uint16_t>(get16(S, 0x22) - 1));
        b = a;
    }
    for (int i = idx; i < n; ++i) {
        T.players[i] = T.players[i + 1];
        w.players[T.players[i]].index = i;
        Squad_bindPlayer_49FA9C(w, t, i, S[i + 1]);
        S[0x16 + i] = S[0x17 + i];
        S[0xb + i] = S[0xc + i];
    }
    T.players[n] = pid;
    w.players[pid].index = n;
    Squad_bindPlayer_49FA9C(w, t, n, a);
    S[0xb + n] = b;
    S[0x16 + n] = b;
    for (int r = 0; r < 3; ++r) {
        int s = -1;
        for (int k = 0; k < kSlots && s < 0; ++k) {
            const std::uint8_t* e = slotRec(w, t, r, k);
            if (e[0] && get16(e, 4) == a) s = k;
        }
        // s < 0 would make 0x4CF0AC index 0x58B028[-1]; the three records always hold the starters.
        if (s >= 0) removeSlot(s, T.rec(r) + 1, T.rec(r) + 8);
    }
    --T.blk[0xf - 0xc];
    dropTaker(w, t, a);
    if (pid == T.f1e4) T.f1e4 = -1;
}

// 0x4A0134 (EAX team, EDX 1 = refill from the bench / 0 = re-assign the players on the pitch).
void Team_humanAfterSendOff(World& w, int t, int fromBench) {
    TeamState& T = w.teams[t];
    const int n = T.playerCount;
    std::int32_t slotUsed[kSlots] = {};
    std::int32_t used[kSquadMax];
    int k = 0;
    if (fromBench == 1) {
        for (std::int32_t& u : used) u = 1;
        const std::uint8_t* S = sheet(w, t);
        for (int i = 0; i < n; ++i) used[S[i]] = 0;
        for (int pass = 0; pass <= 5; ++pass)
            if (static_cast<u32>(k) < static_cast<u32>(T.playerCount)) k = fillFromSquad(w, pass, t, k, slotUsed, used);
    } else {
        for (int i = 0; i < kSquadMax; ++i) used[i] = i < n ? 0 : 1;
        for (int pass = 0; pass <= 5; ++pass)
            if (k < n) k = fillFromPitch(w, pass, t, k, slotUsed, used);
    }
    Team_buildSupportLinks(w, t);
}

// 0x49EB10 (EAX team): player +0x04 = slot of the active record wearing his squad index.
void Team_assignPositions_49EB10(World& w, int t) {
    TeamState& T = w.teams[t];
    for (int s = 0; s < kSlots; ++s) {
        const std::uint8_t* e = slotRec(w, t, T.formIdx(), s);
        if (!e[0]) continue;
        const int tag = get16(e, 4);
        for (int i = 0; i < T.playerCount; ++i) {
            PlayerState& p = w.players[T.players[i]];
            if (p.dbPlayer == tag) { // 0x65EF60[t*0x4D + tag] == player+0x24
                p.f004 = s;
                break;
            }
        }
    }
}

// 0x4A0AA0 (EAX team): set-piece takers team+0x160 (9 squad tags; 0..3 by slot order / ratings,
// 4..8 by A[0x37] * A[0x38]); invalid or injured tags are replaced.
void Team_allocateTakers_4A0AA0(World& w, int t) {
    TeamState& T = w.teams[t];
    std::uint16_t tags[kSlots];
    std::int32_t wA[kSlots], wB[kSlots], blocked[kSlots];
    for (int s = 0; s < kSlots; ++s) {
        const std::uint8_t* e = slotRec(w, t, T.formIdx(), s);
        if (e[0]) {
            const std::uint16_t tag = get16(e, 4);
            tags[s] = tag;
            const PlayerAttr& A = attrRec(w, t, tag);
            if (A.at(0x40) & 1) {
                wA[s] = wB[s] = -1;
                blocked[s] = 1;
            } else {
                wA[s] = A.at(0x38) * 100 + A.at(0x37);
                wB[s] = A.at(0x37) * A.at(0x38);
                blocked[s] = 0;
            }
        } else {
            wA[s] = wB[s] = static_cast<std::int32_t>(0xffffff01);
            blocked[s] = 1;
            tags[s] = 0xff;
        }
    }
    if (wA[0] > 0) wA[0] = 0;
    if (wB[0] > 0) wB[0] = 0;
    for (int i = 0; i < 4; ++i)
        if (taker(T, i) != 0xff && findTag(taker(T, i), kSlots, tags) < 0) setTaker(T, i, 0xff);
    for (int i = 4; i <= 8; ++i) {
        if (taker(T, i) == 0xff) continue;
        const int k = findTag(taker(T, i), kSlots, tags);
        if (k < 0) setTaker(T, i, 0xff);
        else wB[k] = -2;
    }
    const std::uint8_t* slots = slotRec(w, t, T.formIdx(), 0);
    for (int j = 0; j < 9; ++j) {
        if (taker(T, j) != 0xff) continue;
        switch (j) {
        case 0: setTaker(T, j, takerByOrder(slots, kTakerOrder0, blocked)); break;
        case 1: setTaker(T, j, takerByOrder(slots, kTakerOrder1, blocked)); break;
        case 2: case 3: setTaker(T, j, get16(slots + argMax(kSlots, wA) * 6, 4)); break;
        default: {
            const int k = argMax(kSlots, wB);
            if (wB[k] > -2) setTaker(T, j, get16(slots + k * 6, 4));
            wB[k] = -2;
            break;
        }
        }
    }
    // 0x4A0C5C: Team_playerBySquad(opponent, 0xFF) looks for a squad index 0xFF, which no player has
    // (indices < 22), so the man-marking pair is always cleared.
    T.f1e8 = -1;
    T.f1ec = -1;
}

// 0x4A08FC (EAX = team+0x0C as the source block, EDX = team; called on its own block by 0x49EBCC):
// re-select every record, re-assign the players per record, restore the active record, validate
// the taker tags.
void Team_reapplyRecords_4A08FC(World& w, int t) {
    TeamState& T = w.teams[t];
    const int saved = T.formIdx();
    for (int r = 0; r < 3; ++r) {
        Formation_selectTeamFormation(w, t, r);
        T.formIdx() = static_cast<std::uint8_t>(r);
        Formation_applyDescriptor(w, t, T.rec(r) + 1);
        Team_humanAfterSendOff(w, t, 0);
    }
    Team_setFormation(w, t, saved);
    for (int k = 0; k < 9; ++k)
        if (slotOfSquad(w, t, taker(T, k)) < 0) setTaker(T, k, 0xff);
    Team_allocateTakers_4A0AA0(w, t);
}

} // namespace wc98::sim
