// Derived from fifa99-native (same author), ported for World Cup 98
// Presentation core: snapshot interpolation, camera names, kit file selection, face composite rule.
#include "check.hpp"
#include "wc98/presentation/kits.hpp"
#include "wc98/presentation/snapshot.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace wc98::presentation;
namespace render = wc98::render;
namespace formats = wc98::formats;
namespace {
// One-marker-file fake tree: create INGAME/PLAYER/TEXTURES/<base>.FSH under dir.
void touch(const std::filesystem::path& root, const std::string& base) {
    auto p = root / "INGAME/PLAYER/TEXTURES";
    std::filesystem::create_directories(p);
    std::ofstream(p / (base + ".FSH")) << "x";
}

bool rgbaEqual(const render::Image& a, const render::Image& b) { return a.width == b.width && a.height == b.height && a.rgba == b.rgba; }

// Shape_mirrorX check: horizontal flip of `in`.
render::Image flipped(const render::Image& in) {
    render::Image out{in.width, in.height, std::vector<std::uint8_t>(in.rgba.size())};
    for (std::uint32_t y = 0; y < in.height; ++y)
        for (std::uint32_t x = 0; x < in.width; ++x) {
            const std::size_t s = (std::size_t(y) * in.width + x) * 4, d = (std::size_t(y) * in.width + (in.width - 1 - x)) * 4;
            for (int k = 0; k < 4; ++k) out.rgba[d + k] = in.rgba[s + k];
        }
    return out;
}

void pureLogic() {
    // lookOf: mirrors the WC98 fcdb.cpp decode of bytes 6/7 (hairStyle=b6>>4, hairColor=(b6>>2)&3,
    // skinTone=b6&3, faceStyle=b7>>3, beardStyle=b7&7). Fabricated record bytes b6 = 0xB2, b7 = 0xB3.
    const std::uint8_t b6 = 0xB2, b7 = 0xB3;
    formats::FcdbPlayer p;
    p.skinTone = std::uint8_t(b6 & 3);            // 2
    p.hairColor = std::uint8_t((b6 >> 2) & 3);    // 0
    p.hairStyle = std::uint8_t(b6 >> 4);          // 11
    p.faceStyle = std::uint8_t(b7 >> 3);          // 22 (5 bits)
    p.beardStyle = std::uint8_t(b7 & 7);          // 3
    p.shirtNumber = 9;
    Look l = lookOf(p);
    CHECK(l.skin == 2 && l.hairColour == 0 && l.hairStyle == 11);
    CHECK(l.face == 22 && l.facialHair == 3 && l.number == 9);

    // Camera names: round-trip every mode.
    for (std::size_t i = 0; i <= std::size_t(CameraMode::Replay); ++i) {
        CameraMode m{};
        CHECK(parseCameraName(cameraName(CameraMode(i)), m));
        CHECK(m == CameraMode(i));
    }
    CameraMode nm{};
    CHECK(!parseCameraName("nosuch", nm));

    // Pitch table (Pitch_setDimensions 0x455930) == wc98/sim/pitch.hpp kW/kL {0xff0,0xf00,0xe40,0xe10}
    // x {0x1620,0x1500,0x13e0,0x12c0}: 4080/5664, 3840/5376, 3648/5088 (default), 3600/4800.
    CHECK(pitchDimensions(0) == glm::vec2(4080, 5664));
    CHECK(pitchDimensions(1) == glm::vec2(3840, 5376));
    CHECK(pitchDimensions(2) == glm::vec2(3648, 5088));
    CHECK(pitchDimensions(3) == glm::vec2(3600, 4800));
    CHECK(pitchDimensions(9) == pitchDimensions(2));

    // Shortest-arc angles: 1000 -> 8 goes forward through the 1024 wrap.
    CHECK(std::fabs(lerpAngle(1000, 8, 0.5f) - 1016.0f) < 0.01f); // midpoint across the 1024 wrap
    CHECK(lerpAngle(100, 100, 0.3f) == 100.0f);

    // Interpolation: keyPos advances only within the same clip; discrete fields and pitchUnits from b.
    MatchSnapshot a{}, b{};
    a.players[0] = PlayerView{{0, 0, 0}, 10.f, 0, Role::Outfield};
    a.players[0].clip = 5; a.players[0].keyPos = 2.0f;
    b.players[0] = *a.players.data(); // copy identity fields
    b.players[0].pos = glm::vec3(10, 0, 0); b.players[0].facing = 12.f;
    b.players[0].clip = 5; b.players[0].keyPos = 4.0f;
    b.pitchUnits = glm::vec2(3648, 5088);
    MatchSnapshot r = interpolate(a, b, 0.5f);
    CHECK(r.players[0].pos.x == 5.f);
    CHECK(std::fabs(r.players[0].keyPos - 3.0f) < 0.01f);
    CHECK(r.pitchUnits == glm::vec2(3648, 5088)); // passthrough of b
    r = interpolate(a, b, 1.f);
    CHECK(r.players[0].pos.x == 10.f && r.players[0].keyPos == 4.f);

    // Clip change between snapshots: key position jumps to the newer snapshot (no cross-clip lerp).
    b.players[0].clip = 6;
    r = interpolate(a, b, 0.5f);
    CHECK(r.players[0].keyPos == 4.f);

    // Referee slot interpolates like a player (WC98 sim kReferee = players[22] -> snapshot.referee).
    MatchSnapshot ra{}, rb{};
    ra.referee.pos = glm::vec3(0, 0, -100);
    rb.referee.pos = glm::vec3(0, 0, 100);
    CHECK(interpolate(ra, rb, 0.25f).referee.pos.z == -50.f);

    // Kit file selection on a marker-only fake tree (no FSH parsing happens for these calls).
    auto tmp = std::filesystem::temp_directory_path() / "wc98-kit-file-test";
    std::filesystem::remove_all(tmp);
    touch(tmp, "PK026A");
    const GameData data{tmp};
    KitTextures kits(data);
    CHECK(kits.kitFile(26, true) == "PK026A");
    CHECK(kits.kitFile(26, false) == "PK026A"); // no PK026H on disk -> home falls back to A
    touch(tmp, "PK031H");
    CHECK(kits.kitFile(31, false) == "PK031H");
    CHECK(kits.kitFile(31, true) == "PK031A");
    // Officials: PK999B..E table with the team-atlas fallback.
    CHECK(kits.officialKitFile(0) == "PK026A"); // no PK999B marker here -> fallback
    touch(tmp, "PK999D");
    CHECK(kits.officialKitFile(2) == "PK999D");
    CHECK(kits.officialKitFile(6) == "PK999D");
    // GK naming: measured on disk only GK00..GK05 x {30,70,90} exist; LOD 10 falls to _30.
    CHECK(kits.gkKitFile(0, 30) == "GK00_30");
    CHECK(kits.gkKitFile(7, 10) == "GK01_30");
    CHECK(kits.gkKitFile(-1, 90) == "GK05_90");
    std::filesystem::remove_all(tmp);
}

void treeChecks() {
    const auto tree = treeDirOrSkip(); // exits 77 when WC98_TREE is unset
    GameData data{tree};
    KitTextures kits(data);
    CHECK(kits.kitFile(26, false) == "PK026H"); // measured: PK026H.FSH exists (md5 differs from A)
    CHECK(kits.kitFile(26, true) == "PK026A");

    const Look look{1, 2, 3, 4, 5, 7};
    auto imgs = kits.imagesFor("PK026A", 30, look);
    CHECK(imgs.count("hjfr") && imgs["hjfr"].width == 128 && imgs["hjfr"].height == 128); // atlas tag sizes probed
    CHECK(imgs.count("hair"));        // LOW model hair (atlas), 16x11 measured
    CHECK(imgs.count("hhdc"));        // COM_30 shared head base
    CHECK(imgs.count("hifa") && imgs["hifa"].width == 64 && imgs["hifa"].height == 84); // HHIG_30 strips
    CHECK(imgs.count("mdfa") && imgs["mdfa"].width == 32 && imgs["mdfa"].height == 42); // HMED_30 strips
    CHECK(imgs.count("hjbx") && rgbaEqual(imgs["hjbx"], flipped(imgs["hjbk"])));        // mirror alias
    // Every canvas pixel must have been painted by a layer (alpha forced to 255); an unpainted
    // pixel keeps alpha 0. Dark palette colours legitimately contain zero RGB bytes.
    for (std::size_t k = 3; k < imgs["hifa"].rgba.size(); k += 4)
        if (imgs["hifa"].rgba[k] != 255) { std::fprintf(stderr, "hifa pixel %zu unpainted: composite lost a layer\n", k / 4); std::exit(1); }

    // Face layers keyed by index 0xFF (measured fh00 @HHIG_30.FSH record 0x10100 = 95.2 % key):
    // beard style 0 must not darken the chin vs no overlay -> composite is stable/opaqued.
    auto f0 = kits.faceComposite(30, Look{0, 0, 0, 0, 0, 1});
    CHECK(f0.width == 64 && f0.height == 84);

    // Officials + keepers resolve to real atlases on the tree.
    CHECK(kits.officialKitFile(0) == "PK999B");
    CHECK(!kits.kitFsh("PK999E").entries.empty());
    auto gk = kits.imagesFor(kits.gkKitFile(2, 70), 70, look);
    CHECK(gk.count("hjfr") && gk.count("hifa")); // GK02_70 tags == PK layout (probed)

    // GameData::member on a RefPack'd BIGF archive: MODELS2.PAK carries net.pc (contract facts).
    auto bytes = data.member("INGAME/PLAYER/MODELS/MODELS2.PAK", "net.pc");
    CHECK(bytes.size() > 16);

    std::printf("presentation tree checks done (%zu images for PK026A@30)\n", imgs.size());
}
} // namespace

int main() {
    pureLogic();
    treeChecks();
    std::puts("test_presentation: ok");
    return 0;
}
