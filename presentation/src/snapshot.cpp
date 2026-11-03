// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/presentation/snapshot.hpp"

#include <cmath>

namespace wc98::presentation {

namespace {
constexpr std::array<const char*, 9> kCameraNames = {"tele", "sideline", "action", "end", "stadium", "shoulder", "ball", "tower", "replay"};

PlayerView lerp(const PlayerView& a, const PlayerView& b, float t) {
    PlayerView r = b; // discrete fields (team/role/shirt/lod/clip/mirror/yaw16/visible) come from b
    r.pos = glm::mix(a.pos, b.pos, t);
    r.facing = lerpAngle(a.facing, b.facing, t);
    // Same animation instance: advance the key fraction (WC98 sim AnimInstance keyA + frac/0x10000,
    // wc98/sim/anim.hpp; the FIFA 99 clipTime is the 60 Hz clock of the same playback).
    if (a.clip == b.clip && a.mirror == b.mirror && b.keyPos >= a.keyPos) r.keyPos = a.keyPos + (b.keyPos - a.keyPos) * t;
    return r;
}
} // namespace

const char* cameraName(CameraMode m) { return kCameraNames[std::size_t(m)]; }

bool parseCameraName(std::string_view s, CameraMode& out) {
    for (std::size_t i = 0; i < kCameraNames.size(); ++i)
        if (s == kCameraNames[i]) return out = CameraMode(i), true;
    return false;
}

glm::vec2 pitchDimensions(std::uint8_t idx) {
    // Pitch_setDimensions 0x455930 (docs/reverse-engineering/SIM_PORT_NOTES.md). Cross-checked
    // against wc98/sim/pitch.hpp kW/kL (Pitch::make, sim 0x65F324/0x65F328): identical for every
    // index: 0 = {0xff0, 0x1620}, 1 = {0xf00, 0x1500}, 2 = {0xe40, 0x13e0} (default), 3 = {0xe10, 0x12c0}.
    switch (idx) {
    case 0: return {4080, 5664};
    case 1: return {3840, 5376};
    case 3: return {3600, 4800};
    default: return {3648, 5088}; // 2 (default) and 4
    }
}

float lerpAngle(float a, float b, float t) {
    float d = std::remainder(b - a, 1024.0f); // shortest arc in [-512, 512]
    float r = std::fmod(a + d * t, 1024.0f);
    return r < 0 ? r + 1024.0f : r;
}

MatchSnapshot interpolate(const MatchSnapshot& a, const MatchSnapshot& b, float t) {
    MatchSnapshot r = b; // pitchUnits/hud/score/attackDir/camera pass through from the newer snapshot
    for (std::size_t i = 0; i < r.players.size(); ++i) r.players[i] = lerp(a.players[i], b.players[i], t);
    r.referee = lerp(a.referee, b.referee, t); // WC98: the single sim kReferee slot (players[22])
    r.ball.pos = glm::mix(a.ball.pos, b.ball.pos, t);
    r.ball.heading = lerpAngle(a.ball.heading, b.ball.heading, t);
    if (b.hud.clockSeconds >= a.hud.clockSeconds) r.hud.clockSeconds = a.hud.clockSeconds + (b.hud.clockSeconds - a.hud.clockSeconds) * t;
    return r;
}

} // namespace wc98::presentation
