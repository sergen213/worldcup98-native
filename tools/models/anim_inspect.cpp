// Derived from fifa99-native (same author), ported for World Cup 98
// wc98-anim-inspect: list / describe / dump FIFA 99 player animation clips (MOAI.VIV, ANIM.BIG) and print
// the posed 15-joint skeleton. glTF export of clips is done by wc98-model-export --anim.
#include "model_common.hpp"

#include <cstdio>

using namespace wc98::tools;

namespace {

int usage() {
    std::fputs("usage: wc98-anim-inspect list ARCHIVE\n"
               "       wc98-anim-inspect describe ARCHIVE:CLIP\n"
               "       wc98-anim-inspect json ARCHIVE:CLIP\n"
               "       wc98-anim-inspect skeleton MODEL ARCHIVE:CLIP [KEY] [--json]\n",
               stderr);
    return 2;
}

std::string channelRole(const AnimClip& c, std::size_t i) {
    static const char* scalar[9] = {"flags (bit1 R foot down, bit2 L foot down)", "facing yaw (1024 = 360 deg)", "unknown", "root x (x4)",
                                    "pelvis height (x4, = triplet0.y)", "root z (x4)", "unknown (constant per clip)", "unknown (constant per clip)",
                                    "unknown (constant per clip)"};
    static const char* trip5[5] = {"pelvis", "L shoulder", "R shoulder", "L hip", "R hip"};
    static const char* axis = "xyzw";
    if (i < AnimClip::kFirstTriplet) return scalar[i];
    std::size_t t = (i - AnimClip::kFirstTriplet) / 3;
    if (t < c.tripletCount) {
        std::string who = c.tripletCount == 5 ? trip5[t] : std::format("triplet {}", t);
        return std::format("{} pos.{} (x4)", who, axis[(i - AnimClip::kFirstTriplet) % 3]);
    }
    std::size_t q = (i - c.quatChannel(0)) / 4;
    std::string who = c.quatCount == 14 ? std::string(PlayerRig::kNames[q + 1]) : std::format("quat {}", q);
    return std::format("{} rot.{} (x256)", who, axis[(i - c.quatChannel(0)) % 4]);
}

void header(const AnimClip& c) {
    std::printf("header %u  ticks %u  keys %u  marks %d %d %d  flags 0x%04x (%s)  channels %u = 9 + 3*%u + 4*%u  field18 %u\n", c.headerSize,
                c.ticks, c.keyCount, c.marks[0], c.marks[1], c.marks[2], c.flags, c.compressed() ? "delta-compressed" : "raw keys",
                c.channelCount, c.tripletCount, c.quatCount, c.field18);
}

} // namespace

int main(int argc, char** argv) try {
    if (argc < 3) return usage();
    std::string cmd = argv[1];
    if (cmd == "list") {
        Archive a(argv[2]);
        std::printf("%-10s %6s %5s %5s %6s %4s %s\n", "name", "bytes", "keys", "ticks", "flags", "chan", "foot flags per key (R/L down)");
        for (const auto& e : a.dir.entries) {
            auto d = a.member(e);
            if (!AnimClip::isAnim(d)) continue;
            auto c = AnimClip::parse(d);
            std::string feet;
            for (const auto& k : c.keys) feet += "_RLB"[(k[0] >> 1) & 3];
            if (feet.size() > 40) feet = feet.substr(0, 40) + "...";
            std::printf("%-10s %6u %5u %5u 0x%04x %4u %s\n", e.name.c_str(), e.size, c.keyCount, c.ticks, c.flags, c.channelCount, feet.c_str());
        }
        return 0;
    }
    if (cmd == "describe" || cmd == "json") {
        auto c = AnimClip::parse(loadInput(argv[2]));
        if (cmd == "describe") {
            header(c);
            std::printf("%4s %5s %7s %7s  %s\n", "ch", "bits", "min", "max", "role");
            for (std::size_t i = 0; i < c.channelCount; ++i) {
                int lo = 1 << 30, hi = -(1 << 30);
                for (const auto& k : c.keys) lo = std::min(lo, k[i]), hi = std::max(hi, k[i]);
                std::printf("%4zu %5s %7d %7d  %s\n", i, c.compressed() ? std::to_string(c.bitWidths[i]).c_str() : "-", lo, hi, channelRole(c, i).c_str());
            }
        } else {
            std::string o = std::format(R"({{"ticks":{},"keyCount":{},"flags":{},"marks":[{},{},{}],"triplets":{},"quaternions":{},"bitWidths":[)", c.ticks,
                                        c.keyCount, c.flags, c.marks[0], c.marks[1], c.marks[2], c.tripletCount, c.quatCount);
            for (std::size_t i = 0; i < c.bitWidths.size(); ++i) o += std::format("{}{}", i ? "," : "", c.bitWidths[i]);
            o += R"(],"keys":[)";
            for (std::size_t k = 0; k < c.keys.size(); ++k) {
                o += k ? ",[" : "[";
                for (std::size_t i = 0; i < c.keys[k].size(); ++i) o += std::format("{}{}", i ? "," : "", c.keys[k][i]);
                o += "]";
            }
            std::puts((o + "]}").c_str());
        }
        return 0;
    }
    if (cmd == "skeleton" && argc >= 4) {
        auto m = PcmdModel::parse(loadInput(argv[2]));
        auto c = AnimClip::parse(loadInput(argv[3]));
        std::size_t key = 0;
        bool json = false;
        for (int i = 4; i < argc; ++i) {
            if (std::string(argv[i]) == "--json") json = true;
            else key = std::stoul(argv[i]);
        }
        auto rig = PlayerRig::fromModel(m);
        auto local = rig.localPose(c, key);
        std::array<JointPose, PlayerRig::kJoints> world;
        for (std::size_t j = 0; j < PlayerRig::kJoints; ++j) {
            int p = PlayerRig::kParent[j];
            world[j] = p < 0 ? local[j] : compose(world[std::size_t(p)], local[j]);
        }
        if (json) {
            std::string o = "[";
            for (std::size_t j = 0; j < PlayerRig::kJoints; ++j) {
                const auto& w = world[j];
                o += std::format(R"({}{{"joint":{},"parent":{},"position":[{},{},{}],"rotation":[{},{},{},{}]}})", j ? "," : "", jstr(PlayerRig::kNames[j]),
                                 PlayerRig::kParent[j], w.translation[0], w.translation[1], w.translation[2], w.rotation[0], w.rotation[1],
                                 w.rotation[2], w.rotation[3]);
            }
            std::puts((o + "]").c_str());
            return 0;
        }
        std::printf("key %zu/%zu  flags 0x%x  (positions in model units, Y up)\n", key, c.keys.size(), c.keys[key][0]);
        auto print = [&](auto&& self, int j, int depth) -> void {
            const auto& t = world[std::size_t(j)].translation;
            std::printf("%*s%-12s (%8.2f %8.2f %8.2f)\n", depth * 2, "", std::string(PlayerRig::kNames[std::size_t(j)]).c_str(), t[0], t[1], t[2]);
            for (std::size_t c2 = 0; c2 < PlayerRig::kJoints; ++c2)
                if (PlayerRig::kParent[c2] == j) self(self, int(c2), depth + 1);
        };
        print(print, 0, 0);
        return 0;
    }
    return usage();
} catch (const std::exception& e) {
    std::fprintf(stderr, "wc98-anim-inspect: %s\n", e.what());
    return 1;
}
