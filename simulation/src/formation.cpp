// Formation module: GEN4TBLS.T formation tables, ball zones, slot targets, positioning callbacks and
// the per-team slot result caches (FIFA99.EXE v1.1, 0x4CE0D0..0x4CFEEA). Ported from disassembly.
#include <cstring>

#include "wc98/sim/data.hpp"
#include "wc98/sim/world.hpp"

namespace wc98::sim {

namespace {

// 0x58AD8C / 0x58ADA0: lateral spread threshold base / slope per role class (0x4CE758).
constexpr std::int32_t kSpreadBase[5] = {0, 0, 16, 16, 11};  // exe 0x58ad8c
constexpr std::int32_t kSpreadSlope[5] = {0, 0, -8, -7, -3}; // exe 0x58ada0
// 0x58ADB4..0x58AE33: wide-formation slot offsets (x, z) applied by 0x4CEE60.
constexpr std::int32_t kWide[32] = { // exe 0x58adb4
    0, -10, 0, 10, 0, -10, 10, 10, -10, -10, 0, -10, 10, 10, -10, -10,
    0, 10, 0, -10, 0, 10, 10, 10, -10, -10, 0, 10, 10, 10, -10, -10,
};
// 0x58AE70: 15 formation descriptors {gk, sweeper, def, mid, fwd, wideA, wideB} (ptrs 0x58AFCC).
constexpr std::uint8_t kStyleDesc[15][7] = { // exe 0x58ae70
    {1, 1, 4, 4, 1, 0, 0}, {1, 1, 4, 3, 2, 0, 0}, {1, 1, 3, 5, 1, 0, 0}, {1, 1, 3, 4, 2, 0, 0},
    {1, 1, 3, 3, 3, 0, 0}, {1, 0, 4, 4, 2, 0, 0}, {1, 0, 4, 3, 3, 0, 0}, {1, 0, 3, 5, 2, 0, 0},
    {1, 0, 3, 4, 3, 0, 0}, {1, 0, 4, 5, 1, 0, 0}, {1, 1, 2, 5, 2, 0, 0}, {1, 0, 2, 3, 5, 0, 0},
    {1, 0, 3, 2, 5, 0, 0}, {1, 0, 4, 2, 4, 0, 0}, {1, 0, 1, 1, 2, 0, 0},
};
// 0x58AED9: line-removal lists (3 bytes each) used when 8..10 players remain (0x4CECC4).
constexpr std::uint8_t kRemoval[48] = { // exe 0x58aed9
    3, 4, 2, 4, 2, 4, 3, 3, 4, 4, 3, 4, 4, 4, 4, 4, 4, 3, 4, 3, 3, 3, 3, 2,
    2, 1, 3, 1, 3, 3, 3, 1, 3, 1, 3, 4, 3, 2, 3, 2, 3, 4, 3, 3, 3, 2, 4, 2,
};
// 0x58AF54 (not behind) / 0x58AF90 (behind): offsets of each style's list in kRemoval.
constexpr std::uint8_t kRemovalA[15] = {0, 3, 6, 9, 12, 15, 12, 9, 9, 21, 18, 12, 12, 3, 15};   // exe 0x58af54
constexpr std::uint8_t kRemovalB[15] = {24, 24, 27, 30, 33, 36, 39, 42, 21, 21, 42, 9, 3, 45, 0}; // exe 0x58af90
// 0x58AF09: (line, minimum) pairs for 0x4CEB74; table A at +0 (behind), B at +8. The loop reads up to
// pair 4 so table B runs into the first two bytes of 0x58AF19.
constexpr std::uint8_t kAdjust[18] = {2, 3, 3, 3, 4, 1, 1, 0, 3, 2, 4, 2, 2, 3, 1, 0, 1, 0}; // exe 0x58af09
// 0x58AF19: descriptors for fewer than 8 players (ptrs 0x58B008[n] = 0x58AF4A - 7n).
constexpr std::uint8_t kFewDesc[8][7] = { // exe 0x58af19.. reversed by n
    {0, 0, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 0, 0}, {1, 0, 1, 0, 0, 0, 0}, {1, 0, 2, 0, 0, 0, 0},
    {1, 0, 2, 1, 0, 0, 0}, {1, 0, 1, 2, 1, 0, 0}, {1, 0, 2, 2, 1, 0, 0}, {1, 0, 2, 2, 2, 0, 0},
};

// FUN_004CE5EC divide: (a / b) << 16 + ((a % b) << 16) / b.
Fixed divFix(std::int32_t a, std::int32_t b) {
    return static_cast<Fixed>(static_cast<std::uint32_t>(a / b) << 16) + fdiv(a % b, b);
}

std::int32_t shl16(std::int32_t v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << 16); }
// `shl 16; sar 7`: a difference as a 16.16 fraction of 128.
std::int32_t frac128(std::int32_t v) { return shl16(v) >> 7; }

const formats::Gen4Tables& tables(const World& w) { return w.data->formations; }
const std::int8_t* plRow(const World& w, int slot) { return tables(w).pl.data() + slot * 0x4ac; }

// Descriptor / slot records of the record bound by Formation_selectTeamFormation (0x6765FC / 0x6765F4).
const std::uint8_t* selDesc(const World& w, int t) { return w.teams[t].rec(w.formation.selRec[t]) + 1; }
const std::uint8_t* selSlot(const World& w, int t, int s) { return w.teams[t].rec(w.formation.selRec[t]) + 8 + s * 6; }

void clampPitch(const World& w, Vec3& o) {
    const std::int32_t hw = w.pitch.halfWidth, hl = w.pitch.halfLength;
    if (-hw > o.x) o.x = -hw;
    else if (o.x > hw) o.x = hw;
    if (-hl > o.z) o.z = -hl;
    else if (o.z > hl) o.z = hl;
    o.y = 0;
}

// FUN_004CE758 (EAX = a, EDX = b, EBX = factor, ECX = role class, stack = z): lateral spread toward
// the mirror slot.
std::int32_t spread(std::int32_t a, std::int32_t b, Fixed factor, int role, std::int32_t z) {
    z = iabs(z);
    const std::int32_t thr = kSpreadBase[role] + (z * kSpreadSlope[role]) / 0x48;
    const std::int32_t d = b - a, ad = iabs(d);
    if (ad <= thr) return 0;
    std::int32_t r = (ad * factor) >> 17;
    const std::int32_t h = (ad - thr) >> 1;
    if (h < r) r = h;
    return d < 0 ? -r : r;
}

// 0x4CFA18 (callbacks 0, 12, 13): kick-off line-up from G99KO.
void koPosition(World& w, int pid, Vec3& out) {
    const PlayerState& p = w.players[pid];
    const int t = p.team, side = w.teams[t].side, slot = p.curSlot;
    const int v = t == w.match.phaseTeam ? 0 : 2;
    const std::int8_t* ko = tables(w).ko.data();
    const std::int32_t x = w.formation.slotOff[t][slot][0] + ko[slot * 4 + v];
    const std::int32_t z = w.formation.slotOff[t][slot][1] + ko[slot * 4 + v + 1];
    std::int32_t spr = 0;
    const int ms = kSlotMirror[slot];
    const Fixed adj = Formation_lineAdjust(w, pid);
    if (adj) spr = spread(x, w.formation.slotOff[t][ms][0] + ko[ms * 4 + v], adj, p.role, z);
    const std::uint8_t* rec = selSlot(w, t, slot);
    out.x = fx((static_cast<std::int8_t>(rec[1]) >> 1) + x + spr, w.formation.scaleX);
    out.z = fx((static_cast<std::int8_t>(rec[2]) >> 1) + z, w.formation.scaleZ);
    if (side == 1) {
        out.x = -out.x;
        out.z = -out.z;
        if (out.z < 0x10) out.z = 0x10;
        else if (out.z > w.pitch.halfLength) out.z = w.pitch.halfLength;
    } else {
        if (-w.pitch.halfLength > out.z) out.z = -w.pitch.halfLength;
        else if (out.z > -0x10) out.z = -0x10;
    }
    if (-w.pitch.halfWidth > out.x) out.x = -w.pitch.halfWidth;
    else if (out.x > w.pitch.halfWidth) out.x = w.pitch.halfWidth;
    out.y = 0;
}

// 0x4CFBE0 (callbacks 3, 20): slot position with the zone forced to 0xA2 (team of the phase) or 0x89.
void setPiecePosition(World& w, int pid, Vec3& out, int val) {
    const int zone = w.players[pid].team == w.match.phaseTeam ? 0xa2 : 0x89;
    Formation_slotPosition(w, pid, out, zone, val);
}

// 0x4CFC04 (callback 4): penalty line-up from G99PK (absolute units, 0x58B110/0x58B114).
void pkPosition(World& w, int pid, Vec3& out) {
    const PlayerState& p = w.players[pid];
    const int side = w.teams[p.team].side;
    const std::int32_t rz = w.ball.restartPos.z;
    const int v = side == 0 ? (rz > 0 ? 1 : 0) : (rz < 0 ? 1 : 0);
    const std::int16_t* pk = tables(w).pk.data() + p.curSlot * 4 + v * 2;
    out.x = fx(pk[0], w.formation.pkScaleX);
    out.z = fx(pk[1], w.formation.pkScaleZ);
    if (side == 1) {
        out.x = -out.x;
        out.z = -out.z;
    }
    if (rz > 0) out.z -= 0x60;
    else out.z += 0x60;
    clampPitch(w, out);
}

// 0x4CFD00 (callbacks 1, 2, 5, 7): free-kick line-up from G99FK, zone = fkZone[restartSide]. The exe
// adds the slot offset twice (`add eax,[ecx]` after ebx = offset + value); kept.
void fkPosition(World& w, int pid, Vec3& out) {
    const PlayerState& p = w.players[pid];
    if (p.state == 0x18c) return;
    const int t = p.team, side = w.teams[t].side, slot = p.curSlot;
    const std::int32_t* off = w.formation.slotOff[t][slot];
    int v = w.ball.fkZone[w.match.restartSide] * 2;
    if (t != w.match.phaseTeam) ++v;
    const std::int8_t* e = tables(w).fk.data() + slot * 0x4ac + v * 2;
    const std::int32_t x = off[0] + e[0], z = off[1] + e[1];
    out.x = fx(off[0] + x, w.formation.scaleX);
    out.z = fx(off[1] + z, w.formation.scaleZ);
    if (side == 1) {
        out.x = -out.x;
        out.z = -out.z;
    }
    clampPitch(w, out);
}

// 0x4CFE3C (callbacks 6, 19): goalkeeper beside the goal, others on an arc (random +0xF0..0x12F).
void arcPosition(World& w, int pid, Vec3& out) {
    const PlayerState& p = w.players[pid];
    if (p.slot == 0) {
        out.x = w.pitch.halfWidth + 0x30;
        out.z = w.pitch.halfLength - 0x4b0;
        if (w.ball.restartPos.z < 0) out.z = -out.z;
        out.y = 0;
        return;
    }
    const Vec3 r = offsetInDirection(Vec3{}, -0x1e0, p.index << 5);
    out.x = r.x;
    out.z = r.z;
    out.z += static_cast<std::int32_t>(w.rng.next() & 0x3f) + 0xf0;
    out.x -= static_cast<std::int32_t>(w.rng.next() & 0x3f) + 0xf0;
    if (p.team == 1) out.x = -out.x;
    out.y = 0;
}

} // namespace

// 0x4CE5EC (from Pitch_setDimensions 0x455930).
void Formation_initZoneScales(World& w) {
    FormationState& f = w.formation;
    const std::int32_t W = w.pitch.width, L = w.pitch.length;
    f.colPerUnit = divFix(11, W);
    f.rowPerUnit = divFix(21, L);
    f.cellW = W / 11;
    f.cellL = L / 21;
    std::int32_t wr = W;
    if (W > 0xb2c) {
        wr = W - (W - 0xb2c) / 4;
        if (wr < 0xb2c) wr = 0xb2c;
    }
    f.scaleX = divFix(wr << 5, 0xb2c);
    f.pkScaleX = divFix(wr, 0xb2c);
    std::int32_t lr = L;
    if (L > 0x1504) {
        lr = L - (L - 0x1504) / 4;
        if (lr < 0x1504) lr = 0x1504;
    }
    f.scaleZ = divFix(lr << 5, 0x1504);
    f.pkScaleZ = divFix(lr, 0x1504);
}

// 0x4CE510 (Match_startPeriod 0x454B1D).
void Formation_resetCaches(World& w) {
    for (SlotCache* arr : {w.formation.cur, w.formation.base}) {
        for (int t = 0; t < 2; ++t) {
            SlotCache& c = arr[t];
            c = SlotCache{};
            for (int s = 0; s < kSlots; ++s) {
                c.e[s] = SlotCacheEntry{};
                c.e[s].state = 0;
                c.e[s].player = -1;
                c.e[s].fn = 0;
                c.e[s].zone = -1;
                c.queue[s] = 0x11;
            }
        }
    }
}

// 0x4CE2E0 (EAX = team, EDX = record index).
void Formation_selectTeamFormation(World& w, int team, int rec) {
    FormationState& f = w.formation;
    f.selRec[team] = rec;
    f.cur[team].dirty |= 0x1fffe;
    f.base[team].dirty |= 0x1fffe;
    const std::uint8_t* d = w.teams[team].rec(rec) + 1;
    f.base[team].line358 = d[1] + d[2];
    f.base[team].line35c = d[3];
}

// Tail of Formation_loadTables 0x4CE374 (tables come from MatchData). Both calls read team 0's
// record index byte 0x65A2E0 (exe quirk, kept).
void Formation_init(World& w) {
    const int r = w.teams[0].formIdx();
    Formation_selectTeamFormation(w, 0, r);
    Formation_selectTeamFormation(w, 1, r);
    w.formation.col = 0;
    w.formation.row = 0;
}

// 0x4CF230.
void Formation_ballZone(World& w, const Vec3& p, std::int32_t* out, int hysteresis) {
    FormationState& f = w.formation;
    const std::int32_t x0 = (f.col - 1) * f.cellW - w.pitch.halfWidth;
    const std::int32_t z0 = (f.row - 1) * f.cellL - w.pitch.halfLength;
    const std::int32_t loX = shl16(x0 - 0x50), hiX = shl16(f.cellW + x0 + 0x50);
    const std::int32_t loZ = shl16(z0 - 0x50), hiZ = shl16(f.cellL + z0 + 0x50);
    std::int32_t col = f.col, row = f.row;
    if (!(hysteresis && loX < p.x && hiX > p.x && loZ < p.z && hiZ > p.z)) {
        col = ((fx(p.x, f.colPerUnit) + 0x58000) >> 16) + 1;
        row = ((fx(p.z, f.rowPerUnit) + 0xa8000) >> 16) + 1;
    }
    if (col < 0) col = 0;
    else if (col > 12) col = 12;
    if (row < 0) row = 0;
    else if (row > 22) row = 22;
    f.row = row;
    f.col = col;
    const std::int32_t zone = col + row * 13;
    if (out) {
        out[0] = zone;
        out[1] = 0x12a - zone;
    }
}

// 0x4CE7C8 (EAX = player): lateral factor from the line size and the slot's place in its line.
Fixed Formation_lineAdjust(const World& w, int pid) {
    const PlayerState& p = w.players[pid];
    const TeamState& t = w.teams[p.team];
    const int role = p.role;
    const bool noPoss = !(t.flags & 2);
    if (role < 2) return 0;
    std::int32_t cnt = 0, i = 0;
    if (role == 2) {
        cnt = t.line358 - selDesc(w, p.team)[1];
        i = p.curSlot - 2;
    } else if (role == 3) {
        i = p.curSlot - 7;
        cnt = t.line35c;
    } else if (role == 4) {
        cnt = selDesc(w, p.team)[4];
        i = p.curSlot - 12;
    }
    Fixed r = 0;
    const bool inner = i == 1 || i == 3, outer = i == 0 || i == 4;
    if (role == 2) {
        if (cnt == 2 && inner) r = 0xa666;
        else if (cnt == 3 && inner) r = noPoss ? 0 : -0x4ccc;
        else if (cnt == 4 && outer) r = noPoss ? 0x8000 : 0x3333;
        else if (cnt == 4 && inner) r = 0xc000;
    } else if (role == 3) {
        if (cnt == 2 && inner) r = 0xccc;
        else if (cnt == 3 && inner) r = -0x1999;
        else if (cnt == 4 && outer) r = noPoss ? 0x2e14 : 0;
        else if (cnt == 4 && inner) r = 0x4ccc;
    } else {
        if (cnt == 3 && inner) r = -0x8000;
    }
    if (cnt == 3 && w.match.phase == 0) r -= 0xb333;
    return r;
}

// 0x4CF678 (EAX = player, EDX = out, EBX = zone (<0: current), ECX = attack lerp 0..0x80).
void Formation_slotPosition(World& w, int pid, Vec3& out, int zone, int val) {
    const PlayerState& p = w.players[pid];
    const int t = p.team, side = w.teams[t].side, slot = p.curSlot, role = p.role;
    const FormationState& f = w.formation;
    const std::int32_t* off = f.slotOff[t][slot];
    if (zone < 0) zone = w.ball.zone[side];
    const Fixed adj = Formation_lineAdjust(w, pid);
    const int ms = kSlotMirror[slot];
    const std::int8_t* tab = plRow(w, slot) + zone * 4;
    const std::int8_t* mtab = plRow(w, ms) + zone * 4;
    const std::int32_t mox = f.slotOff[t][ms][0];
    if (!(w.teams[t].flags & 2)) {
        std::int32_t x = tab[2] + off[0];
        const std::int32_t z = tab[3] + off[1];
        if (adj) x += spread(x, mtab[2] + mox, adj, role, z);
        out.x = fx(x, f.scaleX);
        out.z = fx(z, f.scaleZ);
    } else {
        std::int32_t ax = tab[0] + off[0];
        const std::int32_t az = tab[1] + off[1];
        if (adj) ax += spread(ax, mtab[0] + mox, adj, role, az);
        out.x = fx(ax, f.scaleX);
        out.z = fx(az, f.scaleZ);
        std::int32_t dx = off[0] + tab[2];
        const std::int32_t dz = off[1] + tab[3];
        if (adj) dx += spread(dx, mtab[2] + mox, adj, role, dz);
        const std::int32_t bx = frac128(fx(dx, f.scaleX) - out.x);
        const std::int32_t bz = frac128(fx(dz, f.scaleZ) - out.z);
        out.x += fx(val, bx);
        out.z += fx(val, bz);
    }
    const std::uint8_t* rec = selSlot(w, t, slot);
    out.x += static_cast<std::int8_t>(rec[1]) >> 1;
    out.z += static_cast<std::int8_t>(rec[2]) >> 1;
    if (side == 1) {
        out.x = -out.x;
        out.z = -out.z;
    }
    clampPitch(w, out);
}

// Indirect call of a cached positioning callback (EAX = player, EDX = out, EBX = zone, ECX = val).
void Formation_callPos(World& w, std::int32_t fn, int pid, Vec3& out, int zone, int val) {
    switch (fn) {
    case 0x4cfa18: koPosition(w, pid, out); break;
    case 0x4cfd00: fkPosition(w, pid, out); break;
    case 0x4cfbe0: setPiecePosition(w, pid, out, val); break;
    case 0x4cfc04: pkPosition(w, pid, out); break;
    case 0x4cfe3c: arcPosition(w, pid, out); break;
    case 0x4cfe0c: out = w.players[pid].posI; break; // own position
    case 0x4cf678: Formation_slotPosition(w, pid, out, zone, val); break;
    case 0x4cfe1c: { // 0x65F3C8 + t*0xC
        const std::int32_t* e = w.pitch.entryPos[2 + w.players[pid].team];
        out = {e[0], e[1], e[2]};
        break;
    }
    default: break;
    }
}

// Positioning callback table 0x58B0B0 (index = player positioning mode).
constexpr std::int32_t kPosTable[22] = { // exe 0x58b0b0
    0x4cfa18, 0x4cfd00, 0x4cfd00, 0x4cfbe0, 0x4cfc04, 0x4cfd00, 0x4cfe3c, 0x4cfd00, 0x4cfe0c, 0x4cf678, 0x4cfe0c,
    0x4cfe0c, 0x4cfa18, 0x4cfa18, 0x4cfe1c, 0x4cfe1c, 0x4cfe1c, 0x4cfe1c, 0x4cfe1c, 0x4cfe3c, 0x4cfbe0, 0x4cfe1c,
};

// 0x4CF358 (EAX = cb, EDX = player, EBX = out, ECX = zone, stack val, store).
std::int32_t Formation_evalSlotCallback(World& w, int cb, int pid, Vec3& out, int zone, int val, int store) {
    const std::int32_t fn = kPosTable[cb];
    const PlayerState& p = w.players[pid];
    const int t = p.team;
    if (zone < 0) zone = w.ball.zone[w.teams[t].side];
    Formation_callPos(w, fn, pid, out, zone, val);
    SlotCache& c = w.formation.cur[t];
    const int slot = p.curSlot;
    SlotCacheEntry& e = c.e[slot];
    const std::uint32_t bit = 1u << slot;
    if (fn != e.fn) {
        c.dirty |= bit;
        w.formation.base[t].dirty |= bit;
    }
    if (store) {
        e.player = pid;
        e.zone = static_cast<std::int16_t>(zone);
        e.fn = fn;
        e.val = static_cast<std::uint16_t>(val);
        e.pos = out;
        c.dirty &= bit ^ 0x1fffe;
        if (e.state == 2) e.state = 1;
    }
    return fn;
}

// 0x4CF450 (EAX = fn, EDX = player, EBX = out, ECX = zone, stack = base cache flag).
void Formation_attackParticipation(World& w, std::int32_t fn, int pid, Vec3& out, int zoneArg, int base) {
    const PlayerState& p = w.players[pid];
    const int t = p.team;
    const TeamState& tm = w.teams[t];
    std::int32_t ment = tm.mentality;
    SlotCache& c = base ? w.formation.base[t] : w.formation.cur[t];
    const int key = base ? p.slot : p.curSlot;
    const int role = base ? p.baseRole : p.role;
    SlotCacheEntry& e = c.e[key];
    // The exe only loads the zone when ECX < 0 (otherwise [esp+0xc] is uninitialised); every traced
    // caller passes -1. Here a non-negative ECX is used as the zone.
    std::int32_t zone = zoneArg;
    if (zoneArg < 0) zone = w.ball.zone[tm.side];
    if ((tm.flags & 0x200) || tm.f1b4 != 0) ment = 0;
    std::int32_t v = tables(w).at[ment * 5 + role] + 0x10 - 2 * Player_attr(w, pid, 0x2e);
    if (v < 0) v = 0;
    else if (v > 0x80) v = 0x80;
    const std::uint32_t bit = 1u << key;
    if (!(c.dirty & bit) && e.zone == zone && e.val == v) {
        out = e.pos;
        return;
    }
    e.player = pid;
    e.val = static_cast<std::uint16_t>(v);
    e.fn = fn;
    e.zone = static_cast<std::int16_t>(zone);
    if (zoneArg == -1) e.flag = 1;
    if (e.state == 0) {
        e.state = 2;
        c.queue[c.writeIdx] = static_cast<std::uint8_t>(key);
        c.writeIdx = c.writeIdx < 0x10 ? c.writeIdx + 1 : 0;
    }
    e.state = 2;
    out = e.pos;
}

// 0x4CF5F8 (EAX = out, EDX = team, ECX = a, EBX = b).
void Formation_lerpByMentality(World& w, Vec3& out, int team, const Vec3& a, const Vec3& b) {
    const TeamState& t = w.teams[team];
    if (t.flags & 2) {
        out = b;
        return;
    }
    const std::int32_t df = tables(w).df[t.mentality];
    out.x = b.x + fx(frac128(a.x - b.x), df);
    out.z = b.z + fx(frac128(a.z - b.z), df);
}

// 0x4CE0D0 (EAX = 0x675BA4 current-slot caches or 0x676364 base-slot caches; both teams).
void Formation_flushSlotCache(World& w, int base) {
    for (int t = 0; t < 2; ++t) {
        SlotCache& c = base ? w.formation.base[t] : w.formation.cur[t];
        TeamState& tm = w.teams[t];
        const int r = c.readIdx;
        const int q = c.queue[r];
        if (q == 0x11) continue;
        SlotCacheEntry& e = c.e[q];
        if (e.state == 2) {
            if (e.flag) {
                e.flag = 0;
                e.zone = static_cast<std::int16_t>(w.ball.zone[tm.side]);
            }
            PlayerState& p = w.players[e.player];
            const std::int32_t sSlot = p.curSlot, sRole = p.role, s358 = tm.line358, s35c = tm.line35c;
            p.curSlot = q;
            p.role = kSlotRole[q];
            tm.line358 = c.line358;
            tm.line35c = c.line35c;
            Formation_callPos(w, e.fn, e.player, e.pos, e.zone, e.val);
            p.curSlot = sSlot;
            p.role = sRole;
            tm.line358 = s358;
            tm.line35c = s35c;
            c.dirty &= (1u << q) ^ 0x1fffe;
        }
        e.state = 0;
        c.queue[r] = 0x11;
        c.readIdx = static_cast<std::uint8_t>(r < 0x10 ? r + 1 : 0);
    }
}

// FUN_004CE254 (Sim_advance, before each time slice).
void Formation_drainQueue(World& w) {
    for (int t = 0; t < 2; ++t) {
        SlotCache& c = w.formation.cur[t];
        const TeamState& tm = w.teams[t];
        const std::int32_t poss = (tm.flags >> 1) & 1;
        if (c.line358 != tm.line358 || c.line35c != tm.line35c || c.poss != poss) {
            c.line358 = tm.line358;
            c.line35c = tm.line35c;
            c.poss = poss;
            c.dirty |= 0x1fffe;
        }
    }
    Formation_flushSlotCache(w, 0);
    Formation_flushSlotCache(w, 1);
}

// 0x4CEE60 (EAX = team, EDX = descriptor): installs a descriptor into the bound record, derives the
// used flags of the 17 slot records, the player total (team+0x0F) and the wide-formation offsets.
void Formation_applyDescriptor(World& w, int team, const std::uint8_t src[7]) {
    TeamState& t = w.teams[team];
    std::uint8_t* d = t.rec(w.formation.selRec[team]) + 1;
    std::uint8_t tmp[7];
    std::memcpy(tmp, src, 7); // src may alias d
    std::memcpy(d, tmp, 7);
    std::uint8_t* s = d + 7; // slot records (rec + 8)
    s[0] = d[0] > 0;
    s[6] = d[1] > 0;
    for (int i = 0; i < 3; ++i) {
        const std::uint8_t n = d[2 + i];
        std::uint8_t* l = s + (2 + 5 * i) * 6;
        l[0] = n >= 4;
        l[6] = n >= 2;
        l[12] = n & 1;
        l[18] = n >= 2;
        l[24] = n >= 4;
    }
    t.blk[0x0f - 0x0c] = static_cast<std::uint8_t>(d[0] + d[1] + d[2] + d[3] + d[4]);
    std::int32_t(*off)[2] = w.formation.slotOff[team];
    std::memset(off, 0, sizeof(w.formation.slotOff[team]));
    auto put = [&](int slot, int tblOff) { // movs 8 bytes from 0x58ADB4 + tblOff
        off[slot][0] = kWide[tblOff / 4];
        off[slot][1] = kWide[tblOff / 4 + 1];
    };
    if (d[5]) {
        switch (d[3]) { // jump table 0x4CEE40
        case 2: put(10, 0x30); put(8, 0x38); break;
        case 3: put(9, 0x28); break;
        case 4: put(10, 0x18); put(8, 0x20); break;
        case 5: put(10, 0x00); put(9, 0x08); put(8, 0x10); break;
        default: break;
        }
    }
    if (d[6]) {
        switch (d[4]) { // jump table 0x4CEE50
        case 2: put(15, 0x70); put(13, 0x78); break;
        case 3: put(14, 0x68); break;
        case 4: put(15, 0x58); put(13, 0x60); break;
        case 5: put(15, 0x40); put(14, 0x48); put(13, 0x50); break;
        default: break;
        }
    }
}

namespace {

// FUN_004CEA24 (EAX = desc, EDX = from, EBX = from2, ECX = to, stack = to2): moves one player
// between lines.
void moveLine(std::uint8_t* d, int from, int from2, int to, int to2) {
    int src = from;
    if (d[from] <= 2) {
        if (d[from2] <= 2) return;
        src = from2;
    }
    std::uint32_t a = d[to], b = d[to2];
    if (to == 2) a += d[1];
    if (to2 == 2) b += d[1];
    int dst = to;
    if (static_cast<std::int32_t>(a) >= 5) {
        if (static_cast<std::int32_t>(b) >= 5) return;
        dst = to2;
    }
    if (dst == 1 && d[1] != 0) return;
    --d[src];
    ++d[dst];
}

// FUN_004CEA9C (EAX = team, EDX = desc): late in the first... in a period of type 1 with < 300 s
// left push forward when leading / (< 60 s) when behind.
void scoreAdapt(World& w, int team, std::uint8_t* d) {
    if (w.opt.gameMode == 6) return;
    const std::int32_t rem = w.match.durationSec - w.match.elapsedSec;
    if (w.match.periodType != 1) return;
    if (rem >= 0x4650) return;
    const int opp = team == 0 ? 1 : 0;
    const std::int32_t diff = w.match.score[team] - w.match.score[opp];
    std::int32_t& st = w.formation.adapt[team];
    if (diff > 0) {
        if (st == 2) return;
        moveLine(d, 4, 3, 2, 3);
        st = 2;
    } else if (diff < 0) {
        if (rem > 0xe10) return;
        if (st == 1) return;
        moveLine(d, 1, 2, 4, 3);
        st = 1;
    }
}

// FUN_004CEB74 (EAX = team, EDX = desc, EBX = player count): human team under AI management.
void adaptHuman(World& w, int team, std::uint8_t* d, std::uint32_t n) {
    if (n < 8) {
        std::uint8_t tmp[7];
        std::memcpy(tmp, kFewDesc[n], 7);
        scoreAdapt(w, team, tmp);
        Formation_applyDescriptor(w, team, tmp);
        return;
    }
    const int opp = team == 0 ? 1 : 0;
    const std::uint8_t* tab =
        static_cast<std::uint32_t>(w.match.score[opp]) > static_cast<std::uint32_t>(w.match.score[team]) ? kAdjust
                                                                                                          : kAdjust + 8;
    std::int32_t sum = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint8_t& v = d[tab[2 * i]];
        if (v < tab[2 * i + 1]) {
            sum += v - tab[2 * i + 1];
            v = tab[2 * i + 1];
        }
    }
    const std::uint8_t* prev = tab + 4;
    const std::uint8_t* cur = tab + 6;
    std::int32_t ecx = d[prev[0]] - prev[1];
    int k = 2;
    while (sum < 0) {
        const std::int32_t val = d[cur[0]] - cur[1];
        std::int32_t e = ecx - val;
        if (e >= 0) {
            if (ecx - e > 0) ++e;
            if (e > -sum) e = -sum;
            sum += e;
            d[prev[0]] = static_cast<std::uint8_t>(d[prev[0]] - e);
        }
        if (--k < 0) k = 4;
        prev = cur;
        cur = tab + k * 2;
        ecx = val;
    }
    scoreAdapt(w, team, d);
    Formation_applyDescriptor(w, team, d);
}

// FUN_004CECC4 (EAX = team, EDX = style, EBX = player count): AI team.
void adaptAi(World& w, int team, int style, std::uint32_t n) {
    std::uint8_t tmp[7];
    if (style == 0xe) {
        Formation_applyDescriptor(w, team, n < 5 ? kFewDesc[n] : kStyleDesc[style]);
        return;
    }
    if (n >= 8 && n <= 10) {
        std::memcpy(tmp, kStyleDesc[style], 7);
        const int opp = team == 0 ? 1 : 0;
        const std::uint8_t* lst = kRemoval + (static_cast<std::uint32_t>(w.match.score[team]) <
                                                      static_cast<std::uint32_t>(w.match.score[opp])
                                                  ? kRemovalB[style]
                                                  : kRemovalA[style]);
        for (int i = 0; i < static_cast<int>(11 - n); ++i) --tmp[lst[i]];
    } else {
        std::memcpy(tmp, n < 8 ? kFewDesc[n] : kStyleDesc[style], 7);
    }
    scoreAdapt(w, team, tmp);
    Formation_applyDescriptor(w, team, tmp);
}

} // namespace

// 0x4CEDAC (EAX = team): re-derives the active formation from the player count and the score.
void Formation_adaptToPlayers(World& w, int team) {
    TeamState& t = w.teams[team];
    std::uint8_t* d = t.rec(t.formIdx()) + 1;
    const std::uint32_t n = static_cast<std::uint32_t>(t.playerCount);
    if (t.humans != 0 && d[0] != 0) {
        if (t.setupAuto == 0) Formation_applyDescriptor(w, team, d);
        else adaptHuman(w, team, d, n);
        return;
    }
    adaptAi(w, team, t.formStyle, n);
}

} // namespace wc98::sim
