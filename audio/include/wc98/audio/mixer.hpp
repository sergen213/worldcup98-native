#pragma once
// OpenAL-soft playback: one-shot sounds from decoded PCM and SCHl streams (music, commentary)
// refilled block-by-block by a background thread. One Mixer per process (it owns the current
// OpenAL context).
#include "wc98/formats/schl.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace wc98::audio {

using SoundId = std::uint32_t;

class Mixer {
public:
    // Opens the default output device. With `loopbackRate` an OpenAL-soft loopback device is used
    // instead (no hardware; output is pulled with render()), e.g. for tests and offline mixing.
    explicit Mixer(std::optional<std::uint32_t> loopbackRate = std::nullopt);
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    SoundId play(const formats::Pcm& pcm, float gain = 1.0f, bool loop = false);
    // Streams `s` from `file` (kept alive by the mixer while playing).
    SoundId stream(std::shared_ptr<const std::vector<std::uint8_t>> file, const formats::SchlStream& s, float gain = 1.0f, bool loop = false);
    void stop(SoundId id);
    [[nodiscard]] bool playing(SoundId id) const;
    void setGain(SoundId id, float gain);
    // Scene-transition gain ramp (SNDstreamautovol semantics, FUN_004e6aa0 + state-cb tick): move
    // the voice's gain toward `target` in `ticks` engine ticks (1 tick = 1/50 s, the pump applies a
    // step every 20 ms). The original ramps stream volumes over the scene-switch seam (FUN_0048ec64
    // argument 0xc8 = 200 ticks at FE scene transitions).
    void fade(SoundId id, float target, int ticks);
    void setMasterGain(float gain);
    // Audio frames actually consumed by the output device for stream voices (one-shot voices
    // are not counted). Mirrors the exe audio frame counter `DAT_0058951c`, which the movie
    // pump uses to pace video frames on the audio clock.
    [[nodiscard]] std::uint64_t framesPlayed() const;
    // Loopback devices only: refills streams and renders interleaved stereo output.
    void render(std::span<std::int16_t> stereo);

private:
    struct Voice;
    void pumpLocked();
    void refill(Voice& v, std::size_t bufferIndex);

    void* device_ = nullptr;  // ALCdevice*
    void* context_ = nullptr; // ALCcontext*
    bool loopback_ = false;
    mutable std::mutex mu_;
    std::vector<std::unique_ptr<Voice>> voices_;
    SoundId nextId_ = 1;
    std::uint64_t tickCount_ = 0; // pump iterations; a fade step fires every 2 (20 ms = 1/50 s tick)
    std::uint64_t playedFrames_ = 0; // stream frames released by the device (DAT_0058951c mirror)
    std::jthread pump_; // last member: stops before the rest is destroyed
};

} // namespace wc98::audio
