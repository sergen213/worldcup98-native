// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
// Layered, case-insensitive, read-only view of the game data. WC98 delta: the tree root IS the
// data root (no DATA/ directory), and there is no patch layer — the layers are Base (game tree)
// and User (an overrides dir with the same layout as the tree).
//
//   Precedence: Layer::User > Layer::Base; within one layer the most recently mounted root wins.
//   Paths:      "COMMON/FCDB.DBI" (any case, '/' or '\\'), BIG members with ':' —
//               "INGAME/STADIUM/PARI___D.BIG:stad_n", nested "A.BIG:inner.big:member".
//
// Mount everything first; afterwards all const members are safe to call from any thread.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wc98::assets {

enum class Layer { Base = 0, User = 1 };

class Vfs {
public:
    using Bytes = std::shared_ptr<const std::vector<std::uint8_t>>;

    explicit Vfs(std::size_t cacheBytes = std::size_t{64} << 20);

    // Mounts a WC98 game tree (or overrides dir). The tree is indexed once here; files added
    // later are not seen until it is mounted again. Executables and scripts are indexed too —
    // the VFS is a view, the required-file policy lives in manifest.cpp.
    void mountTree(const std::filesystem::path& root, Layer layer);

    struct Resolved {
        std::filesystem::path file;
        Layer layer;
    };
    // Resolves a plain file path (no ':' member part) through the layers.
    [[nodiscard]] std::optional<Resolved> resolve(std::string_view gamePath) const;
    [[nodiscard]] bool exists(std::string_view path) const;
    // Reads a file or BIG member; `decompress` unpacks RefPack payloads. Throws std::runtime_error
    // when the path does not resolve. Results are shared with the cache: never modify them.
    [[nodiscard]] Bytes read(std::string_view path, bool decompress = false) const;
    // Every resolvable canonical file path (highest layer wins), sorted.
    [[nodiscard]] std::vector<std::string> files() const;

    [[nodiscard]] std::size_t cachedBytes() const;

private:
    struct Mount {
        Layer layer;
        std::unordered_map<std::string, std::filesystem::path> index; // canonical path -> file
    };
    Bytes readFileCached(const std::filesystem::path& file, bool decompress) const;
    Bytes cacheGet(const std::string& key) const;
    void cachePut(const std::string& key, const Bytes& b) const;

    std::vector<Mount> mounts_; // highest precedence first

    // ponytail: one mutex around an LRU list of whole files, capped at cacheCap_ bytes; a file
    // larger than the cap is returned uncached. Per-shard locks / mmap if contention ever shows.
    std::size_t cacheCap_;
    mutable std::mutex mu_;
    mutable std::size_t cacheSize_ = 0;
    mutable std::list<std::pair<std::string, Bytes>> lru_;
    mutable std::unordered_map<std::string, std::list<std::pair<std::string, Bytes>>::iterator> lruIndex_;
};

} // namespace wc98::assets
