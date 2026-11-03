#include <cstring>
#include <type_traits>

#include "wc98/sim/world.hpp"

namespace wc98::sim {

namespace {

template <class T>
void hashBytes(std::uint64_t& h, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "sim state must be trivially copyable");
    static_assert(std::has_unique_object_representations_v<T>, "sim state must have no padding");
    unsigned char b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));
    for (unsigned char c : b) h = (h ^ c) * 0x100000001b3ull;
}

} // namespace

std::uint64_t stateHash(const World& w) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    hashBytes(h, w.rng);
    hashBytes(h, w.time);
    hashBytes(h, w.territory);
    hashBytes(h, w.ball);
    hashBytes(h, w.match);
    hashBytes(h, w.teams);
    hashBytes(h, w.formation);
    hashBytes(h, w.players);
    hashBytes(h, w.controllers);
    hashBytes(h, w.attrs);
    hashBytes(h, w.pl);
    hashBytes(h, w.anim);
    return h;
}

} // namespace wc98::sim
