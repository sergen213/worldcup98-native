// See input.hpp. Bit layout ported from the sibling FIFA 99 native project (FIFA99.EXE
// Input_readKeyboard 0x42E2D0, Input_readJoystick 0x42E4B4 -> Input_readPadBits 0x664F8); World
// Cup 98 shares the engine family and the same ControllerState pad word (pass-4 RE).
#include "wc98/input/input.hpp"
#include <SDL3/SDL.h>
#include <algorithm>

namespace wc98::input {

std::uint32_t actionBit(config::Action a) {
    using A = config::Action;
    switch (a) {
    // dpad nibble (Up 1, Down 2, Left 8, Right 4)
    case A::MoveUp: return kUp;
    case A::MoveDown: return kDown;
    case A::MoveLeft: return kLeft;
    case A::MoveRight: return kRight;
    // buttons << 4: A lob 0x10, S pass 0x20, D shoot 0x40, Q 0x80, W sprint 0x100,
    // E 0x200, Ctrl/Shift 0x400, Alt/Ins 0x800, Space 0x1000
    case A::Lob: return 0x10;
    case A::Pass: return 0x20;
    case A::Shoot: return 0x40;
    case A::Evade: return 0x80;
    case A::Sprint: return 0x100;
    case A::ThroughPass: return 0x200;
    case A::Skill1: return 0x400;
    case A::Skill2: return 0x800;
    case A::Igm: return 0x1000;
    case A::Pause: return 0x2000;
    // in-game tactics keys << 16: Z 0x40000, X 0x80000, C 0x100000, V 0x200000
    case A::TacticWingBack: return 0x40000;
    case A::TacticThroughRun: return 0x80000;
    case A::TacticOffside: return 0x100000;
    case A::TacticPress: return 0x200000;
    default: return 0;
    }
}

Input::Input(const config::Settings& settings) : settings_(settings) {
    devices_.push_back(Device{});
    devices_[0].id = 1, devices_[0].name = "Keyboard";
    states_.resize(2);
    auto* b = settings.bindings("keyboard");
    states_[1].bindings = b ? *b : config::defaultKeyboard();
}

Input::~Input() {
    for (auto& s : states_)
        if (s.pad) SDL_CloseGamepad(s.pad);
}

Device* Input::find(std::uint32_t id) {
    auto it = std::find_if(devices_.begin(), devices_.end(), [&](auto& d) { return d.id == id; });
    return it == devices_.end() ? nullptr : &*it;
}

void Input::assign(std::uint32_t id, Side side) {
    if (auto* d = find(id)) d->side = side;
}

std::vector<const Device*> Input::humans() const {
    std::vector<const Device*> r;
    for (auto& d : devices_)
        if (d.connected && d.side != Side::None) r.push_back(&d);
    return r;
}

void Input::recompute(Device& d) {
    auto& s = state(d);
    std::uint32_t bits = 0;
    for (std::size_t a = 0; a < config::kActionCount; ++a)
        for (auto& in : s.bindings.actions[a])
            if (std::find(s.active.begin(), s.active.end(), in) != s.active.end()) bits |= actionBit(config::Action(a));
    // Queue every level change so a press shorter than a tick (or a re-press) is still seen.
    for (int b = 0; b < 32; ++b) {
        bool now = bits >> b & 1;
        auto& q = s.queue[std::size_t(b)];
        if (now == bool(s.phys >> b & 1)) continue;
        q.push_back(now);
        if (q.size() > 8) q.erase(q.begin(), q.begin() + 2); // never sampled (device unused): keep it bounded
    }
    s.phys = bits;
}

void Input::setInput(Device& d, const std::string& name, bool on) {
    auto& a = state(d).active;
    auto it = std::find(a.begin(), a.end(), name);
    if (on == (it != a.end())) return;
    if (on) a.push_back(name);
    else a.erase(it);
    recompute(d);
}

bool Input::handle(const SDL_Event& e) {
    auto pad = [&](SDL_JoystickID which) -> Device* {
        auto it = std::find_if(devices_.begin(), devices_.end(), [&](auto& d) {
            return d.kind == config::DeviceKind::Gamepad && d.connected && d.instance == which;
        });
        return it == devices_.end() ? nullptr : &*it;
    };
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (e.key.repeat) return true;
        setInput(devices_[0], SDL_GetScancodeName(e.key.scancode), e.type == SDL_EVENT_KEY_DOWN);
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        mouse_.x = int(e.motion.x), mouse_.y = int(e.motion.y);
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        std::uint8_t bit = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : 0;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) mouse_.buttons |= bit;
        else mouse_.buttons &= std::uint8_t(~bit);
        return true;
    }
    case SDL_EVENT_GAMEPAD_ADDED: {
        if (pad(e.gdevice.which)) return true;
        SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which);
        if (!g) return true;
        char guid[33] = {};
        SDL_GUIDToString(SDL_GetGamepadGUIDForID(e.gdevice.which), guid, sizeof guid);
        const char* nm = SDL_GetGamepadName(g);
        // A re-plugged pad (same GUID, currently disconnected) keeps its id and side.
        auto it = std::find_if(devices_.begin(), devices_.end(), [&](auto& d) { return !d.connected && d.guid == guid; });
        Device* d;
        if (it != devices_.end()) {
            d = &*it;
        } else {
            devices_.push_back(Device{});
            devices_.back().id = nextId_++, devices_.back().kind = config::DeviceKind::Gamepad, devices_.back().guid = guid;
            d = &devices_.back();
            states_.resize(d->id + 1);
            auto* b = settings_.bindings("gamepad." + std::string(guid));
            states_[d->id].bindings = b ? *b : config::defaultGamepad();
        }
        d->name = nm ? nm : "Gamepad";
        d->instance = e.gdevice.which;
        d->connected = true;
        state(*d).pad = g;
        return true;
    }
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (auto* d = pad(e.gdevice.which)) {
            auto& s = state(*d);
            SDL_CloseGamepad(s.pad);
            s.pad = nullptr;
            s.active.clear();
            recompute(*d); // releases everything held
            d->connected = false;
        }
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (auto* d = pad(e.gbutton.which))
            if (const char* n = SDL_GetGamepadStringForButton(SDL_GamepadButton(e.gbutton.button)))
                setInput(*d, n, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (auto* d = pad(e.gaxis.which)) {
            auto ax = SDL_GamepadAxis(e.gaxis.axis);
            const char* n = SDL_GetGamepadStringForAxis(ax);
            if (!n) return true;
            int v = e.gaxis.value;
            if (ax == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ax == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                setInput(*d, n, v > 0x4000); // half travel, as the stick thresholds below
            } else {
                // joyGetPosEx position p = v + 0x8000; digital 8-way dead zone:
                // p < 0x4000 -> '-', p > 0xC000 -> '+' (the analog reader is a stub on PC).
                int p = v + 0x8000;
                setInput(*d, std::string(n) + "-", p < 0x4000);
                setInput(*d, std::string(n) + "+", p > 0xC000);
            }
        }
        return true;
    default: return false;
    }
}

std::uint32_t Input::sample(std::uint32_t id) {
    auto* d = find(id);
    if (!d) return 0;
    auto& s = state(*d);
    std::uint32_t r = 0;
    for (int b = 0; b < 32; ++b) {
        auto& q = s.queue[std::size_t(b)];
        bool level;
        if (!q.empty()) {
            level = q.front();
            q.pop_front();
        } else {
            level = s.phys >> b & 1;
        }
        r |= std::uint32_t(level) << b;
    }
    s.reported = r;
    return r;
}

void Input::fill(std::uint32_t id, sim::ControllerState& c) {
    c.padBits = sample(id);
    c.devType = 0;
}

} // namespace wc98::input
