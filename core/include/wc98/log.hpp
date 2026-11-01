// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Structured stderr logging (header-only in wc98 so core stays INTERFACE).
#include <atomic>
#include <cstdio>
#include <format>
#include <mutex>
#include <string_view>

namespace wc98::log {

enum class Level { Debug, Info, Warn, Error };

inline std::atomic<Level>& level() {
    static std::atomic<Level> g{Level::Info};
    return g;
}
inline std::mutex& mutex() {
    static std::mutex g;
    return g;
}

inline void setLevel(Level l) { level() = l; }

inline void write(Level l, std::string_view msg) {
    if (l < level()) return;
    static constexpr const char* names[] = {"debug", "info", "warn", "error"};
    std::lock_guard lk(mutex());
    std::fprintf(stderr, "[%s] %.*s\n", names[int(l)], int(msg.size()), msg.data());
}

template <class... A> void info(std::format_string<A...> f, A&&... a) { write(Level::Info, std::format(f, std::forward<A>(a)...)); }
template <class... A> void warn(std::format_string<A...> f, A&&... a) { write(Level::Warn, std::format(f, std::forward<A>(a)...)); }
template <class... A> void error(std::format_string<A...> f, A&&... a) { write(Level::Error, std::format(f, std::forward<A>(a)...)); }
template <class... A> void debug(std::format_string<A...> f, A&&... a) { write(Level::Debug, std::format(f, std::forward<A>(a)...)); }

}  // namespace wc98::log
