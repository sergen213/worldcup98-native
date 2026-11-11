// Movement object M at player+0xB8 (Players agent): targets, warps, the per-frame motion step and
// the kinematic distance predictor. Player-relative field names; M offsets in player.hpp.
#include <algorithm>

#include "wc98/sim/world.hpp"
#include "player_states.hpp"
// Speed caps + turn thresholds, byte-exact WC98 rows (DAT_0058aee4 stride 4 / DAT_0058aef0
// stride 8; pass-15 S2 BYTES.md). Provenance gate: tests/unit/test_motion_tables.cpp. The
// FIFA-99 cites 0x58AD60 / 0x58AD6C name zeros in WC98, not tables. Public header shared with
// the self-test step-13 replica and the gate (one source of truth).
#include "wc98/sim/motion_tables.inc" // kSpeedCap, kTurnClass

namespace wc98::sim {

namespace {


const Vec3& targetVec(const PlayerState& p) { return p.targetRef ? p.altTarget : p.target; }

// Ball distance/angle refresh shared by the warp helpers (octDistance(posI, ball) into M+0x64,
// atan2 into M+0x70, copy into M+0x74).
void refreshBall(World& w, PlayerState& p) {
    const OctDist d = octDistance(p.posI, w.ball.posI);
    p.distBall = d.dist;
    p.dxBall = d.dx;
    p.dzBall = d.dz;
    p.angleBall = atan2i(d.dx, d.dz);
    p.distBallPrev = p.distBall;
}

void setTargetDelta(PlayerState& p, const Vec3& t) {
    const std::int32_t dx = t.x - p.posI.x, dz = t.z - p.posI.z;
    p.targetDx = dx;
    p.targetDz = dz;
    p.targetDist = octLength(dx, dz);
}

} // namespace

// FUN_004F4E70 (WC98; FIFA-99 name Motion_setTarget cites 0x4CBA54 — off by 0x3b): stores
// target x/y/z (+0x34.., y cleared), [M+0x4C] = &record main-target (port: targetRef = 0),
// byte 0x5E &= ~1 clears the alt-target flag, then dx/dz = target - posI ([M+0x1C]/[M+0x24])
// and octagonal distance approximation [M+0x60] = hi + lo/2 (sum - min>>1 branch pair).
// Unconditional: no flag test, no equality compare-and-skip (D-59; settarget_004f4e70.txt l.15-31).
void Motion_setTarget(World& w, int pid, const Vec3& t) {
    PlayerState& p = w.players[pid];
    p.target.x = t.x;
    p.target.z = t.z;
    p.targetRef = 0;
    p.moveFlags &= ~1u; // byte 0x5E &= ~1 (exe alt-target clear)
    setTargetDelta(p, t);
}

// 0x4CB7FC (EAX = M, EDX = target): alternative target M+0x40 with flag 1; true when within 8.
bool Motion_setTargetAlt(World& w, int pid, const Vec3& t) {
    PlayerState& p = w.players[pid];
    if (!(p.moveFlags & 1) || p.altTarget.x != t.x || p.altTarget.z != t.z) {
        p.altTarget.x = t.x;
        p.altTarget.z = t.z;
        p.targetRef = 1;
        p.moveFlags = (p.moveFlags | 1) & ~4u;
        setTargetDelta(p, t);
        if (p.targetDist < 8) return true;
    }
    return false;
}

// FUN_004F4F80 (WC98; FIFA-99 name Motion_warpTo cites 0x4CBA78 — off by 0x174): a pure snap.
// pos int+frac store, refreshBall x2, atan2 -> facingDesired [M+0x7C], copy angleBall
// [M+0x70] -> turnDelta [M+0x80], updateHeadAim tail. NO flag bit 8, NO setTarget call,
// NO speed/m98/m9c zeroing (D-60; dossier warpto_004f4f80.txt l.10-23).
void Motion_warpTo(World& w, int pid, const Vec3* pos) {
    PlayerState& p = w.players[pid];
    if (pos) {
        p.posI.x = pos->x;
        p.posI.z = pos->z;
        p.frac.x = 0;
        p.frac.z = 0;
        p.pos.x = static_cast<Fixed>(static_cast<std::uint32_t>(pos->x) << 16);
        p.pos.z = static_cast<Fixed>(static_cast<std::uint32_t>(pos->z) << 16);
    }
    refreshBall(w, p); // FUN_004f31d0 x2: recomputes dist/dx/dz/angleBall [M+0x64..0x70]
    const Vec3 v = {w.ball.tpos.x - p.posI.x, 0, w.ball.tpos.z - p.posI.z}; // l.18-21: atan2(ball-rel dx,dz)
    p.facingDesired = static_cast<std::int32_t>(atan2i(v.x, v.z)); // -> [M+0x7C] facingDesired
    p.turnDelta = p.angleBall;                                      // l.22: copy [M+0x70] -> [M+0x80]
    Motion_updateHeadAim(w, pid, pos != nullptr); // FUN_004fd1c0 tail
    refreshBall(w, p);
}

// WC98 FUN_004F5000 (EAX = M, EDX = pos; D-06 integration entry, pass-17 S5): the band's
// setPosAndTarget - frac-zeroed pos pair, setTarget(pos) inserted before the heading tail,
// facing=facingDesired=atan2(ball) pair (+0x84/+0x88), turnAmount/turnClass zero, dir8 quantize
// of facing, updateHeadAim tail, speed/m98/m9c zero, refreshBall + facingDesired/turnDelta tail.
// Dossier analysis/pass17s5/. Gate: test_sim_scenarios scenarioSetPosAndTarget (D-06).
void Motion_setPosAndTarget(World& w, int pid, const Vec3& pos) {
    PlayerState& p = w.players[pid];
    p.posI.x = pos.x;
    p.posI.z = pos.z;
    p.frac.x = 0;
    p.frac.z = 0;
    p.pos.x = static_cast<Fixed>(static_cast<std::uint32_t>(pos.x) << 16);
    p.pos.z = static_cast<Fixed>(static_cast<std::uint32_t>(pos.z) << 16);
    Motion_setTarget(w, pid, pos); // FUN_004f4e70 site 0x4f5042: target := pos (+delta block)
    refreshBall(w, p);             // l.13-14: refreshBall x2 inside the ECX!=0 branch (D-60 precedent)
    refreshBall(w, p);
    const Vec3 v = {w.ball.tpos.x - p.posI.x, 0, w.ball.tpos.z - p.posI.z}; // FUN_004f3db4 atan2 pair
    const std::int32_t face = static_cast<std::int32_t>(atan2i(v.x, v.z));
    p.facing = face;               // +0x84 = facing (D-62 map row "facing/facingDesired/dir8")
    p.facingDesired = face;        // +0x88
    p.turnAmount = 0;              // +0x98 zeroed with turnClass
    p.turnClass = 0;               // +0x9c
    p.dir8 = ((p.facing + 0x40) & 0x3ff) >> 7; // +0x94 = ((+0x84 + 0x40) & 0x3ff) >> 7
    Motion_updateHeadAim(w, pid, 1);           // FUN_004fd1c0 tail (EBX = 1 site lane)
    p.speed = 0;                   // +0xa0/+0xa4/+0xa8: speed/m98/m9c zero (D-62 map rows)
    p.m98 = 0;
    p.m9c = 0;
    refreshBall(w, p);             // l.30: refreshBall before the +0x7c/+0x80 tail writes
    const Vec3 b = {w.ball.tpos.x - p.posI.x, 0, w.ball.tpos.z - p.posI.z};
    p.facingDesired = static_cast<std::int32_t>(atan2i(b.x, b.z)); // -> [M+0x7C] tail (warpTo pattern)
    p.turnDelta = p.angleBall;                                      // copy [M+0x70] -> [M+0x80]
}

// 0x4CB884 (EAX = M, EDX = pos): position snap without target reset. FIFA-99-derived shape kept
// for the self-snap sites; the WC98 body is not located (D-06 open lane - pass-17 S5 band scan:
// 0x4f4exx holds setTarget/warpTo/setPosAndTarget only, no target-less snap).
void Motion_snapTo(World& w, int pid, const Vec3& pos) {
    PlayerState& p = w.players[pid];
    p.posI.x = pos.x;
    p.frac.x = 0;
    p.frac.z = 0;
    p.posI.z = pos.z;
    p.pos.x = static_cast<Fixed>(static_cast<std::uint32_t>(pos.x) << 16);
    p.pos.z = static_cast<Fixed>(static_cast<std::uint32_t>(pos.z) << 16);
    p.moveFlags &= ~4u;
    const OctDist t = octDistance(pos, targetVec(p)); // octDistance(EDX pos, *M+0x4C) -> M+0x54
    p.targetDist = t.dist;
    p.targetDx = t.dx;
    p.targetDz = t.dz;
    const OctDist d = octDistance(pos, w.ball.posI);
    p.distBall = d.dist;
    p.dxBall = d.dx;
    p.dzBall = d.dz;
    p.angleBall = atan2i(d.dx, d.dz);
    p.distBallPrev = p.distBall;
    Motion_updateHeadAim(w, pid, 1);
}

// 0x4CB9F8: heading to the target, cached in M+0x60 with flag 4 (facing when within 8).
Angle Motion_dirToTarget(World& w, int pid) {
    PlayerState& p = w.players[pid];
    if (p.targetDist < 8) return p.facing;
    if (!(p.moveFlags & 4)) {
        p.targetAngle = atan2i(p.targetDx, p.targetDz);
        p.moveFlags |= 4;
    }
    return p.targetAngle;
}

// 0x4CB47C (EAX = M, EDX = anim ctl, EBX = pos): memset the movement object, place, target self.
void Motion_init(World& w, int pid, const Vec3& pos) {
    PlayerState& p = w.players[pid];
    // memset(M, 0, 0xD8): player+0xB8 .. +0x18F
    p.animRef = 0;
    p.pos = {};
    p.animFacing = p.animFacingDesired = p.turnAgility = 0;
    p.posI = {};
    p.frac = {};
    p.target = {};
    p.altTarget = {};
    p.targetRef = 0;
    p.moveFlags = 0;
    p.targetDist = p.targetDx = p.targetDz = p.targetAngle = 0;
    p.distBall = p.dxBall = p.dzBall = p.angleBall = p.distBallPrev = 0;
    p.facing = p.facingDesired = p.turnDelta = p.headAim = p.dir8 = p.turnAmount = p.turnClass = 0;
    p.speed = p.m98 = p.m9c = p.velAngle = p.speedCap = p.sprintMax = p.runMax = p.mB0 = 0;
    p.decel = p.accel = p.sprintAccel = p.accelRecip = p.sprintAccelRecip = p.decelRecip = 0;
    p.moveReq = p.moveStatus = 0;
    p.arrivalCb = 0;
    p.posI.x = pos.x;
    p.posI.z = pos.z;
    p.pos.x = static_cast<Fixed>(static_cast<std::uint32_t>(pos.x) << 16);
    p.pos.z = static_cast<Fixed>(static_cast<std::uint32_t>(pos.z) << 16);
    Motion_setTarget(w, pid, p.posI);
    p.turnClass = 0;
    p.arrivalCb = 0;
    p.dir8 = ((p.facing + 0x40) & 0x3ff) >> 7;
    refreshBall(w, p);
}

// 0x4CBA60: distance covered in `frames` (int) while changing speed toward `speed`, after the
// turn penalty (|desired facing - heading| >> 5) * (speed >> 18) frames. Returns int units.
std::int32_t Motion_predictDistance(const World& w, int pid, Fixed speed, Angle heading, std::int32_t frames,
                                    int sprint, int ignoreTurnClass) {
    const PlayerState& p = w.players[pid];
    const Fixed v = p.speed;
    const Fixed cap = ignoreTurnClass ? kSpeedCap[0] : kSpeedCap[p.turnClass];
    Fixed acc, rec;
    if (sprint) {
        if (speed > p.sprintMax) speed = p.sprintMax;
        acc = p.sprintAccel;
        rec = p.sprintAccelRecip;
    } else {
        if (speed > p.runMax) speed = p.runMax;
        acc = p.accel;
        rec = p.accelRecip;
    }
    if (speed > cap) speed = cap;
    std::int32_t d = (p.facingDesired - heading) & 0x3ff;
    if (d > 0x200) d = 0x400 - d;
    std::int32_t t = static_cast<std::int32_t>(static_cast<std::uint32_t>(frames - (d >> 5) * (v >> 18)) << 16);
    if (t <= 0) return 0;
    std::int32_t dist = 0;
    const std::int32_t kMin = 0x4ccc; // 0.3: below it the player counts as standing (0x4CBB78)
    if (v == speed) {
        if (speed >= 0x10000) dist += fx(speed, t);
        return dist >> 16;
    }
    const bool decel = v > speed;
    const Fixed tChange = decel ? fx(v - speed, p.decelRecip) : fx(speed - v, rec);
    if (tChange < t) {
        if (v >= kMin || speed >= kMin) dist += fx((v + speed) >> 1, tChange);
        t -= tChange;
        if (speed >= kMin) dist += fx(speed, t);
        return dist >> 16;
    }
    const Fixed vEnd = decel ? v - fx(p.decel, t) : fx(acc, t) + v;
    if (v < kMin && vEnd < kMin) return dist >> 16;
    dist += fx((vEnd + v) >> 1, t);
    return dist >> 16;
}

// 0x4CB1E8 (EAX = M, EDX = sim frames (int), EBX = no-turn flag): consume the position the anim
// step left in M+0x04, derive speed, turn the desired facing toward the target, split position.
void Motion_applyTurnAndRootPos(World& w, int pid, Fixed dt, int ebx) {
    PlayerState& p = w.players[pid];
    Angle face = p.animFacingDesired;      // M+0x14
    Angle animFace = p.animFacing;         // M+0x10
    const Vec3 np = p.pos;
    const Fixed ox = static_cast<Fixed>(static_cast<std::uint32_t>(p.posI.x) << 16) + p.frac.x;
    const Fixed oz = static_cast<Fixed>(static_cast<std::uint32_t>(p.posI.z) << 16) + p.frac.z;
    const std::int32_t len = octLength(np.x - ox, np.z - oz); // 0x4BAD9C
    auto splitXZ = [&] {
        p.posI.x = np.x >> 16;
        p.frac.x = np.x & 0xffff;
        p.posI.z = np.z >> 16;
        p.frac.z = np.z & 0xffff;
        p.moveFlags &= ~4u;
    };
    if (ebx) {
        if (len) splitXZ();
    } else {
        // speed = len / frames in 16.16 (two idiv steps, 0x4CB2C5)
        const std::int64_t div = static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(dt) << 16));
        const std::int32_t q = static_cast<std::int32_t>(len / div);
        const std::int64_t r = len % div;
        const std::int32_t f = static_cast<std::int32_t>((r << 16) / div);
        Fixed spd = static_cast<Fixed>(static_cast<std::uint32_t>(q) << 16) + f;
        if (spd > 0xc0000) spd = 0xc0000;
        std::int32_t d = (face - animFace) & 0x3ff;
        p.turnDelta = 0;
        if (d > 0x200) d = 0x400 - d;
        const auto& th = kTurnClass[p.turnClass];
        const std::int32_t cls = d > th[0] ? (d > th[1] ? 2 : 1) : 0;
        p.turnAmount = d;
        p.turnClass = cls;
        if ((p.anim.flags() & 1) && p.targetDist > 8) { // anim+0x37 bit 0
            const Angle a = Motion_dirToTarget(w, pid);
            if (a != face) {
                std::int32_t diff = (a - face) & 0x3ff;
                if (diff > 0x200) diff -= 0x400;
                const std::int32_t lim = (p.turnAgility + 0xe - (spd >> 16)) * dt;
                if (diff < -lim) diff = -lim;
                else if (diff > lim) diff = lim;
                face += diff;
            }
        }
        face &= 0x3ff;
        if (face > 0x200) face -= 0x400;
        animFace &= 0x3ff;
        if (animFace > 0x200) animFace -= 0x400;
        p.facingDesired = face;
        p.facing = animFace;
        p.dir8 = ((animFace + 0x40) & 0x3ff) >> 7;
        if (len) {
            Motion_setVelocity(w, pid, spd, face); // 0x4BB1A8 (EAX speed, EDX angle, EBX = &M+0x94)
            splitXZ();
        }
    }
    Fixed h = Anim_rootHeight(w, pid) - 0x500000; // 0x4607CC(anim+0x10)
    if (h < 0) h = 0;
    p.posI.y = h >> 16;
    p.pos.y = 0;
    p.frac.y = h & 0xffff;
}

// 0x4CB510 (EAX = M, EDX = sim frames): one motion step, arrival handling, arrival callback, head
// aim and ball distance refresh.
void Motion_update(World& w, int pid, Fixed dt) {
    PlayerState& p = w.players[pid];
    const std::int32_t oldDx = p.targetDx, oldDz = p.targetDz;
    p.moveStatus = 0;
    std::int32_t warpedByCb = 0;
    const std::uint32_t animFlags = p.anim.flags(); // anim+0x37
    auto sgn = [](std::int32_t v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); };
    if (!(animFlags & 8)) {
        Anim_moveStep(w, pid);                         // 0x4CCD80
        Motion_applyTurnAndRootPos(w, pid, dt, (animFlags >> 3) & 1);
        const OctDist t = octDistance(p.posI, targetVec(p));
        p.targetDist = t.dist;
        p.targetDx = t.dx;
        p.targetDz = t.dz;
    } else {
        std::int32_t ebx = static_cast<std::int32_t>(dt); // EDX survives as EBX when bit 0 is clear
        if (animFlags & 1) ebx = Motion_rootMotionPre(w, pid, dt); // 0x4CAB40
        Motion_rootMotionStep(w, pid, dt, ebx);                     // 0x4CAED0
        const OctDist t = octDistance(p.posI, targetVec(p));
        p.targetDist = t.dist;
        p.targetDx = t.dx;
        p.targetDz = t.dz;
        if (t.dist < 0x50 && sgn(oldDx) != sgn(t.dx) && sgn(oldDz) != sgn(t.dz)) {
            if (p.moveFlags & 1) { // passed the alternative target: back to the main one
                p.moveFlags &= ~1u;
                p.targetRef = 0;
                p.moveStatus |= 4;
                Motion_snapTo(w, pid, p.altTarget);
            } else {
                const Vec3 tv = targetVec(p);
                Motion_warpTo(w, pid, &tv);
            }
        } else if ((p.moveFlags & 1) && t.dist < 8) {
            p.moveFlags &= ~1u;
            p.moveStatus |= 4;
            p.targetRef = 0;
        }
    }
    p.moveStatus |= p.moveReq & 0x5010009u;
    if (p.arrivalCb && Player_arrivalCallback(w, pid, p.arrivalCb)) {
        p.pos.x = static_cast<Fixed>(static_cast<std::uint32_t>(p.posI.x) << 16) + p.frac.x;
        warpedByCb = 1;
        p.pos.z = static_cast<Fixed>(static_cast<std::uint32_t>(p.posI.z) << 16) + p.frac.z;
    }
    Motion_updateHeadAim(w, pid, warpedByCb);
    p.distBallPrev = p.distBall;
    const OctDist d = octDistance(p.posI, w.ball.posI);
    p.distBall = d.dist;
    p.dxBall = d.dx;
    p.dzBall = d.dz;
    p.angleBall = atan2i(p.dxBall, p.dzBall);
    p.moveFlags &= ~4u;
}

// 0x4BB1A8 on M+0x94 (EAX speed, EDX angle, EBX = &M+0x94): {speed, speed*sin, speed*cos, angle}.
void Motion_setVelocity(World& w, int pid, Fixed speed, Angle a) {
    PlayerState& p = w.players[pid];
    std::int32_t v[4];
    Math_polarRecord(speed, a, v);
    p.speed = v[0];
    p.m98 = v[1];
    p.m9c = v[2];
    p.velAngle = v[3];
}

// 0x4CBA30 (EAX = M): ball within 0x55 of the facing.
bool Motion_facingBall(World& w, int pid) {
    const PlayerState& p = w.players[pid];
    std::int32_t d = (p.angleBall - p.facing) & 0x3ff;
    if (d > 0x200) d = 0x400 - d;
    return d <= 0x55;
}

// 0x4CBC6C (EAX = M): facing reached the head-aim direction (turning at most +-0x72 while running).
bool Motion_turnSettled(World& w, int pid) {
    const PlayerState& p = w.players[pid];
    if ((p.moveStatus & 1) && p.speed > 0x4ccc) {
        std::int32_t d = (p.headAim - p.facingDesired) & 0x3ff;
        if (d > 0x200) d -= 0x400;
        d = std::clamp(d, -0x72, 0x72);
        return d + p.facingDesired == p.facing;
    }
    return p.facing == p.headAim;
}

// 0x4CAB40 (EAX = M, EDX = sim frames): root-motion anims: turn the desired facing toward the
// target and the facing toward the head aim, classify the turn. Returns 1 when a running turn was
// clamped hard (EBX of 0x4CAED0).
std::int32_t Motion_rootMotionPre(World& w, int pid, Fixed dt) {
    PlayerState& p = w.players[pid];
    const std::uint32_t flag2 = (p.moveFlags >> 1) & 1;
    Angle face = p.facingDesired, cur = p.facing;
    const std::uint32_t req = p.moveReq;
    const int cls = p.turnClass;
    std::int32_t ret = 0, turnDelta = 0, changed = 0;
    const bool slow = p.speed < 0x4ccc;
    const Angle a = Motion_dirToTarget(w, pid);
    auto wrap = [](std::int32_t v) {
        v &= 0x3ff;
        return v > 0x200 ? v - 0x400 : v;
    };
    if (face != a) {
        changed = 1;
        if (slow) {
            face = a;
        } else { // 0x4CAD15
            std::int32_t e = wrap(a - face);
            std::int32_t amt = (a - face) & 0x3ff;
            if (amt > 0x200) amt = 0x400 - amt;
            // WC98 rootpre (FUN_004f3e60 l.52-65) has NO req-bit forced-turn branch: the
            // direction flip is only the headAim block gated by amt > 0x1c7. The FIFA-99
            // 0x180/0x100 re-wrap tested bits that WC98 producers use as cascade cap bits
            // (keeper sets 0x80/0x100), so it is absent here (register D-56).
            if (amt > 0x1c7) {
                const std::int32_t h = wrap(p.headAim - face);
                if (h > 0) {
                    if (e < 0) e += 0x400;
                } else if (h < 0 && e > 0) {
                    e -= 0x400;
                }
            }
            const std::int32_t r = p.turnAgility + 0xe - (p.speed >> 16);
            std::int32_t lim = r * dt, step = e;
            if (req & 0x20) { // l.74: the clamp-turn flag halves the rate limit
                lim >>= 1;
                if (lim < 2) lim = 2;
                step = std::clamp(e, -lim, lim);
            } else if (e > lim) {
                if (e - lim > r * 4) ret = 1;
                step = lim;
            } else if (e < -lim) {
                if (e + lim < -(r * 4)) ret = 1;
                step = -lim;
            }
            face = wrap(face + step);
        }
        p.facingDesired = face;
        p.animFacingDesired = face;
    }
    const Angle tgt = ((req & 0x10) && !slow) ? face : static_cast<Angle>(p.headAim); // l.107
    std::int32_t d = wrap(tgt - cur); // (the 0x180 re-wraps at 0x4CABF6 leave d unchanged)
    if (d != 0) {
        changed = 1;
        std::int32_t lim = (p.turnAgility + 0xe) * dt;
        if (req & 0x20) lim >>= 1; // l.153
        if (slow) turnDelta = d;
        d = std::clamp(d, -lim, lim);
        cur = wrap(cur + d);
        p.facing = cur;
        p.animFacing = cur;
        p.dir8 = ((cur + 0x40) & 0x3ff) >> 7;
    }
    if (changed) {
        const std::int32_t sd = wrap(face - cur);
        std::int32_t amt = (face - cur) & 0x3ff;
        if (amt > 0x200) amt = 0x400 - amt;
        int nc = 0;
        if (amt > kTurnClass[cls][0]) nc = amt > kTurnClass[cls][1] ? 2 : 1;
        if (nc == 1 && sd < 0) p.moveFlags |= 2;
        else p.moveFlags &= ~2u;
        if (nc != cls || ((p.moveFlags >> 1) & 1) != flag2) p.anim.req |= 0x80; // [M+0]+0x18 byte |= 0x80
        p.turnClass = nc;
        p.turnAmount = amt;
    }
    p.turnDelta = turnDelta;
    return ret;
}

// 0x4CAED0 (EAX = M, EDX = sim frames, EBX = 0x4CAB40 result): root-motion step: speed toward the
// capped target speed, advance along the desired facing, run the anim step, height.
void Motion_rootMotionStep(World& w, int pid, Fixed dt, std::int32_t ebx) {
    PlayerState& p = w.players[pid];
    const std::uint32_t req = p.moveReq;
    const std::int32_t dist = p.targetDist;
    const Fixed spd = p.speed;
    Fixed cap = p.speedCap;
    // WC98 cascade on the flags dword [M+0xD8], applied BEFORE the arrival band
    // (cand_rootstep_004f4380 l.30-44): bit 0x40 -> min(cap,0x18000), 0x80 -> 0x40000,
    // the rest of the trio -> 0x68000; only when any of the three bits is set.
    if ((req & 0x1c0) != 0) {
        if (req & 0x40) cap = std::min(cap, Fixed(0x18000));
        else if (req & 0x80) cap = std::min(cap, Fixed(0x40000));
        else cap = std::min(cap, Fixed(0x68000));
    }
    // Arrival band (l.45-54): gate byte[+0x5E] & 1; dist<8 -> 0, else scaled distance, store-if-lesser.
    if (!(p.moveFlags & 1) && dist < 0xa0) {
        if (dist < 8) {
            cap = 0;
        } else {
            const Fixed v = ((dist << 16) >> 5) + 0x10000;
            if (v < cap) cap = v;
        }
    }
    // Sprint/penalty (l.56-78): sprint flag bit 0x100000 selects the ceiling, penalty mask
    // 0x6000000 subtracts [M+0xBC] (mB0). The FIFA-99 consumer read these flags at the old
    // offsets with masks 0x38/0x40; WC98 producers set the bits above (keeper 0x80/0x100,
    // penalty sites player.cpp:862/918/922, run states reqBits(p,0x11)).
    Fixed acc;
    if ((req & 0x100000) == 0) {
        Fixed lim = p.runMax;
        if ((req & 0x6000000) != 0) lim -= p.mB0;
        cap = std::min(cap, lim);
        acc = p.accel;
    } else {
        Fixed lim = p.sprintMax;
        if ((req & 0x6000000) != 0) lim -= p.mB0;
        cap = std::min(cap, lim);
        acc = p.sprintAccel;
    }
    // Class-eval flag mirror (l.79-99): gate byte[+0xD8] & 2.
    if ((req & 2) != 0) {
        std::int32_t amt = (p.headAim - p.facingDesired) & 0x3ff;
        if (amt > 0x200) amt = 0x400 - amt;
        int c = 0;
        if (amt > kTurnClass[p.turnClass][0]) c = amt > kTurnClass[p.turnClass][1] ? 2 : 1;
        if (cap >= (kSpeedCap[c] >> 1)) p.moveStatus |= 2;
    }
    const Fixed clsCap = kSpeedCap[p.turnClass]; // l.100: class cap clamp
    if (cap > clsCap) {
        if (spd >= clsCap) p.moveStatus |= 2;
        cap = clsCap;
    }
    // EBX-collapse gate (l.103): the hard-turn overshoot collapses the cap only when
    // byte[+0xD8] & 4 == 0.
    if (ebx != 0 && (req & 4) == 0) cap = 0;
    const Fixed decel = p.decel;
    // "counts as standing" gate (exe 0x4CBB78, kMin = 0x4ccc): zero the velocity and run
    // the anim step; the ramp ladder below is the exe's LAB_004f455a path.
    if (spd < 0x4ccc && cap < 0x4ccc) {
        p.speed = p.m98 = p.m9c = 0;
        Anim_moveStep(w, pid);
    } else {
        const Vec3 pos16{static_cast<Fixed>(static_cast<std::uint32_t>(p.posI.x) << 16) + p.frac.x, 0,
                         static_cast<Fixed>(static_cast<std::uint32_t>(p.posI.z) << 16) + p.frac.z};
        Fixed step, ns;
        if (spd == cap) {
            step = spd * dt;
            ns = cap;
        } else {
            const Fixed avg = ((spd + cap + cap * 2) >> 2) * dt;
            if (spd > cap) {
                p.anim.req |= 0x40;
                const Fixed n = spd - decel * dt;
                if (n < cap) {
                    ns = cap;
                    step = avg;
                } else {
                    ns = n;
                    step = ((spd + n) >> 1) * dt;
                }
            } else {
                p.anim.req |= 0x20;
                const Fixed n = acc * dt + spd;
                if (n > cap) {
                    ns = cap;
                    step = avg;
                } else {
                    ns = n;
                    step = ((spd + n) >> 1) * dt;
                }
            }
        }
        const Vec3 o = offsetInDirection(pos16, step, p.facingDesired); // 0x4BB0D0
        Motion_setVelocity(w, pid, ns, p.facingDesired);
        Anim_moveStep(w, pid);
        p.posI.x = o.x >> 16;
        p.frac.x = o.x & 0xffff;
        p.posI.z = o.z >> 16;
        p.frac.z = o.z & 0xffff;
        p.pos.x = o.x;
        p.pos.z = o.z;
        p.moveFlags &= ~4u;
    }
    Fixed h = Anim_rootHeight(w, pid) - 0x500000; // 0x4CB09A
    if (h < 0) h = 0;
    p.pos.y = 0;
    p.posI.y = h >> 16;
    p.frac.y = h & 0xffff;
}

} // namespace wc98::sim
