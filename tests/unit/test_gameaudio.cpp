// Derived from fifa99-native (same author), ported for World Cup 98
// Runtime-audio gate: deterministic OpenAL-soft LOOPBACK capture (no sound device). Asserts the
// FE music SCHl stream renders audible PCM over >1 s, the whistle/kick/trill/roar/crowd BNK
// slots decode to non-silent PCM, the scripted event seam fires SFX voices on the loopback
// mixer, and the idx2 crowd re-level seam fades the running bed to the lane gain.
// Model: docs/reverse-engineering/AUDIO_RUNTIME.md (MUSC/SFXS/CRWD paths, slot mapping).
#include "check.hpp"
#include "../app/game_audio.hpp"
#include "../app/commentary.hpp" // SpeechGain ramp mirror (pass-73)

#include "wc98/file.hpp"
#include "wc98/formats/bnk.hpp"
#include "wc98/formats/crd.hpp"
#include "wc98/formats/schl.hpp"
#include "wc98/sim/data.hpp" // (e) boot lane: sim::loadGameData + FCDB team ids
#include "wc98/audio/scene_levels.hpp" // director lane percent (DAT_00632778)

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

using namespace wc98;

namespace {

constexpr std::uint32_t kRate = 44100; // loopback render rate (Mixer contract)

// Pull `seconds` of stereo loopback output, return the max RMS over 0.1 s windows.
double tailRms(audio::Mixer& m, double seconds) {
    std::vector<std::int16_t> out(std::size_t(kRate * 2 * seconds));
    for (std::size_t off = 0; off < out.size(); off += 4096) {
        const std::size_t n = std::min<std::size_t>(4096, out.size() - off);
        m.render(std::span<std::int16_t>(out).subspan(off, n));
    }
    double acc = 0;
    for (std::size_t i = out.size() - 4410; i < out.size(); ++i) acc += double(out[i]) * out[i] / 4410.0;
    return std::sqrt(acc); // RMS of the final 0.1 s: the settled post-fade gain
}

double peakRms(audio::Mixer& m, double seconds) {
    std::vector<std::int16_t> out(std::size_t(kRate * 2 * seconds));
    for (std::size_t off = 0; off < out.size(); off += 4096) {
        const std::size_t n = std::min<std::size_t>(4096, out.size() - off);
        m.render(std::span<std::int16_t>(out).subspan(off, n));
    }
    double peak = 0;
    for (std::size_t off = 0; off + 4410 <= out.size(); off += 4410) { // 0.1 s windows
        double acc = 0;
        for (std::size_t i = off; i < off + 4410; ++i) acc += double(out[i]) * out[i] / 4410.0;
        peak = std::max(peak, std::sqrt(acc));
    }
    return peak;
}

std::int32_t peakAbs(const formats::Pcm& p) {
    std::int32_t peak = 0;
    for (const auto v : p.samples) peak = std::max(peak, std::abs(v));
    return peak;
}

formats::Pcm decodeSlot(std::uint32_t slot, const std::filesystem::path& file) {
    const auto data = readFile(file);
    const formats::Bnk bnk = formats::Bnk::parse(data);
    for (const auto& s : bnk.sounds)
        if (s.slot == slot) return formats::Bnk::decode(data, s);
    return {};
}

} // namespace

int main() {
    const auto tree = treeDirOrSkip(); // banks/streams need the extracted game tree

    // (a) FE menu music: SCHl .ASF stream is audible over >=1 s of loopback render.
    {
        audio::Mixer m{kRate};
        std::string track;
        CHECK(app::startMenuMusic(m, tree, &track));
        CHECK(track.starts_with("FE_")); // FE_*.ASF selection (exe glob FE*.ASF @0x566b3e)
        CHECK(peakRms(m, 1.5) > 20.0);   // non-silent music PCM
    }

    // (b) BNK slot decodes are non-silent: whistle chirp, kick thud, crowd loop.
    {
        const auto game = tree / "AUDIO" / "SFXS" / "GAME00.BNK";
        CHECK(peakAbs(decodeSlot(81, game)) > 200); // whistle (acoustic-verified chirp)
        CHECK(peakAbs(decodeSlot(23, game)) > 50);  // ball-strike thud
        CHECK(peakAbs(decodeSlot(85, game)) > 200); // period-end whistle trill (1.95 s bright)
        CHECK(peakAbs(decodeSlot(45, game)) > 200); // goal roar (4.12 s loud-noise burst)
        const auto crowd = decodeSlot(2, tree / "AUDIO" / "CRWD" / "CROWD.BNK");
        CHECK(crowd.frames() > 44100);              // ~3 s loop material
        CHECK(peakAbs(crowd) > 50);
    }

    // (c) Event seam: scripted KickOff creates the whistle voice, crowd loop starts on the
    // loopback mixer; null-mixer path stays headless-safe (counts only).
    {
        audio::Mixer m{kRate};
        app::MatchAudio ma(tree);
        ma.setMixer(&m);
        ma.startCrowd();
        CHECK(ma.crowdStarted());
        // CROWD.CRD opens its whole enabled slot list as bed layers (measured card: slots
        // 0..3 -> two swell + two murmur voices, each on its own tableRef curve).
        CHECK(ma.crowdCardSlots() == 4);
        CHECK(ma.crowdLayerCount() == 4);
        const double ambience = peakRms(m, 0.5);
        CHECK(ambience > 10.0); // crowd loop audible through the mixer

        sim::SimEvent ev{};
        ev.type = sim::EventType::KickOff;
        ma.trigger(ev);
        CHECK(ma.whistles() == 1);
        ev.type = sim::EventType::Shot;
        ma.trigger(ev);
        CHECK(ma.kicks() == 1);
        ev.type = sim::EventType::PeriodEnd;
        ma.trigger(ev);
        CHECK(ma.whistles() == 2); // KickOff/PeriodEnd/MatchEnd chain

        ev.type = sim::EventType::Goal;
        ma.trigger(ev);
        CHECK(ma.goals() == 1);
        CHECK(peakRms(m, 0.5) > 10.0); // goal roar (GAME00 slot 45 burst) audible over the bed

        ev.type = sim::EventType::Foul;
        ma.trigger(ev);
        CHECK(ma.whistles() == 3); // foul decision gets the referee chirp

        // idx2 crowd re-level seam on a fresh bed: options row 100->0 fades the running loop
        // toward silence; restoring 80 ramps it back. Dedicated mixer, no one-shot interference.
        {
            audio::Mixer m2{kRate};
            app::MatchAudio ma2(tree);
            ma2.setMixer(&m2);
            audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);
            ma2.startCrowd();
            CHECK(peakRms(m2, 0.4) > 10.0);
            audio::sceneLevels().setLevel(audio::kSceneCrowd, 0);
            ma2.relevelCrowd();
            CHECK(tailRms(m2, 2.6) < 80.0); // fade settles to silence (lane gain 0; measured ~8)
            audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);
            ma2.relevelCrowd();
            CHECK(tailRms(m2, 2.6) > 1000.0); // restored lane level ramps the bed back up (measured ~1700)
        }
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);

        app::MatchAudio nullPath(tree); // no device: counters advance, no voices, no throw
        nullPath.startCrowd();
        ev.type = sim::EventType::MatchEnd;
        nullPath.trigger(ev);
        CHECK(nullPath.whistles() == 1 && !nullPath.crowdStarted());
    }

    // (d) MenuMusicDirector: lane percent -> re-level, launch fade-out seam, re-entry ramp.
    {
        audio::sceneLevels().setLevel(audio::kSceneMusic, 50); // options music row -> idx2
        audio::Mixer m{kRate};
        app::MenuMusicDirector dir;
        CHECK(dir.start(m, tree));
        CHECK(!dir.trackName().empty());
        CHECK(dir.playing());
        CHECK(peakRms(m, 1.0) > 8.0); // audible through the 50% lane (SMIX installer float < 1)
        dir.fadeOut();                // launch seam: 200-tick ramp (FUN_0048ec64 0xc8)
        for (int i = 0; i < 300 && dir.playing(); ++i) dir.update();
        CHECK(!dir.playing());        // seam complete -> stream stopped (rotation halted at exit)
        dir.enterScene();             // returning to the FE: ramp-in seam
        CHECK(dir.playing());
        CHECK(peakRms(m, 1.5) > 8.0); // audible again (gain ramping up from 0)
        audio::sceneLevels().setLevel(audio::kSceneMusic, 80);
        dir.relevel();                // options row changed while playing
        dir.update();
        CHECK(dir.playing());
        audio::sceneLevels().setLevel(audio::kSceneMusic, 80); // restore boot default
    }

    // (e) Match-lane boot seam (pass-51): boot keys the lane pools by the FCDB match data - the
    // 113-slot chant pool (FUN_0048dfc4 `iVar1 < 0x71`, exe name table @0x57eb5c + CD CGENER
    // lane), the A_T/G_T%04d announcer banks for both teams (FUN_0048fa08), the squad P%05d.PBK
    // player banks (FUN_0048fb10), the CRD card as crowd-bed authority, the stadium lane pair +
    // anthem voice, and the percent idx2 notifier dispatch re-level over the whole lane.
    {
        audio::Mixer m{kRate};
        const auto game = sim::loadGameData(tree);
        const std::uint16_t homeId = game.db.teams[0].id;  // id 26: full lane set on the CD
        const std::uint16_t awayId = game.db.teams[26].id; // id 67: C0006700 + A_T/G_T0067
        app::MatchAudio ma(tree);
        ma.setMixer(&m);
        ma.boot(homeId, awayId, game.db.teams[0].roster, game.db.teams[26].roster);
        CHECK(ma.crowdStarted() && ma.trafficStarted() && ma.anthemStarted());
        CHECK(audio::sceneLevels().hasNotifier(audio::kSceneCrowd)); // DAT_004904b0 slot [2]
        CHECK(ma.crowdCardSlots() == 4);                             // CROWD.CRD slot list
        CHECK(ma.chantBanks() == 113 && ma.chantPhrases() > 0);      // exe boot pool = 0x71 slots
        CHECK(ma.announcerBanks() == 4 && ma.announcerPhrases() > 0); // A_T/G_T x both teams
        CHECK(ma.plrsBanks() >= 30 && ma.plrsPhrases() > 0);          // 19+19 squad banks of 26/67
        CHECK(peakRms(m, 0.6) > 10.0); // bed + traffic + anthem audible on the loopback lane
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 0); // percent write -> notifier dispatch
        CHECK(tailRms(m, 2.6) < 80.0); // whole stadium lane fades to the idx2=0 gain
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);
        CHECK(tailRms(m, 2.6) > 1000.0); // dispatch again: lane ramps back up
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);
        app::MatchAudio nullPath(tree); // no device: catalog counters advance, no voices
        nullPath.boot(game.db.teams[0].id, game.db.teams[1].id,
                      game.db.teams[0].roster, game.db.teams[1].roster);
        CHECK(!nullPath.crowdStarted() && !nullPath.trafficStarted() && !nullPath.anthemStarted());
        CHECK(nullPath.chantBanks() == 113 && nullPath.announcerBanks() >= 4 &&
              nullPath.plrsBanks() > 0);
        // Registry mirror (pass-57): FUN_0048d198 -> FUN_0048d114 registers every parsed
        // phrase while the preload gate DAT_0063241c is set, so registry == parse count.
        CHECK(ma.nameSlotsRegistered() == ma.plrsPhrases() + ma.announcerPhrases());
        // Scene-10 CRWD bank table DAT_006325d0 + name pointers DAT_0057eb44/0x566adc
        // (FUN_0048e280 disassembly): CMIDI.BNK, CROWD.PBK, COMOS.PBK all ship on the CD.
        for (const char* name : app::MatchAudio::kSceneCrowdSlots)
            CHECK(std::filesystem::exists(tree / "AUDIO" / "CRWD" / name));
        // CRWD phrase-lane registry mirror (pass-69, FUN_0048e6d0 init): the parsed
        // phraseCount(CROWD.PBK) sizes the SNDCRD arena and pushes that many stride-12
        // records into queue DAT_006325e0; COMOS.PBK fills slot DAT_006325d8.
        CHECK(nullPath.crwdPhrasesRegistered() > 0 && nullPath.crwdComosPhrases() > 0);
        // SFXS install-lane mirror (pass-72, FUN_0048f1c4/FUN_0048eff0 +
        // SNDbankadd2files FUN_004e3724): the exe registers the game bank (GAME00/01
        // chosen by language byte) + player bank (PLRS00/01 chosen by tick parity) into
        // the BKNL registry. Measured bank contents: GAME00/01 = 53 sounds (86-slot
        // table), PLRS00/01 = 17 sounds each, 4 shipped banks total.
        CHECK(nullPath.sfxsGameSounds() == 53);
        CHECK(nullPath.sfxsPlrsSounds() == 17);
        CHECK(nullPath.sfxsBanksRegistered() == 4);
    }

    // (f) Chant pump lane (pass-53): boot classification lanes the 113-entry pool by the match
    // teams (FUN_0048dcd8: home -> lane 0, away -> lane 1, CGENER -> lane 2), and the pump
    // FUN_0048da50 tops the 3-slot voice pool on the 100-frame cadence. Clips are phrase-grouped
    // segment banks (class 'a', pass-52 census): first non-empty phrase, segments concatenated
    // through decodePlayTrack. The percent idx2 row gates audibility (FUN_004906d0 target
    // vol*percent[idx2]*0x7f/10000), so row 0 keeps requests silent with no voice.
    {
        audio::Mixer m2{kRate};
        const auto game = sim::loadGameData(tree);
        app::MatchAudio ma2(tree);
        ma2.setMixer(&m2);
        ma2.boot(game.db.teams[0].id, game.db.teams[26].id,
                 game.db.teams[0].roster, game.db.teams[26].roster);
        CHECK(ma2.chantPlays() == 0 && ma2.chantActive() == 0); // boot catalogs; pump not run yet
        CHECK(ma2.chantLaneCount(0) > 0 && ma2.chantLaneCount(1) > 0 &&
              ma2.chantLaneCount(2) > 0); // id 26/67 both ship C0 banks + CGENER pool
        for (int frame = 0; frame < 1200; ++frame) ma2.pumpChants(); // 12 cadence hits
        CHECK(ma2.chantPlays() >= 1);
        CHECK(ma2.chantActive() >= 1);
        const auto& clip = ma2.lastChantClip();
        CHECK(clip.frames() > 0);
        double acc = 0.0; // decoded chant clip content (SPEECH_PHRASES.md §1 lane) must be loud
        for (const auto s : clip.samples) acc += double(s) * double(s);
        const double rms = clip.samples.empty() ? 0.0
                         : std::sqrt(acc / double(clip.samples.size()));
        CHECK(rms > 10.0);
        CHECK(ma2.chantActive() <= 3); // DAT_006324d0 pool cap
        audio::Mixer m3{kRate};
        app::MatchAudio maSilent(tree);
        maSilent.setMixer(&m3);
        maSilent.boot(game.db.teams[0].id, game.db.teams[26].id,
                      game.db.teams[0].roster, game.db.teams[26].roster);
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 0); // SNDautovol target 0 -> silent
        for (int frame = 0; frame < 400; ++frame) maSilent.pumpChants();
        CHECK(maSilent.chantPlays() >= 1 && maSilent.chantActive() == 0); // requests, no voices
        audio::sceneLevels().setLevel(audio::kSceneCrowd, 80);
    }

    // Request RNG mirror (pass-56): exe random FUN_004ce2f3 is an LCG
    // (state = state*0x41c64e6d + 0x3039, draw = (state >> 16) & 0x7fff). The pump drain
    // branch (FUN_0048da50) consumes draw parity to rotate queue entries between head and
    // tail (qpush FUN_004c4780 vs qbottom FUN_004c4800), not play-vs-stop; the boot
    // classification (FUN_0048dcd8) feeds the same draw into qfetchindex picks.
    // Seed-0 draw sequence verified against the exe LCG arithmetic.
    {
        app::MatchAudio::ExeLcg rng;
        CHECK(rng.draw() == 0);     // state 0x3039 (12345) -> (0x3039>>16) == 0
        CHECK(rng.draw() == 21468); // state 0xD3DC167E -> bits 16..30 = 21468
        CHECK(rng.draw() == 9988);
        CHECK(rng.draw() == 22117);
    }

    // Presentation announcement lane port (pass-77): ring-slot {type,id} -> loader path
    // mirror of FUN_004894ac/FUN_0048953c: type 2 schedule / type 3 standing with the
    // DAT_0057e5f8 selector G_ (home) vs A_ (away); SMP files are single PT v2 play tracks
    // ("PT" magic at file offset 0), team PBKs are phrase banks; the id 0x32f neutral
    // special rewrites to A_FE_USA.SMP and hits the missing-file lane (file not shipped);
    // activation is consume-on-use (FUN_0048d1f8 qremove). G_SCH000.SMP is a genuinely
    // silent asset (its payload is MicroTalk zero-excitation frames repeated over zero
    // padding, measured: decoded samples all zero) - decode-only asserts there; A_STD001
    // carries a real speech payload and is the audibility lane.
    {
        app::Announcer ann(tree);
        audio::sceneLevels().setLevel(audio::kSceneSpeech, 80);
        audio::Mixer m4{kRate};
        ann.setMixer(&m4);
        ann.setSlot(0, 2, 0); // home schedule board announcement 0
        const auto schedPath = ann.loadSlot(0);
        CHECK(schedPath.filename() == "G_SCH000.SMP");
        CHECK(ann.slotLoaded(0));
        CHECK(ann.decoded() == 1);
        CHECK(ann.lastClip().frames() > 0); // silent asset: frames decode, samples are zero
        CHECK(ann.activate(0));             // plays once on the speech lane
        CHECK(!ann.activate(0));            // consume-on-use: second pick is silent
        CHECK(ann.plays() == 1);
        ann.setAwaySelector(true);
        ann.setSlot(1, 3, 1); // away standing board announcement 1 (real speech payload)
        const auto stdPath = ann.loadSlot(1);
        CHECK(stdPath.filename() == "A_STD001.SMP");
        CHECK(ann.slotLoaded(1));
        CHECK(ann.decoded() == 2);
        CHECK(ann.lastClip().frames() > 0);
        CHECK(ann.activate(1));
        CHECK(ann.plays() == 2);
        CHECK(peakRms(m4, 0.6) > 1.0); // decoded announcement audible over loopback
        ann.setNeutralGate(true);
        ann.setSlot(2, 1, app::Announcer::kNeutralId); // 0x32f special
        const auto neutral = ann.loadSlot(2);
        CHECK(neutral.filename() == "A_FE_USA.SMP");
        CHECK(ann.missing() == 1);
        CHECK(ann.slotLoaded(2));
        CHECK(!ann.activate(2)); // silent entry: qfind-null lane, no voice
        CHECK(ann.plays() == 2);
        ann.loadSlot(2); // re-arm test: pending flag cleared -> one counted attempt
        CHECK(ann.loads() == 3);
        CHECK(ann.missing() == 1);
        ann.setAwaySelector(false);        // home side: G_T prefix (DAT_0057e5f8 == 0)
        ann.setSlot(3, 1, 26); // G_T0026.PBK team bank (phrase records, nested BIGF)
        const auto team = ann.loadSlot(3);
        CHECK(team.filename() == "G_T0026.PBK");
        CHECK(ann.slotLoaded(3));
        CHECK(ann.phrasesParsed() > 0);
        CHECK(ann.activate(3)); // first non-empty phrase decoded and played
        CHECK(ann.plays() == 3);
    }

    // pass-79 selector mirror + presentation scene feeds (measured grid, skip sets and
    // remmap table: analysis/pass78grid/grid_dump.txt; AUDIO_RUNTIME.md pass-77b/78).
    {
        app::Announcer sel(tree);
        audio::sceneLevels().setLevel(audio::kSceneSpeech, 80);
        CHECK(sel.remmap(0x56) == 0xa1);  // team 86 -> schedule 161 (slot 0 -> next word)
        CHECK(sel.remmap(0x1a) == 0xbd);  // team 26 -> schedule 189
        CHECK(sel.remmap(0x2c0) == 0xd6); // last scan slot reads the adjacent word
        CHECK(sel.remmap(0x999) == 0xa1); // unmatched team: default word
        sel.resetCursor();
        CHECK(sel.fillSelector(0x26, false)); // grid B [20,25)
        CHECK(sel.selCount() == 4);           // records 20..24 minus type-2 skip 23
        CHECK(sel.pickSelector() == 21);
        CHECK(sel.fillSelector(0x36, false)); // grid B [100,105)
        CHECK(sel.selCount() == 5);           // no type-2 skip inside the range
        CHECK(sel.pickSelector() == 102);     // cursor lane continues across fills
        CHECK(sel.fillSelector(0, true));     // grid A [0,5) standings lane
        CHECK(sel.selCount() == 5);           // no type-3 skip below record 5
        CHECK(sel.pickSelector() == 3);       // cursor 2 into the freshly filled list
        CHECK(!sel.fillSelector(0x1d, false)); // wrap word v[0x1d]: not a selector entry
        sel.resetCursor();
        app::feedTeamSelectionAnnouncer(sel, 0x1a); // FUN_00458cf0 lanes 0x458dc2/0x458df6
        CHECK(sel.slotType(0) == 1 && sel.slotId(0) == 0x1a); // G_T0026.PBK team bank
        CHECK(sel.slotType(1) == 3 && sel.slotId(1) == 1);    // G_STD001.SMP real speech
        CHECK(sel.loads() == 2 && sel.missing() == 0);
        CHECK(sel.plays() == 1); // one announcement per pick call, never stacked
        CHECK(sel.pickNext());   // slot 1 std lane plays on the next pick
        CHECK(sel.plays() == 2);
        CHECK(!sel.pickNext());  // queue drained: qfind-null lane
        sel.resetCursor(); // exe cursor DAT_0062fd9c is global scratch; reset for the board lane
        app::feedBoardAnnouncer(sel, 0x1a); // FUN_00453a6c family lanes
        CHECK(sel.slotType(2) == 2 && sel.slotId(2) == 0);    // 0x453b15 silent schedule 000
        CHECK(sel.slotType(3) == 2 && sel.slotId(3) == 0xbd); // 0x453b2e remmap -> G_SCH189
        CHECK(sel.slotType(1) == 2 && sel.slotId(1) == 21);   // 0x453bd2 selector 0x26
        CHECK(sel.slotType(4) == 2 && sel.slotId(4) == 102);  // 0x453c77 selector 0x36
        CHECK(sel.slotType(0) == 1 && sel.slotId(0) == 0x1a); // 0x453b07 team lane
        CHECK(sel.loads() == 7 && sel.missing() == 0);        // all five files shipped
        CHECK(sel.plays() == 3); // the feed itself picks exactly one entry
        for (int i = 0; i < 4; ++i) CHECK(sel.pickNext()); // the remaining four announcements
        CHECK(sel.plays() == 7); // every entry builds a clip (000 counts: zero samples)
        CHECK(!sel.pickNext());
    }

    // Speech master-gain ramp mirror (pass-73): SNDspeechinit (FUN_004df820) stores
    // cur = vol<<16 with delta 0; SNDspeechautovol (FUN_004df8a4) sets delta =
    // (target<<16 - cur)/frames (frames clamped >=1); the state-cb pump (installed at
    // 0x4dfca0) adds delta per frame and clamps at target in both directions, zeroing
    // delta on clamp. Applied volume = cur>>16 into the SNDvol [0,0x7f] domain.
    {
        app::SpeechGain g;
        g.set(80);
        CHECK(g.applied() == 80);
        g.ramp(1, 3);        // delta = (0x10000 - 0x500000)/3 = -1725781 (C truncation)
        g.tick(); CHECK(g.applied() == 53); // 0x35A8AB >> 16
        g.tick(); CHECK(g.applied() == 27); // 0x1B5556 >> 16
        g.tick(); CHECK(g.applied() == 1);  // 0x10001 >> 16, still above target
        g.tick(); CHECK(g.applied() == 1);  // cur <= target: clamp, delta zeroed
        g.ramp(0x7f, 1);
        g.tick();
        CHECK(g.applied() == 0x7f);         // rising direction clamps in one frame
    }

    // (g) Crowd reaction envelope (pass-55): weights from the card header byte 7 (CROWD 70 ->
    // be 358 / bc 154, exe FUN_004e4ba0), fresh machine indexes the curve at (358*358)>>9 = 250,
    // the idle pump walk (FUN_004e510c) breathes the blend index inside the blend formula's
    // algebraic band [250, 404] (level range [0, 0x1ff]), and SNDcrowdpulse (FUN_004e4cac) ramps
    // the 16.16 level toward the card record pulse target, clamped at the record ceiling.
    // TRAFF weights byte 7 = 4 -> be 20/bc 492.
    {
        app::CrowdEnvelope env;
        env.init(0x46, 132);
        CHECK(env.be() == 358 && env.bc() == 154);
        CHECK(env.blendIndex() == 250); // fresh machine: level 0
        for (int i = 0; i < 4000; ++i) env.tick(); // long idle walk (~67 s at 60 fps)
        CHECK(env.blendIndex() >= 250 && env.blendIndex() <= 404); // [be^2>>9, (be^2+bc*511)>>9]
        CHECK(env.level() >= 0 && env.level() <= (0x1ff << 16)); // target clamp [0, 0x1ff]
        // Card record 0 (CROWD.CRD bytes 0x28..0x37 measured dump): delta 350, ceiling 450
        // (u16 0x01c2 at bytes 6..7), ramp step 0x38000 = 3.5 in 16.16, idle rate 0x40000
        // (u32 at bytes 12..15) -> 132 ramp frames at 60 fps. Record 1: delta 250, ceiling 450,
        // step 0x28000 = 2.5, rate 0x60000 -> 198 frames.
        const auto crowdBytes = readFile(tree / "AUDIO" / "CRWD" / "CROWD.CRD");
        const formats::Crd card = formats::Crd::parse(std::span<const std::uint8_t>(crowdBytes));
        CHECK(card.records.size() >= 2);
        CHECK((card.records[0].values ==
              std::array<std::uint16_t, 6>{350, 450, 0x8000, 0x0003, 0x0000, 0x0004}));
        CHECK((card.records[1].values ==
              std::array<std::uint16_t, 6>{250, 450, 0x8000, 0x0002, 0x0000, 0x0006}));
        CHECK(card.records[0].rate() == 0x40000 && card.records[1].rate() == 0x60000); // idle ramp
        CHECK(card.records[0].step() == 0x38000 && card.records[1].step() == 0x28000);  // ramp step
        const formats::CrdRecord rec = card.records[0];
        CHECK(rec.step() == 0x38000 && rec.rate() == 0x40000); // rate * 0x21 >> 16 == 132 frames
        app::CrowdEnvelope pe;
        pe.init(0x46, 132);
        CHECK(pe.pulse(rec));                                   // fresh level 0 < ceiling: applied
        for (int i = 0; i < 100; ++i) pe.tick();                // 100 x step 0x38000 = 350<<16
        CHECK(pe.level() == 350 << 16);                         // exact pulse arrival (0 + delta)
        CHECK(pe.pulse(rec));                                   // ramp on up to the ceiling
        // pulse arrival at the ceiling, then the idle pump resumes the random walk: tick only
        // up to the arrival tick so the ceiling clamp is checked while the pulse is still live.
        for (int i = 0; i < 200 && pe.level() < (450 << 16); ++i) pe.tick();
        CHECK(pe.level() == 450 << 16);                         // ceiling clamp (values[1] = 0x1c2)
        CHECK(!pe.pulse(rec));                                  // at ceiling: exe status -8 refusal
        app::CrowdEnvelope trafEnv;
        trafEnv.init(0x04, 132);
        CHECK(trafEnv.be() == 20 && trafEnv.bc() == 492);       // TRAFF byte 7 = 4
    }

    std::printf("test_gameaudio: OK\n");
    return 0;
}
