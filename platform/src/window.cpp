#include "wc98/platform/window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <stdexcept>
#include <string>

namespace wc98::platform {

[[noreturn]] static void sdlFail(const char* what) { throw std::runtime_error(std::string(what) + ": " + SDL_GetError()); }

Window::Window(const char* title, int width, int height, bool fullscreen) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) sdlFail("SDL_InitSubSystem(VIDEO)");
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    window_ = SDL_CreateWindow(title, width, height, flags);
    if (!window_) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        sdlFail("SDL_CreateWindow");
    }
}

Window::~Window() {
    SDL_DestroyWindow(window_);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

Window::Events Window::poll() {
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
        default: break;
        }
    }
    return ev;
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
