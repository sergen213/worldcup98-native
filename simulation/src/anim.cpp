// Animation engine (0x45F000..0x461000) and the player animation layer (0x4CBD9C..0x4CDD54) of
// FIFA99.EXE v1.1, ported from the disassembly. All player root motion comes from here.
// Exe pointers are kept as indices: instance links = index + 1, clip = AnimData::clips index,
// callbacks = exe addresses. The per-instance key decode cache (+0xAC..) is replaced by direct
// key access with the same clamping (0x5034F0 / 0x5006C0 clamp the key index to [0, keys - 1]).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "wc98/file.hpp"
#include "wc98/reader.hpp" // FormatError from rig pose decoding
#include "wc98/formats/big.hpp"
#include "wc98/formats/refpack.hpp"
#include "wc98/sim/anim.hpp"
#include "wc98/sim/ball.hpp"
#include "wc98/sim/data.hpp"
#include "wc98/sim/world.hpp"

#include "player_states.hpp"
namespace wc98::sim {

namespace {

using u32 = std::uint32_t;

constexpr u32 kCbSynced = 0x4CC2A4, kCbNormal = 0x4CC408, kCbChained = 0x4CC520, kCbScripted = 0x4CC608;

const AnimData& AD(const World& w) { return w.data->anim; }
AnimInstance& I(World& w, int ref) { return w.anim.inst[static_cast<std::size_t>(ref - 1)]; }
const AnimInstance& I(const World& w, int ref) { return w.anim.inst[static_cast<std::size_t>(ref - 1)]; }
int refOf(const World& w, const AnimInstance& a) { return static_cast<int>(&a - w.anim.inst.data()) + 1; }

const AnimClip* clipOf(const World& w, int clip) { // an instance without a clip reads an empty one
    static const AnimClip kNone{};
    const auto& c = AD(w).clips;
    return clip >= 0 && clip < static_cast<int>(c.size()) ? &c[static_cast<std::size_t>(clip)] : &kNone;
}
int keyShift(const AnimClip& c) { return c.flags & 1; } // key span 0x200 << f (8.8 frames)
// Channel `ch` of key k, index clamped like 0x5034F0 / 0x5006C0.
std::int32_t keyCh(const AnimClip& c, std::int32_t k, int ch) {
    if (c.keys.empty()) return 0;
    const std::int32_t n = static_cast<std::int32_t>(c.keys.size());
    k = k < 0 ? 0 : (k >= n ? n - 1 : k);
    return c.keys[static_cast<std::size_t>(k)][static_cast<std::size_t>(ch)];
}
// 0x500910: clip duration in 8.8 frames.
std::int32_t clipDuration(const AnimClip& c) { return (static_cast<std::int32_t>(c.keyCount) - 1) * (0x200 << keyShift(c)); }
std::int32_t mark0(const AnimClip& c) { return c.marks[0] * 0x100; }
std::int32_t mark1(const AnimClip& c) { return c.marks[1] * 0x100 - 0x200; }
// Selector of 0x4CD3B0 / 0x4CD70C / 0x4CD998 / 0x4CDA80 -> 8.8 time.
std::int32_t selTime(const AnimClip& c, std::int32_t sel) {
    if (sel == -100) return mark0(c);
    if (sel == -99) return mark1(c);
    if (sel == -98) return c.marks[2] * 0x100 >= 0 ? c.marks[2] * 0x100 : mark0(c);
    if (sel == -97) return clipDuration(c);
    return sel << 8;
}
// 0x500850 / 0x5008B0: root (ch3..5) or ball bone (ch6..8) at an 8.8 time, nearest key below.
Vec3 clipPoint(const AnimClip& c, std::int32_t time, bool bone, bool mirror) {
    const std::int32_t k = time >> (9 + keyShift(c));
    const int b = bone ? 6 : 3;
    Vec3 v{keyCh(c, k, b) << 12, keyCh(c, k, b + 1) << 12, keyCh(c, k, b + 2) << 12};
    if (mirror) v.x = -v.x;
    return v;
}
std::int32_t wrap200(std::int32_t a) {
    a &= 0x3ff;
    return a > 0x200 ? a - 0x400 : a;
}

// ---- engine (instance level) ----

int alloc(World& w) { // 0x45FA10
    AnimShared& s = w.anim;
    const int r = s.freeHead;
    AnimInstance& a = I(w, r);
    s.freeHead = a.prev;
    a.next = 0;
    a.prev = 0;
    --s.freeCount;
    a.clip = -1;
    a.flags |= 0x80000000u;
    a.time = 0;
    return r;
}

int insertAfter(World& w, int ref) { // 0x45FA54: new instance becomes the newer neighbour of ref
    const int n = alloc(w);
    AnimInstance& a = I(w, n);
    a.prev = ref;
    if (ref) {
        AnimInstance& o = I(w, ref);
        a.next = o.next;
        if (a.next) I(w, a.next).prev = n;
        o.next = n;
    } else {
        a.next = ref;
    }
    return n;
}

int freeInst(World& w, int ref) { // 0x45FA84: returns prev ? prev : next
    AnimShared& s = w.anim;
    AnimInstance& a = I(w, ref);
    a.flags &= 0x7fffffffu;
    a.clip = -1;
    const int nx = a.next, pv = a.prev;
    if (nx) {
        I(w, nx).prev = pv;
        if (!pv) I(w, nx).flags &= ~0x20u;
    }
    int ret;
    if (pv) {
        I(w, pv).next = nx;
        ret = pv;
    } else {
        ret = nx;
    }
    a.prev = s.freeHead;
    s.freeHead = ref;
    ++s.freeCount;
    return ret;
}

int settle(World& w, int ref) { // 0x45FB08: full weight, drop the fading-out older instances
    AnimInstance& a = I(w, ref);
    a.weight = 0x10000;
    a.flags &= ~0xa0u;
    while (a.prev && (I(w, a.prev).flags & 0x40)) {
        if (!freeInst(w, a.prev)) break;
    }
    return ref;
}

void setRate(World& w, int ref, Fixed r) { I(w, ref).rate = r < 0 ? 0 : r; } // 0x45FB44 (fild vs 0.0)

void setAngle(World& w, int ref, std::int32_t angle) { // 0x45FB68
    AnimInstance& a = I(w, ref);
    if (angle == a.angle) return;
    a.angle = angle;
    const SinCos sc = sinCos(-angle);
    a.sinA = sc.s; // [+0x58] = first output (sin)
    a.cosA = sc.c; // [+0x54] = second output (cos)
}

void keys(World& w, AnimInstance& a) { // 0x45FC30
    const AnimClip& c = *clipOf(w, a.clip);
    const std::int32_t t = (a.time >> (keyShift(c) + 1)) << 8;
    a.frac = t & 0xffff;
    const std::int32_t k = t >> 16;
    if (k == a.key) return;
    a.key = k;
    a.keyA = k;
    a.keyB = k + 1;
    a.cache = 8;
}

void yaw(World& w, AnimInstance& a) { // 0x45FC9C
    if (!(a.cache & 8)) keys(w, a);
    const AnimClip& c = *clipOf(w, a.clip);
    const std::int32_t ya = keyCh(c, a.keyA, 1), yb = keyCh(c, a.keyB, 1);
    const std::int32_t d2 = (yb - ya) & 0x3ff, d1 = (ya - yb) & 0x3ff;
    // esi = (yb - ya) & 0x3ff, eax = (ya - yb) & 0x3ff; esi < eax -> ya + fx(esi, frac)
    std::int32_t v = ya + (d2 < d1 ? fx(d2, a.frac) : fx(d1, 0x10000 - a.frac)); // (sic: d1 with 1 - frac)
    a.yaw = (a.flags & 1) ? -v : v;
    a.cache |= 4;
}

void root(World& w, AnimInstance& a, bool noAccum) { // 0x45FD24
    if (!(a.cache & 8)) keys(w, a);
    const AnimClip& c = *clipOf(w, a.clip);
    Vec3 v;
    std::int32_t* out[3] = {&v.x, &v.y, &v.z};
    for (int i = 0; i < 3; ++i) {
        const std::int32_t lo = keyCh(c, a.keyA, 3 + i) << 12, hi = keyCh(c, a.keyB, 3 + i) << 12;
        *out[i] = lo + fx(a.frac, hi - lo);
    }
    if (a.flags & 1) v.x = -v.x;
    a.curRoot = v;
    const std::int32_t dx = (a.flags & 0x200) ? 0 : a.curRoot.x - a.prevRoot.x;
    const std::int32_t dz = (a.flags & 0x400) ? 0 : a.curRoot.z - a.prevRoot.z;
    a.prevRoot = a.curRoot;
    a.cache |= 1;
    if ((dx || dz) && !noAccum) {
        a.root.x += fx(dx, a.cosA) + fx(dz, -a.sinA);
        a.root.y = v.y;
        a.root.z += fx(dx, a.sinA) + fx(dz, a.cosA);
    }
}

void bone(World& w, AnimInstance& a, Fixed scale) { // 0x45FEBC
    if (!(a.cache & 8)) keys(w, a);
    if (!(a.cache & 1)) root(w, a, false);
    const AnimClip& c = *clipOf(w, a.clip);
    std::int32_t b[3];
    for (int i = 0; i < 3; ++i) {
        const std::int32_t lo = keyCh(c, a.keyA, 6 + i) << 12, hi = keyCh(c, a.keyB, 6 + i) << 12;
        b[i] = lo + fx(a.frac, hi - lo);
    }
    const std::int32_t ex = (a.flags & 1) ? -(b[0] + a.curRoot.x) : b[0] - a.curRoot.x;
    std::int32_t y = fx(0x300000, scale) + b[1] - 0x300000;
    a.bone.y = y < 0 ? 0 : y;
    const std::int32_t ez = b[2] - a.curRoot.z;
    a.bone.x = a.root.x + fx(ex, a.cosA) + fx(ez, -a.sinA);
    a.bone.z = a.root.z + fx(ex, a.sinA) + fx(ez, a.cosA);
    a.cache |= 2;
}

Vec3 cachedBone(World& w, AnimInstance& a, Fixed scale) { // 0x460778
    if (!(a.cache & 2)) bone(w, a, scale);
    return a.bone;
}
Vec3 rootXZ(World& w, AnimInstance& a) { // 0x4607A0
    if (!(a.cache & 1)) root(w, a, false);
    return {a.root.x, 0, a.root.z};
}
Fixed rootY(World& w, AnimInstance& a) { // 0x4607CC
    if (!(a.cache & 1)) root(w, a, false);
    return a.root.y;
}
void setRootXZ(World& w, AnimInstance& a, const Vec3& p) { // 0x45FB8C
    if (a.clip >= 0 && !(a.cache & 1)) root(w, a, false);
    a.root.x = p.x;
    a.root.z = p.z;
}
std::int32_t instYaw(World& w, AnimInstance& a) { // 0x4607EC
    if (a.flags & 0x1000) return a.angle;
    if (!(a.cache & 4)) yaw(w, a);
    return a.angle + a.yaw;
}
// 0x500650: foot flags of a mirrored clip (swap bits 1/2 and groups 3..5 / 6..8).
std::int32_t mirrorFeet(std::int32_t f) {
    return (f & 1) | (((f >> 2) & 1) << 1) | (((f >> 1) & 1) << 2) | (((f >> 6) & 7) << 3) | (((f >> 3) & 7) << 6) |
           (((f >> 9) & 3) << 9);
}
std::int32_t footFlags(World& w, AnimInstance& a) { // 0x460818
    if (!(a.cache & 8)) keys(w, a);
    const std::int32_t f = keyCh(*clipOf(w, a.clip), a.keyA, 0);
    return (a.flags & 1) ? mirrorFeet(f) : f;
}

void callback(World& w, AnimInstance& a);

void markFlags(World& w, AnimInstance& a) { // 0x460594
    const AnimClip& c = *clipOf(w, a.clip);
    const std::int32_t m0 = mark0(c), m1 = mark1(c);
    if (m0 >= 0 && m0 <= a.time && !(a.flags & 0x50000)) a.flags |= 0x14000;
    if (m1 >= 0 && m1 <= a.time && !(a.flags & 0x40000)) a.flags = (a.flags & 0xfffa7fffu) | 0x48000;
}

struct Desc { // 0x6757CC
    std::int32_t clip, f1c;
    u32 iflags;
    std::int32_t start, animId, pid;
    u32 cb;
    std::int32_t angle;
    Vec3 pos;
};

void setup(World& w, AnimInstance& a, const Desc& d) { // 0x460440
    a.clip = d.clip;
    const u32 keep = a.flags & 0x80000020u;
    std::int32_t base = d.start;
    if (a.prev) {
        const AnimInstance& p = I(w, a.prev);
        if ((p.flags & 0x80000) && p.savedTime > p.time) base -= p.savedTime - p.time;
    }
    a.weight = 0;
    a.weightRate = 0;
    a.rate = 0x10000;
    a.f38 = 0x100;
    a.savedTime = d.start;
    a.time = d.start;
    a.flags = d.iflags | keep;
    const AnimClip* c = clipOf(w, a.clip);
    a.duration = c ? fx(clipDuration(*c), a.rate) : 0;
    if (a.duration == 0) a.duration = 0x200;
    a.f1c = d.f1c;
    a.animId = d.animId;
    a.pid = d.pid;
    a.callback = d.cb;
    a.markBase = base;
    a.angle = d.angle;
    const SinCos sc = sinCos(-d.angle);
    a.sinA = sc.s;
    a.cosA = sc.c;
    a.root = d.pos;
    a.headYaw = 0;
    a.headPitch = 0;
    a.cache = 0;
    a.key = -1;
    root(w, a, true);
}

int playNoBlend(World& w, std::int32_t& head, const Desc& d) { // 0x460604
    if (head) settle(w, head);
    else head = alloc(w);
    AnimInstance& a = I(w, head);
    setup(w, a, d);
    a.stamp = w.anim.clock;
    a.weight = 0x10000;
    a.flags &= ~0x20u;
    if (a.callback) {
        markFlags(w, a);
        callback(w, a);
    }
    return head;
}

int playBlend(World& w, std::int32_t& head, const Desc& d, std::int32_t blend) { // 0x460674 (blend 8.8)
    if (!head || blend < 0x200 || w.anim.freeCount == 0) return playNoBlend(w, head, d);
    if (I(w, head).prev) settle(w, head);
    AnimInstance& old = I(w, head);
    if (!(old.flags & 4)) blend = std::min(blend, old.duration - old.time);
    if (blend < 0x200) return playNoBlend(w, head, d);
    const int oldRef = head;
    const int n = insertAfter(w, oldRef);
    head = n;
    I(w, oldRef).flags |= 0x40;
    AnimInstance& a = I(w, n);
    setup(w, a, d);
    a.flags |= 0xa2;
    const std::int32_t frames = (blend + ((blend >> 31) & 0xff)) >> 8; // cdq-style sar of a signed value
    std::int32_t r = static_cast<std::int32_t>(0x100000000LL / (static_cast<std::int64_t>(frames) << 16));
    if (r < 0x100) r = 0x100;
    a.weightRate = r;
    I(w, oldRef).weight = 0x10000 - r;
    a.weight = r;
    a.stamp = w.anim.clock;
    if (a.callback) {
        markFlags(w, a);
        callback(w, a);
    }
    return n;
}

// Invoke the instance callback around a moment `t` (snap = time and markBase moved there).
void fireAt(World& w, AnimInstance& a, AnimInstance* partner, bool snap, std::int32_t t) {
    a.savedTime = a.time;
    if (snap) {
        a.time = t;
        a.markBase = t;
    }
    if (!(a.cache & 8)) keys(w, a);
    if (partner && !(partner->cache & 8)) keys(w, *partner);
    callback(w, a);
    a.time = a.savedTime;
}

void advance(World& w, std::int32_t* slot) { // 0x460050 (EAX = link slot holding the instance)
    AnimInstance* ap = &I(w, *slot);
    AnimInstance& a = *ap;
    if (a.flags & 0x40) return;
    const AnimClip& c = *clipOf(w, a.clip);
    const std::int32_t m0 = mark0(c), m1 = mark1(c);
    const bool snap = !(a.flags & 0x2000); // edi: marks snap time/markBase unless flag 0x2000
    std::int32_t dt = w.anim.clock - a.stamp;
    if (a.stamp >= w.anim.clock) return;
    if (dt > 0xc00) dt = 0xc00;
    for (;;) {
        const std::int32_t step = fx(dt, a.rate);
        if (step == 0) return;
        AnimInstance* partner = (a.flags & 0x20) ? &I(w, a.prev) : nullptr;
        if (!(a.flags & 0x100)) {
            a.time += step;
            a.cache = 0;
            if (partner) {
                partner->time += step;
                partner->cache = 0;
                if (partner->time >= partner->duration) {
                    if (partner->flags & 4) {
                        while (partner->time >= partner->duration) partner->time -= partner->duration;
                    } else if (!(partner->flags & 0x800)) {
                        settle(w, refOf(w, a));
                        partner = nullptr;
                    }
                }
            }
        }
        if (a.time < a.duration) {
            if (m0 >= 0 && m0 <= a.time && !(a.flags & 0x50000)) {
                a.flags |= 0x14000;
                if (a.callback) fireAt(w, a, partner, snap, m0);
            }
            if (m1 >= 0 && m1 <= a.time && !(a.flags & 0x40000)) {
                a.flags = (a.flags & 0xfffa7fffu) | 0x48000;
                if (a.callback) fireAt(w, a, partner, snap, m1);
            }
        } else if (a.flags & 4) { // loop
            a.flags |= 0x20000;
            while (a.time >= a.duration) {
                a.time -= a.duration;
                a.markBase -= a.duration;
            }
            a.savedTime = a.time;
            a.time = a.duration;
            a.cache = 0;
            root(w, a, false);
            if (a.callback) {
                if (snap) a.markBase = a.time;
                if (!(a.cache & 8)) keys(w, a);
                if (partner && !(partner->cache & 8)) keys(w, *partner);
                callback(w, a);
            }
            a.time = a.savedTime;
            a.cache = 0;
            root(w, a, true);
            if (snap) a.markBase = -1;
        } else {
            a.flags |= 0x10;
            if (a.callback) fireAt(w, a, partner, snap, a.duration);
        }
        if (a.callback && a.time - a.markBase >= 0x400) { // periodic re-arm every 4 frames
            a.savedTime = a.time;
            a.flags |= 0x80000;
            a.markBase += 0x400;
            a.time = a.markBase;
            if (!(a.cache & 8)) keys(w, a);
            if (partner && !(partner->cache & 8)) keys(w, *partner);
            callback(w, a);
            a.time = a.savedTime;
        }
        if (a.flags & 0x10) { // ended
            if (a.flags & 0x20) settle(w, refOf(w, a));
            if (*slot == refOf(w, a)) *slot = freeInst(w, *slot);
            else freeInst(w, refOf(w, a));
            return;
        }
        if (a.flags & 0x80) { // fading in
            a.weight += (step * a.weightRate) >> 8;
            if (a.weight >= 0x10000) {
                settle(w, refOf(w, a));
                a.weight = 0x10000;
                a.flags &= ~2u;
            } else if (a.weight <= 0) {
                freeInst(w, refOf(w, a));
                return;
            }
        }
        a.stamp = w.anim.clock;
        // 0x4603FF jumps back to the stamp test: a second pass sees dt = 0 and stops.
        dt = w.anim.clock - a.stamp;
        if (a.stamp >= w.anim.clock) return;
    }
}

void advanceList(World& w, std::int32_t* head) { // 0x46041C
    std::int32_t* slot = head;
    while (*slot) {
        advance(w, slot);
        if (*slot) slot = &I(w, *slot).prev;
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Loading (load-time passes on MatchData)

bool parseAnimSeqTable(AnimData& a, const std::vector<std::uint8_t>& tbi) {
    auto u32at = [&](std::size_t o) {
        return static_cast<std::uint32_t>(tbi[o] | (tbi[o + 1] << 8) | (tbi[o + 2] << 16) | (static_cast<u32>(tbi[o + 3]) << 24));
    };
    auto u16at = [&](std::size_t o) { return static_cast<std::uint16_t>(tbi[o] | (tbi[o + 1] << 8)); };
    if (tbi.size() < 8) return false;
    const u32 count = u32at(0), namesOff = u32at(4);
    if (8 + static_cast<std::size_t>(count) * 0x14 > tbi.size() || namesOff > tbi.size()) return false;
    a.clips.clear();
    for (std::size_t o = namesOff; o + 0x18 <= tbi.size() && tbi[o]; o += 0x18) {
        AnimClip c;
        c.name.assign(reinterpret_cast<const char*>(&tbi[o]), strnlen(reinterpret_cast<const char*>(&tbi[o]), 10));
        a.clips.push_back(std::move(c));
    }
    a.seqs.resize(count);
    for (u32 i = 0; i < count; ++i) {
        const std::size_t o = 8 + static_cast<std::size_t>(i) * 0x14;
        AnimSeq& s = a.seqs[i];
        s.id = u16at(o);
        s.next = u16at(o + 2);
        s.fallback = u16at(o + 4);
        s.angle = static_cast<std::int16_t>(u16at(o + 6));
        const u32 nameOff = u32at(o + 8); // fixed up to a pointer by 0x4CBD9C
        s.clip = nameOff >= namesOff && (nameOff - namesOff) % 0x18 == 0 &&
                         (nameOff - namesOff) / 0x18 < a.clips.size()
                     ? static_cast<std::int32_t>((nameOff - namesOff) / 0x18)
                     : -1;
        s.blendIn = u16at(o + 0xc);
        s.blendOut = u16at(o + 0xe);
        s.flags = u32at(o + 0x10);
    }
    return true;
}

namespace {

int seqResolve(const AnimData& a, int id) { // 0x4CC984 on loaded data
    const AnimSeq& s = a.seqs[static_cast<std::size_t>(id)];
    if (s.flags & 0x80) return id;
    if (s.clip >= 0 && a.clips[static_cast<std::size_t>(s.clip)].loaded) return id;
    return s.fallback;
}

bool parseClip(AnimClip& c, const std::vector<std::uint8_t>& d) {
    auto u16at = [&](std::size_t o) { return static_cast<std::uint16_t>(d[o] | (d[o + 1] << 8)); };
    if (d.size() < 0x12) return false;
    const std::size_t header = u16at(2);
    c.keyCount = u16at(6);
    c.marks[0] = static_cast<std::int16_t>(u16at(8));
    c.marks[1] = static_cast<std::int16_t>(u16at(10));
    c.marks[2] = static_cast<std::int16_t>(u16at(12));
    c.flags = u16at(0xe);
    c.keys.clear();
    std::size_t o = header;
    for (int k = 0; k < c.keyCount; ++k) { // key blocks: u16 size, then raw ch0..ch8
        if (o + 20 > d.size()) return false;
        std::array<std::int16_t, 9> ch{};
        for (int i = 0; i < 9; ++i) ch[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(u16at(o + 2 + 2 * static_cast<std::size_t>(i)));
        c.keys.push_back(ch);
        const std::size_t sz = u16at(o);
        if (sz == 0) return false;
        o += sz;
    }
    return true;
}

bool sameName(const std::string& a, std::string_view b) {
    if (b.size() > 10) b = b.substr(0, 10);
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// 0x4CBDD4: blend frames limited by the clip length / marks.
void blendPass(AnimData& a) {
    for (AnimSeq& s : a.seqs) {
        const AnimClip* c = s.clip >= 0 ? &a.clips[static_cast<std::size_t>(s.clip)] : nullptr;
        if (!(s.flags & 0x80) && !(c && c->loaded)) continue;
        if (!c) continue;
        const std::int32_t frames = clipDuration(*c) >> 8;
        const std::int32_t m1 = mark1(*c) >> 8, m0 = mark0(*c) >> 8;
        if (m1 >= 0 || m0 >= 0) s.flags |= 0x40;
        std::int32_t lim = frames > m0 ? frames >> 1 : m0;
        if (lim < s.blendIn) s.blendIn = static_cast<std::uint16_t>(lim & 1 ? lim - 1 : lim);
        lim = frames > m1 ? frames >> 1 : m1;
        if (lim < s.blendOut) s.blendOut = static_cast<std::uint16_t>(lim & 1 ? lim - 1 : lim);
    }
}

// Locomotion tables 0x59F81C..0x59FC2F (tools/sim-tables/dump.py), resolved by 0x4CBEA0.
#include "anim_tables.inc"

std::int32_t contactInfoData(const AnimData& a, int anim, int edx, int ecx, int ebx, int s1, std::int32_t* out44,
                             std::int32_t* out48);

void locoPass(AnimData& a) { // 0x4CBEA0
    std::memcpy(a.gaitType, kGaitType, sizeof a.gaitType);
    std::memcpy(a.loco, kLoco, sizeof a.loco);
    std::memcpy(a.turnL, kTurnL, sizeof a.turnL);
    std::memcpy(a.turnR, kTurnR, sizeof a.turnR);
    std::memcpy(a.skillA, kSkillA, sizeof a.skillA);
    std::memcpy(a.skillB, kSkillB, sizeof a.skillB);
    // The tables' "no clip" filler is the end of the loaded seq table in both families (FIFA-99
    // uses 0x1c4 with a 452-entry table; WC98's TBI carries 534 records so its filler id is 534).
    // Normalize to the 0x1c4 sentinel the consumers test (anim.cpp l.841) and keep every resolved
    // id inside a.seqs (locoPass runs before any clip load, on the raw table ids).
    const std::uint16_t kSeqEnd = static_cast<std::uint16_t>(a.seqs.size());
    auto res = [&](std::uint16_t v) { return v < kSeqEnd ? seqResolve(a, v) : 0x1c4; };
    for (int r = 0; r < 6; ++r)
        for (int j = 0; j < 11; ++j) {
            a.turnL[r][j] = static_cast<std::uint16_t>(res(a.turnL[r][j]));
            a.turnR[r][j] = static_cast<std::uint16_t>(res(a.turnR[r][j]));
            const int id = res(a.loco[r][j][0]);
            std::int32_t dist = 0, ang = 0;
            const std::int32_t frames = contactInfoData(a, id, 0, 0, -97, 0, &dist, &ang);
            const std::int64_t num = static_cast<std::int64_t>(dist) << 16, den = static_cast<std::int64_t>(frames) << 16;
            std::int32_t q = 0;
            if (den != 0) { // fdiv2: integer quotient then 16-bit fraction of the remainder
                const std::int32_t q1 = static_cast<std::int32_t>(static_cast<std::int32_t>(num) / static_cast<std::int32_t>(den));
                const std::int32_t rm = static_cast<std::int32_t>(static_cast<std::int32_t>(num) % static_cast<std::int32_t>(den));
                q = (q1 << 16) + static_cast<std::int32_t>((static_cast<std::int64_t>(rm) << 16) / static_cast<std::int32_t>(den));
            }
            a.loco[r][j][0] = static_cast<std::uint16_t>(id);
            a.loco[r][j][1] = static_cast<std::uint16_t>(q >> 5);
        }
    for (int r = 0; r < 6; ++r)
        for (int j = 0; j < 11; ++j) { // the exe never advances the skillB pointer (esi): kept
            std::uint16_t& sa = a.skillA[r][j];
            if (sa != kSeqEnd && sa != 0x1c4) {
                sa = static_cast<std::uint16_t>(res(sa));
                if (a.seqs[sa].flags & 1) sa = 0x1c4;
            } else
                sa = 0x1c4;
            std::uint16_t& sb = a.skillB[r][0];
            if (sb != kSeqEnd && sb != 0x1c4) {
                sb = static_cast<std::uint16_t>(res(sb));
                if (a.seqs[sb].flags & 1) a.skillA[r][j] = 0x1c4;
            } else
                a.skillB[r][0] = 0x1c4;
        }
}

} // namespace

bool loadMatchAnimData(MatchData& d, const std::filesystem::path& gameDir) {
    // WC98 tree layout (SIM_PORT_NOTES.md section 5): AISEQS.TBI and the ANIM.MO BIGF live in COMMON/.
    const auto tbiPath = findCaseInsensitive(gameDir, "COMMON/AISEQS.TBI");
    const auto moPath = findCaseInsensitive(gameDir, "COMMON/ANIM.MO");
    if (tbiPath.empty() || moPath.empty()) return false;
    AnimData& a = d.anim;
    if (!parseAnimSeqTable(a, readFile(tbiPath))) return false;
    const auto mo = readFile(moPath);
    const auto big = formats::BigArchive::parse(mo);
    // 0x45F8C0: every archive member whose name matches a name entry loads that clip.
    for (const auto& e : big.entries) {
        std::string_view n = e.name;
        if (const auto p = n.find_last_of("\\/"); p != std::string_view::npos) n = n.substr(p + 1);
        if (const auto p = n.find('.'); p != std::string_view::npos) n = n.substr(0, p);
        for (AnimClip& c : a.clips)
            if (!c.loaded && sameName(c.name, n)) {
                const auto raw = formats::refpack::decompressIfNeeded(formats::BigArchive::payload(mo, e));
                // WC98 adaptation: keep the full-channel decode (triplets/quaternions) beside the
                // ch0..8 root-motion keys; Anim_evalSkeleton FK-evaluates it through PlayerRig.
                if (parseClip(c, raw)) {
                    try { c.fmt = std::make_shared<formats::AnimClip>(formats::AnimClip::parse(raw)); } catch (...) {}
                    c.loaded = true;
                }
                break;
            }
    }
    // WC98 adaptation: the PCMD player rig (joint hierarchy + static offsets) for Anim_evalSkeleton.
    const auto pcPath = findCaseInsensitive(gameDir, "INGAME/PLAYER/MODELS/HIG_30.PC");
    if (!pcPath.empty()) {
        try { a.rig = formats::PlayerRig::fromModel(formats::PcmdModel::parse(readFile(pcPath))); a.rigLoaded = true; } catch (...) {}
    }
    blendPass(a);
    locoPass(a);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Engine entry points

void Anim_initEngine(World& w) { // 0x4CC6B0 state part: memset 0x6757C8, pool 0x45F710/0x45F648, clock 0
    AnimShared& s = w.anim;
    s = AnimShared{};
    for (int i = 0; i < kAnimInstances; ++i) {
        s.inst[static_cast<std::size_t>(i)].prev = i + 1 < kAnimInstances ? i + 2 : 0;
        s.inst[static_cast<std::size_t>(i)].clip = -1;
    }
    s.freeHead = 1;
    s.freeCount = kAnimInstances;
    s.clock = 0;
}
void Anim_initTables_4CC6B0(World& w) { Anim_initEngine(w); }
void Anim_resetEngine_45F648(World& w) { // pool reset only
    const std::int32_t aim = w.anim.aimRate, mp = w.anim.motionPid;
    Anim_initEngine(w);
    w.anim.aimRate = aim;
    w.anim.motionPid = mp;
}

void Players_animate(World& w, Fixed simDt) { // 0x4CC8F4
    w.anim.aimRate = (simDt >> 16) * 9;
    w.anim.clock += simDt >> 8; // 0x45F7D8
}

// ---------------------------------------------------------------------------------------------
// Player layer (ctl = player+0x64)

namespace {

AnimCtl& C(World& w, int pid) { return w.players[static_cast<std::size_t>(pid)].anim; }
const AnimSeq& seqRec(const World& w, int id) { return AD(w).seqs[static_cast<std::size_t>(id)]; }
int seqId(const AnimCtl& c) { return c.seq - 1; }
Vec3 mpos16(const PlayerState& p) { return {(p.posI.x << 16) + p.frac.x, (p.posI.y << 16) + p.frac.y, (p.posI.z << 16) + p.frac.z}; }

void dribbleBone(World& w, AnimInstance& a, AnimCtl& ctl) { // shared head of the callbacks
    if (ctl.flags() & 0x10) {
        const Vec3 v = cachedBone(w, a, 0x10000);
        std::int32_t y = (v.y >> 16) - 8;
        Ball_setDribblePosition(w, {v.x >> 16, y < 0 ? 0 : y, v.z >> 16});
    }
    ctl.flags() &= ~0x10;
}

// 0x545530: start time of a synced locomotion clip matching the old clip's gait phase.
std::int32_t syncStart(World& w, const Desc& d, int oldRef) {
    if (!oldRef) return 0;
    AnimInstance& o = I(w, oldRef);
    if (!(o.cache & 8)) keys(w, o);
    const AnimClip& oc = *clipOf(w, o.clip);
    std::int32_t pa = keyCh(oc, o.keyA, 2), pb = keyCh(oc, o.keyB, 2);
    if (o.flags & 1) {
        pa = -pa;
        pb = -pb;
    }
    const AnimClip& c = *clipOf(w, d.clip);
    const bool mirror = d.iflags & 1, loop = d.iflags & 4;
    std::int32_t n = loop ? c.keyCount - 1 : c.keyCount; // 0x50093C (limit 6)
    if (n >= 6) n = 6;
    int best = -1;
    std::int32_t bestv = 1000000;
    for (int i = 0; i < n; ++i) {
        std::int32_t x0 = keyCh(c, i, 2), x1 = keyCh(c, i + 1, 2);
        if (mirror) {
            x0 = -x0;
            x1 = -x1;
        }
        std::int32_t e1 = (x1 - x0) - (pb - pa);
        if (e1 <= 0) e1 = -e1;
        std::int32_t e2 = x1 - pb;
        if (e2 <= 0) e2 = -e2;
        e2 += e1;
        if (best < 0 || e2 < bestv) {
            best = i;
            bestv = e2;
        }
    }
    return (0x200 << keyShift(c)) * best + fx(o.frac, 0x200 << keyShift(oc));
}

} // namespace

int Anim_resolveId(const World& w, int animId) { return seqResolve(AD(w), animId); } // 0x4CC984

int Anim_selectLocomotion(World& w, int pid) { // 0x4CC028
    AnimCtl& ctl = C(w, pid);
    const PlayerState& M = w.players[static_cast<std::size_t>(pid)];
    const AnimData& a = AD(w);
    const u32 req = ctl.req;
    int type = ctl.type();
    if (type == 0xb) type = 0;
    std::int32_t gait = (M.speed + 0x8000) >> 16;
    if (gait == 0 && M.speed > 0x4ccc) gait = 1;
    int row = 0;
    if (req & 0x100f) row = (req & 4) ? 2 : (req & 1) ? 4 : (req & 2) ? 5 : (req & 0x1000) ? 3 : 1;
    const std::uint8_t* base;
    if (M.turnClass == 0) base = a.gaitType[type];
    else if (M.turnClass == 1) base = wrap200(M.facing - M.facingDesired) >= 0 ? a.gaitType[13] : a.gaitType[12];
    else base = a.gaitType[11]; // turn class 2 (0x59F8E2)
    int nt = base[gait];
    if (type == nt && ctl.stamp30 < static_cast<u32>(w.time.totalFrames) && M.turnClass == 0) nt = a.gaitType[0][gait];
    ctl.type() = static_cast<std::uint8_t>(nt);
    int id;
    const std::uint16_t* loco = nullptr;
    if (M.turnDelta < -0x1c) id = a.turnL[row][nt];
    else if (M.turnDelta > 0x1c) id = a.turnR[row][nt];
    else {
        loco = a.loco[row][nt];
        id = loco[0];
    }
    if ((req & 0x100) && id == 0x166) {
        ctl.rateReq = 0x10000;
        return 7;
    }
    Fixed rr = 0;
    if (loco) {
        std::int32_t d = M.speed - (static_cast<std::int32_t>(loco[1]) << 5);
        d = std::clamp(d, -0x13330, 0x13330);
        rr = (d >> 2) + 0x10000;
    }
    ctl.rateReq = rr;
    return id;
}

int Anim_play(World& w, int pid, int animId, std::int32_t ecx, std::uint32_t chooser, std::int32_t angleOfs) { // 0x4CC9BC
    AnimCtl& ctl = C(w, pid);
    PlayerState& M = w.players[static_cast<std::size_t>(pid)];
    const Vec3 m16 = mpos16(M);
    w.anim.motionPid = pid;
    const bool scripted = ctl.flags() & 0x40;
    const int oldSeq = ctl.seq;
    if (!scripted && oldSeq && seqId(ctl) == animId && ctl.inst && !(I(w, ctl.inst).flags & 0x10)) return animId;
    int id = Anim_resolveId(w, animId);
    u32 f = seqRec(w, id).flags;
    if (f & 1) {
        if (ctl.type() == 0xb) {
            id = Anim_selectLocomotion(w, pid);
            f = seqRec(w, id).flags;
        }
        ctl.timedCb = 0;
        ctl.timedCount = 0;
    }
    const AnimSeq& rec = seqRec(w, id);
    ctl.seq = id + 1;
    ctl.chooser = chooser;
    std::uint8_t fl = ctl.flags() & 0xbf;
    fl = (fl & 0xfe) | ((f & 2) ? 1 : 0);
    fl = (fl & 0xfb) | ((f & 0x10) ? 4 : 0);
    fl = (fl & 0xfd) | ((f & 1) ? 2 : 0);
    fl = (fl & 0xf7) | ((f & 4) ? 8 : 0);
    fl &= 0xef;
    ctl.flags() = fl;
    ctl.stamp30 = static_cast<std::uint16_t>(w.time.totalFrames + 0x1e);
    ctl.height = 0;
    ctl.stamp32 = static_cast<std::uint16_t>(w.time.totalFrames + 0xa);
    Desc d{rec.clip, 0, f >> 15, 0, id, pid, 0, 0, {}};
    const bool sync = (f & 1) && (f & 0x20000) && (f & 4);
    if (sync) {
        if (f & 0x40) {
            ctl.flags() &= 0x7f;
            d.cb = kCbSynced;
        } else {
            ctl.flags() |= 0x80;
            d.cb = 0;
        }
    } else {
        ctl.flags() &= 0x7f;
        if (ctl.inst) I(w, ctl.inst).flags &= 0xfff53fffu;
        d.cb = (f & 0x20000) ? kCbChained : kCbNormal;
    }
    std::int32_t blend, angle;
    if (ecx == 0) {
        d.pos = {m16.x, 0, m16.z}; // desc y is the stale global 0x6757F0, always 0 (only 0x4607A0 writes it)
        blend = rec.blendIn;
        angle = rec.angle + angleOfs;
    } else {
        blend = rec.blendOut;
        d.pos = ctl.inst ? rootXZ(w, I(w, ctl.inst)) : Vec3{};
        if (oldSeq && (seqRec(w, oldSeq - 1).flags & 8)) angle = rec.angle + instYaw(w, I(w, ctl.inst));
        else angle = atan2i(d.pos.x - m16.x, d.pos.z - m16.z) + rec.angle;
    }
    angle = wrap200(angle);
    if (scripted) blend = 0;
    d.angle = angle;
    ctl.setAngle(angle);
    d.start = sync ? syncStart(w, d, ctl.inst) : static_cast<std::int32_t>(static_cast<u32>(w.time.simAcc) >> 8);
    playBlend(w, ctl.inst, d, blend << 8);
    return id;
}

int Anim_start(World& w, int pid, int animId, std::int32_t ebx, std::int32_t ecx) { // 0x4CCCFC
    AnimCtl& ctl = C(w, pid);
    ctl.type() = 0xb;
    ctl.driftFrames = 0;
    ctl.flags() &= 0xdf;
    return Anim_play(w, pid, animId, 0, static_cast<u32>(ebx), ecx);
}

bool Anim_trySkill(World& w, int pid, int row, std::int32_t heading) { // 0x4CCD14
    AnimCtl& ctl = C(w, pid);
    int t = ctl.type();
    if (t == 0xb) t = 2;
    if (ctl.req == 1) return false;
    const AnimData& a = AD(w);
    const int id = (ctl.req & 0x800) ? a.skillB[row][t] : a.skillA[row][t];
    if (id == 0x1c4) return false;
    Anim_start(w, pid, id, 0, heading);
    return true;
}

void Anim_init(World& w, int pid, bool fresh) { // 0x4CC918
    AnimCtl& ctl = C(w, pid);
    if (fresh) {
        ctl.seq = 0;
        ctl.chooser = 0;
        ctl.inst = 0;
        ctl.pitchLimit = -14;
    }
    Anim_play(w, pid, 0x166, 0, 0, 0);
    ctl.f50 = 1;
    ctl.pose = 0x6757fc;
    ctl.type() = 0;
    ctl.headPitch = ctl.pitchLimit;
    ctl.headPitchTarget = 0;
    const std::int32_t y = instYaw(w, I(w, ctl.inst));
    ctl.headYaw = static_cast<std::int16_t>(y);
    ctl.headYawTarget = static_cast<std::int16_t>(y);
}

void Anim_moveStep(World& w, int pid) { // 0x4CCD80 (EAX = player+0x64, EDX = M)
    AnimCtl& ctl = C(w, pid);
    PlayerState& M = w.players[static_cast<std::size_t>(pid)];
    const int type = ctl.type();
    if ((ctl.req & 0x80) && (ctl.flags() & 8)) ctl.flags() |= 0x20;
    if ((ctl.flags() & 0x80) && (ctl.stamp32 < static_cast<u32>(w.time.totalFrames) || (ctl.flags() & 0x20))) {
        if (I(w, ctl.inst).flags & 0x80) {
            ctl.flags() |= 0x20;
        } else {
            const int id = Anim_selectLocomotion(w, pid);
            if (id != seqId(ctl)) {
                Anim_play(w, pid, id, 0, 0, ctl.type() == 0 ? M.facing : M.facingDesired);
            } else {
                if (ctl.rateReq) {
                    setRate(w, ctl.inst, ctl.rateReq);
                    ctl.rateReq = 0;
                }
                ctl.stamp32 = static_cast<std::uint16_t>(w.time.totalFrames + 0xa);
            }
            ctl.flags() &= 0xdf;
        }
    } else if (ctl.driftFrames && type == 0xb) {
        std::int32_t left = ctl.driftFrames, n = w.time.simFrames;
        if (n > left) n = left;
        AnimInstance& a = I(w, ctl.inst);
        Vec3 r = rootXZ(w, a);
        r.x += ctl.driftX() * n;
        left -= n;
        r.z += n * ctl.driftZ();
        setRootXZ(w, a, r);
        ctl.driftFrames = left;
    }
    w.anim.motionPid = pid;
    advanceList(w, &ctl.inst);
    if (type == 0xb && ctl.inst) {
        AnimInstance& a = I(w, ctl.inst);
        const Vec3 r = rootXZ(w, a);
        const std::int32_t dx = r.x - M.pos.x, dz = r.z - M.pos.z;
        const std::int32_t y = wrap200(instYaw(w, a));
        const Angle mv = static_cast<u32>(octLength(dx, dz)) > 0x18000u ? atan2i(dx, dz) : y;
        M.pos = {r.x, static_cast<std::int32_t>(ctl.height), r.z};
        M.animFacing = y;
        M.animFacingDesired = mv;
        ctl.headYaw = static_cast<std::int16_t>(y);
    }
    // ctl+0x0C timed callback: no writer in the exe (always 0).
}

void Anim_updateScripted(World& w, int pid) { // 0x4CCFC0
    AnimCtl& ctl = C(w, pid);
    if (!(ctl.flags() & 0x40)) return;
    w.anim.motionPid = pid;
    if (ctl.turnStep) {
        ctl.setAngle(ctl.angle() + ctl.turnStep);
        setAngle(w, ctl.inst, ctl.angle());
    }
    if (ctl.rateReq) {
        setRate(w, ctl.inst, ctl.rateReq);
        ctl.rateReq = 0;
    }
    advanceList(w, &ctl.inst);
}

bool Anim_setDrift(World& w, int pid, std::int32_t dx, std::int32_t dz, std::int32_t frames) { // 0x4CD2F0
    AnimCtl& ctl = C(w, pid);
    if ((ctl.flags() & 2) || frames == 0 || (dx == 0 && dz == 0)) return false;
    auto div2 = [](std::int32_t a, std::int32_t b) { // cdq; idiv; shrd; idiv (16.16 quotient)
        const std::int32_t q1 = a / b, r = a % b;
        return (q1 << 16) + static_cast<std::int32_t>((static_cast<std::int64_t>(r) << 16) / b);
    };
    if (frames) {
        const std::int32_t f16 = frames << 16;
        ctl.set32(0x39, div2(dx << 16, f16));
        ctl.set32(0x41, div2(dz << 16, f16));
        ctl.driftFrames = frames;
    }
    return true;
}

bool Anim_clipUnavailable(const World& w, int animId) { // 0x4CD384: true when usable (name kept)
    const AnimSeq& s = seqRec(w, animId);
    return (s.flags & 0x80) || (s.clip >= 0 && AD(w).clips[static_cast<std::size_t>(s.clip)].loaded);
}

namespace {

std::int32_t contactInfoData(const AnimData& a, int anim, int edx, int ecx, int ebx, int s1, std::int32_t* out44,
                             std::int32_t* out48) { // 0x4CD3B0
    const int id = seqResolve(a, anim);
    const AnimSeq& rec = a.seqs[static_cast<std::size_t>(id)];
    *out44 = 0;
    *out48 = 0;
    if (rec.clip < 0) return -1;
    const AnimClip& c = a.clips[static_cast<std::size_t>(rec.clip)];
    const bool mirror = rec.flags & 0x8000;
    const std::int32_t from = selTime(c, edx);
    if (from < 0) return -1;
    const std::int32_t to = selTime(c, ebx);
    if (to < 0) return -1;
    const std::int32_t frames = to - from;
    if (frames < 0) return -1;
    const Vec3 p0 = clipPoint(c, from, ecx == 1, mirror); // (selectors > 1 leave stack garbage: unused)
    const Vec3 p1 = clipPoint(c, to, s1 == 1, mirror);
    const std::int32_t dx = p1.x - p0.x, dz = p1.z - p0.z;
    const std::int32_t dist = Math_length(iabs(dx) >> 16, iabs(dz) >> 16);
    *out44 = dist;
    *out48 = dist ? wrap200(atan2i(dx, dz) + rec.angle) : 0;
    return frames >> 8;
}

} // namespace

int Anim_contactInfo(const World& w, int anim, int edx, int ecx, int ebx, int s1, std::int32_t* out44, std::int32_t* out48) {
    return contactInfoData(AD(w), anim, edx, ecx, ebx, s1, out44, out48);
}

Vec3 Anim_predictEndPos(World& w, int pid) { // 0x4CD60C
    AnimCtl& ctl = C(w, pid);
    const PlayerState& M = w.players[static_cast<std::size_t>(pid)];
    std::int32_t dist = 0, ang = 0;
    Anim_contactInfo(w, seqId(ctl), I(w, ctl.inst).time >> 8, 0, -97, 0, &dist, &ang);
    ang = wrap200(ang + ctl.angle());
    const SinCos sc = sinCos(ang);
    Vec3 out = M.posI;
    out.x += (fx(dist << 16, sc.s) + fx(ctl.driftX(), ctl.driftFrames)) >> 16;
    out.y = 0;
    out.z += (fx(dist << 16, sc.c) + fx(ctl.driftZ(), ctl.driftFrames)) >> 16;
    return out;
}

int Anim_boneHeightAt(const World& w, int animId, int sel) { // 0x4CD70C
    const int id = Anim_resolveId(w, animId);
    const AnimSeq& rec = seqRec(w, id);
    if (rec.clip < 0) return -1;
    const AnimClip& c = AD(w).clips[static_cast<std::size_t>(rec.clip)];
    const std::int32_t t = selTime(c, sel);
    if (t < 0) return -1;
    const std::int32_t y = (clipPoint(c, t, true, rec.flags & 0x8000).y >> 16) - 8;
    return y < 0 ? 0 : y;
}

int Anim_clipFrames(const World& w, int animId) { // 0x4CD7D8
    const AnimSeq& rec = seqRec(w, Anim_resolveId(w, animId));
    return rec.clip < 0 ? 0 : clipDuration(AD(w).clips[static_cast<std::size_t>(rec.clip)]) >> 8;
}

int Anim_markFrame0(const World& w, int animId) { // 0x4CD810 (no resolve)
    const AnimSeq& rec = seqRec(w, animId);
    if (rec.clip < 0) return -1;
    const std::int32_t m = mark0(AD(w).clips[static_cast<std::size_t>(rec.clip)]);
    return m < 0 ? -1 : m >> 8;
}
int Anim_markFrame1(const World& w, int animId) { // 0x4CD858
    const AnimSeq& rec = seqRec(w, animId);
    if (rec.clip < 0) return -1;
    const std::int32_t m = mark1(AD(w).clips[static_cast<std::size_t>(rec.clip)]);
    return m < 0 ? -1 : m >> 8;
}
int Anim_markFrame2(const World& w, int animId) { // 0x4CD8A4
    const AnimSeq& rec = seqRec(w, animId);
    if (rec.clip < 0) return -1;
    const AnimClip& c = AD(w).clips[static_cast<std::size_t>(rec.clip)];
    std::int32_t m = c.marks[2] * 0x100;
    if (m < 0) m = mark0(c);
    return m < 0 ? -1 : m >> 8;
}

Vec3 Anim_contactPos(World& w, int pid) { // 0x4CD8F8
    const Vec3 v = cachedBone(w, I(w, C(w, pid).inst), 0x10000);
    const std::int32_t y = (v.y >> 16) - 8;
    return {v.x >> 16, y < 0 ? 0 : y, v.z >> 16};
}
int Anim_frame(const World& w, int pid) { return I(w, w.players[static_cast<std::size_t>(pid)].anim.inst).time >> 8; }
int Anim_lengthFrames(const World& w, int pid) { return I(w, w.players[static_cast<std::size_t>(pid)].anim.inst).duration >> 8; }
int Anim_currentMark2(const World& w, int pid) { // 0x4CD95C
    const AnimClip& c = *clipOf(w, I(w, w.players[static_cast<std::size_t>(pid)].anim.inst).clip);
    std::int32_t m = c.marks[2] * 0x100;
    if (m < 0) m = mark0(c);
    return m < 0 ? -1 : m >> 8;
}
int Anim_yawAt(const World& w, int animId, int sel) { // 0x4CD998
    const int id = Anim_resolveId(w, animId);
    const AnimSeq& rec = seqRec(w, id);
    if (rec.clip < 0) return 0x400;
    const AnimClip& c = AD(w).clips[static_cast<std::size_t>(rec.clip)];
    const std::int32_t t = selTime(c, sel);
    if (t < 0) return 0x400;
    // 0x50081C: ch1 at the key below t, negated when mirrored.
    std::int32_t y = keyCh(c, t >> (9 + keyShift(c)), 1);
    if (rec.flags & 0x8000) y = -y;
    return wrap200(y + rec.angle);
}

Fixed Anim_rootHeight(World& w, int pid) { return rootY(w, I(w, C(w, pid).inst)); }
Vec3 Anim_ballBonePos(World& w, int pid) { return cachedBone(w, I(w, C(w, pid).inst), 0x10000); }

// 0x4CD00C (EAX = ctl player+0x64, EDX = M, EBX = position warped this frame, ECX = force placement):
// re-place the anim instance on the movement object (angle, root) and step the head yaw / pitch
// toward their targets by at most DAT_006757C8 per frame.
void Motion_updateHeadAim(World& w, int pid, std::int32_t ebx, std::int32_t ecx) {
    AnimCtl& ctl = C(w, pid);
    const PlayerState& M = w.players[static_cast<std::size_t>(pid)];
    if (!ctl.inst) return;
    AnimInstance& a = I(w, ctl.inst);
    auto s16 = [](std::int32_t v) { return static_cast<std::int32_t>(static_cast<std::int16_t>(v)); };
    std::int32_t yawOut = 0, pitchOut = 0;
    const std::uint8_t fl = ctl.flags();
    if (((fl & 1) && !(fl & 0x20)) || ecx != 0) { // 0x4CD042
        const std::int32_t base = ctl.type() != 0 ? M.facingDesired : M.facing;
        setAngle(w, ctl.inst, base + seqRec(w, ctl.seq - 1).angle); // dword rec+4 >> 16
    }
    if ((ctl.flags() & 8) || ebx != 0) { // 0x4CD068
        const u32 req = ctl.req;
        setRootXZ(w, a, mpos16(M));
        if (ctl.flags() & 8) {
            std::int32_t pitch = s16(ctl.headPitch);
            const std::int32_t rate = w.anim.aimRate;
            const std::int32_t y = instYaw(w, a);
            std::int32_t yawT = s16(ctl.headYaw);
            std::int32_t pt = (ctl.type() >= 2 && ctl.type() <= 4) ? s16(ctl.pitchLimit) : -14;
            if (req & 0x20) pt += 0x1c;
            else if (req & 0x40) pt -= 0xe;
            std::int32_t d;
            if (req & 0x400) {
                d = wrap200(y - yawT);
            } else {
                d = std::clamp(wrap200(s16(ctl.headYawTarget) - y), -0x8e, 0x8e);
                d = wrap200(y - yawT + d);
                if (req & 0x200) pt = wrap200(s16(ctl.headPitchTarget));
            }
            if (d != 0) yawT = wrap200(yawT + std::clamp(d, -rate, rate));
            yawOut = std::clamp(wrap200(yawT - y), -0x8e, 0x8e);
            ctl.headYaw = static_cast<std::int16_t>(y + yawOut);
            pt = std::clamp(pt, -0xc7, 0x8e);
            const std::int32_t dp = pt - pitch;
            if (dp != 0) {
                pitch += std::clamp(dp, -rate, rate);
                ctl.headPitch = static_cast<std::int16_t>(pitch);
            }
            pitchOut = pitch;
        }
    }
    a.headYaw = yawOut;     // 0x45FB3C
    a.headPitch = pitchOut; // 0x45FB40
}

int Anim_playScripted(World& w, int pid, int animId, std::int32_t startFrame, std::int32_t angle, std::int32_t blend,
                      bool mirror) { // 0x4CDB70 (registered script anims id >= 0x1C4 are cut-scene only)
    AnimCtl& ctl = C(w, pid);
    const PlayerState& M = w.players[static_cast<std::size_t>(pid)];
    const Vec3 m16 = mpos16(M);
    w.anim.motionPid = pid;
    const int id = Anim_resolveId(w, animId);
    const AnimSeq& rec = seqRec(w, id);
    if (blend == 0) blend = 2;
    else if (blend < 0) blend = rec.blendIn;
    u32 f = rec.flags & 0x8000;
    std::int32_t a;
    if (mirror) {
        f ^= 0x8000;
        a = wrap200(angle - rec.angle);
    } else {
        a = wrap200(rec.angle + angle);
    }
    ctl.chooser = 0;
    ctl.type() = 0xb;
    ctl.stamp30 = 0;
    ctl.stamp32 = 0;
    ctl.rateReq = 0;
    ctl.seq = id + 1;
    ctl.height = 0;
    ctl.flags() = static_cast<std::uint8_t>((ctl.flags() | 0x44) & 0x64);
    ctl.setAngle(a);
    Desc d{rec.clip, 0, (f >> 15) | 0x2004, static_cast<std::int32_t>((startFrame << 8) + (static_cast<u32>(w.time.simAcc) >> 8)),
           id, pid, kCbScripted, a, {m16.x, 0, m16.z}};
    playBlend(w, ctl.inst, d, blend << 8);
    return id;
}

void untraced_Anim_poseExtent(const World&, int, int, std::int8_t* outX, std::int8_t* outY, std::int8_t* outZ) {
    *outX = *outY = *outZ = 0;
}

// ---------------------------------------------------------------------------------------------
// Instance callbacks (inst+0xA0)

namespace {

void callback(World& w, AnimInstance& a) {
    const int pid = a.pid;
    AnimCtl& ctl = C(w, pid);
    switch (a.callback) {
    case kCbSynced: { // 0x4CC2A4
        const std::int32_t ft = footFlags(w, a);
        const u32 f = a.flags;
        a.flags &= 0xfff53fffu;
        const int mp = w.anim.motionPid;
        if (f & 0x40) return;
        if (f & 0x8000) {
            dribbleBone(w, a, ctl);
            Player_onAnimContact(w);
        } else if (f & 0x10000) {
            ctl.flags() |= 0x10;
        }
        if (!((ft & 6) || ctl.stamp32 <= static_cast<u32>(w.time.totalFrames) || (ctl.flags() & 0x20))) return;
        if (f & 0x80) return;
        ctl.flags() &= 0xdf;
        const int id = Anim_selectLocomotion(w, pid);
        if (id != seqId(ctl)) {
            const PlayerState& M = w.players[static_cast<std::size_t>(mp >= 0 ? mp : pid)];
            Anim_play(w, pid, id, 0, 0, ctl.type() == 0 ? M.facing : M.facingDesired);
        } else {
            if (ctl.rateReq) {
                setRate(w, ctl.inst, ctl.rateReq);
                ctl.rateReq = 0;
            }
            ctl.stamp32 = static_cast<std::uint16_t>(w.time.totalFrames + 0xa);
        }
        return;
    }
    case kCbNormal: { // 0x4CC408
        const u32 f = a.flags;
        if (f & 0x40) return;
        if (f & 0x8000) {
            dribbleBone(w, a, ctl);
            Player_onAnimContact(w);
        } else if (f & 0x10000) {
            ctl.flags() |= 0x10;
        }
        a.flags &= 0xfff73fffu;
        if (!(f & 0x10) && ((a.duration - a.time) >> 8) < seqRec(w, seqId(ctl)).blendOut) {
            // (falls through to the chain below)
        } else if (!(f & 0x10)) {
            return;
        }
        int next = seqRec(w, seqId(ctl)).next;
        std::uint32_t out = 0;
        if (ctl.chooser) next = Player_animChooser(w, pid, ctl.chooser, seqId(ctl), next, &out);
        Anim_play(w, pid, next, 1, out, 0);
        return;
    }
    case kCbChained: { // 0x4CC520
        const u32 f = a.flags;
        a.flags &= 0xfff53fffu;
        if (f & 0x40) return;
        if (f & 0x8000) {
            dribbleBone(w, a, ctl);
            Player_onAnimContact(w);
        } else if (f & 0x10000) {
            ctl.flags() |= 0x10;
        }
        if (!(f & 0x20000) || !ctl.chooser) return;
        std::uint32_t out = 0;
        const int cur = seqId(ctl);
        const int r = Player_animChooser(w, pid, ctl.chooser, cur, cur, &out);
        if (r == cur) ctl.chooser = out;
        else Anim_play(w, pid, r, 1, out, 0);
        return;
    }
    case kCbScripted: { // 0x4CC608
        const u32 f = a.flags;
        if (f & 0x40) {
            a.flags &= 0xfff73fffu;
            return;
        }
        if (f & 0x8000) {
            dribbleBone(w, a, ctl);
            Player_onAnimContact(w);
        } else if (f & 0x10000) {
            ctl.flags() |= 0x10;
        }
        a.flags &= 0xfff73fffu;
        return;
    }
    default: return;
    }
}

} // namespace
// WC98 reconstruction of Anim_evalSkeleton (FIFA 99 exe 0x460ACC, left unported upstream;
// SIM_PORT_NOTES.md section 5). World Cup 98 fills the keeper body-capsule skeleton by FK-evaluating
// the current pose of the player's animation instance through wc98::formats (PlayerRig + the
// full-channel clip decode kept in AnimClip::fmt). bones[16] slots follow the capsule topology
// Keeper_skeletonBlockNormal tests: 0..3 pelvis / spine / chest / head, 4..6 arm A shoulder/elbow/
// hand tip, 7..9 arm B, 10..12 leg A hip/knee/toe tip, 13..15 leg B. Rig joints used (kNames):
// TorsoChn, LowerTorso, UpperTorso, Head, LUpperArm, LLowArm, RUpperArm, RLowArm, RThigh, RLowLeg,
// LThigh, LLowLeg; the four distal tips are extrapolated (the exe's 16th-bone placement is untraced).
namespace {

using FmtPose = std::array<formats::JointPose, formats::PlayerRig::kJoints>;
using Float3 = std::array<float, 3>;

// The engine interpolates keyA/keyB by frac 0..0xFFFF (root(): lo + fx(frac, hi - lo)); triplets
// are stored x4 and root channels enter as key << 12, so one model unit = 4096 fixed.
constexpr float kFixedPerUnit = 4096.0f;

FmtPose lerpRigPose(const FmtPose& a, const FmtPose& b, float t) {
    // Translations lerp, rotations nlerp (shortest arc + renormalise: the exe blend 0x4608F0 /
    // 0x502338 with normalise 0x5019F4 reduced to the two-key engine case).
    FmtPose r;
    for (std::size_t j = 0; j < r.size(); ++j) {
        for (int c = 0; c < 3; ++c) r[j].translation[c] = a[j].translation[c] + t * (b[j].translation[c] - a[j].translation[c]);
        formats::Quat qa = a[j].rotation, qb = b[j].rotation;
        if (qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3] < 0)
            for (auto& x : qb) x = -x;
        for (int c = 0; c < 4; ++c) r[j].rotation[c] = qa[c] + t * (qb[c] - qa[c]);
        const float n = std::sqrt(r[j].rotation[0] * r[j].rotation[0] + r[j].rotation[1] * r[j].rotation[1] +
                                  r[j].rotation[2] * r[j].rotation[2] + r[j].rotation[3] * r[j].rotation[3]);
        if (n > 0) for (auto& x : r[j].rotation) x /= n;
        r[j].scale = a[j].scale + t * (b[j].scale - a[j].scale);
    }
    return r;
}

// Instance flag bit 1: mirrored clip. Swap the L/R limb joints and negate x offsets / quat x,z
// (documented WC98 adaptation of the exe's mirror, cf. root()/bone() negating v.x).
void mirrorRigPose(FmtPose& p) {
    static constexpr int kSwap[5][2] = {{5, 7}, {6, 8}, {9, 12}, {10, 13}, {11, 14}};
    for (const auto& s : kSwap) std::swap(p[static_cast<std::size_t>(s[0])], p[static_cast<std::size_t>(s[1])]);
    for (auto& j : p) {
        j.translation[0] = -j.translation[0];
        j.rotation[0] = -j.rotation[0];
        j.rotation[2] = -j.rotation[2];
    }
}

bool rigWorldPose(const World& w, const AnimInstance& a, FmtPose& out) {
    const AnimClip* c = clipOf(w, a.clip);
    if (!c->fmt || !w.data || !w.data->anim.rigLoaded) return false;
    const formats::AnimClip& f = *c->fmt;
    if (f.tripletCount != 5 || f.quatCount != formats::PlayerRig::kJoints - 1 || f.keyCount == 0) return false;
    const auto& rig = w.data->anim.rig;
    const std::int32_t last = static_cast<std::int32_t>(f.keyCount) - 1;
    const std::size_t ka = static_cast<std::size_t>(std::clamp(a.keyA, 0, last));
    const std::size_t kb = static_cast<std::size_t>(std::clamp(a.keyB, 0, last));
    FmtPose pa, pb;
    try {
        pa = rig.localPose(f, ka);
        pb = ka == kb ? pa : rig.localPose(f, kb);
    } catch (const FormatError&) { return false; } // zero quaternion / short key list
    const float t = static_cast<float>(static_cast<std::uint32_t>(a.frac) & 0xffffu) / 65536.0f;
    FmtPose p = lerpRigPose(pa, pb, t);
    if (a.flags & 1) mirrorRigPose(p);
    for (std::size_t j = 0; j < p.size(); ++j) {
        const int par = formats::PlayerRig::kParent[j];
        out[j] = par < 0 ? p[static_cast<std::size_t>(par + 1)] : formats::compose(out[static_cast<std::size_t>(par)], p[j]);
    }
    return true;
}

Float3 tipOf(const Float3& end, const Float3& from) {
    // ponytail: hand/toe tips are not rig joints; extrapolate 0.55 segment lengths past the distal
    // joint (the exe's exact distal placement is untraced). Capsule radii 3..6 cover the tip ball.
    const float dx = end[0] - from[0], dy = end[1] - from[1], dz = end[2] - from[2];
    const float len2 = dx * dx + dy * dy + dz * dz;
    if (len2 <= 0) return {end[0], end[1] - 4.0f, end[2]}; // hanging limb: a hand's size below
    const float s = 0.55f / std::sqrt(len2);
    return {end[0] + dx * s, end[1] + dy * s, end[2] + dz * s};
}

bool posedBones(const World& w, const AnimInstance& a, int pid, Vec3 bones[16]) {
    FmtPose pose;
    if (!rigWorldPose(w, a, pose)) return false;
    // Slot -> rig joint (tip slots filled below). Head uses the Head joint centre (capsule r 6).
    static constexpr int kBoneRig[12] = {0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 12, 13};
    static constexpr int kBoneSlot[12] = {0, 1, 2, 3, 4, 5, 7, 8, 10, 11, 13, 14};
    std::array<Float3, 16> bp{};
    for (int i = 0; i < 12; ++i) bp[kBoneSlot[i]] = pose[kBoneRig[i]].translation;
    static constexpr int kTip[4][3] = {{6, 5, 4}, {9, 8, 7}, {12, 11, 10}, {15, 14, 13}};
    for (const auto& t : kTip) bp[t[0]] = tipOf(bp[t[1]], bp[t[2]]);
    // Placement: anchor the rig root at the player position and rotate clip x/z by the instance
    // yaw like AnimInstance::root() (cosA/sinA), keeping pose y as height above the ground.
    const Vec3 base = mpos16(w.players[static_cast<std::size_t>(pid)]);
    const Float3& pel = bp[0];
    for (int s = 0; s < 16; ++s) {
        const std::int32_t dx = static_cast<std::int32_t>((bp[s][0] - pel[0]) * kFixedPerUnit);
        const std::int32_t dy = static_cast<std::int32_t>(bp[s][1] * kFixedPerUnit);
        const std::int32_t dz = static_cast<std::int32_t>((bp[s][2] - pel[2]) * kFixedPerUnit);
        bones[s].x = base.x + fx(dx, a.cosA) + fx(dz, -a.sinA);
        bones[s].y = base.y + dy;
        bones[s].z = base.z + fx(dx, a.sinA) + fx(dz, a.cosA);
    }
    return true;
}

// Fallback skeleton: upright standing pose (model units ~ 2 cm: head at 84 ~ 1.7 m tall), used when
// no posed clip is available (rig file absent, unposed variant clip). The capsules then describe a
// generic body around the player - never degenerate zero-length bones for Keeper_hitBone.
void standingBones(const Vec3& base, Vec3 bones[16]) {
    struct B { int x, y, z; }; // (lateral, up, forward) model units
    static constexpr B kBones[16] = {
        {0, 45, 0},   {0, 57, 0},   {0, 68, 0},   {0, 84, 0},
        {-16, 68, 0}, {-22, 50, 0}, {-27, 34, 0}, {16, 68, 0},
        {22, 50, 0},  {27, 34, 0},  {-9, 45, 0},  {-10, 22, 0},
        {-11, 3, 11}, {9, 45, 0},   {10, 22, 0},  {11, 3, 11}};
    for (int s = 0; s < 16; ++s)
        bones[s] = {base.x + (kBones[s].x << 16), base.y + (kBones[s].y << 16), base.z + (kBones[s].z << 16)};
}

} // namespace

void Anim_evalSkeleton(const World& w, int pid, Vec3 bones[16]) { // declared in player_states.hpp
    const PlayerState& p = w.players[static_cast<std::size_t>(pid)];
    if (const int ref = p.anim.inst) {
        const AnimInstance& a = I(w, ref);
        if (posedBones(w, a, pid, bones)) {
            // Crossfade with the fading-out partner instance (flag 0x40), weight +0x44.
            if (a.prev && a.weight > 0 && a.weight < 0x10000 && (I(w, a.prev).flags & 0x40)) {
                Vec3 pb[16];
                if (posedBones(w, I(w, a.prev), pid, pb)) {
                    for (int s = 0; s < 16; ++s) {
                        bones[s].x = fx(a.weight, bones[s].x) + fx(0x10000 - a.weight, pb[s].x);
                        bones[s].y = fx(a.weight, bones[s].y) + fx(0x10000 - a.weight, pb[s].y);
                        bones[s].z = fx(a.weight, bones[s].z) + fx(0x10000 - a.weight, pb[s].z);
                    }
                }
            }
            return;
        }
    }
    standingBones(mpos16(p), bones);
}

} // namespace wc98::sim
