// Frame loop: Sim_advance 0x454ED8 -> Sim_dispatchTimeSlice 0x4DFED4 -> Sim_frame 0x4550C0, and the
// period/match driver of Match_run 0x40A664. Presentation-only callees (cut-scene scripts
// 0x463DD4/0x462824, camera focus 0x65F2B4 + SeqTrigger 0x1F, the timed
// callback list 0x46A7DC) are not part of the simulation and are skipped (SIMULATION_STATUS.md).
#include "wc98/sim/sim.hpp"

namespace wc98::sim {

namespace {

// Sim_accumulateTotalFrames 0x454E94.
void accumulateTotalFrames(World& w, Fixed simDt) {
    w.time.stampAcc += simDt;
    const std::int32_t n = static_cast<std::uint32_t>(w.time.stampAcc) >> 16;
    if (n) {
        w.time.stampFrames += n;
        w.time.stampAcc -= n << 16;
    }
}

// FUN_004D02AC(EAX = frames, EDX = possessing team or 2, EBX = ball z): possession time by third.
void possessionByThird(World& w, int team, std::int32_t ballZ) {
    if (team == 2 || w.match.phase != 9) return;
    TeamState& t = w.teams[team];
    const std::int32_t third = w.pitch.thirdLineZ[1];
    if (ballZ <= third && -third <= ballZ) t.possessionThird[1] += w.time.simFrames;      // 0x65E220
    else if (t.side != static_cast<std::int32_t>(ballZ > -1)) t.possessionThird[0] += w.time.simFrames; // 0x65E21C
    else t.possessionThird[2] += w.time.simFrames;                                         // 0x65E224
}

// FUN_004560D8(EAX = frames): territory percentages.
void territory(World& w) {
    if (w.match.phase != 9) return;
    std::int32_t outfield = w.teams[0].playerCount + w.teams[1].playerCount - 2;
    std::int32_t scale = outfield * 6;
    const std::int32_t c = w.opt.surfaceColumn;
    if (c == 2 || c == 4) scale = outfield * 12; // surface columns 2 and 4 double the rate (0x456114)
    const std::uint32_t denom = static_cast<std::uint32_t>(w.opt.halfLengthMinutes * 60 + 1);
    const std::int32_t z = w.ball.posI.z;
    int i = 0;
    if (w.pitch.thirdLineZ[1] < z) i = 1;
    else if (z < -w.pitch.thirdLineZ[1]) i = 2;
    w.territory.frames[i] += w.time.simFrames;
    std::uint32_t pct = static_cast<std::uint32_t>((w.territory.frames[i] / 60) * scale) / denom;
    w.territory.percent[i] = static_cast<std::int32_t>(pct > 100 ? 100 : (pct & 0xffff));
}

} // namespace


void Sim_frame(World& w, Fixed simDt, Fixed clockDt) {
    FrameTime& t = w.time;
    accumulateTotalFrames(w, simDt);
    t.simAcc += simDt;
    t.clockAcc += clockDt;
    const std::int32_t frames = static_cast<std::uint32_t>(t.simAcc) >> 16;
    if (!frames) return;
    t.simFrames = frames;
    t.clockFrames = static_cast<std::uint32_t>(t.clockAcc) >> 16;
    t.clockAcc -= t.clockFrames << 16;
    t.totalFrames += frames;
    t.simAcc -= frames << 16;
    t.thirtyCounter += frames;
    if (w.opt.fatigueInjuries) {
        t.fatigueCounter += frames;
        if (static_cast<std::uint32_t>(t.fatigueCounter) > static_cast<std::uint32_t>(t.fatigueInterval)) {
            Team_applyFatigue(w, 0);
            Team_applyFatigue(w, 1);
            t.fatigueCounter = 0;
        }
    }
    Players_animate(w, simDt); // 0x4CC8F4 (0x462824 instead while a cut-scene plays; not modelled)
    if (t.timer673210 > 0) {
        t.timer673210 -= frames;
        if (t.timer673210 < 0) t.timer673210 = 0;
    }
    if (t.timer65f1f8 != 0) t.timer65f1f8 -= frames; // unsigned compare (jbe)
    t.teamOrder ^= 1;
    w.match.offBallControl = -1; // DAT_0065f29c = 0 every frame
    Input_updateControllers(w);
    SetPiece_updateAim(w);
    Ball_update(w);
    Match_updateRules(w);
    Team_updateSupportTargets(w, 0);
    Team_updateSupportTargets(w, 1);
    Team_lateGameTactics(w);
    if (static_cast<std::uint32_t>(t.thirtyCounter) > 30) {
        t.blink ^= 1;
        t.thirtyCounter = 0;
        Team_autoMentality(w, t.blink);
    }
    const std::int32_t owner = w.ball.possessor;
    if (w.match.phase == 9 && owner >= 0) {
        const PlayerState& p = w.players[owner];
        if (p.slot != 0 || w.teams[owner / kPlayersPerTeam].f1e4 == 0) {
            if (p.speed >= 0x19999) t.possessorSlowFrames = 0;
            else t.possessorSlowFrames += frames;
            t.possessionFrames += frames;
        }
    }
    if (t.teamOrder) {
        Team_updateTactics(w, 0);
        Team_updateTactics(w, 1);
        Team_updatePlayers(w, 0);
        Team_updatePlayers(w, 1);
    } else {
        Team_updateTactics(w, 1);
        Team_updateTactics(w, 0);
        Team_updatePlayers(w, 1);
        Team_updatePlayers(w, 0);
    }
    if (w.opt.gameMode != 1 && w.opt.gameMode != 3) Team_aiSubstitutions_453E58(w); // 0x455283
    Match_frameFlags(w);
    // EDX = owner team, else team of player DAT_0065f2a0, else 2 (0x4553BE).
    int possTeam = 2;
    if (owner >= 0) possTeam = owner / kPlayersPerTeam;
    else if (w.match.player2a0 >= 0) possTeam = w.match.player2a0 / kPlayersPerTeam;
    possessionByThird(w, possTeam, w.ball.posI.z);
    territory(w);
    // FUN_0046A7DC (timed presentation callbacks) skipped.
    // 0x4552F9..0x455327 / 0x4553D4..0x4553FC: focus follows a low-ball possessor, else the ball
    // (SeqTrigger 0x1F that follows each write is presentation).
    if (w.match.phase == 9) {
        const std::int32_t p = w.ball.possessor;
        if (p >= 0 && w.ball.posI.y <= 0x30) t.focus = p;
        else if (!(p >= 0 && w.ball.posI.y <= 0xa0 && t.focus == p)) t.focus = -1;
    }
}

void Sim_initSplitList(World& w) {
    FrameTime& t = w.time;
    for (int i = 0; i < 20; ++i) {
        t.splitPrev[i] = i - 1;
        t.splitNext[i] = i + 1;
    }
    t.splitNext[19] = -1; // 0x677560
    t.splitFree = 0;
    t.splitHead = -1;
    t.splitCount = 0;
    t.sliceCarry = 0;
}

int Sim_scheduleSplit(World& w, std::int32_t frames) {
    FrameTime& t = w.time;
    std::int32_t d = static_cast<std::int32_t>(static_cast<std::uint32_t>(frames) << 16);
    if (t.splitCount == 20 || d <= 0) return 0;
    const int f = t.splitFree, fnext = t.splitNext[f];
    auto unlinkFree = [&] {
        if (fnext != -1) t.splitPrev[fnext] = -1;
        t.splitFree = fnext;
    };
    if (t.splitCount <= 0) { // 0x4E0176: first entry, relative to the current fractional frame
        unlinkFree();
        if (t.simAcc != 0) d -= t.simAcc;
        t.splitNext[f] = -1;
        t.splitPrev[f] = -1;
        t.splitTime[f] = d;
        t.splitHead = f;
        ++t.splitCount;
        return 1;
    }
    int cur = t.splitHead;
    std::int32_t frac = t.splitTime[cur] & 0xffff;              // only the head keeps a fraction
    std::int32_t span = t.splitTime[cur] & static_cast<std::int32_t>(0xffff0000u);
    if (frac) span += 0x10000;                                   // head rounded up to a whole frame
    while (span < d) {
        d -= span;
        const int nxt = t.splitNext[cur];
        if (nxt == -1) { // 0x4E007B: append at the tail
            unlinkFree();
            t.splitNext[f] = -1;
            t.splitTime[f] = d;
            t.splitPrev[f] = cur;
            t.splitNext[cur] = f;
            ++t.splitCount;
            return 1;
        }
        cur = nxt;
        frac = 0;
        span = t.splitTime[cur];
    }
    if (span == d) return 1; // 0x4E00D2: a boundary already falls there
    unlinkFree();            // 0x4E00D4: insert before cur
    if (frac) d += frac - 0x10000;
    t.splitTime[f] = d;
    t.splitNext[f] = cur;
    t.splitPrev[f] = t.splitPrev[cur];
    if (t.splitPrev[cur] != -1) t.splitNext[t.splitPrev[cur]] = f;
    else t.splitHead = f;
    t.splitPrev[cur] = f;
    t.splitTime[cur] -= d;
    ++t.splitCount;
    return 1;
}

void Sim_dispatchTimeSlice(World& w, Fixed dt, Fixed clockDt, Fixed cap) {
    FrameTime& t = w.time;
    dt += t.sliceCarry;
    t.sliceCarry = 0;
    if (dt > cap) dt = cap;
    if (t.splitCount > 0) {
        const int h = t.splitHead;
        const std::int32_t due = t.splitTime[h];
        if (due > dt) {
            t.splitTime[h] = due - dt;
        } else { // 0x4DFF24: run up to the split, carry the rest to the next call
            t.splitTime[h] = 0;
            t.sliceCarry = dt - due;
            const int nxt = t.splitNext[h];
            if (nxt != -1) t.splitPrev[nxt] = -1;
            t.splitNext[h] = t.splitFree;
            if (t.splitFree != -1) t.splitPrev[t.splitFree] = h; // list never full here in practice
            --t.splitCount;
            t.splitFree = h;
            t.splitHead = nxt;
            Sim_frame(w, due, clockDt);
            return;
        }
    }
    if (dt != 0) Sim_frame(w, dt, clockDt);
}

void Sim_advance(World& w, Fixed simDt, Fixed clockDt) {
    MatchState& m = w.match;
    if (m.flags278 & 0x800) return;
    if (w.opt.gameSpeed != 0) simDt += simDt >> ((4 - w.opt.gameSpeed) * 2); // 0x454FBF
    simDt &= ~0xff; // 0x454F15: `mov [esp], bl` clears the low byte of the scaled dt
    // DAT_0065f278 bits 0x40/0x80: fast-forward through cut-scene waits (set by the restart
    // sequences via 0x454E3C). The SeqTrigger fires/queries 0x21/0x24/0x28/0x22/0x25 around it
    // are presentation (queries never busy), so bit 0x100 handling reduces to clearing it.
    if (m.flags278 & 0xc0) {
        if (m.flags278 & 0x100) { // 0x454FDE
            accumulateTotalFrames(w, simDt);
            m.flags278 &= ~0x100u;
        }
        while (m.flags278 & 0xc0) {
            if (m.phase == 9) { // 0x45502E: one last slice runs with the bits cleared
                m.f280 = 0;
                m.flags278 &= ~0xc0u;
            }
            if (m.status & 0x20) return;
            Formation_drainQueue(w);
            Sim_dispatchTimeSlice(w, 0x1e0000, (m.flags278 & 0x40) ? kTickDt : 0x1e0000, 0x1e0000);
            ++m.f280;
        }
        return;
    }
    if (m.status & 0x20) return;
    if (simDt <= 0) return;
    Formation_drainQueue(w);
    Sim_dispatchTimeSlice(w, simDt, clockDt, kSliceCap);
}

SimRunner::SimRunner(const MatchData& data, const Options& opt, std::uint32_t seed) {
    w_.data = &data;
    w_.opt = opt;
    Match_init(w_, seed);
    Match_startPeriod(w_);
    period_ = 1;
}

bool SimRunner::tick() {
    if (finished_) return false;
    Sim_advance(w_, kTickDt, kTickDt); // MatchLoop_stepTick 0x41B8AC: Sim_advance(dt, dt, 60)
    switch (Match_periodResult(w_)) {
    case 1:
        Match_startPeriod(w_);
        ++period_;
        break;
    case 2:
        finished_ = true;
        break;
    default: break;
    }
    return !finished_;
}

} // namespace wc98::sim
