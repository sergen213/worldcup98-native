// Derived from fifa99-native (same author), ported for World Cup 98
#include "game_audio.hpp"

#include "wc98/file.hpp"
#include "wc98/formats/bnk.hpp"
#include "wc98/formats/crd.hpp"
#include "wc98/formats/phrs.hpp"
#include "wc98/formats/refpack.hpp"
#include "wc98/formats/schl.hpp"
#include "wc98/audio/smix_voice.hpp"
#include "wc98/audio/scene_levels.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <memory>

namespace wc98::app {
namespace {

// SFX slot mapping (evidence analysis/pass8/audio1_strings.txt: AUDIO\SFXS\%s + game00.bnk
// @0x566b5a, loader FUN_0048eff0/owner FUN_0048f1c4; the exe plays slots through table-driven
// SNDplay sites (FUN_0051d480 -> byte (&DAT_0058f3b2)[scene*4]), so the slot numbers below are
// acoustic-verified from decoded WAVs, not constants in the exe: GAME00 slot 81 = short bright
// chirp (whistle), slot 23 = short low-pitched thud (ball strike); CROWD.BNK slot 2/3 pair is a
// continuous ~3 s crowd murmur suited to looping. Hypothesis on exact event->slot ids.
constexpr std::uint32_t kWhistleSlot = 81, kKickSlot = 23; // AUDIO/SFXS/GAME00.BNK
constexpr std::uint32_t kWhistleTrillSlot = 85;            // 1.95 s bright sustained (measured: zcr 9.8 kHz)
constexpr std::uint32_t kGoalRoarSlot = 45;                // 4.12 s loud-noise burst (measured profile)
constexpr std::uint32_t kCrowdSlot = 2;                    // AUDIO/CRWD/CROWD.BNK

// Decode one BNK slot best-effort; silent Pcm on any miss.
formats::Pcm decodeSlot(const std::filesystem::path& file, std::uint32_t slot) {
    try {
        const auto data = readFile(file);
        const formats::Bnk bnk = formats::Bnk::parse(data);
        for (const auto& s : bnk.sounds)
            if (s.slot == slot) return formats::Bnk::decode(data, s);
    } catch (const std::exception&) { // missing tree/file/bad bank: silent slot
    }
    return {};
}

// SMIX native mixer lane (pass-18smix, additive): the exe's DirectSound 2D voice path installs
// per-voice L/R volume ints through FUN_0054ccfe and mixes with the installer floats; scene Q7
// percents (FUN_004906a4) feed those ints. Mirror it behind the existing scene-level gains:
// each lane's play gain = base * installer float at (gainQ7(ch), gainQ7(ch)). At 100% levels
// the installer is exactly unity (int 0x7F -> 1.0f, gated in test_smix_grammar), so streams
// keep bit-identical v0.3.0 gains; lowering a scene percent drives the voice through SMIX.
float smixLaneGain(int channel) {
    audio::SmixVoice v;                    // record model of the DAT_00661068 slot band
    const int q7 = audio::sceneLevels().gainQ7(channel);
    audio::Smix_startVoice(v, 0x7F, 0x7F, static_cast<std::uint8_t>(q7)); // FUN_005483bc rows:
    audio::Smix_setPan(v, q7, q7);         // initial pair + gate byte +0x6B, ramp armed (FUN_00548a08)
    v.setVolume(q7, q7);                   // FUN_0054ccfe installer on the scene Q7 ints
    return v.gainRef().volL;               // mono voice: L-slot float gain
}

} // namespace

bool startMenuMusic(audio::Mixer& mixer, const std::filesystem::path& tree, std::string* trackOut) {
    // exe glob is FE*.ASF; port matches FE_*.ASF in the extracted tree (BE_PAU*.ASF play on the
    // mode-2 pause-screen list (FUN_0048ed9c FUN_00446624()==2 branch), not wired yet).
    std::vector<std::filesystem::path> tracks;
    const auto dir = tree / "AUDIO" / "MUSC";
    if (!std::filesystem::is_directory(dir)) return false;
    for (const auto& de : std::filesystem::directory_iterator(dir)) {
        const auto name = de.path().filename().string();
        if (name.starts_with("FE_") && name.ends_with(".ASF")) tracks.push_back(de.path());
    }
    std::ranges::sort(tracks);
    for (const auto& p : tracks) {
        try {
            auto file = std::make_shared<std::vector<std::uint8_t>>(readFile(p));
            const auto streams = formats::scanSchl(*file);
            if (streams.empty()) continue;
            // Scene-level layer (FUN_004906a4): the exe applies the percent-table Q7 gain of the
            // music channel on top of the stream volume; default init 100 => unity. The SMIX voice
            // lane (pass-18smix) supplies that gain as the FUN_0054ccfe installer float, exactly
            // unity at 100%.
            mixer.stream(file, streams.front(), smixLaneGain(audio::kSceneMusic), /*loop=*/true);
            if (trackOut) *trackOut = p.filename().string();
            return true;
        } catch (const std::exception&) { // try the next track
        }
    }
    return false;
}

MatchAudio* MatchAudio::live_ = nullptr;

MatchAudio::MatchAudio(const std::filesystem::path& tree) : tree_(tree) {
    whistle_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kWhistleSlot);
    kick_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kKickSlot);
    goalRoar_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kGoalRoarSlot);
    whistleTrill_ = decodeSlot(tree / "AUDIO" / "SFXS" / "GAME00.BNK", kWhistleTrillSlot);
    // Stadium bed layers (pass-51/54): the lane builder FUN_0048e78c reads the card file
    // (AUDIO/CRWD/CROWD.CRD, FUN_0047aeb0 lookup). Its bank-slot list (0..3) opens ALL four
    // CROWD.BNK samples together (2 short swells + 2 murmur loops), each bound to a gain
    // curve by the u32 tableRefs @0x128: pump handler 0x4e502c fixes slot pointers as
    // card + slotAddr(0x128+i*4) + ref (FUN_004e4a80) and reads gain byte curve[level]
    // (/127 scale). Measured pointers: slots 0..3 -> 0x168/0x568/0x968/0xd68 = tables 0/2/4/6
    // (slots 8..11 -> tables 1/3/5/7, unused here). Slots 2/3 carry the zero-floor curve
    // (entries 0..4 == 0 -> options row 0 silences the bed); slots 0/1 carry the 21-floor
    // curve (audible even at row 0), exactly as the original pump slots bind.
    std::vector<std::uint32_t> slots{kCrowdSlot};
    try {
        const auto card = formats::Crd::parse(readFile(tree / "AUDIO" / "CRWD" / "CROWD.CRD"));
        std::vector<std::uint32_t> fromCard;
        for (const auto s : card.bankSlots)
            if (s != 0xFF) fromCard.push_back(s);
        crowdCardSlots_ = static_cast<std::uint32_t>(fromCard.size());
        if (!fromCard.empty()) slots = fromCard;
        if (card.tables.size() >= 5 && card.tables[4][127] != 0) {
            crowdCurve_ = card.tables[4];
            crowdCurveValid_ = true;
        }
        // Reaction machine (pass-55): weights from header byte 7 (measured CROWD 70 ->
        // be 358/bc 154), idle ramp frames from record 0 byte 12 (exe rate * 0x21 >> 16).
        crowdRecords_ = card.records;
        {
            const int rate = crowdRecords_.empty()
                ? 132
                : std::max(1, int((std::int64_t(crowdRecords_[0].rate()) * 0x21) >> 16));
            crowdEnvelope_.init(card.byte7, rate);
        }
        for (std::size_t i = 0; i < slots.size(); ++i) {
            CrowdLayer layer;
            layer.pcm = decodeSlot(tree / "AUDIO" / "CRWD" / "CROWD.BNK", slots[i]);
            if (!layer.pcm.frames()) continue;
            const std::uint32_t ref = i < card.tableRefs.size() ? card.tableRefs[i] : 0;
            if (ref) { // card + slotAddr + ref -> table entry; (addr - 0x168) / 512 == table idx
                const std::uint64_t addr = 0x128 + i * 4 + ref;
                if (addr >= 0x168) {
                    const std::size_t idx = std::size_t((addr - 0x168) / 512);
                    if (idx < card.tables.size()) layer.curve = card.tables[idx];
                }
            }
            crowdLayers_.push_back(std::move(layer));
        }
    } catch (const std::exception&) { // no card: keep the measured slot as single fallback
    }
    if (crowdLayers_.empty()) {
        CrowdLayer layer;
        layer.pcm = decodeSlot(tree / "AUDIO" / "CRWD" / "CROWD.BNK", kCrowdSlot);
        if (layer.pcm.frames()) crowdLayers_.push_back(std::move(layer));
    }
    traffic_ = decodeSlot(tree / "AUDIO" / "CRWD" / "TRAFF.BNK", 0); // TRAFF.CRD slot 0 (measured)
    try {
        const auto traf = formats::Crd::parse(readFile(tree / "AUDIO" / "CRWD" / "TRAFF.CRD"));
        if (traf.tableRefs[0] && !traf.tables.empty()) { // slot 0 -> table 0 (flat 117)
            trafficCurve_ = traf.tables[0];
            trafficCurveValid_ = true;
        }
        // Traffic card (pass-55): header byte 7 = 4 -> weights be 20/bc 480; the card ships
        // no reaction records (all 0x00 -> SNDcrowdpulse refuses it, measured dump).
        trafficRecords_ = traf.records;
        trafficEnvelope_.init(traf.byte7, 132); // no records: idle rate default 132 frames [H]
    } catch (const std::exception&) { // no card: gain falls back to the bed curve
    }
}

MatchAudio::~MatchAudio() {
    if (live_ == this) live_ = nullptr; // boot() registered the percent notifier (FUN_0049067c
                                        // dispatch lane): never leave a dangling back-pointer
}

void MatchAudio::crowdNotifier(int channel) {
    (void)channel;
    if (live_) live_->relevelCrowd(); // percent write -> lane re-level (FUN_0049067c dispatch)
}

// --- CrowdEnvelope (pass-55) -------------------------------------------------------------
// Mirror of the cmn\scrowd.c reaction machine: idle pump FUN_004e510c (DAT_005894ab state
// walk), pulse FUN_004e4cac, blend consumer FUN_005444. The exe shares ONE machine between
// the CROWD and TRAFF cards (single 16.16 level, slot curve pointers per card); the port
// runs the bed and hum envelopes independently, each from its own card header byte 7 [H].
void CrowdEnvelope::init(std::uint32_t cardByte7, int rateFrames) {
    // FUN_004e4ba0 install: weights from header byte 7 (be = byte7<<9/100, bc = 0x200 - be).
    be_ = int((cardByte7 << 9) / 100);
    bc_ = 0x200 - be_;
    c0_ = 0;
    c4_ = 0;
    c8_ = 0;
    ab_ = 0;
    // Idle ramp frames, exe `*(int*)(recordArea + 0xc + cardIdx*0x10) * 0x21 >> 0x10`
    // (record byte 12 u32, CROWD record 0 measured 0x40000 -> 132 frames at 60 fps).
    rateFrames_ = std::max(1, rateFrames);
}

int CrowdEnvelope::targetLevel() {
    // exe draw = (int)(rng64 >> 32) + (int)rng64 clamped [0, 0x1ff] (PRNG FUN_0052f3e0,
    // 128-bit state approximated like the chant variant draw [H]).
    return int(rng_() % 0x200u);
}

void CrowdEnvelope::tick() {
    switch (ab_) {
    case 0: // ramp toward target (up); on arrival draw a new target
        c0_ += c8_;
        if (c4_ <= c0_) {
            c0_ = c4_;
            const std::int64_t t = std::int64_t(targetLevel()) << 16;
            // exe: distance = target below current ? (c0 - target*0x10000) : 0x10000 (rise by 1.0)
            const std::int64_t dist = t < c0_ ? std::int64_t(c0_ - t) : 0x10000;
            c4_ = std::int32_t(t);
            std::int64_t step = dist / rateFrames_; // exe frames = rate * 0x21 >> 16
            if (step < 1) step = 1;
            c8_ = std::int32_t(step);
            ab_ = 1;
        }
        break;
    case 1: // ramp down toward target; on arrival draw target + frame count, switch to ramp up
        c0_ -= c8_;
        if (c0_ <= c4_) {
            c0_ = c4_;
            c4_ = std::int32_t(std::int64_t(targetLevel()) << 16);
            const int frames = rampFrames(); // exe reads a runtime frame slot (+0x18) [H]
            c8_ = std::int32_t((std::int64_t(c4_) - c0_) / frames); // signed: ramp direction
            ab_ = 2;
        }
        break;
    case 2: // ramp (c8 may be negative -> walks down); arrival draws + switches to 3
        c0_ += c8_;
        if (c4_ <= c0_) {
            c0_ = c4_;
            c4_ = std::int32_t(std::int64_t(targetLevel()) << 16);
            const int frames = rampFrames(); // exe frame slot (+0xc) [H]
            // exe case 2 arrival: c8 = (c0 - target) / frames -> positive walk down in case 3
            c8_ = std::int32_t((std::int64_t(c0_) - std::int64_t(c4_)) / frames);
            ab_ = 3;
        }
        break;
    default: // 3: ramp down toward target; arrival draws + switches back to 2
        c0_ -= c8_;
        if (c0_ <= c4_) {
            c0_ = c4_;
            c4_ = std::int32_t(std::int64_t(targetLevel()) << 16);
            const int frames = rampFrames();
            c8_ = std::int32_t((std::int64_t(c4_) - c0_) / frames); // exe: signed, up in case 2
            ab_ = 2;
        }
        break;
    }
}

bool CrowdEnvelope::pulse(const formats::CrdRecord& rec) {
    // SNDcrowdpulse FUN_004e4cac: ceiling = record bytes 4..7 MSB half (values[1]),
    // delta = bytes 2..5 MSB half (values[0]); only when the level is below the ceiling
    // (exe returns 0xfffffff8 in the refused case).
    const std::int64_t ceiling = std::int64_t(rec.values[1]) << 16;
    if (c0_ >= ceiling) return false;
    std::int64_t target = std::int64_t(c0_) + (std::int64_t(rec.values[0]) << 16);
    if (ceiling < target) target = ceiling;
    c4_ = std::int32_t(target);
    c8_ = std::int32_t(rec.step()); // u32 16.16 ramp step per frame
    ab_ = 0;                        // ramp up into the pulse target
    return true;
}

int CrowdEnvelope::blendIndex() const {
    // FUN_005444 control dword high half: cc = (be*be + bc*(c0>>16)) >> 9, used directly as
    // the curve table index (CROWD algebraic band [250, 404] over level [0, 0x1ff]).
    return std::clamp((be_ * be_ + bc_ * (c0_ >> 16)) >> 9, 0, 0x1ff);
}

void MatchAudio::boot(std::uint16_t homeTeamId, std::uint16_t awayTeamId,
                      const std::vector<std::uint16_t>& homeSquadIds,
                      const std::vector<std::uint16_t>& awaySquadIds) {
    live_ = this;
    // DAT_004904b0 percent-notifier install (FUN_0049067c dispatch table): idx2 =
    // SNDstadiumreactions re-levels the running stadium loops on an options-row write.
    audio::sceneLevels().setNotifier(audio::kSceneCrowd, &MatchAudio::crowdNotifier);
    startCrowd();
    startTraffic();
    startAnthem(homeTeamId);
    chantHomeKey_ = int(homeTeamId); // classification seam: DAT_006324b8/bc team keys (C0%04d digits)
    chantAwayKey_ = int(awayTeamId);
    catalogChants();
    classifyChants();
    catalogAnnouncers(homeTeamId, awayTeamId);
    catalogPlayers(homeSquadIds);
    catalogPlayers(awaySquadIds);
    catalogCrwd(); // scene-10 init lane (FUN_0048e6d0)
    catalogSfxs(); // SFXS install-lane registry (FUN_0048f1c4, pass-72)
}

void MatchAudio::startTraffic() {
    // Stadium lane pair: the lane builder's second container load is the traffic hum
    // (AUDIO/CRWD/TRAFF.BNK slot 0, 79.84 s, gated by the same percent idx2), looped by the
    // FUN_004e3128/FUN_004e4a80 start points. Measured bed RMS 1416 vs crowd bed 9111 -> mix
    // it under the crowd bed [H gain balance]. Its TRAFF.CRD curve gain (pass-54 measured:
    // slot 0 tableRef -> flat table 117/127) scales the voice like the pump slot does.
    if (mixer_ && traffic_.frames() && !trafficId_)
        trafficId_ = mixer_->play(traffic_,
                                  0.12f * smixLaneGain(audio::kSceneCrowd) * trafficGain(),
                                  /*loop=*/true);
}

void MatchAudio::startAnthem(std::uint16_t homeTeamId) {
    // Presentation-lane nation sample: AUDIO/ANTH/ANTH%04d.BNK slot 0 = 23.8 s chant sample of
    // the team's nation (measured: ANTH0026 slot 0 decodes, 765 KB mono 22050). Played once at
    // match boot (presentation lane gain [H]).
    const auto file = tree_ / "AUDIO" / "ANTH" / std::format("ANTH{:04d}.BNK", homeTeamId);
    if (mixer_ && !anthemId_) {
        const auto anthem = decodeSlot(file, 0);
        if (anthem.frames())
            anthemId_ = mixer_->play(anthem, 0.8f * smixLaneGain(audio::kSceneCrowd), /*loop=*/false);
    }
}

void MatchAudio::catalogChants() {
    // Chant pool (pass-51): FUN_0048dfc4 allocates the 113-slot chant lane (DAT_006324c0, loop
    // `iVar1 < 0x71`) + a 3-slot side pool (DAT_006324d0). The bank names come from the exe's
    // own table at VA 0x57eb5c (`PTR_s_C0001800_PBK`): 74 team-keyed entries `C0%04d` digits =
    // match-record nationality id at `DAT_00641014 + 0x32` (boot classification FUN_0048dcd8
    // builds the same name with format string @0x566a88) + %02d variant (e.g. C0001800..C0001801,
    // C0002600), consumed by the
    // per-chant loader FUN_0048d53c (`AUDIO\CHNT\%s` @0x566a78, `table[req+4]` index). The
    // remaining 39 CD files are the CGENER%02d generic-chant lane (name string `GENER` in the
    // same block, absent from the team table). Boot loads all 113, so the port catalogs every
    // AUDIO/CHNT/*.PBK file; playback is driven by the frame pump (pumpChants), whose lane
    // model + request cadence is decoded in pass 53 (analysis/pass53/ch5/ch6/ch7.txt): the
    // lane pair (home 0 / away 1 / shared+generic 2), the 100-frame request cadence, and
    // the percent idx2 clip level. The old "trigger mapping undecoded, catalog only" state
    // is closed: chants run continuously like the exe pump, not on invented event triggers.
    const auto dir = tree_ / "AUDIO" / "CHNT";
    if (!std::filesystem::is_directory(dir)) return;
    std::vector<std::filesystem::path> files;
    for (const auto& de : std::filesystem::directory_iterator(dir))
        if (de.path().filename().string().ends_with(".PBK")) files.push_back(de.path());
    std::ranges::sort(files);
    for (const auto& path : files) {
        try {
            const auto bytes = readFile(path);
            chantPhrases_ += formats::PhraseBank::parse(std::span<const std::uint8_t>{bytes})
                                 .phraseCount();
            ++chantBanks_;
            ChantEntry entry;
            entry.path = path;
            const std::string name = path.filename().string(); // C0 + %04d team id + %02d variant
            if (name.size() >= 6 && name.compare(0, 2, "C0") == 0 &&
                std::all_of(name.begin() + 2, name.begin() + 6,
                            [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
                entry.teamKey = std::stoi(name.substr(2, 4)); // CGENER entries stay key 0
            chantEntries_.push_back(std::move(entry));
        } catch (const std::exception&) { // member present but unparsable: exe skips silently
        }
    }
}

// Boot classification (pass-53 RE, analysis/pass53/ch5.txt/ch6.txt): FUN_0048dcd8 walks the
// 113-entry pool and stamps every node's lane field (+8) from the team-bank availability flags
// DAT_006324b8/bc (home/away team present in the runtime chant-team vector DAT_005883d8,
// lookup FUN_00446a94). Flags: neither team has banks -> lane -1 (pool silent, pump never
// fires); a same-team match (local_20) -> lane 2; otherwise the entry's team lands lane 0
// (home) or 1 (away), and the pump body FUN_0048da50 fans out over the lanes topping each
// lane while laneCount > 0 and lane active < desired (DAT_006324e0, cap 1 per lane, set on
// first assignment) with the 3-slot pool cap (DAT_006324d0, e8 decrement on assign).
void MatchAudio::classifyChants() {
    bool homeHas = false, awayHas = false;
    for (const auto& e : chantEntries_) {
        if (e.teamKey == chantHomeKey_) homeHas = true;
        if (e.teamKey == chantAwayKey_) awayHas = true;
    }
    for (auto& e : chantEntries_) {
        if (!homeHas && !awayHas) {
            e.lane = -1; // DAT_006324b8 == 0 && DAT_006324bc == 0: whole pool dropped
            continue;
        }
        if (chantHomeKey_ == chantAwayKey_) e.lane = 2; // same-team branch (local_20)
        else if (e.teamKey == chantHomeKey_) e.lane = 0;
        else if (e.teamKey == chantAwayKey_) e.lane = 1;
        else e.lane = e.teamKey == 0 ? 2 : -1; // CGENER generic lane; other teams dropped
        if (e.lane >= 0) {
            ++chantLaneCount_[std::size_t(e.lane)];
            if (chantLaneDesired_[std::size_t(e.lane)] == 0)
                chantLaneDesired_[std::size_t(e.lane)] = 1; // DAT_006324e0[lane] += 1 cap
        }
    }
}

// Chant frame pump (pass-53): exe scene handler FUN_0048da50. The request cadence counter
// (DAT_00636a14) gates loads at every 100 frames (`< 0x64` guard, increment sites
// 0x48db33/0x48dbeb/0x48dc27 in the pump body); between cadence hits the drain branch walks
// qlength/2 steps (FUN_004c4af0), each step a LCG draw (FUN_004ce2f3) whose parity picks
// qbottom (FUN_004c4800) vs qpush (FUN_004c4780) on the fetched entry — queue rotation
// between head and tail, not a play-vs-stop choice (pass-56 decode: the earlier residual
// "play-vs-stop random branch" claim is retracted, the branch reorders the pool). Then the
// top-up fires only while the match scene is live (FUN_004adb30) and the queues are empty.
// Port: voices retire on the mixer play-status probe (SNDplay status analog), the pool
// order rotates on the cadence, then lanes refill under the caps.
void MatchAudio::pumpChants() {
    ++chantPumpTick_;
    if (mixer_) {
        for (auto& v : chantVoices_) {
            if (v.id != 0 && !mixer_->playing(v.id)) {
                if (v.lane >= 0 && chantLaneActive_[std::size_t(v.lane)] > 0)
                    --chantLaneActive_[std::size_t(v.lane)];
                if (chantActiveTotal_ > 0) --chantActiveTotal_;
                v = {};
            }
        }
    }
    if ((chantPumpTick_ % 100) != 0) return; // 100-frame cadence gate
    // Drain rotation (FUN_0048da50 drain branch, pass-56): length/2 steps, each step an
    // LCG draw; even parity pushes the fetched entry to the queue bottom, odd pushes it
    // to the top. Mirrored as pool reordering: even -> front, odd -> back.
    for (std::size_t i = 0, n = chantEntries_.size() / 2; i < n; ++i) {
        if ((chantRng_.draw() & 1u) == 0) {
            std::rotate(chantEntries_.begin(), chantEntries_.begin() + 1,
                        chantEntries_.end());
        } else {
            std::rotate(chantEntries_.begin(), chantEntries_.end() - 1,
                        chantEntries_.end());
        }
    }
    if (!mixer_ || chantEntries_.empty() || mixer_ == nullptr) return; // device-less: exe needs one
    if (chantActiveTotal_ >= chantVoices_.size()) return;              // 3-slot pool full
    for (int lane = 0; lane < 3; ++lane) {
        if (chantLaneCount_[std::size_t(lane)] == 0) continue;
        if (chantLaneActive_[std::size_t(lane)] >= chantLaneDesired_[std::size_t(lane)]) continue;
        if (chantActiveTotal_ >= chantVoices_.size()) break;
        playChantLane(lane);
    }
}

// Lane request (FUN_0048d53c loader + FUN_0048d678 start): pick a random pool entry of the
// lane (exe LCG draws FUN_004ce2f3 drive the queue index picks), decode its chant clip,
// and start it at the percent-idx2 scaled level: the start site FUN_0048d678 feeds
// SNDautovol the target FUN_004906d0 computes as (playVol * percent[idx2] * 0x7f) / 10000,
// so the options stadium-reaction row gates chant audibility; row 0 => target 0 => silent.
bool MatchAudio::playChantLane(int lane) {
    std::vector<ChantEntry const*> candidates;
    for (auto& e : chantEntries_)
        if (e.lane == lane) candidates.push_back(&e);
    if (candidates.empty()) return false;
    auto& entry = *candidates[chantRng_.draw() % candidates.size()];
    const auto clip = decodeChantClip(entry.path);
    chantPlays_ += 1; // request counted even on decode-miss: exe request counter semantics
    if (clip.samples.empty()) return false;
    chantClip_ = clip;
    const int pct = audio::sceneLevels().level(audio::kSceneCrowd);
    if (pct < 1) return false; // percent idx2 -> SNDautovol target 0: silent request
    const float gain = std::min(1.0f, float(pct) / 100.0f) * smixLaneGain(audio::kSceneCrowd);
    for (auto& v : chantVoices_) {
        if (v.lane == -1) {
            v.id = mixer_->play(clip, gain, /*loop=*/false);
            v.lane = lane;
            ++chantLaneActive_[std::size_t(lane)];
            ++chantActiveTotal_;
            return true;
        }
    }
    return false;
}

// Clip decode (pass-53): CHNT banks are phrase-grouped segment banks (classLetter 'a', the
// pass-52 histogram); a chant clip is one top-level phrase = all its segments in TOC order,
// each segment decoded with decodePlayTrack (the commentary lane seam, SPEECH_PHRASES.md §1).
// The phrase is drawn randomly (exe variant randomness), segments concatenate into one clip.
formats::Pcm MatchAudio::decodeChantClip(const std::filesystem::path& path) {
    formats::Pcm out;
    try {
        const auto bytes = readFile(path);
        const auto bank = formats::PhraseBank::parse(std::span<const std::uint8_t>{bytes});
        std::vector<std::uint32_t> playable;
        for (std::uint32_t p = 0; p < bank.phraseCount(); ++p)
            if (!bank.emptyPhrase(p)) playable.push_back(p);
        if (playable.empty()) return out;
        const std::uint32_t phrase = playable[chantRng_.draw() % playable.size()];
        const auto member = formats::refpack::decompressIfNeeded(bank.memberBytes(phrase));
        const std::span<const std::uint8_t> m{member};
        if (!formats::BigArchive::isBig(m)) {
            // Flat member (measured: retail CHNT banks are one phrase = one member holding a
            // 20-byte phrase record + a PT play track, e.g. C0002600.PBK = 23100 B, one
            // phrase, one record classLetter 'a', decode@20 -> 43008 frames). Decode it at
            // the record boundary like the commentary lane.
            ++chantSegments_;
            return formats::decodePlayTrack(m, std::min<std::size_t>(20, m.size()));
        }
        const auto nested = formats::BigArchive::parse(m);
        for (std::uint32_t i = 0; i < bank.recordCount(); ++i) {
            const auto& r = bank.record(i);
            if (r.phrase != phrase || r.segment >= nested.entries.size()) continue;
            const auto seg = formats::BigArchive::payload(m, nested.entries[r.segment]);
            const auto pcm = formats::decodePlayTrack(seg, std::min<std::size_t>(20, seg.size()));
            if (pcm.frames() == 0) continue; // MIDx/empty payloads: exe skips, counts decoded
            ++chantSegments_;
            if (out.samples.empty()) {
                out = pcm;
            } else if (pcm.sampleRate == out.sampleRate && pcm.channels == out.channels) {
                out.samples.insert(out.samples.end(), pcm.samples.begin(), pcm.samples.end());
            }
        }
    } catch (const std::exception&) { // malformed bank/phrase: silent, like the exe loader lane
    }
    return out;
}

void MatchAudio::catalogAnnouncers(std::uint16_t homeTeamId, std::uint16_t awayTeamId) {
    // Announcer lane (pass-51): the match-boot installer FUN_0048fa08 loads
    // `AUDIO\TEAM\A_T%04d.PBK` (@0x566bac) once per match team, ids read from the match record
    // slots (DAT_00641014 + 0x34 / DAT_00641110 + 0x34, u16), 4 slots per team. The request
    // handler FUN_0048953c picks the G_T%04d variant (`AUDIO\team\G_T%04d.pbk` @0x56698c) when
    // DAT_0057e5f8 is set and the A_T variant (@0x566974) otherwise, plus a hardcoded
    // `AUDIO\team\A_FE_USA.SMP` neutral-English fallback. Catalog both prefixes per team.
    const std::uint16_t ids[2] = {homeTeamId, awayTeamId};
    for (const std::uint16_t id : ids) {
        for (const char* prefix : {"A_T", "G_T"}) {
            const auto path = tree_ / "AUDIO" / "TEAM" / std::format("{}{:04d}.PBK", prefix, id);
            if (!std::filesystem::exists(path)) continue;
            try {
                const auto bytes = readFile(path);
                const std::uint32_t phrases =
                    formats::PhraseBank::parse(std::span<const std::uint8_t>{bytes})
                        .phraseCount();
                announcerPhrases_ += phrases;
                nameSlotsRegistered_ += phrases; // FUN_0048d114 registration loop (pass-57)
                ++announcerBanks_;
            } catch (const std::exception&) {
            }
        }
    }
}

void MatchAudio::catalogPlayers(const std::vector<std::uint16_t>& ids) {
    // Player-name announcer lane (pass-51): the match-boot installer FUN_0048fb10 walks both
    // team squads (2 records x 17 slot entries, stride 0x66 with 6-byte slot records over the
    // squad array @DAT_0064101c) and loads `AUDIO\PLRS\P%05d.PBK` (@0x566bc4) for every
    // non-zero player id (u16 at slot+2), one bank pool entry each. FCDB roster ids feed the
    // same %05d key; the CD ships 1020 Pxxxxx.PBK banks. Pass-57 decoded the boot registry:
    // each loaded bank flows through FUN_0048d198 (open via FUN_00439810) -> FUN_0048d114,
    // which registers its parse-count phrase records into the phrase queue while the preload
    // gate DAT_0063241c is set; the voice pump FUN_0048f920 stamps each play through
    // _DAT_006326bc = u16@phrase+0x12. The gameplay event -> name-request site stays [H]
    // (no static caller formats or picks PLRS ids), so playback of name phrases is disabled.
    for (const std::uint16_t id : ids) {
        if (!id) continue;
        const auto path = tree_ / "AUDIO" / "PLRS" / std::format("P{:05d}.PBK", id);
        if (!std::filesystem::exists(path)) continue;
        try {
            const auto bytes = readFile(path);
            const std::uint32_t phrases =
                formats::PhraseBank::parse(std::span<const std::uint8_t>{bytes})
                    .phraseCount();
            plrsPhrases_ += phrases;
            nameSlotsRegistered_ += phrases; // FUN_0048d114 loop, gate DAT_0063241c set
                                             // by preload enable FUN_0048d378 at boot
            ++plrsBanks_;
        } catch (const std::exception&) {
        }
    }
}

void MatchAudio::catalogCrwd() {
    // Scene-10 init lane (pass-56 cr5 disassembly of FUN_0048e6d0/FUN_0048e280): the
    // loader loop formats AUDIO\CRWD\%s (@0x566acc) from pointer table DAT_0057eb44 and
    // loads exactly two banks -> CROWD.PBK slot DAT_006325d4, COMOS.PBK slot DAT_006325d8.
    // FUN_004a49a0 reads CROWD.PBK's phrase count, FUN_0049f5a0 reserves an "SNDCRD"
    // (@0x566ae8) arena of count*12 bytes, and count stride-12 records are pushed into
    // queue DAT_006325e0 via qbottom; FUN_0048e1c4 stamps phrase->MIDI ids at record +6
    // (FUN_004e43c0, sentinel EDX = 0xffffffff). The queue feeds the CMIDI.BNK MIDI
    // table lane, which needs CMIDI.DLL's synth (A5, blocked on Linux) - the port
    // mirrors the registry counts only, no audible lane is invented.
    const std::filesystem::path crwd = tree_ / "AUDIO" / "CRWD";
    try {
        const auto bytes = readFile(crwd / "CROWD.PBK");
        crwdPhrasesRegistered_ =
            formats::PhraseBank::parse(std::span<const std::uint8_t>{bytes}).phraseCount();
    } catch (const std::exception&) {
    }
    try {
        const auto bytes = readFile(crwd / "COMOS.PBK");
        crwdComosPhrases_ =
            formats::PhraseBank::parse(std::span<const std::uint8_t>{bytes}).phraseCount();
    } catch (const std::exception&) {
    }
}

void MatchAudio::catalogSfxs() {
    // SFXS install lane (pass-72, FUN_0048f1c4/FUN_0048eff0): the exe boots exactly two
    // stream slots - game bank (game00.bnk on language byte 0/1, game01.bnk otherwise) and
    // player bank (plrs00.bnk on tick-parity 1, plrs01.bnk otherwise) - opened through
    // AUDIO\SFXS\%s and registered via SNDbankadd2files FUN_004e3724 into the BKNL
    // registry DAT_00659de8 (max 10 entries). The port parses all four shipped banks to
    // mirror the registry contents (the variant pick itself is uptime-nondeterministic
    // in the exe: tick parity; language byte mirrors settings::Settings::language).
    const std::filesystem::path sfxs = tree_ / "AUDIO" / "SFXS";
    for (const char* name : {"GAME00.BNK", "GAME01.BNK"}) {
        try {
            const auto data = readFile(sfxs / name);
            const formats::Bnk bnk = formats::Bnk::parse(data);
            sfxsGameSounds_ = static_cast<std::uint32_t>(bnk.sounds.size());
            ++sfxsBanksRegistered_;
        } catch (const std::exception&) {
        }
    }
    for (const char* name : {"PLRS00.BNK", "PLRS01.BNK"}) {
        try {
            const auto data = readFile(sfxs / name);
            const formats::Bnk bnk = formats::Bnk::parse(data);
            sfxsPlrsSounds_ = static_cast<std::uint32_t>(bnk.sounds.size());
            ++sfxsBanksRegistered_;
        } catch (const std::exception&) {
        }
    }
}

// Percent row -> SNDcrowdcontrol level -> CROWD.CRD curve entry. FUN_004906a4 computes
// level = pct*0x7f/100 clamped to >= 1 and FUN_004e502c (SNDcrowdcontrol pump, cmn\scrowd.c)
// reads the slot's gain byte curve[level] at that level (byte range 0..127, /127 scale,
// measured pointer arithmetic from the card). Normalizing the fallback curve at entry 127
// (the percent lane's max level) keeps options 100 == full lane gain and entry 0
// (options 0 -> level 1 -> curve 0) == silent bed, matching the measured original envelope.
float MatchAudio::crowdCurveRatio() const {
    const int pct = std::clamp(crowdPctApplied_, 0, 100);
    if (!crowdCurveValid_) return pct / 100.f;
    const int level = std::max(1, pct * 0x7f / 100); // FUN_004906a4
    return float(crowdCurve_[std::min(level, 127)]) / float(crowdCurve_[127]);
}

float MatchAudio::crowdLayerGain(const CrowdLayer& layer) const {
    // FUN_005444 consumer: every frame the slot's gain comes from its curve table at the
    // envelope blend index (cc high half, CROWD algebraic band [250, 404]), scaled by the
    // percent-lane controller (curve byte * DAT_005894ac >> 0x17). Modeled here as the
    // blend curve gain times the percent lane linearly [M]: blend moves the bed up/down
    // within the slot curve, the options row gates it to silence.
    const float pct = std::clamp(crowdPctApplied_, 0, 100) / 100.f;
    const int idx = std::min(crowdEnvelope_.blendIndex(), 511);
    if (layer.curve[511] == 0) { // unbound slot -> fallback percent curve (no card: linear gate)
        if (!crowdCurveValid_) return pct;
        const int max = std::max(1, int(crowdCurve_[127]));
        return crowdCurveRatio() * float(crowdCurve_[idx]) / float(max);
    }
    return float(layer.curve[idx]) / 127.f * pct;
}

float MatchAudio::trafficGain() const {
    if (!trafficCurveValid_) return crowdCurveRatio();
    const float pct = std::clamp(crowdPctApplied_, 0, 100) / 100.f;
    const int idx = std::min(trafficEnvelope_.blendIndex(), 511);
    return float(trafficCurve_[idx]) / 127.f * pct;
}

bool MatchAudio::crowdStarted() const {
    for (const auto& layer : crowdLayers_)
        if (layer.id) return true;
    return false;
}

void MatchAudio::startCrowd() {
    // Scene-level layer (pass-49 correction: DAT_00632778 idx2 = SNDstadiumreactions, the
    // crowd/CRWD stream per FUN_0047a7c4) scales the loop voices through the SMIX voice lane;
    // the card controller curves (pass-52/54) then shape each slot's voice float.
    crowdPctApplied_ = audio::sceneLevels().level(audio::kSceneCrowd);
    appliedCrowdIdx_ = crowdEnvelope_.blendIndex();
    appliedTrafficIdx_ = trafficEnvelope_.blendIndex();
    if (!mixer_) return;
    const float lane = smixLaneGain(audio::kSceneCrowd);
    for (auto& layer : crowdLayers_)
        if (!layer.id)
            layer.id = mixer_->play(layer.pcm, 0.6f * lane * crowdLayerGain(layer),
                                    /*loop=*/true);
}

void MatchAudio::applyCrowdGains() {
    // Re-fade the running lane voices to their current blended gains (the exe consumer
    // writes the voice gain bytes directly; a short fade approximates the seam [M]).
    if (!mixer_) return;
    const float lane = smixLaneGain(audio::kSceneCrowd);
    for (auto& layer : crowdLayers_)
        if (layer.id) mixer_->fade(layer.id, 0.6f * lane * crowdLayerGain(layer), 2);
    if (trafficId_) mixer_->fade(trafficId_, 0.12f * lane * trafficGain(), 2);
}

void MatchAudio::tickCrowd() {
    // Frame pump (pass-55): idle walk + blend. Voices are rewritten only when the curve
    // index moves (FUN_005444 change gate), like the exe consumer.
    crowdEnvelope_.tick();
    trafficEnvelope_.tick();
    const int ci = crowdEnvelope_.blendIndex();
    const int ti = trafficEnvelope_.blendIndex();
    const bool moved = ci != appliedCrowdIdx_ || ti != appliedTrafficIdx_;
    appliedCrowdIdx_ = ci;
    appliedTrafficIdx_ = ti;
    if (moved) applyCrowdGains();
}

bool MatchAudio::crowdPulse(std::size_t recordIdx) {
    // SNDcrowdpulse lane (FUN_004e4cac): absent records (byte 0 == 0) are refused; the
    // parse only keeps records present in the card, so this is the enable gate.
    if (recordIdx >= crowdRecords_.size()) return false;
    return crowdEnvelope_.pulse(crowdRecords_[recordIdx]);
}

void MatchAudio::relevelCrowd() {
    // Options slider writes percent idx2 -> gate value; rewrite the running lane's voice floats
    // with a fade (FUN_004906a4 -> FUN_00550530 0.75x -> voice 0x34 float). idx2 gates the whole
    // stadium lane: bed layers + traffic hum + presentation sample all follow the percent row,
    // each voice at its own slot's card curve.
    const int pct = audio::sceneLevels().level(audio::kSceneCrowd);
    if (pct == crowdPctApplied_) return;
    crowdPctApplied_ = pct;
    const float lane = smixLaneGain(audio::kSceneCrowd);
    if (!mixer_) return;
    for (auto& layer : crowdLayers_)
        if (layer.id) mixer_->fade(layer.id, 0.6f * lane * crowdLayerGain(layer), 50); // 50 x 20 ms ticks
    if (trafficId_) mixer_->fade(trafficId_, 0.12f * lane * trafficGain(), 50);
    if (anthemId_ && mixer_->playing(anthemId_))
        mixer_->fade(anthemId_, 0.8f * lane * crowdCurveRatio(), 50);
}

void MatchAudio::trigger(const sim::SimEvent& e) {
    switch (e.type) {
    case sim::EventType::KickOff: // short opening chirp
        ++whistles_;
        if (mixer_ && whistle_.frames()) mixer_->play(whistle_, 1.0f * smixLaneGain(audio::kSceneMaster));
        break;
    case sim::EventType::PeriodEnd:
    case sim::EventType::MatchEnd: // long closing trill; whistle chain FUN_004bedb0 -> FUN_0051d480
        ++whistles_;
        if (mixer_ && whistleTrill_.frames()) mixer_->play(whistleTrill_, 1.0f * smixLaneGain(audio::kSceneMaster));
        break;
    case sim::EventType::Shot: // ball strike; the exe plays it at the kick code points
        ++kicks_;
        if (mixer_ && kick_.frames()) mixer_->play(kick_, 0.8f * smixLaneGain(audio::kSceneMaster));
        break;
    case sim::EventType::Foul: // referee chirp on the foul decision [H]
        ++whistles_;
        if (mixer_ && whistle_.frames()) mixer_->play(whistle_, 1.0f * smixLaneGain(audio::kSceneMaster));
        break;
    case sim::EventType::Goal: // crowd roar swell over the bed (measured GAME00 slot 45 profile)
        ++goalRoars_;
        if (mixer_ && goalRoar_.frames()) {
            if (goalId_) mixer_->stop(goalId_);
            goalId_ = mixer_->play(goalRoar_, 0.9f * smixLaneGain(audio::kSceneCrowd), /*loop=*/false);
        }
        break;
    default: break;
    }
}

// FE music list: exe glob FE*.ASF; port matches FE_*.ASF in the extracted tree. The
// BE_PAU*.ASF tracks belong to the mode-2 pause-screen list (FUN_0048ed9c FUN_00446624()==2
// branch DAT_00632670), not wired yet.
namespace {
std::vector<std::filesystem::path> collectMusicTracks(const std::filesystem::path& tree) {
    std::vector<std::filesystem::path> tracks;
    const auto dir = tree / "AUDIO" / "MUSC";
    if (!std::filesystem::is_directory(dir)) return tracks;
    for (const auto& de : std::filesystem::directory_iterator(dir)) {
        const auto name = de.path().filename().string();
        if (name.starts_with("FE_") && name.ends_with(".ASF")) tracks.push_back(de.path());
    }
    std::ranges::sort(tracks);
    return tracks;
}
} // namespace

bool MenuMusicDirector::start(audio::Mixer& mixer, const std::filesystem::path& tree) {
    files_ = collectMusicTracks(tree);
    if (files_.empty()) return false;
    mixer_ = &mixer;
    idx_ = 0;
    lane_ = laneGain();
    return playCurrent(lane_);
}

float MenuMusicDirector::laneGain() const { return smixLaneGain(audio::kSceneMusic); }

bool MenuMusicDirector::playCurrent(float gain) {
    if (!mixer_ || files_.empty()) return false;
    for (std::size_t attempt = 0; attempt < files_.size(); ++attempt) {
        const std::size_t i = (idx_ + attempt) % files_.size();
        try {
            curFile_ = std::make_shared<std::vector<std::uint8_t>>(readFile(files_[i]));
            const auto streams = formats::scanSchl(*curFile_);
            if (streams.empty()) continue;
            idx_ = i;
            // One-shot stream (loop=false): the update() rotation replaces the original pump's
            // end-of-track detection (FUN_0048eb98 list rotation).
            id_ = mixer_->stream(curFile_, streams.front(), gain, /*loop=*/false);
            return true;
        } catch (const std::exception&) { // try the next candidate
        }
    }
    curFile_.reset();
    return false;
}

std::string MenuMusicDirector::trackName() const {
    return id_ && idx_ < files_.size() ? files_[idx_].filename().string() : std::string();
}

void MenuMusicDirector::update() {
    if (!mixer_) return;
    if (leaving_) {
        if (--fadeFrames_ <= 0) { // seam complete: stop as the original ramp-out does
            mixer_->stop(id_);
            id_ = 0;
            leaving_ = false;
            if (reenterPending_) { // FE returned before the ramp finished: re-enter now
                reenterPending_ = false;
                lane_ = laneGain();
                if (playCurrent(0.f)) mixer_->fade(id_, lane_, 200);
            }
        }
        return;
    }
    const float lane = laneGain();
    if (lane != lane_) { lane_ = lane; mixer_->fade(id_, lane, 50); } // SNDstreamautovol re-level
    if (id_ && !mixer_->playing(id_)) { // track end -> next track (pump rotation)
        idx_ = (idx_ + 1) % files_.size();
        playCurrent(lane);
    }
}

void MenuMusicDirector::fadeOut() {
    if (!mixer_ || !id_ || leaving_) return;
    leaving_ = true;
    fadeFrames_ = 240; // 200 ticks (FUN_0048ec64 argument 0xc8) at ~60 fps
    mixer_->fade(id_, 0.f, 200);
}

void MenuMusicDirector::enterScene() {
    if (!mixer_) return;
    if (leaving_) { reenterPending_ = true; return; // ramp-out seam wins; restart when it ends
    }
    if (id_) return; // already playing
    lane_ = laneGain();
    if (playCurrent(0.f)) mixer_->fade(id_, lane_, 200); // ramp-in seam on scene entry
}

void MenuMusicDirector::relevel() {
    if (!mixer_ || !id_ || leaving_) return;
    lane_ = laneGain();
    mixer_->fade(id_, lane_, 50);
}

// --- Announcer (pass-77 mirror of the presentation announcement lane, FUN_004894ac/
// FUN_0048953c/LAB_0048970c/FUN_00489460/FUN_0048d1f8) -------------------------------------
Announcer::Announcer(const std::filesystem::path& tree) : tree_(tree) {}

void Announcer::setSlot(int slot, int type, std::uint16_t id) {
    if (slot < 0 || slot >= kRingSlots) return; // ring bounds DAT_0057e380 stride 0x10
    auto& s = slots_[std::size_t(slot)];
    s.type = type;
    s.id = id;
    s.pending = true; // exe stores [slot+8]=0 at fill (pending load); callback sets it built
    s.loaded = false;
    s.consumed = false;
    s.clip = {};
}

std::filesystem::path Announcer::pathFor(int slot) const {
    if (slot < 0 || slot >= kRingSlots) return {};
    const auto& s = slots_[std::size_t(slot)];
    if (s.type == 0) return {}; // empty ring slot: loader switch has no path
    // `id == 0x32f && DAT_00640fe8 == 1` neutral special: type rewritten to 4, file
    // `AUDIO\team\A_FE_USA.SMP` (not shipped -> missing-file lane below).
    if (s.type == 1 && s.id == kNeutralId && neutralGate_)
        return tree_ / "AUDIO" / "TEAM" / "A_FE_USA.SMP";
    const std::string dir = "AUDIO";
    switch (s.type) {
    case 1:
        return tree_ / dir / "TEAM" /
               std::format("{}T{:04d}.PBK", away_ ? "A_" : "G_", s.id); // selector DAT_0057e5f8
    case 2:
        // Exe format strings are lowercase (`AUDIO\pbp\schedule\G_sch%03d.smp` @0x566938);
        // the shipped ISO stores names uppercased and Windows loads case-insensitively, so
        // the port uses the tree's real spelling on case-sensitive Linux.
        return tree_ / dir / "PBP" / "SCHEDULE" /
               std::format("{}_SCH{:03d}.SMP", away_ ? "A" : "G", s.id);
    case 3:
        return tree_ / dir / "PBP" / "STANDING" /
               std::format("{}_STD{:03d}.SMP", away_ ? "A" : "G", s.id);
    case 4: // type rewritten by the neutral special; away selector per the switch's case 4
        return away_ ? tree_ / dir / "TEAM" / "A_FE_USA.SMP" : std::filesystem::path{};
    default:
        return {};
    }
}

std::filesystem::path Announcer::loadSlot(int slot) {
    if (slot < 0 || slot >= kRingSlots) return {};
    auto& st = slots_[std::size_t(slot)];
    if (st.type == 0 || !st.pending || st.loaded) return {}; // re-arm: [slot+8] already done
    const auto path = pathFor(slot);
    if (path.empty()) return {};
    ++loads_; // loader request counted (FUN_0048953c entry)
    if (!std::filesystem::exists(path)) { // exe missing-file lane: record queued, entry silent
        ++missing_;
        st.pending = false;
        st.loaded = true; // callback still builds the entry (pending flag set), silent
        return path;
    }
    auto& s = slots_[std::size_t(slot)];
    try {
        const auto bytes = readFile(path);
        const std::span<const std::uint8_t> data{bytes};
        if (s.type == 1 || s.type == 4) {
            // Team bank: phrase records (nested BIGF containers, pass-51 catalog lane). The
            // builder tail adds phrase id `[item+0E]` (pick unmeasured [H]) -> play the first
            // non-empty phrase decoded through the commentary segment seam.
            const auto bank = formats::PhraseBank::parse(data);
            phrasesParsed_ += bank.phraseCount();
            for (std::uint32_t p = 0; p < bank.phraseCount(); ++p) {
                if (bank.emptyPhrase(p)) continue;
                const auto member = formats::refpack::decompressIfNeeded(bank.memberBytes(p));
                const std::span<const std::uint8_t> m{member};
                if (formats::BigArchive::isBig(m)) {
                    const auto nested = formats::BigArchive::parse(m);
                    formats::Pcm clip;
                    for (std::uint32_t i = 0; i < bank.recordCount(); ++i) {
                        const auto& r = bank.record(i);
                        if (r.phrase != p || r.segment >= nested.entries.size()) continue;
                        const auto seg = formats::BigArchive::payload(m, nested.entries[r.segment]);
                        const auto pcm = formats::decodePlayTrack(seg, std::min<std::size_t>(20, seg.size()));
                        if (pcm.frames() == 0) continue;
                        if (clip.samples.empty()) clip = pcm;
                        else if (pcm.sampleRate == clip.sampleRate && pcm.channels == clip.channels)
                            clip.samples.insert(clip.samples.end(), pcm.samples.begin(), pcm.samples.end());
                    }
                    if (!clip.samples.empty()) { s.clip = std::move(clip); break; }
                } else {
                    auto pcm = formats::decodePlayTrack(m, std::min<std::size_t>(20, m.size()));
                    if (pcm.frames() != 0) { s.clip = std::move(pcm); break; }
                }
            }
        } else {
            // .SMP = single PT v2 play track at file offset 0 (measured: "PT" magic @0).
            s.clip = formats::decodePlayTrack(data, 0);
        }
    } catch (const std::exception&) { // malformed file: entry silent like the exe callback
    }
    s.loaded = true; // callback LAB_0048970c: [slot+8] pending flag set + class queue push
    if (!s.clip.samples.empty()) { ++decoded_; lastClip_ = s.clip; }
    return path;
}

bool Announcer::activate(int slot) {
    if (slot < 0 || slot >= kRingSlots) return false;
    auto& s = slots_[std::size_t(slot)];
    if (!s.loaded || s.consumed || s.clip.samples.empty()) return false; // qfind null / silent
    s.consumed = true; // activation FUN_0048d1f8: qremove, plays exactly once
    ++plays_;
    lastClip_ = s.clip;
    if (!mixer_) return true; // null-mixer drivers stay headless-safe (counted, silent)
    // Voice pump class = speech autovol lane (idx3, FUN_0048fca0); percent 0 -> silent
    // request like the percent-gated chant start (FUN_0048d678).
    const int pct = audio::sceneLevels().level(audio::kSceneSpeech);
    if (pct < 1) return true;
    const float gain = std::min(1.0f, float(pct) / 100.0f) * smixLaneGain(audio::kSceneSpeech);
    mixer_->play(s.clip, gain, /*loop=*/false);
    return true;
}

bool Announcer::pumpPending() {
    bool any = false;
    for (int slot = 0; slot < kRingSlots; ++slot) { // builder FUN_00489460 scan
        auto& s = slots_[std::size_t(slot)];
        if (s.type == 0 || s.loaded || !s.pending) continue; // re-arm test: [slot+8] + [slot+0]
        loadSlot(slot);
        any = true;
    }
    return any;
}

bool Announcer::pickNext() {
    for (int slot = 0; slot < kRingSlots; ++slot) { // pick lane, class-queue order [H]
        auto& s = slots_[std::size_t(slot)];
        if (!s.loaded || s.consumed) continue;
        if (s.clip.samples.empty()) { // silent entry: the exe pick lane qremoves the
            s.consumed = true;        // queue entry regardless of clip, so it cannot
            continue;                 // stall the scan ahead of playable slots
        }
        return activate(slot); // one entry per call
    }
    return false; // queue drained: qfind-null lane
}

namespace {
// FUN_00489bdc skip predicate: hardcoded per-record exclusions (measured constants,
// analysis/pass78grid/grid_dump.txt; both sets sorted ascending -> binary search).
constexpr std::uint16_t kSkips2[] = {23, 31, 34, 37, 38, 50, 62, 85, 133};
constexpr std::uint16_t kSkips3[] = {5, 6, 7, 8, 11, 13, 17, 18, 39, 43, 44, 46, 47, 50, 53, 54,
                                     95, 96, 97, 98, 120, 121, 122, 125, 127, 130, 131, 132};
// Raw DAT_0057e3fe pair table (u16 pairs, 40 scan entries + adjacent 0xd6 word). remmap
// matches pair.second (the SI team id) and returns the *next* slot's first u16 (the exe
// `*(int*)(base + i + 2) >> 0x10` read), so team -> schedule id pairs are encoded shifted.
constexpr std::pair<std::uint16_t, std::uint16_t> kRemmapPairs[] = {
    {0x000, 0x056}, {0x0a1, 0x057}, {0x0a2, 0x058}, {0x0a3, 0x039}, {0x0a4, 0x03b}, {0x0a5, 0x03c},
    {0x0a6, 0x03d}, {0x0a7, 0x03f}, {0x0a8, 0x040}, {0x0a9, 0x041}, {0x0aa, 0x257}, {0x0ac, 0x043},
    {0x0ad, 0x045}, {0x0ae, 0x047}, {0x0af, 0x048}, {0x0b0, 0x029}, {0x0b1, 0x036}, {0x0b2, 0x2f8},
    {0x0b4, 0x02f}, {0x0b5, 0x031}, {0x0b6, 0x034}, {0x0b9, 0x035}, {0x0ba, 0x038}, {0x0bb, 0x01a},
    {0x0bd, 0x31c}, {0x0be, 0x01d}, {0x0c0, 0x01f}, {0x0c2, 0x020}, {0x0c3, 0x021}, {0x0c4, 0x022},
    {0x0c5, 0x023}, {0x0c6, 0x024}, {0x0c7, 0x025}, {0x0c8, 0x026}, {0x0c9, 0x027}, {0x0ca, 0x27b},
    {0x0cc, 0x32f}, {0x0cf, 0x2f0}, {0x0d2, 0x03e}, {0x0d3, 0x2c0}, {0x0d6, 0x056}, // next word
};
} // namespace

std::uint16_t Announcer::remmap(std::uint16_t team) {
    for (std::size_t i = 0; i < 40; ++i) // exe loop bound `iVar1 < 0xa0`
        if (team == kRemmapPairs[i].second) return kRemmapPairs[i + 1].first;
    return 0xa1; // default word
}

bool Announcer::isMember(std::uint32_t needle) const {
    for (const auto id : membership_) // exe scan `iVar1 < DAT_0062facc * 4`
        if (needle == id) return true;
    return false; // DAT_0062facc == 0 -> not a member (exe zero-count lane)
}

std::uint16_t Announcer::selectedTeamId(std::uint32_t needle) const {
    return isMember(needle) ? homeId_ : awayId_; // FUN_00485240: hit fab0, miss fab4
}

std::pair<std::uint16_t, std::uint16_t> Announcer::storePair(std::uint32_t needle) const {
    // FUN_0048525c ordering: the pair lands (f468, f46c) when FUN_00485240() == DAT_0062fab0
    // (membership hit), and reversed on a miss.
    return isMember(needle) ? std::pair<std::uint16_t, std::uint16_t>{pairFirst_, pairSecond_}
                            : std::pair<std::uint16_t, std::uint16_t>{pairSecond_, pairFirst_};
}

bool Announcer::fillSelector(int selector, bool type3) {
    unsigned start = 0, end = 0;
    if (selector >= 0x22) { // second uniform 5-block grid: s -> [5k, 5k+5)
        start = 5u * static_cast<unsigned>(selector - 0x22);
        end = start + 5u;
    } else if (selector >= 0 && selector <= 0x1c) { // grid A partition of records 0..160
        if (selector < 0x13) start = 5u * static_cast<unsigned>(selector);
        else if (selector <= 0x15) start = 95u + 10u * static_cast<unsigned>(selector - 0x13);
        else start = 125u + 5u * static_cast<unsigned>(selector - 0x16);
        const unsigned nxt = static_cast<unsigned>(selector + 1);
        if (nxt < 0x13) end = 5u * nxt;
        else if (nxt <= 0x15) end = 95u + 10u * (nxt - 0x13);
        else if (nxt <= 0x1c) end = 125u + 5u * (nxt - 0x16);
        else end = 0xa0; // wrap word v[0x1d] = 0xa0 closes the last block
    } else if (selector >= 0x1d && selector <= 0x21) {
        // The exe fill routine FUN_00489974 indexes the boundary table at any selector
        // (`*(int *)(&DAT_0057e938 + EAX*4)` + next word), so the words behind the wrap
        // word 0xa0 are still selectable. Table values v[0x1d..0x22] dumped from raw
        // 0x17cfac: {0xa0, 0x29, 0x2a, 0x50, 0x51, 0x0} - 0x1d and 0x21 give empty
        // ranges (start > end), 0x1e fills [41,42) (record 41 -> id 42), 0x1f
        // [42,80), 0x20 [80,81).
        static const unsigned kAdj[6] = {0xa0, 0x29, 0x2a, 0x50, 0x51, 0};
        start = kAdj[selector - 0x1d];
        end = kAdj[selector - 0x1d + 1];
        if (start >= end) { // empty range: exe loop never runs, list stays reset
            selList_.clear();
            return false;
        }
    } else {
        return false;
    }
    selList_.clear();
    const std::span<const std::uint16_t> skips =
        type3 ? std::span<const std::uint16_t>{kSkips3} : std::span<const std::uint16_t>{kSkips2};
    for (unsigned rec = start; rec < end && selList_.size() <= 0x28; ++rec) { // count guard
        if (std::binary_search(skips.begin(), skips.end(), rec)) continue; // skip = 0
        selList_.push_back(static_cast<std::uint16_t>(rec + 1)); // DAT_0062fbf8 stores rec+1
    }
    return !selList_.empty();
}

std::int32_t Announcer::pickSelector() {
    if (selList_.empty()) return -1; // exe -1 sentinel
    // Exe pick lanes (byte scan): `DAT_0057e600` (cursor-lane mode flag, initialized
    // DGROUP, static dword 0) has exactly one read - 0x4899e0 `mov edx,[0x57e600]` - and
    // one store, inside the setter at 0x489ba4 (`mov [DAT_0062fd9c],EDX; mov
    // [DAT_0057e600],EAX`): no E8/E9 caller and no imm32 (function-pointer) reference to
    // 0x489ba0/4/6 anywhere in the file -> retail stays on the tick-fraction lane
    // `DAT_0062fbf8[ROUND(FUN_004973b0(tick))]`; the cursor lane (clamp store 0x4899fc,
    // cursor never increments) is unreached. Tick lane unmirrored [H]: the port returns
    // entry 0 as a stand-in.
    return selList_[0];
}

void Announcer::dispatchResult(int outcome, bool matchFlagLane, std::uint32_t needle) {
    dispatchResult(outcome, matchFlagLane, needle, VariantFeed{});
}

void Announcer::dispatchResult(int outcome, bool matchFlagLane, std::uint32_t needle,
                               const VariantFeed& variant) {
    // exe dispatch 0x458dcb: `cmp eax,3; ja 0x458dfb` - outcome -1 (FUN_0048503c miss)
    // or > 3 enters the shared tail at 0x458dfb, skipping only the branch fill/pick/setSlot.
    if (outcome >= 0 && outcome <= 3) {
        // Branch bodies 0x458dd7/0x458eaf/0x458ed9/0x458f00: EDX=2 type flag, selector =
        // outcome (b2 loads EAX=EDX=2 via 0x458ede `mov %edx,%eax`).
        fillSelector(outcome, false);
        const std::int32_t id = pickSelector(); // FUN_004899dc at 0x458de3/0x458ebe/0x458ee5/0x458f0f
        if (id >= 0) setSlot(1, 3, static_cast<std::uint16_t>(id)); // tail 0x458df6 FUN_004894ac
    }
    // 0x458dfb FUN_00489a64: DAT_0x62fd98 := 0 (global word reset, no port state).
    // Order ecx lane (0x458e0b-22) over the FUN_0048525c stack words = storePair output.
    const auto words = storePair(needle);
    const int order = words.first > words.second ? 1 : (words.first == words.second ? 3 : 0);
    int sel = -1; // pass-104 selector under the record+3 dispatch (0x458e32-43)
    if (matchFlagLane) { // 0x458e27 cmp dh,1 on DAT_0x6099c0
        if (variant.statusByte == 2) {
            // 0x458f30 ECX lane: FUN_00498e90 gate (team membership, record+0x10 array -
            // solid), fail arms 0x459058/0x4590ac/0x459103 with FUN_004998c0 EBX=2/1
            // what-if outcome lookups (EDX = EDI branch value vs group+0x16 team array,
            // snapshot/restore standings predicate - pass-107). ECX==3 arm: EBX=1 hit ->
            // 0x7, miss -> 0x9.
            if (variant.teamRegistered) sel = 0xe;
            else if (order == 1) sel = variant.lookup2Hit ? 0xd : (variant.lookup1Hit ? 0x4 : 0x6);
            else if (order == 0) sel = variant.lookup2Hit ? 0xd : (variant.lookup1Hit ? 0xa : 0xc);
            else if (order == 3) sel = variant.lookup2Hit ? 0xd : (variant.lookup1Hit ? 0x7 : 0x9);
        } else if (variant.statusByte == 3) {
            // 0x458ff4 EDI lane: the EBP pair-equality predicate (pass-103 0x458d83-eaa,
            // group+0xe == group+0x10 OR DAT 0x606bd4 == 2 - independent of the ECX
            // word order) gates the direct fills; fail arms 0x459130/0x459191 re-check
            // FUN_00498e90 then pick ECX: ECX == EDI -> 0x4, ECX == 0 -> 0xa,
            // ECX == 3 -> 0x1e, otherwise the continue lane (no fill). EDI == 2 fills
            // sel 0x11 with EDX = branch index (0x459037, value 2 -> type-2 fill lane);
            // EDI > 3 goes straight to the second pick with no fill.
            if (variant.branchIndex == 0) {
                if (variant.ebpPairEqual) sel = 0xf;
                else if (variant.teamRegistered) sel = 0x11;
                else if (order == 1) sel = 0x4;
                else if (order == 0) sel = 0xa;
                else if (order == 3) sel = 0x1e;
            } else if (variant.branchIndex == 1) {
                if (variant.ebpPairEqual) sel = 0x10;
                else if (variant.teamRegistered) sel = 0x11;
                else if (order == 1) sel = 0x4; // ECX == EDI
                else if (order == 0) sel = 0xa;
                else if (order == 3) sel = 0x1e;
            } else if (variant.branchIndex == 2) {
                sel = 0x11;
            } else if (variant.branchIndex == 3) {
                sel = 0x12;
            }
        } else {
            // status 1 plain fills (status 0 goes straight to the pick; the port's
            // pass-102 flag-lane approximation keeps order {5, 0xb, 8}).
            if (order == 1) sel = 5;            // 0x458e4a
            else if (order == 0) sel = 0xb;     // 0x458e5d
            else if (order == 3) sel = 8;       // 0x458e71
        }
    }
    if (sel >= 0) fillSelector(sel, false); // fill target 0x489974 lane
    const std::int32_t pid = pickSelector(); // 0x458e80 second pick
    if (pid >= 0) { // 0x458f23 je epilogue; else 0x4591f1 target
        setSlot(2, 3, static_cast<std::uint16_t>(pid)); // 0x4591f1 EAX=2 EDX=3 EBX=id
        // call 0x4894c4 builder arm -> port pumpPending()/pickNext() tail lane
    }
    pumpPending(); // entry FUN_00489460 builder load + arm
    pickNext();    // FUN_0048d0dc activation lane: one announcement, never stacked
}

void feedTeamSelectionAnnouncer(Announcer& announcer, std::int32_t team,
                                int outcome, bool matchFlagLane) {
    if (team < 0) return; // FUN_004dac90() >= 7 gate lane: no pick, nothing filled
    announcer.setSlot(0, 1, static_cast<std::uint16_t>(team)); // 0x458dc2 SI team bank
    // pass-102 jump-table tail: outcome index (FUN_0048503c schedule-pool scan, FE
    // board pool -> 0 for the picked team [H]); needle = picked team id [H] (pass-84
    // pack-pair provenance lane).
    announcer.dispatchResult(outcome, matchFlagLane, static_cast<std::uint32_t>(team));
}

void feedBoardAnnouncer(Announcer& announcer, std::int32_t homeTeam) {
    announcer.fillSelector(0x26, false); // 0x453bd2 schedule lane
    const std::int32_t id26 = announcer.pickSelector();
    announcer.fillSelector(0x36, false); // 0x453c77 schedule lane
    const std::int32_t id36 = announcer.pickSelector();
    if (homeTeam >= 0) {
        const auto team = static_cast<std::uint16_t>(homeTeam);
        announcer.setSlot(0, 1, team);                     // 0x453b07 pair-store lane (pair order
                                                           // decoded pass-83; ECX id = stored id,
                                                           // provenance closed pass-84)
        announcer.setSlot(3, 2, Announcer::remmap(team));  // 0x453b2e remmap -> schedule id
    }
    announcer.setSlot(2, 2, 0); // 0x453b15 literal id 0 (silent schedule sample)
    if (id26 >= 0) announcer.setSlot(1, 2, static_cast<std::uint16_t>(id26));
    if (id36 >= 0) announcer.setSlot(4, 2, static_cast<std::uint16_t>(id36));
    announcer.pumpPending();
    announcer.pickNext(); // one announcement per pick call (FUN_0048d0dc semantics)
}

// pass-101 caption-board fill mirror. Scene attribution is resolved by the scene registry
// (`FUN_00446598` registration block, analysis/pass8/tourney_registry_entries.txt): every
// announcer handler call site sits inside a dispatcher registered under a feflowNN name VA:
//   feflow21 disp 0x454298 <- 0x4542c9 -> FUN_00454338
//   feflow23 disp 0x4545fc <- 0x45463b -> FUN_00454d60
//   feflow24 disp 0x45502c <- 0x455064 -> FUN_0045548c
//   feflow25 disp 0x45570c <- 0x45574b -> FUN_00455a3c
//   feflow26 disp 0x455d28 <- 0x455d5b -> FUN_00455f30
// Fill sites (pass-77b windows): selector lane + type-1 lane in exe order; the later t1
// store overwrites the earlier t3 slot-0 fill exactly as the exe ring keeps both stores
// and the builder consumes the pending entry. The t1 id is the FUN_00485240 selection
// lane; needle provenance closed pass-84 (`analysis/pass84needle/n1.txt` - the needle is
// the stored id from the handler's packed-pair ECX upper word), so membership-feed state
// decides.
void feedBoardHandlerAnnouncer(Announcer& announcer, int scene, std::int32_t team) {
    int sel0 = -1, sel1 = -1;
    bool t1 = true;
    switch (scene) {
    case 21: sel0 = 0x1b; sel1 = 0x16; break; // 0x45437d s0 t3, 0x454398 s0 t1, 0x4543bd s1 t3
    case 23: sel0 = 0x18; sel1 = 0x13; break; // 0x454da5 s0 t3, 0x454dc0 s0 t1, 0x454de5 s1 t3
    case 24: sel0 = 0x17; t1 = false; break;  // 0x4554c9: single s0 t3 lane, no t1 site
    case 25: sel0 = 0x19; sel1 = 0x14; break; // 0x455a81 s0 t3, 0x455a9c s0 t1, 0x455ac1 s1 t3
    case 26: sel0 = 0x1a; sel1 = 0x15; break; // 0x455f75 s0 t3, 0x455f90 s0 t1, 0x455fb5 s1 t3
    default: return;                          // 20/27/35 and unknown: no handler fill here
                                             // (20 -> feedBoard, 35 -> feedTeamSelection,
                                             //  27 -> dispatcher without announcer call site)
    }
    if (sel0 >= 0) {
        announcer.fillSelector(sel0, true); // t3 slot -> standings skip set
        const std::int32_t id = announcer.pickSelector();
        if (id >= 0) announcer.setSlot(0, 3, static_cast<std::uint16_t>(id));
    }
    if (t1) // FUN_00485240 lane: hit -> home id, miss -> away id (unset -> 0 default)
        announcer.setSlot(0, 1, announcer.selectedTeamId(static_cast<std::uint32_t>(team)));
    if (sel1 >= 0) {
        announcer.fillSelector(sel1, true);
        const std::int32_t id = announcer.pickSelector();
        if (id >= 0) announcer.setSlot(1, 3, static_cast<std::uint16_t>(id));
    }
    announcer.pumpPending();
    announcer.pickNext();
}

int Announcer::slotType(int slot) const {
    if (slot < 0 || slot >= kRingSlots) return 0;
    return slots_[std::size_t(slot)].type;
}

std::uint16_t Announcer::slotId(int slot) const {
    if (slot < 0 || slot >= kRingSlots) return 0;
    return slots_[std::size_t(slot)].id;
}

bool Announcer::slotLoaded(int slot) const {
    if (slot < 0 || slot >= kRingSlots) return false;
    return slots_[std::size_t(slot)].loaded;
}

} // namespace wc98::app
