// Derived from fifa99-native (same author), ported for World Cup 98
// Simulation core: fixed-point rounding, trig/atan tables, octagonal distance, RNG.
#include "check.hpp"
#include "wc98/sim/fixed.hpp"
#include "wc98/sim/pitch.hpp"
#include "wc98/sim/rng.hpp"
#include "wc98/sim/sim.hpp"

using namespace wc98::sim;

int main() {
    // fx: (a*b + 0x8000) >> 16 (bit 15 of the low word added, arithmetic for negatives).
    CHECK(fx(0x10000, 0x10000) == 0x10000);
    CHECK(fx(0x18000, 0x8000) == 0xc000);
    CHECK(fx(1, 0x8000) == 1);       // 0.5 ulp rounds up
    CHECK(fx(1, 0x7fff) == 0);
    CHECK(fx(-1, 0x8000) == 0);      // -0.5 ulp rounds up to 0
    CHECK(fx(-1, 0x8001) == -1);
    CHECK(fx(0x2f5c0, 0xeb) == 0x2b8); // gravity-sized value times drag
    CHECK(fxt(-1, 0x8000) == -1);    // truncating variant floors
    CHECK(fdiv(0x10000, 0x30000) == 0x5555);
    CHECK(fdiv(-0x10000, 0x30000) == -0x5555); // idiv truncates toward zero

    // Angles: 0 faces +z, 0x100 faces +x.
    CHECK(sinCos(0).s == 0 && sinCos(0).c == 0x10000);
    CHECK(sinCos(0x100).s == 0x10000 && sinCos(0x100).c == 0);
    CHECK(sinCos(0x200).s == 0 && sinCos(0x200).c == -0x10000);
    CHECK(sinCos(0x300).s == -0x10000 && sinCos(0x300).c == 0);
    CHECK(sinCos(-0x100).s == -0x10000);
    CHECK(atan2i(0, 100) == 0);
    CHECK(atan2i(100, 0) == 0x100);
    CHECK(atan2i(0, -100) == 0x200);
    CHECK(atan2i(-100, 0) == -0x100);
    CHECK(atan2i(100, 100) == 0x80);
    CHECK(atan2i(-100, -100) == -0x180);
    for (Angle a = -0x200; a < 0x200; a += 7) {
        const SinCos sc = sinCos(a);
        const Angle back = atan2i(sc.s, sc.c);
        CHECK(iabs(wrapAngle(back - a)) <= 1);
    }

    // Math_octDistance: |dx| + |dz| - min/2.
    CHECK(octLength(300, -400) == 550);
    CHECK(octLength(-3, 3) == 5);
    const OctDist d = octDistance({10, 0, 10}, {-90, 0, 210});
    CHECK(d.dx == -100 && d.dz == 200 && d.dist == 250);

    // RNG: reference values from an independent transcription of the Ghidra output of
    // 0x4BB000 / 0x4BAFD8.
    Rng r;
    r.seed(0);
    const std::uint32_t e0[] = {512, 1829, 4927, 11195, 22605, 41818};
    for (std::uint32_t v : e0) CHECK(r.next() == v);
    r.seed(0x12345678);
    const std::uint32_t e1[] = {45570, 10026, 13128, 48072, 43103, 58227};
    for (std::uint32_t v : e1) CHECK(r.next() == v);
    r.seed(7);
    std::uint32_t sum = 0, last = 0;
    for (int i = 0; i < 100000; ++i) sum += last = r.next();
    CHECK(last == 1835 && sum == 3282095454u);

    // Pitch sizes.
    CHECK(Pitch::make(0).width == 4080 && Pitch::make(0).length == 5664);
    CHECK(Pitch::make(2).halfLength == 2544 && Pitch::make(5).width == 3648);
    CHECK(Pitch::make(2).penSpotZ == 2544 - 576 && Pitch::make(2).thirdLineZ[1] == 848);

    // Timing constants.
    CHECK(kTickDt == 0x20000);
    CHECK(renderInterpolation(1000) == 0x3c0000 && renderInterpolation(16) == 0xf5c2);
    return 0;
}
