// mix_onset_probe.cpp - pass-118 artifact (committed with raw output: mix_onset_probe.out).
// Measures the ~2 ms claim of AUDIO_LATENCY_ANALYSIS.md: wall time from alSourcePlay to
// the moment the OpenAL Soft mixer has consumed the first sample of the buffer
// (AL_SAMPLE_OFFSET > 0). Device-side sink tail is measured separately by
// latency_probe.cpp; together they bound event->mix and mix->audible stages.
// Build: g++ -O2 mix_onset_probe.cpp -lopenal -o /tmp/wc98_mix_onset_probe
#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>

int main() {
    ALCdevice* dev = alcOpenDevice(nullptr);
    ALCcontext* ctx = dev ? alcCreateContext(dev, nullptr) : nullptr;
    if (!ctx || !alcMakeContextCurrent(ctx)) { std::printf("device open failed\n"); return 1; }
    ALuint src, buf;
    alGenSources(1, &src);
    alGenBuffers(1, &buf);
    const ALsizei rate = 48000, frames = rate; // 1 s silent, long enough to still be PLAYING
    short* data = new short[frames];
    std::memset(data, 0, frames * sizeof *data);
    alBufferData(buf, AL_FORMAT_MONO16, data, frames * 2, rate);
    alSourcei(src, AL_BUFFER, buf);

    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    alSourcePlay(src);
    ALint off = 0;
    long firstUs = -1;
    for (int i = 0; i < 1000 && firstUs < 0; ++i) { // 1 ms poll resolution, 1 s window
        std::this_thread::sleep_for(std::chrono::microseconds(250));
        alGetSourcei(src, AL_SAMPLE_OFFSET, &off);
        if (off > 0) firstUs = std::chrono::duration_cast<std::chrono::microseconds>(clk::now() - t0).count();
    }
    std::printf("first consumed frame (AL_SAMPLE_OFFSET>0) %.2f ms after alSourcePlay\n",
                firstUs / 1000.0);
    std::printf("interpretation: time until the OpenAL Soft mixer pulls the queued buffer; "
                "poll granularity 0.25 ms, so value is a ceiling, not an exact onset\n");

    alDeleteSources(1, &src);
    alDeleteBuffers(1, &buf);
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(ctx);
    alcCloseDevice(dev);
    return firstUs >= 0 ? 0 : 2;
}
