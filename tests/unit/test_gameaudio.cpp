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

    std::printf("test_gameaudio: OK\n");
    return 0;
}
