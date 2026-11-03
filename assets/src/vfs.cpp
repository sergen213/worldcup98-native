// Derived from fifa99-native (same author), ported for World Cup 98
#include "wc98/assets/vfs.hpp"
#include "wc98/assets/install.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/big.hpp"
#include "wc98/formats/refpack.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace wc98::assets {

Vfs::Vfs(std::size_t cacheBytes) : cacheCap_(cacheBytes) {}

void Vfs::mountTree(const fs::path& root, Layer layer) {
    Mount m{layer, {}};
    if (!fs::is_directory(root)) throw std::runtime_error("not a directory: " + root.string());
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        std::string g = normalizeGamePath(fs::relative(e.path(), root).generic_string());
        if (!g.empty()) m.index.emplace(std::move(g), e.path());
    }
    auto pos = std::ranges::find_if(mounts_, [&](const Mount& x) { return x.layer <= layer; });
    mounts_.insert(pos, std::move(m));
}

std::optional<Vfs::Resolved> Vfs::resolve(std::string_view gamePath) const {
    std::string g = normalizeGamePath(gamePath);
    if (g.empty()) return std::nullopt;
    for (const auto& m : mounts_)
        if (auto it = m.index.find(g); it != m.index.end()) return Resolved{it->second, m.layer};
    return std::nullopt;
}

bool Vfs::exists(std::string_view path) const {
    if (path.find(':') == std::string_view::npos) return resolve(path).has_value();
    try {
        (void)read(path);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

Vfs::Bytes Vfs::read(std::string_view path, bool decompress) const {
    std::vector<std::string_view> parts;
    for (std::size_t s = 0;;) {
        std::size_t c = path.find(':', s);
        parts.push_back(path.substr(s, c == path.npos ? path.npos : c - s));
        if (c == path.npos) break;
        s = c + 1;
    }
    auto res = resolve(parts[0]);
    if (!res) throw std::runtime_error("not found: " + std::string(parts[0]));
    if (parts.size() == 1) return readFileCached(res->file, decompress);

    std::string key = res->file.string();
    for (std::size_t i = 1; i < parts.size(); ++i) key += ":" + std::string(parts[i]);
    key += decompress ? "|d" : "|r";
    if (auto hit = cacheGet(key)) return hit;

    Bytes container = readFileCached(res->file, true); // a whole BIG may itself be RefPack-packed
    for (std::size_t i = 1; i < parts.size(); ++i) {
        auto big = formats::BigArchive::parse(*container);
        const auto* e = big.find(parts[i]);
        if (!e) throw std::runtime_error("no member '" + std::string(parts[i]) + "' in " + std::string(path));
        auto payload = formats::BigArchive::payload(*container, *e);
        bool last = i + 1 == parts.size();
        container = std::make_shared<const std::vector<std::uint8_t>>(
            !last || decompress ? formats::refpack::decompressIfNeeded(payload) : std::vector<std::uint8_t>(payload.begin(), payload.end()));
    }
    cachePut(key, container);
    return container;
}

std::vector<std::string> Vfs::files() const {
    std::set<std::string> all;
    for (const auto& m : mounts_)
        for (const auto& [g, p] : m.index) all.insert(g);
    return {all.begin(), all.end()};
}

std::size_t Vfs::cachedBytes() const {
    std::lock_guard l(mu_);
    return cacheSize_;
}

Vfs::Bytes Vfs::readFileCached(const fs::path& file, bool decompress) const {
    std::string key = file.string() + (decompress ? "|d" : "|r");
    if (auto hit = cacheGet(key)) return hit;
    auto raw = readFile(file); // outside the lock: concurrent misses may read twice, harmless
    auto b = std::make_shared<const std::vector<std::uint8_t>>(decompress ? formats::refpack::decompressIfNeeded(raw) : std::move(raw));
    cachePut(key, b);
    return b;
}

Vfs::Bytes Vfs::cacheGet(const std::string& key) const {
    std::lock_guard l(mu_);
    auto it = lruIndex_.find(key);
    if (it == lruIndex_.end()) return nullptr;
    lru_.splice(lru_.begin(), lru_, it->second);
    return it->second->second;
}

void Vfs::cachePut(const std::string& key, const Bytes& b) const {
    if (b->size() > cacheCap_) return;
    std::lock_guard l(mu_);
    if (lruIndex_.contains(key)) return;
    lru_.emplace_front(key, b);
    lruIndex_.emplace(key, lru_.begin());
    cacheSize_ += b->size();
    while (cacheSize_ > cacheCap_) {
        cacheSize_ -= lru_.back().second->size();
        lruIndex_.erase(lru_.back().first);
        lru_.pop_back();
    }
}

} // namespace wc98::assets
