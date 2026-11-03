// Outfield player states 0x002-0x004 (off-ball positioning), 0x080-0x086 (fouled / injured /
// down) and 0x100 (tackle) — owner: SimPorter2. Ported from FIFA99.EXE v1.1 0x5335EC..0x535870.
// Exe addresses in comments; see player_states.hpp for conventions.
#include "player_states.hpp"
#include "wc98/sim/sim.hpp"

namespace wc98::sim {
namespace {

PlayerAttr& A(World& w, const PlayerState& p) { return w.attrs[static_cast<std::size_t>(p.attr)]; }
TeamState& T(World& w, const PlayerState& p) { return w.teams[static_cast<std::size_t>(p.team)]; }
TeamState& Opp(World& w, const PlayerState& p) { return w.teams[static_cast<std::size_t>(T(w, p).opponent)]; }

constexpr std::int32_t wrapDiff(std::int32_t a) { // and 0x3ff; > 0x200 -> -= 0x400
    a &= 0x3ff;
    return a > 0x200 ? a - 0x400 : a;
}
constexpr std::int32_t absDiff(std::int32_t a) { // and 0x3ff; > 0x200 -> 0x400 - a
    a &= 0x3ff;
    return a > 0x200 ? 0x400 - a : a;
}
void nextSub(PlayerState& p) {
    p.stateTimer = 0;
    ++p.stateSub2;
}
// `or [p+0x184], m; [[p+0xB8]+0x18] = moveReq sar 16`
void reqBits(PlayerState& p, std::uint32_t m) {
    p.moveReq |= m;
    p.anim.req = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.moveReq) >> 16);
}
// 0x4CF450 with zone -1 evaluated on the copy slot +0x10 (0x5337F9 / 0x5339E7 pattern).
void participationOnCopySlot(World& w, int pid, Vec3& out) {
    PlayerState& p = P(w, pid);
    const std::int32_t saved = p.curSlot;
    p.curSlot = p.slotCopy;
    Formation_attackParticipation(w, p.posCb, pid, out, -1, 0);
    p.curSlot = saved;
}
// 0x5337BA / 0x533BA7: 0x240 - ctrl1f4 * 0xC0
std::int32_t sprintRange(const TeamState& t) { return 0x240 - t.ctrl1f4 * 0xc0; }

// Facing the referee (0x534580 / 0x5347C7): head aim from the victim to the referee.
void faceReferee(World& w, int v) {
    const OctDist d = Math_vecTo(P(w, v).posI, Referee_posI(w));
    P(w, v).flags22c &= ~0x10u;
    Motion_faceVector(w, v, d.dx, d.dz);
}

} // namespace

// 0x002 0x5335EC: AI support positioning when the team has the ball.
void State002(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    const int pw = t.possWinner; // edi
    const int ph = w.match.phase;
    Vec3 v;
    if (!(w.match.flags278 & 0x20) && ph != 0xc && ph != 0 && ph != 3 && ph != 2) {
        const std::uint32_t line = static_cast<std::uint32_t>(p.f014); // jump table 0x5335D0
        if (line <= 3 || line == 6) {
            Player_offBallRun(w, pid);
            return;
        }
        // 0x5336D3
        if (p.flags22c & 0x2000) Player_supportTarget(w, pid, v);
        else participationOnCopySlot(w, pid, v);
        if (w.opt.rules & 2) { // 0x5336E9
            const PlayerState& q = P(w, pw);
            std::int32_t lim = w.pitch.penAreaLineZ;
            const std::int32_t z = q.posI.z + (T(w, q).side == 1 ? 0x90 : -0x90);
            const int poss = w.ball.possessor;
            if (pw == poss && (z >= 0 ? 1 : 0) == T(w, P(w, poss)).side && iabs(z) > lim) lim = iabs(z);
            if (-lim > v.z) v.z = -lim;
            else if (lim < v.z) v.z = lim;
        }
        if (p.distBall < 0x180 && Player_avoidPlayer(w, pid, pw, v, v)) { // 0x533851
            Motion_setTargetAlt(w, pid, v);
            Player_requestSprint(w, pid);
            return;
        }
        // 0x53376F
        const std::int32_t d = w.ball.landing.z + (t.side == 1 ? 0x1b0 : -0x1b0) - p.posI.z;
        if ((d >= 0 ? 1 : 0) == t.side && sprintRange(t) < p.targetDist && !(w.ball.flags & 0x20))
            Player_requestSprint(w, pid);
        Player_moveToward_4BE804(w, pid, v);
        return;
    }
    // 0x53361A
    Formation_attackParticipation(w, p.posCb, pid, v, -1, 0);
    if (ph == 3 && static_cast<std::uint32_t>(p.f014) < 4) {
        std::int32_t bx = w.ball.posI.x;
        if (t.side == 1) bx = -bx;
        if ((bx > 0 && p.f014 == 2) || (bx < 0 && p.f014 == 1)) { // 0x53365E
            const std::int32_t z = w.pitch.penAreaLineZ - 0xf0;
            v.z = t.side == 1 ? z : -z;
        }
    }
    Player_adjustRunTarget(w, pid, v, v);
    Player_moveToward_4BE804(w, pid, v);
}

// 0x003 0x533898: AI defensive positioning when the opponent has the ball (marking, line
// holding, intercepting the opponent's ball winner).
void State003(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    const int k = t.f1e4;
    const int ph = w.match.phase;
    Vec3 v;
    if (p.markTarget != 0 && ph != 0xc && ph != 0 && ph != 3 && (k < 0 || P(w, k).state != 0x201)) {
        State281(w, pid); // 0x5338E2
        if (t.flags & 0x100) Player_requestSprint(w, pid);
    } else { // 0x5339E7
        participationOnCopySlot(w, pid, v);
        Vec3 u;
        const std::int32_t pz = p.posI.z;
        Player_adjustRunTarget(w, pid, p.posI, u);
        if (pz == u.z) { // 0x533A9E
            Player_adjustRunTarget(w, pid, v, v);
            Player_moveToward_4BE804(w, pid, v);
        } else {
            if ((v.z - u.z >= 0 ? 1 : 0) == t.side) {
                Motion_setTargetAlt(w, pid, v);
            } else { // 0x533A6B
                if (iabs(v.x) > iabs(u.z >> 1)) u.x = v.x;
                Player_moveToward_4BE804(w, pid, u);
            }
            if (p.targetDist > 0x80) Player_requestSprint(w, pid); // 0x533A52
        }
    }
    // 0x5338FC
    const int o = Opp(w, p).f1b4;
    if (o < 0) { // 0x533B3B
        if (w.match.phase == 9) {
            Player_holdOffsideLine_4BCA4C(w, pid);
            if (p.f254 != 0) {
                Formation_lerpByMentality(w, v, t.index, Motion_targetVec(w, pid), Motion_targetVec(w, p.f254 - 1));
                Player_moveToward_4BE804(w, pid, v);
            }
        }
    } else {
        p.speedCap = p.runMax;
        bool chase = false;
        if (o == T(w, P(w, o)).f1c0 && (p.role == 2 || p.curSlot == 1)) {
            chase = true;
            const int m = p.markTarget;
            if (m != 0) {
                const PlayerState& mp = P(w, m - 1);
                if ((mp.posI.z >= 0 ? 1 : 0) != T(w, mp).side && Pitch_inPenaltyArea(w, mp.posI)) chase = false;
            }
        }
        if (chase) { // 0x533943
            if (t.ctrl1f4 != 0) Player_requestSprint(w, pid);
            const PlayerState& op = P(w, o);
            Vec3 out;
            if (Player_solveIntercept(w, p.posI, (p.speed + p.runMax) >> 1, w.ball.posI, op.m98, op.m9c,
                                      w.pitch.halfWidth, w.pitch.halfLength, out) < 0) { // 0x533B08
                out.x = w.ball.posI.x >> 1;
                out.z = (w.pitch.goalLineZ[t.side] + w.ball.posI.z) >> 1;
                Player_moveToward_4BE804(w, pid, out);
            } else {
                Motion_setTargetAlt(w, pid, out);
            }
        }
    }
    // 0x5339A8
    if (t.flags & 0x100) {
        if (p.role != 4 || pid == t.f1bc) Player_requestSprint(w, pid);
        return;
    }
    if (sprintRange(t) >= p.targetDist) return; // 0x533B98
    const std::int32_t d = w.ball.landing.z + (t.side == 1 ? 0x1b0 : -0x1b0) - p.posI.z;
    if ((d >= 0 ? 1 : 0) == t.side) Player_requestSprint(w, pid);
}

// 0x004 0x533C4C: generic positioning (human pad / AI support or defence by possession), restart
// line-up waits (flags278 & 0x20) and set-piece attention.
void State004(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    if (Player_animId(w, pid) == 0x26) {
        Anim_start(w, pid, 0x27, 0, p.facing);
        return;
    }
    p.speedCap = p.runMax;
    Vec3 v;
    if (w.match.flags278 & 0x20) {
        p.flags22c |= 0x80;
        bool clear;
        switch (w.match.phase) { // jump table 0x533C0C
        case 0: // 0x533C95
            clear = (p.posI.z >= 0 ? 1 : 0) == t.side &&
                    (!(w.match.flags278 & 2) || !Player_clearOfRestart_4BD05C(w, p.posI));
            break;
        case 1: case 2: case 5: // 0x533E55
            clear = w.match.phaseTeam == t.index || !(w.match.flags278 & 2) ||
                    Player_clearOfRestart_4BD05C(w, p.posI);
            break;
        case 3: case 4: case 6: // 0x533E15
            clear = w.ball.restartPos.z * p.posI.z <= 0 || !Pitch_inPenaltyArea(w, p.posI);
            break;
        default: clear = true; break;
        }
        if (clear) {
            p.stateTimer = 0;
            p.flags22c &= ~0x80u;
        }
        if (static_cast<std::uint32_t>(p.stateTimer) > 0x384) p.flags22c &= ~0x80u; // 0x533CCF
        if (p.flags22c & 0x80) { // 0x533EB6
            Formation_attackParticipation(w, p.posCb, pid, v, -1, 0);
            Player_moveToward_4BE804(w, pid, v);
            Player_refreshSpeedCap(w, pid);
            return;
        }
    }
    v = Motion_targetVec(w, pid); // 0x533CEC
    Player_refreshSpeedCap(w, pid);
    if (p.ctrl != 0) {
        const int ph = w.match.phase;
        if (ph == 4 || ph == 6 || ph == 0xa) return;
        p.flags22c |= 0x10;
        Player_humanMoveTarget(w, pid, v);
        p.flags22c |= 0x100000;
        if (w.match.phase != 9) {
            if (p.padOffX == 0 && p.padOffZ == 0) {
                if (w.match.phase == 0 && (w.match.flags278 & 0x20)) {
                    Formation_attackParticipation(w, p.posCb, pid, v, -1, 0);
                } else if ((w.match.phase == 5 || w.match.phase == 3) && pid != w.match.setPiecePartner) { // 0x533EE5
                    Player_adjustRunTarget(w, pid, v, v);
                }
            }
            if (w.match.phase == 0) { // 0x533D89
                if (t.side == 0) {
                    if (v.z > -0x20) v.z = -0x20;
                } else if (v.z < 0x20) {
                    v.z = 0x20;
                }
            }
        }
        Pitch_clampToField(w, v); // 0x533DAE
        Player_moveToward_4BE804(w, pid, v);
    } else {
        switch (w.match.phase) { // 0x533F2D, jump table 0x533C28 [phase - 1]
        case 1: case 2: case 3: case 5: case 7: case 9: { // 0x533F43
            if (t.flags & 2) State002(w, pid);
            else State003(w, pid);
            if (w.match.phase != 9) break;
            p.flags22c &= ~0x10u;
            std::int32_t a = 0xf0, d = 0x1e0;
            const std::uint32_t st = p.moveStatus;
            if ((t.flags & 2) && static_cast<std::uint32_t>(p.f014) < 4) {
                a = 0x90;
                d = 0x120;
            }
            p.headAim = p.angleBall;
            if (st & 1) {
                if (p.distBall <= 0x180) break;
                if (a >= p.targetDist) {
                    const PlayerAttr& at = A(w, p);
                    if (at.f10 == 0 || (at.at(0x40) & 0x40)) break;
                }
                reqBits(p, 0x4000001);
            } else {
                if (d >= p.targetDist || !(st & 2)) break;
                reqBits(p, 0x4000001);
            }
            break;
        }
        default: // 0x534045
            Formation_attackParticipation(w, p.posCb, pid, v, -1, 0);
            Player_moveToward_4BE804(w, pid, v);
            break;
        }
    }
    if (w.match.periodType == 3) { // 0x533DBE -> 0x534061
        Formation_attackParticipation(w, p.posCb, pid, v, -1, 0);
        Pitch_clampToField(w, v);
        Player_moveToward_4BE804(w, pid, v);
    }
}

// 0x534090 (EAX = a, EDX = o, EBX = height): angle for the knock-down of `o` by `a`: o's desired
// facing when the contact is low (<= 0x39) or both stand on the same spot, else 0x4DA094.
Angle Player_knockAngle_534090(World& w, int a, int o, std::int32_t h) {
    if (h > 0x39 && (P(w, a).posI.x != P(w, o).posI.x || P(w, a).posI.z != P(w, o).posI.z))
        return Player_cachedAngle(w, a, o);
    return P(w, o).facingDesired;
}

// 0x080 0x5340D4: knocked down — play the fall (stateArg0 anim, stateArg1 heading), then the
// injury state by the foul's injury level (0x65E040, table 0x5340C0).
void State080(World& w, int pid) {
    PlayerState& p = P(w, pid);
    if (p.posI.y != 0) Player_moveToward_4BE804(w, pid, p.posI);
    Kick_clearPending(w, pid);
    Ball_releaseFromPossessor(w, pid);
    Player_clearTeamRefs_4BE6D0(w, pid);
    p.flags22c |= 1;
    Anim_start(w, pid, p.stateArg0, 0, p.stateArg1);
    static constexpr int kNext[5] = {0x81, 0x83, 0x84, 0x85, 0x85};
    const std::uint32_t inj = static_cast<std::uint32_t>(w.match.injury);
    if (inj > 4) return;
    Player_setState(w, pid, kNext[inj], p.stateArg0, p.stateArg1, false, false);
}

// 0x081 0x5341A0: fall without injury; get up when the fall anim (type 0xB) is over.
void State081(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const std::int32_t s = p.stateSub2;
    if (s == 0) {
        Kick_clearPending(w, pid);
        Ball_releaseFromPossessor(w, pid);
        Player_clearTeamRefs_4BE6D0(w, pid);
        Anim_start(w, pid, p.stateArg0, 0, p.stateArg1);
        nextSub(p);
        return;
    }
    if (s == 1 && p.anim.type() != 0xb) {
        Player_moveToward_4BE804(w, pid, p.posI);
        Player_dropBallAndReset(w, pid);
    }
}

// 0x082 0x534214: dive (fun mode 2: one of 3 anims 0x59F24C) or simulation dive; in phase 9 the
// referee may call a foul on the opponent chaser (team+0x1CC).
void State082(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    std::int32_t s = p.stateSub2;
    if (s == 0) {
        Kick_clearPending(w, pid);
        Ball_releaseFromPossessor(w, pid);
        // 0x4D6A40 (EAX = EDX = pid): commentary chain, not simulated.
        if (w.opt.funModes & 2) {
            static constexpr std::int32_t kDive[3] = {0x31, 0xa, 0xf3}; // 0x59F24C
            const std::uint32_t r = w.rng.next() % 3;
            Anim_start(w, pid, kDive[r], 0, p.facingDesired);
        } else {
            if (w.match.phase == 9) { // 0x5342F1
                const int c = Opp(w, p).f1cc;
                const OctDist d = Math_vecTo(P(w, c).posI, p.posI);
                std::uint32_t mask = 1;
                if (iabs(p.posI.z) > w.pitch.halfLength - 0x360 && iabs(p.posI.x) < w.pitch.penAreaHalfWidth) mask = 7;
                bool foul = false;
                if (w.opt.funModes & 8) {
                    foul = true;
                } else if (d.dist < 0x90) { // 0x5343C4
                    bool tryIt;
                    if (Player_isState100Kind1(w, c) || Player_isState100Kind0(w, c) || Player_isState100Kind2(w, c))
                        tryIt = true;
                    else
                        tryIt = w.rng.next() % static_cast<std::uint32_t>(d.dist) < 0x30;
                    if (tryIt && !(w.rng.next() & mask)) foul = true; // 0x5343EE
                }
                if (foul) {
                    const int kind = (w.rng.next() & 0xf) ? 6 : 2;
                    Rules_registerFoul(w, kind, c, pid, &p.posI, 0);
                    Match_dispatchEvent(w, 7, t.index);
                    // 0x4D6BA0 (non-fun-mode path only): commentary chain, not simulated.
                }
            }
            Anim_start(w, pid, 0xa, 0, p.facingDesired); // 0x53438B
            // 0x4A2E20: speech, not simulated.
        }
        t.f1e0 = -1; // 0x5342B4
        Opp(w, p).f1e0 = -1;
        nextSub(p);
        s = p.stateSub2;
    }
    if (s == 1) { // 0x534238
        if (!(p.anim.flags() & 2)) return;
        nextSub(p);
    } else if (s != 2) {
        return;
    }
    Player_moveToward_4BE804(w, pid, p.posI); // 0x53424F
    Player_dropBallAndReset(w, pid);
}

// 0x083 0x534454: light knock — wait for the anim, face the referee in phase 10.
void State083(World& w, int pid) {
    PlayerState& p = P(w, pid);
    if (p.stateSub2 == 0) {
        // 0x4A2E20: speech, not simulated.
        nextSub(p);
    }
    if (p.stateSub2 != 1 || !(p.anim.flags() & 2)) return;
    p.flags22c &= ~1u;
    if (w.match.phase == 0xa) {
        p.flags22c &= ~0x10u;
        Player_moveToward_4BE804(w, pid, p.posI);
        Motion_facePoint(w, pid, Referee_posI(w));
    }
    Player_dropBallAndReset(w, pid);
}

namespace {
// 0x534610 / 0x534883: once the turn has settled, maybe play one of the two appeal anims 0x59F248.
// Returns false when the turn has not settled (handler returns).
bool appealAnim(World& w, int pid) {
    PlayerState& p = P(w, pid);
    if (!Motion_turnSettled(w, pid)) return false;
    static constexpr std::int32_t kAppeal[2] = {0x3e, 0x3f}; // u16 0x59F248
    const std::int32_t id = kAppeal[w.rng.next() & 1];
    if (Anim_clipUnavailable(w, id)) Anim_start(w, pid, id, 0, p.facing);
    return true;
}
} // namespace

// 0x084 0x5344E8: hurt (injury 2) — lie 120 frames, the victim faces the referee and appeals.
void State084(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const int v = w.match.victim;
    switch (p.stateSub2) { // jump table 0x5344D4
    case 0:
        // 0x4A2E20: speech, not simulated.
        nextSub(p);
        [[fallthrough]];
    case 1: // 0x53451A
        if (!(p.anim.flags() & 2)) return;
        Player_moveToward_4BE804(w, pid, p.posI);
        if (static_cast<std::uint32_t>(p.stateTimer) < 0x78) return;
        nextSub(p);
        return;
    case 2: // 0x534556
        if (pid == v && (w.opt.rules & 0x10)) faceReferee(w, v);
        nextSub(p);
        return;
    case 3: // 0x5345AE
        if (pid == v && (w.opt.rules & 0x10) && P(w, v).distBall > 0x140 && w.opt.gameMode != 1 &&
            !appealAnim(w, pid))
            return;
        nextSub(p);
        [[fallthrough]];
    case 4: // 0x5345E4
        if (!(p.anim.flags() & 2)) return;
        p.flags22c &= ~1u;
        Player_dropBallAndReset(w, pid);
        return;
    default: return;
    }
}

// 0x085 0x53467C: injured (injury 3/4) — auto-substitution for AI-managed teams, writhing anim
// (0x1C3 or 0x25 by the fall anim), get up after 300 frames, appeal to the referee.
void State085(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    switch (p.stateSub2) { // jump table 0x534664
    case 0: { // 0x534699
        // [0x581BB0](0x1B): presentation trigger, not simulated.
        if (t.humans == 0 || t.setupAuto != 0) {
            const int r = Squad_pickInjurySub_49FA00(w, t.index, p.index, 0);
            if (r != 0xff) {
                Squad_substitute_49F03C(w, t.index, A(w, p).squadIndex, r);
                Squad_applyLineup_49F140(w, t.index);
            }
        }
        nextSub(p);
        [[fallthrough]];
    }
    case 1: { // 0x534701
        if (SeqTrigger_busy(w, 0x1b)) return;
        if (Player_animId(w, pid) == p.stateArg0) return;
        // 0x4A2E20: speech, not simulated.
        const std::int32_t a = p.stateArg0;
        const int id = (a == 0x1e || a == 0x10 || a == 0x2f || a == 0x37 || a == 0x11 || a == 0x12) ? 0x1c3 : 0x25;
        Anim_start(w, pid, id, 0, p.facing);
        nextSub(p);
        [[fallthrough]];
    }
    case 2: // 0x534756
        if (static_cast<std::uint32_t>(p.stateTimer) <= 0x12c) return;
        Anim_start(w, pid, 0x27, 0, p.facing);
        nextSub(p);
        return;
    case 3: // 0x5347B4
        if (pid == w.match.victim && (w.opt.rules & 0x10)) faceReferee(w, pid);
        if (p.anim.type() == 0xb) return;
        nextSub(p);
        [[fallthrough]];
    case 4: // 0x53480E
        if (pid == w.match.victim && (w.opt.rules & 0x10) && p.distBall > 0x140 && !appealAnim(w, pid)) return;
        nextSub(p); // 0x53482B
        [[fallthrough]];
    case 5: // 0x534839
        if (!(p.anim.flags() & 2)) return;
        p.flags22c &= ~1u;
        if (w.match.phase == 0xa) {
            Player_moveToward_4BE804(w, pid, p.posI);
            Motion_facePoint(w, pid, Referee_posI(w));
        }
        Player_dropBallAndReset(w, pid);
        return;
    default: return;
    }
}

// 0x086 0x5348DC: stumble (anim 0x73), then reset.
void State086(World& w, int pid) {
    PlayerState& p = P(w, pid);
    const std::uint32_t s = static_cast<std::uint32_t>(p.stateSub2);
    if (s == 0) {
        Kick_clearPending(w, pid);
        Ball_releaseFromPossessor(w, pid);
        Anim_start(w, pid, 0x73, 0, p.speed > 0x4ccc ? p.facingDesired : p.facing);
        T(w, p).f1e0 = -1;
        Opp(w, p).f1e0 = -1;
        nextSub(p);
        return;
    }
    if (s == 1 && (p.anim.flags() & 2)) Player_dropBallAndReset(w, pid);
}

namespace {

// 0x535800: knock-down recovery frames (20 - A[0x35]) * 3 + 10.
std::int32_t knockFrames_535800(World& w, int o) { return (0x14 - A(w, P(w, o)).at(0x35)) * 3 + 10; }

// 0x535824: o is his team's keeper holding the ball (possessed, or throw/kick state 0x300 entered
// from 0x201).
bool keeperHolding_535824(World& w, int o) {
    const int k = T(w, P(w, o)).f1e4;
    if (o != k || k != w.ball.possessor) return false;
    return (w.ball.flags & 0x20) || (P(w, k).state == 0x300 && P(w, k).prevState == 0x201);
}

// 0x535384 (EDX src, EBX out): where a won tackle pokes the ball: ball + 0x70..0xF0 along the
// tackle anim's limb direction (row bit 2; 0x4CDD54 pose extent, untraced) or along pid->src
// (+ distance + 0x30).
void pokeTarget_535384(World& w, int pid, const Vec3& src, Vec3& out) {
    PlayerState& p = P(w, pid);
    Angle a;
    std::int32_t d;
    const auto spread = [&] {
        const std::uint32_t n = static_cast<std::uint32_t>(0x14 - A(w, p).at(0x32));
        return static_cast<std::int32_t>(((w.rng.next() % n) << 4) >> 1);
    };
    if (Tackle_flag_4D22E4(w, p.stateSub, 2)) {
        std::int8_t ex = 0, ey = 0, ez = 0;
        untraced_Anim_poseExtent(w, Player_animId(w, pid), 0, &ex, &ey, &ez);
        a = p.anim.angle() + atan2i(ex, ez);
        d = spread() + 0x70;
    } else {
        const std::int32_t dx = src.x - p.posI.x, dz = src.z - p.posI.z;
        a = atan2i(dx, dz);
        const std::int32_t l = octLength(dx, dz);
        d = spread() + l + 0x30;
    }
    const SinCos sc = sinCos(a); // 0x4BB0D0
    out.x = w.ball.posI.x + fx(d, sc.s);
    out.z = w.ball.posI.z + fx(d, sc.c);
    out.y = 0;
}

// 0x53547C (EDX o, EBX pos): the tackle connects with o (phase 9): o's recovery countdown, then the
// row's outcome rolls (bytes +0x1C..+0x25 as cumulative percentages): fall anim (row +0x00/+0x04),
// state 0x80/0x81 and the foul kind given to the referee.
void knockDown_53547C(World& w, int pid, int o, const Vec3& pos) {
    if (w.match.phase != 9) return;
    PlayerState& p = P(w, pid);
    P(w, o).countdown = knockFrames_535800(w, o);
    const bool keeperHold = keeperHolding_535824(w, o); // [esp+0x30]
    if (!keeperHold && octDistance(P(w, o).posI, pos).dist > 0x50) return;
    const int row = p.stateSub;
    const auto u32 = [&](int off) {
        std::uint32_t v = 0;
        for (int k = 3; k >= 0; --k) v = (v << 8) | Tackle_u8(w, row, off + k);
        return static_cast<std::int32_t>(v);
    };
    const std::int32_t fallB = u32(0), fallA = u32(4); // 0x4D2304 / 0x4D2398
    const std::uint8_t b1E = Tackle_u8(w, row, 0x1e), b20 = Tackle_u8(w, row, 0x20);
    std::uint8_t b1F = Tackle_u8(w, row, 0x1f);
    const auto c4C = static_cast<std::uint8_t>(Tackle_u8(w, row, 0x1d) + b1E);
    const auto c5C = static_cast<std::uint8_t>(Tackle_u8(w, row, 0x21) + b20);
    const auto c44 = static_cast<std::uint8_t>(Tackle_u8(w, row, 0x22) + c5C);
    const auto c48 = static_cast<std::uint8_t>(Tackle_u8(w, row, 0x23) + c44);
    const auto c3C = static_cast<std::uint8_t>(Tackle_u8(w, row, 0x24) + c48);
    const int cls = Tackle_u8(w, row, 0x1b);
    const std::uint32_t r = w.rng.next() % 100;
    if (r < b1E && !keeperHold) return; // 0x535732 (commentary 0x4D6AAC(0), not simulated)
    std::int32_t fall;
    if (r < c4C) { // 0x5355E1
        fall = fallA;
        b1F = 0;
    } else {
        fall = fallB;
    }
    // 0x4D6AAC(1 / 2): commentary chain, not simulated.
    if (fall == -1) return;
    std::int32_t injury = 0, state = 0x81, kind;
    if (P(w, o).slot != 0 && w.rng.next() % 100 < b1F) { // 0x535753
        injury = 1;
        state = 0x80;
    }
    const std::int32_t h = Tackle_height_4D22C4(w, row);
    const Angle knock = Player_knockAngle_534090(w, pid, o, h);
    Player_cachedAngle(w, pid, o); // 0x4BEB3C -> pad rumble 0x466F20 (platform), angles only refresh the cache
    Player_cachedAngle(w, o, pid);
    const std::uint32_t r3 = w.rng.next() % 100;
    if (r3 < b20) kind = 0;
    else if (r3 < c5C) kind = 1;
    else if (r3 < c44) kind = 2;
    else if (r3 < c48) kind = 5;
    else if (r3 < c3C) kind = 4;
    else kind = 6;
    if (keeperHold) { // 0x535699
        kind = 4;
        injury = 0;
    }
    (void)cls; // cls == 2: 0x4D6AD0 commentary, not simulated
    if (p.slot == 0 && (kind == 2 || kind == 5 || kind == 4)) kind = 6;
    Rules_registerFoul(w, kind, pid, o, &P(w, o).posI, injury);
    Player_setState(w, o, state, fall, knock, false, false);
    const int off = w.match.offender;
    if (off >= 0 && o == w.match.victim) Match_dispatchEvent(w, 7, T(w, P(w, off)).opponent);
    // 0x4A2DB0: audio, not simulated.
}

} // namespace

// 0x100 0x534990: tackle (stateSub = row of the tackle table 0x58B340). Sub 1 plans the move
// (re-picks the row against the target, AI timing rolls, approach), sub 2 resolves the contact
// (knock-down, ball poked away or won), sub 3 waits for the anim end.
void State100(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    switch (p.stateSub2) { // jump table 0x534980
    case 0:
        p.stateTimer = 0;
        p.stateSub2 = 1;
        p.stateSub = p.stateArg0;
        [[fallthrough]];
    case 1: { // 0x5349C5
        std::int32_t rowKind = 4; // [esp+0xE4]
        const int tgt = Tackle_target_4D21C4(w, pid, Tackle_kindOf_4D221C(w, pid));
        // 0x4D6A40(pid, tgt): commentary chain, not simulated.
        const bool own = Tackle_ownTeamBall_4D2250(w, pid);
        const Vec3* face = &w.ball.posI;
        if (tgt >= 0) { // 0x534DA9
            rowKind = Tackle_u8(w, p.stateSub, 0x1a);
            const int r = Tackle_pickMove_4D1A68(w, pid, tgt, rowKind);
            if (r != -1) p.stateSub = r;
            face = &P(w, tgt).posI;
        }
        const int anim = Tackle_anim_4D227C(w, p.stateSub, Player_pointLeftOf_4BE080(w, pid, *face) ? 1 : 0);
        std::int32_t cdist = 0, cang = 0;
        const std::int32_t frames = Anim_contactInfo(w, anim, 0, 0, -0x62, 1, &cdist, &cang);
        Vec3 b80;
        Tackle_ballAfter_4D1C64(w, frames, b80);
        Vec3 tp; // [esp+0x68]
        if (tgt >= 0) {
            std::int32_t skill = A(w, p).at(0x2d);
            const OctDist d = octDistance(p.posI, b80);
            const Angle a = atan2i(d.dx, d.dz);
            const std::int32_t e1 = absDiff(a - P(w, tgt).angleBall);
            const std::int32_t e2 = absDiff(Player_cachedAngle(w, pid, tgt) - P(w, tgt).angleBall);
            if (e1 < 0x72 && e2 < 0x72) skill >>= 1;
            else if (e1 < 0x19c) skill = 0x10;
            if (rowKind == 0) skill += 4;
            if (p.ctrl == 0) {
                static constexpr std::int32_t kAdj[3] = {4, 0, -4}; // 0x59F260
                skill += kAdj[t.ctrl1f8];
            }
            if (!(w.rng.next() % 20 < static_cast<std::uint32_t>(skill))) { // mistimed
                Vec3 o2;
                if (d.dist >= 0x40) {
                    o2 = offsetInDirection(b80, 0x38, a);
                } else {
                    o2 = offsetInDirection(p.posI, 0x40, (p.speed >> 16) >= 5 ? p.facingDesired : p.facing);
                }
                b80.x = o2.x;
                b80.z = o2.z;
            }
            Player_predictPos_4BEB5C(w, tgt, frames, tp);
        }
        Vec3 stand, ballT; // [esp+0xC8], [esp+0x8C]
        Fixed vx, vz;      // edi, [esp+0xF8]
        if (tgt >= 0 && Player_isState100Kind2(w, pid)) {
            Player_meetPoint_4BE0FC(w, pid, anim, tp, 0, 0, &p.posI, stand);
            ballT = tp;
            vz = P(w, tgt).m9c;
            vx = P(w, tgt).m98;
        } else if (tgt >= 0 && static_cast<std::uint32_t>(Player_cachedDistance(w, pid, tgt)) <= 0x30) { // 0x534E86
            Player_meetPoint_4BE0FC(w, pid, anim, w.ball.posI, 0, 0, &p.posI, stand);
            ballT = w.ball.posI;
            vz = w.ball.vel.z;
            vx = w.ball.vel.x;
        } else { // 0x534E36
            Player_meetPoint_4BE0FC(w, pid, anim, b80, 0, 0, &p.posI, stand);
            ballT = b80;
            vz = w.ball.vel.z;
            vx = w.ball.vel.x;
        }
        bool kind02 = Player_isState100Kind0(w, pid) || Player_isState100Kind2(w, pid); // 0x534C03
        if (kind02) {
            const std::int32_t c = t.ctrl1f8;
            const std::int32_t lim = c == 0 ? 0x2d : c == 2 ? 0xf : 0x1e;
            if (static_cast<std::uint32_t>(lim) <= static_cast<std::uint32_t>(p.stateTimer) || own) { // 0x534FBF
                if (p.targetDist >= 0x1e0) {
                    Player_dropBallAndReset(w, pid);
                    return;
                }
            } else {
                const OctDist d40 = Math_vecTo(p.posI, stand);
                const OctDist d0 = Math_vecTo(p.posI, ballT);
                const OctDist d20 = Math_vecTo(stand, ballT);
                if (d40.dist >= 0x18 && !(d0.dist < d20.dist && d0.dist <= cdist)) { // 0x534F17: approach first
                    Vec3 m = stand;
                    if (d40.dist > 0x50)
                        Player_solveIntercept(w, p.posI, (p.speed + p.runMax) >> 1, ballT, vx, vz, w.pitch.halfWidth,
                                              w.pitch.halfLength, m);
                    Player_moveToward_4BE804(w, pid, m);
                    Motion_setTargetAlt(w, pid, m);
                    Player_requestSprint(w, pid);
                    return;
                }
            }
            kind02 = Player_isState100Kind0(w, pid) || Player_isState100Kind2(w, pid); // 0x534C92
        }
        std::int32_t n;
        if (kind02) {
            n = frames * 5;
        } else { // 0x534FF2
            static constexpr std::int32_t kLead[3] = {2, 0, -2}; // 0x59F26C
            n = (frames - kLead[t.ctrl1f8]) * 3;
            if (n < 0) n = 0;
        }
        Vec3 step;
        Angle h = 0;
        Player_stepToward_4BE890(w, pid, stand, ballT, n, step, &h);
        Anim_start(w, pid, anim, 0, wrapDiff(h - cang));
        Sim_scheduleSplit(w, frames);
        Anim_setDrift(w, pid, step.x - p.posI.x, step.z - p.posI.z, frames);
        // row bit 1 (0x4D2378): 0x4D6AE4 commentary + 0x4A2EA8 audio, not simulated.
        p.stateTimer = 0;
        p.stateSub2 = 2;
        return;
    }
    case 2: { // 0x535021
        const int kind = Tackle_kindOf_4D221C(w, pid);
        const int tgt = Tackle_target_4D21C4(w, pid, kind);
        if (w.match.phase != 9) {
            nextSub(p);
            return;
        }
        if ((tgt >= 0 && Player_dodgesTackle_4E03E4(w, tgt, pid)) || (p.anim.flags() & 2)) { // 0x53514E
            // 0x4D6BA0: commentary chain, not simulated.
            p.stateTimer = 0;
            p.stateSub2 = 3;
            return;
        }
        const Vec3 c = Anim_contactPos(w, pid);
        const bool hold = tgt >= 0 && keeperHolding_535824(w, tgt); // [esp+0xFC]
        bool atPlayer = false; // [esp+0xF4]
        Vec3 at;               // [esp+0x50]
        if (tgt >= 0 && (kind == 2 || (kind == 1 && hold))) {
            atPlayer = true;
            at = P(w, tgt).posI;
        } else {
            at = w.ball.posI;
        }
        bool hit = false, ball = false; // edi, esi
        if (atPlayer) {
            if (octDistance(c, P(w, tgt).posI).dist < 0x30) {
                hit = true;
                if (Player_ballAtContact_4BEA94(w, pid)) ball = true;
            }
        } else if (Player_ballAtContact_4BEA94(w, pid)) {
            hit = ball = true;
        }
        if (!(Player_isState100Kind0(w, pid) && tgt >= 0 && hold) && hit) { // 0x5351C5
            if (atPlayer) {
                if (tgt >= 0) knockDown_53547C(w, pid, tgt, at);
            } else {
                std::int32_t won = Tackle_flag_4D22E4(w, p.stateSub, 0); // [esp+0xEC]
                if (won == 0 && w.rng.next() % 20 > A(w, p).at(0x2d)) won = 1; // 0x5352DB
                if (won) { // 0x53522B
                    if (tgt >= 0) knockDown_53547C(w, pid, tgt, at);
                    if (tgt == w.ball.dribbler) w.ball.dribbler = -1;
                    Ball_releaseFromPossessor(w, tgt);
                    if (ball) {
                        const Vec3 end = Anim_predictEndPos(w, pid);
                        Vec3 to;
                        pokeTarget_535384(w, pid, end, to);
                        Vec3 b = w.ball.posI;
                        to.y = 0;
                        b.y = 0;
                        Ball_placeAt(w, b, false, 0);
                        Ball_kickGroundPass(w, pid, to, 0x3c, 0);
                        Ball_takePossession(w, pid, 0);
                        Ball_releaseFromPossessor(w, pid);
                        // 0x4D6ADC(1): commentary chain, not simulated.
                    }
                } else { // 0x535308
                    if (tgt >= 0) knockDown_53547C(w, pid, tgt, at);
                    Ball_takePossession(w, pid, 0);
                    if (pid == w.ball.possessor) w.ball.dribbler = w.ball.possessor;
                    // 0x4D6ADC(2): commentary chain, not simulated.
                }
            }
            // 0x4D6BA0: commentary chain, not simulated.
            p.stateTimer = 0;
            p.stateSub2 = 3;
            return;
        }
        if (Anim_frame(w, pid) < Anim_currentMark2(w, pid)) return; // 0x535109
        p.stateTimer = 0;
        p.stateSub2 = 3;
        return;
    }
    case 3: // 0x535341
        if (!(p.anim.flags() & 2)) return;
        if (pid != w.ball.possessor) {
            Player_dropBallAndReset(w, pid);
            return;
        }
        Player_assignControlToReceiver(w, pid, -1);
        Player_setState(w, pid, 0x308, 0, 0, false, false);
        return;
    default: return;
    }
}

} // namespace wc98::sim
