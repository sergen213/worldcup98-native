#pragma once
// Per-user directories following the XDG Base Directory specification.
#include <filesystem>
#include <string_view>

namespace wc98::platform {

struct UserDirs {
    std::filesystem::path config; // $XDG_CONFIG_HOME/<app>  (default ~/.config/<app>)
    std::filesystem::path data;   // $XDG_DATA_HOME/<app>    (default ~/.local/share/<app>)
    std::filesystem::path state;  // $XDG_STATE_HOME/<app>   (default ~/.local/state/<app>)
    std::filesystem::path log;    // <state>/log
};

// Resolves the directories without creating them. Relative XDG values are ignored, as the
// spec requires. Throws std::runtime_error when neither the XDG variable nor $HOME is set.
UserDirs userDirs(std::string_view app = "wc98");

} // namespace wc98::platform
