// Derived from fifa99-native presentation/src/match_scene.cpp (same author); the FIFA 99 exe
// addresses in the comments document the shared Gen3 engine family (GHIDRA_PASS5_IG.md).
// MatchScene: the original World Cup 98 match presentation on the backend-neutral renderer.
// Coordinates are WC98 game units (1/16 ft, snapshot.hpp); x across the pitch, y up, z along it.
//
// Port deltas vs FIFA 99 (deliberate scope cuts for this slice):
//   * No RPARMS.DAT: the WC98 tree has no DATA/BE render-parameters archive (measured: the tree is
//     INGAME/, COMMON/, FE/, ...; probe of $WC98_TREE). Sky/fog/shadow tints and line colours are
//     constants instead of the rparms lerp tables.
//   * No brightness grid / SKIN.SHD .lm prelight: WC98 ships no CMAP `.lm` files for the player
//     models (measured ls INGAME/PLAYER/MODELS), so DrawItem::lightBlend stays zero and vertex
//     colours come from the PCMD prelit corners.
// * HUD: the scoreboard strip, clock, radar and shirt-number labels are ported (presentation/src/
//   hud.cpp layout + drawHud below; docs/reverse-engineering/HUD_WC98.md). The controller ground
//   markers use the MISC8 sb0* diamonds tinted by the f99 family table exe::kControllerTint.
// * Shirt-number decal tint: skin tones ride PlayerView::skinTone, supplied from
//     MatchData::anim with the AISEQS.TBI clip list and the HIG_30.PC rig (docs/reverse-engineering/
//     SIM_PORT_NOTES.md section 5); this scene owns one scratch MatchData for that.
#include "wc98/presentation/match_scene.hpp"
#include "wc98/presentation/hud.hpp"

#include "exe_tables.inc"

#include "wc98/formats/big.hpp"
#include "wc98/formats/fsh.hpp"
#include "wc98/formats/ffn.hpp"
#include "wc98/formats/refpack.hpp"
#include "wc98/log.hpp"
#include "wc98/sim/anim.hpp"
#include "wc98/sim/data.hpp"
#include "wc98/sim/pitch.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string_view>

namespace wc98::presentation {

using namespace wc98::formats;
using render::Image;
using render::Material;
using render::MeshId;
using render::TextureId;

namespace {

// stadium table 0x57C688 +4/+8 (render) -> game units, port of the FIFA 99 sibling.
constexpr float kStadiumScale = 415.0f / 4.0f;
// SOFTIMAGE-exported models (stadium bowl, nets) are right-handed with v pointing up: in the
// game's left-handed view every text texture (boards, stadium sign) only reads correctly with x
// mirrored and v flipped. Mirror placement: hypothesis from the assets; the exe code doing it was
// not located (f99 comment block kept verbatim).
constexpr float kPlayerScale = 0.25f;   // FUN_0040FB98(1.0) in render units, sibling value
constexpr float kAlphaTest = 0.5f / 255.0f; // D3DA default ALPHATEST GREATER 0

// Ball disc radius: wc98/sim/ball.hpp B+0xDA4 (0x6691D4) `radius = 5` game units — the same exe
// address as FIFA 99's DAT_006691d4 (sibling value; WC98 field read from the ported sim struct).
constexpr float kBallRadius = 5.0f;

// Grass tile size in game units for the 64x64 TT01 texture (TURFS0P<col>.PAK, measured 64x64
// type-0x7B records via /tmp/m6probe). Chosen by eye from test renders; keep as the knob.
constexpr float kGrassTile = 96.0f;

// Fixed light for planar shadows: FIFA 99 Shadow_init 0x4145B4 day sun (weather 0) at render
// {43200, 1440<<6, 32000} / 4 = game units. WC98 has no traced light table; a single fixed
// sun/floodlight direction is ported as the hypothesis for the day matches (night uses the same).
struct Light {
    glm::vec3 pos; // game units
};
constexpr Light kSun{{10800.0f, 23040.0f, 8000.0f}};

std::string trimTagS(std::string t) {
    while (!t.empty() && (t.back() == ' ' || t.back() == '\0')) t.pop_back();
    return t;
}
std::string_view trimTag(std::string_view t) {
    while (!t.empty() && (t.back() == ' ' || t.back() == '\0')) t.remove_suffix(1);
    return t;
}

// Planar shadow shear onto y = 0 (Shadow_buildProjMatrix 0x4147F0): k = |obj - light|_xz / Ly <= 2.
// The k=2 clamp is the original's and is kept.
glm::mat4 shadowMatrix(glm::vec3 obj, const Light& l, float lift) {
    glm::vec2 d{obj.x - l.pos.x, obj.z - l.pos.z};
    float len = glm::length(d);
    float k = std::min(len / l.pos.y, 2.0f);
    glm::vec2 dir = len > 0 ? d / len : glm::vec2(0);
    glm::mat4 m(1.0f);
    m[1] = glm::vec4(k * dir.x, 0, k * dir.y, 0);
    m[3] = glm::vec4(0, lift, 0, 1);
    return m;
}

enum class Alpha { None, Binary, Blend };
Alpha alphaKind(const Image& img) {
    Alpha k = Alpha::None;
    for (std::size_t i = 3; i < img.rgba.size(); i += 4) {
        std::uint8_t a = img.rgba[i];
        if (a != 255 && a != 0) return Alpha::Blend;
        if (a == 0) k = Alpha::Binary;
    }
    return k;
}

// Player model chain, indices into KitTextures::kLodSuffixes {10,30,70,90}:
//   0 SHI_10 (best) -> 3 LOW_90 (worst). FIFA 99 loaded five families slo/low/med/shi from one
//   HMLSH.BIG (Player_loadModels 0x41621C); WC98 ships loose files per family+suffix
//   (measured ls INGAME/PLAYER/MODELS: SHI_10/30/70/90, HIG_30/70/90, MED_30/70/90, LOW_30/70/90).
//   The 4-step chain (one file per family) and the best->worst order SHI>HIG>MED>LOW are a port
//   hypothesis; no SLO family exists in WC98. Node counts differ per model (measured: SHI_10/HIG_30
//   28, MED_70 27, LOW_90 16 nodes), so each model gets its own rig via PlayerRig::fromModel; the
//   palette slots stay canonical because every rig orders joints by PlayerRig::kNames.
constexpr std::array<const char*, 4> kLodModels = {"SHI_10", "HIG_30", "MED_70", "LOW_90"};

// Stadium art bases observed on disk (ls $WC98_TREE/INGAME/STADIUM: <BASE>___D.BIG / ___N.BIG).
// The FCDB team-record stadium byte -> base mapping is UNTRACED in FIFAWC.EXE; the demo passes 0.
constexpr std::array<const char*, 14> kStadiumBases = {
    "STDE", "STET", "CLAS", "PARI", "PRAC", "LENS", "LYON",
    "MARS", "MONT", "NANT", "TOUL", "FINA", "SEMI", "BORD"};

// One player LOD family: model + its own rig + skinned meshes built once (all mesh nodes, one part
// per material). WC98 has no .lm corner-light files, so buildSkinnedParts gets no lights. The rig
// members stay default-constructed until loadPlayers() parses the model (PlayerRig::fromModel
// throws on an empty node list, which a member initializer would hit at Impl construction time).
struct PlayerArt {
    PcmdModel model;
    PlayerRig rig;
    std::vector<std::pair<std::uint32_t, MeshId>> parts; // material index -> mesh
};
// Player head height above the feet, in game units (f99 sibling; shirt-label anchor).
constexpr float kShirtHeadUnits = 70.f;

} // namespace

struct MatchScene::Impl {
    render::Renderer& r;
    const GameData& data;
    MatchSetup setup;
    glm::vec2 dims;                       // load-time dimensions (setup/pitchSize)
    std::array<std::uint16_t, 2> teamIds{};

    sim::MatchData scratch;               // anim clips + rig via sim::loadMatchAnimData
    int standClip = -1;                   // first fully loaded clip: pose for players with clip < 0

    std::vector<render::DrawItem> statics;   // grass, runoff, stadium bowl, goals, flags
    std::map<std::string, TextureId> misc8;  // MISC8.FSH tags (post/net2/net3/flag/pole/mshd/ball)
    std::array<TextureId, 12> ballFrames{};  // MISC16.FSH bl00..bl11
    TextureId white = 0, dark = 0, checker = 0;

    std::unique_ptr<KitTextures> kits;
    std::array<PlayerArt, 4> lods;
    PcmdModel shadowModel;
    PlayerRig shadowRig;                  // parsed in loadPlayers(); SHADOW.PC joint chain
    MeshId shadowMesh = 0;                // SHADOW.PC, dark tint baked (parts concatenated)

    std::array<std::map<int, Look>, 2> lookByKey{}; // (shirt<<8)|skin -> FCDB appearance
    std::map<std::string, std::map<std::string, TextureId>> playerTex; // atlas key -> tag -> texture
    std::set<std::string> warnedTags;     // one warning per missing (key:tag)

    std::array<int, 23> lodState{};
    std::array<bool, 23> shadowOn{};      // start off; hysteresis enables past metric 60 (drawPlayer)

    // line markings for the current live dims (rebuilt when MatchSnapshot::pitchUnits changes)
    glm::vec2 lineDims{-1, -1};
    std::vector<std::pair<glm::vec3, glm::vec3>> segs;

    // per frame
    std::vector<render::DrawItem> draws;
    std::vector<glm::mat4> joints;
    render::WorldBatch world;

    // HUD overlay (presentation/src/hud.cpp layout; docs/reverse-engineering/HUD_WC98.md).
    render::SpriteBatch ui;                       // 640x480 virtual screen, drawn last over the scene
    formats::FfnFont hudFont;                     // match font: FE/PACK/IGFONTS.BIG "eras14bi.ffn"
    TextureId fontTex = 0;                        // hudFont.atlas texture (0 until loaded)
    std::map<std::string, TextureId> hudArt;      // radar/marker FSH tags -> textures
    HudGeometry hudGeo;                           // layout knobs ([gameplay] passthrough, see HUD_WC98.md)
    std::array<std::string, 2> hudTeamNames{};    // FCDB STR0 abbreviations (fallback for s.hud.teamNames)
    bool hudReady = false;                        // font + radar panel resolved; graceful skip when false
    render::Frame frame;

    Impl(render::Renderer& rr, const GameData& d, const MatchSetup& s) : r(rr), data(d), setup(s), dims(pitchDimensions(s.pitchSize)) {}

    TextureId texture(const Image& img, render::Wrap wrap, const std::string& key) { return r.createTexture(img, wrap, key); }
    Material materialFor(TextureId t, const Image* img) {
        Material m{t};
        m.alphaCutoff = kAlphaTest;
        if (img && alphaKind(*img) == Alpha::Blend) m.blend = render::Blend::Alpha;
        return m;
    }

    FshFile parseFsh(std::string_view rel) { return FshFile::parse(formats::refpack::decompressIfNeeded(data.file(rel))); }

    void loadTextures();
    void loadPitch();
    void loadStadium();
    void loadGoals();
    void loadPlayers();
    void loadHud();
    void drawHud(const MatchSnapshot& s, const render::Camera& cam, const glm::vec2& live);
    [[nodiscard]] TextureId hudTex(const std::string& key) const;

    const std::map<std::string, TextureId>& texturesFor(const std::string& fileBase, int lodSuffix, const Look& look);
    float lodMetric(const PlayerView& p, const render::Camera& cam, float focal) const;
    void drawPlayer(const PlayerView& p, int slot, int lod, float metric, bool enhanced);
    void drawBall(const BallView& b, const render::Camera& cam, int camYaw16);
    void buildSegments();
    void drawLines(const render::Camera& cam, float focal);

    [[nodiscard]] const formats::AnimClip* clipFmt(int i) const {
        if (i < 0 || std::size_t(i) >= scratch.anim.clips.size()) return nullptr;
        return scratch.anim.clips[std::size_t(i)].fmt.get();
    }
};

// The duplicate build() declaration in the contract header was removed by its owner; this file
// implements the single declared signature (match_scene.hpp line 40).

// ------------------------------------------------------------------------------------------- load

void MatchScene::Impl::loadTextures() {
    white = texture(Image{1, 1, {255, 255, 255, 255}}, render::Wrap::Clamp, "white");
    dark = texture(Image{1, 1, {0x20, 0x20, 0x20, 255}}, render::Wrap::Clamp, "player:z4"); // 0xFF202020 (Kit_buildPlayerClut)
    // ponytail: 8x8 magenta/black checker = visible placeholder for material tags no WC98 atlas
    // carries (measured gap: HIG/SHI materials sample 'hjbx', atlases ship 'hjbk'; kits.cpp mirrors
    // hjbk->hjbx so the normal path resolves, and GK/official sets without a tag fall back here).
    // Upgrade path: trace FIFAWC.EXE's texture-resolve fallback for missing tags.
    Image chk{8, 8, std::vector<std::uint8_t>(8 * 8 * 4)};
    for (std::uint32_t y = 0; y < 8; ++y)
        for (std::uint32_t x = 0; x < 8; ++x) {
            const bool m = ((x >> 2) ^ (y >> 2)) & 1;
            chk.rgba[(y * 8 + x) * 4 + 0] = m ? 255 : 0;
            chk.rgba[(y * 8 + x) * 4 + 1] = 0;
            chk.rgba[(y * 8 + x) * 4 + 2] = m ? 255 : 0;
            chk.rgba[(y * 8 + x) * 4 + 3] = 255;
        }
    checker = texture(chk, render::Wrap::Repeat, "checker");

    // MISC16.FSH: ball roll frames bl00..bl11 (measured via /tmp/m6probe: 32x32 type-0xED records).
    FshFile m16 = parseFsh("INGAME/MISC/MISC16.FSH");
    for (int i = 0; i < 12; ++i) {
        auto img = fshImage(m16, std::format("bl{:02}", i));
        if (!img) throw std::runtime_error("MISC16.FSH: bl" + std::to_string(i) + " missing");
        ballFrames[std::size_t(i)] = texture(*img, render::Wrap::Clamp, std::format("misc16:bl{:02}", i));
    }
    // MISC8.FSH (8-bit): goal post/net/flag textures and the ball ground shadow mshd/lshd
    // (measured: rpit 180x120, ball 64x64, mshd 32x32, lshd 16x32, post 16x16, net2 16x16,
    // net3 32x16, flag 16x8, pole 5x5).
    FshFile m8 = parseFsh("INGAME/MISC/MISC8.FSH");
    for (std::string_view t : {"post", "net2", "net3", "flag", "pole", "mshd", "lshd", "ball"})
        for (const auto& e : m8.entries)
            if (e.bitmap && trimTag(e.tag) == t && !misc8.contains(std::string(t))) {
                misc8[std::string(t)] = texture(toImage(*e.bitmap), render::Wrap::Clamp, "misc8:" + std::string(t));
                break;
            }
    for (std::string_view t : {"post", "net2", "net3", "flag", "pole", "mshd"})
        if (!misc8.contains(std::string(t))) throw std::runtime_error("MISC8.FSH: tag " + std::string(t) + " missing");
}

void MatchScene::Impl::loadPitch() {
    // Grass quads tiled with TT01 from INGAME/MISC/TURFS0P<surfaceColumn>.PAK (measured members
    // TTT1 TTT2 TT01 TT02 DIRT, 64x64). Options::surfaceColumn 5 = random is resolved upstream.
    int col = std::clamp(setup.surfaceColumn, 0, 4);
    Image grass;
    for (int c : {col, 0}) { // fall back to column 0 when one is absent
        if (!data.exists(std::format("INGAME/MISC/TURFS0P{}.PAK", c))) continue;
        // Measured (probe /tmp/m6probe): TURFS0P*.PAK = RefPack(SHPI archive), not BIGF.
        FshFile f = parseFsh(std::format("INGAME/MISC/TURFS0P{}.PAK", c));
        if (auto img = fshImage(f, "TT01")) { grass = *img; break; }
    }
    if (grass.rgba.empty()) throw std::runtime_error("no TURFS0P*.PAK TT01 surface tile");
    TextureId gtex = texture(grass, render::Wrap::Repeat, std::format("turf{}:TT01", col));

    auto groundQuad = [&](glm::vec2 size, float y, std::uint32_t rgba, const std::string& key) {
        render::MeshData md;
        const float w = size.x / 2, l = size.y / 2;
        const glm::vec4 uv{0.0f, 0.0f, size.x / kGrassTile, size.y / kGrassTile}; // tile per unit
        auto corner = [&](float x, float z, float u, float v) { md.vertices.push_back({{x, y, z}, {0, 1, 0}, {u, v}, rgba}); };
        const float uw = uv.z, vw = uv.w; // Repeat wrap tiles with uv > 1
        corner(-w, l, 0, vw); corner(-w, -l, 0, 0); corner(w, -l, uw, 0); corner(w, l, uw, vw);
        md.indices = {0, 2, 1, 0, 3, 2}; // winding chosen so (v1-v0)x(v2-v0) points +y for both triangles
        Material m{gtex}; // opaque grass (f99 PITCH mesh was opaque)
        statics.push_back({r.createMesh(md, key), m, glm::mat4(1.0f), 0});
    };
    // Runoff: the standWidth x standLength surround, darker (Pitch_setDimensions 0x455930:
    // standLength = length + 0xA20 at 0x65F360, standWidth = width + 0x840 at 0x65F364).
    sim::Pitch p = sim::Pitch::make(int(setup.pitchSize));
    groundQuad({float(p.standWidth), float(p.standLength)}, -0.2f, render::packRgba(128, 128, 128), "runoff");
    // The pitch surface itself: live dimensions redraw over it; statics keep the setup dims.
    groundQuad(dims, 0.0f, 0xFFFFFFFFu, "grass");
}

void MatchScene::Impl::loadStadium() {
    if (!setup.stadiumArt) return;
    const char t = setup.time == TimeOfDay::Night ? 'N' : 'D';
    const std::string base = kStadiumBases[std::size_t(setup.stadium % kStadiumBases.size())];
    const std::string rel = std::format("INGAME/STADIUM/{0}___{1}.BIG", base, t);
    try {
        // Measured members of STDE___D.BIG via /tmp/m6probe: stad_n.pc stad_s.pc stad_e.pc
        // stad_w.pc + stadium0.fsh (SOFTIMAGE static models, SHPI plain inside the BIGF).
        FshFile atlas = FshFile::parse(data.member(rel, "stadium0.fsh"));
        std::map<std::string, TextureId> tex;
        for (const auto& e : atlas.entries)
            if (e.bitmap) tex[trimTagS(e.tag)] = texture(toImage(*e.bitmap), render::Wrap::Repeat, rel + ":" + trimTagS(e.tag));
        const glm::mat4 xf = glm::scale(glm::mat4(1.0f), glm::vec3(-kStadiumScale, kStadiumScale, kStadiumScale)); // see SOFTIMAGE mirror note
        for (std::string_view mem : {"stad_n.pc", "stad_s.pc", "stad_e.pc", "stad_w.pc"}) {
            PcmdModel m = PcmdModel::parse(data.member(rel, mem));
            for (auto& part : buildStaticParts(m)) {
                const std::string tag = trimTagS(m.materials[part.material].texture);
                auto it = tex.find(tag);
                if (it == tex.end()) log::warn("{}: stadium texture '{}' not found", rel, tag);
                Material mm = materialFor(it != tex.end() ? it->second : checker, nullptr);
                mm.doubleSided = true; // the THRASH cull state is not enabled for the bowl (f99)
                statics.push_back({r.createMesh(part.mesh, std::format("{}#{}#{:02x}", rel, mem, part.material)), mm, xf, 0});
            }
        }
    } catch (const std::exception& e) {
        setup.stadiumArt = false; // the caller's MatchSetup copy stays as passed in
        log::warn("stadium art disabled: {}", e.what());
    }
}

void MatchScene::Impl::loadGoals() {
    // MODELS2.PAK (RefPack'd BIGF, measured members 3darrow.pc arroshad.pc net.pc net_b/_l/_r.pc
    // innet*.pc cflag.pc). Materials of net.pc/net_b.pc reference MISC8 tags 'post' + 'ball'
    // (measured via MATR walk); cflag.pc uses 'flag' + 'pole'. Placed at z = +-length/2; the
    // vertical scale normalises the model height to goalHeight 0x98 = 152 (pitch.hpp 0x65F340,
    // f99 FUN_0040B88C scaled net.pc to the same DAT_0065F340). Goal-mouth width comes from
    // pitch.goalWidth 0x1c8 (pitch.hpp 0x65F344) for context; models are uniform-scaled.
    constexpr std::string_view pak = "INGAME/PLAYER/MODELS/MODELS2.PAK";
    auto yRange = [](const PcmdModel& m) {
        float lo = 1e30f, hi = -1e30f;
        for (const auto& n : m.nodes)
            for (const auto& v : n.vertices) lo = std::min(lo, v[1]), hi = std::max(hi, v[1]);
        return std::pair{lo, hi};
    };
    const sim::Pitch p = sim::Pitch::make(int(setup.pitchSize));
    auto place = [&](const PcmdModel& m, const glm::mat4& xf, const std::string& key) {
        for (auto& part : buildStaticParts(m)) {
            const std::string tag = trimTagS(m.materials[part.material].texture);
            auto it = misc8.find(tag);
            Material mm{it != misc8.end() ? it->second : checker, render::Blend::Alpha};
            mm.alphaCutoff = kAlphaTest;
            mm.doubleSided = true;
            statics.push_back({r.createMesh(part.mesh, std::format("{}#{:02x}", key, part.material)), mm, xf, 0});
        }
    };
    for (std::string_view mem : {"net.pc", "net_b.pc", "net_l.pc", "net_r.pc"}) {
        PcmdModel net = PcmdModel::parse(data.member(pak, mem));
        auto [lo, hi] = yRange(net);
        const float s = p.goalHeight / (hi - lo); // 152 game units tall
        for (int end = 0; end < 2; ++end) {
            const float z = (end ? -1.0f : 1.0f) * dims.y / 2;
            glm::mat4 xf = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, z)) *
                           glm::rotate(glm::mat4(1.0f), end ? std::numbers::pi_v<float> : 0.0f, glm::vec3(0, 1, 0));
            place(net, xf * glm::scale(glm::mat4(1.0f), glm::vec3(s)), std::format("models2:{}#{}", mem, end));
        }
    }
    // Corner flags (f99 FUN_00406F70): 384 render units = 96 game units tall at (+-W/2, +-L/2).
    {
        PcmdModel flag = PcmdModel::parse(data.member(pak, "cflag.pc"));
        auto [lo, hi] = yRange(flag);
        const float fs = 96.0f / (hi - lo);
        for (int i = 0; i < 4; ++i) {
            glm::vec3 pos{(i & 1 ? -1.0f : 1.0f) * dims.x / 2, -lo * fs, (i & 2 ? -1.0f : 1.0f) * dims.y / 2};
            place(flag, glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), glm::vec3(fs)),
                  std::format("models2:cflag.pc#{}", i));
        }
    }
}

void MatchScene::Impl::loadPlayers() {
    // Animation: the simulation's clip table (AISEQS.TBI names, COMMON/ANIM.MO BIGF members) plus
    // the HIG_30.PC rig; SIM_PORT_NOTES.md section 5. The scene owns a scratch MatchData so callers
    // never thread it through; PlayerView::clip indexes match AnimInstance::clip / sim clips order.
    if (!sim::loadMatchAnimData(scratch, data.root())) throw std::runtime_error("sim::loadMatchAnimData failed");
    if (!scratch.anim.rigLoaded) throw std::runtime_error("player rig (INGAME/PLAYER/MODELS/HIG_30.PC) not loaded");
    for (std::size_t i = 0; i < scratch.anim.clips.size(); ++i)
        if (scratch.anim.clips[i].fmt && scratch.anim.clips[i].fmt->keyCount > 0) { standClip = int(i); break; }
    if (standClip < 0) throw std::runtime_error("ANIM.MO: no full-channel clip decoded");

    // LOD models + shadow mesh. buildSkinnedParts uses rig.node[] indices on the given model, so
    // every family needs its own rig (measured node counts differ per model: 28/27/16).
    for (std::size_t l = 0; l < 4; ++l) {
        const std::string rel = "INGAME/PLAYER/MODELS/" + std::string(kLodModels[l]) + ".PC"; // loose, RefPack-aware
        lods[l].model = PcmdModel::parse(data.file(rel));
        lods[l].rig = PlayerRig::fromModel(lods[l].model);
        for (auto& part : buildSkinnedParts(lods[l].model, lods[l].rig))
            lods[l].parts.emplace_back(part.material, r.createMesh(part.mesh, std::format("models:{}#{:02x}", kLodModels[l], part.material)));
    }
    // SHADOW.PC: flat skinned mesh (measured material type 0 'Z0', TorsoChn joint chain). Dark
    // tint baked into the vertex colours; alpha 150 chosen by eye (WC98 has no rparms +0x1C).
    shadowModel = PcmdModel::parse(data.file("INGAME/PLAYER/MODELS/SHADOW.PC"));
    shadowRig = PlayerRig::fromModel(shadowModel);
    auto parts = buildSkinnedParts(shadowModel, shadowRig);
    render::MeshData sm;
    std::size_t voff = 0;
    for (auto& part : parts) { // one mesh: same flat material on every polygon
        sm.vertices.insert(sm.vertices.end(), part.mesh.vertices.begin(), part.mesh.vertices.end());
        sm.skin.insert(sm.skin.end(), part.mesh.skin.begin(), part.mesh.skin.end());
        for (auto ix : part.mesh.indices) sm.indices.push_back(ix + std::uint32_t(voff));
        voff += part.mesh.vertices.size();
    }
    for (auto& v : sm.vertices) v.rgba = render::packRgba(0x20, 0x20, 0x20, 150); // Player_drawShadow flat colour
    shadowMesh = r.createMesh(sm, "models:SHADOW.PC#tint");

    // Team identities + FCDB appearance per player. The demo fills PlayerView::shirtNumber/skinTone
    // from the same sheets (makeTeamSheet order = roster order); hair/face strip indices are looked
    // up by (number, skin) — hypothesis: shirt numbers are unique inside a national squad.
    auto db = sim::loadFcdb(data.root());
    for (int t = 0; t < 2; ++t) {
        const int idx = setup.teamIndex[std::size_t(t)];
        if (idx < 0 || std::size_t(idx) >= db.teams.size()) throw std::invalid_argument("team index out of range");
        teamIds[std::size_t(t)] = db.teams[std::size_t(idx)].id;
        const auto sheet = sim::makeTeamSheet(db, std::size_t(idx));
        for (const auto& pl : sheet.squad) {
            Look lk = lookOf(pl);
            lookByKey[std::size_t(t)].try_emplace((int(lk.number) << 8) | lk.skin, lk); // first wins on a collision
        }
    }
    kits = std::make_unique<KitTextures>(data);
}

const std::map<std::string, TextureId>& MatchScene::Impl::texturesFor(const std::string& fileBase, int lodSuffix, const Look& look) {
    // imagesFor depends on the atlas base, the LOD texture set and the face/hair/beard strip indices.
    const std::string key = std::format("{}:{}:{}:{}:{}", fileBase, lodSuffix, look.face % kFaceCount,
                                        look.hairStyle % kHairCount, look.facialHair % kBeardCount);
    auto it = playerTex.find(key);
    if (it != playerTex.end()) return it->second;
    std::map<std::string, TextureId> out;
    for (auto& [tag, img] : kits->imagesFor(fileBase, lodSuffix, look))
        out[tag] = texture(img, render::Wrap::Repeat, "player:" + key + ":" + tag); // composite hifa/mdfa included
    return playerTex.emplace(key, std::move(out)).first->second;
}

// ------------------------------------------------------------------------------------------- frame

// Projected pixel height of a 112 game-unit object at the player (Proj_screenHeight 0x40B0EC, f99).
float MatchScene::Impl::lodMetric(const PlayerView& p, const render::Camera& cam, float focal) const {
    glm::vec4 v = cam.view() * glm::vec4(p.pos.x, 56, p.pos.z, 1);
    return v.z > 1 ? 112.0f * focal / v.z : 0;
}

void MatchScene::Impl::drawPlayer(const PlayerView& p, int slot, int lod, float metric, bool enhanced) {
    if (!p.visible) return;
    const formats::AnimClip* clip = clipFmt(p.clip >= 0 ? p.clip : standClip);
    if (!clip || clip->keyCount == 0) return;
    LocalPose pose = samplePose(scratch.anim.rig, *clip, p.clip >= 0 ? p.keyPos : 0.0f, {p.mirror, !enhanced, false});
    auto pal = jointPalette(pose);
    pal[4] = pal[4] * glm::scale(glm::mat4(1.0f), glm::vec3(1.03f)); // Player_setHeadScale 0x416CA8 (sibling)
    const auto jointBase = std::uint32_t(joints.size());
    joints.insert(joints.end(), pal.begin(), pal.end());

    const float yaw = p.facing * 2.0f * std::numbers::pi_v<float> / 1024.0f; // angle 0 faces +z (snapshot.hpp)
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(p.pos.x, 0, p.pos.z)) *
                      glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0)) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(kPlayerScale));

    // Kit atlas: outfield PK###H/A (home = team 0; the original's A/H home-away selection table is
    // untraced in WC98, kits.hpp keeps Home->H / Away->A as the documented hypothesis). Keepers
    // GK##_<n> with setIndex = teamId % 6 (measured: only GK00..GK05 x {30,70,90} exist; the
    // original's keeper-kit selection table is untraced -> hypothesis). Officials: PK999B.
    const bool gk = p.role == Role::Goalkeeper, refe = p.role == Role::Referee;
    const int team = p.team & 1;
    const int suffix = kLodSuffixes[std::size_t(lod)];
    std::string fileBase;
    Look look{};
    if (refe) {
        fileBase = kits->officialKitFile(0);
        look.skin = p.skinTone >= 0 ? p.skinTone & 3 : 0;
    } else if (gk) {
        fileBase = kits->gkKitFile(int(teamIds[std::size_t(team)]) % 6, suffix);
        look.skin = p.skinTone >= 0 ? p.skinTone & 3 : 1;
    } else {
        fileBase = kits->kitFile(teamIds[std::size_t(team)], team == 1);
        auto it = lookByKey[std::size_t(team)].find((int(p.shirtNumber) << 8) | (p.skinTone >= 0 ? p.skinTone & 3 : -1));
        if (it == lookByKey[std::size_t(team)].end()) it = lookByKey[std::size_t(team)].find((int(p.shirtNumber) << 8) | 0);
        if (it != lookByKey[std::size_t(team)].end()) {
            look = it->second;
            if (p.skinTone >= 0) look.skin = p.skinTone & 3;
        } else {
            look.skin = p.skinTone >= 0 ? p.skinTone & 3 : 1;
            look.number = p.shirtNumber;
        }
    }
    const auto& tex = texturesFor(fileBase, suffix, look);

    PlayerArt& L = lods[std::size_t(lod)];
    for (auto [matIdx, mesh] : L.parts) {
        const PcmdMaterial& pm = L.model.materials[matIdx];
        TextureId t;
        if (pm.type == 0) t = dark; // flat-colour material (Kit_buildPlayerClut dark ramp)
        else if (auto it = tex.find(trimTagS(pm.texture)); it != tex.end()) t = it->second;
        else {
            t = checker; // ponytail: missing tag placeholder; see loadTextures()
            auto [_, fresh] = warnedTags.insert(fileBase + ":" + trimTagS(pm.texture) + ":" + std::to_string(suffix));
            if (fresh) log::warn("kit atlas {}: no texture for model tag '{}' (LOD {})", fileBase, pm.texture, suffix);
        }
        Material m{t};
        m.alphaCutoff = kAlphaTest;
        draws.push_back({mesh, m, model, jointBase}); // lightBlend stays 0: WC98 has no .lm prelight
    }

    // Planar shadow (Player_drawShadow 0x415024): SHADOW.PC sheared toward the fixed sun through
    // shadowMatrix 0x4147F0 (k=2 clamp kept). Distance cull by the f99 level table's top bound:
    // shadows switch off past metric 130 (sHi[3]=130, sibling Player_drawShadow levels).
    bool& S = shadowOn[std::size_t(slot)];
    if (S && metric < 45) S = false; // hysteresis: re-enable below metric 60
    else if (!S && metric > 60) S = true;
    if (S) {
        Material sm{white, render::Blend::Alpha};
        sm.doubleSided = true;
        draws.push_back({shadowMesh, sm, shadowMatrix(p.pos, kSun, 0.4f) * model, jointBase});
    }
}

void MatchScene::Impl::drawBall(const BallView& b, const render::Camera& cam, int camYaw16) {
    if (!b.visible) return;
    auto [right, up] = cam.axes();
    // FUN_0040265C (f99): 36/40/44 render-unit sprite size by distance -> game units /4.
    const float dist = glm::length(b.pos - cam.eye);
    const float rr = dist > 2500 ? 9.0f : dist > 1750 ? 10.0f : 11.0f;
    const float half = rr - 1.0f, lift = rr; // f99 (rr-4)/4 and rr/4 in game units
    // Frame select (0x468370): octant of ball heading vs camera yaw, mirror bit 0x80 in octants
    // 1/5; exe table 0x581d34 kept in exe_tables.inc.
    const int oct = ((int(b.heading) + (camYaw16 >> 6) + 0x40) & 0x3FF) >> 7;
    const std::uint8_t fr = exe::kBallFrame[std::clamp(b.rollPhase, 0, 3)][oct];
    const glm::vec4 uv = (fr & 0x80) ? glm::vec4{1, 0, 0, 1} : glm::vec4{0, 0, 1, 1};
    // Ground shadow first: MISC8 'mshd' quad under the ball, projected toward the light
    // (f99 FUN_00402948 used bsha + rparms; WC98 constant tint).
    glm::vec3 c = b.pos;
    if (b.pos.y > 0) {
        const float k = -b.pos.y / (b.pos.y - kSun.pos.y);
        c = b.pos + (b.pos - kSun.pos) * k;
    }
    c.y = 0.5f;
    Material shm{misc8.at("mshd"), render::Blend::Alpha};
    shm.doubleSided = true;
    const float shs = kBallRadius * 1.2f;
    world.quad(shm, {c + glm::vec3(-shs, 0, shs), c + glm::vec3(-shs, 0, -shs), c + glm::vec3(shs, 0, -shs), c + glm::vec3(shs, 0, shs)});
    Material bm{ballFrames[fr & 0x0F], render::Blend::Alpha};
    bm.alphaCutoff = kAlphaTest;
    bm.doubleSided = true;
    world.billboard(bm, b.pos + glm::vec3(0, lift - half, 0), {half, half}, right, up, uv);
}

void MatchScene::Impl::buildSegments() {
    // Line markings derived from the FIFA-98 laws scaled to game units (48 units = 1 yd) through
    // the sim's Pitch constants (pitch.hpp): the boundary, halfway line, centre circle, penalty
    // areas (penAreaHalfWidth 0x840/2 at 0x65F370, penAreaLineZ = halfLength-864 at 0x65F374) and
    // goal areas (goalAreaHalfWidth 0x3c0/2 at 0x65F378, goalAreaLineZ = halfLength-288 at
    // 0x65F388). f99 FUN_0040803C drew the same layout (its 1056/864/480/288 literals equal these
    // fields); its centre-circle R = 384 (table 0x5A3DAC) is 8 yd — WC98's line table is untraced,
    // so the law-accurate 10 yd = 480 units is used as a documented hypothesis. Corner arcs and
    // penalty spots from the f99 point list are not ported (known gap).
    segs.clear();
    const float W = dims.x / 2, L = dims.y / 2;
    const sim::Pitch p = sim::Pitch::make(int(setup.pitchSize));
    auto pt = [](float x, float z) { return glm::vec3(x, 0.01f, z); }; // y = 0.01: above the grass, below feet
    auto line = [&](glm::vec2 a, glm::vec2 b) { segs.emplace_back(pt(a.x, a.y), pt(b.x, b.y)); };

    for (const float s : {1.0f, -1.0f}) { // both ends: penalty + goal area (open on the goal line)
        const float gl = s * L;
        const float pz = s * (L - float(p.penAreaDepth)), gz = s * (L - float(p.goalAreaDepth));
        const float pw = float(p.penAreaWidth) / 2, gw = float(p.goalAreaWidth) / 2;
        line({-pw, gl}, {-pw, pz}); line({-pw, pz}, {pw, pz}); line({pw, pz}, {pw, gl});
        const float qz = s * (L - float(p.penSpotDist)); // penalty spot 0x65F3AC = halfLength-576
        line({-8, qz}, {8, qz});                         // tiny tick marking the spot
        line({-gw, gl}, {-gw, gz}); line({-gw, gz}, {gw, gz}); line({gw, gz}, {gw, gl});
    }
    line({0, L - 24}, {0, L - float(p.goalHeight) * 3}); // goal-line notch under each goal (width hint)
    // Centre circle + halfway line.
    const float R = 480.0f; // hypothesis above (10 yd law; f99 sibling 384)
    for (int k = 0; k < 24; ++k) {
        const double a0 = 2 * std::numbers::pi_v<double> * k / 24, a1 = a0 + std::numbers::pi_v<double> / 12.0;
        segs.emplace_back(pt(float(R * std::sin(a0)), float(R * std::cos(a0))), pt(float(R * std::sin(a1)), float(R * std::cos(a1))));
    }
    line({-W, 0}, {W, 0});
    // Touchlines + goal lines (the full boundary).
    line({-W, -L}, {W, -L}); line({W, -L}, {W, L}); line({W, L}, {-W, L}); line({-W, L}, {-W, -L});
}

void MatchScene::Impl::drawLines(const render::Camera& cam, float focal) {
    // f99 FUN_004097CC: ~2 game-unit-wide strips with a 0.6 px minimum half-width on screen; the
    // alpha halves when a segment sits at that minimum (rparms colour halved there — WC98 constant).
    const std::uint32_t full = render::packRgba(255, 255, 255, 235);
    const std::uint32_t thin = render::packRgba(255, 255, 255, 117);
    Material m{white, render::Blend::Alpha};
    m.doubleSided = true;
    const glm::mat4 view = cam.view();
    for (auto [p0, p1] : segs) {
        glm::vec3 d = p1 - p0;
        if (glm::dot(d, d) == 0) continue;
        glm::vec3 n = glm::normalize(glm::vec3(-d.z, 0, d.x));
        float z0 = (view * glm::vec4(p0, 1)).z, z1 = (view * glm::vec4(p1, 1)).z;
        if (z0 <= 1 && z1 <= 1) continue;
        float w0 = 0.6f * std::max(z0, 1.0f) / focal, w1 = 0.6f * std::max(z1, 1.0f) / focal;
        bool minimal = w0 >= 1.25f && w1 >= 1.25f; // ~2-unit nominal width reached on screen
        w0 = std::max(w0, 1.25f), w1 = std::max(w1, 1.25f);
        world.quad(m, {p0 + n * w0, p0 - n * w0, p1 - n * w1, p1 + n * w1}, {0, 0, 1, 1}, minimal ? full : thin);
    }
}

const render::Frame& MatchScene::build(const MatchSnapshot& s, const render::Camera& cam, bool enhanced) {
    Impl& m = *impl_;
    // Live dimensions from the simulation (pitch.hpp width/length at 0x65F324/0x65F328 in game
    // units); fall back to pitchDimensions(pitchSize) when the snapshot carries none.
    glm::vec2 live = s.pitchUnits.x > 0 && s.pitchUnits.y > 0 ? s.pitchUnits : m.dims;
    if (live != m.lineDims) m.lineDims = live, m.dims = live, m.buildSegments();

    m.draws = m.statics;
    m.joints.clear();
    m.world.clear();

    const float focal = 240.0f / std::tan(cam.verticalFovDeg * std::numbers::pi_v<float> / 360.0f); // 640x480 reference (Proj_screenHeight)
    m.drawLines(cam, focal);

    // Players: LOD levels with hysteresis + the f99 detail budgets (Lod_init 0x4256D0 sibling:
    // <= 4 best / <= 8 third-best on screen). Chain order here is SHI(0) best -> LOW(3) worst, i.e.
    // the inverse index of f99's slo..shi array; thresholds are its kLo/kHi mirrored.
    struct Item { const PlayerView* p; int slot; float metric; };
    std::vector<Item> items;
    for (std::size_t i = 0; i < s.players.size(); ++i) items.push_back({&s.players[i], int(i), m.lodMetric(s.players[i], cam, focal)});
    items.push_back({&s.referee, 22, m.lodMetric(s.referee, cam, focal)}); // sim kReferee slot (world.hpp l.33)
    std::ranges::stable_sort(items, [](const Item& a, const Item& b) { return a.metric > b.metric; });
    static constexpr float kIn[4] = {160, 85, 45, -1.f};   // metric above -> step one level better (SHI..LOW)
    static constexpr int kBudget[4] = {4, 23, 8, 23};      // SHI capped at 4, MED at 8 (f99 shi/med caps)
    int used[4] = {};
    for (const Item& it : items) {
        int& L = m.lodState[std::size_t(it.slot)];
        while (L > 0 && it.metric > kIn[L]) --L;   // close: improve
        while (L < 3 && it.metric <= kIn[L + 1] - 25.0f) ++L; // far: degrade with a 25-unit hysteresis band
        while (L > 0 && used[L] >= kBudget[L]) ++L;
        ++used[L];
        int lod = L;
        if (it.p->lod >= 0) lod = std::clamp(int(it.p->lod), 0, 3); // PlayerView forces the chain index
        m.drawPlayer(*it.p, it.slot, lod, it.metric, enhanced);
    }

    const int camYaw16 = sim::atan2i(cam.target.x - cam.eye.x, cam.target.z - cam.eye.z) * 64; // 1024 -> 65536/turn
    m.drawBall(s.ball, cam, camYaw16);

    // HUD overlay (radar, scoreboard strip, shirt numbers, controller markers): hud.cpp layout.
    m.drawHud(s, cam, live);
    m.frame = render::Frame{cam, m.draws, s.hud.scoreboard || s.hud.radar ? &m.ui : nullptr,
                            s.time == TimeOfDay::Night ? glm::vec4(0.05f, 0.07f, 0.12f, 1.f) : glm::vec4(0.36f, 0.55f, 0.75f, 1.f),
                            m.joints, &m.world, render::Fog{}, nullptr}; // fog off: no WC98 rparms table (ponytail: add when the sky/fog path is traced)
    return m.frame;
}

// ------------------------------------------------------------------------------------------- public

MatchScene::MatchScene(render::Renderer& r, const GameData& data, const MatchSetup& setup) : impl_(std::make_unique<Impl>(r, data, setup)), setup_(setup) {
    Impl& m = *impl_;
    if (setup.teamIndex[0] == setup.teamIndex[1]) throw std::invalid_argument("the two sides need distinct FCDB team indices");
    m.loadTextures();
    m.loadPitch();
    m.loadGoals();
    m.loadPlayers(); // rig/clips + kits; sets up the player art and appearance tables
    m.loadHud(); // graceful: warns and sets hudReady=false when the HUD art is absent
    if (m.setup.stadiumArt) m.loadStadium(); // last: it may flip setup.stadiumArt on load failure
}

MatchScene::~MatchScene() = default;

const formats::AnimClip& MatchScene::clip(int index) const {
    const formats::AnimClip* c = impl_->clipFmt(index);
    if (!c) throw std::out_of_range("no ANIM.MO clip at index " + std::to_string(index));
    return *c;
}

std::array<std::uint16_t, 2> MatchScene::teamIds() const { return impl_->teamIds; }
void MatchScene::setHudGeometry(const HudGeometry& g) { impl_->hudGeo = g; }



// ------------------------------------------------------------------------------------------- hud

TextureId MatchScene::Impl::hudTex(const std::string& key) const {
    if (key.empty()) return white; // solid strip rects/panel fills use the white texel + vertex rgba
    if (key == "font") return fontTex;
    auto it = hudArt.find(key);
    return it == hudArt.end() ? 0 : it->second;
}

void MatchScene::Impl::loadHud() {
    // Font: the strip draw routine picks font-table slot 0 (FUN_00438670@0x4386de MOV EAX,0 ->
    // FUN_0046a2a8 copies record[0] of DAT_0060b704; name table @0x57d2c0 = eras10 first) =>
    // FE/ERAS10.FFN (pass10/hud). Shirt-label face untraced: IGFONTS Eras14bi kept as fallback.
    try {
        hudFont = formats::FfnFont::parse(data.file("FE/ERAS10.FFN"));
    } catch (const std::exception& e) {
        log::warn("hud: FE/ERAS10.FFN not loaded ({}) -> IGFONTS eras14bi.ffn", e.what());
        hudFont = formats::FfnFont::parse(data.member("FE/PACK/IGFONTS.BIG", "eras14bi.ffn"));
    }
    fontTex = texture(toImage(hudFont.atlas), render::Wrap::Clamp, "hud:font");
    // Radar panel + markers. Measured in INGAME/MISC/MISC8.FSH (analysis/pass7/hud_probe.txt):
    // rpit/rpi2 180x120 panels, rsp0..rsp4 8x8 selectors, sb00..sb04 32x32 controller diamonds.
    for (std::string_view t : {"rpit", "rpi2", "rsp0", "rsp1", "rsp2", "rsp3", "rsp4",
                               "rpl0", "rpl1", "ball", "sb00"}) {
        const std::string tag(t);
        if (hudArt.count(tag)) continue;
        try {
            FshFile f = parseFsh("INGAME/MISC/MISC8.FSH");
            for (const auto& e : f.entries)
                if (e.bitmap && trimTag(e.tag) == t) { hudArt[tag] = texture(toImage(*e.bitmap), render::Wrap::Clamp, "hud:" + tag); break; }
        } catch (const std::exception&) {
            log::warn("hud: MISC8.FSH tag {} not loaded", tag);
        }
    }
    // Team abbreviations for the strip: FCDB STR0 rows pair 1:1 with FCDB.DBI team order.
    try {
        const std::vector<std::uint8_t> eng = data.file("COMMON/FCDB_ENG.DBI");
        const formats::FcdbNames names = formats::FcdbNames::parse(eng);
        for (int i = 0; i < 2 && std::size_t(setup.teamIndex[i]) < names.teams.size(); ++i)
            hudTeamNames[i] = names.teams[std::size_t(setup.teamIndex[i])].abbreviation;
    } catch (const std::exception& e) {
        log::warn("hud: team abbreviations not loaded ({})", e.what());
    }
    hudReady = fontTex && !hudArt.empty();
}

void MatchScene::Impl::drawHud(const MatchSnapshot& s, const render::Camera& cam, const glm::vec2& live) {
    ui.clear();
    if (!hudReady) return;
    HudView hud = s.hud;
    for (int t = 0; t < 2; ++t)
        if (hud.teamNames[t].empty()) hud.teamNames[t] = hudTeamNames[t];

    const auto emit = [&](const std::vector<HudQuad>& quads) {
        for (const HudQuad& q : quads) ui.draw(hudTex(q.texture), q.dst, q.uv, q.rgba);
    };
    if (hud.radar) emit(layoutRadar(s, live, hudGeo));
    if (hud.scoreboard) emit(layoutScoreboard(hud, hudFont, hudGeo));

    // Shirt-number labels above the user's side and the ball possessor: world -> screen projection.
    const glm::mat4 vp = cam.projection(render::SpriteBatch::kWidth / render::SpriteBatch::kHeight) * cam.view();
    for (std::size_t i = 0; i < s.players.size(); ++i) {
        const PlayerView& p = s.players[i];
        if (!p.visible || !p.shirtNumber) continue;
        if (p.team != 0 && int(i) != s.possessor && hud.controlled[0] != int(i)) continue;
        glm::vec4 c = vp * glm::vec4(p.pos.x, p.pos.y + kShirtHeadUnits, p.pos.z, 1.f);
        if (c.w <= 1e-3f) continue;
        const float x = (c.x / c.w * 0.5f + 0.5f) * render::SpriteBatch::kWidth;
        const float y = (1.f - (c.y / c.w * 0.5f + 0.5f)) * render::SpriteBatch::kHeight;
        emit(layoutTextCentered(hudFont, std::to_string(p.shirtNumber), x, y - hudGeo.shirtHeadPx,
                                hudGeo.shirtSize, hudGeo.textColor));
    }
}
} // namespace wc98::presentation
