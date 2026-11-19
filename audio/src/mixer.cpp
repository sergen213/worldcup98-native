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
// Concurrent-voice cap. The original owns 16 DirectSound mix voices (SMIX) + a 16-slot SND voice
// table (DAT_006597e8 stride 0x60); overflow steals, so one-shots never grow unbounded. The port
// keeps 24 (16 voices + streams + headroom); oldest finished one-shots are reclaimed first.
constexpr std::size_t kMaxVoices = 24;

void check(const char* what) {
    if (ALenum e = alGetError(); e != AL_NO_ERROR) throw std::runtime_error(std::format("OpenAL {} failed (0x{:x})", what, e));
}
void clearStaleError() { // a stale alGetError flag would make an unrelated play() throw
    while (alGetError() != AL_NO_ERROR) {}
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
    // gain ramp state (fade())
    float gainCur = 0.f, fadeTarget = 0.f, fadeStep = 0.f;
    int fadeLeft = 0;
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
                try {
                    std::lock_guard lk(mu_);
                    pumpLocked();
                } catch (const std::exception&) { // never let the pump thread throw out of the process
                    while (alGetError() != AL_NO_ERROR) {}
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
    if (voices_.size() >= kMaxVoices) { // steal: reclaim the oldest one-shot (exe voice-table semantics)
        for (auto it = voices_.begin(); it != voices_.end(); ++it)
            if (!(*it)->decoder) {
                alSourceStop((*it)->source);
                alDeleteSources(1, &(*it)->source);
                alDeleteBuffers(ALsizei((*it)->buffers.size()), (*it)->buffers.data());
                voices_.erase(it);
                break;
            }
    }
    clearStaleError();
    auto v = std::make_unique<Voice>();
    v->id = nextId_++;
    v->gainCur = gain;
    v->buffers.resize(1);
    alGenBuffers(1, v->buffers.data());
    try {
        alBufferData(v->buffers[0], alFormat(pcm.channels), pcm.samples.data(), ALsizei(pcm.samples.size() * 2), ALsizei(pcm.sampleRate));
        alGenSources(1, &v->source);
        alSourcei(v->source, AL_BUFFER, ALint(v->buffers[0]));
        alSourcei(v->source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
        alSourcef(v->source, AL_GAIN, gain);
        alSourcePlay(v->source);
        check("play");
    } catch (...) { // allocated AL objects must not survive a failed publish
        if (v->source) alSourcei(v->source, AL_BUFFER, 0); // detach before deleting the attached buffer
        alDeleteBuffers(ALsizei(v->buffers.size()), v->buffers.data());
        if (v->source) alDeleteSources(1, &v->source);
        clearStaleError();
        throw;
    }
    voices_.push_back(std::move(v));
    return voices_.back()->id;
}

void Mixer::refill(Voice& v, unsigned buffer) {
    // Decode whole SCDl blocks until a chunk is full (or the stream ends / loops).
    while (v.pending.size() < kStreamChunkFrames * v.channels && !v.exhausted) {
        if (!v.decoder->next(v.pending)) {
            if (v.loop) {
                v.decoder->rewind();
                // A zero-data looped stream would rewind forever; the exe lane never loops an
                // empty bank — treat it exhausted and let the queued buffers drain out.
                if (!v.decoder->next(v.pending)) v.exhausted = true;
            } else {
                v.exhausted = true;
            }
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
    clearStaleError();
    auto v = std::make_unique<Voice>();
    v->id = nextId_++;
    v->decoder = std::make_unique<formats::SchlDecoder>(*file, s);
    v->file = std::move(file);
    v->rate = s.header.sampleRate;
    v->channels = s.header.channels;
    v->loop = loop;
    v->gainCur = gain;
    v->buffers.resize(kStreamBuffers);
    alGenBuffers(kStreamBuffers, v->buffers.data());
    alGenSources(1, &v->source);
    alSourcef(v->source, AL_GAIN, gain);
    try {
        for (ALuint b : v->buffers) refill(*v, b);
        alSourcePlay(v->source);
        check("stream");
    } catch (...) {
        if (v->source) {
            alSourceStop(v->source);
            alSourcei(v->source, AL_BUFFER, 0); // detach queued buffers before deleting them
        }
        alDeleteBuffers(ALsizei(v->buffers.size()), v->buffers.data());
        if (v->source) alDeleteSources(1, &v->source);
        clearStaleError();
        throw;
    }
    voices_.push_back(std::move(v));
    return voices_.back()->id;
}

void Mixer::pumpLocked() {
    ++tickCount_;
    const bool fadeTick = (tickCount_ % 2) == 0; // every pump iteration is 10 ms; a step per 20 ms tick
    for (auto it = voices_.begin(); it != voices_.end();) {
        Voice& v = **it;
        ALint state = 0, queued = 0;
        if (v.decoder) {
            ALint processed = 0;
            alGetSourcei(v.source, AL_BUFFERS_PROCESSED, &processed);
            try {
                while (processed-- > 0) {
                    ALuint b = 0;
                    alSourceUnqueueBuffers(v.source, 1, &b);
                    refill(v, b);
                }
            } catch (const std::exception&) { // corrupted stream member: stop refilling, drain what queued
                while (alGetError() != AL_NO_ERROR) {}
                v.exhausted = true;
            }
            alGetSourcei(v.source, AL_BUFFERS_QUEUED, &queued);
            alGetSourcei(v.source, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING && state != AL_PAUSED && queued > 0 && !v.exhausted) alSourcePlay(v.source); // underrun recovery
        }
        if (fadeTick && v.fadeLeft > 0) {
            v.gainCur += v.fadeStep;
            if (--v.fadeLeft == 0) v.gainCur = v.fadeTarget;
            alSourcef(v.source, AL_GAIN, v.gainCur);
            while (alGetError() != AL_NO_ERROR) {}
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
        if (v->id == id) {
            alSourcef(v->source, AL_GAIN, gain);
            v->gainCur = gain;
            v->fadeLeft = 0; // an explicit gain command cancels a ramp (SNDstreamautovol semantics)
        }
}

void Mixer::fade(SoundId id, float target, int ticks) {
    std::lock_guard lk(mu_);
    for (auto& v : voices_)
        if (v->id == id) {
            if (ticks < 1) {
                alSourcef(v->source, AL_GAIN, target);
                v->gainCur = target;
                v->fadeLeft = 0;
                return;
            }
            v->fadeTarget = target;
            v->fadeStep = (target - v->gainCur) / float(ticks);
            v->fadeLeft = ticks;
            return;
        }
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
