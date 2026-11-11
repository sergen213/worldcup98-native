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

// 0x4CB778 (EAX = M, EDX = target).
void Motion_setTarget(World& w, int pid, const Vec3& t) {
    PlayerState& p = w.players[pid];
    if ((p.moveFlags & 8) || (p.moveFlags & 1) || p.target.x != t.x || p.target.z != t.z) {
        p.target.x = t.x;
        p.target.z = t.z;
        p.targetRef = 0;
        p.moveFlags &= ~0x0du;
        setTargetDelta(p, t);
    }
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

// 0x4CB904 (EAX = M, EDX = pos or null, EBX = point to face or null).
void Motion_warpTo(World& w, int pid, const Vec3* pos, const Vec3* lookAt) {
    PlayerState& p = w.players[pid];
    if (pos) {
        p.posI.x = pos->x;
        p.posI.z = pos->z;
        p.frac.x = 0;
        p.frac.z = 0;
        p.pos.x = static_cast<Fixed>(static_cast<std::uint32_t>(pos->x) << 16);
        p.pos.z = static_cast<Fixed>(static_cast<std::uint32_t>(pos->z) << 16);
    }
    p.moveFlags |= 8;
    Motion_setTarget(w, pid, p.posI);
    if (lookAt) {
        const OctDist d = octDistance(p.posI, *lookAt);
        p.facingDesired = p.facing = atan2i(d.dx, d.dz);
        p.turnAmount = 0;
        p.turnClass = 0;
        p.dir8 = ((p.facing + 0x40) & 0x3ff) >> 7;
    }
    Motion_updateHeadAim(w, pid, pos != nullptr); // EBX = pos argument (0x4CB958)
    p.speed = 0;
    p.m98 = 0;
    p.m9c = 0;
    refreshBall(w, p);
}

// 0x4CB884 (EAX = M, EDX = pos): position snap without target reset.
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
    p.moveFlags |= 8;
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
                Motion_warpTo(w, pid, &tv, nullptr);
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
            if (req & 0x180) {
                if (req & 0x100) {
                    if (e > 0) e -= 0x400;
                } else if (e < 0) {
                    e += 0x400;
                }
            } else if (amt > 0x1c7) {
                const std::int32_t h = wrap(p.headAim - face);
                if (h > 0) {
                    if (e < 0) e += 0x400;
                } else if (h < 0 && e > 0) {
                    e -= 0x400;
                }
            }
            const std::int32_t r = p.turnAgility + 0xe - (p.speed >> 16);
            std::int32_t lim = r * dt, step = e;
            if (req & 4) {
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
    const Angle tgt = ((req & 1) && !slow) ? face : static_cast<Angle>(p.headAim);
    std::int32_t d = wrap(tgt - cur); // (the 0x180 re-wraps at 0x4CABF6 leave d unchanged)
    if (d != 0) {
        changed = 1;
        std::int32_t lim = (p.turnAgility + 0xe) * dt;
        if (req & 4) lim >>= 1;
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
    if (!(p.moveFlags & 1) && dist < 0xa0) {
        if (dist < 8) {
            cap = 0;
        } else {
            const Fixed v = ((dist << 16) >> 5) + 0x10000;
            if (v < cap) cap = v;
        }
    }
    if (ebx != 0 && !(req & 2)) cap = 0;
    if (spd < 0x4ccc && cap < 0x4ccc) {
        p.speed = p.m98 = p.m9c = 0;
        Anim_moveStep(w, pid);
    } else {
        const Fixed decel = p.decel, clsCap = kSpeedCap[p.turnClass];
        if (req & 0x38) {
            if (req & 8) cap = std::min(cap, 0x18000);
            else if (req & 0x10) cap = std::min(cap, 0x40000);
            else cap = std::min(cap, 0x68000);
        }
        Fixed acc;
        if (req & 0x100000) {
            Fixed lim = p.sprintMax;
            if (req & 0x40) lim -= p.mB0;
            cap = std::min(cap, lim);
            acc = p.sprintAccel;
        } else {
            Fixed lim = p.runMax;
            if (req & 0x40) lim -= p.mB0;
            cap = std::min(cap, lim);
            acc = p.accel;
        }
        if (p.moveReq & 1) {
            std::int32_t amt = (p.headAim - p.facingDesired) & 0x3ff;
            if (amt > 0x200) amt = 0x400 - amt;
            int c = 0;
            if (amt > kTurnClass[p.turnClass][0]) c = amt > kTurnClass[p.turnClass][1] ? 2 : 1;
            if (cap >= (kSpeedCap[c] >> 1)) p.moveStatus |= 2;
        }
        if (cap > clsCap) {
            if (spd >= clsCap) p.moveStatus |= 2;
            cap = clsCap;
        }
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
