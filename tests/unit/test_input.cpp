// f99_input with synthetic SDL events: keyboard pad words (exe tables 0x57E188/0x57E130/0x57E1A8),
// wc98_input with synthetic SDL events: keyboard pad words (exe tables, FIFA99-documented layout),
// (0x42E4B4), hot-plug with stable ids, controller select and ControllerState output.
#include "check.hpp"
#include "wc98/input/input.hpp"
#include <SDL3/SDL.h>

using namespace wc98::input;

static void pump(Input& in) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) in.handle(e);
}

static void key(Input& in, SDL_Scancode sc, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = sc;
    e.key.down = down;
    CHECK(SDL_PushEvent(&e));
    pump(in);
}

static SDL_JoystickID attachPad() {
    SDL_VirtualJoystickDesc d;
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
    d.button_mask = (1u << (SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1)) - 1; // virtual index == SDL button
    d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    d.vendor_id = 0x1234, d.product_id = 0x5678;
    d.name = "Test Pad";
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
    CHECK(id != 0);
    return id;
}

int main() {
    CHECK(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD));
    wc98::config::Settings settings;
    Input in(settings);
    CHECK(in.devices().size() == 1 && in.devices()[0].id == 1);
    const std::uint32_t kb = 1;

    // Keyboard: S pass 0x20, Up 1, W sprint 0x100, Space 0x1000, Esc pause 0x2000, Z/C tactics.
    key(in, SDL_SCANCODE_S, true);
    key(in, SDL_SCANCODE_UP, true);
    CHECK(in.sample(kb) == (0x20 | kUp));
    key(in, SDL_SCANCODE_S, false);
    CHECK(in.sample(kb) == kUp);
    key(in, SDL_SCANCODE_UP, false);
    key(in, SDL_SCANCODE_W, true);
    key(in, SDL_SCANCODE_SPACE, true);
    key(in, SDL_SCANCODE_ESCAPE, true);
    key(in, SDL_SCANCODE_Z, true);
    key(in, SDL_SCANCODE_C, true);
    CHECK(in.sample(kb) == (0x100 | 0x1000 | 0x2000 | 0x40000 | 0x100000));
    for (auto sc : {SDL_SCANCODE_W, SDL_SCANCODE_SPACE, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_Z, SDL_SCANCODE_C}) key(in, sc, false);
    CHECK(in.sample(kb) == 0);
    // Both Ctrl keys map to Skill1 0x400; releasing one keeps it held.
    key(in, SDL_SCANCODE_LCTRL, true);
    key(in, SDL_SCANCODE_RCTRL, true);
    key(in, SDL_SCANCODE_LCTRL, false);
    CHECK(in.sample(kb) == 0x400);
    key(in, SDL_SCANCODE_RCTRL, false);
    CHECK(in.sample(kb) == 0);

    // WC98 grammar (register D-69): a press that opens and closes between two device reads
    // produces NO event - the reader reports current physical levels, edges are the XOR-diff of
    // consecutive reads. The old port queue surfaced such taps for one tick; that is refuted.
    key(in, SDL_SCANCODE_D, true);
    key(in, SDL_SCANCODE_D, false);
    CHECK(in.sample(kb) == 0);
    CHECK(in.sample(kb) == 0);
    // A double tap completed inside one frame is invisible the same way; only presses that are
    // still held at read time enter the pad word stream.
    key(in, SDL_SCANCODE_D, true);
    key(in, SDL_SCANCODE_D, false);
    key(in, SDL_SCANCODE_D, true);
    key(in, SDL_SCANCODE_D, false);
    std::uint32_t seq[5];
    for (auto& s : seq) s = in.sample(kb) & 0x40;
    CHECK(seq[0] == 0 && seq[1] == 0 && seq[2] == 0 && seq[3] == 0 && seq[4] == 0);
    // A held key spanning ticks stays pressed every tick (hold frames are counted by the sim).
    key(in, SDL_SCANCODE_D, true);
    for (int i = 0; i < 20; ++i) CHECK(in.sample(kb) == 0x40);
    key(in, SDL_SCANCODE_D, false);
    CHECK(in.sample(kb) == 0);

    // Hot-plug a virtual gamepad.
    SDL_JoystickID jid = attachPad();
    pump(in);
    CHECK(in.devices().size() == 2);
    const Device& pad = in.devices()[1];
    std::uint32_t padId = pad.id;
    CHECK(padId == 2 && pad.connected && pad.kind == wc98::config::DeviceKind::Gamepad);
    SDL_Joystick* joy = SDL_OpenJoystick(jid);
    CHECK(joy);
    auto button = [&](SDL_GamepadButton b, bool down) {
        CHECK(SDL_SetJoystickVirtualButton(joy, b, down));
        SDL_UpdateJoysticks();
        pump(in);
    };
    auto axis = [&](SDL_GamepadAxis a, Sint16 v) {
        CHECK(SDL_SetJoystickVirtualAxis(joy, a, v));
        SDL_UpdateJoysticks();
        pump(in);
    };
    button(SDL_GAMEPAD_BUTTON_SOUTH, true);   // "a" -> pass
    button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true); // evade 0x80
    CHECK(in.sample(padId) == (0x20 | 0x80));
    button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
    CHECK(in.sample(padId) == 0);
    // Stick digitisation: |v| must exceed 0x4000 (p < 0x4000 / p > 0xC000 with p = v + 0x8000).
    axis(SDL_GAMEPAD_AXIS_LEFTX, -0x4000);
    CHECK(in.sample(padId) == 0);
    axis(SDL_GAMEPAD_AXIS_LEFTX, -0x4001);
    CHECK(in.sample(padId) == kLeft);
    axis(SDL_GAMEPAD_AXIS_LEFTX, 0x4001);
    axis(SDL_GAMEPAD_AXIS_LEFTY, -0x7000);
    CHECK(in.sample(padId) == (kRight | kUp));
    axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    axis(SDL_GAMEPAD_AXIS_LEFTY, 0);
    axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0x7FFF); // skill 1
    CHECK(in.sample(padId) == 0x400);
    axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
    CHECK(in.sample(padId) == 0);

    // Controller select and ControllerState output.
    in.assign(kb, Side::Home);
    in.assign(padId, Side::Away);
    CHECK(in.humans().size() == 2 && in.humans()[1]->id == padId);
    button(SDL_GAMEPAD_BUTTON_NORTH, true); // "y" -> sprint
    wc98::sim::ControllerState cs{};
    in.fill(padId, cs);
    CHECK(cs.padBits == 0x100 && cs.devType == 0);

    // Unplug while held: the release still reaches the sim, the device drops out of the humans.
    SDL_CloseJoystick(joy);
    CHECK(SDL_DetachVirtualJoystick(jid));
    pump(in);
    CHECK(!in.devices()[1].connected);
    CHECK(in.sample(padId) == 0);
    CHECK(in.humans().size() == 1);
    // Re-plug: same id and side.
    jid = attachPad();
    pump(in);
    CHECK(in.devices().size() == 2 && in.devices()[1].connected && in.devices()[1].id == padId);
    CHECK(in.devices()[1].side == Side::Away && in.humans().size() == 2);
    joy = SDL_OpenJoystick(jid);
    button(SDL_GAMEPAD_BUTTON_EAST, true); // "b" -> shoot
    CHECK(in.sample(padId) == 0x40);
    SDL_CloseJoystick(joy);
    SDL_DetachVirtualJoystick(jid);
    pump(in);

    // Mouse state.
    SDL_Event m{};
    m.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    m.button.button = SDL_BUTTON_LEFT;
    m.button.down = true;
    in.handle(m);
    m.type = SDL_EVENT_MOUSE_MOTION;
    m.motion.x = 100, m.motion.y = 50;
    in.handle(m);
    CHECK(in.mouse().buttons == 1 && in.mouse().x == 100 && in.mouse().y == 50);

    SDL_Quit();
    std::puts("test_input: ok");
    return 0;
}
