#pragma once
// Runtime audio wiring for the shell: front-end menu music (.ASF SCHl streams) and live-match
// crowd ambience + whistle/kick SFX (BNK slots), driven off the sim event seam.
// RE grounding: docs/reverse-engineering/AUDIO_RUNTIME.md §Menu/SFX paths; evidence analysis/pass8/.
#include "wc98/audio/mixer.hpp" // SoundId + Mixer (OpenAL soft; one per process)
#include "wc98/formats/crd.hpp"
#include "wc98/formats/schl.hpp"
#include "wc98/sim/events.hpp"

#include <array>
#include <cstdint>
#include <algorithm>
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

// Crowd reaction envelope (pass-55): the cmn\scrowd.c reaction machine behind the stadium
// lane. SNDcrowdload (FUN_004e4a80) derives the blend weights from the card header byte 7:
// be = (byte7 << 9) / 100, bc = 0x200 - be (CROWD 70 -> 358/154). The per-frame idle pump
// (FUN_00510c) walks a 16.16 level c0 through alternating ramp states (exe DAT_005894ab:
// case 0 up, 1 down, 2 up, 3 down, then 2 <-> 3 forever): when a ramp lands on its target
// it draws a new random target clamped to [0, 0x1ff] and divides the remaining distance by
// a ramp frame count read from the card records (byte 12 u32; exe frames = rate * 0x21 >>
// 16, measured 132/198/264 frames - ~2..4 s ramps at 60 fps). The consumer (FUN_005444)
// computes every frame the control blend cc = (be * be + bc * (c0 >> 16)) >> 9 and indexes
// the card curve tables at [cc >> 16] - CROWD lands in the blend formula's algebraic band
// [250, 404] (be = 358, bc = 154, level in [0, 0x1ff]), so the bed gain breathes instead
// of sitting at one fixed level. SNDcrowdcontrol
// (FUN_004e4d8c) keeps the percent-lane controller level in the low half of cc (pass-49/52
// options gate); the envelope drives the curve index. SNDcrowdpulse (FUN_004e4cac) raises
// the target by a card record's delta, clamped at the record ceiling, ramping at the
// record step. Random target draws are an approximation [H] of the exe 128-bit PRNG
// (FUN_004c321c state) the way the chant variant draw is modeled.
class CrowdEnvelope {
public:
    // cardByte7 = header weight; rateFrames = idle ramp frames (exe record byte 12 formula).
    void init(std::uint32_t cardByte7, int rateFrames); // fresh machine (c0 = level 0)
    void tick();                        // per-frame idle pump (exact state machine)
    // SNDcrowdpulse record lane: delta/ceiling/step from the card record (CrdRecord).
    // Returns true when the pulse was applied (exe FUN_004e4cac status: success vs -8
    // "level already at/above ceiling").
    bool pulse(const formats::CrdRecord& rec);
    [[nodiscard]] int blendIndex() const; // curve index cc >> 16 (exe control blend)
    [[nodiscard]] std::int32_t level() const { return c0_; } // 16.16 envelope level
    [[nodiscard]] int be() const { return be_; }             // blend weights (gates)
    [[nodiscard]] int bc() const { return bc_; }
private:
    int targetLevel();  // random walk target (clamped 0..0x1ff)
    int rampFrames() { return std::max(1, int(rng_() % std::uint32_t(std::max(2, rateFrames_ * 2)))) + 1; }

    int be_ = 0, bc_ = 0x200;       // blend weights (exe DAT_005894b8.hi / DAT_005894bc)
    std::int32_t c0_ = 0;           // envelope level, 16.16 (DAT_005894c0)
    std::int32_t c4_ = 0;           // ramp target, 16.16 (DAT_005894c4)
    std::int32_t c8_ = 0;           // ramp step per frame (DAT_005894c8)
    int ab_ = 0;                    // ramp state (DAT_005894ab)
    int rateFrames_ = 132;          // idle ramp frames, record 0 rate at install (FUN_00510c)
    std::mt19937 rng_{0x1998u};
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

    // Crowd envelope seam (pass-55): the reaction machine pumps every frame while the
    // stadium lane runs (exe SMIX update chain driving FUN_00510c idle walk + FUN_005444
    // blend consumer). Re-fades the running bed/traffic voices when the curve index moves
    // (the exe consumer only rewrites voice bytes on change). Call per frame next to
    // pumpChants() (livematch seam).
    void tickCrowd();
    // SNDcrowdpulse lane (FUN_004e4cac): raise the crowd envelope target by the card
    // record's delta (clamped at the record ceiling). Event -> record-index mapping is
    // undecoded (runtime vtable dispatch, AUDIO_DISCREPANCIES A4); the API is exercised by
    // the gates, not wired to sim events.
    bool crowdPulse(std::size_t recordIdx); // SNDcrowdpulse lane; true when applied
    [[nodiscard]] int crowdEnvelopeIndex() const { return crowdEnvelope_.blendIndex(); }
    [[nodiscard]] std::int32_t crowdEnvelopeLevel() const { return crowdEnvelope_.level(); }

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
    // Phrase records registered in the exe boot registry (FUN_0048d198 -> FUN_0048d114: each
    // loaded bank registers its parse-count phrases while the preload gate DAT_0063241c is
    // set; playback picks them through the voice pump stamp _DAT_006326bc = u16@phrase+0x12).
    std::uint32_t nameSlotsRegistered() const { return nameSlotsRegistered_; }
    // Scene-10 CRWD bank table (pass-57 disassembly of FUN_0048e280): DAT_006325d0 slots
    // +0 = CMIDI.BNK (bank-table create FUN_004e3128 @0x48e6f6, EDX = handle loaded from
    // literal @0x566adc), +4 = CROWD.PBK, +8 = COMOS.PBK, names from pointer table
    // DAT_0057eb44 (0x566ab4/0x566ac0) in the do-while loop at 0x48e283. The phrase-data
    // arena is reserved under tag "SNDCRD" (@0x566ae8) via FUN_0049f5a0 sized
    // phraseCount(CROWD.PBK)*3*4 (LEA ESI,[EAX*4]; SUB; SHL 2 at 0x48e73d-0x48e746).
    static constexpr std::array<const char*, 3> kSceneCrowdSlots = {
        "CMIDI.BNK", "CROWD.PBK", "COMOS.PBK"};
    std::uint32_t crowdCardSlots() const { return crowdCardSlots_; } // CROWD.CRD slot list size

    // Exe request RNG (pass-56): FUN_004ce2f3 — LCG state = state*0x41c64e6d + 0x3039,
    // draw = (state >> 16) & 0x7fff. The exe draws it for queue-index picks
    // (FUN_004ce2f3 -> FUN_004c4f30 qfetchindex in boot classification FUN_0048dcd8)
    // and for the pump drain rotation parity (FUN_0048da50 drain branch).
    // FUN_004ce317 seeds the state; runtime seed source not recovered [H].
    struct ExeLcg {
        std::uint32_t state = 0;
        std::uint32_t draw() {
            state = state * 0x41c64e6du + 0x3039u;
            return (state >> 16u) & 0x7fffu;
        }
    };

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
    std::uint32_t nameSlotsRegistered_ = 0; // FUN_0048d114 registration loop count
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
    ExeLcg chantRng_{0x1998u};          // variant/rotation draw (FUN_004ce2f3 LCG)
    // CROWD.CRD controller curve fallback (pass-52 RE): percent row -> SNDcrowdcontrol level
    // (pct*0x7f/100, clamped >= 1, FUN_004906a4). Bound layers carry their own measured curve;
    // this zero-floor table (t4) gates the unbound fallback layer and the presentation voices.
    std::array<std::uint8_t, 512> crowdCurve_{};
    bool crowdCurveValid_ = false;
    [[nodiscard]] float crowdCurveRatio() const; // applied percent -> curve gain 0..1
    [[nodiscard]] float crowdLayerGain(const CrowdLayer& layer) const; // slot curve gain byte/127
    // Reaction envelopes (pass-55): weights from the card byte 7, idle walk + blend. The
    // blend index feeds crowdLayerGain/trafficGain curve lookups.
    CrowdEnvelope crowdEnvelope_, trafficEnvelope_;
    std::vector<formats::CrdRecord> crowdRecords_, trafficRecords_;
    int appliedCrowdIdx_ = -1, appliedTrafficIdx_ = -1; // last faded curve index (change gate)
    void applyCrowdGains(); // re-fade bed + traffic + anthem to the current blended gains
    // TRAFF.CRD controller curve (measured: slot 0 ref 0x40 -> table 0, flat 117), same
    // SNDcrowdcontrol level path as the bed; the hum runs at its curve gain.
    std::array<std::uint8_t, 512> trafficCurve_{};
    bool trafficCurveValid_ = false;
    [[nodiscard]] float trafficGain() const;
};

} // namespace wc98::app
