// D-27 set-piece grammar gate (pass-17setpiece): pins the FUN_0049e400 band initializer rows,
// the box-band writer arithmetic, the FUN_004c1730 selector arbiter on fixed input segments, and
// the FUN_004c09a0 per-case placement triples (exact >>0x10 rounded coordinates). Dossier:
// analysis/pass17setpiece/. Pure simulation; no tree gate.
#include "wc98/sim/setpieces.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_fail;
    }
}
constexpr std::int32_t sh(std::int32_t v) { return std::int32_t(std::uint32_t(v) << 16); }

using namespace wc98::sim;

} // namespace

int main() {
    // ---- 1. Band initializer (FUN_0049e400): stadium switch rows + derived constants ----------
    {
        const WC98Band b0 = makeWC98Band(0);   // l.12-15 row pair
        check(b0.width == 0xff0 && b0.length == 0x1620, "stadium0 rows");
        const WC98Band b1 = makeWC98Band(1);   // l.16-19
        check(b1.width == 0xf00 && b1.length == 0x1500, "stadium1 rows");
        const WC98Band b3 = makeWC98Band(3);   // l.20-23
        check(b3.width == 0xe10 && b3.length == 0x12c0, "stadium3 rows");
    }
    {
        const WC98Band b = makeWC98Band(2);    // default row (l.30-35) + derived block
        check(b.width == 0xe40 && b.length == 0x13e0, "default rows");
        check(b.halfWidth == 0x720 && b.halfLength == 0x9f0, "half rows (l.57-60)");
        check(b.standWidth == 0x1680 && b.standHalfWidth == 0xb40, "stand width (l.37-39,69)");
        check(b.standLength == 0x1e00 && b.standHalfLength == 0xf00, "stand length (l.51-53,71)");
        check(b.penAreaDepthDelta == 0x840 && b.penAreaHalfWidth == 0x420, "pen-area depth rows");
        check(b.penSpotDist == 0x240 && b.penSpotBand == 0x7b0 && b.negPenSpotBand == -0x7b0,
              "pen-spot band (l.41,73-79)");
        check(b.goalAreaWidth == 0x3c0 && b.goalAreaHalfWidth == 0x1e0, "goal-area width");
        check(b.goalAreaDepth == 0x120 && b.thirdBandZ == 0x8d0, "third band z (l.43,64)");
        check(b.penAreaDepth == 0x360 && b.goalAreaLineZ == 0x690 && b.halfPenDepth == 0x1b0,
              "goal-area line z (l.55,62,99)");
        check(b.goalWidth == 0x1c8 && b.goalHalfWidth == 0xe4, "goal width rows (l.47,67)");
        check(b.postThickness == 6 && b.barThickness == 6, "f8=fc=6 (l.48-49: DAT_006412f8=f8");
        check(b.netDepth == 0x4c && b.barHeightConst == 0x98 && b.goalHeight == 0x98, "e8/f0/f4 rows");
        check(b.sixthLength == 0x350 && b.negSixthLength == -0x350, "sixth rows (l.76-80)");
        check(b.invHalfLength == 1688273, "inv half-length split divide (l.81-82)");
        check(b.fifthWidth == 729 && b.fifthLength == 1017, "fifths (l.83-86)");
        check(b.quarterWidth == 0x390 && b.quarterLength == 0x4f8, "quarters (l.87-94)");
        check(b.thirdWidth == 1216 && b.thirdLength == 1696, "thirds (l.95-98)");
        check(b.positiveHalfLengthAlias == 0x9f0 && b.levelId == 2, "alias + level rows");
    }

    // ---- 2. Box band writer (FUN_004bf1e0), scale=5, aspect r = 1.0 [P model] ------------------
    {
        const WC98Band b = makeWC98Band(2);
        const BoxBand box = makeBoxBand(b, 5, 0x10000);
        check(box.goalNear == sh(0xa88) - 0x50000, "goalNear = (hl+f4)<<16 - fdiv(5<<16,r)");
        check(box.goalFar == sh(0xa88) + 0x50000, "goalFar = (hl+f4)<<16 + fx(5<<16,r)");
        check(box.crossNear == sh(0xa3c) - 0x50000, "crossNear = (hl+f0)<<16 - fx(5<<16,r)");
        check(box.crossHigh == sh(0xa3c) + 0x50000, "crossHigh = (hl+f0)<<16 + fx(5<<16,r)");
    }

    // ---- 3. Selector arbiter (FUN_004c1730 l.91-171) on fixed segments ------------------------
    // Compact synthetic band keeps the predicate magnitudes inside the exe's int32 helper lanes;
    // section 1 pins the initializer table rows separately.
    WC98Band sb{};                                    // width/length unused by the arbiter gates
    sb.halfWidth = 0x20;                              // 64 >> 1
    sb.halfLength = 0x40;                             // 128 >> 1
    sb.postThickness = 6;
    sb.barThickness = 6;
    sb.goalHeight = 0x98;
    sb.barHeightConst = 0x98;
    sb.netDepth = 0x4c;
    sb.goalHalfWidth = 8;
    sb.thirdBandZ = 50;                               // hl - goalAreaDepth lane (units)
    const BoxBand sbox = makeBoxBand(sb, 5, 0x10000);

    {   // Goal-line crossing segment: gate-A window open, sweep R=(f8>>1)+scale hit -> selector 1.
        SetPieceRec g{};
        g.pos = {sh(55), 0, sh(2)};
        g.prev = {sh(4), 0, -sh(6)};                  // absPrevX inside the c window, absPrevZ < bhi
        g.vx = -sh(22);
        g.vz = -sh(4);                                // sweep heads toward the origin cylinder
        g.absX = sh(55);
        g.absZ = sh(2);
        g.absPrevX = sh(4);
        g.absPrevZ = sh(6);
        const ArbiterResult a1 = SetPiece_arbitrate(g, sb, sbox, 0, false, 5);
        check(a1.selector == 1 && a1.time <= 0x10001, "goal-line band segment -> selector 1");
    }
    {   // Touchline crossing: gate-A bhi window closed (prev z past), y above f row, sweep hit.
        SetPieceRec t{};
        t.pos = {sh(55), sh(0xa0), sh(75)};           // |y| opens the f gate; z closes the bhi window
        t.prev = {sh(51), 0, sh(75)};                 // absPrevZ > bhi -> gate-A pair fails
        t.vx = -sh(55);                               // sweep aims at the origin cylinder
        t.vz = -sh(75);
        t.absX = sh(55);
        t.absZ = sh(75);
        t.absPrevX = sh(51);
        t.absPrevZ = sh(75);
        const ArbiterResult a2 = SetPiece_arbitrate(t, sb, sbox, 0, false, 5);
        check(a2.selector == 2, "touchline band segment -> selector 2");
    }
    {   // Box-band + predicate segment: |x| inside thirdBandZ window over crossNear -> selector 9.
        SetPieceRec c{};
        c.pos = {sh(10), sh(1), sh(0x90)};
        c.absX = sh(10);                              // < sh(thirdBandZ)
        c.absZ = sh(0x90);                            // >= crossNear (synthetic hl+f0 band scaled)
        const ArbiterResult a3 = SetPiece_arbitrate(c, sb, sbox, 0, false, 5);
        check(a3.selector == 9, "box-band segment -> selector 9");
    }
    {   // Velocity event x lane (DAT_00641011&2): absX past halfWidth-sidePlane -> selector 3.
        SetPieceRec v{};
        v.pos = {sh(40), sh(0x98), sh(0x10)};
        v.prev = {sh(36), 0, 0};
        v.absX = sh(40);                              // > tgtX = (halfWidth-0-1)<<16 = sh(31)
        v.absPrevX = sh(36);                          // delta != 0
        const ArbiterResult a4 = SetPiece_arbitrate(v, sb, sbox, 0, true, 5);
        check(a4.selector == 3 && a4.time < 0x10001, "velocity x-lane segment -> selector 3");
    }
    {   // Velocity z lane: goal-plane stamp -1 + band open + early time -> selector 4.
        SetPieceRec w{};
        w.pos = {sh(8), sh(0xa0), sh(70)};            // y above fc>>1+e8-scale opens the bar row
        w.prev = {w.pos.x, w.pos.y, sh(60)};          // absPrevZ below tgtZ -> positive early time
        w.absZ = sh(70);                              // > (halfLength-0-1)<<16 = sh(63)
        w.absPrevZ = sh(60);
        const ArbiterResult a5 = SetPiece_arbitrate(w, sb, sbox, 0, true, 5);
        check(a5.selector == 4 && a5.time < 0x10001, "velocity z-lane segment -> selector 4");
    }
    {   // Guard: ball state==2 takes the handler lane (selector stays 0).
        SetPieceRec s{};
        s.state = 2;
        const ArbiterResult a6 = SetPiece_arbitrate(s, sb, sbox, 0, true, 5);
        check(a6.selector == 0 && a6.time == 0x10001, "state==2 -> handler lane (no selector)");
    }

    // ---- 4. Placement machine (FUN_004c09a0): per-case exact rounded triples -------------------
    const WC98Band b = makeWC98Band(2);
    const BoxBand box = makeBoxBand(b, 5, 0x10000);
    {   // case 5 (l.95-112): target := args, y := 0, vy slot zeroed, subState=1/state=2.
        SetPieceRec r{};
        const MachineOutcome o = SetPiece_place(5, r, b, box, 5, {sh(10), sh(7), -sh(3)});
        check(o.rounded == Vec3{10, 0, -3}, "case 5 rounded triple");
        check(o.vyZero && o.state == 2 && o.subState == 1 && (o.flagsE & 0x101) == 0x101,
              "case 5 flags/state");
    }
    {   // case 8 (l.156-170): y(b84) += -0x60000; triple write; no handler tail.
        SetPieceRec r{};
        const MachineOutcome o = SetPiece_place(8, r, b, box, 5, {sh(2), sh(9), sh(30)});
        check(o.rounded == Vec3{2, 3, 30}, "case 8 rounded triple (y -= 6<<16)");
        check(o.state == 2 && o.subState == 5 && (o.flagsE & 0x110) == 0x110, "case 8 flags/state");
    }
    {   // case 9 (l.171-238): z band -> sign branch reads b88; goalNear overshoot lane.
        SetPieceRec r{};
        r.vz = sh(1);                                    // off = fx(vz,0x78000) = +0x78000
        const Vec3 in{sh(-2704), 0, -sh(2704)};          // |off+z| > goalNear, z < 1 branch
        const MachineOutcome o = SetPiece_place(9, r, b, box, 5, in);
        check(o.target.z == sh(2) - box.goalNear, "case 9 overshoot lane (l.208-209)");
        check(o.rounded.x == -2704 && o.rounded.y == 0 && o.rounded.z == ((sh(2) - box.goalNear) >> 16),
              "case 9 rounded triple overshoot");
        check((o.flagsF & 1) == 1 && (o.state == 2) && (o.subState == 7 || o.subState == 6),
              "case 9 flags/state pair");

        // Add-off lane: goalNear > |off+z| >= halfLength<<16 -> target.z += off (l.216-222).
        SetPieceRec m{};
        m.vz = sh(1);
        const MachineOutcome o2 = SetPiece_place(9, m, b, box, 5, {sh(4), 0, sh(0x9f0)});
        check(o2.target.z == sh(0x9f0) + fx(sh(1), 0x78000), "case 9 add-off lane (l.216-222)");

        // Pitch-edge lane: |off+z| < halfLength<<16 -> halfLength +/- 0x20000 (l.225-230).
        SetPieceRec q{};
        q.vz = sh(8);                                    // off = fx(8<<16,0x78000) = 0x3C0000
        const MachineOutcome o3 = SetPiece_place(9, q, b, box, 5, {sh(4), 0, sh(0x40)});
        check(o3.target.z == sh(b.halfLength) + sh(2), "case 9 pitch-edge lane");
    }
    {   // case 7 (l.113-155): x band with truncating product; overshoot and add lanes.
        SetPieceRec r{};
        r.vx = sh(1);                                    // off = fxt(vx,0x50000) = +0x50000
        const MachineOutcome a = SetPiece_place(7, r, b, box, 5, {-sh(2), sh(4), sh(6)});
        check(a.target.x == -sh(2) + fxt(sh(1), 0x50000), "case 7 add-off lane (l.145-148)");

        const MachineOutcome c = SetPiece_place(7, r, b, box, 5, {-sh(0xe9), sh(4), sh(6)});
        check(c.target.x == sh(b.postThickness / 2 + b.goalHalfWidth - 5) - sh(0x10),
              "case 7 overshoot lane (l.139-142, vx>=1)");

        SetPieceRec n{};
        n.vx = 0;                                        // vx < 1 -> mirrored overshoot write
        const MachineOutcome e = SetPiece_place(7, n, b, box, 5, {-sh(0xe9), sh(4), sh(6)});
        check(e.target.x == -(sh(b.postThickness / 2 + b.goalHalfWidth - 5)) + sh(0x10),
              "case 7 overshoot vx<1 lane (l.136-137)");
        check(e.state == 2 && e.subState == 4 && (e.flagsE & 0x108) == 0x108, "case 7 flags/state");
    }
    {   // case 10 (l.239-259): z = +/- DAT_00648f94 (goalFar).
        SetPieceRec r{};
        const MachineOutcome p = SetPiece_place(10, r, b, box, 5, {sh(7), sh(2), sh(1)});
        check(p.rounded == Vec3{7, 0, box.goalFar >> 16}, "case 10 +goalFar");
        const MachineOutcome n = SetPiece_place(10, r, b, box, 5, {sh(7), sh(2), -sh(1)});
        check(n.rounded.z == (-box.goalFar) >> 16 && n.subState == 8 && (n.flagsE & 0x180) == 0x180,
              "case 10 -goalFar + flags");
    }
    {   // cases 3/4 (l.53-94): sign-inverted rounded product with DAT_00646a24&0x40 clamp band.
        SetPieceRec r{};
        r.vx = sh(2);
        const MachineOutcome a = SetPiece_place(3, r, b, box, 5, {}, 0x40);
        check(a.vx == -fx(sh(2), 0xc000) && !a.velClamped, "case 3 sign inversion (l.60-61)");

        SetPieceRec c{};
        c.vx = sh(8);                                    // prod=+0x60000 -> neg=-0x60000 < -0x50000
        const MachineOutcome b4 = SetPiece_place(3, c, b, box, 5, {}, 0x40);
        check(b4.vx == -0x50000 && b4.velClamped, "case 3 lower clamp (l.63-64: 0xfffb0000)");

        SetPieceRec d{};
        d.vz = -sh(9);                                   // prod=-0x6C000 -> neg=+0x6C000 > 0x4FFFF
        const MachineOutcome c4 = SetPiece_place(4, d, b, box, 5, {}, 0x40);
        check(c4.vz == 0x50000 && c4.velClamped, "case 4 upper clamp (l.87-88: 0x50000)");

        const MachineOutcome off = SetPiece_place(3, c, b, box, 5, {}, 0);   // gate bit clear
        check(off.vx == -fx(sh(8), 0xc000) && !off.velClamped, "clamp gate 0x40 clear (l.62)");
    }
    {   // cases 1/2 (l.18-52): placement pair = helper target args; sign rows exact.
        SetPieceRec r{};
        r.pos.x = sh(5);
        r.pos.z = sh(0x9f0);
        const MachineOutcome a = SetPiece_place(1, r, b, box, 5, {sh(25), 0, sh(0x9f0)});
        check(a.target.x == sh(25) - sh(b.postThickness / 2 + b.goalHalfWidth), "case 1 x sign row (l.22-24)");
        check(a.target.z == sh(0x9f0) - sh(b.halfLength - b.postThickness / 2), "case 1 z sign row (l.25-26)");

        SetPieceRec t{};
        const MachineOutcome c = SetPiece_place(2, t, b, box, 5, {sh(3), sh(4), sh(0x9f0)});
        check(c.target.y == sh(4) - sh((b.barThickness >> 1) + b.goalHeight - 5), "case 2 y row (l.39,48)");
        check(c.target.z == sh(0x9f0) + sh(b.halfLength - b.postThickness / 2), "case 2 z sign row (l.41-43)");
        check((c.flagsD & 0x20) != 0, "case 2 flag bit");
    }
    {   // selector 6 tail (l.261-331): case-9 grammar with subState 3/2 and y := 0.
        SetPieceRec r{};
        r.pos.x = sh(4);
        r.pos.z = sh(1);                                   // z/x same-sign -> subState 3 (l.274-276)
        r.vz = sh(8);                                      // off = 0x3C0000; |off+z| below halfLength<<16
        const MachineOutcome o = SetPiece_place(6, r, b, box, 5, {sh(4), 0, sh(0x40)});
        check(o.state == 2 && o.subState == 3 && (o.flagsF & 1) == 1, "tail flags/state (l.274-280)");
    }
    {   // guard (l.13): selectors 0 and 11 write nothing.
        SetPieceRec r{};
        const MachineOutcome g0 = SetPiece_place(0, r, b, box, 5, {sh(1), sh(1), sh(1)});
        const MachineOutcome gB = SetPiece_place(11, r, b, box, 5, {sh(1), sh(1), sh(1)});
        check(g0.state == -1 && g0.target == Vec3{} && g0.rounded == Vec3{}, "guard selector 0");
        check(gB.state == -1, "guard selector 11");
    }

    std::printf("setpiece-grammar: %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
