// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Minimal assertion helpers for test executables (no framework).
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); std::exit(1); } } while (0)
#define CHECK_THROWS(expr) do { bool t_ = false; try { (void)(expr); } catch (...) { t_ = true; } CHECK(t_ && "expected throw: " #expr); } while (0)

// Returns the extracted World Cup 98 tree (WC98_TREE) or exits with 77 (CTest skip).
// Data-dependent checks are gated this way so CI stays green without the game (77 = skip).
inline std::filesystem::path treeDirOrSkip() {
    const char* d = std::getenv("WC98_TREE");
    if (!d || !*d || !std::filesystem::exists(d)) { std::fprintf(stderr, "WC98_TREE unset: skipping\n"); std::exit(77); }
    return d;
}
