#pragma once
// MatchScene: loads the original WC98 stadium, pitch, players, ball and shadow art once and turns
// a MatchSnapshot into a render::Frame. Port of the FIFA 99 sibling's match renderer with the
// WC98 asset family (INGAME/PLAYER/MODELS+TEXTURES, INGAME/MISC, INGAME/STADIUM).
#include "wc98/formats/anim.hpp"
#include "wc98/formats/pcmd.hpp"
#include "wc98/presentation/data.hpp"
#include "wc98/presentation/kits.hpp"
#include "wc98/presentation/model.hpp"
#include "wc98/presentation/snapshot.hpp"
#include "wc98/presentation/hud.hpp"
#include "wc98/renderer/renderer.hpp"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace wc98::presentation {

struct MatchSetup {
    std::uint32_t stadium = 0;      // FCDB team-record stadium byte: selects INGAME/STADIUM/<base>_<D|N>.BIG art
    TimeOfDay time = TimeOfDay::Day;
    Pitch pitch = Pitch::C;
    std::uint8_t pitchSize = 2;     // Options::pitchSize (surface tiles from INGAME/MISC/TURFS0P<col>.PAK)
    int surfaceColumn = 0;          // Options::surfaceColumn 0..4 -> TURFS0P<col>.PAK
    std::array<int, 2> teamIndex{0, 1}; // FCDB.DBI team table indices (56 teams; ids 26, 29: PK###A and H both exist)
    bool stadiumArt = true;         // load the stadium side models + atlas when the archive resolves
};

class MatchScene {
public:
    MatchScene(render::Renderer& r, const GameData& data, const MatchSetup& setup);
    ~MatchScene();

    // Builds the frame for an (interpolated) snapshot seen through `camera` (MatchCamera::camera();
    // its yaw selects the ball sprite roll frame). Player LODs come from PlayerView::lod
    // (-1 = by camera distance). The returned frame references storage owned by the scene and stays
    // valid until the next build().
    const render::Frame& build(const MatchSnapshot& s, const render::Camera& camera, bool enhanced);

    [[nodiscard]] const MatchSetup& setup() const { return setup_; }
    // Full-channel clips of ANIM.MO (via wc98::formats), for samplePose: rig = data.anim.rig.
    // Index domain: sim AnimData::clips order (AISEQS.TBI name table) — the same index as
    // PlayerView::clip / AnimInstance::clip; loaded internally via sim::loadMatchAnimData.
    [[nodiscard]] const formats::AnimClip& clip(int index) const;   // throws on bad index
    // Team FCDB ids of the two sides (for kit atlas naming by the caller's snapshot builder).
    [[nodiscard]] std::array<std::uint16_t, 2> teamIds() const;
    // HUD overlay layout knobs (settings.ini [gameplay] hud_* passthrough; HUD_WC98.md). Call after the
    // ctor to recalibrate the scoreboard strip / radar panel / shirt-label placement.
    void setHudGeometry(const HudGeometry& g);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MatchSetup setup_;
};

} // namespace wc98::presentation
