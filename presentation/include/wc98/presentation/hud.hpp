#pragma once
// In-match 2D HUD overlay: pure layout logic (snapshot + font -> quad list), no renderer calls.
// The scene's drawHud feeds these quads to render::SpriteBatch; tests run this layer headless.
// Coordinate system: screen pixels, top-left origin, 640x480 reference (the original drew the HUD
// in back-buffer pixels; Proj_screenHeight 240 ties that surface height to 480 game units).
// Evidence and hypothesis flags: docs/reverse-engineering/HUD_WC98.md.
#include "wc98/formats/ffn.hpp"
#include "wc98/presentation/snapshot.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace wc98::presentation {

// One textured screen quad. `texture` is a key the scene resolves: "" = solid colour (renderer
// texture 0 substitutes opaque white), "font" = the FFN atlas, anything else = an FSH tag bitmap.
struct HudQuad {
    std::string texture;
    glm::vec4 dst{};          // x, y, w, h in screen px
    glm::vec4 uv{0, 0, 1, 1}; // u0, v0, u1, v1
    std::uint32_t rgba = 0xFFFFFFFFu;
};

// Scoreboard/radar geometry. Defaults are the FIFA 99 family layout scaled to 640x480 (Hypothesis:
// WC98 screen coordinates were not recoverable from FIFAWC.EXE, see HUD_WC98.md); every field is a
// calibration knob settable through settings.ini [gameplay] hud_* passthrough keys. The radar
// rect/offsets ARE exe-confirmed (rpit draw site 0x41536c, float block VA 0x55a9a8).
struct HudGeometry {
    // Scoreboard strip (centered): panel + name/score/clock slots.
    float sbX = 268.f, sbW = 104.f;   // panel left/width
    float sbY = 4.f, sbH = 24.f;      // panel top/height
    float nameCx[2] = {284.f, 356.f}; // team abbreviation centres (left/right of the score)
    float scoreCx = 320.f;            // "a-b" centre
    float clockCy = 17.f;             // clock baseline centre y
    std::uint32_t panelFill = 0xE0000000u, panelEdge = 0xE0FFFFFFu, textColor = 0xFFFFFFFFu;
    // Radar (exe-confirmed): x0 533, y0 67 from the float block at VA 0x55a9a8; panel w/h scale
    // with pitch size (sx = 180/length, sy = 120/width, init code storing 0x5c619c/0x5c61a0).
    float radarX = 533.f, radarY = 67.f;
    float radarW = 180.f, radarH = 120.f; // tele-view (rpit) size at full pitch; scaled by pitch dims
    float dotHalf = 4.f;                  // rpl*/rso* are 8x8 bitmaps centred on the player
    // Shirt-number label above the controlled player's head (f99 projection model, Hypothesis).
    float shirtHeadPx = 20.f;   // px above the projected head point
    float shirtSize = 14.f;     // rendered cap height in px
};

// Text layout: advances per glyph from the FFN table, space = digit advance (knob kSpaceAdvance).
std::vector<HudQuad> layoutText(const formats::FfnFont& font, std::string_view text, float x, float y,
                                float sizePx, std::uint32_t rgba);
// Centered variant: returns quads for `text` centred on (cx, cy) at cap height sizePx.
std::vector<HudQuad> layoutTextCentered(const formats::FfnFont& font, std::string_view text, float cx,
                                        float cy, float sizePx, std::uint32_t rgba);
// Laid-out width of `text` at cap height sizePx, px (pen model; used for FE label alignment).
[[nodiscard]] float textWidthPx(const formats::FfnFont& font, std::string_view text, float sizePx);

// Scoreboard strip: panel rects + team abbreviations (HudView::teamNames) + "a-b" score + clock.
// Clock semantics (DAT_0065F1C8): whole seconds elapsed in the current period -> floor/60 ':' ss.
std::vector<HudQuad> layoutScoreboard(const HudView& hud, const formats::FfnFont& font, const HudGeometry& g);

// Radar: panel bitmap (tag "rpit" tele / "rpi2" zoomed), ball marker ("ball"), player dots
// ("rpl0"/"rpl1", first 11 of each side), controlled-player selector ring (tags rsp0..rsp4 by
// side+view). `dims` = live pitch {width x, length z} in game units.
std::vector<HudQuad> layoutRadar(const MatchSnapshot& s, const glm::vec2& dims, const HudGeometry& g);

// "MM:SS" for DAT_0065F1C8 seconds-in-period (integer minutes ':' two-digit seconds).
std::string formatClock(int clockSeconds);

} // namespace wc98::presentation
