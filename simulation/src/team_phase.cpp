// Team set-up on a phase change (restart placement and taker selection): FUN_00452D10 and its
// helpers 0x4524B0..0x453360 (owner: TeamAI, TeamPhaseSetup). Exe player pointers are player ids
// (-1 = null), the team block pointer is the team index.
#include "wc98/sim/anim.hpp"
#include "player_states.hpp"

namespace wc98::sim {

// Owned by SimTeamAI / TeamAiDecisions (redeclared here until they land in team.hpp).
void Team_lineUp(World& w, int team, int cb);   // 0x453628
void Team_onPossessionChange(World& w, int team); // 0x4D37AC

namespace {

// Octagonal distance of the x/z delta, compared unsigned by the exe (`jae`).
std::uint32_t octU(std::int32_t dx, std::int32_t dz) { return static_cast<std::uint32_t>(octLength(dx, dz)); }

// Shared body of 0x4524B0 / 0x4527E8: player whose current move target (*(player+0x104)) is
// nearest to pt; skips +0x22C bit 1, curSlot == skipSlot and (checkFlag1) +0x22C bit 0.
int nearestTarget(World& w, const Vec3& pt, int team, int skipSlot, bool checkFlag1, std::int32_t* outDist) {
    const TeamState& T = w.teams[team];
    std::uint32_t best = 0x7fff8000; // exe 0x4524BF / 0x4527F6
    int found = -1;
    for (int i = 0; i < T.playerCount; ++i) {
        const int pid = T.players[i];
        if (pid < 0) continue; // empty roster slot
        const PlayerState& p = P(w, pid);
        if (p.flags22c & 2) continue;
        if (p.curSlot == skipSlot) continue;
        if (checkFlag1 && (p.flags22c & 1)) continue;
        const Vec3& t = Motion_targetVec(w, pid);
        const std::uint32_t d = octU(t.x - pt.x, t.z - pt.z);
        if (d < best) {
            found = pid;
            best = d;
        }
    }
    if (outDist) *outDist = static_cast<std::int32_t>(best);
    return found;
}

void setStateRun(World& w, int pid, int state) { Player_setState(w, pid, state, 0, 0, false, true); }

} // namespace

// FUN_004524B0 (EAX = point, EDX = team, EBX = curSlot to skip, ECX = out distance or null).
int Team_nearestTargetToPoint(World& w, const Vec3& pt, int team, int skipSlot, std::int32_t* outDist) {
    return nearestTarget(w, pt, team, skipSlot, true, outDist);
}

// FUN_004527E8 (EAX = point, EDX = team, EBX = curSlot to skip): as 0x4524B0 without the bit-0 test
// and the distance output.
int Team_nearestTargetToPointAll(World& w, const Vec3& pt, int team, int skipSlot) {
    return nearestTarget(w, pt, team, skipSlot, false, nullptr);
}

// FUN_00452A90 (EAX = team, EDX = state, EBX = state to skip, ECX = curSlot to skip).
void Team_setPlayersState(World& w, int team, int state, int exceptState, int exceptSlot) {
    const TeamState& T = w.teams[team];
    for (int i = 0; i < T.playerCount; ++i) {
        const int pid = T.players[i];
        if (pid < 0) continue; // empty roster slot
        PlayerState& p = P(w, pid);
        if (p.state == exceptState || p.curSlot == exceptSlot) continue;
        if (p.kick.flags & 4) Kick_clearPending(w, pid); // +0x1E4 bit 2
        Player_setState(w, pid, state, 0, 0, false, false);
    }
}

// FUN_00452B10 (EAX = team, EDX = state, EBX = curSlot to skip): skips players in states
// 0x80..0x85 and in state 0x100 sub-steps of kind 0/1/2 (0x4D2578, 0x4D250C, 0x4D2540).
void Team_setPlayersStateFiltered(World& w, int team, int state, int exceptSlot) {
    const TeamState& T = w.teams[team];
    for (int i = 0; i < T.playerCount; ++i) {
        const int pid = T.players[i];
        if (pid < 0) continue; // empty roster slot
        PlayerState& p = P(w, pid);
        if (p.curSlot == exceptSlot) continue;
        if (p.state >= 0x80 && p.state <= 0x85) continue;
        if (Player_isState100Kind1(w, pid) || Player_isState100Kind0(w, pid) || Player_isState100Kind2(w, pid)) continue;
        if (p.kick.flags & 4) Kick_clearPending(w, pid);
        Player_setState(w, pid, state, 0, 0, false, false);
    }
}

// FUN_00452BD8 (EAX = team index, EDX = k): player whose squad index equals the u16 tag at
// team+0x160 + k*2 (set-piece taker tags after formation record 2); otherwise the last player of
// the list, or null with a full team of 11.
int Team_playerForSlotTag(const World& w, int team, int k) {
    const TeamState& T = w.teams[team];
    const std::uint8_t* b = T.blk + (0x160 - 0x0C) + k * 2;
    const std::int32_t tag = b[0] | (b[1] << 8);
    for (int i = 0; i < T.playerCount; ++i) {
        const int qT = T.players[i];
        if (qT < 0) continue; // empty roster slot
        if (w.attrs[static_cast<std::size_t>(P(w, qT).attr)].squadIndex == tag) return qT;
    }
    if (T.playerCount == 11) return -1;
    return T.players[T.playerCount - 1];
}

// FUN_00452C5C (EAX = team): players with +0x22C bit 2 start anim 0x166 (chooser 0, own facing)
// and drop the bit.
void Team_resetAnimFlag4(World& w, int team) {
    const TeamState& T = w.teams[team];
    for (int i = 0; i < T.playerCount; ++i) {
        const int pid = T.players[i];
        if (pid < 0) continue; // empty roster slot
        PlayerState& p = P(w, pid);
        if (!(p.flags22c & 4)) continue;
        Anim_start(w, pid, 0x166, 0, p.facing);
        p.flags22c &= ~4u;
    }
}

// FUN_004532D0 (EAX = team, EDX = anim for slot 0, EBX = anim for the others): every player starts
// its anim; a player with +0x22C bit 0 and an anim other than 0x26 starts 0x27 instead and drops
// the bit.
void Team_resetAnimFlag1(World& w, int team, int keeperAnim, int outfieldAnim) {
    const TeamState& T = w.teams[team];
    for (int i = 0; i < T.playerCount; ++i) {
        const int pid = T.players[i];
        if (pid < 0) continue; // empty roster slot
        PlayerState& p = P(w, pid);
        const int anim = p.slot == 0 ? keeperAnim : outfieldAnim;
        if ((p.flags22c & 1) && anim != 0x26) {
            Anim_start(w, pid, 0x27, 0, p.facing);
            p.flags22c &= ~1u;
        } else {
            Anim_start(w, pid, anim, 0, p.facing);
        }
    }
}

// FUN_00453360 (EAX = team): defensive wall for a free kick within 0x780 of the aimed goal point.
void Team_lineUpWall(World& w, int team) {
    const TeamState& T = w.teams[team];
    const int n = T.playerCount;
    for (int i = 0; i < n; ++i) { const int qT = T.players[i]; if (qT >= 0) P(w, qT).flags22c &= ~4u; }

    const Vec3& r = w.ball.restartPos;           // 0x66919C
    const std::int32_t hw6 = w.pitch.halfWidth / 6; // 0x65F368 / 6 (idiv)
    Vec3 wall;                                    // [esp+0x10]: aimed goal point, then wall centre
    if (hw6 < r.x) wall.x = w.pitch.goalHalfWidth;    // 0x65F37C
    else if (-hw6 <= r.x) wall.x = 0;
    else wall.x = -w.pitch.goalHalfWidth;
    wall.y = 0;
    wall.z = w.pitch.goalLineZ[T.side];           // 0x65F3E4[side]
    const OctDist d = octDistance(r, wall);       // 0x4BADD4
    if (d.dist > 0x780) return;

    int size = 4;
    if (iabs(r.x) > w.pitch.penAreaHalfWidth) size = 3;
    if (iabs(r.z) < w.pitch.penAreaLineZ - 0xc0) size = 3;

    const Angle a = atan2i(d.dx, d.dz);           // 0x4BB8F8
    wall = offsetInDirection(r, 0x230, a);        // 0x4BB0D0 (y stays 0)
    wall.y = 0;
    const SinCos sc = sinCos(a - 0x100);          // 0x4BB168(0x26, a - 0x100): step across the line
    std::int32_t stepX = fx(0x26, sc.s), stepZ = fx(0x26, sc.c);

    if (hw6 < r.x && T.side == 0) {
        stepX = -stepX;
        stepZ = -stepZ;
    } else if (hw6 > r.x && T.side == 1) {
        stepX = -stepX;
        stepZ = -stepZ;
    } else if (iabs(r.x) < hw6) {
        const std::int32_t half = (size - 1) >> 1;
        wall.x -= stepX * half;
        wall.z -= stepZ * half;
    }

    for (int i = 0, placed = 0; i < n && placed < size; ++i) {
        const Vec3 spot{wall.x + stepX * placed, 0, wall.z + stepZ * placed};
        std::int32_t dist;
        const int pid = Team_nearestTargetToPoint(w, spot, team, 0, &dist);
        if (pid < 0) continue;
        // 0x4D90D8(pid): post-match rating "involved" flag (0x66704C), stats module, not modelled.
        ++placed;
        Player_setState(w, pid, 0x18c, 0, 0, false, false);
        P(w, pid).flags22c |= 6;
        Player_moveToward_4BE804(w, pid, spot);
    }
    for (int i = 0; i < n; ++i) {
        const int qT = T.players[i];
        if (qT < 0) continue; // empty roster slot
        PlayerState& p = P(w, qT);
        if (p.flags22c & 4) p.flags22c &= ~2u;
    }
}

// FUN_00466ED0: every controlled player hands control to the team-mate nearest the ball
// (0x4522A0 with its own curSlot, outfield only) when that is someone else (0x452884).
void Input_controlNearestToBall(World& w) {
    for (int c = 0; c < kMaxControllers; ++c) {
        const int pid = w.controllers[c].player - 1;
        if (pid < 0) continue;
        const PlayerState& p = P(w, pid);
        const int other = Team_nearestPlayer_4522A0(w, w.ball.posI, p.team, p.curSlot, 1);
        if (other >= 0 && other != pid) Player_transferControl(w, pid, other);
    }
}

// FUN_00452D10 (EAX = team index): set the team up for the phase just entered (Match_setPhase).
// The calls to 0x4D5250 (commentary cue for throw-ins/corners/free kicks) have no simulation state
// and are not ported.
void Team_onPhaseChange(World& w, int t) {
    TeamState& T = w.teams[t];
    MatchState& m = w.match;
    Team_onPossessionChange(w, t);
    Team_resetAnimFlag4(w, t);
    T.oneTouch = -1;
    m.setPieceArmed = 0; // DAT_0065F2E4
    if (m.phase != 9) T.f1b8 = -1;
    const int poss = w.ball.possessor;
    int cb;
    if (m.phase == 9 && poss >= 0 && (w.ball.flags & 0x20) && P(w, poss).slot == 0) cb = 3;
    else cb = m.periodType == 3 ? 6 : m.phase;
    Team_lineUp(w, t, cb);
    if (m.flags278 & 1) Team_setPlayersState(w, t, 0, 0xff, 0xff);

    const Vec3& r = w.ball.restartPos;
    const int keeper = Team_findKeeper(w, t);
    const int a = Team_nearestTargetToPoint(w, r, t, 0, nullptr);
    int s0 = keeper, s4 = a;
    const int b = Team_nearestTargetToPointAll(w, r, t, 0xff);
    if (a < 0) {
        s4 = b;
    } else {
        P(w, a).flags22c |= 2;
        s0 = Team_nearestTargetToPoint(w, r, t, 0, nullptr);
        P(w, a).flags22c &= ~2u;
        if (s0 < 0) s0 = Team_nearestTargetToPoint(w, r, t, P(w, a).curSlot, nullptr);
    }

    const bool taking = t == m.phaseTeam;
    switch (m.phase) { // jump table 0x452CB8
    case 0: // kick-off
        Input_controlNearestToBall(w);
        Team_setPlayersState(w, t, 4, 0xff, 0xff);
        if (taking && T.playerCount != 0) {
            setStateRun(w, s4, 0x180);
            if (T.playerCount != 1) setStateRun(w, s0, 0x181);
        }
        break;
    case 1:
        T.f1e0 = -1;
        Team_setPlayersState(w, t, 1, 0xff, 0xff);
        if (taking) setStateRun(w, s4, 0x182);
        break;
    case 2: {
        Team_setPlayersState(w, t, 1, 0xff, 0xff);
        if (!taking) break;
        const std::int32_t prod = static_cast<std::int32_t>(static_cast<std::uint32_t>(r.x) * static_cast<std::uint32_t>(r.z));
        setStateRun(w, Team_playerForSlotTag(w, t, prod > 0 ? 1 : 0), 0x183);
        break;
    }
    case 3:
        Team_setPlayersState(w, t, 1, 0xff, 0xff);
        if (taking && T.f1e4 >= 0 && keeper >= 0) setStateRun(w, keeper, 0x188);
        break;
    case 4:
        Team_setPlayersState(w, t, 0, 0xff, 0xff);
        if (!taking) {
            if (T.f1e4 >= 0 && keeper >= 0) setStateRun(w, keeper, 0x203);
        } else {
            setStateRun(w, Team_playerForSlotTag(w, t, 3), 0x185);
        }
        break;
    case 5:
        Team_setPlayersState(w, t, 0, 0xff, 0);
        if (!taking) {
            Team_lineUpWall(w, t);
        } else if (keeper >= 0 && (r.z >= 0 ? 1 : 0) == w.teams[P(w, keeper).team].side && Pitch_inPenaltyArea(w, r)) {
            setStateRun(w, keeper, 0x184);
        } else {
            setStateRun(w, Team_playerForSlotTag(w, t, 2), 0x184);
        }
        break;
    case 6:
        Team_setPlayersState(w, t, 0, 0xff, 0xff);
        if (!taking) {
            if (T.f1e4 >= 0 && keeper >= 0) setStateRun(w, keeper, 0x203);
        } else {
            setStateRun(w, T.players[m.so.kicker[m.phaseTeam]], 0x189); // 0x65EE74[phaseTeam]
        }
        break;
    case 8: Team_setPlayersState(w, t, 0, 0xff, 0xff); break;
    case 9:
        if (poss >= 0 && (w.ball.flags & 0x20) && P(w, poss).slot == 0 && taking && T.f1e4 >= 0 && keeper >= 0)
            setStateRun(w, keeper, 0x201);
        break;
    case 0xa:
    case 0xb: Team_setPlayersStateFiltered(w, t, 1, 0xff); break;
    case 0xc: Team_setPlayersState(w, t, 0x18a, 0xff, 0xff); break;
    case 0xd:
    case 0x13: Team_setPlayersState(w, t, 0x18d, 0xff, 0xff); break;
    case 0xe:
    case 0xf:
    case 0x10:
    case 0x11:
    case 0x15: Team_setPlayersState(w, t, 0x18e, 0xff, 0xff); break;
    case 0x14: Team_setPlayersState(w, t, 1, 0xff, 0xff); break;
    default: break; // 7, 0x12, > 0x15
    }
}

} // namespace wc98::sim
