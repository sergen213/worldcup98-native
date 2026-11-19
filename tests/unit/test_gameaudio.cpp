// Derived from fifa99-native (same author), ported for World Cup 98
// Runtime-audio gate: deterministic OpenAL-soft LOOPBACK capture (no sound device). Asserts the
// FE music SCHl stream renders audible PCM over >1 s, the whistle/kick/trill/roar/crowd BNK
// slots decode to non-silent PCM, the scripted event seam fires SFX voices on the loopback
// mixer, and the idx2 crowd re-level seam fades the running bed to the lane gain.
// Model: docs/reverse-engineering/AUDIO_RUNTIME.md (MUSC/SFXS/CRWD paths, slot mapping).
#include "check.hpp"
#include "../app/game_audio.hpp"

#include "wc98/file.hpp"
#include "wc98/formats/bnk.hpp"
#include "wc98/formats/schl.hpp"
#include "wc98/sim/data.hpp" // (e) boot lane: sim::loadGameData + FCDB team ids
#include "wc98/audio/scene_levels.hpp" // director lane percent (DAT_00632778)

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

using namespace wc98;

namespace {

constexpr std::uint32_t kRate = 44100; // loopback render rate (Mixer contract)

// Pull `seconds` of stereo loopback output, return the max RMS over 0.1 s windows.
double tailRms(audio::Mixer& m, double seconds) {
    std::vector<std::int16_t> out(std::size_t(kRate * 2 * seconds));
    for (std::size_t off = 0; off < out.size(); off += 4096) {
        const std::size_t n = std::min<std::size_t>(4096, out.size() - off);
        m.render(std::span<std::int16_t>(out).subspan(off, n));
    }
    double acc = 0;
    for (std::size_t i = out.size() - 4410; i < out.size(); ++i) acc += double(out[i]) * out[i] / 4410.0;
    return std::sqrt(acc); // RMS of the final 0.1 s: the settled post-fade gain
}

double peakRms(audio::Mixer& m, double seconds) {
    std::vector<std::int16_t> out(std::size_t(kRate * 2 * seconds));
    for (std::size_t off = 0; off < out.size(); off += 4096) {
        const std::size_t n = std::min<std::size_t>(4096, out.size() - off);
        m.render(std::span<std::int16_t>(out).subspan(off, n));
    }
    double peak = 0;
    for (std::size_t off = 0; off + 4410 <= out.size(); off += 4410) { // 0.1 s windows
        double acc = 0;
        for (std::size_t i = off; i < off + 4410; ++i) acc += double(out[i]) * out[i] / 4410.0;
        peak = std::max(peak, std::sqrt(acc));
    }
    return peak;
}

std::int32_t peakAbs(const formats::Pcm& p) {
    std::int32_t peak = 0;
    for (const auto v : p.samples) peak = std::max(peak, std::abs(v));
    return peak;
}

formats::Pcm decodeSlot(std::uint32_t slot, const std::filesystem::path& file) {
    const auto data = readFile(file);
    const formats::Bnk bnk = formats::Bnk::parse(data);
    for (const auto& s : bnk.sounds)
        if (s.slot == slot) return formats::Bnk::decode(data, s);
    return {};
}

} // namespace

int main() {
    const auto tree = treeDirOrSkip(); // banks/streams need the extracted game tree

    // (a) FE menu music: SCHl .ASF stream is audible over >=1 s of loopback render.
    {
        audio::Mixer m{kRate};
        std::string track;
        CHECK(app::startMenuMusic(m, tree, &track));
        CHECK(track.starts_with("FE_")); // FE_*.ASF selection (exe glob FE*.ASF @0x566b3e)
        CHECK(peakRms(m, 1.5) > 20.0);   // non-silent music PCM
    }

    // (b) BNK slot decodes are non-silent: whistle chirp, kick thud, crowd loop.
    {
        const auto game = tree / "AUDIO" / "SFXS" / "GAME00.BNK";
        CHECK(peakAbs(decodeSlot(81, game)) > 200); // whistle (acoustic-verified chirp)
        CHECK(peakAbs(decodeSlot(23, game)) > 50);  // ball-strike thud
        CHECK(peakAbs(decodeSlot(85, game)) > 200); // period-end whistle trill (1.95 s bright)
        CHECK(peakAbs(decodeSlot(45, game)) > 200); // goal roar (4.12 s loud-noise burst)
        const auto crowd = decodeSlot(2, tree / "AUDIO" / "CRWD" / "CROWD.BNK");
        CHECK(crowd.frames() > 44100);              // ~3 s loop material
        CHECK(peakAbs(crowd) > 50);
    }

    // (c) Event seam: scripted KickOff creates the whistle voice, crowd loop starts on the
    // loopback mixer; null-mixer path stays headless-safe (counts only).
    {
        audio::Mixer m{kRate};
        app::MatchAudio ma(tree);
        ma.setMixer(&m);
        ma.startCrowd();
        CHECK(ma.crowdStarted());
        const double ambience = peakRms(m, 0.5);
        CHECK(ambience > 10.0); // crowd loop audible through the mixer

        sim::SimEvent ev{};
        ev.type = sim::EventType::KickOff;
        ma.trigger(ev);
        CHECK(ma.whistles() == 1);
        ev.type = sim::EventType::Shot;
        ma.trigger(ev);
        CHECK(ma.kicks() == 1);
        ev.type = sim::EventType::PeriodEnd;
        ma.trigger(ev);
        CHECK(ma.whistles() == 2); // KickOff/PeriodEnd/MatchEnd chain

        ev.type = sim::EventType::Goal;
        ma.trigger(ev);
        CHECK(ma.goals() == 1);
        CHECK(peakRms(m, 0.5) > 10.0); // goal roar (GAME00 slot 45 burst) audible over the bed

        ev.type = sim::EventType::Foul;
        ma.trigger(ev);
        CHECK(ma.whistles() == 3); // foul decision gets the referee chirp

        // idx2 crowd re-level seam on a fresh bed: options row 100->0 fades the running loop
        // toward silence; restoring 80 ramps it back. Dedicated mixer, no one-shot interference.
        {
            audio::Mixer m2{kRate};
            app::MatchAudio ma2(tree);
            ma2.setMixer(&m2);
            audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);
            ma2.startCrowd();
            CHECK(peakRms(m2, 0.4) > 10.0);
            audio::sceneLevels().setLevel(audio::kSceneCrowd, 0);
            ma2.relevelCrowd();
            CHECK(tailRms(m2, 2.6) < 80.0); // fade settles on the idx2=0 lane gain (~43 int16 RMS)
            audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);
            ma2.relevelCrowd();
            CHECK(tailRms(m2, 2.6) > 1000.0); // restored lane level ramps the bed back up (~4300)
        }
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);

        app::MatchAudio nullPath(tree); // no device: counters advance, no voices, no throw
        nullPath.startCrowd();
        ev.type = sim::EventType::MatchEnd;
        nullPath.trigger(ev);
        CHECK(nullPath.whistles() == 1 && !nullPath.crowdStarted());
    }

    // (d) MenuMusicDirector: lane percent -> re-level, launch fade-out seam, re-entry ramp.
    {
        audio::sceneLevels().setLevel(audio::kSceneMusic, 50); // options music row -> idx2
        audio::Mixer m{kRate};
        app::MenuMusicDirector dir;
        CHECK(dir.start(m, tree));
        CHECK(!dir.trackName().empty());
        CHECK(dir.playing());
        CHECK(peakRms(m, 1.0) > 8.0); // audible through the 50% lane (SMIX installer float < 1)
        dir.fadeOut();                // launch seam: 200-tick ramp (FUN_0048ec64 0xc8)
        for (int i = 0; i < 300 && dir.playing(); ++i) dir.update();
        CHECK(!dir.playing());        // seam complete -> stream stopped (rotation halted at exit)
        dir.enterScene();             // returning to the FE: ramp-in seam
        CHECK(dir.playing());
        CHECK(peakRms(m, 1.5) > 8.0); // audible again (gain ramping up from 0)
        audio::sceneLevels().setLevel(audio::kSceneMusic, 80);
        dir.relevel();                // options row changed while playing
        dir.update();
        CHECK(dir.playing());
        audio::sceneLevels().setLevel(audio::kSceneMusic, 80); // restore boot default
    }

    // (e) Match-lane boot seam (pass-51): boot keys the lane pools by the FCDB match data - the
    // 113-slot chant pool (FUN_0048dfc4 `iVar1 < 0x71`, exe name table @0x57eb5c + CD CGENER
    // lane), the A_T/G_T%04d announcer banks for both teams (FUN_0048fa08), the squad P%05d.PBK
    // player banks (FUN_0048fb10), the CRD card as crowd-bed authority, the stadium lane pair +
    // anthem voice, and the percent idx2 notifier dispatch re-level over the whole lane.
    {
        audio::Mixer m{kRate};
        const auto game = sim::loadGameData(tree);
        const std::uint16_t homeId = game.db.teams[0].id;  // id 26: full lane set on the CD
        const std::uint16_t awayId = game.db.teams[26].id; // id 67: C0006700 + A_T/G_T0067
        app::MatchAudio ma(tree);
        ma.setMixer(&m);
        ma.boot(homeId, awayId, game.db.teams[0].roster, game.db.teams[26].roster);
        CHECK(ma.crowdStarted() && ma.trafficStarted() && ma.anthemStarted());
        CHECK(audio::sceneLevels().hasNotifier(audio::kSceneCrowd)); // DAT_004904b0 slot [2]
        CHECK(ma.crowdCardSlots() == 4);                             // CROWD.CRD slot list
        CHECK(ma.chantBanks() == 113 && ma.chantPhrases() > 0);      // exe boot pool = 0x71 slots
        CHECK(ma.announcerBanks() == 4 && ma.announcerPhrases() > 0); // A_T/G_T x both teams
        CHECK(ma.plrsBanks() >= 30 && ma.plrsPhrases() > 0);          // 19+19 squad banks of 26/67
        CHECK(peakRms(m, 0.6) > 10.0); // bed + traffic + anthem audible on the loopback lane
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 0); // percent write -> notifier dispatch
        CHECK(tailRms(m, 2.6) < 80.0); // whole stadium lane fades to the idx2=0 gain
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);
        CHECK(tailRms(m, 2.6) > 1000.0); // dispatch again: lane ramps back up
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);
        app::MatchAudio nullPath(tree); // no device: catalog counters advance, no voices
        nullPath.boot(game.db.teams[0].id, game.db.teams[1].id,
                      game.db.teams[0].roster, game.db.teams[1].roster);
        CHECK(!nullPath.crowdStarted() && !nullPath.trafficStarted() && !nullPath.anthemStarted());
        CHECK(nullPath.chantBanks() == 113 && nullPath.announcerBanks() >= 4 &&
              nullPath.plrsBanks() > 0);
    }

    // (f) Chant pump lane (pass-53): boot classification lanes the 113-entry pool by the match
    // teams (FUN_0048dcd8: home -> lane 0, away -> lane 1, CGENER -> lane 2), and the pump
    // FUN_0048da50 tops the 3-slot voice pool on the 100-frame cadence. Clips are phrase-grouped
    // segment banks (class 'a', pass-52 census): first non-empty phrase, segments concatenated
    // through decodePlayTrack. The percent idx2 row gates audibility (FUN_004906d0 target
    // vol*percent[idx2]*0x7f/10000), so row 0 keeps requests silent with no voice.
    {
        audio::Mixer m2{kRate};
        const auto game = sim::loadGameData(tree);
        app::MatchAudio ma2(tree);
        ma2.setMixer(&m2);
        ma2.boot(game.db.teams[0].id, game.db.teams[26].id,
                 game.db.teams[0].roster, game.db.teams[26].roster);
        CHECK(ma2.chantPlays() == 0 && ma2.chantActive() == 0); // boot catalogs; pump not run yet
        CHECK(ma2.chantLaneCount(0) > 0 && ma2.chantLaneCount(1) > 0 &&
              ma2.chantLaneCount(2) > 0); // id 26/67 both ship C0 banks + CGENER pool
        for (int frame = 0; frame < 1200; ++frame) ma2.pumpChants(); // 12 cadence hits
        CHECK(ma2.chantPlays() >= 1);
        CHECK(ma2.chantActive() >= 1);
        const auto& clip = ma2.lastChantClip();
        CHECK(clip.frames() > 0);
        double acc = 0.0; // decoded chant clip content (SPEECH_PHRASES.md §1 lane) must be loud
        for (const auto s : clip.samples) acc += double(s) * double(s);
        const double rms = clip.samples.empty() ? 0.0
                         : std::sqrt(acc / double(clip.samples.size()));
        CHECK(rms > 10.0);
        CHECK(ma2.chantActive() <= 3); // DAT_006324d0 pool cap
        audio::Mixer m3{kRate};
        app::MatchAudio maSilent(tree);
        maSilent.setMixer(&m3);
        maSilent.boot(game.db.teams[0].id, game.db.teams[26].id,
                      game.db.teams[0].roster, game.db.teams[26].roster);
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 0); // SNDautovol target 0 -> silent
        for (int frame = 0; frame < 400; ++frame) maSilent.pumpChants();
        CHECK(maSilent.chantPlays() >= 1 && maSilent.chantActive() == 0); // requests, no voices
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);
    }

    std::printf("test_gameaudio: OK\n");
    return 0;
}
