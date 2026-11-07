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
#include <cerrno>
#include <cstring>
#include <format>

#include <unistd.h>

using namespace wc98;
namespace fs = std::filesystem;

namespace {
// libstdc++ <print> needs GCC 14; the CI floor (Ubuntu 24.04 gcc-13) has full <format>.
inline void printStr(std::string_view s) { std::fwrite(s.data(), 1, s.size(), stdout); }
template <class... A> void println(std::string_view f, A&&... a) {
    printStr(std::vformat(f, std::make_format_args(a...)));
    std::fputc('\n', stdout);
}

int selfTest() {
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        println("[{}] {}", ok ? "PASS" : "FAIL", what);
        failures += !ok;
    };
    auto p = launcher::paths();
    println("wc98-launcher {} self-test", WC98_VERSION);

    // 1. Settings: the user's file parses; a fully populated model survives serialize/parse and an
    //    atomic save/load next to it without warnings.
    config::Settings user;
    try {
        auto r = config::load(p.settings);
        user = r.settings;
        check(true, std::format("load {} ({}, {} warning(s))", p.settings.string(),
                                r.existed ? "exists" : "absent, defaults", r.warnings.size()));
        for (auto& w : r.warnings) println("       {}", w);
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
    for (auto& d : found) println("       {} [{}]", d.path.string(), assets::layoutName(d.layout));
    if (candidates.empty())
        println(
            "       (no game path configured and WC98_TREE unset: detection of a real install not exercised)");

    // 4. Environment report (informational).
    printStr(launcher::diagnostics());
    println("{}", failures ? std::format("self-test FAILED ({} failure(s))", failures)
                           : std::string("self-test passed"));
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    QString page, shots;
    auto consume = [&](int& i, int n) {  // drop consumed launcher flags so Qt/the game never see them
        for (int k = i - n + 1; ; ++k)
            if ((argv[k] = argv[k + n]) == nullptr) break;  // shifts tail incl. sentinel
        argc -= n;
        --i;
    };
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--self-test") return selfTest();
        if (a == "--version") return println("wc98-launcher {}", WC98_VERSION), 0;
        if (a == "--page" && i + 1 < argc) {
            page = argv[++i];
            consume(i, 2);
        } else if (a == "--screenshot-pages" && i + 1 < argc) {
            shots = argv[++i];
            consume(i, 2);
        }
        else if (a == "-h" || a == "--help") {
            println("usage: wc98-launcher [--page NAME] [--self-test] [--screenshot-pages DIR] [--version]");
            return 0;
        }
        else if (!a.empty() && a.front() == '-') {
            // AppImage single-entry contract (README): unknown flags pass through to the game,
            // e.g. `wc98-native-*.AppImage --smoke 3` -> wc98-native --smoke 3.
            if (auto game = launcher::findGame(); !game.empty()) {
                std::vector<char*> pass{const_cast<char*>(game.c_str())};  // argv[0] = program name
                for (int k = i; k < argc; ++k) pass.push_back(argv[k]);
                pass.push_back(nullptr);
                execv(game.c_str(), pass.data());  // returns only on failure
                println("wc98-launcher: cannot exec {}: {}", game.string(), std::strerror(errno));
                return 1;
            }
            println("wc98-launcher: unknown option {} and no game executable found", a);
            return 2;
        }
    }
    // Qt reads qt.conf relative to the *invoked* binary; an AppImage starts through a root-level
    // `AppRun` symlink, so usr/bin/qt.conf is invisible and the platform-plugin search path comes
    // out empty -> "Could not find the Qt platform plugin "xcb" in """. Seed the path from the
    // symlink-resolved /proc/self/exe before QApplication reads it.
    {
        std::error_code ec;
        auto exeDir = fs::read_symlink("/proc/self/exe", ec).parent_path();
        for (const auto& candidate: {exeDir / "plugins", exeDir.parent_path() / "plugins"}) {
            if (!fs::is_directory(candidate, ec)) continue;
            const std::string dir = candidate.string();
            QByteArray joined = qgetenv("QT_PLUGIN_PATH");
            if (!joined.isEmpty()) joined += ':';
            joined += QByteArray::fromStdString(dir);
            qputenv("QT_PLUGIN_PATH", joined);
            break;
        }
    }
    QApplication app(argc, argv); // consumes Qt's own options (-platform/-style pairs), shrinking argc
    if (argc > 1) {              // AppRun path: linuxdeploy's AppRun pre-consumes -s/-style pairs,
        auto game = launcher::findGame();  // e.g. `AppImage --smoke 3` arrives as Qt-stripped argv.
        if (!game.empty()) {
            std::vector<char*> pass(argv + 1, argv + argc);
            pass.push_back(nullptr);
            execv(game.c_str(), pass.data());  // returns only on failure
            println("wc98-launcher: cannot exec {}: {}", game.string(), std::strerror(errno));
            return 1;
        }
        println("wc98-launcher: leftover arguments and no game executable found");
        return 2;
    }
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
            if (!w.grab().save(file)) return fprintf(stderr, "cannot write %s\n", file.toStdString().c_str()), 1;
            println("{}", file.toStdString());
        }
        return 0;
    }
    return app.exec();
}
