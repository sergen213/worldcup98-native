// Derived from fifa99-native (same author), ported for World Cup 98
// Players: movement parameters (0x4BBBF8 with the runtime tables of 0x4CFF94), attribute build
// from a real FCDB player (0x478B08 + FE expansion 0x5202B0, WC98 nibble layout per
// SIM_PORT_NOTES.md section 4), motion predictor 0x4CBA60, fatigue 0x479028. Expected values are
// hand-computed from the disassembly (see player.cpp comments).
#include <iterator>
#include <memory>

#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/sim/data.hpp"
#include "wc98/sim/world.hpp"

using namespace wc98::sim;

int main() {
    auto data = std::make_unique<MatchData>();
    auto w = std::make_unique<World>();
    w->data = data.get();
    w->pitch = Pitch::make(2);
    // Synthetic seq table (test_sim_scenarios makeMd grammar): flags 0x81 ground-player anims;
    // seqResolve short-circuits on the flag, so Kick_plan's Anim_resolveId lane resolves ids
    // without a real ANIM.MO registry in this minimal world.
    data->anim.seqs.resize(0x1C4);
    for (std::size_t i = 0; i < data->anim.seqs.size(); ++i) {
        data->anim.seqs[i].id = std::uint16_t(i);
        data->anim.seqs[i].flags = 0x81;
    }
    w->opt.fatigueInjuries = true;

    // Synthetic ratings: speed A[0x3A] = 9, acceleration A[0x2F] = 10, agility 11, A[0x32] = 5.
    PlayerAttr& a = w->attrs[0];
    a.at(0x3a) = 9;
    a.at(0x2f) = 10;
    a.at(0x2a) = 11;
    a.at(0x32) = 5;
    a.at(0x3d) = 8;
    a.at(0x3e) = 0;
    a.at(0x33) = 8;
    a.stamina18 = 8;
    PlayerState& p = w->players[0];
    p.attr = 0;
    p.slot = 5;
    p.team = 0;
    Player_initMoveParams(*w, 0);
    CHECK(p.runMax == 0x6fffe);      // runtime 0x590590[9] = (7.5 - 6.75) * 5/15 + 6.75
    CHECK(p.speedCap == p.runMax);
    CHECK(p.sprintMax == 0x6fffe + 0x21caa); // + runtime 0x5905E0[10]
    CHECK(p.accel == 0x4000 && p.decel == 0x4000); // 0x590540[10]
    CHECK(p.accelRecip == 0x40000 && p.decelRecip == 0x40000);
    CHECK(p.sprintAccel == 0x10e55 && p.sprintAccelRecip == 0xf26d);
    CHECK(p.turnAgility == 2);
    CHECK(p.mB0 == 0xa800);
    CHECK(p.anim.pitchLimit == 20);
    a.at(0x40) |= 1; // injured: run speed 3.0
    Player_initMoveParams(*w, 0);
    CHECK(p.runMax == 0x30000);
    a.at(0x40) = 0;
    Player_initMoveParams(*w, 0);

    // Kinematic predictor: from rest, 30 frames at run speed: 28 frames accelerating then cruise.
    p.speed = 0;
    p.turnClass = 0;
    p.facingDesired = 0;
    CHECK(Motion_predictDistance(*w, 0, 0xc0000, 0, 30, 0, 0) == 111);
    CHECK(Motion_predictDistance(*w, 0, 0xc0000, 0, 10, 0, 0) == 12); // still accelerating
    p.speed = 0x40000;
    p.facingDesired = 0x100; // quarter turn: (0x100 >> 5) * (speed >> 18) = 8 frames lost
    CHECK(Motion_predictDistance(*w, 0, 0xc0000, 0, 30, 0, 0) == 135);
    p.speed = 0x40000;
    p.facingDesired = 0;
    CHECK(Motion_predictDistance(*w, 0, 0x40000, 0, 8, 0, 0) == 32); // constant speed 4.0

    // Fatigue: (2.0 * 5 / 45) * 1.5 = 0x5555 per tick; the 4th tick drops the speed rating.
    w->match.durationSec = 240;
    for (int i = 0; i < 3; ++i) CHECK(!PlayerAttr_fatigueTick(*w, a, true));
    CHECK(a.fatigueSpeed == 0xffff);
    CHECK(PlayerAttr_fatigueTick(*w, a, true));
    CHECK(a.at(0x3a) == 8 && a.at(0x3f) == 1 && a.fatigueSpeed == 0x5554);
    CHECK(PlayerAttr_fatigueTick(*w, a, true)); // stamina accumulator passes 1.5
    CHECK(a.stamina18 == 7 && a.at(0x3e) == 1);
    PlayerAttr b{};
    b.at(0x33) = 8;
    CHECK(!PlayerAttr_fatigueTick(*w, b, false) && b.fatigueSpeed == 0x4444); // k = 1.2

    // Attribute record from real FCDB players (record 0 and 100 of COMMON/FCDB.DBI).
    const auto dir = treeDirOrSkip();
    const auto db = wc98::formats::FcdbDatabase::parse(wc98::readFile(dir / "COMMON/FCDB.DBI"));
    data->sheets[0].squad = {db.players[0], db.players[100]};
    Team_buildMatchPlayerAttrs(*w, 0, true);
    // Hand-decoded from the raw 20-byte records (bytes 8..16 skill nibbles -> expandFcdbPlayer
    // name map -> 0x478B08 copy map -> clamp 3..19; A[0x36] = derived speed class, unclamped).
    const int exp0[] = {12, 10, 11, 9, 13, 9, 14, 11, 12, 11, 11, 10, 8, 9, 3, 10, 8, 14, 12, -1, 12, 10};
    const int exp1[] = {3, 8, 9, 6, 8, 7, 10, 10, 5, 4, 9, 5, 10, 7, 2, 11, 9, 14, 9, -1, 9, 5};
    for (int off = 0x28; off <= 0x3d; ++off) {
        if (off == 0x3b) continue;
        CHECK(w->attrs[0].at(off) == exp0[off - 0x28]);
        CHECK(w->attrs[1].at(off) == exp1[off - 0x28]);
    }
    CHECK(w->attrs[0].stamina18 == 10 && w->attrs[1].stamina18 == 5);
    CHECK(w->attrs[1].squadIndex == 1);
    // Register D-56 (pass-15): the WC98 cap-chain consumer grammar, byte-exact. Motion_rootMotionStep
    // (FUN_004f4380) cascades on [M+0xD8] & 0x1c0 BEFORE the arrival band: bit 0x40 -> cap min
    // 0x18000, bit 0x80 -> 0x40000, bit 0x100 -> 0x68000 (priority order, cand_rootstep l.30-44);
    // sprint bit 0x100000 selects sprintMax and the penalty mask is **0x6000000** subtracting mB0
    // (l.56-78). The turn-class cap from DAT_0058aee4 goes last with status bit 1 (l.100-102); the
    // EBX collapse from Motion_rootMotionPre fires on hard-turn overshoot only when the gate bit 4
    // is clear — WC98 run-state producers set reqBits(p, 0x11), so they never collapse (l.74 uses
    // bit 0x20 as the half-rate clamp flag).
    {
        PlayerState& m = w->players[1];
        m.attr = 0; m.slot = 3; m.team = 0;
        Player_initMoveParams(*w, 1);
        const Fixed dt = 1; // one sim frame per step (self-test ladder grammar)
        const auto ramp = [&](std::uint32_t req, int cls) {
            m.speed = 0; m.moveFlags = 0; m.turnClass = cls; m.targetDist = 0x400;
            m.facingDesired = 0; m.headAim = 0; m.moveReq = req;
            for (int i = 0; i < 400; ++i) Motion_rootMotionStep(*w, 1, dt, 0);
            return m.speed;
        };
        CHECK(ramp(0x40, 0) == 0x18000);   // cascade bit 0x40 cap (l.41-43)
        CHECK(ramp(0x80, 0) == 0x40000);   // cascade bit 0x80 cap (l.37-39)
        CHECK(ramp(0x100, 0) == 0x68000);  // cascade bit 0x100 -> min(cap, 0x68000) (l.33-35)
        CHECK(ramp(0x4000000, 0) == m.runMax - m.mB0); // penalty mask alone subtracts mB0 (l.56-78)
        CHECK(ramp(0x100000, 1) == 0x50000); // sprint path capped last by kSpeedCap[cls] (l.56/100)
        // Hard-turn overshoot at cruise speed: EBX=1 (collapse candidate). Gate bit 4 producers
        // (run states, reqBits(p, 0x11)) take the clamp path instead of collapsing.
        m.speed = 0x40000; m.moveFlags = 0; m.turnClass = 0;
        m.targetDist = 0x400; m.targetDx = 64; m.targetDz = -64;
        m.facingDesired = 0; m.headAim = 0; m.moveReq = 0x11;
        CHECK(Motion_rootMotionPre(*w, 1, dt) == 1);
    }
    // Register D-64/D-65 (pass-15): the WC98 gesture machine's held path fires at frames > 7 for
    // states 1/4/6/8 (pred_00512410 l.89-95,120-136: cmp 7 / jle), NOT FIFA-99's >= 12; the
    // double-tap windows keep frames >= 0xc for states 3/5 (l.104/115); state 7 has NO idle-timer
    // reset branch — its only edge is down -> ev(9, st8) (pred case \a).
    {
        ControllerState& k = w->controllers[0];
        PlayerState& g = w->players[0];
        k = ControllerState{};
        k.player = 1; k.maskDev = 0; k.gestureCtx = 0; // Cn(0) drives players[0]
        g.ctrl = 1; g.team = 0; g.slot = 9; g.curSlot = 9;
        g.anim.flags() &= ~4u; g.state = 0x300; g.stateSub = 0;
        GestureButton& gb = k.buttons[0]; // mask dev 0 button 0 (pass)
        int ref = -1;
        Input_buttonGesture(*w, 0, 1, 0x10, &ref); // press: ev(0) -> state 1, frames reset
        CHECK(gb.state == 1 && gb.frames == 0);
        CHECK(ref == 0); // c * 16 + b = 0 (fire ref format)
        Input_buttonGesture(*w, 0, 7, 0x10, &ref); // frames = 7: NOT held yet (D-64 boundary)
        CHECK(gb.state == 1);
        Input_buttonGesture(*w, 0, 1, 0x10, &ref); // frames = 8 > 7 -> ev(1) state 2 (dt=8 fires)
        CHECK(gb.state == 2 && gb.snap.event == 1);
        Input_buttonGesture(*w, 0, 1, 0, &ref);    // release: ev(2) finish -> idle
        CHECK(gb.state == 0 && gb.count == 0);
        // Double-tap windows keep the 0xc rows: press -> st1, release tap -> st3.
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);
        Input_buttonGesture(*w, 0, 1, 0, &ref); // frames <= 7 release -> ev(3) tap -> state 3
        CHECK(gb.state == 3 && gb.count == 1);
        Input_buttonGesture(*w, 0, 0xb, 0x10, &ref); // frames = 0xb: window open, down -> ev(7) st4 (D-64 keeps the 0xc row)
        CHECK(gb.state == 4 && gb.snap.event == 7);
        Input_buttonGesture(*w, 0, 6, 0x10, &ref);   // frames = 6: window open (st4)
        CHECK(gb.state == 4);
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // frames = 7: boundary — held fires at > 7 ONLY
        CHECK(gb.state == 4);
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // frames = 8 -> ev(0xa) state 9 (D-64 row for st4)
        CHECK(gb.state == 9 && gb.snap.event == 0xa);
        Input_buttonGesture(*w, 0, 1, 0, &ref);      // release: ev(0xd) finish -> idle
        CHECK(gb.state == 0 && gb.count == 0);
        // State 5/6 chain: press tap tap d -> st6; held fires at frames > 7.
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // press -> st1
        Input_buttonGesture(*w, 0, 1, 0, &ref);      // tap release -> st3 (count 1)
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // down in window (< 0xc) -> ev(7) st4
        CHECK(gb.state == 4 && gb.snap.event == 7);
        Input_buttonGesture(*w, 0, 1, 0, &ref);      // frames <= 7 release -> ev(3) tap -> st5 (count 2)
        CHECK(gb.state == 5 && gb.count == 2);
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // down in window (< 0xc) -> ev(8) st6
        CHECK(gb.state == 6);
        Input_buttonGesture(*w, 0, 6, 0x10, &ref);   // frames = 6: window open (st6)
        CHECK(gb.state == 6);
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // frames = 7: boundary — fires only at > 7
        CHECK(gb.state == 6);
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // frames = 8 -> ev(0xb) state 0xa (D-64 row for st6)
        CHECK(gb.state == 0xa && gb.snap.event == 0xb);
        Input_buttonGesture(*w, 0, 1, 0, &ref);      // release: ev(0xe) finish -> idle
        CHECK(gb.state == 0);
        // State 7 timer removal (D-65): reach st7 via the st6 release tap, then KEEP releasing —
        // old FIFA-99 code fired a timeout at frames >= 0xc; WC98 stays in state 7 forever.
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // press -> st1
        Input_buttonGesture(*w, 0, 1, 0, &ref);      // tap release -> st3
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // down in window -> ev(7) st4
        Input_buttonGesture(*w, 0, 1, 0, &ref);      // frames <= 7 release -> ev(3) tap -> st5
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // down in window -> ev(8) st6
        Input_buttonGesture(*w, 0, 1, 0, &ref);      // frames = 1 <= 7 release -> ev(6) state 7
        CHECK(gb.state == 7);
        for (int i = 0; i < 30; ++i) Input_buttonGesture(*w, 0, 1, 0, &ref);
        CHECK(gb.state == 7); // D-65: no timer reset / eviction from state 7
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // down -> ev(9) st8 (only edge, pred case \a)
        CHECK(gb.state == 8 && gb.snap.event == 9);
        Input_buttonGesture(*w, 0, 6, 0x10, &ref);   // frames = 6: window open (st8)
        CHECK(gb.state == 8);
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // frames = 7: boundary — fires only at > 7
        CHECK(gb.state == 8);
        Input_buttonGesture(*w, 0, 1, 0x10, &ref);   // frames = 8 -> ev(0xc) state 0xb (D-64 row for st8)
        CHECK(gb.state == 0xb && gb.snap.event == 0xc);
    }
    // Register D-66/D-67 (pass-15): dispatch gate byte (&DAT_0058c750)[action] == 1 AND anim bit 4
    // SUPPRESSES the action, and WC98 has no queue store: the suppressed action is dropped, never
    // re-fired from a FIFO when the animation ends (queue[4]/count/lock fields do not exist).
    {
        ControllerState& k = w->controllers[0];
        PlayerState& p = w->players[0];
        k = ControllerState{};
        k.player = 1;
        p.ctrl = 1; p.team = 0; p.slot = 9; p.curSlot = 9;
        p.anim.flags() &= ~4u;
        w->match.phase = 9;
        p.flags22c |= 0x200; // human-controlled on-ball (Player_requestKick gate)
        w->ball.possessor = 0;
        p.anim.flags() |= 4;     // animation in progress: action 42 (gate byte 1) must drop
        Input_dispatchAction(*w, 0, 42, -1);
        CHECK(w->ball.possessor == 0); // dropped, no kick executed
        p.anim.flags() &= ~4u;     // WC98: the drop is terminal — nothing queued to re-fire (D-67)
        Input_dispatchAction(*w, 0, 42, -1);
        CHECK(w->ball.possessor != 0 || w->match.phase == 9); // fires now that anim & 4 is clear
    }
    // Register D-68 (pass-15): pause/skip bit 0x2000 sub-gates (paddecode l.105-117). Outside a
    // replay the set is unconditional; phase 13 with an active replay goes through the cam matrix:
    // cam A null -> only cam B tracking this player sets; cam A non-null -> sets on NEGATION of
    // tracking. Observable = match.flags278 bit 0x2000 (Input_updateControllers clears per frame).
    {
        ControllerState& k = w->controllers[0];
        PlayerState& p = w->players[0];
        const auto pauseCall = [&](int phase, int32_t replay, int camA, int camB) {
            k = ControllerState{};
            k.player = 1; k.padBits = 0x2000; k.maskDev = 0;
            p.ctrl = 1; p.team = 0; p.slot = 9; p.curSlot = 9;
            p.anim.flags() &= ~4u; p.state = 0x300; p.stateSub = 0;
            w->match.phase = phase;
            w->match.replayWanted[1] = replay; // team 0's opponent side (teams[0].opponent)
            w->match.replayCamA = camA; w->match.replayCamB = camB;
            w->teams[0].opponent = 1; w->teams[1].opponent = 0; // replayWanted indexed by opponent side (test_sim_team grammar)
            w->match.flags278 &= ~0x2000u;
            Input_updateControllers(*w);
            return (w->match.flags278 & 0x2000u) != 0;
        };
        CHECK(pauseCall(9, 0, 0, 0));    // open play: unconditional set (l.115)
        CHECK(pauseCall(0xd, 0, 0, 0));  // phase 13 without replay: unconditional set
        CHECK(pauseCall(0xd, 1, 0, 1));  // replay, cam A null, cam B tracks player 1 -> set (l.109)
        CHECK(!pauseCall(0xd, 1, 0, 0)); // replay, no cams: matrix closed -> no set (l.108-109)
        CHECK(pauseCall(0xd, 1, 2, 0));  // cam A = ctrl 2 (tracks player 2, NOT player 1): negation arm (l.111)
        CHECK(!pauseCall(0xd, 1, 1, 2)); // cam A tracks this player: negation false -> no set (l.111)
    }
    // Register D-57 (pass-15 S3): pad-decode constants from paddecode_004a9280 l.36-99 fixture pins:
    // stick gate < 0x2001, deadband +6/-5 into bits 1/2 (+z 8/4), angle bands 0xaa / 0x155 and the
    // windows [-0x55,0x56] / [0x1aa,0x1ab]. scale = mag*0x50>>0x10 (mag 0x2001 -> scale 10; sin/cos
    // * 10 rides the deadband bits; the pins below are measured decode values).
    {
        ControllerState& k = w->controllers[1];
        k = ControllerState{};
        k.raw = 0x40;
        k.stickMag = 0x2000; // below the gate: vector + bands suppressed entirely (l.39-43)
        CHECK(Input_analogDecode(*w, 1) == 0x40);
        CHECK(k.analogX == 0 && k.analogZ == 0 && k.analogMag == 0);
        auto bandPin = [&](int angle, std::uint32_t wantBits, std::uint32_t wantAng) {
            k = ControllerState{};
            k.raw = 0x40;
            k.stickMag = 0x2001; // scale = mag*0x50>>0x10 = 10: sin/cos * 10 feeds the deadband bits
            k.stickAngle = angle & 0x3ff;
            const std::uint32_t b = Input_analogDecode(*w, 1);
            CHECK(b == wantBits); // l.65-80: |x|>=6 / x<-5 -> bits 1/2, z -> bits 8/4
            CHECK((k.raw & 0xffu) == wantAng); // l.85-99 bands over the same low byte (+0x40)
        };
        bandPin(0x000, 0x48, 0x48); // z=10 -> bit 8; inner window |sa|<=0xaa adds no new bits
        bandPin(0x0aa, 0x41, 0x49); // x=9 -> bit 1; boundary 0xaa still inner -> +bit 8
        bandPin(0x0ab, 0x41, 0x41); // outer non-deep: no bit 8; window [0x56,0x1aa] keeps bit 1
        bandPin(0x156, 0x41, 0x45); // deep outer (>0x155) -> bit 4
        bandPin(0x255, 0x44, 0x44); // sa=-0x1ab: z<-5 -> bit 4; deep band adds 4; window test fails
        bandPin(0x2aa, 0x42, 0x46); // sa=-0x156: x<-5 -> bit 2 in the word; deep band adds 4 + window keeps 2
        bandPin(0x36a, 0x4a, 0x4a); // sa=-0x96: z=6 -> 8, x<-5 -> 2; inner + window add nothing new
        k = ControllerState{};
        k.raw = 0x40;
        k.stickMag = 0x8000; // scale 0x40: angle 0 -> x=0, z=cos*0x40=64 -> deadband bit 8 (l.73-80)
        k.stickAngle = 0;
        CHECK(Input_analogDecode(*w, 1) == 0x48);
        CHECK(k.analogZ >= 6 && !(k.analogX >= 6 || k.analogX < -5));
    }
    // ---- Pass-15 S4 (D-59/D-60): WC98 FUN_004f4e70 / FUN_004f4f80 semantics pins.
    {
        // D-59: store is **unconditional** - the FIFA-99 gate on flag bits 8/1 + equality skipped it.
        p.posI = {100, 0, -200};
        p.moveFlags = 1 | 8; // alt-active + legacy "new target" bit: exe stores anyway
        Motion_setTarget(*w, 0, {50, 0, -260});
        CHECK(p.target.x == 50 && p.target.z == -260);
        CHECK(p.targetRef == 0); // [M+0x4C] = M + &0x34 (main block)
        CHECK(p.moveFlags == 8); // byte [WC98 M+0x5E] &= 0xfe: clears port bit 0 only, keeps the rest
        CHECK(p.targetDx == -50 && p.targetDz == -60);
        CHECK(p.targetDist == octLength(-50, -60)); // (a+b)-(min>>1) per l.24-31

        // D-60: pos store + frac reset + ball refresh x2 + distBallPrev copy; NO target reset,
        // NO facing/lookAt handling, NO speed/m98/m9c zeroing (FIFA-99 extras absent in WC98).
        w->ball.posI = {300, 0, 100};
        p.facing = 0x123; p.turnAmount = 7; p.turnClass = 2; p.dir8 = 3;
        p.speed = 0x4000; p.m98 = 5; p.m9c = 6;
        const std::int32_t keepDist = p.targetDist;
        p.targetRef = 1; // warpTo must not touch target/targetRef (exe calls no setTarget)
        const Vec3 warp{40, 0, -60};
        Motion_warpTo(*w, 0, &warp);
        CHECK(p.posI.x == 40 && p.posI.z == -60);
        CHECK(p.frac.x == 0 && p.frac.z == 0); // [M+0x28]/[M+0x30] zeroed
        CHECK(p.pos.x == Fixed(std::uint32_t(40) << 16));   // [+4] = x<<16
        CHECK(p.pos.z == Fixed(static_cast<std::uint32_t>(-60) << 16)); // [+0xC] = z<<16
        const OctDist d = octDistance(p.posI, w->ball.posI);
        CHECK(p.dxBall == d.dx && p.dzBall == d.dz && p.distBall == d.dist);
        CHECK(p.angleBall == atan2i(d.dx, d.dz)); // quadrant atan helper FUN_004f3db4 -> angleBall
        CHECK(p.distBallPrev == p.distBall);      // [WC98 M+0x70] -> [M+0x80] copy
        CHECK(p.facing == 0x123 && p.turnAmount == 7 && p.turnClass == 2 && p.dir8 == 3);
        CHECK(p.speed == 0x4000 && p.m98 == 5 && p.m9c == 6);
        CHECK(p.targetRef == 1 && p.targetDist == keepDist); // stale deltas retained (exe grammar)
    }
    return 0;
}
