// WC98 set-piece placement grammar (D-27): FUN_0049e400 band initializer, FUN_004c1730 selector
// arbiter lane, FUN_004c09a0 placement machine. Grammar authority = FIFAWC.EXE decompiles in
// analysis/pass17setpiece/ (NOT the FIFA-99 note). Line cites are setpiece_0x4c09a0.txt /
// driver_0x4c1730.txt / pitchinit_0x49e400.txt. All arithmetic is 16.16; rounded writes use the
// exe's inline `(lo>>16)+(hi*0x10000)+((prod>>15&1)!=0)` form (fx) unless the cited row shows the
// adc-less truncating variant (fxt).
#include "wc98/sim/setpieces.hpp"

#include <cmath>
#include <cstdlib>
#include <initializer_list>

namespace wc98::sim {
namespace {


constexpr std::int32_t sh(std::int32_t v) { return Fixed(std::uint32_t(v) << 16); }

// Cylinder sweep predicate (pred_0x4bf860 / pred_0x4bf910 share the quadratic; radius rows
// f8>>1 vs fc>>1 + scale). [P] register-arg recovery: Ghidra soup shows EDX/ECX/EAX/EBX = o/d
// pair with EBX = result slot (chosen (pos.x,pos.z)+(vx,vz), documented in NOTES.md); the
// helper-chain scale convention (FUN_0049e16c square / FUN_0049e1d0 dot / FUN_004a19b8 square /
// FUN_0049e13c sqrt / FUN_0049e220 divide) is modeled with 16.16 >>16-scaled products in wide
// intermediates. Sets min `t` (LAB_004bf8e2 accumulator).
bool fdivBig(__int128 num, long long den, Fixed& out) {           // FUN_0049e220 split divide [P]
    const __int128 q = (num << 16) / den;                          // truncated toward zero
    if (q > 0x7fffffff || q < -0x80000000LL) return false;         // saturate out of Fixed range
    out = Fixed(q);
    return true;
}
bool sweepPlane(const Vec3& pos, Fixed vx, Fixed vz, std::int32_t radius, Fixed& t) {
    const __int128 ox = pos.x, oz = pos.z, dx = vx, dz = vz;
    const long long A = (long long)((dx * dx + dz * dz) >> 16);   // FUN_0049e16c square pair l.3-5 [P]
    if (A <= 0) return false;                                      // l.6-7: zero quadratic -> no hit
    const long long dot = (long long)(((ox * dx + oz * dz) << 1) >> 16);   // FUN_0049e1d0 chain l.6 [P]
    const __int128 Cw = ((ox * ox + oz * oz - (__int128(radius) * radius << 32)) >> 16);   // l.8-10 [P]
    const __int128 D = __int128(dot) * dot - 4 * A * Cw;           // FUN_004a19b8 square chain [P]
    if (D < 0) return false;                                       // l.11-13: FUN_0049e13c sqrt guard
    const long long sq = (long long)(sqrtl((long double)D));      // wide isqrt lane [P]
    Fixed t1, t2;                                                  // l.18-21 root pair
    const bool h1 = fdivBig(-(__int128)dot + sq, 2 * A, t1);
    const bool h2 = fdivBig(-(__int128)dot - sq, 2 * A, t2);
    bool any = false;                                              // LAB_004bf8e2 min accumulator l.23-27
    for (const Fixed c : {h1 ? t1 : Fixed(-1), h2 ? t2 : Fixed(-1)})
        if (c >= 0 && (!any || c < t)) { t = c; any = true; }
    return any;
}

// Case 1/2 velocity re-aim helpers FUN_004bf660 / FUN_004bf760: sqrt(vx²+vz²) magnitude, speed =
// fx(mag, 0x8000), direction via atan/sin helper pair (FUN_004a6fa0 + FUN_004a5d50). [P] register
// recovery: modeled with the ported sinCos(atan2i) accessors; not consumed by the D-27 gates.

} // namespace

WC98Band makeWC98Band(int stadiumId, const WC98Band& reuseBand) {
    WC98Band b;
    switch (stadiumId) {                       // FUN_0049e400 switch(in_EAX), l.10-36
    case 0:  b.width = 0xff0; b.length = 0x1620; break;
    case 1:  b.width = 0xf00; b.length = 0x1500; break;
    case 3:  b.width = 0xe10; b.length = 0x12c0; break;
    case 5:                                    // l.24-28: reuse non-zero band [P]
        b = reuseBand;
        if (b.width != 0) { b.levelId = stadiumId; return b; }
        [[fallthrough]];
    default: b.width = 0xe40; b.length = 0x13e0; break;
    }
    b.standWidth = b.width + 0x840;            // l.37-39 (DAT_0064130c)
    b.penAreaDepthDelta = 0x840;               // DAT_006412d4 (l.40)
    b.penSpotDist = 0x240;                     // DAT_006412dc (l.41)
    b.goalAreaWidth = 0x3c0;                   // DAT_006412e0 (l.42)
    b.goalAreaDepth = 0x120;                   // DAT_006412e4 (l.43)
    b.goalHeight = 0x98;                       // l.44
    b.barHeightConst = 0x98;                   // l.45 (DAT_006412f4, feeds the box band writer)
    b.netDepth = 0x4c;                         // DAT_006412f0 (l.46)
    b.goalWidth = 0x1c8;                       // DAT_006412ec (l.47)
    b.postThickness = 6;                       // l.48-49: f8 = fc = 6
    b.barThickness = 6;
    b.standLength = b.length + 0xa20;          // l.51-53 (DAT_00641308)
    b.goalWidthVisual = 0x180;                 // DAT_00641300 (l.54)
    b.penAreaDepth = 0x360;                    // DAT_006412d8 (l.55)
    b.flag40 = 0x40;                           // _DAT_00641304 (l.56)
    const std::int32_t hw = b.width >> 1, hl = b.length >> 1;   // iVar1/iVar2 l.57-58
    b.halfWidth = hw;                          // DAT_00641310 (l.59)
    b.halfLength = hl;                         // DAT_00641314 (l.60)
    b.penAreaHalfWidth = 0x840 / 2;            // DAT_00641318 = d4/2 (l.61)
    b.goalAreaLineZ = hl - b.penAreaDepth;     // DAT_0064131c (l.62-63)
    b.thirdBandZ = hl - b.goalAreaDepth;       // DAT_00641330 (l.64-65)
    b.goalAreaHalfWidth = 0x3c0 / 2;           // DAT_00641320 = e0/2 (l.66)
    b.goalHalfWidth = 0x1c8 / 2;               // DAT_00641324 (l.67-68)
    b.standHalfWidth = b.standWidth >> 1;      // DAT_00641328 (l.69-70)
    b.standHalfLength = b.standLength >> 1;    // DAT_0064132c (l.71-72)
    b.penSpotBand = hl - b.penSpotDist;        // DAT_00641354 (l.73-74)
    b.negHalfLength = -hl;                     // DAT_0064138c (l.75)
    b.sixthLength = b.length / 6;              // DAT_00641398 (l.76-77)
    b.negPenSpotBand = -(hl - b.penSpotDist);  // DAT_00641348 (l.78-79)
    b.negSixthLength = -(b.length / 6);        // DAT_00641394 (l.80)
    b.invHalfLength = fdiv(0x10000, hl);       // DAT_0064139c (l.81-82 split divide)
    b.fifthWidth = b.width / 5;                // DAT_006413a0 (l.83-84)
    b.fifthLength = b.length / 5;              // DAT_006413a4 (l.85-86)
    const std::int32_t q0 = ((b.width >> 31) & 3) + b.width;   // watcom div-by-4 l.87-90
    b.quarterWidth = (q0 >> 2) | ((b.width < 0 && std::uint32_t(b.width) != 0x80000000u) ? -0x40000000 : 0);
    const std::int32_t q1 = ((b.length >> 31) & 3) + b.length; // l.91-94
    b.quarterLength = (q1 >> 2) | ((b.length < 0 && std::uint32_t(b.length) != 0x80000000u) ? -0x40000000 : 0);
    b.thirdWidth = b.width / 3;                // DAT_006413b0 (l.95-96)
    b.thirdLength = b.length / 3;              // DAT_006413b4 (l.97-98)
    b.halfPenDepth = 0x360 / 2;                // DAT_006413b8 = d8/2 (l.99)
    b.positiveHalfLengthAlias = hl;            // _DAT_00641390 (l.100)
    b.levelId = stadiumId;                     // _DAT_006413bc (l.101)
    return b;
}

BoxBand makeBoxBand(const WC98Band& band, std::int32_t scale, Fixed aspect) {
    const Fixed near = sh(band.halfLength + band.barHeightConst);   // iVar3 l.46-47 (f4 row)
    const Fixed far = sh(band.halfLength + band.netDepth);          // iVar5 l.48-49 (f0 row)
    return {near - fdiv(sh(scale), aspect),     // DAT_00648f8c l.51-53 (+ FUN_004a72bc divide)
            near + fx(sh(scale), aspect),       // DAT_00648f94 l.54-57
            far - fx(sh(scale), aspect),        // DAT_00648f90 l.58-61
            far + fx(sh(scale), aspect)};       // DAT_00648f88 l.62-65
}

ArbiterResult SetPiece_arbitrate(const SetPieceRec& rec, const WC98Band& band, const BoxBand& box,
                                 std::int32_t sidePlaneY, bool velEvents, std::int32_t scale) {
    ArbiterResult res;                           // local_20 = 0; local_1c = 0x10001 (driver l.21-22)
    const Fixed postHalf = band.postThickness >> 1;

    if (velEvents) {                             // DAT_00641011 & 2 velocity-band lane, driver l.91-116
        const std::int32_t tgtX = sh(band.halfWidth - sidePlaneY + -1);   // l.93-95 (d30.y slot)
        if (tgtX < rec.absX && rec.absX - rec.absPrevX != 0) {            // l.96-97
            res.time = fdiv(tgtX - rec.absPrevX, rec.absX - rec.absPrevX);// l.98-100 FUN_004bf390 form
            res.selector = 3;                    // l.100 (unconditional on the band hit)
        }
        const std::int32_t tgtZ = sh(band.halfLength - sidePlaneY + -1);  // l.101-103
        if (rec.tagGoalPlane == -1 && tgtZ < rec.absZ &&                  // l.104 (bdc stamp)
            (sh((band.barThickness >> 1) + band.goalHeight - scale) < rec.pos.y ||   // f8>>1 row [P]
             sh(postHalf + band.goalHalfWidth) < rec.absX)) {             // l.105-106
            const Fixed d = rec.absZ - rec.absPrevZ;                      // l.107-109
            if (d != 0) {                                                // l.110 divide guard
                const Fixed t = fdiv(tgtZ - rec.absPrevZ, d);            // l.111-112
                if (t < res.time && -1 < t) { res.time = t; res.selector = 4; }   // l.113-115
            }
        }
    }

    if (rec.state == 2)                          // ball state==2 -> handler lane FUN_004c12e0 [P]
        return res;                              // selector stays 0: driver takes LAB_004c1a85

    // Band gate A (driver l.124-136): goal-mouth band window on |x|/|z|, then pred_0x4bf860.
    const std::int32_t a = sh(band.halfLength - (band.postThickness + scale) + -2);   // l.124
    const std::int32_t bhi = sh(band.halfLength + scale + 2);                         // l.126-127
    const std::int32_t c = sh(band.postThickness + sidePlaneY + band.goalHalfWidth + 2);   // l.128-129
    if ((a <= rec.absX || a <= rec.absZ) && (rec.absZ <= bhi || rec.absPrevZ <= bhi) &&
        (rec.absX <= c || rec.absPrevX <= c)) {                                        // l.125-129
        const std::int32_t d = sh(band.goalHalfWidth - scale + -2);                   // l.130
        const std::int32_t e = sh((band.barThickness >> 1) + band.goalHeight - scale);// l.132-133 [P]
        if ((d <= rec.absX || d <= rec.absPrevX) && (rec.pos.y <= e || rec.prev.y <= e)) {   // l.131/134
            Fixed t = 0;                                                             // l.135-136
            if (sweepPlane(rec.pos, rec.vx, rec.vz, postHalf + scale, t)) {          // pred_0x4bf860
                res.selector = 1;                                                    // driver l.137
                if (t < res.time) res.time = t;                                      // LAB_004bf8e2 min
            }
        }
    }
    // Touchline band gate + pred_0x4bf910 (driver l.137-150): |y| window, radius fc>>1+scale.
    {
        const std::int32_t f = sh(band.goalHeight - scale + -2);          // e8 row gate [P]
        Fixed t = 0;
        if ((f <= rec.pos.y || f <= rec.prev.y) &&                        // l.137-141 y-band tests [P]
            sweepPlane(rec.pos, rec.vx, rec.vz, (band.barThickness >> 1) + scale, t)) {   // pred_0x4bf910
            res.selector = 2;                                             // driver l.143-150
            if (t < res.time) res.time = t;
        }
    }

    // Box-band gates (driver l.152-163): the box window test pair on pos/prev vs DAT_00641330/20
    // bands, then pred FUN_004bffd0 core (loose-ball designation soup [P]) -> selector 9.
    if (rec.absX < sh(band.thirdBandZ) && rec.absZ >= box.crossNear) {    // l.152-156 [P]
        res.selector = 9;                                                 // driver l.157-163
    }
    // Event predicate pred_0x4bfed0 (driver l.165-168): player-record +0xc20 tag-table scan [P]:
    if (rec.id != 0 && rec.tagGoalPlane == -1)                            // [P] model: stamped-tag event
        res.selector = 8;                                                 // unconditional on true
    return res;
}

MachineOutcome SetPiece_place(int selector, const SetPieceRec& recIn, const WC98Band& band,
                              const BoxBand& box, std::int32_t scale, const Vec3& xyz,
                              std::uint32_t clampFlags) {
    MachineOutcome out;
    if (selector < 1 || selector > 10) return out;         // guard l.13: `9 < param_1 - 1U`
    SetPieceRec r = recIn;                                 // machine mutates slots in place
    const Fixed postHalf = band.postThickness >> 1;        // iVar5 l.16 (f8>>1)

    switch (selector) {                                    // switch(param_1) l.17
    case 1: {                                              // goal-line restart, l.18-37
        const Fixed xoff = sh(postHalf + band.goalHalfWidth);            // l.19-24 magnitude
        const Fixed sx = r.pos.x < 1 ? xoff : -xoff;                     // sign on pos.x slot (bba4)
        const Fixed sz = r.pos.z > 0 ? -sh(band.halfLength - postHalf)   // l.25-28 sign on pos.z
                                     : sh(band.halfLength - postHalf);
        if (r.tagBE0 == -1) r.tagBE0 = std::int16_t(r.id);               // l.29-31 tag from b54 [P slot]
        out.flagsD |= 0x10;                                // l.32 EBX+0xd bit
        out.target = {xyz.x + sx, xyz.y, xyz.z + sz};      // FUN_004bf660 target pair (x+iVar3,z+iVar2) l.33
        out.reaim = true;                                  // velocity re-aim lane runs [P]
        r.pos = xyz;                                       // l.34-37 pos := args triple
        break;                                             // no b80 target write: placement is the
        // re-aim lane + tag stamp only (the driver's rounded triple lives in the ball block).
    }
    case 2: {                                              // touchline restart, l.38-52
        const Fixed yoff = -sh((band.barThickness >> 1) + band.goalHeight - scale);   // l.39 (fc>>1 row)
        const Fixed zoff = r.pos.z > 0 ? -sh(band.halfLength - postHalf)              // l.40-43
                                       : sh(band.halfLength - postHalf);
        if (r.tagBE0 == -1) r.tagBE0 = std::int16_t(r.id);               // l.44-46 (be0 slot)
        out.flagsD |= 0x20;                                // l.47 EBX+0xd bit
        out.reaim = true;                                  // FUN_004bf760 (y+yoff*-1, z+zoff) l.48 [P]
        out.target = {xyz.x, xyz.y + yoff, xyz.z + zoff};  // helper target args (placement pair)
        r.pos = xyz;                                       // l.49-51 pos := raw args triple
        break;
    }
    case 3:                                                // velocity event x lane, l.53-73
    case 4: {                                              // velocity event z lane, l.74-94
        const Fixed prod = fx(selector == 3 ? r.vx : r.vz, 0xc000);   // l.55-58/76-79 rounded product
        Fixed res = -prod;                                                // l.60-61 sign inversion write
        if ((clampFlags & 0x40) != 0 && res < -0x50000) {   // l.62-65 DAT_00646a24 & 0x40 gate
            out.velClamped = true;
            res = -0x50000;                                  // 0xfffb0000 saturating write (l.64/85)
        } else if ((clampFlags & 0x40) != 0 && prod != -0x50000 && 0x4ffff < res) {   // l.66-68/87-88
            out.velClamped = true;
            res = 0x50000;                                   // +0x50000 saturating write
        }
        if (selector == 3) out.vx = res;                     // l.59/61 b98 slot write
        else out.vz = res;                                   // l.80/82 ba0 slot write
        out.flagsD |= 0x80;                                  // l.54/75 EBX+0xd bit
        r.pos = xyz;                                         // l.70-72/91-93 pos := args
        break;                                               // no b80 target write
    }
    case 5:                                                // kickoff-style placement, l.95-112
        r.prevSlot = r.prev;                               // l.96-98 copy prev triple into +0xb8c slot
        out.vyZero = true;                                 // l.99 vy slot (b9c) := 0
        out.flagsE |= 0x101;                               // l.101 EBX+0xe bits 0|8
        out.subState = 1;                                  // l.102 b60=1
        out.state = 2;                                     // l.103 b5c=2
        out.target = xyz;                                  // l.104-106 target := args triple
        out.target.y = 0;                                  // l.107 y := 0
        out.rounded = {out.target.x >> 16, 0, out.target.z >> 16};       // l.108-110 (>>0x10) [P slot]
        r.pos = xyz;                                       // placement triple written for the replica
        break;
    case 7: {                                              // goal-mouth x-band restart, l.113-155
        r.prevSlot = r.prev;                               // l.114-116 prev copy
        out.flagsE |= 0x108;                               // l.118 EBX+0xe bits 3|8
        out.subState = 4;                                  // l.119 b60=4
        out.state = 2;                                     // l.120 b5c=2
        Vec3 t = xyz;                                      // l.121-123 target := args triple
        const Fixed off = fxt(r.vx, 0x50000);              // l.124-131 adc-less truncating product [P form]
        const std::int32_t s = (off + xyz.x < 0) ? -(off + xyz.x) : (off + xyz.x);   // abs(x+off) l.125-132
        const Fixed bandv = sh(postHalf + band.goalHalfWidth - scale);   // l.133-134 (f8>>1 row)
        if (bandv < s) {                                   // l.135 band test on |x+off|
            t.x = r.vx < 1 ? -sh(postHalf + band.goalHalfWidth - scale) + sh(0x10)   // l.136-142
                           : bandv - sh(0x10);             // -band<<..+0x100000 / band-0x100000
        } else {
            t.x += off;                                    // l.145-148 target.x += truncating product
        }
        out.target = t;
        out.target.y = 0;                                  // l.150 y := 0
        out.rounded = {out.target.x >> 16, 0, out.target.z >> 16};       // l.151-153 [P slot]
        break;
    }
    case 8:                                                // event placement, l.156-170
        r.prevSlot = r.prev;                               // l.157-159 prev copy
        out.flagsE |= 0x110;                               // l.160 EBX+0xe bits 4|8
        out.subState = 5;                                  // l.161 b60=5
        out.state = 2;                                     // l.162 b5c=2
        out.target = {xyz.x, xyz.y - sh(6), xyz.z};        // l.163-166 y(b84) += -0x60000
        out.rounded = {out.target.x >> 16, out.target.y >> 16, out.target.z >> 16};   // l.167-169
        break;                                             // no handler call (l.170 return)
    case 9: {                                              // box-band restart, l.171-238
        r.prevSlot = r.prev;                               // l.172-174 prev copy
        const bool sameSign = (r.pos.z > 0) == (r.pos.x > 0);            // l.175 z/x sign equality
        out.flagsE |= sameSign ? 0x40 : 0x20;              // l.176-180 EBX+0e bits
        out.flagsF |= 1;                                   // l.181 EBX+0xf bit0
        out.subState = sameSign ? 7 : 6;                   // l.182-187 b60=7/6
        out.state = 2;                                     // l.188 b5c=2
        Vec3 t = xyz;                                      // l.189-191 target := args triple
        const Fixed off = fx(r.vz, 0x78000);               // l.192-193 rounded vz*0x78000
        const std::int32_t s = (off + xyz.z < 0) ? -(off + xyz.z) : (off + xyz.z);    // abs(z+off) l.194-206
        if (box.goalNear < s) {                            // DAT_00648f8c test l.207
            t.z = xyz.z < 1 ? sh(2) - box.goalNear : box.goalNear - sh(2);   // l.208-213 (b88<1 read l.208)
        } else if (sh(band.halfLength) <= s) {             // DAT_00641314<<16 test l.216
            t.z += off;                                    // l.217-222 target.z += rounded product
        } else {
            t.z = xyz.z < 1 ? -sh(band.halfLength) - sh(2) // l.225-230 sign on b88 slot read
                            : sh(band.halfLength) + sh(2);
        }
        out.target = t;
        out.target.y = 0;                                  // LAB_004c1067 l.234 y := 0
        out.rounded = {out.target.x >> 16, 0, out.target.z >> 16};       // l.235-237 [P slot]
        break;                                             // no handler call (l.238 return)
    }
    case 10: {                                             // goal-net placement lane, l.239-259
        r.prevSlot = r.prev;                               // l.240-242 prev copy
        out.flagsE |= 0x180;                               // l.243 EBX+0xe bits 7|8
        out.subState = 8;                                  // l.244 b60=8
        out.state = 2;                                     // l.245 b5c=2
        Vec3 t = {xyz.x, xyz.y, box.goalFar};              // l.246-254 DAT_00648f94 with negation
        if (xyz.z < 1) t.z = -box.goalFar;                 // l.248-250 param_5<1 -> -goalFar
        out.target = t;
        out.target.y = 0;                                  // l.255 y := 0
        out.rounded = {t.x >> 16, 0, t.z >> 16};           // l.256-258 [P slot]
        break;
    }
    default: {                                             // selector 6 tail block, l.261-331 [P]
        r.prevSlot = r.prev;                               // l.261-263 prev copy
        if (r.absZ < box.crossNear && r.absPrevZ < box.crossNear) {}    // l.264-266 FUN_004c08d0 gate [P]
        const bool sameSign = (r.pos.z > 0) == (r.pos.x > 0);             // l.267-279 sign equality flags
        out.flagsE |= sameSign ? 4 : 2;                    // l.268-272 EBX+0e bits
        out.flagsF |= 1;                                   // l.273 EBX+0xf bit0
        out.subState = sameSign ? 3 : 2;                   // l.274-279 b60=3/2
        out.state = 2;                                     // l.280 b5c=2
        Vec3 t = xyz;                                      // l.281-283 target := args triple
        const Fixed off = fx(r.vz, 0x78000);               // l.284-298 rounded vz*0x78000 (adc form)
        const std::int32_t s = (off + xyz.z < 0) ? -(off + xyz.z) : (off + xyz.z);
        if (box.goalNear < s) {                            // l.299-305 DAT_00648f8c test
            t.z = xyz.z < 1 ? sh(2) - box.goalNear : box.goalNear - sh(2);
        } else if (sh(band.halfLength) <= s) {             // l.308-315 DAT_00641314<<16 test
            t.z += off;
        } else {
            t.z = xyz.z < 1 ? -sh(band.halfLength) - sh(2) : sh(band.halfLength) + sh(2);   // l.317-322
        }
        out.target = t;
        out.target.y = 0;                                  // LAB_004c0cbc y := 0 (l.326)
        out.rounded = {out.target.x >> 16, 0, out.target.z >> 16};       // l.327-329 [P slot]
        break;                                             // FUN_004c12e0 handler call l.330 [P]
    }
    }
    return out;
}

} // namespace wc98::sim
