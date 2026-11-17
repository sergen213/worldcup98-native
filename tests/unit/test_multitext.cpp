// "[multitext]" section-blob parser: synthetic grammar round trips + a WC98-tree gate over the
// 8x120 shipped .HIS cells (every cell parses; Ireland DU/EN blobs pin section counts, bufsize,
// and localized content). Grammar evidence: docs/asset-formats/TEAMHIST.md + MULTITEXT.md.
#include "check.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/his.hpp"
#include "wc98/formats/multitext.hpp"
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace wc98::formats;

static void synthetic() {
    const std::string blob =
        "[multitext]\nnumsections=2\nbufsize=966\n\n"
        "[section]\n{tabs l100 l500}\n{colour 1}B\u00f6l\u00f6ni{t}108\n"
        "[section]\n{centre}{wrap}{font 4}{colour 0}Second body";
    auto m = Multitext::parse(blob);
    CHECK(m.numsections == 2 && m.bufsize == 966);
    CHECK(m.sections.size() == 2);
    CHECK(m.sections[0].find("B\u00f6l\u00f6ni") != std::string_view::npos);
    CHECK(m.sections[1] == "\n{centre}{wrap}{font 4}{colour 0}Second body");

    const std::string hdr = "[multitext]\nnumsections=1\nbufsize=100\n\n[section]\nbody";
    CHECK_THROWS(Multitext::parse("nope"));                    // marker gate
    CHECK_THROWS(Multitext::parse("[multitext]\nnumsections=2\nbufsize=9\n\n[section]\nx")); // count mismatch
    CHECK_THROWS(Multitext::parse("[multitext]\nnumsections=0\nbufsize=9\n\n[section]\nx")); // zero sections
    CHECK_THROWS(Multitext::parse("[multitext]\nnumsections=1\nbufsize=0\n\n[section]\nx")); // zero bufsize
    CHECK_THROWS(Multitext::parse("[multitext]\nnumsections=1\nbufsize=9\n\n"));             // no marker
    CHECK_THROWS(Multitext::parse("[multitext]\nsections=1\nbufsize=9\n\n[section]\nx"));    // key gate
    CHECK_THROWS(Multitext::parse("[multitext]\nnumsections=x\nbufsize=9\n\n[section]\nx")); // digit gate
    const std::string extra = hdr + "[section]\nthird";
    CHECK_THROWS(Multitext::parse(extra));                     // 3 markers vs numsections=1
}

static void original() {
    const auto tree = treeDirOrSkip();
    static constexpr std::string_view kLangs[] = {"du", "en", "fr", "ge", "it", "da", "sp", "sw"};
    for (const auto l : kLangs) {
        const auto hisP = wc98::findCaseInsensitive(tree, "FE/TEAMHIST.ENG/" + std::string(l) + ".his");
        CHECK(!hisP.empty());
        const auto hb = wc98::readFile(hisP);
        const auto h = His::parse(hb);
        // Every cell parses as a multitext blob with its declared section count.
        std::size_t total = 0;
        for (std::size_t t = 0; t < h.nTeams; ++t)
            for (std::size_t c = 0; c < h.nCols; ++c)
                total += Multitext::parse(h.cellText(t, c)).sections.size();
        CHECK(total >= 120); // 40x3 cells, each >= 1 section

        if (l == "du") {
            const auto gen = Multitext::parse(h.cellText(2, kHisColGen));
            CHECK(gen.numsections == 1 && gen.sections.size() == 1);
            CHECK(gen.bufsize == 190); // measured Ireland DU
            CHECK(gen.sections[0].find("The Football Association") != std::string_view::npos);
            const auto triv = Multitext::parse(h.cellText(2, kHisColTriv));
            CHECK(triv.numsections == 8 && triv.sections.size() == 8);
            CHECK(triv.sections.front().find("V:") != std::string_view::npos);
            const auto greats = Multitext::parse(h.cellText(2, kHisColGreats));
            CHECK(greats.numsections == 2 && greats.sections.size() == 2);
            CHECK(greats.sections[0].find("tabs l100 l500") != std::string_view::npos);
            CHECK((greats.sections[0].find("Quinn") != std::string_view::npos ||
                   greats.sections[1].find("Quinn") != std::string_view::npos));
        }
        if (l == "en") {
            const auto gen = Multitext::parse(h.cellText(2, kHisColGen));
            CHECK(gen.sections[0].find("The Football Association") != std::string_view::npos);
            const auto greats = Multitext::parse(h.cellText(2, kHisColGreats));
            CHECK((greats.sections[0].find("Quinn") != std::string_view::npos ||
                   greats.sections[1].find("Quinn") != std::string_view::npos));
        }
    }
    std::printf("MULTITEXT: 8 languages, all 120-cell blobs parse\n");
}

int main() {
    synthetic();
    original();
    return 0;
}
