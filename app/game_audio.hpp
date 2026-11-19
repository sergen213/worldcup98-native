#pragma once
// Runtime audio wiring for the shell: front-end menu music (.ASF SCHl streams) and live-match
// crowd ambience + whistle/kick SFX (BNK slots), driven off the sim event seam.
// RE grounding: docs/reverse-engineering/AUDIO_RUNTIME.md §Menu/SFX paths; evidence analysis/pass8/.
#include "wc98/audio/mixer.hpp" // SoundId + Mixer (OpenAL soft; one per process)
#include "wc98/formats/schl.hpp"
#include "wc98/sim/events.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
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

    // Chant pump (pass-53): the exe's chant-lane pump FUN_0048da50. Boot enables the lane on
    // the match-scene entry (enable FUN_0048d378, site 0x49078f; disable on scene exit
    // FUN_0048d3f4 site 0x4907ee) and installs the runtime patch bank DAT_00632420
    // (SNDbnkcreate, FUN_0048d0c0 adder). Every 100 frames (counter gate 0x64, DAT_00636a14
    // >= 0x64 in the pump body) the pump drains its three request queues and, when the match
    // is live (FUN_004adb30) and the queues are empty, tops up lanes 0/1/2 while the pool
    // stays under its 3-slot cap (DAT_006324d0) and each lane under its desired cap
    // (DAT_006324e0, set to 1 by the boot classification FUN_0048dcd8). Lane assignment from
    // classification: home-team-keyed banks -> lane 0, away -> lane 1, same-team or generic
    // (CGENER) -> lane 2; neither team having banks assigns every entry lane -1 (silent).
    // Clip level comes from the percent idx2 table (FUN_0048d678 -> FUN_004906d0
    // (vol * percent[idx2] * 0x7f) / 10000), i.e. the options reaction row gates the chant.
    void pumpChants();
    std::uint32_t chantPlays() const { return chantPlays_; } // clips started (request counter)
    std::uint32_t chantActive() const { return chantActiveTotal_; } // live pool voices
    std::uint32_t chantLaneCount(int lane) const { return chantLaneCount_[std::size_t(lane)]; }
    const formats::Pcm& lastChantClip() const { return chantClip_; } // last decoded clip (gate)

    std::uint32_t whistles() const { return whistles_; }
    std::uint32_t kicks() const { return kicks_; }
    std::uint32_t goals() const { return goalRoars_; }
    bool crowdStarted() const;
    std::uint32_t crowdLayerCount() const { return std::uint32_t(crowdLayers_.size()); }
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
    void classifyChants();                   // boot lane split (FUN_0048dcd8 classification)
    bool playChantLane(int lane);            // pump top-up: load + play one clip (FUN_0048d53c)
    formats::Pcm decodeChantClip(const std::filesystem::path& path); // first non-empty phrase
    void catalogAnnouncers(std::uint16_t homeTeamId, std::uint16_t awayTeamId); // FUN_0048fa08 lane
    void catalogPlayers(const std::vector<std::uint16_t>& ids);                 // FUN_0048fb10 lane
    static void crowdNotifier(int channel); // DAT_004904b0 slot [2] -> lane re-level
    static MatchAudio* live_;               // one live match audio per process (exe global lanes)

    std::filesystem::path tree_;
    formats::Pcm whistle_, kick_;
    formats::Pcm goalRoar_;     // GAME00 slot 45: 4.1 s loud-noise burst (measured profile)
    formats::Pcm whistleTrill_; // GAME00 slot 85: 1.95 s bright sustained (period-end whistle)
    formats::Pcm traffic_;      // TRAFF.BNK slot 0 (stadium lane pair member)
    // Stadium bed layers (pass-54): the card's enabled bank slots are opened together, each
    // bound to its own curve through the measured tableRef pointer (0x128 + i*4 + ref ->
    // absolute curve address in the card tables region). Pump handler 0x4e502c reads the gain
    // byte curve[level] (bytes 0..127, scale/127) at the SNDcrowdcontrol level.
    struct CrowdLayer {
        formats::Pcm pcm;
        audio::SoundId id = 0;
        std::array<std::uint8_t, 512> curve{}; // empty == unbound (fallback gate curve)
    };
    std::vector<CrowdLayer> crowdLayers_;
    audio::Mixer* mixer_ = nullptr;
    audio::SoundId goalId_ = 0;
    audio::SoundId trafficId_ = 0;
    audio::SoundId anthemId_ = 0;
    std::uint32_t whistles_ = 0, kicks_ = 0, goalRoars_ = 0;
    std::uint32_t chantBanks_ = 0, chantPhrases_ = 0, announcerBanks_ = 0, announcerPhrases_ = 0;
    std::uint32_t plrsBanks_ = 0, plrsPhrases_ = 0;
    std::uint32_t crowdCardSlots_ = 0;
    int crowdPctApplied_ = -1;
    // Chant pool state (pass-53). Entries carry the team key (C0 + %04d team-id digits, 0 for
    // the CGENER generic lane) and the boot classification lane (-1 = dropped, matches the exe's
    // node +8 lane field). Voice slots mirror the 3-slot pump pool DAT_006324d0.
    struct ChantEntry {
        std::filesystem::path path;
        int teamKey = 0; // C0%04d team-id table key; 0 = generic (CGENER) entry
        int lane = -1;
    };
    struct ChantVoice {
        audio::SoundId id = 0;
        int lane = -1;
    };
    std::vector<ChantEntry> chantEntries_;
    std::array<ChantVoice, 3> chantVoices_{};
    std::array<std::uint32_t, 3> chantLaneCount_{}, chantLaneActive_{}, chantLaneDesired_{};
    std::uint32_t chantActiveTotal_ = 0, chantPlays_ = 0;
    std::uint64_t chantPumpTick_ = 0;   // frame counter (exe DAT_00636a14, 100-frame gate)
    int chantHomeKey_ = 0, chantAwayKey_ = 0; // match teams * 10 (classification seam)
    formats::Pcm chantClip_;            // last decoded clip (decode evidence for gates)
    std::uint32_t chantSegments_ = 0;   // decoded segment count across clips (gate evidence)
    std::mt19937 chantRng_{0x98u};      // variant draw (exe random FUN_004c3ea0)
    // CROWD.CRD controller curve fallback (pass-52 RE): percent row -> SNDcrowdcontrol level
    // (pct*0x7f/100, clamped >= 1, FUN_004906a4). Bound layers carry their own measured curve;
    // this zero-floor table (t4) gates the unbound fallback layer and the presentation voices.
    std::array<std::uint8_t, 512> crowdCurve_{};
    bool crowdCurveValid_ = false;
    [[nodiscard]] float crowdCurveRatio() const; // applied percent -> curve gain 0..1
    [[nodiscard]] float crowdLayerGain(const CrowdLayer& layer) const; // slot curve gain byte/127
    // TRAFF.CRD controller curve (measured: slot 0 ref 0x40 -> table 0, flat 117), same
    // SNDcrowdcontrol level path as the bed; the hum runs at its curve gain.
    std::array<std::uint8_t, 512> trafficCurve_{};
    bool trafficCurveValid_ = false;
    [[nodiscard]] float trafficGain() const;
};

} // namespace wc98::app
