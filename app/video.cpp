// EA .DCT movie playback: SCHl/mTCD frames -> SRE luma decode -> RGBA texture
// upload per frame through the Vulkan renderer (docs/asset-formats/VIDEO.md).
// Movie audio: the .DCT SCHl stream (SCDl blocks) plays through the Mixer;
// the exe movie pump runs the A/V-sync loop only when the parsed block count
// is > 2 (SNDstreaminit FUN_004ea4e0 return gated by `2 < _DAT_00632a0c`,
// release FUN_004e57b0 `SNDstreamfree`; analysis/pass75movie/,
// docs/reverse-engineering/AUDIO_RUNTIME.md pass-75).
#include "video.hpp"

#include "wc98/audio/mixer.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/dct.hpp"
#include "wc98/platform/window.hpp"
#include "wc98/renderer/vulkan.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>


#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace wc98::app {
namespace {

// Per-frame presentation interval: BE u16 at mTCD record +18 (0x38 = 56 ms in
// every INT_*/SEQ_* file; docs/asset-formats/VIDEO.md, Hypothesis-tagged).
int frameDelayMs(const std::vector<std::uint8_t>& bytes, const formats::DctFrameHeader& h) {
    if (h.offset + 20 > bytes.size()) return 56;
    const int ms = (bytes[h.offset + 18] << 8) | bytes[h.offset + 19];
    return ms >= 8 && ms <= 1000 ? ms : 56;
}

} // namespace

int runVideoPlay(const std::string& path, int gate, unsigned width, unsigned height, bool fullscreen) {
    const std::filesystem::path file{path};
    if (!std::filesystem::exists(file)) {
        std::printf("skip: no video file %s\n", path.c_str());
        return gate ? 2 : 1;
    }
    std::vector<std::uint8_t> bytes;
    std::vector<formats::DctFrameHeader> frames;
    try {
        bytes = readFile(file);
        frames = formats::dctScan(bytes);
    } catch (const std::exception& e) {
        std::printf("skip: dct open failed: %s\n", e.what());
        return gate ? 2 : 1;
    }

    // Movie audio (pass-75): the exe movie pump starts the SND stream service on the
    // same .DCT file after the first frame fetch + viewport setup (`SNDstreaminit`
    // FUN_004ea4e0 -> parse loop FUN_004e64ac -> SCDl blocks); it runs the
    // A/V-synced presentation loop only when the parsed block count is > 2
    // (`2 < _DAT_00632a0c`) and releases the stream at the end (`SNDstreamfree`
    // FUN_004e57b0). Mirror: gate on > 2 SCDl blocks, start after the renderer
    // exists. pass-96: frame pacing moved from wall-clock (SDL_Delay) onto the audio frame
    // counter `Mixer::framesPlayed()`, mirroring the exe counter `DAT_0058951c` that paces video.
    std::unique_ptr<audio::Mixer> movieAudio;
    audio::SoundId movieSound = 0;
    std::size_t movieAudioBlocks = 0;
    std::size_t movieAudioFrames = 0; // header.numSamples per channel
    unsigned movieAudioRate = 0;      // stream sample rate: video frame pacing clock

    std::unique_ptr<render::Renderer> renderer;
    std::unique_ptr<platform::Window> window;
    auto messages = std::make_shared<std::atomic<std::uint32_t>>(0);
    render::VulkanConfig vcfg;
    vcfg.validationCounter = messages;
    if (gate == 0) {
        try {
            window = std::make_unique<platform::Window>("World Cup 98 video", int(width), int(height), fullscreen);
        } catch (const std::exception& e) {
            std::printf("skip: no display (%s)\n", e.what());
            return 2;
        }
        vcfg.instanceExtensions = platform::Window::vulkanInstanceExtensions();
        vcfg.headlessFallback = platform::dummyLane();
        vcfg.createSurface = [&](VkInstance inst) {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (!vcfg.headlessFallback && !SDL_Vulkan_CreateSurface(window->sdl(), inst, nullptr, &surface))
                std::printf("SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
            return surface;
        };
        vcfg.drawableSize = [&] { return window->pixelSize(); };
    }
    try {
        renderer = render::createVulkanRenderer(vcfg);
    } catch (const render::NoVulkanDevice& e) {
        std::printf("skip: %s\n", e.what());
        return 2;
    }

    render::Camera cam;
    cam.eye = {0, 0, 10};
    cam.verticalFovDeg = 45;
    { // stream start (post-setup, exe ordering mirror): gate on > 2 SCDl blocks
        auto shared = std::make_shared<std::vector<std::uint8_t>>(bytes); // mixer keeps the file alive
        try {
            for (const auto& s : formats::scanSchl({bytes.data(), bytes.size()}))
                if (s.dataBlocks.size() > 2) {
                    movieAudioBlocks = s.dataBlocks.size();
                    movieAudioFrames = s.header.numSamples;
                    movieAudioRate = s.header.sampleRate;
                    movieAudio = std::make_unique<audio::Mixer>(
                        gate ? std::optional<std::uint32_t>{s.header.sampleRate} : std::nullopt);
                    movieSound = movieAudio->stream(shared, s);
                    break;
                }
        } catch (const std::exception& e) {
            std::printf("movie audio: disabled (%s)\n", e.what());
            movieSound = 0;
            movieAudio.reset();
        }
    }
    int done = 0;
    double fps = 0;
    std::uint64_t videoMs = 0; // cumulative frame-delay schedule (audio-clock pacing)
    const auto wallStart = std::chrono::steady_clock::now();
    for (const auto& h : frames) {
        formats::DctFrame f;
        try {
            f = formats::dctDecode({bytes.data() + h.offset, h.size}, h);
        } catch (const std::exception&) {
            break; // corrupt stream end: present what decoded so far
        }
        std::vector<std::uint8_t> rgba;
        formats::dctToRgba(f, rgba);
        const render::Image img{std::uint32_t(f.width), std::uint32_t(f.height), std::move(rgba)};
        const auto tex = renderer->createTexture(img, render::Wrap::Clamp);
        render::SpriteBatch ui;
        ui.draw(tex, {0, 0, render::SpriteBatch::kWidth, render::SpriteBatch::kHeight});
        const render::Frame frame{cam, {}, &ui, {0, 0, 0, 1}, {}, nullptr, {}, nullptr};
        if (gate) renderer->renderOffscreen(frame, std::uint32_t(f.width), std::uint32_t(f.height));
        else renderer->present(frame);
        renderer->destroyTexture(tex);
        const int ms = frameDelayMs(bytes, h);
        fps += 1000.0 / ms;
        ++done;
        if (!gate) {
            // pass-96: pace the video frame on the audio frame counter - the exe movie pump
            // waits on the stream voice (`DAT_0058951c`), not a wall-clock timer. Absolute
            // schedule: the video frame N must wait until the mixer has played the audio
            // frames belonging to the cumulative frame delays; skip the wait when audio is
            // already ahead. `SDL_Delay` only runs when no stream voice drives the clock.
            videoMs += static_cast<std::uint64_t>(ms);
            if (movieAudio && movieAudioRate) {
                const std::uint64_t target = videoMs * movieAudioRate / 1000u;
                std::uint64_t last = movieAudio->framesPlayed();
                int stall = 0;
                while (movieAudio->framesPlayed() < target) {
                    SDL_Delay(2);
                    stall += 2;
                    const std::uint64_t now = movieAudio->framesPlayed();
                    if (now != last) { last = now; stall = 0; } // stall guard: counter moving
                    else if (stall >= 250) break; // device stalled (ended/underrun): proceed
                }
            } else {
                SDL_Delay(Uint32(ms));
            }
            SDL_Event ev;
            while (SDL_PollEvent(&ev))
                if (ev.type == SDL_EVENT_QUIT || ev.key.scancode == SDL_SCANCODE_ESCAPE) goto done_presenting;
        }
    }
done_presenting:
    std::size_t movieRenderedFrames = 0;
    std::uint64_t playedCounter = 0; // DAT_0058951c mirror value
    bool granOk = true;
    int moviePeak = 0;
    if (movieAudio) {
        if (gate) { // loopback lane: drain the stream through the mixer pump, measure it
            std::vector<std::int16_t> buf(2048 * 2);
            while (movieAudio->playing(movieSound) && movieRenderedFrames < movieAudioFrames) {
                movieAudio->render({buf.data(), buf.size()});
                movieRenderedFrames += buf.size() / 2;
                // counter granularity gate: the played counter must track the rendered
                // frames within one render slice + one queued buffer (no 8192-frame steps).
                if (const std::int64_t d = static_cast<std::int64_t>(movieAudio->framesPlayed()) -
                                           static_cast<std::int64_t>(movieRenderedFrames);
                    d < 0 || d > static_cast<std::int64_t>(buf.size() / 2) + 8192)
                    granOk = false;
                for (const std::int16_t v : buf) {
                    const int a = v < 0 ? -v : v;
                    if (a > moviePeak) moviePeak = a;
                }
                if (movieRenderedFrames > movieAudioFrames) movieRenderedFrames = movieAudioFrames;
            }
        }
        movieAudio->stop(movieSound);
        playedCounter = movieAudio->framesPlayed(); // DAT_0058951c mirror
        movieAudio.reset(); // SNDstreamfree mirror
    }
    renderer.reset(); // drain validation messages
    const long long wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - wallStart).count();
    std::printf("VIDEO frames=%d size=%dx%d fps=%.1f movie-audio-blocks=%zu movie-audio-rendered=%zu movie-audio-played=%llu video-ms=%llu wall-ms=%lld peak=%d\n",
                done, frames.front().width, frames.front().height,
                done ? fps / done : 0.0, movieAudioBlocks, movieRenderedFrames,
                (unsigned long long)playedCounter, (unsigned long long)videoMs, wallMs, moviePeak);
    if (!granOk) { std::printf("movie-audio-granularity BAD\n"); return 4; }
    if (*messages) { std::printf("%u validation message(s)\n", messages->load()); return gate ? 3 : 3; }
    return 0;
}

} // namespace wc98::app
