#pragma once
// Pitch geometry (Pitch_setDimensions 0x455930) and the match options block. Units: 1/16 ft.
#include <cstdint>

namespace wc98::sim {

struct Pitch {
    std::int32_t width = 0, length = 0;        // 0x65F324 / 0x65F328
    std::int32_t penAreaWidth = 0x840;         // 0x65F32C (44 yd)
    std::int32_t penAreaDepth = 0x360;         // 0x65F330 (18 yd)
    std::int32_t penSpotDist = 0x240;          // 0x65F334 (12 yd)
    std::int32_t goalAreaWidth = 0x3c0;        // 0x65F338 (20 yd)
    std::int32_t goalAreaDepth = 0x120;        // 0x65F33C (6 yd)
    std::int32_t goalHeight = 0x98;            // 0x65F340 crossbar height used by ball collision
    std::int32_t goalWidth = 0x1c8;            // 0x65F344 goal mouth width used by ball collision
    std::int32_t f348 = 0x4c, f34c = 0x98;     // 0x65F348 / 0x65F34C (net depth / net height; strong)
    std::int32_t postThickness = 6;            // 0x65F350
    std::int32_t barThickness = 6;             // 0x65F354
    std::int32_t goalWidthVisual = 0x180;      // 0x65F358 (8 yd; read by AI/render code)
    std::int32_t f35c = 0x40;                  // 0x65F35C
    std::int32_t standLength = 0, standWidth = 0; // 0x65F360 = length+0xA20, 0x65F364 = width+0x840
    // Derived (0x65F368..):
    std::int32_t halfWidth = 0, halfLength = 0;   // 0x65F368 / 0x65F36C
    std::int32_t penAreaHalfWidth = 0;            // 0x65F370
    std::int32_t penAreaLineZ = 0;                // 0x65F374 = halfLength - 864
    std::int32_t goalAreaHalfWidth = 0;           // 0x65F378
    std::int32_t goalHalfWidth = 0;               // 0x65F37C = 228
    std::int32_t goalAreaLineZ = 0;               // 0x65F388 = halfLength - 288
    std::int32_t penSpotZ = 0;                    // 0x65F3AC = halfLength - 576
    std::int32_t goalLineZ[2] = {};               // 0x65F3E4[side] = -halfLength, +halfLength
    std::int32_t fifthWidth = 0, fifthLength = 0; // 0x65F3F8 / 0x65F3FC
    std::int32_t quarterWidth = 0, quarterLength = 0; // 0x65F400 / 0x65F404
    std::int32_t thirdWidth = 0, thirdLength = 0;     // 0x65F408 / 0x65F40C
    std::int32_t halfPenDepth = 0;                    // 0x65F410
    std::int32_t invHalfLength = 0;                   // 0x65F3F4 = 1/halfLength in 16.16
    std::int32_t standHalfWidth = 0, standHalfLength = 0; // 0x65F380 / 0x65F384
    // 0x65F3B0 + i*0xC (x, y, z), set when Match_setupOptions passes ECX != 0 (team 1 player count):
    // [side] = player entry point used by Team_initPlayers 0x451210 (Motion_init / Player_reset).
    std::int32_t entryPos[4][3] = {};
    // Spots (x, y, z): centre 0x65F38C, penalty spots 0x65F398 (z = -penSpotZ) / 0x65F3A4 (+).
    std::int32_t penSpot[2] = {};                     // z of the spot per side (0x65F3A0 / 0x65F3AC)
    std::int32_t thirdLineZ[2] = {};              // 0x65F3EC[side] = -length/6, +length/6 (0x65F3F0)

    // Pitch_setDimensions 0x455930, EAX = size index 0..5 (2 and 4/5 = default 76 x 106 yd).
    static Pitch make(int sizeIndex) {
        static constexpr std::int32_t kW[4] = {0xff0, 0xf00, 0xe40, 0xe10};
        static constexpr std::int32_t kL[4] = {0x1620, 0x1500, 0x13e0, 0x12c0};
        const int i = (sizeIndex >= 0 && sizeIndex <= 3) ? sizeIndex : 2;
        Pitch p;
        p.width = kW[i];
        p.length = kL[i];
        p.standLength = p.length + 0xa20;
        p.standWidth = p.width + 0x840;
        p.halfWidth = p.width / 2;
        p.halfLength = p.length / 2;
        p.penAreaHalfWidth = p.penAreaWidth / 2;
        p.penAreaLineZ = p.halfLength - p.penAreaDepth;
        p.goalAreaHalfWidth = p.goalAreaWidth / 2;
        p.goalHalfWidth = p.goalWidth / 2;
        p.goalAreaLineZ = p.halfLength - p.goalAreaDepth;
        p.penSpotZ = p.halfLength - p.penSpotDist;
        p.goalLineZ[0] = -p.halfLength;
        p.goalLineZ[1] = p.halfLength;
        p.thirdLineZ[0] = -(p.length / 6);
        p.thirdLineZ[1] = p.length / 6;
        p.fifthWidth = p.width / 5;
        p.fifthLength = p.length / 5;
        p.quarterWidth = p.width / 4;
        p.quarterLength = p.length / 4;
        p.thirdWidth = p.width / 3;
        p.thirdLength = p.length / 3;
        p.halfPenDepth = p.penAreaDepth / 2;
        p.invHalfLength = static_cast<std::int32_t>((std::int64_t{1} << 16) / p.halfLength);
        p.standHalfWidth = p.standWidth / 2;
        p.standHalfLength = p.standLength / 2;
        p.penSpot[0] = -p.penSpotZ;
        p.penSpot[1] = p.penSpotZ;
        // 0x455C07..0x455C5D (ECX != 0 path): halfW = width/2 (sar, sign-corrected).
        const std::int32_t hw = p.width / 2;
        p.entryPos[0][0] = p.entryPos[1][0] = hw + 0x2d0;
        p.entryPos[2][0] = p.entryPos[3][0] = hw - 0x1e0;
        p.entryPos[0][2] = -0x50;
        p.entryPos[1][2] = 0x50;
        p.entryPos[2][2] = -0x70;
        p.entryPos[3][2] = 0x70;
        // Formation_initZoneScales (0x4CF1xx, called from here) belongs to the formation module.
        return p;
    }
};

// Options_applyHalfLength 0x44E330: settings byte +0x23 index 0..6 -> real minutes per half.
inline constexpr std::int32_t kHalfLengthMinutes[7] = {4, 6, 8, 10, 20, 45, 120};

// Match option globals written by the front end / Match_setupOptions 0x4544D0.
struct Options {
    std::int32_t halfLengthMinutes = 4; // DAT_0065EEE0
    std::int32_t gameSpeed = 0;         // DAT_0065EED0 0..4 (settings +0xF)
    std::int32_t pitchSize = 2;         // DAT_0065EEE4
    std::int32_t surfaceColumn = 0;     // DAT_0065EED4 0..4 (settings +0x2D; 5 = random)
    std::int32_t surfaceRow = 0;        // DAT_0065EED8 0/1
    std::int32_t clockMode = 0;         // DAT_0065EEDC (1 = clock stops on dead ball, no attack extension)
    std::uint8_t rules = 3;             // DAT_0065EF54: 1 bookings, 2 offside (see Rules)
    std::uint8_t funModes = 0;          // DAT_0065EF55: 1 side walls, 4 big curve, 0x10 possession timer
    std::int32_t refereeStrictness = 50;// DAT_0065EF50 percent chance a foul is given / a card shown; resolved by
                                        // the front end (0x44E52F settings byte +0xF, or 50 + rand%31 at 0x44E559
                                        // with the input RNG 0x466FD0)
    std::int32_t maxSubs = 3;           // DAT_0065EEE8 substitutions allowed (front end 0x44E578: settings +0x10
                                        // 0 -> 3, 1 -> 5, 2 -> 9999 = unlimited)
    bool fatigueInjuries = true;        // DAT_0065EEF4 (Team_applyFatigue every 5 game minutes)
    std::int32_t goldenGoalTarget = 1;  // DAT_0065EEF0
    // Rules agent additions:
    std::int32_t gameMode = 0;          // DAT_0065EEC8 0..0x11: 1 training, 3 penalty shoot-out, 6 golden goal
                                        // (Match_buildPeriodTable mode derived as in 0x40A414)
    std::int32_t tieBreak = 0;          // DAT_0065EEEC 0 draw allowed, !=0 extra time/shoot-out, 4 two legs
    std::int32_t firstLegGoals[2] = {}; // 0x65F04C / 0x65F180 (team setup +0x08, two-leg aggregate)
    std::uint32_t winTarget[2][3] = {{0xffffffffu, 0xffffffffu, 0xffffffffu},
                                     {0xffffffffu, 0xffffffffu, 0xffffffffu}}; // 0x65F074/78/7C + t*0x134
                                        // (FUN_0046FD24 early-decision targets; 0x554214 sets -1 = none)
    std::int32_t teamPlayers[2] = {11, 11}; // 0x65F048 / 0x65F17C players on the pitch (clamped <= 11)
    std::int32_t presentationMode = 0;  // DAT_0065EF00 (dispatcher 0x4741A0)
    std::int32_t handicapMode = 0;      // DAT_0065EECC (1: FUN_0045564C balances team ratings)
    std::int32_t skillLevel = 0;        // DAT_0065EEF8 (EAX of FUN_0045564C)
    std::int32_t aiAssist = 0;          // DAT_0065EEFC 0..2 (Team_setControlLevels 0x45159C; added by TeamAI)
    std::uint32_t kickAssist = 0;   // DAT_00641011 (WC98 live options block): bit 0x40 boosts team side-0,
                                    // bit 0x80 side-1 kicker power by +50% in Kick_plan (WC98 FUN_00502fc0).
};

} // namespace wc98::sim
