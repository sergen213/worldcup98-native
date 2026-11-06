// Derived from fifa99-native (same author), ported for World Cup 98
// Runtime-audio gate: deterministic OpenAL-soft LOOPBACK capture (no sound device). Asserts the
// FE music SCHl stream renders audible PCM over >1 s, the whistle/kick/crowd BNK slots decode to
// non-silent PCM, and a scripted KickOff event fires the SFX voice on the loopback mixer.
// Model: docs/reverse-engineering/AUDIO_RUNTIME.md (MUSC/SFXS/CRWD paths, slot mapping).
#include "check.hpp"
#include "../app/game_audio.hpp"

#include "wc98/file.hpp"
#include "wc98/formats/bnk.hpp"
#include "wc98/formats/schl.hpp"

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

    std::printf("test_gameaudio: OK\n");
    return 0;
}
