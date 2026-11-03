#pragma once
// User settings shared by the app and tools: $XDG_CONFIG_HOME/wc98-native/settings.ini.
//
// Format: INI. `[section]` headers, `key = value` lines, `#` or `;` comment lines. Known keys are
// typed below; unknown sections/keys are kept verbatim and written back (comments are not).
// Invalid values are reported as warnings and replaced by the default. Keys that may repeat
// (game.data) form a list. `[meta] version` drives migration of older files (see kVersion).
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wc98::config {

inline constexpr int kVersion = 1;


enum class Presentation { Original, Enhanced };
enum class Aspect { Auto, R4x3, R16x9, R16x10, R21x9 };
enum class WindowMode { Windowed, Borderless, Fullscreen };
enum class TextureFilter { Nearest, Linear, Anisotropic };
enum class Backend { Auto, Vulkan }; // renderer backend selection
std::string_view name(Presentation v);
std::string_view name(Aspect v);
std::string_view name(WindowMode v);
std::string_view name(TextureFilter v);
std::string_view name(Backend v);

struct GameSettings {
    std::vector<std::filesystem::path> data; // game trees/ISOs, first usable one wins
};

struct GraphicsSettings {
    Presentation presentation = Presentation::Original;
    std::uint32_t width = 0, height = 0; // 0x0 = desktop size (fullscreen) / 1280x960 (windowed)
    Aspect aspect = Aspect::Auto;
    WindowMode window = WindowMode::Windowed;
    bool vsync = true;
    std::uint32_t msaa = 4; // samples: 1 (off), 2, 4, 8; enhanced only
    TextureFilter filter = TextureFilter::Nearest;
    std::string device; // Vulkan device name or pipelineCacheUUID hex; empty = automatic
    bool validation = false;
    Backend backend = Backend::Vulkan; // "auto" probes available backends, "vulkan" requires it
};

struct AudioSettings {
    int master = 100, music = 80, sfx = 100, commentary = 100, crowd = 100; // 0..100
    std::string device;                                                     // OpenAL device name; empty = default
};

// The original action set (manual control table + in-game tactics). Ported from fifa99-native.
enum class Action {
    MoveUp, MoveDown, MoveLeft, MoveRight, Pass, Shoot, Sprint, Lob, Evade, ThroughPass,
    Skill1, Skill2, Igm, TacticOffside, TacticPress, TacticThroughRun, TacticWingBack, Pause, Count
};
inline constexpr std::size_t kActionCount = std::size_t(Action::Count);
std::string_view actionKey(Action a);   // "move_up" (INI key)
std::string_view actionLabel(Action a); // "Move up"

enum class DeviceKind { Keyboard, Gamepad };
// One input per entry: an SDL scancode name ("Left Ctrl") for keyboards; an SDL gamepad button
// ("a", "leftshoulder", "dpup"), trigger ("lefttrigger") or axis direction ("leftx-") for gamepads.
using Binding = std::vector<std::string>;
struct DeviceBindings {
    std::string id; // "keyboard", "gamepad" (any pad) or "gamepad.<SDL GUID>"
    std::array<Binding, kActionCount> actions;
    [[nodiscard]] DeviceKind kind() const;
};
DeviceBindings defaultKeyboard();
DeviceBindings defaultGamepad();
// Validates one input name for the device kind (keyboard: SDL scancode name; gamepad: see Binding).
bool validInput(DeviceKind kind, std::string_view input);


struct Entry {
    std::string key, value;
};
struct Section {
    std::string name;
    std::vector<Entry> entries;
};

struct Settings {
    GameSettings game;
    GraphicsSettings graphics;
    AudioSettings audio;
    std::vector<DeviceBindings> controls{defaultKeyboard(), defaultGamepad()};
    std::vector<Entry> gameplay;  // passed through to the game options untouched
    std::vector<Section> unknown; // unknown sections + unknown keys of known sections

    // Bindings for a device id; falls back to "gamepad" for "gamepad.<guid>" without its own set.
    [[nodiscard]] const DeviceBindings* bindings(std::string_view id) const;
    DeviceBindings& bindingsFor(std::string_view id); // creates from the defaults when absent
};

struct LoadResult {
    Settings settings;
    std::vector<std::string> warnings; // "line 12: [graphics] msaa: ..."
    int fileVersion = 0;               // as read (0: unversioned or missing)
    bool migrated = false;
    bool existed = false;
};

LoadResult parse(std::string_view text);
std::string serialize(const Settings& s);

// $XDG_CONFIG_HOME/wc98-native/settings.ini (or $WC98_CONFIG when set).
std::filesystem::path defaultPath();
// Missing file: defaults with existed=false. Unreadable file: throws std::runtime_error.
LoadResult load(const std::filesystem::path& file);
// Atomic: writes "<file>.tmp-<pid>", fsyncs, renames over `file`, fsyncs the directory.
void save(const std::filesystem::path& file, const Settings& s);

} // namespace wc98::config
