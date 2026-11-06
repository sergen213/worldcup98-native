// FE flow implementation — see fe_flow.hpp for the design notes.
#include "fe_flow.hpp"

#include "wc98/renderer/renderer.hpp"

namespace wc98::app {
namespace fs = std::filesystem;
using presentation::FeDrawStats;
using presentation::ScreenArt;

FeFlow::FeFlow(render::Renderer& r, fs::path tree) : tree_(std::move(tree)), art_(r, tree_, tree_ / "FE/PACK/LAYOUTS") {
    // Main menu screen (LAY_COMPOSITOR_NOTES.md measured table: FEFLOW05 -> 26 objects -> 11 shapes).
    std::string text;
    try {
        fs::path layDir;
        text = presentation::readLayout((tree_ / "FE/PACK/LAYOUTS/LAYOUTS.BIG").string() + "@FEFLOW05.LAY", layDir);
        const auto doc = formats::LayoutDocument::parse(std::string_view(text));
        screen_ = formats::ScreenLayout::build("FEFLOW05", doc, nullptr, nullptr, "PC");
        haveScreen_ = true;
    } catch (const std::exception& e) {
        // Menu art absent: the shell still navigates; drawCurrent reports shapes == 0.
        lastError_ = e.what();
    }
}

FeDrawStats FeFlow::drawCurrent(render::SpriteBatch& ui) const {
    FeDrawStats st;
    if (haveScreen_) st = presentation::composite(screen_, ui, const_cast<ScreenArt&>(art_));
    // Selection marker: two 20x20 flat panels, filled when selected. Text labels need the FFN/FC*.BIN
    // slice (LAY_COMPOSITOR_NOTES.md open item); ponytail: boxes until that lands.
    for (int i = 0; i < kFeEntryCount; ++i) {
        const float y = 380.f + 28.f * i;
        if (i == sel_) ui.draw(0, {36.f, y, 18.f, 18.f}, {}, render::packRgba(90, 200, 255, 255));
        else { // 1-px outline
            const std::uint32_t w = render::packRgba(255, 255, 255, 220);
            ui.draw(0, {36.f, y, 18.f, 1}, {}, w), ui.draw(0, {36.f, y + 17.f, 18.f, 1}, {}, w);
            ui.draw(0, {36.f, y, 1.f, 18.f}, {}, w), ui.draw(0, {53.f, y, 1.f, 18.f}, {}, w);
        }
    }
    return st;
}

void FeFlow::move(int delta) {
    sel_ = (sel_ + delta) % kFeEntryCount;
    if (sel_ < 0) sel_ += kFeEntryCount;
}

} // namespace wc98::app
