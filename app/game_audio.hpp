#pragma once
// Runtime audio wiring for the shell: front-end menu music (.ASF SCHl streams) and live-match
// crowd ambience + whistle/kick SFX (BNK slots), driven off the sim event seam.
// RE grounding: docs/reverse-engineering/AUDIO_RUNTIME.md §Menu/SFX paths; evidence analysis/pass8/.
#include "wc98/audio/mixer.hpp" // SoundId + Mixer (OpenAL soft; one per process)
#include "wc98/formats/schl.hpp"
#include "wc98/sim/events.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace wc98::audio {
class Mixer;
}

namespace wc98::app {

// Front-end music: the exe queues AUDIO\MUSC via glob `FE*.ASF` (strings @0x566b3e, list built in
// FUN_0048ed9c, pumped per-frame by FUN_0048eb98 from the FE driver region). The port streams the
// first FE_*.ASF track as a looping stream. Returns false if no playable track was found.
bool startMenuMusic(audio::Mixer& mixer, const std::filesystem::path& tree, std::string* trackOut);

// Live-match audio: CROWD.BNK ambience loop + GAME00.BNK whistle/kick one-shots. Bank decode is
// best-effort (missing files leave the slots silent). With mixer == nullptr (no OpenAL device)
// trigger() still counts events, so drivers stay headless-safe like Commentary's null-mixer path.
class MatchAudio {
public:
    explicit MatchAudio(const std::filesystem::path& tree);
    void setMixer(audio::Mixer* mixer) { mixer_ = mixer; }

    void startCrowd(); // looped crowd ambience (CROWD.BNK slot 2)
    void trigger(const sim::SimEvent& e); // whistle on KickOff/PeriodEnd/MatchEnd, kick thud on Shot

    std::uint32_t whistles() const { return whistles_; }
    std::uint32_t kicks() const { return kicks_; }
    bool crowdStarted() const { return crowdId_ != 0; }

private:
    formats::Pcm whistle_, kick_, crowd_;
    audio::Mixer* mixer_ = nullptr;
    audio::SoundId crowdId_ = 0;
    std::uint32_t whistles_ = 0, kicks_ = 0;
};

} // namespace wc98::app
