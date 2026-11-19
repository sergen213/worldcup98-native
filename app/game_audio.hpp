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

    void startCrowd(); // looped crowd ambience (CROWD.CRD card slots, FUN_0048e78c)
    // Match-lane boot (pass-51): the exe's match-scene builder FUN_00490728 installs the
    // DAT_004904b0 percent notifiers, starts the stadium lane pair (CROWD bed + TRAFF traffic
    // loop, FUN_0048e78c/FUN_004e3128/FUN_004e4a80), plays the presentation lane (ANTH%04d.BNK
    // slot 0 = the nation's chant sample, 23.8 s, string `AUDIO\ANTH\ANTH%04d.BNK` @0x56735f,
    // xref 0x490b0a in FUN_00490acc), and builds the match-keyed lane pools: the chant pool
    // (113 slots, FUN_0048dfc4) from the exe name table @0x57eb5c + CGENER lane, the announcer
    // banks AUDIO\TEAM\A_T%04d.PBK for both match teams (FUN_0048fa08, ids read from the match
    // record slots DAT_00641014/0x641110 +0x34), and the player banks AUDIO\PLRS\P%05d.PBK for
    // every squad id of both teams (FUN_0048fb10, 2 teams x 17 slot records). Catalogs are
    // decode-counted, not played: their event trigger mapping is undecoded
    // (docs/reverse-engineering/AUDIO_DISCREPANCIES.md).
    void boot(std::uint16_t homeTeamId, std::uint16_t awayTeamId,
              const std::vector<std::uint16_t>& homeSquadIds = {},
              const std::vector<std::uint16_t>& awaySquadIds = {});
    void trigger(const sim::SimEvent& e); // whistle on KickOff/PeriodEnd/MatchEnd, kick thud on Shot
    // Crowd-bed re-level seam (pass-49): options stadium-reaction row -> percent idx2 rewrites
    // the SMIX voice lane float of the running loop (FUN_004906a4 -> voice offset 0x34), faded.
    void relevelCrowd();

    std::uint32_t whistles() const { return whistles_; }
    std::uint32_t kicks() const { return kicks_; }
    std::uint32_t goals() const { return goalRoars_; }
    bool crowdStarted() const { return crowdId_ != 0; }
    bool trafficStarted() const { return trafficId_ != 0; }
    bool anthemStarted() const { return anthemId_ != 0; }
    std::uint32_t chantBanks() const { return chantBanks_; }    // parseable CHNT team banks
    std::uint32_t chantPhrases() const { return chantPhrases_; }
    std::uint32_t announcerBanks() const { return announcerBanks_; } // A_T/G_T banks parsed
    std::uint32_t announcerPhrases() const { return announcerPhrases_; }
    std::uint32_t plrsBanks() const { return plrsBanks_; } // PLRS player banks parsed (FUN_0048fb10)
    std::uint32_t plrsPhrases() const { return plrsPhrases_; }
    std::uint32_t crowdCardSlots() const { return crowdCardSlots_; } // CROWD.CRD slot list size

private:
    void startTraffic(); // TRAFF.BNK slot 0: 79.8 s traffic hum loop (stadium lane pair member)
    void startAnthem(std::uint16_t homeTeamId); // presentation-lane chant sample (home nation)
    void catalogChants();                    // full CHNT pool (FUN_0048dfc4, 113 slots)
    void catalogAnnouncers(std::uint16_t homeTeamId, std::uint16_t awayTeamId); // FUN_0048fa08 lane
    void catalogPlayers(const std::vector<std::uint16_t>& ids);                 // FUN_0048fb10 lane
    static void crowdNotifier(int channel); // DAT_004904b0 slot [2] -> lane re-level
    static MatchAudio* live_;               // one live match audio per process (exe global lanes)

    std::filesystem::path tree_;
    formats::Pcm whistle_, kick_, crowd_;
    formats::Pcm goalRoar_;     // GAME00 slot 45: 4.1 s loud-noise burst (measured profile)
    formats::Pcm whistleTrill_; // GAME00 slot 85: 1.95 s bright sustained (period-end whistle)
    formats::Pcm traffic_;      // TRAFF.BNK slot 0 (stadium lane pair member)
    audio::Mixer* mixer_ = nullptr;
    audio::SoundId crowdId_ = 0;
    audio::SoundId goalId_ = 0;
    audio::SoundId trafficId_ = 0;
    audio::SoundId anthemId_ = 0;
    std::uint32_t whistles_ = 0, kicks_ = 0, goalRoars_ = 0;
    std::uint32_t chantBanks_ = 0, chantPhrases_ = 0, announcerBanks_ = 0, announcerPhrases_ = 0;
    std::uint32_t plrsBanks_ = 0, plrsPhrases_ = 0;
    std::uint32_t crowdCardSlots_ = 0;
    int crowdPctApplied_ = -1;
};

} // namespace wc98::app
