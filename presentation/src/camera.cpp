// WC98 provenance (pass-16cam / pass-17cam, register D-54 CLOSED) for this file's exe cites:
//   - FOV/task-height rows: **WC98-proven** at DAT_0057FB82 (`cam_wc98_tables.inc`, gate
//     `test_cam_tables`). The FIFA-99 cite `FOV i16 0x57FAD8` is REFUTED for WC98 — that VA holds an
//     ascending u16 pair table ({0005,0004,0007,0006,..}), not FOV halfwords.
//   - camera-record defaults + mode-broadcast constants: **WC98-proven** code immediates of the init
//     FUN_004a0810 (FOV default `cam+0x78 = 0xe28`) and accessor FUN_004a0790 (`analysis/pass16cam/`).
//   - per-mode **parameter rows**: WC98 preset records (stride 0x2c, DAT_00586xxx band) selected by
//     the mode word DAT_00580df0 in selector FUN_004ca880 — grammar + live-patch lanes decoded in
//     `analysis/pass17cam/NOTES.md`; every value below is byte-exact from FIFAWC.EXE and gated by
//     `test_cam_tables` (port table == image bytes). The FIFA-99 zone-table row signatures
//     ({60900,61050,..}, {-2000,-750,750,2000}, {12250,..}, height triples {0,800,-3000}) occur ZERO
//     times in the WC98 image (`analysis/pass16cam/absent_scan.txt`): those FIFA-99 `kModes` rows are
//     deleted; the camera model here is the preset-record model.
//   - Gen3 family confirmation: docs/reverse-engineering/GHIDRA_PASS5_IG.md (sim clock l.125).
// Resolved (pass-15): the orbit trig lane uses the WC98 fixed-point table sine (`sine16x16` /
// FUN_00497740 over DAT_00585898, byte-exact + asserted) with the engine's fixed-multiply rounding —
// the old libm path drifted by one unit of the 16-bit angle. `atan2_16` keeps a libm approximation
// (the WC98 accessor site is not located yet, register D-53).
#include "wc98/presentation/camera.hpp"

#include "cam_wc98_tables.inc"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <numbers>

namespace wc98::presentation {

namespace {

constexpr double kTurn = 65536.0;

int fxTrig(int len, int trig16) {
    const std::int64_t v = std::int64_t(len) * trig16;
    return int((v >> 16) + ((v & 0x8000) ? 1 : 0));
}
int sin16x(int len, int a16) { return fxTrig(len, sine16x16(a16)); }
int cos16x(int len, int a16) { return fxTrig(len, cosine16x16(a16)); }
int hdist(int dx, int dz) { return int(std::sqrt(double(dx) * dx + double(dz) * dz)); }
int wrap16(int a) { return a & 0xFFFF; }
int signed16(int a) { return std::int16_t(std::uint16_t(a)); }

struct Angles {
    int yaw, pitch;
};
// Cam_anglesTo 0x4C2F04: d = eye - target.
Angles anglesTo(glm::ivec3 eye, glm::ivec3 t) {
    glm::ivec3 d = eye - t;
    int hd = hdist(d.x, d.z);
    return {wrap16(0x18000 - atan2_16(d.x, d.z)), wrap16(0x10000 - atan2_16(d.y, hd))};
}

glm::ivec3 toInt(glm::vec3 v) { return {int(v.x), int(v.y), int(v.z)}; }

// --- WC98 preset-record grammar (selector FUN_004ca880; pass-17cam NOTES.md) --------------------
// The live camera parameters of one mode are a stride-0x2c static record picked by the selector on
// the mode word DAT_00580df0 {1,2,3,5} + sign pairs of the ball position (DAT_00644858 x,
// DAT_00644860 z). Eye trio = camera-record fields cam+0x108 / cam+0x10c / cam+0x110 fed from record
// bytes +0xa/+0xc/+0xe (FUN_004caa70 direct lane, FUN_004a0f40 orbit lane). The byte-exact record
// table is `kPresets` in camera.hpp (+ case-5 patch constants); gated field-for-field by test_cam_tables.

// Selector FUN_004ca880 preset pick: DAT_00580df0 mode + sign pairs (ball x/z) + sub-mode-3 gate
// (record field rec+0x10 == 3, written by the scene setup). Returns kPresets slot or -1 for modes
// without a static record (selector default lane: Tele/Stadium/Ball/Tower/Replay keep the task lane).
int presetSlot(CameraMode m, bool xNeg, bool zNeg) {
    switch (m) {
        case CameraMode::Sideline: return xNeg ? 0 : 1;
        case CameraMode::Action:   return 2 + (xNeg ? (zNeg ? 0 : 1) : (zNeg ? 2 : 3));
        case CameraMode::End:      return zNeg ? 6 : 7; // live-match scene gate rec+0x10 == 3 [P]
        case CameraMode::Shoulder: return 10;
        default:                   return -1;
    }
}

// DAT_00580df0 mode space per port enum (WC98-proven: selector FUN_004ca880 dispatches the word via
// mov eax,[df0]/dec/cmp-4 at 0x4ca88f; gate test_cam_tables pins those dispatch bytes):
int exeModeOf(CameraMode m) {
    switch (m) {
        case CameraMode::Tele:     return 0;
        case CameraMode::Sideline: return 1;
        case CameraMode::Action:   return 2;
        case CameraMode::End:      return 3;
        case CameraMode::Stadium:  return 4;
        case CameraMode::Shoulder: return 5;
        default:                   return -1; // no exe mode (FOV lane falls to the grid default)
    }
}

constexpr int kReplayFov = 0xE28; // WC98-proven: caminit FUN_004a0810 writes 0xe28 to cam+0x78 (FOV default,
                                  // the target of FOV-table sentinel -5); gate test_cam_tables.

double rad(int a16) { return double(a16) * 2.0 * std::numbers::pi / kTurn; }
// Exe polar-offset idiom (`dist * trig(x)`, OrbitCam_update 0x48D18C / BallCam_update 0x4E90B4):
// fixed-point product of the distance with FUN_00497740/4977b0 table sine/cosine (quadrant-folded
// DAT_00585898, slope term `* 0x6487e >> 9`), rounded by the engine's fixed-multiply pattern
// `(prod >> 16) + bit-15 carry` (FUN_00517f20). Replaces the libm path that drifted in the last ulp
// of the 16-bit angle; `sine16x16/cosine16x16` are byte-exact against the image table (pass-12cam,
// asserted in test_presentation), so orbit offsets are now exact by construction.

} // namespace

int atan2_16(int y, int x) {
    if (x == 0 && y == 0) return 0;
    int a = int(std::lround(std::atan2(double(y), double(x)) * kTurn / (2.0 * std::numbers::pi)));
    return a == 0x8000 ? 0x8000 : signed16(a); // +-pi -> 0x8000 like the exe's octant code
}

int follow16(int cur, int target, int div, int& vel, int frames) {
    int diff = signed16(target - cur);
    vel = (diff / div + vel) / 2; // C division truncates toward zero, as Watcom's idiv
    int step = vel * frames;
    if ((diff >= 0 && step > diff) || (diff < 0 && step < diff)) step = diff;
    return wrap16(cur + step);
}

int clampYaw16(int a, int A, int B) {
    if (A < B) return a < 0x8000 ? std::min(a, A) : std::max(a, B); // window wraps through 0
    if (a > A) a = A;
    if (a < B) a = B;
    return a;
}

int clampPitch16(int a, int A, int B) {
    if (A < B) return a < 0x8000 ? std::min(a, A) : std::max(a, B);
    if (a < 0x8000) return A;
    if (a > A) a = A;
    if (a < B) a = B;
    return a;
}

glm::vec3 cameraForward(int yaw16, int pitch16) {
    double y = rad(yaw16), p = rad(pitch16);
    return {float(-std::sin(y) * std::cos(p)), float(std::sin(p)), float(std::cos(y) * std::cos(p))};
}

float verticalFovDeg4x3(int fovHalf16) {
    return float(2.0 * std::atan(std::tan(rad(fovHalf16)) * 3.0 / 4.0) * 180.0 / std::numbers::pi);
}

// FOV target of a mode: selector FUN_004ca880 entry calls the accessor FUN_004a0790 (FOV grid row
// 13, word[14] = sentinel -1) and stores the result in rec+0x14. The grid expands per exe mode:
// 1 -> 0x1280; 2 -> ball x >= 0 ? 0x1280 : 0x440 (DAT_00644858 > -1); 3 -> scene sub-mode-3 gate
// ? 0x1980 : 0x1100; modes without a table entry -> 0x1540. Sign/sub-mode sampled at mode set.
int MatchCamera::fovTargetFor(CameraMode m, bool ballXPos) const {
    if (m == CameraMode::Replay) return kReplayFov;
    const int exeM = exeModeOf(m);
    return tabATaskHeight(13, exeM < 0 ? -1 : exeM, ballXPos, m == CameraMode::End,
                          task_.playerHeight(), task_.camHeight());
}

void MatchCamera::setMode(CameraMode m) {
    mode_ = m;
    reset_ = true;
    int target = fovTargetFor(m, smoothBall_.x >= 0);
    if (started_ && target != fov_) fovFrom_ = fov_, fovTo_ = target, fovGlide_ = 0; // Cam_startFovGlide 0x4E9CEC
    else fov_ = fovFrom_ = fovTo_ = target;
}

void MatchCamera::tick(const MatchSnapshot& s, int frames) {
    constexpr float kTick = 1.0f / 30.0f;
    if (!started_) setMode(mode_), started_ = true;
    // FOV smoothstep glide over 1.5 s (Hermite with zero tangents, Cam_fovGlideStep 0x4E9E8C).
    if (fovGlide_ < 1.5f) {
        fovGlide_ = std::min(1.5f, fovGlide_ + kTick * float(frames) / 2.0f);
        float u = fovGlide_ / 1.5f;
        fov_ = fovFrom_ + int(float(fovTo_ - fovFrom_) * (3 * u * u - 2 * u * u * u));
    }
    const glm::ivec3 ball = toInt(s.ball.pos);
    // WC98 deviation: prefer the live sim pitch dims; fallback when pitchUnits is unset.
    const glm::vec2 dims = (s.pitchUnits.x > 0.0f && s.pitchUnits.y > 0.0f) ? s.pitchUnits : pitchDimensions(s.pitchSize);
    const int halfW = int(dims.x) / 2, halfL = int(dims.y) / 2;
    const PlayerView* holder = s.possessor >= 0 && s.possessor < int(s.players.size()) ? &s.players[std::size_t(s.possessor)] : nullptr;
    const int holderDir = holder ? s.attackDir[holder->team & 1] : 0;

    // Smoothed ball (CamPos_smoothBall 0x544574): snap on reset, else half-average per tick.
    smoothBall_ = reset_ ? ball : (smoothBall_ + ball) / 2;

    if (mode_ == CameraMode::Replay) { // OrbitCam_update 0x48D18C
        glm::ivec3 b{std::clamp(ball.x, -(halfW - 128), halfW - 128), ball.y, std::clamp(ball.z, -(halfL - 128), halfL - 128)};
        glm::ivec3 off{sin16x(s.camera.orbitDist, s.camera.orbitYaw), 0, -cos16x(s.camera.orbitDist, s.camera.orbitYaw)};
        int up = sin16x(s.camera.orbitDist, s.camera.orbitElev);
        float fit = 1.0f;
        if (std::abs(b.x + off.x) > halfW + 320 && off.x) fit = std::min(fit, float(halfW + 320 - std::abs(b.x)) / float(std::abs(off.x)));
        if (std::abs(b.z + off.z) > halfL + 320 && off.z) fit = std::min(fit, float(halfL + 320 - std::abs(b.z)) / float(std::abs(off.z)));
        fit = std::max(fit, 0.0f);
        off = glm::ivec3(glm::vec3(off) * fit);
        eye_ = {b.x + off.x, std::max(160, b.y + int(float(up) * fit)), b.z + off.z};
        yaw_ = wrap16(s.camera.orbitYaw);
        pitch_ = wrap16(atan2_16(b.y - eye_.y, hdist(off.x, off.z)));
        reset_ = false;
        return;
    }

    if (mode_ == CameraMode::Ball) { // BallCam_update 0x4E90B4
        glm::ivec3 b{std::clamp(smoothBall_.x, -halfW, halfW), smoothBall_.y, std::clamp(smoothBall_.z, -halfL, halfL)};
        if (ballSide_ < 0 || reset_) ballSide_ = s.attackDir[0] > 0 ? 0 : 1;
        if (holderDir) ballSide_ = holderDir > 0 ? 0 : 1;
        // ponytail: the 2.5 s / 4 s confirmation timers and the 90 deg/s orbit swing (0x4E9FCC/0x4EA0B4)
        // are not reproduced; the camera switches sides immediately when possession changes ends.
        glm::ivec3 g{0, 240, ballSide_ == 0 ? 3200 : -3200};
        int a = atan2_16(b.z - g.z, b.x - g.x);
        eye_ = {b.x + cos16x(960, a), 240, b.z + sin16x(960, a)};
        yaw_ = wrap16(atan2_16(eye_.z - g.z, eye_.x - g.x) + 0x4000);
        pitch_ = 0xFA00;
        reset_ = false;
        return;
    }

    // --- position: WC98 preset-record lane (selector FUN_004ca880, records DAT_00586xxx) ---------
    const bool gliding = task_.dollyActive(); // WC98 timer C dolly owns the position lane (section 7 delta 1)
    const int slot = presetSlot(mode_, smoothBall_.x < 0, smoothBall_.z < 0);
    const CamPreset& p = kPresets[slot >= 0 ? std::size_t(slot) : 0];
    // Mode smoothing divisor [P]: the exe lerp duration lives in runtime task records (no static
    // byte); derive it from the record's smoothing word (+0x26), which scales with cam speed.
    const int div = slot >= 0 ? std::max(4, int(p.speed) >> 8) : 16;

    if (!gliding && !reset_ && mode_ == CameraMode::Action) {
        // Mode-2 FOV sign lane DAT_00644858 is live: resample the grid when the ball crosses x = 0.
        const int target = fovTargetFor(mode_, smoothBall_.x >= 0);
        if (target != fovTo_) fovFrom_ = fov_, fovTo_ = target, fovGlide_ = 0;
    }

    glm::ivec3 tgt{};
    int yawT = yaw_, pitchT = pitch_;
    if (gliding) {
        task_.advanceDolly(frames); // FUN_00517be0: advance live counter, snap A = B on completion
        eye_ = task_.eye();         // snapshot A + signed fixed-point products (FUN_00517f20)
        velX_ = velZ_ = 0; // dolly handoff: no residual follow velocity when the task releases
    } else {
        if (slot < 0) {
            // No static record in WC98 for these modes (the selector default lane keeps the previous
            // preset; the task-handler lane drives cut-scenes). Live fallback [P]: high central
            // broadcast orbit at the byte-proven h12 preset height 0x7b8, polar offset scaled by the
            // caminit rec+0x8c default kPolarSpeed (960 units); yaw/pitch auto-atan toward the ball.
            tgt = {p.cx + fxTrig(smoothBall_.z, CameraTask::kPolarSpeed), cameraPresetH12(0).height,
                   p.cz - fxTrig(smoothBall_.x, CameraTask::kPolarSpeed)};
            Angles ang = anglesTo(tgt, smoothBall_);
            yawT = ang.yaw;
            pitchT = ang.pitch;
        } else if (mode_ == CameraMode::Shoulder) {
            // Case-5 live patches: eye x/z offsets proportional to the ball coords with multiplier
            // DAT_00586244, rounded and negated by the selector; height 0x180; pitch target 63000.
            tgt = {-fxTrig(smoothBall_.x, kCase5Mul), kCase5Height, -fxTrig(smoothBall_.z, kCase5Mul)};
            Angles a = anglesTo(tgt, smoothBall_); // FUN_004ca7b0 case-5 prelude: atan(ball-eye)+0x4000
            yawT = wrap16(a.yaw + 0x4000);
            pitchT = signed16(kCase5Pitch);        // selector-patched rec+0x20 (record word 0xf618)
        } else if (mode_ == CameraMode::End) {
            // Goal camera: eye.x follows the ball (selector writes rec+0xa = DAT_00649330 = ball x);
            // z lane = ±(goal-line distance + 0x210) mirrored by the ball-z sign (DAT_00644860).
            const int src = std::abs(smoothBall_.z); // DAT_0064477c goal-distance lane [P mapping]
            tgt = {smoothBall_.x, p.height, smoothBall_.z < 0 ? -(src + 0x210) : src + 0x210};
            yawT = wrap16(p.yawRaw);                 // direct yaw record word (+0x1c: 0 / 0x7fa6 side)
            pitchT = anglesTo(tgt, smoothBall_).pitch;
        } else {
            // Polar orbit lane (FUN_004caa70): ball rotated by the base angle scaled to the record
            // radius around the (+0x18,+0x1a) center pair. Base angle FUN_004ca7b0: mode 1 =
            // (-player facing & 0x3ff) << 6 (DAT_00644848 lane); mode 2 default = quarter turn 0x4000.
            const int a = mode_ == CameraMode::Action ? 0x4000
                          : signed16((-(holder && holder->role != Role::Goalkeeper
                                             ? wrap16(int(std::lround(holder->facing * kTurn / (2.0 * std::numbers::pi))))
                                             : 0) & 0x3ff) << 6);
            const int rx = cos16x(smoothBall_.x, a) + sin16x(smoothBall_.z, a); // ball rotated by base angle
            const int rz = -sin16x(smoothBall_.x, a) + cos16x(smoothBall_.z, a);
            tgt = {p.cx + fxTrig(rx, p.radius), p.height, p.cz + fxTrig(rz, p.radius)};
            Angles ang = anglesTo(tgt, smoothBall_); // flag &4 auto-pitch/atan look lane
            yawT = ang.yaw;
            pitchT = ang.pitch;
        }

        if (reset_) {
            eye_ = tgt;
            velX_ = velZ_ = 0;
        } else {
            const int qx = (tgt.x - eye_.x) / div * frames;
            const int qy = (tgt.y - eye_.y) / div * frames;
            const int qz = (tgt.z - eye_.z) / div * frames;
            velX_ = (velX_ + qx) / 2;
            velZ_ = (velZ_ + qz) / 2;
            eye_.x += velX_;
            eye_.z += velZ_;
            if (qy) eye_.y += qy * frames; // [P] height lane: same fixed divisor, no velocity state
            else eye_.y += std::clamp(tgt.y - eye_.y, -frames, frames);
        }
    }

    // --- angles: direct-pitch record lanes clamp to their byte-derived targets, rest auto-atan ----
    if (reset_) {
        yaw_ = wrap16(yawT), pitch_ = wrap16(pitchT), velYaw_ = velPitch_ = 0;
    } else {
        pitch_ = follow16(pitch_, pitchT, div, velPitch_, frames);
        yaw_ = follow16(yaw_, yawT, slot == 7 ? 32 : div, velYaw_, frames); // End yawRaw lane: slow yaw [P]
    }
    reset_ = false;
}

render::Camera MatchCamera::camera() const {
    render::Camera c;
    c.eye = glm::vec3(eye_);
    c.target = c.eye + cameraForward(yaw_, pitch_) * 1000.0f;
    c.verticalFovDeg = verticalFovDeg4x3(fov_);
    // The exe projects with near 1 / far 60000 render units (0.25 / 15000 game units, 0x47FBA0) into a
    // linear z buffer; a perspective depth buffer needs a larger near plane for precision.
    c.nearZ = 8.0f;
    c.farZ = 15000.0f;
    c.leftHanded = true;
    return c;
}

// --- WC98 task-handler camera model (TASK_HANDLERS_WC98.md sections 1/4/5, section 7 deltas 1-2) --
// Byte-proven static tables read from the FIFAWC.EXE image with the DGROUP mapping raw = VA - 0x55a000
// + 0x158600 (pass-12cam dump; decompiles ghidra-scripts/GhidraWc98CamDolly.py).
namespace {

// DAT_00585898: quarter-wave sine, u32[256] with T[i] = sin(2*pi*i/1024)*65536 (pattern verified);
// the 257th word VA 0x585898+0x400 = 0x10000 is the adjacent entry the quadrant fold indexes into.
constexpr std::uint32_t kSineTable[257] = {
    0x0000,0x0192,0x0324,0x04B6,0x0648,0x07DA,0x096C,0x0AFE,
    0x0C8F,0x0E21,0x0FB2,0x1144,0x12D5,0x1466,0x15F6,0x1787,
    0x1917,0x1AA7,0x1C37,0x1DC7,0x1F56,0x20E5,0x2273,0x2402,
    0x2590,0x271D,0x28AA,0x2A37,0x2BC4,0x2D50,0x2EDB,0x3066,
    0x31F1,0x337B,0x3505,0x368E,0x3817,0x399F,0x3B26,0x3CAD,
    0x3E33,0x3FB9,0x413E,0x42C3,0x4447,0x45CA,0x474D,0x48CE,
    0x4A50,0x4BD0,0x4D50,0x4ECF,0x504D,0x51CA,0x5347,0x54C3,
    0x563E,0x57B8,0x5931,0x5AAA,0x5C22,0x5D98,0x5F0E,0x6083,
    0x61F7,0x636A,0x64DC,0x664D,0x67BD,0x692D,0x6A9B,0x6C08,
    0x6D74,0x6EDF,0x7049,0x71B1,0x7319,0x7480,0x75E5,0x774A,
    0x78AD,0x7A0F,0x7B70,0x7CD0,0x7E2E,0x7F8B,0x80E7,0x8242,
    0x839C,0x84F4,0x864B,0x87A1,0x88F5,0x8A48,0x8B9A,0x8CEA,
    0x8E39,0x8F87,0x90D3,0x921E,0x9368,0x94B0,0x95F6,0x973C,
    0x987F,0x99C1,0x9B02,0x9C42,0x9D7F,0x9EBC,0x9FF6,0xA12F,
    0xA267,0xA39D,0xA4D2,0xA605,0xA736,0xA866,0xA994,0xAAC0,
    0xABEB,0xAD14,0xAE3B,0xAF61,0xB085,0xB1A8,0xB2C8,0xB3E7,
    0xB504,0xB620,0xB73A,0xB852,0xB968,0xBA7C,0xBB8F,0xBCA0,
    0xBDAE,0xBEBC,0xBFC7,0xC0D0,0xC1D8,0xC2DE,0xC3E1,0xC4E3,
    0xC5E3,0xC6E2,0xC7DE,0xC8D8,0xC9D1,0xCAC7,0xCBBB,0xCCAE,
    0xCD9F,0xCE8D,0xCF7A,0xD064,0xD14D,0xD233,0xD318,0xD3FA,
    0xD4DB,0xD5B9,0xD695,0xD770,0xD848,0xD91E,0xD9F2,0xDAC4,
    0xDB94,0xDC61,0xDD2D,0xDDF6,0xDEBE,0xDF83,0xE046,0xE106,
    0xE1C5,0xE282,0xE33C,0xE3F4,0xE4AA,0xE55E,0xE60F,0xE6BE,
    0xE76B,0xE816,0xE8BF,0xE965,0xEA09,0xEAAB,0xEB4B,0xEBE8,
    0xEC83,0xED1C,0xEDB2,0xEE46,0xEED8,0xEF68,0xEFF5,0xF080,
    0xF109,0xF18F,0xF213,0xF294,0xF314,0xF391,0xF40B,0xF484,
    0xF4FA,0xF56D,0xF5DE,0xF64D,0xF6BA,0xF724,0xF78B,0xF7F1,
    0xF853,0xF8B4,0xF912,0xF96E,0xF9C7,0xFA1E,0xFA72,0xFAC5,
    0xFB14,0xFB61,0xFBAC,0xFBF5,0xFC3B,0xFC7E,0xFCBF,0xFCFE,
    0xFD3A,0xFD74,0xFDAB,0xFDE0,0xFE13,0xFE43,0xFE70,0xFE9B,
    0xFEC4,0xFEEA,0xFF0E,0xFF2F,0xFF4E,0xFF6A,0xFF84,0xFF9C,
    0xFFB1,0xFFC3,0xFFD3,0xFFE1,0xFFEC,0xFFF4,0xFFFB,0xFFFE,
    0x10000}; // T[256] = sin(quarter turn) * 65536
static_assert(kSineTable[1] == 0x192 && kSineTable[8] == 0x0C8F && kSineTable[255] == 0xFFFE);

// DAT_0057FB82 (WC98-proven FOV/task-height table) lives in `cam_wc98_tables.inc` — byte-exact dump,
// gate `test_cam_tables`. Sentinels -5 -> rec+0x78 (FOV default 0xe28), -4 -> rec+0x114, -1 -> the
// mode constants in tabATaskHeight; accessor FUN_004a0790 reads `>>16` of dword[base + [cam+8]*2],
// i.e. word[idx+1].

// h12 fixed preset rows (DAT_00585FEA/EC/FEE/FF0; stride 10 bytes, {angle,speed,height,yawOff}):
constexpr CameraPresetRow kH12Presets[2] = {{0x244c, 0xf000, 0x7b8, 0x30},   // row 0 (DAT_00649338 <= 0)
                                            {0x5bab, 0xf000, 0x7b8, 0x30}};  // row 1 (> 0)
// h14 per-slot preset rows (DAT_00586046/48/4A/4C; stride 10 bytes; slot = task record rec+0x60):
constexpr CameraPresetRow kH14Presets[4] = {{0x0,    0xf000, 0x7b8, 0x30},   // slot 0: away base (yaw atan + 0x4000)
                                            {0x7fa6, 0xf000, 0x7b8, 0x30},   // slot 1: home base (yaw atan pair)
                                            {0x2000, 0xf000, 0x7b8, 0x30},   // slot 2
                                            {0x4000, 0xf000, 0x7b8, 0x30}};  // slot 3
// h28 singleton preset (DAT_005862D2/2D4/2D6/2D8 = {0x445c, 0xf3e8, 0x08fc, 0x0030}):
constexpr CameraPresetRow kH28Preset = {0x445c, 0xf3e8, 0x8fc, 0x30};

// Exe fixed-product rounding: (live * step) >> 16 with bit-15 carry (FUN_00517f20 pattern).
std::int32_t fixedMulRound(std::uint32_t live, std::uint32_t step) {
    const std::uint64_t v = static_cast<std::uint64_t>(live) * step;
    return static_cast<std::int32_t>((v >> 16) + ((v & 0x8000u) ? 1u : 0u));
}
// Exe fixed-point division pattern (FUN_00517b50/e30): q*0x10000 + (r<<16)/d == num<<16/d.
std::uint32_t fixedDiv(std::uint32_t num, int d) { return static_cast<std::uint32_t>((static_cast<std::uint64_t>(num) << 16) / unsigned(d)); }

} // namespace

int sineWc98(int i) { return int(kSineTable[i < 0 || i > 256 ? 0 : std::size_t(i)]); } // [port] bounds-guard for the gate

// FUN_00497740 lane: quadrant-folded table sin with the exe's slope term
// ((cosFold >> 2) * ((frac * 0x6487e) >> 9)) >> 21 (pass-12cam decompile, verified numerically).
int sine16x16(int a) {
    const unsigned idx = (unsigned(a) >> 6) & 0x3FF; // quadrant index, one turn = 65536 angle units
    const int q = int(idx >> 8), f = int(idx & 0xff), frac = a & 0x3f;
    const int tS = (q & 1) ? int(kSineTable[std::size_t(256 - f)]) : int(kSineTable[std::size_t(f)]);
    const int sS = q >= 2 ? -tS : tS; // sine sign per quadrant
    const int e = (q + 1) & 3;        // slope component: folded cosine = sine at quadrant + 1
    const int tC = (e & 1) ? int(kSineTable[std::size_t(256 - f)]) : int(kSineTable[std::size_t(f)]);
    const int sC = e >= 2 ? -tC : tC;
    const int w = (frac * 0x6487e) >> 9;
    return sS + (((sC >> 2) * w) >> 21);
}

int cosine16x16(int a) { return sine16x16(a + 0x4000); } // quarter-turn shift (FUN_004977b0 lane)

int cameraPresetRowCount() { return 2; } // h12 rows; h14 has 4 (see cameraPresetH14)

CameraPresetRow cameraPresetH12(int row) {
    if (row < 0 || row >= int(std::size(kH12Presets))) return kH12Presets[0]; // [port] clamp: h12 gates on DAT_00649338>0
    return kH12Presets[std::size_t(row)];
}

CameraPresetRow cameraPresetH14(int slot) {
    if (slot < 0 || slot >= int(std::size(kH14Presets))) return kH14Presets[0]; // [port] clamp rec+0x60 range
    return kH14Presets[std::size_t(slot)];
}

CameraPresetRow cameraPresetH28() { return kH28Preset; }

// h23 FUN_004cafe0 quality-tier scene camera [P]. Tier deltas from the quality word
// DAT_00644778 (producer = task pool init FUN_004a0810 argument; per-scene launch parameter):
// <=1 {0x71,0x3d}, 2 {0x65,0x31}, 3 {0x59,0x25}; >3 falls back to the low lane (exe branch shape).
TierCamEye tierCamera(int quality, int flags, int xEdge, int zEdge, int ballZ, int blockZ, int playerZ) {
    int dx = 0x71, dz = 0x3d; // LAB_004cb003 default lane (quality <= 1 and > 3 fall-through [P])
    if (quality > 1) {
        if (quality == 2) { dx = 0x65; dz = 0x31; }
        else if (quality == 3) { dx = 0x59; dz = 0x25; }
    }
    // z source: DAT_00644830 default; bit4 clear -> block DAT_00644860, and with bit5 set -> player
    // pair DAT_00644820 (in-condition assignment order per decompile [P]).
    int z = ballZ;
    if ((flags & 0x10) == 0) {
        z = blockZ;
        if (flags & 0x20) z = playerZ;
    }
    // x = DAT_00644780 + dx*0x10 clamped to [-0x140-xEdge, xEdge+0x760]; z clamped to
    // [0x230-zEdge, zEdge-0x2a0] (symmetric pitch-edge lanes [P]).
    int x = xEdge + dx * 0x10;
    if (x > xEdge + 0x760) x = xEdge + 0x760;
    if (x < -0x140 - xEdge) x = -0x140 - xEdge;
    if (z > zEdge - 0x2a0) z = zEdge - 0x2a0;
    if (z < 0x230 - zEdge) z = 0x230 - zEdge;
    return {x, z, dz << 4, 0x640}; // dz<<4 atan/commit lane + rec+0x10c bytes {0x40,6} = 0x640 [P]
}

// h21 FUN_004ca690 ball-follow TV camera [P]. Source triple select (task flags byte[1] bit2 ->
// carrier, byte0 bit4 -> player pair, else ball), then bisection clamp to +/- edge insets.
FollowCamEye followCamera(int flags, int ballX, int ballZ, int pairX, int pairZ, int carrierX,
                          int carrierZ, int halfW, int halfL) {
    int sx = ballX, sz = ballZ; // DAT_00644818 default triple [P]
    if (flags & 0x100)      { sx = carrierX; sz = carrierZ; } // byte[1] bit2 -> DAT_00644870 pair
    else if (flags & 0x10)  { sx = pairX;   sz = pairZ;   }   // byte0 bit4  -> DAT_00644828 pair
    const int xe = halfW - 0x1e0, ze = halfL - 0x140;          // inset edges (DAT_00644780/7c [P])
    if (sx > xe) sx = xe; else if (sx < -xe) sx = -xe;         // bisection clamp idiom [P]
    if (sz > ze) sz = ze; else if (sz < -ze) sz = -ze;
    return {sx, sz, 0xeb58}; // rec+0x104 bytes {0x58, 0xeb} angle/speed constant [P]
}

// h18 FUN_004cb600 pitch-edge side camera [P]: z lane by ball x (DAT_006447c4) thresholds
// 0x1e0 / -0x1df, rec+0x110 scene constant in the mid band; mismatched selection forces the
// near lane (-zEdge-0x140). polarHeight = DAT_00644794 - 0x10 (rec+0xc polar-offset lane).
SideCamEye sideCamera(int ballX, int sceneRec110, int zEdge, int pitchConst) {
    const int far = zEdge + 0x140, near = -zEdge - 0x140; // DAT_7c+0x140 / 0xfffffec0-DAT_7c [P]
    int z;
    if (ballX >= 0x1e0)      z = far;        // ball past +x edge -> far (+z) side camera
    else if (ballX < -0x1df) z = near;       // ball past -x edge -> near (-z) side camera
    else                     z = sceneRec110;
    if (z != far && z != near) z = near;     // bisection guard [P]
    return {z, pitchConst - 0x10};           // DAT_00644794-0x10 polar height lane [P]
}

// h20 FUN_004c9fd0 overhead camera [P]: polarDist rec+0x8 = -DAT_00644780 (negative x-edge),
// angle/speed rec+0xc bytes {0x90,0} = 0x90; atan pair from FUN_00518060 is caller-side.
OverheadCamPolar overheadCamera(int xEdge) { return {-xEdge, 0x90}; }

int tabATaskHeight(int idx, int mode, bool liveRefPos, bool subMode3, int playerHeight, int camHeight) {
    if (idx < 0 || std::size_t(idx + 1) >= std::size(kTaskHeights)) return camHeight; // [port] beyond dump: rec+0x114 lane
    const unsigned w = static_cast<unsigned>(static_cast<int>(std::int16_t(kTaskHeights[std::size_t(idx) + 1]))); // packed >>16 sign-extended halfword
    if (w == 0xfffffffbu) return playerHeight; // sentinel -5 -> rec+0x78
    if (w == 0xfffffffcu) return camHeight;    // sentinel -4 -> rec+0x114
    if (w == 0xffffffffu) {                    // sentinel -1 -> mode constants (DAT_00580df0)
        switch (mode) {
            case 1: return 0x1280;
            case 2: return liveRefPos ? 0x1280 : 0x440; // DAT_00644858 > -1
            case 3: return subMode3 ? 0x1980 : 0x1100;  // rec+0x10 == 3
            default: return 0x1540;
        }
    }
    return int(w); // plain height (the exe also passes -3/-2 straight through)
}

// --- CameraTask: timer B/C snapshot-swap model (FUN_00517b50/e30/f20/cc0/be0 semantics) ---------
void CameraTask::startFade(int end) {
    if (end < 1) return; // [port] exe divides by EBX unguarded
    fadeEnd_ = end;      // rec+0x198
    fadeLive_ = 0;       // rec+0x1a4
    fadeStep_ = fixedDiv(0x10000u, end); // rec+0x1b0: step = 0x10000*0x10000/end (16.16)
    fadeActive_ = true;  // rec+0x1bc bit0 |= 1
}

int CameraTask::advanceFade(int frames) {
    if (!fadeActive_) return -1; // FUN_00517cc0 inactive path (returns 0xffffffff, *out = 0)
    fadeLive_ += frames;         // rec+0x1a4 += EAX
    const int remaining = fadeLive_ - fadeEnd_;
    if (fadeLive_ >= fadeEnd_) { // completion: FUN_005176f0 clears the bit and snaps live = end
        fadeActive_ = false;
        fadeLive_ = fadeEnd_;
    }
    return remaining;
}

int CameraTask::fadeRamp16x16() const { return fixedMulRound(unsigned(fadeLive_), fadeStep_); }

void CameraTask::startDolly(glm::ivec3 from, glm::ivec3 to, int durTicks) {
    if (durTicks < 1) { // [port] degenerate duration: commit-snap semantics (A = B = target)
        snapA_ = snapB_ = to;
        dollyActive_ = false;
        return;
    }
    snapA_ = from;      // snapshot A (+0xa0..): dolly start
    snapB_ = to;        // snapshot B (+0xd0..): live target
    dollyEnd_ = durTicks;   // rec+0x190
    dollyLive_ = 0;         // rec+0x19c
    const int dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    signX_ = dx < 0 ? -1 : 1;
    signY_ = dy < 0 ? -1 : 1;
    signZ_ = dz < 0 ? -1 : 1;
    stepX_ = fixedDiv(unsigned(dx < 0 ? -dx : dx), durTicks); // rec+0x1a8: |delta| << 16 / dur
    stepZ_ = fixedDiv(unsigned(dz < 0 ? -dz : dz), durTicks); // rec+0x1c0
    stepY_ = fixedDiv(unsigned(dy < 0 ? -dy : dy), durTicks); // [port] y lane: the exe dollies two axes per start call
    dollyActive_ = true;                                       // rec+0x1b4 bit0 |= 1
}

bool CameraTask::advanceDolly(int frames) {
    if (!dollyActive_) return false;
    dollyLive_ += frames; // rec+0x19c += EAX (FUN_00517be0)
    if (dollyLive_ >= dollyEnd_) {
        dollyActive_ = false; // FUN_00517670: clear active, live = end, snap snapshot A = B
        dollyLive_ = dollyEnd_;
        snapA_ = snapB_;
        return true;
    }
    return false;
}

glm::ivec3 CameraTask::eye() const {
    if (!dollyActive_) return snapB_; // snapped: A == B after completion/commit
    const std::uint32_t live = unsigned(dollyLive_);
    return snapA_ + glm::ivec3(signX_ * fixedMulRound(live, stepX_), signY_ * fixedMulRound(live, stepY_),
                               signZ_ * fixedMulRound(live, stepZ_)); // magnitudes x sign(B-A) (FUN_00517f20)
}

void CameraTask::commit(glm::ivec3 live) {
    // Commit trio FUN_00517730/760/790: copy the scene live block to snapshot B, then the clear
    // helpers snap A = B and idle both timers (rec+0x1b4 bit0 / rec+0x1bc bit0).
    snapB_ = live;
    snapA_ = live;
    dollyActive_ = false;
    fadeActive_ = false;
}
// --- Broadcast grid box (WC98 FIFAWC.EXE FUN_00501910 + init FUN_00500e60) --------------------
// The exe keeps the box state in DAT_0058bcd4/bcd8, scale factors DAT_0058bcc4/bcc8 and cell sizes
// DAT_0058bccc/bcd0; index = cellX + cellZ*13 with complement 298-index feeds camera-mode selection.
void camGridBoxInit(CamGridBox& g, int arenaW, int arenaH) { // FUN_00500e60 (DAT_006412cc/d inputs)
    g.scaleFX = static_cast<std::int32_t>(static_cast<std::int64_t>(11) * 0x10000 / arenaW); // Fixed(11/w)
    g.scaleFZ = static_cast<std::int32_t>(static_cast<std::int64_t>(21) * 0x10000 / arenaH); // Fixed(21/h)
    g.sizeX = arenaW / 11;
    g.sizeZ = arenaH / 21;
}

std::pair<int, int> camGridBoxUpdate(CamGridBox& g, bool follow, glm::ivec2 pitchPos, glm::ivec2 camPos) {
    // Guard rectangle in units domain: pos must lie inside [(cell-1)*size - 0x50, cell size + 0x50]
    // relative to the box footprint anchored at camPos (exe compares ball<<16 vs these <<16).
    const long cx = static_cast<long>(g.cellX - 1) * g.sizeX - camPos.x;
    const long cz = static_cast<long>(g.cellZ - 1) * g.sizeZ - camPos.y;
    const bool outX = pitchPos.x <= cx - 0x50 || static_cast<long>(g.sizeX) + cx + 0x50 <= pitchPos.x;
    const bool outZ = pitchPos.y <= cz - 0x50 || static_cast<long>(g.sizeZ) + cz + 0x50 <= pitchPos.y;
    if (!follow || outX || outZ) {
        // exe: (ball<<16 * scale >> 16 + carry + off) >> 16 + 1. The carry bit is the LSB-1 of the
        // Fixed product; with an integer scale and a <<16 ball it is always zero, so in units domain
        // cell = (pos*scale + off) / 65536 + 1 (off 0x58000 -> x-grid half-height 5.5 cells, etc.).
        const std::int64_t vx = static_cast<std::int64_t>(pitchPos.x) * g.scaleFX + 0x58000;
        g.cellX = static_cast<int>(vx >> 16) + 1;
        const std::int64_t vz = static_cast<std::int64_t>(pitchPos.y) * g.scaleFZ + 0xa8000;
        g.cellZ = static_cast<int>(vz >> 16) + 1;
    }
    g.cellX = std::clamp(g.cellX, 0, 0xc);
    g.cellZ = std::clamp(g.cellZ, 0, 0x16);
    const int idx = g.cellX + g.cellZ * 0xd;
    return {idx, 0x12a - idx};
}
} // namespace wc98::presentation
