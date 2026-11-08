#pragma once
// The original in-match cameras (port of the FIFA 99 family, docs/MATCH_RENDERING.md of the
// sibling; WC98 clock evidence: 60 Hz frame clock mod 0x3c, GHIDRA_PASS5_IG.md l.125). Like the
// exe, the camera is stepped once per game tick with the tick's 60 Hz frame count (normally 2) in
// integer game units and 16-bit angles (65536 per turn); the render camera is not interpolated
// between ticks.
#include "wc98/presentation/snapshot.hpp"
#include "wc98/renderer/renderer.hpp"
#include <utility> // std::pair (camGridBoxUpdate)

namespace wc98::presentation {

// Signed 16-bit angle helpers (65536 per turn), as used by the camera code.
[[nodiscard]] int atan2_16(int y, int x);                 // Atan2_16 0x461A10
[[nodiscard]] int follow16(int cur, int target, int div, int& vel, int frames); // Angle16_follow 0x4C2FC4
[[nodiscard]] int clampYaw16(int a, int A, int B);        // Angle16_clampYaw 0x4C3548
[[nodiscard]] int clampPitch16(int a, int A, int B);      // Angle16_clampPitch 0x4C351C
// World forward vector of a camera with these angles: (-sin y cos p, sin p, cos y cos p).
[[nodiscard]] glm::vec3 cameraForward(int yaw16, int pitch16);
// Horizontal half-angle (65536/turn) -> vertical FOV in degrees of the 4:3 picture (Proj_setFocal 0x47F8B0).
[[nodiscard]] float verticalFovDeg4x3(int fovHalf16);
// --- Broadcast grid box (WC98 FIFAWC.EXE FUN_00501910, init FUN_00500e60) -------------------
// The exe tracks the ball in a 13x23 broadcast cell grid over the arena: cell state DAT_0058bcd4/bcd8
// (clamped to [0..12] x [0..22]), scale factors DAT_0058bcc4/bcc8, cell sizes DAT_0058bccc/bcd0.
// Emits index = cellX + cellZ*13 and its complement 298-index (feed for camera-mode selection).
struct CamGridBox {
    int cellX = 6, cellZ = 11;                     // DAT_0058bcd4/bcd8 box state
    std::int32_t scaleFX = 0x10000, scaleFZ = 0x10000; // DAT_0058bcc4/bcc8: Fixed(11/arenaW), Fixed(21/arenaH)
    int sizeX = 1, sizeZ = 1;                      // DAT_0058bccc/bcd0: arenaW/11, arenaH/21 units per cell
};
void camGridBoxInit(CamGridBox& g, int arenaW, int arenaH);           // FUN_00500e60 (DAT_006412cc/d)
// follow=false -> recompute unconditionally; true -> only when the point leaves the box rectangle
// around camPos (the exe's EBX flag). pitch/cam positions are game units. {index, complement}.
std::pair<int, int> camGridBoxUpdate(CamGridBox& g, bool follow, glm::ivec2 pitchPos, glm::ivec2 camPos);

// --- WC98 task-handler camera model (TASK_HANDLERS_WC98.md sections 1/4/5; pass-12cam decompiles
// ghidra-scripts/GhidraWc98CamDolly.py) ----------------------------------------------------------
// Fixed-point sine/cosine on the exe's quarter-wave table DAT_00585898 (FUN_00497740/4977b0):
// 16.16 magnitudes, angle units = 65536 per turn. Table T[i] = sin(2*pi*i/1024)*65536 with the
// quadrant-fold + quadratic slope interpolation of the handler pair. sineWc98 exposes raw entries
// (T[256]=0x10000 is the adjacent word the exe's fold indexes into).
[[nodiscard]] int sineWc98(int i);            // T[i], i in [0,256]
[[nodiscard]] int sine16x16(int angle16);     // FUN_00497740 lane: sin of the u16-turn angle (16.16)
[[nodiscard]] int cosine16x16(int angle16);   // FUN_004977b0 pair lane: sin(angle + quarter turn)

// Preset camera rows {angle, speed, height, yaw offset} installed from the byte-proven static u16
// tables (section 7 delta 2). angle/speed are 16-bit-angle/velocity units; height is game units.
struct CameraPresetRow {
    std::uint16_t angle, speed, height, yawOff;
};
[[nodiscard]] int cameraPresetRowCount();     // rows available per table: 2 (h12), 4 (h14)
// h12 fixed preset rows at DAT_00585FEA/EC/FEE/FF0 (stride 10; row = DAT_00649338 > 0).
[[nodiscard]] CameraPresetRow cameraPresetH12(int row);
// h14 per-slot preset rows at DAT_00586046/48/4A/4C (stride 10; slot = task record rec+0x60).
[[nodiscard]] CameraPresetRow cameraPresetH14(int slot);
// h28 singleton preset at DAT_005862D2/2D4/2D6/2D8.
[[nodiscard]] CameraPresetRow cameraPresetH28();

// tabA_pre height prelude FUN_004a0790: packed u32 at DAT_0057FB82 + idx*2, sign-extended top
// halfword = DAT_0057FB82[idx+1]. Sentinels -5 -> playerHeight (rec+0x78), -4 -> camHeight
// (rec+0x114), -1 -> mode constants {mode 1: 0x1280; mode 2: liveRefPos ? 0x1280 : 0x440;
// mode 3: subMode3 ? 0x1980 : 0x1100; else 0x1540}. Returns the plain height otherwise.
[[nodiscard]] int tabATaskHeight(int idx, int mode, bool liveRefPos, bool subMode3, int playerHeight, int camHeight);

// h23 FUN_004cafe0 scene-level quality-tier camera [P]: eye from the quality word DAT_00644778
// (tier deltas below), x = clamp(xEdge + dx*0x10, -0x140-xEdge .. xEdge+0x760); z source by task
// flags bit4/bit5: ball DAT_00644830 / player-pair DAT_00644820 (bit5) / block DAT_00644860,
// clamped to zEdge-0x2a0 .. 0x230-zEdge. speedAngle = rec+0x10c bytes {0x40,6} = 0x640.
struct TierCamEye {
    int x, z;
    int polarDist;   // dz << 4 tier lane (FUN_00518060 + atan pair argument [P])
    unsigned speedAngle; // rec+0x10c constant (angle/speed pack)
};
TierCamEye tierCamera(int quality, int flags, int xEdge, int zEdge, int ballZ, int blockZ, int playerZ);

// h21 FUN_004ca690 ball-follow TV camera [P]: source triple by task flags - carrier DAT_00644870
// (byte[1] bit2), else player-pair DAT_00644828 (byte0 bit4), else ball DAT_00644818; x/z
// bisection-clamped to +/- (halfW-0x1e0) / +/- (halfL-0x140); speedAngle = rec+0x104 {0x58,0xeb} =
// 0xeb58 (zoom word rec+0x68 is the live lane, caller-side).
struct FollowCamEye {
    int x, z;
    unsigned speedAngle; // rec+0x104 constant (angle/speed pack)
};
FollowCamEye followCamera(int flags, int ballX, int ballZ, int pairX, int pairZ, int carrierX,
                          int carrierZ, int halfW, int halfL);

// h18 FUN_004cb600 pitch-edge side camera [P]: the z lane from ball x (DAT_006447c4): >= 0x1e0 ->
// far (+z) lane zEdge+0x140, < -0x1df -> near (-z) lane -zEdge-0x140, else the rec+0x110 scene
// constant; a selection matching neither lane forces the near lane (bisection guard).
// polarHeight = DAT_00644794-0x10 (rec+0xc polar-offset lane). h20 FUN_004c9fd0 overhead:
// polarDist rec+0x8 = -xEdge, angle/speed 0x90.
struct SideCamEye {
    int z;
    int polarHeight; // DAT_00644794 - 0x10 (rec+0xc)
};
SideCamEye sideCamera(int ballX, int sceneRec110, int zEdge, int pitchConst);
struct OverheadCamPolar {
    int polarDist;   // rec+0x8 = -DAT_00644780 (negative x-edge)
    unsigned angleSpeed; // rec+0xc = 0x90 [P]
};
OverheadCamPolar overheadCamera(int xEdge);

// Camera presentation task record: the WC98 timer B (fade/zoom) + timer C (position dolly)
// snapshot-swap model over a camera snapshot pair (section 7 delta 1). Mirrors the task-record
// fields consumed by the h6/h8/h13/h21/h24/h27 follow family: snapshot A = dolly start,
// snapshot B = live target; commit trio writes B from the scene block and snaps A = B.
class CameraTask {
public:
    static constexpr int kPolarSpeed = 0x3c0;   // rec+0x8c pool-init default (FUN_004a0810): 960-unit polar move
    static constexpr int kPlayerHeight = 0xe28; // rec+0x78 pool-init default player/camera height
    // Handler-index slots rec+8 / rec+0xc: pool init writes both together (section 1).
    void setHandler(int idx) { handler_ = idx; event_ = idx; }
    [[nodiscard]] int handler() const { return handler_; }
    [[nodiscard]] int event() const { return event_; }
    // Preset row index rec+0x60 (h12/h14 table rows) and camera height rec+0x114.
    void setPresetRow(int r) { presetRow_ = r; }
    [[nodiscard]] int presetRow() const { return presetRow_; }
    void setCamHeight(int h) { camHeight_ = h; }
    [[nodiscard]] int camHeight() const { return camHeight_; }
    [[nodiscard]] int playerHeight() const { return kPlayerHeight; }

    // Timer B (FUN_00517b50/cc0): fade/zoom ramp over `end` ticks. Step = 0x10000*0x10000/end in
    // 16.16; live counts up and the active bit clears at completion (FUN_005176f0 snap).
    void startFade(int end);
    int advanceFade(int frames);            // FUN_00517cc0: returns live-end; >= 0 once complete
    [[nodiscard]] bool fadeActive() const { return fadeActive_; }
    [[nodiscard]] int fadeRamp16x16() const; // progress 0..0x10000 (live * step >> 16, exe rounding)

    // Timer C (FUN_00517e30/be0/f20): position dolly from snapshot A toward B over durTicks.
    // Per-axis steps = |delta| << 16 / dur; the products are magnitudes applied with sign(B-A)
    // in eye(). Completion snaps A = B and clears the active bit (FUN_00517670).
    void startDolly(glm::ivec3 from, glm::ivec3 to, int durTicks);
    bool advanceDolly(int frames);          // true on the completing frame
    [[nodiscard]] bool dollyActive() const { return dollyActive_; }
    [[nodiscard]] glm::ivec3 eye() const;   // snapshot A + signed fixed-point products
    [[nodiscard]] glm::ivec3 snapshotA() const { return snapA_; }
    [[nodiscard]] glm::ivec3 snapshotB() const { return snapB_; }

    // Commit trio writes (FUN_00517730/760/790 + clear helpers): B = live, A = B, timers idle.
    void commit(glm::ivec3 live);

private:
    int handler_ = 0, event_ = 0;      // rec+8 / rec+0xc
    int presetRow_ = 0;                // rec+0x60
    int camHeight_ = kPlayerHeight;    // rec+0x114 (pool init loads via tabA_pre)
    glm::ivec3 snapA_{}, snapB_{};     // snapshots A (+0xa0..+0xbc) / B (+0xd0..+0xfc) camera pos group
    int fadeEnd_ = 0, fadeLive_ = 0;   // rec+0x198/+0x1a4
    std::uint32_t fadeStep_ = 0;       // rec+0x1b0 (16.16)
    bool fadeActive_ = false;          // rec+0x1bc bit0
    int dollyEnd_ = 0, dollyLive_ = 0; // rec+0x190/+0x19c
    std::uint32_t stepX_ = 0, stepZ_ = 0, stepY_ = 0; // rec+0x1a8/+0x1c0 (port adds the y lane)
    int signX_ = 0, signY_ = 0, signZ_ = 0;           // sign(B-A) applied to the magnitudes in eye()
    bool dollyActive_ = false;         // rec+0x1b4 bit0
};

class MatchCamera {
public:
    // Selects a camera; the next tick snaps (Cam_applyPendingMode 0x45A578 sets the reset flags)
    // and the FOV glides to the new mode's value over 1.5 s (Cam_modeInitFov 0x45A52C).
    void setMode(CameraMode m);
    [[nodiscard]] CameraMode mode() const { return mode_; }
    // Forces a snap on the next tick (kick-off, end of a cut-scene, replay jump).
    void reset() { reset_ = true; }
    // One game tick; `frames` = 60 Hz frames simulated in it (DAT_0065F1E0 / World::time.simFrames).
    void tick(const MatchSnapshot& s, int frames = 2);
    // Render camera in game units (left-handed like the original's view space).
    [[nodiscard]] render::Camera camera() const;

    [[nodiscard]] glm::ivec3 eye() const { return eye_; }
    // Scene-level quality word DAT_00644778 (h23 tier selector): the per-scene launch parameter
    // written by task pool init (FUN_004a0810 argument [P]); consumed via presentation::tierCamera.
    void setQualityTier(int q) { quality_ = q; }
    [[nodiscard]] int qualityTier() const { return quality_; }
    [[nodiscard]] int yaw() const { return yaw_; }
    [[nodiscard]] int pitch() const { return pitch_; }
    [[nodiscard]] int fovHalf16() const { return fov_; }
    // Camera transition dolly (WC98 timer C, section 7 delta 1): interpolate the eye to `target`
    // over `ticks` ticks instead of snapping (kickoff/goal transitions). While active, tick() hands
    // the position lane to the CameraTask; angles keep smoothing. ticks < 1 snaps immediately.
    void glideTo(glm::ivec3 target, int ticks) { task_.startDolly(eye_, target, ticks); if (ticks < 1) eye_ = target; }
    [[nodiscard]] bool gliding() const { return task_.dollyActive(); }
    [[nodiscard]] CameraTask& task() { return task_; }
    [[nodiscard]] const CameraTask& task() const { return task_; }

private:
    CameraMode mode_ = CameraMode::Tele;
    bool reset_ = true, started_ = false;
    glm::ivec3 eye_{0, 650, 0}, smoothBall_{};
    int yaw_ = 0, pitch_ = 0, velYaw_ = 0, velPitch_ = 0, velX_ = 0, velZ_ = 0;
    int side_ = 0;           // height-table side (possessing team's attack direction)
    int quality_ = 1;      // DAT_00644778 scene-level quality word (h23 tier lane, default low)
    unsigned overrides_ = 0; // loose-ball override flags 0x100000 / 0x200000
    int fov_ = 3200, fovFrom_ = 3200, fovTo_ = 3200;
    float fovGlide_ = 1.5f;  // seconds into the 1.5 s glide
    int ballSide_ = -1;      // Ball Cam attacking side
    CameraTask task_;        // WC98 task-handler camera model (timer B/C + presets)
};

} // namespace wc98::presentation
