// Team AI on the ball: decisions 0x4DC614 / 0x4DCAC8 / 0x4DCE94 / 0x4DD324 / 0x4DF2CC and the pass /
// shot evaluator 0x5455C0..0x546B2C they share (owner: SimKeeperAI). Ported from the FIFA99.EXE v1.1
// disassembly; tuning constants and tables are read through their exe addresses (aiD32/aiD8).
#include "player_states.hpp"
#include "wc98/sim/sim.hpp"

namespace wc98::sim {
namespace {

// exe 0x59FC30..0x59FDBF: evaluator constants (dwords 0x59FC30..0x59FD0B) and mentality / formation-style
// byte tables (0x59FD40..). Read-only in the exe.
constexpr std::uint8_t kAiData[0x190] = {
    171, 0, 0, 0, 28, 1, 0, 0, 85, 1, 0, 0, 112, 2, 0, 0, 128, 7, 0, 0, 228, 0, 0, 0,
    128, 7, 0, 0, 64, 11, 0, 0, 176, 4, 0, 0, 112, 0, 0, 0, 48, 0, 0, 0, 80, 1, 0, 0,
    0, 1, 0, 0, 86, 1, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0, 5, 0, 0, 0,
    20, 0, 0, 0, 30, 0, 0, 0, 80, 0, 0, 0, 140, 0, 0, 0, 43, 0, 0, 0, 128, 0, 0, 0,
    50, 0, 0, 0, 40, 0, 0, 0, 60, 0, 0, 0, 96, 0, 0, 0, 192, 0, 0, 0, 128, 1, 0, 0,
    80, 0, 0, 0, 171, 0, 0, 0, 32, 1, 0, 0, 64, 2, 0, 0, 192, 3, 0, 0, 64, 5, 0, 0,
    192, 6, 0, 0, 0, 3, 0, 0, 128, 1, 0, 0, 14, 0, 0, 0, 43, 0, 0, 0, 114, 0, 0, 0,
    171, 0, 0, 0, 85, 0, 0, 0, 100, 0, 0, 0, 50, 0, 0, 0, 40, 0, 0, 0, 90, 0, 0, 0,
    106, 255, 255, 255, 212, 254, 255, 255, 44, 1, 0, 0, 60, 0, 0, 0, 128, 0, 0, 0, 171, 0, 0, 0,
    48, 0, 0, 0, 128, 0, 0, 0, 160, 0, 0, 0, 85, 0, 0, 0, 32, 1, 0, 0, 64, 1, 0, 0,
    128, 0, 0, 0, 114, 0, 0, 0, 192, 0, 0, 0, 128, 0, 0, 0, 185, 0, 0, 0, 96, 0, 0, 0,
    240, 0, 0, 0, 144, 0, 0, 0, 50, 60, 70, 80, 90, 5, 15, 30, 40, 50, 100, 100, 80, 50, 40, 0,
    10, 30, 30, 40, 0, 5, 10, 10, 10, 0, 20, 0, 20, 60, 30, 40, 50, 30, 20, 5, 70, 0, 0, 40,
    40, 40, 40, 20, 0, 20, 30, 20, 40, 30, 20, 35, 0, 20, 0, 20, 40, 0, 20, 40, 10, 50, 50, 50,
    200, 200, 200, 200, 200, 150, 150, 150, 150, 150, 80, 60, 50, 0, 0, 20, 10, 5, 0, 0, 10, 5, 0, 0,
    0, 40, 20, 5, 0, 0, 50, 35, 20, 20, 20, 100, 90, 60, 60, 60, 100, 100, 95, 95, 95, 100, 100, 100,
    100, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 96, 116, 84, 0,
};
std::int32_t aiD32(std::uint32_t a) {
    const std::uint8_t* p = kAiData + (a - 0x59fc30);
    return static_cast<std::int32_t>(p[0] | p[1] << 8 | p[2] << 16 | static_cast<std::uint32_t>(p[3]) << 24);
}
std::int32_t aiD8(std::uint32_t a) { return kAiData[a - 0x59fc30]; }
// exe 0x590400 / 0x590450 / 0x5904A0 / 0x5904F0: per-rating (0..19) tables (attr 0x31 pass weights,
// attr 0x38 shot range, attr 0x37 shot spread).
constexpr std::int32_t kRateTab[80] = {
    0x14, 0x19, 0x1e, 0x23, 0x28, 0x2d, 0x32, 0x37, 0x3c, 0x46,
    0x50, 0x5a, 0x5f, 0x62, 0x64, 0x64, 0x69, 0x6e, 0x73, 0x78,
    0x3, 0x5, 0x8, 0xa, 0xc, 0xf, 0x14, 0x19, 0x1e, 0x23,
    0x32, 0x3c, 0x46, 0x55, 0x5a, 0x64, 0x69, 0x6e, 0x73, 0x78,
    0x2a0, 0x330, 0x330, 0x3f0, 0x420, 0x4e0, 0x540, 0x540, 0x570, 0x600,
    0x6c0, 0x780, 0x8a0, 0x930, 0xa50, 0xc30, 0xc60, 0xd50, 0xe40, 0xf30,
    0x36, 0x33, 0x30, 0x2e, 0x2b, 0x28, 0x25, 0x22, 0x1f, 0x1c,
    0x1a, 0x17, 0x14, 0x11, 0xe, 0xb, 0x9, 0x6, 0x3, 0x0,
};
std::int32_t rateTab(std::uint32_t base, int r) { return kRateTab[(base - 0x590400) / 4 + r]; }

TeamState& T(World& w, const PlayerState& p) { return w.teams[static_cast<std::size_t>(p.team)]; }
const TeamState& T(const World& w, const PlayerState& p) { return w.teams[static_cast<std::size_t>(p.team)]; }
std::int32_t wrapAbs(std::int32_t d) { // `and 0x3ff; > 0x200 -> 0x400 - d`
    d &= 0x3ff;
    return d > 0x200 ? 0x400 - d : d;
}
int s01(std::int32_t z) { return z >= 0 ? 1 : 0; }
int sideOf(const World& w, int pid) { return w.teams[static_cast<std::size_t>(P(w, pid).team)].side; }
// `z - thirdLineZ[side] >= 0` / `z + thirdLineZ[side] >= 0` compared with the team side.
bool pastOwnThird(const World& w, int pid) { return s01(P(w, pid).posI.z - w.pitch.thirdLineZ[sideOf(w, pid)]) == sideOf(w, pid); }
std::uint32_t fxMulU(std::int32_t a, std::int32_t b) { return static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b); }

// One evaluated pass/shot target, 0x28 bytes (arrays at [ebp-0x208] etc. of the deciders).
struct PassRec {
    std::int32_t kind = 0;  // +0x00 0 short/back, 1 normal, 2 long (0x5455C0); shots: unused
    Vec3 target;            // +0x04
    std::int32_t r10 = 0;   // +0x08+8 lead / loft (0x5456EC out, shots 0x546540)
    std::int32_t open = 0;  // +0x14 receiver openness 0..100 (0x5456EC) / shot chance (0x546540)
    std::int32_t facing = 0; // +0x18 facing factor (0x545A6C)
    std::int32_t r1c = 0;   // +0x1C position bonus (0x545AAC / 0x5467FC)
    std::int32_t r20 = 0;   // +0x20 mentality bonus (0x545D5C / 0x5468D8)
    std::int32_t score = 0; // +0x24
};
struct DistAng {
    std::int32_t dist, dx, dz, ang;
};

// 0x5455C0 (EDX dAng, EBX my z, ECX target z (both mirrored to side 1), stack x, dist; ret 8): pass class.
int passKind_5455C0(const World& w, std::int32_t dAng, std::int32_t mz, std::int32_t tz, std::int32_t x, std::int32_t dist) {
    const std::int32_t phw = w.pitch.penAreaHalfWidth;
    if (mz < w.pitch.thirdLineZ[0]) {
        std::int32_t lim = mz + aiD32(0x59fca4);
        if (lim < -w.pitch.penAreaLineZ) lim = -w.pitch.penAreaLineZ;
        if (tz > lim) return 2;
        if (x >= phw || x <= -phw) return 1;
        if (tz < -(w.pitch.halfLength - w.pitch.penAreaDepth)) return 0;
        return dAng >= aiD32(0x59fc30) ? 1 : 0;
    }
    const bool back = tz < mz;
    const std::int32_t ql = w.pitch.quarterLength;
    if (tz < ql) {
        if (dAng < aiD32(0x59fc30)) return 0;
        if (dAng >= aiD32(0x59fc34) && !(dAng < aiD32(0x59fc38) && aiD32(0x59fc3c) > dist)) return 2;
        if (tz < -ql && x < phw && x > -phw) return 0;
        return dist > aiD32(0x59fc40) ? 2 : 1;
    }
    if (!back) return 2;
    if (x < phw && x > -phw) return 2;
    if (dAng > aiD32(0x59fc34)) return 2;
    return 0; // (the exe's trailing `return 1` is unreachable)
}

// 0x545F54 (EAX movement object, EDX point): frames to run there, octagonal distance / ((2*runMax + speed) / 2).
std::int32_t runFrames_545F54(const World& w, int pid, const Vec3& p) {
    const PlayerState& q = P(w, pid);
    const std::int32_t dx = iabs(p.x - q.posI.x), dz = iabs(p.z - q.posI.z);
    const std::int32_t mn = dx > dz ? dz : dx;
    const std::int32_t oct = dx + dz - (mn >> 1);
    const std::int32_t sp = (q.runMax * 2 + q.speed) >> 1;
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(oct) << 16) / sp;
}
// 0x545FB0: ball travel frames: dist / 32, doubled for a lofted (far) ball.
std::int32_t ballFrames_545FB0(std::int32_t dist, int far) { return far ? dist >> 5 : (dist >> 5) * 2; }
// 0x545FBC (EAX receiver frames, EDX opponent frames, EBX ball frames): openness 0..100 of a lofted pass.
std::int32_t openLofted_545FBC(std::int32_t tm, std::int32_t to, std::int32_t tb) {
    const std::int32_t d = to - tm;
    if (iabs(d) < aiD32(0x59fcfc)) return aiD32(0x59fc90);
    std::int32_t e;
    if (d >= 0) {
        e = d;
        if (tb >= tm) {
            if (tb >= to) return aiD32(0x59fc98);
            e = to - tb;
        }
        const std::int32_t c = aiD32(0x59fcf8);
        if (e > c) return 0x64;
        return (0x64 - aiD32(0x59fc98)) * e / c + aiD32(0x59fc98);
    }
    e = d;
    if (tb >= to) {
        if (tb >= tm) return aiD32(0x59fc94);
        e = tb - to;
    }
    const std::int32_t c = aiD32(0x59fcf4);
    if (e < c) return 0;
    return aiD32(0x59fc94) * e / c;
}
// 0x546060 (EAX frame margin, EDX receiver past his third): openness 5..100 of a ground pass.
std::int32_t openGround_546060(std::int32_t diff, int flag) {
    const std::int32_t c = flag ? aiD32(0x59fcec) : aiD32(0x59fce8);
    if (diff >= c) return 0x64;
    if (diff > 0) return diff * 50 / c + 0x32;
    const std::int32_t lo = aiD32(0x59fcf0);
    if (diff <= lo) return 5;
    return diff * 45 / -lo + 0x32;
}

// 0x5456EC (EAX kicker, EDX receiver, EBX target, ECX distance/angle, stack lead out; ret 4): receiver
// openness: interceptors on the line (0x4B0 .. dist + 0xF0) veto or force a lofted pass; then the
// receiver's vs the nearest opponent's arrival frames.
std::int32_t openness_5456EC(World& w, int me, int mate, const Vec3& target, const DistAng& D, std::int32_t* lead) {
    const PlayerState& k = P(w, me);
    const TeamState& t = T(w, k);
    if (static_cast<std::uint32_t>(t.playerCount) < 2) return 0;
    *lead = 0;
    const std::int32_t ang = D.ang, dist = D.dist;
    int far = aiD32(0x59fc48) > dist ? 1 : 0;
    std::int32_t ld = (dist >> 3) + (dist >> 6);
    if (ld < 0xe4) ld = 0xe4;
    *lead = far ? 0 : ld;
    const PlayerState& m = P(w, mate);
    if (m.flags22c & 1) return 0;
    if (Player_attr(w, mate, 0x40) & 1) return 0;
    if (dist < aiD32(0x59fca0)) return 0;
    if (w.ball.posI.y > 0x72 && dist > aiD32(0x59fc4c)) return 0;
    if (w.match.phase == 1 && dist > 0x4b0) return 0;
    const int sideOK = s01(target.z) == t.side ? 1 : (Pitch_inPenaltyArea(w, target) ? 0 : 1);
    const TeamState& o = w.teams[static_cast<std::size_t>(t.opponent)];
    for (int i = 0; i < o.playerCount; ++i) {
        const int q = o.players[i];
        const std::int32_t d = Player_cachedDistance(w, me, q);
        if (d > dist + 0xf0) continue;
        std::int32_t wd = d >> 3;
        if (wd < aiD32(0x59fc58)) wd = aiD32(0x59fc58);
        else if (wd > aiD32(0x59fc54)) wd = aiD32(0x59fc54);
        const Angle a = Player_cachedAngle(w, me, q);
        std::int32_t s = Math_asinRatio(wd, d >> 1);
        if (s < aiD32(0x59fc88)) s = aiD32(0x59fc88);
        else if (s > aiD32(0x59fc8c)) s = aiD32(0x59fc8c);
        if (wrapAbs(a - ang) >= s) continue;
        const std::int32_t e = dist - d;
        if (e < 0) continue;
        if (dist < aiD32(0x59fc50)) continue;
        if (d < 0xf0) return 0;
        if (e < 0xf0 && sideOK != 0) return 0;
        *lead = ld;
        far = 0;
        break;
    }
    const int o2 = Team_nearestPlayer_4523E4(w, target, t.opponent, 0xff, nullptr);
    std::int32_t tm = runFrames_545F54(w, mate, target);
    std::int32_t to = o2 == -1 ? tm * 4 : runFrames_545F54(w, o2, target);
    const std::int32_t tb = ballFrames_545FB0(dist, far);
    if (*lead > 0x72) return openLofted_545FBC(tm, to, tb);
    if (to < tb) to = tb;
    if (tm < tb) tm = tb;
    return openGround_546060(to - tm, pastOwnThird(w, mate) ? 1 : 0);
}

// 0x545A6C / 0x5467BC (EDX angle, EBX mid, ECX far): 100 within 0x100 of the facing, `far` from 0x156.
std::int32_t facingFactor_545A6C(const World& w, int me, Angle ang, std::int32_t mid, std::int32_t farV) {
    const std::int32_t d = wrapAbs(ang - P(w, me).facing);
    if (d < aiD32(0x59fc60)) return 0x64;
    return d >= aiD32(0x59fc64) ? farV : mid;
}

// 0x545AAC (EDX receiver, EBX pass distance, ECX lead): positional bonus by formation style (team +0x210)
// from tables 0x59FD59.. and 0x59FD68 + zone.
std::int32_t posBonus_545AAC(const World& w, int me, int mate, std::int32_t dist, std::int32_t lead) {
    if (mate == -1) return 0;
    const PlayerState& k = P(w, me);
    const PlayerState& m = P(w, mate);
    const std::int32_t mz = iabs(m.posI.z);
    const std::int32_t c30 = w.pitch.fifthLength > 0x3c0 ? 0x3c0 : w.pitch.fifthLength;
    const std::int32_t c38 = w.pitch.thirdLength > 0x690 ? 0x690 : w.pitch.thirdLength;
    const std::int32_t c2c = w.pitch.halfLength > 0xa50 ? 0xa50 : w.pitch.halfLength;
    const std::uint32_t fs = static_cast<std::uint32_t>(T(w, k).f210);
    const std::int32_t v10 = aiD8(0x59fd59 + fs);
    std::int32_t v28 = aiD8(0x59fd5c + fs), v34 = aiD8(0x59fd62 + fs), v5f = aiD8(0x59fd5f + fs);
    const int mside = sideOf(w, mate);
    if (s01(m.posI.z - w.pitch.thirdLineZ[mside]) == mside) {
        v28 *= 2;
        v5f = aiD32(0x59fc68);
        v34 = aiD32(0x59fc6c);
    } else {
        const bool own = s01(m.posI.z) == mside;
        const int kside = T(w, k).side;
        if (own || s01(k.posI.z + w.pitch.thirdLineZ[kside]) != kside) { // 0x545C5B
            if (dist > c38) {
                v5f = 0;
                v34 = 0;
            } else {
                v5f = aiD32(0x59fc70);
                v34 = aiD32(0x59fc74);
            }
        }
    }
    std::int32_t add;
    if (dist < 0xc0) add = v10;
    else if (dist < c30) add = v28;
    else if (dist < c38) add = lead > 0x72 ? v5f >> 1 : v5f;
    else if (dist < c2c) add = v34;
    else add = 0;
    std::uint32_t idx = 0x59fd68;
    if (iabs(m.posI.x) < w.pitch.quarterWidth) idx += 0xc;
    if (s01(m.posI.z) == mside) {
        if (mz < w.pitch.quarterLength) idx += 3;
    } else {
        idx += 6;
        if (mz > w.pitch.quarterLength) idx += 3;
    }
    return aiD8(idx + fs) + add;
}

// 0x545D5C (EDX receiver, EBX pass kind): mentality (team +0x208) bonus per kind (tables 0x59FD4A /
// 0x59FD4F / 0x59FD54); kind 0 adds box / wide-attack bonuses. Draws one random number.
std::int32_t mentalityBonus_545D5C(World& w, int me, int mate, int kind) {
    if (mate == -1) return 0;
    const PlayerState& k = P(w, me);
    const PlayerState& m = P(w, mate);
    const std::uint32_t mt = static_cast<std::uint32_t>(T(w, k).mentality);
    const std::int32_t v1 = aiD8(0x59fd4f + mt);
    std::int32_t v0 = aiD8(0x59fd4a + mt), v2 = aiD8(0x59fd54 + mt);
    const int mside = sideOf(w, mate);
    if (s01(m.posI.z) != mside && Pitch_inPenaltyArea(w, m.posI)) {
        v0 += aiD32(0x59fc78);
        if (iabs(m.posI.x) < w.pitch.goalHalfWidth * 3) v0 += aiD32(0x59fc7c);
    }
    const auto r = static_cast<std::int32_t>(w.rng.next() % static_cast<std::uint32_t>(w.pitch.halfPenDepth >> 4));
    const std::int32_t zl = w.pitch.halfLength - w.pitch.penAreaDepth + (r << 4);
    const std::int32_t xl = w.pitch.halfWidth - w.pitch.penAreaHalfWidth;
    const int kside = T(w, k).side;
    if (s01(k.posI.z) != kside && iabs(k.posI.z) > zl && iabs(k.posI.x) > xl) v0 += aiD32(0x59fc80);
    if (s01(m.posI.z) == mside && s01(k.posI.z + w.pitch.thirdLineZ[kside]) != kside) v2 = 0;
    const std::int32_t v[3] = {v0, v1, v2};
    return v[kind];
}

// 0x5460CC (EAX kicker, EDX receiver, EBX target, ECX facing mid, stack facing far, attack angle, rec,
// distance out; ret 0x10): kind, openness and facing of one pass; score = open * facing / 100, x4.
void evalPass_5460CC(World& w, int me, int mate, const Vec3& target, std::int32_t mid, std::int32_t farV, Angle dirAng,
                     PassRec& rec, DistAng& D) {
    const PlayerState& k = P(w, me);
    const OctDist od = octDistance(k.posI, target);
    D = {od.dist, od.dx, od.dz, atan2i(od.dx, od.dz)};
    rec.target = target;
    const std::int32_t dAng = wrapAbs(D.ang - dirAng);
    const int side = T(w, k).side;
    const std::int32_t tz = side == 1 ? rec.target.z : -rec.target.z;
    const std::int32_t mz = side == 1 ? k.posI.z : -k.posI.z;
    rec.kind = passKind_5455C0(w, dAng, mz, tz, rec.target.x, D.dist);
    if (w.pitch.quarterLength < D.dist && rec.kind == 2) {
        rec.score = 0;
        return;
    }
    rec.open = openness_5456EC(w, me, mate, rec.target, D, &rec.r10);
    const PlayerState& m = P(w, mate);
    if (s01(m.posI.z) != sideOf(w, mate) && Pitch_inPenaltyArea(w, m.posI) && rec.open > 0 && rec.open < 0x32) rec.open = 0x32;
    rec.facing = facingFactor_545A6C(w, me, D.ang, mid, farV);
    const std::int32_t s = fx(static_cast<std::int32_t>(fxMulU(rec.open, rec.facing)), 0x28f);
    rec.score = ((s + 2) >> 2) << 2;
}

// 0x54628C (EAX kicker, EDX 11 records, EBX best[3][2] out, ECX {best, best long} out, stack kind to
// skip; ret 4): evaluate every outfield team-mate (alternate calls use his run target or his position
// shifted by distBall/4, flag 0x65F244). Per kind: best score and best with bonuses > 0x8C.
void evalPasses_54628C(World& w, int me, PassRec* recs, std::int32_t best[3][2], std::int32_t out[2], int skipKind) {
    PlayerState& k = P(w, me);
    TeamState& t = T(w, k);
    const Angle dirAng = t.side == 1 ? 0x200 : 0;
    const int a31 = Player_attr(w, me, 0x31);
    const std::int32_t mid = rateTab(0x590400, a31), farV = rateTab(0x590450, a31);
    std::int32_t top[3][2];
    for (int i = 0; i < 3; ++i) {
        top[i][0] = top[i][1] = -1;
        best[i][0] = best[i][1] = 0;
    }
    out[0] = out[1] = 0;
    std::int32_t best1 = -1, best2 = -1;
    const int n = t.playerCount;
    for (int i = 0; i < n; ++i) recs[i] = PassRec{};
    MatchState& m = w.match;
    m.f244 = (m.f244 & ~0xff) | ((m.f244 ^ 1) & 0xff); // byte 0x65F244 ^= 1
    for (int i = 0; i < n; ++i) {
        const int q = t.players[i];
        PassRec& r = recs[i];
        PlayerState& pl = P(w, q);
        if (pl.slot == 0 || q == me) continue;
        if (iabs(pl.posI.x) > w.pitch.halfWidth || iabs(pl.posI.z) > w.pitch.halfLength) continue;
        DistAng D{};
        if (m.f244 != 0) {
            evalPass_5460CC(w, me, q, pl.runTarget, mid, farV, dirAng, r, D);
        } else {
            const std::int32_t lead = pl.distBall >> 2;
            // y: stale stack word in the exe ([esp+0x2C]); never read for this target
            const Vec3 tg{pl.posI.x, 0, pl.posI.z + (t.side == 1 ? -lead : lead)};
            evalPass_5460CC(w, me, q, tg, mid, farV, dirAng, r, D);
        }
        if (r.score == 0) continue;
        r.r1c = posBonus_545AAC(w, me, q, D.dist, r.r10);
        r.r20 = mentalityBonus_545D5C(w, me, q, r.kind);
        const std::int32_t bonus = r.r1c + r.r20;
        r.score += bonus >> 3;
        if (r.kind == skipKind) continue;
        std::int32_t* L = top[r.kind];
        if (r.score >= L[0]) {
            L[0] = r.score;
            best[r.kind][0] = i;
            if (r.score >= best1) {
                best1 = r.score;
                out[0] = i;
            }
        }
        if (bonus > aiD32(0x59fc84) && r.score >= L[1]) {
            L[1] = r.score;
            best[r.kind][1] = i;
            if (r.score >= best2) {
                best2 = r.score;
                out[1] = i;
            }
        }
    }
}

// 0x4D1730 (EAX facing, EDX angle): angle more than 0x180 off the facing.
bool behind_4D1730(Angle facing, Angle a) { return wrapAbs(a - facing) > 0x180; }

// 0x546AD8 (EAX angle off, EDX player cone, EBX keeper cone): shot chance band 20..100 past the keeper.
std::int32_t keeperBand_546AD8(std::int32_t da, std::int32_t s, std::int32_t ks) {
    const std::int32_t q = (ks - s) >> 2;
    if (da >= ks) return 0x64;
    if (da <= s) return 0x14;
    if (da >= s + q * 3) return 0x50;
    if (da < s + q * 2) return 0x28;
    return 0x3c;
}

// 0x5469CC (EDX nearest opponent, EBX shot angle): the opponent cannot close the shot down (too far
// for his speed/turn, or off the line); 0 without an opponent.
bool shotClear_5469CC(World& w, int me, int o, Angle ang) {
    if (o == -1) return false;
    const PlayerState& k = P(w, me);
    const PlayerState& q = P(w, o);
    const Angle a = Player_cachedAngle(w, me, o);
    const std::int32_t d1 = wrapAbs(ang - k.facing);
    const std::int32_t d2 = wrapAbs(a - q.facingDesired);
    const Fixed sc = q.speedCap;
    const std::uint32_t sh = static_cast<std::uint32_t>(sc >> 17) & 31;
    const std::int32_t turn = static_cast<std::int32_t>((static_cast<std::uint32_t>(d2) << sh) << 16) >> 6;
    std::int32_t r = (fx(sc - turn, d1) >> 5) + aiD32(0x59fd00);
    if (s01(k.posI.z) == T(w, k).side && Pitch_inPenaltyArea(w, k.posI)) r += aiD32(0x59fd08);
    if (static_cast<std::uint32_t>(Player_cachedDistance(w, me, o)) >= static_cast<std::uint32_t>(r)) return true;
    return wrapAbs(a - ang) >= aiD32(0x59fd04);
}

// 0x546540 (EDX target (y may be set), EBX distance/angle, ECX loft out): shot chance at one goal point:
// outfield blockers on the line veto; the keeper's cone sets the band (0x546AD8) and may lift or chip.
std::int32_t evalShot_546540(World& w, int me, Vec3& target, const DistAng& D, std::int32_t* loft) {
    const PlayerState& k = P(w, me);
    const Angle ang = D.ang;
    const std::int32_t dist = D.dist;
    const std::int32_t c4 = aiD32(0x59fcc0), c8 = aiD32(0x59fcc4), cone = aiD32(0x59fcdc);
    if (c4 < dist) return 0;
    const int opp = T(w, k).opponent;
    const int o = Team_nearestPlayer_4523E4(w, k.posI, opp, 0, nullptr);
    if (behind_4D1730(k.facing, ang) && !shotClear_5469CC(w, me, o, ang)) return 0;
    const TeamState& ot = w.teams[static_cast<std::size_t>(opp)];
    std::int32_t h = w.pitch.goalHeight - 0x40 + static_cast<std::int32_t>(w.rng.next() & 0xf);
    if ((dist >> 1) < h) h = dist >> 1;
    std::int32_t res = 0x64;
    for (int i = 0; i < ot.playerCount; ++i) {
        const int q = ot.players[i];
        const std::int32_t d = Player_cachedDistance(w, me, q);
        const Angle a = Player_cachedAngle(w, me, q);
        std::int32_t s = Math_asinRatio(0x30, d >> 1);
        const std::int32_t da = wrapAbs(a - ang);
        if (P(w, q).slot != 0) {
            if (s > cone) s = cone;
            if (da < s) return 0;
            continue;
        }
        const std::int32_t ks = Math_asinRatio(aiD32(0x59fca8), d >> 1);
        std::int32_t kr = aiD32(0x59fc5c);
        res = keeperBand_546AD8(da, s, ks);
        if (ot.flags & 1) kr += 0x60;
        if (d < kr && (w.rng.next() & 3)) {
            if (w.rng.next() & 1) {
                *loft = 0;
                target.y = 0;
            } else {
                target.y = h;
            }
            continue;
        }
        if (res > 0x1e) continue;
        target.y = h;
        if (c8 < dist && d < (dist >> 1)) {
            *loft = (dist >> 4) + 0xcd;
            res *= 2;
        }
    }
    if ((c4 >> 1) < dist) res -= res >> 2;
    return res;
}

// 0x5467FC (EDX distance, EBX loft): distance bonus by mentality (tables 0x59FD80..0x59FD94), with the
// attr 0x38 range shift; + 0x59FCE0 inside the opponents' box.
std::int32_t shotDistBonus_5467FC(const World& w, int me, std::int32_t dist, std::int32_t loft) {
    const PlayerState& k = P(w, me);
    dist += 0x80 - (static_cast<std::int32_t>(Player_attr(w, me, 0x38)) << 4);
    const std::uint32_t mt = static_cast<std::uint32_t>(T(w, k).mentality);
    std::int32_t v;
    if (dist < aiD32(0x59fcb0)) v = aiD8(0x59fd80 + mt);
    else if (dist < aiD32(0x59fcb4)) v = aiD8(0x59fd85 + mt);
    else if (dist < aiD32(0x59fcb8)) v = aiD8(0x59fd8a + mt);
    else if (dist < aiD32(0x59fcbc)) {
        v = aiD8(0x59fd8f + mt);
        if (loft >= 0xcd) v += aiD32(0x59fce4);
    } else v = aiD8(0x59fd94 + mt);
    if (s01(k.posI.z) != T(w, k).side && Pitch_inPenaltyArea(w, k.posI)) v += aiD32(0x59fce0);
    return v;
}

// 0x5468D8: shooting-angle bonus: the goal mouth's angular width (tables 0x59FD99..0x59FDAD).
std::int32_t shotAngleBonus_5468D8(const World& w, int me) {
    const PlayerState& k = P(w, me);
    std::int32_t gx = w.pitch.goalHalfWidth, gz = -w.pitch.halfLength;
    if (T(w, k).side == 0) {
        gx = -gx;
        gz = -gz;
    }
    const Angle a1 = atan2i(gx - k.posI.x, gz - k.posI.z);
    const Angle a2 = atan2i(-gx - k.posI.x, gz - k.posI.z);
    const std::int32_t d = (a2 - a1) & 0x3ff;
    const std::uint32_t mt = static_cast<std::uint32_t>(T(w, k).mentality);
    if (d < aiD32(0x59fccc)) return aiD8(0x59fd99 + mt);
    if (d < aiD32(0x59fcd0)) return aiD8(0x59fd9e + mt);
    if (d < aiD32(0x59fcd4)) return aiD8(0x59fda3 + mt);
    if (d < aiD32(0x59fcd8)) return aiD8(0x59fda8 + mt);
    return aiD8(0x59fdad + mt);
}

// 0x546B2C (EDX 5 records): evaluate 5 points across the goal mouth (order 0, 1, 4, 3, 2 of the
// spread, random side, x jittered by attr 0x37 spread, random height): best index.
int evalShots_546B2C(World& w, int me, PassRec* recs) {
    PlayerState& k = P(w, me);
    TeamState& t = T(w, k);
    std::int32_t bestScore = -1;
    int bestIdx = 0;
    std::int32_t c1, c2;
    if (k.role == 4) {
        c1 = c2 = 0x64;
    } else {
        const int a31 = Player_attr(w, me, 0x31);
        c1 = rateTab(0x590400, a31);
        c2 = rateTab(0x590450, a31);
    }
    const std::uint32_t sp =
        static_cast<std::uint32_t>((rateTab(0x5904f0, Player_attr(w, me, 0x37)) * 2) >> (t.ctrl1f4 & 31)) + 0x10;
    const std::int32_t off = static_cast<std::int32_t>(w.rng.next() % sp) - static_cast<std::int32_t>(sp >> 1);
    const std::int32_t maxDist = rateTab(0x5904a0, Player_attr(w, me, 0x38));
    std::uint32_t dg = static_cast<std::uint32_t>(octLength(k.posI.x, w.pitch.halfLength - iabs(k.posI.z))) >> 1;
    const std::uint32_t cap = static_cast<std::uint32_t>(((2 - t.ctrl1f4) << 5) + w.pitch.goalHeight);
    if (static_cast<std::int32_t>(dg) > static_cast<std::int32_t>(cap)) dg = cap;
    Vec3 pts[5];
    const std::int32_t y = static_cast<std::int32_t>(w.rng.next() % dg);
    const std::int32_t z = -w.pitch.goalLineZ[t.side];
    pts[0] = {w.pitch.goalHalfWidth - 0x18, y, z};
    std::int32_t x = w.pitch.goalHalfWidth, step = w.pitch.goalHalfWidth >> 1;
    if (w.rng.next() & 1) step = -step;
    else x = -x;
    int order[5] = {0, 1, 2, 3, 4};
    for (int c = 1; c < 5; ++c) {
        x += step;
        pts[c] = {x, y, z};
    }
    pts[4].x = -w.pitch.goalHalfWidth + 0x10;
    std::swap(order[2], order[4]);
    for (int j = 0; j < 5; ++j) {
        PassRec& r = recs[j];
        r.score = 0;
        r.target = pts[order[j]];
        const OctDist od = octDistance(k.posI, r.target);
        DistAng D{od.dist, od.dx, od.dz, atan2i(od.dx, od.dz)};
        r.target.x += off;
        if (maxDist < D.dist) D.dist = maxDist;
        std::int32_t pr[4];
        Math_polarRecord(D.dist, D.ang, pr); // rewrites the distance record in place (EBX = &D)
        D = {pr[0], pr[1], pr[2], pr[3]};
        r.r10 = -1;
        r.open = evalShot_546540(w, me, r.target, D, &r.r10);
        if (r.open == 0) continue;
        r.facing = facingFactor_545A6C(w, me, D.ang, c1, c2);
        r.r1c = shotDistBonus_5467FC(w, me, D.dist, r.r10);
        r.r20 = shotAngleBonus_5468D8(w, me);
        r.score = ((fx(static_cast<std::int32_t>(fxMulU(r.open, r.facing)), 0x28f) + 3) >> 2) << 2;
        if (r.score != 0) r.score += (r.r1c + r.r20) >> 1;
        if (r.score >= bestScore) {
            bestScore = r.score;
            bestIdx = j;
        }
    }
    return bestIdx;
}


// Mentality pass thresholds (exe bytes 0x58B68C / 0x58B691 / 0x58B696 / 0x58B69B by team +0x208) and the
// difficulty offset 0x58B6A0[team +0x1F4].
constexpr std::uint8_t kShotThr[5] = {0x78, 0x82, 0x91, 0x9b, 0xa0};  // 0x58B68C
constexpr std::uint8_t kPassThr91[5] = {0x55, 0x55, 0x55, 0x55, 0x55}; // 0x58B691
constexpr std::uint8_t kPassThr96[5] = {0x46, 0x46, 0x46, 0x46, 0x46}; // 0x58B696
constexpr std::uint8_t kPassThr9B[5] = {0x3c, 0x3c, 0x3c, 0x3c, 0x3c}; // 0x58B69B
constexpr std::int32_t kDiffAdj[3] = {0x1e, 0x1e, 0};                  // 0x58B6A0
constexpr std::int32_t kSectorReach[8] = {0x180, 0x150, 0x120, 0x120, 0xc0, 0x120, 0x120, 0x150}; // 0x58B6AC
[[maybe_unused]] constexpr std::int32_t kSectorClose[8] = {0x120, 0x120, 0xf0, 0xf0, 0xc0, 0xf0, 0xf0, 0x120}; // 0x58B6CC (WC98: superseded by reach>>1 gate)
constexpr std::int32_t kDribbleBudget[20] = {1, 1, 2, 3, 3, 4, 4, 5, 6, 7, 8, 9, 0xb, 0xd, 0xf, 0x12, 0x13, 0x14, 0x15, 0x16}; // 0x590630

// Kick spec lists 0x58B1D8 (16 bytes: u8 kind, ..., +0xC u32 anim class) selected by 0x4D175C via the
// pointer tables 0x58B288 / 0x58B298 / 0x58B2E0 / 0x58B328.
constexpr std::uint8_t kKickList[0xb0] = {
    11, 4, 96, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 4, 96, 0, 0, 3, 114, 0,
    16, 0, 0, 0, 3, 0, 0, 0, 6, 8, 240, 0, 64, 11, 19, 0, 64, 0, 0, 0, 4, 0, 0, 0,
    5, 4, 224, 1, 160, 5, 95, 0, 32, 0, 0, 0, 5, 0, 0, 0, 4, 6, 240, 0, 208, 2, 135, 0,
    64, 0, 0, 0, 6, 0, 0, 0, 1, 8, 224, 1, 64, 11, 133, 0, 10, 0, 0, 0, 1, 0, 0, 0,
    2, 6, 192, 0, 160, 5, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 3, 8, 224, 1, 112, 8, 19, 0,
    64, 0, 0, 0, 2, 0, 0, 0, 1, 8, 224, 1, 224, 16, 133, 0, 10, 0, 0, 0, 1, 0, 0, 0,
    2, 8, 192, 0, 160, 5, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 3, 8, 224, 1, 96, 9, 19, 0,
    64, 0, 0, 0, 1, 0, 0, 0,
};
struct KickSpec {
    std::int32_t kind, cls;
};
KickSpec kickSpec(std::uint32_t a) {
    const std::uint8_t* q = kKickList + (a - 0x58b1d8);
    return {q[0], static_cast<std::int32_t>(q[0xc] | q[0xd] << 8 | q[0xe] << 16 | static_cast<std::uint32_t>(q[0xf]) << 24)};
}
constexpr std::uint32_t kKickTab[40] = {
    0x58b1d8, 0x58b1d8, 0x58b1e8, 0x58b228, 0x58b228, 0x58b228, 0x58b1f8, 0x58b1f8, 0x58b218, 0x58b208,
    0x58b238, 0x58b238, 0x58b1f8, 0x58b1f8, 0x58b218, 0x58b208, 0x58b248, 0x58b248, 0x58b1f8, 0x58b1f8,
    0x58b218, 0x58b208, 0x58b258, 0x58b258, 0x58b258, 0x58b258, 0x58b258, 0x58b258, 0x58b268, 0x58b268,
    0x58b268, 0x58b268, 0x58b268, 0x58b268, 0x58b278, 0x58b268, 0x58b278, 0x58b268, 0x58b278, 0x58b268};
constexpr std::uint32_t kKickTabAir[6] = {0x58b218, 0x58b208, 0x58b218, 0x58b208, 0x58b218, 0x58b208}; // 0x58B328 (kicker off the ground)
// 0x4D175C (EAX facing, EDX kicker height (player +0xD8: airborne -> table 0x58B328), EBX ball height,
// ECX goalkeeper table, stack class flags, x, z; ret 0xC): kick spec by height band, class and whether
// the target is behind.
std::uint32_t kickList_4D175C(Angle facing, std::int32_t kickerY, std::int32_t h, std::int32_t alt, std::int32_t f,
                              std::int32_t x, std::int32_t z) {
    const int c = (f & 0x40) ? 2 : ((f & 0x10) ? 0 : 1);
    int back = 0;
    if (x != 0 || z != 0) back = behind_4D1730(facing, atan2i(x, z)) ? 1 : 0;
    if (kickerY != 0) return kKickTabAir[c * 2 + back];
    if (f == 0x30) return kKickTab[back + 2]; // 0x58B288 + 8
    const int hb = h < 0x39 ? 0 : (h < 0x72 ? 1 : 2);
    const std::uint32_t base = alt != 0 ? (0x58b2e0 - 0x58b288) / 4 : (0x58b298 - 0x58b288) / 4;
    return kKickTab[base + static_cast<std::uint32_t>(back + c * 6 + hb * 2)];
}

// 0x4D1838 (EAX kick request, EDX/EBX outs): angular window around the kick heading for the side class
// (+0x51 0..3; others keep the caller's ECX/ESI, never seen), mirrored when target-left (+0x54 & 2).
void kickWindow_4D1838(const KickRequest& k, std::int32_t* lo, std::int32_t* hi) {
    std::int32_t a = 0, b = 0;
    switch (k.side) {
    case 0: a = -0x52; b = 0x52; break;
    case 1: a = 0x55; b = 0xc4; break;
    case 2: a = 0xc7; b = 0x17d; break;
    case 3: a = 0x17d; b = -0x17d; break;
    default: break;
    }
    if (k.flags & 2) {
        a = -a;
        b = -b;
    }
    auto wr = [](std::int32_t v) {
        v &= 0x3ff;
        return v > 0x200 ? v - 0x400 : v;
    };
    a = wr(a + k.heading);
    b = wr(b + k.heading);
    if (a < b) {
        *lo = a;
        *hi = b;
    } else {
        *lo = b;
        *hi = a;
    }
}

// 0x4E0A20 (EDX opponent, EBX distance, ECX relative heading, stack relative facing; ret 4): random skill
// move from table 0x58B804 (17 x {anim, heading range, distance range, facing range, condition bits})
// whose conditions hold; -1 none (always on the lowest difficulty).
struct SkillPick {
    std::int32_t anim, h0, h1, d0, d1, f0, f1, bits;
};
constexpr SkillPick kSkillPick[17] = {
    {0x6d, -0x80, -0x39, 0x40, 0xf0, 0x80, 0x180, 0x2d},   {0x6e, 0x39, 0x80, 0x40, 0xf0, -0x180, -0x80, 0x2b},
    {0x70, -0x80, -0x39, 0x40, 0xf0, 0x80, 0x180, 0x2b},   {0x6f, 0x39, 0x80, 0x40, 0xf0, -0x180, -0x80, 0x2d},
    {0x6b, -0x2b, 0x2b, 0x40, 0xb0, 0x1d5, -0x1d5, 0},     {0x6a, -0x2b, 0x2b, 0x40, 0xb0, 0x1d5, -0x1d5, 0},
    {0x75, -0x64, 0x64, 0x50, 0x120, -0x200, -0x18e, 8},   {0x74, -0x64, 0x64, 0x50, 0x120, 0x18e, 0x200, 8},
    {0x7d, -0x39, 0x39, 0x80, 0xf0, 0x1d5, -0x1d5, 8},     {0x7c, -0x39, 0x39, 0x80, 0xf0, 0x1d5, -0x1d5, 8},
    {0x77, 0, 0x55, 0x60, 0xf0, -0x200, -0x172, 0x20},      {0x76, -0x55, 0, 0x60, 0xf0, 0x172, 0x200, 0x20},
    {0x7e, -0x80, -0xe, 0x30, 0xc0, 0x155, 0x200, 0x68},   {0x7f, 0xe, 0x80, 0x30, 0xc0, -0x200, -0x155, 0x68},
    {0x1b5, -0x100, -0x80, 0x20, 0xa0, -0x200, 0x200, 0x10}, {0x1b4, 0x80, 0x100, 0x20, 0xa0, -0x200, 0x200, 0x10},
    {0x73, -0x180, 0x180, 0x20, 0x120, -0x200, 0x200, 0x30}};
std::int32_t pickSkill_4E0A20(World& w, int pid, int opp, std::int32_t dist, std::int32_t rel, std::int32_t rel2) {
    const PlayerState& p = P(w, pid);
    const int side = T(w, p).side;
    std::int32_t r = -1;
    std::uint32_t cnt = 0;
    if (T(w, p).ctrl1f4 == 0) return -1;
    auto inRange = [](std::int32_t a, std::int32_t lo, std::int32_t hi) { return lo < hi ? (a >= lo && a <= hi) : (a > lo || a < hi); };
    for (const SkillPick& e : kSkillPick) {
        if (dist < e.d0 || dist > e.d1) continue;
        if (!inRange(rel, e.h0, e.h1) || !inRange(rel2, e.f0, e.f1)) continue;
        bool fail = false;
        std::uint32_t bits = static_cast<std::uint32_t>(e.bits);
        for (int i = 0; bits != 0 && !fail; ++i, bits >>= 1) {
            if (!(bits & 1) || i > 7) continue;
            switch (i) {
            case 0: fail = s01(p.posI.z) == side && w.pitch.halfLength * 2 > iabs(p.posI.z) * 3; break;
            case 1: fail = (p.posI.x < 0 ? 1 : 0) != (side == 1 ? 1 : 0); break;
            case 2: fail = (p.posI.x > 0 ? 1 : 0) != (side == 1 ? 1 : 0); break;
            case 3: fail = (iabs(p.facing) > 0x100 ? 1 : 0) != (side == 1 ? 1 : 0); break;
            case 4: fail = !Player_isState100Kind1(w, opp); break;
            case 5: fail = w.pitch.halfWidth * 3 < iabs(p.posI.x) * 4; break;
            case 6: fail = iabs(p.posI.x) * 2 < w.pitch.halfWidth; break;
            default: fail = w.rng.next() % 100 >= 5; break;
            }
        }
        if (fail) continue;
        ++cnt;
        if (w.rng.next() % cnt == 0) r = e.anim;
    }
    return r;
}

// 0x4DC0D8 (EDX opponent, EBX his distance, ECX his angle, stack keep-away flag, out polar record; ret 8):
// dribble heading 0x18 long: toward goal (or the wing, team flag 0x200) bent away from the opponent; may
// pick a skill move (0x4E0A20 -> +0x1E8/+0x1EA) or turn along the touch/goal line and sprint.
void dribbleDir_4DC0D8(World& w, int pid, int opp, std::int32_t b, Angle heading, std::int32_t keep, std::int32_t* out) {
    PlayerState& p = P(w, pid);
    const PlayerState& o = P(w, opp);
    TeamState& t = T(w, p);
    const bool wide = (t.flags >> 9) & 1;
    if (out == nullptr) return;
    const std::int32_t len = 0x18;
    const std::int32_t hw = w.pitch.halfWidth;
    const std::int32_t zz = w.pitch.quarterLength - (t.side == 1 ? p.posI.z : -p.posI.z);
    std::int32_t dx, dz;
    if (b < aiD32(0x59fd1c) && zz < 0 && iabs(p.posI.x) < w.pitch.penAreaHalfWidth) {
        dz = w.pitch.thirdLineZ[t.side] - p.posI.z;
        dx = p.posI.x >= 0 ? hw - 0xc0 : -(hw - 0xc0);
    } else {
        dz = -w.pitch.goalLineZ[t.side] - p.posI.z;
        if (wide) dx = (p.posI.x >= 0 ? hw - 0xc0 : -(hw - 0xc0)) - p.posI.x;
        else dx = -p.posI.x;
    }
    const Angle a = atan2i(dx, dz);
    std::int32_t s = Math_asinRatio(aiD32(0x59fd0c), b >> 1);
    if (b < aiD32(0x59fd10)) s = 0xe4;
    if (s < aiD32(0x59fd14)) s = aiD32(0x59fd14);
    std::int32_t d = (a - heading) & 0x3ff;
    if (d > 0x200) d -= 0x400;
    std::int32_t e;
    if (keep != 0 && iabs(d) > aiD32(0x59fd30)) {
        e = heading - 0x200;
        e = d > 0 ? e - s : e + s;
        Math_polarRecord(len, e, out);
        return;
    }
    if (iabs(d) <= aiD32(0x59fcac)) {
        e = heading;
    } else {
        if (s > 0x64) s = 0x64;
        e = a;
    }
    e = d > 0 ? e + s : e - s;
    const std::int32_t code = pickSkill_4E0A20(w, pid, opp, b, heading - p.facing, o.facing - p.facingDesired);
    p.pendingCode = (p.pendingCode & 0xffff0000u) | (static_cast<std::uint32_t>(code) & 0xffffu);
    p.pendingCode = (p.pendingCode & 0xffffu) | (static_cast<std::uint32_t>(heading) << 16);
    if (static_cast<std::int16_t>(p.pendingCode & 0xffff) != -1) {
        Math_polarRecord(len, e, out);
        return;
    }
    const Vec3& bp = w.ball.posI;
    const bool bx = iabs(bp.x) > hw - aiD32(0x59fd20);
    const bool bz = iabs(bp.z) > w.pitch.halfLength - aiD32(0x59fd20);
    auto wr = [](std::int32_t v) {
        v &= 0x3ff;
        return v > 0x200 ? v - 0x400 : v;
    };
    if (bx && !bz) {
        e = wr(e);
        if (bp.x > 0 && e > 0) e = iabs(e) >= 0x100 ? 0x200 : 0;
        else if (bp.x < 0 && e < 0) e = iabs(e) >= 0x100 ? 0 : 0x200;
    } else if (bz && !bx) {
        if (s01(p.posI.z) == t.side || iabs(bp.x) < w.pitch.goalHalfWidth) {
            e = wr(e + 0x100);
            if (bp.z > 0 && e > 0) e = iabs(e) >= 0x100 ? 0x100 : -0x100;
            else if (bp.z < 0 && e < 0) e = iabs(e) >= 0x100 ? -0x100 : 0x100;
        }
    }
    const std::int32_t ax = iabs(p.posI.x), az = iabs(p.posI.z);
    const std::int32_t da = wrapAbs(e - (t.side == 1 ? 0x200 : 0));
    const std::int32_t dh = wrapAbs(heading - e), df = wrapAbs(p.facingDesired - e);
    if (dh > 0xab && df < 0x55 && t.ctrl1f4 != 0) {
        if (s01(p.posI.z) == t.side) Player_requestSprint(w, pid);
        else if (w.pitch.halfLength - w.pitch.penAreaDepth >= az) Player_requestSprint(w, pid);
        else if (ax >= w.pitch.penAreaHalfWidth && da >= 0xab) Player_requestSprint(w, pid);
    }
    Math_polarRecord(len, e, out);
}

// 0x4DB924 (EDX vector out, EBX total out): opponents ahead within 0x55 of the line to goal (returned)
// plus near ones (< 0x120 within 0xC7, or < 0x90).
int crowd_4DB924(World& w, int pid, std::int32_t* vec, std::int32_t* total) {
    const PlayerState& p = P(w, pid);
    const TeamState& t = T(w, p);
    vec[1] = -p.posI.x;
    vec[2] = -w.pitch.goalLineZ[t.side] - p.posI.z;
    const Angle a = atan2i(vec[1], vec[2]);
    int near = 0, ahead = 0;
    const TeamState& o = w.teams[static_cast<std::size_t>(t.opponent)];
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(o.playerCount); ++i) {
        const int q = o.players[i];
        const PlayerState& oq = P(w, q);
        if (oq.slot == 0 || (oq.anim.b34[3] & 4)) continue;
        const std::int32_t da = wrapAbs(Player_cachedAngle(w, pid, q) - a);
        if (da < 0x55) {
            ++ahead;
            continue;
        }
        const std::int32_t d = Player_cachedDistance(w, pid, q);
        if ((d < 0x120 && da < 0xc7) || d < 0x90) ++near;
    }
    *total = ahead + near;
    return ahead;
}

// 0x4DB7CC (EDX vector out or null): out wide enough (team style 1: from halfWidth - thirdWidth/2, else
// quarterWidth) with a clear run down the wing (no fit opponent within half the distance, 0x2B wide).
bool wingClear_4DB7CC(World& w, int pid, std::int32_t* vec) {
    if (pid == -1) return false;
    const PlayerState& p = P(w, pid);
    const TeamState& t = T(w, p);
    const std::int32_t lim = t.f210 == 1 ? w.pitch.halfWidth - (w.pitch.thirdWidth >> 1) : w.pitch.quarterWidth;
    if (iabs(p.posI.x) < lim) return false;
    std::int32_t c = w.pitch.halfWidth - 0xc0;
    if (p.posI.x < 0) c = -c;
    const std::int32_t dx = c - p.posI.x;
    const std::int32_t dz = -w.pitch.goalLineZ[t.side] - p.posI.z;
    if (vec != nullptr) {
        vec[1] = dx;
        vec[2] = dz;
    }
    const Angle a = atan2i(dx, dz);
    const auto r = static_cast<std::uint32_t>(octLength(dx, dz)) >> 1;
    const TeamState& o = w.teams[static_cast<std::size_t>(t.opponent)];
    for (int i = 0; i < o.playerCount; ++i) {
        const int q = o.players[i];
        const PlayerState& oq = P(w, q);
        if (oq.slot == 0 || (oq.flags22c & 1) || (Player_attr(w, q, 0x40) & 1)) continue;
        if (static_cast<std::uint32_t>(Player_cachedDistance(w, pid, q)) >= r) continue;
        if (wrapAbs(Player_cachedAngle(w, pid, q) - a) < 0x2b) return false;
    }
    return true;
}

// 0x4DD324 frame (esp-relative, 0x380 bytes); 0x4DD2BC / 0x4DD070 work on it through EDX.
struct ActFrame {
    std::int32_t lo[0x34] = {}; // +0x000..+0x0CF
    PassRec recs[11];           // +0x0D0
    PassRec shots[5];           // +0x288
    const Vec3* p350 = nullptr; // +0x350 shot target
    std::int32_t budgetScratch = 0; // WC98 local_78: high-half draw budget (LAB_0050d24b [P])
    std::int32_t hi[0xb] = {};  // +0x354..+0x37F
    std::int32_t& at(int o) { return o < 0xd0 ? lo[o >> 2] : hi[(o - 0x354) >> 2]; }
    std::int32_t* ptr(int o) { return &at(o); }
    // shots[-1] (index -1 when no shot was evaluated) reads recs[10], as in the exe frame.
    PassRec& shot(int i) { return i >= 0 ? shots[i] : recs[11 + i]; }
};

// 0x4DD2BC (EDX frame): evaluate passes once (+0x14), skipping long balls from the own end; +0x10 =
// best with bonuses unless below the +0x40 threshold.
void framePasses_4DD2BC(World& w, int pid, ActFrame& F) {
    if (F.at(0x14) != 0) return;
    std::int32_t best[3][2], out[2];
    evalPasses_54628C(w, pid, F.recs, best, out, F.at(0x1c) != 0 ? 2 : 3);
    for (int k = 0; k < 3; ++k) {
        F.at(0xac + k * 8) = best[k][0];
        F.at(0xb0 + k * 8) = best[k][1];
    }
    F.at(0xc4) = out[0];
    F.at(0xc8) = out[1];
    F.at(0x10) = out[1];
    if (F.at(0x40) > F.recs[out[1]].score) F.at(0x10) = out[0];
    F.at(0x14) = 1;
}

// 0x4DD070 (EDX frame): plan the chosen kick (+0x78 kind: 1 lofted, 2 ground, 3 shot at +0x350; lift
// +0x30; vector +0x8C) through the kick spec 0x4D175C and enter state 0x300; a short pass in the own half
// of an AI team by a +0x14 == 6 player arms the give-and-go (team +0x1B8).
void frameKick_4DD070(World& w, int pid, ActFrame& F) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    if (F.at(0x78) != 3) {
        if (F.at(0x30) < 0) F.at(0x30) = 0;
        else if (F.at(0x30) > 0x640) F.at(0x30) = 0x640;
    }
    const std::int32_t k = F.at(0x78);
    const std::int32_t fr = k == 3 ? 0x40 : (k == 1 ? 0x10 : 0x20);
    Vec3 v;
    Ball_predictPos(w, 0x14, 0, &v);
    const KickSpec ks = kickSpec(kickList_4D175C(p.facing, p.posI.y, v.y, p.slot == 0 ? 1 : 0, fr, F.at(0x90), F.at(0x94)));
    F.at(0x78) = ks.kind;
    if (F.at(0x7c) != 7) F.at(0x7c) = ks.cls;
    const std::int32_t* rel = F.ptr(0x8c);
    if (F.at(0x78) != 3) {
        Kick_planToPoint(w, pid, rel, nullptr, F.at(0x30), -1, 0, F.at(0x78), F.at(0x7c), p.posI.y > 0 ? 1 : 0, 2);
    } else {
        const Vec3* tg = F.p350;
        if (F.at(0x30) > 0) {
            if (tg->y + 0x10 > F.at(0x30)) F.at(0x30) = tg->y + 0x10;
            Kick_planToPoint(w, pid, rel, tg, F.at(0x30), tg->y, 0, F.at(0x78), F.at(0x7c), 0, 3);
        } else if (F.at(0x30) == 0) {
            Kick_planToPoint(w, pid, rel, tg, F.at(0x30), tg->y, 0, 2, 0, 0, 3);
        } else {
            Kick_planToPoint(w, pid, rel, tg, F.at(0x30), tg->y, 0, F.at(0x78), F.at(0x7c), 0, 3);
        }
    }
    Player_setState(w, pid, 0x300, 0, 0, false, false);
    F.at(0x90) = iabs(F.at(0x90));
    F.at(0x94) = iabs(F.at(0x94));
    if (static_cast<std::uint32_t>(t.humans) > 0) return;
    if (F.at(0x24) != 0 && F.at(0x78) == 2 && F.at(0x8c) < 0xf0 && (p.f014 == 5 || p.f014 == 6)) t.f1b8 = pid; // WC98 f014 set {5,6} [P]
}

// Shared opening of 0x4DD324 / 0x4DF2CC: the "go long" flag from the line counts (team +0x364 vs
// opponents +0x360) with the given random draws.

} // namespace


// 0x4DC614 (EDX kick code): kick on taking the ball (keeper distribution 0x201, clearances): best pass
// over the mentality threshold, else a clearance up field (kick-off: to the nearest team-mate); state 0x300.
// 0x50C820 (WC98 FUN_0050c820, pass-12 air2 dossier): same tri-table select as FIFA-99 0x4DC614 but the
// thresholds load RAW from DAT_0058bfd1/bfd6/bfdb - the formation/difficulty base addition is GONE (pair 43).
void TeamAI_onBallWon_4DC614(World& w, int pid, int code) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    t.f1e0 = -1;
    const std::uint32_t m = static_cast<std::uint32_t>(t.mentality);
    // WC98 gate constant: player record slot +0x44 == 0x281 (FIFA-99 +0x48 == 0x201; WC98 state-id family).
    const int f201 = p.state == 0x281 ? 1 : 0;
    std::int32_t thr;
    if (s01(p.posI.z + w.pitch.thirdLineZ[t.side]) != t.side || f201) thr = kPassThr96[m];
    else thr = s01(p.posI.z - w.pitch.thirdLineZ[t.side]) == t.side ? kPassThr91[m] : kPassThr9B[m];
    if (static_cast<std::uint32_t>(t.humans) == 0 && pid == t.f1b8) t.f1b8 = -1;
    const int skip = s01(p.posI.z - w.pitch.thirdLineZ[t.side]) == t.side ? 2 : 3;
    PassRec recs[11];
    std::int32_t best[3][2], out[2];
    evalPasses_54628C(w, pid, recs, best, out, skip);
    int i = best[0][1];
    if (thr > recs[i].score) i = best[0][0];
    std::int32_t od[4] = {0, 0, 0, 0}; // [ebp-0x38]
    {
        const OctDist d = octDistance(p.posI, recs[i].target);
        od[0] = d.dist;
        od[1] = d.dx;
        od[2] = d.dz;
    }
    std::int32_t lift;
    bool clear = thr > recs[i].score || od[0] < 0x30;
    if (!clear) {
        t.f1e0 = t.players[i];
        lift = recs[i].r10;
        if (w.match.phase == 1) lift = (od[0] >> 5) + (od[0] >> 6);
    } else if (w.match.phase == 1) { // 0x4DC75E: kick-off: to the nearest team-mate
        p.flags22c |= 2;
        const int n = Team_nearestToBall_4522FC(w, p.team, 0);
        p.flags22c &= ~2u;
        if (n == -1) {
            od[1] = p.posI.x > 0 ? 0x258 : -0x258;
            od[2] = t.side == 0 ? 0x4b0 : -0x4b0;
            od[0] = octLength(od[1], od[2]);
            if (od[0] > 0x4b0) od[0] = 0x4b0;
            t.f1e0 = -1;
        } else {
            od[0] = Player_cachedDistance(w, pid, n);
            if (od[0] > 0x4b0) od[0] = 0x4b0;
            Math_polarRecord(od[0], Player_cachedAngle(w, pid, n), od);
            t.f1e0 = n;
        }
        lift = (od[0] >> 5) + (od[0] >> 6);
    } else { // 0x50C897 (WC98 FUN_0050c820): clear up field toward a random point halfLength ahead - the
        // Team_nearestPlayer target lane, the dz+(dist>>4) nudge and the >>3+>>4 lift scale are GONE:
        // offsetXZ writes dx/dz, distance flows raw to the mode gate (register args hidden [P]).
        const Angle a = (t.side == 1 ? 0x200 : 0) + 0x40 - static_cast<std::int32_t>(w.rng.next() & 0x7f);
        const Vec3 S = offsetInDirection(w.ball.posI, w.pitch.halfLength, a); // FUN_004f3590 offsetXZ (y stale)
        const OctDist d = octDistance(w.ball.posI, S);
        od[1] = d.dx;
        od[2] = d.dz;
        od[0] = octLength(d.dx, d.dz);
        t.f1e0 = -1;
        lift = od[0]; // iVar3 = offsetXZ distance return value [P]
    }
    if (f201) { // 0x50C9F0 (WC98): code-select lane only - FIFA-99's lift min-clamps (0x72 / 0x8e bump) are
        // GONE, and the distance thresholds move 0x781 -> 0x3c1, 0x2d1 -> 0x1e1.
        if (od[0] >= 0x3c1) {
            code = 8;
        } else if (od[0] >= 0x1e1) {
            code = 9;
        } else {
            code = 0xa;
        }
    }
    std::int32_t a2;
    if (w.match.phase == 1) {
        Motion_faceVector(w, pid, od[1], od[2]);
        a2 = 9;
    } else {
        a2 = lift != 0 ? 1 : 2;
    }
    if (lift == 0) lift = od[0] >> 3;
    if (w.match.phase == 1) lift += 0xab;
    Kick_planToPoint(w, pid, od, nullptr, lift, -1, 0, a2, code, p.posI.y > 0 ? 1 : 0, 2);
    Player_setState(w, pid, 0x300, 0, 0, false, false);
}

// 0x4DCAC8: kick plan for a ball in the air at the AI player (header/volley): the one-touch player (or a
// player in the opponents' box) shoots at the best goal point; otherwise the best pass, or a clearance
// toward goal from the y=142 point to the nearest team-mate. Kick code 6/4 (spec 2).
void TeamAI_planAirKick_4DCAC8(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    const std::uint32_t m = static_cast<std::uint32_t>(t.mentality);
    // WC98 FUN_0050cc70 (pass-12 air2): thresholds RAW from DAT_0058bfd6/bfdb - the FIFA-99 formation/
    // difficulty base addition is GONE.
    const std::int32_t A = kPassThr96[m], B = kPassThr9B[m];
    t.f1e0 = -1;
    PassRec recs[11], shots[5];
    std::int32_t best[3][2], out[2];
    std::int32_t rel[4] = {0, 0, 0, 0}; // [esp+0x298]
    evalPasses_54628C(w, pid, recs, best, out, 2);
    const int si = evalShots_546B2C(w, pid, shots);
    int i = out[1];
    if (recs[i].score < A) i = out[0];
    const Vec3* tg = nullptr;
    std::int32_t lift;
    if (pid == t.oneTouch || (s01(p.posI.z) != t.side && Pitch_inPenaltyArea(w, p.posI))) {
        tg = &shots[si].target;
        lift = shots[si].r10;
    } else {
        if (A <= recs[best[0][1]].score) i = best[0][1];
        if (B > recs[i].score) { // 0x50CCF3 (WC98 FUN_0050cc70): clear toward goal from the y=142 descent
            // point - the Team_nearestPlayer target lane, dz+(dist>>4) nudge and (rel[0]>>3)+0x8e lift are
            // GONE: atan2 + offsetXZ write rel dx/dz only; the receiver stays cleared at entry (-1), and the
            // kick power is derived from the rel vector by Kick_plan (register args hidden [P]).
            rel[1] = -p.posI.x;
            rel[2] = -w.pitch.goalLineZ[t.side] - p.posI.z;
            const Vec3 S = offsetInDirection(w.ball.cross142Pos, aiD32(0x59fc4c), atan2i(rel[1], rel[2])); // y stale
            const OctDist d = octDistance(w.ball.cross142Pos, S);
            rel[1] = d.dx;
            rel[2] = d.dz;
            rel[0] = octLength(d.dx, d.dz); // offsetXZ distance output [P]
            t.f1e0 = -1;
            lift = 0;
        } else {
            const OctDist d = octDistance(p.posI, recs[i].target);
            rel[0] = d.dist;
            rel[1] = d.dx;
            rel[2] = d.dz;
            t.f1e0 = t.players[i];
            lift = recs[i].r10 != 0 ? (rel[0] >> 3) + 0x8e : 0;
        }
    }
    const int lo = w.ball.consumed + 0x19 < w.ball.cross142Frame ? 1 : 0;
    if (tg != nullptr) {
        lift = lift >= -1 ? (lift > 0x640 ? 0x640 : lift) : -1;
        rel[1] = tg->x - w.ball.cross142Pos.x;
        rel[2] = tg->z - w.ball.cross142Pos.z;
        Kick_planToPoint(w, pid, rel, tg, lift, tg->y, 0, 4, 6, lo, 2);
    } else {
        lift = lift < 0 ? 0 : (lift > 0x640 ? 0x640 : lift);
        Kick_planToPoint(w, pid, rel, nullptr, lift, -1, 0, 4, 6, lo, 2);
    }
}

// 0x4DCE94: at contact, re-aim an AI shot/volley (modes 4/6) at the receiver (team +0x1E0) inside the
// kick's angular window (0x4D1838): lifted kicks keep their lift (/4), ground ones become a driven kick
// (Kick_plan mode 5) clamped to 0x50..0x320.
void TeamAI_replanShot_4DCE94(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    KickRequest& K = p.kick;
    if (t.f1e0 == -1) return;
    if (K.mode != 4 && K.mode != 6) return;
    std::int32_t lo = 0, hi = 0;
    kickWindow_4D1838(K, &lo, &hi);
    Vec3 tg = P(w, t.f1e0).posI; // [esp+0x10]
    const std::int32_t dd = Player_cachedDistance(w, pid, t.f1e0);
    tg.z += static_cast<std::int32_t>(static_cast<std::uint32_t>(dd) >> 4);
    std::int32_t od[4] = {0, 0, 0, 0}; // [esp]: octDistance writes 3 dwords; [esp+0xC] stale
    {
        const OctDist d = octDistance(p.posI, tg);
        od[0] = d.dist;
        od[1] = d.dx;
        od[2] = d.dz;
    }
    std::int32_t h = atan2i(od[1], od[2]) & 0x3ff;
    if (h > 0x200) h -= 0x400;
    int clamped = 0;
    if (h < lo) {
        clamped = 1;
        h = lo;
    } else if (h > hi) {
        clamped = 1;
        h = hi;
    }
    if (K.lift != 0) {
        const std::int32_t l = od[0] >> 2;
        if (clamped) Math_polarRecord(od[0], h, od);
        Kick_planToPoint(w, pid, od, nullptr, l, -1, 0, 4, 6, 1, 2);
        return;
    }
    std::int32_t d = od[0];
    bool redo = false;
    if (d < 0x50) {
        d = 0x50;
        redo = true;
    } else if (d > 0x320) {
        d = 0x320;
        redo = true;
    } else if (clamped) {
        redo = true;
    }
    if (redo) {
        tg = offsetInDirection(p.posI, d, h); // 0x4BB0D0 (x/z into [esp+0x10])
        const OctDist e = octDistance(p.posI, tg);
        od[0] = e.dist;
        od[1] = e.dx;
        od[2] = e.dz;
    }
    const std::int32_t e = od[0];
    std::int32_t v = e > 0x140 ? e >> 7 : e >> 8;
    if (v <= 0xf) v = 0xf;
    tg.y = -0xc0;
    od[3] = 0; // stale stack dword in the exe; Kick_plan only copies it into the request
    Kick_plan(w, pid, od, 5, 1, &tg, static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << 16), 0, 0, 4, 6, 1, 3);
}

// 0x4DD324: AI decision for the player on the ball (state 0x308 tick): shoot (best goal point 0x546B2C),
// pass (0x54628C via 0x4DD2BC, mentality thresholds), cross from the wing, skill move (state 0x30D) or
// dribble (0x4DC0D8 / dribble vector 0x6731F8). Returns 1 when an action state was entered.
int TeamAI_onBallAct_4DD324(World& w, int pid) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    TeamState& o = w.teams[static_cast<std::size_t>(t.opponent)];
    ActFrame F;
    auto f = [&F](int off) -> std::int32_t& { return F.at(off); };
    std::uint32_t ediDif = static_cast<std::uint32_t>(t.ctrl1f4); // EDI
    std::int32_t edx = 0;
    const int side = t.side;
    f(0x74) = 0;
    f(0x78) = 2;
    f(0x7c) = 0;
    F.p350 = nullptr;
    f(0x14) = 0;
    p.pendingCode |= 0xffffu;
    t.f1e0 = -1;
    t.f1b4 = -1;
    f(0x60) = (t.f1b8 != -1 && P(w, t.f1b8).ctrl != 0) ? 1 : 0;
    const std::uint32_t m = static_cast<std::uint32_t>(t.mentality);
    // WC98 FUN_0050cf50 (pair 46) loads the pass thresholds RAW from DAT_0058bfd1/bfd6/bfdb:
    // no +DAT_0058b6a0 formation-base addition that FIFA-99 applies. SIM_PORT_NOTES "TeamAI
    // decision-family WC98 deltas".
    f(0x40) = kPassThr91[m];
    f(0x44) = kPassThr96[m];
    f(0x48) = kPassThr9B[m];
    f(0x5c) = aiD32(0x59fc5c);
    f(0x58) = o.f1e4 != -1 ? Player_cachedDistance(w, pid, o.f1e4) : 0x960;
    if (o.flags & 1) f(0x5c) += 0x90;
    const int ch = o.f1cc; // [esp+0x88]
    if (ch != -1) {
        f(0x54) = Player_cachedAngle(w, pid, ch);
        f(0x4c) = Player_cachedAngle(w, pid, ch);
    } else {
        f(0x54) = side == 1 ? 0 : 0x200;
        f(0x4c) = 0x960;
    }
    f(0x1c) = s01(p.posI.z - w.pitch.thirdLineZ[side]) == side ? 1 : 0;
    f(0x20) = (s01(w.ball.posI.z) == side && Pitch_inPenaltyArea(w, w.ball.posI)) ? 1 : 0;
    f(0x28) = s01(p.posI.z + w.pitch.thirdLineZ[side]) != side ? 1 : 0;
    f(0x24) = s01(p.posI.z) != side ? 1 : 0;
    f(0x2c) = (s01(p.posI.z) != side && Pitch_inPenaltyArea(w, p.posI)) ? 1 : 0;
    f(0x34) = f(0x30) = 0;
    f(0x9c) = f(0xa4) = f(0xa0) = 0;
    f(0x8c) = f(0x94) = f(0x90) = 0;
    f(0x6c) = crowd_4DB924(w, pid, F.ptr(0x9c), F.ptr(0x70));
    f(0x3c) = kShotThr[m];
    f(0x38) = f(0x3c) + 0x3c - static_cast<std::int32_t>((2 - ediDif) << 3); // WC98 prologue unconditional (0x50cfab [P])
    f(0x18) = evalShots_546B2C(w, pid, F.shots); // WC98 0x50d137: shot eval is unconditional [P]
    if (static_cast<std::uint32_t>(t.humans) == 0 && t.f1b8 != -1 && pid == t.f1b8) t.f1b8 = -1;
    if (pid == t.oneTouch) goto L_4def38;
    if (f(0x20) == 0) {
        const std::uint32_t r = w.rng.next();
        const auto w1 = static_cast<std::uint32_t>(t.w360[1]);
        const std::int32_t lines = o.w360[0] - t.w360[1];
        if ((w1 <= 1 && lines == 2 && (r & 3)) || (w1 <= 1 && lines == 3 && (r & 0xf)) ||
            (w1 <= 1 && lines > 3 && (r & 0x7f)) || (t.w360[1] == 2 && lines > 4 && (r & 1)) ||
            (f(0x24) == 0 && (static_cast<std::uint32_t>(t.playerCount) >> 1) > w1 && (r & 0xf)))
            f(0x34) = 1;
        f(0x37c) = lines;
    }
    if (f(0x60) == 0) goto L_4dd9d6;
L_4dd6ad: {
    const int a32 = Player_attr(w, pid, 0x32);
    w.rng.next(); // WC98 0x50CF50: first FUN_004f33e0() draw is discarded (FIFA-99 has none)
    // WC98 0x50CF50 (pair 46): budget = table[attr] + (rand & 0xf) drawn unconditionally; the
    // FIFA-99 difficulty scaling `<< (dif == 2 ? 0 : 1)` and the conditional 0x1f / 0xf draws are
    // gone. The f210 branch gate becomes [team+0x144]==1 && formation != 0 with shift >>2
    // (FIFA-99: t.f210 == 1 -> >>= 3).
    const std::int32_t d10 = static_cast<std::int32_t>(w.rng.next());
    f(0x74) = kDribbleBudget[a32] + (d10 & 0xf);
    F.budgetScratch = d10 >> 8; // WC98 local_78 = high half of the same draw [P] (rng is a 16-bit stream)
    if (t.f210 == 1 && m != 0) { f(0x74) >>= 2; F.budgetScratch >>= 3; } // WC98 0x50d266 dual shift [P]
    std::int32_t d4 = (f(0x54) - p.facingDesired) & 0x3ff;
    if (d4 > 0x200) d4 -= 0x400;
    f(0x04) = d4;
    f(0x00) = ((d4 + 0x40) & 0x3ff) >> 7;
    f(0x0c) = kSectorReach[f(0x00)]; // WC98 0x50CF50: the FIFA-99 difficulty-0 `-= reach >> 2` clamp is not present.
    if (f(0x20) != 0) f(0x0c) += static_cast<std::int32_t>(static_cast<std::uint32_t>(kSectorReach[f(0x00)]) >> 1);
    f(0x0c) += Player_attr(w, pid, 0x2f);
    if (p.distBall > p.distBallPrev) f(0x0c) += ((w.ball.speed - p.speed) << 6) >> 16;
    if (static_cast<std::uint32_t>(t.humans) == 0 && t.f1b8 != -1) { // 0x4DDFC1
        const auto pf = static_cast<std::uint32_t>(w.time.possessionFrames);
        if (pf < 0x1e) {
            std::int32_t a = Player_cachedAngle(w, pid, t.f1b8) & 0x3ff;
            if (a > 0x200) a -= 0x400;
            if (iabs(a) <= 0x80) f(0x9c) = f(0xa4) = f(0xa0) = 0;
            else Math_polarRecord(0x18, a, F.ptr(0x9c));
            goto L_4dd814;
        }
        if (pf > 0xb4) t.f1b8 = -1;
    }
    if (ch == -1) {
        f(0xa0) = 0;
        f(0x37c) = 0x18;
        f(0xa4) = side == 1 ? -0x18 : 0x18;
        f(0x9c) = 0x18;
        goto L_4dd814;
    }
    goto L_4de05e;
}
L_4dd814:
    if (static_cast<std::int16_t>(p.pendingCode & 0xffff) < 0) goto L_4df000;
    ++w.pl.g67320c;
    if ((p.pendingCode & 0xffff) == 0x71) goto L_4defa5; // WC98 skill code (FIFA-99 0x73; anim ids shift -2)
    Player_setState(w, pid, 0x30d, 0, 0, false, true);
    return 1;
L_4dd9d6: {
    const auto d16v = static_cast<std::uint32_t>(w.pitch.penAreaDepth >> 4);
    f(0x37c) = w.pitch.halfLength - static_cast<std::int32_t>((w.rng.next() % d16v) << 4) - 0x90;
    const std::int32_t az = iabs(p.posI.z);
    if (f(0x28) == 0) goto L_4ddbc6;
    if (iabs(p.posI.x) <= (w.pitch.penAreaHalfWidth + w.pitch.halfWidth) >> 1) goto L_4ddbc6;
    if (f(0x70) == 0) goto L_4ddbc6;
    t.flags |= 0x200;
    framePasses_4DD2BC(w, pid, F);
    if (F.recs[f(0xac)].score <= 0) goto L_4ddbc6;
    if (az <= f(0x37c)) goto L_4ddbc6;
    if (iabs(p.posI.x) <= w.pitch.penAreaHalfWidth) goto L_4ddbc6;
    const int i = f(0xac);
    f(0x37c) = i;
    if (!Pitch_inPenaltyArea(w, F.recs[i].target)) goto L_4ddbc6;
    f(0x10) = i;
    if (f(0x44) >= F.recs[i].score) goto L_4decd0;
    const OctDist d = octDistance(p.posI, F.recs[i].target);
    f(0x8c) = d.dist;
    f(0x90) = d.dx;
    f(0x94) = d.dz;
    f(0x37c) = F.recs[i].r10;
    if (f(0x37c) < 0xe4 && f(0x37c) > 0) F.recs[i].r10 = 0xe4;
    f(0x30) = F.recs[f(0x10)].r10;
    goto L_4ddb72;
}
L_4ddb72:
    if (f(0x30) == 0) {
        f(0x7c) = 0;
        f(0x78) = 2;
    } else {
        f(0x78) = 1;
        f(0x7c) = 7;
    }
    frameKick_4DD070(w, pid, F);
    return 1;
L_4ddbc6:
    if (Team_beatsDefender_4DBA34(w, pid, F.ptr(0x9c))) {
        t.f1b4 = pid;
        t.flags &= ~0x200u;
        if (f(0x2c) != 0) {
            if (!(w.rng.next() & 1) && f(0x18) > 0 && f(0x38) <= F.shot(f(0x18)).score) goto L_4ddc95; // WC98 mask = 1 (0x50d6dd [P])
        }
        if (f(0x58) <= f(0x5c)) goto L_4ddc95;
        Math_polarRecord(f(0x2c) != 0 ? 6 : 0x18, atan2i(f(0xa0), f(0xa4)), F.ptr(0x9c));
        if (f(0x37c) != 0 && f(0x28) == 0 && ediDif != 0) Player_requestSprint(w, pid);
        goto L_4dd814;
    L_4ddc95:
        if (f(0x18) < 0) goto L_4ddd0d;
        f(0xa0) = iabs(f(0xa0));
        f(0xa4) = iabs(f(0xa4));
        {
            const std::int32_t mn = f(0xa0) > f(0xa4) ? f(0xa4) : f(0xa0);
            f(0x37c) = f(0xa0) + f(0xa4) - (mn >> 1);
        }
        if (f(0x37c) < w.pitch.halfPenDepth && (w.rng.next() & 0x1f)) goto L_4def38;
        if (w.rng.next() & 0xf) goto L_4def38;
    }
L_4ddd0d:
    if (f(0x18) >= 0 && f(0x58) < f(0x5c)) goto L_4def38;
    if (static_cast<std::uint32_t>(t.humans) == 0 && t.f1b8 != -1 &&
        static_cast<std::uint32_t>(w.time.possessionFrames) < 0x1e)
        goto L_4dd6ad;
    if (f(0x18) >= 0 && f(0x38) <= F.shot(f(0x18)).score) goto L_4def38;
    framePasses_4DD2BC(w, pid, F);
    edx = f(0xb0);
    if (f(0x40) > F.recs[edx].score) {
        edx = f(0xac);
        if (f(0x40) <= F.recs[edx].score) goto L_4ddd8e;
        if (w.ball.posI.y < 0x72) goto L_4dd6ad;
        framePasses_4DD2BC(w, pid, F);
        goto L_4ddd92;
    }
L_4ddd8e:
    f(0x10) = edx;
L_4ddd92:
    if (static_cast<std::uint32_t>(t.humans) == 0 && t.f1b8 != -1) {
        const int c = P(w, t.f1b8).index;
        if (F.recs[c].open > 0x32) f(0x10) = c;
    }
    f(0x7c) = 0;
    f(0x37c) = F.recs[f(0x10)].score;
    if (ch == -1) goto L_4deec8;
    if (f(0x37c) > 0) {
        if (f(0x37c) >= f(0x48)) goto L_4deec8;
    } // WC98 LAB_0050d9c8: no RNG draw between thr9B and the w360 gate (FIFA-99 extra removed [P])
    if (static_cast<std::uint32_t>(o.w360[0]) < 2) goto L_4dec04;
    if (f(0x1c) == 0 && w.ball.posI.y <= 0x72) goto L_4de6e9; // WC98 DAT_00646920 < 0x73 [P]
    f(0x08) = side == 1 ? 0x200 : 0;
    if (w.ball.posI.y < 0x72) goto L_4dedee;
    Math_polarRecord(0xb40, f(0x08), F.ptr(0x8c));
    ediDif = 6;
    f(0x7c) = 6;
    f(0x30) = 0x156;
L_4dde6d: {
    const Vec3 tg{p.posI.x + f(0x90), 0, p.posI.z + f(0x94)}; // [esp+0x370]
    t.f1e0 = Team_nearestPlayer_4523E4(w, tg, p.team, p.curSlot, F.ptr(0x37c));
}
L_4ddecc:
    f(0x78) = f(0x30) != 0 ? 1 : 2;
    frameKick_4DD070(w, pid, F);
    return 1;
L_4dedee: {
    const std::int32_t d = Player_cachedDistance(w, pid, ch);
    f(0x4c) = d;
    if (d < 0xf0) {
        std::int32_t e = (f(0x08) - f(0x54)) & 0x3ff;
        if (e > 0x200) e -= 0x400;
        f(0x00) = f(0x54);
        std::int32_t sct;
        if (w.rng.next() & 1) {
            sct = Math_asinRatio(0x40, d >> 1);
            if (sct > 0xab) sct = 0xab;
        } else {
            sct = Math_asinRatio(aiD32(0x59fd0c), d >> 1);
        }
        f(0x50) = sct;
        if (iabs(e) <= sct) f(0x08) = f(0x00) + (e < 0 ? -sct : sct);
    }
    Math_polarRecord(w.pitch.halfWidth, f(0x08), F.ptr(0x8c));
    f(0x30) = 0x156;
    goto L_4dde6d;
}
L_4deec8: {
    const int i = f(0x10);
    const OctDist d = octDistance(p.posI, F.recs[i].target);
    f(0x8c) = d.dist;
    f(0x90) = d.dx;
    f(0x94) = d.dz;
    f(0x30) = F.recs[i].r10;
    t.f1e0 = t.players[i];
    goto L_4ddecc;
}
L_4def38: {
    PassRec& sr = F.shot(f(0x18));
    F.p350 = &sr.target;
    f(0x30) = sr.r10;
    f(0x78) = 3;
    f(0x7c) = 2;
    const OctDist d = octDistance(p.posI, sr.target);
    f(0x8c) = d.dist;
    f(0x90) = d.dx;
    f(0x94) = d.dz;
    frameKick_4DD070(w, pid, F);
    return 1;
}
L_4defa5: // skill 0x73: push the ball 0x180 ahead and run onto it
    Math_polarRecord(0x180, p.facingDesired, F.ptr(0x354));
    Kick_planToPoint(w, pid, F.ptr(0x354), nullptr, 0x40, -1, 0, 0xc, 0xe, 0, 0);
    if (pid == w.ball.possessor) w.ball.dribbler = w.ball.possessor;
    Player_setState(w, pid, 0x300, 0, 0, false, true);
    return 1;
L_4df000: { // dribble: dribble vector toward +0x9C, turned along the touch / goal lines
    Player_dribbleVector(w, pid, f(0x9c), atan2i(f(0xa0), f(0xa4)), F.ptr(0x9c));
    f(0x64) = w.pitch.halfWidth - 0x120;
    f(0x68) = w.pitch.halfLength - 0x120;
    const std::int32_t ez = p.posI.z + f(0xa4);
    if (iabs(ez) > f(0x68)) {
        if (f(0x18) >= 0 && f(0x38) <= F.shot(f(0x18)).score) goto L_4def38;
        if (f(0x60) == 0 && (w.rng.next() & 3) && f(0x24) != 0) {
            framePasses_4DD2BC(w, pid, F);
            if (f(0x48) < F.recs[f(0x10)].score) {
                const int i = f(0x10);
                t.f1e0 = t.players[i];
                const OctDist d = octDistance(p.posI, F.recs[i].target);
                f(0x8c) = d.dist;
                f(0x90) = d.dx;
                f(0x94) = d.dz;
                f(0x30) = F.recs[i].r10;
                f(0x78) = f(0x30) != 0 ? 1 : 2;
                f(0x7c) = 0;
                frameKick_4DD070(w, pid, F);
                return 1;
            }
        }
        if (f(0x24) != 0 && ((ez > 0 && f(0xa4) > 0) || (ez < 0 && f(0xa4) < 0))) {
            f(0xa4) = 0;
            if (p.posI.z > 0) {
                f(0xa0) = -f(0x9c);
                f(0xa8) = -0x100;
            } else {
                f(0xa0) = f(0x9c);
                f(0xa8) = 0x100;
            }
        }
    } else {
        const std::int32_t ex = p.posI.x + f(0xa0);
        if (iabs(ex) > f(0x64) && ((ex > 0 && f(0xa0) > 0) || (ex < 0 && f(0xa0) < 0))) {
            f(0xa0) = 0;
            if (side == 1) {
                f(0xa4) = -f(0x9c);
                f(0xa8) = 0x200;
            } else {
                f(0xa8) = 0;
                f(0xa4) = f(0x9c);
            }
        }
    }
    t.f1e0 = -1;
    for (int k = 0; k < 4; ++k) w.pl.dribbleVec[k] = f(0x9c + k * 4);
    ++w.pl.g67320c;
    return 0;
}
L_4de05e: // an opponent chases (+0x88)
    if (f(0x0c) > P(w, ch).distBall && f(0x60) == 0) goto L_4de21d;
    Motion_setTarget(w, pid, w.ball.posI);
    {
        const std::int32_t d = P(w, ch).distBall;
        if (d < 0xc0 || (d < 0x180 && f(0x00) == 0)) { // WC98 0x50CF50: near-chaser dribble gate 0x150/0x1b0 -> 0xc0/0x180
            dribbleDir_4DC0D8(w, pid, ch, f(0x4c), f(0x54), f(0x34), F.ptr(0x9c));
            --w.pl.g67320c;
            goto L_4dd814;
        }
    }
    w.rng.next(); // 0x4DE0D7: drawn, unused
    f(0x37c) = 0x18;
    if (f(0x00) == 0 || f(0x00) == 1 || f(0x00) == 7) {
        f(0x37c) = P(w, ch).distBall >> 6;
        if (f(0x37c) < 0x18) f(0x37c) = 0x18;
        else if (f(0x37c) > 0x30) f(0x37c) = 0x30;
    }
    if (f(0x28) != 0) {
        Math_polarRecord(f(0x37c), atan2i(f(0xa0), f(0xa4)), F.ptr(0x9c));
    } else {
        f(0xa0) = 0;
        f(0xa4) = side == 1 ? -f(0x37c) : f(0x37c);
        f(0x9c) = f(0x37c);
    }
    f(0x37c) = f(0x0c) + aiD32(0x59fd28) > P(w, ch).distBall ? 0 : 1;
    if (f(0x37c) != 0 && f(0x34) == 0) Player_requestSprint(w, pid);
    --w.pl.g67320c;
    goto L_4dd814;
L_4de21d: // free of the chaser
    if (static_cast<std::uint32_t>(t.humans) == 0 && t.f1b8 != -1 && P(w, ch).distBall <= 0x240) t.f1b8 = -1;
    f(0x37c) = wrapAbs(p.facingDesired - (side == 0 ? 0x200 : 0));
    if (f(0x37c) >= aiD32(0x59fd24) || f(0x00) < 3 || f(0x00) > 5) goto L_4de3a4;
    if (ediDif == 2) goto L_4de2a8;
    if (ediDif == 1 && !(w.rng.next() & 3)) goto L_4de2a8;
    goto L_4de3a4;
L_4de2a8:
    framePasses_4DD2BC(w, pid, F);
    f(0x37c) = (f(0x34) != 0 && s01(p.posI.z - w.pitch.thirdLineZ[side]) != side) ? 2 : 1;
    {
        const int k = f(0xac + f(0x37c) * 8);
        if (f(0x44) <= F.recs[k].score) {
            f(0x10) = k;
            goto L_4ddd92;
        }
        f(0x37c) = f(0x37c) == 2 ? 1 : 2;
        const int k2 = f(0xac + f(0x37c) * 8);
        if (f(0x44) <= F.recs[k2].score) f(0x10) = k2;
        goto L_4ddd92;
    }
L_4de3a4:
    if (Player_isState100Kind1(w, ch) || Player_isState100Kind0(w, ch)) goto L_4de3b8;
    goto L_4de559;
L_4de3b8: { // the chaser is tackling
    f(0x37c) = iabs(p.posI.x) - w.pitch.goalHalfWidth;
    const std::int32_t e2 = (w.pitch.halfLength - iabs(p.posI.z)) * 2;
    if (f(0x18) >= 0 && f(0x2c) != 0 && !(f(0x37c) >= 0 && e2 < f(0x37c))) goto L_4def38;
    if (f(0x18) >= 0 && t.f1bc != -1) {
        framePasses_4DD2BC(w, pid, F);
        f(0x37c) = f(0xb4);
        if (f(0x48) < F.recs[f(0x37c)].score) {
            f(0x10) = f(0xb4);
            goto L_4ddd92;
        }
    }
}
    // 0x4DE42D
    if (f(0x20) != 0) goto L_4de6ce;
    if (static_cast<std::uint32_t>(t.humans) == 0 && t.f1b8 != -1) goto L_4de6ce;
    if (f(0x1c) != 0) goto L_4de6ce;
    if (ediDif == 0 && (w.rng.next() & 3)) goto L_4de6e9; // WC98: dif==0 && (rand & 3) -> dribble (FIFA-99 !(rand&1))
    if (P(w, ch).distBall >= 0x120) {
        dribbleDir_4DC0D8(w, pid, ch, f(0x4c), f(0x54), f(0x34), F.ptr(0x9c));
        goto L_4dd814;
    }
    framePasses_4DD2BC(w, pid, F);
    f(0x10) = f(0xac);
    if (f(0x48) > F.recs[f(0x10)].score) f(0x10) = f(0xb4);
    goto L_4ddd92;
L_4de6ce:
    framePasses_4DD2BC(w, pid, F);
    goto L_4ddd92;
L_4de6e9:
    ediDif = static_cast<std::uint32_t>(f(0x34));
    dribbleDir_4DC0D8(w, pid, ch, f(0x4c), f(0x54), f(0x34), F.ptr(0x9c));
    goto L_4dd814;
L_4de559:
    if (f(0x18) >= 0 && f(0x2c) != 0 && f(0x70) >= 2) goto L_4def38;
    if (f(0x28) == 0) goto L_4de946;
    if (f(0x70) < 3) goto L_4de80d;
    if (iabs(p.posI.z) <= w.pitch.halfLength - w.pitch.penAreaDepth - 0x90) goto L_4de80d;
    if (iabs(p.posI.x) >= w.pitch.penAreaHalfWidth + 0x90) goto L_4de80d;
    {
        const std::uint32_t r = w.rng.next() % (ediDif * 2 + 3);
        const int fl = r == 0 ? 1 : 0;
        f(0x37c) = fl;
        if (f(0x18) >= 0 && (f(0x3c) < F.shot(f(0x18)).score || fl != 0)) goto L_4def38;
    }
    framePasses_4DD2BC(w, pid, F);
    f(0x37c) = f(0xac);
    if (f(0x44) <= F.recs[f(0x37c)].score && (F.recs[f(0x37c)].r10 == 0 || f(0xac) != 0)) {
        f(0x10) = f(0x37c);
        goto L_4ddd92;
    }
    f(0x37c) = f(0xb4);
    if (f(0x44) <= F.recs[f(0x37c)].score && (F.recs[f(0x37c)].r10 == 0 || f(0xb4) != 0)) {
        f(0x10) = f(0x37c);
        goto L_4ddd92;
    }
    if (f(0x18) >= 0 && f(0x4c) < aiD32(0x59fd18)) { // WC98 gate global DAT_00595230
        // WC98 0x50CF50 (pair 46): the shot-lane goal-side offset is +/-0x40, not FIFA-99's +/-0x80.
        f(0x37c) = p.posI.z + (side == 1 ? -0x40 : 0x40) - P(w, ch).posI.z;
        if (s01(f(0x37c)) == side) goto L_4def38;
    }
L_4de80d:
    if (ch == -1 || f(0x2c) != 0 || f(0x70) < 2) goto L_4de946;
    if (!wingClear_4DB7CC(w, pid, F.ptr(0x9c))) goto L_4de946;
    t.flags |= 0x200;
    t.f1b4 = -1; // EBP = +0x2C (0): null
    if (P(w, ch).distBall >= 0xc0) { // WC98 LAB_0050dff3: wing-clear chaser gate 0xf0 -> 0xc0
        Math_polarRecord(0x18, atan2i(f(0xa0), f(0xa4)), F.ptr(0x9c));
        if (f(0x28) == 0) Player_requestSprint(w, pid);
        goto L_4dd814;
    }
    framePasses_4DD2BC(w, pid, F);
    edx = f(0xb0);
    if (f(0x44) <= F.recs[edx].score) goto L_4ddd8e;
    edx = f(0xac);
    if (f(0x44) <= F.recs[edx].score) goto L_4ddd8e;
    dribbleDir_4DC0D8(w, pid, ch, f(0x4c), f(0x54), f(0x34), F.ptr(0x9c));
    goto L_4dd814;
L_4de946:
    if (f(0x20) != 0) goto L_4de6ce;
    if (f(0x1c) != 0 || pid == t.f1bc) {
        if (P(w, ch).distBall < 0x240) goto L_4de6ce;
    }
    {
        int flag = 0; // ECX
        if (static_cast<std::uint32_t>(f(0x74) + 0xb4) >= static_cast<std::uint32_t>(w.time.possessionFrames)) goto L_4deb5b; // WC98 budget+0xb4 (FIFA-99 +0x1e)
        if (wrapAbs(side == 0 ? p.facing - 0x200 : p.facing) < 0x55 || w.time.possessionFrames > 0x21c) goto L_4de6ce; // WC98 fused facing/frames gate
        f(0x37c) = wrapAbs(f(0x54) - 0x200 - p.facingDesired);
        std::int32_t c = static_cast<std::int32_t>((2 - ediDif) << 2) - 2;
        if (c < 0) c = 0;
        if (f(0x37c) < aiD32(0x59fd24) && P(w, ch).distBall <= P(w, ch).distBallPrev) {
            const std::uint32_t mask = (1u << (c & 31)) - 1;
            if (!(w.rng.next() & mask)) flag = 1;
        }
        // WC98 LAB_0050e... : the FIFA-99 `frames <= 0x5a && flag == 0` skip is gone, folded into
        // the fused facing gate above; the mask-draw flag detection below stays an OPEN item.
        framePasses_4DD2BC(w, pid, F);
        f(0x37c) = f(0xac);
        if (f(0x44) <= F.recs[f(0x37c)].score && !(F.recs[f(0x37c)].r10 != 0 && f(0xac) == 0)) {
            f(0x10) = f(0x37c);
            goto L_4ddd92;
        }
        f(0x37c) = f(0xb4);
        if (flag != 0) {
            f(0x10) = f(0x37c);
            goto L_4ddd92;
        }
        if (f(0x44) <= F.recs[f(0x37c)].score && (F.recs[f(0x37c)].r10 == 0 || f(0xb4) != 0)) {
            f(0x10) = f(0x37c);
            goto L_4ddd92;
        }
    }
L_4deb5b:
    // WC98 LAB_0050e... (pair 46): the kSectorClose table gate is replaced by `(reach + attr [+half]) >> 1 < distBall`.
    if (static_cast<std::int32_t>(f(0x0c) >> 1) < P(w, ch).distBall || (w.rng.next() & 0xf)) {
        // WC98 0x50ded2/0x50def5: two flag-gated counter tests - budget vs f(0x34), scratch draw vs f(0x24).
        if (f(0x34) != 0 && w.pl.g67320c < f(0x74)) {
            dribbleDir_4DC0D8(w, pid, ch, f(0x4c), f(0x54), f(0x34), F.ptr(0x9c));
            goto L_4dd814;
        }
        if (f(0x24) != 0 && w.pl.g67320c < F.budgetScratch) {
            dribbleDir_4DC0D8(w, pid, ch, f(0x4c), f(0x54), f(0x34), F.ptr(0x9c));
            goto L_4dd814;
        }
    }
    w.rng.next(); // WC98 0x50dee6 unconditional draw [P]
    if (f(0x2c) != 0) {
        // WC98 0x50dee6..0x50def0: threshold = high half of the (rand&0x1f) draw, no global lane and
        // no FIFA-99 f(0x18)<0 dribble gate [P].
        const std::uint32_t u = w.rng.next();
        if ((u & 0x1f) != 0 && iabs(p.posI.z) <= static_cast<std::int32_t>(u >> 8)) goto L_4dec04;
        goto L_4def38; // LAB_0050d3b8 shot tail (port pass-chain model [P])
    }
    framePasses_4DD2BC(w, pid, F);
    if (f(0x44) <= F.recs[f(0xb8)].score) {
        f(0x10) = f(0xb8);
        goto L_4ddd92;
    }
    edx = f(0xb4);
    if (f(0x44) <= F.recs[edx].score) goto L_4ddd8e;
    if (f(0x74) >= w.pl.g67320c) goto L_4dec04;
    edx = f(0xbc);
    if (f(0x40) > F.recs[edx].score) goto L_4dec04;
    goto L_4ddd8e;
L_4dec04:
    dribbleDir_4DC0D8(w, pid, ch, f(0x4c), f(0x54), f(0x34), F.ptr(0x9c));
    goto L_4dd814;
L_4decd0: { // cross into the box: target a random point in the six-yard area (only the draws matter)
    std::int32_t zz = w.pitch.halfLength - 0x90 - static_cast<std::int32_t>(w.rng.next() & 0x200);
    if (side == 1) zz = -zz;
    f(0x36c) = zz;
    f(0x364) = static_cast<std::int32_t>(w.rng.next() % static_cast<std::uint32_t>(w.pitch.penAreaHalfWidth - 0xc0));
    f(0x37c) = p.posI.x;
    const OctDist d = octDistance(p.posI, F.recs[f(0x10)].target);
    f(0x8c) = d.dist;
    f(0x90) = d.dx;
    f(0x94) = d.dz;
    f(0x30) = (d.dist >> 4) + 0x8e;
    goto L_4ddb72;
}
}

// 0x4DF2CC (EDX kick height for the spec pick): AI first-time header/volley pass or shot at the ball
// arriving in the air; 0 when nothing worth it (the caller lets the ball come down).
bool TeamAI_tryHeaderPass_4DF2CC(World& w, int pid, std::int32_t h) {
    PlayerState& p = P(w, pid);
    TeamState& t = T(w, p);
    TeamState& o = w.teams[static_cast<std::size_t>(t.opponent)];
    const int side = t.side;
    t.f1e0 = -1;
    std::int32_t kind = 2, cls = 0; // [ebp+0x76] / [ebp+0x6a]
    const int k32 = (t.f1b8 != -1 && P(w, t.f1b8).ctrl != 0) ? 1 : 0;
    const std::uint32_t m = static_cast<std::uint32_t>(t.mentality);
    std::int32_t k5e = (kShotThr[m] + 0x3c) >> 1;
    // WC98 FUN_0050e740 (pair 47) loads the pass thresholds as raw bytes from DAT_0058bfd1 /
    // DAT_0058bfd6 / DAT_0058bfdb indexed by mentality; FIFA-99's `+ DAT_0058b6a0` formation-base
    // addition is gone. SIM_PORT_NOTES "TeamAI decision-family WC98 deltas".
    const std::int32_t k66 = kPassThr91[m], k62 = kPassThr9B[m];
    const std::int32_t t96 = kPassThr96[m]; // ESI
    const int ch = o.f1cc;                         // EDI
    const int k36 = s01(p.posI.z - w.pitch.thirdLineZ[side]) == side ? 1 : 0;
    const int k2e = (s01(w.ball.posI.z) == side && Pitch_inPenaltyArea(w, w.ball.posI)) ? 1 : 0;
    const int k2a = s01(p.posI.z + w.pitch.thirdLineZ[side]) != side ? 1 : 0;
    const int k56 = s01(p.posI.z) != side ? 1 : 0;
    const int k4a = (s01(p.posI.z) != side && Pitch_inPenaltyArea(w, p.posI)) ? 1 : 0;
    // ponytail: the exe has no null check on the chaser (+0x1CC); without one 0 / 0x960 stand in.
    const Angle k4e = ch != -1 ? Player_cachedAngle(w, pid, ch) : 0;
    const std::int32_t k42 = ch != -1 ? Player_cachedDistance(w, pid, ch) : 0x960;
    int k52 = 0;
    const Vec3* k6e = nullptr;
    k5e -= static_cast<std::int32_t>((2 - static_cast<std::uint32_t>(t.ctrl1f4)) << 3);
    std::int32_t rel[4] = {0, 0, 0, 0}; // [ebp+0xE] {dist, dx, dz}
    PassRec recs[11], shots[5];
    std::int32_t best[3][2], out[2];
    evalPasses_54628C(w, pid, recs, best, out, k36 ? 2 : 3);
    const int si = evalShots_546B2C(w, pid, shots);
    int ci = si; // ECX
    std::int32_t lift = t96; // ESI
    if (static_cast<std::uint32_t>(t.humans) == 0 && t.f1b8 != -1 && t.f1b8 == pid) t.f1b8 = -1;
    if (pid == t.oneTouch) goto L_4dfa95;
    if (k2e == 0) {
        const auto w1 = static_cast<std::uint32_t>(t.w360[1]);
        const std::int32_t lines = o.w360[0] - t.w360[1];
        if (w1 <= 1 && lines == 2 && (w.rng.next() & 3)) k52 = 1;
        else if (w1 <= 1 && lines == 3 && (w.rng.next() & 0xf)) k52 = 1;
        else if (w1 <= 1 && lines > 3 && (w.rng.next() & 0x7f)) k52 = 1;
        else if (t.w360[1] == 2 && lines > 4 && (w.rng.next() & 1)) k52 = 1;
        else if (k56 == 0 && (static_cast<std::uint32_t>(t.playerCount) >> 1) > w1 && (w.rng.next() & 0xf)) k52 = 1;
    }
    {   // WC98 FUN_0050e740 (pair 47) 0x50edc1/0x50ee71: skip lane gated by f(0x1c), f(0x28) + shot score;
        // entry requires az > k26 || az > penHalf+line; local_50 = f(0x24) forces the cross-lift from
        // the skip path. [P]
        const auto d16v = static_cast<std::uint32_t>(w.pitch.penAreaDepth >> 4);
        const std::int32_t k26 = w.pitch.halfLength - static_cast<std::int32_t>((w.rng.next() % d16v) << 4) - 0xf0; // WC98 local_6c [P]
        const std::int32_t az = iabs(p.posI.z);
        const bool skipline = (k36 == 0 || k2a != 0) || shots[si].score < 1;
        const bool deepok = !skipline && !(az <= k26 && az <= (w.pitch.penAreaDepth >> 1) + w.pitch.penAreaLineZ);
        if (deepok || (skipline && k56 != 0)) { // LAB_0050ea8e entered; local_50 force [P]
        const int i = best[0][0];
        if (Pitch_inPenaltyArea(w, recs[i].target) || (w.rng.next() & 1)) {
            // 0x4DF693: cross toward the far penalty spot area
            PassRec& R = recs[i];
            R.target.z += side == 1 ? -0xc0 : 0xc0;
            OctDist d = octDistance(p.posI, R.target);
            rel[0] = d.dist;
            rel[1] = d.dx;
            rel[2] = d.dz;
            if (rel[0] < 0x2d0) {
                const Vec3 spot{0, 0, side == 0 ? w.pitch.penSpotZ : -w.pitch.penSpotZ}; // 0x65F398 + i*0xC
                std::int32_t dd = 0;
                const int n = Team_nearestPlayer_4523E4(w, spot, p.team, 0xff, &dd);
                if (n != -1 && k26 > 0x3c0) return false; // WC98 :947 target non-null + deep threshold [P]
                if (n == -1) return false; // ponytail: the exe would read null + 0xD4 here (never seen)
                t.f1e0 = n;
                R.target = P(w, n).posI;
                R.target.z += side == 1 ? -0xc0 : 0xc0;
                d = octDistance(p.posI, R.target);
                rel[0] = d.dist;
                rel[1] = d.dx;
                rel[2] = d.dz;
                R.r10 = (rel[0] >> 3) + 0x72;
            }
            if (R.r10 < 0xe4) R.r10 = 0xe4;
            lift = R.r10;
            goto L_4df7fd;
        }
    }
        }
    if (k32 != 0) return false;
    if (static_cast<std::uint32_t>(t.humans) == 0 && t.f1b8 != -1) return false;
    if (k4a != 0) goto L_4dfa95;
    if (k5e <= shots[ci].score) goto L_4dfa95;
    {
        const int b01 = best[0][1], b00 = best[0][0];
        if (k66 <= recs[b01].score) ci = b01;
        else if (k66 <= recs[b00].score) ci = b00;
        else if (k52 != 0 && k42 < 0x120 && t96 <= recs[b01].score) ci = b01;
        else if (k52 != 0 && k42 < 0x120 && t96 <= recs[b00].score) ci = b00;
        else if (t96 <= recs[best[1][1]].score) ci = best[1][1];
        else if (t96 <= recs[best[1][0]].score) ci = best[1][0];
        else return false;
    }
    if (static_cast<std::uint32_t>(t.humans) == 0 && t.f1b8 != -1) {
        const int c = P(w, t.f1b8).index;
        if (recs[c].open > 0x32) ci = c;
    }
    cls = 0;
    if (recs[ci].score >= k62) {
        const OctDist d = octDistance(p.posI, recs[ci].target);
        rel[0] = d.dist;
        rel[1] = d.dx;
        rel[2] = d.dz;
        t.f1e0 = t.players[ci];
        lift = recs[ci].r10;
        kind = lift != 0 ? 1 : 2;
        goto L_4df7fd;
    }
    if (static_cast<std::uint32_t>(o.w360[0]) < 2) return false;
    if (k36 == 0 && w.ball.posI.y <= 0x72) return false;
    {
        Angle k46 = side == 1 ? 0x200 : 0;
        const std::int32_t d = ch != -1 ? Player_cachedDistance(w, pid, ch) : 0x960;
        if (d < 0xf0) {
            std::int32_t e = (k46 - k4e) & 0x3ff;
            if (e > 0x200) e -= 0x400;
            std::int32_t sct;
            if (w.rng.next() & 1) {
                sct = Math_asinRatio(0x40, d >> 1);
                if (sct > 0xab) sct = 0xab;
            } else {
                sct = Math_asinRatio(aiD32(0x59fd0c), d >> 1);
            }
            if (iabs(e) <= sct) k46 = k4e + (e < 0 ? -sct : sct);
        }
        Math_polarRecord(w.pitch.halfWidth, k46, rel);
        lift = 0x156;
        kind = 1;
        goto L_4df7fd;
    }
L_4dfa95: {
    PassRec& sr = shots[ci];
    k6e = &sr.target;
    lift = sr.r10;
    kind = 3;
    cls = 2;
    const OctDist d = octDistance(p.posI, sr.target);
    rel[0] = d.dist;
    rel[1] = d.dx;
    rel[2] = d.dz;
}
L_4df7fd:
    if (kind != 3 && lift < 0) lift = (rel[0] >> 7) + (rel[0] >> 5);
    else if (kind != 3 && lift > 0x640) lift = 0x640;
    else if (lift == 0) lift = (rel[0] >> 7) + (rel[0] >> 5);
    {
        const std::int32_t fr = kind == 3 ? 0x40 : (kind == 1 ? 0x10 : 0x20);
        const std::uint32_t ks = kickList_4D175C(p.facing, p.posI.y, h, p.slot == 0 ? 1 : 0, fr, rel[1], rel[2]);
        const KickSpec spec = kickSpec(ks);
        kind = spec.kind;
        cls = spec.cls;
    }
    if (k6e != nullptr) Kick_planToPoint(w, pid, rel, k6e, lift, k6e->y, 0, kind, cls, p.posI.y > 0 ? 1 : 0, 3);
    else Kick_planToPoint(w, pid, rel, nullptr, lift, -1, 0, kind, cls, p.posI.y > 0 ? 1 : 0, 2);
    rel[1] = iabs(rel[1]);
    rel[2] = iabs(rel[2]);
    if (static_cast<std::uint32_t>(t.humans) == 0 && k56 != 0 && kind == 2 && rel[0] < 0xf0 &&
        (p.f014 == 5 || p.f014 == 6)) t.f1b8 = pid; // WC98: passback gate f014 in {5,6} (FIFA-99 == 6)
    return true;
}

} // namespace wc98::sim
