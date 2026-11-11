// Derived from fifa99-native (same author), ported for World Cup 98
#include "game_audio.hpp"

#include "wc98/file.hpp"
#include "wc98/formats/bnk.hpp"
#include "wc98/audio/scene_levels.hpp"

#include <algorithm>
#include <memory>

namespace wc98::app {
namespace {

// SFX slot mapping (evidence analysis/pass8/audio1_strings.txt: AUDIO\SFXS\%s + game00.bnk
// @0x566b5a, loader FUN_0048eff0/owner FUN_0048f1c4; the exe plays slots through table-driven
// SNDplay sites (FUN_0051d480 -> byte (&DAT_0058f3b2)[scene*4]), so the slot numbers below are
// acoustic-verified from decoded WAVs, not constants in the exe: GAME00 slot 81 = short bright
// chirp (whistle), slot 23 = short low-pitched thud (ball strike); CROWD.BNK slot 2/3 pair is a
// continuous ~3 s crowd murmur suited to looping. Hypothesis on exact event->slot ids.
constexpr std::uint32_t kWhistleSlot = 81, kKickSlot = 23; // AUDIO/SFXS/GAME00.BNK
constexpr std::uint32_t kCrowdSlot = 2;                    // AUDIO/CRWD/CROWD.BNK

// Decode one BNK slot best-effort; silent Pcm on any miss.
formats::Pcm decodeSlot(const std::filesystem::path& file, std::uint32_t slot) {
    try {
        const auto data = readFile(file);
        const formats::Bnk bnk = formats::Bnk::parse(data);
        for (const auto& s : bnk.sounds)
            if (s.slot == slot) return formats::Bnk::decode(data, s);
    } catch (const std::exception&) { // missing tree/file/bad bank: silent slot
    }
    return {};
}

} // namespace

bool startMenuMusic(audio::Mixer& mixer, const std::filesystem::path& tree, std::string* trackOut) {
    // exe glob is FE*.ASF; port matches FE_*.ASF in the extracted tree (BE_PAU*.ASF play on the
    // mode-2 pause-screen list (FUN_0048ed9c FUN_00446624()==2 branch), not wired yet).
    std::vector<std::filesystem::path> tracks;
    const auto dir = tree / "AUDIO" / "MUSC";
    if (!std::filesystem::is_directory(dir)) return false;
    for (const auto& de : std::filesystem::directory_iterator(dir)) {
        const auto name = de.path().filename().string();
        if (name.starts_with("FE_") && name.ends_with(".ASF")) tracks.push_back(de.path());
    }
    std::ranges::sort(tracks);
    for (const auto& p : tracks) {
        try {
            auto file = std::make_shared<std::vector<std::uint8_t>>(readFile(p));
            const auto streams = formats::scanSchl(*file);
            if (streams.empty()) continue;
// Scene-level layer (FUN_004906a4): the exe applies the percent-table Q7 gain of the
// music channel on top of the stream volume; default init 100 => unity.
            mixer.stream(file, streams.front(), float(audio::sceneLevels().gainQ7(audio::kSceneMusic)) / 0x7f, /*loop=*/true);
            if (trackOut) *trackOut = p.filename().string();
            return true;
        } catch (const std::exception&) { // try the next track
        }
    }
    return false;
}

MatchAudio::MatchAudio(const std::filesystem::path& tree) {
    whistle_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kWhistleSlot);
    kick_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kKickSlot);
    crowd_ = decodeSlot(tree / "AUDIO" / "CRWD" / "CROWD.BNK", kCrowdSlot);
}

void MatchAudio::startCrowd() {
    // Scene-level layer (DAT_00632778 idx3 = stream channel, FUN_004906a4 Q7 gain) scales this
    // loop; the base level is pass-8 acoustic tuning. Chant-card + MIDx ramp layers stay a RE lane.
    if (mixer_ && crowd_.frames() && !crowdId_)
        crowdId_ = mixer_->play(crowd_, 0.6f * float(audio::sceneLevels().gainQ7(audio::kSceneStream)) / 0x7f, /*loop=*/true);
}

void MatchAudio::trigger(const sim::SimEvent& e) {
    switch (e.type) {
    case sim::EventType::KickOff:
    case sim::EventType::PeriodEnd:
    case sim::EventType::MatchEnd: // whistle chain FUN_004bedb0 -> FUN_0051d480 (match event sites)
        ++whistles_;
        if (mixer_ && whistle_.frames()) mixer_->play(whistle_, 1.0f);
        break;
    case sim::EventType::Shot: // ball strike; the exe plays it at the kick code points
        ++kicks_;
        if (mixer_ && kick_.frames()) mixer_->play(kick_, 0.8f);
        break;
    default: break;
    }
}

} // namespace wc98::app
