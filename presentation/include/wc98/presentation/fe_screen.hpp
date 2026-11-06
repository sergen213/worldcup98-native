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
#include <optional>
#include <string>
#include <vector>
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
    FeText(render::Renderer& r, std::filesystem::path tree);

    [[nodiscard]] bool ready() const { return tex_ != 0 && strings_.size() > 1; } // index 0 is the FCLG magic
    [[nodiscard]] render::TextureId texture() const { return tex_; }
    [[nodiscard]] const formats::FfnFont* font() const { return font_ ? &*font_ : nullptr; }
    // Resolved label for one object's TEXTID key ("" when absent/unresolvable). The string bytes
    // are cp1252, matching FFN glyph codes; layoutText feeds them straight through.
    [[nodiscard]] std::string label(const formats::LayoutObject& o) const;

private:
    std::filesystem::path tree_;
    render::TextureId tex_ = 0;
    std::optional<formats::FfnFont> font_;
    std::vector<std::string> strings_;            // FC*.BIN, index = textid value
    std::optional<formats::FemsgTable> femsg_;    // name -> value (TEXTID "SM_..." form)
};

// Elements drawn, and why the rest were skipped. Text objects with an unresolvable TEXTID keep
// counting as noArt; `texts` counts glyph quads drawn through FeText.
struct FeDrawStats {
    int shapes = 0, rects = 0, hidden = 0, noArt = 0, missing = 0;
    int texts = 0; // glyph quads drawn for text objects (see FeText)
};

// Draws the merged element list into `ui`. Textured elements are blitted at their shape's native size
// anchored at (xposition, yposition): the layout position wins and WIDTH/HEIGHT is the interaction box
// (FIFA 99 LAYOUT.md "FSH header position fields are authoring positions only"; measured: nvicons
// 'qui1' is 51x55 in a 55x50 box, aflag '0001' is 193x100 in a 322x106 box). Elements with no art file
// (fname null/RECT) fall back to their flat colour key as a filled or 1-pixel-outlined box.
// Text labels: when `text` is ready, objects with a resolvable TEXTID key draw glyph quads at
// their position (see LAY_COMPOSITOR_NOTES.md "Text objects"); unresolvable ones keep counting
// as noArt. nullptr keeps the pre-text behaviour exactly (reference compositor).
FeDrawStats composite(const formats::ScreenLayout& screen, render::SpriteBatch& ui, ScreenArt& art,
                      FeText* text = nullptr);

// Reads a layout given as "path" or "archive.big@MEMBER.LAY"; `dir` receives the directory that
// authored vpath references are relative to (the archive's own directory for members).
std::string readLayout(const std::string& spec, std::filesystem::path& dir);

} // namespace wc98::presentation
