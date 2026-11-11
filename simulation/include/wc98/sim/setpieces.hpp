#pragma once
// WC98 set-piece placement grammar (D-27, pass-17setpiece): the pitch-geometry band initializer
// FUN_0049e400, the ball-sweep selector arbiter lane of FUN_004c1730, and the placement machine
// FUN_004c09a0 (the "switch 1..10" case table; case 6 falls into the tail). Dossier:
// analysis/pass17setpiece/{setpiece_0x4c09a0.txt, driver_0x4c1730.txt, pitchinit_0x49e400.txt,
// bandwriter_0x4bf1e0.txt, pred_*.txt}. All arithmetic is 16.16 (fx = imul/shr/adc round-half-up,
// fdiv = idiv split form); the machine writes rounded (>>0x10) integer triples into the ball block.
#include <cstdint>
#include "wc98/sim/fixed.hpp"

namespace wc98::sim {

// DAT_006412cc..DAT_006413bc band: runtime-initialized bss (NOT image bytes), FUN_0049e400.
// The switch is on the stadium/level id; every other field is a derived constant of that pair.
struct WC98Band {
    std::int32_t width = 0, length = 0;      // +0xcc / +0xd0 (stadium table rows)
    std::int32_t standWidth = 0;             // +0x130c = width + 0x840
    std::int32_t penAreaDepthDelta = 0x840;  // +0xd4
    std::int32_t penSpotDist = 0x240;        // +0xdc
    std::int32_t goalAreaWidth = 0x3c0;      // +0xe0
    std::int32_t goalAreaDepth = 0x120;      // +0xe4
    std::int32_t goalHeight = 0x98;          // +0xe8
    std::int32_t netDepth = 0x4c;            // +0xf0 (DAT_006412f0)
    std::int32_t barHeightConst = 0x98;      // +0xf4 (DAT_006412f4, feeds the box band writer)
    std::int32_t goalWidth = 0x1c8;          // +0xec
    std::int32_t postThickness = 6;          // +0xf8
    std::int32_t barThickness = 6;           // +0xfc
    std::int32_t standLength = 0;            // +0x1308 = length + 0xa20
    std::int32_t goalWidthVisual = 0x180;    // +0x1300
    std::int32_t penAreaDepth = 0x360;       // +0xd8
    std::int32_t flag40 = 0x40;              // _DAT_00641304
    // Derived half/quarter/fifth/third rows (LAB_0049e4d3):
    std::int32_t halfWidth = 0, halfLength = 0;   // +0x1310 / +0x1314
    std::int32_t penAreaHalfWidth = 0;            // +0x1318 = penAreaDepthDelta/2
    std::int32_t goalAreaLineZ = 0;               // +0x131c = halfLength - penAreaDepth
    std::int32_t thirdBandZ = 0;                  // +0x1330 = halfLength - goalAreaDepth
    std::int32_t goalAreaHalfWidth = 0;           // +0x1320 = goalAreaWidth/2
    std::int32_t goalHalfWidth = 0;               // +0x1324 = goalWidth/2
    std::int32_t standHalfWidth = 0;              // +0x1328
    std::int32_t standHalfLength = 0;             // +0x132c
    std::int32_t penSpotBand = 0;                 // +0x1354 = halfLength - penSpotDist
    std::int32_t negHalfLength = 0;               // +0x138c = -halfLength
    std::int32_t sixthLength = 0;                 // +0x1398 = length/6
    std::int32_t negPenSpotBand = 0;              // +0x1348 = -penSpotBand
    std::int32_t negSixthLength = 0;              // +0x1394 = -sixthLength
    std::int32_t invHalfLength = 0;               // +0x139c = 1/halfLength (16.16 split divide)
    std::int32_t fifthWidth = 0, fifthLength = 0; // +0x13a0 / +0x13a4
    std::int32_t quarterWidth = 0, quarterLength = 0;   // +0x13a8 / +0x13ac (watcom div-by-4 form)
    std::int32_t thirdWidth = 0, thirdLength = 0;  // +0x13b0 / +0x13b4
    std::int32_t halfPenDepth = 0;                // +0x13b8 = penAreaDepth/2
    std::int32_t positiveHalfLengthAlias = 0;     // +0x1390 = halfLength
    std::int32_t levelId = 0;                     // _DAT_006413bc
};

// FUN_0049e400 replica. {0:(0xff0,0x1620) 1:(0xf00,0x1500) 3:(0xe10,0x12c0), default (0xe40,0x13e0)}.
// Case 5 reuses a live non-zero band [P: the exe reads the DAT rows; modeled here by keeping the
// caller's pair in `band` when non-zero]; the DAT_00641388!=0 entry-position branch is [P]
// (pitch.hpp::make pins the same rows).
WC98Band makeWC98Band(int stadiumId, const WC98Band& reuseBand = {});

// Box band DAT_00648f8c/94/90/88 written by FUN_004bf1e0 (single write site; dossier bandwriter).
// `aspect` r is the fixed-point ratio produced by the trig-helper pair FUN_004a6fa0/FUN_004a5d50
// at the call site [P: register args not recoverable from Ghidra soup].
struct BoxBand {
    Fixed goalNear = 0;   // DAT_00648f8c = (halfLength+barHeightConst)<<16 - fdiv(scale<<16, r)
    Fixed goalFar = 0;    // DAT_00648f94 = (halfLength+barHeightConst)<<16 + fx(scale<<16, r)
    Fixed crossNear = 0;  // DAT_00648f90 = (halfLength+netDepth)<<16 - fx(scale<<16, r)
    Fixed crossHigh = 0;  // DAT_00648f88 = (halfLength+netDepth)<<16 + fx(scale<<16, r)
};
BoxBand makeBoxBand(const WC98Band& band, std::int32_t scale, Fixed aspect);

// Trajectory-record fields the arbiter/machine consume (ball.cpp integrator aliases).
struct SetPieceRec {
    std::int32_t id = 0;                  // +0xb54 player tag
    int state = 0;                        // T+0xB5C contactMode
    int subState = 0;                     // T+0xB60 netCase
    Vec3 target{};                        // +0xB80 triple (netPos)
    Vec3 prevSlot{};                      // +0xB8C triple (prev-copy slot, written from tprev)
    Fixed vx = 0, vy = 0, vz = 0;         // +0xB98/0xB9C/0xBA0 tvel
    Vec3 pos{};                           // +0xBA4.. tpos
    Vec3 prev{};                          // +0xBB0.. tprev
    Fixed absX = 0, absZ = 0, absPrevX = 0, absPrevZ = 0;   // +0xBBC..0xBC8
    std::int16_t tagBE0 = -1;             // +0xBE0 (machine cases 1/2 stamp from id)
    std::int16_t tagGoalPlane = -1;       // +0xBDC (first goal-plane stamp; arbiter selector-4 gate)
};

struct ArbiterResult {
    int selector = 0;        // 0 none, 1 goal-line band, 2 touchline band, 3/4 velocity events, 8 event, 9 box band
    Fixed time = 0x10001;    // local_1c: earliest-intersection accumulator (fixed-point)
};

// FUN_004c1730 selector lane (l.91-171). `sidePlaneY` = ball block +0xD34 field (posI_d30.y);
// `velEvents` = DAT_00641011 & 2; `scale` = DAT_006469a8 (=5, set by FUN_004b1730).
ArbiterResult SetPiece_arbitrate(const SetPieceRec& rec, const WC98Band& band, const BoxBand& box,
                                 std::int32_t sidePlaneY, bool velEvents, std::int32_t scale);

struct MachineResult {
    // EBX flag bytes: +0xd byte (cases 1/2/3/4), +0xe ushort word, +0xf byte bit0.
    std::uint32_t flagsD = 0, flagsE = 0, flagsF = 0;
    int state = -1;        // T+0xB5C write (-1 = case did not write)
    int subState = -1;     // T+0xB60 netCase write
    Vec3 target{};         // +0xB80 triple after placement (cases 2/5/7/8/9/10/6 tail)
    Fixed vx = 0, vz = 0;  // velocity-slot writes (cases 3/4 only)
    bool vyZero = false;   // case 5: +0xB9C (vy slot) := 0
    bool velClamped = false;
};

// FUN_004c09a0 replica: selector guard `9 < sel-1U` returns unchanged. `xyz` = the interpolated
// triple the driver computes from the accumulator time. `clampFlags` = DAT_00646a24 (bit 0x40 =
// velocity clamp band active). The rounded (>>0x10) integer triple that the exe writes into the
// ball block (+0xccd.. [P slot alignment]) is returned in `rounded`.
struct MachineOutcome : MachineResult {
    Vec3 rounded{};      // integer triple written to the ball block
    bool reaim = false;  // cases 1/2 velocity re-aim lane executed (atan/sqrt helpers, [P])
};
MachineOutcome SetPiece_place(int selector, const SetPieceRec& rec, const WC98Band& band,
                              const BoxBand& box, std::int32_t scale, const Vec3& xyz,
                              std::uint32_t clampFlags = 0x40);

} // namespace wc98::sim
