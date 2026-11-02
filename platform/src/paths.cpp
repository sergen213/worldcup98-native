#include "wc98/platform/paths.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace wc98::platform {

static std::filesystem::path base(const char* var, const char* homeRel) {
    if (const char* v = std::getenv(var); v && *v && std::filesystem::path(v).is_absolute()) return v;
    const char* home = std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error(std::string("neither ") + var + " nor HOME is set");
    return std::filesystem::path(home) / homeRel;
}

UserDirs userDirs(std::string_view app) {
    UserDirs d;
    d.config = base("XDG_CONFIG_HOME", ".config") / app;
    d.data = base("XDG_DATA_HOME", ".local/share") / app;
    d.state = base("XDG_STATE_HOME", ".local/state") / app;
    d.log = d.state / "log";
    return d;
}

} // namespace wc98::platform
