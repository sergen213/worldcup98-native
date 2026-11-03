#pragma once
// The original in-match cameras (port of the FIFA 99 family, docs/MATCH_RENDERING.md of the
// sibling; WC98 clock evidence: 60 Hz frame clock mod 0x3c, GHIDRA_PASS5_IG.md l.125). Like the
// exe, the camera is stepped once per game tick with the tick's 60 Hz frame count (normally 2) in
// integer game units and 16-bit angles (65536 per turn); the render camera is not interpolated
// between ticks.
#include "wc98/presentation/snapshot.hpp"
#include "wc98/renderer/renderer.hpp"

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
