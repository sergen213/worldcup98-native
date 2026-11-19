#pragma once
// Runtime audio wiring for the shell: front-end menu music (.ASF SCHl streams) and live-match
// crowd ambience + whistle/kick SFX (BNK slots), driven off the sim event seam.
// RE grounding: docs/reverse-engineering/AUDIO_RUNTIME.md §Menu/SFX paths; evidence analysis/pass8/.
#include "wc98/audio/mixer.hpp" // SoundId + Mixer (OpenAL soft; one per process)
#include "wc98/formats/schl.hpp"
#include "wc98/sim/events.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace wc98::audio {
class Mixer;
}

namespace wc98::app {

// Front-end music: the exe queues AUDIO\MUSC via glob `FE*.ASF` (strings @0x566b3e, list built in
// FUN_0048ed9c: DAT_00632654 default list / DAT_00632670 alternate under FUN_00446624()==2, pump
// DAT_0063268c), pumped per-frame by FUN_0048eb98. The port streams the FE_*.ASF tracks one after
// another (track end -> next track), the pump's rotation. Returns false if no playable track.
bool startMenuMusic(audio::Mixer& mixer, const std::filesystem::path& tree, std::string* trackOut);

// Scene-aware FE music: owns the track rotation + the scene-transition seam. The exe ramps the
// stream volume out over 0xc8 (200) ticks when a scene leaves (FUN_0048ec64 DAT_0058c1a6 lane) and
// ramps back in on scene entry (state-cb tick + FUN_004e6aa0 autovol, consumer FUN_0048ed9c);
// update() also re-levels the live voice when the music percent row changes (SNDstreamautovol).
class MenuMusicDirector {
public:
    bool start(audio::Mixer& mixer, const std::filesystem::path& tree); // load track list + play first
    void update();     // per frame: rotation on track end, lane relevel on percent change, fade seam
    void fadeOut();    // live-match launch: ramp to 0 over 200 ticks, then stop (original seam)
    void enterScene(); // returning from a match: restart the current track ramped in
    [[nodiscard]] bool playing() const { return id_ != 0; }
    void relevel();    // options music row changed: ramp the live voice to the new lane gain
    [[nodiscard]] std::string trackName() const;

private:
    bool playCurrent(float gain);
    [[nodiscard]] float laneGain() const; // scene music percent -> SMIX lane gain

    audio::Mixer* mixer_ = nullptr;
    std::vector<std::filesystem::path> files_;
    std::shared_ptr<const std::vector<std::uint8_t>> curFile_;
    std::size_t idx_ = 0;
    audio::SoundId id_ = 0;
    float lane_ = 0.f;      // last applied lane gain (change detection)
    int fadeFrames_ = 0;    // fade-out seam countdown (frames; ~200 ticks at 60 fps)
    bool leaving_ = false;
    bool reenterPending_ = false; // enterScene() during the ramp-out: restart when the seam ends
};

// Live-match audio: CROWD.BNK ambience loop + GAME00.BNK whistle/kick one-shots. Bank decode is
// best-effort (missing files leave the slots silent). With mixer == nullptr (no OpenAL device)
// trigger() still counts events, so drivers stay headless-safe like Commentary's null-mixer path.
class MatchAudio {
public:
    explicit MatchAudio(const std::filesystem::path& tree);
    void setMixer(audio::Mixer* mixer) { mixer_ = mixer; }

    void startCrowd(); // looped crowd ambience (CROWD.BNK slot 2)
    void trigger(const sim::SimEvent& e); // whistle on KickOff/PeriodEnd/MatchEnd, kick thud on Shot
    // Crowd-bed re-level seam (pass-49): options stadium-reaction row -> percent idx2 rewrites
    // the SMIX voice lane float of the running loop (FUN_004906a4 -> voice offset 0x34), faded.
    void relevelCrowd();

    std::uint32_t whistles() const { return whistles_; }
    std::uint32_t kicks() const { return kicks_; }
    std::uint32_t goals() const { return goalRoars_; }
    bool crowdStarted() const { return crowdId_ != 0; }

private:
    formats::Pcm whistle_, kick_, crowd_;
    formats::Pcm goalRoar_;     // GAME00 slot 45: 4.1 s loud-noise burst (measured profile)
    formats::Pcm whistleTrill_; // GAME00 slot 85: 1.95 s bright sustained (period-end whistle)
    audio::Mixer* mixer_ = nullptr;
    audio::SoundId crowdId_ = 0;
    audio::SoundId goalId_ = 0;
    std::uint32_t whistles_ = 0, kicks_ = 0, goalRoars_ = 0;
    int crowdPctApplied_ = -1;
};

} // namespace wc98::app
