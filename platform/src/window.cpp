#include "wc98/platform/window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string>

namespace wc98::platform {

[[noreturn]] static void sdlFail(const char* what) { throw std::runtime_error(std::string(what) + ": " + SDL_GetError()); }

// Left-stick deadzone: SDL axis range is -32767..32767; ~25% ignores stick drift.
static constexpr std::int16_t kStickDeadzone = 8000;

Window::Window(const char* title, int width, int height, bool fullscreen) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) sdlFail("SDL_InitSubSystem(VIDEO)");
    // Gamepads are optional: a broken pad subsystem must never stop the shell from starting.
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) { /* pads stay absent */ }
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    window_ = SDL_CreateWindow(title, width, height, flags);
    if (!window_) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        sdlFail("SDL_CreateWindow");
    }
}

Window::~Window() {
    for (auto& p : pads_) SDL_CloseGamepad(p.pad);
    SDL_DestroyWindow(window_);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// d-pad-as-buttons: all buttons incl. d-pad land in padButtons; d-pad bits also arm nav dirs.
static void applyPadButton(Window::Events& ev, unsigned* dpadMask, int button, bool down) {
    const int c = Window::canonicalPadButton(button);
    if (c >= 0 && down) ev.padButtons.push_back(c);
    if (!dpadMask || c < Window::kPadDPadUp || c > Window::kPadDPadRight) return;
    const unsigned bit = 1u << unsigned(c - Window::kPadDPadUp);
    if (down) *dpadMask |= bit;
    else *dpadMask &= ~bit;
}

Window::Events Window::poll() {
    auto findPadDpad = [this](int id) -> unsigned* { PadState* p = findPad(id); return p ? &p->dpadMask : nullptr; };
    Events ev;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: ev.quit = true; break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: ev.resized = true; break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.repeat) break;
            ev.keys.push_back(int(e.key.scancode)); // app-level nav (FE flow) reads this
            if (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT))) toggleFullscreen();
            else if (e.key.key >= SDLK_F1 && e.key.key <= SDLK_F10) ev.functionKey = int(e.key.key - SDLK_F1) + 1;
            break;
        case SDL_EVENT_GAMEPAD_ADDED: onPadAdded(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_REMOVED: onPadRemoved(e.gdevice.which); break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            applyPadButton(ev, findPadDpad(e.gbutton.which), int(e.gbutton.button), true);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            applyPadButton(ev, findPadDpad(e.gbutton.which), int(e.gbutton.button), false);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            PadState* p = findPad(e.gaxis.which);
            if (!p) break;
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) p->axisX = e.gaxis.value;
            else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) p->axisY = e.gaxis.value;
            break;
        }
        default: break;
        }
    }
    // Continuous nav: d-pad first, then left stick with a deadzone; edges + kEveryMs repeats.
    const long long now = (long long)SDL_GetTicks();
    for (auto& p : pads_) {
        int dir = 0;
        if (p.dpadMask & 1u) dir = kPadNavUp;
        else if (p.dpadMask & 2u) dir = kPadNavDown;
        else if (p.dpadMask & 4u) dir = kPadNavLeft;
        else if (p.dpadMask & 8u) dir = kPadNavRight;
        else if (p.axisY < -kStickDeadzone) dir = kPadNavUp;
        else if (p.axisY > kStickDeadzone) dir = kPadNavDown;
        else if (p.axisX < -kStickDeadzone) dir = kPadNavLeft;
        else if (p.axisX > kStickDeadzone) dir = kPadNavRight;
        for (int i = 0; i < 4; ++i)
            if (p.nav[i].fire(i + 1 == dir ? dir : 0, now)) ev.padNav.push_back(i + 1);
    }
    // Mouse state is queried, not event-pooled: the FE cursor sprite must track the pointer
    // between motion events (and while parked). SDL3 reports points; app maps to virtual pixels.
    float mx = 0.f, my = 0.f;
    SDL_GetMouseState(&mx, &my); // x/y set to ±FLT_MAX when the mouse is outside any display
    auto q = [](float v) { return int(std::clamp(v, -2147483647.f, 2147483647.f)); };
    ev.mouseX = q(mx), ev.mouseY = q(my);
    ev.connectedCount = int(pads_.size());
    return ev;
}

void Window::onPadAdded(int instanceId) {
    SDL_Gamepad* pad = SDL_OpenGamepad(instanceId);
    if (!pad) return; // hot-unplug race / open failure: stay pad-less for this device
    pads_.push_back(PadState{instanceId, pad, 0, 0, 0, {}});
}

void Window::onPadRemoved(int instanceId) {
    const auto it = std::ranges::find(pads_, instanceId, &PadState::id);
    if (it == pads_.end()) return;
    SDL_CloseGamepad(it->pad);
    pads_.erase(it);
}

int Window::canonicalPadButton(int sdlButton) {
    // The canonical layout mirrors SDL3's cross-platform mapping one-to-one.
    return sdlButton < 0 || sdlButton >= SDL_GAMEPAD_BUTTON_COUNT ? -1 : sdlButton;
}

bool PadNavThrottle::fire(int dir, long long nowMs) {
    if (dir == 0) { lastDir_ = 0; return false; }          // released: arm the next edge
    if (dir != lastDir_) { lastDir_ = dir; lastMs_ = nowMs; return true; } // press edge fires instantly
    if (nowMs - lastMs_ >= kEveryMs) { lastMs_ = nowMs; return true; }     // held: auto-repeat
    return false;
}

bool initGamepads() { return SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD); }

int connectedGamepadCount() {
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) SDL_free(ids);
    return n > 0 ? n : 0;
}

void Window::toggleFullscreen() {
    // Borderless desktop fullscreen (no mode change); SDL reports the new pixel size by event.
    bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    if (!SDL_SetWindowFullscreen(window_, !full)) sdlFail("SDL_SetWindowFullscreen");
}

bool Window::minimized() const { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0; }

std::pair<std::uint32_t, std::uint32_t> Window::pixelSize() const {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    return {static_cast<std::uint32_t>(w > 0 ? w : 0), static_cast<std::uint32_t>(h > 0 ? h : 0)};
}

std::vector<const char*> Window::vulkanInstanceExtensions() {
    Uint32 n = 0;
    const char* const* ext = SDL_Vulkan_GetInstanceExtensions(&n);
    if (!ext) sdlFail("SDL_Vulkan_GetInstanceExtensions");
    return {ext, ext + n};
}

} // namespace wc98::platform
