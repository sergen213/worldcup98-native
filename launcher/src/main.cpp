// Derived from fifa99-native (same author), ported for World Cup 98
// wc98-launcher: settings, game-file setup and diagnostics for World Cup 98 Native.
//   wc98-launcher                            GUI
//   wc98-launcher --page controls            GUI opened on a page
//   wc98-launcher --self-test                headless check of settings round trip + game detection (CI)
//   wc98-launcher --screenshot-pages DIR     renders every page to DIR/NN-name.png (use QT_QPA_PLATFORM=offscreen)
#include "common.hpp"
#include "mainwindow.hpp"

#include "wc98/assets/install.hpp"
#include "wc98/config/settings.hpp"
#include "wc98/formats/options.hpp"

#include <QApplication>
#include <QDir>

#include <cstdio>
#include <cstdlib>
#include <format>
#include <print>
#include <unistd.h>

using namespace wc98;
namespace fs = std::filesystem;

namespace {

int selfTest() {
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::println("[{}] {}", ok ? "PASS" : "FAIL", what);
        failures += !ok;
    };
    auto p = launcher::paths();
    std::println("wc98-launcher {} self-test", WC98_VERSION);

    // 1. Settings: the user's file parses; a fully populated model survives serialize/parse and an
    //    atomic save/load next to it without warnings.
    config::Settings user;
    try {
        auto r = config::load(p.settings);
        user = r.settings;
        check(true, std::format("load {} ({}, {} warning(s))", p.settings.string(),
                                r.existed ? "exists" : "absent, defaults", r.warnings.size()));
        for (auto& w : r.warnings) std::println("       {}", w);
    } catch (const std::exception& e) {
        check(false, std::format("load {}: {}", p.settings.string(), e.what()));
    }
    config::Settings s = user;
    s.game.data.push_back("/self-test/World Cup 98");
    s.graphics.presentation = config::Presentation::Enhanced;
    s.graphics.aspect = config::Aspect::R16x10;
    s.graphics.width = 1920, s.graphics.height = 1200;
    s.audio.crowd = 33;
    s.gameplay = {{"stadium", "5"}, {"daynight", "1"}, {"lang", "3"}}; // [gameplay] keys, canonical lowercase
    s.bindingsFor("gamepad.selftest").actions[0] = {"dpup"};
    s.unknown.push_back({"selftest", {{"kept", "yes"}}});
    auto text = config::serialize(s);
    auto back = config::parse(text);
    check(back.warnings.empty() && config::serialize(back.settings) == text, "settings serialize/parse round trip");
    check(back.settings.gameplay.size() >= 3, "[gameplay] option keys survive the round trip");
    auto tmp = p.settings;
    tmp += std::format(".selftest-{}", ::getpid());
    try {
        config::save(tmp, s);
        auto loaded = config::load(tmp);
        check(loaded.existed && config::serialize(loaded.settings) == text,
              "atomic save + reload in " + tmp.parent_path().string());
    } catch (const std::exception& e) {
        check(false, std::string("save/reload: ") + e.what());
    }
    std::error_code ec;
    fs::remove(tmp, ec);

    // 2. OPTIONS.SAV codec round trip (synthetic block; the real file test is gated on WC98_TREE).
    {
        using namespace formats;
        std::vector<std::uint8_t> blk(kOptionsBlockSize, 0);
        for (std::size_t off : {std::size_t(0), std::size_t(4)}) {
            blk[off + 0] = 'A'; blk[off + 1] = 'F'; blk[off + 2] = 'I'; blk[off + 3] = 'F'; // AFIFAFIF save magic
        }
        blk[kOptionsLangOffset] = 3;
        auto o = OptionsSave::parse(blk);
        check(o.lang == 3, "OPTIONS.SAV magic pair accepted, language byte read");
        o.stadium = 7; o.dayNight = 1; o.halfLength = 45; o.cameraModes[2] = 6;
        o.syncToRaw();
        auto b2 = OptionsSave::parse(o.serialize());
        check(b2.stadium == 7 && b2.dayNight == 1 && b2.halfLength == 45 && b2.cameraModes[2] == 6,
              "named fields sync to raw and parse back");
    }

    // 3. Game detection: every configured path (and WC98_TREE) must be a recognised game tree.
    std::vector<fs::path> candidates = user.game.data;
    if (const char* g = std::getenv("WC98_TREE"); g && *g) candidates.emplace_back(g);
    for (auto& c : candidates) {
        auto d = assets::detect(c);
        check(d.layout == assets::Layout::GameTree,
              std::format("detect {}: {} ({})", c.string(), assets::layoutName(d.layout), d.detail));
    }
    auto found = assets::searchCommonLocations();
    check(true, std::format("search common locations: {} candidate(s)", found.size()));
    for (auto& d : found) std::println("       {} [{}]", d.path.string(), assets::layoutName(d.layout));
    if (candidates.empty())
        std::println(
            "       (no game path configured and WC98_TREE unset: detection of a real install not exercised)");

    // 4. Environment report (informational).
    std::print("{}", launcher::diagnostics());
    std::println("{}", failures ? std::format("self-test FAILED ({} failure(s))", failures)
                                : std::string("self-test passed"));
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    QString page, shots;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--self-test") return selfTest();
        if (a == "--version") return std::println("wc98-launcher {}", WC98_VERSION), 0;
        if (a == "--page" && i + 1 < argc)
            page = argv[++i];
        else if (a == "--screenshot-pages" && i + 1 < argc)
            shots = argv[++i];
        else if (a == "-h" || a == "--help") {
            std::println("usage: wc98-launcher [--page NAME] [--self-test] [--screenshot-pages DIR] [--version]");
            return 0;
        }
    }
    QApplication app(argc, argv); // also consumes Qt's own options (-platform, -style, ...)
    QApplication::setApplicationName("wc98-launcher");
    QApplication::setApplicationVersion(WC98_VERSION);
    QApplication::setDesktopFileName("wc98-native"); // packaging/linux/wc98-native.desktop
    launcher::MainWindow w;
    for (int i = 0; i < w.pageCount(); ++i)
        if (w.pageName(i).compare(page, Qt::CaseInsensitive) == 0 ||
            w.pageName(i).section(' ', 0, 0).compare(page, Qt::CaseInsensitive) == 0)
            w.showPage(i);
    w.show();
    if (!shots.isEmpty()) {
        QDir().mkpath(shots);
        for (int i = 0; i < w.pageCount(); ++i) {
            w.showPage(i);
            for (int k = 0; k < 5; ++k) QApplication::processEvents();
            auto file = QString("%1/%2-%3.png")
                            .arg(shots)
                            .arg(i, 2, 10, QChar('0'))
                            .arg(w.pageName(i).toLower().replace(' ', '-'));
            if (!w.grab().save(file)) return std::println(stderr, "cannot write {}", file.toStdString()), 1;
            std::println("{}", file.toStdString());
        }
        return 0;
    }
    return app.exec();
}
