// Team AI decision routines called from Team_updateTactics 0x4536A8 (0x4D2644..0x4D43FF,
// 0x4DA190..0x4DA6B1) and the team-side helpers they use. Ported from the FIFA99.EXE v1.1
// disassembly (decode notes: SimTeamAI handoff). Mark fields hold pid + 1 (0 = none).
#include "wc98/sim/world.hpp"

namespace wc98::sim {

namespace {

constexpr std::int8_t kSupportLimit[5] = {5, 4, 4, 3, 2};                                       // exe 0x58b5d8
constexpr std::int32_t kMarkSlot1[17] = {17, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2};  // exe 0x58b550
constexpr std::int32_t kMarkSlot2[17] = {17, 14, 15, 14, 14, 14, 13, 10, 9, 9, 9, 8, 5, 4, 4, 4, 3};   // exe 0x58b594
constexpr std::int32_t kLineRows[25] = {4, 0, 0, 0, 0, 3, 5, 0, 0, 0, 3, 4, 5,
                                        0, 0, 2, 3, 5, 6, 0, 2, 3, 4, 5, 6};                    // exe 0x58b5e0
constexpr std::int32_t kPushDist[5] = {9600, 9600, 288, 576, 9600};                             // exe 0x58b644
constexpr std::int32_t kBoxDepth[5] = {144, 144, 192, 240, 240};                                // exe 0x58b658
constexpr std::int32_t kGaps[12] = {0, 1, 4, 13, 40, 121, 364, 1093, 3280, 9841, 29524, 88573}; // exe 0x582e50

int s01(std::int32_t z) { return z >= 0 ? 1 : 0; }
TeamState& teamOf(World& w, int pid) { return w.teams[w.players[pid].team]; }
bool injured(const World& w, int pid) { return Player_attr(w, pid, 0x40) & 1; }
std::int32_t octV(const Vec3& a, const Vec3& b) { return octLength(a.x - b.x, a.z - b.z); }
bool skipTurn(const World& w, int team) { return (w.time.teamOrder ^ team) != 0; }
const std::uint8_t* selDesc(const World& w, int t) { return w.teams[t].rec(w.formation.selRec[t]) + 1; }

bool beatsDefender(World& w, int pid) { return Team_beatsDefender_4DBA34(w, pid, nullptr); }

// 0x4D25B0 (EAX = v, EDX = dx, EBX = limX, ECX = limZ, stack = z threshold).
void spotShift(Vec3& v, std::int32_t dx, std::int32_t limX, std::int32_t limZ, std::int32_t zthr) {
    v.x += dx;
    if (limX < v.x) {
        std::int32_t e = ((v.x - limX) * 21) >> 3;
        if (e > dx) e = dx;
        v.z = v.z > zthr ? v.z - e : v.z + e;
        v.x = limX;
    } else {
        const std::int32_t t = (dx >> 2) + v.z;
        if (t < limZ) v.z = t;
    }
}

// 0x4D2614 (EAX = v, EDX = r, EBX = add): step toward the attacked goal mouth.
void spotTowardGoal(const World& w, Vec3& v, std::int32_t r, std::int32_t add) {
    const Angle a = atan2i(-v.x, w.pitch.halfLength - v.z - 0x150);
    const Vec3 o = offsetInDirection(v, r + add, a);
    v.x = o.x;
    v.z = o.z;
}

// Zone predicates on an opponent p (0x4D3800, 0x4D386C, 0x4D38C0, 0x4D3968); "opp" = p's opponent.
bool pred3800(const World& w, int pid) {
    const PlayerState& p = w.players[pid];
    const TeamState& t = w.teams[p.team];
    if (w.teams[t.opponent].mentality != 0) return false;
    const Vec3& tg = Motion_targetVec(w, pid);
    const std::int32_t q = w.pitch.quarterLength - 0x30;
    if (t.side == 1) return q < p.posI.z && w.pitch.penAreaLineZ < tg.z;
    return -q > p.posI.z && -w.pitch.penAreaLineZ > tg.z;
}
bool pred386C(const World& w, int pid) {
    const PlayerState& p = w.players[pid];
    const TeamState& t = w.teams[p.team];
    if (w.teams[t.opponent].mentality != 0) return true;
    const std::int32_t q = w.pitch.thirdLineZ[1] - 0x30;
    if (t.side == 1) return q > p.posI.z;
    return -q < p.posI.z;
}
bool pred38C0(const World& w, int pid) {
    const PlayerState& p = w.players[pid];
    const TeamState& t = w.teams[p.team];
    const Vec3& tg = Motion_targetVec(w, pid);
    const std::int32_t c = w.pitch.penAreaHalfWidth + 0x90;
    if (iabs(p.posI.x) > c || iabs(tg.x) > c) return false;
    const std::int32_t b = w.pitch.penAreaLineZ - (kBoxDepth[w.teams[t.opponent].mentality] + 0xf0);
    if (t.side == 1) return -b > p.posI.z && -b > tg.z;
    return b < p.posI.z && b < tg.z;
}
bool pred3968(const World& w, int pid) {
    const PlayerState& p = w.players[pid];
    const TeamState& t = w.teams[p.team];
    const std::int32_t c = w.pitch.penAreaHalfWidth + 0xc0;
    if (iabs(p.posI.x) > c) return true;
    const std::int32_t b = w.pitch.penAreaLineZ - (kBoxDepth[w.teams[t.opponent].mentality] + 0x120);
    if (t.side == 1) return -b < p.posI.z;
    return b > p.posI.z;
}

// 0x4DA1DC (EAX = team, EDX = target): send up to two midfielders/forwards to press the target.
void pressTarget(World& w, int team, int tgt) {
    TeamState& t = w.teams[team];
    t.w37c[14] = 0; // +0x3B4
    Team_clearPressFlags(w, team);
    t.w37c[12] = tgt; // +0x3AC
    t.w37c[13] = -1;  // +0x3B0
    const int n = t.playerCount;
    std::int32_t keys[11], idx[11];
    for (int i = 0; i < n; ++i) {
        keys[i] = -Player_cachedDistance(w, t.players[i], tgt);
        idx[i] = i;
    }
    Sort_shellDesc(n, keys, idx);
    Angle ang[2] = {};
    int cnt = 0;
    for (int k = 0; cnt < 2 && k < n; ++k) {
        const int pid = t.players[idx[k]];
        PlayerState& p = w.players[pid];
        if (p.slot == 0) continue;
        if (p.role != 4 && p.role != 3) continue;
        bool ok = true;
        for (int j = 0; j < cnt && ok; ++j) {
            std::int32_t d = (ang[j] - Player_cachedAngle(w, pid, tgt)) & 0x3ff;
            if (d > 0x200) d = 0x400 - d;
            if (d < 0x72) ok = false;
        }
        if (!ok || p.ctrl != 0) continue;
        if (!((static_cast<std::uint32_t>(p.state) >> 7) == 7 || (p.flags22c & 0x200))) continue;
        p.flags22c |= 0x200000;
        Player_setState(w, pid, 0x383, 0, 0);
        ang[cnt++] = Player_cachedAngle(w, pid, tgt);
        ++t.w37c[14];
    }
}

} // namespace

// 0x4DBA34 (EAX player, EDX vector or null): the possessor (or a free-ball player) in his attacking half
// nearer the opponents' goal than their nearest defender. Past the half check the exe writes
// {-x, -goalLineZ[side] - z} into the EDX vector at +4/+8.
bool Team_beatsDefender_4DBA34(World& w, int pid, std::int32_t* vec) {
    const PlayerState& p = w.players[pid];
    const int side = w.teams[p.team].side;
    if ((w.ball.possessor >= 0 && pid != w.ball.possessor) || s01(p.posI.z) == side) return false;
    if (vec != nullptr) {
        vec[1] = -p.posI.x;
        vec[2] = -w.pitch.goalLineZ[side] - p.posI.z;
    }
    const int o = w.teams[p.team].opponent;
    const Vec3 goal{0, 0, w.pitch.goalLineZ[w.teams[o].side]}; // 0x65F418 + side*12
    std::int32_t d = 0;
    Team_nearestPlayer_4523E4(w, goal, o, 0, &d);
    return octV(p.posI, goal) <= d;
}

// 0x481560 (EAX = n, EDX = keys, EBX = parallel index array): shell sort, descending by key.
void Sort_shellDesc(int n, std::int32_t* keys, std::int32_t* idx) {
    int k = 1;
    while (kGaps[k + 1] < n) ++k;
    std::int32_t gap;
    do {
        gap = kGaps[k--];
        if (gap >= n) continue;
        for (int i = gap; i < n; ++i) {
            const std::int32_t tk = keys[i], ti = idx[i];
            int j = i - gap;
            while (j >= 0 && tk > keys[j]) {
                keys[j + gap] = keys[j];
                idx[j + gap] = idx[j];
                j -= gap;
            }
            keys[j + gap] = tk;
            idx[j + gap] = ti;
        }
    } while (gap > 1);
}

// 0x4D3734 (EAX = marker, EDX = marked).
void Team_mark(World& w, int a, int b) {
    if (a < 0 || b < 0) return;
    w.players[a].f258 = 0x78;
    w.players[a].markTarget = b + 1;
    w.players[b].markedBy = a + 1;
    ++teamOf(w, a).f354;
}

// 0x4D3760 (EAX = marked player). The counter decrement hits the marked player's team (exe quirk).
void Team_unmark(World& w, int x) {
    if (x < 0) return;
    PlayerState& p = w.players[x];
    if (p.markedBy) {
        p.f258 = 0;
        w.players[p.markedBy - 1].markTarget = 0;
        p.markedBy = 0;
    }
    TeamState& t = teamOf(w, x);
    if (static_cast<std::uint32_t>(t.f354) > 0) --t.f354;
}

// 0x4DA190.
void Team_clearPressFlags(World& w, int team) {
    const TeamState& t = w.teams[team];
    for (int i = 0; i < t.playerCount; ++i) w.players[t.players[i]].flags22c &= ~0x200000u;
}

// 0x4DABC8 (IGT action, called from 0x4E3272): team mode 4, press the opponents' ball winner.
void Team_startPress(World& w, int team) {
    TeamState& t = w.teams[team];
    if (static_cast<std::uint32_t>(t.playerCount) < 3) return;
    if (static_cast<std::uint32_t>(w.teams[t.opponent].playerCount) < 2) return;
    t.f378 = 4;
    t.w37c[0] = 0;  // +0x37C
    t.w37c[15] = 0; // +0x3B8
    t.w37c[14] = 0; // +0x3B4
    pressTarget(w, team, w.teams[t.opponent].possWinner);
}

// 0x4DA670 (IGT action 70): arm the offside trap.
void Team_armOffsideTrap(World& w, int team) {
    TeamState& t = w.teams[team];
    if (static_cast<std::uint32_t>(t.playerCount) < 5) return;
    if (static_cast<std::uint32_t>(w.teams[t.opponent].playerCount) < 2) return;
    t.f378 = 1;
    t.w37c[2] = 0;  // +0x384
    t.w37c[0] = 0;  // +0x37C
    t.w37c[15] = 0; // +0x3B8
}

// 0x452650 (EAX = pos, EDX = team, EBX = excluded slot).
int Team_nearestPlayer_452650(const World& w, const Vec3& pos, int team, int exclSlot) {
    const TeamState& t = w.teams[team];
    std::uint32_t best = 0x7fff8000;
    int r = -1;
    for (int i = 0; i < t.playerCount; ++i) {
        const int pid = t.players[i];
        const PlayerState& p = w.players[pid];
        if ((p.flags22c & 2) || p.curSlot == exclSlot || (p.flags22c & 1)) continue;
        const std::uint32_t d = static_cast<std::uint32_t>(octV(pos, p.posI));
        if (s01(p.posI.z - w.ball.posI.z) != t.side && d > 0x60) continue;
        if (d < best) {
            best = d;
            r = pid;
        }
    }
    return r;
}

// 0x4D37AC.
void Team_onPossessionChange(World& w, int team) {
    TeamState& t = w.teams[team];
    for (int i = 0; i < t.playerCount; ++i) {
        PlayerState& p = w.players[t.players[i]];
        if (p.markTarget) {
            w.players[p.markTarget - 1].markedBy = 0;
            p.markTarget = 0;
        }
    }
    t.f354 = 0;
}

// 0x4D3DBC.
int Team_decidePossession(World& w, int team) {
    TeamState& T = w.teams[team];
    TeamState& O = w.teams[T.opponent];
    T.flags &= ~0x100u;
    if (O.f1cc < 0 || T.f1cc < 0) return 0;
    const BallState& B = w.ball;
    const std::int32_t dlz = B.landing.z - B.posI.z;
    const std::int32_t d2 = 2 * octLength(B.landing.x - B.posI.x, dlz);
    if (s01(dlz) == T.side && d2 > w.pitch.halfLength) {
        T.flags |= 0x100;
        return 0;
    }
    const bool inPlay = w.match.phase == 9;
    if (!inPlay) {
        T.f1b4 = -1;
        T.flags &= ~0x200u;
    }
    const int P = B.possessor;
    if (P >= 0) {
        const PlayerState& pp = w.players[P];
        const TeamState& PT = w.teams[pp.team];
        if (inPlay) {
            if (pp.team == team) {
                O.f1b4 = -1;
                O.flags &= ~0x200u;
                if (pp.ctrl != 0) {
                    if (s01(pp.posI.z + w.pitch.thirdLineZ[PT.side]) != PT.side &&
                        iabs(pp.posI.x) > w.pitch.penAreaHalfWidth) {
                        T.flags |= 0x200;
                        return 1;
                    }
                    if (O.f1bc < 0) return 1;
                    if (s01(pp.posI.z) != PT.side &&
                        iabs(pp.posI.z) - iabs(w.players[O.f1bc].posI.z) > 0x60 && beatsDefender(w, P)) {
                        T.f1b4 = P;
                        return 1;
                    }
                }
            }
            if (O.f1b4 >= 0) return 0;
            if (T.flags & 0x200) return 1;
        }
        T.f1b4 = -1;
        const std::int32_t db = w.players[O.f1cc].distBall;
        if (s01(pp.posI.z) == PT.side && Pitch_inPenaltyArea(w, pp.posI) && db < 0xc0) return 0;
        if (s01(pp.posI.z - w.pitch.thirdLineZ[PT.side]) == PT.side && db < 0x60) return 0;
        return pp.team == team ? 1 : 0;
    }
    // No possessor.
    T.flags &= ~0x200u;
    O.f1b4 = -1;
    const int A = T.possWinner, Bw = O.possWinner;
    const std::int32_t e = octV(B.landing, w.players[Bw].posI) - octV(B.landing, w.players[A].posI);
    const std::int32_t lz = B.landing.z;
    const int aSide = w.teams[w.players[A].team].side, bSide = w.teams[w.players[Bw].team].side;
    if (iabs(B.landing.x) > w.pitch.penAreaHalfWidth) {
        if (s01(w.pitch.thirdLineZ[bSide] + lz) != bSide && e > -0xa0) O.flags |= 0x200;
        if (s01(lz + w.pitch.thirdLineZ[aSide]) != aSide && e < 0xa0) T.flags |= 0x200;
    } else {
        std::int32_t h = B.apexHeight;
        if (h <= B.posI.y) h = B.posI.y;
        if (h > 0x72 && Pitch_inPenaltyArea(w, B.landing) && iabs(B.vel.x >> 1) > iabs(B.vel.z)) {
            if (s01(lz) == T.side) O.flags |= 0x200;
            else T.flags |= 0x200;
        }
    }
    const int Q = B.prevPossessor;
    if (Q >= 0) {
        const TeamState& QT = teamOf(w, Q);
        const int R = QT.f1e0;
        if (w.players[Q].team == team && R >= 0) {
            const int L = w.teams[QT.opponent].f1c0;
            if (L < 0) return 1;
            const PlayerState& r = w.players[R];
            if (s01(r.posI.z) == w.teams[r.team].side && Pitch_inPenaltyArea(w, r.posI)) {
                const PlayerState& l = w.players[L];
                const int ls = w.teams[l.team].side;
                return s01(l.posI.z + w.pitch.thirdLineZ[ls]) == ls ? 1 : 0;
            }
            return 1;
        }
        if (w.players[Q].team != team && R >= 0) return 0;
    }
    if (inPlay && e < 0x60 && Bw == O.f1c0 && beatsDefender(w, Bw)) {
        O.f1b4 = Bw;
        return 0;
    }
    const int sd = w.teams[w.players[T.players[0]].team].side;
    if (s01(lz - w.pitch.thirdLineZ[sd]) == sd) return e > 0x60 ? 1 : 0;
    if (s01(lz + w.pitch.thirdLineZ[sd]) == sd && e < 0) return 0;
    return 1;
}

// 0x4D2644: support roles (player +0x14 / +0x214) and the four support spots at team+0x310.
void Team_attackAi_4D2644(World& w, int team) {
    if (skipTurn(w, team)) return;
    TeamState& T = w.teams[team];
    const int C = w.ball.possessor >= 0 ? w.ball.possessor : T.f1e0;
    const int lim = kSupportLimit[T.mentality];
    std::int32_t nd = T.line358, nm = T.line35c;
    int k = 0;
    for (int i = 0; i < T.playerCount; ++i) {
        const int pid = T.order[i];
        PlayerState& p = w.players[pid];
        if (pid == C) {
            p.f014 = 6;
        } else if (p.role == 4) {
            p.f014 = 6;
            p.markSpot = k + 1;
            ++k;
        } else {
            p.f014 = 4;
            if (k < lim) {
                if ((p.role == 1 || p.role == 2) && nd >= 1) {
                    --nd;
                    p.f014 = 6;
                    p.markSpot = k + 1;
                    ++k;
                } else if (p.role == 3 && nm >= 1) {
                    --nm;
                    p.f014 = 6;
                    p.markSpot = k + 1;
                    ++k;
                }
            }
        }
    }
    if (C < 0) return;
    const PlayerState& c = w.players[C];
    const int cs = teamOf(w, C).side;
    const std::int32_t wd = (iabs(c.posI.x) > w.pitch.penAreaHalfWidth &&
                             s01(c.posI.z + w.pitch.thirdLineZ[cs]) != cs)
                                ? w.pitch.quarterWidth
                                : 0;
    Vec3 A = c.posI, Bv = c.posI, Cc = c.posI, D = c.posI;
    const std::int32_t h = w.pitch.thirdWidth >> 1, lx = w.pitch.halfWidth - 0xf0;
    const std::int32_t lz = w.pitch.halfLength - w.pitch.thirdLength, zt = w.pitch.penAreaLineZ;
    const std::int32_t r = w.pitch.halfLength >> 2;
    if (T.side == 1) {
        D.z = -D.z;
        spotShift(D, h, lx, lz, zt);
        D.z = -D.z;
        Cc.x = -Cc.x;
        Cc.z = -Cc.z;
        spotShift(Cc, h, lx, lz, zt);
        Cc.x = -Cc.x;
        Cc.z = -Cc.z;
        Bv.z = -Bv.z;
        spotTowardGoal(w, Bv, r, wd);
        Bv.z = -Bv.z;
        spotTowardGoal(w, A, r, 0);
    } else {
        D.x = -D.x;
        spotShift(D, h, lx, lz, zt);
        D.x = -D.x;
        spotShift(Cc, h, lx, lz, zt);
        spotTowardGoal(w, Bv, r, wd);
        A.z = -A.z;
        spotTowardGoal(w, A, r, 0);
        A.z = -A.z;
    }
    const Vec3* spots[4] = {&Bv, &D, &Cc, &A}; // team +0x310 / +0x31C / +0x328 / +0x334
    for (int s = 0; s < 4; ++s) {
        T.w310[s * 3] = spots[s]->x;
        T.w310[s * 3 + 1] = spots[s]->y;
        T.w310[s * 3 + 2] = spots[s]->z;
    }
    for (int s = 0; s < 4; ++s) {
        const int l = T.links[c.curSlot * 4 + s];
        if (l == 0xff) continue;
        if (l == c.index) {
            if (T.f1f0 >= 0) w.players[T.f1f0].f014 = s;
        } else {
            w.players[T.players[l]].f014 = s;
        }
    }
}

// 0x4D2A44: defensive line z (team +0x214 / +0x218) and marking assignment (player +0x254). The
// assignment is skipped while the team has the ball (always the case from Team_updateTactics).
void Team_attackAi_4D2A44(World& w, int team) {
    if (skipTurn(w, team)) return;
    TeamState& T = w.teams[team];
    const TeamState& O = w.teams[T.opponent];
    std::int32_t flag[18] = {}, map[18];
    for (std::int32_t& m : map) m = 0xb;
    int nd = 0, nm = 0;
    std::int32_t ds = 0, ms = 0;
    for (int i = 0; i < T.playerCount && (nd < 2 || nm < 2); ++i) {
        const int pid = T.players[i];
        const PlayerState& p = w.players[pid];
        if (p.state != 4 || injured(w, pid) || (p.flags22c & 0x6000)) continue;
        if (p.role == 2 || p.curSlot == 1) {
            if (nd < 2) {
                ds += p.posI.z;
                ++nd;
            }
        } else if (p.role == 3 && nm < 2) {
            ms += p.posI.z;
            ++nm;
        }
    }
    ds >>= nd >> 1;
    ms >>= nm >> 1;
    const std::int32_t hl = w.pitch.halfLength;
    if (T.side == 1) {
        if (nd == 0) ds = -hl;
        if (nm == 0) ms = -hl;
        if (ms > ds) ds = ms;
        ds -= 0x1e0;
        ms -= 0x2d0;
    } else {
        if (nd == 0) ds = hl;
        if (nm == 0) ms = hl;
        if (ms < ds) ds = ms;
        ds += 0x1e0;
        ms += 0x2d0;
    }
    T.line218 = ms;
    T.line214 = ds;
    if (T.flags & 2) return;
    std::uint32_t c1 = 0, c2 = 0;
    for (int i = 0; i < O.playerCount; ++i) {
        const int q = O.players[i];
        const PlayerState& p = w.players[q];
        if ((p.flags22c & 1) || injured(w, q) || q == w.ball.possessor) continue;
        if (!(p.flags22c & 0x2800)) {
            map[p.slotCopy] = p.index;
            flag[p.slotCopy] = 1;
        }
        const std::int32_t z = Motion_targetVec(w, q).z;
        if (T.side == 1) {
            if (ms > z) continue;
            if (z < ds) ++c2;
            else ++c1;
        } else {
            if (ms < z) continue;
            if (z > ds) ++c2;
            else ++c1;
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        const std::int32_t* tab = pass == 0 ? kMarkSlot1 : kMarkSlot2;
        for (int i = 0; i < T.playerCount; ++i) {
            PlayerState& p = w.players[T.players[i]];
            if (pass == 0) p.f254 = 0;
            if (p.state != 4 || (pass == 1 && p.f254 != 0)) continue;
            bool ok;
            if (p.role == 2 || p.curSlot == 1) ok = c1 <= static_cast<std::uint32_t>(T.line358);
            else ok = p.role == 3 && c2 <= static_cast<std::uint32_t>(T.line35c);
            if (!ok) continue;
            const int j = tab[p.slotCopy];
            if (flag[j]) {
                flag[j] = 0;
                p.f254 = O.players[map[j]] + 1;
            }
        }
    }
}

// 0x4D2F24: slot swap between possWinner and the nearest team-mate of the target line; push-ups.
void Team_attackAi_4D2F24(World& w, int team) {
    if (skipTurn(w, team)) return;
    TeamState& T = w.teams[team];
    T.f1f0 = -1;
    const std::uint8_t* d = selDesc(w, team);
    const std::int32_t defN = d[1] + d[2], midN = d[3];
    T.line35c = midN;
    T.line358 = defN;
    std::int32_t edi = defN, ebp = midN;
    const int W = T.possWinner;
    PlayerState& pw = w.players[W];
    pw.curSlot = pw.slotCopy = pw.slot;
    pw.role = pw.baseRole;
    int rf = -1, best = -1;
    std::int32_t minD = 0;
    Vec3 o;
    if (w.ball.possessor == W &&
        (pw.slot != 0 ||
         (W != T.f1e4 && s01(pw.posI.z - ((w.pitch.goalLineZ[T.side] + T.line214) >> 1)) != T.side))) {
        if (!(pw.role == 4 && static_cast<std::uint32_t>(T.mentality) > 2)) {
            rf = 3;
            Formation_attackParticipation(w, pw.posCb, W, o, -1, 1);
            minD = octV(pw.posI, o);
            if (s01(pw.posI.z - T.line214) == T.side) rf = 2;
        }
    }
    for (int i = 0; i < T.playerCount; ++i) {
        const int pid = T.players[i];
        PlayerState& p = w.players[pid];
        p.curSlot = p.slotCopy = p.slot;
        p.role = p.baseRole;
        p.flags22c &= ~0x6800u;
        if (p.role == rf && p.role != 4 && w.match.phase == 9) {
            Formation_attackParticipation(w, p.posCb, pid, o, -1, 1);
            const std::int32_t dd = octV(pw.posI, o);
            if (dd < minD) {
                minD = dd;
                best = pid;
            }
        }
    }
    if (best >= 0) {
        PlayerState& b = w.players[best];
        b.curSlot = b.slotCopy = pw.slot;
        pw.curSlot = pw.slotCopy = b.slot;
        b.role = pw.baseRole;
        pw.role = b.baseRole;
        b.flags22c |= 0x6000;
        T.f1f0 = best;
    }
    if (w.match.phase == 9) {
        for (int i = 0; i < T.playerCount; ++i) {
            const int pid = T.players[i];
            PlayerState& p = w.players[pid];
            if (p.role == 4) continue;
            if (p.ctrl == 0) {
                if (pid == T.possWinner || pid == T.f1e8 || (p.flags22c & 1) || injured(w, pid) ||
                    p.state == 0x309 || p.state == 0x280 || Player_isState100Kind1(w, pid) ||
                    Player_isState100Kind0(w, pid) || p.state == 0x81 || p.markTarget ||
                    !(p.flags22c & 0x2000) || p.f014 < 4)
                    continue;
            }
            Formation_attackParticipation(w, p.posCb, pid, o, -1, 1);
            const std::uint32_t dist =
                static_cast<std::uint32_t>(iabs(o.z - p.posI.z) + (iabs(o.x - p.posI.x) >> 1));
            if (dist > static_cast<std::uint32_t>(kPushDist[p.role])) {
                if (p.role == 2 && edi > 1) {
                    p.flags22c |= 0x800;
                    --edi;
                } else if (p.role == 3 && ebp > 1) {
                    p.flags22c |= 0x800;
                    --ebp;
                }
            }
        }
    }
    if (edi < defN || ebp < midN) {
        std::int32_t A[17];
        for (std::int32_t& a : A) a = -1;
        for (int i = 0; i < T.playerCount; ++i) {
            const PlayerState& p = w.players[T.players[i]];
            if (!(p.flags22c & 0x800) && (p.curSlot == 1 || p.role == 2 || p.role == 3)) A[p.curSlot] = i;
        }
        if (edi > 0 && edi < defN) {
            const std::int32_t* row = kLineRows + (edi - 1) * 5;
            int j = 0;
            for (int s = 1; s <= 6; ++s)
                if (A[s] >= 0) w.players[T.players[A[s]]].slotCopy = row[j++];
        }
        if (ebp > 0 && ebp < midN) {
            const std::int32_t* row = kLineRows + (ebp - 1) * 5;
            int j = 0;
            for (int s = 7; s <= 11; ++s)
                if (A[s] >= 0) w.players[T.players[A[s]]].slotCopy = row[j++] + 5;
        }
    }
    T.line358 = edi;
    T.line35c = ebp;
}

// 0x4D35AC: players ahead of / behind the ball (team +0x360/+0x364) and free defenders (+0x368).
void Team_attackAi_4D35AC(World& w, int team) {
    if (skipTurn(w, team)) return;
    TeamState& T = w.teams[team];
    T.w360[0] = T.w360[1] = T.w360[2] = 0;
    const std::int32_t bz = w.ball.posI.z;
    const bool s1 = T.side == 1;
    const std::int32_t hi = s1 ? bz + 0x60 : bz - 0x60, lo = s1 ? bz - 0x30 : bz + 0x30;
    for (int i = 0; i < T.playerCount; ++i) {
        const int pid = T.players[i];
        const PlayerState& p = w.players[pid];
        if (p.slot == 0 || (p.flags22c & 1) || injured(w, pid)) continue;
        const std::int32_t z = p.posI.z;
        if (s1 ? lo < z : lo > z) {
            ++T.w360[0];
            if ((p.role == 2 || p.curSlot == 1) && !p.markTarget && pid != T.possWinner && !(p.flags22c & 1))
                ++T.w360[2];
        }
        if (s1 ? hi > z : hi < z) ++T.w360[1];
    }
}

// 0x4D39E4: man marking while defending in play.
void Team_defendAi_4D39E4(World& w, int team) {
    if (skipTurn(w, team)) return;
    TeamState& T = w.teams[team];
    TeamState& O = w.teams[T.opponent];
    const std::uint8_t* d = selDesc(w, team);
    const std::int32_t defN = d[1] + d[2];
    std::int32_t cnt = 0;
    const std::int32_t fr = w.time.simFrames;
    for (int i = 0; i < T.playerCount; ++i) {
        const int pid = T.players[i];
        PlayerState& p = w.players[pid];
        if (p.f258 > 0) p.f258 -= fr;
        p.flags22c &= ~2u;
        if (!p.markTarget) continue;
        if (T.ctrl1f4 == 0) {
            Team_unmark(w, p.markTarget - 1);
            continue;
        }
        if (p.role == 2 || p.curSlot == 1) ++cnt;
        if (p.f258 > 0 || !(p.flags22c & 0x800) || p.markTarget - 1 == O.f1c0 || (p.state != 4 && p.state != 0))
            p.flags22c |= 2;
    }
    if (T.ctrl1f4 == 0) return;
    if (cnt == defN) {
        const Vec3 spot{0, 0, w.pitch.penSpot[T.side]}; // 0x65F398 + side*12
        std::int32_t dd = 0;
        const int q = Team_nearestPlayer_4523E4(w, spot, team, 0, &dd);
        if (q >= 0 && dd > 0xf0) {
            if (w.players[q].markTarget) Team_unmark(w, w.players[q].markTarget - 1);
            w.players[q].flags22c |= 2;
        }
    }
    if (T.f1e8 >= 0) w.players[T.f1e8].flags22c |= 2;
    if (T.possWinner >= 0) {
        PlayerState& pw = w.players[T.possWinner];
        pw.flags22c |= 2;
        if (pw.markTarget) Team_unmark(w, pw.markTarget - 1);
    }
    if (O.possWinner >= 0 && w.players[O.possWinner].markedBy) Team_unmark(w, O.possWinner);
    const int n = O.playerCount;
    std::int32_t keys[11], idx[11];
    for (int i = 0; i < n; ++i) {
        keys[i] = -w.players[O.players[i]].distBall;
        idx[i] = i;
    }
    Sort_shellDesc(n, keys, idx);
    for (int k = 0; k < n; ++k) {
        const int q = O.players[idx[k]];
        const PlayerState& p = w.players[q];
        if (p.slot == 0 || (p.flags22c & 1) || injured(w, q) || q == O.possWinner || q == w.ball.possessor ||
            p.state == 0x309 || p.state == 0x183 || q == T.f1ec)
            continue;
        if (!p.markedBy && (pred38C0(w, q) || pred3800(w, q))) {
            const Vec3& tg = Motion_targetVec(w, q);
            int c = Team_nearestPlayer_452650(w, tg, team, 0);
            if (c < 0) c = Team_nearestPlayer_4523E4(w, tg, team, 0, nullptr);
            if (c >= 0) {
                if (w.players[c].markTarget) Team_unmark(w, w.players[c].markTarget - 1);
                w.players[c].flags22c |= 2;
                Team_mark(w, c, q);
            }
        } else if (p.markedBy && pred3968(w, q) && pred386C(w, q)) {
            Team_unmark(w, q);
        }
    }
    if (T.f1e8 >= 0) {
        const int m = w.players[T.f1e8].markTarget - 1;
        if (m >= 0 && m != T.f1ec) {
            Team_unmark(w, m);
            Team_unmark(w, T.f1ec);
        }
        if (w.players[T.f1e8].markTarget == 0) Team_mark(w, T.f1e8, T.f1ec);
    }
    for (int i = 0; i < T.playerCount; ++i) w.players[T.players[i]].flags22c &= ~2u;
}

// 0x4DA3C4: team+0x378 mode machine (jump table 0x4DA3B4): 1 offside trap, 2/3 wait for a dead
// ball, 4 press. w37c: [0] +0x37C timer, [1] +0x380 trap line, [2] +0x384 runners, [12] +0x3AC press
// target, [13] +0x3B0 press player, [14] +0x3B4 pressers, [15] +0x3B8 flags.
void Team_ai_4DA3C4(World& w, int team) {
    TeamState& T = w.teams[team];
    T.w37c[0] += w.time.simFrames;
    const TeamState& O = w.teams[T.opponent];
    const int P0 = w.ball.possessor;
    switch (T.f378) {
    case 1: {
        if (w.match.phase != 9) break;
        if (P0 >= 0 && w.players[P0].team == team) break;
        if (T.w37c[0] > 0x168) break;
        const std::int32_t lz = w.players[O.f1c0].posI.z;
        std::int32_t bz = w.ball.posI.z;
        const std::int32_t lnd = w.ball.landing.z;
        if (T.side == 1) bz = bz > lnd ? bz : lnd;
        else bz = bz < lnd ? bz : lnd;
        std::int32_t e = iabs(bz - lz);
        if (e < 0xf0) {
            T.w37c[15] |= 1;
            return;
        }
        e -= 0xf0;
        if (e > 0xf0) e = 0xf0;
        std::int32_t line;
        if (T.side == 1) line = lz - e > 0 ? lz - e : 0;
        else line = lz + e >= 0 ? 0 : lz + e;
        T.w37c[1] = line;
        std::int32_t n = T.w37c[2];
        for (int i = 0; i < T.playerCount; ++i) {
            const int pid = T.players[i];
            const PlayerState& p = w.players[pid];
            if ((p.flags22c & 0x200000) || p.ctrl != 0 || p.slot == 0) continue;
            if (T.side == 1 ? p.posI.z < line : p.posI.z > line) continue;
            if (n >= 6) continue;
            Player_setState(w, pid, 0x380, 0, 0);
            ++n;
        }
        T.w37c[2] = n;
        T.w37c[15] &= 0xfffe;
        return;
    }
    case 2:
    case 3:
        if (w.match.phase == 9) return;
        break;
    case 4: {
        const bool nopl = w.match.phase != 9;
        bool flag = false;
        int P = P0;
        bool skipPress = false;
        if (P < 0) {
            P = O.possWinner;
            if (P < 0) {
                T.w37c[12] = -1;
                flag = true;
                skipPress = true;
            }
        } else if (w.players[P].team == team) {
            break;
        }
        if (!skipPress && P != T.w37c[12]) pressTarget(w, team, P);
        if (nopl) break;
        if (T.w37c[14] < 2 && static_cast<std::uint32_t>(T.playerCount) > 2) pressTarget(w, team, O.possWinner);
        const int pp = T.w37c[13];
        if (pp >= 0 && pp != T.possWinner) Player_setState(w, pp, 0x383, 0, 0);
        if (flag) T.w37c[15] |= 1;
        else T.w37c[15] &= 0xfffe;
        return;
    }
    default:
        return;
    }
    Team_clearPressFlags(w, team);
    T.f378 = 0;
}

// 0x4BD308 (from Team_updateTactics when player +0x22C bit 9 is set and the countdown expired).
void Player_resetIdle_4BD308(World& w, int pid) {
    PlayerState& p = w.players[pid];
    TeamState& T = w.teams[p.team];
    const int P = w.ball.possessor;
    int st = p.state;
    auto finish = [&](int s) {
        if (w.match.phase == 0) s = 4;
        if (s != p.state) Player_setState(w, pid, s, 0, 0);
    };
    auto toward = [&](int s) { // 0x4BD398
        if (p.state == 0x280 && s != 0x280) {
            s = 1;
            Player_setState(w, pid, 1, 0, 0);
        }
        finish(s);
    };
    if (P >= 0) {
        if (w.players[P].team == p.team) {
            if (pid == P) {
                T.possWinner = P;
                return;
            }
            finish(pid == T.f1e0 ? 0x30b : 4);
            return;
        }
        if (w.match.phase != 9) {
            finish(st);
            return;
        }
        if ((w.ball.flags & 0x20) && w.players[P].slot == 0) {
            toward(4);
            return;
        }
        const int c = T.f1d0;
        if (pid == c) {
            if (w.players[c].state != 0x280) {
                st = 0x280;
                Team_unmark(w, pid);
                toward(st);
                return;
            }
        } else if (c >= 0 && p.state == 0x280 && w.players[c].distBall + 0x40 < p.distBall) {
            toward(4);
            return;
        }
        if (st == 0x280) Team_unmark(w, pid);
        toward(st);
        return;
    }
    if (w.match.phase != 9) {
        finish(p.ctrl == 0 ? 4 : st);
        return;
    }
    const int q = w.match.player2a0;
    if (q >= 0 && w.players[q].team == p.team) {
        finish(pid == q ? 0x309 : 4);
        return;
    }
    if (pid != T.f1cc || p.state == 0x303) {
        finish(4);
        return;
    }
    if (q != T.f1e4 || q < 0) finish(0x309);
    else finish(4);
}

} // namespace wc98::sim
