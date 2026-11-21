// latency_probe.cpp - pass-118 timing artifact (committed with raw output: latency_probe.out).
// Measures OpenAL Soft source latency on the default device via AL_SOFT_source_latency:
// AL_SEC_OFFSET_LATENCY_SOFT returns {offset, tail} in seconds - offset = time until the
// sample at the current read position reaches the monitor, tail = monitor queue depth.
// A fresh 100 ms silent buffer queued at t0 therefore becomes audible ~offset+tail after
// alSourcePlay - this covers the OpenAL mix + sink latency the mixer's own probe could
// not see. Build: g++ -O2 latency_probe.cpp -lopenal -o /tmp/wc98_latency_probe
#include <AL/al.h>
#define AL_ALEXT_PROTOTYPES
#include <AL/alc.h>
#include <AL/alext.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>

int main() {
    ALCdevice* dev = alcOpenDevice(nullptr);
    ALCcontext* ctx = dev ? alcCreateContext(dev, nullptr) : nullptr;
    if (!ctx || !alcMakeContextCurrent(ctx)) { std::printf("device open failed\n"); return 1; }
    std::printf("device: %s\n", alcGetString(dev, ALC_DEVICE_SPECIFIER));
    if (!std::strstr(alGetString(AL_EXTENSIONS), "AL_SOFT_source_latency")) {
        std::printf("AL_SOFT_source_latency unavailable\n");
        return 1;
    }

    ALuint src, buf;
    alGenSources(1, &src);
    alGenBuffers(1, &buf);
    const ALsizei rate = 48000, frames = rate / 10; // exactly 100 ms
    short* data = new short[frames];
    std::memset(data, 0, frames * sizeof *data);
    alBufferData(buf, AL_FORMAT_MONO16, data, frames * 2, rate);
    alSourcei(src, AL_BUFFER, buf);

    ALdouble v[2];
    alGetSourcedvSOFT(src, AL_SEC_OFFSET_LATENCY_SOFT, v);
    std::printf("idle: offset=%.1f ms tail=%.1f ms\n", v[0] * 1000.0, v[1] * 1000.0);

    alSourcePlay(src);
    double minAudible = 1e9, maxAudible = 0;
    for (int i = 0; i < 60; ++i) { // 60 x 5 ms = 300 ms window (buffer is 100 ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        alGetSourcedvSOFT(src, AL_SEC_OFFSET_LATENCY_SOFT, v);
        ALint state = AL_PLAYING;
        alGetSourcei(src, AL_SOURCE_STATE, &state);
        const double audible = (v[0] + v[1]) * 1000.0; // head reaches monitor in ms
        if (state != AL_PLAYING && v[0] == 0.0) continue; // drained
        if (audible < minAudible) minAudible = audible;
        if (audible > maxAudible) maxAudible = audible;
    }
    std::printf("head-audible estimate window: min=%.1f ms max=%.1f ms (queued 100 ms buffer)\n",
                minAudible, maxAudible);
    std::printf("interpretation: min = first sample reaches monitor ~ this many ms after "
                "alSourcePlay (mix + sink); max = mix + sink tail while the buffer streams\n");

    alDeleteSources(1, &src);
    alDeleteBuffers(1, &buf);
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(ctx);
    alcCloseDevice(dev);
    return 0;
}
