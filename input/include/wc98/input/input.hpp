#pragma once
// SDL3 keyboard/mouse/gamepad input assembling the pad word the original engine's device readers
// produce (FIFAWC.EXE reads keyboard/joystick state into per-controller records; see
// docs/reverse-engineering/GHIDRA_PASS4_SIMULATION.md for ControllerState/padBits). Ported from
// the sibling FIFA 99 native project, whose exe addresses document the bit layout.
//
// One Input per process. Feed every SDL event to handle(); call sample() once per sim tick per
// human controller. WC98 grammar (register D-69, paddecode l.18/85-90): the device reader reports
// the CURRENT physical level at read time, and edges come from the XOR-diff of consecutive reads -
// a press opening and closing between two reads produces NO event in the pad word stream. Gesture
// timing (taps, double taps, holds) is the simulation's job.
#include "wc98/config/settings.hpp"
#include "wc98/sim/player.hpp"
#include <cstdint>
#include <string>
#include <vector>

union SDL_Event;
struct SDL_Gamepad;

namespace wc98::input {

// Pad-word bits (exe layout; Space 0x1000 is moved to 0x20000 inside the sim).
inline constexpr std::uint32_t kUp = 1, kDown = 2, kRight = 4, kLeft = 8;
std::uint32_t actionBit(config::Action a); // 0 for actions without a pad bit

// Controller Select (the original's per-device side choice).
enum class Side : std::int8_t { None = -1, Home = 0, Away = 1 };

struct Device {
    std::uint32_t id = 0;            // stable: 1 = keyboard; gamepads keep theirs across re-plugs (same GUID)
    config::DeviceKind kind = config::DeviceKind::Keyboard;
    std::string name, guid;
    std::uint32_t instance = 0;      // SDL_JoystickID while connected
    bool connected = true;
    Side side = Side::None;
};

struct Mouse {
    int x = 0, y = 0;                // window coordinates
    std::uint8_t buttons = 0;        // bit 0 left, bit 1 right
};

class Input {
public:
    explicit Input(const config::Settings& settings);
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // Keyboard keys, mouse, gamepad add/remove/buttons/axes. Returns true if consumed.
    bool handle(const SDL_Event& e);

    [[nodiscard]] const std::vector<Device>& devices() const { return devices_; }
    void assign(std::uint32_t deviceId, Side side);
    // Connected devices with a side, in device order: the match's human controllers.
    [[nodiscard]] std::vector<const Device*> humans() const;

    // Pad word of one device for this read (WC98 direct level + XOR-diff grammar, D-69).
    std::uint32_t sample(std::uint32_t deviceId);
    // sample() into ControllerState::padBits; devType 0 (every device reports the keyboard layout).
    void fill(std::uint32_t deviceId, sim::ControllerState& c);

    [[nodiscard]] const Mouse& mouse() const { return mouse_; }

private:
    struct State {
        std::uint32_t phys = 0; // current physical pad word (device reader reads levels directly)
        std::vector<std::string> active; // currently active input names
        config::DeviceBindings bindings;
        SDL_Gamepad* pad = nullptr;
    };
    Device* find(std::uint32_t id);
    State& state(const Device& d) { return states_[d.id]; }
    void setInput(Device& d, const std::string& name, bool on);
    void recompute(Device& d);

    const config::Settings& settings_;
    std::vector<Device> devices_;
    std::vector<State> states_;     // indexed by device id
    std::uint32_t nextId_ = 2;
    Mouse mouse_;
};

} // namespace wc98::input
