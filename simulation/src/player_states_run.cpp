#include <cstdio>
// Outfield restart / presentation states 0x180-0x18D (kick-off, throw-in, corners, free kicks,
// goal kicks, penalties, substitutions, bench idling, walk-offs) and the sequence-anim helpers
// 0x535870..0x535B0C — owner: SimPorter2. Ported from FIFA99.EXE v1.1 0x535870..0x538760 and
// 0x557A24..0x557FBC. Exe addresses in comments; see player_states.hpp for conventions.
// SeqTrigger_arm [0x581BB0] (presentation), sounds and commentary calls are not simulated.
#include "player_states.hpp"
#include "wc98/sim/sim.hpp"

namespace wc98::sim {
namespace {

TeamState& T(World& w, const PlayerState& p) { return w.teams[static_cast<std::size_t>(p.team)]; }
const PlayerAttr& A(const World& w, const PlayerState& p) { return w.attrs[static_cast<std::size_t>(p.attr)]; }

void nextSub(PlayerState& p) {
    p.stateTimer = 0;
    ++p.stateSub2;
}
void reqBits(PlayerState& p, std::uint32_t m) {
    p.moveReq |= m;
    p.anim.req = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.moveReq) >> 16);
}
std::uint32_t timer(const PlayerState& p) { return static_cast<std::uint32_t>(p.stateTimer); }
// FUN_004BB0D0: base + dist along a (x/z only).
void offsetXZ(Vec3& out, const Vec3& base, Fixed d, Angle a) {
    const Vec3 r = offsetInDirection(base, d, a);
    out.x = r.x;
    out.z = r.z;
}
// 0x454E3C (EAX mask): outside phase 9, bit 0 -> flags278 |= 0x40, bit 1 (with 0x4000) -> 0x80; f280 = 0.
void ffOn(World& w, int a) {
    MatchState& m = w.match;
    if (m.phase == 9) return;
    if (a & 1) m.flags278 |= 0x40;
    if ((a & 2) && (m.flags278 & 0x4000)) m.flags278 |= 0x80;
    m.f280 = 0;
}
// 0x454E74 (EAX mask): clear flags278 0x40 (bit 0) / 0x80 (bit 1).
void ffOff(World& w, int a) {
    if (a & 1) w.match.flags278 &= ~0x40u;
    if (a & 2) w.match.flags278 &= ~0x80u;
}
// 0x475174: ball.flags bit 0x20 = on & 1.
void setPossessed(World& w, int on) { w.ball.flags = (w.ball.flags & ~0x20u) | ((static_cast<std::uint32_t>(on) & 1) << 5); }
// 0x4D0750 (EAX kick request, EDX player): zero the request, kicker = pid.
void kickReset(World& w, int pid) {
    P(w, pid).kick = KickRequest{};
    P(w, pid).kick.kicker = pid;
}
void dribblerIfPossessor(World& w, int pid) {
    if (pid == w.ball.possessor) w.ball.dribbler = w.ball.possessor;
}

// Sequence tables 0x59F2C8 (u32 entries: anim = e & 0x7FF, bits 11-15 flags, bit 16 timer kind,
// bits 17+ frames) and the per-anim pointer table 0x59F2FC. f1FC keeps the exe address.
constexpr std::uint32_t kSeqBase = 0x59f2c8;
constexpr std::uint32_t kSeq[13] = {0x21151, 0x21166, 0x21166, 0x21166, 0x2014d, 0x79014e, 0x2014f,
                                    0x20154, 0x790155, 0x20156, 0x20001, 0x29002a, 0x2002b};
constexpr std::uint32_t kSeqPtr[4] = {0x59f2cc, 0x59f2d8, 0x59f2e4, 0x59f2f0}; // 0x59F2FC
std::uint32_t seqEntry(std::uint32_t addr) { return kSeq[(addr - kSeqBase) >> 2]; }

// i16 tables 0x59F30C (run-up offsets), 0x59F31C (celebration anims), 0x59F32C (taker headings).
constexpr std::int16_t k59F30C[8] = {192, 192, 192, 192, 192, 192, 32, 192};
constexpr std::int16_t kCelebAnim[4] = {358, 334, 341, 42};
constexpr std::int16_t kTakerHead[8] = {256, -256, -256, 256, -256, 256, 256, -256};
// u16 0x59F340 idle anims (State18C, chooser 0x537F98).
constexpr std::uint16_t kIdleAnims[24] = {0x168, 0x168, 0x168, 0x162, 0x163, 0x164, 0x162, 0x163,
                                          0x164, 0x162, 0x163, 0x164, 0x162, 0x163, 0x164, 0x162,
                                          0x163, 0x164, 0x162, 0x163, 0x164, 0x162, 0x163, 0x164};
// i32 0x5906D0[(A[0x2B] + A[0x37]) >> 1] (State183 free-kick spin).
constexpr std::int32_t k5906D0[20] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10};

std::uint32_t lo16(std::int32_t v) { return static_cast<std::uint32_t>(v) & 0xffff; }
std::uint32_t hi16(std::int32_t v) { return static_cast<std::uint32_t>(v) >> 16; }
void setLo16(std::int32_t& v, std::uint32_t x) {
    v = static_cast<std::int32_t>((static_cast<std::uint32_t>(v) & 0xffff0000u) | (x & 0xffff));
}
void setHi16(std::int32_t& v, std::uint32_t x) {
    v = static_cast<std::int32_t>((static_cast<std::uint32_t>(v) & 0xffffu) | (x << 16));
}

} // namespace

// 0x535870: sequence status: 1 not running (f204 bit 30 clear), 2 last entry reached (bit 31), 0 running.
int Seq_status_535870(const World& w, int pid) {
    const auto f = static_cast<std::uint32_t>(P(w, pid).f204);
    if (!(f & 0x40000000u)) return 1;
    return (f & 0x80000000u) ? 2 : 0;
}

// 0x535A38 (EDX heading, EBX table, ECX count): start an anim sequence: f1FC = table, f204 count
// (bits 16-29) and running bit; the first entry plays with chooser 0x535894 unless it is final.
void Seq_start_535A38(World& w, int pid, Angle heading, std::uint32_t table, std::int32_t count) {
    PlayerState& p = P(w, pid);
    auto f = static_cast<std::uint32_t>(p.f204);
    f = (f & 0x3fffffffu & 0xffffu) | (f & 0xc0000000u) | ((static_cast<std::uint32_t>(count) & 0x3fff) << 16);
    p.f1FC = static_cast<std::int32_t>(table);
    f |= 0x40000000u;
    p.timer200 = 0;
    f &= 0x7fff0000u;
    const std::uint32_t e = seqEntry(table);
    if (((e << 16) >> 27) & 2) {
        f = (f & 0xffff0000u) | 1;
        f |= 0x80000000u;
        p.f204 = static_cast<std::int32_t>(f);
        Anim_start(w, pid, static_cast<int>(e & 0x7ff), 0, heading);
    } else {
        p.f204 = static_cast<std::int32_t>(f);
        Anim_start(w, pid, static_cast<int>(e & 0x7ff), 0x535894, heading);
    }
}

namespace {

// 0x535894 (EDX next, EBX out; player = w.pl.current): advance the sequence. Entry bit 16 selects
// the timer half (lo16 = anim frames counted by the engine, hi16 = chooser calls); an entry plays
// again until its timer reaches e >> 17. Flags (bits 11-15): 2 = final entry, 1 = loop to start.
int seqChooser(World& w, int nextAnim, std::uint32_t* out) {
    PlayerState& p = P(w, w.pl.current);
    std::uint32_t addr = static_cast<std::uint32_t>(p.f1FC) + lo16(p.f204) * 4;
    std::uint32_t e = seqEntry(addr);
    if (!(e & 0x10000)) setHi16(p.timer200, hi16(p.timer200) + 1); // 0x535997
    for (;;) {
        const std::uint32_t t = (e & 0x10000) ? lo16(p.timer200) : hi16(p.timer200); // 0x5358C2
        if (t < (e >> 17)) {
            *out = 0x535894;
            return static_cast<int>(e & 0x7ff);
        }
        // 0x5358E5
        const std::uint32_t idx = (lo16(p.f204) + 1) & 0xffff;
        setLo16(p.f204, idx);
        const std::uint32_t cnt = (static_cast<std::uint32_t>(p.f204) << 2) >> 18;
        if (idx >= cnt) {
            p.f204 &= ~0x40000000;
            *out = 0;
            return nextAnim;
        }
        p.timer200 = 0;
        addr += 4;
        e = seqEntry(addr);
        const std::uint32_t fl = (e << 16) >> 27;
        if (fl & 2) {
            const std::uint32_t idx2 = (lo16(p.f204) + 1) & 0xffff;
            setLo16(p.f204, idx2);
            if (idx2 >= ((static_cast<std::uint32_t>(p.f204) << 2) >> 18)) p.f204 &= ~0x40000000;
            p.f204 = static_cast<std::int32_t>(static_cast<std::uint32_t>(p.f204) | 0x80000000u);
            *out = 0;
            return static_cast<int>(e & 0x7ff);
        }
        if (fl & 1) { // 0x535A1B: loop
            addr = static_cast<std::uint32_t>(p.f1FC);
            e = seqEntry(addr);
            setLo16(p.f204, 0);
        }
    }
}

// 0x537F98 (EBX out; player = w.pl.current): idle anim loop of State18C: replay the idle anim
// 0x59F340[i] while lo16(timer200) < hi16, then pick a new limit and play the follow-up.
int idleChooser(World& w, std::uint32_t* out) {
    PlayerState& p = P(w, w.pl.current);
    const std::uint32_t i = lo16(p.f204);
    if (lo16(p.timer200) < hi16(p.timer200)) {
        *out = 0x537f98;
        return kIdleAnims[i];
    }
    setHi16(p.timer200, ((w.rng.next() & 0xff) + 0xb4) & 0xffff);
    p.f204 &= ~0x40000000;
    setLo16(p.timer200, 0);
    *out = 0;
    return kIdleAnims[i + 1];
}

} // namespace

// Next-anim chooser call through AnimCtl::chooser (exe address).
int Player_animChooser(World& w, int pid, std::uint32_t chooser, int curAnim, int nextAnim,
                       std::uint32_t* nextChooser) {
    switch (chooser) {
    case 0x535894: return seqChooser(w, nextAnim, nextChooser);
    case 0x537f98: return idleChooser(w, nextChooser);
    case 0x53c62c: {
        std::int32_t o = 0;
        const int r = Keeper_nextAnimCb(w, curAnim, nextAnim, &o);
        *nextChooser = static_cast<std::uint32_t>(o);
        return r;
    }
    default: // unreachable: the exe stores no other chooser (every Anim_play/Anim_start EBX and every
             // chooser out-write in FIFA99.EXE is 0, 0x535894, 0x537F98 or 0x53C62C)
        (void)pid;
        *nextChooser = 0;
        return nextAnim;
    }
}

// 0x535B0C (EAX team, EDX z, EBX skip slot): player behind z (toward his own goal line) nearest
// to the far goal line by |line - z| + |x|; -1 none.
int Team_nearestToLine_535B0C(const World& w, int team, std::int32_t z, int skipSlot) {
    const TeamState& t = w.teams[static_cast<std::size_t>(team)];
    std::int32_t line = w.pitch.halfLength;
    if (t.side == 1) line = -line;
    int best = -1;
    std::uint32_t bestS = 0xffff;
    for (int i = 0; i < t.playerCount; ++i) {
        const int q = t.players[i];
        const PlayerState& o = P(w, q);
        if ((o.flags22c & 2) || skipSlot == o.curSlot || (o.flags22c & 1)) continue;
        if (((o.posI.z - z >= 0) ? 1 : 0) == t.side) continue;
        const auto s = static_cast<std::uint32_t>(iabs(line - o.posI.z) + iabs(o.posI.x));
        if (s < bestS) {
            best = q;
            bestS = s;
        }
    }
    return best;
}

// 0x5374D8 (EAX side, EDX dist, EBX out): out.xz = restart spot moved dist away from the goal of
// `side` (out.y untouched).
void Restart_spotFromGoal_5374D8(const World& w, int side, std::int32_t dist, Vec3& out) {
    const std::int32_t gz = side == 1 ? -w.pitch.halfLength : w.pitch.halfLength;
    const Vec3 v = w.ball.restartPos;
    offsetXZ(out, v, dist, atan2i(v.x, v.z - gz));
}

// 0x536774: penalty run-up anim pick (match.throwAnim 0..3, first available of 0x59F31C).
void Penalty_pickRunUp_536774(World& w) {
    w.match.throwAnim = static_cast<std::int32_t>(w.rng.next() & 3);
    w.match.f258 = 0;
    for (int k = 0; k < 4; ++k) {
        if (Anim_clipUnavailable(w, kCelebAnim[w.match.throwAnim])) break;
        w.match.throwAnim = (w.match.throwAnim + 1) & 3;
    }
}

// 0x557A24 (EDX dz, EBX speed, ECX alt): near a goal line (beyond halfLength - 0x50) walk further
// off at `speed`; 0 when not near a goal line.
int Player_walkOff_557A24(World& w, int pid, std::int32_t dz, std::int32_t speed, int alt) {
    PlayerState& p = P(w, pid);
    Vec3 v = p.posI;
    if (w.pitch.halfLength - 0x50 > v.z && -w.pitch.halfLength + 0x50 < v.z) return 0;
    p.speedCap = static_cast<Fixed>(static_cast<std::uint32_t>(speed) << 16);
    if (w.pitch.goalHalfWidth - 0x30 > v.x) v.x += static_cast<std::int32_t>((w.rng.next() & 3) << 4);
    v.z += v.z > 0 ? -dz : dz;
    if (alt) Motion_setTargetAlt(w, pid, v);
    else Player_moveToward_4BE804(w, pid, v);
    return 1;
}

// 0x4BF358 (EDX out): random relative kick vector into the opponent's half (x within +-thirdWidth/8).
void Player_randomPointInHalf_4BF358(World& w, int pid, KickVec& out) {
    const auto nx = static_cast<std::uint32_t>(w.pitch.thirdWidth >> 3);
    out.dx = static_cast<std::int32_t>(w.rng.next() % nx);
    if (w.rng.next() & 1) out.dx = -out.dx;
    out.dz = w.pitch.halfLength;
    out.dz += static_cast<std::int32_t>(w.rng.next() % static_cast<std::uint32_t>(w.pitch.halfLength));
    if (out.dz < w.pitch.thirdLength) out.dz = w.pitch.thirdLength;
    if (T(w, P(w, pid)).side == 1) out.dz = -out.dz;
    out.dist = octLength(out.dx, out.dz);
}

// ------------------------------------------------------------------------------------ handlers

// 0x180 0x535BEC: kick-off taker.
void State180(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    if (w.match.phase != 0) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    switch (p.stateSub2) { // jump table 0x535BD8
    case 0: { // 0x535C17
        w.match.throwAnim = Anim_clipUnavailable(w, 0x151) ? static_cast<std::int32_t>(w.rng.next() & 1) : 1;
        w.match.f258 = 0;
        ffOff(w, 3);
        Ball_takePossession(w, pid, 0);
        Motion_snapTo(w, pid, p.posI);
        if (p.ctrl == 0) Player_assignControlToReceiver(w, pid, -1);
        p.flags22c |= 0x80;
        Vec3 v = w.ball.restartPos;
        if (w.match.throwAnim == 0) {
            std::int32_t d = 0, a = 0;
            const int fr = Anim_contactInfo(w, 0x151, 0, 0, -0x64, 1, &d, &a);
            Kick_setAnimParams(w, pid, 0x151, fr, d, a);
            offsetXZ(v, w.ball.restartPos, d, a + (t.side == 0 ? 0x200 : 0));
            v.x += p.posI.x >= 0 ? 0x10 : -0x10;
        } else { // 0x535F4A
            v.x += p.posI.x >= 0 ? 0x20 : -0x20;
            v.z += t.side == 1 ? 6 : -6;
            w.match.throwAnim = 1;
        }
        Player_moveToward_4BE804(w, pid, v);
        nextSub(p);
        [[fallthrough]];
    }
    case 1: // 0x535D2A
        Player_assignControlToReceiver(w, pid, -1);
        if (p.targetDist > 0x10) return;
        nextSub(p);
        [[fallthrough]];
    case 2: // 0x535D4E
        if (w.match.throwAnim == 0 && w.match.f258 == 0) {
            if (timer(p) < 0xf) return;
            p.flags22c &= ~0x10u;
            p.headAim = t.side == 0 ? -0xff : 0x100;
            Seq_start_535A38(w, pid, p.angleBall - p.kick.heading, kSeqBase, 1);
            ++w.match.f258;
        }
        p.flags22c &= ~0x80u;
        if (w.match.flags278 & 0x20) return;
        nextSub(p);
        [[fallthrough]];
    case 3: { // 0x535DDC
        if (p.ctrl == 0 && timer(p) < 0x3c) return;
        Player_assignControlToReceiver(w, pid, -1);
        if (p.distBall > 0x84 && timer(p) < 0x78) return;
        if (p.ctrl != 0) { // 0x535FA1
            const ControllerState& c = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
            const std::uint32_t held = c.pressed & 0xc00;
            ffOff(w, 3);
            const std::uint32_t btn = (c.devType == 2 || c.devType == 3) ? 0x10 : c.devType == 4 ? 0x40 : 0x20;
            if ((c.released & btn) && held == 0) {
                p.stateTimer = 0;
                ++p.stateSub2; // the exe increments again at 0x535E16
            } else if (timer(p) <= 0x708 || w.opt.gameMode == 1) {
                return;
            }
        }
        nextSub(p); // 0x535E16
        [[fallthrough]];
    }
    case 4: { // 0x535E24 (Match_stamp 0x46C4E4 / 0x504FBC: audio only)
        const int r = Team_nearestPlayer_4523E4(w, p.posI, p.team, p.curSlot, nullptr);
        Player_assignControlToReceiver(w, r, -1);
        std::int32_t rel[4];
        if (r < 0) {
            Math_polarRecord(0x120, p.facing, rel);
        } else {
            rel[1] = P(w, r).posI.x - w.ball.posI.x;
            rel[2] = P(w, r).posI.z - w.ball.posI.z + (t.side == 1 ? -0x10 : 0x10);
            rel[0] = octLength(rel[1], rel[2]);
        }
        if (t.side == 1 && rel[2] > -4) rel[2] = -0x10;
        if (t.side == 0 && rel[2] < 4) rel[2] = 0x10;
        if (static_cast<std::uint32_t>(t.playerCount) > 1) w.ball.lastKicker = pid;
        Kick_planToPoint(w, pid, rel, nullptr, 8, -1, 0, 2, 0, 0, 2);
        Player_setState(w, pid, 0x300, 0, 0, false, true);
        return;
    }
    default: return;
    }
}

// 0x181 0x536064: kick-off partner: step beside the ball, then (AI) pass back toward a random
// spot's nearest team-mate and freeze the two nearest opponents for 120 frames.
void State181(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    const int ph = w.match.phase;
    if (ph != 0 && ph != 9) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    switch (p.stateSub2) { // jump table 0x536054
    case 0: { // 0x536088
        if (w.match.phase != 0) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        p.flags22c |= 0x80;
        Vec3 v = w.ball.restartPos;
        const int pw = t.possWinner;
        if (pw < 0) {
            if (timer(p) >= 0x258) {
                p.flags22c &= ~0x80u;
                Player_dropBallAndReset(w, pid);
            }
            return;
        }
        v.x = Motion_targetVec(w, pw).x > 0 ? -0x80 : 0x80;
        v.z = t.side == 1 ? 0x10 : -0x10;
        Player_moveToward_4BE804(w, pid, v);
        nextSub(p);
        return;
    }
    case 1: // 0x536152
        if (p.targetDist > 0x10) return;
        p.stateTimer = 0;
        p.flags22c &= ~0x80u;
        ++p.stateSub2;
        [[fallthrough]];
    case 2: // 0x53617C
        if (w.match.phase != 9) return;
        if (p.ctrl != 0) {
            Player_setState(w, pid, 0x309, 0, 0, false, true);
            return;
        }
        nextSub(p);
        [[fallthrough]];
    case 3: { // 0x53619E
        if (p.distBall > 0x40) Motion_setTargetAlt(w, pid, w.ball.posI);
        Ball_takePossession(w, pid, 0);
        const auto a = static_cast<std::uint32_t>(w.pitch.thirdLength >> 1);
        const std::int32_t b = w.pitch.thirdLength >> 2;
        Vec3 v;
        const std::int32_t z = b + static_cast<std::int32_t>(w.rng.next() % a);
        v.z = t.side == 1 ? z : -z;
        v.x = static_cast<std::int32_t>(w.rng.next() % static_cast<std::uint32_t>(w.pitch.width)) - w.pitch.halfWidth;
        v.y = 0;
        int r = Team_nearestPlayer_4523E4(w, v, p.team, 0, nullptr);
        if (r == pid) r = -1;
        t.f1e0 = r;
        if (r < 0) {
            Player_setState(w, pid, 0x308, 0, 0, false, true);
            return;
        }
        const OctDist d = octDistance(p.posI, P(w, r).posI);
        const int o = Team_nearestPlayer_4523E4(w, w.ball.posI, t.opponent, 0xff, nullptr);
        if (o >= 0 && P(w, o).ctrl == 0) {
            P(w, o).countdown = 0x78;
            Player_setState(w, o, 0, 0, 0, false, true);
            const int o2 = Team_nearestPlayer_4523E4(w, w.ball.posI, t.opponent, P(w, o).curSlot, nullptr);
            if (o2 >= 0 && P(w, o2).ctrl == 0) {
                P(w, o2).countdown = 0x78;
                Player_setState(w, o2, 0, 0, 0, false, true);
            }
        }
        const std::int32_t rel[3] = {d.dist, d.dx, d.dz};
        Kick_planToPoint(w, pid, rel, nullptr, d.dist >> 4, -1, 0, 2, 0, 0, 2);
        Player_setState(w, pid, 0x300, 0, 0, false, true);
        return;
    }
    default: return;
    }
}

// 0x185 0x5367E0 (also 0x189): penalty / shoot-out taker.
void State185(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    MatchState& m = w.match;
    const int k = Team_findKeeper(w, t.opponent); // ecx
    if ((m.phase != 9 && m.phase != m.restartType) || (m.flags278 & 4)) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (p.stateSub2 == 0) m.flags278 |= 0x4000;
    if (static_cast<std::uint32_t>(p.stateSub2) <= 6 && (m.flags278 & 0x2000)) ffOn(w, 2);
    switch (p.stateSub2) { // jump table 0x5367B8
    case 0: // 0x536855
        Penalty_pickRunUp_536774(w);
        m.setPieceArmed = 0;
        nextSub(p);
        [[fallthrough]];
    case 1: { // 0x53686E
        if (timer(p) < 0x3c && m.restartType == 6) return;
        Ball_takePossession(w, pid, 0);
        p.flags22c |= 0x80;
        Vec3 v = w.ball.restartPos;
        w.time.focus = pid;
        if (m.restartType == 6) {
            Vec3 u = v;
            u.z += u.z >= 0 ? -0x320 : 0x320;
            Motion_snapTo(w, pid, u);
        }
        std::int32_t off = 0, a = 0;
        if (m.throwAnim == 3) Anim_contactInfo(w, 1, 0, 0, -0x64, 1, &off, &a);
        else off = k59F30C[2 * m.throwAnim];
        v.z += w.ball.restartPos.z > 0 ? -off : off;
        Player_moveToward_4BE804(w, pid, v);
        kickReset(w, pid);
        // 0x4D4400: commentary, not simulated.
        nextSub(p);
        return;
    }
    case 2: // 0x53699D
        w.time.focus = pid;
        nextSub(p);
        return;
    case 3: { // 0x5369B5
        if (p.targetDist > 8) return;
        m.f258 = 1;
        const OctDist d = octDistance(p.posI, w.ball.restartPos);
        p.flags22c &= ~0x10u;
        Motion_faceVector(w, pid, d.dx, d.dz);
        nextSub(p);
        [[fallthrough]];
    }
    case 4: // 0x536A04
        if (!Motion_turnSettled(w, pid)) return;
        Seq_start_535A38(w, pid, p.facing, kSeqPtr[m.throwAnim], 3);
        nextSub(p);
        return;
    case 5: { // 0x536A4E
        if (m.throwAnim != 0 && Seq_status_535870(w, pid) == 0) return;
        Vec3 v = w.ball.restartPos;
        const std::int32_t off = k59F30C[2 * m.throwAnim + 1];
        v.z += w.ball.restartPos.z > 0 ? -off : off;
        v.x += (w.rng.next() & 1) ? (-off >> 1) : (off >> 1);
        p.flags22c &= ~0x10u;
        const OctDist d = octDistance(v, w.ball.restartPos);
        Motion_faceVector(w, pid, d.dx, d.dz);
        Player_moveToward_4BE804(w, pid, v);
        nextSub(p);
        [[fallthrough]];
    }
    case 6: // 0x536AF9
        p.speedCap = 0x14000;
        if (p.targetDist > 0x18) return;
        p.flags22c &= ~0x80u;
        m.flags278 &= ~0x4000u;
        if (m.flags278 & 0x80) {
            ffOff(w, 2);
            return;
        }
        if ((m.flags278 & 0x20) || timer(p) < 0x78) return;
        w.pl.spWait185 = m.elapsedSec + 0x1e;
        nextSub(p);
        [[fallthrough]];
    case 7: // 0x536B5D
        if (k >= 0 && (P(w, k).flags22c & 0x80)) return;
        m.setPieceArmed = 0;
        if (p.targetDist > 8) return;
        m.setPieceKind = 0;
        m.setPieceArmed = 1;
        nextSub(p);
        [[fallthrough]];
    case 8: // 0x536B9E
        p.flags22c |= 0x10;
        if (t.humans > 0) {
            if (m.phase == 6) {
                Input_giveControlToNth(w, m.so.round, pid);
            } else if (w.opt.gameMode == 1) {
                Input_giveControlToNth(w, Training_ctrl66741C(w), pid);
            } else if (p.state == 0x185 && m.victimCtrl != 0) {
                Player_transferControl(w, w.controllers[static_cast<std::size_t>(m.victimCtrl - 1)].player - 1, pid);
            } else if (p.ctrl == 0) {
                Input_giveControlToNth(w, t.f374++, pid);
            }
            ffOff(w, 3); // 0x536BD2
            if (m.setPieceArmed && static_cast<std::uint32_t>(m.elapsedSec) > static_cast<std::uint32_t>(w.pl.spWait185) &&
                w.opt.gameMode != 1) {
                m.setPieceArmed = 0;
                m.setPieceKind = 1;
            }
        }
        if (p.ctrl == 0 && timer(p) > 0xb4) m.setPieceArmed = 0;
        else if (m.setPieceArmed != 0) return;
        nextSub(p); // 0x536C2B
        [[fallthrough]];
    case 9: { // 0x536C39
        KickVec v{};
        std::int32_t b = 0, c = 0;
        Kick_aimSetPiece(w, pid, &v, &b, &c, p.ctrl != 0 ? m.setPieceKind : 7, 0x10000);
        m.penaltyTaker = pid;
        m.blockUntil = w.time.totalFrames + 0x5a;
        const std::int32_t rel[3] = {v.dist, v.dx, v.dz};
        Kick_planToPoint(w, pid, rel, nullptr, b, c, 0, 3, 2, 0, 3);
        Player_setState(w, pid, 0x300, 0, 0, false, true);
        return;
    }
    default: return;
    }
}

// 0x536D74: skip the penalty run-up to step 7 (stand at the side offset, facing restored).
void Penalty_skipRunUp_536D74(World& w, int pid) {
    PlayerState& p = P(w, pid);
    Vec3 v = w.ball.restartPos;
    std::int32_t off = k59F30C[2 * w.match.throwAnim + 1];
    v.z += w.ball.restartPos.z > 0 ? -off : off;
    if (p.posI.x > 0) off = -off;
    v.x += off >> 1;
    Player_moveToward_4BE804(w, pid, v);
    p.stateTimer = 0;
    p.stateSub2 = 7;
    p.flags22c &= ~0x10u;
}

// 0x182 0x536E04: throw-in taker.
void State182(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    MatchState& m = w.match;
    bool aiRun = false; // [esp+0x24]
    if ((m.phase != 9 && m.phase != m.restartType) || (m.flags278 & 4) || (m.flags278 & 0x80000)) {
        SetPiece_reset(w);
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (m.flags278 & 0x2000) ffOn(w, 2);
    reqBits(p, 0x11);
    Vec3 v;
    switch (p.stateSub2) { // jump table 0x536DF0
    case 0: // 0x536E85
        m.f258 = 0;
        m.flags278 |= 0x4000;
        Ball_takePossession(w, pid, 0);
        p.flags22c |= 0x80;
        v = w.ball.restartPos;
        v.x += w.ball.restartPos.x > 0 ? 0x40 : -0x40;
        Motion_setTarget(w, pid, v);
        nextSub(p);
        return;
    case 1: // 0x536F1F
        if (timer(p) < 0x3c) return;
        ffOn(w, 1);
        nextSub(p);
        return;
    case 2: // 0x536F47
        if (timer(p) < 0x3c) return;
        m.f258 = 1;
        v = w.ball.restartPos;
        v.x += w.ball.restartPos.x > 0 ? 0x40 : -0x40;
        Motion_setTarget(w, pid, v);
        Motion_snapTo(w, pid, v);
        w.time.focus = pid;
        Ball_placeAt(w, w.ball.restartPos, false, 0);
        p.flags22c &= ~0x10u;
        p.headAim = p.posI.x >= 0 ? -0x100 : 0x100;
        nextSub(p);
        return;
    case 3: // 0x536FEF
        if (timer(p) < 0x3c) return;
        SetPiece_begin(w, pid, p.posI.x >= 0 ? -0xff : 0x100, 1);
        setPossessed(w, 1);
        Ball_takePossession(w, pid, 1); // EDX = 1 kept through 0x475174
        dribblerIfPossessor(w, pid);
        ffOff(w, 1);
        m.flags278 &= ~0x4000u;
        p.flags22c &= ~0x80u;
        m.setPieceArmed = 1;
        m.setPieceKind = 0;
        w.pl.spWait182 = m.elapsedSec + 0x1e;
        nextSub(p);
        return;
    case 4: { // 0x5370A0
        if (m.flags278 & 0x20) return;
        if (m.setPieceMode == 2 && m.setPiecePartner >= 0) v = P(w, m.setPiecePartner).posI;
        else if (m.setPieceMode != 1) v = m.setPieceAim;
        if (m.setPieceMode != 1) { // 0x5370D7
            Motion_warpTo(w, pid, &p.posI);
            Motion_faceVector(w, pid, v.x - p.posI.x, v.z - p.posI.z);
        }
        Ball_takePossession(w, pid, 1);
        dribblerIfPossessor(w, pid);
        if (m.flags278 & 8) { // 0x537253
            SetPiece_begin(w, pid, p.headAim, 1);
            setPossessed(w, 1);
            return;
        }
        if (p.ctrl == 0 && m.setPieceMode == 1) {
            const int r = Team_nearestToBall_4522FC(w, p.team, p.curSlot);
            if (static_cast<std::uint32_t>(Player_cachedDistance(w, pid, r)) >= 0x460 && timer(p) <= 0x12c) return;
            m.setPieceKind = 8; // 0x537171
            aiRun = true;
            if (t.side == 0 && p.posI.z > 0x3c0) {
                v.x = 0;
                v.z = (p.posI.z + w.pitch.halfLength) >> 1;
            } else if (t.side == 1 && p.posI.z < -0x3c0) {
                v.x = 0;
                v.z = (p.posI.z - w.pitch.halfLength) >> 1;
            } else {
                const auto r2 = static_cast<std::int32_t>(w.rng.next() % 0x2d0);
                v.x = p.posI.x >= 0 ? r2 : -r2;
                v.z = p.posI.z;
            }
        } else { // 0x5372F7
            ffOff(w, 3);
            if (m.setPieceMode == 1) SetPiece_updatePower(w, pid);
            if (t.humans > 0 && static_cast<std::uint32_t>(m.elapsedSec) > static_cast<std::uint32_t>(w.pl.spWait182) &&
                w.opt.gameMode != 1) { // 0x537345: auto-take
                const int poss = w.ball.possessor;
                if (m.setPieceMode == 2) {
                    Input_action56(w, m.setPiecePartner, 0);
                } else if (m.setPieceMode == 1) { // 0x5373F0
                    KickVec kv{};
                    std::int32_t lift = 0;
                    Fixed spin = 0;
                    w.pl.sp.power = 0x8000;
                    SetPiece_computeKick(w, &kv, &lift, &spin);
                    lift += 0x72;
                    const std::int32_t rel[3] = {kv.dist, kv.dx, kv.dz};
                    Kick_planToPoint(w, poss, rel, nullptr, lift, -1, 0, 9, 0xb, 0, 0);
                } else { // 0x53735D
                    const OctDist d = octDistance(w.ball.posI, m.setPieceAim);
                    std::int32_t dist = d.dist;
                    if (dist < 0xa0) dist = 0xa0;
                    else if (dist > 0x780) dist = 0x780;
                    std::int32_t rel[4];
                    Math_polarRecord(dist, atan2i(d.dx, d.dz), rel);
                    Kick_planToPoint(w, poss, rel, nullptr, 0x72 + (rel[0] >> 3), -1, 0, 9, 0xb, 0, 0);
                }
            } else if (m.setPieceArmed != 0) {
                return;
            }
        }
        // 0x5371BA
        SetPiece_reset(w);
        m.f258 = 2;
        w.time.timer65f1f8 = 0x12c;
        w.ball.lastKicker = w.ball.possessor;
        m.restartTaker = pid;
        if (!aiRun) {
            const int poss = w.ball.possessor;
            w.ball.dribbler = poss;
            Player_setState(w, poss, 0x300, 0, 0, false, true);
            return;
        }
        { // 0x537457
            const OctDist d = octDistance(p.posI, v);
            std::int32_t dist = d.dist;
            if (dist < 0x50) dist = 0x50;
            else if (dist > 0x460) dist = 0x460;
            std::int32_t rel[4];
            Math_polarRecord(dist, atan2i(d.dx, d.dz), rel);
            Motion_faceVector(w, pid, rel[1], rel[2]);
            TeamAI_onBallWon_4DC614(w, pid, 0xb);
        }
        return;
    }
    default: return;
    }
}

// 0x183 0x53756C (also 0x184, 0x18B): corner / free-kick / indirect taker.
void State183(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    MatchState& m = w.match;
    if ((m.phase != 9 && m.phase != m.restartType) || (m.flags278 & 4)) {
        SetPiece_reset(w);
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (m.flags278 & 0x2000) ffOn(w, 2);
    Vec3& spot = w.pl.takerSpot;
    const auto cornerHead = [&] { return m.phase == 2 ? static_cast<std::int32_t>(kTakerHead[4 + m.f258]) : p.angleBall; };
    switch (p.stateSub2) { // jump table 0x537528
    case 0: { // 0x5375C3
        m.flags278 |= 0x4000;
        m.setPieceTaker = pid;
        if (t.humans > 0) {
            if (p.state == 0x184 && m.victimCtrl != 0 && w.opt.gameMode != 1)
                Player_transferControl(w, w.controllers[static_cast<std::size_t>(m.victimCtrl - 1)].player - 1, pid);
            else
                Input_giveControlToNth(w, t.f374++, pid);
        }
        Ball_takePossession(w, pid, 0);
        p.flags22c |= 0x80;
        m.f258 = 0;
        spot = w.ball.restartPos;
        if (spot.z > 0) m.f258 = spot.x > 0 ? 0 : 1;
        else m.f258 = spot.x > 0 ? 3 : 2;
        if (m.phase == 2) {
            offsetXZ(spot, w.ball.restartPos, 0xa0, kTakerHead[m.f258]);
            spot.y = w.ball.restartPos.y;
        } else {
            Restart_spotFromGoal_5374D8(w, t.side, 0xa0, spot);
        }
        Player_moveToward_4BE804(w, pid, spot);
        Ball_takePossession(w, pid, 0);
        nextSub(p);
        return;
    }
    case 1: // 0x53773C
        if (timer(p) < 0x3c) return;
        ffOn(w, 1);
        nextSub(p);
        return;
    case 2: // 0x537764
        if (timer(p) < 0x3c) return;
        Motion_snapTo(w, pid, spot);
        p.flags22c &= ~0x10u;
        p.headAim = cornerHead();
        Ball_placeAt(w, w.ball.restartPos, false, 0);
        w.time.focus = pid;
        nextSub(p);
        return;
    case 3: // 0x5377C9
        if (timer(p) < 0x3c) return;
        if (p.slot == 0) Team_resetPlayersPeriod(w, p.team, 0);
        m.flags278 &= ~0x4000u;
        ffOff(w, 1);
        p.flags22c &= ~0x10u;
        p.headAim = cornerHead();
        // phase 5: 0x4D444C commentary, not simulated.
        nextSub(p);
        return;
    case 4: // 0x537860
        if ((p.ctrl != 0 || m.phase != 2) && timer(p) < 0x28) return;
        p.flags22c &= ~0x80u;
        if (m.flags278 & 0x20) return;
        SetPiece_begin(w, pid, cornerHead(), 1);
        m.setPieceArmed = 1;
        m.setPieceKind = 0;
        w.pl.spWait183 = m.elapsedSec + 0x1e;
        nextSub(p);
        return;
    case 5: { // 0x5378F6
        if (p.ctrl == 0 && m.phase == 2 && timer(p) < (w.ball.restartPos.x > 0 ? 0x8cu : 0xc8u)) return;
        if (m.flags278 & 8) {
            SetPiece_begin(w, pid, p.headAim, 1);
            return;
        }
        if (p.ctrl == 0 && m.setPieceMode == 1) {
            if (timer(p) <= 0x3c) return;
            int recv = -1; // ebx
            bool kind8 = false;
            if (m.phase == 2) {
                m.setPieceKind = 0x11;
            } else if (m.phase == 5 && iabs(w.ball.posI.z) > w.pitch.halfLength - 0x240) {
                m.setPieceKind = 0x11;
            } else if (m.phase == 5 &&
                       ((p.posI.z + w.pitch.thirdLineZ[t.side] >= 0) ? 1 : 0) != t.side &&
                       iabs(w.ball.posI.x) <= (w.pitch.halfWidth >> 2) * 3) {
                const std::int32_t room = w.pitch.halfLength - iabs(w.ball.posI.z);
                const std::uint32_t r = w.rng.next() % 0x2d0;
                if (static_cast<std::uint32_t>(room - 0x3c0) >= r) m.setPieceKind = 0xa;
                else kind8 = true;
            } else {
                kind8 = true;
            }
            if (kind8) { // 0x537ACB
                m.setPieceKind = 8;
                recv = Team_nearestToLine_535B0C(w, p.team, w.ball.posI.z, p.curSlot);
                if (recv < 0) m.setPieceKind = 0xa;
            }
            KickVec kv{};
            switch (m.setPieceKind) { // 0x537966, table 0x537544
            case 8: { // 0x53797E
                spot = P(w, recv).posI;
                const OctDist d = octDistance(p.posI, spot);
                std::int32_t vec[4] = {d.dist, d.dx, d.dz, 0};
                if (w.rng.next() & 1) {
                    const std::int32_t spin = 1000 * static_cast<std::int32_t>((w.rng.next() & 3) + 3);
                    const std::int32_t lift = d.dist / static_cast<std::int32_t>((w.rng.next() & 7) + 4);
                    Kick_planToPoint(w, pid, vec, nullptr, lift, -1, spin, 1, 1, 0, 2);
                } else { // 0x537CB9
                    Kick_plan(w, pid, vec, 0, 1, &spot, 0, 0, 0, 1, 1, 0, 2);
                }
                break;
            }
            case 9: { // 0x537CE3
                std::int32_t lift = 0;
                Fixed spin = 0;
                SetPiece_computeKick(w, &kv, &lift, &spin);
                const std::int32_t rel[3] = {kv.dist, kv.dx, kv.dz};
                Kick_planToPoint(w, pid, rel, nullptr, lift, -1, spin, 3, 2, 0, 3);
                break;
            }
            case 0xa: { // 0x537D16
                const PlayerAttr& at = A(w, p);
                const int i = (at.at(0x2b) + at.at(0x37)) >> 1;
                const std::uint32_t r1 = w.rng.next();
                const std::uint32_t r2 = w.rng.next();
                const std::int32_t e = static_cast<std::int32_t>((r1 & 3) + (r2 & 3) + (w.rng.next() & 1));
                std::int32_t spin = (k5906D0[i] + e) * 1000;
                if (w.rng.next() & 1) spin = -spin;
                std::int32_t lift = 0, y = 0;
                Kick_aimSetPiece(w, pid, &kv, &lift, &y, 6, 0x4ccc);
                Vec3 tgt = w.ball.posI;
                tgt.x += kv.dx;
                tgt.z += kv.dz;
                const std::int32_t rel[3] = {kv.dist, kv.dx, kv.dz};
                Kick_planToPoint(w, pid, rel, &tgt, lift, y, spin, 3, 2, 0, 0);
                break;
            }
            case 0x11: { // 0x537BC6
                const auto r1 = static_cast<std::int32_t>(w.rng.next() % 0x3c0);
                const auto r2 = static_cast<std::int32_t>(w.rng.next() & 0x3ff);
                const std::int32_t depth = r1 + 0x140;
                spot.x = w.pitch.goalHalfWidth - r2;
                if (p.posI.x < 0) spot.x = -spot.x;
                spot.z = w.pitch.halfLength - depth;
                if (t.side == 1) spot.z = -spot.z;
                const OctDist d = octDistance(p.posI, spot);
                const std::uint32_t q1 = w.rng.next();
                const std::uint32_t q2 = w.rng.next();
                const std::uint32_t e = (q1 & 3) + (q2 & 3) + (w.rng.next() & 1);
                const auto spin = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>((depth - 0x140) * static_cast<std::int32_t>(1000 * e)) / 0x320);
                const std::int32_t lift = d.dist / static_cast<std::int32_t>((w.rng.next() & 7) + 6);
                const std::int32_t rel[3] = {d.dist, d.dx, d.dz};
                Kick_planToPoint(w, pid, rel, nullptr, lift, -1, spin, 1, 1, 0, 2);
                break;
            }
            default: break; // 0x0B..0x10: 0x537A0B
            }
        } else { // 0x537DEB
            ffOff(w, 3);
            if (m.setPieceMode == 1) SetPiece_updatePower(w, pid);
            else SetPiece_updateHuman(w, pid);
            if (t.humans > 0 && static_cast<std::uint32_t>(m.elapsedSec) > static_cast<std::uint32_t>(w.pl.spWait183) &&
                w.opt.gameMode != 1) { // 0x537E42 auto-take
                const int poss = w.ball.possessor;
                std::int32_t rel[4] = {0, 0, 0, 0};
                std::int32_t lift = 0;
                if (m.setPieceMode == 2) {
                    const int partner = m.setPiecePartner;
                    Player_transferControl(w, partner, poss);
                    KickVec kv{};
                    Kick_aimTo(w, poss, &kv, 0, 0, 0, partner);
                    T(w, P(w, partner)).f1e0 = partner;
                    Player_setState(w, partner, 0x30b, 0, 0, false, false);
                    std::int32_t dist = kv.dist;
                    if (dist < 0x40) dist = 0x40;
                    else if (dist > 0xb40) dist = 0xb40;
                    Math_polarRecord(dist, atan2i(kv.dx, kv.dz), rel);
                    lift = rel[0] / 6;
                } else if (m.setPieceMode == 1) { // 0x537F52
                    KickVec kv{};
                    Fixed spin = 0;
                    w.pl.sp.power = 0x8000;
                    SetPiece_computeKick(w, &kv, &lift, &spin);
                    lift += 0x72;
                    rel[0] = kv.dist;
                    rel[1] = kv.dx;
                    rel[2] = kv.dz;
                } else {
                    const OctDist d = octDistance(w.ball.posI, m.setPieceAim);
                    std::int32_t dist = d.dist;
                    if (dist < 0x40) dist = 0x40;
                    else if (dist > 0xb40) dist = 0xb40;
                    Math_polarRecord(dist, atan2i(d.dx, d.dz), rel);
                    lift = rel[0] / 6;
                }
                Kick_planToPoint(w, poss, rel, nullptr, lift, -1, 0, 1, 1, 0, 2); // 0x537EDD
            } else if (m.setPieceArmed != 0) {
                return;
            }
        }
        SetPiece_reset(w); // 0x537A0B
        w.ball.lastKicker = w.ball.possessor;
        nextSub(p);
        [[fallthrough]];
    }
    case 6: { // 0x537A33 (0x4D539C / 0x4D458C: commentary)
        const int poss = w.ball.possessor;
        if (m.phase == 2) w.time.timer65f1f8 = 0x12c;
        if (P(w, poss).slot == 0) Keeper_release_4BF29C(w, poss);
        m.blockUntil = w.time.totalFrames + 0x5a;
        Player_setState(w, poss, 0x300, 0, 0, false, true);
        return;
    }
    default: return;
    }
}

// 0x186 0x53636C: substituted player walks off to the entry point, is warped to the touchline and
// the injured are replaced (0x49F684), then walks back in.
void State186(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    reqBits(p, 0x100);
    const Vec3 entry{w.pitch.entryPos[t.side][0], w.pitch.entryPos[t.side][1], w.pitch.entryPos[t.side][2]};
    switch (p.stateSub2) { // jump table 0x536358
    case 0: nextSub(p); [[fallthrough]];
    case 1: // 0x5363C1
        if (SeqTrigger_busy(w, 0xd)) return;
        p.flags22c |= 0x100;
        if (p.flags22c & 0x40) w.pl.subWalkPos = {-w.pitch.halfWidth + 0xa0, 0, p.posI.z};
        else w.pl.subWalkPos = p.posI;
        p.flags22c &= ~0x10u;
        Motion_facePoint(w, pid, entry);
        Player_moveToward_4BE804(w, pid, entry);
        nextSub(p);
        [[fallthrough]];
    case 2: // 0x536457
        if (p.targetDist > 0x10 && SeqTrigger_busy(w, 0xc)) return;
        Motion_warpTo(w, pid, &entry);
        Team_removeInjured_49F684(w, t.index);
        Player_clearArrivalCb(w, pid);
        Anim_start(w, pid, 0xeb, 0, p.facingDesired);
        p.flags22c &= ~0x41u;
        Motion_facePoint(w, pid, w.pl.subWalkPos);
        Player_moveToward_4BE804(w, pid, w.pl.subWalkPos);
        nextSub(p);
        [[fallthrough]];
    case 3: // 0x5364F7
        if (SeqTrigger_busy(w, 0xe)) return;
        nextSub(p);
        [[fallthrough]];
    case 4: // 0x53651F
        if (SeqTrigger_busy(w, 0xf)) return;
        p.flags22c &= ~0x100u;
        w.match.flags278 &= ~4u;
        Player_setState(w, pid, 0, 0, 0, false, true);
        return;
    default: return;
    }
}

// 0x187 0x5365D0: substitution without walking (the injured replacement only).
void State187(World& w, int pid) {
    PlayerState& p = P(w, pid);
    switch (p.stateSub2) { // jump table 0x5365BC
    case 0: nextSub(p); [[fallthrough]];
    case 1: // 0x5365FE
        if (SeqTrigger_busy(w, 0xd)) return;
        p.stateTimer = 0;
        p.flags22c |= 0x100;
        ++p.stateSub2;
        [[fallthrough]];
    case 2: // 0x53662A
        if (SeqTrigger_busy(w, 0xc)) return;
        Team_removeInjured_49F684(w, T(w, p).index);
        Player_clearArrivalCb(w, pid);
        p.stateTimer = 0;
        p.flags22c &= ~0x41u;
        ++p.stateSub2;
        [[fallthrough]];
    case 3: // 0x53667C
        if (SeqTrigger_busy(w, 0xe)) return;
        nextSub(p);
        [[fallthrough]];
    case 4: // 0x5366A6
        if (SeqTrigger_busy(w, 0xf)) return;
        p.flags22c &= ~0x100u;
        w.match.flags278 &= ~4u;
        Player_setState(w, pid, 0, 0, 0, false, true);
        return;
    default: return;
    }
}

// 0x18C 0x538040: waiting during a restart: face the spot, idle anim loop (chooser 0x537F98), then
// a reaction anim when play resumes (0xEA near the ball after idle 0x168, else 0x167).
void State18C(World& w, int pid) {
    PlayerState& p = P(w, pid);
    if (w.match.flags278 & 4) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    switch (p.stateSub2) { // jump table 0x538030
    case 0: // 0x53805E
        p.flags22c |= 0x80;
        if ((p.moveFlags & 1) || p.targetDist > 8) return;
        p.flags22c &= ~0x10u;
        Motion_facePoint(w, pid, w.ball.restartPos);
        nextSub(p);
        return;
    case 1: // 0x5380C2
        if (!Motion_turnSettled(w, pid)) return;
        Anim_start(w, pid, 0x169, 0, p.facing);
        setHi16(p.timer200, ((w.rng.next() & 0xff) + 0x78) & 0xffff);
        nextSub(p);
        p.flags22c &= ~0x80u;
        setLo16(p.timer200, 0);
        p.f204 &= ~0x40000000;
        return;
    case 2: { // 0x53814C
        if (lo16(p.timer200) > hi16(p.timer200) && !(p.f204 & 0x40000000)) {
            setHi16(p.timer200, ((w.rng.next() & 0x7f) + 0x2d) & 0xffff);
            setLo16(p.timer200, 0);
            setLo16(p.f204, (((w.rng.next() & 7) * 3) + 1) & 0xffff);
            p.f204 |= 0x40000000;
            Anim_start(w, pid, kIdleAnims[lo16(p.f204) - 1], 0x537f98, p.facing);
        }
        if (w.match.phase != 9) return; // 0x538204
        if (p.f204 & 0x40000000) {
            setHi16(p.timer200, 1);
            p.f204 &= ~0x40000000;
        }
        int anim = 0x167;
        if (p.distBall < 0xa0 && (w.rng.next() & 4) && Player_animId(w, pid) == 0x168) anim = 0xea;
        Anim_start(w, pid, anim, 0, p.facing);
        p.flags22c &= ~4u;
        nextSub(p);
        return;
    }
    case 3: // 0x538286
        if (timer(p) < 5 && !(p.anim.flags() & 2)) return;
        Player_dropBallAndReset(w, pid);
        return;
    default: return;
    }
}

// 0x188 0x5382C4: goal-kick taker.
void State188(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    MatchState& m = w.match;
    if ((m.phase != 9 && m.phase != 3) || (m.flags278 & 4)) {
        SetPiece_reset(w);
        Player_dropBallAndReset(w, pid);
        return;
    }
    const Angle kickDir = w.ball.restartPos.z > 0 ? 0x200 : 0;
    switch (p.stateSub2) { // jump table 0x5382A8
    case 0: // 0x5382F7
        m.f2e8[5] = 0;
        m.throwAnim = 0;
        p.stateTimer = 0;
        p.flags22c |= 0x80;
        ++p.stateSub2;
        return;
    case 1: // 0x53833F (0x4D53B0: commentary)
        if (timer(p) < 0x3c) return;
        m.flags278 |= 0x4000;
        Team_resetPlayersPeriod(w, p.team, 0);
        ffOn(w, 1);
        nextSub(p);
        return;
    case 2: { // 0x538389
        if (timer(p) < 0x3c) return;
        if (t.humans > 0) Input_giveControlToNth(w, t.f374++, pid);
        Vec3 v = w.ball.restartPos;
        Ball_takePossession(w, pid, 0);
        Ball_placeAt(w, w.ball.restartPos, false, 0);
        p.speedCap = 0x28000;
        w.time.focus = pid;
        v.z += w.ball.restartPos.z > 0 ? 0xa0 : -0xa0;
        v.x += w.ball.restartPos.z > 0 ? 0x8c : -0x8c;
        reqBits(p, 1);
        Player_moveToward_4BE804(w, pid, v);
        Motion_dirToTarget(w, pid);
        nextSub(p);
        return;
    }
    case 3: { // 0x53848C
        if (p.targetDist > 0x10) {
            p.speedCap = 0x28000;
            reqBits(p, 1);
            return;
        }
        p.speedCap = 0x28000;
        p.flags22c &= ~0x10u;
        const OctDist d = octDistance(p.posI, w.ball.restartPos);
        Motion_faceVector(w, pid, d.dx, d.dz);
        nextSub(p);
        return;
    }
    case 4: { // 0x53852C
        p.flags22c &= ~0x80u;
        const OctDist d = octDistance(p.posI, w.ball.restartPos);
        Motion_faceVector(w, pid, d.dx, d.dz);
        nextSub(p);
        return;
    }
    case 5: // 0x53857A
        if (!Motion_turnSettled(w, pid)) return;
        m.flags278 &= ~0x4000u;
        ffOff(w, 3);
        if (m.flags278 & 0x20) return;
        SetPiece_begin(w, pid, kickDir, 1);
        m.setPieceKind = 0;
        m.setPieceArmed = 1;
        w.pl.spWait188 = m.elapsedSec + 0x1e;
        nextSub(p);
        return;
    case 6: { // 0x538607
        if (m.flags278 & 8) {
            SetPiece_begin(w, pid, kickDir, 1);
            return;
        }
        KickVec kv{};
        if (p.ctrl == 0 && m.setPieceMode == 1) {
            m.setPieceKind = 8;
            Player_randomPointInHalf_4BF358(w, pid, kv);
            const std::int32_t rel[3] = {kv.dist, kv.dx, kv.dz};
            Kick_planToPoint(w, pid, rel, nullptr, kv.dist >> 3, -1, 0, 1, 1, 0, 0);
        } else { // 0x5386D4
            if (m.setPieceMode == 1) SetPiece_updatePower(w, pid);
            else SetPiece_updateHuman(w, pid);
            if (t.humans > 0 && static_cast<std::uint32_t>(m.elapsedSec) > static_cast<std::uint32_t>(w.pl.spWait188) &&
                w.opt.gameMode != 1) {
                Player_randomPointInHalf_4BF358(w, pid, kv);
                const std::int32_t rel[3] = {kv.dist, kv.dx, kv.dz};
                Kick_planToPoint(w, pid, rel, nullptr, kv.dist >> 3, -1, 0, 1, 1, 0, 2);
            } else if (m.setPieceArmed != 0) {
                return;
            }
        }
        // 0x538687 (0x505084: audio)
        SetPiece_reset(w);
        const int poss = w.ball.possessor;
        Keeper_release_4BF29C(w, poss);
        Player_setState(w, poss, 0x300, 0, 0, false, true);
        return;
    }
    default: return;
    }
}

// 0x18D 0x557AD4: walk off the pitch (end of period / shoot-out), then back to the slot position.
void State18D(World& w, int pid) {
    PlayerState& p = P(w, pid);
    Vec3 v{}; // 0x557A00
    reqBits(p, 0x4000001);
    switch (p.stateSub2) { // jump table 0x557AC0
    case 0: // 0x557B16
        v = p.posI;
        if (w.match.periodType == 3) {
            v.z += v.z > 0 ? -0x50 : 0x50;
        } else {
            v.x += v.x > 0 ? -0x20 : 0x20;
            v.z += v.z > 0 ? 0x60 : -0x60;
        }
        p.flags22c |= 0x10;
        p.speedCap = 0x20000;
        p.flags22c &= ~0x20u;
        Player_moveToward_4BE804(w, pid, v);
        nextSub(p);
        return;
    case 1: // 0x557BB6
        if (w.match.periodType != 3) {
            Player_walkOff_557A24(w, pid, 0xa0, 3, 0);
            if (p.targetDist < 0x10) {
                Formation_attackParticipation(w, 0x4cfa18, pid, v, -1, 0);
                Player_moveToward_4BE804(w, pid, v);
            }
        }
        if (w.match.flags278 & 0x20) return;
        nextSub(p);
        [[fallthrough]];
    case 2: // 0x557BD6
        if (!CutScene_playerReady(w, pid)) return;
        nextSub(p);
        [[fallthrough]];
    case 3: // 0x557BEF
        if (!(w.match.scriptState & 1) && !(w.match.flags278 & 0x80)) return;
        nextSub(p);
        return;
    case 4: // 0x557C5E
        Formation_evalSlotCallback(w, w.match.periodType == 3 ? 6 : 0, pid, v, -1, 0, 1);
        Motion_warpTo(w, pid, &v);
        Player_dropBallAndReset(w, pid);
        return;
    default: return;
    }
}

// 0x18A 0x557CF0 (also 0x18E, 0x18F): cut-scene placement: warp to the phase's slot position (or
// park far off-pitch at 0x27100), wait for the script, then stand still.
void State18A(World& w, int pid) {
    PlayerState& p = P(w, pid);
    reqBits(p, 0x4000001);
    switch (p.stateSub2) { // jump table 0x557CB4
    case 0: // 0x557D2C (the 0xC && 0x15 test at 0x557D33 can never pass)
        p.flags22c &= ~0x20u;
        p.speedCap = 0x20000;
        if (w.match.flags278 & 0x20) return;
        nextSub(p);
        [[fallthrough]];
    case 1: // 0x557D64
        p.flags22c &= ~0x20u;
        if (w.match.cutScene == 0) {
            p.stateSub2 = 3;
            p.stateTimer = 0;
            return;
        }
        p.speedCap = w.match.phase == 0xc ? 0 : 0x20000;
        if (CutScene_playerReady(w, pid)) nextSub(p);
        return;
    case 2: // 0x557DCD
        if (!(w.match.scriptState & 1) && !(w.match.flags278 & 0x80)) return;
        nextSub(p);
        return;
    case 3: { // 0x557DF7
        if (w.match.flags278 & 0x20) return;
        if (w.opt.presentationMode == 0 && CutScene_active663420(w) != 0) {
            nextSub(p);
            return;
        }
        Vec3 v{}; // 0x557A0C
        const int ph = w.match.phase;
        int cb;
        bool park = false;
        if (ph >= 0xf) {
            if (ph <= 0x11 || ph == 0x15) park = true;
            else cb = ph == 0x14 ? 3 : ph;
        } else if (ph >= 0xa) {
            if (ph == 0xa) cb = 5;
            else if (ph == 0xc) cb = 0;
            else if (ph == 0xe) park = true;
            else cb = ph;
        } else {
            cb = ph == 3 ? 3 : ph;
        }
        if (park) v = {0x27100, 0, 0x27100};
        else Formation_evalSlotCallback(w, cb, pid, v, -1, 0, 1);
        Motion_warpTo(w, pid, &v);
        p.stateSub2 = 6;
        p.stateTimer = 0;
        return;
    }
    case 4: // 0x557EFF
        if (!CutScene_playerReady(w, pid)) return;
        nextSub(p);
        [[fallthrough]];
    case 5: { // 0x557F1C
        if (!(w.match.scriptState & 1)) return;
        const Vec3 v{160000, 0, 160000}; // 0x557A18
        Motion_warpTo(w, pid, &v);
        nextSub(p);
        return;
    }
    case 6: { // 0x557F5E, table 0x557CD0 [phase - 0xE]
        const int ph = w.match.phase;
        if (ph == 0xe || ph == 0xf || ph == 0x10 || ph == 0x11 || ph == 0x15) {
            Anim_start(w, pid, 0x166, 0, p.facingDesired);
            p.speedCap = 0;
            Motion_setTarget(w, pid, p.posI);
        } else {
            Player_dropBallAndReset(w, pid);
        }
        return;
    }
    default: return;
    }
}

// ------------------------------------------------------- team runs 0x380-0x383 (0x4DA6B4..)
// Team run block TeamState::w37c (+0x37C): [1] +0x380 trap line, [2] +0x384 runners, [6] +0x394
// overlap runner, [7] +0x398 wide side, [11] +0x3A8 wide runner, [12] +0x3AC press target,
// [13] +0x3B0 press player, [14] +0x3B4 pressers, [15] +0x3B8 flags (player ids, -1 none).

namespace {
// 0x4DA90C / 0x4DAFC0 common entry: the run ends (team mode cleared, player reset) when the
// player is the one-touch player / ball winner, or the opponent / he himself has the ball, or
// the team mode is no longer `mode` or he is human-controlled. Execution continues afterwards.
void runEntry(World& w, int pid, int mode) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    bool stop = pid == w.match.player2a0 || pid == t.possWinner;
    if (!stop) {
        const int poss = w.ball.possessor;
        if (poss >= 0 && (P(w, poss).team == t.opponent || poss == pid)) stop = true;
        else if (t.f378 != mode || p.ctrl != 0) stop = true;
    }
    if (stop) {
        Team_clearPressFlags(w, p.team);
        t.f378 = 0;
        Player_dropBallAndReset(w, pid);
    }
}
// Run line: side 1 min(0, ball z, opponent offside-line z), side 0 max(...).
std::int32_t runLine(const World& w, const TeamState& t) {
    const std::int32_t f = P(w, w.teams[static_cast<std::size_t>(t.opponent)].f1bc).posI.z;
    std::int32_t z = 0;
    if (t.side == 1) {
        if (z >= w.ball.posI.z) z = w.ball.posI.z;
        if (z >= f) z = f;
    } else {
        if (z <= w.ball.posI.z) z = w.ball.posI.z;
        if (z <= f) z = f;
    }
    return z;
}
// Sub 1 of both runs: run 0xA0 ahead along x = tx; end the run once 0xA0 past the line.
void runAhead(World& w, int pid, std::int32_t tx, std::int32_t line) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    const Vec3 v{tx, 0, p.posI.z + (t.side == 1 ? -0xa0 : 0xa0)};
    Player_moveToward_4BE804(w, pid, v);
    p.speedCap = 0x50000;
    const std::int32_t e = p.posI.z - line;
    if (iabs(e) < 0xf0) Player_requestSprint(w, pid);
    if (t.side == 1 ? e < -0xa0 : e > 0xa0) {
        Team_clearPressFlags(w, p.team);
        t.f378 = 0;
        Player_dropBallAndReset(w, pid);
    }
}
// Sub 0 of both runs: head for x = tx, at most 0x3C0 - |dx| (>= 0xA0) toward the line.
bool runApproach(World& w, int pid, std::int32_t tx, std::int32_t line, std::int32_t done) {
    PlayerState& p = P(w, pid);
    const TeamState& t = T(w, p);
    const std::int32_t d = iabs(p.posI.x - tx);
    std::int32_t dd = 0x3c0 - d;
    if (dd < 0xa0) dd = 0xa0;
    Vec3 v{tx, 0, p.posI.z};
    if (t.side == 1) {
        v.z -= dd;
        if (line + 0xa0 > v.z) v.z = line + 0xa0;
    } else {
        v.z += dd;
        if (line - 0xa0 < v.z) v.z = line - 0xa0;
    }
    Player_moveToward_4BE804(w, pid, v);
    Player_requestSprint(w, pid);
    return d <= done;
}
// 0x4DA758 / 0x4DADE8: pick the uncontrolled outfield player nearest (|dx| + |dz|) to a corner
// point and start his run; mode 2 stores him in [6], mode 3 in [11] with the wide side in [7].
void startRun(World& w, int team, int mode, int state, int slot) {
    TeamState& t = w.teams[static_cast<std::size_t>(team)];
    if (static_cast<std::uint32_t>(w.teams[static_cast<std::size_t>(t.opponent)].playerCount) < 2) return;
    if (t.f378 == mode) {
        Team_clearPressFlags(w, team);
        t.f378 = 0;
        if (t.w37c[slot] >= 0) Player_dropBallAndReset(w, t.w37c[slot]);
    }
    std::int32_t z, x; // [esp+4], [esp+0xC]
    if (mode == 2) {
        z = t.side == 1 ? -w.pitch.halfLength : w.pitch.halfLength;
        x = w.ball.posI.x > 0 ? -w.pitch.halfWidth : w.pitch.halfWidth;
    } else {
        z = t.side == 1 ? w.pitch.halfLength : -w.pitch.halfLength;
        t.w37c[7] = w.ball.posI.x > 0 ? 1 : 0;
        x = w.ball.posI.x > 0 ? w.pitch.halfWidth : -w.pitch.halfWidth;
    }
    t.f378 = mode;
    t.w37c[0] = 0;
    t.w37c[15] = 0;
    int best = -1;
    std::int32_t bestD = 0xbb80;
    for (int i = 0; i < t.playerCount; ++i) {
        const int q = t.players[i];
        const PlayerState& o = P(w, q);
        if (o.ctrl != 0 || q == w.ball.possessor || o.slot == 0) continue;
        const std::int32_t d = iabs(o.posI.x - x) + iabs(o.posI.z - z);
        if (d < bestD) {
            bestD = d;
            best = q;
        }
    }
    if (best < 0) {
        Team_clearPressFlags(w, team);
        t.f378 = 0;
        return;
    }
    t.w37c[slot] = best;
    P(w, best).flags22c |= 0x200000;
    Player_setState(w, best, state, 0, 0, false, false);
}
} // namespace

// 0x4DA758 (EAX team): start an overlap run (team mode 2, state 0x381).
void Team_startOverlap_4DA758(World& w, int team) { startRun(w, team, 2, 0x381, 6); }
// 0x4DADE8 (EAX team): start a wide run (team mode 3, state 0x382).
void Team_startWideRun_4DADE8(World& w, int team) { startRun(w, team, 3, 0x382, 11); }

// 0x380 0x4DA6B4: offside-trap step-up: positioned like State004 but no deeper than the trap line.
void State380(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    const std::int32_t line = t.w37c[1];
    if (t.f378 != 1 || (t.w37c[15] & 1) || p.ctrl != 0) {
        --t.w37c[2];
        p.flags22c &= ~0x200000u;
        Player_dropBallAndReset(w, pid);
        return;
    }
    p.flags22c |= 0x200000;
    State004(w, pid);
    Vec3 v = Motion_targetVec(w, pid);
    if (t.side == 1 ? line > v.z : line < v.z) return;
    v.z = line;
    Player_requestSprint(w, pid);
    Player_moveToward_4BE804(w, pid, v);
}

// 0x381 0x4DA90C: overlap run up the middle, then ahead past the line.
void State381(World& w, int pid) {
    PlayerState& p = P(w, pid);
    runEntry(w, pid, 2);
    const std::int32_t line = runLine(w, T(w, p));
    switch (p.stateSub2) {
    case 0:
        if (!runApproach(w, pid, 0, line, 0xa0)) return;
        p.stateTimer = 0;
        ++p.stateSub2;
        [[fallthrough]];
    case 1: // 0x4DA9A5
        runAhead(w, pid, p.posI.x < w.ball.posI.x ? p.posI.x + 0xf0 : p.posI.x - 0xf0, line);
        return;
    default: return;
    }
}

// 0x382 0x4DAFC0: wide run along the touchline (x = +-(halfWidth - 0x80) by [7]).
void State382(World& w, int pid) {
    PlayerState& p = P(w, pid);
    runEntry(w, pid, 3);
    const TeamState& t = T(w, p);
    const std::int32_t line = runLine(w, t);
    std::int32_t tx = w.pitch.halfWidth - 0x80;
    if (t.w37c[7] == 0) tx = -tx;
    switch (p.stateSub2) {
    case 0:
        if (!runApproach(w, pid, tx, line, 0x50)) return;
        p.stateTimer = 0;
        ++p.stateSub2;
        [[fallthrough]];
    case 1: runAhead(w, pid, tx, line); return; // 0x4DB06E
    default: return;
    }
}

// 0x383 0x4DAC18: press the team's press target ([12]): follow his target point, sprint, tackle
// when close (0x4D19C8 kind 3).
void State383(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    const int tgt = t.w37c[12];
    bool quit = false;
    if (t.f378 != 4 || (t.w37c[15] & 1) || tgt < 0) {
        p.flags22c &= ~0x200000u;
        quit = true;
    }
    if (!(p.flags22c & 0x200000)) quit = true;
    const int pw = t.possWinner;
    if (pid == pw && w.ball.possessor < 0) {
        t.w37c[13] = pw;
        quit = true;
    }
    const auto end = [&] { // 0x4DAC97
        if (t.f378 == 4 && (p.flags22c & 0x200000)) {
            --t.w37c[14];
            p.flags22c &= ~0x200000u;
        }
    };
    if (quit) {
        end();
        Player_dropBallAndReset(w, pid);
        return;
    }
    Vec3 v = Motion_targetVec(w, tgt);
    v.z += t.side == 1 ? 0x60 : -0x60;
    Player_moveToward_4BE804(w, pid, v);
    const std::int32_t td = p.targetDist;
    if (td > 0x140 && (P(w, tgt).slot != 0 || !(w.ball.flags & 0x20))) Player_requestSprint(w, pid);
    const std::int32_t odds = Mark_tackleOdds(w, pid, tgt, p.distBall);
    if (tgt != w.ball.possessor) return;
    if (P(w, tgt).slot == 0 && (w.ball.flags & 0x20)) return;
    if (td >= 0xc0 || odds <= 0 || w.ball.posI.y >= 0x20) return;
    p.flags22c |= 0x200;
    if (Player_tryTackle(w, pid, 3)) {
        end();
        return;
    }
    p.flags22c &= ~0x200u;
}

} // namespace wc98::sim
