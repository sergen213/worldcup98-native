// Kick request pipeline (player+0x190): Kick_clearRequest 0x4D0750, Kick_clearPending 0x4D0770,
// Kick_execute 0x4D07B0, Kick_arm 0x4D0BB4, Kick_planToPoint 0x4D0BF0, Kick_setRequestParams
// 0x4D0C68, Kick_plan 0x4D0CFC (WC98: FUN_00502fc0 — HANDLER_DIFF_AUDIT.md pair 24; WC98 deltas in
// SIM_PORT_NOTES.md "Kick family WC98 deltas"), Kick_setAnimParams 0x4D1708 — ported from the disassembly.
#include "wc98/sim/anim.hpp"
#include "wc98/sim/world.hpp"
#include "player_states.hpp"

namespace wc98::sim {

namespace {

// Kick_plan anim tables, index moving*10 + side*2 + leftFlag (tools/sim-tables/dump.py).
// exe 0x58B158 (anim class 2)
constexpr std::uint16_t kAnim158[20] = {276, 277, 297, 296, 299, 298, 287, 286, 0, 0, 265, 266, 262, 261, 264, 263, 256, 255, 0, 0};
// exe 0x58B180 (anim classes 1 and 7)
constexpr std::uint16_t kAnim180[20] = {269, 270, 283, 282, 285, 284, 287, 286, 0, 0, 253, 254, 253, 254, 252, 251, 256, 255, 0, 0};
// exe 0x58B1A8 (anim class 0)
constexpr std::uint16_t kAnim1A8[20] = {271, 272, 289, 288, 291, 290, 287, 286, 0, 0, 274, 275, 258, 257, 260, 259, 256, 255, 0, 0};
// exe 0x58B1D0: random mask for the alternative volley anim by surface column
constexpr std::uint8_t kVolleyMask[5] = {7, 7, 3, 3, 1};
// Kick power (WC98 FUN_00502fc0 lines 49-61; FIFA-99 Kick_plan has no counterpart): phase base
// DAT_0064015c == 2 -> 0x960, else 0xb40, +50% (>>1) when the kicker side's assist option bit
// (DAT_00641011 0x40 / 0x80 -> w.opt.kickAssist) is set.
constexpr std::int32_t kPowerPhase2 = 0x960, kPowerBase = 0xb40;
// WC98 FUN_00502fc0: the short parameter stored at record +0x14 (KickVec angle slot) snaps to
// 0x534 when it exceeds DAT_0056d148 = 1243.2 (double, read from the exe .bss image).
constexpr std::int32_t kParamClampMax = 1243;

bool zSide(const World& w, int pid, std::int32_t z) {
    const PlayerState& p = w.players[static_cast<std::size_t>(pid)];
    return static_cast<std::int32_t>(z >= 0) == w.teams[static_cast<std::size_t>(p.team)].side;
}

std::int32_t wrapA(std::int32_t a) {
    a &= 0x3ff;
    return a > 0x200 ? a - 0x400 : a;
}

EventType kickEvent(int kind) {
    switch (kind) {
    case 3: return EventType::Cross;
    case 4: return EventType::Shot;
    case 9: case 10: return EventType::Header;
    default: return EventType::Pass;
    }
}

} // namespace

void Kick_clearPending(World& w, int pid) { // 0x4D0770
    KickRequest& k = w.players[static_cast<std::size_t>(pid)].kick;
    if (w.ball.dribbler == k.kicker) w.ball.dribbler = -1;
    k.flags &= 0xfffffff3u;
}

void Kick_arm(World& w, int pid) { // 0x4D0BB4
    KickRequest& k = w.players[static_cast<std::size_t>(pid)].kick;
    if (k.flags & 4) return;
    k.flags = (k.flags | 4) & ~0x10u;
    Anim_start(w, k.kicker, k.animId, 0, k.heading);
}

int Kick_setAnimParams(World& w, int pid, int animId, std::int32_t frames, std::int32_t dist, std::int32_t angle) { // 0x4D1708
    KickRequest& k = w.players[static_cast<std::size_t>(pid)].kick;
    if (k.flags & 4) return 0;
    k.animId = static_cast<std::uint16_t>(animId);
    k.contactFrame = static_cast<std::int16_t>(frames);
    k.animOut44 = dist;
    k.heading = static_cast<std::int16_t>(angle);
    return 1;
}

// 0x4D0CFC (EAX player, EDX vec {_, dx, dz}, EBX mode, ECX coordMode, stack target*, lift, useHigh,
// spin, kind, animClass, a6, (int8) animArg).
int Kick_plan(World& w, int pid, std::int32_t vec[4], int mode, int coordMode, const Vec3* target, std::int32_t lift,
              std::int32_t useHigh, Fixed spin, int kind, int animClass, std::int32_t a6, int animArg) {
    PlayerState& p = w.players[static_cast<std::size_t>(pid)];
    TeamState& T = w.teams[static_cast<std::size_t>(p.team)];
    KickRequest& k = p.kick;
    int moving = 1, side = 0;
    std::int32_t anim = 0x10;
    k.deferredCode = -1;
    k.flags |= 8;
    k.target = target ? *target : Vec3{vec[1], 0, vec[2]};
    // Kick_setRequestParams 0x4D0C68
    k.mode = mode;
    k.coordMode = coordMode;
    k.lift = lift;
    k.useHigh = useHigh;
    k.spin = spin;
    k.receiver = T.f1e0;
    k.animArg = static_cast<std::uint8_t>(animArg);
    k.kicker = pid;
    k.receiver = T.f1e0;
    vec[0] = Math_length(vec[1], vec[2]);
    if (vec[3] > kParamClampMax) vec[3] = 0x534; // WC98 FUN_00502fc0 clamp (DAT_0056d148); FIFA-99 none
    k.vec = {vec[0], vec[1], vec[2], vec[3]};
    Angle hd = atan2i(vec[1], vec[2]);
    const std::int32_t dist = vec[0];
    // Kick power gate (WC98 FUN_00502fc0 lines 49-65). The helper FUN_00500660 returns nonzero only
    // for a slot-0 kicker passing FUN_0049eb60(dist) with team+0x138 == 0 (predicate not modelled —
    // SIM_PORT_NOTES "Kick family WC98 deltas"), and the kind==1 branch keeps DAT_00641314 (pitch
    // half-length) as power; every other path takes the phase base.
    std::int32_t power = w.match.phase == 2 ? kPowerPhase2 : kPowerBase; // DAT_0064015c gate
    if (p.slot == 0 && kind == 1) power = w.pitch.halfLength;            // FUN_00500660 != 0, param_5 == 1
    // +50% assist bump gated by the team side byte (**(player+0x20)) vs DAT_00641011 bits.
    if ((T.side == 0 && (w.opt.kickAssist & 0x40)) || (T.side == 1 && (w.opt.kickAssist & 0x80))) power += power >> 1;
    static_cast<void>(power); // the dist>power vector clamp (FUN_004f3590 + FUN_004f34b0 offsetXZ, arg
                              // registers hidden in the decompile) is an OPEN item — not applied.
    if (animClass == 0x10) { // 0x4D16E3 / WC98 FUN_00502fc0 param_6 == 0x10: no animation, armed at once
        k.contactFrame = -1; // iStack_28 = -1
        k.flags |= 4;        // WC98 sets +0x3c bit 2 only (keeps contact/left bits); FIFA-99 cleared them
        k.animOut44 = 0;     // uStack_30 = 0
        k.heading = static_cast<std::int16_t>(hd);
        k.kind = static_cast<std::uint8_t>(kind);
        k.animId = static_cast<std::uint16_t>(anim);
        k.side = 0;
        return 1;
    }
    // WC98 FUN_00502fc0 lines 66-69: identical gate — posI.z sign (WC98 player+0xa8) vs team side byte.
    if (kind == 3 && animClass == 2 && static_cast<std::int32_t>(p.posI.z >= 0) == w.teams[static_cast<std::size_t>(p.team)].side) animClass = 1;
    // WC98 takes the current facing from Motion_applyTurnAndRootPos FUN_004f3db4 (FIFA-99 direct field
    // read); class 6 reads angleBall (WC98 player+0x100 == FIFA-99 +0x128). Port models p.facing.
    std::int32_t d = wrapA(hd - (animClass == 6 && !(kind == 3) ? p.angleBall : p.facing));
    // WC98 lines 77-85 identical: d sign sets/clears record +0x3c bit 2 (target on the left).
    if (d > 0) k.flags &= ~2u;
    else k.flags |= 2;
    std::int32_t ad = d & 0x3ff;
    if (ad > 0x200) ad = 0x400 - ad;
    // WC98 FUN_00502fc0 penalty ladder identical: wrapped |angle| vs 0x55 / 199 / 0x180 -> classes 0..3.
    side = ad < 0x55 ? 0 : ad < 0xc7 ? 1 : ad < 0x180 ? 2 : 3;
    // WC98 reads the control-type byte at player+0x82 (FIFA-99 +0x9A); {2,3,4} = moving kick.
    const int ctlType = p.anim.type();
    if (ctlType != 2 && ctlType != 3 && ctlType != 4) moving = 0;
    const int left = (k.flags >> 1) & 1;
    auto idx = [&] { return moving * 10 + side * 2 + left; };
    const int phase = w.match.phase;
    switch (animClass) {
    case 0:
        hd = p.facing;
        // DAT_0064015c == 9 (WC98 phase global; FIFA-99 DAT_0065F22C): lines 110-114 re-map the kind to
        // 0xe on side class 3 && dist < 0x1e0 (the special anims below are from the FIFA-99 full body).
        if (phase == 9) {
            if (moving == 1 && side == 2 && dist < 0x1e0) {
                anim = left ? 0xf5 : 0xf7;
                break;
            }
            if (side == 3 && dist < 0x1e0) {
                anim = 0xef;
                kind = 0xe;
                break;
            }
        } else {
            moving = phase == 0 ? 0 : 1;
        }
        anim = kAnim1A8[idx()];
        break;
    case 1:
        if (phase != 9) {
            if (p.slot == 0) {
                anim = left ? 0xfa : 0xf9;
                hd = p.facing;
                break;
            }
            moving = 1;
        }
        anim = kAnim180[idx()];
        hd = p.facing;
        break;
    case 2: {
        // 0x4D71E0 raises the commentary flag DAT_0066727C (not simulated)
        const OctDist d1 = octDistance(p.posI, w.ball.landing);
        const TeamState& O = w.teams[static_cast<std::size_t>(T.opponent)];
        std::int32_t d2 = 0x3e80;
        if (O.possWinner >= 0) d2 = octDistance(w.players[static_cast<std::size_t>(O.possWinner)].posI, w.ball.landing).dist;
        if (pid == T.oneTouch && side <= 2 && Pitch_inPenaltyBox(w, O.side, w.ball.landing) && d1.dist > 0x80 && d1.dist + 0xa0 > d2) {
            anim = left ? 0x117 : 0x118;
            hd = p.facing;
            break;
        }
        // WC98 lines 126-128: DAT_0064015c == 9 && side class 3 && dist < 0x1e0 -> kind 0xe. The one-touch/
        // box branch above is subsumed by WC98 FUN_00508630 (identical to FIFA-99 per the audit table).
        if (phase != 9) moving = 1;
        else if (side == 3 && dist < 0x1e0) kind = 0xe;
        anim = kAnim158[idx()];
        hd = p.facing;
        break;
    }
    case 3: side = 4; anim = 0x111; break;
    case 4:
        if (dist <= 0x2d0) {
            anim = left ? 0xf6 : 0xf8;
            break;
        }
        if (side == 0 && (w.rng.next() & 3) > 2) {
            anim = 0x125;
            hd = p.facing;
            break;
        }
        anim = left ? 0x126 : 0x127;
        if (static_cast<std::int32_t>(p.posI.z + w.pitch.thirdLineZ[T.side] >= 0) != T.side &&
            static_cast<std::int32_t>(vec[2] >= 0) != T.side && iabs(vec[2]) > (dist >> 1))
            anim = left ? 0x14 : 0x15;
        hd = wrapA(hd + (left ? 0x100 : -0xff));
        side = 2;
        break;
    case 5:
        side = 0;
        anim = (w.rng.next() & 3) ? 0xf0 : 2;
        hd = wrapA(hd + 0x200);
        break;
    case 6:
        if (a6) anim = side == 0 ? 0x23 : side == 3 ? 0x22 : left ? 0x10b : 0x10c;
        else anim = 0x119;
        hd = p.angleBall;
        break;
    case 7:
        if (pid == T.oneTouch && side <= 2 && (w.rng.next() & 3) == 0) {
            anim = left ? 0x117 : 0x118;
            hd = p.facing;
            break;
        }
        anim = kAnim180[idx()];
        if (moving == 1 && (side == 2 || side == 3)) {
            int mask = kVolleyMask[w.opt.surfaceColumn];
            if (kind == 4) mask >>= 1;
            if ((w.rng.next() & static_cast<std::uint32_t>(mask)) == 0) anim = left ? 0xf1 : 0xf2;
        }
        if ((side == 2 || side == 3) && (w.rng.next() & 7) == 0) anim = left ? 0xf1 : 0xf2;
        hd = p.facing;
        break;
    case 8:
        side = 0;
        // WC98 gate reads DAT_00641314 == FIFA-99 DAT_0065F36C (pitch half-length), identical.
        anim = (dist > w.pitch.halfLength && Anim_clipUnavailable(w, 0xb9)) ? 0xb9 : 0xb7;
        break;
    case 9: side = 0; anim = 0xb8; break;
    case 10: side = 0; anim = dist < 0x120 ? 0xba : 0xbb; break;
    case 11: side = 0; anim = 0x124; break;
    case 12: anim = 0x24; side = 4; hd = p.facing; break;
    case 13: side = 4; anim = 0x80; hd = p.facing; break;
    case 14: side = 4; anim = 0x73; break;
    case 15: side = 4; anim = 0x116; break;
    default: break;
    }
    anim = Anim_resolveId(w, anim);
    k.contactFrame = static_cast<std::int16_t>(Anim_contactInfo(w, anim, 0, 0, -100, 1, &k.animOut44, &k.animHeading));
    const int m1 = Anim_markFrame1(w, anim);
    // WC98 lines 201-213/234-236: tail = FUN_004fccd0 anim id, FUN_004fd320 contact frame, FUN_004fd760
    // mark compare (flags bit 0 cleared then re-set); heading merge + zeroing identical to FIFA-99.
    k.flags = (k.flags & ~1u) | (k.contactFrame < m1 ? 1u : 0u);
    k.animHeading = wrapA(k.animHeading + hd);
    if (k.contactFrame < 0) k.animOut44 = k.animHeading = 0;
    k.heading = static_cast<std::int16_t>(hd);
    k.kind = static_cast<std::uint8_t>(kind);
    k.animId = static_cast<std::uint16_t>(anim);
    k.side = static_cast<std::uint8_t>(side);
    return 1;
}

void Kick_planToPoint(World& w, int pid, const std::int32_t* rel, const Vec3* target, std::int32_t ecx, std::int32_t y,
                      std::int32_t a1, std::int32_t a2, std::int32_t a3, std::int32_t a4, std::int32_t a5) { // 0x4D0BF0
    std::int32_t vec[4] = {0, 0, 0, 0};
    Vec3 t;
    int coord;
    if (target) {
        t = {target->x, y, target->z};
        coord = 1;
    } else {
        t = {rel[1], y, rel[2]};
        coord = 0;
    }
    vec[1] = rel ? rel[1] : 0;
    vec[2] = rel ? rel[2] : 0;
    Kick_plan(w, w.players[static_cast<std::size_t>(pid)].kick.kicker, vec, 6, coord, &t, ecx, 0, a1, a2, a3, a4,
              static_cast<std::int8_t>(a5));
}

int Kick_execute(World& w, int pid) { // 0x4D07B0 (EAX = player+0x190)
    KickRequest& k = w.players[static_cast<std::size_t>(pid)].kick;
    const int p = k.kicker;
    if (!(k.flags & 4)) return 0;
    auto fail = [&] {
        Kick_clearPending(w, pid);
        return 0;
    };
    if (w.match.phase == 0xb) return fail();
    if (w.ball.possessor >= 0 && w.ball.possessor != p) return fail();
    PlayerState& P = w.players[static_cast<std::size_t>(p)];
    TeamState& T = w.teams[static_cast<std::size_t>(P.team)];
    if (k.kind == 4 && T.humans <= 0) {
        k.flags &= 0xf3u;
        TeamAI_replanShot_4DCE94(w, p);
        k.flags |= 0xc;
    }
    if (!(k.animId == 0x10 && k.contactFrame < 0)) {
        const Vec3 c = Anim_contactPos(w, p);
        const std::int32_t dy = iabs(c.y - w.ball.posI.y);
        const std::int32_t d = octLength(c.x - w.ball.posI.x, c.z - w.ball.posI.z);
        if (dy > 0x26 || d > 0x26) return fail();
        if (k.kind == 4 && P.distBall > P.distBallPrev && d > 0x10) return fail();
    }
    if (k.deferredCode >= 0) Player_executeKickDeferred(w, k.kicker, k.deferredCode, w.ball.posI.y); // 0x4E3380
    OctDist dv;
    if (k.coordMode == 1) {
        dv = Math_vecTo(w.ball.posI, k.target);
    } else {
        dv = {k.vec.dist, k.vec.dx, k.vec.dz};
        k.target.x += w.ball.posI.x;
        k.target.z += w.ball.posI.z;
    }
    Fixed spin = k.spin;
    if (spin == 0 && (k.kind == 1 || k.kind == 3)) {
        spin = static_cast<Fixed>(((w.rng.next() & 15) + 3) * 500);
        std::int32_t m = w.pitch.halfLength - iabs(w.ball.posI.z);
        const std::int32_t mx = w.pitch.halfWidth - iabs(w.ball.posI.x);
        if (mx < m) m = mx;
        if (spin > 25 * m) spin = 25 * m;
    }
    if ((k.flags & 2) ? spin > 0 : spin < 0) spin = -spin;
    switch (k.mode) {
    case 0: Ball_kickLob(w, p, dv); break;
    case 1: Ball_kickWithVelocity(w, p, k.target, spin, k.kind, 0); break;
    case 2: Ball_kickGroundPass(w, p, k.target, k.lift, k.kind); break;
    case 3: Ball_kickTimed(w, p, k.target, k.lift, spin, k.kind); break;
    case 4: Ball_kickLofted(w, p, k.target, spin, k.useHigh, k.lift, k.kind); break;
    case 5: Ball_kickDriven(w, p, k.target, k.lift, spin, k.kind); break;
    case 6: Ball_kickToTarget(w, p, dv, k.target.y, k.lift, spin, k.kind); break;
    default: break;
    }
    if (p != w.ball.possessor) Ball_takePossession(w, p, 0);
    Ball_releaseFromPossessor(w, p);
    if (k.receiver >= 0) {
        w.match.player2a0 = k.receiver;
        T.f1e0 = k.receiver;
    }
    if (p >= 0 && P.ctrl && T.f1e0 >= 0) {
        const int r = T.f1e0;
        if (!w.players[static_cast<std::size_t>(r)].ctrl && T.f1b8 < 0) {
            if (static_cast<std::uint32_t>(T.humans) <= 1) Player_transferControl(w, p, r); // 0x452884
            else Player_assignControlToReceiver(w, r, p);                                  // 0x4528C4
        }
    }
    // Kick sound 0x4A2F20 and commentary 0x4D54F8 are presentation. The exe classifies these by
    // kick code (table 0x4E33AC): codes 9/10 = header/chest share solver kind 6 with code 11, and
    // the keeper's distribution throw is code 0xb (State201 drop / requestKick), so the event lane
    // maps those codes explicitly, falling back to the solver family for the rest.
    const int code = w.kickCode[static_cast<std::size_t>(p)];
    const EventType et = code == 9 || code == 10   ? EventType::Header
                         : code == 0xb             ? EventType::KeeperThrow
                                                    : kickEvent(k.kind);
    w.emit(et, P.team, p, k.kind, k.mode);
    if (p == w.match.ai6731f4) w.time.timer673210 = 0x14;
    Kick_clearPending(w, pid);
    k.flags |= 0x10;
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Kick codes: Player_executeKick 0x4E3478 and its aim helpers 0x4E3D88..0x4E4D8C (SimLink).

namespace {

// exe 0x58ce80, 26 x fx: aim spread scale per kick code (Kick_aimSetPiece stack arg 1).
constexpr Fixed kAimScale[26] = {0x10000, 0x10000, 0x18000, 0x10000, 0x10000, 0x10000, 0x10000, 0x10000, 0x10000,
                                 0x18000, 0x18000, 0x20000, 0x18000, 0x18000, 0x20000, 0x20000, 0x20000, 0x28000,
                                 0x20000, 0x10000, 0x10000, 0x10000, 0x8000,  0x8000,  0x10000, 0};
// exe 0x5904f0, 20 x i32: aim error by accuracy rating A[0x37].
constexpr std::int32_t kAimError[20] = {54, 51, 48, 46, 43, 40, 37, 34, 31, 28, 26, 23, 20, 17, 14, 11, 9, 6, 3, 0};
// exe 0x590680, 20 x i32: keeper kick distance (<< 4) by A[0x38].
constexpr std::int32_t kKeeperKick[20] = {20, 21, 22, 23, 23, 23, 24, 24, 24, 25, 26, 27, 28, 29, 29, 30, 30, 30, 30, 30};
// exe 0x58ceec / 0x58cf10, 9 x i32: goal aim point by pad direction (side 0 / side 1 or phase 6).
constexpr std::int32_t kAimByDir0[9] = {4, 5, 8, 7, 6, 3, 0, 1, 2};
constexpr std::int32_t kAimByDir1[9] = {4, 5, 2, 1, 0, 3, 6, 7, 8};
// exe 0x58cf34 (6) / 0x58cf4c (3): random aim points.
constexpr std::int32_t kAimLowHigh[6] = {0, 3, 6, 2, 5, 8};
constexpr std::int32_t kAimCentre[3] = {1, 4, 7};
// exe 0x4e3360 / 0x4e3370, 4 x fx: lead factor by receiver facing vs pass direction.
constexpr Fixed kLeadA[4] = {0x14000, 0x10000, 0x9999, 0x4000};
constexpr Fixed kLeadB[4] = {0x10000, 0xcccc, 0x8000, 0x4000};

// Goal aim points 0x677574 (9 x {x, y, z}), built from the goal size by 0x4E3D00 (called from
// Pitch_setDimensions 0x455C6C): columns -hw', 0, hw' (hw' = width/2 - 0x24) by rows
// height - 0x20, height / 2, 0. z is never written (0).
Vec3 aimPoint(const World& w, int i) {
    const std::int32_t hw = (w.pitch.goalWidth >> 1) - 0x24;
    static constexpr int kCol[3] = {-1, 0, 1};
    const std::int32_t ys[3] = {w.pitch.goalHeight - 0x20, w.pitch.goalHeight >> 1, 0};
    return {kCol[i % 3] * hw, ys[i / 3], 0};
}

void polar(std::int32_t r, Angle a, KickVec& v) {
    std::int32_t o[4];
    Math_polarRecord(r, a, o);
    v = {o[0], o[1], o[2], o[3]};
}

// 0x4D0C68 (EAX player, EDX mode, EBX coordMode, ECX target, stack lift, useHigh, spin).
void setRequestParams(World& w, int pid, int mode, int coordMode, const Vec3& target, std::int32_t lift,
                      std::int32_t useHigh, Fixed spin) {
    PlayerState& p = P(w, pid);
    KickRequest& k = p.kick;
    k.mode = mode;
    k.coordMode = coordMode;
    k.target = target;
    k.lift = lift;
    k.useHigh = useHigh;
    k.spin = spin;
    k.receiver = w.teams[static_cast<std::size_t>(p.team)].f1e0;
}

// 0x4E446C (EDX = type, EBX = &angle, stack = vec): aim angle of the kicker and the receiver it picks.
int aimReceiver(World& w, int pid, int type, Angle* angle, KickVec* vec) {
    PlayerState& p = P(w, pid);
    if (p.ctrl == 0) {
        *angle = p.facing;
    } else if (w.pl.kickDeferred && p.kick.aimAngle != -99999) {
        *angle = p.kick.aimAngle;
        p.kick.aimAngle = *angle;
    } else {
        ControllerState& c = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
        vec->dx = c.dirX; // (analog path omitted: analogDevice is never set)
        vec->dz = c.dirZ;
        *angle = (vec->dx == 0 && vec->dz == 0) ? p.facing : atan2i(vec->dx, vec->dz);
        p.kick.aimAngle = *angle;
    }
    if (type == 1 || type == 2) return Player_selectReceiver(w, w.ball.posI, pid, p.team, *angle, type, 0);
    if (type == 6) return Player_selectReceiver(w, w.ball.posI, pid, p.team, *angle, 6, vec->dist);
    const int ot = w.teams[static_cast<std::size_t>(p.team)].oneTouch;
    if (pid == ot) return Player_selectReceiver(w, w.ball.landing, ot, p.team, *angle, type, 0);
    return Player_selectReceiver(w, w.ball.posI, pid, p.team, *angle, type, 0);
}

// 0x4E4598 (EDX angle, EBX type): kick distance with no receiver.
std::int32_t freeKickDistance(const World& w, int pid, Angle a, int type) {
    const PlayerState& p = P(w, pid);
    if (p.slot == 0) {
        const std::int32_t d = kKeeperKick[w.attrs[static_cast<std::size_t>(p.attr)].at(0x38)] << 4;
        return type == 2 ? d + 0x5a0 : d + 0x230;
    }
    if (type == 2) return 0x640;
    const Pitch& g = w.pitch;
    std::int32_t ex, ez; // distance to the touchline / goal line the angle points at
    if (a >= 0x100) {
        ex = iabs(g.halfWidth - p.posI.x);
        ez = iabs(-g.halfLength - p.posI.z);
    } else if (a >= 0) {
        ex = iabs(g.halfWidth - p.posI.x);
        ez = iabs(g.halfLength - p.posI.z);
    } else if (a >= -0x100) {
        ex = iabs(-g.halfWidth - p.posI.x);
        ez = iabs(g.halfLength - p.posI.z);
    } else {
        ex = iabs(-g.halfWidth - p.posI.x);
        ez = iabs(-g.halfLength - p.posI.z);
    }
    const SinCos sc = sinCos(a);
    const std::int32_t s = iabs(sc.s), c = iabs(sc.c);
    const std::int32_t d = s * ez > c * ex ? fdiv(ex, s) : fdiv(ez, c);
    if (d > 0x6e0) return 0x500;
    if (d > 0x220) return d - 0x1c0;
    return 0x3c0;
}

// 0x4E4780 (EDX receiver, EBX lead mode, ECX type, stack &dist, &angle): lead of a pass to `recv`.
void passLead(World& w, int pid, int recv, int lead, int type, std::int32_t* dist, Angle* angle) {
    const PlayerState& p = P(w, pid);
    const PlayerState& r = P(w, recv);
    const TeamState& T = w.teams[static_cast<std::size_t>(p.team)];
    if (r.flags22c & 0x200000) {
        const std::int32_t m = w.teams[static_cast<std::size_t>(r.team)].f378;
        if (m == 2 || m == 3) lead = 2;
    }
    const OctDist vt = Math_vecTo(p.posI, r.posI);
    const Angle toRecv = atan2i(vt.dx, vt.dz);
    const int ph = w.match.phase;
    auto byFacing = [&](const Fixed* tab, std::int32_t d) {
        std::int32_t dd = (*angle - r.facingDesired) & 0x3ff;
        if (dd > 0x200) dd = 0x400 - dd;
        dd >>= 7;
        if (dd > 3) dd = 3;
        return fx(tab[dd], d);
    };
    if (ph == 1) {
        *angle = r.facingDesired;
        *dist = r.speed > 0x20000 ? Motion_predictDistance(w, recv, r.speedCap, *angle, 0x41, 0, 0) : 0x20;
    } else if (lead == 0) {
        *dist = 0;
        *angle = 0;
    } else if (lead == 3) {
        *dist = (w.pitch.halfLength - iabs(r.posI.z)) >> 1;
        *angle = T.side == 1 ? 0x200 : 0;
    } else if (type == 1 || type == 6) {
        *dist = fx(vt.dist, 0x1999) + 0xc0;
        *angle = r.facingDesired;
    } else if (ph == 5 || ph == 2) {
        *dist = 0;
        *angle = 0;
    } else {
        const bool own = zSide(w, pid, p.posI.z - w.pitch.thirdLineZ[T.side]);
        if (static_cast<std::int32_t>(vt.dz >= 0) == T.side && static_cast<std::int32_t>(r.posI.z >= 0) == T.side) {
            *dist = fx(vt.dist, own || type == 1 || type == 6 ? 0x2000 : 0x6000);
            if (*dist < 0x30) *dist = 0x30;
            else if (*dist > 0x80) *dist = 0x80;
            *angle = r.facingDesired;
        } else if (lead == 2) {
            *dist = fx(vt.dist, own || type == 1 || type == 6 ? 0x4000 : 0x6666);
            *angle = T.side == 1 ? 0x200 : 0;
            *dist = byFacing(kLeadA, *dist);
            if (*dist < 0x50) *dist = 0x50;
            else if (*dist > 0x1e0) *dist = 0x1e0;
        } else {
            *angle = T.side == 1 ? 0x200 : 0;
            std::int32_t dd = (*angle - toRecv) & 0x3ff;
            if (dd > 0x200) dd = 0x400 - dd;
            std::int32_t d;
            if (dd < 0x155) {
                d = fx(vt.dist, 0x8000) + ((vt.dist * vt.dist) >> ((dd >> 7) + 10));
                if (d < 0xf0) d = 0xf0;
                else if (d > 0x3c0) d = 0x3c0;
                if ((type == 1 || type == 6) && d > 0x320) d = 0x320;
            } else {
                d = fx(vt.dist, 0x6000);
                if (d < 0xf0) d = 0xf0;
                else if (d > 0x320) d = 0x320;
            }
            d = byFacing(kLeadB, d);
            if (d < 0xa0) d = 0xa0;
            *dist = d;
        }
    }
    // 0x4E4836: in the attacking half, stay out of the opposing keeper's reach.
    if (zSide(w, pid, p.posI.z + w.pitch.thirdLineZ[T.side])) return;
    const int k = Team_findKeeper(w, T.index == 0 ? 1 : 0);
    if (k < 0) return;
    const std::int32_t dx = iabs(r.posI.x - P(w, k).posI.x), dz = iabs(r.posI.z - P(w, k).posI.z);
    if (dz == 0) return; // exe: idiv by zero (#DE); never reached with players on separate spots
    std::int32_t rr = (dx * dx + dz * dz) / (2 * dz) - 0x20;
    if (rr < 0x50) rr = 0x50;
    if (rr < *dist) *dist = rr;
}

} // namespace

// 0x4E3D88 (EDX vec out, EBX outB, ECX outC, stack kind, scale): aimed set-piece / shot vector
// with the kicker's accuracy error.
void Kick_aimSetPiece(World& w, int pid, KickVec* vec, std::int32_t* outB, std::int32_t* outC, std::int32_t kind,
                      std::int32_t scale) {
    PlayerState& p = P(w, pid);
    const TeamState& T = w.teams[static_cast<std::size_t>(p.team)];
    const OctDist vt = Math_vecTo(p.posI, w.ball.posI);
    int idx = 0;
    const Vec3& origin = vt.dist > 0x140 ? p.posI : w.ball.posI;
    const bool pen = p.state == 0x185 || p.state == 0x189;
    // (kind != 2: 0x4D71E0 raises the commentary flag DAT_0066727C)
    Vec3 A;
    if (kind == 2) {
        A = {p.posI.x, 0xa0, 0};
    } else if (kind == 6) {
        idx = kAimLowHigh[w.rng.next() % 6u];
        A = aimPoint(w, idx);
    } else if (p.ctrl != 0 && ((w.controllers[static_cast<std::size_t>(p.ctrl - 1)].flags1a0 & 1) || pen)) {
        const ControllerState& c = w.controllers[static_cast<std::size_t>(p.ctrl - 1)];
        idx = (T.side != 0 || w.match.phase == 6) ? kAimByDir1[c.dir] : kAimByDir0[c.dir];
        A = aimPoint(w, idx); // (analog scaling omitted: analogDevice is never set)
    } else if (kind == 7) { // AI penalty: pick a side away from the keeper
        const std::int32_t kx = P(w, w.teams[static_cast<std::size_t>(T.opponent)].f1e4).posI.x;
        const std::uint32_t r = w.rng.next() & 0x7f;
        kind = r < 8 ? 4 : 0;
        if ((r & 7) == 0) {
            idx = kAimCentre[w.rng.next() % 3u];
            A = aimPoint(w, idx);
        } else if (iabs(kx) < 0x20 || w.rng.next() % static_cast<std::uint32_t>(iabs(kx)) <= 0x20) {
            idx = kAimLowHigh[w.rng.next() % 6u];
            A = aimPoint(w, idx);
        } else {
            const std::int32_t hw = (w.pitch.goalWidth >> 1) - 0x24;
            A.x = kx > 0 ? -hw : hw;
            A.z = 0;
            if (w.rng.next() & 1) A.y = w.pitch.goalHeight >> 1;
            else if (w.rng.next() & 3) A.y = 0;
            else A.y = w.pitch.goalHeight - 0x20;
        }
    } else {
        if ((w.rng.next() & 0x7f) <= 0x64) idx = kAimLowHigh[w.rng.next() % 6u];
        else idx = kAimCentre[w.rng.next() % 3u];
        A = aimPoint(w, idx);
    }
    // 0x4E3E0E: goal line target z (just behind the kicker when right in front of goal).
    if (iabs(p.posI.z) > w.pitch.halfLength - 0xa0 && iabs(p.posI.x) < w.pitch.goalHalfWidth + 0x50 &&
        static_cast<std::int32_t>(p.posI.z >= 0) != T.side)
        A.z = p.posI.z > 0 ? p.posI.z + 0xa0 : p.posI.z - 0xa0;
    else
        A.z = w.pitch.goalLineZ[w.teams[static_cast<std::size_t>(T.opponent)].side];
    if (kind == 5 && (idx == 8 || idx == 6 || idx == 7)) A.y -= static_cast<std::int32_t>(w.rng.next() % 0xf0u) + 0x50;
    std::int32_t err = fx(kAimError[w.attrs[static_cast<std::size_t>(p.attr)].at(0x37)], scale);
    err += err >> 2;
    if (pen) {
        const std::int32_t a = p.posI.x > 0, b = A.x > 0;
        if (a != b) err += err >> 3;
    }
    if (err < 1) err = 1;
    const std::uint32_t ue = static_cast<std::uint32_t>(err);
    std::int32_t angErr = static_cast<std::int32_t>(w.rng.next() % ue);
    angErr -= static_cast<std::int32_t>(w.rng.next() % ue);
    const OctDist d = Math_vecTo(origin, A);
    vec->dist = d.dist;
    vec->dx = d.dx;
    vec->dz = d.dz;
    vec->dist += (vec->dist * angErr) / 0x5a0;
    if (vec->dist < 0x4b0)
        angErr = fx(angErr, fdiv(static_cast<std::int32_t>(static_cast<std::uint32_t>(vec->dist) << 16), 0x4b00000));
    if (kind != 3) {
        std::int32_t e = static_cast<std::int32_t>(w.rng.next() % ue);
        e += static_cast<std::int32_t>(w.rng.next() % ue);
        const std::int32_t e3 = static_cast<std::int32_t>(w.rng.next() % ue);
        const std::int32_t e4 = static_cast<std::int32_t>(w.rng.next() & 0xf);
        e += e3 + e4;
        if (w.rng.next() % 3u) e = -e;
        A.y += (vec->dist * e) / 0x640;
    }
    if (pen && A.y < 0) A.y = 0;
    *outC = A.y;
    if (kind == 2) {
        const std::int32_t r = static_cast<std::int32_t>(w.rng.next() & 7);
        *outB = (vec->dist >> 5) + 0xc0 + (r << 4);
    } else if (kind == 3) {
        if (A.y < 0x80) A.y = 0x80;
        *outB = A.y + (vec->dist >> 4);
        if (*outB < 0xa0) *outB = 0xa0;
        else if (*outB > 0x190) *outB = 0x190;
    } else {
        *outB = -1; // (kind 4 also recomputes A.y = min((dist >> 7) + 0x14, 0x20); never read)
    }
    const Angle a = wrapA(atan2i(vec->dx, vec->dz) + angErr);
    if (kind == 2) polar(0x5a0 + static_cast<std::int32_t>(w.rng.next() & 0xf) * 0x60, a, *vec);
    else if (kind == 4) polar((vec->dist * 7) >> 2, a, *vec);
    else polar(vec->dist, a, *vec);
}

// 0x4E4D8C (EDX vec out, EBX type, ECX lead mode, stack s0 (one-touch aim), s1 receiver):
// receiver and kick vector of a pass; returns the receiver (-1 none).
int Kick_aimTo(World& w, int pid, KickVec* vec, std::int32_t type, std::int32_t lead, std::int32_t s0, int recv) {
    PlayerState& p = P(w, pid);
    const TeamState& T = w.teams[static_cast<std::size_t>(p.team)];
    Angle angle = 0;
    if (p.ctrl == 0 && T.f1b8 >= 0 && recv != T.f1b8) return -1;
    if (recv < 0) recv = aimReceiver(w, pid, type, &angle, vec);
    Vec3 out;
    if (recv >= 0) {
        std::int32_t d = 0;
        Angle a = 0;
        passLead(w, pid, recv, lead, type, &d, &a);
        out = offsetInDirection(P(w, recv).posI, d, a);
        if (w.pitch.halfWidth - 0x50 < out.x) out.x = w.pitch.halfWidth - 0x50;
        else if (0x50 - w.pitch.halfWidth > out.x) out.x = 0x50 - w.pitch.halfWidth;
        const std::int32_t hi = w.pitch.halfLength - ((d >> 2) + 0xa0);
        if (hi < out.z) out.z = hi;
        else if ((d >> 3) + 0xa0 - w.pitch.halfLength > out.z) out.z = (d >> 3) + 0xa0 - w.pitch.halfLength;
    } else {
        (void)s0;
        if (w.pl.passZone != -1 && !zSide(w, pid, p.posI.z + w.pitch.thirdLineZ[T.side]) &&
            (type == 0 || type == 1 || type == 4) && iabs(p.posI.x) < w.pitch.penAreaHalfWidth) {
            const std::int32_t az = iabs(p.posI.z);
            if (type == 0 ? az > w.pitch.halfLength - 0x3c0 : az > w.pitch.halfLength - 0x690) {
                w.pl.passZone = type == 0 ? 2 : 1;
                return -1;
            }
        }
        const std::int32_t d = type == 6 ? vec->dist : freeKickDistance(w, pid, angle, type);
        out = offsetInDirection(p.posI, d, angle);
    }
    out.y = 0;
    const OctDist v = Math_vecTo(w.ball.posI, out);
    vec->dist = v.dist;
    vec->dx = v.dx;
    vec->dz = v.dz;
    return recv;
}

// 0x4E3478 (EDX code, EBX ball height): plan a kick of one of the 26 kick codes.
void Player_executeKick(World& w, int pid, int code, std::int32_t ballY) {
    PlayerState& p = P(w, pid);
    TeamState& T = w.teams[static_cast<std::size_t>(p.team)];
    w.kickCode[static_cast<std::size_t>(pid)] = code; // port-internal: event mapping (9/10 header)
    KickVec V{0, 0, 0, 0};
    std::int32_t L = 0, M = 0, H = 0, U = 0, mode = 6;
    int R = -1;
    auto aimSP = [&](int s0) { Kick_aimSetPiece(w, pid, &V, &M, &L, s0, kAimScale[code]); };
    switch (code) { // table 0x4E33AC (code - 1)
    case 1: case 23: aimSP(3); break;
    case 2: case 22: aimSP(4); break;
    case 3:
        L = 0x72;
        R = pid;
        polar(0x30, p.facing, V);
        break;
    case 6: case 11: case 14: case 17: case 21:
        if (p.ctrl != 0) aimSP(code == 0xe || code == 0x11 ? 5 : 0);
        else aimSP(1);
        break;
    case 18: aimSP(2); break;
    case 25:
        L = 0x55;
        R = Kick_aimTo(w, pid, &V, 0, 0, 0, w.match.setPiecePartner);
        break;
    case 4: case 5: case 7: case 8: case 9: case 10: case 12: case 13: case 15: case 16: case 19: case 20: {
        const int lead = (code == 8 || code == 7) ? 1 : (code == 0x13 || code == 0x14) ? 3 : (code == 0xd || code == 9) ? 0 : 2;
        int type;
        if (code == 5 || code == 8 || code == 9 || code == 0x10) {
            L = 0;
            type = 0;
        } else if (code == 0xd || code == 0xc) {
            L = 0;
            type = 3;
        } else if (code == 0x13 || code == 0x14) {
            L = 0x55;
            type = 4;
        } else {
            L = 0x55;
            type = 1;
        }
        w.pl.passZone = (code == 0xa || code == 9 || code == 0xc || code == 0xd) ? -1 : 0;
        if (p.ctrl != 0) R = Kick_aimTo(w, pid, &V, type, lead, (code == 0x13 || code == 0x14) ? 1 : 0, -1);
        else R = Kick_aimTo(w, pid, &V, type, lead, 0, T.f1b8);
        if (w.pl.passZone == 2) {
            code = 2;
            aimSP(4);
        } else if (w.pl.passZone == 1) {
            code = 1;
            aimSP(3);
        }
        break;
    }
    default: break; // 0, 24, > 25
    }
    if (p.state == 0x182) L = 0x55;
    std::int32_t S = 0, minDist = 0, D = 0, maxDist = 0;
    int C = 0, kind = 1, B = 0;
    switch (code) { // table 0x4E3410
    case 0: case 1: C = 1; minDist = 0x1e0; S = 0x60; D = 0x2c; B = 2; maxDist = 0x640; kind = 1; break;
    case 2: case 5: case 8: case 22: C = 0; minDist = 0xc0; S = 0; D = 0x20; kind = 2; B = 3; maxDist = 0x5a0; break;
    case 3: C = 1; minDist = 0x20; S = 0xb2; D = 3; B = 0; maxDist = 0x40; kind = 1; break;
    case 6: case 18: case 21: C = 2; minDist = 0x1e0; S = 0x13; D = 0x40; kind = 3; B = 3; maxDist = 0x870; break;
    case 9: {
        mode = 5;
        std::int32_t e = V.dist >> 6;
        if (e <= 0x10) e = 0x10;
        M = e << 16;
        C = 4; L = -0xc0; minDist = 0xa0; kind = 6; B = 3; maxDist = 0x780;
        break;
    }
    case 10: C = 4; minDist = 0x1e0; S = 0xa0; D = 0x20; kind = 6; B = 3; maxDist = 0x1680; break;
    case 11: C = 4; kind = 6; minDist = 0x140; S = 0x30; D = 0x32; B = 3; maxDist = 0x780; break;
    case 12: C = 6; kind = 4; minDist = 0x1e0; S = 0xa0; maxDist = 0x5a0; D = 0x16; U = 1; B = 3; break;
    case 13: {
        mode = 5;
        std::int32_t e = V.dist > 0x140 ? V.dist >> 7 : V.dist >> 8;
        if (e <= 0xf) e = 0xf;
        M = e << 16;
        C = 6; kind = 4; minDist = 0x50; L = -0xc0; U = 1; B = 3; maxDist = 0x320;
        break;
    }
    case 14: C = 6; minDist = 0xf0; S = 0x18; D = 0x40; U = 1; kind = 4; B = 3; maxDist = 0x690; break;
    case 15: case 16: case 17: C = 5; minDist = 0x140; S = 0x8e; D = 0x40; B = 3; maxDist = 0x780; kind = 5; break;
    case 19: case 20: {
        C = 1; // [ebp-0x2C] is always 0 here (7 otherwise)
        minDist = 0x1e0; S = 0xa0; D = fx(0x16, p.kickPower); kind = 1; B = 3; maxDist = 0x1680;
        std::int32_t h = (w.pitch.halfLength - iabs(w.ball.posI.z)) * 25;
        if (h > 0x1f40) h = 0x1f40;
        H = h;
        break;
    }
    case 25: C = 0xb; kind = 9; minDist = 0xc0; S = 0x92; maxDist = 0x640; D = 0x18; U = 0; B = 0; break;
    default: // 4, 7, 23, 24, > 25
        C = 1; minDist = 0x1e0; S = 0xa0; D = 0x16; B = 3; maxDist = 0x1680; kind = 1;
        if (code == 4 || code == 7) D = fx(0x16, p.kickPower);
        break;
    }
    if (code != 3) {
        if (V.dist < minDist) V.dist = minDist;
        else if (V.dist > maxDist) V.dist = maxDist;
        polar(V.dist, atan2i(V.dx, V.dz), V);
    }
    const bool noLift = code == 6 || code == 0x12 || code == 1 || code == 0xd || code == 9 || code == 2 ||
                        code == 0x11 || code == 0xe || code == 0xb;
    if (!noLift) {
        M = S;
        if (V.dist != 0 && kind != 3) {
            bool add = true;
            if (kind == 4 || kind == 6 || kind == 5) {
                if (code == 0xa || code == 0xc) {
                    M = ballY + 0x80;
                    D = 8;
                } else if (code == 9 || code == 0xd) {
                    M = -1;
                    add = D != 0;
                } else {
                    D = 0x20;
                }
            } else {
                add = D != 0;
            }
            if (add) M += V.dist / D;
        }
    }
    T.f1e0 = R;
    const OctDist vt = Math_vecTo(p.posI, w.ball.posI);
    const Vec3& base = vt.dist > 0x140 ? p.posI : w.ball.posI;
    const Vec3 target{base.x + V.dx, L, base.z + V.dz};
    if (w.pl.kickDeferred) {
        setRequestParams(w, pid, mode, 1, target, M, 0, H);
        return;
    }
    std::int32_t v[4] = {V.dist, V.dx, V.dz, V.angle};
    Kick_plan(w, pid, v, mode, 1, &target, M, maxDist, H, kind, C, U, static_cast<std::int8_t>(B));
}

// 0x4E3380: re-aim a planned kick at the contact frame (Kick_execute).
void Player_executeKickDeferred(World& w, int pid, int code, std::int32_t ballY) {
    w.pl.kickDeferred = 1;
    Player_executeKick(w, pid, code, ballY);
    w.pl.kickDeferred = 0;
    P(w, pid).kick.aimAngle = -99999;
}

// 0x4BDF38 (anim event callback [0x675A08], player = DAT_0065F2A4): release the armed kick.
int Player_onAnimContact(World& w) {
    const int pid = w.pl.current;
    if (pid < 0) return 0;
    if (pid == w.ball.dribbler) w.ball.dribbler = -1;
    PlayerState& p = P(w, pid);
    if (!(p.kick.flags & 4)) return 0;
    if (p.kick.kind >= 0xf) {
        Kick_clearPending(w, pid);
        return 0;
    }
    if (!Kick_execute(w, pid)) {
        if (pid == w.match.player2a0) w.match.player2a0 = -1;
        return 1;
    }
    TeamState& T = w.teams[static_cast<std::size_t>(p.team)];
    if (pid == T.oneTouch) T.oneTouch = -1;
    const std::int32_t pass[3] = {p.kick.vec.dist, p.kick.vec.dx, p.kick.vec.dz};
    Rules_checkOffside(w, pid, pass);
    if (p.prevState == 0x182 || p.prevState == 0x201) Keeper_holdOff(w, 0x1e);
    w.match.player2a0 = T.f1e0;
    if (w.match.phase == 9 && w.teams[static_cast<std::size_t>(T.opponent)].ctrl1f4 != 2)
        Team_holdPlayers_451C2C(w, T.opponent, 0xf, w.match.player2a0, 1);
    return 1;
}

} // namespace wc98::sim
