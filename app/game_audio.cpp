// Derived from fifa99-native (same author), ported for World Cup 98
#include "game_audio.hpp"

#include "wc98/file.hpp"
#include "wc98/formats/bnk.hpp"
#include "wc98/audio/smix_voice.hpp"
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

MatchAudio::MatchAudio(const std::filesystem::path& tree) {
    whistle_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kWhistleSlot);
    kick_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kKickSlot);
    crowd_ = decodeSlot(tree / "AUDIO" / "CRWD" / "CROWD.BNK", kCrowdSlot);
    goalRoar_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kGoalRoarSlot);
    whistleTrill_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kWhistleTrillSlot);
}

void MatchAudio::startCrowd() {
    // Scene-level layer (pass-49 correction: DAT_00632778 idx2 = SNDstadiumreactions, the
    // crowd/CRWD stream per FUN_0047a7c4) scales this loop through the SMIX voice lane.
    crowdPctApplied_ = audio::sceneLevels().level(audio::kSceneCrowd);
    if (mixer_ && crowd_.frames() && !crowdId_)
        crowdId_ = mixer_->play(crowd_, 0.6f * smixLaneGain(audio::kSceneCrowd), /*loop=*/true);
}

void MatchAudio::relevelCrowd() {
    // Options slider writes percent idx2 -> gate value; rewrite the running loop's voice lane
    // float with a fade (FUN_004906a4 -> FUN_00550530 0.75x -> voice 0x34 float).
    const int pct = audio::sceneLevels().level(audio::kSceneCrowd);
    if (pct == crowdPctApplied_) return;
    crowdPctApplied_ = pct;
    if (mixer_ && crowdId_)
        mixer_->fade(crowdId_, 0.6f * smixLaneGain(audio::kSceneCrowd), 50); // 50 x 20 ms ticks
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
