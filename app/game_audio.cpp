// Derived from fifa99-native (same author), ported for World Cup 98
#include "game_audio.hpp"

#include "wc98/file.hpp"
#include "wc98/formats/bnk.hpp"
#include "wc98/formats/crd.hpp"
#include "wc98/formats/phrs.hpp"
#include "wc98/audio/smix_voice.hpp"
#include "wc98/audio/scene_levels.hpp"

#include <algorithm>
#include <format>
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
constexpr std::uint32_t kWhistleTrillSlot = 85;            // 1.95 s bright sustained (measured: zcr 9.8 kHz)
constexpr std::uint32_t kGoalRoarSlot = 45;                // 4.12 s loud-noise burst (measured profile)
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

// SMIX native mixer lane (pass-18smix, additive): the exe's DirectSound 2D voice path installs
// per-voice L/R volume ints through FUN_0054ccfe and mixes with the installer floats; scene Q7
// percents (FUN_004906a4) feed those ints. Mirror it behind the existing scene-level gains:
// each lane's play gain = base * installer float at (gainQ7(ch), gainQ7(ch)). At 100% levels
// the installer is exactly unity (int 0x7F -> 1.0f, gated in test_smix_grammar), so streams
// keep bit-identical v0.3.0 gains; lowering a scene percent drives the voice through SMIX.
float smixLaneGain(int channel) {
    audio::SmixVoice v;                    // record model of the DAT_00661068 slot band
    const int q7 = audio::sceneLevels().gainQ7(channel);
    audio::Smix_startVoice(v, 0x7F, 0x7F, static_cast<std::uint8_t>(q7)); // FUN_005483bc rows:
    audio::Smix_setPan(v, q7, q7);         // initial pair + gate byte +0x6B, ramp armed (FUN_00548a08)
    v.setVolume(q7, q7);                   // FUN_0054ccfe installer on the scene Q7 ints
    return v.gainRef().volL;               // mono voice: L-slot float gain
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
            // music channel on top of the stream volume; default init 100 => unity. The SMIX voice
            // lane (pass-18smix) supplies that gain as the FUN_0054ccfe installer float, exactly
            // unity at 100%.
            mixer.stream(file, streams.front(), smixLaneGain(audio::kSceneMusic), /*loop=*/true);
            if (trackOut) *trackOut = p.filename().string();
            return true;
        } catch (const std::exception&) { // try the next track
        }
    }
    return false;
}

MatchAudio* MatchAudio::live_ = nullptr;

MatchAudio::MatchAudio(const std::filesystem::path& tree) : tree_(tree) {
    whistle_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kWhistleSlot);
    kick_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kKickSlot);
    goalRoar_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kGoalRoarSlot);
    whistleTrill_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kWhistleTrillSlot);
    // Bed-slot selection (pass-51): the lane builder FUN_0048e78c reads the card file
    // (AUDIO/CRWD/CROWD.CRD, FUN_0047aeb0 lookup) for its .BNK slot list; CROWD.CRD slots
    // 0..3, where 0/1 decode to 0.75 s short swells and 2/3 to the 3.03 s murmur loop.
    // Take the card's slot order and pick the first slot that decodes long enough to be the
    // continuous bed (>= 2 s); slot 2 is the only candidate that way, so the audible bed is
    // unchanged, and the card is now the authority for the slot list.
    std::vector<std::uint32_t> slots{kCrowdSlot};
    try {
        const auto card = formats::Crd::parse(readFile(tree / "AUDIO" / "CRWD" / "CROWD.CRD"));
        std::vector<std::uint32_t> fromCard;
        for (const auto s : card.bankSlots)
            if (s != 0xFF) fromCard.push_back(s);
        crowdCardSlots_ = static_cast<std::uint32_t>(fromCard.size());
        if (!fromCard.empty()) slots = fromCard;
    } catch (const std::exception&) { // no card: keep the measured slot
    }
    for (const auto slot : slots) {
        auto pcm = decodeSlot(tree / "AUDIO" / "CRWD" / "CROWD.BNK", slot);
        if (pcm.frames() >= 22050u * 2u) { crowd_ = std::move(pcm); break; }
        if (!crowd_.frames() && pcm.frames()) crowd_ = std::move(pcm);
    }
    traffic_ = decodeSlot(tree / "AUDIO" / "CRWD" / "TRAFF.BNK", 0); // TRAFF.CRD slot 0 (measured)
}

void MatchAudio::crowdNotifier(int channel) {
    (void)channel;
    if (live_) live_->relevelCrowd(); // percent write -> lane re-level (FUN_0049067c dispatch)
}

void MatchAudio::boot(std::uint16_t homeTeamId, std::uint16_t awayTeamId,
                      const std::vector<std::uint16_t>& homeSquadIds,
                      const std::vector<std::uint16_t>& awaySquadIds) {
    live_ = this;
    // DAT_004904b0 percent-notifier install (FUN_0049067c dispatch table): idx2 =
    // SNDstadiumreactions re-levels the running stadium loops on an options-row write.
    audio::sceneLevels().setNotifier(audio::kSceneCrowd, &MatchAudio::crowdNotifier);
    startCrowd();
    startTraffic();
    startAnthem(homeTeamId);
    catalogChants();
    catalogAnnouncers(homeTeamId, awayTeamId);
    catalogPlayers(homeSquadIds);
    catalogPlayers(awaySquadIds);
}

void MatchAudio::startTraffic() {
    // Stadium lane pair: the lane builder's second container load is the traffic hum
    // (AUDIO/CRWD/TRAFF.BNK slot 0, 79.84 s, gated by the same percent idx2), looped by the
    // FUN_004e3128/FUN_004e4a80 start points. Measured bed RMS 1416 vs crowd bed 9111 -> mix
    // it under the crowd bed [H gain balance].
    if (mixer_ && traffic_.frames() && !trafficId_)
        trafficId_ = mixer_->play(traffic_, 0.12f * smixLaneGain(audio::kSceneCrowd), /*loop=*/true);
}

void MatchAudio::startAnthem(std::uint16_t homeTeamId) {
    // Presentation-lane nation sample: AUDIO/ANTH/ANTH%04d.BNK slot 0 = 23.8 s chant sample of
    // the team's nation (measured: ANTH0026 slot 0 decodes, 765 KB mono 22050). Played once at
    // match boot (presentation lane gain [H]).
    const auto file = tree_ / "AUDIO" / "ANTH" / std::format("ANTH{:04d}.BNK", homeTeamId);
    if (mixer_ && !anthemId_) {
        const auto anthem = decodeSlot(file, 0);
        if (anthem.frames())
            anthemId_ = mixer_->play(anthem, 0.8f * smixLaneGain(audio::kSceneCrowd), /*loop=*/false);
    }
}

void MatchAudio::catalogChants() {
    // Chant pool (pass-51): FUN_0048dfc4 allocates the 113-slot chant lane (DAT_006324c0, loop
    // `iVar1 < 0x71`) + a 3-slot side pool (DAT_006324d0). The bank names come from the exe's
    // own table at VA 0x57eb5c (`PTR_s_C0001800_PBK`): 74 team-keyed entries C0+team*10+%02d
    // variant (e.g. C0001800..C0001801, C0002600, team 47 has six variants), consumed by the
    // per-chant loader FUN_0048d53c (`AUDIO\CHNT\%s` @0x566a78, `table[req+4]` index). The
    // remaining 39 CD files are the CGENER%02d generic-chant lane (name string `GENER` in the
    // same block, absent from the team table). Boot loads all 113, so the port catalogs every
    // AUDIO/CHNT/*.PBK file; playback stays open - the event->chant trigger mapping is
    // undecoded (docs/reverse-engineering/AUDIO_DISCREPANCIES.md), and playing banks on an
    // invented schedule is exactly what the goal doc forbids.
    const auto dir = tree_ / "AUDIO" / "CHNT";
    if (!std::filesystem::is_directory(dir)) return;
    std::vector<std::filesystem::path> files;
    for (const auto& de : std::filesystem::directory_iterator(dir))
        if (de.path().filename().string().ends_with(".PBK")) files.push_back(de.path());
    std::ranges::sort(files);
    for (const auto& path : files) {
        try {
            const auto bytes = readFile(path);
            chantPhrases_ += formats::PhraseBank::parse(std::span<const std::uint8_t>{bytes})
                                 .phraseCount();
            ++chantBanks_;
        } catch (const std::exception&) { // member present but unparsable: exe skips silently
        }
    }
}

void MatchAudio::catalogAnnouncers(std::uint16_t homeTeamId, std::uint16_t awayTeamId) {
    // Announcer lane (pass-51): the match-boot installer FUN_0048fa08 loads
    // `AUDIO\TEAM\A_T%04d.PBK` (@0x566bac) once per match team, ids read from the match record
    // slots (DAT_00641014 + 0x34 / DAT_00641110 + 0x34, u16), 4 slots per team. The request
    // handler FUN_0048953c picks the G_T%04d variant (`AUDIO\team\G_T%04d.pbk` @0x56698c) when
    // DAT_0057e5f8 is set and the A_T variant (@0x566974) otherwise, plus a hardcoded
    // `AUDIO\team\A_FE_USA.SMP` neutral-English fallback. Catalog both prefixes per team.
    const std::uint16_t ids[2] = {homeTeamId, awayTeamId};
    for (const std::uint16_t id : ids) {
        for (const char* prefix : {"A_T", "G_T"}) {
            const auto path = tree_ / "AUDIO" / "TEAM" / std::format("{}{:04d}.PBK", prefix, id);
            if (!std::filesystem::exists(path)) continue;
            try {
                const auto bytes = readFile(path);
                announcerPhrases_ += formats::PhraseBank::parse(std::span<const std::uint8_t>{bytes})
                                         .phraseCount();
                ++announcerBanks_;
            } catch (const std::exception&) {
            }
        }
    }
}

void MatchAudio::catalogPlayers(const std::vector<std::uint16_t>& ids) {
    // Player-name announcer lane (pass-51): the match-boot installer FUN_0048fb10 walks both
    // team squads (2 records x 17 slot entries, stride 0x66 with 6-byte slot records over the
    // squad array @DAT_0064101c) and loads `AUDIO\PLRS\P%05d.PBK` (@0x566bc4) for every
    // non-zero player id (u16 at slot+2), one bank pool entry each. FCDB roster ids feed the
    // same %05d key; the CD ships 1020 Pxxxxx.PBK banks. Phrase decode stays counted, not
    // played - the id->phrase trigger mapping is undecoded.
    for (const std::uint16_t id : ids) {
        if (!id) continue;
        const auto path = tree_ / "AUDIO" / "PLRS" / std::format("P{:05d}.PBK", id);
        if (!std::filesystem::exists(path)) continue;
        try {
            const auto bytes = readFile(path);
            plrsPhrases_ += formats::PhraseBank::parse(std::span<const std::uint8_t>{bytes})
                                .phraseCount();
            ++plrsBanks_;
        } catch (const std::exception&) {
        }
    }
}

void MatchAudio::startCrowd() {
    // Scene-level layer (pass-49 correction: DAT_00632778 idx2 = SNDstadiumreactions, the
    // crowd/CRWD stream per FUN_0047a7c4) scales this loop through the SMIX voice lane.
    crowdPctApplied_ = audio::sceneLevels().level(audio::kSceneCrowd);
    if (mixer_ && crowd_.frames() && !crowdId_)
        crowdId_ = mixer_->play(crowd_, 0.6f * smixLaneGain(audio::kSceneCrowd), /*loop=*/true);
}

void MatchAudio::relevelCrowd() {
    // Options slider writes percent idx2 -> gate value; rewrite the running lane's voice floats
    // with a fade (FUN_004906a4 -> FUN_00550530 0.75x -> voice 0x34 float). idx2 gates the whole
    // stadium lane: bed + traffic hum + presentation sample all follow the percent row.
    const int pct = audio::sceneLevels().level(audio::kSceneCrowd);
    if (pct == crowdPctApplied_) return;
    crowdPctApplied_ = pct;
    const float lane = smixLaneGain(audio::kSceneCrowd);
    if (mixer_ && crowdId_) mixer_->fade(crowdId_, 0.6f * lane, 50); // 50 x 20 ms ticks
    if (mixer_ && trafficId_) mixer_->fade(trafficId_, 0.12f * lane, 50);
    if (mixer_ && anthemId_ && mixer_->playing(anthemId_)) mixer_->fade(anthemId_, 0.8f * lane, 50);
}

void MatchAudio::trigger(const sim::SimEvent& e) {
    switch (e.type) {
    case sim::EventType::KickOff: // short opening chirp
        ++whistles_;
        if (mixer_ && whistle_.frames()) mixer_->play(whistle_, 1.0f * smixLaneGain(audio::kSceneMaster));
        break;
    case sim::EventType::PeriodEnd:
    case sim::EventType::MatchEnd: // long closing trill; whistle chain FUN_004bedb0 -> FUN_0051d480
        ++whistles_;
        if (mixer_ && whistleTrill_.frames()) mixer_->play(whistleTrill_, 1.0f * smixLaneGain(audio::kSceneMaster));
        break;
    case sim::EventType::Shot: // ball strike; the exe plays it at the kick code points
        ++kicks_;
        if (mixer_ && kick_.frames()) mixer_->play(kick_, 0.8f * smixLaneGain(audio::kSceneMaster));
        break;
    case sim::EventType::Foul: // referee chirp on the foul decision [H]
        ++whistles_;
        if (mixer_ && whistle_.frames()) mixer_->play(whistle_, 1.0f * smixLaneGain(audio::kSceneMaster));
        break;
    case sim::EventType::Goal: // crowd roar swell over the bed (measured GAME00 slot 45 profile)
        ++goalRoars_;
        if (mixer_ && goalRoar_.frames()) {
            if (goalId_) mixer_->stop(goalId_);
            goalId_ = mixer_->play(goalRoar_, 0.9f * smixLaneGain(audio::kSceneCrowd), /*loop=*/false);
        }
        break;
    default: break;
    }
}

// FE music list: exe glob FE*.ASF; port matches FE_*.ASF in the extracted tree. The
// BE_PAU*.ASF tracks belong to the mode-2 pause-screen list (FUN_0048ed9c FUN_00446624()==2
// branch DAT_00632670), not wired yet.
namespace {
std::vector<std::filesystem::path> collectMusicTracks(const std::filesystem::path& tree) {
    std::vector<std::filesystem::path> tracks;
    const auto dir = tree / "AUDIO" / "MUSC";
    if (!std::filesystem::is_directory(dir)) return tracks;
    for (const auto& de : std::filesystem::directory_iterator(dir)) {
        const auto name = de.path().filename().string();
        if (name.starts_with("FE_") && name.ends_with(".ASF")) tracks.push_back(de.path());
    }
    std::ranges::sort(tracks);
    return tracks;
}
} // namespace

bool MenuMusicDirector::start(audio::Mixer& mixer, const std::filesystem::path& tree) {
    files_ = collectMusicTracks(tree);
    if (files_.empty()) return false;
    mixer_ = &mixer;
    idx_ = 0;
    lane_ = laneGain();
    return playCurrent(lane_);
}

float MenuMusicDirector::laneGain() const { return smixLaneGain(audio::kSceneMusic); }

bool MenuMusicDirector::playCurrent(float gain) {
    if (!mixer_ || files_.empty()) return false;
    for (std::size_t attempt = 0; attempt < files_.size(); ++attempt) {
        const std::size_t i = (idx_ + attempt) % files_.size();
        try {
            curFile_ = std::make_shared<std::vector<std::uint8_t>>(readFile(files_[i]));
            const auto streams = formats::scanSchl(*curFile_);
            if (streams.empty()) continue;
            idx_ = i;
            // One-shot stream (loop=false): the update() rotation replaces the original pump's
            // end-of-track detection (FUN_0048eb98 list rotation).
            id_ = mixer_->stream(curFile_, streams.front(), gain, /*loop=*/false);
            return true;
        } catch (const std::exception&) { // try the next candidate
        }
    }
    curFile_.reset();
    return false;
}

std::string MenuMusicDirector::trackName() const {
    return id_ && idx_ < files_.size() ? files_[idx_].filename().string() : std::string();
}

void MenuMusicDirector::update() {
    if (!mixer_) return;
    if (leaving_) {
        if (--fadeFrames_ <= 0) { // seam complete: stop as the original ramp-out does
            mixer_->stop(id_);
            id_ = 0;
            leaving_ = false;
            if (reenterPending_) { // FE returned before the ramp finished: re-enter now
                reenterPending_ = false;
                lane_ = laneGain();
                if (playCurrent(0.f)) mixer_->fade(id_, lane_, 200);
            }
        }
        return;
    }
    const float lane = laneGain();
    if (lane != lane_) { lane_ = lane; mixer_->fade(id_, lane, 50); } // SNDstreamautovol re-level
    if (id_ && !mixer_->playing(id_)) { // track end -> next track (pump rotation)
        idx_ = (idx_ + 1) % files_.size();
        playCurrent(lane);
    }
}

void MenuMusicDirector::fadeOut() {
    if (!mixer_ || !id_ || leaving_) return;
    leaving_ = true;
    fadeFrames_ = 240; // 200 ticks (FUN_0048ec64 argument 0xc8) at ~60 fps
    mixer_->fade(id_, 0.f, 200);
}

void MenuMusicDirector::enterScene() {
    if (!mixer_) return;
    if (leaving_) { reenterPending_ = true; return; // ramp-out seam wins; restart when it ends
    }
    if (id_) return; // already playing
    lane_ = laneGain();
    if (playCurrent(0.f)) mixer_->fade(id_, lane_, 200); // ramp-in seam on scene entry
}

void MenuMusicDirector::relevel() {
    if (!mixer_ || !id_ || leaving_) return;
    lane_ = laneGain();
    mixer_->fade(id_, lane_, 50);
}

} // namespace wc98::app
