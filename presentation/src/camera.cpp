// The exe addresses in this file are FIFA 99-family documentation retained for the WC98 port: World
// Cup 98 is the same Gen3 engine family, confirmed by docs/reverse-engineering/GHIDRA_PASS5_IG.md
// (architecture; sim clock evidence l.125). WC98-specific rederivation of these addresses is a
// follow-up slice. Ported from fifa99-native presentation/src/camera.cpp:
// Port of the FIFA99.EXE v1.1 in-match cameras. Parameter values were read from the per-mode init
// functions (rotation CamRot_init* 0x4C3BAC.., position CamPos_init* 0x544FAC..) and the FOV table
// i16 0x57FAD8; see docs/MATCH_RENDERING.md and docs/reverse/MATCH_CAMERAS.md for every address.
// ponytail: sin/cos/atan2 use libm instead of the exe's 0x582EB4 quarter-wave table; results can differ
// from the original by a unit in the last place of the 16-bit angle. Extract the table if bit-exact
// replays are ever compared.
#include "wc98/presentation/camera.hpp"

#include "exe_tables.inc"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <span>

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
int sin16x(int len, int a16) { return int(double(len) * std::sin(rad(a16))); }
int cos16x(int len, int a16) { return int(double(len) * std::cos(rad(a16))); }
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
    if (m.posFlags & 2) {
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
    if (reset_) {
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
