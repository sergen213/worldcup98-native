#include "wc98/audio/mixer.hpp"
#include <AL/al.h>
#include <AL/alc.h>
#define AL_ALEXT_PROTOTYPES
#include <AL/alext.h>
#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>

namespace wc98::audio {

namespace {
constexpr int kStreamBuffers = 4;
constexpr std::size_t kStreamChunkFrames = 8192; // per queued buffer (~0.37 s at 22050 Hz)

void check(const char* what) {
    if (ALenum e = alGetError(); e != AL_NO_ERROR) throw std::runtime_error(std::format("OpenAL {} failed (0x{:x})", what, e));
}
ALenum alFormat(std::uint32_t channels) {
    if (channels == 1) return AL_FORMAT_MONO16;
    if (channels == 2) return AL_FORMAT_STEREO16;
    throw std::runtime_error(std::format("{} channels unsupported", channels));
}
} // namespace

struct Mixer::Voice {
    SoundId id = 0;
    ALuint source = 0;
    std::vector<ALuint> buffers;
    // streaming only
    std::shared_ptr<const std::vector<std::uint8_t>> file;
    std::unique_ptr<formats::SchlDecoder> decoder;
    std::vector<std::int16_t> pending;
    std::uint32_t rate = 0, channels = 0;
    bool loop = false, exhausted = false;
};

Mixer::Mixer(std::optional<std::uint32_t> loopbackRate) : loopback_(loopbackRate.has_value()) {
    ALCdevice* dev = loopback_ ? alcLoopbackOpenDeviceSOFT(nullptr) : alcOpenDevice(nullptr);
    if (!dev) throw std::runtime_error(loopback_ ? "OpenAL loopback device unavailable" : "no OpenAL output device");
    ALCint loopAttrs[] = {ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT,
                          ALC_FREQUENCY, ALCint(loopbackRate.value_or(0)), 0};
    ALCcontext* ctx = alcCreateContext(dev, loopback_ ? loopAttrs : nullptr);
    if (!ctx || !alcMakeContextCurrent(ctx)) {
        if (ctx) alcDestroyContext(ctx);
        alcCloseDevice(dev);
        throw std::runtime_error("OpenAL context creation failed");
    }
    device_ = dev;
    context_ = ctx;
    if (!loopback_)
        pump_ = std::jthread([this](std::stop_token st) {
            while (!st.stop_requested()) {
                {
                    std::lock_guard lk(mu_);
                    pumpLocked();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
}

Mixer::~Mixer() {
    pump_ = {}; // request stop + join before tearing down OpenAL state
    for (auto& v : voices_) {
        alSourceStop(v->source);
        alDeleteSources(1, &v->source);
        alDeleteBuffers(ALsizei(v->buffers.size()), v->buffers.data());
    }
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(static_cast<ALCcontext*>(context_));
    alcCloseDevice(static_cast<ALCdevice*>(device_));
}

SoundId Mixer::play(const formats::Pcm& pcm, float gain, bool loop) {
    std::lock_guard lk(mu_);
    auto v = std::make_unique<Voice>();
    v->id = nextId_++;
    v->buffers.resize(1);
    alGenBuffers(1, v->buffers.data());
    alBufferData(v->buffers[0], alFormat(pcm.channels), pcm.samples.data(), ALsizei(pcm.samples.size() * 2), ALsizei(pcm.sampleRate));
    alGenSources(1, &v->source);
    alSourcei(v->source, AL_BUFFER, ALint(v->buffers[0]));
    alSourcei(v->source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    alSourcef(v->source, AL_GAIN, gain);
    alSourcePlay(v->source);
    check("play");
    voices_.push_back(std::move(v));
    return voices_.back()->id;
}

void Mixer::refill(Voice& v, unsigned buffer) {
    // Decode whole SCDl blocks until a chunk is full (or the stream ends / loops).
    while (v.pending.size() < kStreamChunkFrames * v.channels && !v.exhausted) {
        if (!v.decoder->next(v.pending)) {
            if (v.loop) v.decoder->rewind();
            else v.exhausted = true;
        }
    }
    std::size_t n = std::min(v.pending.size(), kStreamChunkFrames * v.channels);
    if (!n) return;
    alBufferData(buffer, alFormat(v.channels), v.pending.data(), ALsizei(n * 2), ALsizei(v.rate));
    alSourceQueueBuffers(v.source, 1, &buffer);
    v.pending.erase(v.pending.begin(), v.pending.begin() + std::ptrdiff_t(n));
}

SoundId Mixer::stream(std::shared_ptr<const std::vector<std::uint8_t>> file, const formats::SchlStream& s, float gain, bool loop) {
    std::lock_guard lk(mu_);
    auto v = std::make_unique<Voice>();
    v->id = nextId_++;
    v->decoder = std::make_unique<formats::SchlDecoder>(*file, s);
    v->file = std::move(file);
    v->rate = s.header.sampleRate;
    v->channels = s.header.channels;
    v->loop = loop;
    v->buffers.resize(kStreamBuffers);
    alGenBuffers(kStreamBuffers, v->buffers.data());
    alGenSources(1, &v->source);
    alSourcef(v->source, AL_GAIN, gain);
    for (ALuint b : v->buffers) refill(*v, b);
    alSourcePlay(v->source);
    check("stream");
    voices_.push_back(std::move(v));
    return voices_.back()->id;
}

void Mixer::pumpLocked() {
    for (auto it = voices_.begin(); it != voices_.end();) {
        Voice& v = **it;
        ALint state = 0, queued = 0;
        if (v.decoder) {
            ALint processed = 0;
            alGetSourcei(v.source, AL_BUFFERS_PROCESSED, &processed);
            while (processed-- > 0) {
                ALuint b = 0;
                alSourceUnqueueBuffers(v.source, 1, &b);
                refill(v, b);
            }
            alGetSourcei(v.source, AL_BUFFERS_QUEUED, &queued);
            alGetSourcei(v.source, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING && state != AL_PAUSED && queued > 0) alSourcePlay(v.source); // underrun recovery
        }
        alGetSourcei(v.source, AL_SOURCE_STATE, &state);
        if (state == AL_STOPPED && queued == 0) {
            alDeleteSources(1, &v.source);
            alDeleteBuffers(ALsizei(v.buffers.size()), v.buffers.data());
            it = voices_.erase(it);
        } else {
            ++it;
        }
    }
}

void Mixer::stop(SoundId id) {
    std::lock_guard lk(mu_);
    for (auto it = voices_.begin(); it != voices_.end(); ++it)
        if ((*it)->id == id) {
            alSourceStop((*it)->source);
            alSourcei((*it)->source, AL_BUFFER, 0); // detaches/unqueues all buffers
            alDeleteSources(1, &(*it)->source);
            alDeleteBuffers(ALsizei((*it)->buffers.size()), (*it)->buffers.data());
            voices_.erase(it);
            return;
        }
}

bool Mixer::playing(SoundId id) const {
    std::lock_guard lk(mu_);
    return std::ranges::any_of(voices_, [&](auto& v) { return v->id == id; });
}

void Mixer::setGain(SoundId id, float gain) {
    std::lock_guard lk(mu_);
    for (auto& v : voices_)
        if (v->id == id) alSourcef(v->source, AL_GAIN, gain);
}

void Mixer::setMasterGain(float gain) {
    std::lock_guard lk(mu_);
    alListenerf(AL_GAIN, gain);
}

void Mixer::render(std::span<std::int16_t> stereo) {
    if (!loopback_) throw std::runtime_error("render() needs a loopback mixer");
    std::lock_guard lk(mu_);
    constexpr std::size_t kSlice = 1024; // frames per refill/render step
    for (std::size_t done = 0; done < stereo.size() / 2;) {
        pumpLocked();
        std::size_t n = std::min(kSlice, stereo.size() / 2 - done);
        alcRenderSamplesSOFT(static_cast<ALCdevice*>(device_), stereo.data() + done * 2, ALCsizei(n));
        done += n;
    }
    pumpLocked(); // reclaim voices that finished in the last slice
}

} // namespace wc98::audio
