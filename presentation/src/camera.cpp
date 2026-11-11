// The exe addresses in this file are FIFA 99-family documentation retained for the WC98 port: World
// Cup 98 is the same Gen3 engine family, confirmed by docs/reverse-engineering/GHIDRA_PASS5_IG.md
// (architecture; sim clock evidence l.125). WC98-specific rederivation of these addresses is a
// follow-up slice. Ported from fifa99-native presentation/src/camera.cpp:
// Port of the FIFA99.EXE v1.1 in-match cameras. Parameter values were read from the per-mode init
// functions (rotation CamRot_init* 0x4C3BAC.., position CamPos_init* 0x544FAC..) and the FOV table
// i16 0x57FAD8; see docs/MATCH_RENDERING.md and docs/reverse/MATCH_CAMERAS.md for every address.
// Resolved (pass-15): the orbit/lead trig lane now uses the WC98 fixed-point table sine (`sine16x16` /
// FUN_00497740 over DAT_00585898, byte-exact + asserted) with the engine's fixed-multiply rounding —
// the old libm path drifted by one unit of the 16-bit angle. Remaining open items: `atan2_16` keeps a
// libm approximation (the WC98 accessor site is not located yet, register D-53), and the per-mode
// **parameter rows** above are FIFA-99 cites pending WC98 re-dump (register D-54).
#include "wc98/presentation/camera.hpp"

#include "exe_tables.inc"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <iterator>

namespace wc98::presentation {

namespace {

constexpr double kTurn = 65536.0;

struct HeightKey {
    int xAdd, y, zKey;
};

struct ModeParams {
    // rotation (0x589C50 flags, 0x589C84..)
    unsigned flags;
    int pitchA, pitchB, yawA, yawB, yawDiv, pitchDiv;
    std::span<const int> yawZoneKeys;  // thresholds (entries 1..n-2 of 0x589D6C), key ball z
    std::span<const int> yawMax, yawMin; // e08[i], de4[i]
    std::span<const int> pitchZoneKeys; // thresholds of 0x589CA8, key ball x if flag 0x4000 else z
    std::span<const int> pitchMax, pitchMin; // d44[i], d20[i]
    int yawBox[4], pitchBox[4];          // x0, x1, z0, z1 (flags 0x20000 / 0x10000)
    int hiY, hiTrig, hiPitchB, hiPitchA, hiDiv;
    glm::ivec3 hiOff;
    int looseOn, looseOff, looseB, looseA, looseDiv;
    std::span<const int> leadKeys;       // thresholds of 0x673800 (key look-at z)
    std::span<const int> lead0, lead1;   // 0x673818 (side 0) / 0x67382C (side 1)
    // position (0x59F610..)
    unsigned posFlags; // 2 = height table
    int offX, height, offZ, div, hdiv;
    int boxX0, boxX1, boxZ0, boxZ1;
    std::span<const HeightKey> table0, table1;
};

constexpr unsigned kYawZones = 0x400, kPitchZones = 0x200, kPitchKeyX = 0x4000, kYawBox = 0x20000, kPitchBox = 0x10000;
constexpr unsigned kHighBall = 0x100000, kLooseYaw = 0x200000;

// Tele (0)
constexpr int kTeleYawKeys[] = {-2000, 2000}, kTeleYawMax[] = {16768, 18768, 24576}, kTeleYawMin[] = {8192, 14000, 15500};
constexpr int kXKeys[] = {-1000, -500, 500, 1000};
constexpr int kTelePitchMax[] = {60900, 61050, 61200, 61350, 61500}, kTelePitchMin[] = {60500, 60700, 60900, 61100, 61300};
constexpr int kLeadKeys[] = {-1750, -250, 250, 1750};
constexpr int kTeleLead0[] = {100, 100, 200, 300, 300}, kTeleLead1[] = {300, 300, 200, 100, 100};
// Height tables {x add, height, z key} are static exe data, extracted by reverse/tables/gen_presentation_tables.py.
template <std::size_t N> constexpr std::array<HeightKey, N> heights(const std::int32_t (&t)[N][3]) {
    std::array<HeightKey, N> r{};
    for (std::size_t i = 0; i < N; ++i) r[i] = {t[i][0], t[i][1], t[i][2]};
    return r;
}
constexpr auto kTeleH0 = heights(exe::kCamHeightTele0), kTeleH1 = heights(exe::kCamHeightTele1);
// Sideline (1)
constexpr int kSidePitchMax[] = {61750, 61750, 61750, 61750, 61750}, kSidePitchMin[] = {61400, 61400, 61400, 61400, 61400};
constexpr int kLead100[] = {100, 100, 100, 100, 100};
constexpr auto kSideH0 = heights(exe::kCamHeightSide0), kSideH1 = heights(exe::kCamHeightSide1);
// Action (2)
constexpr int kActionPitchMax[] = {61500, 61500, 61500, 61500, 61500}, kActionPitchMin[] = {60000, 60000, 60000, 60000, 60000};
constexpr int kLead300[] = {300}, kLead200[] = {200}, kLead150[] = {150};
// Goal (3)
constexpr int kGoalPitchKeys[] = {-1500, -500, 500, 1000};
constexpr int kGoalPitchMax[] = {63000, 63000, 63000, 62000, 62000}, kGoalPitchMin[] = {59700, 59700, 59700, 59700, 59700};
constexpr auto kGoalH = heights(exe::kCamHeightGoal);
// Stadium (4)
constexpr int kStadYawKeys[] = {-2000, -750, 750, 2000}, kStadYawMax[] = {12250, 12000, 11750, 11500, 11250}, kStadYawMin[] = {7000, 7000, 7000, 7000, 7000};
constexpr int kStadPitchMax[] = {61500, 61500, 61500, 61500, 61500}, kStadPitchMin[] = {59000, 59400, 59800, 61000, 61000};
// Tower (7)
constexpr int kTowerLeadKeys[] = {-1750, -250, 250}, kLead500[] = {500, 500, 500, 500};

// Mode table, index = CameraMode for 0..5 and 7 (6 = Ball Cam has its own update).
constexpr ModeParams kModes[8] = {
    // 0 Tele: CamRot_initTele 0x4C3BAC, CamPos_initTele 0x544FAC
    {kPitchBox | kPitchKeyX | kYawZones | kPitchZones, 62500, 61900, 24576, 8192, 50, 48, kTeleYawKeys, kTeleYawMax, kTeleYawMin, kXKeys,
     kTelePitchMax, kTelePitchMin, {0, 0, 0, 0}, {-2000, 2000, 3000, 3500}, 240, 100, 59500, 62000, 30, {0, -100, 0}, 100, 5, 13000, 20000,
     20, kLeadKeys, kTeleLead0, kTeleLead1, 2, 1700, 650, 0, 24, 64, 50, 3350, -2500, 2500, kTeleH0, kTeleH1},
    // 1 Sideline: 0x4C413C / 0x5450D0
    {kPitchKeyX | kPitchZones, 63000, 58000, 19200, 14000, 40, 30, {}, {}, {}, kXKeys, kSidePitchMax, kSidePitchMin, {0, 0, 0, 0},
     {0, 0, 0, 0}, 100, 100, 32768, 63500, 30, {0, -75, 0}, 50, 1, 12000, 21000, 20, kLeadKeys, kLead100, kLead100, 2, 1200, 550, 0, 20, 64,
     -200, 2800, -2700, 2700, kSideH0, kSideH1},
    // 2 Action: 0x4C43BC / 0x54517C
    {kPitchZones, 62000, 60000, 1000, 64500, 50, 50, {}, {}, {}, kXKeys, kActionPitchMax, kActionPitchMin, {0, 0, 0, 0}, {0, 0, 0, 0}, 240,
     100, 32768, 62500, 40, {0, 0, 0}, 100, 5, 62000, 3000, 20, {}, kLead300, kLead300, 0, 0, 700, -1500, 20, 64, -1800, 1800, -3800, 800,
     {}, {}},
    // 3 Goal (End): 0x4C4598 / 0x545214
    {kPitchZones, 63000, 58000, 1500, 64000, 50, 50, {}, {}, {}, kGoalPitchKeys, kGoalPitchMax, kGoalPitchMin, {0, 0, 0, 0}, {0, 0, 0, 0},
     160, 100, 32768, 63200, 30, {0, 0, 0}, 50, 1, 61000, 3500, 20, {}, kLead200, kLead200, 2, 0, 700, -1350, 20, 64, -1800, 1800, -3900,
     900, kGoalH, kGoalH},
    // 4 Stadium: 0x4C4778 / 0x5452B8
    {kYawBox | kPitchBox | kPitchKeyX | kYawZones | kPitchZones, 62000, 59500, 13500, 6000, 50, 40, kStadYawKeys, kStadYawMax, kStadYawMin,
     kXKeys, kStadPitchMax, kStadPitchMin, {-2000, 1600, -3250, 3500}, {-2000, 1000, -4000, 4000}, 240, 100, 32768, 62200, 25, {0, 0, 0},
     100, 5, 4500, 10500, 20, {}, kLead200, kLead200, 0, 1000, 700, -1100, 32, 64, 0, 2900, -3450, 1350, {}, {}},
    // 5 Shoulder: 0x4C4A68 / 0x545338
    {0, 63000, 62000, 10500, 3000, 50, 50, {}, {}, {}, {}, {}, {}, {0, 0, 0, 0}, {0, 0, 0, 0}, 160, 100, 32768, 63500, 30, {0, 0, 0}, 50, 1,
     3000, 8200, 20, {}, kLead150, kLead150, 0, 950, 600, -1650, 24, 64, -750, 2750, -3850, 2450, {}, {}},
    // 6 Ball Cam (BallCam_update 0x4E90B4) has its own update; this row is unused
    {0, 0, 0, 0, 0, 1, 1, {}, {}, {}, {}, {}, {}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0, 0, 0, 0, 1, {0, 0, 0}, 0, 0, 0, 0, 1, {}, {}, {}, 0, 0, 240, 0,
     1, 1, 0, 0, 0, 0, {}, {}},
    // 7 Tower: 0x4C3EE0 / 0x545058 (flags 0x6000: zones off)
    {kPitchKeyX | 0x2000, 57500, 56500, 17600, 15000, 65, 50, {}, {}, {}, {}, {}, {}, {0, 0, 0, 0}, {0, 0, 0, 0}, 200, 100, 49152, 58500, 30,
     {0, 0, 0}, 150, 5, 13500, 19500, 25, kTowerLeadKeys, kLead500, kLead500, 0, 1600, 1520, 0, 24, 64, -150, 3150, -2550, 2550, {}, {}},
};
constexpr int kReplayFov = 0xE28; // cam+0x78 default (FOV table entry -5 = "use cam+0x78")
static_assert(exe::kCamFov[8] == -5);

double rad(int a16) { return double(a16) * 2.0 * std::numbers::pi / kTurn; }
// Exe polar-offset idiom (`dist * trig(x)`, OrbitCam_update 0x48D18C / BallCam_update 0x4E90B4):
// fixed-point product of the distance with FUN_00497740/4977b0 table sine/cosine (quadrant-folded
// DAT_00585898, slope term `* 0x6487e >> 9`), rounded by the engine's fixed-multiply pattern
// `(prod >> 16) + bit-15 carry` (FUN_00517f20). Replaces the libm path that drifted in the last ulp
// of the 16-bit angle; `sine16x16/cosine16x16` are byte-exact against the image table (pass-12cam,
// asserted in test_presentation), so orbit/lead offsets are now exact by construction.
int fxTrig(int len, int trig16) {
    const std::int64_t v = std::int64_t(len) * trig16;
    return int((v >> 16) + ((v & 0x8000) ? 1 : 0));
}
int sin16x(int len, int a16) { return fxTrig(len, sine16x16(a16)); }
int cos16x(int len, int a16) { return fxTrig(len, cosine16x16(a16)); }
int hdist(int dx, int dz) { return int(std::sqrt(double(dx) * dx + double(dz) * dz)); }
int wrap16(int a) { return a & 0xFFFF; }
int signed16(int a) { return std::int16_t(std::uint16_t(a)); }
int absDiff16(int a, int b) { return std::abs(signed16(a - b)); } // Angle16_absDiff 0x4C2F80

struct Angles {
    int yaw, pitch;
};
// Cam_anglesTo 0x4C2F04: d = eye - target.
Angles anglesTo(glm::ivec3 eye, glm::ivec3 t) {
    glm::ivec3 d = eye - t;
    int hd = hdist(d.x, d.z);
    return {wrap16(0x18000 - atan2_16(d.x, d.z)), wrap16(0x10000 - atan2_16(d.y, hd))};
}

int zoneOf(int key, std::span<const int> thresholds) {
    int i = 0;
    for (int t : thresholds) i += key >= t;
    return i;
}

bool inBox(const int b[4], glm::ivec3 p) { return p.x > b[0] && p.x < b[1] && p.z > b[2] && p.z < b[3]; }

glm::ivec3 toInt(glm::vec3 v) { return {int(v.x), int(v.y), int(v.z)}; }

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

void MatchCamera::setMode(CameraMode m) {
    mode_ = m;
    reset_ = true;
    int target = m == CameraMode::Replay ? kReplayFov : exe::kCamFov[std::size_t(m)];
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
    // WC98 deviation: prefer the live sim pitch dims; the FIFA 99 size-index table (camera height
    // tables are keyed to pitch size) stays as fallback when pitchUnits is unset.
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

    const ModeParams& m = kModes[std::size_t(mode_)];
    // --- position (CamPos_follow 0x5448B8) ---
    int gx = smoothBall_.x + m.offX, gz = smoothBall_.z + m.offZ, yT = m.height;
    const bool gliding = task_.dollyActive(); // WC98 timer C dolly owns the position lane (section 7 delta 1)
    if (gliding) {
        task_.advanceDolly(frames);      // FUN_00517be0: advance live counter, snap A = B on completion
        eye_ = task_.eye();              // snapshot A + signed fixed-point products (FUN_00517f20)
    } else if (m.posFlags & 2) {
        if (holder) side_ = holderDir >= 0 ? 1 : 0;
        std::span<const HeightKey> tab = side_ ? m.table1 : m.table0;
        int key = eye_.z, n = int(tab.size()), k = 1;
        while (k < n - 1 && key >= tab[std::size_t(k)].zKey) ++k;
        const HeightKey &a = tab[std::size_t(k - 1)], &c = tab[std::size_t(k)];
        float t = float(key - a.zKey) / float(c.zKey - a.zKey); // 16.16 in the exe, not clamped
        yT = a.y + int(float(c.y - a.y) * t);
        gx += a.xAdd + int(float(c.xAdd - a.xAdd) * t);
        if (!reset_) {
            int q = (yT - eye_.y) / m.hdiv;
            if (q) eye_.y += q * frames;
            else eye_.y += std::clamp(yT - eye_.y, -frames, frames);
        }
    } else {
        eye_.y = m.height;
    }

    if (gliding) {
        velX_ = velZ_ = 0; // dolly handoff: no residual follow velocity when the task releases
    } else if (reset_) {
        eye_ = {std::clamp(smoothBall_.x + m.offX, m.boxX0, m.boxX1), m.height, std::clamp(smoothBall_.z + m.offZ, m.boxZ0, m.boxZ1)};
        velX_ = velZ_ = 0;
    } else {
        gx = std::clamp(gx, m.boxX0, m.boxX1);
        gz = std::clamp(gz, m.boxZ0, m.boxZ1);
        velX_ = (velX_ + (gx - eye_.x) / m.div * frames) / 2;
        velZ_ = (velZ_ + (gz - eye_.z) / m.div * frames) / 2;
        eye_.x += velX_;
        eye_.z += velZ_;
    }

    // --- look-at (FollowCam_computeLookat 0x4C30D8): ball, led ahead of the ball holder ---
    glm::ivec3 look = ball;
    if (holder) {
        int i = zoneOf(look.z, m.leadKeys);
        std::span<const int> leads = holderDir >= 0 ? m.lead1 : m.lead0;
        int lead = leads[std::size_t(std::min<int>(i, int(leads.size()) - 1))];
        if (holder->role == Role::Goalkeeper) look.z += holder->pos.z > 0 ? -lead : lead; // flag 0x1000
        else {
            int h = int(holder->facing) << 6;
            look.x += sin16x(lead, h), look.z += cos16x(lead, h);
        }
    }
    Angles S = anglesTo(eye_, look);

    // --- angle windows (0x4C37A0 step 5) ---
    bool yawZones = (m.flags & kYawZones) && !((m.flags & kYawBox) && inBox(m.yawBox, ball));
    bool pitchZones = (m.flags & kPitchZones) && !((m.flags & kPitchBox) && inBox(m.pitchBox, ball));
    if (yawZones) {
        int i = zoneOf(ball.z, m.yawZoneKeys);
        S.yaw = clampYaw16(S.yaw, m.yawMax[std::size_t(i)], m.yawMin[std::size_t(i)]);
    } else {
        S.yaw = clampYaw16(S.yaw, m.yawA, m.yawB);
    }
    if (pitchZones) {
        int i = zoneOf((m.flags & kPitchKeyX) ? ball.x : ball.z, m.pitchZoneKeys);
        S.pitch = clampPitch16(S.pitch, m.pitchMax[std::size_t(i)], m.pitchMin[std::size_t(i)]);
    } else {
        S.pitch = clampPitch16(S.pitch, m.pitchA, m.pitchB);
    }

    // --- loose-ball overrides (FollowCam_looseBallOverride 0x4C3634) ---
    if (!holder) {
        glm::ivec3 p = ball;
        if (p.y > m.hiY) {
            p += m.hiOff;
            Angles q = anglesTo(eye_, p);
            if (overrides_ & kHighBall) {
                S.pitch = clampPitch16(q.pitch, m.hiPitchA, m.hiPitchB);
            } else {
                glm::ivec3 d = eye_ - p;
                int raw = wrap16(atan2_16(d.y, hdist(d.x, d.z))); // Cam_anglesToRaw: un-negated (original quirk)
                if (absDiff16(raw, S.pitch) > m.hiTrig) S.pitch = clampPitch16(raw, m.hiPitchA, m.hiPitchB), overrides_ |= kHighBall;
            }
        } else {
            overrides_ &= ~kHighBall;
        }
        Angles q = anglesTo(eye_, p);
        int d = absDiff16(q.yaw, S.yaw);
        if (overrides_ & kLooseYaw) {
            if (d < m.looseOff) overrides_ &= ~kLooseYaw;
        } else if (d > m.looseOn) {
            S.yaw = clampYaw16(q.yaw, m.looseA, m.looseB);
            overrides_ |= kLooseYaw;
        }
    } else {
        overrides_ &= ~(kHighBall | kLooseYaw);
    }

    // --- smoothing (Angle16_follow 0x4C2FC4) ---
    if (reset_) {
        yaw_ = S.yaw, pitch_ = S.pitch, velYaw_ = velPitch_ = 0;
    } else {
        pitch_ = follow16(pitch_, S.pitch, (overrides_ & kHighBall) ? m.hiDiv : m.pitchDiv, velPitch_, frames);
        yaw_ = follow16(yaw_, S.yaw, (overrides_ & kLooseYaw) ? m.looseDiv : m.yawDiv, velYaw_, frames);
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

// DAT_0057FB82: packed task-height words; the tabA_pre prelude FUN_004a0790 reads
// *(u32*)(&DAT_0057FB82 + idx*2) sign-shifted >> 16 = this table's u16 at idx+1. Sentinels
// 0xfffb/-5 -> rec+0x78, 0xfffc/-4 -> rec+0x114, 0xffff/-1 -> mode constants (see tabATaskHeight).
constexpr std::uint16_t kTaskHeights[32] = { // raw 0x17E182..; VA DAT_0057FB82
    0x0,   0xcc0, 0xcc0, 0xcc0, 0xa00, 0xcc0, 0x880, 0xac0,
    0xf00, 0xfffb, 0xcc0, 0xcc0, 0xfffc, 0x1100, 0xffff, 0x1280,
    0x1280, 0x880, 0xfffb, 0x1540, 0xe40, 0x1a80, 0x1280, 0x1540,
    0xf00, 0x1540, 0x8fc, 0xcc0, 0xcc0, 0xcc0, 0xcc0, 0x6163};

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
