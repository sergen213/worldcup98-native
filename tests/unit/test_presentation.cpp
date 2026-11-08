// Derived from fifa99-native (same author), ported for World Cup 98
// Presentation core: snapshot interpolation, camera names, kit file selection, face composite rule.
#include "check.hpp"
#include "wc98/presentation/kits.hpp"
#include "wc98/presentation/camera.hpp" // CamGridBox broadcast grid box gate
#include "wc98/presentation/snapshot.hpp"

#include "wc98/presentation/fe_screen.hpp"
#include "wc98/formats/fsh.hpp"
#include "wc98/formats/refpack.hpp"
#include "wc98/file.hpp"

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
    // Broadcast grid box (WC98 FIFAWC.EXE FUN_00501910 + init FUN_00500e60). Arena 5760x7680 signed
    // from centre: scale FX=Fixed(11/w)=125, FZ=Fixed(21/h)=179; cell sizes w/11=523, h/21=365. The
    // origin (0,0) is the arena centre -> cell (6, 11); edges clamp to [0..12] x [0..22].
    {
        CamGridBox g;
        camGridBoxInit(g, 5760, 7680);
        CHECK(g.scaleFX == 125 && g.scaleFZ == 179);
        CHECK(g.sizeX == 523 && g.sizeZ == 365);
        const auto [idx, comp] = camGridBoxUpdate(g, false, {0, 0}, {0, 0});
        CHECK(idx == 149 && comp == 149); // 6 + 11*13 and the symmetric complement 298-idx
        // Guard rectangle around the box footprint anchored at camPos: ball (0,0) with the box based
        // there stays put on follow=true (cell state preserved from the recompute that seeded it).
        const auto r2 = camGridBoxUpdate(g, true, {0, 0}, {2615, 3650});
        CHECK(r2.first == idx && g.cellX == 6 && g.cellZ == 11);
        const auto [ix2, ic2] = camGridBoxUpdate(g, true, {100000, 100000}, {0, 0}); // far corner
        CHECK(g.cellX == 0xc && g.cellZ == 0x16 && ix2 + ic2 == 298);               // clamp max
        camGridBoxUpdate(g, true, {-100000, -100000}, {0, 0});
        CHECK(g.cellX == 0 && g.cellZ == 0);                                        // clamp min
    }
    // --- WC98 task-handler camera model (TASK_HANDLERS_WC98.md section 7 deltas 1-2 gate) -----
    // Byte-proven preset rows installed from the exe image (delta 2): h12 DAT_00585FEA/EC/FEE/FF0,
    // h14 per-slot DAT_00586046/48/4A/4C, h28 singleton DAT_005862D2/2D4/2D6/2D8.
    {
        CHECK(cameraPresetRowCount() == 2); // h12 rows; h14 has 4
        const auto r0 = cameraPresetH12(0), r1 = cameraPresetH12(1);
        CHECK(r0.angle == 0x244c && r0.speed == 0xf000 && r0.height == 0x7b8 && r0.yawOff == 0x30);
        CHECK(r1.angle == 0x5bab && r1.speed == 0xf000 && r1.height == 0x7b8 && r1.yawOff == 0x30);
        const auto s0 = cameraPresetH14(0), s1 = cameraPresetH14(1), s2 = cameraPresetH14(2), s3 = cameraPresetH14(3);
        CHECK(s0.angle == 0x0 && s1.angle == 0x7fa6 && s2.angle == 0x2000 && s3.angle == 0x4000);
        CHECK(s1.speed == 0xf000 && s2.height == 0x7b8 && s3.yawOff == 0x30); // all h14 rows share the tail fields
        const auto p28 = cameraPresetH28();
        CHECK(p28.angle == 0x445c && p28.speed == 0xf3e8 && p28.height == 0x8fc && p28.yawOff == 0x30);
    }

    // h23 FUN_004cafe0 quality-tier scene camera (section 7 delta 5): tier table {0x71,0x3d}/
    // {0x65,0x31}/{0x59,0x25}, pitch-edge x/z clamps and flags bit4/bit5 z-source lanes [P].
    {
        const auto t1 = tierCamera(1, 0x10, 1000, 3000, 100, 200, 300);
        CHECK(t1.x == 1000 + 0x71 * 0x10 && t1.z == 100 && t1.polarDist == 0x3d << 4 && t1.speedAngle == 0x640); // bit4 set -> ball z
        const auto t2 = tierCamera(2, 0x10, 1000, 3000, 100, 200, 300);
        CHECK(t2.x == 1000 + 0x65 * 0x10 && t2.polarDist == 0x31 << 4);
        const auto t3 = tierCamera(3, 0x10, 1000, 3000, 100, 200, 300);
        CHECK(t3.x == 1000 + 0x59 * 0x10 && t3.polarDist == 0x25 << 4);
        CHECK(tierCamera(0, 0x10, 1000, 3000, 100, 200, 300).x == t1.x); // quality <=1 shares low lane
        CHECK(tierCamera(4, 0x10, 1000, 3000, 100, 200, 300).x == t1.x); // >3 exe fall-through [P]
        const auto zB = tierCamera(1, 0x00, 1000, 3000, 100, 200, 300);   // bit4 clear -> block DAT_00644860
        CHECK(zB.z == 200);
        const auto zP = tierCamera(1, 0x20, 1000, 3000, 100, 200, 300);   // bit4 clear + bit5 -> DAT_00644820
        CHECK(zP.z == 300);
        CHECK(tierCamera(1, 0x30, 1000, 3000, 100, 200, 300).z == 100);   // bit4 set wins over bit5 [P]
        const auto lo = tierCamera(3, 0x10, -1000, 3000, 100, 200, 300);  // x lower clamp -0x140-xEdge
        CHECK(lo.x == -0x140 + 1000);
        CHECK(tierCamera(1, 0x10, 1000, 1000, 9999, 200, 300).z == 1000 - 0x2a0); // z upper clamp zEdge-0x2a0
        CHECK(tierCamera(1, 0x10, 1000, 1000, -9999, 200, 300).z == 0x230 - 1000); // z lower clamp 0x230-zEdge
    }

    // h21 FUN_004ca690 ball-follow TV camera (DATA-DELTA closure): source triple select by task
    // flags + inset-edge bisection clamps +/- (halfW-0x1e0)/(halfL-0x140), 0xeb58 constant [P].
    {
        const auto f0 = followCamera(0x00, 10, 20, 500, 600, 700, 800, 2500, 2674); // ball default [P]
        CHECK(f0.x == 10 && f0.z == 20 && f0.speedAngle == 0xeb58);
        const auto fp = followCamera(0x10, 10, 20, 500, 600, 700, 800, 2500, 2674); // byte0 bit4 -> pair
        CHECK(fp.x == 500 && fp.z == 600);
        const auto fc = followCamera(0x110, 10, 20, 500, 600, 700, 800, 2500, 2674); // byte[1] bit2 wins
        CHECK(fc.x == 700 && fc.z == 800);
        const auto cl = followCamera(0x00, 99999, -99999, 0, 0, 0, 0, 2500, 2674);
        CHECK(cl.x == 2500 - 0x1e0 && cl.z == -(2674 - 0x140)); // inset-edge clamp [P]
    }

    // h18 FUN_004cb600 pitch-edge side cam + h20 overhead [P]: lane thresholds 0x1e0/-0x1df,
    // far zEdge+0x140 / near -zEdge-0x140, rec+0x110 mid band with forced-near guard.
    {
        const auto far = sideCamera(0x200, 0x777, 3000, 0x1140); // ball x >= 0x1e0 -> far lane
        CHECK(far.z == 3000 + 0x140 && far.polarHeight == 0x1140 - 0x10);
        const auto near = sideCamera(-0x1e0, 0x777, 3000, 0x1140); // ball x < -0x1df -> near lane
        CHECK(near.z == -(3000 + 0x140));
        const auto mid = sideCamera(0, 500, 3000, 0x1140);   // rec+0x110 constant mismatch -> near
        CHECK(mid.z == -(3000 + 0x140));
        const auto keep = sideCamera(0, 3000 + 0x140, 3000, 0x1140); // matches far lane -> kept
        CHECK(keep.z == 3000 + 0x140);
        const auto ov = overheadCamera(2500); // h20: rec+0x8 = -xEdge, angle/speed 0x90
        CHECK(ov.polarDist == -2500 && ov.angleSpeed == 0x90);
    }
    // tabA_pre height prelude FUN_004a0790 on DAT_0057FB82 u16[32]: value = H[idx+1] with the
    // sentinel lanes -5 -> rec+0x78, -4 -> rec+0x114, -1 -> mode constants (pass-12cam).
    {
        CHECK(tabATaskHeight(0, 0, false, false, 0xe28, 0x114) == 0xcc0); // H[1]
        CHECK(tabATaskHeight(3, 0, false, false, 0xe28, 0x114) == 0xa00); // H[4] (idx+1 read)
        CHECK(tabATaskHeight(8, 0, false, false, 0xe28, 0x114) == 0xe28); // H[9]=0xfffb -> player height
        CHECK(tabATaskHeight(11, 0, false, false, 0xe28, 0x114) == 0x114); // H[12]=0xfffc -> cam height
        CHECK(tabATaskHeight(13, 1, false, false, 0, 0) == 0x1280);       // H[14]=0xffff: mode 1
        CHECK(tabATaskHeight(13, 2, false, false, 0, 0) == 0x440);        // mode 2, liveRefPos off
        CHECK(tabATaskHeight(13, 2, true, false, 0, 0) == 0x1280);        // mode 2, DAT_00644858 > -1
        CHECK(tabATaskHeight(13, 3, false, true, 0, 0) == 0x1980);        // mode 3 + rec+0x10==3
        CHECK(tabATaskHeight(13, 3, false, false, 0, 0) == 0x1100);       // mode 3 base
        CHECK(tabATaskHeight(13, 7, false, false, 0, 0) == 0x1540);       // default lane
    }
    // Quarter-wave sine DAT_00585898 (T[i] = sin(2*pi*i/1024)*65536) + FUN_00497740 fold.
    {
        CHECK(sineWc98(1) == 0x192 && sineWc98(8) == 0x0C8F); // image bytes at raw 0x16DE98
        CHECK(sineWc98(256) == 0x10000);                      // adjacent word the exe fold indexes into
        CHECK(sine16x16(0) == 0 && sine16x16(0x4000) == 0x10000); // quarter-turn identity exact in table mode
        CHECK(cosine16x16(0) == 0x10000 && sine16x16(-0x8000) == 0);
    }
    // Timer B fade (FUN_00517b50/cc0): step = 0x10000*0x10000/end, live counts up, snap at end.
    {
        CameraTask t;
        CHECK(t.playerHeight() == 0xe28); // rec+0x78 pool-init default (FUN_004a0810)
        t.setHandler(13);
        CHECK(t.handler() == 13 && t.event() == 13); // rec+8 / rec+0xc written together by pool init
        t.startFade(64);
        CHECK(t.fadeActive());
        CHECK(t.advanceFade(32) == -32);            // live-end at halfway
        CHECK(t.fadeRamp16x16() == 0x8000);         // (live*step)>>16 midpoint
        CHECK(t.advanceFade(32) == 0 && !t.fadeActive()); // completion snap: live = end, bit cleared
        CHECK(t.fadeRamp16x16() == 0x10000);
        CHECK(t.advanceFade(1) == -1);              // FUN_00517cc0 inactive path
    }
    // Timer C position dolly (FUN_00517e30/be0/f20 + commit trio 0x517730/760/790): per-axis steps
    // |delta|<<16/dur, magnitudes x sign(B-A), completion snaps A = B. dur=48 divides all axes by 96
    // so the midpoints are exact; fixed rounding (>>16 + bit-15 carry) keeps them integral.
    {
        CameraTask t;
        t.startDolly({-2000, 300, -3500}, {2600, 900, 1800}, 48); // dx=4600 dy=600 dz=5300
        CHECK(t.dollyActive());
        for (int i = 0; i < 12; ++i)
            CHECK(!t.advanceDolly(2)); // ticks of 2 frames: live = 24 at midpoint
        CHECK(t.eye() == glm::ivec3(300, 600, -850)); // A + (delta/2): no snap, interpolation proven
        CHECK(t.snapshotA() == glm::ivec3(-2000, 300, -3500) && t.snapshotB() == glm::ivec3(2600, 900, 1800));
        for (int i = 0; i < 11; ++i)
            CHECK(!t.advanceDolly(2));
        CHECK(t.advanceDolly(2)); // live reaches end: completing frame
        CHECK(!t.dollyActive() && t.eye() == glm::ivec3(2600, 900, 1800)); // A snapped to B
        t.commit({400, 500, 600});                                          // commit trio: B = live, A = B
        CHECK(t.snapshotA() == glm::ivec3(400, 500, 600) && t.snapshotB() == t.snapshotA());
    }
    // MatchCamera integration (delta 1): the transition interpolates instead of snapping. Deltas
    // from the live eye are multiples of 96 so mid-glide eye is exactly halfway; ticks x frames = 2.
    {
        MatchCamera cam;
        const MatchSnapshot s{};
        cam.tick(s, 2); // seeds the follow state (initial snap)
        const glm::ivec3 e0 = cam.eye();
        const glm::ivec3 target = e0 + glm::ivec3(4608, 576, 4704);
        cam.glideTo(target, 48);
        CHECK(cam.gliding());
        for (int i = 0; i < 12; ++i)
            cam.tick(s, 2); // live = 24
        CHECK(cam.eye() == e0 + glm::ivec3(2304, 288, 2352)); // halfway: no snap to target
        CHECK(cam.gliding());
        for (int i = 0; i < 12; ++i)
            cam.tick(s, 2); // live = 48: completion snaps A = B
        CHECK(!cam.gliding() && cam.eye() == target);
        cam.tick(s, 2); // release: normal follow resumes from the dolly endpoint (no snap back)
        CHECK(cam.task().snapshotA() == target && !cam.gliding()); // follow state lives at the endpoint, not snapped back
    }

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

    // FE mouse cursor art (MAUS.PAK, exe string offset 1451894): two `curs` entries; entry 0 is
    // the 28x26 arrow with real transparent pixels and hotspot straight from the FSH header.
    if (auto pal = wc98::findCaseInsensitive(tree, "FE/PACK/PAL.PAK"); !pal.empty())
        formats::setGlobalPalette(formats::wc98GlobalPaletteFromPalArchive(wc98::readFile(pal))); // app loadFePalette pattern
    wc98::presentation::FeCursor cur;
    CHECK(wc98::presentation::loadFeCursor(tree, cur));
    CHECK(cur.entries == 2);
    CHECK(cur.bmp.width == 28 && cur.bmp.height == 26 && !cur.bmp.rgba.empty());
    {
        const auto maus = formats::FshFile::parse(formats::refpack::decompressIfNeeded(
            wc98::readFile(wc98::findCaseInsensitive(tree, "FE/PACK/MAUS.PAK"))));
        CHECK(maus.entries.size() == 2 && maus.entries[0].tag == "curs");
        CHECK(cur.hotX == maus.entries[0].bitmap->centerX && cur.hotY == maus.entries[0].bitmap->centerY);
        int zeros = 0;
        for (std::size_t k = 3; k < cur.bmp.rgba.size(); k += 4)
            if (cur.bmp.rgba[k] == 0) ++zeros;
        CHECK(zeros > 0); // the arrow is keyed, not an opaque rectangle (measured: 250/728)
    }
    // REDLEFT.PAK ships keyb/joys/gpad/grav only: CONTROL1.LAY's `mous` shape is a shipped-asset
    // gap (the original silently skips it), not a loader bug.
    const auto redleft = formats::FshFile::parse(formats::refpack::decompressIfNeeded(
        wc98::readFile(wc98::findCaseInsensitive(tree, "FE/PACK/FEFLOW09/REDLEFT.PAK"))));
    std::string rlTags;
    for (const auto& e : redleft.entries) { CHECK(e.tag != "mous"); rlTags += e.tag + ","; }
    for (const char* t : {"keyb", "joys", "gpad", "grav"})
        CHECK(rlTags.find(std::string(t) + ",") != std::string::npos);

    // FE text colour-state gate (LAY_COMPOSITOR_NOTES RESOLVED): the hilite pane draws its label
    // with HILITECOLOUR, every other pane with NORMALCOLOUR. Evidence: parse chain 0x46e0d9/0x46e10b
    // maps NORMALCOLOUR/HILITECOLOUR to widget fields +0x68/+0x6C (global-palette indices), and the
    // listbox draw FUN_00477f54 @0x477f54 takes the hilite text path only for the selected row.
    struct NullRenderer : render::Renderer {
        wc98::render::TextureId createTexture(const wc98::render::Image&, wc98::render::Wrap, std::string_view) override { return ++n; }
        void destroyTexture(wc98::render::TextureId) override {}
        wc98::render::MeshId createMesh(const wc98::render::MeshData&, std::string_view) override { return 0; }
        void destroyMesh(wc98::render::MeshId) override {}
        void setSettings(const wc98::render::RenderSettings&) override {}
        const wc98::render::RenderSettings& settings() const override { static wc98::render::RenderSettings s; return s; }
        bool present(const wc98::render::Frame&) override { return false; }
        wc98::render::Image renderOffscreen(const wc98::render::Frame&, std::uint32_t, std::uint32_t) override { return {}; }
        std::uint32_t validationMessageCount() const override { return 0; }
        std::string deviceName() const override { return "null"; }
        wc98::render::TextureId n = 0;
    } nullR;
    {
        wc98::presentation::FeText txt(nullR, tree);
        CHECK(txt.ready());
        // Pick the first FC string index with a non-empty label (deterministic on this tree).
        int textId = -1;
        for (int id = 1; id < 512 && textId < 0; ++id) {
            formats::LayoutObject probe; probe.props["TEXTID"] = std::to_string(id);
            if (!txt.label(probe).empty()) textId = id;
        }
        CHECK(textId > 0);
        const auto mk = [&](const char* pane) {
            const std::string lay = std::string("[screen]\nnumobjects=1\n") + (pane ? "HILITEPANE=TXT\n" : "") +
                "[object 1]\nobjecttype=0\nobjectname=TXT\nxposition=10\nyposition=10\nwidth=12\nheight=12\n"
                "vpath=.\\global\\\nfname=null.fsh\nshapeid=null\ntextid=" + std::to_string(textId) +
                "\nfontid=4\nhilitecolour=2\nnormalcolour=1\n";
            auto d = formats::LayoutDocument::parse(std::string_view(lay));
            return formats::ScreenLayout::build("HLTEST", d, nullptr, nullptr);
        };
        wc98::presentation::ScreenArt art(nullR, tree, tree / "FE" / "INGAME");
        const auto& pal = formats::globalPalette();
        CHECK(pal.size() > 21);
        const std::uint32_t wantN = render::packRgba(pal[1][0], pal[1][1], pal[1][2]);
        const std::uint32_t wantH = render::packRgba(pal[2][0], pal[2][1], pal[2][2]);
        render::SpriteBatch uiN, uiH;
        CHECK(composite(mk(nullptr), uiN, art, &txt).texts > 0);
        CHECK(composite(mk("TXT"), uiH, art, &txt).texts > 0);
        const auto firstRgba = [](const render::SpriteBatch& u) { return u.vertices().empty() ? 0u : u.vertices()[0].rgba; };
        CHECK(firstRgba(uiN) == wantN);   // plain pane -> NORMALCOLOUR index 1
        CHECK(firstRgba(uiH) == wantH);   // hilitePane=TXT -> HILITECOLOUR index 2
        // Scene-ownership visibility (FE main-menu composite): objectname whitelist; foreign-scene
        // objects count as hidden and are never painted.
        const std::vector<std::string> visTxt{"TXT"}, visNone{};
        render::SpriteBatch uiV, uiX;
        CHECK(composite(mk(nullptr), uiV, art, &txt, &visTxt).texts > 0);
        const auto hid = composite(mk(nullptr), uiX, art, &txt, &visNone);
        CHECK(hid.texts == 0 && hid.hidden == 1);
        // Widget-class dynamic content: a list widget carrying TEXTID (MNMSETUP's modem lists,
        // objecttype=4) is engine-driven; a static composite must not draw the bank caption.
        const std::string wlay = "[screen]\nnumobjects=1\n[object 1]\nobjecttype=4\nobjectname=LST\n"
            "xposition=10\nyposition=10\nwidth=40\nheight=12\nvpath=.\\global\\\nfname=null.fsh\nshapeid=null\ntextid="
            + std::to_string(textId) + "\nfontid=4\nnormalcolour=1\n";
        auto wd = formats::ScreenLayout::build("LSTTEST", formats::LayoutDocument::parse(std::string_view(wlay)), nullptr, nullptr);
        render::SpriteBatch uiW;
        CHECK(composite(wd, uiW, art, &txt).texts == 0);
        // Button-class interaction states (Feflow06 side icons, nvicons.fsh qui1/qui5 measured):
        // hover/press swap the shape keys; the released key is absent on this tree -> Normal misses.
        const std::string blay = "[screen]\nnumobjects=1\n[object 1]\nobjecttype=32\nobjectname=BTN\n"
            "xposition=0\nyposition=0\nwidth=55\nheight=50\nvpath=.\\global\\\nfname=nvicons.fsh\n"
            "releasedshapeid=zzz9\nhilitedshapeid=qui5\npushedshapeid=qui1\n";
        auto bd = formats::ScreenLayout::build("BTNTST", formats::LayoutDocument::parse(std::string_view(blay)), nullptr, nullptr);
        render::SpriteBatch uiBN, uiBH, uiBP;
        const auto stN = composite(bd, uiBN, art, nullptr, nullptr, nullptr);
        CHECK(stN.missing >= 1 && stN.shapes == 0);
        wc98::presentation::FeUiState hs; hs.hover = "BTN"; hs.mx = -1.f; hs.my = -1.f;
        const auto stH = composite(bd, uiBH, art, nullptr, nullptr, &hs);
        CHECK(stH.shapes >= 1 && stH.missing == 0); // HILITEDSHAPEID qui5 paints
        wc98::presentation::FeUiState ps; ps.pressed = "BTN"; ps.mx = -1.f; ps.my = -1.f;
        const auto stP = composite(bd, uiBP, art, nullptr, nullptr, &ps);
        CHECK(stP.shapes >= 1 && stP.missing == 0); // PUSHEDSHAPEID qui1 paints
        // POINTS= polygon hit box (fe07 modem quad): centroid inside, corner far outside.
        formats::LayoutObject po;
        po.type = int(formats::LayoutObjectType::Tbb); po.name = "P";
        po.x = 266; po.y = 236; po.width = 65; po.height = 78;
        po.props["POINTS"] = "4+\n,268,269, 290,240, 330,240, 284,313";
        CHECK(widgetHit(po, 300.f, 270.f));
        CHECK(!widgetHit(po, 267.f, 315.f));
    }
    // Language knob (--lang): every shipped FC*.BIN bank loads through FeText and the same
    // label index resolves per-language (FCFRE 220 "Are you sure?" differs from FCENG).
    for (const char* code : {"eng", "fre", "ger", "ita", "spa", "dut", "swe", "dan", "jap"}) {
        wc98::presentation::FeText txt(nullR, tree, code);
        CHECK(txt.ready());
        CHECK(txt.language() == std::string_view(code));
    }
    {
        const auto eng = wc98::presentation::FeText(nullR, tree, "eng");
        const auto fre = wc98::presentation::FeText(nullR, tree, "fre");
        formats::LayoutObject probe; probe.props["TEXTID"] = "220"; // Are you sure.. (FCENG.BIN:178)
        const std::string a = eng.label(probe), b = fre.label(probe);
        CHECK(a.size() > 3 && b.size() > 3 && a != b);
    }
}
} // namespace

int main() {
    pureLogic();
    treeChecks();
    std::puts("test_presentation: ok");
    return 0;
}
