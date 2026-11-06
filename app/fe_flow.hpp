#pragma once
// Front-end flow (M4/M9 slice): the windowed shell's screen state machine. Loads FE/PACK/
// LAYOUTS/LAYOUTS.BIG through the tolerant BIG reader, composites the main-menu screen
// (FEFLOW05) with presentation::composite, and drives menu selection + entry points.
// ponytail: minimal by design — a data-driven FE graph (LAY button objects -> command ids,
// FC*.BIN text) is an open item in LAY_COMPOSITOR_NOTES.md; promote this table when it lands.
#include "wc98/formats/layout.hpp"
#include "wc98/presentation/fe_screen.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace wc98::render {
class Renderer;
class SpriteBatch;
} // namespace wc98::render

namespace wc98::app {

// FE menu entries the shell can launch. Index = selection state.
enum class FeEntry : int { Exhibition = 0, Tournament = 1, Count };
constexpr int kFeEntryCount = int(FeEntry::Count);

class FeFlow {
public:
    // tree = extracted game tree (FE/PACK/LAYOUTS/LAYOUTS.BIG + art under it).
    FeFlow(render::Renderer& r, std::filesystem::path tree);

    // Composites the current screen into `ui` and returns draw stats (shapes > 0 = real art drew).
    presentation::FeDrawStats drawCurrent(render::SpriteBatch& ui) const;
    [[nodiscard]] const std::string& lastError() const { return lastError_; } // menu load failure reason ("" = ok)

    void move(int delta);              // arrow keys: cycle menu selection
    [[nodiscard]] int selected() const { return sel_; }

private:
    std::filesystem::path tree_;
    presentation::ScreenArt art_; // texture handles cached per FSH member
    std::string lastError_; // set when LAYOUTS.BIG@FEFLOW05.LAY cannot be loaded
    formats::ScreenLayout screen_;     // main-menu layout (FEFLOW05)
    bool haveScreen_ = false;
    int sel_ = 0;
};

} // namespace wc98::app
