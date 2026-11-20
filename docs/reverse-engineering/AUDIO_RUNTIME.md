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
`FUN_00485240/0x48525c/0x489920/0x489974+0x4899dc` or loop regs; accessor internals [H]).

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
`AUDIO_DISCREPANCIES.md` A7. A7 stays **Open (partial)** — the live-match pick table, and the
presentation handler's scene→{slot,type,id} argument extraction (accessor-internal, pass-76)
remain the unresolved halves. The presentation announcement lane itself is **ported**
(Pass-77 section below).

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
  the port — the exe scene→id mapping stays [H], so the API is gate-driven; wiring into
  tournament presentation scenes is open work.

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
(`0xa0->0x29` wrap) is adjacent data, not a selector entry. Record pool identity (which
fixtures 0..159 are) stays [H]; no shipped fixture table establishes the equivalence.
Fill semantics: the loop walks `rec` through `[start, end-1]`; `FUN_00489bdc` is a
**skip predicate** `(in_EAX=type, ECX=record)` returning 0 = skip — a hardcoded per-record
exclusion table whose constants are the same grid's words (measured: type-2 skips
{23, 31, 34, 37, 38, 50, 62, 85, 133}; type-3 skips {5, 6, 7, 8, 11, 13, 17, 18, 39, 43,
44, 46, 47, 50, 53, 54, 95, 96, 97, 98, 120, 121, 122, 125, 127, 130, 131, 132}; with
`DAT_00640fe8==1` -> `FUN_00489bb0` override and `==3` one extra skip). Surviving records
are stored as `record + 1` (`DAT_0062fbf8` holds 1-based record numbers; whether these
list ids reach the loader's `%03d` filenames is [H]) at `DAT_0062fbf8[count++]` with the
`0x27`/range-end guards. Evidence: `analysis/pass78grid/sel1.txt` (function pseudocode),
`analysis/pass78grid/grid_dump.txt` (raw grid + skip-constant words),
`analysis/pass78grid/GhidraWc98Sel1.py`.
