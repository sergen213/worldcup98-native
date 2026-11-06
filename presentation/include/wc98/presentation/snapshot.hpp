#pragma once
// Render-only view of a match moment. The simulation (fixed point, 30 Hz ticks / 60 Hz frames)
// fills one snapshot per tick; the renderer interpolates two of them with the render alpha.
// Everything is float and in World Cup 98 game units: 1 unit = 1/16 ft, x across the pitch,
// y up, z along it (docs/reverse-engineering/SIM_PORT_NOTES.md). Angles use the game's convention:
// 1024 units per turn, 0 faces +z, direction = (sin a, 0, cos a) (Anim ch1 yaw, 1024/turn).
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace wc98::presentation {

// The original's in-match camera ids (port of the FIFA 99 family; docs/MATCH_RENDERING.md of the
// sibling). Mode 8 is the replay orbit camera.
enum class CameraMode : std::uint8_t { Tele, Sideline, Action, End, Stadium, Shoulder, Ball, Tower, Replay };
inline constexpr std::size_t kSelectableCameras = 8;
[[nodiscard]] const char* cameraName(CameraMode m);         // "tele", "sideline", ...
[[nodiscard]] bool parseCameraName(std::string_view s, CameraMode& out);

enum class Role : std::uint8_t { Outfield, Goalkeeper, Referee };
enum class TimeOfDay : std::uint8_t { Day, Night };
// Pitch condition letter (FIFA 99 PITCH_<c><t>.FSH family; WC98 surface art lives in
// INGAME/MISC/TURFS0P*.PAK + the stadium archives - kept for setup plumbing only).
enum class Pitch : std::uint8_t { C, H, R, S, W };

struct PlayerView {
    glm::vec3 pos{};          // feet on the ground: y = 0 when standing
    float facing = 0;         // game angle units (1024/turn), sim facing (+0x130)
    std::uint8_t team = 0;    // 0 / 1 (officials: ignored)
    Role role = Role::Outfield;
    std::uint8_t shirtNumber = 0; // the FCDB squad player's number for this slot
    std::int8_t skinTone = -1;    // 0..3; -1 = the FCDB squad player's skin tone
    std::int8_t lod = -1;     // forced model LOD index into kLodSuffixes; -1 = by distance
    int clip = -1;            // AnimData::clips index of the playing instance (-1 = stand pose)
    float keyPos = 0;         // fractional key position in that clip (keyA + frac/0x10000)
    bool mirror = false;      // instance flag bit 1 (AISEQS mirror)
    int yaw16 = 0;            // instance placement yaw, 65536/turn (AnimInstance::angle, +0x50)
    bool visible = true;
};

struct BallView {
    glm::vec3 pos{};   // centre; radius ~5 units (WC98 ball size: measure from the MISC8 'ball'
                       // sprite usage in the exe renderer; 5 is the FIFA 99 DAT_006691d4 value)
    int rollPhase = 0; // 0..3 sprite-frame phase of the 'ball' billboard (MISC8.FSH tag "ball")
    float heading = 0; // travel direction, game angle units (from BallState::vel)
    bool visible = true;
};

struct CameraView {
    CameraMode mode = CameraMode::Tele;
    // Replay orbit camera (mode 8) around the ball; defaults of Cam_init 0x45A128.
    int orbitYaw = 0;       // 65536 per turn
    int orbitDist = 1120;   // game units
    int orbitElev = 0x500;  // 65536 per turn (7.03 deg)
};

struct HudView {
    std::array<int, 2> score{};
    float clockSeconds = 0;               // game clock shown on the scoreboard
    int period = 0;                       // 0/1 halves
    std::array<std::string, 2> teamNames; // short names / abbreviations (FCDB STR0)
    std::array<int, 2> controlled{-1, -1}; // player index controlled by user 1 / 2 (indicator arrows)
    bool radar = true, scoreboard = true, names = true; // HUD overlay master switches (presentation/src/hud.cpp)
};

struct MatchSnapshot {
    std::array<PlayerView, 22> players{}; // 0..10 team 0, 11..21 team 1
    PlayerView referee{};                  // sim kReferee slot (players[22])
    BallView ball;
    int possessor = -1;                    // player index holding the ball (cameras lead ahead of him)
    std::array<std::int8_t, 2> attackDir{1, -1}; // +1: team attacks +z
    CameraView camera;
    TimeOfDay time = TimeOfDay::Day;
    Pitch pitch = Pitch::C;
    std::uint8_t pitchSize = 2;            // Options::pitchSize (Pitch table index)
    glm::vec2 pitchUnits{0, 0};            // live sim dimensions in game units {width x, length z}
    HudView hud;
};

// Game pitch dimensions for a size index (port of the FIFA 99 Pitch_setDimensions 0x455930 table):
// {width (x), length (z)}. The slice uses MatchSnapshot::pitchUnits from the sim when non-zero.
[[nodiscard]] glm::vec2 pitchDimensions(std::uint8_t sizeIndex);

// Positions, angles (shortest arc), key positions (same clip) and the camera target are
// interpolated; discrete fields come from `b`, the newer snapshot. alpha 0 = a, 1 = b.
[[nodiscard]] MatchSnapshot interpolate(const MatchSnapshot& a, const MatchSnapshot& b, float alpha);
[[nodiscard]] float lerpAngle(float a, float b, float alpha); // game angle units, result in [0, 1024)

} // namespace wc98::presentation
