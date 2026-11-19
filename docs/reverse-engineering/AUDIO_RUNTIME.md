# Audio runtime — streaming model of FIFAWC.EXE (measured 2026-11-01)

Consolidates the audio data-path facts recovered across Ghidra passes and the 2026-11-01
speech-decode session. Companion docs: [GHIDRA_PASS2_AUDIO.md](GHIDRA_PASS2_AUDIO.md)
(now carries a correction banner), [AUDIO.md](../asset-formats/AUDIO.md) (container/codec
formats). Raw decompiles live outside the repo under `analysis/pass3/` (non-distributed);
this doc records addresses and structure only. All `FUN_…`/`DAT_…` names are Ghidra defaults.

## 1. PE layout of FIFAWC.EXE (measured from the section table)

| Section | VA | Raw offset | Size |
|---|---|---|---|
| imagebase | `0x400000` | — | — |
| AUTO | `0x401000` | `0x400` | `0x157000` |
| .idata | `0x558000` | `0x157400` | `0x1200` |
| DGROUP | `0x55a000` | `0x158600` | rawsize `0x3d600` (**VirtualSize = 0** in the optional header) |
| .bss | `0x598000` | — | — |
| .reloc | `0x676000` | — | — |

## 2. Ghidra pitfall: DGROUP `VirtualSize = 0`

Because the optional header declares DGROUP's VirtualSize as 0, Ghidra treats the whole
section (VA `0x55a000`–`0x597600`, initialized file bytes at raw `0x158600`+) as
**uninitialized**, so `Memory.findBytes` skips it — string searches silently miss every
string in DGROUP, and "no xrefs found" conclusions drawn that way are false negatives.

Recipe: resolve strings in this range via **explicit VAs**, not memory scans. Worked example:
the source-path string `cmn\sspeech.c` sits at VA `0x56ae14`, the `SNDspeechinit` message
strings around `0x56ae24`+; a pointer scan over initialized bytes found speech-module
pointer users clustered in the `FUN_004df8xx`–`FUN_004e6xxx` region.

## 3. cmn_stream reader: `'lECs'` chunk framing

Region `0x42bb54–0x42d0bb` is the EA **cmn_stream** reader — NOT a MicroTalk CELP decoder
(the pass-2 claim; see the correction banner in GHIDRA_PASS2_AUDIO.md). Identifying features:

- Chunk magic `'lECs'`: byteswapped compare against `0x5343456c` (i.e. `ScEl`/`lECs` record
  framing of the EA audio library, cf. debug blob `D:\audio\lib\win\sdirect.c`).
- Ring refill loop feeding fixed-size slots.
- Codec dispatch at `stream+0x94`, case ids `0xd`, `0x14`, `0x11`, `0x12`, `0x13`.

## 4. Streaming data path (function map)

| Function | Role |
|---|---|
| `FUN_0042e368` | **cmn_stream pump**: parses `.SMP`-style `[u32 BE len][payload]` chunk records into a ring of 16 × 512-byte slots at base `&DAT_005ea540`; slot layout: `+0x0` buffer pointer, `+0xc` read position, `+0x10` total |
| `FUN_0042e290` | FIFO consumer of the ring slots |
| `FUN_0042d5a4` | output buffer set |
| `FUN_00432918` | feed gate — decompiles to a bool "can feed" check: chunk-framed mode iff `DAT_005ff3d4 == 3`; active-voice count from bitmask `DAT_005ff420` via `FUN_0043376c` (popcount) |
| `FUN_004c4af0` / `FUN_004c4890` / `FUN_004c49c0` | cmn_queue length / pop / insert |
| `FUN_0049a900` | allocator used by the queue/stream layer |
| `FUN_00431494` | iSNDserve entry: pops the cmn_queue and sets the stream flag byte at `+0xee` (Run24 report, `analysis/pass3/FIFAWC.EXE_speech.txt`) |
| `FUN_004314d8` | serve loop; checks field `+0x20` before servicing a stream |

Model: speech/SMP data reaches the mixer as chunk-framed records through cmn_stream
(ring of 512-byte slots), streams are queued per voice (cmn_queue + serve entry/loop), and
the feed gate throttles refill by mode byte + active-voice bitmask. Decoding itself is the
`PT`-tagged payload path documented in [AUDIO.md](../asset-formats/AUDIO.md) — no codec
tables were located at, or needed from, the pass-2 claimed VAs: speech decodes fully with
those VAs unmapped and unloaded (measured 2026-11-01).

## 5. Consequences for the port

- `.PBK`/`.SMP` playback needs only the file-level model (PT play track at member offset 20,
  codec tag routing) — not a bit-exact clone of this runtime pump — for offline decode.
- If streaming behavior is reproduced (voice counts, feed gating), use the table above as
  the semantic reference.

## 6. Bank load + sound select (pass 8, measured 2026-11-06)

Raw decompiles: `analysis/pass8/audio1_strings.txt`, `audio2_select.txt`, `audio3_play.txt`
(byte-scan of initialized memory incl. DGROUP VAs; xrefs; targeted decompiles).

**CONFIRMED — path strings (DGROUP).** `AUDIO\MUSC\` + glob `FE*.ASF` / `BE*.ASF` @0x566b3e;
`AUDIO\SFXS\%s` + `game00.bnk`/`game01.bnk`/`plrs00.bnk`/`plrs01.bnk` @0x566b5a..0x566b9c;
`AUDIO\CRWD\%s` + `CMIDI.BNK`/`SNDCRD`/`TRAFF.BNK`/`TRAFF.CRD`/`CROWD.BNK`/`CROWD.CRD`
@0x566ae1..0x566b29; `AUDIO\ANTH\ANTH%04d.BNK` + `ANTHEM.EAM` @0x56735f.

**CONFIRMED — music queue.** `FUN_0048ed9c` (callers: FE frame drivers `FUN_0043ccac`,
`FUN_0047a8f4`, init `FUN_00490728`) builds the two track lists at `DAT_00632654` (FE) /
`DAT_00632670` (BE); the active list pointer `DAT_0063268c` is chosen by mode:
`FUN_00446624()==2` -> BE list (pause/behind-the-scenes screens), else FE list. Per-frame pump
`FUN_0048eb98` (14 callers in the `0x439xxx`/`FUN_0047a8f4` FE driver region) pops queue entries
(`FUN_004c4890` cmn_queue pop + stream setup), i.e. menu music = sequential .ASF streams from
the glob list, not a fixed track. Slot ids inside the stream layer are codec-level, not per-file.

**CONFIRMED — SFX bank owner.** loader `FUN_0048eff0` (xref 0x48f007) is called twice by
`FUN_0048f1c4` (callers: init `FUN_00409ef4`, audio-init `FUN_00490728`) -> handle `DAT_006326ac`;
user `FUN_0048f228` shuffles two bank handles (`DAT_0063269c`/`DAT_00632698`, game00/game01 pair).
Crowd banks: loader `FUN_0048e78c` (caller init `FUN_00409ef4`) loads CROWD.BNK -> `DAT_006325c0`
and the CROWD.CRD card -> `DAT_006325c4` (`FUN_0048e190` = AUDIO\CRWD\%s open).

**CONFIRMED — whistle trigger chain.** `FUN_004bedb0` (caller `FUN_004b5290`; gates:
`DAT_00641270==1`, scene `DAT_0064015c==9`, distance `|DAT_00646924| <= DAT_00644934`) ->
`FUN_0051d480` (33 callers at match-event sites 0x4ae8xx/0x4b8xxx/0x505xxx): plays sound index
byte `(&DAT_0058f3b2)[sceneId*4]` through the SNDplay pointer vector - **slot ids are table-
driven per scene, not immediates**, so exact slot numbers stay unresolved statically.

**Hypothesis (acoustic verification) — slot -> SFX.** All bank slots decoded to WAV and measured
(length/RMS/spectral centroid/zero-cross rate): GAME00.BNK #81 = 0.17 s bright mono chirp at
22.05 kHz (whistle), #23 = 0.24 s low-ZCR bass thud (ball strike); CROWD.BNK #2/#3 identical pair
= 3.03 s continuous murmur (loop material). Port mapping: FE flow -> first `FE_*.ASF` looped
stream; live match -> CROWD.BNK #2 loop on the idx2 `SNDstadiumreactions` lane (pass-49: the
options reaction row fades the running voice via `MatchAudio::relevelCrowd`), GAME00 #81 chirp
on KickOff + Foul [H], #85 1.95 s bright trill on PeriodEnd/MatchEnd, #45 4.12 s loud-noise
burst as the goal crowd swell [H, profile-selected — slot roles beyond 81/23 are acoustic
inference], #23 on Shot (`app/game_audio.{hpp,cpp}`, gate `test_gameaudio` (a)-(d)). Slot
inventory measured in full (`analysis/pass49audio/GAME00_slots.txt`): bank slots 0..35/42/44/
45/46 + 80..85, 36-41/43 empty; CROWD.BNK carries only slots 0..3, so per-event crowd "chorus"
slots do not exist. Crowd chant layering (CROWD.CRD cards + CMIDI.BNK MIDx streams) and
proximity gain ramps remain unimplemented.

**Pass 12 addendum — FE menus never call SNDplay [C]** (evidence `analysis/pass12/sfx_fe.txt`, raw streams `nav12/13/14_raw.txt`): a full E8-scan of all `FUN_0051d480` (SNDplay) call sites finds **42 sites, all inside live-match code** (dispatcher `FUN_004ae730`, ids 0x1f9/0x1fa/0x1fb; sites 0x4ae7xx/0x4b8xxx/0x505xxx–0x52xxxx) and **zero** in FE scene handlers, the hub, or the widget path. The per-scene slot table `DAT_0058f3b2` is read-only at exactly 5 sites (0x51d419/46c/600/645, 0x51d7bb); it is built at runtime by the SND bank subsystem near `FUN_0x4ac862` ("sfx tbl" VA 0x568d64; per-scene bank format `"audsfx.%c"` VA 0x568dd7). Menu "sound feedback" in the original = (a) FE music streams (§music queue above), (b) scene ambient banks, and (c) an audio **ramp**: feflow04 tag `"  5"` registers animation/timer callbacks (`FUN_00436180` → `DAT_00600688` pump `FUN_004361d0`, countdown → ramp `FUN_0048ec64(EAX=0xc8)` + callback; selector dispatch `FUN_004447dc` switches `tmflag`/`mesg`/`feflow10`). There are **no per-button click sounds** in the FE. The LAY `nosfx=1` key exists only on FEFLOW82.LAY objecttype=44 label widgets (10 hits); no `nosfx` literal appears in any shipped binary → class-default suppression [H]. Port rule: do NOT synthesize menu click SFX; wire FE music + ambient bank, ramp at scene switches.

**Pass-51 — match-boot lane installs [C]** (evidence `analysis/pass16audio/NOTES.md` +
`/tmp/wc98pass51/lane_decomp.txt`, string/xref table `analysis/pass8/audio1_strings.txt`).
The audio subsystem boot builder `FUN_00490728` (gated `DAT_00632774`, 24-iteration loop +
lane-loader sequence `FUN_0048e8e8/0048d378/0048ed9c/00490acc/00490250/0048fd54/0048dfc4/0048f1c4`;
teardown `FUN_004907c0` calls the matching drain set) is the authority for what the match scene
loads. Per-lane installs decoded:

- **Chant pool**: `FUN_0048dfc4` allocates a 113-entry pool (`DAT_006324c0`, loop `iVar1 < 0x71`)
  + a 3-entry side pool (`DAT_006324d0`). Names come from the exe's own pointer table at VA
  0x57eb5c (`PTR_s_C0001800_PBK`): 74 team-keyed entries `C0%04d` digits = match-record
  nationality id `+0x32` (pass-53 correction: boot classification `FUN_0048dcd8` builds the
  names with format `C0%04d` @0x566a88 from `DAT_00641014/0x641110 + 0x32` — direct ids, the
  earlier `team*10` claim was wrong) + `%02d` variant (18, 26, 29, 31, 33, 35, 36, 37, 38, 41,
  47 with six variants, 49, 52, ... legend ids 862..875),
  all present on the CD (cross-check: 0 missing, 0 extra in the table). The remaining 39 CD files
  are the `CGENER%02d` generic-chant lane. Per-chant loader `FUN_0048d53c`: `AUDIO\CHNT\%s`
  (@0x566a78) with `table[req+4]` name index — the chant index is a request argument, not a
  filename computation.
- **Announcer banks (match teams)**: `FUN_0048fa08` loads `AUDIO\TEAM\A_T%04d.PBK` (@0x566bac)
  once per match team, u16 ids read from the match record slots `DAT_00641014/0x641110` +0x32/+0x34,
  4 pool slots per team. The request handler `FUN_0048953c` switches per message class:
  `AUDIO\team\A_T%04d.pbk` (@0x566974) vs `G_T%04d.pbk` (@0x56698c) on `DAT_0057e5f8`, a hardcoded
  `AUDIO\team\A_FE_USA.SMP` neutral fallback (case 0x32f, `DAT_00640fe8==1`), and tournament
  schedule/standing requests `AUDIO\pbp\schedule\A/G_sch%03d.smp` + `AUDIO\pbp\standing\A/G_std%03d.smp`.
- **Player-name banks (squads)**: `FUN_0048fb10` walks both team squads (2 records, stride 0xfc,
  17 slot entries at 0x66, 6-byte records; id u16 at +2, shirt byte at +0xF) and loads
  `AUDIO\PLRS\P%05d.PBK` (@0x566bc4) per non-zero id. The CD ships 1020 `Pxxxxx.PBK` banks; FCDB
  roster ids resolve 19/22 for teams 26/67 (reserve players have no bank); each bank parses with
  1..2 name phrases.
- **Anthem lane**: `AUDIO\ANTH\ANTH%04d.BNK` (@0x56735f) xref 0x490b0a in `FUN_00490acc` (boot
  sequence member); `ANTHEM.EAM` MIDx sits in the same string block. Slot 0 of ANTH banks = 23.8 s
  nation chant sample (measured).
- **Stadium pair**: `FUN_0048e78c` loads the CROWD container + card (`CROWD.BNK`/`CROWD.CRD`
  strings @0x566b08/0x566b14; `FUN_0048e190` = `AUDIO\CRWD\%s`), then `FUN_004e3128`/`FUN_004e4a80`
  start it (`DAT_006325b0` gate); the traffic hum pair is `TRAFF.BNK`/`TRAFF.CRD` in the same
  string block.
Port side (pass-51): `MatchAudio::boot` mirrors this install set — percent idx2 notifier, CROWD.CRD
card as bed authority, TRAFF loop, ANTH slot-0 one-shot, and decode-counted catalogs for the chant
pool, announcer banks and squad PLRS banks (chant playback wired by pass-53 below; announcer and
name-phrase playback stays disabled until the request-handler trigger mapping is decoded; see
AUDIO_DISCREPANCIES A3/A6/A7).
Gates: `test_gameaudio` (e), selftest `match-audio-lanes` (chants=113, announcers=4, plrs=38).

**Pass-53 — chant pump + boot classification [C]** (evidence `analysis/pass53audio/NOTES.md`
+ `ch1.txt`..`ch7.txt`, flat-bank probe `chantprobe.cpp`). The pump `FUN_0048da50` runs
per-frame from the commentary tick host `FUN_004906f8` (sole caller 0x49070e) under gate
`DAT_006324b4`: each frame stamps the lane voice delay `+10` and applies
`FUN_004906d0`/`SNDautovol` when it resolves; the load cadence `DAT_0057eb40 <=
DAT_00636a14 ⇒ +100` drains queues, releases one `+8`-lane voice (`DAT_006324d4[lane]--`)
and tops up lanes 0..2 iff `DAT_006324c4[lane] != 0 && DAT_006324d4[lane] <
DAT_006324e0[lane]` while the match scene is live (`FUN_004adb30`) and the request count is
`< 3`, requesting through loader `FUN_0048d53c`. Boot classification `FUN_0048dcd8` stamps
node `+8` lanes from the team availability flags `DAT_006324b8/bc` (membership test
`FUN_00446a94` over the u16 vector at `DAT_005883d8`, setter `FUN_0048dc8c`): neither team
present ⇒ every node `-1` (pool silent), same-nation match (`local_20`, ids at record `+0x32`)
⇒ lane 2, otherwise home lane 0 / away lane 1, generic draws lane 2; capacity `DAT_006324e0`
bumps to 1 per lane on first assignment with budget `DAT_006324e8` decrementing. Measured flat
banks: retail `CHNT/*.PBK` are single-phrase flat members (C0002600.PBK: one phrase, one
record 'a', `decodePlayTrack(m,20)` → 43008 frames; offset 0 → 0) — the pass-52 flat skip was
wrong and `decodeChantClip` now decodes flat members at the record boundary. Port:
`classifyChants` mirrors the lane table, `pumpChants` mirrors cadence/retire/top-up, wired
per-frame in `livematch.cpp`; gate detail `match-audio-lanes` now reports
`chantPump plays/active/clip` and `test_gameaudio` (f) asserts lanes + audible clip.
