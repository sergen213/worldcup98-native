#pragma once
// SDL3 window with Vulkan support, high-DPI pixel sizes and a fullscreen toggle.
// SDL3 gamepad polling: canonical button layout (cross-platform mapping), d-pad/left-stick
// navigation with a repeat throttle, and connection tracking. The app layer never sees SDL types.
#include <cstdint>
#include <utility>
#include <vector>

struct SDL_Window;
struct SDL_Gamepad;

namespace wc98::platform {

// Repeat throttle for one nav direction (pure clock math; unit-testable without SDL/hardware).
// Fires once on a direction edge, then every kEveryMs ms while the direction is held.
struct PadNavThrottle {
    // dir: PadNav value (0 = released/none). Returns true when this tick fires.
    bool fire(int dir, long long nowMs);
    static constexpr long long kEveryMs = 250;

private:
    int lastDir_ = 0;
    long long lastMs_ = 0;
};

class Window {
public:
    // Initialises the SDL video + gamepad subsystems (gamepad failure is non-fatal: pads stay
    // absent); throws std::runtime_error on video failure.
    Window(const char* title, int width, int height, bool fullscreen);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    struct Events {
        bool quit = false;    // window closed (quit / close-request events)
        bool resized = false; // drawable pixel size changed (includes fullscreen toggles)
        int functionKey = 0;  // last F1..F10 pressed during this poll (1..10), 0 = none
        std::vector<int> keys; // SDL_Scancode values of non-repeat key presses this poll, in order.
                               // Escape is surfaced here, not wired to quit: the FE flow uses Esc
                               // as "back" and owns app-level exit.

        // Canonical gamepad button values (PadButton) pressed this poll, non-repeat; d-pad
        // buttons are surfaced here too (d-pad-as-buttons). Empty when no pad is connected.
        std::vector<int> padButtons;
        // PadNav direction values fired this poll: d-pad / left-stick edges plus kEveryMs
        // repeats while held (PadNavThrottle); feeds the same nav actions as the arrow keys.
        std::vector<int> padNav;
        int connectedCount = 0; // gamepads open right now
    };
    // Drains pending events. F11 and Alt+Enter toggle fullscreen; window close requests quit.
    Events poll();

    // Canonical gamepad buttons: SDL3's cross-platform layout (values mirror SDL_GamepadButton),
    // so app code needs no SDL3 include.
    enum PadButton : int {
        kPadSouth = 0, kPadEast, kPadWest, kPadNorth,          // Xbox A/B/X/Y positions
        kPadBack, kPadGuide, kPadStart,
        kPadLeftStick, kPadRightStick,                          // press
        kPadLeftShoulder, kPadRightShoulder,                    // L / R
        kPadDPadUp, kPadDPadDown, kPadDPadLeft, kPadDPadRight,
    };
    static int canonicalPadButton(int sdlButton);

    // Navigation directions surfaced by poll() (d-pad + left stick).
    enum PadNav : int { kPadNavNone = 0, kPadNavUp, kPadNavDown, kPadNavLeft, kPadNavRight };

    void toggleFullscreen();
    [[nodiscard]] bool minimized() const;
    // Drawable size in physical pixels (differs from the logical size on high-DPI displays).
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> pixelSize() const;
    [[nodiscard]] SDL_Window* sdl() const { return window_; }
    // Instance extensions SDL needs to create a VkSurfaceKHR for this window.
    [[nodiscard]] static std::vector<const char*> vulkanInstanceExtensions();

private:
    struct PadState {
        int id = 0;                              // SDL_JoystickID
        SDL_Gamepad* pad = nullptr;              // owned by this Window
        std::int16_t axisX = 0, axisY = 0;       // left stick, from GAMEPAD_AXIS events
        unsigned dpadMask = 0;                   // bits: bit 0 Up, 1 Down, 2 Left, 3 Right
        PadNavThrottle nav[4];                   // per-direction slot (Up, Down, Left, Right)
    };
    PadState* findPad(int id) {
        for (auto& p : pads_) if (p.id == id) return &p;
        return nullptr;
    }
    void onPadAdded(int instanceId);
    void onPadRemoved(int instanceId);

    SDL_Window* window_ = nullptr;
    std::vector<PadState> pads_;
};

// Standalone gamepad subsystem init (idempotent). False = unavailable: callers must stay
// pad-less, never fatal. Used by the --pad-smoke headless gate before any display exists.
bool initGamepads();
int connectedGamepadCount(); // 0 when the subsystem is unavailable

} // namespace wc98::platform
