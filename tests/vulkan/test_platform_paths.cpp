// XDG base-directory resolution.
#include "check.hpp"

#include "wc98/platform/paths.hpp"

#include <cstdlib>

int main() {
    setenv("HOME", "/home/u", 1);
    unsetenv("XDG_CONFIG_HOME"), unsetenv("XDG_DATA_HOME"), unsetenv("XDG_STATE_HOME");
    auto d = wc98::platform::userDirs();
    CHECK(d.config == "/home/u/.config/wc98" && d.data == "/home/u/.local/share/wc98");
    CHECK(d.state == "/home/u/.local/state/wc98" && d.log == "/home/u/.local/state/wc98/log");

    setenv("XDG_CONFIG_HOME", "/cfg", 1);
    setenv("XDG_DATA_HOME", "relative/ignored", 1);
    d = wc98::platform::userDirs("x");
    CHECK(d.config == "/cfg/x" && d.data == "/home/u/.local/share/x");

    unsetenv("HOME"), unsetenv("XDG_CONFIG_HOME");
    CHECK_THROWS(wc98::platform::userDirs());
    return 0;
}
