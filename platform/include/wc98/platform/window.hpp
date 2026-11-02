#pragma once
// SDL3 window with Vulkan support, high-DPI pixel sizes and a fullscreen toggle.
#include <cstdint>
#include <utility>
#include <vector>

struct SDL_Window;

namespace wc98::platform {

class Window {
public:
    // Initialises the SDL video subsystem; throws std::runtime_error on failure.
    Window(const char* title, int width, int height, bool fullscreen);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    struct Events {
        bool quit = false;    // window closed or ESC pressed
        bool resized = false; // drawable pixel size changed (includes fullscreen toggles)
        int functionKey = 0;  // last F1..F10 pressed during this poll (1..10), 0 = none
    };
    // Drains pending events. F11 and Alt+Enter toggle fullscreen, ESC requests quit.
    Events poll();

    void toggleFullscreen();
    [[nodiscard]] bool minimized() const;
    // Drawable size in physical pixels (differs from the logical size on high-DPI displays).
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> pixelSize() const;
    [[nodiscard]] SDL_Window* sdl() const { return window_; }
    // Instance extensions SDL needs to create a VkSurfaceKHR for this window.
    [[nodiscard]] static std::vector<const char*> vulkanInstanceExtensions();

private:
    SDL_Window* window_ = nullptr;
};

} // namespace wc98::platform
