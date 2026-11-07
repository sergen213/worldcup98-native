#pragma once
// FE .LAY screen renderer: draws a parsed formats::ScreenLayout (shape bitmaps composited from
// resolved FSH art, flat colour panels, 1-px frame rects) into a render::SpriteBatch.
// Ported verbatim from the tools/render-demo/main.cpp compositor (M4 slice 2); the grammar stays
// in formats/layout.* — this layer only resolves art and emits quads. Element model, placement
// semantics and measured evidence: docs/reverse-engineering/LAY_COMPOSITOR_NOTES.md.
#include "wc98/formats/layout.hpp"
#include "wc98/formats/fcdb.hpp"
#include "wc98/formats/ffn.hpp"
#include "wc98/formats/fsh.hpp"
#include "wc98/renderer/renderer.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <span>
namespace wc98::presentation {

// ".LAY" texture paths use Windows separators; base-name lookups are case-insensitive.
std::string slashes(std::string_view s);

// "HFFFFFFFF" / "0x80202020": the rectangle colour keys are ARGB hex (FIFA 99 parses them at 0x516724).
std::uint32_t argbOf(std::string_view v);

// Resolves the texture references of one screen's elements to renderer textures. Art is looked up by
// base name under the user's tree: loose .FSH (FE/GLOBAL/…) or packed as a RefPack-wrapped SHPI .PAK
// (FE/PACK/<family>/<name>.pak), which is what the EXE pack redirector at 0x4798dc produces.
class ScreenArt {
public:
    struct Shape {
        render::TextureId tex = 0; // 0 = unresolved
        std::uint32_t w = 0, h = 0;
    };

    ScreenArt(render::Renderer& renderer, std::filesystem::path root, std::filesystem::path layDir)
        : r_(renderer), root_(std::move(root)), layDir_(std::move(layDir)) {}

    // `tag` is a 4-char SHPI entry name (shapeID/sname); an empty tag falls back to entry `index`.
    [[nodiscard]] Shape shape(const std::string& vpath, const std::string& fname, const std::string& tag, int index);
    // "file:tag" references that produced no bitmap, in first-seen order.
    [[nodiscard]] const std::vector<std::string>& missing() const { return missing_; }

private:
    [[nodiscard]] const formats::FshFile* file(const std::string& vpath, const std::string& fname);
    void indexTree();

    render::Renderer& r_;
    std::filesystem::path root_, layDir_;
    std::map<std::string, std::shared_ptr<const formats::FshFile>> files_; // lower(vpath/fname)
    std::map<std::string, Shape> shapes_;                                  // file key + '#' + tag
    std::map<std::string, std::vector<std::filesystem::path>> byStem_;     // lower base name, built lazily
    bool indexed_ = false;
    std::vector<std::string> missing_;
};

// Text resources shared by FE screens (one per app session): the FFN face eras14bi.ffn from
// FE/PACK/IGFONTS.BIG (the HUD-proven face; fontID -> FEFONTS.BIG family mapping is unmeasured)
// plus FC<lang>.BIN ("FCLG") UI strings and the FEMSG symbol table. Loads are non-throwing:
// a tree without the assets leaves ready() false and composite keeps its noArt counting.
class FeText {
public:
    // `lang` is an FC*.BIN code (eng, fre, ger, ita, spa, dut, dan, swe, jap); falls back to eng.
    FeText(render::Renderer& r, std::filesystem::path tree, std::string lang = "eng");

    [[nodiscard]] bool ready() const { return tex_ != 0 && strings_.size() > 1; } // index 0 is the FCLG magic
    [[nodiscard]] render::TextureId texture() const { return tex_; }
    [[nodiscard]] const formats::FfnFont* font() const { return font_ ? &*font_ : nullptr; }
    // FontID -> face: exe font pointer table DAT_0057d2c0 (raw 0x17b8c0), 11 entries; the loader
    // loop iterates ids 0..10 opening "%s.ffn" members of FE/PACK/FEFONTS.BIG + IGFONTS.BIG
    // (decompiled loader: code VA 0x46933a `mov edi,[eax+057d2c0]` feeds FUN_0045ca68; the loop
    // body at 0x46936f prints "lp %2d"). Slot 9 is NULL in the exe table. face() null = id not
    // present on this tree (drawLabel then uses font()).
    static constexpr std::array<const char*, 11> kFontNames = {
        "eras10", "eras12", "eras20", "eras36", "eras11i", "eras8i",
        "eras11bi", "eras14bi", "eras9bi", "", "eras20bi"};
    [[nodiscard]] const formats::FfnFont* face(int fontId) const;
    [[nodiscard]] render::TextureId faceTexture(int fontId) const; // lazy; 0 = face absent (use texture())
    [[nodiscard]] std::size_t facesLoaded() const { return facesCount_; }
    [[nodiscard]] std::string_view language() const { return lang_; }
    // Resolved label for one object's TEXTID key ("" when absent/unresolvable). The string bytes
    // are cp1252, matching FFN glyph codes; layoutText feeds them straight through.
    [[nodiscard]] std::string label(const formats::LayoutObject& o) const;
    // Direct FC-bank access for engine-driven row captions (help pane `help=NNN,...` leading index,
    // list rows, value strings). "" when the index is out of range.
    [[nodiscard]] std::string_view stringAt(std::size_t idx) const { return idx < strings_.size() ? std::string_view{strings_[idx]} : std::string_view{}; }

private:
    std::filesystem::path tree_;
    render::Renderer* r_ = nullptr; // lazy texture creation target
    render::TextureId tex_ = 0;
    std::optional<formats::FfnFont> font_;
    std::vector<std::unique_ptr<formats::FfnFont>> faces_; // parallel to kFontNames (null = absent)
    mutable std::array<render::TextureId, 11> faceTex_{};
    std::size_t facesCount_ = 0;
    std::string lang_;                                    // FC*.BIN code in use
    std::vector<std::string> strings_;            // FC*.BIN, index = textid value
    std::optional<formats::FemsgTable> femsg_;    // name -> value (TEXTID "SM_..." form)
};

// Elements drawn, and why the rest were skipped. Text objects with an unresolvable TEXTID keep
// counting as noArt; `texts` counts glyph quads drawn through FeText.
struct FeDrawStats {
    int shapes = 0, rects = 0, hidden = 0, noArt = 0, missing = 0;
    int texts = 0; // glyph quads drawn for text objects (see FeText)
    int cursors = 0; // mouse cursor sprites drawn after the composite (see FeCursor)
};

// FE mouse cursor (MAUS.PAK/MAUS.FSH). The engine hard-loads ".\fe\pack\maus.pak" — string
// block at FIFAWC.EXE file offset 1451894 (VPATH/FNAME/SHAPEID keys adjacent: LAY-style object,
// no .LAY references it). SHPI carries two `curs` entries; entry 0 is the 28x26 arrow.
struct FeCursor {
    render::Image bmp;              // first curs bitmap RGBA (alpha channel has transparent pixels)
    std::int16_t hotX = 0, hotY = 0; // FSH header centerX/centerY (measured: 0,0 for both entries)
    int entries = 0;                // number of decoded `curs` entries in the file (expected: 2)
};
// Non-throwing: locates FE/PACK/MAUS.PAK then FE/MAUS.FSH case-insensitively, decodes through
// RefPack. False = art absent/undecodable on this tree; `out` stays empty. Paletted bitmaps
// resolve via the shared FE palette (formats::setGlobalPalette), as loaded by app main.cpp.
bool loadFeCursor(const std::filesystem::path& root, FeCursor& out);
// Pointer-driven widget state for the composite (LAY button classes objecttype 6 ScrollBar /
// 32 Tbb, e.g. Feflow06.lay's hub buttons and side icons). The engine tracks hover/press per
// widget and repaints with HILITEDSHAPE*/PUSHEDSHAPE* keys; FRAMES="a,b,c,d" gives two frame
// ranges: idle animation (a..b) and state animation (c..d) over the shape-name family
// (nvicons qui1 + frames 6,9 -> animated qui6..qui9; franim 0000 + frames 1,15 -> 0001..0015).
enum class FeWidgetState { Normal, Hover, Press };
struct FeUiState {
    std::string hover;        // objectname under the pointer ("" = none)
    std::string pressed;      // objectname held down by the mouse button
    float mx = 0.f, my = 0.f; // pointer position in the 640x480 UI space (hit-test fallback)
    std::uint32_t animMs = 0; // frame clock driving FRAMES= ranges (150 ms per frame)
    // Modes the port does not simulate render with DISABLEDSHAPEID/DISABLEDSHAPE (Feflow06.lay
    // fe04/fe02 netplay icons, fe33 shootout, fe90 classic); input on them is a documented no-op.
    std::vector<std::string> disabled; // objectnames painting the disabled shape
};
// Hit box: POINTS= polygon when present, else the xposition/yposition/width/height rect.
[[nodiscard]] bool widgetHit(const formats::LayoutObject& o, float x, float y);

// ---- League-family widget painters (MENU_PARITY.md §4 rank 7) ---------------------------------
// Kit colour table for the FCDB kit swatches: FcdbKit1998 shirtColor1..3 / shortsColor indices
// (5-bit fields, 0..31). RGB values from the community editor fifa98edit hslCols HSV tuples
// (analysis/fifa98edit/__init__.py line 90, MIT), converted hsv->rgb; indices past the table render black.
[[nodiscard]] std::uint32_t kitColor(int idx); // packed RGBA (opaque)

// Row-label painter for kt/menu row widgets (objecttype 42/44, clickenable=1 rows;
// analysis/pass12/list_painter_notes.txt): caption centred-left inside the widget rect with the
// screen's palette text colour, filled hilite pane under `hilited`, and — when kit is given
// (tricolour=1) — three small kit-colour squares at the row start. Emits into `st`.
void paintRowLabel(render::SpriteBatch& ui, const formats::LayoutObject& row, std::string_view caption,
                   const FeText& txt, int fontId, bool hilited, const formats::FcdbKit1998* kit,
                   FeDrawStats& st);

// Slider thumb (objecttype 11 Sbar / 19 KnobBar tracks): the value position as a flat quad sized
// proportional to the track (vertical for Sbar, horizontal for KnobBar). The engine paints the
// thumb shape over BARSHAPEID art; the port draws the quad at the same ratio.
void paintSliderThumb(render::SpriteBatch& ui, const formats::LayoutObject& track, int value, int lo, int hi);
// Click/drag on a slider track: value from pointer position (analysis/pass12/widget_slice_spec.md §4).
[[nodiscard]] int sliderValueAt(const formats::LayoutObject& track, float x, float y, int lo, int hi);

// List-box painter (objecttype 9 Slider class): N row captions inside the LAY box rect at the
// ROWHEIGHT stride (FC-bank strings via FeText), hilite pane on cursorRow. The widget_slice_spec.md
// §1 requirement; league rows reuse paintRowLabel per-widget, this paints a whole box list.
void paintListRows(render::SpriteBatch& ui, const formats::LayoutObject& box, std::span<const std::string> captions,
                   int cursorRow, const FeText& txt, int fontId, FeDrawStats& st);

// Draws the merged element list into `ui`. Textured elements are blitted at their shape's native size
// anchored at (xposition, yposition): the layout position wins and WIDTH/HEIGHT is the interaction box
// (FIFA 99 LAYOUT.md "FSH header position fields are authoring positions only"; measured: nvicons
// 'qui1' is 51x55 in a 55x50 box, aflag '0001' is 193x100 in a 322x106 box). Elements with no art file
// (fname null/RECT) fall back to their flat colour key as a filled or 1-pixel-outlined box.
// Text labels: when `text` is ready, objects with a resolvable TEXTID key draw glyph quads at
// their position (see LAY_COMPOSITOR_NOTES.md "Text objects"); unresolvable ones keep counting
// as noArt. nullptr keeps the pre-text behaviour exactly (reference compositor).
// Scene-ownership visibility: the engine paints a LAY object only in the scene that owns it
// (MNMSETUP's netplay widgets are absent from the main-menu screen). `visible`, when given, is
// the whitelist of objectnames for this composite; every other object counts as hidden and is
// not painted. nullptr = draw everything (reference compositor / render-demo gate).
FeDrawStats composite(const formats::ScreenLayout& screen, render::SpriteBatch& ui, ScreenArt& art,
                      FeText* text = nullptr, const std::vector<std::string>* visible = nullptr,
                      const FeUiState* uiState = nullptr);

// Reads a layout given as "path" or "archive.big@MEMBER.LAY"; `dir` receives the directory that
// authored vpath references are relative to (the archive's own directory for members).
std::string readLayout(const std::string& spec, std::filesystem::path& dir);

} // namespace wc98::presentation
