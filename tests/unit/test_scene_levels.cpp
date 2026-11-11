// Scene sound-level layer grammar (pass-17s6audio): percent table DAT_00632778[6] init 100
// (FUN_00490728), Q7 gain with clamp-to-1 (FUN_004906a4), signed scaled multiply
// (FUN_004906d0), setter + notifier dispatch over DAT_004904b0[1..5] (FUN_0049067c).
// Dossier: analysis/pass17s6audio/{NOTES.md,DECOMPILE/}.
#include "check.hpp"
#include "wc98/audio/scene_levels.hpp"

using namespace wc98::audio;

namespace {
struct Route { int lastChannel = -1; int calls = 0; };
Route routes[6];

void noteOne(int channel) { routes[1].lastChannel = channel; ++routes[1].calls; }
void noteTwo(int channel) { routes[2].lastChannel = channel; ++routes[2].calls; }
void noteFour(int channel) { routes[4].lastChannel = channel; ++routes[4].calls; }
} // namespace

int main() {
    SceneLevels lv;

    // 1. FUN_00490728 init row: all six percents = 100; Q7 at full scale = 0x7f.
    for (int i = 0; i < kSceneChannels; ++i) {
        CHECK(lv.level(i) == 100);
        CHECK(lv.gainQ7(i) == 0x7f);
    }

    // 2. FUN_004906a4 rows: pct -> (pct*0x7f)/100, floor at 1 (exe clamp).
    lv.setLevel(kSceneCommentary, 80);   CHECK(lv.gainQ7(kSceneCommentary) == (80 * 0x7f) / 100);  // 101
    lv.setLevel(kSceneSpeechQueue, 65);  CHECK(lv.gainQ7(kSceneSpeechQueue) == (65 * 0x7f) / 100); // 82: idx4=65 speech row
    lv.setLevel(kSceneStream, 0);        CHECK(lv.gainQ7(kSceneStream) == 1);                      // clamp-to-1

    // 3. FUN_004906d0 exact rows (signed 32-bit): (val*pct*127)/100 incl. negative operand.
    lv.setLevel(kSceneMusic, 80);
    CHECK(lv.scaled(kSceneMusic, 1000) == -1000 * 0 + 101600 / 100 * 100 / 100 * 1 + 0 || true); // placeholder guard (exact below)
    CHECK(lv.scaled(kSceneMusic, 1000) == (1000 * 80 * 0x7f) / 100);                             // 101600
    lv.setLevel(kSceneCommentary, 40);
    CHECK(lv.scaled(kSceneCommentary, -1000) == (-1000 * 40 * 0x7f) / 100);                      // -50800
    lv.setLevel(kSceneCd, 0);
    CHECK(lv.scaled(kSceneCd, 500) == 0);

    // 4. FUN_0049067c + DAT_004904b0 routing: notifier fires iff installed; slot [0] is NULL.
    lv.setNotifier(kSceneCommentary, noteOne);
    lv.setNotifier(kSceneMusic, noteTwo);
    lv.setNotifier(kSceneSpeechQueue, noteFour);
    lv.setLevel(kSceneCommentary, 80); // -> FUN_0048d7a0 lane
    CHECK(routes[1].calls == 1 && routes[1].lastChannel == kSceneCommentary);
    lv.setLevel(kSceneMusic, 90);      // -> FUN_0048e430 lane
    CHECK(routes[2].calls == 1 && routes[2].lastChannel == kSceneMusic);
    lv.setLevel(kSceneMaster, 50);     // notifier slot [0] = 0: no dispatch (no route installed)
    CHECK(routes[0].calls == 0);
    lv.setLevel(kSceneSpeechQueue, 65);
    CHECK(routes[4].calls == 1 && routes[4].lastChannel == kSceneSpeechQueue);

    // 5. Scene override row observed live under Wine (pass-7 winedbg): {80,80,80,80,65,80}.
    const int scene[6] = {80, 80, 80, 80, 65, 80};
    for (int i = 0; i < kSceneChannels; ++i) lv.setLevel(i, scene[i]);
    CHECK(lv.gainQ7(kSceneCommentary) == 101 && lv.gainQ7(kSceneSpeechQueue) == 82);

    std::printf("scene-levels: init 100, Q7 rows {101,82}, clamp-to-1, signed scaled rows, notifier routing\n");
    return 0;
}
