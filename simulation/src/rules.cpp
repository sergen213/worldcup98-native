// Match rules: lifecycle, clock, phase state machine, event dispatchers, ball out of play, fouls,
// cards, offside, penalty shoot-out and the statistics block. Ported from FIFA99.EXE v1.1 (addresses
// in the comments). Presentation-only callees (camera 0x470C80/0x4CB778/0x4CB904/0x4BC124, sounds
// 0x4A273C/0x4A2798, commentary 0x4D6AF0/0x4D6B00/0x4D6AAC/0x4D7FE8, replay 0x454E3C/0x454E74/0x4D9D14/
// 0x4D45A0, popups 0x480958, SeqTrigger_arm 0x581BB0) are dropped; none of them touches the RNG or
// simulation state.
#include <algorithm>

#include "wc98/sim/sim.hpp"

namespace wc98::sim {

namespace {

using u32 = std::uint32_t;

int teamOf(const World& w, int p) { return w.players[p].team; }
int oppOf(const World& w, int t) { return w.teams[t].opponent; }

// FUN_00452978(EAX = z): team attacking the goal at that end.
int teamAttackingZ(const World& w, std::int32_t z) {
    if (z < 1) return w.teams[0].side == 1 ? 0 : 1;
    return w.teams[0].side == 0 ? 0 : 1;
}

// FUN_0045299C(EAX = team): first player of the team list with slot 0.
int findKeeper(World& w, int t) {
    const TeamState& T = w.teams[t];
    for (int i = 0; i < T.playerCount; ++i)
        if (w.players[T.players[i]].slot == 0) return T.players[i];
    if (w.opt.gameMode != 1) (void)Team_tooFewPlayers(w, t);
    return -1;
}

// FUN_004523E4(EAX = pos, EDX = team, EBX = excluded curSlot, ECX = out distance): nearest player.
int nearestPlayer(const World& w, const Vec3& pos, int t, int excl, std::int32_t* outDist) {
    const TeamState& T = w.teams[t];
    u32 best = 0x7fff8000;
    int found = -1;
    for (int i = 0; i < T.playerCount; ++i) {
        const PlayerState& p = w.players[T.players[i]];
        if (p.flags22c & 2) continue;
        if (p.curSlot == excl) continue;
        if (p.flags22c & 1) continue;
        std::int32_t dx = iabs(pos.x - p.posI.x), dz = iabs(pos.z - p.posI.z);
        const u32 d = static_cast<u32>(dx + dz - ((dx <= dz ? dx : dz) >> 1));
        if (d < best) {
            best = d;
            found = T.players[i];
        }
    }
    if (outDist) *outDist = static_cast<std::int32_t>(best);
    return found;
}

// FUN_00455D98: inside a penalty area (either end).
bool inPenaltyBox(const World& w, const Vec3& p) {
    return iabs(p.z) > w.pitch.penAreaLineZ && iabs(p.x) < w.pitch.penAreaHalfWidth;
}

// FUN_00455F38: clamp x/z to the field.
void clampToField(const World& w, Vec3& p) {
    const std::int32_t hw = w.pitch.halfWidth, hl = w.pitch.halfLength;
    if (-hw > p.x) p.x = -hw;
    else if (p.x > hw) p.x = hw;
    if (-hl > p.z) p.z = -hl;
    else if (p.z > hl) p.z = hl;
}

// Game time recorded by goals and cards: totalSec without the added time of this period.
std::int32_t cappedTotal(const World& w) {
    const MatchState& m = w.match;
    std::int32_t t = m.totalSec;
    if (static_cast<u32>(m.elapsedSec) > static_cast<u32>(m.durationSec)) t = t - m.elapsedSec + m.durationSec;
    return t;
}

// FUN_0049F7E4(team, playerIndex): squad number from the lineup table 0x66E564.
int squadOf(const World& w, int t, int idx) { return w.pl.lineup[t][idx]; }
PlayerAttr& attrOf(World& w, int t, int squad) { return w.attrs[t * kSquadMax + squad]; }

// FUN_004D00A0(EAX team, EDX player index, EBX own goal, ECX penalty, stack time): goal log.
void recordGoal(World& w, int t, int idx, int og, int pen, std::int32_t time) {
    MatchState& m = w.match;
    const bool scoring = w.opt.gameMode != 3 && m.periodType != 3;
    if (scoring) ++m.score[og ? (t == 0 ? 1 : 0) : t];
    if (m.goalCount == 0x50) {
        for (int i = 0; i < 0x4f; ++i) m.goalLog[i] = m.goalLog[i + 1];
        --m.goalCount;
    }
    const int sq = squadOf(w, t, idx);
    const u32 b0 = (t & 1) | ((og & 1) << 1) | ((pen & 1) << 2) | ((m.periodType == 3 ? 1u : 0u) << 3);
    m.goalLog[m.goalCount] = (m.goalLog[m.goalCount] & 0xf0) | b0 | ((sq & 0xff) << 8) |
                             (static_cast<u32>(time & 0xffff) << 16);
    ++m.goalCount;
    if (!scoring) return;
    PlayerAttr& a = attrOf(w, t, sq);
    if (og) ++a.ownGoals;
    else ++a.goals;
}

// FUN_004CFFE4(EAX team, EDX player index, EBX red, ECX time): card log.
void recordCard(World& w, int t, int idx, int red, std::int32_t time) {
    MatchState& m = w.match;
    ++m.stats[t].cards;
    const int sq = squadOf(w, t, idx);
    if (m.cardCount == 0x14) {
        for (int i = 0; i < 0x13; ++i) m.cardLog[i] = m.cardLog[i + 1];
        --m.cardCount;
    }
    m.cardLog[m.cardCount] = (m.cardLog[m.cardCount] & 0xfc) | (t & 1) | ((red & 1) << 1) | ((sq & 0xff) << 8) |
                             (static_cast<u32>(time & 0xffff) << 16);
    ++m.cardCount;
}

// FUN_004D0224: foul statistics.
void countFoul(World& w) {
    const MatchState& m = w.match;
    if (m.victim < 0 || m.offender < 0) return;
    ++w.attrs[w.players[m.offender].attr].fouls;
    ++w.attrs[w.players[m.victim].attr].fouled;
    ++w.match.stats[teamOf(w, m.offender)].fouls;
}

// Stats_countOffside 0x4D0274.
void countOffside(World& w) {
    const int o = w.match.offender;
    if (o < 0) return;
    ++w.attrs[w.players[o].attr].offsides;
    ++w.match.stats[teamOf(w, o)].offsides;
}

// FUN_004D03D0(EAX = team): match abandoned, 2-0 against the team.
void abandonMatch(World& w, int t) {
    MatchState& m = w.match;
    m.score[t] = 0;
    m.score[t == 0 ? 1 : 0] = 2;
    m.goalCount = 0;
    m.stats[t].flags |= 1;
    for (int k = 0; k < kSquadMax; ++k) {
        w.attrs[k].goals = w.attrs[k].ownGoals = 0;
        w.attrs[kSquadMax + k].goals = w.attrs[kSquadMax + k].ownGoals = 0;
    }
}

// FUN_0046FD94: early decision for sudden death (two-leg aggregate when DAT_0065EEEC == 4).
int decideSuddenDeath(const World& w) {
    const MatchState& m = w.match;
    if (w.opt.tieBreak == 4) {
        int a = m.score[0] + w.opt.firstLegGoals[0], b = m.score[1] + w.opt.firstLegGoals[1];
        if (a == b) {
            a = w.opt.firstLegGoals[0];
            b = m.score[1];
        }
        return Match_decideByTargets(a, b, w);
    }
    return Match_decideByTargets(m.score[0], m.score[1], w);
}

// FUN_00470188 / 0x4701C0: a goalkeeper holding the ball drops it.
void keeperRelease(World& w, int t) {
    const int k = findKeeper(w, t);
    if (k < 0 || k != w.ball.possessor || !(w.ball.flags & 0x20)) return;
    Ball_releaseFromPossessor(w, k);
    Anim_start(w, k, 0x166, 0, w.players[kReferee].facing); // 0x4CCCFC(player+0x64, 0x166, EBX 0, ECX referee facing [0x6676C4])
    if (w.players[k].slot == 0) Keeper_release_4BF29C(w, k);
}

// FUN_00470188: both teams' goalkeepers drop a held ball.
void keepersReleaseAll(World& w) {
    keeperRelease(w, 0);
    keeperRelease(w, 1);
}

// FUN_0046F734(EAX = team): shoot-out order (5 designated takers team+0x168, then the outfield
// players in team order, the goalkeeper last).
void buildKickOrder(World& w, int t) {
    MatchState& m = w.match;
    std::int32_t used[11] = {};
    int n = 0;
    const TeamState& T = w.teams[t];
    for (int k = 0; k < 5; ++k) {
        const int off = 0x168 - 0x0c + k * 2;
        const int squad = T.blk[off] | (T.blk[off + 1] << 8);
        const int p = Team_playerBySquad(w, t, squad);
        if (p >= 0) {
            used[w.players[p].index] = 1;
            m.kickOrder[t][n++] = w.players[p].index;
        }
    }
    int keeper = 0xff;
    for (int i = 0; i < T.playerCount; ++i) {
        const PlayerState& p = w.players[T.players[i]];
        if (used[p.index]) continue;
        if (p.slot == 0) {
            keeper = p.index;
            continue;
        }
        m.kickOrder[t][n++] = p.index;
        used[p.index] = 1;
    }
    if (keeper != 0xff) m.kickOrder[t][n] = keeper;
}

// FUN_004701E8(EAX team, EDX round).
int kickerFor(const World& w, int t, int round) {
    return w.match.kickOrder[t][static_cast<u32>(round) % static_cast<u32>(w.teams[t].playerCount)];
}

// FUN_00470224(EAX = scored): records a shoot-out kick, returns the winner or 2.
int recordShootoutKick(World& w, int scored) {
    MatchState& m = w.match;
    ShootoutState& s = m.so;
    if (s.flags & 0x20) return m.endTeam;
    const int t = s.flags & 1, o = t ^ 1;
    --s.remaining[t];
    recordGoal(w, t, s.kicker[t], scored ? 0 : 1, 1, s.round);
    const int slot = static_cast<int>(static_cast<u32>(s.round) % 5);
    if (scored) {
        ++s.goals[t];
        s.result[t][slot] = 2;
    } else {
        s.result[t][slot] = 1;
    }
    s.taker[t][slot] = w.teams[t].players[s.kicker[t]];
    int winner = 2;
    if (static_cast<u32>(s.goals[o] + s.remaining[o]) < static_cast<u32>(s.goals[t])) winner = t;
    else if (static_cast<u32>(s.goals[t] + s.remaining[t]) < static_cast<u32>(s.goals[o])) winner = o;
    int cur;
    if (winner != 2) {
        s.flags |= 0x20;
        ++m.score[winner];
        s.flags &= ~6u;
        cur = s.flags & 1;
    } else {
        if (s.remaining[t] == 0 && s.remaining[o] == 0) {
            s.remaining[o] = 1;
            s.remaining[t] = 1;
        }
        s.flags = (s.flags & ~1u) | static_cast<u32>(o);
        if (static_cast<std::int32_t>(s.flags & 1) == s.first) {
            ++s.round;
            if (static_cast<u32>(s.round) % 5 == 0)
                for (int i = 0; i < 5; ++i) s.result[0][i] = s.result[1][i] = 0;
        }
        cur = s.flags & 1;
        s.kicker[cur] = kickerFor(w, cur, s.round);
        s.result[o][static_cast<u32>(s.round) % 5] = 3;
        const int prev = cur ^ 1;
        s.flags = (s.flags & ~2u) |
                  (static_cast<u32>(s.goals[cur] + 1) > static_cast<u32>(s.goals[prev] + s.remaining[prev]) ? 2u : 0u);
        s.flags = (s.flags & ~4u) |
                  (static_cast<u32>(s.goals[cur] + s.remaining[cur] - 1) < static_cast<u32>(s.goals[prev]) ? 4u : 0u);
    }
    m.shootoutKicker = w.teams[cur].players[s.kicker[cur]];
    return winner;
}

// Training module 0x46D420..0x46DC00 and the set-piece aim helpers 0x44C3E0/0x44C560/0x44C5B4 are
// not ported (training mode only); their effects are listed in SIMULATION_STATUS.md.
void untraced_Training_reset_46D704(World&) {}
void untraced_Training_placeBall_46D7BC(World&) {}
int untraced_Training_ballPlaced_46D83C(World&) { return 1; }
void untraced_Training_drillEnd_46DA18(World&) {}
void untraced_Training_nextDrill_46D658(World&) {}
void untraced_Training_restart_46D570(World&) {}
void untraced_Training_onPhase_46DB28(World&) {}

} // namespace

// ---------------------------------------------------------------------------------------------
// Lifecycle

void Match_setupOptions(World& w) {
    Options& o = w.opt;
    if (static_cast<u32>(o.gameMode) > 0x11) o.gameMode = 0x11;
    if (static_cast<u32>(o.surfaceColumn) > 4) o.surfaceColumn = 4;
    if (static_cast<u32>(o.surfaceRow) > 1) o.surfaceRow = 1;
    if (static_cast<u32>(o.clockMode) > 1) o.clockMode = 1;
    for (int t = 0; t < 2; ++t)
        if (static_cast<u32>(o.teamPlayers[t]) > 0xb) o.teamPlayers[t] = 0xb; // 0x65F048 / 0x65F17C
    // memset 0x65F324 (0x120), Pitch_setDimensions 0x455930 (-> Formation_initZoneScales 0x4CE5EC)
    w.pitch = Pitch::make(o.pitchSize);
    Formation_initZoneScales(w);
    w.teams = {};                 // memset 0x65A2D0, 0x790
    MatchState fresh;             // memset 0x65F22C (0xC4), 0x6675D0 (0x154), 0x65F1C0 (0x6C), 0x667410
    fresh.periods[0] = w.match.periods[0];
    w.match = fresh;
    // 0x4545E9.. Players_initArrays 0x4511D0, 0x4BB9E0, Script_initEngine 0x464344 (presentation)
    Players_initArrays_4511D0(w);
    Players_init_4BB9E0(w);
    Sim_initSplitList(w);         // FUN_004DFE80 at 0x454652
    Formation_init(w);            // Formation_loadTables 0x4CE374
    Anim_initTables_4CC6B0(w);    // 0x4CC6B0 (0x4CB2C4 is an empty function)
    Input_initControllers(w); // 0x466D84
    PlayerAI_reset_4BEC34(w);
    Tackle_buildMoves(w);         // 0x4D18E0 (0x4749A0 = SeqTrigger init, presentation)
    for (TeamState& t : w.teams) t.setupAuto = 1; // DAT_0065F088 / 0x65F1BC = 1
}

void Match_buildPeriodTable(World& w, int index, int mode) {
    MatchState& m = w.match;
    PeriodEntry* p = m.periods;
    const std::int32_t half = w.opt.halfLengthMinutes;
    switch (mode) {
    default: {
        p[0].type = 0;
        p[1].type = 1;
        p[2].type = p[3].type = mode == 0 ? 2 : 1;
        p[4].type = 3;
        p[0].durationSec = half * 60;
        p[4].durationSec = 0;
        p[0].sideFlag = 0;
        p[2].durationSec = half * 20;
        p[2].sideFlag = 0;
        p[4].sideFlag = 0;
        p[1].sideFlag = 1;
        p[3].sideFlag = 1;
        p[1].durationSec = p[0].durationSec;
        p[3].durationSec = p[2].durationSec;
        p[0].kickoffTeam = (w.rng.next() & 1) == 0 ? 1 : 0;
        p[1].kickoffTeam = p[0].kickoffTeam == 0 ? 1 : 0;
        p[3].kickoffTeam = p[0].kickoffTeam == 0 ? 1 : 0;
        p[4].kickoffTeam = 1;
        p[2].kickoffTeam = p[0].kickoffTeam;
        break;
    }
    case 2:
        if (static_cast<u32>(index) < 6) p[index] = {3, 0, 0, 1};
        break;
    case 3:
        if (static_cast<u32>(index) < 5) {
            const std::int32_t d = half * 20;
            p[index] = {2, d, p[0].sideFlag, p[0].kickoffTeam};
            p[index + 1] = {2, d, p[0].sideFlag == 0 ? 1 : 0, p[0].kickoffTeam == 0 ? 1 : 0};
        }
        break;
    case 4:
        if (static_cast<u32>(index) < 6) p[index] = {0, 3000, 0, 0};
        break;
    case 5:
        p[index].type = 4;
        p[index].sideFlag = 0;
        p[index].durationSec = half * 60;
        p[index].kickoffTeam = (w.rng.next() & 1) ? 0 : 1;
        break;
    }
}

void Match_init(World& w, std::uint32_t seed) {
    Match_setupOptions(w);
    // Match_init 0x4546F8
    MatchState& m = w.match;
    w.rng.seed(seed);
    w.time.focus = w.opt.gameMode == 3 ? -3 : -1;
    m.flags278 &= 0xfffef7ff;
    m.totalSec = m.elapsedSec = m.injurySec = m.clockFrames = m.durationSec = 0;
    w.time.totalFrames = w.time.stampFrames = w.time.stampAcc = 0;
    w.time.fatigueInterval = w.time.fatigueCounter = 0;
    m.blockUntil = m.keeperHoldUntil = m.f26c = 0;
    m.status &= 0x80;
    for (std::uint8_t& b : m.f2e8) b = 0;
    // FUN_004CFF94 zeroes 0x65E050..0x65EE88 (score, logs, team stats, attribute records, shoot-out).
    Anim_resetEngine_45F648(w);   // 0x4547C2
    Players_buildSpeedTables(w);
    m.score[0] = m.score[1] = m.goalCount = m.cardCount = 0;
    for (std::uint32_t& e : m.goalLog) e = 0;
    for (std::uint32_t& e : m.cardLog) e = 0;
    m.stats[0] = m.stats[1] = TeamStats{};
    m.so = ShootoutState{};
    Team_initPlayers(w, 0, w.opt.teamPlayers[0]);
    Team_initPlayers(w, 1, w.opt.teamPlayers[1]);
    Team_initMatch(w, 0);
    Team_initMatch(w, 1);
    // FUN_0046F39C: memset the referee movement object 0x66764C (0xD8) and the foul state 0x65E018
    // (0x38), link M+0 <-> anim 0x6675F8, Anim_resetToIdle(referee, fresh), then FUN_0046F2EC.
    {
        PlayerState& r = w.players[kReferee];
        const AnimCtl a = r.anim;
        r = PlayerState{};
        r.anim = a;
        Anim_init(w, kReferee, true);
    }
    m.foulClass = m.card = m.foulInBox = 0;
    m.foulSpot = {};
    m.offender = m.victim = m.injuredPlayer = -1;
    m.injury = m.cardRange = m.offenderCtrl = m.victimCtrl = 0;
    MatchSeq_reset(w);
    Ball_init(w);
    SkillMoves_build(w); // 0x4E073C
    PlayerAI_reset_4BEC34(w);
    PlayerAttr_resetTeam(w, 0);
    PlayerAttr_resetTeam(w, 1);
    w.teams[0].flags &= ~0x400u; // byte 0x65A695 &= 0xFB
    w.teams[1].flags &= ~0x400u;
    Match_handicap(w, w.opt.skillLevel);
    m.f27c = 0x8e;
    (void)w.rng.next(); // FUN_0046A704: commentary timer DAT_0058F864 = rand & 0xF (presentation)
    // Match_run 0x40A414: period table mode from the game mode.
    int mode = 0;
    const u32 gm = static_cast<u32>(w.opt.gameMode);
    if (gm < 3) mode = gm == 1 ? 4 : 0;
    else if (gm == 3) mode = 2;
    else if (gm == 6) mode = 5;
    Match_buildPeriodTable(w, 0, mode);
}

// Match_run 0x40A874: Match_startPeriod(DAT_005AAB98); DAT_005AAB98++.
void Match_startPeriod(World& w) { Match_startPeriodIndex(w, w.match.nextPeriod++); }

void Match_startPeriodIndex(World& w, int index) {
    MatchState& m = w.match;
    FrameTime& t = w.time;
    Formation_resetCaches(w);
    Match_resetPhase(w);
    m.f280 = m.f26c = m.throwAnim = m.f258 = 0;
    m.flags278 &= 0xfffe2a3f;
    m.totalSec = m.totalSec - m.elapsedSec + m.durationSec;
    const PeriodEntry& p = m.periods[index];
    m.periodType = p.type;
    m.durationSec = p.durationSec;
    m.periodIndex = index;
    m.elapsedSec = m.clockFrames = m.injurySec = 0;
    t.clockFrames = 0;
    m.stoppageFrames = m.deadFrames = 0;
    m.blockUntil = m.keeperHoldUntil = 0;
    t.simAcc = t.simFrames = t.clockAcc = t.timer65f1f8 = 0;
    m.f1fc = m.f200 = 0;
    t.fatigueInterval = static_cast<std::int32_t>((static_cast<u32>(p.durationSec * 60) / 45) * 5);
    m.status = ((m.status | 3) & 0x93);
    m.possessionTimer = 9999;
    t.fatigueCounter = 0;
    if (w.opt.funModes & 0x10) m.possessionTimer = static_cast<std::int32_t>((w.rng.next() & 0xf) + 10);
    const std::int32_t flag = p.sideFlag;
    std::int32_t side1 = flag == 0 ? 1 : 0;
    m.periodSideFlag = flag;
    if (p.type == 3) {
        m.phase = m.restartType = 6;
        side1 = flag;
    }
    Team_startPeriod(w, 0, flag);
    Team_startPeriod(w, 1, side1);
    if (m.periodIndex != 0) {
        PlayerAttr_newPeriod(w, 0);
        PlayerAttr_newPeriod(w, 1);
    }
    Team_setControlLevels(w, 0);
    Team_setControlLevels(w, 1);
    PlayerAI_reset_4BEC34(w);     // 0x454CA6
    Ball_resetForPeriod(w);
    Match_selectEventDispatcher(w, p.type);
    w.emit(EventType::PeriodStart, p.kickoffTeam, -1, index, p.type);
    Match_dispatchEvent(w, 0, p.kickoffTeam);
}

// WC98 h6 kickoff placement task (FUN_00516f40) modeled per-frame in the port sim —
// TASK_HANDLERS_WC98 §5 "h6" / §7 delta-3. The exe task dollies its presentation target from the
// ball world position with a side-flip + spin adjust ±DAT_00656e08 (attack sign); the port drives
// the ball itself toward the centre spot over the kickoff restart window, replacing the direct-set
// placement in the kick path (Match_resetPhase, seq4TakeRestart rt == 0). Other restarts untouched.
namespace {
constexpr std::int32_t kKickoffDollySpeed = 0x3c0; // h6 path A polar speed sin/cos x 0x3c0 per frame [P]
constexpr std::int32_t kKickoffSideFlipTimeout = 0x27fff; // rec+0x1c4 side-flip compare threshold [P]

// h6 0x51712B..0x5171B7 (rec+0x1cc): direction +1 when (ballX >= 1) == (sideBit != 0), else -1. [P]
void kickoffPlaceSetDir(World& w, KickoffPlaceState& kp) {
    kp.dir = ((w.ball.posI.x >= 1) == (kp.sideByte != 0)) ? 1 : -1;
}
} // namespace

std::int32_t kickoffSpinUnits(const World& w) {
    // DAT_00656e08: h6 spin adjust ±DAT_00656e08 by attack sign [P, sci4 0x517043..0x51704B] and the
    // pitch-mesh z-grid origin - FUN_005166f0 samples cell i as `(z + DAT_00656e08) * DAT_00656e18`
    // (§4 grid layout [P]). The slot is a BSS variable computed at setup (write site MOV dword ptr
    // [0x656e08], ECX; pass7 pick3 raw 161970), so the numeric value is not byte-provable from the
    // image: port derivation = the pitch mesh z-grid origin, w.pitch.halfLength. Value [H], uses [P].
    return w.pitch.halfLength;
}

void KickoffPlace_init(World& w) {
    MatchState& m = w.match;
    KickoffPlaceState& kp = m.kickoffPlace;
    // Side byte: h6 init from the kit/side index DAT_00644760 + DAT_006447f8*4 (rec+0x1c8 == -1),
    // then the side-flip compare DAT_00644814/880 gated by DAT_00644800/87c [P]; the port reads the
    // restart-taker's live side (Match_setPhase: m.restartSide = w.teams[team].side).
    kp.sideByte = m.restartSide & 1;
    kp.timer = 0;
    kp.bearing = 0;
    kp.dollyActive = 1;               // rec+0x1ca := 1 (timer C start, FUN_00517e30(0,rec))
    kickoffPlaceSetDir(w, kp);        // rec+0x1cc side-aware direction
}

bool KickoffPlace_step(World& w) {
    MatchState& m = w.match;
    KickoffPlaceState& kp = m.kickoffPlace;
    if (!kp.dollyActive) return false;
    BallState& b = w.ball;
    // Placement timer accumulates the scene tick word (dispatcher writes rec+4 each frame), gated on
    // ball y < 0x80 (h6 DAT_006447c0 < 0x80 [P]); placement windows are ground-level, so it accrues.
    kp.timer += std::max(1, w.time.simFrames);
    // Timeout side-flip compare (h6 0x5170E2..: rec+0x1c4 > 0x27fff -> DAT_00644814/880 vs the saved
    // rec+0x1c6 halfword): a flipped attacking side re-arms direction and spin sign.
    if (kp.timer > kKickoffSideFlipTimeout && (m.restartSide & 1) != kp.sideByte) {
        kp.sideByte = m.restartSide & 1;
        kickoffPlaceSetDir(w, kp);
    }
    const Vec3 centre{0, 0, 0};       // centre spot 0x65F38C
    const OctDist od = octDistance(b.posI, centre);
    if (od.dist <= kKickoffDollySpeed) {
        // Arrival: place the ball on the centre spot ("place ball at the kickoff spot" [P, §5]) and
        // apply spin at the placement-phase tick (h6 phase gate DAT_006447fc == 9 [H]), sign by the
        // attack-side bit: side != 0 -> -DAT_00656e08 [P, sci4 0x517043..0x51704B].
        Ball_placeAt(w, centre);
        const std::int32_t s = kickoffSpinUnits(w);
        b.spin = kp.sideByte != 0 ? -fxi(s) : fxi(s);
        kp.dollyActive = 0;
        kp.timer = 0;                 // h6 arrival path: rec+0x1c4 := 0
        return true;                  // final tick of the placement window
    }
    // Normal dolly = pure centre-tracking (h6 `rec+0x1c4 < 0x40001` branch [P]); the spin-deflected
    // heading belongs to the post-timeout spin-off branch (`timer >= 0x40001` && rec+0x1ca armed,
    // FUN_004a6fa0 spin argument = dir * DAT_00656e08 [P]).
    const bool spinMode = kp.timer >= 0x40001;
    const Angle a = wrapAngle(spinMode ? atan2i(od.dx, od.dz + kp.dir * kickoffSpinUnits(w))
                                       : atan2i(od.dx, od.dz));
    kp.bearing = a;                   // rec+0x1d0 live dolly bearing (livematch pose consumes)
    // Polar per-frame step toward the centre (sin/cos x 960 idiom [P]), clamped to the remainder.
    const std::int32_t step = std::min<std::int32_t>(kKickoffDollySpeed, od.dist);
    b.prevPosI = b.posI;              // presentation camera focus follows the dolly
    b.posI = offsetInDirection(b.posI, step, a);
    b.posI.y = 0;                     // ground placement (ball y < 0x80 gate [P])
    b.pos = {fxi(b.posI.x), 0, fxi(b.posI.z)};
    b.framePos16 = b.pos;
    return true;
}

void Match_resetPhase(World& w) {
    MatchState& m = w.match;
    if (w.opt.gameMode != 1 || m.trainDrill == 2) m.phase = m.restartType = 0;
    w.time.teamOrder = w.time.blink = 0;
    m.f244 = 0;
    m.flags278 = (m.flags278 & 0xffff0000u) | (m.flags278 & 0xfc0a);
    w.time.possessionFrames = w.time.possessorSlowFrames = 0;
    m.f280 = 0;
    m.shootoutKicker = m.player2a0 = m.f294 = -1;
    w.ball.lastKicker = m.scorer = m.ownGoalPlayer = -1;
    w.time.focus = -1;
    m.setPieceAim = w.ball.posI;
    m.setPieceAimMode = m.setPieceAimCtrl = 0;
    m.setPieceTaker = m.setPiecePartner = -1;
    m.setPieceKind = 0;
    m.setPieceMode = 1;
    if (!(w.opt.gameMode == 1 && m.trainDrill != 2)) {
        // §7 delta-3: kickoff placement dolly toward the centre spot (0x65F38C), side-aware offset,
        // spin at the placement-phase tick; replaces the direct-set placement for kick restarts.
        KickoffPlace_init(w);
    }
    // FUN_004BBB3C (AI timers; DAT_00673210 lives in FrameTime)
    m.ai6731f4 = 0;
    const std::int32_t r = static_cast<std::int32_t>(w.rng.next() & 3);
    m.ai673218 = m.ai673214 = 0;
    w.time.timer673210 = 0;
    m.ai67320c = m.ai67321c = 0;
    m.ai673208 = r;
}

void Match_setPhase(World& w, int phase, int team) {
    MatchState& m = w.match;
    m.prevPhase = m.phase;
    m.phase = phase;
    if (static_cast<u32>(team) >= 2) team = 0;
    m.phaseTeam = team;
    m.restartSide = w.teams[team].side;
    if (phase == 9) {
        Team_setPlayersFlag80(w, 0, 0);
        Team_setPlayersFlag80(w, 1, 0);
    } else if (phase == 1) {
        m.player2a0 = -1;
    }
    if (w.teams[0].playerCount != 0) Team_onPhaseChange(w, 0);
    if (w.teams[1].playerCount != 0) Team_onPhaseChange(w, 1);
    if (w.opt.gameMode == 1) untraced_Training_onPhase_46DB28(w);
}

void Match_selectEventDispatcher(World& w, int periodType) { // 0x46F630
    MatchState& m = w.match;
    MatchSeq_reset(w); // 0x46F2EC
    const Vec3 entry{w.pitch.entryPos[0][0], w.pitch.entryPos[0][1], w.pitch.entryPos[0][2]}; // 0x65F3B0
    if (w.opt.gameMode == 1) {
        m.rulesMode = 1;
        m.dispatcher = 1;
        Motion_setTarget(w, kReferee, Vec3{0x2710, 0, 0x2710}); // 0x46F6CE
    } else if (w.opt.presentationMode == 1) {
        m.dispatcher = 3;
        m.rulesMode = 3;
        Motion_setTarget(w, kReferee, entry); // 0x46F6F5
    } else if (periodType == 3) {
        Referee_onPhase_470C80(w, 6);
        m.rulesMode = 2;
        m.dispatcher = 2;
    } else {
        Referee_onPhase_470C80(w, 0);
        m.dispatcher = 0;
        m.rulesMode = 0;
        if (m.periodIndex == 0) Motion_setTarget(w, kReferee, entry); // 0x46F72A
    }
    const Vec3 t = Motion_targetVec(w, kReferee); // 0x46F696: warp to [M+0x4C]
    Motion_warpTo(w, kReferee, &t, nullptr);
}

void Match_dispatchEvent(World& w, int event, int team) {
    switch (w.match.dispatcher) {
    case 1: Match_onTrainingEvent(w, event, team); break;
    case 2: Match_onShootoutEvent(w, event, team); break;
    case 3: Match_onPresentationEvent(w, event, team); break;
    default: Match_onEvent(w, event, team); break;
    }
}

int Match_periodResult(const World& w) {
    // MatchLoop_runPeriod 0x41B480 sets DAT_0057C604 after the presentation layer (0x43CD47 /
    // 0x43D015) ended the period loop, which happens once the end-of-period sequence 9 set status
    // 0x20 (Sim_advance stops). 0x10 -> 2, 8 -> 1, neither -> 0, which Match_run treats as the end.
    const u32 s = w.match.status;
    if (!(s & 0x20)) return 0;
    if (s & 0x10) return 2;
    if (s & 8) return 1;
    return 2;
}

void Match_frameFlags(World& w) {
    // Sim_frame 0x45529A: if (DAT_0065F278 & 8) { FUN_0045564C(DAT_0065EEF8); DAT_0065F278 &= ~8; }
    if (!(w.match.flags278 & 8)) return;
    Match_handicap(w, w.opt.skillLevel);
    w.match.flags278 &= ~8u;
}

void Match_handicap(World& w, int level) {
    MatchState& m = w.match;
    if (w.opt.handicapMode == 1) {
        const std::int32_t a = PlayerAttr_teamRating(w, 0), b = PlayerAttr_teamRating(w, 1);
        if (a < 10 && b < 10) {
            m.stats[0].handicap = 10 - a;
            m.stats[1].handicap = 10 - b;
        } else if (a - b < 0) {
            m.stats[1].handicap = 0;
            m.stats[0].handicap = b - a;
        } else {
            m.stats[1].handicap = a - b;
            m.stats[0].handicap = 0;
        }
    } else {
        m.stats[0].handicap = m.stats[1].handicap = 0;
    }
    PlayerAttr_applyHandicap(w, 0, 1, (w.teams[0].flags >> 10) & 1, level);
    PlayerAttr_applyHandicap(w, 1, 1, (w.teams[1].flags >> 10) & 1, level);
    Team_setControlLevels(w, 0);
    Team_setControlLevels(w, 1);
}

// ---------------------------------------------------------------------------------------------
// Per-frame rules

void Match_updateRules(World& w) {
    MatchState& m = w.match;
    // Script_tick 0x463DD4 (runs earlier in Sim_frame): a cut-scene request without a script
    // (state 4) returns to idle on the next tick.
    if (m.scriptState & 4) m.scriptState = 1;
    w.pl.current = -1; // DAT_0065F2A4 = 0
    if (m.rulesMode == 0) {
        MatchClock_update(w);
        if (m.seqId != 5 && m.phase == 9 && (w.ball.flags & 1)) Rules_checkBallOut(w);
        if (m.phase == 9) Referee_onPhase_470C80(w, 9); // 0x46F979
        if (m.phase == 9 && (w.opt.funModes & 0x10) && m.possessionTimer == 0) {
            m.possessionTimer = static_cast<std::int32_t>((w.rng.next() & 0xf) + 10);
            int t = -1;
            if (w.ball.possessor >= 0) {
                t = teamOf(w, w.ball.possessor);
                Ball_releaseFromPossessor(w, w.ball.possessor);
                Team_resetAnimFlag1(w, t, 0x10, 0xf); // FUN_004532D0
            } else if (w.ball.prevPossessor >= 0) {
                t = teamOf(w, w.ball.prevPossessor);
                Team_resetAnimFlag1(w, t, 0x10, 0xf);
            }
            if (t >= 0) {
                Team_setPlayersState(w, t, 0, 0xff, 0xff);
                Team_holdPlayers_451C2C(w, t, 0x3c, -1, 0); // FUN_00451C2C(team, 0x3C, ECX 0 = no player, EBX 0)
            }
        }
    } else if (m.rulesMode == 1) {
        MatchClock_updateAlt(w);
    } else if (m.rulesMode == 2) {
        if (m.seqId != 5 && m.phase == 9 && (w.ball.flags & 1)) Rules_checkBallOut(w);
    }
    // 0x46F8AB: the referee walks (option bit 0x10, set by the match loader 0x41085F) or warps to
    // its target; then its request flags are rebuilt (bit 0 outside play phases 9/10).
    {
        PlayerState& r = w.players[kReferee];
        if (!(w.opt.rules & 0x10)) {
            const Vec3 t = Motion_targetVec(w, kReferee);
            Motion_warpTo(w, kReferee, &t, nullptr);
        } else {
            Player_frameMove(w, kReferee, m.seqE4, m.seqE8, 0);
        }
        r.moveReq = 0;
        if (m.phase != 9 && m.phase != 10) r.moveReq |= 1;
        r.anim.req = static_cast<std::uint32_t>(static_cast<std::int32_t>(r.moveReq) >> 16);
    }
    // §7 delta-3: the kickoff placement dolly (h6 FUN_00516f40) advances every frame during the
    // restart window, before the sequence pump consumes the step.
    KickoffPlace_step(w);
    if (m.seqId != 0) {
        m.seqFrames += w.time.simFrames;
        MatchSeq_run(w, m.seqId);
    }
    // 0x46F92D: script-attached ball (DAT_0066762F & 0x10) is a cut-scene effect, not modelled.
}

void MatchClock_update(World& w) {
    MatchState& m = w.match;
    if ((m.status & 8) || (m.status & 2) || ((m.status & 1) && w.opt.clockMode == 1)) return;
    const std::int32_t cf = w.time.clockFrames;
    m.clockFrames += cf;
    if (static_cast<u32>(m.clockFrames) >= 60) {
        m.clockFrames -= 60;
        ++m.totalSec;
        ++m.elapsedSec;
        if ((w.opt.funModes & 0x10) && m.phase == 9) --m.possessionTimer;
    }
    if (m.status & 1) {
        if (static_cast<u32>(m.deadFrames) < 600) m.deadFrames += cf;
        else m.stoppageFrames += cf;
        m.injurySec = w.opt.gameMode == 6 ? 0 : static_cast<std::int32_t>(static_cast<u32>(m.stoppageFrames) / 60);
        if (static_cast<u32>(m.durationSec) >> 2 < static_cast<u32>(m.injurySec))
            m.injurySec = static_cast<std::int32_t>(static_cast<u32>(m.durationSec) >> 2);
    } else {
        m.deadFrames = 0;
    }
    if (m.elapsedSec == m.durationSec && m.injurySec != 0 && !(m.status & 4)) m.status |= 4; // + added-time board
    if (static_cast<u32>(m.durationSec + m.injurySec) > static_cast<u32>(m.elapsedSec)) return;
    if (m.phase != 9) return;
    if (w.opt.clockMode != 1 && MatchClock_canEndPeriod(w)) return;
    if (m.status & 8) return;
    m.status |= 8;
    if (w.opt.gameMode == 6) {
        Match_decidePeriodOutcome(w);
    } else if (m.periodType == 0 || m.periodType == 2) {
        Match_requestPeriodEnd(w, 1, 0xe, 2);
    } else {
        Match_decidePeriodOutcome(w);
    }
    Match_dispatchEvent(w, 9, 0);
}

void MatchClock_updateAlt(World& w) {
    MatchState& m = w.match;
    if (!(m.status & 0xb)) {
        m.clockFrames += w.time.clockFrames;
        if (static_cast<u32>(m.clockFrames) >= 60) {
            m.clockFrames -= 60;
            ++m.totalSec;
            ++m.elapsedSec;
        }
        if (static_cast<u32>(m.durationSec + m.injurySec) <= static_cast<u32>(m.elapsedSec)) Match_dispatchEvent(w, 9, 0);
    }
    if (m.phase != 9) return;
    if (m.trainDrill != 2 && w.ball.posI.z < 0) {
        m.trainResult = 5;
        Match_dispatchEvent(w, 0xe, m.phaseTeam);
        return;
    }
    if (w.ball.flags & 1) Rules_checkBallOut(w);
}

// FUN_0046FAAC(EAX = team): a player of the team is in an unfinished action (or the foul sequence
// runs).
static int teamActionPending(const World& w, int t) {
    if (w.match.seqId == 5) return 1;
    const TeamState& T = w.teams[t];
    for (int i = 0; i < T.playerCount; ++i) {
        const int p = T.players[i];
        const std::int32_t s = w.players[p].state;
        if (Player_isState100Kind1(w, p) || Player_isState100Kind0(w, p) || Player_isState100Kind2(w, p)) return 1;
        if (s == 0x82 || s == 0x80 || s == 0x81 || s == 0x83 || s == 0x84 || s == 0x85 || s == 0x18f) return 1;
    }
    return 0;
}

int MatchClock_canEndPeriod(World& w) {
    const MatchState& m = w.match;
    const BallState& b = w.ball;
    if (m.phase != 9 || (b.flags & 4)) return 1;
    if (Ball_markerInForecast(w, 0) || Ball_markerInForecast(w, 2) || Ball_markerInForecast(w, 1)) return 1;
    if (teamActionPending(w, 0) || teamActionPending(w, 1)) return 1;
    const u32 limit = static_cast<u32>(m.durationSec + m.injurySec);
    const int p = b.possessor;
    if (p >= 0) {
        const TeamState& T = w.teams[teamOf(w, p)];
        const int flag = w.players[p].posI.z + w.pitch.thirdLineZ[T.side] >= 0 ? 1 : 0;
        if (flag != T.side) {
            if (static_cast<u32>(w.time.possessorSlowFrames) > 300) return 0;
            if (static_cast<u32>(w.time.possessionFrames) > 600) return 0;
            return limit + 30 > static_cast<u32>(m.elapsedSec) ? 1 : 0;
        }
        if (inPenaltyBox(w, b.posI) && limit + 10 > static_cast<u32>(m.elapsedSec)) return 1;
        return static_cast<u32>(w.time.possessionFrames) < 60 ? 1 : 0;
    }
    if (iabs(b.posI.z) > w.pitch.thirdLineZ[1]) return 1;
    return iabs(b.landing.z) > w.pitch.thirdLineZ[1] ? 1 : 0;
}

std::int32_t MatchClock_toGameSeconds(World& w, std::int32_t sec) {
    MatchState& m = w.match;
    if (m.durationSec == 0) return 0;
    const u32 t = static_cast<u32>(sec * 60 + m.clockFrames);
    const u32 dur = static_cast<u32>(m.durationSec);
    std::int32_t r;
    if (m.periodType == 2) {
        r = static_cast<std::int32_t>(t * 15 / dur);
    } else {
        r = static_cast<std::int32_t>(t * 45 / dur);
        if (m.periodType == 1) r += 2700;
    }
    m.displayOverflow = 0;
    switch (m.periodIndex) {
    case 0:
        if (r >= 2700) { m.displayOverflow = r - 2700; r = 2700; }
        break;
    case 1:
        if (r >= 5400) { m.displayOverflow = r - 5400; r = 5400; }
        break;
    case 2:
        if (r < 900) r += 5400;
        else { m.displayOverflow = r - 900; r = 6300; }
        break;
    case 3:
        if (r < 900) r += 6300;
        else { m.displayOverflow = r - 900; r = 7200; }
        break;
    default: break;
    }
    return r;
}

// ---------------------------------------------------------------------------------------------
// Period outcome

void Match_requestPeriodEnd(World& w, int whistle, int next, int team) {
    w.match.endWhistle = whistle;
    w.match.nextPhase = next;
    w.match.endTeam = team;
    if (next != 0xe) w.match.status |= 0x10;
}

int Match_decideByTargets(int a, int b, const World& w) {
    const auto tgt = [&](int t, int k) { return w.opt.winTarget[t][k]; };
    const u32 ua = static_cast<u32>(a), ub = static_cast<u32>(b);
    if (a == b) {
        if (ub >= tgt(0, 1)) return 0;
        if (ub < tgt(1, 1)) return 2;
        return 1;
    }
    if (a > b) {
        if (ua >= tgt(0, 2)) return 0;
        if (ub < tgt(1, 0)) return 2;
        return 1;
    }
    if (ub >= tgt(1, 2)) return 1;
    if (ua < tgt(0, 0)) return 2;
    return 0;
}

void Match_decidePeriodOutcome(World& w) {
    MatchState& m = w.match;
    if (w.opt.tieBreak == 4) {
        int a = m.score[0] + w.opt.firstLegGoals[0], b = m.score[1] + w.opt.firstLegGoals[1];
        if (a == b) {
            a = w.opt.firstLegGoals[0];
            b = m.score[1];
        }
        const int r = Match_decideByTargets(a, b, w);
        if (r != 2) return Match_requestPeriodEnd(w, 2, 0x11, r);
        if (a == b) {
            if (Team_tooFewPlayers(w, 0) || Team_tooFewPlayers(w, 1))
                return Match_requestPeriodEnd(w, 2, 0x10, static_cast<u32>(m.score[0]) <= static_cast<u32>(m.score[1]) ? 1 : 0);
            return Match_requestPeriodEnd(w, 1, 0xe, 2);
        }
        return Match_requestPeriodEnd(w, 2, 0x10, a <= b ? 1 : 0);
    }
    const int r = Match_decideByTargets(m.score[0], m.score[1], w);
    if (r != 2) return Match_requestPeriodEnd(w, 2, 0x11, r);
    if (m.periodType == 3)
        return Match_requestPeriodEnd(w, 2, 0x10, static_cast<u32>(m.so.goals[0]) > static_cast<u32>(m.so.goals[1]) ? 0 : 1);
    if (m.score[0] != m.score[1])
        return Match_requestPeriodEnd(w, 2, 0x10, static_cast<u32>(m.score[0]) > static_cast<u32>(m.score[1]) ? 0 : 1);
    if (w.opt.tieBreak == 0 || w.opt.gameMode == 6) return Match_requestPeriodEnd(w, 2, 0xf, 2);
    Match_requestPeriodEnd(w, 1, 0xe, 2);
}

void Match_endPeriodSuddenDeath(World& w, int winner) {
    w.match.restartType = 8;
    const int r = decideSuddenDeath(w);
    if (r == 0 || r == 1) Match_requestPeriodEnd(w, 2, 0x11, r);
    else Match_requestPeriodEnd(w, 0, 0x10, winner);
    MatchSeq_push(w, 9);
}

int Team_tooFewPlayers(const World& w, int t) {
    if (w.opt.gameMode == 1) return 0;
    return static_cast<u32>(w.teams[t].playerCount) < 7 ? 1 : 0;
}

int Match_checkAbandon(World& w) {
    MatchState& m = w.match;
    if (m.status & 0x10) return 0;
    if (m.clockFrames + m.totalSec != 0) return 0;
    if (!Team_tooFewPlayers(w, 0) && !Team_tooFewPlayers(w, 1)) return 0;
    const u32 a = static_cast<u32>(w.teams[0].playerCount), b = static_cast<u32>(w.teams[1].playerCount);
    int loser;
    if (a < b) loser = 0;
    else if (a > b) loser = 1;
    else loser = (w.rng.next() & 1) ? 0 : 1;
    abandonMatch(w, loser);
    Match_decidePeriodOutcome(w);
    Match_dispatchEvent(w, 9, 0);
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Event dispatchers

void Match_onEvent(World& w, int event, int team) {
    MatchState& m = w.match;
    MatchSeq_clear(w);
    m.phaseTeam = team;
    int runNow = 0;
    switch (event) {
    case 0: // kick-off
        m.status |= 2;
        m.restartType = 0;
        CutScene_play(w, 0xc);
        Match_setPhase(w, 0xc, m.phaseTeam);
        MatchSeq_push(w, 3);
        MatchSeq_push(w, 4);
        runNow = 1;
        w.emit(EventType::KickOff, team);
        break;
    case 12: // kick-off without cut-scene
        m.restartType = 0;
        m.status |= 1;
        MatchSeq_push(w, 4);
        runNow = 1;
        w.emit(EventType::KickOff, team);
        break;
    case 1:
        m.restartType = 1;
        m.status |= 1;
        MatchSeq_push(w, 4);
        runNow = 1;
        w.emit(EventType::ThrowIn, team);
        break;
    case 2:
        m.status |= 1;
        ++m.stats[team].corners;
        m.restartType = 2;
        MatchSeq_push(w, 4);
        runNow = 1;
        w.emit(EventType::Corner, team);
        break;
    case 3:
        m.status |= 1;
        m.restartType = 3;
        MatchSeq_push(w, 4);
        runNow = 1;
        w.emit(EventType::GoalKick, team);
        break;
    case 4:
        Team_onPhaseChange(w, 0);
        Team_onPhaseChange(w, 1);
        break;
    case 5:
        Team_setPlayersState(w, team, 0, 0xff, 0xff);
        Team_setPlayersState(w, oppOf(w, team), 0, 0xff, 0xff);
        break;
    case 6: { // goal
        m.status |= 1;
        const bool og = m.ownGoalPlayer >= 0;
        m.phaseTeam = og ? teamOf(w, m.ownGoalPlayer) : oppOf(w, teamOf(w, m.scorer));
        const std::int32_t time = cappedTotal(w);
        const int pen = (m.prevPhase == 4 && m.scorer == m.penaltyTaker) ? 1 : 0;
        const int st = teamOf(w, m.scorer);
        recordGoal(w, st, w.players[m.scorer].index, og ? 1 : 0, pen, time);
        if (!og) Team_afterGoal(w, st);
        // FUN_0045299C(team phaseTeam) + FUN_004D7FE8: goal commentary (presentation).
        (void)findKeeper(w, m.phaseTeam);
        if (!og) ++m.stats[st].goals;
        m.flags278 &= ~0x200u;
        w.emit(og ? EventType::OwnGoal : EventType::Goal, st, m.scorer, MatchClock_toGameSeconds(w, m.elapsedSec), pen);
        const int winner = static_cast<u32>(m.score[0]) > static_cast<u32>(m.score[1]) ? 0 : 1;
        if (m.periodType == 2) {
            Match_endPeriodSuddenDeath(w, winner);
        } else if (m.periodType == 4 && static_cast<u32>(m.score[team]) >= static_cast<u32>(w.opt.goldenGoalTarget)) {
            Match_endPeriodSuddenDeath(w, winner);
        } else {
            m.restartType = 0;
            Match_lineUpRestart(w, 0);
            MatchSeq_push(w, 7);
            MatchSeq_push(w, 4);
            runNow = 1;
        }
        break;
    }
    case 7: // foul
        if (!Rules_resolveFoul(w)) return;
        m.status |= 1;
        m.phaseTeam = oppOf(w, teamOf(w, m.offender));
        m.restartType = m.foulInBox ? 4 : 5;
        MatchSeq_push(w, 5);
        runNow = 1;
        w.emit(EventType::Foul, teamOf(w, m.offender), m.offender, m.foulClass, m.victim);
        w.emit(m.foulInBox ? EventType::Penalty : EventType::FreeKick, m.phaseTeam);
        break;
    case 8: // offside
        if (m.phase == 9) {
            const int p = w.ball.possessor;
            if (p >= 0 && (w.ball.flags & 0x20) && w.players[p].slot == 0) return;
            if (w.ball.flags & 0x20) return;
        }
        if (m.phase != 9 && m.phase != 5) return;
        if (!(w.opt.rules & 2)) break;
        m.phaseTeam = oppOf(w, teamOf(w, m.offender));
        m.status |= 1;
        m.restartType = 5;
        countOffside(w);
        MatchSeq_push(w, 6);
        runNow = 1;
        w.emit(EventType::Offside, teamOf(w, m.offender), m.offender);
        break;
    case 9: // time up
        m.status |= 1;
        m.flags278 |= 0x20;
        m.restartType = 8;
        if (m.endWhistle == 1) MatchSeq_push(w, 1);
        else if (m.endWhistle == 2) MatchSeq_push(w, 2);
        MatchSeq_push(w, 9);
        Team_resetPlayersPeriod(w, 0, 0);
        Team_resetPlayersPeriod(w, 1, 0);
        runNow = 1;
        break;
    case 11: // play resumes
        w.time.focus = -1;
        m.status &= ~3u;
        m.flags278 &= ~2u;
        w.ball.flags &= ~1u;
        Match_setPhase(w, 9, team);
        Team_resetPlayersPeriod(w, 0, 1);
        Team_resetPlayersPeriod(w, 1, 1);
        break;
    case 13:
        m.restartType = 5;
        m.status |= 1;
        MatchSeq_push(w, 0xd);
        MatchSeq_push(w, 4);
        runNow = 1;
        break;
    case 15:
        m.restartType = 5;
        m.status |= 1;
        MatchSeq_push(w, 0xe);
        MatchSeq_push(w, 4);
        runNow = 1;
        break;
    case 16: // goal kick after a near miss
        m.status |= 1;
        m.restartType = 3;
        MatchSeq_push(w, 0xf);
        MatchSeq_push(w, 4);
        runNow = 1;
        w.emit(EventType::GoalKick, team);
        break;
    default: break; // 10, 14, > 16
    }
    Team_lateGameFormation(w, 0);
    Team_lateGameFormation(w, 1);
    MatchSeq_startNext(w, runNow);
}

void Match_onShootoutEvent(World& w, int event, int team) {
    MatchState& m = w.match;
    ShootoutState& s = m.so;
    if (event == 7) return;
    MatchSeq_clear(w);
    m.phaseTeam = team;
    switch (event) {
    case 0: {
        s = ShootoutState{};
        buildKickOrder(w, 0);
        buildKickOrder(w, 1);
        s.remaining[1] = 5;
        s.flags = (s.flags & ~1u) | static_cast<u32>(team & 1);
        s.remaining[0] = 5;
        s.first = team;
        const int cur = s.flags & 1;
        s.kicker[cur] = kickerFor(w, cur, 0);
        m.shootoutKicker = w.teams[cur].players[s.kicker[cur]];
        s.result[cur][s.round] = 3;
        m.flags278 &= ~0x20000u;
        m.restartType = 6;
        m.restartSide = w.teams[team].side;
        Match_lineUpRestart(w, 6);
        Ball_placeAt(w, w.ball.restartPos);
        w.time.focus = -1;
        const int other = team == 0 ? 1 : 0;
        const int k = findKeeper(w, other);
        if (w.teams[other].f1e4 >= 0 && k >= 0) Player_reset(w, k, nullptr, 6, false); // 0x4BBA2C(EDX 0, EBX 6)
        CutScene_play(w, 0xc);
        Match_setPhase(w, 0xc, m.phaseTeam);
        MatchSeq_push(w, 3);
        MatchSeq_push(w, 4);
        MatchSeq_startNext(w, 0);
        return;
    }
    case 1: case 2: case 3: case 4: case 12: case 16: { // kick missed / saved / timed out
        m.flags278 &= ~0x40000u;
        m.flags278 |= ((s.flags & 1) ? 0u : 1u) << 18;
        const int kicker = m.shootoutKicker;
        const int r = recordShootoutKick(w, 0);
        m.phaseTeam = s.flags & 1;
        m.flags278 &= ~0x20000u;
        w.emit(EventType::ShootoutMiss, kicker >= 0 ? teamOf(w, kicker) : -1, kicker, s.goals[0], s.goals[1]);
        if (r != 2) {
            Match_endPeriodSuddenDeath(w, r);
            MatchSeq_startNext(w, 0);
            return;
        }
        m.restartType = 6;
        Match_lineUpRestart(w, 6);
        MatchSeq_push(w, 8);
        MatchSeq_push(w, 4);
        MatchSeq_startNext(w, 0);
        return;
    }
    case 6: { // goal
        m.flags278 &= ~0x40000u;
        m.flags278 |= static_cast<u32>(team & 1) << 18;
        const int kicker = m.shootoutKicker;
        const int r = recordShootoutKick(w, 1);
        m.flags278 |= 0x20000u;
        m.phaseTeam = s.flags & 1;
        w.emit(EventType::ShootoutGoal, kicker >= 0 ? teamOf(w, kicker) : -1, kicker, s.goals[0], s.goals[1]);
        if (r == 2) {
            // FUN_0045299C + FUN_004D7FE8 (commentary) when not training.
            if (w.opt.gameMode != 1) (void)findKeeper(w, m.phaseTeam);
            m.restartType = 6;
            Match_lineUpRestart(w, 6);
            MatchSeq_push(w, 7);
            MatchSeq_push(w, 4);
            MatchSeq_startNext(w, 0);
            return;
        }
        Match_endPeriodSuddenDeath(w, r);
        MatchSeq_startNext(w, 0);
        return;
    }
    case 11: // kick struck
        m.seqFrames = 0;
        m.flags278 &= ~2u;
        m.status &= ~3u;
        w.ball.flags &= ~1u;
        w.time.focus = -1;
        Match_setPhase(w, 9, team);
        w.emit(EventType::ShootoutKick, team, m.shootoutKicker);
        MatchSeq_push(w, 0xc);
        MatchSeq_startNext(w, 0);
        return;
    default:
        MatchSeq_startNext(w, 0);
        return;
    }
}

void Match_onTrainingEvent(World& w, int event, int team) {
    MatchState& m = w.match;
    if (m.trainBusy != 0 || event == 8 || event == 7) return;
    MatchSeq_clear(w);
    m.phaseTeam = team;
    switch (event) {
    case 0:
        untraced_Training_reset_46D704(w);
        switch (m.trainDrill) {
        case 0: m.restartType = 2; MatchSeq_push(w, 0xa); break;
        case 1: m.restartType = 5; MatchSeq_push(w, 0xa); break;
        case 2: m.restartType = m.trainBusy; break;
        case 3: m.restartType = 4; break;
        default: break;
        }
        MatchSeq_push(w, 4);
        break;
    case 1: case 2: case 3: case 16:
        m.trainResult = m.trainDrill == 3 ? 3 : (team != 0 ? 0 : 5);
        m.trainBusy = 1;
        MatchSeq_push(w, 0xb);
        break;
    case 4:
        if (m.trainDrill == 3) {
            m.trainResult = 3;
            m.trainBusy = 1;
            MatchSeq_push(w, 0xb);
        } else {
            Team_onPhaseChange(w, 0);
            Team_onPhaseChange(w, 1);
        }
        break;
    case 6: {
        const int t = m.ownGoalPlayer >= 0 ? teamOf(w, m.ownGoalPlayer) : teamOf(w, m.scorer);
        if (m.ownGoalPlayer >= 0) m.trainResult = t != 0 ? 1 : 2;
        else m.trainResult = t != 0 ? 2 : 1;
        m.trainBusy = 1;
        m.trainGoal = 1;
        MatchSeq_push(w, 0xb);
        break;
    }
    case 9:
        m.trainResult = 4;
        m.trainBusy = 1;
        MatchSeq_push(w, 0xb);
        break;
    case 14:
        m.trainBusy = 1;
        MatchSeq_push(w, 0xb);
        break;
    case 11:
        m.seqFrames = 0;
        m.flags278 &= ~2u;
        m.status &= ~3u;
        w.time.focus = -1;
        w.ball.flags &= ~1u;
        Match_setPhase(w, 9, team);
        Team_resetPlayersPeriod(w, 0, 1);
        Team_resetPlayersPeriod(w, 1, 1);
        if (m.trainDrill == 3) MatchSeq_push(w, 0xc);
        break;
    default: break;
    }
    MatchSeq_startNext(w, 0);
}

void Match_onPresentationEvent(World& w, int event, int team) {
    MatchState& m = w.match;
    MatchSeq_clear(w);
    m.phaseTeam = team;
    if (event != 0) {
        MatchSeq_startNext(w, 0);
        return;
    }
    m.restartType = 0xe;
    Match_setPhase(w, 0x15, team);
    CutScene_play(w, 0x15);
    MatchSeq_push(w, 0x10);
    MatchSeq_startNext(w, 1);
}

// ---------------------------------------------------------------------------------------------
// Restart spot FUN_0046F3EC (EAX = restart type): sets the ball restart position 0x66919C and the
// free-kick ball zone 0x6691C4.

void Match_lineUpRestart(World& w, int type) {
    const Pitch& P = w.pitch;
    const MatchState& m = w.match;
    Vec3 src = w.ball.posAtTouch;
    src.y = 0;
    Vec3 p = src;
    auto spot = [&](int side) { return Vec3{0, 0, P.penSpot[side]}; }; // 0x65F398 + side*12
    if (type == 0) {
        p = Vec3{0, 0, 0};
    } else if (type == 1) {
        p.x += p.x > 0 ? 0x20 : -0x20;
    } else if (type == 2) {
        p = Vec3{src.x < 0 ? -(P.halfWidth - 0x10) : P.halfWidth - 0x10, 0,
                 src.z < 0 ? -(P.halfLength - 0x1c) : P.halfLength - 0x1c}; // FUN_00455D5C
    } else if (type == 3 || type == 0x14) {
        p = Vec3{src.x < 0 ? -(P.goalAreaHalfWidth - 0x10) : P.goalAreaHalfWidth - 0x10, 0,
                 src.z < 0 ? -(P.halfLength - P.goalAreaDepth + 0x20) : P.halfLength - P.goalAreaDepth + 0x20}; // 0x455D18
    } else if (type == 4) {
        p = spot(m.restartSide == 0 ? 1 : 0);
    } else if (type == 5) {
        p = m.foulSpot;
        const std::int32_t lx = P.halfWidth - 0x50, lz = P.halfLength - 0x50;
        if (-lx > p.x) p.x = -lx;
        else if (lx < p.x) p.x = lx;
        if (-lz > p.z) p.z = -lz;
        else if (lz < p.z) p.z = lz;
        if (iabs(p.x) < P.goalAreaHalfWidth) {
            const std::int32_t gz = P.halfLength - 0x120;
            if (-gz > p.z) p.z = -gz;
            else if (gz < p.z) p.z = gz;
        }
    } else if (type == 6) {
        p = spot(m.restartSide);
    }
    const std::int32_t mx = P.halfWidth + 0x40, mz = P.halfLength + 0x40;
    if (-mx > p.x) p.x = -mx;
    else if (mx < p.x) p.x = mx;
    if (-mz > p.z) p.z = -mz;
    else if (mz < p.z) p.z = mz;
    w.ball.restartPos = p;
    Formation_ballZone(w, Vec3{fxi(p.x), fxi(p.y), fxi(p.z)}, w.ball.fkZone, 0);
}

// ---------------------------------------------------------------------------------------------
// Ball out of play / goal

void Rules_checkBallOut(World& w) {
    MatchState& m = w.match;
    BallState& b = w.ball;
    int c = b.possessor >= 0 ? b.possessor : (b.prevPossessor >= 0 ? b.prevPossessor : b.lastToucher);
    if (c < 0) return; // exe dereferences a null player here (unreachable in play)
    if (static_cast<u32>(iabs(b.posAtTouch.x)) >= static_cast<u32>(w.pitch.halfWidth + b.radius)) {
        if (m.phase == 9) Match_dispatchEvent(w, 1, oppOf(w, teamOf(w, c)));
        return;
    }
    const bool fromThrow = m.restartTaker >= 0 && m.prevPhase == 1 && c == m.restartTaker;
    if ((b.flags & 2) && !fromThrow) {
        // Goal: esi = scoring team.
        const int st = m.periodType == 3 ? static_cast<int>(m.so.flags & 1) : teamAttackingZ(w, b.posI.z);
        m.scorer = m.ownGoalPlayer = -1;
        const int lt = b.lastToucher, ps = b.possessor;
        if (lt >= 0 && teamOf(w, lt) == st) m.scorer = lt;
        if (ps >= 0 && teamOf(w, ps) == st) m.scorer = ps;
        if (lt >= 0 && teamOf(w, lt) != st && w.players[lt].slot == 0 && b.kickResult == 1) m.scorer = b.kickerByTeam[st];
        if (ps >= 0 && teamOf(w, ps) != st) {
            if (w.players[ps].slot != 0 || b.kickResult == 1) {
                if (w.players[ps].slot != 0) m.ownGoalPlayer = m.scorer = ps;
            } else {
                m.ownGoalPlayer = m.scorer = ps;
            }
        }
        if (m.scorer < 0) {
            m.scorer = b.kickerByTeam[st];
            const int def = st == 0 ? 1 : 0;
            if (m.scorer < 0) {
                m.scorer = nearestPlayer(w, b.posAtTouch, def, 0, nullptr);
                if (m.scorer < 0 && w.opt.gameMode != 1) m.scorer = findKeeper(w, def);
            }
            if (lt >= 0 && teamOf(w, lt) != st) m.ownGoalPlayer = lt;
            if (ps >= 0 && teamOf(w, ps) != st) m.ownGoalPlayer = ps;
            if (m.ownGoalPlayer < 0) m.ownGoalPlayer = nearestPlayer(w, b.posAtTouch, def, 0, nullptr);
            m.scorer = m.ownGoalPlayer;
        }
        int edi = st;
        if (m.ownGoalPlayer >= 0) edi = oppOf(w, teamOf(w, m.ownGoalPlayer));
        else if (m.scorer >= 0) edi = teamOf(w, m.scorer);
        m.replayWanted[edi] = 1;
        m.replayWanted[edi == 0 ? 1 : 0] = w.teams[edi].humans != 0 ? 0 : 1;
        Match_dispatchEvent(w, 6, st);
        return;
    }
    if (m.phase != 9) return;
    if (!Ball_markerInForecast(w, 0) && m.periodType != 3 && iabs(b.posAtTouch.x) <= w.pitch.penAreaHalfWidth) {
        // shot wide: credit the attacking team
        int def;
        if (b.posI.z > 0) def = w.teams[0].side == 1 ? 0 : 1;
        else def = w.teams[0].side == 0 ? 0 : 1;
        int shooter = -1;
        bool skip = false;
        if (b.lastToucher >= 0) {
            if (teamOf(w, b.lastToucher) != def) shooter = b.lastToucher;
        } else if (b.possessor >= 0) {
            skip = true;
        } else if (b.prevPossessor >= 0 && teamOf(w, b.prevPossessor) != def) {
            shooter = b.prevPossessor;
        }
        if (!skip && shooter >= 0) ++m.stats[def == 0 ? 1 : 0].shotsWide;
    }
    const int ev = ((b.posI.z >= 0 ? 1 : 0) == w.teams[teamOf(w, c)].side) ? 2 : 3;
    const int toTeam = oppOf(w, teamOf(w, c));
    if (ev == 3 && m.phase != 4) {
        const Vec3 pp = w.players[c].posI;
        if ((pp.z >= 0) == (b.posI.z >= 0) && iabs(pp.z) <= w.pitch.penAreaLineZ + 0xf0 &&
            iabs(pp.z) >= w.pitch.penAreaLineZ - 0x120 && iabs(pp.x) <= w.pitch.goalAreaWidth + 0x120) {
            Match_dispatchEvent(w, 0x10, toTeam);
            return;
        }
    }
    Match_dispatchEvent(w, ev, toTeam);
}

// ---------------------------------------------------------------------------------------------
// Fouls and offside

void Rules_registerFoul(World& w, int kind, int offender, int victim, const Vec3* spot, int injury) {
    MatchState& m = w.match;
    if (m.phase == 0xa || m.phase == 0xb) return;
    if (m.seqId == 5) return;
    std::int32_t range = 100;
    m.foulClass = kind;
    m.offender = offender;
    m.victim = victim;
    m.offenderCtrl = offender >= 0 ? w.players[offender].ctrl : 0;
    m.victimCtrl = victim >= 0 ? w.players[victim].ctrl : 0;
    if (injury == 0) {
        m.injury = 0;
    } else {
        const u32 r = w.rng.next() % 100;
        if (r < 40 || w.opt.gameMode == 1 || !(w.opt.rules & 4)) {
            m.injury = 1;
        } else if (r < 70) {
            m.injury = 2;
        } else {
            m.injury = 3;
            m.injuredPlayer = victim;
            if (!(w.attrs[w.players[victim].attr].at(0x40) & 1)) {
                Player_injure(w, victim);
                TeamState& T = w.teams[teamOf(w, victim)];
                ++T.w360[3]; // team +0x36C
                ++T.w360[4]; // team +0x370
            }
        }
    }
    if (spot) m.foulSpot = *spot;
    else if (victim >= 0) m.foulSpot = w.players[victim].posI;
    else if (offender >= 0) m.foulSpot = w.players[offender].posI;
    m.foulSpot.y = 0;
    clampToField(w, m.foulSpot);
    if (m.foulClass == 0) m.foulInBox = 0;
    else m.foulInBox = inPenaltyBox(w, m.foulSpot) && (m.foulSpot.z >= 0 ? 1 : 0) == w.teams[teamOf(w, offender)].side;
    if (m.foulClass == 0 || m.foulClass == 3) return;
    if (victim >= 0 && w.players[victim].slot == 0 && victim == w.teams[teamOf(w, victim)].f1e4 &&
        w.players[offender].slot != 0) {
        if (victim == w.ball.possessor && (w.ball.flags & 0x20)) {
            if (m.phase != 9 || w.opt.gameMode == 1 || w.opt.refereeStrictness != 0) {
                m.foulClass = 4;
                m.cardRange = range;
                return;
            }
            m.foulClass = 6;
            range = 100;
        } else {
            m.foulClass = (w.rng.next() & 3) ? 5 : 4;
            m.cardRange = range;
            return;
        }
    }
    if (victim >= 0 && static_cast<u32>(m.injury) >= 3) range = 50;
    m.cardRange = range;
    if (w.rng.next() % static_cast<u32>(range) >= static_cast<u32>(w.opt.refereeStrictness)) {
        m.foulClass = 0; // not given
        return;
    }
    if (m.foulClass == 6) return;
    const u32 half = static_cast<u32>(w.opt.halfLengthMinutes);
    std::int32_t thr = 0x640;
    if (half < 45) thr = 0x640 - static_cast<std::int32_t>(45 - half) * 31;
    if ((w.rng.next() & 0x7ff) >= static_cast<u32>(thr)) return;
    m.foulClass = 6;
}

int Rules_resolveFoul(World& w) {
    MatchState& m = w.match;
    m.card = 0;
    const int v = m.victim;
    const bool keeperHolding = w.players[v].slot == 0 && w.ball.possessor >= 0 && v == w.ball.possessor &&
                               (w.ball.flags & 0x20);
    if (keeperHolding) {
        if (m.phase != 9 || w.opt.gameMode == 1 || m.offender < 0) return 0;
    } else {
        if (m.phase != 9 || m.offender < 0 || m.victim < 0 || w.opt.gameMode == 1 || w.opt.refereeStrictness == 0 ||
            m.foulClass == 0)
            return 0;
    }
    const int cls = m.foulClass;
    if (cls != 3 && (w.opt.rules & 1) && w.players[m.offender].slot != 0 && w.opt.refereeStrictness != 0) {
        if (cls == 6) {
            countFoul(w);
            return 1;
        }
        int given = 1;
        if (m.cardRange > 0 && w.rng.next() % static_cast<u32>(m.cardRange) >= static_cast<u32>(w.opt.refereeStrictness))
            given = 0;
        switch (cls) {
        case 1: if (given) m.card = 1; break;
        case 2: m.card = given ? 2 : 1; break;
        case 4: m.card = 2; break;
        case 5: m.card = 1; break;
        default: break; // 3 unreachable
        }
        countFoul(w);
    }
    return 1;
}

std::int32_t Rules_isOffsidePosition(const World& w, int pid, int d) {
    if (pid < 0) return 0;
    const PlayerState& p = w.players[pid];
    const int side = w.teams[p.team].side;
    if ((p.posI.z >= 0 ? 1 : 0) == side) return 0;
    std::int32_t line = w.players[d].posI.z;
    if ((line - w.ball.posI.z >= 0 ? 1 : 0) == side) line = w.ball.posI.z;
    const std::int32_t gl = w.pitch.goalLineZ[side];
    const std::int32_t a = iabs(p.posI.z - gl), b2 = iabs(line - gl);
    return a > b2 ? a - b2 : 0;
}

void Rules_checkOffside(World& w, int passer, const std::int32_t pass[3]) {
    if (w.opt.gameMode == 1 || w.time.timer65f1f8 != 0) return;
    if (!(w.opt.rules & 2)) return;
    const TeamState& T = w.teams[teamOf(w, passer)];
    if (T.side == 1 && pass[2] > 0) return;
    if (T.side == 0 && pass[2] < 0) return;
    const int recv = T.f1c0; // team +0x1C0
    const int line = w.teams[T.opponent].f1bc;
    if ((w.players[recv].posI.z >= 0 ? 1 : 0) == w.teams[teamOf(w, recv)].side) return;
    if (recv == passer || recv == w.ball.lastKicker) return;
    const std::int32_t dx = pass[1], dz = pass[2];
    if (iabs(dx) > w.pitch.quarterLength && iabs(dz) < (iabs(dx) >> 2)) {
        const std::int32_t px = w.players[passer].posI.x;
        if (px > 0 && dx > 0) return;
        if (px < 0 && dx < 0) return;
    }
    int target = T.f1e0;
    if (target < 0) {
        Vec3 aim = w.ball.posI;
        aim.x += dx;
        aim.z += dz;
        const std::int32_t d = octLength(w.players[recv].posI.x - aim.x, w.players[recv].posI.z - aim.z);
        const std::int32_t thr = pass[0] - (pass[0] >> 2);
        target = d <= thr ? nearestPlayer(w, aim, teamOf(w, passer), 0, nullptr) : -1;
        if (target == passer) target = -1;
    }
    if ((w.ball.flags & 4) || target < 0) {
        TeamState& PT = w.teams[teamOf(w, passer)];
        for (int i = 0; i < PT.playerCount; ++i) {
            const std::int32_t off = Rules_isOffsidePosition(w, PT.players[i], line);
            if (off) {
                w.players[PT.players[i]].f244 = off;
                PT.flags |= 0x80;
            }
        }
        return;
    }
    if (w.players[target].anim.flags() & 4) return; // player byte +0x9B (anim +0x37) bit 2
    const std::int32_t off = Rules_isOffsidePosition(w, target, line);
    if (off <= 0x20) {
        if (off != 0) (void)w.rng.next();
        return;
    }
    const std::int32_t s = w.players[recv].state;
    if (s == 0x183 || s == 0x182 || s == 0x188 || s == 0x201) return;
    Rules_registerFoul(w, 3, recv, -1, &w.players[recv].posI, 0);
    Match_dispatchEvent(w, 8, oppOf(w, teamOf(w, recv)));
}

int Match_seqIsFoul(const World& w) { return w.match.seqId == 5 ? 1 : 0; }
int Team_nearestPlayer_4523E4(const World& w, const Vec3& pos, int team, int excludeSlot, std::int32_t* outDist) {
    return nearestPlayer(w, pos, team, excludeSlot, outDist);
}
int Team_findKeeper_45299C(World& w, int team) { return findKeeper(w, team); }
bool Pitch_inPenaltyArea(const World& w, const Vec3& p) { return inPenaltyBox(w, p); }
void Pitch_clampToField(const World& w, Vec3& p) { clampToField(w, p); }

// 0x455DD0 / 0x455E34 (EAX side, EDX pos): inside the penalty / goal area at the side's end.
static bool inEndBox(const World& w, int side, const Vec3& p, std::int32_t halfW, std::int32_t depth) {
    if (iabs(p.x) > halfW || iabs(p.z) > w.pitch.halfLength) return false;
    const std::int32_t line = w.pitch.halfLength - depth;
    return side == 1 ? line <= p.z : !(-line < p.z);
}
bool Pitch_inPenaltyBox(const World& w, int side, const Vec3& p) {
    return inEndBox(w, side, p, w.pitch.penAreaHalfWidth, w.pitch.penAreaDepth);
}
bool Pitch_inGoalArea(const World& w, int side, const Vec3& p) {
    return inEndBox(w, side, p, w.pitch.goalAreaHalfWidth, w.pitch.goalAreaDepth);
}
// 0x455E98 (EAX side, EDX pos): inside the goal (behind the line, under the bar, within the net depth).
bool Pitch_inGoal(const World& w, int side, const Vec3& p) {
    if (iabs(p.x) > w.pitch.goalHalfWidth || w.pitch.goalHeight < p.y) return false;
    const std::int32_t gl = iabs(w.pitch.goalLineZ[side]);
    if (iabs(p.z) < gl) return false;
    return iabs(p.z) <= w.pitch.f34c + gl;
}
bool Pitch_onField(const World& w, const Vec3& p) { // 0x456094
    return iabs(p.x) <= w.pitch.halfWidth && iabs(p.z) <= w.pitch.halfLength;
}

// ---------------------------------------------------------------------------------------------
// Referee (movement object 0x66764C = players[kReferee]).

const Vec3& Referee_posI(const World& w) { return w.players[kReferee].posI; } // 0x667668 (M+0x1C)

namespace {

void refWarp(World& w, Vec3 pos) { Motion_warpTo(w, kReferee, &pos, nullptr); } // 0x4CB904(0x66764C, pos, EBX 0)
void refRequest(World& w, std::uint32_t bits) { // or [0x667718], bits; [M+0]->+0x18 = req >> 16
    PlayerState& r = w.players[kReferee];
    r.moveReq |= bits;
    r.anim.req = static_cast<u32>(static_cast<std::int32_t>(r.moveReq) >> 16);
}
// 0x470BB8 (EAX src, EDX out): stand 3/4 of the way along z, off to the side, at least 0x120 away.
Vec3 refStandOff(const Vec3& s) {
    Vec3 o;
    o.z = s.z - (s.z >> 2);
    o.x = (-o.z) >> 1;
    o.y = 0;
    const std::int32_t dx = o.x - s.x, dz = o.z - s.z;
    if (octLength(dx, dz) < 0x120) {
        o.x += dx < 0 ? -0x120 : 0x120;
        o.z += dz < 0 ? -0x120 : 0x120;
    }
    return o;
}
// Restart spot pushed back toward the centre (0x470DF8 / 0x472940 / 0x472E43): offsets by the z sign.
Vec3 refRestartOffset(const World& w) {
    Vec3 t = w.ball.restartPos;
    const bool neg = w.ball.restartPos.z < 0;
    t.x += neg ? 0x180 : -0x180;
    t.z += neg ? 0x1e0 : -0x1e0;
    return t;
}
// 0x473747 / 0x47386F / 0x473984: alternative target 0x60 beyond the foul spot on the referee's side.
void refApproachFoulSpot(World& w) {
    const MatchState& m = w.match;
    const Vec3& r = w.players[kReferee].posI;
    Vec3 t = m.foulSpot;
    t.x += r.x < m.foulSpot.x ? -0x60 : 0x60;
    t.z += r.z < m.foulSpot.z ? -0x60 : 0x60;
    Motion_setTargetAlt(w, kReferee, t);
}

} // namespace

// 0x470C80 (EAX = phase/restart kind): referee speed cap and target for the coming phase.
void Referee_onPhase_470C80(World& w, int kind) {
    if (w.opt.gameMode == 1) return;
    PlayerState& r = w.players[kReferee];
    r.speedCap = r.runMax;
    const Vec3 centre{0, 0, 0}; // 0x65F38C (set to 0 by Pitch_setDimensions 0x455AB6)
    Vec3 t{};
    switch (kind) {
    case 13: case 19: // 0x470CE6
        r.speedCap = 0x60000;
        [[fallthrough]];
    case 0: // 0x470CB6
        t = {w.pitch.goalWidthVisual, 0, 0};
        break;
    case 1: case 2: case 7: // 0x470D10
        r.speedCap = 0xa0000;
        t = refStandOff(w.ball.restartPos);
        break;
    case 3: case 20: // 0x470CF2
        r.speedCap = 0xa0000;
        t = centre;
        t.x += 0xa0;
        break;
    case 4: case 6: // 0x470DF8
        t = refRestartOffset(w);
        break;
    case 5: { // 0x470D29 corner
        r.speedCap = 0xa0000;
        const Vec3& s = w.ball.restartPos;
        t = s;
        if (iabs(s.x) > w.pitch.halfLength - iabs(s.z)) {
            t.x += s.x < 0 ? 0x120 : -0x120;
            t.z += s.z < 0 ? 0x240 : -0x240;
        } else {
            t.x += s.x < 0 ? 0x240 : -0x240;
            t.z += s.z < 0 ? 0x120 : -0x120;
        }
        break;
    }
    case 8: // 0x470CDA: stay
        t = r.posI;
        break;
    case 9: // 0x470DE7: follow play
        t = refStandOff(w.ball.posI);
        break;
    case 12: case 14: case 15: case 16: case 17: // 0x470E45
        t = {w.pitch.entryPos[0][0], w.pitch.entryPos[0][1], w.pitch.entryPos[0][2]};
        r.speedCap = 0x30000;
        break;
    default: return; // 10, 11, 18, > 20
    }
    Motion_setTarget(w, kReferee, t);
}

// ---------------------------------------------------------------------------------------------
// Restart sequences: queue 0x6675BC (MatchSeq_push 0x471F78 / clear 0x471FA8 / startNext 0x472344)
// and the 16 frame-driven handlers of table 0x581008, ported from the disassembly. Presentation
// calls inside them are dropped: sounds (0x4A273C/0x4A3214), replay (0x4D9D14), commentary
// (0x4D45A0), SeqTrigger fire (PTR 0x581BB0). Referee calls (0x470C80/0x4CB7FC/0x4CB904) are kept.
// Cut-scene scripts are not modelled: CutScene_play only raises DAT_0066341C (as 0x4643F0 does
// first) and the script engine stays idle (DAT_006633D8 = 1), so every "wait for script" step
// completes at once, exactly like the exe when no script is selected.

namespace {

// 0x454E3C (EAX = bits): request fast-forward. 1 -> f278 |= 0x40; 2 -> f278 |= 0x80 if the skip
// request 0x4000 is up. Ignored while the ball is in play.
void ffOn(World& w, int a) {
    MatchState& m = w.match;
    if (m.phase == 9) return;
    if (a & 1) m.flags278 |= 0x40;
    if ((a & 2) && (m.flags278 & 0x4000)) m.flags278 |= 0x80;
    m.f280 = 0;
}
// 0x454E74: clear fast-forward bits.
void ffOff(World& w, int a) {
    if (a & 1) w.match.flags278 &= ~0x40u;
    if (a & 2) w.match.flags278 &= ~0x80u;
}
// Common handler prologue: a pause/skip press (0x2000) while skippable (0x4000) skips the scene.
void seqSkipCheck(World& w) {
    MatchState& m = w.match;
    if ((m.flags278 & 0x4000) && !(m.flags278 & 0x80) && (m.flags278 & 0x2000)) {
        ffOn(w, 2);
        m.flags278 &= ~0x4000u;
    }
}
void seqNext(MatchState& m) {
    ++m.seqStep;
    m.seqFrames = 0;
}
bool scriptIdle(const World& w) { return (w.match.scriptState & 1) != 0; }
// Shared "wait until the script engine is idle or 0x5DC0 frames passed" step.
bool scriptDone(const World& w) {
    return (w.match.flags278 & 0x80) || scriptIdle(w) || static_cast<u32>(w.match.seqFrames) > 0x5dc0;
}
// The handlers compare DAT_006675F0 against doubles 30.0 / 90.0 (0x57019C / 0x5701AC) via fild.
bool framesAbove(const World& w, int v) { return static_cast<u32>(w.match.seqFrames) > static_cast<u32>(v); }

// Substitution queue 0x66E586 + t*0x2C: pending substitutions (squad.cpp).
// 0x46549C: picks an end-of-game cut-scene (EG*/EM* scripts); scripts are not modelled.
int untraced_CutScene_pickEndScript_46549C(World&) { return 0; }
// 0x46CDE0: season/competition bookkeeping at full time outside friendlies (game modes != 0/6).
void untraced_Competition_fullTime_46CDE0(World&) {}

// 0x471FD4: substitution between restarts; 1 = a substitution is being made. The leaving player
// walks off in state 0x186, which carries the substitution out (Team_removeInjured_49F684) and clears
// flag 4. (0x581050 / SeqTrigger 0xC and the commentary counter 0x480570 are presentation.)
int seqSubstitution(World& w) {
    MatchState& m = w.match;
    if (m.flags278 & 4) return 1;
    int t = 0, idx = 0;
    if (!Squad_nextSub_49F630(w, &t, &idx)) return 0;
    m.flags278 |= 4;
    const int p = w.teams[t].players[idx];
    m.subPlayer = p;
    Player_setState(w, p, 0x186, 0, 0, false, true); // 0x4BDCD8(EDX 0x186, EBX 1, ECX 0, 0, 0)
    return 1;
}

void seq3PreKickoff(World& w) { // 0x4720A4
    MatchState& m = w.match;
    seqSkipCheck(w);
    switch (m.seqStep) {
    case 0:
        if (m.cutScene == 0) {
            m.seqFrames = 0;
            m.seqStep = 3;
        } else if (CutScene_isReady(w)) {
            seqNext(m);
        }
        return;
    case 1:
        seqNext(m);
        m.flags278 |= 0x4000;
        [[fallthrough]];
    case 2:
        if (scriptDone(w)) {
            if (!(m.flags278 & 0x80)) ffOn(w, 1);
            seqNext(m);
        }
        return;
    case 3: // 0x4721AC
        if (w.opt.gameMode != 1) refWarp(w, {w.pitch.goalWidthVisual, 0, 0});
        seqNext(m);
        return;
    case 4: seqNext(m); return;
    case 5:
        ffOff(w, 3);
        m.flags278 &= ~0x4000u;
        if (Match_checkAbandon(w)) return;
        MatchSeq_startNext(w, 1);
        return;
    default: return;
    }
}

void seq2FullTimeWhistle(World& w) { // 0x472244
    MatchState& m = w.match;
    switch (m.seqStep) {
    case 0:
        Match_setPhase(w, 8, m.endTeam);
        seqNext(m); // whistle 0x23
        return;
    case 1:
        if (static_cast<u32>(m.seqFrames) >= 0x2d) seqNext(m); // whistle 0x23
        return;
    case 2:
        if (static_cast<u32>(m.seqFrames) >= 0x2d) MatchSeq_startNext(w, 1); // whistle 0x22
        return;
    default: return;
    }
}

void seq1HalfTimeWhistle(World& w) { // 0x4722E4
    MatchState& m = w.match;
    if (m.seqStep == 0) {
        Match_setPhase(w, 8, m.endTeam);
        seqNext(m); // whistle 0x23
    } else if (m.seqStep == 1 && static_cast<u32>(m.seqFrames) >= 0x3c) {
        MatchSeq_startNext(w, 1); // whistle 0x22
    }
}

void seq9PeriodEnd(World& w) { // 0x4723F8
    MatchState& m = w.match;
    refRequest(w, 0x4000000); // 0x472401
    m.seqE4 = 0;
    seqSkipCheck(w);
    switch (m.seqStep) {
    case 0:
        m.flags278 |= 0x20;
        keepersReleaseAll(w); // 0x470188
        Referee_onPhase_470C80(w, m.nextPhase);
        Match_setPhase(w, m.nextPhase, m.endTeam);
        w.emit(EventType::PeriodEnd, m.endTeam, -1, m.nextPhase, m.periodType);
        if (m.nextPhase == 0x10 || m.nextPhase == 0x11) w.emit(EventType::MatchEnd, m.endTeam, -1, m.score[0], m.score[1]);
        if (w.opt.gameMode != 0 && w.opt.gameMode != 6 && (m.phase == 0x10 || m.phase == 0x11))
            untraced_Competition_fullTime_46CDE0(w);
        seqNext(m);
        return;
    case 1:
        if (!framesAbove(w, 30)) return;
        CutScene_play(w, m.nextPhase);
        m.flags278 &= ~0x20u;
        seqNext(m);
        return;
    case 2:
        if (m.cutScene == 0) {
            m.seqFrames = 0;
            m.seqStep = 5;
        } else if (CutScene_isReady(w)) {
            seqNext(m);
        }
        return;
    case 3:
        seqNext(m);
        m.flags278 |= 0x4000;
        [[fallthrough]];
    case 4:
        if (scriptDone(w)) {
            if (!(m.flags278 & 0x80)) ffOn(w, 1);
            seqNext(m);
        }
        if (w.opt.rules & 0x40) return; // cinemas option: the scene still runs
        [[fallthrough]];
    case 5:
        if (m.phase != 0xe && untraced_CutScene_pickEndScript_46549C(w)) {
            m.flags278 |= 0x20;
            seqNext(m);
        } else {
            m.seqStep = 0xb;
            m.seqFrames = 0;
        }
        m.flags278 &= ~0x4000u;
        ffOff(w, 3);
        return;
    case 6:
        CutScene_play(w, 0x15);
        m.flags278 &= ~0x20u;
        seqNext(m);
        return;
    case 7:
        if (CutScene_isReady(w)) seqNext(m);
        return;
    case 8:
        seqNext(m);
        [[fallthrough]];
    case 9:
        if (scriptIdle(w) || framesAbove(w, 0x5dc0)) {
            ffOn(w, 1);
            seqNext(m);
        }
        return;
    case 10: seqNext(m); return;
    case 11: // 0x472724: referee off the pitch (0x46F2BC)
        refWarp(w, {160000, 0, 160000});
        seqNext(m);
        return;
    case 12:
        ffOff(w, 3);
        m.status |= 0x20; // simulation halted until the next period (Sim_advance)
        MatchSeq_startNext(w, 1);
        return;
    default: return;
    }
}

void seq8ShootoutMiss(World& w) { // 0x4727B4
    MatchState& m = w.match;
    seqSkipCheck(w);
    switch (m.seqStep) {
    case 0:
        m.flags278 |= 0x20;
        Referee_onPhase_470C80(w, m.restartType);
        Match_setPhase(w, 0x13, m.phaseTeam);
        seqNext(m);
        return;
    case 1:
        if (!framesAbove(w, 30)) return;
        m.flags278 &= ~0x20u;
        CutScene_play(w, 6);
        seqNext(m);
        [[fallthrough]];
    case 2:
        if (CutScene_isReady(w)) seqNext(m);
        return;
    case 3:
        m.flags278 |= 0x4000;
        seqNext(m);
        [[fallthrough]];
    case 4:
        if (scriptDone(w)) {
            if (!(m.flags278 & 0x80)) ffOn(w, 1);
            seqNext(m);
        }
        return;
    case 5: seqNext(m); return;
    case 6:
        refWarp(w, refRestartOffset(w)); // 0x472940
        Ball_placeAt(w, w.ball.restartPos, false, 0);
        seqNext(m);
        return;
    case 7:
        m.flags278 &= ~0x4000u;
        MatchSeq_startNext(w, 1);
        return;
    default: return;
    }
}

void seq15NearMiss(World& w) { // 0x472A08
    MatchState& m = w.match;
    seqSkipCheck(w);
    switch (m.seqStep) {
    case 0:
        Match_setPhase(w, 0x14, m.phaseTeam);
        CutScene_play(w, 0x14);
        m.flags278 |= 0x20;
        seqNext(m);
        return;
    case 1:
        Team_setPlayersState(w, 0, 0x18e, 0xff, 0xff);
        Team_setPlayersState(w, 1, 0x18e, 0xff, 0xff);
        seqNext(m);
        return;
    case 2:
        if (static_cast<u32>(m.seqFrames) < 0x3c) return;
        m.flags278 &= ~0x20u;
        seqNext(m);
        return;
    case 3:
        if (!CutScene_isReady(w)) return;
        m.flags278 |= 0x4000;
        seqNext(m);
        return;
    case 4:
        if (scriptDone(w)) {
            if (!(m.flags278 & 0x80)) ffOn(w, 1);
            seqNext(m);
        }
        return;
    case 5: seqNext(m); return;
    case 6: // 0x472BB1
        refWarp(w, {0xa0, 0, 0});
        seqNext(m);
        return;
    case 7:
        m.flags278 &= ~0x4000u;
        MatchSeq_startNext(w, 1);
        return;
    default: return;
    }
}

void seq7GoalCelebration(World& w) { // 0x472C38
    MatchState& m = w.match;
    seqSkipCheck(w);
    switch (m.seqStep) {
    case 0:
        m.flags278 |= 0x20;
        Referee_onPhase_470C80(w, m.restartType);
        Match_setPhase(w, 0xd, m.phaseTeam);
        seqNext(m);
        return;
    case 1:
        if (SeqTrigger_busy(w, 2)) return;
        if (!framesAbove(w, 90)) return;
        CutScene_play(w, (m.phase == 6 || m.periodType == 3) ? 6 : 0xd);
        m.flags278 &= ~0x20u;
        seqNext(m);
        return;
    case 2:
        if (CutScene_isReady(w)) seqNext(m);
        return;
    case 3:
        m.flags278 |= 0x4000;
        seqNext(m);
        [[fallthrough]];
    case 4:
        if (scriptDone(w)) {
            if (!(m.flags278 & 0x80)) ffOn(w, 1);
            seqNext(m);
        }
        return;
    case 5: seqNext(m); return;
    case 6:
        if (m.periodType == 3) { // 0x472E43
            const Vec3 t = refRestartOffset(w);
            Ball_placeAt(w, w.ball.restartPos, false, 0);
            refWarp(w, t);
        } else {
            refWarp(w, {w.pitch.goalWidthVisual, 0, 0});
        }
        seqNext(m);
        return;
    case 7:
        m.flags278 &= ~0x4000u;
        MatchSeq_startNext(w, 1);
        return;
    default: return;
    }
}

void seq4TakeRestart(World& w) { // 0x472ED8
    MatchState& m = w.match;
    const int rt = m.restartType;
    switch (m.seqStep) {
    case 0:
        keepersReleaseAll(w);
        if (rt == 0) Match_resetPhase(w);
        m.flags278 &= ~0x80000u;
        m.flags278 |= 0x23;
        Team_setPlayersFlag80(w, 0, 1);
        Team_setPlayersFlag80(w, 1, 1);
        Match_lineUpRestart(w, rt);
        if (rt == 4) {
            Ball_placeAt(w, w.ball.restartPos, false, 0);
            w.time.focus = -1;
        }
        Match_setPhase(w, rt, m.phaseTeam);
        m.seqE0 = 0;
        m.seqFrames = 0;
        m.seqFlags &= ~1u;
        ++m.seqStep;
        [[fallthrough]];
    case 1:
        if (Squad_pendingCount_66E586(w, 0) + Squad_pendingCount_66E586(w, 1) > 0 &&
            static_cast<u32>(m.seqFrames) < 0x3c)
            return;
        if (m.totalSec > 0 && m.periodType != 3 && seqSubstitution(w)) {
            m.flags278 |= 0x80000;
            m.seqFlags |= 1;
            return;
        }
        seqNext(m);
        return;
    case 2:
        if (SeqTrigger_busy(w, 0x10)) return;
        m.flags278 &= ~0x80000u;
        if (m.seqFlags & 1) {
            m.seqFlags &= ~1u;
            Match_setPhase(w, rt, m.phaseTeam);
        }
        Referee_onPhase_470C80(w, rt); // 0x473042
        seqNext(m);
        [[fallthrough]];
    case 3:
        Referee_onPhase_470C80(w, rt); // 0x47306A
        if (SeqTrigger_busy(w, 0x14)) return;
        if (static_cast<u32>(m.seqFrames) < 0xb4 && rt == 6) return;
        if (rt == 6) Ball_placeAt(w, w.ball.restartPos, false, 0);
        seqNext(m);
        return;
    case 4: {
        const bool walking = Team_playersWalking(w, 0) || Team_playersWalking(w, 1);
        const bool busy = (m.flags278 & 0x400) || Team_playersBusy(w, 0) || Team_playersBusy(w, 1);
        if ((walking || busy) && static_cast<u32>(m.seqFrames) < 0x960) return;
        Match_lineUpRestart(w, rt);
        if (rt == 0) {
            KickoffPlace_init(w); // §7 delta-3: kickoff dollies to the centre spot, no direct snap
        } else if (rt != 1 && rt != 2 && rt != 3 && rt != 5) {
            Ball_placeAt(w, w.ball.restartPos, false, 0);
        }
        if (rt != 2 && rt != 5 && rt != 6) w.time.focus = -1;
        m.flags278 &= ~1u;
        seqNext(m);
        [[fallthrough]];
    }
    case 5: {
        const int ph = m.phase;
        if (ph == 0 || ph == 4 || ph == 5 || ph == 6) {
            if (static_cast<u32>(m.seqFrames) < 0x3c) return;
            // whistle 0x22 unless a free kick without a card
        }
        m.flags278 &= ~0x1020u;
        ffOff(w, 3);
        m.flags278 &= ~0x4000u;
        MatchSeq_startNext(w, 1);
        return;
    }
    default: return;
    }
}

void seq5FoulAndBooking(World& w) { // 0x4732A8
    MatchState& m = w.match;
    refRequest(w, 1); // 0x4732AE
    seqSkipCheck(w); // (replay pump 0x4D9D14 each frame: presentation)
    switch (m.seqStep) {
    case 0:
        m.flags278 |= 0x20;
        Match_setPhase(w, 0xa, m.phaseTeam);
        seqNext(m);
        return;
    case 1:
        if (static_cast<u32>(m.seqFrames) >= 0xf) seqNext(m); // whistle 0x22
        return;
    case 2: {
        if (static_cast<u32>(m.seqFrames) < 0x3c) return;
        if (m.card != 0) {
            const int p = m.offender, t = teamOf(w, p), idx = w.players[p].index;
            PlayerAttr& a = w.attrs[w.players[p].attr];
            const bool second = m.card == 1 && (a.at(0x40) & 2);
            // FUN_004D0338: card bits in the attribute status byte +0x40.
            if (m.card == 1) {
                if (a.at(0x40) & 2) a.at(0x40) = static_cast<std::uint8_t>((a.at(0x40) & 0xd5) | 0x28);
                else a.at(0x40) = static_cast<std::uint8_t>((a.at(0x40) | 2) & 0xfb);
            } else if (m.card == 2) {
                a.at(0x40) |= 0x28;
            }
            if (second) m.card = 2;
            std::int32_t time = m.totalSec;
            if (static_cast<u32>(m.elapsedSec) > static_cast<u32>(m.durationSec)) time = time - m.elapsedSec + m.durationSec;
            recordCard(w, t, idx, m.card == 2 ? 1 : 0, time);
            if (m.card == 2) m.flags278 |= 0x8000;
            w.emit(m.card == 2 ? EventType::RedCard : EventType::YellowCard, t, p, m.foulClass, second ? 1 : 0);
        }
        CutScene_play(w, 0xa);
        Team_setPlayersState(w, 0, 0x18f, 0xff, 0xff);
        Team_setPlayersState(w, 1, 0x18f, 0xff, 0xff);
        m.f294 = m.victim;
        seqNext(m);
        [[fallthrough]];
    }
    case 3:
        if (SeqTrigger_busy(w, 4)) return;
        m.flags278 &= ~0x8030u;
        seqNext(m);
        [[fallthrough]];
    case 4:
        if (CutScene_isReady(w)) seqNext(m);
        return;
    case 5:
        m.flags278 |= 0x4000;
        m.seqTrigger = m.card ? 6 : 5;
        seqNext(m);
        return;
    case 6:
        if (scriptDone(w)) {
            if (!(m.flags278 & 0x80)) ffOn(w, 1);
            seqNext(m);
        }
        return;
    case 7: {
        if (SeqTrigger_busy(w, m.seqTrigger)) return;
        refWarp(w, m.foulSpot); // 0x4735B4
        const int p = m.offender, t = teamOf(w, p);
        if (!(w.attrs[w.players[p].attr].at(0x40) & 0x20)) {
            seqNext(m);
            return;
        }
        Team_sendOff(w, t, w.players[p].index);
        if (w.opt.gameMode != 1 && Team_tooFewPlayers(w, t)) {
            m.flags278 &= ~0x4000u;
            ffOff(w, 3);
            abandonMatch(w, t); // 0x4D03D0
            Match_decidePeriodOutcome(w);
            Match_dispatchEvent(w, 9, 0);
        } else {
            const int humans = w.teams[t].humans;
            Formation_adaptToPlayers(w, t);
            Team_humanAfterSendOff(w, t, humans ? 0 : 1);
        }
        if (w.players[p].ctrl != 0) Player_switchControl(w, p, 1);
        seqNext(m);
        return;
    }
    case 8: seqNext(m); return;
    case 9: MatchSeq_startNext(w, 1); return;
    default: return;
    }
}

void seq6Offside(World& w) { // 0x4736F8
    MatchState& m = w.match;
    if (m.seqStep == 0) {
        Match_setPhase(w, 0xb, m.phaseTeam);
        m.f294 = m.offender;
        refApproachFoulSpot(w);
        seqNext(m);
    } else if (m.seqStep != 1) {
        return;
    }
    if (SeqTrigger_busy(w, 8)) return;
    if (static_cast<u32>(m.seqFrames) >= 0x78) MatchSeq_startNext(w, 1);
}

void seq12ShootoutTimeout(World& w) { // 0x4737FC
    const MatchState& m = w.match;
    if (m.seqStep != 0) return;
    const bool moving = w.ball.speed != 0 || w.ball.possessor >= 0;
    if (moving && static_cast<u32>(m.seqFrames) < 0x168) return;
    Match_dispatchEvent(w, 1, 0);
}

// Sequences 13/14: set-piece line-up waits for the referee to reach its spot ([0x6676A0] = M+0x54
// target distance <= 0x20).
void seq13SetPieceB(World& w) { // 0x473834
    MatchState& m = w.match;
    if (m.seqStep == 0) {
        Match_setPhase(w, 0xb, m.phaseTeam);
        refApproachFoulSpot(w);
        seqNext(m);
    } else if (m.seqStep != 1) {
        return;
    }
    if (SeqTrigger_busy(w, 0x27)) return;
    if (static_cast<u32>(m.seqFrames) >= 0x78 && w.players[kReferee].targetDist <= 0x20) MatchSeq_startNext(w, 1);
}

void seq14SetPieceC(World& w) { // 0x473938
    MatchState& m = w.match;
    if (m.seqStep == 0) {
        Match_setPhase(w, 0xb, m.phaseTeam);
        refApproachFoulSpot(w);
        seqNext(m);
    } else if (m.seqStep == 1 && static_cast<u32>(m.seqFrames) >= 0x78 && w.players[kReferee].targetDist <= 0x20) {
        MatchSeq_startNext(w, 1);
    }
}

void seq10TrainingResetBall(World& w) { // 0x473D5C (training module calls untraced)
    MatchState& m = w.match;
    if (m.trainDrill == 2 || m.trainDrill == 3) {
        MatchSeq_startNext(w, 1);
        return;
    }
    auto aimAndArm = [&] {
        const int p = w.teams[m.phaseTeam].players[0];
        SetPiece_begin(w, p, atan2i(w.ball.posI.x, w.pitch.halfLength - w.ball.posI.z), 0);
        SetPiece_takeAim_44C560(w, p);
        untraced_Training_placeBall_46D7BC(w);
    };
    switch (m.seqStep) {
    case 0:
        m.flags278 |= 0x20;
        m.phase = m.restartType;
        aimAndArm();
        seqNext(m);
        [[fallthrough]];
    case 1:
        seqNext(m);
        [[fallthrough]];
    case 2:
        if (m.flags278 & 8) aimAndArm();
        if (m.setPieceAimCtrl == 0) {
            do {
                m.setPieceAim.x = w.pitch.halfWidth - static_cast<std::int32_t>(w.rng.next() % static_cast<u32>(w.pitch.width));
                m.setPieceAim.z = static_cast<std::int32_t>(w.rng.next() % static_cast<u32>(w.pitch.halfLength));
            } while (Pitch_inPenaltyArea(w, m.setPieceAim));
        }
        if (untraced_Training_ballPlaced_46D83C(w)) {
            untraced_Training_reset_46D704(w);
            SetPiece_reset(w);
            MatchSeq_startNext(w, 1);
        }
        return;
    default: return;
    }
}

void seq11TrainingDrillEnd(World& w) { // 0x473F84
    MatchState& m = w.match;
    switch (m.seqStep) {
    case 0:
        untraced_Training_drillEnd_46DA18(w);
        m.status |= 2;
        Match_setPhase(w, 8, 0);
        seqNext(m);
        [[fallthrough]];
    case 1:
        if (m.trainGoal != 0 && SeqTrigger_busy(w, 2)) return;
        m.trainGoal = 0;
        seqNext(m);
        [[fallthrough]];
    case 2:
        if (SeqTrigger_busy(w, 9)) return;
        seqNext(m);
        return;
    case 3:
        if (static_cast<u32>(m.seqFrames) < 0xf0) return;
        untraced_Training_nextDrill_46D658(w);
        MatchSeq_startNext(w, 1);
        untraced_Training_restart_46D570(w);
        return;
    default: return;
    }
}

void seq16PresentationEnd(World& w) { // 0x474094
    MatchState& m = w.match;
    switch (m.seqStep) {
    case 0:
        m.flags278 &= ~0x4000u;
        if (CutScene_isReady(w)) seqNext(m);
        return;
    case 1:
        if (scriptIdle(w) || framesAbove(w, 0x5dc0)) {
            ffOn(w, 1);
            seqNext(m);
        }
        return;
    case 2: // 0x47411C: referee off the pitch (0x46F2E0)
        refWarp(w, {160000, 0, 160000});
        seqNext(m);
        return;
    case 3: seqNext(m); return;
    case 4:
        ffOff(w, 3);
        Match_requestPeriodEnd(w, 0, 0x11, 0);
        m.status |= 0x20;
        MatchSeq_startNext(w, 0);
        return;
    default: return;
    }
}

} // namespace

void MatchSeq_push(World& w, int id) { // 0x471F78: drops the id when the ring is full
    MatchState& m = w.match;
    int next = m.seqWrite + 1;
    if (next >= 5) next = 0;
    if (next == m.seqRead) return;
    m.seqQueue[m.seqWrite] = id;
    m.seqWrite = next;
}

void MatchSeq_clear(World& w) { // 0x471FA8
    MatchState& m = w.match;
    m.seqRead = 0;
    m.seqWrite = 1;
    for (std::int32_t& q : m.seqQueue) q = 0;
}

void MatchSeq_startNext(World& w, int runNow) { // 0x472344
    MatchState& m = w.match;
    m.seqQueue[m.seqRead] = 0;
    if (m.seqRead != m.seqWrite) {
        ++m.seqRead;
        if (m.seqRead >= 5) m.seqRead = 0;
    }
    const int id = m.seqQueue[m.seqRead];
    m.seqE4 = m.seqE8 = 1;
    m.seqId = id;
    m.seqFrames = 0;
    m.seqStep = 0;
    if (id != 0 && runNow) MatchSeq_run(w, id);
}

void MatchSeq_reset(World& w) { // FUN_0046F2EC
    MatchState& m = w.match;
    m.seqRead = m.seqWrite = 0;
    for (std::int32_t& q : m.seqQueue) q = 0;
    MatchSeq_clear(w);
    m.seqId = m.seqStep = m.seqFrames = m.seqE0 = m.seqTrigger = 0;
    m.seqE4 = m.seqE8 = 1;
    PlayerState& r = w.players[kReferee];
    r.runMax = 0x40000;      // 0x6676F8 M+0xAC
    r.accel = 0x18000;       // 0x667704 M+0xB8
    r.accelRecip = 0xaaaa;   // 0x66770C = 2^32 / 0x18000
    r.decel = 0x18000;       // 0x667700 M+0xB4
    r.decelRecip = 0xaaaa;   // 0x667714
    r.anim.pitchLimit = -14; // 0x66761C
    r.turnAgility = 3;       // 0x667664 M+0x18
    Anim_init(w, kReferee, false);
}

void MatchSeq_run(World& w, int id) { // table 0x581008
    switch (id) {
    case 1: seq1HalfTimeWhistle(w); break;
    case 2: seq2FullTimeWhistle(w); break;
    case 3: seq3PreKickoff(w); break;
    case 4: seq4TakeRestart(w); break;
    case 5: seq5FoulAndBooking(w); break;
    case 6: seq6Offside(w); break;
    case 7: seq7GoalCelebration(w); break;
    case 8: seq8ShootoutMiss(w); break;
    case 9: seq9PeriodEnd(w); break;
    case 10: seq10TrainingResetBall(w); break;
    case 11: seq11TrainingDrillEnd(w); break;
    case 12: seq12ShootoutTimeout(w); break;
    case 13: seq13SetPieceB(w); break;
    case 14: seq14SetPieceC(w); break;
    case 15: seq15NearMiss(w); break;
    case 16: seq16PresentationEnd(w); break;
    default: break;
    }
}

int SeqTrigger_busy(const World&, int) { return 0; } // PTR 0x581BB4: presentation triggers never block

void CutScene_play(World& w, int) { w.match.cutScene = 1; } // 0x4643F0 prologue; no script selected

int CutScene_isReady(const World& w) { return (w.match.scriptState & 2) ? 0 : -1; } // 0x46520C, no script
// 0x465294 (EAX = player): with no script loaded (DAT_006648BC == 0, the CutScene_play path the port
// models) it returns 0 while the engine is running (DAT_006633D8 & 2) and -1 (ready) otherwise.
int CutScene_playerReady(const World& w, int) { return (w.match.scriptState & 2) ? 0 : -1; }
// DAT_00663420: set to 1 only at 0x4646A9 right before CutScene_play selects a script (0x100/0x120)
// and cleared by 0x462C69 / 0x463F4B; never set on the no-script path.
int CutScene_active663420(const World&) { return 0; }
// DAT_0066741C: written only by the training module (0x46D44F / 0x46D68A); zeroed with the
// 0x667410 block by Match_setupOptions.
int Training_ctrl66741C(const World& w) { return w.match.trainCtrl; }

} // namespace wc98::sim
