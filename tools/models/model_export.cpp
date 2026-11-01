// Derived from fifa99-native (same author), ported for World Cup 98
// wc98-model-export: World Cup 98 PCMD model (.PC/.PC2/.XXX) -> glTF 2.0 (+ PNG textures, preview PNG).
#include "model_common.hpp"

#include <cstdio>

using namespace wc98::tools;

namespace {

int usage(int code = 2) {
    std::fputs("usage: wc98-model-export MODEL --out DIR [options]\n"
               "  MODEL / SPEC          file or ARCHIVE:MEMBER[:MEMBER...] (BIG/VIV/PAK, RefPack handled)\n"
               "  --fsh SPEC            texture source: FSH file, RefPack .PAK, archive (harvested\n"
               "                        recursively; WC98 nests FSH inside BIG-in-BIG, e.g. BEART.BIG)\n"
               "                        or a directory of loose FSH files (repeatable)\n"
               "  --pal SPEC            WC98 shared FE palette archive (FE/PACK/GLOBAL/PAK10042.BIG, PAL*\n"
               "                        members); enables the global-palette fallback (matrix section 4)\n"
               "  --pose SPEC [--key N] animation clip whose key N (default 0) poses a player model\n"
               "  --anim SPEC           add a glTF animation (repeatable); ARCHIVE:* adds every player clip\n"
               "  --all-parts           export alternative player parts (hair styles, hand shapes) too\n"
               "  --preview             also write <name>_preview.png (front / side / top orthographic)\n"
               "  --name NAME           output base name (default: model stem)\n",
               stderr);
    return code;
}

} // namespace

int main(int argc, char** argv) try {
    std::string model, poseSpec, name, palSpec;
    fs::path out;
    std::vector<std::string> fsh, anims;
    std::size_t poseKey = 0;
    bool allParts = false, preview = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&] { if (i + 1 >= argc) throw std::runtime_error("missing value for " + a); return std::string(argv[++i]); };
        if (a == "--help" || a == "-h") return usage(0);
        else if (a == "--out") out = next();
        else if (a == "--fsh") fsh.push_back(next());
        else if (a == "--pal") palSpec = next();
        else if (a == "--pose") poseSpec = next();
        else if (a == "--key") poseKey = std::stoul(next());
        else if (a == "--anim") anims.push_back(next());
        else if (a == "--all-parts") allParts = true;
        else if (a == "--preview") preview = true;
        else if (a == "--name") name = next();
        else if (a.starts_with("--")) return usage();
        else model = a;
    }
    if (model.empty() || out.empty()) return usage();
    if (name.empty()) name = specStem(model);

    // WC98 delta (matrix section 4): WC98 bitmaps index past their attached palette and
    // rely on the shared FE palette archive; load it before any FSH decode.
    if (!palSpec.empty())
        wc98::formats::setGlobalPalette(wc98::formats::wc98GlobalPaletteFromPalArchive(loadInput(palSpec)));

    auto bytes = loadInput(model);
    auto m = PcmdModel::parse(bytes);
    std::optional<std::array<JointPose, PlayerRig::kJoints>> bind;
    if (!poseSpec.empty() && PlayerRig::isPlayerModel(m))
        bind = PlayerRig::fromModel(m).localPose(AnimClip::parse(loadInput(poseSpec)), poseKey);
    auto tex = loadTextures(fsh);
    auto scene = buildScene(m, bind ? &*bind : nullptr, allParts);

    std::vector<NamedClip> clips;
    for (const auto& spec : anims) {
        if (spec.ends_with(":*")) {
            Archive a(spec.substr(0, spec.size() - 2));
            for (const auto& e : a.dir.entries) {
                auto d = a.member(e);
                if (!AnimClip::isAnim(d)) continue;
                auto c = AnimClip::parse(d);
                if (c.tripletCount == 5 && c.quatCount == 14) clips.push_back({e.name, std::move(c)});
            }
        } else {
            clips.push_back({specStem(spec), AnimClip::parse(loadInput(spec))});
        }
    }
    if (!clips.empty() && !scene.rig) throw std::runtime_error("--anim needs a player model");

    fs::create_directories(out);
    writeGltf(out, name, scene, tex, clips);
    std::size_t tris = 0, used = 0;
    for (const auto& p : scene.parts)
        for (const auto& pr : p.prims) tris += pr.indices.size() / 3;
    for (const auto& mat : m.materials) used += tex.contains(mat.texture);
    std::printf("%s: %zu nodes, %zu parts, %zu triangles, %zu/%zu materials textured, %zu animations -> %s\n", model.c_str(), m.nodes.size(),
                scene.parts.size(), tris, used, m.materials.size(), clips.size(), (out / (name + ".gltf")).c_str());
    if (preview) wc98::writeFile(out / (name + "_preview.png"), renderPreview(scene, tex, 512));
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "wc98-model-export: %s\n", e.what());
    return 1;
}
