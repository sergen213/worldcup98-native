// Gamepad platform-layer gates (no hardware): canonical SDL_GamepadButton -> PadButton layout,
// out-of-range rejection, PadNavThrottle edge/repeat math on a fake clock, and the Window::poll
// glue driven by an SDL virtual joystick under the dummy video driver.
#include "check.hpp"
#include "wc98/platform/window.hpp"

#include <SDL3/SDL.h>
#include <memory>
#include <cstdio>

using namespace wc98::platform;

static SDL_JoystickID attachPad() { // test_input.cpp pattern: virtual gamepad, all buttons + axes
    SDL_VirtualJoystickDesc d;
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
    d.button_mask = (1u << (SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1)) - 1; // virtual index == SDL button
    d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    d.vendor_id = 0x1234, d.product_id = 0x5678;
    d.name = "Test Pad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
    CHECK(id != 0);
    return id;
}

int main() {
    // --- canonical layout: mirrors SDL3's cross-platform button order (Xbox positions) ---
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_SOUTH) == Window::kPadSouth);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_EAST) == Window::kPadEast);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_WEST) == Window::kPadWest);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_NORTH) == Window::kPadNorth);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_BACK) == Window::kPadBack);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_START) == Window::kPadStart);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_LEFT_STICK) == Window::kPadLeftStick);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK) == Window::kPadRightStick);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) == Window::kPadLeftShoulder);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) == Window::kPadRightShoulder);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_DPAD_UP) == Window::kPadDPadUp);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) == Window::kPadDPadRight);
    // Values match SDL indices one-to-one (identity mapping, app enum == SDL layout).
    CHECK(int(Window::kPadSouth) == int(SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(int(Window::kPadDPadRight) == int(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    // Out-of-range rejection.
    CHECK(Window::canonicalPadButton(-1) == -1);
    CHECK(Window::canonicalPadButton(SDL_GAMEPAD_BUTTON_COUNT) == -1);
    CHECK(Window::canonicalPadButton(99999) == -1);

    // --- nav repeat throttle on a fake clock: edge, 250 ms repeats, dead window, re-press ---
    PadNavThrottle t;
    CHECK(t.fire(Window::kPadNavUp, 1000));            // press edge fires instantly
    CHECK(!t.fire(Window::kPadNavUp, 1100));           // inside the period: silent
    CHECK(!t.fire(Window::kPadNavUp, 1249));           // one tick short of kEveryMs
    CHECK(t.fire(Window::kPadNavUp, 1250));            // exactly kEveryMs after last fire
    CHECK(t.fire(Window::kPadNavUp, 1500));            // next period from the 1250 fire
    t.fire(0, 1600);                                    // release arms the next edge
    CHECK(t.fire(Window::kPadNavDown, 1700));           // direction change: instant edge
    CHECK(!t.fire(Window::kPadNavDown, 1800));
    CHECK(t.fire(Window::kPadNavDown, 1950));           // held repeat from the 1700 fire
    t.fire(0, 2000);
    CHECK(t.fire(Window::kPadNavLeft, 2050));           // re-press after release = fresh edge
    CHECK(!t.fire(0, 2150));                            // release never fires

    // --- poll() glue: virtual pad through the real video driver (skip 77 headless, like fe gates) ---
    std::unique_ptr<Window> win;
    try {
        win = std::make_unique<Window>("test-gamepad", 64, 64, false);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "no video: %s\n", e.what());
        return 77; // CTest skip
    }
    CHECK(win->poll().connectedCount == 0);             // graceful pad-less startup
    const SDL_JoystickID pad = attachPad();
    int seen = -1;
    for (int i = 0; i < 50 && seen != 1; ++i) {         // drain the add event
        const auto ev = win->poll();
        seen = ev.connectedCount;
        if (!seen) SDL_Delay(10);
    }
    CHECK(seen == 1);

    auto padButton = [&](SDL_EventType type, Uint8 button) {
        SDL_Event e{};
        e.type = type;
        e.gbutton.which = pad;
        e.gbutton.button = button;
        e.gbutton.down = type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        CHECK(SDL_PushEvent(&e));
    };
    auto pollPads = [&] { return win->poll().padButtons; };

    // D-pad-as-buttons: DPAD_UP press lands in padButtons as canonical kPadDPadUp + nav edge.
    padButton(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_DPAD_UP);
    auto ev = win->poll();
    CHECK(ev.padButtons.size() == 1 && ev.padButtons[0] == Window::kPadDPadUp);
    CHECK(ev.padNav.size() == 1 && ev.padNav[0] == Window::kPadNavUp);
    CHECK(win->poll().padNav.empty());                  // held, inside the throttle window
    padButton(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_DPAD_UP);
    CHECK(win->poll().padButtons.empty());              // releases are not presses

    // Face buttons: South press/release -> one canonical press.
    padButton(SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_SOUTH);
    ev = win->poll();
    CHECK(ev.padButtons.size() == 1 && ev.padButtons[0] == Window::kPadSouth);
    padButton(SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_SOUTH);
    CHECK(pollPads().empty());

    // Left stick past the deadzone: nav Right edge; centering stops it.
    auto padAxis = [&](Uint8 axis, Sint16 value) {
        SDL_Event e{};
        e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        e.gaxis.which = pad;
        e.gaxis.axis = axis;
        e.gaxis.value = value;
        CHECK(SDL_PushEvent(&e));
    };
    padAxis(SDL_GAMEPAD_AXIS_LEFTX, 20000);
    ev = win->poll();
    CHECK(ev.padNav.size() == 1 && ev.padNav[0] == Window::kPadNavRight);
    padAxis(SDL_GAMEPAD_AXIS_LEFTX, 500);               // inside deadzone: no new nav edge
    CHECK(win->poll().padNav.empty());

    SDL_DetachVirtualJoystick(pad);
    win.reset();
    SDL_Quit();
    std::puts("test_gamepad: ok");
    return 0;
}
