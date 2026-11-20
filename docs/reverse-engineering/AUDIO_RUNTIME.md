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
slots do not exist. Crowd chant layering (CROWD.CRD cards) and proximity gain ramps
were implemented after this note (pass-54/55: card-slot bed layers + curve gains +
`CrowdEnvelope` blend + `crowdPulse`/`relevelCrowd` in `MatchAudio`); the CMIDI.BNK
MIDI-director stream lane stays device-blocked (A5).

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
AUDIO_DISCREPANCIES A3/A6/A7). Pass-54: the card slot list opens as four simultaneous bed layers,
each bound to its own gain curve by the measured `0x128` slot-pointer fixup (`docs/formats/CRD.md`).
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

**Pass-55 — crowd reaction envelope machine [C]** (evidence `analysis/pass55crowd/NOTES.md`).
The event-envelope blend residual from pass-54 is decoded: the `cmn\scrowd.c` reaction machine
shares one 16.16 level across the CROWD/TRAFF cards. Install `FUN_004e4ba0` derives blend
weights from the card header byte 7 (`be = byte7 << 9 / 100`, `bc = 0x200 - be`; CROWD 70 ->
358/154, TRAFF 4 -> 20/492). The idle pump `FUN_004e510c` walks the level through alternating
ramp states (`DAT_005894ab` 0 up/1 down/2 up/3 down, then 2<->3 forever), drawing a random
target clamped [0,0x1ff] on every ramp arrival (pass-56: the draw is time-based
`((tick & 0x7fff) - 0x4000) * secondTick >> 14`, `FUN_0052f3e0`, not the request LCG) and
dividing the distance by a card-record ramp rate (`rate * 0x21 >> 16`: record 0 -> 132 frames, record 1 -> 198). `SNDcrowdpulse`
(`FUN_004e4cac`) reads record delta/ceiling/step (u16 pair at record bytes 4..7: measured delta
350, ceiling **450** = 0x01c2 — an earlier gate note said 394, decimal arithmetic error) and
refuses with status -8 when the level already sits at/above the ceiling, else ramps to
`level + delta` clamped at the ceiling. The blend consumer `FUN_005444` computes
`cc = (be*be + bc*(c0>>16)) >> 9` every frame and indexes the card curve tables at `cc >> 16`;
for CROWD algebra this band is [250, 404], so the bed index breathes instead of sitting at one
fixed level (closing the A4 fixed-bed discrepancy). Port `CrowdEnvelope` mirrors the machine
exactly; `tickCrowd()` pumps it per frame in `livematch.cpp` and re-fades the layer voices only
when the curve index moves. Gates: `test_gameaudio` (g) card-truth record CHECKs + pulse
arrival/clamp/refusal; selftest `match-crowd-envelope` start 250 / walk band / pulse ramp +3.5.

**Pass-56 — chant drain rotation, request LCG, scene-10 CROWD phrase lane [C]** (evidence
`analysis/pass56crowd/NOTES.md` + `/tmp/wc98pass56/dr1..dr12.txt`, `s10.txt`, `cr*.txt`).
The pump `FUN_0048da50` drain branch (`0x48dbb6..0x48dc82` disassembly) is queue rotation, not
play-vs-stop: when `qlength(DAT_00632524) < 3` it walks `qlength(DAT_00632508)/2` steps (Watcom
callee-saved `ECX` carries the queue base across the call sites), each step drawing the request
LCG `FUN_004ce2f3` (`state = state*0x41c64e6d + 0x3039`, draw `(state >> 16) & 0x7fff`, verified
from the seed-0 sequence 0, 21468, 9988, 22117), reducing it mod length (`IDIV` at 0x48dbf5) and
fetching through `FUN_004c4f30` (`qfetchindex`); the fetched entry is then re-added by parity —
even to `qbottom` (`FUN_004c4800`, tail), odd to `qpush` (`FUN_004c4780`, head), per the
`cmn\queue.c` helper strings (`s_qpush` 0x569560, `s_qbottom` 0x569568). The pass-53 residual
"play-vs-stop random branch" claim is retracted — the branch only reorders the pool, and the
reorder changes which lane candidates `FUN_0048d53c` picks first. `FUN_004ce317` is the seed
setter (wrapper at `PTR_FUN_005940d4` + 0xc); boot classifier `FUN_0048dcd8` seeds it then feeds
LCG draws into `qfetchindex` picks over the 113 chant packs (lane table `DAT_006324c4/d4/e0`,
budget `DAT_006324e8 = 3`, same-team flag at record `+0x32`); the seed source is decoded
(pass-68): `FUN_004ce317` is called with `EAX = DAT_00636a14`, the **multimedia timer tick
counter** (`inittimer` `FUN_00496bb0` zeroes it at `0x496be6`, the timer callback tail
`0x4966b8` does `INC dword [DAT_00636a14]` per multimedia-timer event alongside the
`DAT_00636a10` full counter). Audio boots seed the LCG from the current tick count:
classifier `FUN_0048dcd8` site `0x48dd97`, crowd boot `FUN_0048ed9c` site `0x48ee08` —
uptime-nondeterministic, so `ExeLcg` seeds stay port-side constants [H]. The secondary rotation consumer `FUN_0048e9a0` (gate `DAT_0063268c != 0`) repeats
qfetchindex+qpush. The idle envelope target draw (pass-55 machine) is NOT the LCG:
`FUN_0052f3e0` computes `((tick & 0x7fff) - 0x4000) * secondTick >> 14` from the 64-bit tick
counter `FUN_004c321c` (`DAT_00585cc0/5cc4/5cc8/5ccc`, carry `DAT_00585cbc`), so
`CrowdEnvelope`'s mt19937 stays a [H] approximation. Scene-10 CROWD phrase lane `FUN_0048e6d0` (caller 0x48e83f, once-guard
`DAT_006325b4`): `CMIDI.BNK` (@0x566adc) → `DAT_006325cc`; `FUN_004e3128`/`FUN_004e3724` init
`DAT_006325d0`; `FUN_0048e280` loads exactly two banks — `CROWD.PBK` → `DAT_006325d4`,
`COMOS.PBK` → `DAT_006325d8` — through the `AUDIO\CRWD\%s` format (`FUN_0048e190` @0x566acc) +
loader `FUN_00439810` with type arg `FUN_0048e8d0` (`DAT_00632638`, fallback 2 when
`DAT_00632634 == 0`); three queues spawn at `DAT_006325e0/5fc/618`; the phrase count of
`CROWD.PBK` (`FUN_004a49a0`) sizes a `SNDCRD` (@0x566ae8) reserve (`FUN_0049f5a0` →
`DAT_006325dc`) of `count*12` bytes and `count` stride-12 records are pushed into
`DAT_006325e0` via `qbottom`; `FUN_0048e1c4` stamps phrase→MIDI ids at record `+6`
(`FUN_004e43c0`, sentinel `EDX = 0xffffffff`, `EBX = entry+0x14`). The `AUDIO/CRWD/CROWD` file
is a plain `dir` listing, not data. `DAT_0057eb44` is the CRWD loader field block (pass-59
byte scan): `+0`/`+4` hold the two loader name pointers `CROWD.PBK`/`COMOS.PBK`;
`+8..+0x14` are runtime state fields, not names — slot `+8` is boot-lane scratch that
`FUN_0048ed9c` consumes (zeroed after two `FUN_0048ea3c` releases), `FUN_0048f068` writes a
replay schedule time `DAT_00636a14 + 0x14` to `+0x0C`, and write site `0x48f321` inside
`FUN_0048f2d8` stores a `FUN_0048d070`/`FUN_0048f290` result at `+0x10`; the loader loop
(`XOR EDX / ADD EDX,4 / CMP EDX,0x8`) iterates only the two name entries, so these fields
never load a bank. The chant registry `DAT_0057eb5c` (= `DAT_0057eb44 + 0x18`) is exactly 113
name pointers: 74 `C0%04d` team banks + 39 `CGENER%02d` generic banks (boot classifier 0x71
count), terminated by a pointer to `$FIFAVER$AUDIOV0-5-9` at `0x57ecc0` and followed by a
separate pointer array whose tail entries are `.DCT` video names (video subsystem registry,
not chants). Port: `MatchAudio::ExeLcg` mirrors `FUN_004ce2f3` (`chantRng_` replaces mt19937 for
variant + phrase picks) and `pumpChants` rotates the pool on the 100-frame cadence (even →
front, odd → back) before lane top-up, matching drain-branch order; gate `test_gameaudio`
asserts the seed-0 LCG draw sequence.

## 7. Voice/speech pumps + boot registry (pass 57, measured 2026-11-19)

Evidence: `/tmp/wc98pass57/an1..an9.txt` (Ghidra decompile records),
`/tmp/wc98pass56/cr5.txt` (disassembly `FUN_0048e280`/`FUN_0048e190` + caller region),
`analysis/pass57speech/NOTES.md`.

State helpers (`cmn\sspeech.c`, `cmn\sspeech_c` string pool @0x56ae14, error-line tags
`SNDsentencestatus` 0x3b7 / `SNDsentencestop` 0x34d / `SNDphrasestatus` 0x219):

- `FUN_004dfc0c` `SNDsentencestatus`: engine object `DAT_006597b0 == 0` or sentence engine
  `DAT_006597e0 == 0` -> `-14` (`SPEECH_NOT_INIT`); sentence flag `DAT_0064b15a == 0` ->
  `2` idle, else `3` speaking. The flag's only writers are inside the engine itself
  (raw-code scan of immediate `0x64b15a`: sites `0x4dfb59/0x4dfbea/0x4dfc24/0x4dfcb5/
  0x4dfe08/0x4dfe1f`): `FUN_004dfb40` `SNDsentencestart` sets it to 1, `SNDsentencestop`
  clears it (`0x4dfe08`) and `FUN_004dfe18` (reset, called from `SPEECHINIT` `FUN_004df820`
  site `0x4df837` and stop site `0x4dfbfd`) zeroes the whole byte block `0x64b158..15b`.
  There is no external setter: the game side flips it only by calling the API —
  `SNDsentencestart` call sites `0x48f4ea`, `0x48f77d` (speech-pump glue) and `0x489898`
  (FE trigger). Earlier "setter unrecovered [H]" residual closed (pass-67).
- `FUN_004dfb94` `SNDsentencestop`: clears the sentence flag + queue state, stops in-flight
  sentences through the fan-out `FUN_004e2c60` (twice), resets the channel table
  (`&DAT_0064b070` stride 0xc).
- `FUN_004dfa60` `SNDphrasestatus`: channel state `-100` -> `8` free, play-status `1` -> `7`
  complete, `0` -> `3` playing, else `-1`. The port mirrors these codes in
  `audio::SpeechQueue` (`kStatusFree/Complete/Playing/Busy`, `kSentenceIdle/Speaking`).

Match frame loop `FUN_00409f78` runs the per-frame trio while the livematch scene is live
(`DAT_00640fb4 == 0xb`): speech async-load pump `FUN_004901e0` (pass-64 retraction: NOT a
sentence queue — it drains the STRASYNCH async load-completion queue `DAT_00632758` and runs
each record's `+0x10` load-complete callback, §11), voice request drain `FUN_0048fcc0` (status `3`
-> `sentenceStop`; then releases stamped records through `FUN_0048d0c0` -> `FUN_004e3330`
and requeues), and crowd layer pump `FUN_0048e650` (per-record `FUN_004e48c4` update).
The tick host `FUN_004906f8` additionally runs `FUN_0048f920`: gate speech boot
`DAT_006326b0`; drain queue records whose `SNDphrasestatus != 7` back via `qpush` (break on
first incomplete), release complete records (slot free `FUN_004e3330`, stamp `+0x24` with the
current tick), then for the head record latch `_DAT_006326bc = u16@phraseData+0x12` and play
through the record callback `+0x1c`.

Boot registry (pass-57): every bank open from the PLRS/TEAM/chant installers flows through
`FUN_0048d198` (format path + loader `FUN_00439810`) into `FUN_0048d114`, which — only
while the preload gate `DAT_0063241c` is set (enable `FUN_0048d378`, disable `FUN_0048d3f4`) —
registers each phrase (`FUN_004a4a00` count/entries) into the phrase queue via `qbottom`
with self-links at record `+0x10/+0xc`. Registration count == parse count. The gameplay
event -> name linkage is decoded (pass-66): there is **no runtime name-id feeder** —
the boot installer `FUN_0048fb10` itself picks the ids from the FCDB pointer table
`DAT_0064101c` (u32 stride-4 record pointers; name id = u16 at record `+2`, byte at
`+0xF` stored to queue record `+4`), formats `AUDIO\PLRS\P%05d.PBK` as the sole
formatter (only static reference 0x48fc19), and `FUN_0048d198` loads every non-zero
id once at boot; live speech reaches the banks through the trigger wrapper
`FUN_004893ec` (match-code sites 0x452af0..0x454494 group) + request drain
`FUN_0048fcc0` and the phrase pump `FUN_0048f920`. The port catalogs + registry-counts
(`MatchAudio::nameSlotsRegistered`) but does not play name phrases. Voice engine boot
`FUN_0048fd54`: audio-engine gate + `DAT_006326b0 == 0`, opens the
speech device (`FUN_004df820`), reserves the 8-slot record pool (stride `0x2c`, phrase data
region stamped at record `+0x28`) through `reservememadr` `FUN_0049f5a0`, and prepushes the
slots via `qbottom`.

## 8. TLV sample-format tag map (pass 58, measured 2026-11-19)

`FUN_0052d290` = `iSNDplatformplay` (`win_sdriver.c`, log strings `s_iSNDplatformplay_*`
@0x56e140..0x56e230). TLV loop `(u8 tag, u8 len, BE value)` with `0xFF/0xFE` terminators,
`0xfc` value-less records, `len == 0xFF` extended via `FUN_0052e160`. Parsed fields go into
the per-stream format block at `DAT_00659e84 + slot*0x10`:

| tag | field | slot | notes |
|-----|-------|------|-------|
| 0x81 | rate mode `local_18` | `+3` u8 | whitelist `(0x10,8)` at the play gate; default `0x10` |
| 0x82 | channels `local_44` | `+4` u8 | default 1, whitelist {1,2}; `!= 1` in driver-1 mode logs `DOLBY_PRO LOG` (0x4f4) |
| 0x83 | codec `local_20` | `+5` u8 | driver-3 mode requires `codec == 0` else `DIRECTSOUND_3` (0x50e) |
| 0x84 | sample rate `uVar2` | `+2` u16 | default `0x5622` = 22050 Hz |
| 0x85 | play arg `local_24` | `+6` u32 | passed to `FUN_005483bc` (num samples per `schl.cpp` observation) |
| 0x86/0x87 | loop `iVar7/local_1c` | `+10/+14` | sentinel `-1`; forced `-1` when driver == 2 |
| 0x8A/0x93 | payload/meta pointers | — | `0x93` set feeds the driver-3 float conversion `FUN_004973b0` + `(*DAT_00659e84)` callback |
| 0x91 | playback percent `iVar9` | — | `+ _DAT_00659e64`, clamped `[0,100]`, play-call argument; observed `91 01 64` (=100) in `GAME01.BNK` chains — NOT a loop flag |
| 0x92 | driver type `local_14` | `+1` | 0/2 default, 1 = Dolby-pro (mono + codec 0), 3 = DirectSound3 float path |

Error codes `DAT_00585ca0`: 0x477 invalid platform, 0x4f4, 0x50e, 0x531 unsupported driver 3,
0x544 unsupported sample format (`local_18/local_44` not in {(16,1),(16,2),(8,1),(8,2)}).
Pass-58 corrected the stale `0x82 = duration` and `0x91 = loop flag` rows in
`docs/asset-formats/AUDIO.md`; `formats/src/schl.cpp` (`0x82 -> channels`) matches the exe
and ships unchanged. Evidence: `/tmp/wc98pass58/tlv1.txt`; measured stream
`VIDEO/INT_EASP.DCT` @0x18 `82 01 02` + `83 01 07` (stereo EA-XA).

## 9. Per-scene SFX spec files + playback gate (pass 60, measured 2026-11-19)

Evidence: `analysis/pass60sfx/NOTES.md`, `/tmp/wc98pass60/aud4.txt` (disassembly
`FUN_004ac990`), `/tmp/wc98pass60/aud5.txt` (decompile `FUN_0051d670`, `FUN_0051d480`,
`FUN_004ac840`).

The pass-49 A1 claim "slot numbers come from the scene handler, scene-LAY driven" is
retracted. The per-scene SFX data ships as retail spec files:

- `COMMON/AUDSFX.W` (552 B; header `{u16 0x0912, u16 field=6}`; u32 LE payload from
  offset 4: 480, 1200, 600, 1248, 240, 420, ...);
- `COMMON/AUDSPC.WE/WF/WG/WI/WJ/WS` (5988 B each; header `{u16 0x0912, u16 field}` where
  the field measures 7/263/519/775/1287/1031 — monotonically increasing across file
  order, so offsets/indices [H]; payload begins `0 | 327680,327680 | 655360,655360 | ... |
  1638400,0 | 16,16 | 160,160 | 320,320` — `<<16` fixed-point percent values plus plain
  groups).

The loader chain `FUN_004ac840` (`FUN_004ac930`/`FUN_004ac960` + AUDIOCMN spec-lane slot
`PTR_FUN_00580e34` = exe `spec_load` `0x4904c8` (pass-63), speech-state branch on
`FUN_004e6c40`) installs them;
`FUN_004ac990` chooses the file by mode byte: format `audspc.%s%c` @0x568dc8 (game stem
`W` + scene letter → `AUDSPC.WE..WS`) or `audsfx.%c` @0x568dd4 (→ `AUDSFX.W`); pool string
`sfx.%c` @0x568dd7 is the alternate stem. Load destination is the exe-side AUDIOCMN slot
block `0x580e2c..0x580e38`, installed at boot by `FUN_00490528` → `FUN_004ac7a0`
(pass-63); the earlier "load destination addresses are inside the SND DLL exports" claim
is **retracted** — the DLL receives driver callbacks, see Pass-63 below.

Gate `FUN_0051d670`: entry table `&DAT_0058f3b0` stride 4 indexed by
`(SNDplay id & 0xffff)*4` — `{+0 u8 type, +2 u8 ambient flag, +3 u8 interval}`; ambient
range table `&DAT_0058f330` stride `0x10` indexed by scene selector `DAT_0064492c` —
`{+0 u32 min, +4 u32 max}`. Return values: `1` audio disabled (`PTR_FUN_005896bc` false);
`3` suppressed when `entry+3 <= tick % modulus` (interval throttle, `tick = FUN_004c321c`),
or in the ambient lane (`entry+2 == 0`) when `sel = tick % (max-min+1) + min` exceeds
`DAT_00641220 - DAT_00644e04` or `DAT_00641220 - DAT_00644d5c`, or when the packed
priority arg (`param_2[1]` u16) is nonzero with `type == 0xe`, or when
`DAT_00641220 - param_2[0] < cooldown[type*4]` (table `&DAT_0051cf9c` stride 4); `0` =
allow. Auto-player `FUN_0051d480`: scene-9 lane (`DAT_00640fb4 == 9`, `DAT_00640fe8 == 0`,
`DAT_00644d6c` set, `DAT_00644d2c == 0`), id = `FUN_0051d810(tick)` (skips `0x50a`), on
allow plays through `PTR_FUN_005896c0` and updates the per-id record array
`DAT_00644d54 + id*0xc` (`{+0 = entry+2 byte, +4 u16 counter++, last id → DAT_00644d5c}`).

Port: no code change — `MatchAudio::trigger` fires GAME00 slots directly and the
SNDplay-id→slot mapping is decoded in pass 62 (§10: `{bank = id>>24, slot = low bits}`).
Pass-63 measured the gate tables as **compile-time static initialization** — there is no
runtime builder: `DAT_0058f3b0` is 1295 identical dwords `0x64000000` (all ids
`type=0, ambient=0, interval=100`, so `tick % 100 >= 100` never throttles → allow) and
`DAT_0058f330` is 8 zero `0x10`-stride ranges; the runtime spec files build only the
ambient spec pool `DAT_00644d50` + scene selector `DAT_0064492c`. The allow-everything
default is exactly the port's behavior. (The earlier "SND DLL bank tables unrecovered"
claim is retracted — the DLLs are only the DirectSound driver layer.)

## Pass-61: SND callback wiring (SFX request queue)

`PTR_FUN_005896b4..005896c8` start as compile-time stubs (`.data` measured:
`FUN_004e6c00/c10/c20/c30`) and are installed at runtime by `FUN_004e6b50`
(audio-common installer, boot caller `FUN_00490528`, enable flag
`DAT_00632774`, lane enable `DAT_006326b0` via `FUN_0048fd4c`). Watcom
register args from the boot site (`0x49057f` disasm): `EAX=0x48f730`,
`EDX=0`, `EBX=0x48f728`, `ECX=0x48f78c`, stack pushes `0x48f830`, `0x48f8bc`.
The installer clears to stubs, always installs the gate (`EBX`), and installs
the play/submit slots only when scene `DAT_00640fb4` ∉ {10, 0xc} — SFX is
stub-disabled in those scenes. `FUN_00490634` reinstalls all-zero (disabled).

Installed callbacks:
- gate `0x48f728` = `jmp 0x48ffb8`: probes the SFX request queue
  `DAT_00632734` (iterators `FUN_004a49a0/4a4a00/4a4a30`); `AL` = pending
  flag, `ECX` = queue struct; empty queue (`DAT_00632724 == 0`) → `AL = 0`.
- play `0x48f830` (slot `0x5896c0`): guards `DAT_006326b0`, capacity via
  `FUN_004906a4`, pool-allocates from `DAT_006326c8` (`FUN_004c4890`), entry
  `{+4=EAX, +8=DL type byte, +0x18=BL, +0x1c=cb 0x48f47c}`, submits through
  `FUN_0048f3e0` (request struct with cb pair `0x48f3a4/0x48f380`; flags byte
  `entry+0x20 & 0x3f` selects the pos/pan lane at `FUN_0048f2d8`) → handle;
  pushes into the in-flight queue `DAT_006326e4`.
- pump `0x48f8bc` (slot `0x5896b8`): qfetchs `DAT_006326e4`, advances the
  record (`LEA EDX,[record+0x14]; FUN_0048d084`), publishes
  `DAT_006326bc = u16 [record+0x12]`, invokes the entry callback `[entry+0x1c]`,
  recycles the entry.
- submit form 2 `0x48f78c` (slot `0x5896c4`) with `cb 0x490094`/`0x49001c`;
  level probe `0x48f730` (slot `0x5896b4`, `CMP EAX,3`, lookup `FUN_0048d070`).

Boot-level clamp: `FUN_004e6c40` scans ids `0..0x509` through the gate slot
and stores `DAT_00644de4 = max` gate byte; `FUN_004e6db0` advances the per-id
level byte `DAT_00644d54 + id*0xc + 8` clamped at that max through the
percent table. Gate modulus at `FUN_0051d670`: default branch `ECX = 100`;
`tick % ECX >= entry interval byte` (`DAT_0058f3b0 + id*4 + 3`) throttles.
Ambient range table `DAT_0058f330` stride `0x10`: scene-9 allow only when
`|DAT_00646924| < DAT_00644934` and `tick - DAT_00644d5c <
[DAT_0058f330 + DAT_0064492c*0x10]` (`FUN_004adb30`). Ambient spec table
`DAT_00644d50` stride 8 `{u16 id, u16 value (0x8000 sentinel), u32 aux}`,
count `DAT_00644d48` ≤ 20, scene-9-only adds (`FUN_0051d0c0`), insert-sorted
by plays count (`FUN_0051d180`); consumer `FUN_0051d230 → FUN_0051d3d0` plays
the entry byte `DAT_0058f3b2 + id*4` via the play slot.

Port: no code change — the exe SFX path is a request-queue + spec-throttle
model; the id→sample resolution is the exe's own bank subsystem (§10, pass 62:
`{bank:1, slot:24}` id layout), not a table inside the DLLs. Evidence:
`analysis/pass61snd/NOTES.md`, `analysis/pass62bank/NOTES.md`.

### 61b: CRWD scene-lane filenames (closes the pass-56 open item)
Retracted: "scene-10 loader loops slots 0..8 with an undecoded per-slot
filename". Disassembly (`/tmp/wc98pass60/GhidraWc98Aud20.py`): the slot loop
`FUN_0048e280` runs exactly 2 iterations (`XOR EDX,EDX; ... CMP EDX,0x8;
JNZ`) over 2 pointer-table entries `DAT_0057eb44/+8` — initial file bytes are
non-pointer garbage with no static write site (runtime-patched pool [H).
The filenames come from the scene dispatch `0x48e7a0..0x48e847`:
scene `DAT_00640fb4 == 10` loads `TRAFF.BNK` (VA 0x566af0) + `TRAFF.CRD`
(0x566afc); every other scene loads `CROWD.BNK` (0x566b08) + `CROWD.CRD`
(0x566b14) through loader `FUN_0048e190` — stack buffer format
`"AUDIO\\CRWD\\%s"` (0x566acc, via `0x49168e`), `EDX = FUN_0048e8d0()` count,
`FUN_00439810(path, count)` → handle. Handles land in `DAT_006325c0` (bank),
`DAT_006325c4` (card); slot-loop handles in `DAT_006325d0/5d4`, freed by
teardown `0x48e300` via `0x42b070`. Common wiring: `FUN_004e3128` attach on
`DAT_006325c8`, then `FUN_004e4a80` card gate. The port's crowd bed already
consumes `CROWD.BNK/CRD`; `TRAFF.*` is the scene-10 (stadium) variant of the
same loader, not a new sample source — no port change.

### 97a: scene-10 crowd tick = MIDI sequence drain (pass-97, 2026-11-20)
Crowd tick `FUN_0048e68c` (`analysis/pass97crowd/cr3.txt` disassembly, `0x48e68c..0x48e6c7`)
drains MIDI sequence handles, not sound slots: gated `DAT_006325b0 != 0` (scene-enable flag,
set by `FUN_0048e78c`) and `DAT_006325b4 != 0` (boot done). It runs as the tail of the tick
host (`JMP 0x48e68c`, §Pass-96 tick host `FUN_004906f8`; no direct `E8` site). Teardown
`0x48e84c`: `FUN_004e4f80`,
`FUN_004e4354` (when booted), free the bank through the registry callback `FUN_004e37cc`
(installed at `DAT_006597d0` by boot init) with `EAX = DAT_006325c8`,
`FUN_004e4fd8`, `FUN_0048e2b4` (PBK slot handles), `FUN_0048e300` (boot lane), then zero
`DAT_006325b8/006325b0`.
Loop: `qfetch(DAT_006325fc, predicate 0x48e168)` until no entry matches; each **matching**
entry is moved to queue `DAT_006325e0` via `qbottom` (`FUN_004c4800`); non-matching entries
stay pending in `DAT_006325fc`. The predicate reads the queue entry's MIDI sequence handle at
`+8` and calls `FUN_004e38b4` (`SNDseqstatus`, `cmn\sseqstat.c`, assert string `SNDseqstatus …
MIDI MODULE NOT INITIALIZED` @0x56b380): `EAX` = sequence handle (`handle < 0` or
`handle & 0x1f >= 0x10` → `-8`; server uninitialized `DAT_006597b0 == 0` or `DAT_006597d4 ==
0` → `-14`), looked up in the 36-byte sequence-record table `DAT_0064b160` (`{+0 handle, +12
state byte}`), state byte 0..4 through the jump table at `0x4e38a0` mapping to status codes
`0` (states 0/3), `2` (state 1, and state 2 after a `DAT_0064bee0` bound check), and `3`/`5`/
`6` (state 4 lane, keyed on record `+0x11`). Predicate match = status `== 2` **or** negative
(`CMP EAX,2; JZ keep; TEST EAX; JL keep; XOR EAX,EAX`), so codes `0`/`3`/`5`/`6` remain in the
pending queue and matching handles move to `DAT_006325e0`. What status `2` means (ready vs
done) is not decoded [H]. This is the CMIDI sequence lane (`CMIDI.BNK` boot lane,
`DAT_006325cc`): the MIDI-director stream lane stays device-blocked (A5), the port has no
equivalent lane, so no port change; the crowd bed model (pass-54/59) is untouched by this
lane.
Scene-enable dispatch (`0x48e7a0..0x48e847`, pass-61b) confirmed: scene word `DAT_00640fb4 ==
10` loads `TRAFF.BNK`/`TRAFF.CRD`, other scenes `CROWD.BNK`/`CROWD.CRD`. `E8` scan with the
correct AUTO anchor (`raw = VA - 0x400c00`, verified `0x48e168` reads `8b 40 08 e8`) finds the
static caller at `0x409f04` inside init `FUN_00409ef4`, matching §"SFX bank owner"; a naive
scan with the `+0x401000` delta is off by 4 KiB and misses it.

## 10. SND bank subsystem — id→sample mapping (pass 62, measured 2026-11-19)
Retracted (pass-60/61): "the SNDplay-id→slot mapping lives in the SND DLL bank tables
(unrecovered [H])". `EACSND.DLL`/`EACSNDD.DLL` (tree root) are the **DirectSound driver**
layer — their 13 Watcom `.edata` exports are `_iSNDdirectcaps@4`, `_iSNDcreate3dbuf@32`,
`_iSNDplay3d@28`, `_iSNDpos3d@12`, `_iSNDrate@8`, `_iSNDremovebuf@4`, `_iSNDserve@0`,
`_iSNDsetfunctions@28`, `_iSNDstart@8`, `_iSNDstop@0`, `_iSNDstopbuf@4`, `_iSNDvol@8`,
`_iSNDdllversion@0`. The bank subsystem that resolves play-id→sample lives **inside
FIFAWC.EXE**:

- Bank registry `DAT_00659de8`, stride 8, 10 slots. `FUN_004e33c0` (SNDbankcreate)
  validates the bank header: `numpatches` ∈ 1..0xffff;
  stamps runtime record `{+0 tag 'BNKl' (LE 0x6c4b4e42), +1 u8 = 4, +6 u16 = numpatches,
  +2 u16 = numpatches*4 + 0x14 (record size)}`, zeroes the patch array from `+0x14`
  `numpatches` times; table walk breaks at `0x4f < idx+8` → 10 slots max, overflow
  returns `-9`. File-bank header version byte is read at **+4** by `FUN_004e34e0` /
  `FUN_004e2d74`: `4` → patch-offset array at `+0x14`, `2` → array at `+0xc`, anything
  else → "iSNDdownloadbank: BANK VERSION" assert (file string `cmn\sbank.c` @0x56b29c,
  msg VA 0x56b2a8, return `-11`).
- `FUN_004e3724` (SNDbankadd2files): gate `DAT_006597b0 != 0` else `-14`; registers the
  bank pointer into the first empty registry slot, validates tag `+0 == 'BNKl'`, then
  `FUN_004e34e0` (iSNDdownloadbank) walks the `numpatches` entries — each non-zero delta
  becomes an absolute file offset (`base + i*4 + delta`) in the version-specific array.
- `FUN_004e2d74` (SNDplaysetdef): the play-spec dword id — **high byte `id>>24` = bank
  index into `DAT_00659de8`**, low bits = slot within that bank; rejected (`-8`) unless
  `slot < bank[+6]` count; dispatches to the driver play lane (`FUN_0052e8b0`).
  Retail live-match dispatcher ids (`FUN_004ae730`: 0x1f9/0x1fa/0x1fb) therefore carry
  `bank = 0`; bank index = registration order [H — which bank occupies slot 0 at scene
  runtime is not statically recovered].
- Bank-name loader `0x48eff0`: sprintfs `AUDIO\SFXS\%s` (VA 0x566b54) onto a 256-byte
  template (`ESI=0x48eef0` rep movsd ECX=0x40) carrying embedded names `game00.bnk`
  (0x48f1e2), `game01.bnk`, `plrs00.bnk`, `plrs01.bnk`; handle stored at
  `DAT_00632698 + idx*8` `{+0 handle, +4 stream}`. Ambient set `FUN_0048f068` requires
  `DAT_006326ac` set, `DAT_006326a8` count ≥1, tick ≥ `DAT_0057eb50`; every 0x14 ticks
  `SNDplaysetdef` + percent `DAT_0063269c`, volume `(count*0x7f)/100`.
- SFXS pool boot `FUN_0048fd54`: once only (`DAT_006326b0`), device check `FUN_004df820`,
  loads bank handle `DAT_0063271c`, creates 3 queues (`FUN_004c46a0`×3), allocates an
  8-entry buffer pool (`DAT_006326c0/c4`) sized `((size+0x14)>>10 + 1)*0x400`, `+0x28`
  link, pushed via `qbottom` (`FUN_004c4800`) — same queue family as the chant pump
  (§pass-56).

Port: no code change — `MatchAudio::trigger` fires GAME00 bank slot numbers directly,
which is exactly the retail `{bank:1, slot:24}` slot field. Evidence:
`analysis/pass62bank/NOTES.md`, `/tmp/wc98pass62/snd2.txt`..`snd8.txt`
(`GhidraWc98Snd1.py`..`Snd8.py`).

## Pass-63: AUDIOCMN spec-lane slots + DLL driver-callback direction

Retracted (pass-60): "Load destination addresses are inside the SND DLL exports [H]" and
"`PTR_FUN_00580e30` = SND DLL export". The slot block `PTR_FUN_00580e2c..0x580e38` is
**exe-side**: raw scan of `FIFAWC.EXE` shows the four callback addresses
(`0x49051c/0x4904fc/0x4904c8/0x48f714`) appear at exactly one site each, inside
`FUN_00490528` (`0x49055f..0x490572`): `mov ecx,0x48f714; mov ebx,0x4904c8;
mov edx,0x4904fc; mov eax,0x49051c; call 0x4ac7a0`. `FUN_004ac7a0`
(`audcommon_install`) first resets the slots to null stubs (`FUN_004ac800`→0,
`FUN_004ac810`→`*out=0`, `FUN_004ac820`→0, `LAB_004ac830`), then installs non-null args:
EAX→`0x580e2c`, EDX→`0x580e30`, EBX→`0x580e34`, ECX→`0x580e38` (Watcom arg order).

Installed spec-lane callbacks (decompiled, `/tmp/wc98pass63/aud5.txt`/`aud6.txt`):
- `0x49051c` sentence-release: `EAX==0` no-op, else free via `FUN_0049f750`.
- `0x4904fc` sentence-alloc: count probe `FUN_0048e8d0`, reserve via `FUN_0049f5a0`,
  writes handle to `*out`, returns 1.
- `0x4904c8` spec-load: `sprintf("common\\%s", EAX)` (format VA 0x567340) → open helper
  `FUN_00439750` (retry loop); returns success. This is the `COMMON/AUDSFX.W` /
  `COMMON/AUDSPC.W*` open path (§9) — an exe CRT file open, not a DLL export.
- `0x48f714` record-query: `DAT_0063271c == 0` → passthrough `param<<32`, else
  `CONCAT(param, DAT_0063271c + 0xc)` — reads the SFXS bank record field (§10
  `DAT_0063271c` bank handle) [H for exact field semantics].

The DLL direction is the reverse of what pass-60 assumed: exe resolver `FUN_0052c948`
(`win\sdriver.c`, strings `iSNDloaddll - COULDN'T …` @0x56ddf0..) does
`LoadLibraryA("eacsnd.dll")`, calls `_iSNDdllversion@0` (must return `0x80000` —
required 8.0, mismatch message prints `REQUIRED %i.%i` with args `8,0`), resolves
`_iSNDdirectsetfunctions@28` and calls it with **7 exe driver thunks**
(`LAB_0052c854/0x52c830/0x52c8ac/0x52c888/0x52c8ec`, `FUN_0052c864`, `FUN_0052c83c` —
wrappers onto `FUN_00548ec4`, `FUN_0052bf94`, error-format `FUN_004c7ff3`,
`FUN_004cd5d0`, `FUN_00547f20`, kernel32 import thunk `jmp [0x5597c0]` [H name unresolved]),
then stores the remaining 11 `_iSNDdirect*` exports into `DAT_00659e6c..DAT_00659e94`.
DLL `_iSNDdirectsetfunctions@28` (EACSND.DLL RVA 0x13c8) is a plain 7-store:
`mov eax,[esp+4..1c]; mov [0x40aa14..0x40aa2c], eax` (DLL DGROUP callback block) — the
driver stores the exe's callbacks for its own use; nothing writes exe slots from the DLL.

Gate tables are compile-time static (§9 measurement): no write site for
`DAT_0058f3b0`/`DAT_0058f330` anywhere; shipped values = allow-everything.

Port: no code change — the spec lane's shipped defaults (interval 100, ranges 0) mean the
retail gate never suppresses, matching the port's direct-fire `trigger`. Evidence:
`analysis/pass63snd/NOTES.md`, `/tmp/wc98pass63/aud4.txt`..`aud9.txt`,
`dll1.txt`..`dll7.txt`, `spec1.txt`..`spec4.txt`
(`GhidraWc98Aud4.py`..`Aud9.py`, `GhidraWc98Dll*.py`, `GhidraWc98Spec*.py`).

## 11. STRASYNCH speech async-load lane (pass-64, measured 2026-11-19)

Retracted: "commentary sentence pump `FUN_004901e0` drains the sentence queue through the
`+0x10` word-executor callback" (§7) and COMMENTARY_WC98.md's "queue flush" / "opens
`PHRS.PBK` → `DAT_00632734`" wording. Raw disassembly (`/tmp/wc98pass64/com4.txt`,
`com8.txt`) shows `FUN_004901e0` drains the **async load-completion queue**, and boot
`FUN_00490250` opens `TYPE.PBK` (not `PHRS.PBK`) into `DAT_00632734`.

Boot `FUN_00490250` ("STRASYNCH" lane; tag string @0x566c18), gate
`FUN_00490674` = device flag `DAT_00632774`, one-shot `DAT_00632724`:
- queue object `DAT_00632728` = `FUN_004c3a00` (lockable alloc-queue);
- sync opens `PHRS.HDR` (@0x566c00) → `DAT_00632730`, `TYPE.PBK` (@0x566c0c) →
  `DAT_00632734` via path format `FUN_0048f9d0` → full-path builder `FUN_0043933c`
  (`"%s\%s"` @0x561d34; base dir `DAT_0060133c`, alt `DAT_0060143c` when `DAT_00601338 == 0`)
  → open helper `FUN_00439810`; on either open failing `DAT_00632724 = 0` and both handles
  are closed (`FUN_0x42b070`);
- two queues `DAT_0063273c` (slot pool) + `DAT_00632758` (completion) via `FUN_004c46a0`,
  and a 16-record slot pool `DAT_00632738` reserved through `FUN_0049f5a0`
  (tag `STRASYNCH`; the "count" arg `FUN_0048e8d0` is the **SNDMEM memory-class index**,
  pass-65 §12 — fallback 2 when the heap is not installed), all 16 slots
  `qbottom`-ed into `DAT_0063273c` (stride `0x14`);
- `initasync` `FUN_004d82d0` (`win\async.c`, asserts `initasync - DO_NOT…` :0xc4,
  `FAILED TO START ASYNC THREAD` :0xde) with `EAX=0x10` jobs, `EDX=0x2000` buffer: allocates
  control block `DAT_0064aedc`, 4 queues, `2*jobs` job records, spawns the I/O thread and
  waits for its handshake;
- `PHRS.PBK` (@0x566c24; refs 0x49035d/0x49037e) is formatted twice: first
  `FUN_004d8840` = `setasyncfile` (`win\async.c` :0x203 `MUST_CALL_initasync`, :0x214
  strdup via `FUN_0049f5a0`; job record `+0x1c` filename, `+0x20=2`, seq ids `+4/+0x14`)
  enqueues the async load + wakes the thread (`FUN_004c37b0`); the second build feeds
  `openhandlea` `FUN_004cf5e0` (`win\fileio.c` :0xba `OPEN_FAILED_ON_%s`, :0xc1
  `INFO_FAILED_ON_%s`) → handle `DAT_0063272c`.

Submit `FUN_00490144` (callers: chant loader `FUN_0048d53c` site 0x48d5dd, phrase loader
`FUN_0048953c` site 0x4896f8, boot lane `FUN_00490acc` site 0x490a23): gate `DAT_00632724`;
pop pool slot (`FUN_004c8790`), copy request-record `+0xc→rec+8`, `+0x14→rec+0xc`,
`+0x18→rec+0x10` (completion callback), `rec+4 = submitasync` `FUN_004d8730` result
(thread job id); on enqueue failure requeue slot; pool empty → callback runs **inline**
(synchronous fallback), returns 0.

Pump `FUN_004901e0` (main frame loop `FUN_00409f78` sites 0x40a09e/0x40a0ec; also
`FUN_0046b230` 0x46b24f, `FUN_0048f6a4` 0x48f6cd, trigger wrapper `FUN_004893ec` 0x489427):
queue-object vtable lock `(*DAT_00558464)(DAT_00632728)` + file lock `FUN_00438f58`;
loop `qfetch` (`FUN_004c4890`) from completion queue `DAT_00632758`: per record run
`cancelasyncload` `FUN_004d8aa0` (`win\async.c` :0x2f2) on the `+0x4` job id — it frees the
thread's reserved buffer (`FUN_0049f750`) when the job was still in flight — then if
`rec+0x10 != 0` invoke the load-complete callback with `EAX = rec+0x8`; record always
requeued to slot pool `DAT_0063273c` (`FUN_004c4800`). Unlock `FUN_00438f80`,
`(*DAT_00558528)(DAT_00632728)`.

Teardown `FUN_00490404` (drain set from boot builder `FUN_004907c0`): drains both queues,
closes `DAT_0063272c`, frees pool `DAT_00632738`, closes+zeroes `DAT_00632730/0x632734`,
clears `DAT_00632724`. Boot callers: main init 0x409f13, boot builder `FUN_00490728`
site 0x49079e (lane sequence §pass-51 doc), FE boot `FUN_0044b340` 0x44b363, trigger boot
`FUN_00489350` 0x48935f.

Strings `C0001800.PBK`/`C0001801.PBK` (@0x566c30/0x566c40, chant names) have zero static
immediate references — runtime-computed via the `C0%04d` lane (§pass-53), consistent with
the pointer-table note. The thread's read loop internals (`win\async.c` worker) are not
port-relevant: the port loads phrase banks synchronously (`MatchAudio::decodeChantLane`),
so no mirror change; the pump's frame-loop ordering constraint (drain before trigger)
is noted for `trigger()` callers. Evidence: `analysis/pass64phrs/NOTES.md`,
`/tmp/wc98pass64/com1.txt`..`com8.txt`.

## 12. SND memory-class subsystem + audio boot install order (pass-65, measured 2026-11-19)

Corrects the pass-56/pass-64 reading of `FUN_0048e8d0` as a phrase/speech count: it
returns the **SNDMEM memory-class index** `DAT_00632638` (fallback `2` when the heap
flag `DAT_00632634 == 0`). `DAT_00632638` is written by `FUN_0049f4c0` =
`getfreememclass` (`cmn\memstd.c`, debug assert `NO_FREE CLASSES` :0x10): scans the
class table `&DAT_006414a0 + k*4` until NULL, returns the registered-class count, which
`createMemClass` `FUN_0049f1f0` (`CLASS_NUMBER_OF…` assert :0xd8) then consumes as its
**class slot index** (`(&DAT_0064149c)[EAX & 0xf] = header`). All twelve `FUN_0048e8d0`
consumer sites (0x48e1ad, 0x48e734, 0x48ee89, 0x48eaa0, 0x48f022, 0x48faa9,
0x48fc2d, 0x48fd8e/0xfde8/0xfe17, 0x490283/0x4902a9) pass it as the loader `FUN_00439810`
memory-class tag argument — allocation class, not record count.

Install `0x48e8e8` (sole caller `0x49078a`): gated on enable flag `DAT_00632774`
(`FUN_00490674`), once via `DAT_00632634`; `FUN_0049f5a0` = `reserveMemAdr`
(`BAD SIZE VALUE` assert :0x2cc) with `EAX = "SNDSYS"` (@0x566b20),
`EDX = DAT_0063263c` → block `DAT_00632640`; then `FUN_0049f1f0 createMemClass`
(`EAX` = class index, `EDX = "SNDMEM"` @0x566b28, stack args incl. item size `0x20`,
align `0x8`; header tag marker u16 `0x4253` at `+0x20`, `+0x28 = 0x7fffffff` max,
`+0x40` align, `+0x44` granularity). Size setter `0x48e8a0` (`if (EAX > 0)
DAT_0063263c = EAX`) is called at boot `0x43cc02` with `EAX = 0x300000` — **3 MiB audio
heap reservation**; pass-64's `EDX = 0x140` note was a wrong-register read of this
lane. Teardown `0x48e960` (caller `0x4907f3`) runs `removeMemClass` `FUN_0049f470`
(`FUN_004c3a80` + `FUN_00494368`) on the class, frees `DAT_00632640` via `FUN_0042b070`,
clears both fields.

Audio boot `FUN_00490728` (boot-chain site `0x43cc07`): device probe `FUN_004e95f0` →
`DAT_00632774 = (uint)(-1 < ret)`; success installs in order `FUN_0048e8e8` (SNDMEM
heap) → `FUN_0048d378` (chant pool) → `FUN_0048ed9c` (CRWD boot lane) → `FUN_00490acc`
→ `FUN_00490250` (STRASYNCH §11) → `FUN_0048fd54` (announcer) → `FUN_0048dfc4` →
`FUN_0048f1c4`, after `FUN_004e9840(0xffffffff)` + `FUN_004e41d4` and percent-table
static default: `DAT_00632778` ← **100 for all six scene slots** (loop while
`iVar2 != 0x18`); the `{80,80,80,80,65,80,1}` snapshot (pass-7e) is the later
options rewrite. `DAT_00632774` readers: 0x490528, 0x490634, 0x490674, 0x4906f8
(frame pump), 0x490728, 0x4907c0 (teardown).

Path formatter `FUN_0048f9d0`: `DAT_00632720 == 0` → `sprintf(buf, "AUDIO\PHRS\%s",
EDX)` (@0x566b9c), else `sprintf(buf, "%s%s", EDX, DAT_00632720)` (@0x566b94) — an
installed-root prefix lane. `DAT_00632720` accesses are exactly two (pass-68 raw scan):
read `0x48f9d4` in the formatter, and a **write** in an undecoded Watcom tail stub
`0x48f9c6` (`MOV EAX,EAX` alignment + `MOV dword [0x632720],EAX; RET`, bytes in the
Ghidra gap `0x48f9bf..0x48f9cf` — the earlier "zero static write sites" wording was
wrong; the stub sits after the formatter tail's `RET` at `0x48f9c5`). The stub has no
static caller: an `E8`-rel32 scan and a LE-pointer scan for `0x48f9c6` both find zero
references, so the install-root string arrives through runtime-computed dispatch
(same runtime-injection class as the EULZ expander) [H]. Port: phrase banks load
synchronously under a fixed tree path, so no mirror change; the formatter semantics
are recorded for path-assembly fidelity only.
Evidence: `analysis/pass65snd/NOTES.md`, `/tmp/wc98pass65/p65.txt`..`p65h.txt`,
pass-68 raw scans + disassembly at `/tmp/wc98pass68/p68.txt`..`p68e.txt`.

## Pass-69: scene-10 CRWD phrase-lane registry wired into the port

`MatchAudio::catalogCrwd` (boot, after the PLRS catalogs) parses `AUDIO/CRWD/CROWD.PBK`
and `COMOS.PBK` with `formats::PhraseBank` and registers the phrase counts — the mirror
of the exe `SNDCRD` arena size + stride-12 record push into `DAT_006325e0`
(pass-56 init `FUN_0048e6d0`, §pass-56). `crwdPhrasesRegistered()`/`crwdComosPhrases()`
expose the counts; `test_gameaudio` checks both > 0 on the booted no-device lane. No
audible lane is invented: the queue feeds only the CMIDI.BNK MIDI table path (A5,
synth-blocked on Linux), so the registry counts are the full faithful mirror.

Pass-68 also updated the `ExeLcg` comment: `FUN_004ce317` seeds from the multimedia-timer
tick counter `DAT_00636a14` (`win_inittmr.c`, `INC` at timer callback `0x4966b8`), so
retail chant-LCG seeds are uptime tick counts — the port's constant seeds are the
documented approximation.

Evidence: `analysis/pass68seed/NOTES.md`, `/tmp/wc98pass67/p67b.txt`, gate
`tests/unit/test_gameaudio.cpp` (`crwdPhrasesRegistered` CHECK).

## Pass-70: crowd tick pump body decoded (`0x4e5018..0x4e510b`, Ghidra-unassigned tail)

The tick handler the SNDcrowd lane registers (`DAT_006597c8 = LAB_004e5018`,
control `DAT_006597e4 = FUN_004e4fd8`) sits in a code gap Ghidra never
function-ized; forcing disassembly (`Listing.getInstructions(AddressSet, False)`
+ `FunctionManager.createFunction`) recovered it (`analysis/pass70crowd/NOTES.md`,
`/tmp/wc98pass70/p70i.txt`). Retracted claim: the pass-54 doc's "pump `0x4e502c`
per-slot gain byte `curve[level]`" is wrong — `0x4e502c` is `PUSH EDX`, a Watcom
argument instruction for the idle/blend call site `0x4e502d`; no gain byte is
computed in the pump body.

Decoded pump semantics:

- Runs only while the lane state byte (`0x5894a8`) is `2` or `4`; any other
  state returns immediately.
- Envelope position dword `0x5894ac += delta 0x5894b0`, clamped at floor
  `0x5894b4`. On a *downward* floor hit the pump clears the delta and runs an
  8-iteration release loop (`EDX` stepping 0..0x20 by 4, body
  `FUN_004e2c60`) that stops every crowd-type voice: `FUN_004e2c60` takes the
  voice mutex (`FUN_0052bc04`), allocates a slot (`FUN_0052c0c0`), then walks 16
  stride-`0x18` voice slots from `DAT_006597e8`, stopping each with type byte
  `+0x3b == crowd type`, state `+0xc == 1`, and id ≥ 0 via `FUN_0052ddbc`.
  State then becomes `3`. A *upward* floor hit just clamps position and clears
  the delta.
- A second ramp word `0x5894be` ticks ±4 per call toward the control level word
  `0x5894cc` (written by `SNDcrowdcontrol` `FUN_004e4d8c`), snapping once the
  distance reaches ≤4; the lane's dword `0x5894bc` low half is re-stamped with
  the control level on convergence.
- Pump tail always calls the idle machine `FUN_004e510c` then the blend
  consumer `FUN_004e5444` (pass-55 machine).
- Stop `FUN_004e4f80` (`SNDcrowdstop` lane entry) runs the same 8×
  `FUN_004e2c60` release while state ∈ {2,3}, then state → 1;
  `FUN_004e4fd8` (shutdown control) clears the tick pointer and state flag.

Byte-scan reality check: raw LE-pointer scans for the envelope fields
(`0x5894ac/b0/b4/b8/bc/be`) hit additional sites in `0x4e59xx..0x4e63xx`, but
that region is `cmn\sstream.c` (`SNDstreamqueue/purge/chunksleft`, stream
record dispatch `FUN_004e5e48` on `SCDl/SCHl/SCKl/SCLl/SCEl` magics) — the hits
are operand-byte collisions, not crowd references. Crowd envelope references are
only the pump body + idle/blend pair.

No port change: the pump's ±4 ramp is approximated — the port blends the
control level into the curve index and re-fades the voice lanes on change
(`applyCrowdGains` seam), instead of stepping the word by 4 per tick;
floor-release is driver-side (`Mixer` owns voice slots) and the exe stop-list
is mirrored by layer fade-out.

Evidence: `analysis/pass70crowd/NOTES.md`, `/tmp/wc98pass70/p70.txt`..`p70k.txt`.

## Pass-71: crowd layer-record routing decoded

`SNDcrowdstart` `FUN_004e4ba0` (state 1→2 transition): reads the eight signed
bank-id bytes at card `+0x20 + i` (CROWD `{0,1,2,3,-1,-1,-1,-1}`, TRAFF
`{0,-1,...}`); enabled slots reserve a voice through the `SNDplaysetdef` lane
`FUN_004e2d74` (validates the bank against the `BKNL` registry `DAT_00659de8`,
stride 8, count u16 `+6`, type byte `+4 == 4` selects the alternate reserve lane
`FUN_0052e8b0`), landing voice ids in the slot table `DAT_005894d0 + i*4`
(disabled → `-1`), count in `0x5894b8` low byte, curve-byte lanes
`DAT_005894ed+3..` / `DAT_005894f5+3..` initialised `0xFF`; then `FUN_004e4f48`
(initial level, input 0..0x7f → `position = level << 16` while state != 3) and
`SNDcrowdpulse`.

`SNDcrowdpulse` `FUN_004e4cac(index 0..15)` reads the 16-byte layer records at
card `+0x28`: byte 0 present gate, byte 1 optional bank-slot id (reserves via
`FUN_004e2d74` when `>= 0`; all CROWD records ship `0xFF` = no extra reserve),
`u16 @4` = envelope delta and `u16 @6` = ceiling (both read through `u32 >> 16`;
pass-55's measured 350/450 pair), target clamped at the ceiling, step word
`u32 @8` (the dump's "position" values {0, 16384, 32768, 43690} = 16.16 increments),
ramp state reset, layer index byte stored at `DAT_005894a7+3`. The idle machine
`FUN_004e510c` case 0 pulls its frame divisor from `u32 @12` of the indexed layer
record (`record + 0x34 + idx*0x10`, `rate * 0x21 >> 16`, clamped `>= 1`) —
confirming pass-55's "byte-12 rate" claim — while cases 1..3 divide the distance
by table words at `+0xc`/`+0x18` of a tick-computed pointer.

Blend consumer `FUN_004e5444` exact formula: control word high half `cc =
(be * ramp + bc * c0hi) >> 9` (`be`/`bc` from byte7 split, `ramp` word
`0x5894be`, envelope `c0 = 0x5894ac >> 16`) — the pass-55 doc's `be*be` form is
the post-snap identity where `ramp == be`. Per active slot: murmur gain byte `=
curveB[cc]` via the fixed-up `+0x148 + i*4` ref, swell gain byte `=
curveA[cc] * c0 >> 17` via `+0x128 + i*4`; each written to the voice lane
(`FUN_0052eee0` / `FUN_0052c660`) only when the stored byte changes.

Port unchanged (re-fade seam approximates the change-gated byte writes).
Evidence: `analysis/pass71crowd/NOTES.md`, `/tmp/wc98pass71/p71.txt`..`p71d.txt`.

## Pass-72: SFXS stream-slot install lane decoded (A2 retracted)

The earlier A2 framing "GAME01 music-slot shuffle" was wrong: `GAME01.BNK` is a
sound bank under `AUDIO/SFXS/`, not music, and `FUN_0048f228` is a teardown loop.

Boot owner `FUN_0048f1c4` (livematch boot 0x409ef4 + scene boot `FUN_00490728`):
gate = sound option `DAT_00632774` (user settings, written at 0x49074d/0x490813)
`!= 0` and install flag `DAT_006326ac == 0`; reads the language byte `[0x60cbd3]`
and calls the loader `FUN_0048eff0` twice - slot 0 with name `game00.bnk`
(@0x566b64, language 0 or 1) or `game01.bnk` (@0x566b70, other), slot 1 with
`plrs00.bnk` (@0x566b7c) or `plrs01.bnk` (@0x566b88) chosen by the tick-counter
parity (`FUN_004c321c` AL bit 0 -> plrs00). Loader: copies a 256-byte static
block @0x48eef0, formats `AUDIO\SFXS\%s` (@0x566b54), opens through the file
lane `FUN_00439810`, stores the raw handle at `DAT_00632698 + slot*8`, then
`FUN_004e3128` -> `SNDbankadd2files` `FUN_004e3724` (`cmn\sbank.c` @0x56b29c
line 0x10d): checks the BNK magic word at the handle, appends the bank pointer
to the BKNL registry `DAT_00659de8` (stride 8, aborts past 10 entries), loads
via `FUN_004e34e0` and stores the voice handle into `slot+4`; sets the kill
handler hook `DAT_006597d0 = FUN_004e37cc`. Boot sets `DAT_006326ac = 1`.

Per-frame livematch member `FUN_0048f228` (0x409f83): when the flag is set it
clears `DAT_006326ac`, then for slots 0..1 kills each non-zero bank handle via
`FUN_004e37cc` and frees each non-zero file handle via `FUN_0042b070`
(`FUN_0049f750` release), storing 0 in both arrays. This is the teardown of the
install lane, not a swap.

Play sites: `FUN_0048f068` (38 callers, scene handlers) cooldown-gated 20 ticks
(`DAT_00636a14` vs `DAT_0057eb50` + 0x14) plays bank slot 0's handle
`DAT_0063269c` at level `DAT_006326a8 * 0x7f / 100` - `DAT_006326a8` is written
only by the stub 0x48f060 from settings code `FUN_0046b074`/`FUN_0046b230`;
`FUN_0048f0e4` builds a 16-byte voice record (word0 = caller voice id, byte4 =
bank handle low byte, byte8 = `arg * percent[idx0] * 0x7f / 10000` through
`FUN_004906d0`), default-initialises it via `FUN_004e2d00` (`SNDplaysetdef`
null-pointer guard lane, `cmn\splay.c` @0x56b018 line 0xc3) and plays through
`FUN_004e2d68` -> `FUN_004e2d74`.

Measured bank contents: GAME00/01 = 53 sounds in an 86-slot table, PLRS00/01 =
17 sounds each (BNKl v2 headers). Port `MatchAudio::catalogSfxs` registers the
four banks; gate `test_gameaudio` asserts the counts.
Evidence: `analysis/pass72sfx/NOTES.md`, `/tmp/wc98pass72/m1.txt`..`m8.txt`.

## Pass-73: speech autovol ramp + beat scheduler fully decoded (A8 ramp closed)

`SNDspeechinit` `FUN_004df820` (0x4df820..0x4df8a2): gate `DAT_006597b0`,
validate vol in `[0,0x7f]`, store `cur = vol << 16` (`DAT_00588d68`, 16.16),
delta `DAT_00588d6c = 0`, install state-cb pump `0x4dfca0` into `DAT_006597c4`
and cleanup handler `0x4dfc64` into `DAT_006597e0`.

`SNDspeechautovol` `FUN_004df8a4`: validate vol `[-1,0x7f]`, frames clamped
`>=1`, server getter `(*DAT_00558464)(DAT_006597bc)` via
`FUN_0052bc04/FUN_0052bc58` (SND-server object, DLL-side internals [H]), then
`delta = (serverCur - cur)/frames`, target `DAT_0064b064 = serverCur`.

Pump `0x4dfca0` (registered per tick; called from `0x4df847` region): delta
`!= 0` → `cur += delta`; clamp both directions at target (`>= target` with
delta>0, `<= target` with delta<0 → `cur = target`, delta=0); apply `cur>>16`
through `SNDvol` `FUN_0052c71c` to the voice group pair `DAT_00588d74`
(current) then `DAT_00588d70` (previous, swapped at `0x4dfdc7`); negative cur
→ sentence stop `FUN_004dfb94`.

Beat scheduler (inside pump when delta==0 and byte `DAT_0064b15a` != 0):
idx byte `DAT_0064b159` walks `0..0x13` over the 20-slot table
`DAT_0064b068` stride 12 `{u16@0 vol low, u16@2, u32@4 id, u32@8 voice handle
| -100 unused}`; compare `SNDgroupgetvol FUN_0052c780(DAT_00588d74)` against
`entry>>16` and exit when greater (operand always 0 at shipped add sites,
`XOR EBX,EBX` literal — semantics unresolved [H]); cycle bytes
`DAT_0064b158/159/15b` (final count / beat idx / entries); build
`SNDplaysetdef` record via `FUN_004e2d00` `{+0 = entry[4], byte+4 = entry[0]
low byte, byte+8 = cur>>16}`, play `FUN_004e2d68`, handle → `DAT_00588d74`
and entry+8. Add site `FUN_004df968` (guard `>=0x13` → `0xfffffff3` full) is
fed from commentary records: `record+0xe` sound id through mapper
`FUN_0048d2f0` (`EAX = [DAT_00632420]` when `DAT_0063241c` set else 0),
returned slot idx stored to `record+0x6`; enable `FUN_004dfb40`, reset
`FUN_004dfe18` (zeroes 15a/158/159/15b), status `FUN_004dfa60`
(`8 = unused, 7 = playing, 3 = queued`) / `FUN_004dfc0c` (`3 = running`).
Beat-add caller clusters: `0x48f4df..0x48f775` (commentary request lane
`FUN_0048f730`: gate sound option `DAT_006326b0`, skip while running, priority
compare `FUN_0048f664` = max `+0x18` over queues `DAT_006326e4/632700`, pick
type-3 record `FUN_0048d070`) and scene boot `0x48987d`.

Game-side vol source: `FUN_004906a4 = percent[idx]*0x7f/100 clamp>=1` over
the scene percent table `DAT_00632778`; duck caller `FUN_0048fca0` (gate
`DAT_006326b0`) from scene boot `0x4904bc` autovol's with percent idx3,
frames=1. Sentence stop `FUN_004dfb94` clears the beat flag, closes both voice
lanes (`FUN_004e2c60`) and resets beat state.

Port: `SpeechGain` (`app/commentary.hpp`) already mirrored the ramp; pass-73
gate `test_gameaudio` asserts exact fixed-point division/truncation numbers
(`set(80) → ramp(1,3)`: applied 53, 27, 1, clamp) — A8 autovol-ramp residual
closed. Beat scheduler stays exe-side scheduling semantics (port commentary
queue models sentence scheduling; gate operand unresolved [H]).
Evidence: `analysis/pass73gain/NOTES.md`, `/tmp/wc98pass73/a1..a10*.txt`.

## Pass-74: .DCT movie audio — claim re-corrected (A9 reopened, Open-recoverable)

**Correction of this pass's first report:** the initial "movies are silent /
no audio payload exists" finding was a scan bug — the fourcc pattern tuple
omitted `SCDl`/`SCEl`/`SCCl`, so the scan reported only `mTCD`. Full
histogram over **all 29** `.DCT` files (`analysis/pass74dct/hist.txt`):
every file contains `SCHl` + `SCCl` + `SCEl` + `SCDl×N` interleaved with
`mTCD` — `INT_FIFA` = 1017 `mTCD` + 1017 `SCDl`; `FL0_*`/`FL1_*` =
300–303 pairs; `SEQ_*`, `FL2/3` = 375–603 pairs. The `SCHl` PT tags
(`82 01 02` = 2 channels, `83 01 07` = EA-XA) declare **stereo audio**;
the port `schl` lane already decodes these streams (test lane counts 29
`.DCT` files — `test_audio.cpp`).

Exe movie pipeline (`analysis/pass74dct/m1..m10b.txt`):
- `FUN_00490ee8` boot: viewport from `DAT_00632a14/2c`, two frame buffers
  (`FUN_0049f5a0` → `DAT_00632a24/30`).
- `FUN_0049110c` pump: open `FUN_0042b4e8/bd04`, chunk filter `FUN_004910d8`
  — passes only `0x4443546d` ("mTCD") to the blit lane; **every other chunk
  is freed** via `FUN_0042d2c0` and the filter returns 0. No SND call on
  this path, so audio chunks are *not* consumed by the video pump.
- Audio chunks belong to the SND stream service `iSNDstreamservice`
  `FUN_004e64ac` (`cmn\sstream.c` `s_cmn_sstream_c_0056b6f4`, error string
  `iSNDstreamservice --- ERROR PARSING`): reads file via `FUN_0042d194`,
  dispatches chunks `FUN_004e5e48`; `SCDl` handler `FUN_004e5f90` appends
  block records to list `DAT_00589508/0c`, counts `DAT_00589520/24`.
  Its four callers (`FUN_004ea4e0`, `FUN_004e57b0`, `FUN_004e5aa4`,
  `FUN_004e6834`) are not yet traced to movie playback — **routing of
  `.DCT` audio during movies remains Open**.
- Percent-lane duck caller `FUN_004906a4` inside the pump region is
  conditional (`_DAT_00632a0c > 2`); args unverified — no claim made.

Unverified, kept [H]: the trailer-before-preamble / MDEC-continuation theory
for the bytes past the mTCD u16 code-size field (not re-checked this pass).

Port state: `app/video.cpp` presents video only; `.DCT` audio decodes in the
`schl` test lane but nothing plays during movies. A9 row reopened;
`MENU_PARITY.md` intro-movies row corrected to "audio stream present,
routing open".

## Pass-75: movie audio routing decoded — A9 closed, port wired

Movie pump `FUN_0049110c` (`m12.txt`) calls `SNDstreaminit` `FUN_004ea4e0`
(`cmn\ssinit.c`; strings `SNDstreaminit --- AUDIO_STREAMER`, `STREAMBUFSIZE`):
call site disasm (`analysis/pass75movie/callsite_disasm.txt`): `0x4911c5`
`mov $0xc8,%ebx` (EBX=200), `0x4911ca` `mov $0xf,%edx` (request count 15),
`0x4911cf` `xor %ecx,%ecx`, `0x4911d1` `mov %ebp,%eax`, `0x4911f1` `push %ecx`
(stack arg 0), `0x4911f8` `mov $0x40000,%ecx` (256 KiB), `0x4911fd`
`call 0x4ea4e0`; then `0x491202` stores the return into `_DAT_00632a0c` and
`0x491207` `cmp $0x2,%eax; jle 0x49133c` — the A/V-synced presentation loop
runs **only when the parsed block count > 2**; the skipped path is the plain
video drain loop. Ghidra watcom mapping of the body: `param_1` = ECX
(0x40000 — passes the `param_1 < 0x401` `STREAMBUFSIZE` check), `param_2` =
EDX = 15 (count, stored `DAT_0058959c`, parse loop runs until
`DAT_00589598 >= param_2` or the tick deadline `DAT_006597b4`), `param_3` =
stack push (0 → `DAT_00589586`), `in_EAX` → `DAT_00589504`; EBX = 200 stays
callee-saved across the service calls, consumed downstream [H]. SCDl blocks
append via `FUN_004e5f90` to the list `DAT_00589508/0c`.

Sync lane: audio progress is `SNDstreamframes` `FUN_004ea6a0` →
`DAT_0058951c`; the pump presents when `audioFrames - presented < 2`, keeps
`_DAT_00632a08` as the high-water counter, and force-exits at
`DAT_0057f690 * 2` elapsed (Esc/skip). Release: `FUN_004e57b0`
(`SNDstreamfree`, cleanup handler installed by `FUN_004e6834` into
`DAT_006597dc`, frees stream + block list `FUN_004e5f10`). Decode consumer
callback `LAB_004e622c` (`DAT_00589694`) runs SND-server-side [H].

Port mirror (`app/video.cpp`, pass-75): start the SCHl stream through the Mixer
after the renderer exists (exe post-setup ordering), gated on `dataBlocks > 2`
(exe `2 < _DAT_00632a0c`), stop + mixer destruct at movie end. **Deviation:**
frame pacing is wall-clock `SDL_Delay(frameDelayMs)`, not audio-counter driven
(the exe paces on `SNDstreamframes` `DAT_0058951c`). Gate mode uses a loopback
mixer and drains the stream through `render()`, measuring it:
`VIDEO frames=1017 ... movie-audio-rendered=1494277 peak=31605` (frames clamped
to `header.numSamples`, peak = max |sample| mixed — matches the independent
decoder probe `analysis/pass75movie/audioprobe.txt`: 2,988,554 stereo samples
@ 22050 Hz, RMS 6903, peak 31605). `video_play` PASS regex requires
`movie-audio-rendered` and `peak` non-zero. `wc98-video-play` now links
`wc98_audio`. A9 row Closed (pacing deviation noted); evidence
`analysis/pass75movie/` (`m11..m13.txt`, `callsite_disasm.txt`,
`audioprobe.txt`).

## Pass-76: commentary ring-slot preload lane decoded (A7 narrowed; scheduler lane reclassified chant-lane)

A7 said "per-record `+0xC` slot tags preload from an **unmeasured table**" — wrong; the
preload is file-driven and fully decoded. Authority: FIFAWC.EXE with the corrected AUTO
delta **VA = raw + 0x400C00** (control scan: `E8` → `SNDphraseadd` 0x4df968 hits
0x48987d/0x48f4df, matching pass-12 §1). Evidence: `analysis/pass76pbp/`
(`disasm.txt`, `a7*.txt`, `NOTES.md`).

Ring: 6 slots `DAT_0057e380` stride 0x10 — `+0` type byte, `+4` id word, `+8` pending
flag, `+0C` lane-class store. Slot filler `FUN_004894ac` (Watcom reg args `EAX`=slot,
`EDX`=type, `EBX`=id; store `shl eax,4; [slot+0]=EDX; [slot+4]=EBX; [slot+8]=0`) is called
from 21 tournament-presentation scene sites (0x453b07, 0x453b15, 0x453b2e, 0x453bd2,
0x453c77, 0x45437d, 0x454398, 0x4543bd, 0x454da5, 0x454dc0, 0x454de5, 0x4554c9, 0x455a81,
0x455a9c, 0x455ac1, 0x455f75, 0x455f90, 0x455fb5, 0x458dc2, 0x458df6, 0x4591fd), each with
`mov edx,type; mov ebx,id; mov eax,slot` (`type` ∈ {1,2,3}; id from accessor returns
`FUN_00485240/0x48525c/0x489920/0x489974+0x4899dc` or loop regs; accessor internals
decoded by pass-83, see Pass-83 section).

Loader `FUN_0048953c` formats the filename from {type,id}: type 1
`AUDIO\team\G_T%04d.pbk`/`A_T%04d.pbk` (home/away selector `DAT_0057e5f8`), type 2
`AUDIO\pbp\schedule\{G,A}_sch%03d.smp`, type 3
`AUDIO\pbp\standing\{G,A}_std%03d.smp`, plus the `id==0x32f` special (`A_FE_USA.SMP`, type
rewritten to 4; file not shipped — missing-file lane). Path join `FUN_004393d4`; async
load record queued `0x62fda0` with callback `LAB_0048970c`, destination = ring entry
`DAT_0057e3e8[slot]`, enqueue `FUN_00490144`. Measured CD assets: `AUDIO/TEAM` 95 PBK,
`AUDIO/PBP/SCHEDULE` 436 SMP, `AUDIO/PBP/STANDING` 320 SMP.

Callback `FUN_0048970c`: type 2/3/4 builds the entry inline (`+8`=1 class, `+0C`=`[item+0]`
tag, self-link, bank block init, per-item handles −1 via `FUN_0048d084`) and pushes to
`DAT_00632428 + class*0x1c` (0x1c-byte queue objects, wrapper arithmetic `shl 2; shl 3; sub` at
`0x48d0f2..0x48d105`; the `class*4` wording was wrong); type 1 stores `[entry+8]` into ring `[slot+0C]` and registers via
registrar `FUN_0048d114` (`ECX`=record, `EAX`=tag: `rec+0C=EAX`, class queue push, patch
count = byte-swapped word at bank `+0x30+8`, patch items → segment sub-queue); sets
`[slot+8]=1` pending, re-arms next slot while `[slot+0]!=0`; builder tail iterates pending
slots, phrase id `[item+0E]` → `FUN_0048d2f0` + `SNDphraseadd` → `item+6`, then
`FUN_004dfb40` enable + pump `FUN_004906f8`. Scheduler lane `FUN_0048d4f4` (gate
`DAT_006324b4`): record = `[rec+0C]+0x20`, tag = `[rec+4]`, push queue `DAT_00632524`.

Builder/pick `FUN_00489460` (7 callers incl. scene pumps) scans pending slots → qfind
`FUN_0048d0dc(class=slot+0C, EDX=slot)` predicate `FUN_0048cf10` (`[rec+0C] == slot`) →
activation `FUN_0048d1f8` (pass-11 semantics unchanged). The tagged lane is tournament
**presentation** announcement audio. The live-match dispatcher lane (pass-12) calls
`SNDphraseadd` directly with event-record phrase ids. The `FUN_0048d4f4` lane (gate
`DAT_006324b4`) registers records through `FUN_0048d114` with tag = `[obj+4]` and pushes the
object onto queue `DAT_00632524` — **chant-lane** registration (pass-56: chant pump
`FUN_0048da50` drain branch consumes `qlength(DAT_00632524)`; pointer-called, zero direct `E8`
sites), so it is not a commentary-tag preload; the in-play dispatcher has no tags on its pick
path. Per-event live-match phrase content mapping stays unresolved **[H]**; port seam stays
sequential [H]. Evidence: `app/commentary.cpp` trigger(), `COMMENTARY_WC98.md` §pick,
`AUDIO_DISCREPANCIES.md` A7. A7 stays **Open (partial)** — the live-match pick table
remains an unresolved half; the presentation handler's scene→{slot,type,id} argument
extraction (accessor-internal, pass-76) was closed by pass-83 (Pass-83 section). The
presentation announcement lane itself is **ported** (Pass-77 section below).

## Pass-77: presentation announcer lane mirrored (API + gate ported; scene wiring open, measured 2026-11-19)

`app::Announcer` (`app/game_audio.hpp/cpp`, after the MatchAudio block) mirrors the decoded
seam 1:1:

- **Slot ring** `setSlot(slot, type, id)` = `FUN_004894ac` (stride 0x10, `[+8]` pending flag
  stored 0 at fill — port keeps `pending = true` until the loader builds the entry).
- **Loader** `pathFor`/`loadSlot` = `FUN_0048953c` filename formats
  (`AUDIO/TEAM/{G,A}_T%04d.PBK`, `AUDIO/PBP/SCHEDULE/{G,A}_SCH%03d.SMP`,
  `AUDIO/PBP/STANDING/{G,A}_STD%03d.SMP` — shipped ISO names are uppercase, exe strings
  lowercase, Windows loads case-insensitively, port uses the tree's real spelling) plus the
  away selector `DAT_0057e5f8` (`setAwaySelector`) and the neutral special
  (`id == 0x32f` && `neutralGate_` (DAT_00640fe8 == 1) → `A_FE_USA.SMP`, not shipped →
  missing-file lane: counted `missing()`, entry built, silent).
- **Decode shapes measured**: `.SMP` files are single PT v2 play tracks — `PT` magic at file
  offset 0, platform word `0x0002` (version in high byte), u32 header size (0x20), tag stream
  `0x85` samples / `0x83` codec / `0x88` header-relative data offset — decoded with
  `formats::decodePlayTrack(data, 0)` (the existing v2 branch already handled this). Team
  `.PBK` slots are phrase banks with nested BIGF containers; the first non-empty phrase is
  decoded through the pass-51 segment seam (record `[item+0E]` phrase pick unmeasured **[H]**).
- **`G_SCH000.SMP` is a silent asset**: its payload is the 11-byte MicroTalk zero-excitation
  frame `10 04 41 08 21 84 10 42 80 07 06` repeated every 0x30 over zero padding (measured:
  decoded samples all zero, `frames=11024 nonzero=0`). Decode-only in the gate; audibility is
  asserted on `A_STD001.SMP` (real speech payload, `peakRms > 1.0` over the loopback mixer).
  The decoder path is unchanged — the 6,964/6,964 phrase gate already covers it.
- **Activation** `activate(slot)` = builder pick `FUN_0048d0dc` + `FUN_0048d1f8`: plays the
  decoded clip once at the speech percent idx3 gain (`sceneLevels()` → q7 lane), flips
  `consumed` (consume-on-use; second pick returns `false`). No scene currently feeds slots in
  the port — the exe scene→id mapping stays [H], so the API is gate-driven; the pass-79
  selector/remmap/feed helpers are gate-only, screen-to-handler dispatch stays open
  (closed by pass-82 — call sites + region attribution, see Pass-82 section).

Gate: `tests/unit/test_gameaudio.cpp` announcer block (path spellings, silent-schedule
decode-only, consume-on-use counter, neutral special missing lane + re-arm semantics, team
bank phrase decode + play). Evidence: `analysis/pass77pbp/probe.cpp` (refpack no-op,
`frames=11024 avg=0 same=1`), `analysis/pass77pbp/probe2.cpp` (`nonzero=0 max=0`),
`analysis/pass77pbp/NOTES.md`, `analysis/pass76pbp/disasm.txt`.

Pass-77b (corrected same day): rel32 scan (`call` operand = next VA + s32, raw VA = off+0x400C00)
enumerates all 21 `CALL 0x4894ac` fill sites; register windows (`analysis/pass77pbp/ann11.txt`)
show `EAX`=slot, `EDX`=type, `EBX`=id at **every** site (the pass-76 "18/21" gap was a scan
false-negative). Per-site (slot, type, id source):
- 0x453b07 s0 t1 id = `FUN_0048525c` pair-store ECX lane (`{DAT_0062f468, DAT_0062f46c}`
  home/away-ordered; which side lands in EBX [H])
- 0x453b15 s2 t2 id = 0
- 0x453b2e s3 t2 id = `FUN_00489920` remmap table `DAT_0057e3fe` (EAX=SI team, EDX=2, default
  `0xa1`) — remap feeds **schedule** (type-2) ids, not standings
- 0x453bd2 s1 t2 selector `0x26`; 0x453c77 s4 t2 selector `0x36`
- 0x45437d s0 t3 selector `0x1b`; 0x454398 s0 t1 `FUN_00485240`; 0x4543bd s1 t3 selector `0x16`
- 0x454da5 s0 t3 selector `0x18`; 0x454dc0 s0 t1 `FUN_00485240`; 0x454de5 s1 t3 selector `0x13`
- 0x4554c9 s0 t3 selector `0x17`
- 0x455a81 s0 t3 selector `0x19`; 0x455a9c s0 t1 `FUN_00485240`; 0x455ac1 s1 t3 selector `0x14`
- 0x455f75 s0 t3 selector `0x1a`; 0x455f90 s0 t1 `FUN_00485240`; 0x455fb5 s1 t3 selector `0x15`
- 0x458dc2 s0 t1 id = `ESI` (SI team id, gate `FUN_004dac90() >= 7`)
- 0x458df6 s1 t3 selector `0x0`
- 0x4591fd s2 t3 id = branch constant `0x4`/`0xa` (jump lanes `0x4591d8/0x4591e7`)
Selector pair: `FUN_00489974(EAX=selector)` fills `DAT_0062fbf8[]` (count `DAT_0062fd98 <= 0x28`)
from range `&DAT_0057e938/93c[selector]` via `FUN_00489bdc`; `FUN_004899dc` picks cursor
`DAT_0062fd9c` or tick-fraction ROUND index, `-1` sentinel. Scene handler
owners `FUN_00453a6c/454338/454d60/45548c/455a3c/455f30/458cf0` fill then arm `FUN_004894c4`.
Evidence: `analysis/pass77pbp/ann1.txt`, `ann11.txt`.

Pass-78 (2026-11-19): the **selector -> range grid is static initialized data** at VA
`0x57e938` (raw `0x17cf38`, u32 stride 4): `FUN_00489974` reads start = `v[selector]`,
end = word at `0x57e93c + selector*4` = `v[selector+1]`, so range = `[v[s], v[s+1])` (the
"parallel arrays `DAT_0057e938/93c`" are one overlapping grid). Measured values: grid A
(s = 0x00..0x1c) partitions records 0..160 as nineteen 5-blocks `[0,95)`, three 10-blocks
`0x13:[95,105) 0x14:[105,115) 0x15:[115,125)`, seven 5-blocks `[125,160)`; a second uniform
5-block grid starts at index `0x22` (s = 0x22+k -> `[5k,5k+5)`), giving the live selectors
their ranges: `0x26:[20,25)`, `0x36:[100,105)`, `0x0:[0,5)`, `0x13:[95,105)`,
`0x14:[105,115)`, `0x15:[115,125)`, `0x16:[125,130)`, `0x17:[130,135)`,
`0x18:[135,140)`, `0x19:[140,145)`, `0x1a:[145,150)`, `0x1b:[150,155)`. Word `v[0x1d]`
(`0xa0->0x29` wrap) is adjacent data, not a selector entry. Pool size 160 is corroborated
at asset level: shipped `AUDIO/PBP/STANDING` is exactly `A_STD001..160.SMP` +
`G_STD001..160.SMP` (160+160, measured), so list ids `1..160` number the standings-file
lane 1:1; what the records mean (which standings slots) stays [H].
Fill semantics: the loop walks `rec` through `[start, end-1]`; `FUN_00489bdc` is a
**skip predicate** `(in_EAX=type, ECX=record)` returning 0 = skip — a hardcoded per-record
exclusion table whose constants are the same grid's words (measured: type-2 skips
{23, 31, 34, 37, 38, 50, 62, 85, 133}; type-3 skips {5, 6, 7, 8, 11, 13, 17, 18, 39, 43,
44, 46, 47, 50, 53, 54, 95, 96, 97, 98, 120, 121, 122, 125, 127, 130, 131, 132}; with
`DAT_00640fe8==1` -> `FUN_00489bb0` override and `==3` one extra skip). Surviving records
are stored as `record + 1` (`DAT_0062fbf8` holds 1-based record numbers) at
`DAT_0062fbf8[count++]` with the `0x27`/range-end guards — those ids land in the loader
`FUN_0048953c` type-3 filename `{G,A}_std%03d.smp`, whose shipped numbering
`001..160` (measured gapless, both prefixes) covers the full list range exactly;
type-2 ids land in `SCHEDULE` numbering `000..217`. Evidence: `analysis/pass78grid/`
(`sel1.txt` pseudocode, `grid_dump.txt` raw grid + skip-constant words, `assets.txt`
file inventory, `GhidraWc98Sel1.py`).

Pass-79 (2026-11-19): selector mirror + remmap ported, gate-driven (no scene calls them —
screen-to-handler dispatch stays [H], see pass-77 residual; closed by pass-82). Implemented in `app/game_audio
.hpp/cpp` as `Announcer::fillSelector/pickSelector/resetCursor`, `Announcer::remmap`,
`Announcer::pickNext` and free helpers `feedTeamSelectionAnnouncer` (0x458dc2 team bank +
0x458df6 selector-0x0 std lane) and `feedBoardAnnouncer` (0x453b07/0x453b15/0x453b2e/
0x453bd2/0x453c77): each fills the ring, runs the builder load, and plays exactly **one**
entry per call — the exe pick lane `FUN_0048d0dc` plays one announcement per pick, so the
feed never stacks activations; further announcements ride later `pickNext()` calls
(slot-scan order, class-queue order [H]).
**remmap direction corrected from the raw table** (`FUN_00489920`,
`analysis/pass77pbp/ann1.txt` pseudocode + raw bytes `analysis/pass79sel/remmap.txt`): the
scan compares `EAX` against the table's **second** u16 of each u16 pair
(`*(int*)(base+i) >> 0x10`) and returns the **first** u16 of the *next* slot
(`*(int*)(base+i+2) >> 0x10`), i.e. the mapping is shifted by one slot: team `0x056` →
`0xa1`, `0x057` → `0xa2`, … team `0x2c0` → the adjacent word `0xd6`; unmatched teams return
the default word `0xa1`. Values are tournament schedule ids (the shipped `G_SCH161..214`
numbering), keys are SI team ids (40 entries, scan bound `iVar1 < 0xa0`). Gate numbers:
`remmap(0x56)=0xa1`, `remmap(0x1a)=0xbd`, `remmap(0x2c0)=0xd6`, default `0xa1`.
Grid/skip semantics ported verbatim: grid A 5/10-block partition + wrap word `0xa0`
(`fillSelector(0x1d)` = adjacent word, no entry), grid B uniform blocks
(`0x26:[20,25)` minus type-2 skip `23` → ids `{21,22,23,25}`, `0x36:[100,105)` →
`{101..105}`, `0x0:[0,5)` type-3 → `{1..5}`), survivors store `record+1`, count guard
`<= 0x28`. Pick = entry 0 stand-in (exe tick-fraction lane active: mode flag
`DAT_0057e600` static dword 0, its only store is the never-invoked setter 0x489ba4;
cursor lane unreached, pass-101 byte scan).
Gate: `test_gameaudio` pass-79 block (remmap lanes, grid/skip list counts + pick sequence,
both feed sites on real assets `G_T0026.PBK`, `G_STD001.SMP`, `G_SCH000/021/101/189.SMP`,
one-pick-per-call plays counter, drained-queue lane). Evidence: `analysis/pass79sel/NOTES.md`
(`remmap.txt` = raw table dump + mapping derivation).

Pass-82 (2026-11-20): **screen-to-handler dispatch closed** — the residual
`[H]` from pass-77/79 ("no scene calls them") is wrong: every announcer board-handler
owner has exactly one direct `E8` call site in an FE scene-dispatcher region
(raw scan, AUTO delta `VA = raw + 0x400C00`; evidence `analysis/pass82dispatch/`:
`an1.txt` handler pseudocode, `an2.txt` fill-loop helpers, `an3.txt` instruction
dumps + gate helpers).

Call sites and scene attribution (region string/fourcc/state evidence):

| handler owner | call site | dispatcher region evidence |
|---|---|---|
| `FUN_00458cf0` | `0x458806` | state-name stores `mov dword[0x57d79c]="stg2"/"stg3"` (`0x458860/0x458878`), caption `BARDIRECTION` (VA 0x56360c), fourcc `back` load `0x4587b1`, `24ef` fourcc `0x458821` — group-stage standings board |
| `FUN_00453a6c` | `0x453138` | `stg2/stg3` stores `0x45302b/0x453043`, scene string `mult` (VA 0x562e44) + `ow11`, fourcc `eras`/`back` — multiplayer/standings board dispatcher |
| `FUN_00454338` | `0x4542c9` | caption/asset strings `am75` (VA 0x563078) |
| `FUN_00454d60` | `0x45463b` | captions `arks`,`am79`,`ow74`,`ow79`,`am89`,`ow20` (VA 0x563090..0x5630f0) |
| `FUN_0045548c` | `0x455064` | captions `ow37`,`ow56`,`ow06`,`ow91`,`ls%02d` (VA 0x563148..0x5631a8) |
| `FUN_00455a3c` | `0x45574b` | feature captions `red.bin`,`\spcred.bin`,`Take a Dive`,`Big Heads` (VA 0x5631e4..0x563244) |
| `FUN_00455f30` | `0x455d5b` | feature captions `Ball`,`…n Model`,`ffn` (VA 0x56326c..0x5632cc) |

Exact scene-name ids for the five caption-board dispatchers stay **[H]** (regions
carry caption/asset strings, not scene-id stores); the two board handlers above
carry `stg2/stg3` state names verbatim.

Caller pattern (identical at both board sites, `an3.txt` disasm `0x4587f6..0x458812`,
`0x453123..0x453144`): `FUN_00489350` announce-open → gate chain →
`FUN_00489b0c(scene)` returns non-zero ⇒ handler skipped — else handler runs —
then `FUN_00489af4(scene)` appends the scene id to the announce-list
`DAT_0062fafc[DAT_0057e374++]`. The `0x458806` site adds `FUN_00485364`
(gate `[announcer-record+0x431c] == 0`) before the list gate, and the
`0x453138` site adds `FUN_00489a80` (tick-fraction flag lane, `DAT_0057e5f8/5fc`,
constants `0x5669a8/9ac/b0`, semantics [H]).

Gate `FUN_004dac90` byte-count `>= 7` (inside both handlers): walks the 17
six-byte speech-deck records at the `FUN_004cc060` buffer, counts non-empty
decks — fewer than 7 loaded decks ⇒ no announcement. Phrase selectors verified
instruction-level (`an3.txt`): `FUN_00458cf0` fills `0x0/0x1/0x2/0x3` per match
type (jump table `0x458ce0`), then `0x5` win / `0xb` loss / `0x8` draw (mode byte
`[0x6099c0]==1`, team-status byte `< 2` at `DAT_0060a0a4` record), `0xe` after
`FUN_00498e90` team-check, `0x7` after both `FUN_004998c0` checks, and
`0xf/0x10/0x11/0x12` per slot `EDI=0..3` when the draw flag is set;
`FUN_00453a6c` fills first pass `0x23/0x29/0x26` then commit pass
`0x33/0x39/0x36` (second grid, base `0x22`) on the record `+4/+6/+7` result-code
1/2 combinatorics, with `FUN_00489920` remmap at `0x453b2e` (pass-79).
`FUN_00489974` second arg (`EDX=2`) is not read by the fill loop — the loop only
consumes `EAX` (selector) + candidate filter `FUN_00489bdc` (class 2/3 skip list
by sentence id, language mode `DAT_00640fe8 ∈ {1,3}` fallback `FUN_00489bb0` /
xor-swap, pass-79 grid mirror).

Port: **no code change** — pass-79 `feedTeamSelectionAnnouncer`/`feedBoardAnnouncer`
already mirror the two board fill sites and stay gate-driven; this pass only
closes the dispatch residual (the port has no board scene that calls them, which
matches the exe only in structure, not runtime behavior).

## Pass-83: type-1/2/3 id-source accessors decoded (measured 2026-11-20)

Closes the pass-76 residual half "accessor internals [H]" (presentation fill-site
`{slot,type,id}` argument extraction). Disassembly windows around all 21 `FUN_004894ac`
sites (`analysis/pass83accessors/ac2.txt`) give exact provenance per site.

- `FUN_00485210` membership gate: Watcom `EAX` needle scans the u32 list at
  `DAT_0062f790` for `DAT_0062facc` entries (`needle == [list+i]`), returns 1 on hit,
  0 on zero-count or miss.
- `FUN_00485240` id selection: `FUN_00485210() == 0 ? DAT_0062fab4 : DAT_0062fab0`
  (miss -> away id, hit -> home id). Id writers: `FUN_00484950`, `FUN_00484d1c`,
  `FUN_004852e0` (tournament registration writes to `0x62fab0/4`).
- `FUN_0048525c` pair store (`EAX`=dst0, `EDX`=dst4; site `0x453af3`
  `LEA EDX,[ESP+4]; MOV EAX,ESP`): writes `{DAT_0062f468, DAT_0062f46c}` as
  (f468, f46c) when `FUN_00485240() == DAT_0062fab0` (hit) and reversed on miss.
  Writers: `FUN_00485334`, `FUN_00484d1c`. The `mov ebx,ecx` id at `0x453b07` reads a
  register whose provenance around the call stays [H]; the port keeps the direct team id
  for that lane.
- `FUN_00489bdc` predicate class branch keys on `1 < EAX < 3` / `== 3` (selector index
  2/3 only; every other selector returns keep-1), exclusion compares against the static
  grid-window base words `DAT_0057e93c..DAT_0057ea28` (+offset), plus `DAT_00640fe8 == 1`
  -> `FUN_00489bb0` and `== 3` xor-swap on one window id. The port's static `kSkips2/3`
  sets stay the shipped-grid mirror (pass-78); their effect on the tested selectors is
  empty-set either way (values fall outside the tested blocks).
- `FUN_004899dc` pick: `DAT_0057e600 != 0` -> cursor lane over clamped `DAT_0062fd9c`
  into `DAT_0062fbf8[cursor]`; count 0 -> `-1`; else time index
  `DAT_0062fbf8[ROUND(tick split)]` via `FUN_004c321c` tick + `FUN_004973b0` (x87
  `ROUND`, no scaling - the split words feed the FPU stack directly). Byte scan (pass-101):
  `DAT_0057e600` (initialized DGROUP, static dword 0) has one read - 0x4899e0 - and one
  store only inside the setter 0x489ba4 (no E8/E9 caller, no imm32 reference to
  0x489ba0/4/6) -> retail stays on the tick lane; the cursor lane (clamp store 0x4899fc,
  cursor never increments) is unreached. Port returns entry 0 as a stand-in, tick lane
  unmirrored [H]; `-1` sentinel matches.
- `FUN_00489974` fill loop confirmed: window `start = *(int*)(DAT_0057e938 + EAX*4)`,
  `end = (&DAT_0057e93c)[EAX]`, append `id + 1` while predicate keeps, count cap `0x28`,
  early return at `end - 1` and cursor cap boundary.

Site map (handler -> fills, all via `mov edx,type; mov ebx,id; mov eax,slot`):
`FUN_00453a6c` pair-store -> slot0 t1 (`0x453b07`), slot2 t2 id 0 (`0x453b15`),
`FUN_00489920(SI)` -> slot3 t2 (`0x453b2e`), fill(`0x26`)+pick -> slot1 t2 (`0x453bd2`),
fill(`0x36`)+pick -> slot4 t2 (`0x453c77`).
`FUN_00454338/454d60/455a3c/455f30`: scene gate word `DAT_006099c4/0x609b08/0x609d90/
0x609ed4` zero -> fill(`0x1b`/`0x18`/`0x19`/`0x1a`)+pick -> slot0 t3, else
`FUN_00485240` -> slot0 t1; then fill(`0x16`/`0x13`/`0x14`/`0x15`)+pick -> slot1 t3
(`0x4543bd`/`0x454de5`/`0x455ac1`/`0x455fb5`). `FUN_0045548c`: deck gate `AL < 7` RET,
fill(`0x17`)+pick -> slot0 t3 (`0x4554c9`). `FUN_00458cf0`: deck gate `>= 7`,
`setSlot(0, 1, ESI=SI raw)` (`0x458dc2`), jump-table branch fill(selector 0)+pick ->
slot1 t3 (`0x458df6`), pick lane slot2 t2 (`0x4591fd`, jump-table arm with `EAX` id).

Port mirror: `app::Announcer::isMember/selectedTeamId/storePair` + feed setters
`setMembership`/`setTeamIds`/`setPairIds` (`app/game_audio.hpp/cpp`, pass-83 block after
`remmap`); gate `tests/unit/test_gameaudio.cpp` pass-83 block pins empty-list miss,
hit/miss id selection, and the (f468,f46c)/(f46c,f468) store ordering. Feed helpers
stay gate-driven (dispatch closed pass-82; needle provenance at the fill sites closed
by pass-84, Pass-84 section).

Evidence: `analysis/pass83accessors/` (`ac1.txt` accessor decompiles, `ac2.txt` 21-site
disassembly windows, `ac3.txt` `FUN_00485210`/`FUN_00489bdc`/`FUN_004973b0` decompiles +
DAT write-reference attribution for `0x62f468/62f46c/62fab0/62fab4/62fd98/62fbf8`),
Ghidra scripts `GhidraWc98Ac1..Ac3.py`.

## Pass-84: fill-site needle provenance closed (measured 2026-11-20)

Retracts the pass-83 residual "needle provenance at the fill sites [H]" - every `EBX`
id feeding the 21 `FUN_004894ac` (`EAX`=slot, `EDX`=type, `EBX`=id) sites now traces to
a source:

- Raw accessor bytes (file scan, no pseudocode): `FUN_00485210` = `PUSH ECX; PUSH EDX;
  MOV ECX,EAX; MOV EDX,[DAT_0062facc]; TEST/SHL EDX,2; loop CMP ECX,[EAX+0x62f790] ->
  JE found / CMP EAX,EDX; JL loop`; needle = `EAX`, ECX/EDX scratch with PUSH/POP
  restore. `FUN_00485240` = `MOV EAX,[DAT_0062fab0]; CALL FUN_00485210; TEST EAX,EAX;
  JZ -> RET [DAT_0062fab4]; hit RET [DAT_0062fab0]` (needle is the stored id, not a
  caller arg). `FUN_0048525c` = `PUSH ECX; MOV ECX,EAX; CALL FUN_00485240; CMP EAX,
  [DAT_0062fab0]; JNE swap; *ECX=[0x62f468]; *EDX=[0x62f46c]; POP ECX; RET` - the
  compare tests the **selection result**, and ECX is PUSH/POP-preserved across the call.
- `0x453b07`: `CALL 0x48525c; MOV EBX,ECX` - id = handler's packed-team-pair `ECX`
  upper word (`SAR ECX,0x10` at `0x453ab6`), preserved through the pair store.
- Constants: `0x453b15` id 0 (slot 2 t2), `0x45615f` id 1, `0x455a04` lane fixed triple
  slot `0xd`/type `0xa`/id `0xb` feeding the `0x485090` gate (not a fill).
- `0x453b2e`: `MOVSX EAX,SI; CALL FUN_00489920; MOV EBX,EAX` - remmap of loop var.
- Deck lane `0x458dc2`: `MOV EBX,ESI` (deck gate `CALL 0x4dac90`, `CMP AL,7`).
- Selector lanes `0x454379/0x454dde/0x455a7d/0x455a98/0x455aba/0x455f71/0x455f8c/
  0x455fae/0x458ded/0x4591f6`: `MOV EBX,EAX` = `FUN_00489974->FUN_004899dc` or
  `FUN_00489920` return.
- `0x458dd7-0x458dd0`: jump table `[EAX*4+0x458ce0]` selects the branch fill lanes.

No remaining [H] needle provenance; no port change needed (id sources are already
mirrored pass-83/79). Evidence: `analysis/pass84needle/` (`n1.txt` - all 36 direct
call refs to `FUN_00485210`, containing-function decompiles, -90-byte disassembly
windows at all 21 fill sites; `GhidraWc98N1.py`).

## Pass-85: presentation announcement flush lane decoded (measured 2026-11-20)

Completes the announcement lifecycle around the 9 pending-setter sites:

- `FUN_00489350` pending setter (callers `0x453123/453a74/4542b9/45462b/455054/
  0x45573b/0x455d4b/0x4587f6/0x458cf9` - all presentation handlers): if
  `DAT_0057e378 == 1` RET; else loop 6 slots, `CALL FUN_00479a92` reserves ids into
  `DAT_0057e3e8+i`, zeroes per-slot pending words `[slot+8]`, sets `DAT_0057e378 = 1`
  (only absolute write site, raw 0x4893a6).
- Flush lane `FUN_004893ec` (46 raw-scan call sites, all presentation handlers): gates
  `FUN_00490674` = `DAT_00632774` speech-enabled, `FUN_0048fd4c` = `DAT_006326b0`
  speech-init, `DAT_0057e378 != 0`; `while FUN_004dfc0c() == 3 { FUN_004dfb94(); }`
  stops the playing sentence (`FUN_004dfc0c` = `SNDsentencestatus` `cmn\s\speech.c:951`
  -> 2 not-started/3 playing, `SPEECH_NOT_INITIALIZED` `0xfffffff2` when
  `DAT_006597b0 == 0`; `FUN_004dfb94` = `SNDsentencestop` `speech.c:0x34d` ->
  `FUN_0052bc04/FUN_004e2c60 x2/FUN_004dfe18/FUN_0052bc58`, `DAT_0064b15a = 0`);
  wait `FUN_0048fd38()` (status-clear poll); `FUN_004901e0()` speech-queue drain
  (`(*DAT_00558464)(DAT_00632728,...)` + qfetch loop `FUN_004c4890`/`FUN_004d8aa0`/
  `FUN_004c4800`, `(*DAT_00558528)` end); `FUN_00489460()` builder; release 6 reserved
  ids (`DAT_0057e3e8..+0x18` -> `thunk_FUN_0049f750`, refcount + flag-bit `+3 & 0x40`
  linked-list splice); clear `DAT_0057e378/57e37c`.
- Semantics: a scene handler that wants new announcement ids marks pending; the pump
  flush stops the current sentence, drains the speech queue, rebuilds slot ids, and
  releases reserved ids. Live-match scenes never fill pending slots (all 9 pending-setter
  callers are presentation handlers) - so A7's live-match phrase content mapping remains
  the open half; no port change (Announcer feed helpers already gate-driven).

Evidence: `analysis/pass85flush/` (`p1.txt` fill/builder refs, `p2_scan.txt` 46 flush
call sites, `p3_scan.txt` pending-flag write attribution, `p4.txt` pending setter +
gate helper decompiles; `GhidraWc98P1.py`, `GhidraWc98P4.py`).

## Pass-86: live-match commentary dispatch lane decoded (measured 2026-11-20)

Closes A7's open half: the in-play event→phrase lane is the registered `SNDstartSpeech`
handler cluster (`FUN_00490528`, site 0x49059b `CALL FUN_004e6b50` with
EAX=0x48f730 start handler, ECX=0x48f78c presentation lane, pushes 0x48f830 record lane +
0x48f8bc stop-all sweep; gate `FUN_0048fd4c` = speech-init `DAT_006326b0`), pointer-invoked
— zero static `E8` callers of 0x48f7a4/0x48f830/0x49001c/0x490094:

- Start handler `0x48f730`: rejects while `SNDsentencestatus FUN_004dfc0c == 3` (a sentence
  speaks); oldest-pending scan `FUN_0048f664`; scene-3 word lookup `FUN_0048d070` → word
  entry; **phrase = `[entry+0x0E]`** (the A7 `[item+0E]` residual); `SNDphraseadd
  FUN_004df968` appends into the 20-slot ring, entries `DAT_0064b068` stride 0xc (cap 18;
  the u32 `-100` free-marker at entry+8 is what `SNDphraseadd` addresses as `DAT_0064b070`),
  slot id →
  `[entry+6]`; `SNDflush FUN_004dfb40` starts the engine.
- Record lane `0x48f830`: percent-count gate `FUN_004906a4(3)` = `pct*0x7f/100` min 1 (not a
  probability); record from free queue `DAT_006326c8` (`qfetch FUN_004c4890`), fields
  `+0x1c` = dispatcher `0x48f47c` (8-arm table `0x48f45c`, class byte `rec+0x20 & 0x3f`,
  arms prep `FUN_0048d2f0` + `SNDphraseadd`), `+0x24` = start result; driver `FUN_0048f3e0`
  callbacks {`0x48f380`, `0x48f3a4`, engine `0x49001c` gate `DAT_00632724`}; success → active
  queue `DAT_006326e4`; **reject → record recycled to `DAT_006326c8` = event phrase DROPPED,
  never deferred, no interrupt**.
- Tick sweep `FUN_0048f920` (pump host `FUN_004906f8`): word-status `FUN_004dfa60 == 7`
  (finished) → release word, recycle to `DAT_006326c8`.
- Weighting: dispatcher arms class `>>6` → scene for `FUN_0048d070` + byte table
  `DAT_0057eb54[class]` (8 direct-indexed bytes, corrected from the earlier `[class*2]`
  wording); class 7 consults the PHRS preload slot pair `DAT_00641014`/`DAT_00641110`
  (stride 0xFC pointer slots, pass-94: each holds a team-record image pointer, gate
  u16 = field `+0x34` of the pointed-to record). Boot pool `FUN_0048fb10` registers
  `AUDIO\TEAM\A_T%04d.PBK` (fmt 0x566bac) / `AUDIO\PLRS\P%05d.PBK` (fmt 0x566bc4) word
  banks, node pools stride 0xc (accessor stub 0x48f704).
- `FUN_004906a4` semantics correction: percent→count, not probability gate.

Port mirror: `Commentary::triggerLive` (busy-reject + drop, `rejected()` counter) wired in
`livematch.cpp`/`match_demo.cpp` event drains; `trigger` keeps the presentation seam.
Gates: `test_commentary` live-lane block (accept → reject → accept) + selftest
`match-audio-events` PASS.

Evidence: `analysis/pass86speech/` (`t4.txt` driver/callback decompiles, `t9.txt` live-host
disasm 0x48f7a4-0x48fb80, `t10.txt` boot registration 0x490540-0x490600 + `FUN_00490528`,
`t11.txt`/`t13.txt` cluster helper decompiles, `t12.txt` stub handlers + driver prologue +
stop-all sweep, `t3/t6/tick_scan.txt` caller/pointer scans; `GhidraWc98T2..T13.py`).

## Pass-87: A7 selector consumer chain + A8 beat gate operand closed (measured 2026-11-20)

- `DAT_0057eb54` is file-backed (raw 0x17d154): 8 bytes `[0,0,0,0,0,0,1,0]` + pointer field
  0x566c30 at +8 — not a weights array. Dispatch arm `0x48f317`: `BL = table[class]`,
  selector `EBX = 2*BL + (classByte>>6)`, `EDX = [rec+0xC]` (word pointer), `EAX = 3`.
- Consumer chain (`q1.txt`): `FUN_0048d070` = `CALL 0x48d0dc` (EBX PUSH/POP-protected; queue
  slot `DAT_00632428 + EAX*0x1c`, `qfind FUN_004c4ce0` with callback `0x48cf10` and
  EBX = saved `[rec+0xC]`) → `ADD EAX,0x14; MOV EDX,EBX; JMP 0x4c4d50` — selector arrives as
  EDX of `qfindindex FUN_004c4d50`, walked by `FUN_004c45d0`
  (`DEC EDX; while(EAX && EDX!=-1) EAX=[EAX]`) = selector-th successor of the queue head
  `[obj+8]` — selector 0 = head node itself (qcreate zeroes `[obj+8]`; NULL only if the queue
  is empty or the chain ends early). Match
  callback `0x48cf10` (`q4.txt`): `ECX = [EDX]; CMP ECX,[node+0xC]` — tag equality by value.
  Net: entry index = `2*table[class] + (classByte>>6)`, class 6 → +2; queue-entry selection,
  no probability scaling.
- A8 beat gate (`q2.txt`/`q3.txt`): entry `{u16 slot idx, u16 threshold @+2, u32 id @+4,
  handle @+8}`; fire gate `0x4dfd45`: `SAR entry>>16` (= threshold), `CALL 0x52c780` = getvol
  of handle `DAT_00588d74` (server table `DAT_006597e8` record volume byte at `rec+0x3b`),
  `CMP vol,threshold; JG skip` — beat plays only when `vol <= threshold`; all add sites pass
  `EBX = 0` → retail beats fire only under zero group volume.
Evidence: `analysis/pass87beat/` (`q1..q4.txt`, `GhidraWc98Q1..Q4.py`).

## Pass-88: A7 selector mirrored in the port; pass-89 re-decoded the class-byte producer (2026-11-20)

- Port mirror (`app/commentary.hpp/cpp`): `Commentary::entryIndexForClass(classByte)` =
  `2*DAT_0057eb54[class] + (classByte>>6)` with the measured table bytes `[0,0,0,0,0,0,1,0]`;
  `startPick(selector)` picks the selector-th successor of the ready-list head node (index 0 =
  head) and erases the picked entry on activation (qremove `FUN_004c4a70` semantics), short/empty
  chain -> NULL pick silent. The live seam calls it with class byte 0: the event->class
  alignment was OPEN at pass-88 write time; pass-90 below decoded the stamp (nested
  segment-TOC name byte, tags {0x07, 0x40}, no selector-class tags), so the selector never
  fires for retail records and the seam's sequential consumption stays a port deviation [H]
  (pass-90 callback-lane dump below publishes a registry index, not a queue pick), while
  class-7 preload routing is a second open alignment.
  Gate: `test_commentary` selector-mirror CHECKs (classes 0..5/7 -> head index, class 6 -> +2,
  scene bit 0x42 -> +1) on top of the unchanged sequential + live-lane blocks.
- Class-byte producer (RETRACTED pass-88 conclusion; re-decoded pass-89,
  scripts `/tmp/wc98pass87/GhidraWc98R{1..5}.py`, outputs `r{1..5}.txt` +
  `analysis/pass89sel/NOTES.md`): `[rec+0x20]` IS statically stamped — record driver `0x48f3e0`
  does `LEA EDX,[EBX+0x20]; CALL 0x48ffe8` (`r1.txt`), and `FUN_0048ffe8` runs the sound-registry
  find `0x4a4a00` (`CALL 0x4a4820` -> registry find `0x4a45d0`; out slot `DAT_006419e0`
  receives the found member offset (`004a4a08` write, `004a4a1e` read-back), key =
  the record driver arg struct word `[EAX]`/`[EAX+4]`) then `0x4a4a30` (copies the found node's
  byte-chain), and stores the first chain byte at `[rec+0x20]` (`r2.txt`/`r3.txt`). The pass-88
  writer scan missed it because `MOV byte ptr [ESI],AL` indexes through a register, not a
  literal `+0x20` displacement. Registry population is a load-time lane (`0x4a45d0` has the one
  find-site); pass-89 left event->class alignment OPEN [H] — decoded by pass-90 below (the
  registry base is `TYPE.PBK`; the stamped byte is the nested segment-record name byte 0).
  Pass-89 also fixed the selector domain: dispatch jump
  table `0x48f2bc` keys `[rec+0x20]&0x3f` DEC'd — entries `{skip, selector, skip, selector,
  skip, selector, PHRS-arm 0x48f354}`, i.e. classes **2/4/6 only**; classes 0/1/3/5 skip
  selection (engine callback `0x48f442`), class 7 consults the PHRS preload table
  (`DAT_00641014` lane, pass-86). Port `entryIndexForClass` mirrors the domain with
  `kNoEntry` sentinel; `trigger()` no longer short-circuits on an empty pool (gate stop+flush
  runs ahead of the pick, exe `FUN_004893ec` order) and `triggerLive` counts a pool-exhausted
  record as recycled (`rejected()`, free queue `DAT_006326c8`). Gate: `test_commentary`
  selector-mirror CHECKs classes {0,1,3,5,7,0x3f} -> `kNoEntry`, {2,4} -> head, {6} -> +2,
  {0x42} -> +1, {0x46} -> 3, plus pool-exhausted gate block.

Evidence pass-89: `/tmp/wc98pass87/GhidraWc98R{1..5}.py` -> `r{1..5}.txt` (driver body,
`FUN_0048ffe8`, registry helpers `0x4a4a00/0x4a4a30/0x4a4820`, pick-loop jump table read at raw
VA `0x48f2bc`) + `analysis/pass89sel/NOTES.md`; the pass-88 writer-scan evidence
(`q5..qe.txt`, `analysis/pass88sel/NOTES.md`) stays as the retracted route's record.

## Pass-90 (2026-11-20): registry base proven = `TYPE.PBK`; class byte = nested segment-TOC name byte
(RETRACTING this pass's first write-up: it read lookup #2's base as `block + off>>16` and
censused only the OUTER TOC names — the type-2 slot shift is computed, `MOV ECX,4; NEG ECX;
LEA ECX,[ECX*0x8+0x20]` -> `CL = (-4*8)+0x20 = 0` -> `SHR EAX,CL` keeps the **full u32 value**,
so lookup #1 returns the member's full offset and lookup #2 walks the member payload, not the
outer TOC. `r6.txt` type-2 lane `0x4a46a0..0x4a4715`: off slot `0x4a46d0..0x4a46e4`, size slot
`0x4a46f3..0x4a4708`.)
- Population (`/tmp/wc98pass87/r8.txt`, `analysis/pass90reg/l1.txt`): boot loader
  `FUN_00490250` loads `PHRS.HDR` (literal name string `0x566c00`) into `DAT_00632730` and
  **`TYPE.PBK`** (literal `0x566c0c`, path formatted by `0x48f9d0`, loaded via `0x439810`)
  into `DAT_00632734` — the base every driver lookup searches. The block reader `FUN_004a44b0`
  classifies blocks by magic: BE 3-byte `C0FB` -> type-1 lane (`base+0x6`, 24-bit BE fields),
  BE `BIGF` -> **type-2 lane** (`base+0x10`, `{u32be off, u32be size, NUL name}` = raw BIGF
  TOC, numeric hit: counter == key, **return pointer = record NAME (`EBX+8`)**, records advance
  nameLen+9; out slots shifted by `CL = (-typeCount*8) + 0x20` (`MOV ECX,4; NEG; LEA` at
  `0x4a46d0/0x4a46f3`) = 0 for type 2 -> **FULL off**).
- Driver pair (`r6.txt`): `0x4a4a00` (lookup #1, key `[struct+0]` = event phrase id) returns
  `base + off` = the outer member's payload pointer; `0x4a4a30` (lookup #2, key `[struct+4]` =
  segment index) walks **that payload** and copies the found record's byte-chain into
  `&[rec+0x20]`, driver stores byte 0. Every TYPE.PBK member payload is itself a nested `BIGF`
  segment TOC (the PHRS phrase->segment mirror, `SPEECH_INDEXING_NOTES.md` S2), so the class
  byte = **inner segment-record `#struct+4` NAME byte 0**.
- Census (`analysis/pass90reg/census.txt`): outer TOC 1,290 records, all payloads nested BIGF
  (268 with `count=0`); inner records 4,665 (= PHRS segment count, pass-16); inner names are
  exactly 1 byte, 462 non-empty, histogram `{0x07: 211, 0x40: 251}`. `0x07` names cover 139
  phrases (range 139..1017, mostly one segment each) -> **PHRS preload arm** `0x48f354`
  (`DAT_00641014` table lane, pass-86). `0x40` names are segment `#0` of 251 phrases (range
  1035..1289) -> gate `TEST byte,0x3f` == 0 -> **engine callback** (selector skipped, bits
  6..7 unused). Untagged / miss / `count=0` nests stamp `0` -> engine callback. **No tag is in
  {2,4,6}**: the jump-table `0x48f2bc` selector arms are dead code in retail WC98 — that part
  of the pass-89 conclusion stands — but the class-7 preload arm is REACHABLE (139 phrases),
  correcting pass-90's first "all arms dead" claim.
- Callback-lane dump (`analysis/pass90reg/cb2.txt`, forced function `0x49001c`): gate
  `iRam00632724` (`DAT_00632724`) nonzero -> `func_0x0048ff64` = `0x4a4820`/`0x4a4a00`/
  `0x4a4820` registry find; no `qfindindex`/`qfind` on this path. The original write-up of
  this bullet claimed `*out = EBX*2` (2× the found registry index) — **RETRACTED pass-92**:
  the full wrapper disasm (`analysis/pass92cbr/e5.txt`) shows only
  `0048ffa4 MOV EAX,[ESP]` / `0048ffa7 MOV EBX,[ESP+0x4]` / `0048ffab ADD EAX,EBX` /
  `0048ffad MOV [EBP],EAX`, i.e. the out slot receives the SUM of the two located-slot
  values, and no doubling/shl instruction exists in the dumped callback body. The conclusion is unchanged:
  the engine-callback lane PUBLISHES a registry-derived value, and the port's
  sequential head pick is NOT proven retail — it stays deviation [H]; class-7 routing to the
  preload lane is a second open alignment. `entryIndexForClass` remains documentation of the
  unreachable selector arm.
- Cross-file check (`analysis/pass90reg/phrs_walk.txt`): walker over `PHRS.PBK` nested TOCs gives
  the SAME `{0x07: 211, 0x40: 251}` over 4,665 inner records, and `TYPE[j] == PHRS[j][:len]`
  byte-exact for all 1290 j — the tag census is retail PHRS truth, correcting
  `SPEECH_INDEXING_NOTES.md` §2's "zero non-empty names in any PBK TOC" (outer TOC names are
  empty; the NESTED segment TOCs carry the tags).
- Gate: `test_audio.cpp` pass-90 block — outer `BigArchive::parse(TYPE.PBK, tolerant)` + nested
  `BigArchive::parse` per member: 1290 outer, 0 non-BIGF payloads, 4,665 inner records, name
  histogram `{0x07: 211, 0x40: 251}` (462 names).

## Pass-91: class-7 preload arm decoded (measured 2026-11-20)

Evidence `/tmp/wc98pass91/c7b.txt` (decompile `FUN_0048f2d8` switch + `FUN_0048f290`),
`c7c.txt`/`c7d.txt` (full instruction dump 0x48f290..0x48f4fe); notes
`analysis/pass91c7/NOTES.md`.

- Selector/class dispatch `FUN_0048f2d8` (the jump-table `0x48f2bc` consumer):
  `switch(byte[rec+0x20] & 0x3f)` — cases 2/4/6 run the entry-selector arm `0x48f317`
  (pass-87/88 chain); **case 7 is the PHRS preload arm `0x48f354`**, cases 0/1/3/5 fall
  through silent. Return gate: success iff a slot was filled (`*slot != 0`), else the record
  recycles (dispatch 0).
- Preload arm (`c7d.txt`): `EAX=1; EBX=0; EDX=0; CALL 0x48f290` → store `[EDI]` (home slot,
  through the pointer loaded from `DAT_006326b0`); `EBX=1; EDX=1; EAX=1; CALL 0x48f290` →
  store `[ECX]` (away slot, dispatcher stack `[ESP+0x10]`). Two accessor calls, selectors 0/1.
- Accessor `FUN_0048f290` = MATCH-RECORD GATE + queue pick: `idx*0xfc` stride into
  `DAT_00641014` (idx 0 = home, idx 1 = `DAT_00641014+0xfc` = `DAT_00641110`, the
  pass-56 home/away pair as ONE stride-0xfc slot array), loads the slot POINTER
  (`0048f29d MOV EDX,[EAX+0x641014]`) and reads field `+0x34` u16 of the pointed-to
  team record (images stamped by pass-94 producer); if 0 → return 0;
  else `CALL 0x48d070` with `EAX=1` → queue lane `DAT_00632428 + 1*0x1c` (class-1 queue),
  selector = EBX (0 = head node, 1 = successor), i.e. the pass-87 `qfindindex` chain.
- Record dispatcher `0x48f47c` (driver, gate `[DAT_006326b0]` NULL → whole dispatch silent):
  fills the three stack slots through `FUN_0048f2d8`, then `CMP class,7; JA SNDflush` — class
  7 (and below) runs the 8-arm table `0x48f45c`; the phrase-feed arm
  `0x48f4ce` = word entry `[rec+0x28]+0xE` phrase → `SNDphraseadd 0x4df968`, handle stored
  `[word+6]`; then `SNDflush 0x4dfb40`. So class 7 gates on the match-record field, picks
  the class-1 queue head/successor lane, and the phrase feed stays the word-entry pick.
- Port mirror `Commentary::triggerLive7(homeId, awayId)` (`app/commentary.hpp/cpp`): busy
  reject + exhausted-pool lanes shared with `triggerLive`; home gate nonzero → `startPick(0)`;
  home closed away nonzero → `startPick(1)`; both zero → reject (dispatch-return-0 recycle
  lane). Deviation [H]: exe fills TWO pick slots (home through the `DAT_006326b0` struct,
  away on the driver stack) consumed downstream; port seam starts one sentence per call.
  Live events carry no retail class byte yet [H] — callers with match team ids may route
  class-7 announcements through the lane.
- Gate: `test_commentary` pass-91 block — `triggerLive7(0,0)` silent reject; `(0x1234,0)`
  head pick; after drain `(0,0x1234)` successor pick (different phrase).

## Pass-92: engine-callback gate + fileio lane decoded; registry slot arithmetic [H] (measured 2026-11-20)

Evidence `analysis/pass92cbr/e1.txt` (callback 0x49001c disasm + registry helpers),
`e2.txt` (record-lane install sites, sibling callback 0x490094, fileio helpers),
`e3.txt` (enable/disable cluster 0x4902d6..0x490490), `e4.txt` (loader head +
path formatter), `e5.txt` (full registry-find wrapper `FUN_0048ff64` disasm — the
ADD-sum store), `e6.txt` (locatebig writer `FUN_004a45d0` + `0x4a4a00`), `e7.txt`
(fileio op heads `0x4cfb00`/`0x4cfaa0` + name strings `readhandle` 0x56a630 /
`seekhandle` 0x56a648);
Ghidra scripts `GhidraWc98E1..E7.py` alongside; notes `analysis/pass92cbr/NOTES.md`.

- Boot loader `FUN_00490250` (gate `FUN_00490674` scene-enabled, once via `DAT_00632724`):
  `DAT_00632728 = FUN_004c3a00` (fileio object slot); loads `PHRS.HDR` (name 0x566c00,
  path formatter `FUN_0048f9d0` = `%s%s` root-prefix `DAT_00632720` or `AUDIO\PHRS\%s`,
  size param = `FUN_0048e8d0` scene-10 count) into `DAT_00632730` = **registry index table**
  (lookup #1 base, `FUN_0048ff64` head `MOV EAX,[0x00632730]; CALL 0x4a4820`);
  loads `TYPE.PBK` (0x566c0c) into `DAT_00632734` = nested payload (pass-90 base);
  gate = both loads non-null → `DAT_00632724 = 1`; creates queues `DAT_0063273c/0x632758`,
  reserves `STRASYNCH` (0x566c18) `0x140` bytes via `FUN_0049f5a0` → **16 slot records
  stride 0x14** pushed to `DAT_0063273c`; opens `PHRS.PBK` (GENERIC_READ, `FUN_0x4d82d0`
  lane) → handle `DAT_0063272c` via `FUN_004cf5e0`. Disable lane `0x490404` clears the gate,
  frees registry/payload/slot block, destroys queues, closes handles.
- Shipped assets (measured): `AUDIO/PHRS/` = `PHRS.HDR` (11,628 B, BIGF archive),
  `PHRS.INF` (28 B), `PHRS.PBK` (92,811,196 B), `TYPE.PBK` (76,480 B).
- Callback `FUN_0049001c` is installed **per record** (record lane 0x48f830 site 0x48f873
  `MOV ECX,0x49001c`, ECX = driver `FUN_0048f3e0` callback arg; driver table field
  `[ESI+0x1c]=0x48f47c`). Semantics (Watcom EAX=struct): gate `DAT_00632724`; two zeroed
  stack output slots; registry find wrapper `FUN_0048ff64` (Watcom args: `0x48ff6b MOV
  ESI,EAX` struct, `0x48ff6d MOV EBP,EDX` out-low ptr, `0x48ff6f MOV EDI,EBX` out-high ptr,
  `0x48ff7c/0x48ff99 PUSH EDI` stack arg; full disasm `analysis/pass92cbr/e5.txt`) does
  three registry lookups: #1 `0x48ff84 CALL 0x4a4820` base `DAT_00632730` key
  `[struct+0]`, `EDX=0`, writer arg `ECX=ESP` (→ wrapper-local `[ESP]`); #2 `0x48ff94
  CALL 0x4a4a00` base `DAT_00632734` key `EDX=[struct+0]` — the callee prologue `004a4a01
  PUSH ECX` + `004a4a0d LEA ECX,[ESP+0x4]` replaces ECX with its own frame slot and calls
  `locatebigentry` internally (`004a4a15`), so it **writes no wrapper slot**; EAX returns
  the located member pointer (or NULL). #3 `0x48ff9f CALL 0x4a4820` base `EAX` = **the
  return of #2 = the located nested member payload**, key `[struct+4]`, `EDX=0`, ECX
  restored by the callee's `POP ECX` to `&[ESP+4]` — a nested lookup inside the member
  #2 located, NOT a re-find on `DAT_00632734`. Then, verbatim e5.txt:
  `0048ffa4 MOV EAX,dword ptr [ESP]` / `0048ffa7 MOV EBX,dword ptr [ESP + 0x4]` /
  `0048ffab ADD EAX,EBX` / `0048ffad MOV dword ptr [EBP],EAX` writes **`[out low] =
  wrapper-local [ESP] + [ESP+4]`, the SUM of the two located-slot values**. The callback's
  **high** slot is written directly by `locatebigentry`: the wrapper's `0x48ff6f MOV EDI,EBX`
  (callback `00490042 LEA EBX,[ESP + 0x4]`) reaches the writer as `[ESP+0x20]` on BOTH keyed
  calls — `0048ff7c PUSH EBX` (before `0048ff7d` reloads EBX with the key) and `0048ff99
  PUSH EDI` → `004a45d0 004a460d MOV EBP,[ESP+0x20]`; lookup #1 seeds the high slot first,
  lookup #3 overwrites it. Miss path zeroes both pointers at
  `004a4607 MOV dword ptr [EDI],0x0` / `004a4615 MOV dword ptr [EBP],0x0`; the success-store
  bodies are decoded in pass-93 (`0x4a4644`/`0x4a46a0` store lanes — big-endian record
  offset/length fields, see Pass-93 below). **No doubling/shl instruction exists in any dumped body (wrapper 0x48ff64,
  callback 0x49001c, sibling 0x490094) — the pass-90 "2×found-index" wording is retracted
  as unbacked.** Callback (`e1.txt` verbatim): gate on the high slot `0049004d CMP dword ptr
  [ESP + 0x4],0x0` / `00490052 JLE`; then lock pair `FUN_00438f58`/`0x438f80`
  (reentrancy counter `DAT_00577380`, first/last hooks `0x494250`/`0x4b9b50`/`0x4b9b80`)
  around fileio ops on the `PHRS.PBK` handle `DAT_0063272c`: `FUN_004cfb00` =
  **seekhandle** (win_fileio.c line 0x219, name string 0x56a648 → `FUN_00529a20`, clamp vs
  file size `DAT_00593ea8`) with offset from the SUM slot (`00490059 MOV EDX,dword ptr
  [ESP]`), then `FUN_004cfaa0` = **readhandle** (line 0x1d4, name string 0x56a630 →
  `FUN_005296a0`) with length `EBX` = the HIGH slot (`00490066 MOV EBX,dword ptr [ESP +
  0x4]`) and buffer `EDX=[struct+0x10]` (`0049006f`); unlock `00490079 CALL 0x438f80`,
  verify the full read: `00490077 MOV EDX,EAX` (bytes read) / `0049007e CMP EDX,dword ptr
  [ESP + 0x4]` / `00490082 JNZ 0x00490038` → returns 0 unless bytes-read equals the high
  slot; `00490084 MOV EAX,0x1`.
  ⇒ **the engine callback seeks to a registry-derived position (sum-slot value; arithmetic
  decoded pass-93 — two-level big-endian member offsets, see Pass-93) in PHRS.PBK and reads the
  high-slot-sized segment record into `[struct+0x10]`, returning success only on the full
  read** — not from any queue — confirms pass-90b's "does NOT qfindindex"; pool-head
  pick stays port deviation [H].
- Sibling callback `FUN_00490094` (install site 0x48f7f0, same driver): allocates one of the
  16 `STRASYNCH` slots from queue `DAT_0063273c`, copies struct fields `+0xc/+0x14/+0x18`
  into the slot, pushes slot to `DAT_00632758`, registry find #2 (`0x4900e6 LEA EBX,[ESP+0xc]`
  high-slot ptr, `0x4900ef LEA EDX,[ESP+8]` sum-slot ptr) → gate on the high slot
  (`0x4900fa MOV EBP,[ESP+0xc]`, `0x490100 JLE`), then verbatim e2.txt: `00490102 MOV
  EAX,dword ptr [ESP + 0x8]` (sum slot) / `00490106 MOV EBX,EBP` (high slot value) /
  `00490108 MOV EDX,[ECX+0x10]` / `0049010b MOV ECX,0x48ff10` (thunk) / `00490110 CALL
  0x004d89a0` (play-request maker) → play handle stored `[slot+4]` (`00490115`); no
  doubling/shl in the dumped body; empty slot recycled
  (`FUN_004c4920` + push back). Sweep lane `0x4901e2`: fileio service on `DAT_00632728`,
  then queue walk (`0x4c4890` fetch, `0x4d8aa0`, invoke `[obj+0x10]`, release `0x4c4800`).
- Registry bases corrected: lookup #1 = `PHRS.HDR` table (`DAT_00632730`), lookup #2 =
  `TYPE.PBK` payload (`DAT_00632734`). `PHRS.HDR` header shape observed BIGF with u32
  offset/name entries — record decode closed pass-93.

## Pass-93: registry slot arithmetic decoded — BIGF record offset/length fields (measured 2026-11-20)

Evidence `analysis/pass93wr/f1.txt` (writer `0x4a45d0` full incl. success stores
`0x4a4644`/`0x4a46a0`, index-walk `0x4a4798`/`0x4a47ae`, string-scan lane
`0x4a4718`/`0x4a4728`, size reader `0x4a4580`, classifier `0x4a44b0`, `0x4a4820` full
incl. `bigfoot.c` error logs 0x13c/0x13e, name variants `0x4a48a0`/`0x4a48c0`/`0x4a48e0`
not in this lane), `f2.txt` (`0x4a4a00` full — payload-pointer return; `0x4a49a0` count
reader both kinds; `0x4c8849` = ASCII case-insensitive compare); scripts
`GhidraWc98F1.py`/`F2.py`.

- `0x4a44b0` classifier on base: u16 BE magic `0xc0fb` → kind 1; u32 BE magic
  `0x42494746` = "BIGF" → kind 2; NULL → debug-gated log (bigfoot.c:0x30), return 0.
  `0x4a4580` record size: kind 1 u24 BE at `base+2` + 4; kind 2 u32 BE at `base+0xc`.
- `0x4a45d0` args (Watcom, callee-clean): EAX=base, EDX=key **string** ptr (name-scan
  lane `0x4a4718`, case-insensitive via `0x4c8849` + `OR 0x20202020` fast path), EBX =
  **record index** (saved `[ESP+4]`), ECX = out-A ptr, stack arg = out-B ptr. With
  `EDX=NULL` (the callback lane — all three wrapper calls pass `XOR EDX,EDX`): index
  walk from `base+6` (kind 1, stride 6+strlen+7, name at record+6, u24 BE fields at
  record+0/+3) or `base+0x10` (kind 2, stride 8+strlen+1 = `{BE32 offset, BE32 len,
  NUL-terminated name}`, fields at record+0/+4, name at record+8); iteration counter in
  `[ESP+0xc]` (`INC EDI` at `0x4a47c5`), stops when counter == index, miss at payload
  end (`CMP EBX,end; JC` loop) → zero out-A/B pointers.
- kind-2 success store `0x4a46ca`+: out-A = **BE32 at record+0** (member offset), out-B =
  **BE32 at record+4** (member length), return = record+8 (name ptr). kind-1 store
  `0x4a4652`+: u24 BE record+0 / record+3, return record+6.
- `0x4a4a00` = payload-pointer locator (lookup #2): index lookup into payload with its
  own local out-A slot (`004a4a0d LEA ECX,[ESP+0x4]`, stack arg = global `0x6419e0` —
  the discarded length), success `004a4a1e MOV EAX,[ESP]` (found record+0 offset) +
  `004a4a21 ADD EAX,ESI` → **return = base + member offset**; miss → NULL. (pass-92's
  "located member pointer" now fully derived: payload = `TYPE.PBK` payload + HDR-record
  offset field.)
- `0x4a49a0` = record count: kind 1 u24 BE at `base+4`; kind 2 **u32 BE at header+8**
  (`0x4a49da` — measured: both files' header+8 = `0x50a` = 1290, the pass-90 tag census
  count). Wrapper index bound `0048ffd2 CMP EDX,EAX; JGE` → miss when index >= count.
- Payload magics measured (runtime lane ⇒ kind 2): `PHRS.HDR` and `TYPE.PBK` both open
  with `42 49 47 46` (BE "BIGF"); `PHRS.HDR` header+4 `0x1848`, header+8 `0x50a`
  (= 1290 records); record0 at +0x10 = offset `0x14454`, len `0x171`.
- ⇒ **callback arithmetic closed**: `[out low] = BE32(PHRS.HDR record[struct+0]+0) +
  BE32(segment record[struct+4]+0)` (HDR base offset + segment offset inside the nested
  payload), `[out high] = BE32(segment record[struct+4]+4)` = segment length (lookup #1
  seeds it with HDR record+4 via the `0048ff7c` out-B ptr; #3 overwrites). Callback seeks
  `PHRS.PBK` at that sum, reads length bytes into `[struct+0x10]`, returns 1 iff the full
  length was read. No doubling anywhere — pass-92 [H] closed; pool-head pick remains port
  deviation [H] (the lane is offset arithmetic, not selection).
- Port impact: none. The port's in-memory bank decode replaces the runtime seek lane;
  `app/commentary.cpp`/`hpp` callback-lane comments updated to cite the decoded field
  semantics. Deviation notes unchanged.

## Pass-94: class-7 gate feed decoded — FCDB team-record id stamped at match setup (measured 2026-11-20)

Evidence `analysis/pass94gate/` (`t1.txt` raw-dword scan of the `0x641014..0x641200`
operand sites, `t2.txt`/`t3.txt` producer + loader disasm, `t4.txt` pool-parse gate,
`t5.txt` loader miss tails, `t7.txt` gate-absent + caller scan, `t8.txt` loader-caller function ownership).

- **Slot producers.** The stores of `DAT_00641014`/`DAT_00641110` found by the operand
  scan (`t1.txt` site list; remaining sites, e.g. `0x48b8f0`, `0x4909b1`, are loads)
  come from one producer, match setup `FUN_004d4950`: it reads the pair ids from
  scene-record words
  `+0x10`/`+0x12`, loads both team-record images via `CALL 0x4cc060` into the static
  buffers `0x63b0b8` (home, `EDI`) and `0x63b170` (away, `EBP`), then stamps
  `0x4d4ab5 MOV [0x00641014],EDI` / `0x4d4ac7 MOV [0x00641110],EBP`. The pass-91
  stride-`0xfc` "table" is therefore two **pointer slots** to those static images —
  not inline records.
- **Gate field producer.** `FUN_004cc060` (EAX=buffer, EDX=team id, Watcom): the
  `0x4d2c60` lookup pair resolves the FCDB team record = `[0x64aeb0] + idx*0x44`
  (68-byte stride = the pass-44 team-record lane `0x18..0x43`), then
  `AND EAX,0x7ff` + `MOV word [ECX+0x32],AX` + `MOV word [ECX+0x34],AX` (`0x4cc0e2`/
  `0x4cc0ed`). ⇒ `record+0x32/+0x34` = **team's FCDB record id = dword0 bits 0..10**.
  The record pool base is parsed by `FUN_004d2220`, whose gate at `0x4d21f0` checks
  magic `'FCDB'` + version `0x1f5` — measured at `COMMON/FCDB.DBI` header
  `46 43 44 42 f5 01` ✓. Loader also unpacks team bitfields to `+0x36`/`+0x63..0x69`
  (presentation kit/GK flags; not this lane).
- **⇒ class-7 gate semantics (pass-94):** `[home+0x34]`/`[away+0x34]` are the teams'
  FCDB record ids. Gate 0 is observed when the id field reads 0 (id 0 = empty slot);
  on a lookup-#2 miss the loader **returns 0 without touching +0x32/+0x34**
  (`0x4cc36c XOR EDX,EDX; MOV EAX,EDX; RET`, `t5.txt`) — the field keeps its prior
  contents, so a miss zeroes the gate only if the image was 0 beforehand. Lookup-#1
  miss continues with index 0 (`0x4cc365 XOR EAX,EAX; JMP 0x004cc096`), resolving the
  record from entry slot 0. The exe dispatch calls the accessor **twice**, each gated
  on its own record (`c7d.txt`): the home call (EBX=0) fills the `DAT_006326b0`-struct
  slot, the away call (EBX=1) its stack slot — two independent picks. The port's
  `triggerLive7` home-first/away-fallback one-call pick stays the [H] deviation
  (pass-91 two-slot fill). Boot `FUN_0048fa08` same-team flag
  `CMP word [home+0x32],[away+0x32]` = FCDB id equality (pass-56 flag confirmed).
  Remaining pass-91 [H]: live events carry no retail class-7 byte; exe-side class
  stamping for in-play records stays open.
- **No later gate writers.** Raw-dword scan of the image gate absolutes — home
  `0x63b0ea` (+0x32) / `0x63b0ec` (+0x34) and away `0x63b1a2` / `0x63b1a4`: ZERO
  immediate operand sites (`t7.txt`). The ONLY function referencing the buffer
  addresses `0x63b0b8`/`0x63b170` is `FUN_004d4950` (body `0x4d4950..0x4d5886`,
  `t8.txt`), which calls loader `0x4cc060` once per side (`0x4d4a90` home, `0x4d4aab`
  away, ids from `[ESP+0x100]/[ESP+0x102]`); the ~200 other `E8→0x4cc060` callers
  listed in `t7.txt` never load these two buffer addresses as an immediate operand
  (what they pass through non-immediate addressing is not enumerated [H]). ⇒ within
  the match-setup lane the gate field is written once per team image; later
  pointer-path writes stay [H].
- Port impact: none (comments cite the producer chain).

## Pass-95: record pool and class bank-feed patterns decoded (measured 2026-11-20)

Evidence `analysis/pass95rec/` (`t3.txt` pool init/realloc/shutdown, `t4.txt`
priority comparator + word pump, `t5.txt` record alloc + init callback,
`t7.txt` scene-handler registration, `t9.txt` class-dispatch arms, `t11.txt`
record producers, `t12.txt` PHRS callbacks; jump table raw read at VA `0x48f45c`).

- **Scene handler set.** `FUN_00490528` registers the commentary scene through
  dispatcher `0x4e6b50` with handler set {trigger `0x48f730`, per-tick `0x48f728`,
  pump `0x48f830`, start `0x48f8bc`, accessor `0x48f78c`} (t7.txt). `FUN_0048f730`
  has no direct `E8` callers - it runs only through the scene dispatch pointer
  site `0x490595` (t4/t7). => game-event seam is dispatcher-mediated [H].
- **Record pool init `FUN_0048fd54`** (t3.txt): gates scene-enabled `0x490674` and
  once-flag `DAT_006326b0`; loads the `AUDIO\CRWD\...`-path object via
  `0x48f9d0`/`0x439810` into `DAT_0063271c`; creates three queues
  `DAT_006326c8` (free) / `DAT_006326e4` (pending-A) / `DAT_00632700` (pending-B);
  reserves via `0x49f5a0` a word block (`DAT_006326c0`, size
  `((obj+8)+0x14)>>0xa + FUN_0048e8d0)<<0xd`) and an 8-record block
  (`DAT_006326c4`, stride `0x2c`, size param `0x160`); pushes 8 records
  (rec+0x28 = word-block pointer) to the free pool.
- **Realloc pump `FUN_0048fcc0`** (t3.txt): iterate pending-A (`0x6326e4` qfetch
  `0x4c4890`): `entry+0x24 == 0` (no word result) -> push back to free pool
  (`0x4c4800`); else push to pending-B (`0x632700`). Busy bookkeeping = queue
  rotation only; shutdown `FUN_0048fe70` destroys queues (`0x4c4720`), frees
  `0x6326c4`/`0x6326c0`/`0x63271c` (`0x42b070`), clears `DAT_006326b0`.
- **Record alloc `FUN_0048d070`** (t5.txt): `cls*0x1c + 0x14` allocation through
  the `DAT_00632428` queue lane (`0x48d0dc` -> `0x4c4ce0`, init callback
  `FUN_0048cf10` identity-check on `entry+0xc`); word-array pointer = record+0x14;
  `rec+0x6/+0xa = -1` handles; bank load callback `0x4e3134`.
- **Producers stamp class + priority (t11.txt).** `FUN_0048f7a4` (event-message
  creator): guard scene-3 count `0x4906a4 > 0`, class = `[ESP+0x10]` byte through
  `FUN_0048f6a4` accept-gate, pops a record from the free pool, stamps
  `rec+0x1c = dispatch fn 0x48f47c`, `rec+0x24 = 0`, args `rec+0xc/+0x10`, busy
  `rec+0x4`, `rec+0x8 = class dword`, `rec+0x18 = priority`; runs the word pump
  `0x48f3e0` with callback `0x490094`; word produced -> push pending-A, else
  recycle to free pool. Tick producer `0x48f830` (registered pump) stamps the same
  fields with class byte from `DL` and callback `0x49001c`. Record header field
  mapping beyond these stores stays [H].
- **Priority comparator `FUN_0048f664`** (t4.txt): max of `entry+0x18` over
  pending-A and pending-B heads; trigger `FUN_0048f730` rejects while driver
  state `0x4dfc0c == 3` or comparator result `>= 0` vs the class arg (t4.txt lane
  `0x48f730`: reject gate, then alloc+beat-add `0x4df968` with handle -> `rec+6`).
- **Class bank-feed patterns (jump table VA `0x48f45c`, t9.txt).** Class byte
  `[rec+0x20] & 0x3f`, `> 7` -> `SNDflush 0x4dfb40` only. Cases: class 0 ->
  own bank (`rec+0x28`) phrase-feed only; classes 1/2 -> parsed slot-A
  (`[ESP+0x8]`, skipped if null) then slots C/D + own; classes 3..6 -> own then
  slots A/C/D; class 7 -> own then slots A/C/D (`0x48f5e4`). Every non-null slot
  bank adds one beat entry with threshold `EBX = 0`
  (`0x48d2f0` name lookup on `bank+0xe` -> `SNDphraseadd 0x4df968`, handle ->
  `bank+6`); tail `SNDflush`, return 1.
- **Duplicate protection: none at add time.** No dedup compare exists in any
  beat-add arm (t9.txt); repetition control = trigger reject (driver busy /
  priority comparator) + pool-consumption recycle. Port already mirrors busy
  reject + exhausted-pool lanes (`triggerLive`/`triggerLive7`), so no port
  change. Pass-91 residual wording: live-event class byte producers now decoded
  (t11.txt); dispatcher-mediated game seam stays [H].

## Pass-96: percent lane registration + played-frames pacing API (measured 2026-11-20)

Dump windows `analysis/pass96beat/t3.txt..t5.txt` (Ghidra disassembly, AUTO lane):

- **Percent slot lane** (`audio\percent.c` region, table `DAT_00632778` stride 4, enable
  flag `DAT_00632774` read by `FUN_00490674`, 26 consumers):
  - `FUN_004906a4` getter: `vol = max(pct*127/100, 1)` (documented since pass-73).
  - `FUN_0049067c` setter: `EDX = idx*4`; stores `EAX` into `DAT_00632778[idx]`, then if
    callback slot `DAT_004904b0[idx]` is non-NULL calls it (`0x49069a`). The callback table has
    **no static install sites** (imm sites `0x49068a`/`0x49069c` read-only; runtime-computed)
    — the exe dispatches percent writes through runtime-registered per-scene callbacks, same
    pattern as the `.SCI` descriptor array. Setter callers: 16 sites, cluster `0x46b090..0x46b2ed`
    (OPTIONS/settings installation lane: user audio-level settings write the percent slots) +
    `0x4512fc` (scene lane) + `0x47a97a`.
  - `FUN_004906d0` scaled-percent: `((pct*val)*127/100)/100` — the crowd chant-level lane
    (port `playChantLane` mirrors this, pass-53/59).
  - `FUN_004906f8` per-frame tick host: counter `0x4bf0b0`, enable gate, then `CALL 0x48f920`
    (speech queue tick) + `CALL 0x48da50` (chant pump) + `JMP 0x48e68c` (scene-10/crowd tick tail).
  - **Speech autovol wrapper `0x48fca0`** (single `E8` consumer of `SNDspeechautovol` `0x4df8a4`):
    gate `DAT_006326b0`, `EAX=3; CALL FUN_004906a4` -> percent slot 3 -> `EAX`=vol, `EDX=0`
    (frames=0, immediate set). `SNDspeechinit` caller `0x48fd85` sits in the pool-init lane
    `0x48fd54` (pass-95 bank feeds).
  - Percent **callbacks** `0x490978`/`0x490a34` are installed by scene registration
    (`FUN_00490528` tail `PUSH 0x490a34; PUSH 0x490978` -> `CALL 0x4e7060`). `0x490a34` =
    `SNDplayvariant` lane (`ECX=DAT_006327ac`, `EDX=0x80`, `EAX`, `CALL 0x4e39d0`); `0x490978`
    is the kit/uniform path lane (team id word `[slot+0x34]`, stride-63 slot array
    `DAT_0x641014` — the pass-91/94 preload table), not an audio-gain hook.
- **Port side (A9 pacing closure)**: `Mixer::framesPlayed()`
  (`audio/include/wc98/audio/mixer.hpp`) is the stream playback position — pumpLocked charges
  every fully-consumed stream buffer at unqueue with its queued frame count (`Voice::bufFrames`,
  refill takes a buffer index) and adds the live `AL_SAMPLE_OFFSET` position inside the
  head-of-queue buffer, so the counter advances continuously, not per processed buffer.
  `app/video.cpp` paces each video frame on an **absolute schedule** (`videoMs * rate / 1000`,
  cumulative frame-delay targets, no per-frame drift compounding) and falls back to plain
  `SDL_Delay(56)` only when no stream voice drives the clock; a stall guard (counter frozen for
  250 ms) releases waits on device stalls/stream end. Measured timed run (`SDL_VIDEODRIVER=dummy
  ALSOFT_DRIVERS=null`, silent device): `wall-ms=56980` vs scheduled `video-ms=56952` over 1017
  frames (~28 ms total drift), counter advanced continuously. Gate mode drains loopback through
  `render()` and asserts the counter tracks rendered frames within one render slice + one queued
  buffer (`movie-audio-granularity` violation -> exit 4); prints `movie-audio-rendered=1494277
  movie-audio-played=1494277` (`video_play` regex pins exact values). A8 residual CLOSED pass-98
  (no exe class gain — see Pass-98 below).

## Pass-98: A8 class-letter gain residual closed — no per-class gain exists (measured 2026-11-20)
Speech master is the ONLY volume applied to played commentary phrases. Boot lane
`FUN_0048fd54` — the sole direct `SNDspeechinit` `0x4df820` call site in the file (E8 scan over
the AUTO segment, artifact `analysis/pass98gain/scan.txt`: exactly one hit, `0x48fd85`) — gates
on percent-enable `CALL 0x490674`, once-guard `DAT_006326b0`, then `0x48fd75 MOV EAX,0x3;
0x48fd7a CALL 0x4906a4` (`pct[idx3]*0x7f/100`, clamp ≥1) and `0x48fd85 CALL 0x4df820`
(vol = EAX). Audio boot `FUN_00490728` initialises `DAT_00632778` to 100 for all six scene
slots (`AUDIO_RUNTIME.md` §boot, line 668), so the boot seed is `pct(100) = 0x7f`; the pass-7e
live `{80,80,80,80,65,80,1}` snapshot is the later options rewrite. The scene autovol seam
`FUN_0048fca0` (sites `0x48fcb0` percent→vol, `0x48fcb7` `SNDspeechautovol`, `EDX=0` → frames
clamp 1) re-applies pct idx3 as the ramp target. Engine callback lane `0x49001c` (pass-92/93
registry find + PHRS.PBK fileio verify) touches no volume operand, and the class/name-tag byte
is consumed only by the dispatch jump table `0x48f2bc` (`[rec+0x20]&0x3f`, pass-88/90), never
as a gain input — there is no class→gain table anywhere.
Port: `gainForClass`/`slotGain_` removed; pump applies the raw `cur>>16` master byte
(`gain_.applied()`) to every live slot; `volume_` stays `kMaxVol` (boot pct(100)), scene
levels arrive through `setScenePercent`. Evidence `analysis/pass98gain/{NOTES.md,scan.txt,g1.txt,g2.txt}`.

## Pass-99: retail data never enables the DirectSound3 lane — A10 closed (measured 2026-11-20)
The exe carries a positional-playback lane: `iSNDplatformplay` `FUN_0052d290` reads
TLV tag `0x92` (driver type; value 3 = DirectSound3 float path, pass-58 map), and the
platform wrappers are wired — create3dbuf wrapper `0x52d7b4` (host `FUN_0052ecec`) is
called from the SBANK bank-download lane (`0x4e32cf` inside `FUN_004e31d0`
`SNDbankaddpat2files` `cmn\sbankbld.c`, `0x4e3619` inside `FUN_004e34e0`
`iSNDdownloadbank` `cmn\sbank.c`, `0x4e6076` inside `FUN_004e5fe4`), pos3d wrapper
`0x52daac` (host `FUN_0052f1f0`) from crowd-scene code `0x4e4141`
(`analysis/pass99a10/NOTES.md`, xref dumps `/tmp/wc98pass99/x4.txt`/`x5.txt`).
But `play3d` is reachable only when the chain carries `0x92 == 3`, and a strict
TLV scan mirroring the pass-58 grammar — `(u8 tag, u8 len, BE value)`, `0xFF/0xFE`
terminate, `0xFC` skip, per-tag strict lengths and value whitelists (`0x92`∈{0,1,2,3},
`0x82`∈{1,2}, `0x81`∈{8,16}, `0x84`∈8000..48000), anchor tag {0x83,0x85}, ≤12 entries,
`0xFD`-lane candidates rejected — parses 9954 chains across every retail
`AUDIO/**` `.BNK/.PBK/.CRD` and 58 chains across every `VIDEO/*.DCT` with **zero
tag-0x92 occurrences** (the loose `92 01` byte hits inside `FL1_NANT.DCT` entropy
have no chain anchor within ±512 bytes). All tag-`0x82` channel values in the corpus
are 2 (stereo), 18 in AUDIO + 29 in VIDEO. Driver type therefore stays the default
0/2 2D path for every shipped sound: the original never pans, and the port's
mono→stereo upmix with no panning is 1:1. Raw-byte `0x92` counts (384,672 across
compressed payloads) are noise; only chain-structured parsing is evidence.

## Pass-100 (corrected): scene-10 crowd handler set + smidi reaction seam decoded (2026-11-20)
The four crowd handlers have **zero direct E8 call sites** — they dispatch
indirectly through the scene-handler registry. Audio boot installer
`0x4905b3..0x490624` gates on crowd-enable `0x48e6c8` (DAT_006325b0) and
SFX-table enable `0x48dc84` (DAT_006324b4), loads handler pointers
0x48e5a0/0x48e550/0x48e4fc/0x48e488 (plus SFX handlers 0x48d7dc/0x48d9d8 and
stack imm 0x490978/0x490a34) and installs them via `CALL 0x4e7060`
(`analysis/pass100crowd/NOTES.md`, disassembly `/tmp/wc98pass100/p100*.txt`).
Handler roles (watcom EAX=arg1, EDX=arg2; second revision — the first and
second drafts each carried wrong claims; everything below re-disassembled
from bytes with capstone):
- `0x48e5a0` start wrapper: gates DAT_006325b0 != 0 and 5b8 == 0; stores the
  initial-level arg to DAT_006325bc; `CALL 0x48e100` (percent lane below);
  `CALL 0x4e4ba0(scaledLevel, idx)` — smidi crowd start setter; then
  `XOR EAX,EAX; CALL 0x48e550` (control level 0); sets `DAT_006325b8 = 1`.
  No `SNDcrowdstart` call here — pass-71's `0x4e4d1c` is the library entry,
  the wrapper uses the `0x4e4ba0` start setter.
- `0x48e550` percent seam: gates 5b0 != 0, percent gate `CALL 0x4906a4` with
  idx = 2 (`pct*0x7f/100`, pass-52) must be > 0, 5b8 != 0; level =
  **`arg*511/100`** — verified chain: x → 8x+ x=9x → 72x+ x=73x;
  `73x<<3 − 73x = 511x`; IDIV 100; `CALL 0x4e4d8c` (`SNDcrowdcontrol`,
  ≤0x1ff clamp — 511 = `0x1ff` matches it). No argument clamp exists — the
  second draft's `min(arg,10)` CMOV claim is retracted; no `567` either.
- `0x48e488` pulseA: gates 5b0/5b8/5b4 all non-zero; `CALL 0x48e100` =
  **percent-scaled level gate, not a slot count**: `0x48e100` calls
  `0x4906d0` with **EAX = 2 hardcoded** (pct idx2, IMUL, ×127 = SHL7+SUB,
  then two `IDIV 100` → `arg*pct[2]*127/10000`), then
  `CMP DAT_00640fb4, 0xa; JNE → CALL 0x490670`; `0x490670` is the RET tail
  of the handler-deregister function `0x490660` (which calls registry
  `0x4e7060` with NULL args) — an immediate-RET no-op, so the scaled value
  is identical for every scene; caller `TEST EAX; JLE` skips when ≤ 0.
  Card `[0x6325d4]`; slot record = getter `0x4a4a00(card, idx)` + `0x30`;
  divisor = BE-swap dword at record+`0x30` (`0x4a49a0`, EDX=0); tick
  `0x4c321c` `DIV` divisor; **remainder** selects the slot via `0x4a4a00`;
  `SNDcrowdpulse(idx, slotrecord)` `0x4e4cac` (error lane `0x4e4ce2..`
  returns `-8`/`-14`/`-1`); then seam `0x48e3b0(EAX = slotrecord dword+6,
  EDX = arg)`.
- `0x48e4fc` pulseB: gates 5b0/5b4; idx = low word of AX (`XOR ECX,ECX;
  MOV CX,AX`); same `0x48e100` level gate; card `[0x6325d8]` struct: divisor
  = BE-swap of card-struct dword @0 (`0x4a49a0` on the handle), tick DIV
  remainder selects slot via `0x4a4a00`; **no `SNDcrowdpulse`** — seam fires
  directly with slotrecord dword `+6`, EDX = idx.
Reaction seam `0x48e3b0`: gate DAT_006325b4; fetch entry from pool queue
DAT_006325e0 (`0x4c4890`); entry+4 stores the EDX argument; plays through
**`SNDseqplayPVB` `0x4e462c`** (cmn\smidi.c) — a **MIDI sequence player**,
not a digitized-voice player: EAX = **sequence id** (validated
`0x100..0x6500`, bank = `id>>8-1` into the module bank table DAT_0064b3a4),
EDX = PAN (`-1`→64 center), EBX = VOL (`-1`→127), ECX = BEND (`-1`→64) —
all three hardcoded `-1` at the seam (`MOV ECX,0xffffffff; MOV EBX,ECX;
MOV EDX,ECX`). The module gate lives **inside PVB**, not in the seam:
module flag byte DAT_006597b0 == 0 → `-14` (`0x4e4678`); callback pointer
DAT_006597d4 == 0 → `MIDI MODULE NOT INITIALIZED` error path; invalid id →
`-8`. Playback records = stride-`0x24` table DAT_0064b15c (zeroed at
install with defaults vol `0x7f`, pan/bend `0x40`, `0x4e4242..`).
Success (ret ≥ 0): handle stored at entry+8; scaled level =
`id*pct[2]*127/10000` (second `CALL 0x48e100`, EDX = 0x10 ignored by the
lane); `CALL 0x4e39d0(handle, scaled)` — smidi level setter, same module
gate + record lookup `0x4e3ba0`; entry `qpush` (`0x4c4800`) into in-flight
queue DAT_006325fc. Negative return: entry `qpush` back into the free pool
queue 5e0.
Drain handlers: tick handler `0x48e650` (gates 5b0/5b4) loops queue 5fc:
stop each handle `CALL 0x4e48c4(entry+8)` and `qpush` back to 5e0; handler
`0x48e68c` (gates 5b0/5b4) walks queue 5fc via `0x4c4e70` with filter
callback `0x48e168` (`CALL 0x4e38b4(entry+8)`: state `==2` or `<0` =
finished) and recycles finished entries.
Module writers located (retracts the "circular 1:1" wording): audio boot
`0x49073e CALL 0x4e95f0` — device open; success path `0x4e9640..0x4e9644`
`MOV BYTE [DAT_006597b0], 1`; `0x49076a CALL 0x4e9840` (channel config,
vol `0x7f`, count `0x14`); `0x49076f CALL 0x4e41d4` — module installer,
gated on the flag byte (`0x4e41d8`): zeroes the bank table `0x64b39c..`,
the playback-record table `0x64b15c` (16 records), and the `0x64b9dc`
channel table, then stores callback pointers `[DAT_006597cc] = 0x4e3bd4`,
`[DAT_006597d4] = 0x4e4328`.
The seam argument is the stamped **phrase→MIDI sequence id**: scene-10
boot `0x48e6d0` first loads the `0x566adc` name (CMIDI.BNK, pass-56)
through `0x48e190` into `DAT_006325cc`, then calls the CRWD device-open
lane `0x4e3128` with EAX = the address `DAT_006325d0`, EDX = that CMIDI
bank handle, EBX = 0 (the handle lands in `DAT_006325d0` inside that
lane, internals [H]); the loader `0x48e280` runs exactly **two**
iterations (`ADD EDX,4; CMP EDX,8`,
and `0x48e190` preserves `EDX` via `PUSH/POP EDX`): names from the
runtime-populated pointer table `DAT_0057eb44/0x57eb48` (no static pointer
to the string pool exists — that is why the pass-56 name scan came up
empty) open through `0x48e190` (`AUDIO\CRWD\%s` @0x566acc) into handle
slots `DAT_006325d4`/`DAT_006325d8` — the CROWD.PBK/COMOS.PBK lane of the
pass-56 decode, not nine banks. Boot also reserves the
`count(CROWD.PBK) × 12`-byte `SNDCRD` pool (`0x566ae8`, `CALL 0x49f5a0`
→ `DAT_006325dc`) and creates the three queues `DAT_006325e0/5fc/618`
(`CALL 0x4c46a0`). The stamp then runs through the record wrapper
`0x48e248` (callback = `0x48e1c4`): the stamp loop walks the BE container
count, and per record (getter `0x4a4a00`) calls
`SNDphrasemidi 0x4e43c0` with EAX = device handle `[DAT_006325d0]`, EDX =
`-1`, storing the returned MIDI sequence id at **record+`6`** —
`0x4e43c0` is MIDI-module-gated (flag `0x6597b0` → `-13` error lane at
`0x4e4463`). PulseA/B fire the seam with the record dword `+6`, so the
sequence id fed to PVB is the stamped phrase id, not an envelope ceiling:
the pass-71 `{ceiling 350/450}` pair belongs to the CROWD.CRD *layer*
record at card+`0x28` (pass-55 envelope arithmetic) and has nothing to do
with the seam — the second draft's "stadium-jingle sequence ids" claim is
retracted. Without the MIDI module the stamp is `-13`, below the PVB id
floor `0x100` → `-8` → entry recycled: the no-synth lane. Teardown
`0x48e300`: clears gate `5b4`, runs wrapper callback `0x48e208` over both
containers — for each record with stamped `[+6] >= 0` it calls
`SNDseqfreeze 0x4e4568` (same module gate, `-2`/`-14` lanes), then walks
`EDX = 0,4`: closes each container handle `[EDX+0x6325d4]` (CROWD.PBK
then COMOS.PBK) via `CALL 0x42b070` when non-NULL and zeroes the same two
slots (`[EDX]` read at base `+0x6325d4`, store at base `+0x6325d0` with
the incremented EDX — the two addresses describe the identical slot),
then closes the device with `CALL 0x4e37cc([DAT_006325d0])` and closes +
zeroes `DAT_006325cc`; the `DAT_006325dc` pool follows in the release
lane.
Both PVB paths
receive PAN=-1 → center: independent backing for pass-99 A10. Port: no
MIDI device open → flag byte 0 → stamp `-13` → PVB rejects → seam
recycles every entry, silent — equal to retail behavior on a machine
without a synth; retail with a synth renders the CRWD phrase bank as MIDI
sequences (the device-dependent side remains the A5 blocked lane). The
per-phrase semantics of the `0x4e43c0` mapper (EDX = `-1` selector) stay
[H].

## Pass-101 (2026-11-20): board-announcer dispatch — scene registry attribution + wiring

**Retraction of this pass's first draft.** The first draft claimed
`FUN_00453a6c` owned a "multiplayer standings" dispatcher and `FUN_00458cf0`
the group-standings board only. The scene registry overrides that: the
`FUN_00446598` registration block (`MOV EAX,feflowNN-name; CALL 0x446598`)
binds every dispatcher to its registered scene name, and every announcer
handler call site falls inside a dispatcher registered under a feflowNN
name. The full block dump with per-scene struct stores (+0x10 dispatcher,
+0x18/+0x1c/+0x14 side fns) and the `E8` handler scan per dispatcher range is
`analysis/pass101announcer/registry_bind.txt`. The `mult` string sits in the
feflow20 dispatcher region (World Cup
config controller board, the `simu`/`frwd`/`feNN` edge hub of pass-20), not
a multiplayer board.

Corrected handler -> scene -> port-screen mapping (dispatcher/`E8` call
sites from pass-82; names from `analysis/pass8/tourney_registry_entries.txt`):

| exe handler | dispatcher | call site | registry name | port screen | dispatch |
|---|---|---|---|---|---|
| `FUN_00453a6c` | 0x452f34 | 0x453138 | feflow20 | `openWcCfg(20)` | yes, `feedBoardAnnouncer` |
| `FUN_00454338` | 0x454298 | 0x4542c9 | feflow21 | `openWcCfg(21)` | yes, sel 0x1b + t1 + sel 0x16 |
| `FUN_00454d60` | 0x4545fc | 0x45463b | feflow23 | `openWcCfg(23)` | yes, sel 0x18 + t1 + sel 0x13 |
| `FUN_0045548c` | 0x45502c | 0x455064 | feflow24 | `openWcCfg(24)` | yes, single sel 0x17 |
| `FUN_00455a3c` | 0x45570c | 0x45574b | feflow25 | presentation round 1 | yes, sel 0x19 + t1 + sel 0x14 |
| `FUN_00455f30` | 0x455d28 | 0x455d5b | feflow26 | presentation round 2 | yes, sel 0x1a + t1 + sel 0x15 |
| — | 0x456030 | — | feflow27 | presentation round 3 | **no announcer call site** [C absence] |
| `FUN_00458cf0` | 0x458714 | 0x458806 | feflow35 | presentation round 0 + `openWcCfg(35)` | yes, subset (below) |

Fill-site mirror (`feedBoardHandlerAnnouncer`, `app/game_audio.cpp`): each
handler fills slot 0 type 3 from selector `sel0`, then a type-1 lane
(`FUN_00485240` selection; needle provenance closed pass-84 - the needle is
the stored id from the handler's packed-pair ECX upper word,
`analysis/pass84needle/n1.txt`), then
slot 1 type 3 from `sel1` — matching the exe store order where the t1 store
overwrites the earlier t3 slot-0 fill. feflow24 carries no t1 site. Grid A
ranges and the type-3 skip set (pass-78 grid) decide the ids, e.g. sel
0x17 = records [130,135) minus skips {130,131,132} -> pick id 134 (entry 0 stand-in).
Byte scan closes the pick-cursor residual: `DAT_0057e600` (cursor-lane mode flag,
initialized DGROUP, static dword 0) is written only by the setter 0x489ba4 (no E8/E9
caller, no imm32 reference to 0x489ba0/4/6), and `DAT_0062fd9c` stores are just the clamp
(0x4899fc) plus that setter - retail picks via the tick-fraction index, so dispatch state
carries no pick cursor and the port is stateless per pick, entry 0 as a stand-in (tick
lane unmirrored [H]).

**feflow35 partial mirror**: only the fill sites 0x458dc2 (SI team bank)
and 0x458df6 (std selector 0x0) are mirrored via
`feedTeamSelectionAnnouncer`; the match-result jump fills (`0x0-0x3/0x5/0xb/
0x8/0xe/0x7/0xf-0x12`, pass-77b) stay unmirrored — the port has no match
outcome recorded at board entry on this lane [H].

Wiring (`app/fe_flow.cpp`): `dispatchBoardAnnouncer(scene)` runs once per
screen — `openWcCfg(screenId)` on entry and `drawTourneyPresent` per round
(35/25/26/27). First dispatch creates the tree-owned `Announcer`, attaches
`mixer_` (speech autovol lane; null mixer = counted, silent, like every
headless announcer driver). `pick_` (set by `--fe-pick`) feeds the team-id
lane; `-1` keeps the exe gate lane (no fill). Scenes with no announcer site
(18/19/27/81/82) set the flag and no-op.

Gate semantics (measured, CMake 3.28 scratch project): a multi-value
`PASS_REGULAR_EXPRESSION` list is OR — one match passes, so the announcer
assertion lives in `FAIL_REGULAR_EXPRESSION`:
`FE-ANNOUNCER decoded=0 ` plus `FE-ANNOUNCER decoded=[1-9] plays=0 `.
`fe_render_present` runs `--fe-present --fe-pick 26` (`AUDIO/TEAM/G_T0026.PBK`
on the tree). `test_gameaudio` adds the per-handler fill-state asserts
(scene 27 no-op, 24 single lane id 134, 25 t1-overwrite + std lane).

Evidence: `analysis/pass101announcer/NOTES.md`, registry
`analysis/pass8/tourney_registry_entries.txt`, windows
`analysis/pass77pbp/`, grid `analysis/pass78grid/grid_dump.txt`, dispatch
call sites `analysis/pass82dispatch/`.
