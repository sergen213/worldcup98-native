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
    [[nodiscard]] int yaw() const { return yaw_; }
    [[nodiscard]] int pitch() const { return pitch_; }
    [[nodiscard]] int fovHalf16() const { return fov_; }

private:
    CameraMode mode_ = CameraMode::Tele;
    bool reset_ = true, started_ = false;
    glm::ivec3 eye_{0, 650, 0}, smoothBall_{};
    int yaw_ = 0, pitch_ = 0, velYaw_ = 0, velPitch_ = 0, velX_ = 0, velZ_ = 0;
    int side_ = 0;           // height-table side (possessing team's attack direction)
    unsigned overrides_ = 0; // loose-ball override flags 0x100000 / 0x200000
    int fov_ = 3200, fovFrom_ = 3200, fovTo_ = 3200;
    float fovGlide_ = 1.5f;  // seconds into the 1.5 s glide
    int ballSide_ = -1;      // Ball Cam attacking side
};

} // namespace wc98::presentation
