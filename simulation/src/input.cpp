// Human input layer of FIFA99.EXE v1.1, ported from the disassembly: controller update 0x466D40 /
// 0x4668AC, controller binding 0x4554C0 / 0x466D84 / 0x466E34..0x466E94, the gesture machine
// 0x4E0DAC..0x4E1690, context selection 0x4E0E24, the action queue/dispatch 0x4E3288 / 0x4E330C
// and the 98 action handlers of table 0x58CCF8 (0x4E1AB8..0x4E3258), plus Player_selectReceiver
// 0x4BD4B8. The platform layer supplies ControllerState::padBits (docs/INPUT.md).
//
// Not modelled: the player shouts of actions 31-34 (0x4E3100 -> sound 0x4A26DC) and the
// presentation triggers PTR 0x581BB0. The analog stick path is implemented per FUN_004a9280
// l.35-100 (register D-57); it runs only when ControllerState::analogDevice != 0, which stays 0
// on PC (reader 0x580D64 reports no analog), so the digital path is live in every match.
#include <utility>

#include "player_states.hpp"

namespace wc98::sim {
namespace {
#include "input_tables.inc"

// exe 0x580d6c, 16 x u8: dpad bits -> direction index.
constexpr std::uint8_t kDirIndex[16] = {0, 1, 5, 0, 3, 2, 4, 0, 7, 8, 6, 0, 0, 0, 0, 0};
// exe 0x580d7c / 0x580d85, 9 x i8: direction vectors.
constexpr std::int8_t kDirX[9] = {0, 1, 1, 0, -1, -1, -1, 0, 1};
constexpr std::int8_t kDirZ[9] = {0, 0, -1, -1, -1, 0, 1, 1, 1};
// exe 0x580d8e, 64 x u8: dpad remap by orientation (pad bits 14-15).
constexpr std::uint8_t kDirRemap[64] = {
    0, 8, 4, 12, 1, 9, 5, 13, 2, 10, 6, 14, 3, 11, 7, 15, 0, 2, 1, 3, 8, 10, 9, 11, 4, 6, 5, 7, 12, 14, 13, 15,
    0, 4, 8, 12, 2, 6, 10, 14, 1, 5, 9, 13, 3, 7, 11, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};
// exe 0x5890b4, 7 x 5 x i32: Player_selectReceiver rows {range, minDist, maxDist, maxAngle, shift}.
constexpr std::int32_t kReceiverRow[7][5] = {
    {512, 96, 2400, 199, 4},    {2560, 480, 4800, 171, 4}, {3200, 1920, 5600, 114, 4}, {512, 480, 2400, 171, 3},
    {2080, 480, 3840, 256, 4},  {0, 193, 320, 16000, 16},  {1, 1, 0, 0, 0},
};
constexpr std::uint32_t kButtonsMask = 0x3fe2ff0; // pad button bits

ControllerState& Cn(World& w, int c) { return w.controllers[static_cast<std::size_t>(c)]; }
ControllerState& Cof(World& w, int pid) { return Cn(w, P(w, pid).ctrl - 1); }
TeamState& Tm(World& w, int pid) { return w.teams[static_cast<std::size_t>(P(w, pid).team)]; }
int teamOf(const World& w, int pid) { return P(w, pid).team; }
bool sameTeam(const World& w, int a, int b) { return P(w, a).team == P(w, b).team; }
std::int32_t wrap(std::int32_t a) {
    a &= 0x3ff;
    return a > 0x200 ? a - 0x400 : a;
}
// (player z + v >= 0) == team side, the "attacking half" test used by many handlers.
bool zSideIs(const World& w, int pid, std::int32_t z) { return static_cast<std::int32_t>(z >= 0) == w.teams[P(w, pid).team].side; }
// Pad angle: dpad vector of the player's controller, else its facing (0x4E0F10 pattern).
Angle padAngle(World& w, int pid) {
    const ControllerState& c = Cof(w, pid);
    if (c.dirX != 0 || c.dirZ != 0) return atan2i(c.dirX, c.dirZ);
    return P(w, pid).facing;
}
// 0x4E0F28 pattern: DAT_0065F298 = player, DAT_0065F29C = receiver in the pad direction.
void pickOffBall(World& w, int pid, const Vec3& origin, int type) {
    const Angle a = padAngle(w, pid);
    w.match.igtRequester = pid;
    w.match.offBallControl = Player_selectReceiver(w, origin, pid, teamOf(w, pid), a, type, 0);
}
void setStateSimple(World& w, int pid, int state) { Player_setState(w, pid, state, 0, 0); }
void setPending(World& w, int pid, std::uint32_t code) {
    PlayerState& p = P(w, pid);
    p.pendingCode = (p.pendingCode & 0xffff0000u) | (code & 0xffff);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Controllers

std::uint32_t Input_readPadBits(std::uint32_t pad) { // 0x4664F8 tail: Space 0x1000 -> 0x20000
    if (pad & 0x1000) pad = (pad | 0x20000) & ~0x1000u;
    return pad;
}

int Input_dirToVector(std::uint32_t bits, std::int32_t* x, std::int32_t* z) { // 0x466860
    const int i = kDirIndex[bits & 0xf];
    *x = kDirX[i];
    *z = kDirZ[i];
    return i;
}
// FUN_004a9280 l.35-100 analog pad decode (register D-57). Runs only when the controller's
// analogDevice flag is set; PC keeps it 0, so the digital path above is what matches use.
// Grammar constants (paddecode_004a9280 l.36-100): raw word mask 0x3FE2FF0 (l.36); stick gate
// magnitude < 0x2001 zeroes the vector and mag (l.39-43); scale = mag * 0x50 >> 0x10 (l.47);
// +0x2E u16 = angle - 0x100, +0x2C u16 = (angle - prevAngle) & 0x3ff sign-extended to short
// (l.45-53); unit vector via the sine quarter-table helper FUN_004af940 (quadrant switch on
// DAT_00585898 = kSinQuarter, x = sin, z = cos) with 16.16 round-multiply by scale, bit-15
// rounding (l.54-64); axis deadbands v20 >= 6 -> bit 1, v20 < -5 -> bit 2, v24 >= 6 -> bit 8,
// v24 < -5 -> bit 4 into the bits word (+0x3C) (l.65-80); angle bands into the raw byte (+0x40):
// |a| <= 0xaa -> bit 8, further-out & > 0x155 -> bit 4 (l.81-92), window [0x56, 0x1aa] -> bit 1
// and window (-0x1ab, -0x55) -> bit 2 (l.93-100). The digital path's remap base is DAT_00580d9e.
std::uint32_t Input_analogDecode(World& w, int c) { // 0x4A9280 analog branch (EAX = G)
    ControllerState& k = Cn(w, c);
    std::uint32_t bits = k.raw & kButtonsMask; // l.36 reader word masked (+0x40 := same, l.37)
    if (k.stickMag < 0x2001) {                 // l.39-43 stick gate: no vector, no bands
        k.analogX = k.analogZ = 0;
        k.analogMag = 0;
        return bits;
    }
    const std::int32_t scale = k.stickMag * 0x50 >> 0x10; // l.47
    k.analogMag = k.stickMag;                            // l.45 (+0x28)
    const Angle a10 = static_cast<Angle>(k.stickAngle & 0x3ff);
    std::int32_t da = (a10 - k.stickPrevAngle) & 0x3ff;   // l.48-53 (+0x2C, sign-extended short)
    if (da > 0x200) {
        da -= 0x400;
    }
    k.analogAngle = static_cast<std::uint32_t>(static_cast<std::uint16_t>(da)) |
                    (static_cast<std::uint32_t>(static_cast<std::uint16_t>(k.stickAngle - 0x100)) << 16); // l.46 / +0x2E
    const SinCos sc = sinCos(a10);                       // FUN_004af940 quadrant switch, DAT_00585898
    auto mulRound = [](std::int32_t v, std::int32_t s) { // l.55-62: 16.16 x int with bit-15 round
        const std::int64_t p = static_cast<std::int64_t>(v) * s;
        const auto lo = static_cast<std::uint32_t>(p);
        return static_cast<std::int32_t>((lo >> 0x10) +
                                         (static_cast<int>(static_cast<std::uint64_t>(p) >> 0x20)) * 0x10000 +
                                         (((lo >> 0xf) & 1u) != 0 ? 1 : 0));
    };
    k.analogX = mulRound(sc.s, scale); // l.55-58 (+0x20 = sin * scale)
    k.analogZ = mulRound(sc.c, scale); // l.59-64 (+0x24 = cos * scale)
    if (k.analogX >= 6) {              // l.65-72 deadband into bits (+0x3C)
        bits |= 1;
    } else if (k.analogX < -5) {
        bits |= 2;
    }
    if (k.analogZ >= 6) {              // l.73-80
        bits |= 8;
    } else if (k.analogZ < -5) {
        bits |= 4;
    }
    std::int32_t sa = a10 & 0x3ff;                       // l.81-84 signed 10-bit angle for bands
    if (sa > 0x200) {
        sa -= 0x400;
    }
    std::uint8_t ang = static_cast<std::uint8_t>(bits);  // byte (+0x40) := raw low byte, l.37
    if (sa < -0xaa || 0xaa < sa) {                       // l.85-92 outer band
        if (sa < -0x155 || 0x155 < sa) {
            ang |= 4;
        }
    } else {
        ang |= 8;                                        // inner window -> bit 8
    }
    if (sa < 0x56 || 0x1aa < sa) {                       // l.93-97 second-window bits
        if (sa < -0x55 && -0x1ab < sa) {
            ang |= 2;
        }
    } else {                                             // l.98-99 [0x56, 0x1aa] -> bit 1
        ang |= 1;
    }
    k.raw = (k.raw & ~0xffu) | ang; // angle bands land in the raw word's low byte (+0x40)
    return bits;                                          // l.119 (+0x3C) with deadband bits
}

void Input_clearEdges(World& w, int c) { // 0x466888
    if (c < 0) return;
    ControllerState& k = Cn(w, c);
    k.pressed = 0;
    k.released = 0;
    k.f050 = 0;
    k.f044 = 0;
}

GestureSnap* Input_snap(World& w, int ref) {
    if (ref < 0) return nullptr;
    ControllerState& k = Cn(w, ref >> 4);
    const int s = ref & 15;
    return s < 8 ? &k.buttons[s].snap : nullptr; // WC98 gesture block = buttons[8] only (0x5C..0xDC)
}

void Input_bindGestureTables(World& w, int c, int ctx, int devType) { // 0x4E0DAC (EAX = G, EDX ctx, EBX dev)
    ControllerState& k = Cn(w, c);
    k.maskDev = devType;
    k.gestureCtx = ctx;
    for (GestureButton& b : k.buttons) {
        b.snap.event = 0xff;
        b.frames = 0;
        b.count = 0;
        b.snap.lastFrames = 0;
        b.snap.prevCount = 0;
        b.state = 0;
    }
}

// 0x4E0D90: context id of the bound entry (byte +8), -1 when none.

void Input_setGestureContext(World& w, int c, int ctx) { // 0x4E1598
    ControllerState& k = Cn(w, c);
    if (ctx == -1) {
        k.gestureCtx = -1;
        return;
    }
    const int old = k.gestureCtx;
    if (old < 0) { // 0x4E162B: first bind, every button waits for release
        k.gestureCtx = ctx;
        for (GestureButton& b : k.buttons) b.state = 0xc;
        return;
    }
    if (old == ctx) return;
    k.gestureCtx = ctx;
    std::uint8_t keep = kCtxKeep[old][ctx];
    if (keep == 0xff) return;
    for (GestureButton& b : k.buttons) {
        if (!(keep & 1)) b.state = 0xc;
        keep >>= 1;
    }
}

void Input_initControllers(World& w) { // 0x466D84
    for (int i = 0; i < kMaxControllers; ++i) {
        Cn(w, i) = ControllerState{};
        ControllerState& k = Cn(w, i);
        k.analogDevice = 0;
        Input_bindGestureTables(w, i, 0, k.devType);
    }
}

// 0x4554C0 (EAX = controller, EDX = team 0/1 or other = unbind, ECX = device type; 6 stack args
// unused): bind the controller to the team-mate nearest the ball (match start, Match_run 0x40A6ED
// via 0x466600).
void Input_assignController(World& w, int c, int team, int devType) {
    if (c < 0 || c >= kMaxControllers) return;
    ControllerState& k = Cn(w, c);
    k.flags1a0 = ((k.flags1a0 | 0x1b) & 0xdbu & 0xffu) | (k.flags1a0 & ~0xffu);
    k.devType = devType;
    if (k.player != 0) { // 0x455583
        TeamState& t = w.teams[static_cast<std::size_t>(teamOf(w, k.player - 1))];
        --t.humans;
        P(w, k.player - 1).ctrl = 0;
        k.player = 0;
    }
    if (team == 0 || team == 1) {
        const int pid = Team_nearestPlayer_4522B0(w, w.ball.posI, team, 0xff);
        if (pid >= 0) {
            ++w.teams[static_cast<std::size_t>(teamOf(w, pid))].humans;
            P(w, pid).ctrl = c + 1;
            k.player = pid + 1;
        }
        Team_initMatch(w, team);
    }
    w.match.flags278 |= 8;
    w.match.victimCtrl = 0;
    w.match.offenderCtrl = 0;
    Input_bindGestureTables(w, c, 0, k.devType);
}

// 0x466DE4 (EAX a, EDX b): swap the controllers of two controlled players.
static void swapControl(World& w, int a, int b) {
    if (a < 0 || b < 0) return;
    PlayerState& A = P(w, a);
    PlayerState& B = P(w, b);
    if (A.ctrl == 0 || B.ctrl == 0) return;
    std::swap(A.ctrl, B.ctrl);
    Cn(w, B.ctrl - 1).player = b + 1;
    Cn(w, A.ctrl - 1).player = a + 1;
    std::swap(A.padOffX, B.padOffX);
    std::swap(A.padOffZ, B.padOffZ);
}

// 0x466E34 (EAX n, EDX team): the (n mod humans)-th controller driving a player of the team.
static int nthController(World& w, int n, int team) {
    const std::int32_t humans = w.teams[static_cast<std::size_t>(team)].humans;
    if (humans < 1) return -1;
    std::int32_t k = n % humans;
    for (int c = 0; c < kMaxControllers; ++c) {
        const int pl = Cn(w, c).player;
        if (pl == 0 || teamOf(w, pl - 1) != team) continue;
        if (k <= 0) return c;
        --k;
    }
    return -1;
}

void Input_giveControlToNth(World& w, int n, int pid) { // 0x466E94
    const int c = nthController(w, n, teamOf(w, pid));
    if (c < 0 || c + 1 == P(w, pid).ctrl) return;
    if (P(w, pid).ctrl == 0) Player_transferControl(w, Cn(w, c).player - 1, pid);
    else swapControl(w, Cn(w, c).player - 1, pid);
}

// ---------------------------------------------------------------------------------------------
// Gestures

int Input_buttonGesture(World& w, int c, Fixed dt, std::uint32_t buttons, int* ref) { // 0x4E1690
    ControllerState& k = Cn(w, c);
    if (k.gestureCtx < 0) return 0;
    const int table = kCtxTable[k.maskDev][k.gestureCtx];
    for (int b = 0; b < 8; ++b) {
        GestureButton& g = k.buttons[b];
        const std::uint32_t mask = kButtonMask[k.maskDev][b];
        const bool down = (buttons & mask) == mask;
        g.frames += dt;
        bool fire = false;
        auto ev = [&](int e, int st) {
            g.snap.event = static_cast<std::uint32_t>(e);
            g.state = static_cast<std::uint32_t>(st);
        };
        auto restart = [&] { // 0x4E171B: lastFrames = frames, frames = 0, prevCount = count
            g.snap.lastFrames = g.frames;
            g.frames = 0;
            g.snap.prevCount = g.count;
            fire = true;
        };
        auto tap = [&] { // release within 12 frames: count++
            g.snap.lastFrames = g.frames;
            g.frames = 0;
            ++g.count;
            g.snap.prevCount = g.count;
            fire = true;
        };
        auto held = [&] { // 0x4E1725: frames keep running
            g.snap.lastFrames = g.frames;
            g.snap.prevCount = g.count;
            fire = true;
        };
        auto finish = [&] { // back to idle: frames = 0, count = 0, prevCount = old count
            g.snap.lastFrames = g.frames;
            g.frames = 0;
            g.snap.prevCount = g.count;
            g.count = 0;
            fire = true;
        };
        switch (g.state & 0xff) {
        case 0:
            if (down) { ev(0, 1); restart(); } else g.snap.lastFrames = g.frames;
            break;
        case 1: // exe pred l.89-95: held fires at frames > 7 (>= 8), not >= 12 (register D-64)
            if (g.frames <= 7) {
                if (!down) { ev(3, 3); tap(); } else g.snap.lastFrames = g.frames;
            } else { ev(1, 2); held(); }
            break;
        case 2:
            if (down) g.snap.lastFrames = g.frames;
            else { ev(2, 0); finish(); }
            break;
        case 3:
            if (g.frames >= 0xc) { ev(4, 0); finish(); }
            else if (down) { ev(7, 4); restart(); }
            break;
        case 4: // pred: frames > 7 -> ev(0xa, st9) (D-64)
            if (g.frames <= 7) {
                if (!down) { ev(3, 5); tap(); } else g.snap.lastFrames = g.frames;
            } else { ev(0xa, 9); held(); }
            break;
        case 5:
            if (g.frames >= 0xc) { ev(5, 0); finish(); }
            else if (down) { ev(8, 6); restart(); }
            break;
        case 6: // pred: frames > 7 -> ev(0xb, st0xa) (D-64)
            if (g.frames <= 7) {
                if (!down) { ev(6, 7); tap(); } else g.snap.lastFrames = g.frames;
            } else { ev(0xb, 0xa); held(); }
            break;
        case 7: // pred case \a (D-65): the ONLY branch is down -> ev(9, st8); !down continues at
                // LAB_00512514 (lastFrames = frames). WC98 has no timer reset for state 7.
            if (down) { ev(9, 8); restart(); }
            else g.snap.lastFrames = g.frames;
            break;
        case 8: // pred: frames > 7 -> ev(0xc, st0xb) (D-64)
            if (g.frames <= 7) {
                if (!down) { ev(6, 7); tap(); } else g.snap.lastFrames = g.frames;
            } else { ev(0xc, 0xb); held(); }
            break;
        case 9:
            if (down) g.snap.lastFrames = g.frames;
            else { ev(0xd, 0); finish(); }
            break;
        case 0xa:
            if (down) g.snap.lastFrames = g.frames;
            else { ev(0xe, 0); finish(); }
            break;
        case 0xb:
            if (down) g.snap.lastFrames = g.frames;
            else { ev(0xf, 0); finish(); }
            break;
        case 0xc:
            if (!down) {
                g.state = 0;
                g.count = 0;
                g.frames = 0;
            }
            break;
        default: break;
        }
        if (fire) { // 0x4E172E
            const int a = kActionTable[table][g.snap.event & 0xff][b];
            k.lastAction = a;
            *ref = c * 16 + b;
            return a;
        }
    }
    k.lastAction = 0;
    return 0;
}

// 0x4E330C (FUN_005142a0, 54 B): WC98 dispatch suppresses an action iff the single gate table
// byte (&DAT_0058c750)[action] == 1 AND byte[player+0x83] & 4 (animation in progress) — and then
// DROPS the action: there is no queue store anywhere on the pad path (register D-67; the FIFA-99
// FIFO + queue[4]/queueCount/queueLock tail fields do not exist in WC98's 0x15C-byte record).
void Input_dispatchAction(World& w, int c, int action, int ref) { // 0x4E330C
    const ControllerState& k = Cn(w, c);
    const int pid = k.player - 1;
    if (pid < 0) return;
    const int a = action & 0xff;
    // Jump table has exactly 96 entries (a00 = NULL stub): actions >= 96 never dispatch.
    if (a < 96 && kActionQueueGate[a] == 1 && (P(w, pid).anim.flags() & 4)) return;
    Input_runAction(w, pid, a, ref);
}

bool Player_inShootingZone(const World& w, int pid) { // 0x4E19F8
    if (w.match.phase != 9) return false;
    const PlayerState& p = P(w, pid);
    const std::int32_t lim = w.pitch.penAreaLineZ - (iabs(p.posI.x) >> 1);
    if (iabs(p.posI.z) <= lim) return false;
    const TeamState& t = w.teams[static_cast<std::size_t>(p.team)];
    if (static_cast<std::int32_t>(p.posI.z >= 0) == t.side) return false;
    if (t.f1b8 >= 0) return false;
    if (p.ctrl == 0) return true;
    return (w.controllers[static_cast<std::size_t>(p.ctrl - 1)].flags1a0 & 8) != 0;
}

// 0x4BD4B8 (EAX origin, EDX player, EBX angle, ECX team, stack type, dist): best receiver of a
// pass of `type` from `origin` in direction `angle` (-1 none).
int Player_selectReceiver(World& w, const Vec3& origin, int pid, int team, Angle angle, int type, std::int32_t dist) {
    const PlayerState& p = P(w, pid);
    const int slot = p.curSlot;
    std::int32_t range;
    if (type == 4 && p.ctrl != 0) {
        const ControllerState& c = Cof(w, pid);
        Vec3 t{0, 0, w.pitch.halfLength - 0x2d0};
        if (p.posI.z < 0) t.z = -t.z;
        t.x += c.dirX * 0x280;
        t.z += c.dirZ * 0x190;
        const OctDist v = Math_vecTo(p.posI, t);
        range = v.dist;
        angle = atan2i(v.dx, v.dz);
    } else if (type == 6) {
        range = dist << 2;
        type = 1;
    } else {
        range = kReceiverRow[type][0];
    }
    const std::int32_t* row = kReceiverRow[type];
    const std::int32_t minD = row[1], maxD = row[2], maxAngle = row[3], shift = row[4];
    std::uint32_t best = 0x200000;
    int found = -1;
    const TeamState& T = w.teams[static_cast<std::size_t>(team)];
    for (int i = 0; i < T.playerCount; ++i) {
        const int q = T.players[i];
        if (q < 0) continue; // empty roster slot (-1 placeholder): not a candidate receiver
        const PlayerState& o = P(w, q);
        if ((o.flags22c & 2) || o.curSlot == slot || (o.flags22c & 1)) continue;
        int keeper = 0;
        if (o.slot == 0) {
            keeper = 1;
            if (type != 0) continue;
        }
        const std::int32_t dx = o.posI.x - origin.x, dz = o.posI.z - origin.z;
        const std::int32_t d = octLength(dx, dz);
        if (d > maxD || d < minD) continue;
        std::int32_t ad = iabs(wrap(atan2i(dx, dz) - angle));
        bool press = false;
        if (o.flags22c & 0x200000) {
            const std::int32_t m = w.teams[static_cast<std::size_t>(o.team)].f378;
            press = m == 3 || m == 2;
        }
        if (press) ad >>= 1;
        if (ad > maxAngle) continue;
        std::uint32_t score;
        if (press) {
            score = 0;
        } else {
            score = static_cast<std::uint32_t>(ad + (iabs(d - range) >> shift));
            if (keeper) score += score >> 1;
        }
        if (score < best) {
            found = q;
            best = score;
        }
    }
    return found;
}

// 0x4E0E24 (EAX player, EDX dt, EBX buttons): per-frame context selection and the instant actions.
void Input_selectContext(World& w, int pid, Fixed, std::uint32_t) {
    PlayerState& p = P(w, pid);
    const int c = p.ctrl - 1;
    ControllerState& k = Cn(w, c);
    MatchState& m = w.match;
    TeamState& T = Tm(w, pid);
    int edi = 0, ebp = 0, type = 0;
    if (k.bits & 0x10) type = Player_inShootingZone(w, pid) ? 4 : 1;
    if (k.pressed & 0x20000) {
        Input_dispatchAction(w, c, 0x45, -1);
    } else if ((k.bits & 0x400) && (k.bits & 0x800)) {
        p.flags22c &= ~0x20000u;
        ebp = 1;
    } else if ((k.bits & 0x400) && pid == w.ball.possessor) {
        if (m.phase == 9) {
            p.flags22c |= 0x20000;
            edi = 1;
        } else {
            p.flags22c &= ~0x20000u;
        }
    } else if ((k.bits & 0x800) && pid == w.ball.possessor) {
        if (m.phase == 9) {
            P(w, w.ball.possessor).flags22c |= 0x20000;
            edi = 0;
        } else {
            p.flags22c &= ~0x20000u; // 0x4E1003: possessor (== player) bit cleared
        }
    } else {
        p.flags22c &= ~0x20000u;
    }
    if (m.phase == 9 && k.devType == 1 && ((k.pressed >> 16) & 0x3fc)) { // keyboard tactics keys
        const std::uint32_t b = (k.pressed >> 16) & 0xff;
        if (b & 4) Input_dispatchAction(w, c, 0x48, -1);
        else if (b & 8) Input_dispatchAction(w, c, 0x47, -1);
        else if (b & 0x10) Input_dispatchAction(w, c, 0x46, -1);
        else if (b & 0x20) Input_dispatchAction(w, c, 0x49, -1);
    }
    if (ebp == 0 && (k.bits & 0x800)) { // R2: take control of the keeper when defending
        const int poss = w.ball.possessor;
        if (!(poss >= 0 && sameTeam(w, poss, pid))) {
            const int kp = Team_findKeeper(w, p.team);
            if (kp >= 0 && w.pl.keeper[P(w, kp).team].ctrl == 0) {
                w.pl.keeper[P(w, kp).team].ctrl = p.ctrl;
                Input_setGestureContext(w, c, -1);
                return;
            }
        }
    } else { // 0x4E107F
        const int kp = Team_findKeeper(w, p.team);
        if (kp >= 0 && w.pl.keeper[P(w, kp).team].ctrl == p.ctrl) w.pl.keeper[P(w, kp).team].ctrl = 0;
    }
    if (ebp && m.phase == 9 && m.periodType != 3) { // 0x4E0EDE: manual off-ball control
        Input_setGestureContext(w, c, 3);
        pickOffBall(w, pid, w.ball.posI, type);
        return;
    }
    const int st = p.state;
    if (st == 0x18a || (p.kick.flags & 4) || st == 0x180 || st == 0x181) {
        Input_setGestureContext(w, c, -1);
        return;
    }
    int ph = m.phase;
    if ((ph == 4 || ph == 6) && p.slot == 0 && m.phaseTeam != p.team) {
        Input_setGestureContext(w, c, -1);
        return;
    }
    if (st != 0x201 && pid == w.ball.possessor && p.slot == 0 && (w.ball.flags & 0x20)) {
        Input_setGestureContext(w, c, -1);
        return;
    }
    ph = m.phase;
    if (ph == 0xd || ph == 0x13 || ph == 0xa || (m.status & 0x10)) {
        Input_setGestureContext(w, c, 0xa);
        return;
    }
    const int poss = w.ball.possessor;
    if (ph != 9 && m.setPieceArmed && poss >= 0 && sameTeam(w, poss, pid) &&
        (m.setPieceMode == 2 || m.setPieceMode == 3)) {
        Input_setGestureContext(w, c, 8);
        return;
    }
    if (st == 0x185 || st == 0x189) {
        Input_setGestureContext(w, c, m.setPieceArmed ? 0xd : -1);
        return;
    }
    if (st == 0x201) {
        Input_setGestureContext(w, c, 0xc);
        pickOffBall(w, pid, w.ball.posI, type);
        return;
    }
    if (m.phase != 9) {
        Input_setGestureContext(w, c, (m.setPieceArmed && poss >= 0 && sameTeam(w, poss, pid)) ? 0xb : 5);
        return;
    }
    if (poss >= 0 && poss == pid) { // 0x4E12E5: on the ball
        if ((p.flags22c & 0x20000) && (k.flags1a0 & 2)) {
            Input_setGestureContext(w, c, edi ? 1 : 2);
            return;
        }
        Input_setGestureContext(w, c, 0);
        pickOffBall(w, pid, w.ball.posI, type);
        if (k.devType == 1) {
            const std::uint32_t b = (k.pressed >> 16) & 0xff;
            if (b & 0x40) Input_dispatchAction(w, c, 0x2a, -1);
            else if (b & 0x80) Input_dispatchAction(w, c, 0x2b, -1);
        }
        return;
    }
    if (poss >= 0 && !sameTeam(w, poss, pid)) {
        Input_setGestureContext(w, c, 5);
        return;
    }
    std::int32_t h = w.ball.apexHeight;
    if (h <= w.ball.posI.y) h = w.ball.posI.y;
    if (!(h < 0x55 || (st == 0x304 && (p.anim.flags() & 4)))) {
        Input_setGestureContext(w, c, 4);
        return;
    }
    if (pid == T.f1e0 || pid == m.player2a0 || st == 0x30b || st == 0x307) { // 0x4E141F
        Input_setGestureContext(w, c, 6);
        pickOffBall(w, pid, w.ball.landing, type);
        return;
    }
    if (pid == T.f1b8) { // 0x4E1507
        Input_setGestureContext(w, c, 9);
        if (k.devType == 1 && ((k.pressed >> 16) & 0xc0)) Input_dispatchAction(w, c, 0x2c, -1);
        return;
    }
    const int lt = w.ball.lastToucher;
    if (pid == lt && w.ball.consumed <= 0x14) {
        if (zSideIs(w, pid, P(w, lt).posI.z + w.pitch.thirdLineZ[T.side])) {
            Input_setGestureContext(w, c, -1);
            return;
        }
        if (iabs(p.posI.x) >= w.pitch.penAreaHalfWidth ||
            iabs(p.posI.z) <= w.pitch.halfLength - w.pitch.penAreaDepth) {
            Input_setGestureContext(w, c, -1);
            return;
        }
    }
    Input_setGestureContext(w, c, 7);
}

void Input_processController(World& w, int c, Fixed dt) { // 0x4668AC
    ControllerState& k = Cn(w, c);
    MatchState& m = w.match;
    k.raw = Input_readPadBits(k.padBits); // [0x580D60] = 0x4664F8
    // Path select (l.31): digital on PC, analog grammar when a stick device is attached (D-57).
    std::uint32_t bits;
    if (k.analogDevice == 0) { // digital path 0x466BCD: remap base DAT_00580d9e = kDirRemap
        bits = (k.raw & kButtonsMask) |
               kDirRemap[(k.raw & 0xf) + (((k.raw >> 14) & 3) << 4)];
    } else { // l.35-100 analog branch; masked raw feeds the angle-band byte (l.36-37)
        k.raw &= kButtonsMask;
        bits = Input_analogDecode(w, c);
    }
    const std::uint32_t now = bits & kButtonsMask;
    const std::uint32_t old = k.bits & kButtonsMask;
    // pause / skip request 0x2000 (FUN_004a95c0 / FUN_004a9620 via paddecode l.105-117, D-68):
    if ((bits & 0x2000) && k.player != 0) { // l.105 bit test + [G+0x154] player ptr non-null
        const int pid = k.player - 1;
        // l.106-107 (mirror block DAT_0064015c/9c): phase == 0xd AND the per-player replay dword
        // (exe [+0x774]) nonzero -> only the cam matrix below can set. The port maps that gate to
        // m.replayWanted[opponent] (DAT_0065F25C, set by Rules_checkBallOut on goals); no producer
        // for the exe dword itself exists in the ported state.
        const bool replayActive =
            m.phase == 0xd &&
            m.replayWanted[static_cast<std::size_t>(Tm(w, pid).opponent)] != 0;
        if (!replayActive) {
            m.flags278 |= 0x2000; // l.115: set is unconditional outside an active replay (D-68)
        } else {
            // l.108-112 matrix over the replay-cam controllers DAT_006401a8 / DAT_006401ac (port
            // fields m.replayCamA/B = ctrl id + 1, 0 = null): set allowed when cam A == 0 and cam
            // B tracks this player, or cam A != 0 and does NOT track it.
            if (m.replayCamA == 0) {
                if (m.replayCamB > 0 && Cn(w, m.replayCamB - 1).player - 1 == pid) {
                    m.flags278 |= 0x2000; // l.109 fallthrough arm
                }
            } else if (Cn(w, m.replayCamA - 1).player - 1 != pid) { // l.111 negation arm
                m.flags278 |= 0x2000;
            }
        }
    }
    k.bits = bits;
    const std::uint32_t x = now ^ old;
    k.pressed = x & now;
    k.released = x & old;
    if (now != 0) {
        if (static_cast<std::uint32_t>(k.holdFrames) < 0xfa) k.holdFrames += dt;
    } else if (k.released == 0) {
        k.holdFrames = 0;
    }
    k.dir = Input_dirToVector(bits, &k.dirX, &k.dirZ);
    k.rawDir = Input_dirToVector(k.raw, &k.rawDirX, &k.rawDirZ);
    Input_selectContext(w, k.player - 1, dt, now);
    int ref = -1;
    const int action = Input_buttonGesture(w, c, dt, now, &ref); // l.137-141 gesture machine -> pred -> dispatch
    if (action == 0) return; // pred returns '\0' -> no dispatch (paddecode l.137-141)
    Input_dispatchAction(w, c, action, ref);
}

void Input_updateControllers(World& w) { // 0x466D40
    w.match.flags278 &= ~0x2000u;
    for (int c = 0; c < kMaxControllers; ++c)
        if (Cn(w, c).player != 0) Input_processController(w, c, w.time.simFrames);
}

// ---------------------------------------------------------------------------------------------
// Action handlers (table 0x58CCF8). EAX = player, EDX = gesture snapshot (ref).

namespace {

// 0x4E26D4 (EDX hold frames): kick power 1.3 - clamp(hold / 40, 0.3, 1.0).
void setKickPowerFromHold(World& w, int pid, std::int32_t hold) {
    std::int32_t f = fdiv(static_cast<std::int32_t>(static_cast<std::uint32_t>(hold) << 16), 0x280000);
    if (f < 0x4ccc) f = 0x4ccc;
    else if (f > 0x10000) f = 0x10000;
    P(w, pid).kickPower = 0x14ccc - f;
}

// 0x4E2244 (EDX code): queue a one-touch kick for the team-mate the ball is going to.
void queueOneTouch(World& w, int pid, int code) {
    const int c = P(w, pid).ctrl;
    TeamState& T = Tm(w, pid);
    int q = T.oneTouch;
    if (q < 0) q = T.f1e0;
    if (q < 0) q = Team_nearestPlayer_4522A0(w, w.ball.landing, P(w, pid).team, P(w, pid).curSlot, 1);
    if (q >= 0 && q != pid) {
        Player_transferControl(w, pid, q);
        pid = q;
    }
    if (P(w, pid).anim.flags() & 4) return;
    if (Cn(w, c - 1).gestureCtx != 7 && pid == Tm(w, pid).oneTouch) return;
    TeamState& U = Tm(w, pid);
    U.oneTouch = pid;
    if (w.match.player2a0 < 0) w.match.player2a0 = pid;
    U.oneTouchCode = code;
    U.oneTouchTimer = 0x64;
    if (fx(w.ball.speed, 0xc) > P(w, pid).distBall) setStateSimple(w, pid, 0x309);
    else setStateSimple(w, pid, 0x302);
}

// 0x4E1B54 (EDX code, EBX action): header/volley at the landing point by the nearest team-mate.
void airAction(World& w, int pid, int code, int) {
    const int q = Team_nearestPlayer_4522A0(w, w.ball.landing, P(w, pid).team, P(w, pid).curSlot, 1);
    if (q < 0) return;
    if (q == pid) {
        if (P(w, pid).anim.flags() & 4) return;
    } else {
        Player_transferControl(w, pid, q);
        pid = q;
        if (P(w, pid).anim.flags() & 4) return;
    }
    setPending(w, pid, static_cast<std::uint32_t>(code));
    setStateSimple(w, pid, 0x303);
}

// 0x4E1C20 (EDX code, EBX action): first-time kick on the y = 85 descent (0x669190).
void volleyAction(World& w, int pid, int code, int /*action*/) { // EBX action only feeds the queue store (D-67)
    const int q = Team_nearestPlayer_4522A0(w, w.ball.cross85Pos, P(w, pid).team, P(w, pid).curSlot, 1);
    if (q < 0) return;
    if (q != pid) {
        Player_transferControl(w, pid, q);
        pid = q;
    }
    if (P(w, pid).anim.flags() & 4) {
        return; // WC98 drops the action with an animation in progress; the exe's queue store here is
                // FE-path thunk FUN_0049a890 (presentation), out of the pad scope (register D-67).
    }
    setPending(w, pid, static_cast<std::uint32_t>(code));
    Player_executeKick(w, pid, code, 0x55);
    P(w, pid).kick.deferredCode = code;
    setStateSimple(w, pid, 0x30a);
}

// 0x4E2994: manual switch unless on the ball / the set-piece partner outside play.
void switchManual(World& w, int pid) {
    if (pid == w.ball.possessor) return;
    if (w.match.phase != 9 && pid == w.match.setPiecePartner) return;
    Player_switchControl(w, pid, 1);
}

void skillMove(World& w, int pid, std::uint32_t code) {
    setPending(w, pid, code);
    setStateSimple(w, pid, 0x30d);
}

// 0x4E0938 (EDX = flag): skill move by the angle between facing and the goal.
std::uint32_t skillByGoalAngle(const World& w, int pid, bool flag) {
    const PlayerState& p = P(w, pid);
    const std::int32_t dz = w.teams[static_cast<std::size_t>(p.team)].side == 1 ? -w.pitch.halfLength - p.posI.z
                                                                                : w.pitch.halfLength - p.posI.z;
    const std::int32_t d = wrap(p.facing - atan2i(-p.posI.x, dz));
    if (d > 0x100 || d < -0x100) return flag ? 0x77 : 0x76;
    if (d >= 0) return flag ? 0x6f : 0x6e;
    return flag ? 0x6d : 0x70;
}

// Dribble/turn kick of actions 6/65/66/68 (0x4E1DAC pattern): push the ball `r` ahead.
void dribblePush(World& w, int pid, std::int32_t r, Angle a, std::int32_t ecx, std::int32_t a3) {
    std::int32_t v[4];
    Math_polarRecord(r, a, v);
    Kick_planToPoint(w, pid, v, nullptr, ecx, -1, 0, 0xc, a3, 0, 0);
    if (pid == w.ball.possessor) w.ball.dribbler = pid;
    setStateSimple(w, pid, 0x300);
}

// 0x536704: next outfield team-mate after this player (wrapping), else players[0].
int nextOutfield(const World& w, int pid) {
    const TeamState& T = w.teams[static_cast<std::size_t>(P(w, pid).team)];
    const int idx = P(w, pid).index;
    for (int k = idx + 1; k < T.playerCount; ++k)
        if (P(w, T.players[k]).slot != 0) return T.players[k];
    for (int k = 0; k <= idx; ++k)
        if (P(w, T.players[k]).slot != 0) return T.players[k];
    return T.players[0];
}

// 0x4E2DAC (EDX code 0x53..0x5A): set-piece kick by the taker / restart partner.
void setPieceKick(World& w, int pid, int code) {
    MatchState& m = w.match;
    if (m.setPieceMode != 2) {
        if (pid != w.ball.possessor) return;
    } else if (pid != m.setPiecePartner) {
        return;
    }
    if (m.setPieceMode == 2 && m.phase == 1) {
        Player_executeKick(w, w.ball.possessor, 0x19, 0);
        m.setPieceArmed = 0;
        return;
    }
    KickVec v{};
    std::int32_t ecx = 0, y = 0;
    if (m.setPieceMode == 2) {
        if (code == 0x5a || code == 0x59) return;
        const int c = (code == 0x55 || code == 0x53 || code == 0x57) ? 1 : 2;
        Kick_aimTo(w, w.ball.possessor, &v, 0, c, 0, pid);
    } else {
        const OctDist od = octDistance(w.ball.posI, m.setPieceAim);
        v.dist = od.dist;
        v.dx = od.dx;
        v.dz = od.dz;
    }
    const int ph = m.phase;
    if (ph == 1) {
        if (v.dist < 0xc0) v.dist = 0xc0;
        else if (v.dist > 0x640) v.dist = 0x640;
        ecx = v.dist >> 6;
    } else {
        if (v.dist < 0x40) v.dist = 0x40;
        else if (v.dist > 0xb40) v.dist = 0xb40;
        ecx = 0;
    }
    {
        std::int32_t r[4];
        Math_polarRecord(v.dist, atan2i(v.dx, v.dz), r);
        v = {r[0], r[1], r[2], r[3]};
    }
    if (code == 0x58 && m.phase == 1) ecx += v.dist >> 5;
    int ebx, edi;
    if (code == 0x55 || code == 0x56) {
        ecx += m.phase == 1 ? v.dist >> 5 : v.dist / 12;
        edi = 2;
        ebx = 3;
    } else if (code == 0x53 || code == 0x54) {
        if (m.phase == 1) {
            y = 0x55;
            ecx += (v.dist >> 3) + 0x55;
        } else {
            ecx += v.dist / 6;
        }
        edi = 1;
        ebx = 1;
    } else {
        ebx = 2;
        edi = 0;
    }
    std::int32_t spin = 0;
    if (m.phase == 1) {
        ebx = 9;
        edi = 0xb;
    } else {
        spin = (w.pl.sp.spin * 8000) / 0xab;
    }
    const std::int32_t r[4] = {v.dist, v.dx, v.dz, v.angle};
    Kick_planToPoint(w, w.ball.possessor, r, nullptr, ecx, y, spin, ebx, edi, 0, 3);
    m.setPieceArmed = 0;
}

// 0x4E2E14: with the IGT option (controller flag 0x10) the partner becomes the pass-back target.
void partnerPassBack(World& w, int pid) {
    const MatchState& m = w.match;
    if (m.setPieceMode != 2 || pid != m.setPiecePartner) return;
    if (!(Cof(w, m.setPiecePartner).flags1a0 & 0x10)) return;
    Tm(w, m.setPiecePartner).f1b8 = w.ball.possessor;
    Tm(w, pid).f200 = 0;
}

void tactic(World& w, int pid, void (*fn)(World&, int)) { // 0x4E31C8 pattern
    if (w.match.phase != 9 || w.match.periodType == 3) return;
    fn(w, P(w, pid).team);
    Tm(w, pid).w37c[16] = Cof(w, pid).index; // team +0x3BC = controller index
}

void kickCode(World& w, int pid, int code) { Player_requestKick(w, pid, code); }

} // namespace

// 0x4E28CC (EDX code): human kick request.
void Player_requestKick(World& w, int pid, int code) {
    PlayerState& p = P(w, pid);
    if (w.match.phase != 9 || !(p.flags22c & 0x200)) return;
    const int poss = w.ball.possessor;
    if (poss >= 0) {
        if (pid != poss) return;
        Ball_takePossession(w, poss, 0);
        Player_executeKick(w, pid, code, w.ball.posI.y);
        setStateSimple(w, pid, 0x300);
        return;
    }
    if (p.distBall > 0xa0) { // 0x4E262E: run onto the ball first
        setPending(w, pid, static_cast<std::uint32_t>(code));
        setStateSimple(w, pid, 0x30a);
    }
}

void Input_action56(World& w, int pid, int) { setPieceKick(w, pid, 0x56); } // 0x4E2D50

void Input_runAction(World& w, int pid, int action, int ref) {
    MatchState& m = w.match;
    PlayerState& p = P(w, pid);
    TeamState& T = Tm(w, pid);
    const GestureSnap* s = Input_snap(w, ref);
    switch (action) {
    case 0: break;
    case 1: // 0x4E2780
        setKickPowerFromHold(w, pid, s->lastFrames);
        kickCode(w, pid, Player_inShootingZone(w, pid) ? 0x13 : 4); // (0x4E1A78 result unused)
        break;
    case 2: kickCode(w, pid, 5); break; // 0x4E26C8
    case 3: // 0x4E2950
        kickCode(w, pid, zSideIs(w, pid, p.posI.z - w.pitch.thirdLineZ[T.side]) ? 0x12 : 6);
        break;
    case 4: // 0x4E1AB8
        if (!(p.flags22c & 0x200)) break;
        T.oneTouchTimer = 0;
        T.oneTouch = -1;
        Player_requestSprint(w, pid);
        break;
    case 5: // 0x4E2908
        if (zSideIs(w, pid, p.posI.z)) kickCode(w, pid, 0x12);
        kickCode(w, pid, 2);
        break;
    case 6: // 0x4E27C0
        if (p.speed > 0x28000) {
            dribblePush(w, pid, 0x190, p.facing, 0xc0, 3);
        } else {
            p.speedCap = 0;
            p.facingDesired = wrap(p.facing);
            std::int32_t v[4];
            Math_polarRecord(0, p.facing, v); // EBX = &player+0x14C
            p.speed = v[0];
            p.m98 = v[1];
            p.m9c = v[2];
            p.velAngle = v[3];
            Player_moveToward_4BE804(w, pid, p.posI);
            Motion_setTargetAlt(w, pid, p.posI);
            Keeper_holdOff(w, 0x5a);
            m.player2a0 = pid;
            kickCode(w, pid, 3);
        }
        break;
    case 7: // 0x4E1E7C
        if (m.phase == 9 && (p.flags22c & 0x200)) setStateSimple(w, pid, 0x30c);
        break;
    case 8: // 0x4E2734
        setKickPowerFromHold(w, pid, s->lastFrames);
        kickCode(w, pid, Player_inShootingZone(w, pid) ? 0x14 : 7);
        break;
    case 9: kickCode(w, pid, 8); break;  // 0x4E2774
    case 10: kickCode(w, pid, 1); break; // 0x4E28C4
    case 11: airAction(w, pid, 0xc, 0xb); break; // 0x4E1C0C
    case 12: // 0x4E1BC0
    case 18: { // 0x4E1CB4
        const int tk = m.setPieceTaker;
        if (tk >= 0 && P(w, tk).state == 0x201 && tk == w.ball.possessor) switchManual(w, pid);
        else if (action == 12) airAction(w, pid, 0xd, 0xc);
        else volleyAction(w, pid, 9, 0x12);
        break;
    }
    case 13: airAction(w, pid, 0xe, 0xd); break; // 0x4E1BF8
    case 14: // 0x4E1D58
        if (p.state == 0x30a || p.state == 0x303) Player_dropBallAndReset(w, pid);
        break;
    case 15: switchManual(w, pid); break; // 0x4E2994
    case 16: // 0x4E29B8
        if (m.phase != 9 && pid == m.setPiecePartner) break;
        if (pid != w.ball.possessor) {
            const int r = Player_switchControl(w, pid, 1);
            if (r != 2 || w.ball.possessor >= 0) break;
        }
        T.oneTouch = pid;
        T.oneTouchCode = (s->event & 0xff) != 0 ? 8 : 5;
        T.oneTouchTimer = 0x3c;
        break;
    case 17: volleyAction(w, pid, 0xa, 0x11); break; // 0x4E1D44
    case 19: // 0x4E1CEC
        if (zSideIs(w, pid, p.posI.z + w.pitch.thirdLineZ[T.side]) || (w.ball.kickCount & 3))
            volleyAction(w, pid, 0xb, 0x13);
        else
            airAction(w, pid, 0x11, 0x13);
        break;
    case 20: Player_tryTackle(w, pid, 1); break; // 0x4E1AE4
    case 21: Player_tryTackle(w, pid, 0); break; // 0x4E1AF0
    case 22: Player_tryTackle(w, pid, 2); break; // 0x4E1AF8
    case 23: { // 0x4E1B04
        const int poss = w.ball.possessor;
        if (poss >= 0 && (w.ball.flags & 0x20) && P(w, poss).slot == 0 &&
            (poss == pid || pid == m.setPieceTaker || pid == m.setPiecePartner))
            break;
        if (m.phase == 9 && (p.flags22c & 0x200) && !(w.attrs[p.attr].at(0x40) & 1)) Player_tryTackle(w, pid, 2);
        break;
    }
    case 24: queueOneTouch(w, pid, Player_inShootingZone(w, pid) ? 0x13 : 4); break; // 0x4E2340
    case 25: // 0x4E2378
    case 29: { // 0x4E23FC
        const int lt = w.ball.lastToucher;
        if (w.ball.consumed < 0xa && lt >= 0 && sameTeam(w, lt, pid)) break;
        queueOneTouch(w, pid, action == 25 ? 5 : 8);
        break;
    }
    case 26: queueOneTouch(w, pid, 6); break; // 0x4E23B0
    case 27: queueOneTouch(w, pid, zSideIs(w, pid, p.posI.z) ? 0x12 : 2); break; // 0x4E2434
    case 28: queueOneTouch(w, pid, Player_inShootingZone(w, pid) ? 0x14 : 7); break; // 0x4E23C4
    case 30: queueOneTouch(w, pid, 1); break; // 0x4E2474
    case 31: case 32: case 33: case 34: break; // 0x4E30E4..: player shout (sound only)
    case 35: case 36: case 37: case 38: case 40: case 41: { // 0x4E2558.. IGT kick code
        static constexpr int kCode[] = {5, 4, 6, 1, 0, 8, 7};
        if (m.phase == 9) T.f200 = kCode[action - 35];
        break;
    }
    case 39: // 0x4E25B8
        if (m.phase == 9) T.f200 = zSideIs(w, pid, p.posI.z) ? 0x12 : 2;
        break;
    case 42: // 0x4E2684
        if ((Cof(w, pid).flags1a0 & 0x10) && m.phase == 9 && pid == w.ball.possessor) {
            T.f1b8 = pid;
            T.f200 = 0;
        }
        kickCode(w, pid, 5);
        break;
    case 43: // 0x4E2488
        if (!(Cof(w, pid).flags1a0 & 0x10) || pid != w.ball.possessor || m.phase != 9) break;
        if (m.offBallControl < 0)
            m.offBallControl = Player_selectReceiver(w, w.ball.posI, pid, p.team, padAngle(w, pid), 0, 0);
        if (m.offBallControl >= 0) {
            T.f1b8 = m.offBallControl;
            T.f200 = 0;
            Player_transferControl(w, pid, T.f1b8);
        }
        break;
    case 44: // 0x4E25F4
        if (pid != T.f1b8) break;
        T.f1b8 = -1;
        if (w.ball.possessor >= 0 && sameTeam(w, w.ball.possessor, pid)) Player_transferControl(w, pid, w.ball.possessor);
        break;
    case 45: skillMove(w, pid, 0x81); break; // 0x4E2000
    case 46: skillMove(w, pid, (Cof(w, pid).bits & 0x800) ? 0x7b : 0x7a); break; // 0x4E2040
    case 47: skillMove(w, pid, (Cof(w, pid).bits & 0x800) ? 0x75 : 0x74); break; // 0x4E1F38
    case 48: skillMove(w, pid, 0x74); break; // 0x4E1EA8
    case 49: skillMove(w, pid, 0x75); break; // 0x4E1EC8
    case 50: if (s->lastFrames < 0x1e) skillMove(w, pid, 0x74); break; // 0x4E1EE8
    case 51: if (s->lastFrames < 0x1e) skillMove(w, pid, 0x75); break; // 0x4E1F10
    case 52: skillMove(w, pid, (Cof(w, pid).bits & 0x800) ? 0x7d : 0x7c); break; // 0x4E1FAC
    case 53: skillMove(w, pid, 0x7c); break; // 0x4E1F8C
    case 54: skillMove(w, pid, 0x7d); break; // 0x4E1F6C
    case 55: skillMove(w, pid, 0x7e); break; // 0x4E2074
    case 56: skillMove(w, pid, 0x7f); break; // 0x4E2094
    case 57: skillMove(w, pid, (Cof(w, pid).bits & 0x800) ? 0x7f : 0x7e); break; // 0x4E20B4
    case 58: skillMove(w, pid, 0x81); break; // 0x4E2108
    case 59: skillMove(w, pid, skillByGoalAngle(w, pid, (Cof(w, pid).bits & 0x400) != 0)); break; // 0x4E220C
    case 60: case 61: case 62: skillMove(w, pid, 0x82); break; // 0x4E20E8 / 0x4E2020 / 0x4E1FE0
    case 63: skillMove(w, pid, 0x81); break; // 0x4E2128
    case 64: setStateSimple(w, pid, 0x82); break; // 0x4E1D74
    case 65: // 0x4E2148
        if (p.slot == 0 && Pitch_inPenaltyBox(w, T.side, p.posI) && T.f204 == 0) {
            if (w.pl.keeper[p.team].busy != 0) break;
            setStateSimple(w, pid, 0x208);
            break;
        }
        dribblePush(w, pid, 0x180, p.facingDesired, 0x40, 0xe);
        break;
    case 66: dribblePush(w, pid, 0x168, p.facing, 0xc0, 3); break; // 0x4E1E14
    case 67: skillMove(w, pid, 0x6c); break; // 0x4E1D8C
    case 68: dribblePush(w, pid, 0x244, p.facing, 0x90, 0xf); break; // 0x4E1DAC
    case 69: // 0x4E3178
        if (w.opt.gameMode == 1 || w.opt.gameMode == 3 || m.periodType == 3) break;
        T.timer3c0 = 0x78;
        Team_setFormation(w, p.team, (T.formIdx() + 1) % 3);
        break;
    case 70: tactic(w, pid, Team_armOffsideTrap); break;      // 0x4E31C8
    case 71: tactic(w, pid, Team_startOverlap_4DA758); break; // 0x4E31F8
    case 72: tactic(w, pid, Team_startWideRun_4DADE8); break; // 0x4E3228
    case 73: tactic(w, pid, Team_startPress); break;          // 0x4E3258
    case 74: m.setPieceKind = 0xd; break; // 0x4E2FF8
    case 75: m.setPieceKind = 0xc; break;
    case 76: m.setPieceKind = 4; break;
    case 77: m.setPieceKind = 0xf; break;
    case 78: m.setPieceKind = 0xe; break;
    case 79: m.setPieceKind = 0x10; break;
    case 80: m.setPieceKind = 0xb; break; // 0x4E3040
    case 81: { // 0x4E2A44
        const int st = p.state;
        const bool ok = st == 0x182 || st == 0x188 || st == 0x184 || st == 0x183 || st == 0x18b ||
                        (pid == m.setPiecePartner && m.setPieceMode == 2);
        if (!ok) break;
        if (m.setPieceMode == 1 && w.pl.sp.powerRef >= 0) break;
        SetPiece_cycleMode(w, w.ball.possessor);
        break;
    }
    case 82: // 0x4E2AA0
        if (m.setPieceMode == 2 && pid == m.setPiecePartner) Player_switchControl(w, pid, 1);
        break;
    case 83: setPieceKick(w, pid, 0x53); break; // 0x4E2D58
    case 84: setPieceKick(w, pid, 0x54); break; // 0x4E2D60
    case 85: setPieceKick(w, pid, 0x55); break; // 0x4E2D44
    case 86: setPieceKick(w, pid, 0x56); break; // 0x4E2D50
    case 87: // 0x4E2E70
        partnerPassBack(w, pid);
        if (m.setPieceMode == 2 && pid == m.setPiecePartner) Player_switchControl(w, pid, 1); // 0x4E2D8C
        else setPieceKick(w, pid, 0x59);
        break;
    case 88: // 0x4E2E58
        partnerPassBack(w, pid);
        setPieceKick(w, pid, 0x56);
        break;
    case 89: // 0x4E2D8C
    case 90: // 0x4E2D68
        if (m.setPieceMode == 2 && pid == m.setPiecePartner) Player_switchControl(w, pid, 1);
        else setPieceKick(w, pid, action == 89 ? 0x59 : 0x5a);
        break;
    case 91: { // 0x4E2EB8: power-bar set piece
        if (pid != w.ball.possessor || !m.setPieceArmed || m.setPieceMode != 1) break;
        KickVec v{};
        std::int32_t lift = 0;
        Fixed spin = 0;
        SetPiece_computeKick(w, &v, &lift, &spin);
        int edi = 3, ecx = 1;
        if (m.phase == 1) {
            lift += 0x72;
            ecx = 0xb;
            edi = 9;
        }
        if (lift == 0) {
            edi = 2;
            ecx = 0;
        }
        if (spin == 0) spin = 1;
        const std::int32_t r[4] = {v.dist, v.dx, v.dz, v.angle};
        Kick_planToPoint(w, pid, r, nullptr, lift, -1, spin, edi, ecx, 0, 2);
        m.setPieceArmed = 0;
        break;
    }
    case 92: // 0x4E2E84: start the power gauge on this button
        if (pid != w.ball.possessor || !m.setPieceArmed || m.setPieceMode != 1 || w.pl.sp.powerRef >= 0) break;
        w.pl.sp.powerRef = ref;
        Input_snap(w, ref)->lastFrames = 0;
        break;
    case 93: m.setPieceKind = 0; m.setPieceArmed = 0; break; // 0x4E2F8C
    case 94: break;                                          // 0x4E2F88 (ret)
    case 95: m.setPieceKind = 4; m.setPieceArmed = 0; break; // 0x4E2F70
    case 96: // 0x4E2F9C: hand the penalty to the next outfield player
        if (m.restartType != 4 || static_cast<std::uint32_t>(T.playerCount) <= 2 || m.phase != 6) break;
        Player_dropBallAndReset(w, pid);
        Player_setState(w, nextOutfield(w, pid), 0x185, 0, 0, true);
        break;
    case 97: // 0x4E2FE8
        if (m.setPieceArmed) Penalty_skipRunUp_536D74(w, pid);
        break;
    default: break;
    }
}

// ---------------------------------------------------------------------------------------------
// Set-piece aim module 0x44C00C..0x44CBB6 (aim block w.pl.sp = 0x632F20.., restart globals
// DAT_0065F2B8..0x65F2D8). sp.f20 holds the aiming player as pid + 1 (exe: pointer).

namespace {
// exe 0x57f4d4, 4 x {min, max}: allowed aim angles of state 0x183 by (x > 0, z > 0).
constexpr std::int32_t kCornerAim[2][2][2] = {{{6, 250}, {262, 506}}, {{-249, -5}, {-505, -261}}};
constexpr Vec3 kCentreSpot{0, 0, 0}; // 0x65F38C (never written; zeroed with the pitch block)

// 0x44C230 (EAX player): initial aim point of the restart.
void setPieceAimInit(World& w, int pid) {
    MatchState& m = w.match;
    const PlayerState& p = P(w, pid);
    const std::int32_t edge = w.pitch.halfWidth - 0xf0;
    const std::uint32_t ph = static_cast<std::uint32_t>(m.phase);
    if (ph < 2) {
        if (ph != 1) {
            m.setPieceAim = kCentreSpot;
            return;
        }
        m.setPieceAim = w.ball.posAtTouch; // throw-in: towards the middle from the touchline
        m.setPieceAim.y = 0;
        m.setPieceAim.x = w.ball.posAtTouch.x < 0 ? -edge : edge;
        return;
    }
    if (ph == 2) {
        m.setPieceAim = {0, 0, w.pitch.halfLength - 0x180};
        if (w.ball.posI.x < 0) m.setPieceAim.x = -edge;
        if (w.teams[static_cast<std::size_t>(p.team)].side == 1) m.setPieceAim.z = -m.setPieceAim.z;
        return;
    }
    if (ph == 5) {
        const std::int32_t gz = -w.pitch.goalLineZ[w.teams[static_cast<std::size_t>(p.team)].side];
        std::int32_t dx = -p.posI.x, dz = gz - p.posI.z;
        dx = dx < -0x140 ? -0x140 : dx > 0x140 ? 0x140 : dx;
        dz = dz < -0x140 ? -0x140 : dz > 0x140 ? 0x140 : dz;
        m.setPieceAim = {p.posI.x + dx, 0, p.posI.z + dz};
        return;
    }
    m.setPieceAim = kCentreSpot;
}
} // namespace

void SetPiece_updateAim(World& w) { // 0x44C00C (Sim_frame): pad-driven aim marker
    MatchState& m = w.match;
    const Vec3 old = m.setPieceAim;
    if (m.setPieceAimCtrl == 0 || m.setPieceAimMode != 1) return;
    const ControllerState& c = Cn(w, m.setPieceAimCtrl - 1);
    std::int32_t dx = 0, dz = 0;
    const std::uint32_t d = c.bits & 0xff; // (analog path omitted)
    if (d & 0xf) {
        const std::int32_t step = w.time.simFrames << 4;
        if (d & 1) dx = step;
        else if (d & 2) dx = -step;
        if (d & 8) dz = step;
        else if (d & 4) dz = -step;
    }
    Vec3& a = m.setPieceAim;
    a.y = 0;
    a.x += dx;
    a.z += dz;
    const std::int32_t hx = w.pitch.halfWidth - 0x20, hz = w.pitch.halfLength - 0x20;
    if (-hx > a.x) a.x = -hx;
    else if (hx < a.x) a.x = hx;
    if (-hz > a.z) a.z = -hz;
    else if (hz < a.z) a.z = hz;
    if (m.phase != 3) return;
    if (static_cast<std::int32_t>(a.z > 0) != static_cast<std::int32_t>(w.ball.posI.z > 0)) return;
    const std::int32_t line = w.pitch.halfLength - w.pitch.penAreaDepth;
    if (iabs(a.x) < w.pitch.penAreaHalfWidth && iabs(a.z) > line) a.z = old.z;
    if (iabs(a.z) > line && iabs(a.x) < w.pitch.penAreaHalfWidth) a.x = old.x;
}

void SetPiece_begin(World& w, int pid, Angle angle, int) { // 0x44C3E0 (EBX = presentation flag)
    MatchState& m = w.match;
    if (m.phase != m.restartType) return;
    PlayerState& p = P(w, pid);
    p.stateTimer = 0;
    setPieceAimInit(w, pid);
    Ball_takePossession(w, pid, 0);
    m.setPieceTaker = pid;
    m.setPiecePartner = Team_nearestPlayer_4522B0(w, m.setPieceAim, p.team, p.curSlot);
    Player_assignControlToReceiver(w, pid, -1);
    bool partner = false;
    if (p.ctrl != 0 && m.phase == 1) {
        m.setPieceMode = 2;
        const int q = Team_nearestPlayer_4522B0(w, m.setPieceAim, p.team, p.curSlot);
        if (q >= 0 && P(w, q).ctrl == 0 && P(w, q).slot != 0) {
            m.setPiecePartner = q;
            Player_transferControl(w, pid, q);
            m.setPieceAimMode = 2;
            m.f294 = m.setPiecePartner;
            partner = true; // (sp.arg = +-0x100 is overwritten below)
        }
    }
    if (!partner) {
        m.setPieceMode = 1;
        m.setPieceAimMode = 0;
        m.setPieceAimCtrl = 0;
    }
    SetPieceAim& sp = w.pl.sp;
    sp.arg = angle;
    sp.f20 = 0;
    sp.spin = 0;
    sp.loft = m.phase == 3 ? 0x2b : 0x1c;
    sp.power = 0x4ccc;
    sp.powerRef = -1;
    // (analog controllers also run 0x44D37C; analogDevice is never set)
}

// 0x44C560: the taker takes the aim marker back from the partner (training restarts).
void SetPiece_takeAim_44C560(World& w, int pid) {
    MatchState& m = w.match;
    if (P(w, pid).ctrl == 0) return;
    m.setPieceMode = 3;
    Player_transferControl(w, m.setPiecePartner, pid);
    m.setPiecePartner = -1;
    m.setPieceAimCtrl = P(w, pid).ctrl;
    m.setPieceAimMode = 1;
}

void SetPiece_reset(World& w) { // 0x44C5B4
    MatchState& m = w.match;
    m.setPieceMode = 0;
    m.setPieceAimMode = 0;
    m.setPieceAimCtrl = 0;
    w.pl.sp.f20 = 0;
}

void SetPiece_cycleMode(World& w, int pid) { // 0x44C5D4 (EAX = taker)
    MatchState& m = w.match;
    if (++m.setPieceMode == 4) m.setPieceMode = 1;
    if (m.setPieceMode < 2) return;
    if (m.setPieceMode == 2) {
        const int q = Team_nearestPlayer_4522B0(w, m.setPieceAim, P(w, pid).team, P(w, pid).curSlot);
        if (q >= 0 && P(w, q).ctrl == 0 && P(w, q).slot != 0) {
            m.setPiecePartner = q;
            Player_transferControl(w, pid, q);
            m.f294 = m.setPiecePartner;
            m.setPieceAimMode = 2;
            w.pl.sp.f20 = 0;
            return;
        }
        m.setPieceMode = 3;
    }
    if (m.setPieceMode != 3) return;
    Player_transferControl(w, m.setPiecePartner, pid);
    m.setPiecePartner = -1;
    m.setPieceAimCtrl = P(w, pid).ctrl;
    m.setPieceAimMode = 1;
    w.pl.sp.f20 = 0;
}

// 0x44C6D0 (EAX rel out, EDX lift out, EBX spin out): kick vector from the aim block.
void SetPiece_computeKick(World& w, KickVec* rel, std::int32_t* lift, Fixed* spin) {
    const SetPieceAim& sp = w.pl.sp;
    *spin = (sp.spin << 13) / 0xab;
    std::int32_t v = (0x32 - (sp.loft * 5) / 0x55) * sp.power;
    if (w.match.phase == 1) {
        v >>= 1;
        if (v < 0xf0000) v = 0xf0000;
    }
    const SinCos sc = sinCos(sp.loft);
    const Fixed vy = fx(v, sc.s), vh = fx(v, sc.c);
    std::int32_t out[4];
    Ball_solveKick(w, sp.arg, vh, vy > 0x1999 ? vy : 0x1999, *spin, out, lift);
    *rel = {out[0], out[1], out[2], out[3]};
}

void SetPiece_updateHuman(World& w, int pid) { // 0x44C7F4: curl buttons + run-up position
    MatchState& m = w.match;
    SetPieceAim& sp = w.pl.sp;
    const int q = m.setPiecePartner;
    auto rawOf = [&](int who) -> std::uint32_t {
        return (who >= 0 && P(w, who).ctrl != 0) ? Cof(w, who).raw : 0;
    };
    const std::int32_t step = w.time.simFrames * 2;
    if ((rawOf(q) & 0x400) || (rawOf(pid) & 0x400)) {
        sp.spin -= step;
        if (sp.spin < -0xab) sp.spin = -0xab;
    } else if ((rawOf(q) & 0x800) || (rawOf(pid) & 0x800)) {
        sp.spin += step;
        if (sp.spin > 0xab) sp.spin = 0xab;
    }
    const std::int32_t dist = P(w, pid).state == 0x183 ? 0x50 : 0xa0;
    int mover;
    OctDist od;
    if (m.setPieceMode == 3) {
        mover = pid;
        od = octDistance(w.ball.posI, m.setPieceAim);
    } else if (w.ball.possessor >= 0) {
        mover = w.ball.possessor;
        od = octDistance(w.ball.posI, P(w, q).posI);
    } else {
        return;
    }
    const std::int32_t e = wrap(atan2i(od.dx, od.dz) - sp.spin);
    const Vec3 spot = offsetInDirection(w.ball.posI, dist, e + 0x200);
    Player_moveToward_4BE804(w, mover, spot);
    P(w, mover).headAim = e;
}

void SetPiece_updatePower(World& w, int pid) { // 0x44C9D8: power gauge, loft, direction
    MatchState& m = w.match;
    SetPieceAim& sp = w.pl.sp;
    PlayerState& p = P(w, pid);
    if (sp.powerRef >= 0) {
        const std::int32_t hold = Input_snap(w, sp.powerRef)->lastFrames;
        const Fixed f = fdiv(static_cast<std::int32_t>(static_cast<std::uint32_t>(hold) << 16), 0x640000);
        if (m.phase == 3) {
            sp.power = f + 0x8000;
            if (sp.power < 0x8000) sp.power = 0x8000;
            else if (sp.power > 0x10000) sp.power = 0x10000;
        } else {
            sp.power = f + 0x3333;
            if (sp.power < 0x4ccc) sp.power = 0x4ccc;
            else if (sp.power > 0x10000) sp.power = 0x10000;
        }
    }
    if (p.ctrl == 0) return;
    const ControllerState& c = Cof(w, pid);
    Angle head = p.facing;
    const int st = p.state;
    if (st == 0x201 && w.pl.keeper[p.team].k134 != 0) return;
    auto finish = [&] {
        sp.f20 = pid + 1;
        p.headAim = head;
    };
    if (sp.f20 == 0) return finish();
    const std::int32_t f = w.time.simFrames;
    const std::uint32_t d = c.raw; // (analog path omitted)
    if (!(d & 0xc)) {
        if (d & 2) {
            sp.loft += f;
            if (sp.loft > 0x39) sp.loft = 0x39;
        } else if (d & 1) {
            sp.loft -= f;
            const std::int32_t lo = m.phase == 3 ? 0x1a : 9;
            if (sp.loft < lo) sp.loft = lo;
        }
    }
    const bool xPos = p.posI.x > 0;
    if (st == 0x182 || st == 0x201) { // 0x44CB04: throw-in / keeper distribution direction
        if (d & 8) sp.arg += xPos ? -f : f;
        if (d & 4) sp.arg += xPos ? f : -f;
        if (st == 0x182) {
            if (xPos) sp.arg = sp.arg < -0x1d5 ? -0x1d5 : sp.arg > -0x2b ? -0x2b : sp.arg;
            else sp.arg = sp.arg < 0x2b ? 0x2b : sp.arg > 0x1d5 ? 0x1d5 : sp.arg;
        }
        sp.arg = wrap(sp.arg);
        head = sp.arg;
        return finish();
    }
    if (!(st == 0x183 || st == 0x184 || st == 0x188)) return finish();
    // 0x44CCD2: corner (0x183), 0x184, free kick (0x188)
    bool moved = false;
    if ((d & 0xf) && (d & 0xc)) {
        moved = true;
        if (st == 0x183) {
            if (d & 8) sp.arg += xPos ? -f : f;
            if (d & 4) sp.arg += xPos ? f : -f;
        } else if (d & 8) {
            sp.arg -= 2 * f;
        } else if (d & 4) {
            sp.arg += 2 * f;
        }
    }
    const std::int32_t* row = kCornerAim[xPos ? 1 : 0][p.posI.z > 0 ? 1 : 0];
    if (moved) {
        if (st == 0x183) {
            if (sp.arg < row[0]) sp.arg = row[0];
            else if (sp.arg > row[1]) sp.arg = row[1];
            const std::int32_t e = sp.arg - (sp.spin >> 1);
            if (e < row[0]) sp.spin = wrap(wrap(sp.arg - row[0]) * 2);
            if (e > row[1]) sp.spin = wrap(wrap(sp.arg - row[1]) * 2);
        } else if (st == 0x188) {
            sp.arg = wrap(sp.arg);
            if (p.posI.z >= 0) {
                if (sp.arg < 0x100 && sp.arg > 0) sp.arg = 0x100;
                else if (sp.arg > -0x100 && sp.arg < 0) sp.arg = -0x100;
            } else {
                sp.arg = sp.arg < -0x100 ? -0x100 : sp.arg > 0x100 ? 0x100 : sp.arg;
            }
        }
    }
    if (d & 0x400) {
        sp.spin -= 2 * f;
        if (sp.spin < -0xab) sp.spin = -0xab;
    } else if (d & 0x800) {
        sp.spin += 2 * f;
        if (sp.spin > 0xab) sp.spin = 0xab;
    }
    const std::int32_t a = wrap(sp.arg - (sp.spin >> 1));
    if (st == 0x183) {
        if (a < row[0]) sp.arg = wrap(row[0] + (sp.spin >> 1));
        if (a > row[1]) sp.arg = wrap(row[1] + (sp.spin >> 1));
    }
    std::int32_t dir;
    if (st == 0x188) dir = sp.arg + 0x200 + (sp.spin > 0 ? -0x72 : 0x72);
    else dir = wrap(sp.arg - (sp.spin >> 1)) + 0x200;
    const Vec3 spot = offsetInDirection(w.ball.restartPos, st == 0x183 ? 0x50 : 0xa0, dir);
    head = p.angleBall;
    Player_moveToward_4BE804(w, pid, spot);
    // (octDistance(target, ball) at 0x44CFDB: result unused)
    finish();
}

} // namespace wc98::sim
