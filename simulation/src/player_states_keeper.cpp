// Goalkeeper states 0x200-0x22F and their helpers 0x53B04C..0x53C7E0 — owner: SimPorter2.
// Ported from FIFA99.EXE v1.1 0x538760..0x53C7E0 (dive rows 0x580FA0 + state*0x40, shot rows
// 0x589BA0 + (state-0x209)*0x28, throw rows 0x589C30 + (state-0x206)*0x24, read through
// Keeper_d8/Keeper_d32). Keeper decision planners 0x4BF830..0x4C2D00 are in keeper_ai.cpp.
#include "player_states.hpp"
#include "wc98/sim/sim.hpp"

namespace wc98::sim {
namespace {

TeamState& T(World& w, const PlayerState& p) { return w.teams[static_cast<std::size_t>(p.team)]; }
KeeperBlock& KB(World& w, const PlayerState& p) { return w.pl.keeper[static_cast<std::size_t>(p.team)]; }

constexpr std::int32_t wrapDiff(std::int32_t a) {
    a &= 0x3ff;
    return a > 0x200 ? a - 0x400 : a;
}
std::uint32_t row20C(int s) { return 0x580fa0 + (static_cast<std::uint32_t>(s) << 6); }
std::int32_t d16(std::uint32_t a) { return Keeper_d32(a) >> 16; } // `mov eax,[a]; sar eax,16`
std::int32_t d24(std::uint32_t a) { return Keeper_d32(a) >> 24; } // `sar eax,24`: signed byte at a+3

void nextSub(PlayerState& p, int s) {
    p.stateTimer = 0;
    p.stateSub2 = s;
}
// Shared header (0x53A69E / 0x53B13C / 0x53B90C): flag a pending message on the ball winner.
void flagWinner(World& w, const PlayerState& p) {
    PlayerState& q = P(w, T(w, p).possWinner);
    if (q.f21C == 0 && (static_cast<std::uint32_t>(q.f224) >> 29) <= 3) {
        q.f218 = 1;
        q.f220 = 1;
        q.f224 = 0x60000001;
    }
}
// Save statistics (0x53A509 / 0x53AD57 / 0x53B407): opponent stat +0x65E1FC, own stat +0x65E200.
void countSave(World& w, const PlayerState& p) {
    const int t = T(w, p).index;
    ++w.match.stats[t == 0 ? 1 : 0].goals;
    ++w.match.stats[t].f200;
    // [0x581BB0](0x12): presentation trigger, not simulated.
}
void takeAndDribble(World& w, int pid) {
    Ball_takePossession(w, pid, 1);
    if (pid == w.ball.possessor) w.ball.dribbler = w.ball.possessor;
}
// 0x53C198 (EDX point, EBX flag): point inside the keeper's box (0x30 margins; flag: up to the line).
bool inBox_53C198(const World& w, int pid, const Vec3& pt, int flag) {
    const int side = w.teams[static_cast<std::size_t>(P(w, pid).team)].side;
    if (iabs(pt.x) > w.pitch.penAreaHalfWidth - 0x30) return false;
    const std::int32_t e = w.pitch.halfLength - w.pitch.penAreaDepth + 0x30;
    if (side == 1 ? pt.z < e : pt.z > -e) return false;
    const std::int32_t e2 = w.pitch.halfLength - (flag ? 0 : 0x30);
    return side == 1 ? pt.z <= e2 : pt.z >= -e2;
}
// 0x534090-style angle for the knocked opponent (0x53BE5A): o's desired facing for low contacts.
Angle knockAngle(World& w, int pid, int o, std::int32_t h) { return Player_knockAngle_534090(w, pid, o, h); }


// 0x539F78 (EDX d): keeper's guard distance from goal, d * (1 + attr[0x30]*0x28F/65536); x1.5 on 0x4C2DD8.
std::int32_t guardDist_539F78(World& w, int pid, std::int32_t d) {
    const std::int32_t k = fx(0x28f, static_cast<std::int32_t>(Player_attr(w, pid, 0x30)) << 16) + 0x10000;
    std::int32_t r = fx(k, d << 16) >> 16;
    if (Keeper_restartGuard_4C2DD8(w, pid)) r += r >> 1;
    return r;
}
// 0x53A1F8: ball wide (|x| >= 0x5A0) and deep in this keeper's end (|z| >= halfLength - 0x360).
bool ballWideDeep_53A1F8(const World& w, int pid) {
    const Vec3& b = w.ball.posI;
    if (iabs(b.x) < 0x5a0) return false;
    if (w.teams[static_cast<std::size_t>(P(w, pid).team)].side == 1 ? b.z <= 0 : b.z >= 0) return false;
    return iabs(b.z) >= w.pitch.halfLength - 0x360;
}
// 0x53A168 (EDX out): far ball: on the goal->ball line at guardDist(distBall/8), clamped to field.
void farGuard_53A168(World& w, int pid, Vec3& out) {
    PlayerState& p = P(w, pid);
    const std::int32_t d = guardDist_539F78(w, pid, p.distBall / 8);
    const std::int32_t gz = w.pitch.goalLineZ[T(w, p).side];
    const SinCos sc = sinCos(atan2i(w.ball.posI.x, w.ball.posI.z - gz)); // goal x = 0 (0x65F418)
    out.x = fx(d, sc.s);                                                  // 0x4BB0D0 writes x/z only
    out.z = gz + fx(d, sc.c);
    Pitch_clampToField(w, out);
}

} // namespace

// 0x4C2DD8: restart (phase 9) against this team with the keeper block armed (+0x004).
bool Keeper_restartGuard_4C2DD8(const World& w, int pid) {
    const int poss = w.ball.possessor;
    const int team = P(w, pid).team;
    if (poss != -1 && P(w, poss).team == team) return false;
    return w.match.phase == 9 && w.pl.keeper[static_cast<std::size_t>(team)].ctrl != 0;
}

// 0x4C2D80: like 0x4C2DD8 but the opponents must actually hold the ball.
bool Keeper_attackLost_4C2D80(const World& w, int pid) {
    const int poss = w.ball.possessor;
    const int team = P(w, pid).team;
    if (poss == -1 || P(w, poss).team == team) return false;
    return w.match.phase == 9 && w.pl.keeper[static_cast<std::size_t>(team)].ctrl != 0;
}

// 0x4C2658: (code - 1) % 3 (signed): 1 -> 2, 2 -> 0, else 1.
int Keeper_sideCode_4C2658(std::int32_t code) {
    const std::int32_t r = (code - 1) % 3;
    if (r == 1) return 2;
    if (r > 1) return 0;
    return 1;
}

// 0x4BF7D4: free kick (phase 5) to the opponents in this keeper's third.
bool Keeper_freeKickAgainst_4BF7D4(const World& w, int pid) {
    const TeamState& t = w.teams[static_cast<std::size_t>(P(w, pid).team)];
    if (w.match.phase != 5 || w.match.phaseTeam == t.index) return false;
    if (w.match.foulSpot.z - w.pitch.thirdLineZ[t.side] < 0) return t.side == 0;
    return t.side == 1;
}

// 0x539FCC (EDX out): keeper guard position.
void Keeper_position_539FCC(World& w, int pid, Vec3& out) {
    PlayerState& p = P(w, pid);
    if (Keeper_restartGuard_4C2DD8(w, pid)) { out = w.ball.posI; return; }
    if (p.distBall > 0x960) { farGuard_53A168(w, pid, out); return; }
    const int side = T(w, p).side;
    const Vec3& src = w.ball.posI.y > 0x8e ? w.ball.cross142Pos : w.ball.posI;
    const std::int32_t at = Player_attr(w, pid, 0x30);
    if (ballWideDeep_53A1F8(w, pid)) { // 0x53A021
        out.x = w.ball.posI.x >> 5;
        out.y = 0;
        const std::int32_t z = w.pitch.halfLength - 0x50 - ((at << 4) >> 2);
        out.z = side ? z : -z;
        return;
    }
    const Vec3 goal{0, 0, w.pitch.goalLineZ[side]};
    const OctDist od = octDistance(goal, src);
    Angle ang;
    if (od.dx == 0 && od.dz == 0) ang = side == 1 ? 0x200 : 0;
    else ang = atan2i(od.dx, od.dz);
    std::int32_t a = (side == 1 ? ang - 0x200 : ang) & 0x3ff;
    if (a > 0x200) a = 0x400 - a;
    const std::int32_t lo = w.pitch.goalHalfWidth - 0x18 - (0x100 - a);
    std::int32_t d = guardDist_539F78(w, pid, od.dist / 8);
    if (d < lo) d = lo;
    const SinCos sc = sinCos(ang);
    out.x = fx(d, sc.s);
    out.z = goal.z + fx(d, sc.c);
    Pitch_clampToField(w, out);
}

// 0x53B04C: a keeper holding the ball is pushed out of his goal (z within halfLength - 0x20).
void Keeper_keepOutOfGoal(World& w, int pid) {
    PlayerState& p = P(w, pid);
    if (pid != w.ball.possessor || !(w.ball.flags & 0x20)) return;
    if (iabs(p.posI.x) > w.pitch.goalHalfWidth + 0x72) return;
    std::int32_t d = 0;
    const std::int32_t lim = w.pitch.halfLength - 0x20;
    if (T(w, p).side == 1) {
        if (lim < p.posI.z) d = lim - p.posI.z;
    } else if (-lim > p.posI.z) {
        d = -lim - p.posI.z;
    }
    if (d) {
        Vec3 v = p.posI;
        v.z += d;
        Motion_snapTo(w, pid, v);
    }
}

// 0x53B5DC: parry the ball with the dive row's deflection vector (1.5x row vector + ball vel / 4,
// length clamped to the row's min/max).
void Keeper_parryBall(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const int anim = Player_animId(w, pid);
    const int s = p.state;
    std::int32_t x = 0, y = 0, zz = 0, mn, mx;
    if (s >= 0x20c && s <= 0x22f) {
        const std::uint32_t R = row20C(s);
        y = d24(R + 0x2a);
        if (anim == Keeper_d32(R)) {
            x = d24(R + 0x26);
            zz = d24(R + 0x27);
        } else {
            x = d24(R + 0x28);
            zz = d24(R + 0x29);
        }
        mn = Keeper_d8(R + 0x2e);
        mx = Keeper_d8(R + 0x2f);
    } else if (s >= 0x209 && s <= 0x20b) {
        const std::uint32_t Q = 0x589ba0 + static_cast<std::uint32_t>(s - 0x209) * 0x28;
        x = d24(Q + 0xd);
        y = d24(Q + 0xe);
        zz = d24(Q + 0xf);
        mx = d24(Q + 0x11);
        mn = d24(Q + 0x10);
    } else if (s == 0x204) {
        x = d24(0x589c21);
        y = d24(0x589c22);
        zz = d24(0x589c23);
        mx = Keeper_d8(0x589c28);
        mn = Keeper_d8(0x589c27);
    } else { // no traced caller reaches this: x/y/zz are uninitialised in the exe
        mn = 5;
        mx = 10;
    }
    const Angle a = atan2i(x, zz) + static_cast<std::int16_t>(p.anim.angle());
    const Fixed R16 = static_cast<Fixed>(Math_isqrt(static_cast<std::uint32_t>(zz * zz + x * x)) << 16);
    const SinCos sc = sinCos(a);
    const Fixed sx = fx(sc.s, R16), cz = fx(sc.c, R16);
    Vec3 v;
    v.x = (sx >> 1) + sx + (w.ball.vel.x >> 2);
    v.z = (cz >> 1) + cz + (w.ball.vel.z >> 2);
    const Fixed Y = y << 16;
    v.y = (Y >> 1) + Y + (w.ball.vel.y >> 2);
    const Fixed len = Math_vecLength3(v);
    bool lim = false;
    Fixed d = 0;
    if (len > (mx << 16)) {
        lim = true;
        d = mx << 16;
    }
    if (len < (mn << 16)) d = mn << 16;
    else if (!lim) d = 0;
    if (d != 0) {
        const Fixed k = fdiv(len, d);
        v.x = fdiv(v.x, k);
        v.y = fdiv(v.y, k);
        v.z = fdiv(v.z, k);
    }
    Ball_kickWithVelocity(w, pid, v, 0, 0, 1);
    w.emit(EventType::Save, p.team, pid);
}

// 0x53C4E0 (EDX anim, EBX a, ECX b, stack c): dive anim window: c = anim over; a = past the
// window (or another anim); b = the ball is at the contact point inside the window.
void Keeper_animPhase(World& w, int pid, int anim, std::int32_t* a, std::int32_t* b, std::int32_t* c) {
    const int m0 = Anim_markFrame0(w, anim), m1 = Anim_markFrame1(w, anim), cf = Anim_clipFrames(w, anim);
    const int f = Anim_frame(w, pid);
    *a = *b = *c = 0;
    if (P(w, pid).anim.flags() & 2) {
        *c = 1;
        return;
    }
    if (Player_animId(w, pid) != anim) {
        *a = 1;
        return;
    }
    if (m0 >= 0 && m0 > f) return;
    if (m1 >= 0 && m1 <= cf && f > m1) {
        *a = 1;
        return;
    }
    if (Player_ballAtContact_4BEA94(w, pid)) *a = *b = 1;
}

// 0x53C5BC (EDX a, EBX b, ECX c): holding phase after a catch: a = still holding (possessed),
// b = ball dropped low (< 0x10), c = lost the ball; nothing until the anim is over.
void Keeper_holdPhase(World& w, int pid, std::int32_t* a, std::int32_t* b, std::int32_t* c) {
    Keeper_keepOutOfGoal(w, pid);
    *a = *b = *c = 0;
    if (!(P(w, pid).anim.flags() & 2)) return;
    if (pid != w.ball.possessor) *c = 1;
    else if (w.ball.flags & 0x20) *a = 1;
    else if (w.ball.posI.y < 0x10) *b = 1;
}

// 0x53C62C (EAX anim, EDX default, EBX out; player = w.pl.current): next anim of the dive row by
// (anim is the row anim, holding the ball).
std::int32_t Keeper_nextAnimCb(World& w, std::int32_t anim, std::int32_t dflt, std::int32_t* out) {
    *out = 0;
    const int pid = w.pl.current;
    const PlayerState& p = P(w, pid);
    const int s = KB(w, p).diveState;
    const bool in = s >= 0x20c && s <= 0x22f;
    const std::uint32_t base = static_cast<std::uint32_t>(s - 0x206) * 0x24;
    const std::int32_t rowAnim = in ? Keeper_d32(row20C(s)) : Keeper_d32(0x589c30 + base);
    const bool m = anim == rowAnim;
    const bool h = pid == w.ball.possessor && (w.ball.flags & 0x20);
    const int k = m ? (h ? 0 : 2) : (h ? 1 : 3); // (1,1) / (0,1) / (1,0) / (0,0)
    const std::int32_t r = in ? Keeper_d32(0x580fb4 + (static_cast<std::uint32_t>(s) << 6) + k * 4)
                              : Keeper_d32(0x589c40 + base + k * 4);
    return r == -1 ? dflt : r;
}

// 0x53C764 (EAX frames to contact): the shot can no longer arrive: k <= -3, no goal event and the
// goal / post stamps are not ahead in the trajectory.
bool Keeper_ballMarkersGone(const World& w, std::int32_t k) {
    if (k > -3) return false;
    if (w.ball.flags & 2) return false;
    if (w.ball.stamp[6] != -1 && Ball_ringIndex(w, w.ball.stamp[6] - w.ball.consumed) >= 0) return false;
    if (w.ball.stamp[8] != -1 && Ball_ringIndex(w, w.ball.stamp[8] - w.ball.consumed) >= 0) return false;
    return true;
}

// 0x200 0x538760: keeper positioning and save planning.
void State200(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    Player_onBallLost(w, pid);
    if (p.targetDist < 0x90) p.flags22c &= ~0x80u;
    if (p.flags22c & 1) {
        Player_moveToward_4BE804(w, pid, p.posI);
        return;
    }
    const int side = t.side;
    if (Keeper_freeKickAgainst_4BF7D4(w, pid)) {
        Player_setState(w, pid, 0x202, 0, 0, false, true);
        return;
    }
    Vec3 v; // [esp+0x40]
    if (w.match.periodType == 3) { // penalty shoot-out
        v.x = w.pitch.penAreaHalfWidth + 0x30;
        v.z = w.pitch.halfLength - 0x2d0;
        if (w.ball.restartPos.z < 0) v.z = -v.z;
        Player_moveToward_4BE804(w, pid, v);
        if (iabs(p.posI.z) < iabs(v.z)) return;
        Vec3 u = v;
        u.z += v.z > 0 ? 0x140 : -0x140;
        if (p.targetDist > 0x140) Motion_setTargetAlt(w, pid, u);
        return;
    }
    if (w.match.phase == 9 && p.distBall < 0x690) {
        Vec3 a, b, last;
        std::int32_t fr = 0;
        const int r209 = Keeper_planShot_4C182C(w, pid, 0x209, &a, &fr, &b);
        if (r209) last = b;
        const int r20b = Keeper_planShot_4C182C(w, pid, 0x20b, &a, &fr, &b);
        if (r20b) last = b;
        const int r20a = Keeper_planShot_4C182C(w, pid, 0x20a, &a, &fr, &b);
        if (r20a) last = b;
        if (r209 || r20b || r20a) {
            std::int32_t d = 0;
            Team_nearestPlayer_4523E4(w, last, t.opponent, 0xff, &d);
            int st = 0;
            if (d > 0x230) st = r209 ? 0x209 : r20a ? 0x20a : 0;
            else st = r20b ? 0x20b : r209 ? 0x209 : r20a ? 0x20a : 0;
            if (st) {
                Player_setState(w, pid, st, 0, 0, false, true);
                return;
            }
        }
        Vec3 pos;
        std::int32_t frames = 0;
        if (Keeper_catchPlan_4BFD60(w, pid, &pos, &frames)) {
            Player_setState(w, pid, 0x205, 0, 0, false, true);
            return;
        }
        std::int32_t st = 0;
        if (Keeper_divePlan_4C0688(w, pid, &st) == 1) {
            Player_setState(w, pid, st, 0, 0, false, true);
            return;
        }
        if (Keeper_rushOut_4C03F4(w, pid)) {
            Player_setState(w, pid, 0x206, 0, 0, false, true);
            return;
        }
        std::int32_t t1 = 0, t2 = 0, anim = 0;
        Vec3 p2;
        if (Keeper_crossPlan_4BF830(w, pid, &frames, &pos, &t2, &p2, &anim)) {
            Player_setState(w, pid, 0x207, 0, 0, false, true);
            return;
        }
        (void)t1;
        if (Keeper_lowShot_4C2688(w, pid, &pos, &frames)) {
            Player_setState(w, pid, 0x204, 0, 0, false, true);
            return;
        }
    } else if (w.ball.posI.y == 0 && w.ball.vel == Vec3{}) { // 0x538CCA
        Player_clearTeamRefs_4BE6D0(w, pid);
        Keeper_release_4BF29C(w, pid);
    }
    Keeper_position_539FCC(w, pid, v); // 0x538A63
    bool face = w.match.phase != 0 && Pitch_inPenaltyBox(w, side, p.posI);
    if (w.match.phase == 9 && octDistance(p.posI, v).dist > 0x140) {
        Player_requestSprint(w, pid);
        if (w.ball.possessor < 0) {
            const int a = Team_nearestToBall_4522FC(w, p.team, 0xff);
            const int b = Team_nearestToBall_4522FC(w, t.opponent, 0xff);
            const std::int32_t sa = a >= 0 ? P(w, a).distBall : 0x3e80;
            const std::int32_t sb = b >= 0 ? P(w, b).distBall : 0x3e80;
            if (sb > 0xa0 && sa > 0xa0) face = false;
        }
    }
    if (face) { // 0x538D11
        Angle h = p.angleBall;
        if (w.opt.funModes & 0x80) h += 0x200;
        p.flags22c &= ~0x10u;
        p.headAim = h;
        p.facing = h;
    } else {
        p.flags22c |= 0x10;
    }
    Player_moveToward_4BE804(w, pid, v);
}

// 0x202 0x539134: wall/free kick against: stand on the goal line until the kick.
void State202(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const int side = T(w, p).side;
    if (!Keeper_freeKickAgainst_4BF7D4(w, pid)) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    const std::uint32_t s = static_cast<std::uint32_t>(p.stateSub2);
    if (s == 0) {
        p.flags22c |= 0x80;
        Vec3 v{0, 0, w.pitch.goalLineZ[side]}; // 0x65F418 + side*0xC
        v.z += side == 1 ? -0x30 : 0x30;
        Player_moveToward_4BE804(w, pid, v);
        if (p.targetDist < 0x10) nextSub(p, 1);
        return;
    }
    if (s != 1) return;
    const std::int32_t z = w.pitch.halfLength - 0x30;
    const Vec3 v{0, 0, side == 1 ? z : -z};
    Player_moveToward_4BE804(w, pid, v);
    if (p.anim.flags() & 2) p.flags22c &= ~0x80u;
    else if (w.match.phase == 9) Player_dropBallAndReset(w, pid);
}

// 0x205 0x53903C: come for a catchable ball; within claim range: catch (0x304 high / 0x307 low).
void State205(World& w, int pid) {
    PlayerState& p = P(w, pid);
    Vec3 pos;
    std::int32_t frames = 0;
    if (!Keeper_catchPlan_4BFD60(w, pid, &pos, &frames)) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    flagWinner(w, p);
    if (Player_claimRange(w, pid) > p.distBall) {
        p.flags22c &= ~0x10u;
        p.headAim = p.angleBall;
        p.facing = p.angleBall;
        Team_keeperTouch_4D7218(w, pid, 0xa, 0);
        Player_setState(w, pid, pos.y > 0x55 ? 0x304 : 0x307, 0, 0, false, true);
        return;
    }
    Player_requestSprint(w, pid);
    Player_moveToward_4BE804(w, pid, pos);
}

// 0x208 0x538D58: smother the ball at an attacker's feet in the box (anim 0x13F).
void State208(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const Fixed one = 0x10000, damp = 0xe666;
    const int anim = 0x13f;
    switch (static_cast<std::uint32_t>(p.stateSub2)) {
    case 0: {
        const int poss = w.ball.possessor;
        if (pid != poss) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (!Pitch_inPenaltyBox(w, T(w, P(w, poss)).side, w.ball.posI)) {
            Player_dropBallAndReset(w, poss);
            return;
        }
        std::int32_t cd = 0, ca = 0;
        const std::int32_t frames = Anim_contactInfo(w, anim, 0, 0, -0x62, 1, &cd, &ca);
        Vec3 b;
        if (!Ball_predictPos(w, frames, 1, &b)) b = w.ball.posI;
        Vec3 m;
        Player_meetPoint_4BE0FC(w, pid, anim, b, 0, 0, &p.posI, m);
        if (Math_vecTo(p.posI, m).dist > 8) {
            Player_moveToward_4BE804(w, pid, m);
            const Vec3& bv = w.ball.vel;
            if (iabs(bv.x) > one || iabs(bv.z) > one)
                Ball_kickWithVelocity(w, pid, Vec3{fx(bv.x, damp), bv.y, fx(bv.z, damp)}, 0, 0, 0);
            return;
        }
        Vec3 s;
        Angle h = 0;
        Player_stepToward_4BE890(w, pid, m, b, frames * 5, s, &h);
        Anim_start(w, pid, anim, 0, wrapDiff(h - ca));
        Sim_scheduleSplit(w, frames);
        Anim_setDrift(w, pid, s.x - p.posI.x, s.z - p.posI.z, frames);
        nextSub(p, 2);
        return;
    }
    case 2: { // 0x538FD4
        std::int32_t a, b, c;
        Keeper_animPhase(w, pid, anim, &a, &b, &c);
        if (c) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (b) takeAndDribble(w, pid);
        if (a) nextSub(p, 3);
        return;
    }
    case 3:
        if (!(p.anim.flags() & 2)) return;
        Match_dispatchEvent(w, 4, T(w, p).index);
        Player_setState(w, pid, 0x201, 0, 0, false, true);
        return;
    default: return;
    }
}

// 0x20C 0x53A654 (states 0x20C..0x22F): dive save by row 0x580FA0 + state*0x40.
void State20C(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    const int state = p.state;
    const std::uint32_t R = row20C(state);
    Vec3 A, B;
    std::int32_t frames = 0;
    const int ok = Keeper_divePlan20C_4C16D0(w, pid, state, &A, &frames, &B);
    const int sub = p.stateSub2;
    if ((sub == 0 || sub == 1) && !ok) {
        p.countdown = 0;
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (sub != 3 && pid != t.f1e0) flagWinner(w, p);
    switch (static_cast<std::uint32_t>(sub)) {
    case 0: {
        kb.k138 = 0;
        const bool b1 = Keeper_d32(R + 0x38) == 1;
        const bool c = Keeper_sideCode_4C2658(Keeper_d8(R + 0x27)) == 2;
        const std::int32_t v = (!b1 && kb.k010 == -1 && !c) ? static_cast<std::int32_t>(kb.k018 & 0xff) : 0;
        p.countdown = v;
        Sim_scheduleSplit(w, v > frames ? v : frames);
        kb.diveState = state;
        kb.kickSeen = w.ball.kickCount;
        kb.kickSeenFlag = 1;
        p.stateTimer = 0;
        p.stateSub2 = 1;
        [[fallthrough]];
    }
    case 1: {
        int edi = 2;
        if (p.countdown != 0) {
            Player_moveToward_4BE804(w, pid, p.posI);
            kb.k138 = 1;
            return;
        }
        kb.k138 = 0;
        p.flags22c &= ~0x10u;
        Angle a = p.angleBall;
        if (w.opt.funModes & 0x80) a = wrapDiff(a + 0x200);
        p.headAim = a;
        Player_requestSprint(w, pid);
        bool known = false;
        if (kb.k010 != -1) {
            edi = Keeper_sideCode_4C2658(kb.k010);
            known = true;
        }
        const int tt = kb.k13c;
        const std::int32_t ebp = d16(R + 6);
        std::int32_t ang, anim;
        if (known && edi == 0) {
            ang = d16(R + 0xa);
            anim = Keeper_d32(R);
        } else if (known && edi == 1) {
            ang = d16(R + 0xc);
            anim = Keeper_d32(R + 4);
        } else {
            ang = tt ? d16(R + 0xa) : d16(R + 0xc);
            anim = tt ? Keeper_d32(R) : Keeper_d32(R + 4);
        }
        Player_moveToward_4BE804(w, pid, known ? p.posI : A);
        if (frames > 0) return;
        if (frames < 0) {
            kb.kickSeen = w.ball.kickCount;
            kb.kickSeenFlag = 0;
        }
        if (!known && Keeper_ballMarkersGone(w, frames)) return;
        Angle h = 0;
        Vec3 o;
        if (known && edi != 2 && ((edi == 0 && tt == 0) || (edi == 1 && tt == 1))) {
            h = t.side == 1 ? (edi ? -0xff : 0x100) : (edi ? 0x100 : -0xff);
        } else {
            Player_stepToward_4BE890(w, pid, A, B, ebp * 5, o, &h);
        }
        h = wrapDiff(h - ang);
        Anim_start(w, pid, anim, 0x53c62c, h);
        kb.k004 = anim;
        Sim_scheduleSplit(w, ebp);
        if (!known || edi == 2 || (edi == 0 && tt == 1) || (edi == 1 && tt == 0)) {
            const int r = Player_stepToward_4BE890(w, pid, A, B, ebp * 5, o, &h);
            Anim_setDrift(w, pid, o.x - p.posI.x, o.z - p.posI.z, ebp);
            if (!r) {
                kb.kickSeen = w.ball.kickCount;
                kb.kickSeenFlag = 0;
            }
        }
        nextSub(p, 2);
        return;
    }
    case 2: {
        kb.k010 = -1;
        std::int32_t a, b, c;
        Keeper_animPhase(w, pid, kb.k004, &a, &b, &c);
        if (c) {
            kb.k12c = 1;
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (b) {
            const int e = Keeper_d8(R + 0x24);
            if (w.match.periodType != 3 && !(w.ball.flags & 8) && t.f204 == 0) countSave(w, p);
            if (e == 0 || e == 2) {
                takeAndDribble(w, pid);
                w.emit(EventType::Catch, p.team, pid);
            } else if (e == 1 || e == 3 || e == 4) {
                Ball_takePossession(w, pid, 1);
                Ball_releaseFromPossessor(w, pid);
                Keeper_parryBall(w, pid);
            } else if (e == 5 || e == 6) {
                Keeper_dropPossession_4BF788(w, pid);
                Ball_takePossession(w, pid, 0);
                const auto clamp = [](std::int32_t v) { return v > 0x4000 ? 0x4000 : v < -0x4000 ? -0x4000 : v; };
                const Vec3 v{clamp(-w.ball.vel.x), 0, clamp(-w.ball.vel.z)};
                Ball_kickWithVelocity(w, pid, v, 0, 0, 0);
                w.emit(EventType::Save, p.team, pid);
                if (pid == w.ball.possessor) w.ball.dribbler = w.ball.possessor;
            }
            kb.k00c = Keeper_d8(R + 0x25);
            static constexpr int kCode[7] = {4, 3, 2, 5, 1, 10, 10};
            Team_keeperTouch_4D7218(w, pid, e >= 0 && e < 7 ? kCode[e] : 11, Keeper_d8(R + 0x30));
        }
        if (a) nextSub(p, 3);
        return;
    }
    case 3: {
        std::int32_t a, b, c;
        Keeper_holdPhase(w, pid, &a, &b, &c);
        if (c) {
            kb.k12c = 1;
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (a) {
            Match_dispatchEvent(w, 4, t.index);
            Player_setState(w, pid, 0x201, 0, 0, false, true);
            return;
        }
        if (b) {
            Keeper_dropPossession_4BF788(w, pid);
            Player_setState(w, pid, 0x308, 0, 0, false, true);
        }
        return;
    }
    default: return;
    }
}

// 0x209 0x53B11C (states 0x209..0x20B): shot save by row 0x589BA0 + (state-0x209)*0x28.
void State209(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    const int state = p.state;
    const std::uint32_t Q = 0x589ba0 + static_cast<std::uint32_t>(state - 0x209) * 0x28;
    if (p.stateSub2 != 3) flagWinner(w, p);
    Anim_frame(w, pid); // result unused (0x53B15B)
    const std::int32_t lead = d16(Q + 2), ang = d16(Q + 6), anim = Keeper_d32(Q);
    switch (static_cast<std::uint32_t>(p.stateSub2)) {
    case 0:
        kb.kickSeen = w.ball.kickCount;
        kb.kickSeenFlag = 1;
        nextSub(p, 1);
        return;
    case 1: {
        Vec3 A, B;
        std::int32_t frames = 0;
        if (!Keeper_planShot_4C182C(w, pid, state, &A, &frames, &B)) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        Player_moveToward_4BE804(w, pid, A);
        if (frames > 0) {
            Player_requestSprint(w, pid);
            return;
        }
        if (frames < 0) {
            kb.kickSeen = w.ball.kickCount;
            kb.kickSeenFlag = 0;
        }
        if (Keeper_ballMarkersGone(w, frames)) return;
        Vec3 o;
        Angle h = 0;
        const int r = Player_stepToward_4BE890(w, pid, A, B, lead * 5, o, &h);
        Anim_start(w, pid, anim, 0, wrapDiff(h - ang));
        Sim_scheduleSplit(w, lead);
        Anim_setDrift(w, pid, o.x - p.posI.x, o.z - p.posI.z, lead);
        if (!r) {
            kb.kickSeen = w.ball.kickCount;
            kb.kickSeenFlag = 0;
        }
        nextSub(p, 2);
        return;
    }
    case 2: {
        std::int32_t a, b, c;
        Keeper_animPhase(w, pid, anim, &a, &b, &c);
        if (c) {
            kb.k128 = 1;
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (b) {
            const int bb = d24(Q + 0x15);
            const std::int32_t cc = Keeper_d32(Q + 0x1c);
            const auto ahead = [&](int k) {
                return w.ball.stamp[k] != -1 && Ball_ringIndex(w, w.ball.stamp[k] - w.ball.consumed) >= 0;
            };
            if (w.match.periodType != 3 && !(w.ball.flags & 8) && (ahead(6) || ahead(8))) countSave(w, p);
            if (cc == 0) {
                takeAndDribble(w, pid);
                w.emit(EventType::Catch, p.team, pid);
            } else {
                Ball_takePossession(w, pid, 1);
                Ball_releaseFromPossessor(w, pid);
                Keeper_parryBall(w, pid);
            }
            kb.k00c = 8;
            Team_keeperTouch_4D7218(w, pid, cc == 0 ? 2 : cc == 1 ? 1 : 11, bb);
        }
        if (a) nextSub(p, 3);
        return;
    }
    case 3: {
        std::int32_t a, b, c;
        Keeper_holdPhase(w, pid, &a, &b, &c);
        if (c) {
            kb.k128 = 1;
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (a) {
            Match_dispatchEvent(w, 4, t.index);
            Player_setState(w, pid, 0x201, 0, 0, false, true);
            return;
        }
        if (b) {
            Keeper_dropPossession_4BF788(w, pid);
            Player_setState(w, pid, 0x308, 0, 0, false, true);
        }
        return;
    }
    default: return;
    }
}

// 0x206 0x53B8F4: rush out and throw himself at the ball (rows 0x589C30 + (state-0x206)*0x24).
void State206(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    if (p.stateSub2 == 0 && !Keeper_rushOut_4C03F4(w, pid)) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (p.stateSub2 != 2) flagWinner(w, p);
    switch (static_cast<std::uint32_t>(p.stateSub2)) {
    case 0: {
        const int s = p.state;
        const bool r = Player_pointLeftOf_4BE080(w, pid, w.ball.posI);
        const std::uint32_t base = static_cast<std::uint32_t>(s - 0x206) * 0x24;
        const std::int32_t anim = Keeper_d32((r ? 0x589c30 : 0x589c34) + base);
        const std::int32_t frames = d16(0x589c36 + base);
        const std::int32_t dist = d16(0x589c38 + base);
        const std::int32_t hdg = d16((r ? 0x589c3a : 0x589c3c) + base);
        kb.diveState = s;
        kb.k008 = anim;
        const int h = w.ball.possessor >= 0 ? w.ball.possessor : w.ball.prevPossessor;
        Vec3 bp = w.ball.posI; // [esp+0x5C]
        bp.x += fx(P(w, h).m98, frames << 16) >> 16;
        bp.z += fx(P(w, h).m9c, frames << 16) >> 16;
        Vec3 pr;
        if (!Ball_predictPos(w, frames, 1, &pr)) pr = w.ball.posI;
        bp.y = pr.y;
        const OctDist g = Math_vecTo(Vec3{0, 0, w.pitch.goalLineZ[t.side]}, bp);
        const Angle a = (g.dx | g.dz) ? atan2i(g.dx, g.dz) : (t.side == 1 ? 0x200 : 0);
        const SinCos sc = sinCos(a);
        Vec3 stand{bp.x - fx(dist, sc.s), 0, bp.z - fx(dist, sc.c)}; // [esp+0x98] (0x4BB11C)
        const OctDist d0 = Math_vecTo(p.posI, stand);
        bool throwNow = false;
        bool move = true;
        if (bp.y < 0x20) {
            const OctDist d40 = Math_vecTo(p.posI, bp), d10 = Math_vecTo(stand, bp);
            if (d0.dist < 0x18) throwNow = true;
            if (d40.dist < d10.dist && d40.dist <= dist) throwNow = true;
            if (P(w, h).state == 0x300 && w.match.player2a0 == -1) {
                if (d40.dist > 0xf0) {
                    std::int32_t st = 0;
                    if (Keeper_divePlan_4C0688(w, pid, &st) == 1) {
                        Player_setState(w, pid, st, 0, 0, false, true);
                        return;
                    }
                } else {
                    throwNow = true;
                }
            }
            if (throwNow) move = false;
        }
        if (move) { // MOVE
            bool useStand = Keeper_attackLost_4C2D80(w, pid) || Pitch_inPenaltyBox(w, t.side, P(w, h).posI);
            if (!useStand) { // 0x53BF2F
                std::int32_t fr = 0;
                Vec3 e;
                if (Keeper_edgeIntersect_4C2970(w, h, &fr, e)) {
                    const OctDist de = Math_vecTo(p.posI, e);
                    std::int32_t f = fr + 0x78;
                    if (f < 0) f = 0;
                    if (Motion_predictDistance(w, pid, p.sprintMax, atan2i(de.dx, de.dz), f, 0, 1) <= de.dist)
                        useStand = true;
                }
            }
            Vec3 tgt = stand;
            if (!useStand) Keeper_position_539FCC(w, pid, tgt);
            p.flags22c &= ~0x10u;
            p.headAim = p.facing = p.angleBall;
            if (tgt.x != p.posI.x || tgt.z != p.posI.z) {
                Player_moveToward_4BE804(w, pid, tgt);
                Motion_setTargetAlt(w, pid, tgt);
            }
            return;
        }
        // THROW
        Vec3 o;
        Angle ang = 0;
        Player_stepToward_4BE890(w, pid, stand, bp, frames * 5, o, &ang);
        const std::int32_t dx = o.x - p.posI.x, dz = o.z - p.posI.z;
        Vec3 c;
        Player_animContactPoint_4BE26C(w, anim, o, bp, dx, dz, c);
        if (!inBox_53C198(w, pid, c, 1)) {
            Player_tryTackle(w, pid, 1);
            return;
        }
        Anim_start(w, pid, anim, 0x53c62c, wrapDiff(ang - hdg));
        Anim_setDrift(w, pid, dx, dz, frames);
        Sim_scheduleSplit(w, frames);
        nextSub(p, 1);
        return;
    }
    case 1: {
        std::int32_t a, b, c;
        Keeper_animPhase(w, pid, kb.k008, &a, &b, &c);
        if (c) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (b) {
            if (w.match.periodType != 3 && !(w.ball.flags & 8)) ++w.match.stats[t.index].f200;
            int o = w.ball.possessor;
            if (o < 0 && w.ball.prevPossessor != pid) o = w.ball.prevPossessor;
            if (o >= 0) {
                const std::int32_t hy = Anim_contactPos(w, pid).y;
                Player_setState(w, o, 0x81, 0x31, knockAngle(w, pid, o, hy), false, false);
            }
            takeAndDribble(w, pid);
            w.emit(EventType::Catch, p.team, pid);
            Team_keeperTouch_4D7218(w, pid, 6, 2);
        }
        if (!a) return;
        if (w.ball.possessor == -1 && w.ball.prevPossessor == pid) {
            std::int32_t d = 0;
            const int n = Team_nearestPlayer_4523E4(w, p.posI, t.opponent, 0xff, &d);
            if (d < 0x40) {
                const std::int32_t hy = w.ball.posI.y < 0x72 ? w.ball.posI.y : 0x72;
                Player_setState(w, n, 0x81, 0x31, knockAngle(w, pid, n, hy), false, false);
            }
        } else {
            nextSub(p, 2);
        }
        return;
    }
    case 2: {
        std::int32_t a, b, c;
        Keeper_holdPhase(w, pid, &a, &b, &c);
        if (c) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (a) {
            Match_dispatchEvent(w, 4, t.index);
            Player_setState(w, pid, 0x201, 0, 0, false, true);
            return;
        }
        if (b) {
            Keeper_dropPossession_4BF788(w, pid);
            Player_setState(w, pid, 0x308, 0, 0, false, true);
        }
        return;
    }
    default: return;
    }
}

// 0x207 0x53C214: come out for a cross (plan 0x4BF830): run, then the catch anim.
void State207(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    std::int32_t t1 = 0, t2 = 0, anim = 0;
    Vec3 p1, p2;
    const int r = Keeper_crossPlan_4BF830(w, pid, &t1, &p1, &t2, &p2, &anim);
    if (r == 0 && p.stateSub2 == 0 && pid != w.ball.possessor) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (p.stateSub2 != 2) flagWinner(w, p);
    switch (static_cast<std::uint32_t>(p.stateSub2)) {
    case 0:
        if (t2 > 0) {
            Player_requestSprint(w, pid);
            Player_moveToward_4BE804(w, pid, p2);
        } else {
            std::int32_t dist = 0, cang = 0;
            Anim_contactInfo(w, anim, 0, 0, -0x62, 1, &dist, &cang);
            const Angle a = atan2i(p1.x - p2.x, p1.z - p2.z);
            Anim_start(w, pid, anim, 0, wrapDiff(a - cang));
            Anim_setDrift(w, pid, p2.x - p.posI.x, p2.z - p.posI.z, t1 - t2);
            Sim_scheduleSplit(w, t1 - t2);
            nextSub(p, 1);
        }
        return;
    case 1: {
        std::int32_t a, b, c;
        Keeper_animPhase(w, pid, Player_animId(w, pid), &a, &b, &c);
        if (c) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (b) {
            takeAndDribble(w, pid);
            w.emit(EventType::Catch, p.team, pid);
            Team_keeperTouch_4D7218(w, pid, 7, 0);
        }
        if (a) nextSub(p, 2);
        return;
    }
    case 2: {
        std::int32_t a, b, c;
        Keeper_holdPhase(w, pid, &a, &b, &c);
        if (c) {
            kb.k130 = 0;
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (a) {
            Match_dispatchEvent(w, 4, t.index);
            Player_setState(w, pid, 0x201, 0, 0, false, true);
            return;
        }
        if (b) {
            Keeper_dropPossession_4BF788(w, pid);
            Player_setState(w, pid, 0x308, 0, 0, false, true);
        }
        return;
    }
    default: return;
    }
}

namespace {
// 0x4BF2CC: clamp the keeper's current move target into his box (x +-(penAreaHalfWidth - 0x90), z
// between penAreaLineZ + 0x30 and halfLength - 0x30 on his side).
void clampTargetToBox_4BF2CC(World& w, int pid) {
    PlayerState& p = P(w, pid);
    Vec3& v = p.targetRef ? p.altTarget : p.target;
    const std::int32_t xl = w.pitch.penAreaHalfWidth - 0x90;
    if (-xl > v.x) v.x = -xl;
    else if (xl < v.x) v.x = xl;
    const std::int32_t hi = w.pitch.halfLength - 0x30, lo = w.pitch.penAreaLineZ + 0x30;
    if (T(w, p).side == 1) {
        if (lo > v.z) v.z = lo;
        else if (hi < v.z) v.z = hi;
    } else {
        if (-hi > v.z) v.z = -hi;
        else if (-lo < v.z) v.z = -lo;
    }
}
// 0x4C2584: penalty-keeper dive code 1..9 (table 0x4BEC10 by pad direction): AI random, human from
// the pad (analog: thresholds +-0x28, mirrored on the negative half); a centre code keeps the
// previous choice unless a button (0x70) was just pressed.
std::int32_t penaltyDiveCode_4C2584(World& w, int pid) {
    static constexpr std::int32_t kDir[9] = {5, 2, 3, 6, 9, 8, 7, 4, 1};
    PlayerState& p = P(w, pid);
    if (p.ctrl == 0) return kDir[w.rng.next() % 9];
    const std::int32_t cur = KB(w, p).k010;
    const ControllerState& c = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
    if (!(c.bits & 0x70)) return cur;
    std::int32_t code;
    if (c.analogDevice != 0) {
        std::int32_t ax = c.analogX, az = c.analogZ;
        if (p.posI.z < 0) {
            ax = -ax;
            az = -az;
        }
        std::int32_t row = 1, col = 1;
        if (az > 0x28) row = 0;
        if (az < -0x28) row = 2;
        if (ax > 0x28) col = 2;
        if (ax < -0x28) col = 0;
        code = row * 3 + col + 1;
    } else {
        code = kDir[c.rawDir];
    }
    if (code == 5 && cur != 5 && !(c.pressed & 0x70)) return cur;
    return code;
}
// Shared take-control chain (0x539B84 / 0x539CC2): shoot-out round, training, the fouled player's
// controller, else the team's next controller.
void takeControl(World& w, int pid, bool advance) {
    TeamState& t = T(w, P(w, pid));
    const MatchState& m = w.match;
    if (m.phase == 6) Input_giveControlToNth(w, m.so.round, pid);
    else if (w.opt.gameMode == 1) Input_giveControlToNth(w, Training_ctrl66741C(w), pid);
    else if (m.victimCtrl != 0)
        Player_transferControl(w, w.controllers[static_cast<std::size_t>(m.victimCtrl - 1)].player - 1, pid);
    else Input_giveControlToNth(w, advance ? t.f374++ : t.f374, pid);
}
} // namespace

// 0x201 0x539280: keeper holding the ball: walk to the box edge, face the field, then distribute
// (human: set-piece kind 0x65F2C0 picks the kick; AI: 0x4DC614). Tracks the distance walked
// (0x59F464): from 0x90 on the keeper must release.
void State201(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    MatchState& m = w.match;
    PlayerShared& g = w.pl;
    if (pid != w.ball.possessor || pid != w.ball.dribbler) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    const Vec3 centre{0, 0, 0}; // 0x65F38C
    switch (static_cast<std::uint32_t>(p.stateSub2)) {
    case 0: // 0x5392CE
        Player_moveToward_4BE804(w, pid, p.posI);
        g.k201Walk = 0;
        g.k201Last = p.posI;
        p.flags22c &= ~0x80u;
        kb.k134 = 1;
        m.setPiecePartner = -1;
        m.setPieceTaker = pid;
        Player_assignControlToReceiver(w, pid, -1);
        m.setPieceKind = 0;
        nextSub(p, 1);
        break;
    case 1: { // 0x5393C1: walk toward the box, inside 0x140 of its edges
        p.flags22c &= ~0x10u;
        Vec3 v = p.posI;
        const std::int32_t xl = w.pitch.penAreaHalfWidth - 0x140;
        if (xl < v.x) v.x = xl;
        else if (-xl > v.x) v.x = -xl;
        const std::int32_t inner = w.pitch.halfLength - w.pitch.penAreaDepth + 0x140, outer = w.pitch.halfLength - 0x140;
        if (t.side == 1) {
            if (inner > v.z) v.z = inner;
            if (outer < v.z) v.z = outer;
        } else {
            if (-inner < v.z) v.z = -inner;
            if (-outer > v.z) v.z = -outer;
        }
        Motion_facePoint(w, pid, v);
        if (Motion_turnSettled(w, pid)) {
            Player_moveToward_4BE804(w, pid, v);
            nextSub(p, 2);
            break;
        }
        const Vec3 goal{p.posI.x, 0, t.side != 0 ? w.pitch.halfLength : -w.pitch.halfLength}; // y unused
        p.moveReq |= Player_pointLeftOf_4BE080(w, pid, goal) ? 0x80u : 0x100u;
        p.anim.req = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.moveReq) >> 16);
        break;
    }
    case 2: // 0x5394F8
        if (p.targetDist < 8) {
            Motion_setTarget(w, pid, p.posI);
            nextSub(p, 3);
        }
        break;
    case 3: { // 0x539526: face the centre; an AI keeper whose team leads may waste time (anims 0xDB/0xDC)
        Motion_facePoint(w, pid, centre);
        if (!Motion_turnSettled(w, pid)) break;
        const std::int32_t lead = t.index == 0 ? m.score[0] - m.score[1] : m.score[1] - m.score[0];
        if (p.ctrl == 0 && w.opt.gameMode != 1 && lead > 0) {
            Anim_start(w, pid, Anim_resolveId(w, (w.rng.next() & 1) ? 0xdb : 0xdc), 0, p.facing);
            nextSub(p, 4);
            break;
        }
        g.k201Deadline = m.elapsedSec + 0xf;
        nextSub(p, 5);
        break;
    }
    case 4: // 0x5395E8
        if (p.anim.b34[3] & 2) {
            g.k201Deadline = m.elapsedSec + 2;
            nextSub(p, 5);
            break;
        }
        // ball y <= 0x10: bounce sound 0x4A2818 (presentation only)
        Player_moveToward_4BE804(w, pid, p.posI);
        break;
    case 5: { // 0x53963C
        if (static_cast<std::uint32_t>(m.elapsedSec) > static_cast<std::uint32_t>(g.k201Deadline) && w.opt.gameMode != 1)
            m.setPieceKind = 0x10;
        if (p.ctrl == 0 && (m.flags278 & 8)) Player_assignControlToReceiver(w, pid, -1);
        if (p.ctrl == 0 && static_cast<std::uint32_t>(p.stateTimer) > 0x78) {
            nextSub(p, 6);
            break;
        }
        if (p.ctrl != 0) {
            const std::int32_t k = m.setPieceKind;
            if (k == 0xb) { // 0x5396CC: drop the ball to the feet
                kb.k134 = 1;
                w.ball.flags &= ~0x20u; // 0x475174(0)
                if (pid == w.ball.dribbler) w.ball.dribbler = -1;
                Ball_stopDead(w);
                Keeper_dropPossession_4BF788(w, pid);
                kb.busy = 1;
                Match_dispatchEvent(w, 0xb, t.index);
                Player_setState(w, pid, 0x308, 0, 0, false, false);
                return;
            }
            if (k == 0xd || k == 0xc || k == 4 || k == 0xf || k == 0xe || k == 0x10) {
                nextSub(p, 6);
                break;
            }
        }
        if (g.k201Walk < 0x90) { // 0x5397A1
            p.flags22c &= ~0x10u;
            Motion_facePoint(w, pid, centre);
            if (p.ctrl == 0) {
                if (iabs(p.posI.z) <= w.pitch.penAreaLineZ + 0x120) {
                    g.k201Walk = 0x90;
                } else {
                    Vec3 v = p.posI;
                    v.z += t.side == 1 ? -0x10 : 0x10;
                    Player_moveToward_4BE804(w, pid, v);
                }
            } else {
                const ControllerState& c = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
                Vec3 v = p.posI;
                v.x += c.dirX << 4;
                v.z += c.dirZ << 4;
                const std::int32_t lim = w.pitch.halfLength - 0x30;
                if (t.side == 1) {
                    if (lim < v.z) v.z = lim;
                } else if (-lim > v.z) {
                    v.z = -lim;
                }
                Player_moveToward_4BE804(w, pid, v);
            }
        } else { // 0x5398A6
            kb.k134 = 1;
            if (p.ctrl != 0) {
                const ControllerState& c = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
                Motion_faceVector(w, pid, c.dirX, c.dirZ);
            } else {
                nextSub(p, 6);
                break;
            }
        }
        if (w.ball.possessor != pid) { // 0x539806
            Player_dropBallAndReset(w, pid);
            Match_dispatchEvent(w, 0xb, t.index);
            return;
        }
        clampTargetToBox_4BF2CC(w, pid);
        break;
    }
    case 6: { // 0x53991C: distribute
        Player_moveToward_4BE804(w, pid, p.posI);
        // 0x505084(pid, 0xE): commentary, not simulated
        if (p.ctrl == 0) {
            TeamAI_onBallWon_4DC614(w, pid, 8);
            break;
        }
        std::int32_t lift = 0xf0, a3 = 8, a2 = 1;
        KickVec v{}; // kinds 5..11 leave the exe's [ebp-0x40] unwritten (stack garbage); zero here
        if (static_cast<std::uint32_t>(t.playerCount) <= 1) { // 0x539AA7: kick it anywhere ahead
            const std::uint32_t th = static_cast<std::uint32_t>(w.pitch.thirdLength);
            const auto d = static_cast<std::int32_t>(th + w.rng.next() % th);
            Math_polarRecord(d, p.facing, &v.dist);
            const std::int32_t rel[3] = {v.dist, v.dx, v.dz};
            Kick_planToPoint(w, pid, rel, nullptr, (v.dist >> 1) + (v.dist >> 3), -1, 0, a2, a3, 0, a2);
            Player_setState(w, pid, 0x300, 0, 0, false, false);
            break;
        }
        const std::int32_t k = m.setPieceKind;
        if (k == 4 || k == 0x10) { // 0x539974: long kick
            t.f1e0 = Kick_aimTo(w, pid, &v, 2, k == 0x10 ? 1 : 0, 0, -1);
            if (v.dist > 0xd80) v.dist = 0xd80;
            lift = (v.dist >> 4) + (v.dist >> 3);
        } else if (k >= 0xc && k <= 0xf) { // 0x539A0A: throw / roll
            t.f1e0 = Kick_aimTo(w, pid, &v, 0, (k == 0xe || k == 0xf) ? 1 : 0, 0, -1);
            if (v.dist < w.pitch.quarterLength) {
                a3 = a2 = 0xa;
                lift = w.ball.posI.y + 0x20;
            } else {
                a3 = 9;
                a2 = 0xa;
                if (v.dist > 0x960) v.dist = 0x960;
                Math_polarRecord(v.dist, atan2i(v.dx, v.dz), &v.dist);
                lift = (v.dist >> 5) + w.ball.posI.y;
            }
        }
        Motion_faceVector(w, pid, v.dx, v.dz);
        const std::int32_t rel[3] = {v.dist, v.dx, v.dz};
        Kick_planToPoint(w, pid, rel, nullptr, lift, -1, 0, a2, a3, 0, 1);
        Player_setState(w, pid, 0x300, 0, 0, false, false);
        break;
    }
    default: break;
    }
    // 0x539353: distance walked (octagonal) since the last frame
    std::int32_t a = iabs(p.posI.x - g.k201Last.x), b = iabs(p.posI.z - g.k201Last.z);
    const std::int32_t mn = a > b ? b : a;
    g.k201Walk += a + b - (mn >> 1);
    g.k201Last = p.posI;
}

// 0x203 0x539B30: penalty keeper on his line: take position, pick a dive code (0x4C2584) and,
// once the kick is taken (phase 9), dive by 0x4C0688.
void State203(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    const std::int32_t hl = w.pitch.halfLength;
    if (t.humans > 0 && p.ctrl == 0 && p.stateSub2 != 0) takeControl(w, pid, false);
    switch (static_cast<std::uint32_t>(p.stateSub2)) {
    case 0: {
        if (t.humans > 0) takeControl(w, pid, true);
        p.flags22c |= 0x80;
        t.possWinner = pid;
        kb.k010 = 5;
        const Vec3 v{0, 0, w.ball.restartPos.z > 0 ? hl - 0x20 : 0x20 - hl};
        Player_moveToward_4BE804(w, pid, v);
        if (w.match.periodType == 3 && hl / 2 < p.targetDist) {
            const Vec3 u{0, 0, v.z > 0 ? hl / 2 : -hl / 2};
            Motion_warpTo(w, pid, &u);
            Player_moveToward_4BE804(w, pid, v);
        }
        if (iabs(p.posI.z) < iabs(w.ball.restartPos.z)) {
            Vec3 a = w.ball.restartPos;
            a.x += 0xa0;
            Motion_setTargetAlt(w, pid, a);
        }
        nextSub(p, 1);
        return;
    }
    case 1:
        if (p.targetDist < 8 && p.speed == 0) {
            p.flags22c &= ~0x80u;
            Motion_snapTo(w, pid, Motion_targetVec(w, pid));
            nextSub(p, 2);
        }
        return;
    case 2: {
        p.flags22c &= ~0x10u;
        Motion_facePoint(w, pid, Vec3{p.posI.x, p.posI.y, -p.posI.z});
        Vec3 v = p.posI;
        if (p.ctrl == 0) {
            v.x = 0;
            Player_moveToward_4BE804(w, pid, v);
            kb.k010 = penaltyDiveCode_4C2584(w, pid);
        } else {
            kb.k010 = penaltyDiveCode_4C2584(w, pid);
            const ControllerState& c = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
            if (!(c.bits & 0x70)) {
                if (c.analogDevice != 0) {
                    if (c.analogX < -0x28) v.x -= 0x10;
                    if (c.analogX > 0x28) v.x += 0x10;
                } else {
                    const std::int32_t d = c.rawDirZ << 4; // controller +0x1C
                    v.x += p.posI.z > 0 ? -d : d;
                }
            }
            const std::int32_t gw = w.pitch.goalHalfWidth;
            if (v.x > gw) v.x = gw;
            if (-gw > v.x) v.x = -gw;
            v.z = p.posI.z > 0 ? hl - 0x20 : 0x20 - hl;
            Player_moveToward_4BE804(w, pid, v);
        }
        if (w.match.phase != 9) return;
        std::int32_t st = 0;
        if (Keeper_divePlan_4C0688(w, pid, &st) == 1) Player_setState(w, pid, st, 0, 0, false, true);
        else Player_dropBallAndReset(w, pid);
        return;
    }
    default: return;
    }
}

// 0x204 0x53A264: low save (row 0x589C18 + (state-0x204)*0x18: anim, frames, stand-off, angle):
// reach the low-shot point (0x4C2688), play the anim, parry/hold like the dives.
void State204(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KeeperBlock& kb = KB(w, p);
    const std::uint32_t row = 0x589c18 + static_cast<std::uint32_t>(p.state - 0x204) * 0x18;
    const int anim = Keeper_d32(row);
    switch (static_cast<std::uint32_t>(p.stateSub2)) {
    case 0: {
        Vec3 pos;
        std::int32_t frames = 0;
        if (!Keeper_lowShot_4C2688(w, pid, &pos, &frames)) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        const std::int32_t dx = pos.x - p.posI.x, dz = pos.z - p.posI.z;
        const Angle h = (dx != 0 || dz != 0) ? atan2i(dx, dz) : (t.side == 1 ? 0 : 0x200);
        const std::int32_t fr = d16(row + 2), dist = d16(row + 4), ang = d16(row + 6);
        const SinCos sc = sinCos(h); // 0x4BB11C
        const Vec3 stand{pos.x - fx(dist, sc.s), 0, pos.z - fx(dist, sc.c)}; // y unwritten in the exe
        const std::int32_t k = frames - fr;
        if (Keeper_ballMarkersGone(w, k)) return;
        if (k > 0) {
            Player_moveToward_4BE804(w, pid, stand);
            Player_requestSprint(w, pid);
            return;
        }
        Vec3 s;
        Angle a = 0;
        Player_stepToward_4BE890(w, pid, stand, pos, fr * 5, s, &a);
        if (octDistance(s, p.posI).dist > 0x50) {
            kb.k124 = 1;
            return;
        }
        Anim_start(w, pid, anim, 0, wrapDiff(a - ang));
        Anim_setDrift(w, pid, s.x - p.posI.x, s.z - p.posI.z, fr);
        Sim_scheduleSplit(w, fr);
        nextSub(p, 1);
        return;
    }
    case 1: { // 0x53A4C9
        std::int32_t a = 0, b = 0, c = 0;
        Keeper_animPhase(w, pid, anim, &a, &b, &c);
        if (c) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (b) {
            if (w.match.periodType != 3 && !(w.ball.flags & 8)) countSave(w, p);
            Ball_takePossession(w, pid, 1);
            Ball_releaseFromPossessor(w, pid);
            Keeper_parryBall(w, pid);
            kb.k00c = 8;
            Team_keeperTouch_4D7218(w, pid, 0, 2);
        }
        if (a) nextSub(p, 2);
        return;
    }
    case 2: { // 0x53A2BC
        std::int32_t a = 0, b = 0, c = 0;
        Keeper_holdPhase(w, pid, &a, &b, &c);
        if (c) {
            kb.k124 = 1;
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (a) {
            Match_dispatchEvent(w, 4, t.index);
            Player_setState(w, pid, 0x201, 0, 0, false, true);
            return;
        }
        if (b) {
            Keeper_dropPossession_4BF788(w, pid);
            Player_setState(w, pid, 0x308, 0, 0, false, true);
        }
        return;
    }
    default: return;
    }
}

} // namespace wc98::sim

namespace wc98::sim {
// 0x4D7218: commentary event chain (0x667150 counter, 0x4D7B84, 0x4A2D68, 0x50510C...); commentary is
// not simulated, no match state touched.
void Team_keeperTouch_4D7218(World&, int, int, int) {}
// 0x4D61BC: commentary event memory (0x667294/0x666FE8); not simulated.
void Team_noteTouch_4D61BC(World&, int) {}

} // namespace wc98::sim
