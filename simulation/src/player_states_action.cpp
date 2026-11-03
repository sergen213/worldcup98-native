// Player on-ball action states 0x300-0x30E and their helpers (owner SpStates.SpAction):
// handlers 0x53DF6C..0x543280 (table 0x589068) and 0x4E0474..0x4E0935, helpers 0x53DBC4..0x53DF4C,
// 0x53F6BC, 0x53F75C, 0x5404A8, 0x5412A4, 0x542EA0, 0x4DBAE4, 0x4DBC24, 0x4DBFB8, 0x4E073C.
// Ported instruction by instruction from FIFA99.EXE v1.1; see player_states.hpp for conventions.
// Sub-step = player+0x50 (stateSub2), frame timer = +0x54 (stateTimer).
#include "player_states.hpp"
#include "wc98/sim/anim.hpp"
#include "wc98/sim/sim.hpp"

namespace wc98::sim {
namespace {

int oppPossWinner(const World& w, int pid) { // [[player+0x20]+0x8]+0x1B0
    return w.teams[static_cast<std::size_t>(w.teams[static_cast<std::size_t>(P(w, pid).team)].opponent)].possWinner;
}

// kick request +0x198..+0x1A0: {len, dx, dz} of the pass (Rules_checkOffside EDX).
void checkOffside_W1(World& w, int pid) {
    const KickRequest& k = P(w, pid).kick;
    const std::int32_t d[3] = {k.vec.dist, k.vec.dx, k.vec.dz};
    Rules_checkOffside(w, pid, d);
}

// The exe's 0x4BB0D0 never writes out.y (stack garbage); every consumer below only reads x/z
// except Motion_setTargetAlt in State300 sub 3, where the exe passes that stale stack word.
// We keep base.y there (offsetInDirection) — untraced value, see report.
Vec3 untraced_offsetKeepY(const Vec3& base, Fixed scale, Angle a) { return offsetInDirection(base, scale, a); }

// State300 sub 2 via 0x53E2D2 -> 0x53E1BF: the exe reads [esp+0x28..0x3C] that this path never
// wrote (stale stack from earlier calls). Zero deltas make Anim_setDrift 0x4CD2F0 a no-op.
void untraced_staleTurnDelta(std::int32_t& dx, std::int32_t& dz) { dx = 0; dz = 0; }

} // namespace

// 0x53DBC4: team's pass-back target (team+0x1B8) is human-controlled and team+0x200 holds a kick
// code -> true; with EDX != 0 also fires that kick (Player_executeKick, EBX = ball height).
bool Action_humanPassReady(World& w, int pid, bool execute) {
    const TeamState& t = w.teams[static_cast<std::size_t>(P(w, pid).team)];
    const int c = t.f1b8;
    if (c == -1 || P(w, c).ctrl == 0 || t.f200 == 0) return false;
    if (!execute) return true;
    Player_executeKick(w, pid, t.f200, w.ball.posI.y);
    return true;
}

// 0x53DC10: can pid head/shoot the ball at point p (EDX) toward the goal it attacks, within
// reach (EBX) — phase 9 only, ball high enough, p inside the attacking area and in front of goal.
bool Action_canShootAtGoal(World& w, int pid, const Vec3& p, std::int32_t reach) {
    const PlayerState& pl = P(w, pid);
    const Pitch& g = w.pitch;
    if (w.match.phase != 9 || pid == w.ball.lastToucher) return false;
    std::int32_t m = w.ball.apexHeight; // 0x668470
    if (m <= w.ball.posI.y) m = w.ball.posI.y;
    if (m < 0x39) return false;
    if (p.y > 0x72 && p.y < 0x39) return false;
    const std::int32_t half = p.z < 0 ? 0 : 1;
    if (half == w.teams[static_cast<std::size_t>(pl.team)].side) return false;
    if (iabs(p.x) > g.quarterWidth) return false;   // 0x65F400
    if (iabs(p.z) < g.penAreaLineZ) return false;   // 0x65F374
    if (iabs(p.z) > g.halfLength) return false;     // 0x65F36C
    std::int32_t dx = p.x - pl.posI.x, dz = p.z - pl.posI.z;
    const std::int32_t ax = iabs(p.x);
    if (p.z > 0 && dz < 0) return false;
    if (p.z < 0 && dz > 0) return false;
    bool lateralOk = iabs(dz) >= iabs(3 * dx);
    if (!lateralOk) {
        if (ax < g.goalHalfWidth) return false;     // 0x65F37C
        if (p.x > 0 && dx > 0) return false;
        if (p.x < 0 && dx < 0) return false;
    }
    if (iabs(dz) > iabs(dx) && ax > ((g.halfLength - iabs(p.z)) >> 2) + g.goalHalfWidth) return false;
    const std::int32_t d = octLength(dz, dx);
    const std::int32_t d142 = octLength(w.ball.cross142Pos.x - pl.posI.x, w.ball.cross142Pos.z - pl.posI.z);
    if (d142 < d && d142 < 0x20 && w.ball.cross142Frame - w.ball.consumed > 0x3c) return false;
    const std::uint32_t r16 = static_cast<std::uint32_t>(reach) << 16;
    const std::int32_t lim = static_cast<std::int32_t>((r16 << 3) - r16) >> 16;
    return d <= lim;
}

// 0x53DEB0: drift toward the move target (Anim_setDrift over n frames) when it is beyond
// clamp(n*16, 0x20, 0x80) and the opponent possession winner's target is not right there.
bool Action_turnToOpponentTarget(World& w, int pid, std::int32_t n) {
    const int o = oppPossWinner(w, pid);
    const Vec3& a = Motion_targetVec(w, pid);
    const Vec3& b = Motion_targetVec(w, o);
    const std::int32_t d = octLength(b.x - a.x, b.z - a.z);
    std::int32_t lim = n << 4;
    if (lim < 0x20) lim = 0x20;
    else if (lim > 0x80) lim = 0x80;
    const PlayerState& p = P(w, pid);
    if (lim <= p.targetDist) return false;
    if (!(d > 0x20 || (d < 0x20 && p.targetDist < P(w, o).targetDist))) return false;
    Anim_setDrift(w, pid, p.targetDx, p.targetDz, n);
    return true;
}

// 0x53DF6C state 0x300: kick in progress — approach the ball, arm and release the planned kick
// (sub-steps via jump table 0x53DF4C), then hand the restart/out-of-play event to the referee.
// `esi` mirrors the exe register: the "run again" test after most sub-steps (set to 1 by sub 0,
// overwritten with the kick's contact frame by sub 2) and the value stored into stateTimer by
// subs 1 and 5.
void State300(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const int animId0 = static_cast<int>(p.kick.animId); // [esp+0x40]
    const int kind = p.kick.kind;                        // [esp+0x4C] byte +0x1E0
    std::int32_t esi = 0;
    for (;;) {
        switch (p.stateSub2) {
        case 0: // 0x53DFD6
            esi = 1;
            if (p.kick.animId == 0x10) {
                if ((p.kick.flags >> 2) & 1)
                    if (Kick_execute(w, pid)) checkOffside_W1(w, pid);
                p.stateTimer = 0;
                p.stateSub2 = 5;
            } else if (kind == 4 || kind == 5 || kind == 6 || (w.ball.possessor == pid && (w.ball.flags & 0x20))) {
                p.stateTimer = 0;
                p.stateSub2 = 1;
            } else if (w.match.phase == 9) {
                p.stateTimer = 0;
                p.stateSub2 = 2;
            } else {
                p.stateTimer = 0;
                p.stateSub2 = 3;
            }
            break;
        case 1: // 0x53E0B9
            Kick_arm(w, pid);
            Sim_scheduleSplit(w, p.kick.contactFrame);
            Action_turnToOpponentTarget(w, pid, p.kick.contactFrame);
            p.stateSub2 = 4;
            p.stateTimer = esi;
            continue;
        case 2: { // 0x53E0EF
            if (p.distBall > p.distBallPrev && p.distBall > 0x80) {
                Player_dropBallAndReset(w, pid);
                return;
            }
            bool lose = false;
            if (Player_hasPriorityOver(w, pid, oppPossWinner(w, pid))) {
                Ball_releaseFromPossessor(w, pid);
                p.countdown = (0x14 - w.attrs[static_cast<std::size_t>(p.attr)].at(0x35)) * 4;
                lose = true;
            } else if (w.ball.possessor != -1 && w.ball.possessor != pid) {
                lose = true;
            }
            if (lose) { // 0x53E252
                Kick_clearPending(w, pid);
                p.stateTimer = 0;
                p.stateSub2 = 6;
                break;
            }
            const std::int32_t dist = p.distBall; // edi
            if (w.ball.posI.y > 0x1c || dist > 0x60) { // 0x53E27B
                Motion_setTargetAlt(w, pid, w.ball.posI);
                break;
            }
            const std::int32_t frames = p.kick.contactFrame; // [esp+0x58]
            std::int32_t dx, dz;
            if (frames == -1) { // 0x53E2AD
                if (dist > 0x20) {
                    Motion_setTargetAlt(w, pid, w.ball.posI);
                    break;
                }
                if (Kick_execute(w, pid)) checkOffside_W1(w, pid);
                untraced_staleTurnDelta(dx, dz);
            } else {
                const Vec3 raw = offsetInDirection(p.posI, p.kick.animOut44, p.kick.animHeading); // y unused
                if (w.ball.dribbler == pid) w.ball.dribbler = -1;
                Vec3 t = raw;
                esi = frames; // 0x53E19F
                Pitch_clampToField(w, t);
                if (frames != 0) Ball_kickGroundPass(w, pid, t, frames, 0);
                else Ball_placeAt(w, t, false, 0);
                dx = t.x - raw.x;
                dz = t.z - raw.z;
            }
            // 0x53E1BF
            Kick_arm(w, pid);
            Anim_setDrift(w, pid, dx, dz, frames);
            if ((p.kick.flags & 1) && w.ball.possessor == pid) w.ball.dribbler = pid;
            Sim_scheduleSplit(w, frames);
            p.stateTimer = 0;
            p.stateSub2 = 4;
            continue;
        }
        case 3: // 0x53E2F3
            p.headAim = p.angleBall;
            p.flags22c &= ~0x10u;
            if (p.kick.contactFrame == -1) {
                if (p.distBall > 0x20) {
                    Motion_setTargetAlt(w, pid, w.ball.posI);
                    break;
                }
                if (Kick_execute(w, pid)) checkOffside_W1(w, pid);
            } else {
                const Vec3 t = untraced_offsetKeepY(w.ball.posI, p.kick.animOut44, p.kick.animHeading - 0x200);
                if (octDistance(p.posI, t).dist > 0x10) {
                    Motion_setTargetAlt(w, pid, t);
                    break;
                }
            }
            // 0x53E341
            p.flags22c |= 0x10u;
            Kick_arm(w, pid);
            if ((p.kick.flags & 1) && w.ball.possessor == pid) w.ball.dribbler = pid;
            Sim_scheduleSplit(w, p.kick.contactFrame);
            p.stateTimer = 0;
            p.stateSub2 = 4;
            continue;
        case 4: // 0x53E3EB wait for the kick animation to finish
            if (((p.kick.flags >> 2) & 1) && Player_animId(w, pid) == animId0) break;
            Kick_clearPending(w, pid);
            p.stateTimer = 0;
            ++p.stateSub2;
            break;
        case 5: // 0x53E43B
            ++p.stateSub2;
            p.stateTimer = esi;
            [[fallthrough]];
        case 6: { // 0x53E445 restart taken: tell the referee
            const int phase = w.match.phase;
            const int ownTeam = w.teams[static_cast<std::size_t>(p.team)].index;
            if (phase == 9 || phase == 0xb) { // 0x53E5AE
                const int c = w.ball.possessor;
                if (pid == c && P(w, c).prevState == 0x201)
                    Match_dispatchEvent(w, 0xb, w.teams[static_cast<std::size_t>(P(w, c).team)].index);
            } else {
                if (w.ball.flags & 0x40) break;
                const bool inside = iabs(w.ball.posI.x) <= w.pitch.halfWidth && iabs(w.ball.posI.z) <= w.pitch.halfLength;
                if (inside || phase == 5) {
                    Match_dispatchEvent(w, 0xb, ownTeam);
                } else {
                    if (w.ball.bounces <= 0) break;
                    const int oppTeam =
                        w.teams[static_cast<std::size_t>(w.teams[static_cast<std::size_t>(p.team)].opponent)].index;
                    if (p.prevState != 0x183) {
                        Match_dispatchEvent(w, 1, ownTeam);
                        return;
                    }
                    if (iabs(w.ball.posI.z) > w.pitch.halfLength) {
                        Match_dispatchEvent(w, 3, oppTeam);
                        return;
                    }
                    Vec3 t = w.ball.posI;
                    const std::int32_t lim = w.pitch.halfWidth + w.ball.radius; // 0x6691D4
                    // unsigned compares (ja / jae) exactly as the exe
                    if (static_cast<std::uint32_t>(-lim) > static_cast<std::uint32_t>(t.x)) t.x = -lim;
                    else if (static_cast<std::uint32_t>(lim) < static_cast<std::uint32_t>(t.x)) t.x = lim;
                    w.ball.posAtTouch = t; // 0x669178
                    Match_dispatchEvent(w, 1, oppTeam);
                    return;
                }
            }
            // 0x53E4A1
            if (w.ball.possessor == pid) Ball_releaseFromPossessor(w, pid);
            ++p.stateSub2;
            p.stateTimer = 0;
            break;
        }
        case 7: // 0x53E5D1 follow the ball until the animation allows leaving the state
            Motion_setTarget(w, pid, w.ball.posI);
            if (!(p.anim.flags() & 2)) break; // byte +0x9B
            if (p.prevState == 0x180 && static_cast<std::uint32_t>(w.teams[static_cast<std::size_t>(p.team)].playerCount) > 1)
                p.countdown = 0x3c;
            if (w.match.periodType == 3) break;
            Player_dropBallAndReset(w, pid);
            break;
        default: // 0x53E01E
            break;
        }
        if (!esi) return; // 0x53E01E test esi
    }
}

// 0x53E630 state 0x302: one-touch receiver — plan the meeting point with the incoming ball
// (PlayerShared::oneTouch*), run there, arm the queued one-touch kick, else fall back to 0x309.
void State302(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& T = w.teams[static_cast<std::size_t>(p.team)];
    const auto lose = [&] { // 0x53E834
        T.oneTouch = -1;
        Player_dropBallAndReset(w, pid);
    };
    if (w.ball.possessor != -1 && pid != w.ball.possessor) { lose(); return; }
    if (pid != w.match.player2a0 && static_cast<std::uint32_t>(p.stateSub2) < 2 && pid != w.ball.possessor) { lose(); return; }
    p.moveReq |= 1;
    p.anim.req = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.moveReq) >> 16); // [[player+0xB8]+0x18]
    {
        PlayerState& o = P(w, oppPossWinner(w, pid));
        if (o.f21C == 0 && (static_cast<std::uint32_t>(o.f224) >> 29) <= 1) {
            o.f218 = 7;
            o.f220 = 1;
            o.f224 = 0x20000002;
        }
    }
    if (p.ctrl == 0 && T.f1b8 != -1 && P(w, T.f1b8).ctrl != 0 && pid != T.oneTouch && Action_humanPassReady(w, pid, false)) {
        T.oneTouch = pid;
        T.oneTouchCode = T.f200;
        T.oneTouchTimer = 0x78;
    }
    if (T.oneTouch != -1 && pid == T.oneTouch) Player_requestSprint(w, pid);
    if (p.stateSub2 == 1 && p.distBall < 0x30 && p.targetDist > 0x50) {
        std::int32_t a = (p.angleBall - p.facing) & 0x3ff;
        if (a > 0x200) a = 0x400 - a;
        if (a < 0xc7) { // trap the ball dead: keep only its vertical speed
            const Vec3 v{0, w.ball.vel.y, 0};
            Ball_kickWithVelocity(w, pid, v, 0, 0);
            p.stateTimer = 0;
            p.stateSub2 = 0;
        }
    }
    if (pid != T.oneTouch) Player_setState(w, pid, 0x309, 0, 0);
    const int t = T.index; // [esp+0x30]
    PlayerShared& S = w.pl;
    const int sub = p.stateSub2;
    if (sub == 0) {
        if (p.anim.flags() & 4) return;
        if (p.ctrl == 0 && T.f1b8 == -1) {
            if (pid == T.oneTouch) T.oneTouch = T.f1b8;
            Player_setState(w, pid, 0x309, 0, 0);
            return;
        }
        // 0x53E957 plan the one-touch
        Player_executeKick(w, pid, T.oneTouchCode, 0);
        p.kick.deferredCode = T.oneTouchCode; // +0x1C8
        S.oneTouchLead[t] = Anim_markFrame0(w, static_cast<int>(p.kick.animId));
        const std::int32_t need = S.oneTouchLead[t] + 4;
        if (w.ball.landingFrame - w.ball.consumed >= need) { // 0x53EB25
            S.oneTouchFrame[t] = w.ball.landingFrame;
            S.oneTouchPos[t] = w.ball.landing; // 0x66916C
        } else {
            S.oneTouchFrame[t] = Ball_firstStampAfter(w.ball.bounceAt, w.ball.bounceCount, w.ball.consumed + need);
            if (S.oneTouchFrame[t] < 0) S.oneTouchFrame[t] = need + w.ball.consumed;
            if (T.oneTouchTimer <= S.oneTouchFrame[t]) T.oneTouchTimer = S.oneTouchFrame[t];
            Ball_predictPos(w, S.oneTouchFrame[t] - w.ball.consumed, 1, &S.oneTouchPos[t]);
        }
        const std::int32_t limit = S.oneTouchFrame[t] + 0xf; // [esp+0x44]
        for (;;) { // 0x53EA68 push the meeting frame back until the run fits
            const OctDist d = Math_vecTo(p.posI, S.oneTouchPos[t]);
            const Angle h = atan2i(d.dx, d.dz);
            const std::int32_t run = Motion_predictDistance(w, pid, p.speedCap, h, S.oneTouchFrame[t] - w.ball.consumed, 0, 1);
            const std::int32_t diff = d.dist - run;
            if (diff <= 0) break;
            std::int32_t inc = fx(diff, 0); // exe multiplies by 0 (always 0 -> clamped to 2)
            if (inc < 2) inc = 2;
            S.oneTouchFrame[t] += inc;
            if (limit > S.oneTouchFrame[t]) Player_setState(w, pid, 0x309, 0, 0);
        }
        // 0x53EB64
        Ball_predictPos(w, S.oneTouchFrame[t] - w.ball.consumed, 1, &S.oneTouchPos[t]);
        std::int32_t rootDist = 0, rootAng = 0; // [esp+0x2C] / [esp+0x28]
        Anim_contactInfo(w, static_cast<int>(p.kick.animId), 0, 0, -100, 1, &rootDist, &rootAng);
        const std::int32_t aim = p.kick.heading; // dword +0x1DA >> 16 = s16 +0x1DC
        std::int32_t ang = (rootAng + aim + 0x200) & 0x3ff;
        if (ang > 0x200) ang -= 0x400;
        Vec3 spot = offsetInDirection(S.oneTouchPos[t], rootDist, ang);
        spot.y = 0; // [esp+0x14] = 0
        Player_moveToward_4BE804(w, pid, spot);
        Motion_setTargetAlt(w, pid, spot);
        p.headAim = aim;
        Sim_scheduleSplit(w, S.oneTouchFrame[t] - w.ball.consumed - S.oneTouchLead[t]);
        p.stateTimer = 0;
        ++p.stateSub2;
        // falls into the sub 1 body (jmp 0x53E850)
    } else if (sub > 1) {
        if (sub != 2) return;
        goto finish;
    }
    { // 0x53E850 sub 1: arm the kick when the ball is lead frames away
        (void)Motion_dirToTarget(w, pid); // result only feeds a dead compare
        const std::int32_t rem = S.oneTouchFrame[t] - w.ball.consumed - S.oneTouchLead[t];
        if (rem > 0) return;
        std::int32_t near = S.oneTouchLead[t] * 6;
        if (near < 0x28) near = 0x28;
        if (rem != 0 || near <= p.targetDist) { // 0x53EC60
            Kick_clearPending(w, pid);
            Player_setState(w, pid, 0x309, 0, 0);
            return;
        }
        Kick_arm(w, pid);
        Anim_setDrift(w, pid, p.targetDx, p.targetDz, S.oneTouchLead[t]);
        Sim_scheduleSplit(w, S.oneTouchLead[t]);
        p.stateTimer = 0;
        ++p.stateSub2;
    }
finish: // 0x53E8F7
    if (w.ball.consumed <= S.oneTouchFrame[t] && pid == w.match.player2a0) return;
    T.oneTouch = -1;
    T.f1b8 = -1;
    p.countdown = 0x1e;
    Player_dropBallAndReset(w, pid);
}

namespace {

// Kick request helpers (+0x1D4 animOut44, +0x1D8 animHeading, +0x1DC heading s16 / +0x1DE contactFrame s16).
std::int32_t kickFrames(const PlayerState& p) { return p.kick.contactFrame; } // s16 +0x4E (+0x1DC >> 16)
std::int32_t kickRootAngle(const PlayerState& p) { return p.kick.heading; } // s16 +0x4C
std::int32_t pendingCode(const PlayerState& p) { return static_cast<std::int16_t>(p.pendingCode & 0xffff); }
void setPendingCode(PlayerState& p, std::int32_t c) {
    p.pendingCode = (p.pendingCode & 0xffff0000u) | (static_cast<std::uint32_t>(c) & 0xffffu);
}
std::int32_t wrap1024(std::int32_t a) { // and 0x3ff; > 0x200 -> -= 0x400
    a &= 0x3ff;
    return a > 0x200 ? a - 0x400 : a;
}
std::int32_t absDiff1024(std::int32_t a) { // and 0x3ff; > 0x200 -> 0x400 - a
    a &= 0x3ff;
    return a > 0x200 ? 0x400 - a : a;
}
void offsetXZ_W2(Vec3& out, const Vec3& base, Fixed scale, Angle a) { // FUN_004BB0D0 writes x/z only
    const Vec3 r = offsetInDirection(base, scale, a);
    out.x = r.x;
    out.z = r.z;
}
void takePossession_W2(World& w, int pid) {
    const int prev = w.ball.possessor;
    Ball_takePossession(w, pid, 0);
    if (w.ball.possessor != prev) w.emit(EventType::Possession, P(w, pid).team, pid, w.ball.possessor, prev);
}
// 0x53F3E6 / 0x53F843: arm the opponent's ball winner (fields +0x218..+0x224 untraced meaning).
void flagOpponentWinner(World& w, int team, std::int32_t v218) {
    const int o = w.teams[static_cast<std::size_t>(w.teams[static_cast<std::size_t>(team)].opponent)].possWinner;
    if (o < 0) return; // exe dereferences the pointer (never null here)
    PlayerState& q = P(w, o);
    if (q.f21C == 0 && (static_cast<std::uint32_t>(q.f224) >> 29) <= 1) {
        q.f218 = v218;
        q.f220 = 1;
        q.f224 = 0x20000002;
    }
}

// exe 0x53DB80: volley/header kick code by aim sector (16 x 64 units) + [16] no pad direction.
constexpr std::int32_t kAimCode_53DB80[17] = {419, 418, 416, 416, 422, 422, 420, 420, 424,
                                              421, 421, 423, 423, 417, 417, 419, 424};
// exe 0x590860: indexed by attr byte 0x32.
constexpr std::int32_t kT590860[20] = {40, 38, 36, 34, 32, 30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 26, 8, 6, 4, 0};

// 0x4DBFB8: AI aim angle from the player towards the opponent goal, bent away from the opponent
// key player team+0x1CC when he is close/in the way.
Angle aimAngle_4DBFB8(World& w, int pid) {
    const PlayerState& p = P(w, pid);
    const TeamState& t = w.teams[static_cast<std::size_t>(p.team)];
    const int key = w.teams[static_cast<std::size_t>(t.opponent)].f1cc;
    const std::int32_t dz = -w.pitch.goalLineZ[t.side] - p.posI.z;
    std::int32_t dx;
    if ((t.flags >> 9) & 1) {
        std::int32_t e = w.pitch.halfWidth - 0xc0;
        if (p.posI.x < 0) e = -e;
        dx = e - p.posI.x;
    } else {
        dx = -p.posI.x;
    }
    const Angle goal = atan2i(dx, dz);
    if (key < 0) return goal;
    const std::int32_t d = Player_cachedDistance(w, pid, key);
    const Angle toKey = Player_cachedAngle(w, pid, key);
    std::int32_t bend = Math_asinRatio(128, d >> 1); // exe 0x59FD0C = 128
    if (d < 160) bend = 0xe4;                        // exe 0x59FD10 = 160
    if (bend < 85) bend = 85;                        // exe 0x59FD14 = 85
    const std::int32_t diff = wrap1024(goal - toKey);
    if (iabs(diff) > 171) { // exe 0x59FCAC = 171
        if (bend > 0x55) bend = 0x55;
        return diff > 0 ? goal + bend : goal - bend;
    }
    return diff > 0 ? toKey + bend : toKey - bend;
}

// 0x53F6BC: volley kick code: human L2/R2 force 0x1A2/0x1A3, else pad (or AI aim) direction
// relative to the ball heading picks table 0x53DB80.
std::int32_t kickCode_53F6BC(World& w, int pid) {
    const PlayerState& p = P(w, pid);
    const std::int32_t heading = w.ball.heading;
    std::int32_t a;
    if (p.ctrl == 0) {
        a = aimAngle_4DBFB8(w, pid) - heading;
    } else {
        const ControllerState& c = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
        const std::uint32_t b = (c.bits >> 8) & 0xff; // byte +0x3D
        if (b & 4) return 0x1a2;
        if (b & 8) return 0x1a3;
        if (c.dirX == 0 && c.dirZ == 0) return kAimCode_53DB80[16];
        a = wrap1024(atan2i(c.dirX, c.dirZ) - heading);
    }
    return kAimCode_53DB80[((a + 0x20) & 0x3ff) >> 6];
}

// 0x53F75C: AI-only team: if the opponent winner is in 0x301/0x303/0x304 and clearly nearer his
// target than we are to ours, switch to 0x303. Returns 1 if switched.
bool yieldTo303_53F75C(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const TeamState& t = w.teams[static_cast<std::size_t>(p.team)];
    const int o = w.teams[static_cast<std::size_t>(t.opponent)].possWinner;
    if (t.humans != 0) return false;
    if (pid == w.ball.possessor) return false;
    if (o < 0) return false; // exe dereferences the pointer (never null here)
    const PlayerState& q = P(w, o);
    if (q.state != 0x304 && q.state != 0x303 && q.state != 0x301) return false;
    if (q.targetDist >= 0x40) return false;
    if (p.targetDist - q.targetDist <= 0x10) return false;
    Player_setState(w, pid, 0x303, 0, 0, false, true);
    return true;
}

void nextSub(PlayerState& p) {
    p.stateTimer = 0;
    ++p.stateSub2;
}

} // namespace

// 0x53EC9C: volley/half-volley shot at a dropping loose ball (sub 0 pick anim, 1 line up + start
// kick anim with drift, 2 contact: take the ball and plan the shot, 3 wait for the anim end).
void State301(World& w, int pid) {
    PlayerState& p = P(w, pid);
    if (w.ball.possessor != -1 || w.match.phase != 9) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    p.moveReq |= 1;
    p.anim.req = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.moveReq) >> 16);
    if (p.targetDist > 0x80) Player_requestSprint(w, pid);
    else w.attrs[static_cast<std::size_t>(p.attr)].f10 = 0;

    switch (p.stateSub2) { // jump table 0x53EC8C
    case 0: {              // 0x53ED0F
        if (std::max(w.ball.apexHeight, w.ball.posI.y) < 0x39) {
            Motion_setTargetAlt(w, pid, w.ball.posI); // 0x53EDD7
            Player_setState(w, pid, 0x309, 0, 0);
            return;
        }
        if (Player_isKeeperInOwnBox(w, pid)) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        const int anim = Anim_resolveId(w, (w.rng.next() & 1) ? 0xf4 : 0xf3);
        std::int32_t dist = 0, ang = 0;
        const int frames = Anim_contactInfo(w, anim, 0, 0, -100, 1, &dist, &ang);
        Kick_setAnimParams(w, pid, anim, frames, dist, ang);
        Player_moveToward_4BE804(w, pid, w.ball.cross85Pos);
        nextSub(p); // 0x53ED91
        return;
    }
    case 1: { // 0x53EE0F
        if (std::max(w.ball.apexHeight, w.ball.posI.y) < 0x39) {
            Motion_setTargetAlt(w, pid, w.ball.posI);
            Player_setState(w, pid, 0x309, 0, 0);
            return;
        }
        Vec3 v{};
        if (std::max(w.ball.apexHeight, w.ball.posI.y) >= 0x72) { // 0x53EF7D
            const Angle a = atan2i(p.posI.x - w.ball.cross85Pos.x, p.posI.z - w.ball.cross85Pos.z);
            offsetXZ_W2(v, w.ball.cross85Pos, p.kick.animOut44, a - kickRootAngle(p));
        } else {
            Player_solveIntercept(w, p.posI, (p.runMax + p.speed) >> 1, w.ball.posI, w.ball.vel.x, w.ball.vel.z,
                                  w.pitch.halfWidth, w.pitch.halfLength, v);
        }
        Player_moveToward_4BE804(w, pid, v); // 0x53EE83
        Ball_predictPos(w, kickFrames(p), 0, &v);
        if (v.y > 0x72) return;
        if (v.y < 0x39 || iabs(v.z) > w.pitch.halfLength) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        const std::int32_t dx = v.x - p.posI.x, dz = v.z - p.posI.z;
        const std::int32_t reach = p.kick.animOut44;
        const std::int32_t d = octLength(dx, dz);
        if (d > reach + 0xa0 || d < reach - 0x30) return;
        const Angle a = atan2i(dx, dz);
        if (p.ctrl == 0) {
            std::int32_t t = a;
            if (w.pitch.goalLineZ[w.teams[static_cast<std::size_t>(p.team)].side] >= 0) t -= 0x200;
            if (absDiff1024(t) > 0xf2) {
                Player_dropBallAndReset(w, pid);
                return;
            }
        }
        const Angle face = a - kickRootAngle(p); // 0x53EFD3
        Vec3 q{};
        offsetXZ_W2(q, p.posI, reach, face);
        const std::int32_t ddx = v.x - q.x, ddz = v.z - q.z;
        Anim_start(w, pid, p.kick.animId, 0, face - kickRootAngle(p));
        Sim_scheduleSplit(w, kickFrames(p));
        Anim_setDrift(w, pid, ddx, ddz, kickFrames(p));
        nextSub(p);
        return;
    }
    case 2: { // 0x53F073
        if (pid == w.ball.lastToucher) {
            nextSub(p); // 0x53F0B8 (on the last toucher = this player)
            return;
        }
        if (static_cast<std::uint32_t>(kickFrames(p)) > static_cast<std::uint32_t>(p.stateTimer)) return;
        if (w.ball.posI.y > 0x72 && w.ball.posI.y < 0x39) { // 0x53F096 (unreachable in the exe too)
            nextSub(p);
            return;
        }
        const Vec3 c = Anim_contactPos(w, pid); // 0x53F0D0
        const std::int32_t d = octLength(w.ball.posI.x - c.x, w.ball.posI.z - c.z);
        const std::int32_t by = w.ball.posI.y;
        if (d >= 0x30 || by >= 0x72 || by <= 0x39) {
            nextSub(p);
            return;
        }
        takePossession_W2(w, pid);
        Vec3 tgt{};
        bool pad = false;
        if (p.ctrl != 0) {
            const ControllerState& cs = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
            pad = cs.dirX != 0 || cs.dirZ != 0;
        }
        if (pad) { // 0x53F13E
            const std::int32_t r = static_cast<std::int32_t>(w.rng.next() & 7);
            tgt.x = w.controllers[static_cast<std::size_t>(p.ctrl - 1)].dirX * ((r << 4) + 0x30);
        } else { // 0x53F2FF
            tgt.x = 0x80 - static_cast<std::int32_t>(w.rng.next() & 0xff);
        }
        std::int32_t z = w.pitch.halfLength - static_cast<std::int32_t>(w.rng.next() & 3) * 0x30; // 0x53F159
        if (w.teams[static_cast<std::size_t>(p.team)].side == 1) z = -z;
        tgt.z = z;
        const OctDist od = octDistance(w.ball.posI, tgt);
        std::int32_t a = wrap1024(atan2i(od.dx, od.dz) - p.facing);
        if (a < -0x8e) a = -0x8e;
        else if (a > 0x8e) a = 0x8e;
        a = wrap1024(a + p.facing);
        std::int32_t s = fx(p.speed, 2) + (w.ball.speed >> 16);
        if (s > 0x1e) s = 0x1e;
        std::int32_t h = iabs(0x30 - w.ball.posI.y);
        h = 0x60 - h;
        if (h < 10) h = 10;
        std::int32_t pw = s * h + (static_cast<std::int32_t>(w.attrs[static_cast<std::size_t>(p.attr)].at(0x34)) << 6);
        if (pw > 0x780) pw = 0x780;
        std::int32_t rel[4];
        Math_polarRecord(pw, a, rel);
        // 0x505084 (commentary queue) and 0x4A2BC4 (commentary, audio RNG 0x49715C): presentation only.
        const std::int32_t lift = w.ball.posI.y + static_cast<std::int32_t>(w.rng.next() & 0x3f);
        Kick_planToPoint(w, pid, rel, nullptr, lift, -1, 0, 4, 0x10, 0, 3);
        Player_setState(w, pid, 0x300, 0, 0, false, true);
        return;
    }
    case 3: // 0x53F335
        if (p.anim.b34[3] & 2) Player_dropBallAndReset(w, pid);
        return;
    default: return;
    }
}

// 0x53F354: run to the ball's y=142 descent point and head/volley it (human/AI shot check first).
void State303(World& w, int pid) {
    PlayerState& p = P(w, pid);
    std::int32_t left = w.ball.cross142Frame - w.ball.consumed;
    if ((w.attrs[static_cast<std::size_t>(p.attr)].at(0x40) & 1) || Player_isKeeperInOwnBox(w, pid) ||
        w.ball.possessor != -1 || w.match.phase != 9) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    const TeamState& t = w.teams[static_cast<std::size_t>(p.team)];
    bool tryShot = true;
    if (t.humans != 0)
        tryShot = p.ctrl != 0 && ((p.pendingCode & 0xffff) == 0xe ||
                                  (w.controllers[static_cast<std::size_t>(p.ctrl - 1)].flags1a0 & 4));
    if (tryShot && Action_canShootAtGoal(w, pid, w.ball.cross85Pos, w.ball.cross85Frame - w.ball.consumed)) {
        Player_setState(w, pid, 0x301, 0, 0, false, true);
        return;
    }
    flagOpponentWinner(w, p.team, 1);

    if (p.stateSub2 == 0) { // 0x53F5F6
        Vec3 v = w.ball.cross142Pos;
        Pitch_clampToField(w, v);
        Player_moveToward_4BE804(w, pid, v);
        nextSub(p);
        return;
    }
    if (p.stateSub2 == 1) { // 0x53F493
        if (p.targetDist > 0x150 || w.ball.consumed + 10 > w.ball.cross142Frame || w.ball.possessor != -1) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (p.targetDist > 0x70) { // 0x53F628
            Vec3 v = w.ball.cross142Pos;
            Pitch_clampToField(w, v);
            Player_moveToward_4BE804(w, pid, v);
            return;
        }
        if ((p.ctrl == 0 && p.distBall > p.distBallPrev) || p.posI.y != 0) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        bool human = p.ctrl != 0;
        if (human && (w.controllers[static_cast<std::size_t>(p.ctrl - 1)].flags1a0 & 4))
            human = (p.pendingCode & 0xffff) == 0xe; // 0x53F64C
        if (human) { // 0x53F4FE
            // 0x4CD7D8 Anim_clipFrames(0x10B) with EBX = 0xB2: result discarded (pure query).
            Player_executeKick(w, pid, pendingCode(p), 0xb2);
            p.kick.deferredCode = pendingCode(p); // +0x1C8
        } else {
            TeamAI_planAirKick_4DCAC8(w, pid); // 0x53F65A
        }
        Vec3 v{}; // 0x53F52C (y: stack garbage in the exe, unused by the callees)
        offsetXZ_W2(v, w.ball.cross142Pos, p.kick.animOut44, p.kick.animHeading - 0x200);
        Pitch_clampToField(w, v);
        Player_moveToward_4BE804(w, pid, v);
        p.headAim = p.angleBall;
        left -= kickFrames(p);
        p.flags22c &= ~0x10u;
        Sim_scheduleSplit(w, left);
        nextSub(p);
        return;
    }
    if (p.stateSub2 != 2) return; // 0x53F59F
    if (w.ball.possessor != -1 || w.match.phase != 9) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (p.targetDist < 0x20) { // 0x53F666
        Vec3 v{};
        offsetXZ_W2(v, w.ball.cross142Pos, p.kick.animOut44, p.kick.animHeading - 0x200);
        Pitch_clampToField(w, v);
        Player_moveToward_4BE804(w, pid, v);
    }
    if (left > kickFrames(p)) { // 0x53F5CF
        p.headAim = p.angleBall;
        return;
    }
    Player_setState(w, pid, 0x300, 0, 0, false, true);
}

// 0x53F7F4: ball winner meets the ball at its y=85 descent point with a volley/trap (sub 0 run,
// 1 line up + contact params, 2 start kick anim, 3 contact: take the ball, 4 wait for anim end).
void State304(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const std::int32_t left = w.ball.cross85Frame - w.ball.consumed;
    if (p.ctrl == 0 && yieldTo303_53F75C(w, pid)) return;
    if (w.match.phase != 9 || (w.attrs[static_cast<std::size_t>(p.attr)].at(0x40) & 1) ||
        pid != w.teams[static_cast<std::size_t>(p.team)].possWinner) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    flagOpponentWinner(w, p.team, 2);

    switch (p.stateSub2) { // jump table 0x53F7E0
    case 0: {              // 0x53F8A8
        Vec3 v = w.ball.cross85Pos;
        Pitch_clampToField(w, v);
        Player_moveToward_4BE804(w, pid, v);
        nextSub(p);
        return;
    }
    case 1: { // 0x53F8FF
        if (w.ball.consumed > w.ball.cross85Frame || w.ball.possessor != -1) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (p.targetDist > 0x70) return;
        Player_moveToward_4BE804(w, pid, w.ball.cross85Pos);
        if (p.targetDist > 0x70) return;
        if (p.distBall > p.distBallPrev || p.posI.y != 0) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        setPendingCode(p, kickCode_53F6BC(w, pid));
        std::int32_t dist = 0, ang = 0;
        const int frames = Anim_contactInfo(w, pendingCode(p), 0, 0, -100, 1, &dist, &ang);
        Kick_setAnimParams(w, pid, pendingCode(p), frames, dist, ang);
        if (left < frames) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        Vec3 v{}; // 0x53F9B8
        offsetXZ_W2(v, w.ball.cross85Pos, dist, p.angleBall - 0x200);
        Pitch_clampToField(w, v);
        Player_moveToward_4BE804(w, pid, v);
        Sim_scheduleSplit(w, left - frames);
        p.stateTimer = 0;
        p.flags22c &= ~0x10u;
        p.headAim = p.angleBall;
        ++p.stateSub2;
        return;
    }
    case 2: // 0x53FA26
        if (left > kickFrames(p)) {
            p.headAim = p.angleBall;
            return;
        }
        setPendingCode(p, kickCode_53F6BC(w, pid));
        Anim_start(w, pid, pendingCode(p), 0, p.angleBall);
        Action_turnToOpponentTarget(w, pid, kickFrames(p));
        Sim_scheduleSplit(w, kickFrames(p));
        Keeper_holdOff(w, 0x14);
        p.stateTimer = 0;
        p.stateSub2 = 3;
        return;
    case 3: { // 0x53FAB0
        if (w.ball.possessor != -1 || w.match.phase != 9) {
            nextSub(p);
            return;
        }
        if (Anim_frame(w, pid) < kickFrames(p)) return;
        Keeper_holdOff(w, 5);
        const std::int32_t d = absDiff1024(p.angleBall - (w.ball.heading + 0x200));
        const std::int32_t db = p.distBall;
        const bool miss = db > 0x4c || p.posI.y + 0x82 < w.ball.posI.y || p.posI.y + 0x39 > w.ball.posI.y ||
                          (db > 0x10 && d > 0xc7);
        if (miss) { // 0x53FC41
            Anim_start(w, pid, 0x166, 0, p.facingDesired);
            nextSub(p);
            return;
        }
        if (p.slot == 0) Keeper_dropPossession_4BF788(w, pid);
        bool take = true;
        if (p.ctrl != 0 && ((w.controllers[static_cast<std::size_t>(p.ctrl - 1)].bits >> 8) & 0xc)) { // 0x53FC5B
            const std::uint32_t c = p.pendingCode & 0xffff;
            if (c == 0x1a3 || c == 0x1a2) take = false;
        }
        if (take) { // 0x53FB94
            takePossession_W2(w, pid);
            if (w.ball.possessor == pid) w.ball.dribbler = pid;
            w.match.player2a0 = -1;
            w.teams[static_cast<std::size_t>(p.team)].f1e0 = -1;
            // 0x4A2818(ball pos, 4): ball thud sound, presentation only.
        }
        const TeamState& t = w.teams[static_cast<std::size_t>(p.team)]; // 0x53FBCB
        if (t.humans != 0 && t.f1b8 == -1) Player_assignControlToReceiver(w, pid, -1) /* 0x4528C4 */;
        {
            const std::int32_t sh = static_cast<std::int32_t>(w.rng.next() & 1); // 0x53FBE9
            std::int32_t rel[4]; // result unused by the exe (local polar record)
            Math_polarRecord((kT590860[w.attrs[static_cast<std::size_t>(p.attr)].at(0x32)] >> sh) + 0x20, p.facing,
                             rel);
        }
        Player_moveToward_4BE804(w, pid, w.ball.posI);
        nextSub(p);
        return;
    }
    case 4: // 0x53FC87
        if (!(p.anim.b34[3] & 2)) return;
        if (pid != w.ball.possessor) { // 0x53FCCF
            w.match.player2a0 = -1;
            w.teams[static_cast<std::size_t>(p.team)].f1e0 = -1;
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (w.ball.posI.y >= 0x20) {
            Player_moveToward_4BE804(w, pid, w.ball.posI);
            return;
        }
        // 0x4D6054(possessor): commentary trigger, presentation only.
        Player_setState(w, pid, 0x308, 0, 0, false, true);
        return;
    default: return;
    }
}

// 0x53FD08: possessor stops the ball dead and pokes it +-90 degrees off his facing (AI dribble kick).
void State305(World& w, int pid) {
    PlayerState& p = P(w, pid);
    if (w.match.phase != 9 || pid != w.ball.possessor) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    Motion_setTargetAlt(w, pid, w.ball.posI);
    if (p.stateSub2 != 0) return;
    if (p.distBall > 0x42) {
        if (static_cast<std::uint32_t>(p.stateTimer) > 0x3c) Player_dropBallAndReset(w, pid);
        return;
    }
    Ball_stopDead(w); // 0x53FD63
    const Angle a = (w.rng.next() & 1) ? p.facing + 0x100 : p.facing - 0x100;
    std::int32_t rel[4];
    Math_polarRecord(0xa0, a, rel);
    Kick_planToPoint(w, pid, rel, nullptr, 0x20, -1, 0, 0xb, 0xc, 0, 0);
    Player_setState(w, pid, 0x300, 0, 0, false, true);
}

// 0x53FD50: empty handler (the `ret` shared with State305's epilogue).
void State306(World&, int) {}

namespace {
// exe 0x590860: ball push strength by rating A[0x32] (20 entries used).
constexpr std::int32_t kTrapPush_590860[20] = {40, 38, 36, 34, 32, 30, 28, 26, 24, 22,
                                               20, 18, 16, 14, 12, 26, 8, 6, 4, 0};

// `and 0x3ff; cmp 0x200; jle; 0x400 - x`: unsigned angle difference 0..0x200.
std::int32_t angAbs_W3(std::int32_t v) {
    v &= 0x3ff;
    return v > 0x200 ? 0x400 - v : v;
}
// `and 0x3ff; cmp 0x200; jle; sub 0x400`: signed difference -0x1ff..0x200.
std::int32_t angSigned_W3(std::int32_t v) {
    v &= 0x3ff;
    return v > 0x200 ? v - 0x400 : v;
}
// Ball_takePossession 0x475194 (EDX = 0) plus the Possession event when the possessor changes.
void takeBall_W3(World& w, int pid) {
    const int prev = w.ball.possessor;
    Ball_takePossession(w, pid, 0);
    if (w.ball.possessor != prev) w.emit(EventType::Possession, P(w, pid).team, pid, pid, prev);
}
// 0x53FFxx/0x5404xx pattern: Ball_predictPos 0x476E6C(5, 0) and posI when it reports no prediction.
Vec3 ballAhead5_W3(World& w) {
    Vec3 t;
    if (Ball_predictPos(w, 5, 0, &t) == 0) t = w.ball.posI;
    return t;
}
} // namespace

// 0x5404A8: dribble touch after a skill move: inside the pitch pass the ball along the anim's contact
// root motion (Ball_kickGroundPass kind 2), near the lines slow the ball and drift the player onto it.
void Action_dribbleTouch_5404A8(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const int anim = Player_animId(w, pid);
    if (iabs(p.posI.z) < w.pitch.halfLength - 0x140 && iabs(p.posI.x) < w.pitch.halfWidth - 0x140) {
        std::int32_t dist, ang;
        const int frames = Anim_contactInfo(w, anim, 0, 0, -100, 1, &dist, &ang);
        const Vec3 t = offsetInDirection(p.posI, dist, p.anim.angle() + ang); // y unused by the callee
        Ball_kickGroundPass(w, pid, t, frames, 2);
    } else {
        Fixed s = p.speed; // 0x54057B
        if ((p.runMax >> 1) < s) s = p.runMax >> 1;
        if (s < w.ball.speed) Ball_pushInDirection(w, pid, w.ball.heading, s);
        std::int32_t dist, ang;
        const int frames = Anim_contactInfo(w, anim, 0, 0, -100, 1, &dist, &ang);
        ang += p.anim.angle() - 0x200;
        Vec3 t;
        Ball_predictPos(w, frames, (p.anim.flags() & 2) ? 0 : 1, &t);
        t = offsetInDirection(t, dist, ang);
        Anim_setDrift(w, pid, t.x - p.posI.x, t.z - p.posI.z, frames);
    }
    if (pid == w.ball.possessor) w.ball.dribbler = pid; // 0x540648
}

// 0x53FE00: receive/control a loose ball (trap by height, chest/thigh touch, then 0x308 or 0x300).
void State307(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const PlayerAttr& A = w.attrs[static_cast<std::size_t>(p.attr)];
    const std::int32_t push = kTrapPush_590860[A.at(0x32)]; // [esp+0x60]
    if ((w.ball.possessor != -1 && w.ball.possessor != pid) || w.match.phase != 9) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (Player_hasPriorityOver(w, pid, w.teams[static_cast<std::size_t>(w.teams[p.team].opponent)].possWinner)) {
        Ball_releaseFromPossessor(w, pid);
        p.countdown = (0x14 - A.at(0x35)) * 4;
        Player_dropBallAndReset(w, pid);
        return;
    }
    TeamState& team = w.teams[static_cast<std::size_t>(p.team)];
    for (;;) { // 0x53FE80
        switch (p.stateSub2) { // jump table 0x53FDE4
        case 0: { // 0x53FE95
            const std::int32_t y = w.ball.posI.y;
            if (p.slot == 0) Keeper_dropPossession_4BF788(w, pid);
            if (y < 0x72) {
                if (y <= 0x55) {
                    if (y <= 0x20) {
                        // exe calls 0x4A2818 (ball touch sound 0x21): not simulated
                        Team_noteTouch_4D61BC(w, pid);
                        takeBall_W3(w, pid);
                        p.stateTimer = 0;
                        p.stateSub2 = 6;
                        continue;
                    }
                    takeBall_W3(w, pid); // 0x53FFCD
                    if (angAbs_W3(p.facing - p.angleBall) < 0x155) {
                        // exe calls 0x4A2818 (ball touch sound 0x21): not simulated
                        p.stateTimer = 0;
                        p.stateSub2 = 4;
                        continue;
                    }
                } else { // 0x53FF50
                    Ball_releaseFromPossessor(w, pid);
                    const std::int32_t d = angAbs_W3(p.facing - w.ball.heading);
                    if (Motion_facingBall(w, pid) && d > 0x180) {
                        Sim_scheduleSplit(w, w.ball.cross85Frame - w.ball.consumed - 2);
                        p.stateTimer = 0;
                        p.stateSub2 = 1;
                        break;
                    }
                }
            }
            Ball_releaseFromPossessor(w, pid); // 0x53FEA7 (falls through to the move below)
            Player_dropBallAndReset(w, pid);
            break;
        }
        case 1: { // 0x540037 chest/thigh trap
            const std::int32_t d = angAbs_W3(p.angleBall - (w.ball.heading + 0x200));
            if (!Motion_facingBall(w, pid) || d > 0x80) {
                Ball_releaseFromPossessor(w, pid);
                Player_dropBallAndReset(w, pid);
                return;
            }
            const int anim = Anim_resolveId(w, (w.rng.next() & 3) ? 5 : 6);
            std::int32_t dist, ang;
            const int frames = Anim_contactInfo(w, anim, 0, 0, -100, 0, &dist, &ang);
            std::int32_t frames6c = frames;
            Anim_start(w, pid, anim, 0, p.angleBall);
            const Vec3 contact = offsetInDirection(p.posI, dist, p.angleBall + ang);
            Vec3 t;
            Ball_predictPos(w, frames, 0, &t);
            t = offsetInDirection(t, w.ball.radius + 0x10, p.angleBall - 0x200);
            const OctDist od = octDistance(contact, t);
            if (od.dist > 0x40) {
                t = offsetInDirection(p.posI, w.ball.radius + 0x10, p.angleBall);
                t.y = w.ball.posI.y;
                Ball_placeAt(w, t, false, 0);
                frames6c = 0;
            } else {
                Anim_setDrift(w, pid, od.dx, od.dz, frames);
            }
            Kick_setAnimParams(w, pid, anim, frames6c, dist, ang); // 0x540181
            Sim_scheduleSplit(w, frames6c);
            p.stateTimer = 0;
            ++p.stateSub2;
            break;
        }
        case 2: { // 0x5401F1 wait for the contact frame, then pop the ball up
            if (Anim_frame(w, pid) < p.kick.contactFrame) break;
            if (w.ball.vel.x != 0 && w.ball.vel.z != 0 &&
                angAbs_W3(w.ball.heading + 0x200 - p.facing) > 0x72) {
                Player_dropBallAndReset(w, pid);
                return;
            }
            takeBall_W3(w, pid);
            // exe calls 0x4A2818 (ball touch sound 4): not simulated
            const std::int32_t s = (push >> (w.rng.next() & 1)) + 0x20;
            std::int32_t pol[4];
            Math_polarRecord(s, p.facing, pol);
            Ball_kickWithVelocity(w, pid, Vec3{pol[1] << 0xb, static_cast<Fixed>(0xfffd0000), pol[2] << 0xb}, 0, 0);
            p.stateTimer = 0;
            ++p.stateSub2;
            break;
        }
        case 3: // 0x5402E1
            if (!(p.anim.flags() & 2)) break;
            Player_setState(w, pid, 0x308, 0, 0, false, true);
            return;
        case 4: { // 0x54030D cushion the ball forward
            const std::int32_t s = (push >> (w.rng.next() & 4)) + 4;
            const Fixed sp = p.speed;
            Angle a;
            if (sp > 0x38000) a = p.facingDesired;
            else if (sp > 0x18000) a = (p.facingDesired + p.angleBall) >> 1;
            else a = p.angleBall;
            std::int32_t pol[4];
            Math_polarRecord(s, a, pol);
            if (!(pid == w.ball.lastToucher && w.ball.consumed <= 0xa))
                Ball_kickWithVelocity(w, pid, Vec3{(pol[1] << 0xa) + p.m98, 0, (pol[2] << 0xa) + p.m9c}, 0, 0);
            p.stateTimer = 0;
            ++p.stateSub2;
            break;
        }
        case 5: // 0x5403DB
            if (static_cast<std::uint32_t>(p.stateTimer) < 0xf) break;
            Player_setState(w, pid, 0x308, 0, 0, false, true);
            return;
        case 6: { // 0x540406 ground trap
            if (p.distBall > 0x42 && w.ball.speed > 0x40000) {
                // 0x4BB168 (EAX 0x40000, EDX heading) == offsetInDirection from the origin; y = 0
                Ball_kickWithVelocity(w, pid, offsetInDirection(Vec3{}, 0x40000, w.ball.heading), 0, 0);
            }
            const int ot = team.oneTouch;
            if (pid != ot || (P(w, ot).anim.flags() & 4)) {
                Player_setState(w, pid, 0x308, 0, 0, false, true);
                return;
            }
            if (P(w, ot).distBall > 0xc0) break;
            takeBall_W3(w, ot);
            Player_executeKick(w, ot, w.teams[static_cast<std::size_t>(P(w, ot).team)].oneTouchCode, w.ball.posI.y);
            Player_setState(w, pid, 0x300, 0, 0, false, false);
            return;
        }
        default: break;
        }
        break; // 0x53FEB5: [esp+0x58] is never set, the loop only repeats via the explicit `continue`s
    }
    Player_moveToward_4BE804(w, pid, w.ball.posI); // 0x53FEBC
}

// 0x540658: dribble with the ball (human pad pushes / AI decision, skill turns, touchline/goal-line bias).
void State308(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const PlayerAttr& A = w.attrs[static_cast<std::size_t>(p.attr)];
    std::int32_t* dv = w.pl.dribbleVec; // DAT_006731F8
    int row = 6, dirCls = 0;            // [esp+0x30] / [esp+0x2c]
    const int one24 = 1;                // [esp+0x24]
    const int fast = (A.f10 != 0 && !(A.at(0x40) & 0x40)) ? 1 : 0; // [esp+0x28]
    const int noDrift = (p.anim.flags() >> 1) & 1;                  // [esp+0x34] byte +0x9B bit 1
    TeamState& team = w.teams[static_cast<std::size_t>(p.team)];
    const int other = w.teams[static_cast<std::size_t>(team.opponent)].possWinner;
    const int poss = w.ball.possessor;
    if (pid != poss || w.match.phase != 9) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (team.f1b8 != -1 && team.f200 != 0 && Action_humanPassReady(w, poss, true)) {
        Player_setState(w, poss, 0x300, 0, 0, false, true);
        return;
    }
    if (p.distBall > 0xc0) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (pid == team.oneTouch && !(P(w, team.oneTouch).anim.flags() & 4)) {
        Player_executeKick(w, pid, team.oneTouchCode, w.ball.posI.y);
        Player_setState(w, pid, 0x300, 0, 0, false, false);
        return;
    }
    if (other != -1 && Player_hasPriorityOver(w, pid, other)) {
        Ball_releaseFromPossessor(w, pid);
        p.countdown = (0x14 - A.at(0x35)) * 4;
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (p.slot == 0 && p.ctrl == 0) {
        TeamAI_onBallWon_4DC614(w, pid, 1);
        return;
    }
    p.moveReq |= 0x1000041;
    p.anim.req = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.moveReq) >> 16);
    if (w.ball.posI.y > 0x39) { // high ball: follow it
        if (p.distBall > p.distBallPrev) {
            Motion_setTargetAlt(w, pid, w.ball.posI);
            return;
        }
        const std::int32_t d = p.distBall > 0x20 ? 0x20 : p.distBall;
        // exe leaves the local's y uninitialised; Motion_setTarget 0x4CB778 only reads x/z
        Motion_setTarget(w, pid, offsetInDirection(w.ball.posI, d, p.angleBall - 0x200));
        return;
    }
    Vec3 t0c = ballAhead5_W3(w); // 0x5408A8
    if (p.flags22c & 0x20000) {
        p.moveReq |= 0x08000000;
        p.anim.req = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.moveReq) >> 16);
    }
    if (p.stateSub2 == 2 && static_cast<std::uint32_t>(p.stateTimer) >= 0xa) {
        p.stateTimer = 0;
        p.stateSub2 = 0;
    }
    std::int32_t ang38 = 0; // [esp+0x38]
    int esi = 0;            // close-ball flag (0x540B31)
    const std::uint32_t sub = static_cast<std::uint32_t>(p.stateSub2);
    if (sub >= 1) { // 0x540D36
        if (sub == 1) {
            if (static_cast<std::uint32_t>(p.stateTimer) >= 4) {
                p.stateTimer = 0;
                p.stateSub2 = 0;
            }
            if (!noDrift) return;
            goto L540ab9;
        }
        if (sub == 2) {
            p.speedCap = fast ? p.sprintMax : p.runMax;
            Motion_setTargetAlt(w, pid, t0c);
            return;
        }
        goto L540ab9;
    }
    if (p.ctrl == 0) { // 0x540F0E AI
        if (p.distBall > 0x5f && noDrift) {
            Motion_setTargetAlt(w, pid, t0c);
            return;
        }
        const int r = TeamAI_onBallAct_4DD324(w, pid);
        if (r != 0) return;
        p.stateTimer = r;
        ++p.stateSub2;
        goto L5409c9;
    }
    if (p.distBall > 0x42 && noDrift) {
        Motion_setTargetAlt(w, pid, t0c);
        return;
    }
    {
        Vec3 tmp;
        Player_humanMoveTarget(w, pid, tmp);
        const ControllerState& c = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
        if (c.dirX == 0 && c.dirZ == 0) { // 0x540DAE
            dv[0] = dv[2] = dv[1] = 0;
            p.padOffZ = 0;
            p.padOffX = 0;
            goto L5409c9;
        }
        std::int32_t a = angSigned_W3(atan2i(p.padOffX, p.padOffZ));
        const Vec3& b = w.ball.posI;
        if (w.pitch.halfWidth - iabs(b.x) < 0x60) { // near a touchline: steer back in
            if (b.x > 0) {
                if (a > 0) {
                    if (a < 0x55) a = -0xe;
                    else if (a > 0x1ab) a = 0x20e;
                }
            } else if (a < 0) {
                if (a > -0x55) a = 0xe;
                else if (a < -0x1ab) a = 0x1f2;
            }
        } else if (w.pitch.halfLength - iabs(b.z) < 0x60) { // 0x540E1F near a goal line
            const int half = (p.posI.z + w.pitch.thirdLineZ[team.side] >= 0) ? 1 : 0;
            if (half == team.side || iabs(b.x) >= w.pitch.goalHalfWidth) { // 0x540E60
                if (b.z > 0) {
                    if (a < 0x100 && a > 0xab) a = 0x10e;
                    else if (a > -0x100 && a < -0xab) a = -0x10e;
                } else {
                    if (a > 0x100 && a < 0x155) a = 0xf2;
                    else if (a < -0x100 && a > -0x155) a = -0xf2;
                }
            }
        }
        Player_dribbleVector(w, pid, octLength(p.padOffX, p.padOffZ), a, dv); // 0x5409B0
    }
L5409c9:
    if (p.stateArg0 == 0) {
        p.stateArg0 = 1;
        if (noDrift && w.ball.posI.y < 0x20 && (dv[0] != 0 || p.speed > 0x8000)) { // 0x540A00 first touch
            const Fixed spd = p.speed;
            int moving = 0;
            std::int32_t reach = 0x42;
            Angle dir;
            if (spd > 0x8000) {
                dir = p.facingDesired;
                reach = (((spd + 0x8000) << 2) >> 16) + 0x42;
                moving = 1;
            } else {
                dir = dv[3];
                if (spd > 0x4ccc && angAbs_W3(dir - p.facingDesired) < 0xab) {
                    reach = (((p.speed + 0x8000) << 2) >> 16) + 0x42;
                    moving = 1;
                }
            }
            if (fast && moving && p.turnClass == 0) reach += 0x20;
            if (iabs(angSigned_W3(p.facing - dir)) > 0xab) {
                const Vec3 t18 = offsetInDirection(p.posI, reach, dir); // y unused below
                if (iabs(t18.x) < w.pitch.halfWidth && iabs(t18.z) < w.pitch.halfLength) { // 0x540FC7
                    Ball_kickGroundPass(w, pid, t18, 0xa, 0);
                    Sim_scheduleSplit(w, 0xa);
                    Motion_setTargetAlt(w, pid, t18);
                    p.stateTimer = 0;
                    p.stateSub2 = 2;
                    return;
                }
            }
        }
    }
L540ab9:
    {
        std::int32_t d;
        if (noDrift) { // 0x541061
            if (p.distBall > 0x42) {
                Motion_setTargetAlt(w, pid, t0c);
                return;
            }
            goto L540b0f;
        }
        if (p.anim.flags() & 4) goto L54088e;
        if (dv[0] != 0) {
            ang38 = dv[3];
            d = angAbs_W3(p.f1F4 - ang38);
        } else { // 0x54101B
            ang38 = 0x1639;
            if (p.f1F4 == 0x1639 || Anim_frame(w, pid) >= (Anim_lengthFrames(w, pid) >> 1)) d = 0;
            else d = 0x1639;
        }
        if (d < 0x39) goto L54088e; // 0x540B06
    }
L540b0f:
    p.flags22c |= 0x100000;
    esi = (p.distBall < 0x2f) ? 1 : 0;
    if (dv[0] != 0) { // 0x541089
        ang38 = dv[3];
        const std::int32_t s = angSigned_W3(ang38 - p.facing);
        const std::int32_t u = angAbs_W3(ang38 - p.facing);
        if (u > 0x155) {
            row = s > 0 ? 4 : 3;
            dirCls = 1;
        } else if (u > 0xc7) {
            row = s > 0 ? 2 : 1;
            dirCls = 2;
        }
    } else if (w.ball.speed > 0x8000 && one24 != 0) {
        ang38 = 0x1639;
        row = 0;
        dirCls = 3;
        goto L540b74;
    }
    if (row == 6) { // 0x5410E5
        if (!noDrift) {
            Anim_start(w, pid, 0x166, 0, p.facingDesired);
            if (w.ball.dribbler == pid) w.ball.dribbler = -1;
        }
        goto L540bd8;
    }
L540b74:
    if (!(noDrift && p.turnClass != 0)) { // 0x541161 / 0x540B7F
        if (!noDrift && dirCls == p.f1F8) goto L54088e;
        if (Anim_trySkill(w, pid, row, p.facing)) { // 0x540B96 -> 0x541177
            Player_skillMoveBall(w, Player_animId(w, pid), p.facing, pid);
            p.f1F4 = ang38;
            p.f1F8 = dirCls;
            Action_dribbleTouch_5404A8(w, pid);
            return;
        }
    }
    if (row == 0) { // 0x540BB1
        Player_dribbleVector(w, pid, 0x20, w.ball.heading, dv);
        ang38 = dv[3];
    }
L540bd8:
    if (p.distBall <= p.distBallPrev && p.distBall > 0x2f) { // 0x5411B5
        Motion_setTargetAlt(w, pid, t0c);
        return;
    }
    if (dv[0] == 0) { // 0x5411D6
        Player_moveToward_4BE804(w, pid, p.posI);
        return;
    }
    if (p.speed > 0x4ccc) {
        std::int32_t e = angSigned_W3(ang38 - p.facingDesired);
        if (e < -0x55) e = -0x55;
        else if (e > 0x55) e = 0x55;
        ang38 = p.facingDesired + e;
    }
    {
        // 0x540C3D: DAT_0065EED8 surface row / DAT_0065EED4 column
        const int grip = (w.opt.surfaceRow == 0 || w.opt.surfaceColumn == 1) ? 1 : 0;
        std::int32_t sh;
        if (!fast || grip) sh = grip ? 0xa : 0xb;
        else sh = 0xc;
        if (esi) sh += (0x2f - p.distBall) >> 2;
        const unsigned cl = static_cast<unsigned>(sh) & 31;
        Fixed len = octLength(static_cast<std::int32_t>(static_cast<std::uint32_t>(dv[1]) << cl) + p.m98,
                              static_cast<std::int32_t>(static_cast<std::uint32_t>(dv[2]) << cl) + p.m9c);
        Fixed mx = p.speed + (p.speed >> 1);
        const Fixed cap = (p.runMax >> 1) + p.runMax;
        if (mx > cap) mx = cap;
        if (len < 0x20000) len = 0x20000;
        else if (len > mx) len = mx;
        if (fast) {
            p.flags22c &= ~0x40000u;
        } else if ((p.flags22c & 0x40000) ||
                   ((p.flags22c & 0x20000) && p.targetDist < 0x40 && len > 0x4c000)) { // 0x541246
            const std::uint32_t bit17 = (p.flags22c >> 17) & 1;
            p.flags22c = (p.flags22c & ~0x40000u) | (bit17 << 18);
            len = 0x4c000;
        }
        Ball_pushInDirection(w, pid, ang38, len); // 0x540CF4
        t0c = ballAhead5_W3(w);
        Motion_setTargetAlt(w, pid, t0c);
        return;
    }
L54088e:
    Motion_setTargetAlt(w, pid, w.ball.posI);
}

namespace {

// `and 0x3ff; cmp 0x200; jle; 0x400 - a`: absolute angle difference 0..0x200.
std::int32_t angDiff_541468(std::int32_t a) {
    a &= 0x3ff;
    return a > 0x200 ? 0x400 - a : a;
}

// The exe dereferences the opponent's possWinner (team+0x1B0) without a NULL check on several
// paths (a crash in the original). ponytail: a scratch object stands in for NULL so the port has
// no UB; it is never hashed. Replace with an assert if NULL proves unreachable.
PlayerState& opp_541468(World& w, int id) {
    static PlayerState dummy{};
    return id >= 0 ? P(w, id) : dummy;
}
Vec3 oppTarget_541468(World& w, int id) { return id >= 0 ? Motion_targetVec(w, id) : Vec3{}; }

// State309 frame-local target [esp+0x1C]: stateSub2 5 (distBall growing) and the y of stateSub2 7
// keep whatever the caller left on the stack. Not reproducible; zero.
Vec3 untraced_staleTarget_541468() { return {}; }

// Message to another player's +0x218 slot (inlined 17x in the exe): only if +0x21C is clear and
// the top 3 bits of +0x224 are <= 1.
void postMsg_541468(World& w, int to, std::int32_t code) {
    PlayerState& o = opp_541468(w, to);
    if (o.f21C == 0 && (static_cast<std::uint32_t>(o.f224) >> 29) <= 1) {
        o.f218 = code;
        o.f220 = 1;
        o.f224 = 0x20000002;
    }
}

// 0x5412A4 (EDX = chasing): should the AI chaser sprint.
bool shouldSprint_5412A4(World& w, int pid, bool chasing) {
    PlayerState& p = P(w, pid);
    const int side = w.teams[p.team].side;
    const int oid = w.teams[w.teams[p.team].opponent].possWinner;
    if (oid < 0) return false;
    if (chasing) {
        if (p.targetDist <= 0x140) return false;
        return (p.posI.z - w.ball.posI.z >= 0 ? 1 : 0) != side;
    }
    const PlayerState& o = P(w, oid);
    const std::int32_t diff = o.targetDist - p.targetDist;
    const std::int32_t adiff = iabs(diff);
    // 0x541319..0x5413BF: both branches reduce to one penalty-area test.
    if (Pitch_inPenaltyArea(w, p.posI)) return true;
    const OctDist d = octDistance(p.posI, o.posI);
    if (d.dist < 0xf0 && o.speed >= p.speed) return true;
    if (diff >= 0) return false;
    if ((d.dz >= 0 ? 1 : 0) == side) return true;
    if ((p.posI.z >= 0 ? 1 : 0) == side && adiff < 0x1e0) return true;
    if ((p.posI.z + w.pitch.thirdLineZ[side] >= 0 ? 1 : 0) != side) return adiff < 0x140;
    return false;
}

// 0x4DBAE4 (EDX = opponent possWinner): AI may contest the ball at its y=142 descent point.
bool canContestHigh_4DBAE4(World& w, int pid, int oid) {
    PlayerState& p = P(w, pid);
    const std::int32_t c = w.teams[p.team].ctrl1f4;
    if (oid < 0) return false;
    if (c != 2) {
        const std::uint32_t mask = 1u << ((c << 2) & 0xff);
        if ((w.rng.next() & mask) == 0) return false;
    }
    const Vec3& b = w.ball.cross142Pos; // 0x669184
    const OctDist d = octDistance(p.posI, b);
    const std::int32_t frames = w.ball.cross142Frame - w.ball.consumed;
    const std::int32_t run =
        Motion_predictDistance(w, pid, p.speedCap, atan2i(d.dx, d.dz), frames, 0, 0) - d.dist;
    if (run < 96) return false; // exe 0x59FD34
    const PlayerState& o = P(w, oid);
    const std::int32_t od = octLength(b.x - o.posI.x, b.z - o.posI.z);
    if (iabs(b.x) < w.pitch.penAreaHalfWidth && iabs(b.z) > w.pitch.penAreaLineZ) {
        if ((b.z >= 0 ? 1 : 0) != w.teams[p.team].side) return true;
        return d.dist - od < 240; // exe 0x59FD38
    }
    return d.dist - od < 144; // exe 0x59FD3C
}

// 0x4DBC24 (EDX = opponent possWinner, EBX = out target, ECX = out run flag): chase target when the
// ball runs along a touchline / goal line. Returns nonzero when out was set.
bool lineChaseTarget_4DBC24(World& w, int pid, int oid, Vec3& out, std::int32_t& run) {
    PlayerState& p = P(w, pid);
    const BallState& B = w.ball;
    out = B.posI;
    run = 1;
    const std::int32_t adx = iabs(p.dxBall), adz = iabs(p.dzBall);
    const bool byTouch = iabs(B.posI.x) > w.pitch.halfWidth - 128 && adx > 2 * adz;  // exe 0x59FD20
    const bool byGoal = iabs(B.posI.z) > w.pitch.halfLength - 128 && adz > 2 * adx;  // exe 0x59FD20
    if (byTouch == byGoal) return false;
    const int s5 = B.stamp[5], s6 = B.stamp[6]; // sar of the dwords at T+0xBD8 / T+0xBDA
    if (s5 != -1 && Ball_ringIndex(w, s5 - B.consumed) >= 0 && s6 != -1 &&
        Ball_ringIndex(w, s6 - B.consumed) >= 0 && (B.posI.z >= 0 ? 1 : 0) != w.teams[p.team].side)
        return false;
    const std::int32_t reach = oid >= 0 ? P(w, oid).distBall >> 2 : 0;
    const std::int32_t outFrames = s5 == -1 ? 0 : s5 - B.consumed;
    if (s5 != -1 && Ball_ringIndex(w, s5 - B.consumed) >= 0 &&
        w.teams[opp_541468(w, B.prevPossessor).team].index != w.teams[p.team].index &&
        outFrames < reach + 0x3c) {
        const Vec3 r = offsetInDirection(B.posI, 0x60, p.angleBall - 0x200);
        out.x = r.x;
        out.z = r.z;
        return true;
    }
    std::int32_t step = 0x70;
    const Vec3& tv = Motion_targetVec(w, pid);
    if (byTouch) {
        const std::int32_t px = p.posI.x;
        bool near = iabs(tv.x - px) < 0x20;
        if (!near) {
            if (px > 0 && out.x > 0 && px - out.x >= (step >> 2)) near = true;
            else if (px < 0 && out.x < 0 && px - out.x <= -(step >> 2)) near = true;
        }
        if (near) step = 0x10;
        out.z += p.posI.z - out.z > 0 ? step : -step;
        out.x += out.x > 0 ? step : -step;
    } else {
        const std::int32_t pz = p.posI.z;
        bool near = iabs(tv.z - pz) < 0x20;
        if (!near) {
            if (pz > 0 && out.z > 0 && pz - out.z >= (step >> 2)) near = true;
            else if (pz < 0 && out.z < 0 && pz - out.z <= -(step >> 2)) near = true;
        }
        if (near) step = 0x10;
        out.x += p.posI.x - out.x > 0 ? step : -step;
        out.z += out.z > 0 ? step : -step;
    }
    run = 0;
    return true;
}

bool humanOpts4_541468(const World& w, const PlayerState& p) {
    return p.ctrl != 0 && (w.controllers[static_cast<std::size_t>(p.ctrl - 1)].flags1a0 & 4);
}

void takeBall_541468(World& w, int pid) {
    const int prev = w.ball.possessor;
    Ball_takePossession(w, pid, 0);
    if (w.ball.possessor != prev) w.emit(EventType::Possession, P(w, pid).team, pid, pid, prev);
}

} // namespace

// 0x541468: chase/contest a loose ball in the air (sub 0 plan, 1 run to y=85 point, 2 chase,
// 3 contest at y=142, 4 meet at y=85, 5 wait for the drop, 6 face the ball, 7 mark the possessor).
void State309(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& team = w.teams[p.team];
    BallState& B = w.ball;
    std::int32_t moveRun = 1;                                   // [esp+0x34]
    const bool ballMoving = B.vel.x != 0 || B.vel.z != 0;       // [esp+0x38]
    std::int32_t msg = 0;                                       // [esp+0x40]
    const int oid = w.teams[team.opponent].possWinner;          // [esp+0x44]
    Vec3 T = untraced_staleTarget_541468();                     // [esp+0x1C]
    const int side = team.side;

    // 0x5414A4: incoming message.
    if (p.f220 != 0) {
        const std::uint32_t kind = static_cast<std::uint32_t>(p.f224) & 0x1fffffff;
        p.f220 = 0;
        p.f224 &= 0x1fffffff;
        if (kind == 1) {
            msg = p.f218;
        } else if (kind == 2) {
            const std::int32_t code = p.f218;
            if (code == 1) {
                p.stateSub2 = 3;
                p.stateTimer = 0;
            } else if (code == 2 && p.stateSub2 == 2) {
                p.stateSub2 = 4;
                p.stateTimer = 0;
            } else if (code == 4 && p.stateSub2 == 2) {
                p.stateTimer = 0;
                p.stateSub2 = 5;
            } else if (code == 6 && p.stateSub2 == 7) {
                p.stateTimer = 0;
                p.stateSub2 = 0;
            }
            if (oid >= 0) {
                const PlayerState& o = P(w, oid);
                if (o.targetDist < 0x120 && o.distBall <= o.distBallPrev && p.ctrl == 0 &&
                    (B.landing.z + w.pitch.thirdLineZ[side] >= 0 ? 1 : 0) == side && p.stateSub2 != 7 &&
                    p.stateSub2 != 0 && code != 6 && code != 5 && (p.posI.z - o.posI.z >= 0 ? 1 : 0) == side) {
                    const Vec3 otv = Motion_targetVec(w, oid), mtv = Motion_targetVec(w, pid);
                    const std::int32_t dd = octLength(B.posI.x - otv.x, B.posI.z - otv.z) -
                                            octLength(B.posI.x - mtv.x, B.posI.z - mtv.z);
                    const std::int32_t dt = o.targetDist - p.targetDist;
                    std::int32_t lim = code == 7 ? 0 : 0x30, tlim = 0x50;
                    if (team.ctrl1f4 == 0) {
                        lim *= 3;
                        tlim = 0xf0;
                    }
                    if (dd < -lim || (dd < lim && p.distBall > 0x80 && dt < tlim)) {
                        p.stateTimer = 0;
                        p.stateSub2 = 7;
                    }
                }
            }
        }
    }

    // 0x5414DE
    if (p.countdown != 0 || w.match.periodType == 3 || (w.opt.gameMode == 1 && w.match.trainDrill == 3) ||
        (p.ctrl == 0 && msg != 0) || B.possessor != -1 || w.match.phase != 9) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    Player_refreshSpeedCap(w, pid);
    if (team.humans != 0 || p.stateSub2 == 4) {
        const std::int32_t h = B.apexHeight > B.posI.y ? B.apexHeight : B.posI.y;
        if (h > 0x55 && B.consumed <= B.cross85Frame && B.possessor == -1 && p.distBall <= p.distBallPrev &&
            p.posI.y == 0 && Player_canReachCross85(w, pid)) {
            Player_setState(w, pid, 0x304, 0, 0, false, true);
            return;
        }
    }
    if (team.oneTouch != -1 && team.oneTouch == pid) Player_requestSprint(w, pid);

    const auto shootCheck = [&](std::int32_t frames) { // 0x541FA3 tail
        if (!Action_canShootAtGoal(w, pid, B.cross85Pos, frames)) return false;
        Player_setState(w, pid, 0x301, 0, 0, false, true);
        return true;
    };

    switch (p.stateSub2) { // jump table exe 0x541448
    case 0: { // 0x5415B5 (every path ends with T = landing; the cross142/85 copies are overwritten)
        if (B.landingFrame < 0 && B.cross85Frame < 0 && (B.posI.y > 0 || B.vel.y != 0)) {
            T = B.landing;
            break;
        }
        if (team.humans == 0 && canContestHigh_4DBAE4(w, pid, oid)) {
            p.stateTimer = 0;
            p.stateSub2 = 3;
        } else {
            const std::int32_t h = B.apexHeight > B.posI.y ? B.apexHeight : B.posI.y;
            p.stateTimer = 0;
            p.stateSub2 = (h > 0x72 || B.posI.y > 0x72) ? 1 : 2;
        }
        T = B.landing;
        break;
    }
    case 1: { // 0x541C8E
        const std::int32_t frames = B.cross85Frame - B.consumed;
        const OctDist d = octDistance(p.posI, B.cross85Pos);
        const std::int32_t pred = Motion_predictDistance(w, pid, p.speedCap, atan2i(d.dx, d.dz), frames, 0, 0);
        if (pred - d.dist <= 0x30) {
            T = B.landing;
            p.stateTimer = 0;
            p.stateSub2 = 2;
        } else {
            T = B.cross85Pos;
            Sim_scheduleSplit(w, frames - 4);
            p.stateTimer = 0;
            p.stateSub2 = 4;
        }
        break;
    }
    case 2: { // 0x541D2D
        postMsg_541468(w, oid, 3);
        if (opp_541468(w, oid).state == 0x304) {
            p.stateTimer = 0;
            p.stateSub2 = 3;
            break;
        }
        const std::int32_t h = B.apexHeight > B.posI.y ? B.apexHeight : B.posI.y;
        if (h > 0x72) { // 0x542051
            const std::int32_t frames = B.cross85Frame - B.consumed;
            if ((team.humans == 0 || humanOpts4_541468(w, p)) && shootCheck(frames)) return;
            if (p.targetDist < 0x10 && frames > 0x3c) {
                p.stateTimer = 0;
                p.stateSub2 = 5;
            }
            T = B.landing;
            break;
        }
        if (team.oneTouch == pid && !(p.anim.b34[3] & 4) && p.distBall < 0x80) { // byte +0x9B
            takeBall_541468(w, pid);
            Player_executeKick(w, pid, team.oneTouchCode, B.posI.y);
            Player_setState(w, pid, 0x300, 0, 0, false, false);
            return;
        }
        if (lineChaseTarget_4DBC24(w, pid, oid, T, moveRun)) break;
        moveRun = 1;
        const std::int32_t a = angDiff_541468(p.facingDesired - Motion_dirToTarget(w, pid));
        const PlayerAttr& at = w.attrs[static_cast<std::size_t>(p.attr)];
        const bool fast = at.f10 != 0 && !(at.at(0x40) & 0x40);
        std::int32_t spd;
        if (fast) {
            spd = (p.speed + p.sprintMax) >> 1;
            if (a > 0x18e) spd -= 0x30000;
            else if (a > 0x100) spd -= 0x20000;
        } else {
            spd = (p.speed + p.runMax) >> 1;
            if (a > 0x18e) spd -= 0x20000;
            else if (a > 0x100) spd -= 0x10000;
        }
        if (spd < 0x10000) spd = 0x10000;
        const std::int32_t frames = Player_solveIntercept(w, p.posI, spd, B.posI, B.vel.x, B.vel.z,
                                                          w.pitch.halfWidth, w.pitch.halfLength, T);
        if (frames < 0) {
            T = B.landing;
        } else if (team.humans == 0) {
            Vec3 at16;
            Ball_predictPos(w, frames, 0, &at16);
            if (Action_canShootAtGoal(w, pid, at16, frames)) {
                Player_setState(w, pid, 0x301, 0, 0, false, true);
                return;
            }
        }
        if ((fast ? 0xa0 : 0x50) < p.targetDist) moveRun = 0;
        if (!ballMoving || p.distBall > p.distBallPrev) break;
        if (angDiff_541468(p.angleBall - B.heading) > 0x1ef && angDiff_541468(p.angleBall - p.facingDesired) < 0x11) {
            T = B.posI;
            moveRun = 0;
            p.stateSub2 = 6;
            p.stateTimer = 0;
        }
        break;
    }
    case 3: // 0x5420BB
        postMsg_541468(w, oid, 1);
        if (B.consumed < B.cross142Frame) {
            const bool check = team.humans == 0 ? (w.rng.next() & 0x3f) == 0 || humanOpts4_541468(w, p)
                                                : humanOpts4_541468(w, p);
            if (check && shootCheck(B.cross85Frame - B.consumed)) return;
            T = B.cross142Pos;
            if (p.targetDist < 0x100 && team.humans == 0) {
                Player_setState(w, pid, 0x303, 0, 0, false, true);
                return;
            }
        } else {
            T = B.landing;
            postMsg_541468(w, oid, 3);
            p.stateTimer = 0;
            p.stateSub2 = 2;
        }
        break;
    case 4: // 0x542202
        postMsg_541468(w, oid, 2);
        if (B.consumed < B.cross85Frame) {
            const bool ai = team.humans == 0 || humanOpts4_541468(w, p);
            if (ai && shootCheck(B.cross85Frame - B.consumed)) return;
            T = B.cross85Pos;
            const std::int32_t d = octLength(T.x - p.posI.x, T.z - p.posI.z);
            if (ai && d < 0x80 && B.cross85Frame - B.consumed > 0x3c && TeamAI_tryHeaderPass_4DF2CC(w, pid, 0x55))
                Player_setState(w, pid, 0x30a, 0, 0, false, true); // exe continues into the tail
            else
                T = B.cross85Pos;
        } else {
            T = B.landing;
            postMsg_541468(w, oid, 3);
            p.stateTimer = 0;
            p.stateSub2 = 2;
        }
        break;
    case 5: // 0x542383
        postMsg_541468(w, oid, 4);
        if (p.distBall > p.distBallPrev) { // T stays stale (untraced_staleTarget_541468)
            p.stateTimer = 0;
            p.stateSub2 = 2;
            break;
        }
        if ((team.humans == 0 || humanOpts4_541468(w, p)) && shootCheck(B.cross85Frame - B.consumed)) return;
        T = Motion_turnSettled(w, pid) ? B.cross85Pos : B.landing;
        break;
    case 6: // 0x54243F
        postMsg_541468(w, oid, 3);
        T = B.posI;
        if (p.targetDist < 0x10 && !Motion_facingBall(w, pid)) {
            moveRun = 1;
            break;
        }
        moveRun = 0;
        if (angDiff_541468(p.angleBall - B.heading) < 0x1ef || angDiff_541468(p.angleBall - p.facingDesired) > 0x11) {
            p.stateTimer = 0;
            p.stateSub2 = 2;
        }
        break;
    case 7: { // 0x54251E: stand between the possessor and our goal
        postMsg_541468(w, oid, 5);
        const PlayerState& o = opp_541468(w, oid);
        const Angle a = atan2i(-o.posI.x, w.pitch.goalLineZ[side] - o.posI.z);
        const Vec3 r = offsetInDirection(oppTarget_541468(w, oid), team.ctrl1f8 == 0 ? 0x70 : 0x40, a);
        T.x = r.x; // y untouched (stale)
        T.z = r.z;
        if (o.state == 0 || o.state == 4) {
            p.stateTimer = 0;
            p.stateSub2 = 0;
        }
        break;
    }
    default: break;
    }

    // 0x541614: human steering overrides the target.
    if (p.ctrl != 0) {
        const std::int32_t h = B.apexHeight > B.posI.y ? B.apexHeight : B.posI.y;
        if ((h < 0x39 || pid == B.prevPossessor) && team.oneTouch == -1 && p.distBall > 0xc0 &&
            pid != w.match.player2a0 && pid != team.f1e0) {
            Vec3 H;
            Player_humanMoveTarget(w, pid, H);
            p.flags22c |= 0x100000; // byte +0x22E |= 0x10
            const std::int32_t dx = H.x - p.posI.x, dz = H.z - p.posI.z;
            if ((dx == 0 && dz == 0) ||
                angDiff_541468(atan2i(dx, dz) - atan2i(T.x - p.posI.x, T.z - p.posI.z)) > 0x55) {
                postMsg_541468(w, oid, 6);
                moveRun = 1;
                T = H;
            }
        }
    }
    Pitch_clampToField(w, T);
    if (p.ctrl == 0 && shouldSprint_5412A4(w, pid, p.stateSub2 == 7)) Player_requestSprint(w, pid);
    if (moveRun) Player_moveToward_4BE804(w, pid, T);
    else Motion_setTargetAlt(w, pid, T);

    // 0x54177D: take the ball when it drops in reach.
    if (msg != 0) return;
    if (p.distBall > p.distBallPrev && p.distBall > 0x20) return;
    if (p.distBall > 0x60 || B.posI.y > 0x72) return;
    if (p.distBall > 0x20 && angDiff_541468(p.angleBall - p.facing) > 0x155) return;
    {
        const PlayerState& o = opp_541468(w, oid);
        if (p.distBall > o.distBall && o.countdown == 0 && angDiff_541468(o.angleBall - o.facing) < 0x155 &&
            angDiff_541468(o.angleBall - p.angleBall) < 0x55)
            return;
    }
    if (p.ctrl == 0 && team.humans != 0 && p.distBall < 0x60 && team.f1b8 == -1 && p.slot != 0 && pid != team.oneTouch)
        Player_assignControlToReceiver(w, pid, -1);
    if (Player_claimRange(w, pid) < p.distBall) return;
    if (B.consumed <= 0xf && B.speed >= 0x190000) return;
    bool take = false;
    if (p.ctrl == 0 && B.posI.y < 0x39) {
        const PlayerState& o = opp_541468(w, oid);
        if ((p.posI.z - w.pitch.thirdLineZ[side] >= 0 ? 1 : 0) == side && o.distBall < 0x60) take = true;
        else if ((p.posI.z >= 0 ? 1 : 0) == side && Pitch_inPenaltyArea(w, p.posI) && o.distBall < 0xc0) take = true;
    }
    if (take) {
        takeBall_541468(w, pid);
        TeamAI_onBallWon_4DC614(w, pid, 1);
        return;
    }
    Player_setState(w, pid, 0x307, 0, 0, false, true);
}

namespace {

constexpr std::int32_t kSkillMoves_58B6EC[10][7] = { // exe 0x58B6EC (10 x 0x1C bytes)
    {427, 0, 0, 0, 0, 0, 7}, {425, 0, 0, 0, 0, 0, 7}, {429, 0, 0, 0, 0, 0, 5}, {439, 0, 0, 0, 0, 0, 7},
    {428, 0, 0, 0, 0, 0, 7}, {426, 0, 0, 0, 0, 0, 7}, {430, 0, 0, 0, 0, 0, 5}, {438, 0, 0, 0, 0, 0, 7},
    {431, 0, 0, 0, 0, 0, 3}, {432, 0, 0, 0, 0, 0, 3},
};
constexpr std::uint32_t kSkillOdds_4E01E0[3] = {0, 7, 18}; // exe 0x4E01E0 (copied to the stack, [team.ctrl1f8])

// u16 player+0x1E8 / +0x1EA (low / high half of pendingCode), read signed like `sar 0x10`.
std::int32_t code1E8(const PlayerState& p) { return static_cast<std::int16_t>(p.pendingCode & 0xffff); }
std::int32_t code1EA(const PlayerState& p) { return static_cast<std::int16_t>(p.pendingCode >> 16); }
void setCode1E8(PlayerState& p, std::uint32_t v) { p.pendingCode = (p.pendingCode & 0xffff0000u) | (v & 0xffff); }
void setCode1EA(PlayerState& p, std::uint32_t v) { p.pendingCode = (p.pendingCode & 0xffffu) | (v << 16); }

// Shared prologue of 0x542D78 / 0x4E0474: byte player+0x184 |= 1, anim.req = moveReq >> 16 (sar).
void holdBallReq_W5(PlayerState& p) {
    p.moveReq |= 1;
    p.anim.req = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.moveReq) >> 16);
}

// 0x542893.. / 0x5429D0.. (4 copies in 0x5426CC): ask the opponent's possession winner to react
// (+0x218 kind, +0x220 = 1, +0x224 = 0x20000002) unless it is busy (+0x21C) or +0x224 >> 29 > 1.
void alertOppWinner_5426CC(World& w, const PlayerState& p, std::int32_t kind) {
    const int pw = w.teams[w.teams[p.team].opponent].possWinner;
    if (pw < 0) return; // exe dereferences it unchecked
    PlayerState& o = P(w, pw);
    if (o.f21C != 0 || (static_cast<std::uint32_t>(o.f224) >> 29) > 1) return;
    o.f218 = kind;
    o.f220 = 1;
    o.f224 = 0x20000002;
}

bool humanCanKick_5426CC(const World& w, const PlayerState& p) {
    return p.ctrl != 0 && !(w.controllers[p.ctrl - 1].flags1a0 & 4);
}

} // namespace

// 0x5426CC: run to the ball's y=85 descent point (sub 1/3) or its landing point (sub 2/4) and
// volley/head it on arrival: humans fire the pending code (+0x1E8), AI the planned kick.
void State30A(World& w, int pid) {
    PlayerState& p = P(w, pid);
    BallState& b = w.ball;
    if ((b.possessor >= 0 && b.possessor != pid) || w.match.phase != 9) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    if (p.f220 != 0 && p.ctrl == 0) {
        if ((static_cast<std::uint32_t>(p.f224) & 0x1fffffff) == 2) {
            p.f224 = static_cast<std::int32_t>(static_cast<std::uint32_t>(p.f224) & 0x1fffffff);
            p.f220 = 0;
            if (p.f218 == 1 && b.cross142Frame - b.consumed > 0x3c) {
                Player_setState(w, pid, 0x303, 0, 0, false, true); // 0x542820
                return;
            }
        }
        p.f220 = 0;
        p.f224 = static_cast<std::int32_t>(static_cast<std::uint32_t>(p.f224) & 0x1fffffff);
    }
    switch (p.stateSub2) { // jump table 0x5426B8
    case 0: { // 0x542773
        const std::int32_t d85 = octLength(b.cross85Pos.x - p.posI.x, b.cross85Pos.z - p.posI.z);
        const std::int32_t dLand = octLength(b.landing.x - p.posI.x, b.landing.z - p.posI.z);
        const std::int32_t reach = p.ctrl != 0 ? 0x1e0 : 0xf0;
        if ((d85 < dLand || p.ctrl != 0) && !(d85 > reach)) {
            if (b.consumed < b.cross85Frame) { // 0x5427D2
                if (Motion_setTargetAlt(w, pid, b.cross85Pos)) Player_moveToward_4BE804(w, pid, b.cross85Pos);
                p.stateTimer = 0;
                p.stateSub2 = 1;
                return;
            }
        } else if (d85 < dLand || p.ctrl != 0) {
            Player_dropBallAndReset(w, pid); // 0x5427D0
            return;
        }
        // 0x542846
        if (dLand > reach || b.consumed + 0x1e >= b.landingFrame) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (Motion_setTargetAlt(w, pid, b.landing)) Player_moveToward_4BE804(w, pid, b.landing);
        p.stateTimer = 0;
        p.stateSub2 = 2;
        return;
    }
    case 1: { // 0x542893 head/volley at the y=85 point
        alertOppWinner_5426CC(w, p, 2);
        if (p.targetDist > 0xc0) {
            Player_moveToward_4BE804(w, pid, b.cross85Pos);
            // exe then calls Pitch_clampToField 0x455F38 on an uninitialised local: no state effect
            return;
        }
        if (b.consumed >= b.cross85Frame) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (humanCanKick_5426CC(w, p)) {
            Player_executeKick(w, pid, code1E8(p), 0x55);
            p.kick.deferredCode = code1E8(p);
        }
        const Vec3 loc = offsetInDirection(b.cross85Pos, p.kick.animOut44, p.kick.animHeading - 0x200); // exe leaves y unset
        if (Motion_setTargetAlt(w, pid, loc)) Player_moveToward_4BE804(w, pid, loc);
        Sim_scheduleSplit(w, b.cross85Frame - p.kick.contactFrame - b.consumed);
        p.stateTimer = 0;
        p.stateSub2 = 3;
        return;
    }
    case 2: { // 0x5429D0 kick at the landing point
        alertOppWinner_5426CC(w, p, 3);
        if (p.targetDist > 0x120) {
            Player_moveToward_4BE804(w, pid, b.landing);
            // exe then calls Pitch_clampToField 0x455F38 on an uninitialised local: no state effect
            return;
        }
        if (p.distBall > p.distBallPrev) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        if (humanCanKick_5426CC(w, p)) {
            const std::uint32_t c = p.pendingCode & 0xffff;
            if (c == 9) setCode1E8(p, 5);
            else if (c == 0xa) setCode1E8(p, 4);
            else if (c == 0xb) setCode1E8(p, 6);
            Player_executeKick(w, pid, code1E8(p), 0); // 0x542A7D
            p.kick.deferredCode = code1E8(p);
        } else {
            const std::int32_t rel[3] = {p.kick.vec.dist, p.kick.vec.dx, p.kick.vec.dz}; // EDX = &kick.vec
            if (p.kick.mode == 0) { // 0x542B63
                const bool lifted = p.kick.lift > 0;
                Kick_planToPoint(w, pid, rel, nullptr, p.kick.lift, -1, 0, lifted ? 1 : 2, lifted ? 1 : 0, p.kick.mode, 2);
            } else { // 0x542BB3
                const Vec3 t = p.kick.target;
                Kick_planToPoint(w, pid, rel, &t, p.kick.lift, t.y, 0, 3, 2, 0, 3);
            }
        }
        const Vec3 loc = offsetInDirection(b.landing, p.kick.animOut44, p.kick.animHeading - 0x200); // 0x542A9E (y unset in exe)
        if (Motion_setTargetAlt(w, pid, loc)) Player_moveToward_4BE804(w, pid, loc);
        Sim_scheduleSplit(w, b.landingFrame - p.kick.contactFrame - b.consumed);
        p.stateTimer = 0;
        p.stateSub2 = 4;
        return;
    }
    case 3:   // 0x542BE6
    case 4: { // 0x542CBB
        alertOppWinner_5426CC(w, p, p.stateSub2 == 3 ? 2 : 3);
        if (p.targetDist < 0x20) Player_moveToward_4BE804(w, pid, Motion_targetVec(w, pid));
        const std::int32_t frame = p.stateSub2 == 3 ? b.cross85Frame : b.landingFrame;
        if (frame - p.kick.contactFrame > b.consumed) return;
        if (p.distBall > p.distBallPrev && p.targetDist > 0x30) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        Player_setState(w, pid, 0x300, 0, 0, false, true); // 0x542C9B
        return;
    }
    default: return;
    }
}

// 0x542D78: team-mate of the possessor runs to the spot of the planned pass (kick target or
// possessor/restart spot + kick offset), facing the ball.
void State30B(World& w, int pid) {
    PlayerState& p = P(w, pid);
    holdBallReq_W5(p);
    if (p.stateSub2 != 0) return;
    const int poss = w.ball.possessor;
    if (poss < 0 || P(w, poss).team != p.team) {
        Player_dropBallAndReset(w, pid); // 0x542E91
        return;
    }
    const PlayerState& q = P(w, poss);
    p.headAim = p.angleBall;
    p.flags22c &= ~0x10u;
    Vec3 loc{}; // y left unset by the exe on the offset paths
    if (q.kick.coordMode == 1) {
        loc = q.kick.target; // 0x542E54
    } else if (w.match.phase == 9) {
        loc.x = q.posI.x + q.kick.vec.dx;
        loc.z = q.posI.z + q.kick.vec.dz;
    } else { // 0x542E63
        loc.x = w.ball.restartPos.x + q.kick.vec.dx;
        loc.z = w.ball.restartPos.z + q.kick.vec.dz;
    }
    Player_adjustRunTarget(w, pid, loc, loc); // 0x542E3A
    Player_moveToward_4BE804(w, pid, loc);
}

// 0x542EA0 (called by Player_frameMove 0x4BC172): steer the updated player (DAT_0065F2A4) around
// nearby players on the way to the ball. EAX = out target, EDX = &res (player id, -1 none).
// Returns 1 when `out` replaces the target (blocker found or kick-off half clamp).
bool Player_interceptTarget_542EA0(World& w, int /*pid: exe uses DAT_0065F2A4*/, Vec3& out, std::int32_t& res) {
    bool ret = false;
    const int cur = w.pl.current;
    if (cur < 0) {
        res = -1; // 0x54304F
        return false;
    }
    PlayerState& p = P(w, cur);
    const Vec3 tgt = Motion_targetVec(w, cur);
    out = tgt;
    if (w.match.phase == 9 || p.ctrl == 0) { // 0x542EEC
        std::int32_t apex = w.ball.apexHeight;
        if (apex <= w.ball.posI.y) apex = w.ball.posI.y;
        const Vec3& aim = apex > 0x72 ? w.ball.cross85Pos : w.ball.landing;
        if (octLength(aim.x - p.posI.x, aim.z - p.posI.z) > 0x240) {
            res = -1; // 0x5430A9
            return false;
        }
    }
    if ((p.speed >> 16) == 0) { // 0x542F48
        res = -1;
        return false;
    }
    Vec3 loc = tgt;
    std::int32_t bestAng = 0, best = 0;
    const TeamState& own = w.teams[p.team];
    const TeamState& opp = w.teams[own.opponent];
    const int nOwn = own.playerCount, total = nOwn + opp.playerCount;
    int ki = 0, kj = 0;
    for (int i = 0; i < total; ++i) {
        const int o = i < nOwn ? own.players[ki++] : opp.players[kj++];
        const std::int32_t dist = Player_cachedDistance(w, cur, o);
        if (o == cur || dist > 0x86) continue;
        const PlayerState& q = P(w, o);
        const std::int32_t dx = q.posI.x - p.posI.x, dz = q.posI.z - p.posI.z; // 0x5430EA
        const std::int32_t oct = octLength(dx, dz);
        const Angle ang = atan2i(dx, dz);
        if (!(p.stateSub2 & 1) && octLength(q.posI.x - tgt.x, q.posI.z - tgt.z) <= 0x36) {
            const Vec3 side = offsetInDirection(q.posI, 0x26, ang - 0x200); // 0x543187 (x/z only)
            loc.x = side.x;
            loc.z = side.z;
            ret = true;
            res = o;
            break; // -> 0x542FD4
        }
        const std::int32_t s = Math_asinRatio(0x36, oct); // 0x5431D8
        if (p.stateTimer == 0) continue;
        std::int32_t e = (Motion_dirToTarget(w, cur) - ang) & 0x3ff;
        if (e > 0x200) e -= 0x400;
        if (s <= best || s <= iabs(e)) continue;
        best = s;
        res = o;
        bestAng = (e >= 0 ? s : -s) + ang;
    }
    if (best != 0) { // 0x542FDB
        const Vec3 side = offsetInDirection(P(w, res).posI, 0x46, bestAng);
        loc.x = side.x;
        loc.z = side.z;
    }
    if (w.match.phase == 0) { // kick-off: stay in the own half
        if (own.side == 0) {
            if (loc.z > -0x20) { loc.z = -0x20; ret = true; }
        } else if (loc.z < 0x20) { // 0x54325C
            loc.z = 0x20;
            ret = true;
        }
    }
    out = loc;
    out.y = tgt.y;
    return ret;
}

// 0x4E0474: skill move with the ball: run to the predicted contact point of the skill anim
// (code +0x1E8, aim +0x1EA), then play it (flag 0x65F27A bit 0 from a skill roll) and push the
// ball; when the anim releases (anim flag +0x37 bit 1) go to state 0x308.
void State30D(World& w, int pid) {
    PlayerState& p = P(w, pid);
    if (w.match.phase != 9 || w.ball.possessor != pid) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    holdBallReq_W5(p);
    if (p.stateSub2 == 0) { // 0x4E04E7
        setCode1EA(p, static_cast<std::uint32_t>(Player_inputAimAngle(w, pid)));
        setCode1E8(p, static_cast<std::uint32_t>(Anim_resolveId(w, code1E8(p))));
        std::int32_t dist = 0, angle = 0;
        const std::int32_t frames = Anim_contactInfo(w, code1E8(p), 0, 0, -100, 1, &dist, &angle);
        Vec3 pred{};
        if (Ball_predictPos(w, frames, 1, &pred) == 0) pred = w.ball.posI;
        angle = (angle + code1EA(p)) & 0x3ff;
        if (angle > 0x200) angle -= 0x400;
        const Vec3 spot = offsetInDirection(pred, -dist, angle); // exe leaves y unset
        const OctDist d = Math_vecTo(p.posI, spot);
        if (6 * (frames + 6) < d.dist) { // 0x4E05BF: still too far, sprint to the spot
            Player_moveToward_4BE804(w, pid, spot);
            Motion_setTargetAlt(w, pid, spot);
            p.flags22c &= ~0x20u;
            Player_requestSprint(w, pid);
            p.speedCap = p.sprintMax;
            return;
        }
        p.flags22c |= 0x20; // 0x4E0633
        const std::uint32_t limit =
            kSkillOdds_4E01E0[w.teams[p.team].ctrl1f8] + (static_cast<std::uint32_t>(w.match.f26c) << 2);
        const std::uint32_t roll = (w.rng.next() & 0xf) + w.attrs[p.attr].at(0x2b);
        if (roll > limit) w.match.flags278 |= 0x10000;       // byte 0x65F27A |= 1
        else w.match.flags278 &= ~0x10000u;                  // 0x4E0702
        ++w.match.f26c;
        Anim_start(w, pid, code1E8(p), 0, code1EA(p));
        Player_skillMoveBall(w, Player_animId(w, pid), code1EA(p), pid);
        Anim_setDrift(w, pid, d.dx, d.dz, frames);
        if (w.ball.possessor == pid) w.ball.dribbler = pid;
        p.stateTimer = 0;
        ++p.stateSub2;
    } else if (p.stateSub2 != 1) {
        return;
    }
    // 0x4E04B7
    if (p.anim.b34[3] & 2) {
        w.match.skillPlayer = pid; // DAT_00667274
        Player_requestSprint(w, pid);
        Player_setState(w, pid, 0x308, 0, 0, false, true);
        return;
    }
    Motion_setTargetAlt(w, pid, w.ball.posI);
}

// 0x4E073C (Match_init 0x454804): resolve the skill anims of table 0x58B6EC and store their
// contact offsets (bone at +4/+8, root at +0xC/+0x10) and frames to contact (+0x14).
void SkillMoves_build(World& w) {
    for (int i = 0; i < 10; ++i) {
        std::int32_t* e = w.pl.skillMoves[i];
        for (int k = 0; k < 7; ++k) e[k] = kSkillMoves_58B6EC[i][k];
        e[0] = Anim_resolveId(w, e[0]);
        e[5] = Anim_contactInfo(w, e[0], 0, 1, -100, 1, &e[1], &e[2]);
        Anim_contactInfo(w, e[0], 0, 0, -100, 1, &e[3], &e[4]);
    }
}

// 0x4E07F8: immediately hand over to state 0x309.
void State30E(World& w, int pid) { Player_setState(w, pid, 0x309, 0, 0, false, true); }

// 0x4E0814: trap the ball dead at the feet, then knock it 0x140 ahead (away from the opponent's
// possession winner when it is close and in front) and return to state 0x300.
void State30C(World& w, int pid) {
    PlayerState& p = P(w, pid);
    if (w.match.phase != 9 || w.ball.possessor != pid) {
        Player_dropBallAndReset(w, pid);
        return;
    }
    Motion_setTargetAlt(w, pid, w.ball.posI);
    if (p.stateSub2 != 0) return;
    if (p.distBall > 0x42) {
        if (static_cast<std::uint32_t>(p.stateTimer) > 0x3c) Player_dropBallAndReset(w, pid);
        return;
    }
    Ball_stopDead(w); // 0x4E086F
    Angle a = p.facing;
    const int pw = w.teams[w.teams[p.team].opponent].possWinner;
    if (pw >= 0) {
        const PlayerState& o = P(w, pw);
        std::int32_t away = (o.angleBall + 0x200) & 0x3ff;
        if (away > 0x200) away -= 0x400;
        std::int32_t diff = (away - p.facing) & 0x3ff;
        if (diff > 0x200) diff = 0x400 - diff;
        if (diff <= 0x64 && o.distBall <= 0x280) a = away; // 0x4E092B: EDX keeps `away`
    }
    std::int32_t rec[4];
    Math_polarRecord(0x140, a, rec);
    Kick_planToPoint(w, pid, rec, nullptr, 0x12, -1, 0, 0xb, 0xd, 0, 0);
    Player_setState(w, pid, 0x300, 0, 0, false, true);
    Keeper_holdOff(w, 0x5a);
}

} // namespace wc98::sim
