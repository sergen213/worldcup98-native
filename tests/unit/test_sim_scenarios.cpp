// Derived from fifa99-native (same author), ported for World Cup 98
// M7 gameplay regression scenarios (docs/development/M7_REGRESSIONS.md): end-to-end drives of the
// simulation through the public rules/event API on a synthetic MatchData (no game tree, no FCDB
// files). Each scenario runs its own SimRunner at a fixed seed, ticks under an explicit budget and
// asserts on the SimEvent stream + World state. Forcing (Match_dispatchEvent 0xb / executeKick +
// Kick_arm + Player_onAnimContact) is used exactly where M6/M7 proved the natural trigger is
// unreachable in a synthetic world (kick strike needs forced SetPiece kickoff; AI stalls t~130-150);
// every forcing site cites the spec. Assertions compare event types/teams/scores, not exact frames.
#include "check.hpp"

#include "wc98/sim/ball.hpp"
#include "wc98/sim/data.hpp"
#include "wc98/sim/events.hpp"
#include "wc98/sim/player.hpp"
#include "wc98/sim/rules.hpp"
#include "wc98/sim/sim.hpp"
#include "wc98/sim/world.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace wc98::sim {
void Kick_arm(World& w, int pid);                             // kick.cpp 0x4D0BB4 (not in header)
void Player_executeKick(World& w, int pid, int code, std::int32_t ballY); // kick.cpp 0x4E3478
std::int32_t Player_claimRange(const World& w, int pid);       // player_helpers.cpp 0x4BE034 (not in public header)
}

using namespace wc98::sim;

namespace {

// Seed constants picked from runs that survive the open-play AI stall (t~130-150) per
// M7_REGRESSIONS.md "Known gaps"; cards/offside seeds verified to pass the foul-class RNG gate.
constexpr std::uint32_t kSeed = 0x2E7A;

int gTick = 0;       // current tick of the running scenario, for failure prints
const char* gName = "";

void ck(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "[scenario %s] seed=0x%X tick=%d: %s\n", gName, kSeed, gTick, what);
        std::exit(1);
    }
}

MatchData makeMd() {
    MatchData md;
    for (int t = 0; t < 2; ++t) {
        md.sheets[t].team.id = std::uint16_t(26 + t * 41);
        for (int i = 0; i < 12; ++i) {
            wc98::formats::FcdbPlayer p{};
            p.id = std::uint16_t(t * 12 + i);
            p.shirtNumber = std::uint8_t(i + 1);
            p.startingSlot = std::uint8_t(i < 11 ? i : 0); // GK + 10 field, 1 sub each
            p.position = (i == 0) ? 0 : 3;                  // keeper / midfielder codes
            for (auto& a : p.attributes) a = 10;
            md.sheets[t].squad.push_back(p);
        }
    }
    md.anim.seqs.resize(0x1C4); // full seq table; flags 0x81 = ground player anims
    for (std::size_t i = 0; i < md.anim.seqs.size(); ++i) {
        md.anim.seqs[i].id = std::uint16_t(i);
        md.anim.seqs[i].flags = 0x81;
    }
    return md;
}

void setPos(World& w, int pid, int x, int z) {
    auto& p = w.players[static_cast<std::size_t>(pid)];
    p.posI = {x, 0, z};
    p.pos = {x << 16, 0, z << 16};
}

struct Rec {
    std::uint32_t frame;
    EventType type;
    int team, player, a, b;
};

std::vector<Rec> drain(World& w) {
    std::vector<Rec> out;
    for (auto& e : w.events) out.push_back({e.frame, e.type, e.team, e.player, e.a, e.b});
    w.events.clear();
    return out;
}

bool has(const std::vector<Rec>& v, EventType t) {
    for (auto& e : v) if (e.type == t) return true;
    return false;
}

// The forced-kick recipe proven by M6/M7 probes: plan via Player_executeKick, then override the
// contact-frame animation gate (no clip skeleton in the synthetic world -> Anim_contactPos is
// garbage) with animId 0x10 + contactFrame -1, arm and release at contact. M7_REGRESSIONS.md gap
// "kick needs forced Match_dispatchEvent 0xb; kick strike forcing is a sim-layer slice TODO".
void forceKick(World& w, int pid, int code, std::int32_t ballY) {
    w.pl.current = pid;
    Player_executeKick(w, pid, code, ballY);
    auto& k = w.players[static_cast<std::size_t>(pid)].kick;
    k.animId = 0x10;
    k.contactFrame = -1;
    Kick_arm(w, pid);
    Player_onAnimContact(w); // emits the kick event + Rules_checkOffside chain (natural path)
}

// Advance one tick and drain events; returns them.
std::vector<Rec> step(SimRunner& r) {
    ++gTick;
    if (!r.tick()) return drain(r.world());
    return drain(r.world());
}

int tmOf(const World& w, int pid) { return w.players[static_cast<std::size_t>(pid)].team; }

// ---------------------------------------------------------------------------------------------
// 1. Kickoff opens the match: period-start + kickoff events, ball on the centre spot.
void scenarioKickoff() {
    gName = "kickoff";
    MatchData md = makeMd();
    Options opt{};
    SimRunner r(md, opt, kSeed);
    auto& w = r.world();
    std::vector<Rec> all;
    for (int i = 0; i < 30 && !r.finished(); ++i) {
        auto ev = step(r);
        all.insert(all.end(), ev.begin(), ev.end());
    }
    ck(has(all, EventType::PeriodStart), "no period_start event");
    ck(has(all, EventType::KickOff), "no kickoff event");
    for (auto& e : all) {
        if (e.type == EventType::PeriodStart) {
            ck(e.a == 0 && e.b == 0, "period_start a/b not index 0 / type 0");
            ck(e.team == 1, "period_start team != 1");
        }
        if (e.type == EventType::KickOff) ck(e.team == 1, "kickoff team != 1");
    }
    ck(iabs(w.ball.posI.x) < 64 && iabs(w.ball.posI.z) < 64, "ball not on centre spot at start");
    ck(r.periodIndex() >= 0, "period index negative");
}

// ---------------------------------------------------------------------------------------------
// 2. Pass: a planned kick releases as a Pass event and the ball is taken by a same-team player.
void scenarioPass() {
    gName = "pass";
    MatchData md = makeMd();
    Options opt{};
    SimRunner r(md, opt, kSeed);
    auto& w = r.world();
    std::vector<Rec> passEvts, possEvts;
    int passer = -1, window = 0, forceIn = 0, prevPhase = -1;
    for (int i = 1; i <= 500 && !r.finished(); ++i) {
        auto ev = step(r);
        if (window > 0) { // events emitted by the driven kick + flight only
            passEvts.insert(passEvts.end(), ev.begin(), ev.end());
            --window;
        }
        if (w.match.phase == 0 && prevPhase != 0) forceIn = 150; // kickoff restart lineup settles (probe-pinned)
        prevPhase = w.match.phase;
        // Forced in-ball: natural SetPiece_begin kickoff strike is a sim-layer TODO per
        // M7_REGRESSIONS.md; scenario pins the forced path, then arms on the taker's possession.
        if (forceIn > 0 && --forceIn == 0 && w.match.phase == 0) Match_dispatchEvent(w, 0xb, w.match.phaseTeam);
        if (passer < 0 && forceIn == 0 && w.ball.possessor >= 0 && tmOf(w, w.ball.possessor) == 1) {
            passer = w.ball.possessor;
            setPos(w, passer, 0, 50); // centre-spot geometry: the seeded aim target is stable there
            Ball_placeAt(w, {0, 2, 35});
            w.ball.possessor = passer;
            w.ball.dribbler = passer;
            w.ball.flags |= 0x20;
            w.pl.current = passer;
            Player_executeKick(w, passer, 1, 2); // code 1: set-piece pass (kind 1), aims at a team-mate
            auto& k = w.players[static_cast<std::size_t>(passer)].kick;
            ck(k.kind == 1, "plan kind not 1");
            for (int pid = 13; pid <= 21; ++pid) setPos(w, pid, k.target.x + 30 * (pid % 3), k.target.z + 30 * (pid % 4)); // cluster receivers at the aim point
            for (int pid = 1; pid <= 9; ++pid) setPos(w, pid, -600 + 80 * pid, 700); // park team-0 fielders off the path
            k.animId = 0x10;
            k.contactFrame = -1;
            Kick_arm(w, passer);
            window = 350;
            ck(Player_onAnimContact(w) == 1, "kick release refused"); // emits Pass + checkOffside chain
        }
    }
    const Rec* pass = nullptr;
    for (auto& e : passEvts) if (e.type == EventType::Pass && e.a == 1) { pass = &e; break; }
    ck(pass != nullptr, "no Pass event");
    ck(pass->team == 1 && pass->player == passer, "pass team/player wrong");
    ck(pass->a == 1, "pass kind field not 1");
    for (auto& e : passEvts)
        if (e.type == EventType::Possession && tmOf(w, e.a) == 1 && e.a != passer) possEvts.push_back(e);
    ck(!possEvts.empty(), "no same-team possession transfer after pass");
}

// ---------------------------------------------------------------------------------------------
// 3. Shot -> Goal: a shot at the open net emits Shot then Goal and moves the score.
void scenarioShotGoal() {
    gName = "shot-goal";
    MatchData md = makeMd();
    Options opt{};
    SimRunner r(md, opt, kSeed);
    auto& w = r.world();
    std::vector<Rec> all;
    int forceIn = 0, prevPhase = -1, shotTick = 0;
    const int scoreBefore = w.match.score[1];
    bool sawGoal = false;
    for (int i = 1; i <= 400 && !r.finished() && !sawGoal; ++i) {
        auto ev = step(r);
        all.insert(all.end(), ev.begin(), ev.end());
        if (has(ev, EventType::Goal)) sawGoal = true; // stop at the goal: a re-armed kickoff would score twice
        if (w.match.phase == 0 && prevPhase != 0) forceIn = 154;
        prevPhase = w.match.phase;
        if (forceIn > 0 && --forceIn == 0 && w.match.phase == 0) {
            Match_dispatchEvent(w, 0xb, w.match.phaseTeam); // forced in-ball (see scenario 2 note)
            shotTick = gTick + 4;
        }
        if (shotTick && gTick == shotTick) {
            shotTick = 0;
            const int sh = 13; // team-1 field player, attacks the -z goal
            setPos(w, sh, -60, -2450);
            Ball_placeAt(w, {-60, 2, -2460});
            w.ball.possessor = sh;
            w.ball.dribbler = sh;
            w.ball.lastToucher = sh;
            w.ball.flags |= 0x20;
            setPos(w, 12, 0, 1500); // synthetic: keeper off its line so the net is open
            forceKick(w, sh, 14, 2); // code 14: driven shot (kind 4), set-piece aim at goal
        }
    }
    const Rec* shot = nullptr;
    const Rec* goal = nullptr;
    int goalIdx = -1, shotIdx = -1;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!shot && all[i].type == EventType::Shot) { shot = &all[i]; shotIdx = static_cast<int>(i); }
        if (!goal && all[i].type == EventType::Goal) { goal = &all[i]; goalIdx = static_cast<int>(i); }
    }
    ck(shot != nullptr, "no Shot event");
    ck(goal != nullptr, "no Goal event");
    ck(goalIdx > shotIdx, "goal not after shot");
    ck(shot->team == 1 && shot->player == 13 && shot->a == 4, "shot team/player/kind wrong");
    ck(goal->team == 1 && goal->player == 13, "goal credited to wrong team/scorer");
    ck(w.match.score[1] == scoreBefore + 1, "score[1] did not increase by one");
    ck(w.match.stats[1].goals >= 1, "stats goals not counted");
}

// ---------------------------------------------------------------------------------------------
// 4. Offside: a receiver beyond the last defender on a forward pass is flagged.
void scenarioOffside() {
    gName = "offside";
    MatchData md = makeMd();
    Options opt{};
    SimRunner r(md, opt, kSeed);
    auto& w = r.world();
    std::vector<Rec> all;
    int forceIn = 0, prevPhase = -1, probeTick = 0;
    for (int i = 1; i <= 400 && !r.finished(); ++i) {
        auto ev = step(r);
        all.insert(all.end(), ev.begin(), ev.end());
        if (w.match.phase == 0 && prevPhase != 0) forceIn = 154;
        prevPhase = w.match.phase;
        if (forceIn > 0 && --forceIn == 0 && w.match.phase == 0) {
            Match_dispatchEvent(w, 0xb, w.match.phaseTeam); // forced in-ball (see scenario 2 note)
            probeTick = gTick + 3;
        }
        if (probeTick && gTick == probeTick) {
            probeTick = 0;
            // teams[t].side is the goal t side defends; team 0 attacks +z.
            setPos(w, 5, 0, 800);    // passer (team 0)
            setPos(w, 8, 30, 2100);  // receiver running beyond the defensive line
            setPos(w, 16, 0, 1700);  // last defender (team 1), tracked in TeamState::f1bc
            w.teams[0].f1c0 = 8;     // pass bookkeeping: intended receiver / target
            w.teams[0].f1e0 = 8;
            w.teams[1].f1bc = 16;
            w.players[static_cast<std::size_t>(8)].state = 4; // not a keeper-hold state
            Ball_placeAt(w, {0, 2, 810});
            w.ball.possessor = 5;
            w.ball.flags |= 0x20;
            ck(Rules_isOffsidePosition(w, 8, 16) > 0x20, "isOffsidePosition not beyond the line");
            const std::int32_t pass[3] = {1300, 30, 1300}; // forward (+z), tight lateral spread
            w.ball.flags &= ~0x20u; // ball released (in flight)
            // Engine calls Rules_checkOffside from Player_onAnimContact after every kick
            // (kick.cpp); synthetic geometry has no AI pass, so drive the same public hook.
            Rules_checkOffside(w, 5, pass);
        }
    }
    const Rec* off = nullptr;
    for (auto& e : all) if (e.type == EventType::Offside) { off = &e; break; }
    ck(off != nullptr, "no Offside event");
    ck(off->team == 0 && off->player == 8, "offside team/player wrong (want attacking side)");
    ck(w.match.stats[0].offsides >= 1, "offside stat not counted");
    ck(w.match.restartType == 5, "restart type after offside not free kick (5)");
}

// ---------------------------------------------------------------------------------------------
// 5. Set pieces: ball out over the own goal line gives a Corner; out over the touchline gives a
// ThrowIn (both natural Rules_checkBallOut -> Match_dispatchEvent detections). One recipe per run
// (the post-corner restart cycles through lineup states in a synthetic world; spec's "single long
// run" is split into two seeded runs of the same kickoff, M7_REGRESSIONS.md deviation note).
void dribbleOut(World& w, int halfWidth, int halfLength, bool touchline) {
    const int sh = 13; // team-1 field player
    if (touchline) { // over the right touchline: throw-in for team 0
        setPos(w, sh, halfWidth - 55, 900);
        Ball_placeAt(w, {halfWidth - 45, 2, 900});
        w.players[static_cast<std::size_t>(sh)].facing = 0x100; // +x, over the line
    } else { // over his own goal line: corner for team 0
        setPos(w, sh, 300, halfLength - 55);
        Ball_placeAt(w, {300, 2, halfLength - 45});
        w.players[static_cast<std::size_t>(sh)].facing = 0; // +z
    }
    w.ball.possessor = sh;
    w.ball.dribbler = sh;
    w.ball.lastToucher = sh;
    w.ball.flags |= 0x20;
    forceKick(w, sh, 3, 2); // code 3: short push along facing (dribble out)
}

void runSetPiece(std::vector<Rec>& all, Options opt, std::uint32_t seed, bool touchline) {
    MatchData md = makeMd();
    SimRunner r(md, opt, seed);
    auto& w = r.world();
    int forceIn = 0, prevPhase = -1, planTick = 0;
    bool fired = false;
    for (int i = 1; i <= 700 && !r.finished() && !has(all, touchline ? EventType::ThrowIn : EventType::Corner); ++i) {
        auto ev = step(r);
        all.insert(all.end(), ev.begin(), ev.end());
        if (w.match.phase == 0 && prevPhase != 0) forceIn = 154;
        prevPhase = w.match.phase;
        // Forced in-ball: natural SetPiece kickoff strike is a sim-layer TODO per M7_REGRESSIONS.md.
        if (forceIn > 0 && --forceIn == 0 && w.match.phase == 0) {
            Match_dispatchEvent(w, 0xb, w.match.phaseTeam);
            planTick = gTick + 57; // probe-pinned: open play settled at dispatch+61 (probe15 t=211)
        }
        if (!fired && gTick == planTick) {
            fired = true; // one shot only: kicking during dead ball livelocks the restart cycle
            planTick = 0;
            if (w.match.phase == 9 && w.ball.possessor >= 0)
                dribbleOut(w, w.pitch.halfWidth, w.pitch.halfLength, touchline); // natural out-of-play detection path
        }
    }
}

void scenarioSetPieces() {
    gName = "set-pieces";
    std::vector<Rec> cornerEvts, throwEvts;
    runSetPiece(cornerEvts, Options{}, kSeed, false);
    ck(has(cornerEvts, EventType::Corner), "no Corner event from own-goal-line out");
    runSetPiece(throwEvts, Options{}, kSeed, true);
    ck(has(throwEvts, EventType::ThrowIn), "no ThrowIn event from touchline out");
}

// ---------------------------------------------------------------------------------------------
// 6. Foul -> free kick -> card: the foul sequence runs to completion and books the offender.
void scenarioFoulCard() {
    gName = "foul-card";
    MatchData md = makeMd();
    Options opt{};
    SimRunner r(md, opt, kSeed);
    auto& w = r.world();
    std::vector<Rec> all;
    int forceIn = 0, prevPhase = -1, foulTick = 0;
    // Stop ticking at the card: the free-kick restart after it livelocks without human input
    // (M6 AI stall gap, M7_REGRESSIONS.md) - the sequence Foul -> FreeKick -> Card is complete.
    for (int i = 1; i <= 500 && !r.finished() && !has(all, EventType::YellowCard); ++i) {
        auto ev = step(r);
        all.insert(all.end(), ev.begin(), ev.end());
        if (w.match.phase == 0 && prevPhase != 0) forceIn = 61; // probe-pinned RNG alignment (M7_REGRESSIONS.md seeds)
        prevPhase = w.match.phase;
        if (forceIn > 0 && --forceIn == 0 && w.match.phase == 0) {
            Match_dispatchEvent(w, 0xb, w.match.phaseTeam); // forced in-ball (see scenario 2 note)
            foulTick = gTick + 200;
        }
        if (foulTick && gTick == foulTick) {
            foulTick = 0;
            Rules_registerFoul(w, 5, 7, 15, nullptr, 0); // class 5: booking foul, no injury roll
            ck(w.match.foulClass == 5, "foul not registered (RNG gate)");
            // Force: the natural trigger is an AI tackle duel (unobservable in a synthetic world),
            // M7_REGRESSIONS.md; dispatch(7) = Match_onEvent foul path exactly as the exe does.
            Match_dispatchEvent(w, 7, 0);
        }
    }
    const Rec* foul = nullptr;
    const Rec* fk = nullptr;
    const Rec* card = nullptr;
    for (auto& e : all) {
        if (!foul && e.type == EventType::Foul) foul = &e;
        if (!fk && e.type == EventType::FreeKick) fk = &e;
        if (!card && e.type == EventType::YellowCard) card = &e;
    }
    ck(foul != nullptr, "no Foul event");
    ck(foul->a == 5 && foul->b == 15, "foul a(class)/b(victim) fields wrong");
    ck(fk != nullptr, "no FreeKick event");
    ck(fk->team == 1, "free kick not awarded to the fouled side (team 1)");
    ck(card != nullptr, "no YellowCard event in the seq-5 sequence");
    ck(card->player == 7 && card->a == 5, "card player/a(class) fields wrong");

    // Same seed, class 4 = straight red.
    SimRunner r2(md, opt, kSeed);
    auto& w2 = r2.world();
    forceIn = 0; prevPhase = -1; foulTick = 0; gTick = 0;
    all.clear();
    for (int i = 1; i <= 500 && !r2.finished() && !has(all, EventType::RedCard); ++i) {
        auto ev = step(r2);
        all.insert(all.end(), ev.begin(), ev.end());
        if (w2.match.phase == 0 && prevPhase != 0) forceIn = 61;
        prevPhase = w2.match.phase;
        if (forceIn > 0 && --forceIn == 0 && w2.match.phase == 0) {
            Match_dispatchEvent(w2, 0xb, w2.match.phaseTeam);
            foulTick = gTick + 200;
        }
        if (foulTick && gTick == foulTick) {
            foulTick = 0;
            Rules_registerFoul(w2, 4, 7, 15, nullptr, 0); // class 4: sending-off foul
            Match_dispatchEvent(w2, 7, 0);
        }
    }
    ck(has(all, EventType::RedCard), "no RedCard event for class-4 foul");
}

// ---------------------------------------------------------------------------------------------
// 7. Period transitions: full match (4 game-minute halves) with HT restart and FT finish.
void scenarioPeriods() {
    gName = "periods";
    MatchData md = makeMd();
    Options opt{}; // halfLengthMinutes default 4 -> HT ~t=7380, FT ~t=14796 (probe-pinned)
    SimRunner r(md, opt, kSeed);
    auto& w = r.world();
    std::vector<Rec> all;
    int forceIn = 0, prevPhase = -1;
    for (int i = 1; i <= 15200 && !r.finished(); ++i) {
        auto ev = step(r);
        all.insert(all.end(), ev.begin(), ev.end());
        if (w.match.phase == 0 && prevPhase != 0) forceIn = 154;
        prevPhase = w.match.phase;
        if (forceIn > 0 && --forceIn == 0 && w.match.phase == 0) {
            Match_dispatchEvent(w, 0xb, w.match.phaseTeam); // forced in-ball after each restart
            forceIn = 0;
        }
    }
    int starts = 0, ends = 0, kickoffs = 0;
    for (auto& e : all) {
        if (e.type == EventType::PeriodStart) ++starts;
        if (e.type == EventType::PeriodEnd) ++ends;
        if (e.type == EventType::KickOff) ++kickoffs;
    }
    ck(starts >= 2, "fewer than two period_start events");
    ck(ends >= 2, "fewer than two period_end events");
    ck(kickoffs >= 2, "no kickoff after the half-time restart");
    for (auto& e : all) {
        if (e.type == EventType::PeriodStart && e.a == 1)
            ck(e.b == 1, "period index 1 is not a normal period (type 1)");
        if (e.type == EventType::PeriodEnd && ends >= 2 && e.a == 15)
            ck(e.b == 1, "last period_end b not final type");
    }
    ck(r.finished(), "match not finished after regulation");
    ck(r.periodIndex() == 2, "periodIndex not 2 at full time");
}

// ---------------------------------------------------------------------------------------------
// 8. Shootout: gameMode 3 runs the kick sequence; dispatch 11 (kick) + 6/3 (goal/miss) per kick.
void scenarioShootout() {
    gName = "shootout";
    MatchData md = makeMd();
    Options opt{};
    opt.gameMode = 3; // penalty shoot-out
    SimRunner r(md, opt, kSeed);
    auto& w = r.world();
    std::vector<Rec> all;
    int kicks = 0, nextTick = 160;
    for (int i = 1; i <= 800 && !r.finished() && kicks < 4; ++i) {
        auto ev = step(r);
        all.insert(all.end(), ev.begin(), ev.end());
        if (kicks < 4 && i == nextTick) { // loop index = this runner's sim tick (gTick is suite-global)
            const int team = w.match.so.flags & 1; // engine-tracked next kicking side
            Match_dispatchEvent(w, 11, team);      // kick taken (Match_onShootoutEvent path)
            if (kicks % 2 == 0) Match_dispatchEvent(w, 6, team);  // goal
            else Match_dispatchEvent(w, 3, team);                 // miss
            ++kicks;
            nextTick = i + 40;
        }
    }
    int kicksEv = 0, goalsEv = 0, missesEv = 0;
    const Rec* firstKick = nullptr;
    for (auto& e : all) {
        if (e.type == EventType::ShootoutKick) { ++kicksEv; if (!firstKick) firstKick = &e; }
        if (e.type == EventType::ShootoutGoal) ++goalsEv;
        if (e.type == EventType::ShootoutMiss) ++missesEv;
    }
    ck(kicksEv >= 2, "fewer than two shootout_kick events");
    ck(goalsEv >= 1, "no shootout_goal for the scored kicks");
    ck(missesEv >= 1, "no shootout_miss for the missed kick");
    ck(firstKick && tmOf(w, firstKick->player) == firstKick->team, "kick event player not on kicking team");
    ck(w.match.so.goals[0] + w.match.so.goals[1] >= 2, "so.goals not incremented by the kicks");
}

// ---------------------------------------------------------------------------------------------
// 9. Extra time: a knockout fixture (opt.tieBreak != 0, DAT_0065EEEC) ends regulation tied ->
// MatchClock_update at clock expiry runs Match_decidePeriodOutcome (rules.cpp 0x46FBDC): with
// scores level and winTarget unset, requestPeriodEnd(whistle 1, nextPhase 0xe) starts the period
// table entry periods[2] = {type 2 extra time, half*20} built by Match_buildPeriodTable (0x454888)
// "normal" mode. Half length is shortened to one game minute per period so the full regulation
// drive stays in the ~0.25 s gate budget. Seed-pinned: no goal through 90' (a regulation lead ends
// the match at FT via Match_decideByTargets, nextPhase 0x11, and PeriodStart index 2 is not seen).
void scenarioExtraTime() {
    gName = "extra-time";
    MatchData md = makeMd();
    Options opt{};
    opt.tieBreak = 1;
    opt.halfLengthMinutes = 1;
    SimRunner r(md, opt, kSeed);
    auto& w = r.world();
    std::vector<Rec> all;
    int forceIn = 0, prevPhase = -1;
    bool etSeen = false;
    for (int i = 1; i <= 4500 && !r.finished() && !etSeen; ++i) {
        auto ev = step(r);
        all.insert(all.end(), ev.begin(), ev.end());
        for (auto& e : ev) if (e.type == EventType::PeriodStart && e.a == 2 && e.b == 2) etSeen = true;
        if (w.match.phase == 0 && prevPhase != 0) forceIn = 154;
        prevPhase = w.match.phase;
        if (forceIn > 0 && --forceIn == 0 && w.match.phase == 0) {
            Match_dispatchEvent(w, 0xb, w.match.phaseTeam); // forced in-ball (see scenario 2 note)
            forceIn = 0;
        }
    }
    ck(etSeen, "no PeriodStart index 2 type 2 (extra-time entry at tied FT)");
    ck(w.match.score[0] == w.match.score[1], "FT score not tied at extra-time entry");
    ck(w.match.periodType == 2 && w.match.periodIndex == 2, "periodType/periodIndex not 2 in extra time");
    bool endOfSecond = false;
    for (auto& e : all) if (e.type == EventType::PeriodEnd && e.a == 0xe && e.b == 1) endOfSecond = true;
    ck(endOfSecond, "no PeriodEnd(nextPhase 0xe, type 1) for the tied second half");
    ck(r.periodIndex() == 3, "periodIndex not 3 (third period started) in extra time");
}

// ---------------------------------------------------------------------------------------------
// 10. Golden goal: first goal in extra time ends the match immediately - Match_onEvent case 6
// (rules.cpp 0x452A58): m.periodType == 2 -> Match_endPeriodSuddenDeath (FUN_0046FD94), i.e. WC98's
// knockout extra time is sudden death; periods[3] never starts and no period-4 goldenGoalTarget
// check (DAT_0065EEF0, gameMode 6 table) is needed on this path. Shot forcing = scenario 3 recipe.
void scenarioGoldenGoal() {
    gName = "golden-goal";
    MatchData md = makeMd();
    Options opt{};
    opt.tieBreak = 1;
    opt.halfLengthMinutes = 1;
    SimRunner r(md, opt, kSeed);
    auto& w = r.world();
    std::vector<Rec> all;
    int forceIn = 0, prevPhase = -1, shotTick = 0;
    bool etSeen = false, fired = false, sawGoal = false;
    for (int i = 1; i <= 4500 && !r.finished() && !sawGoal; ++i) {
        auto ev = step(r);
        all.insert(all.end(), ev.begin(), ev.end());
        for (auto& e : ev) if (e.type == EventType::PeriodStart && e.a == 2 && e.b == 2) etSeen = true;
        if (w.match.phase == 0 && prevPhase != 0) forceIn = 154;
        prevPhase = w.match.phase;
        if (forceIn > 0 && --forceIn == 0 && w.match.phase == 0) {
            Match_dispatchEvent(w, 0xb, w.match.phaseTeam); // forced in-ball (see scenario 2 note)
            if (etSeen && !fired) shotTick = gTick + 4;      // strike once extra-time play is live
        }
        if (shotTick && gTick == shotTick) {
            shotTick = 0; fired = true;
            const int sh = 13; // team-1 field player attacks the -z goal (ET period sideFlag 0)
            setPos(w, sh, -60, -2450);
            Ball_placeAt(w, {-60, 2, -2460});
            w.ball.possessor = sh;
            w.ball.dribbler = sh;
            w.ball.lastToucher = sh;
            w.ball.flags |= 0x20;
            setPos(w, 12, 0, 1500); // keeper off its line so the net is open (scenario 3 recipe)
            forceKick(w, sh, 14, 2); // code 14: driven shot (kind 4), set-piece aim at goal
        }
        if (has(ev, EventType::Goal)) sawGoal = true;
    }
    ck(etSeen, "extra time not reached");
    ck(fired, "golden-goal shot never fired in extra time");
    const Rec* goal = nullptr;
    for (auto& e : all) if (e.type == EventType::Goal) { goal = &e; break; }
    ck(goal != nullptr, "no Goal event in extra time");
    ck(goal->team == 1 && goal->player == 13, "golden goal credited to wrong team/scorer");
    for (auto& e : all)
        ck(!(e.type == EventType::PeriodStart && e.a == 3), "second extra-time half started after the golden goal");
    int guard = 0; // seq 9 runs the full-time sequence: tick out to finished()
    while (!r.finished() && ++guard < 400) {
        auto ev = step(r);
        all.insert(all.end(), ev.begin(), ev.end());
    }
    ck(r.finished(), "match not finished right after the golden goal");
    ck(has(all, EventType::MatchEnd), "no MatchEnd event after the golden goal");
    ck(w.match.endTeam == 1, "endTeam (winner) is not the scoring team");
    ck(w.match.score[1] == w.match.score[0] + 1 && w.match.score[0] == 0, "golden goal did not win it 1-0");
}
// ---------------------------------------------------------------------------------------------
// 11. Difficulty control levels (gap O): Team_setControlLevels_45159C derives the ctrl1f4/1f8/1fc
// triple from opt.aiAssist + human-controller count; consumers (claim range kRange[ctrl1f8]
// {0x55,0x42,0x2f}, fatigue-tick predicate ctrl1f8!=0, keeper ctrl1fc) must see distinct values.
void scenarioDifficulty() {
    gName = "difficulty";
    MatchData md = makeMd();

    // A: aiAssist 0 (no assist), one human on team 0 vs AI team 1, gameMode 0 (exhibition).
    Options optA{};
    optA.aiAssist = 0;
    SimRunner rA(md, optA, kSeed);
    auto& wA = rA.world();
    wA.teams[0].humans = 1;
    wA.teams[1].humans = 0;
    Team_setControlLevels(wA, 0);
    Team_setControlLevels(wA, 1);
    ck(wA.teams[0].ctrl1f4 == 0 && wA.teams[0].ctrl1f8 == 0 && wA.teams[0].ctrl1fc == 2,
       "assist 0 human side triple != {f4:0,f8:0,fc:2}");   // edx==0: esi=0, --c, eax = AI opp -> 2
    ck(wA.teams[1].ctrl1f4 == 0 && wA.teams[1].ctrl1f8 == 1 && wA.teams[1].ctrl1fc == 0,
       "assist 0 AI side triple != {f4:0,f8:1,fc:0}");       // humans==0 edx==0: eax=esi=0, c stays

    // B: aiAssist 2 (hardest) on the same fixture; consumer-relevant slots must differ from A.
    Options optB{};
    optB.aiAssist = 2;
    SimRunner rB(md, optB, kSeed);
    auto& wB = rB.world();
    wB.teams[0].humans = 1;
    wB.teams[1].humans = 0;
    Team_setControlLevels(wB, 0);
    Team_setControlLevels(wB, 1);
    ck(wB.teams[0].ctrl1f4 == 0 && wB.teams[0].ctrl1f8 == 2 && wB.teams[0].ctrl1fc == 0,
       "assist 2 human side triple != {f4:0,f8:2,fc:0}");    // edx==2: esi=0, ++c
    ck(wB.teams[1].ctrl1f4 == 2 && wB.teams[1].ctrl1f8 == 1 && wB.teams[1].ctrl1fc == 2,
       "assist 2 AI side triple != {f4:2,f8:1,fc:2}");       // humans==0 edx==2: esi=eax=2

    // Behavioural A/B: the claim range follows ctrl1f8 and the fatigue predicate flips; keeper
    // ctrl1fc differs between levels on both sides.
    ck(Player_claimRange(wA, wA.teams[0].players[1]) == 0x55 &&
       Player_claimRange(wB, wB.teams[0].players[1]) == 0x2f,
       "claim range does not follow ctrl1f8 (kRange {0x55,0x42,0x2f})");
    ck((wA.teams[0].ctrl1f8 != 0) == false && (wB.teams[0].ctrl1f8 != 0) == true,
       "fatigue-tick predicate ctrl1f8!=0 identical across difficulty levels");
    ck(wA.teams[0].ctrl1fc != wB.teams[0].ctrl1fc && wA.teams[1].ctrl1fc != wB.teams[1].ctrl1fc,
       "keeper ctrl1fc identical across difficulty levels");
}
} // namespace

int main() {
    scenarioKickoff();
    scenarioPass();
    scenarioShotGoal();
    scenarioOffside();
    scenarioSetPieces();
    scenarioFoulCard();
    scenarioPeriods();
    scenarioShootout();
    scenarioExtraTime();
    scenarioGoldenGoal();
    scenarioDifficulty();
    return 0;
}
