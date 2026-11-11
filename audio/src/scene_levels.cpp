// Scene sound-level layer implementation. Grammar per analysis/pass17s6audio/NOTES.md:
// FUN_00490728 init (table[0..5] = 100, gated by one-time flag DAT_00632774), FUN_004906a4
// Q7 gain with clamp-to-1, FUN_004906d0 signed scaled multiply, FUN_0049067c setter +
// notifier dispatch over DAT_004904b0[1..5].
#include "wc98/audio/scene_levels.hpp"

namespace wc98::audio {

void SceneLevels::reset() {
    // exe: do { *(u32*)&DAT_00632778[i] = 100 } while (i += 4 != 0x18) — six entries.
    for (int i = 0; i < kSceneChannels; ++i) table_[i] = 100u;
}

// FUN_0049067c: table[idx] = value; notifier = DAT_004904b0[idx]; if (notifier) notifier(arg).
void SceneLevels::setLevel(int channel, int value) {
    table_[channel] = static_cast<std::uint32_t>(value);
    if (const Notifier n = notifier_[channel]) n(channel);
}

// FUN_004906a4: q7 = (table[idx] * 0x7f) / 100; if (q7 < 1) q7 = 1.
int SceneLevels::gainQ7(int channel) const {
    int q7 = static_cast<int>((table_[channel] * 0x7fu) / 100u); // (pct*127)/100, exact for pct<=100
    return q7 < 1 ? 1 : q7;
}

// FUN_004906d0: (value * table[idx] * 0x7f) / 100 with the exe's signed 32-bit arithmetic
// (unsigned intermediate for defined overflow, then signed divide by 100).
std::int32_t SceneLevels::scaled(int channel, std::int32_t value) const {
    const std::int32_t prod = static_cast<std::int32_t>(static_cast<std::uint32_t>(value) * table_[channel] * 0x7fu);
    return prod / 100;
}

SceneLevels& sceneLevels() {
    static SceneLevels g; // the exe layer is one global table + notifier pair
    return g;
}

} // namespace wc98::audio
