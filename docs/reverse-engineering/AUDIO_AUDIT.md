# AUDIO_AUDIT — pass-49 full audio audit (FIFAWC.EXE is the sole authority)

Audit date: 2026-11-19. Scope: every audio subsystem vs the original Windows release
(`WorldCup98_Extracted/FIFAWC.EXE` + shipped `AUDIO/` tree), plus the port runtime
(`audio/`, `app/game_audio.*`, `app/commentary.*`, `app/livematch.cpp`, `app/main.cpp`).

Classes:
1 recovered-verified · 2 behaviorally-verified · 3 partial · 4 approximation · 5 incorrect · 6 missing · 7 not investigated.

## 1. Subsystem classification

| Subsystem | Class | Evidence | Original behavior vs port |
|---|---|---|---|
| PT/BNKl/EA-XA/SCHl/BIG+RefPack codecs | 1 | `formats/src/{bnk,schl,big,refpack}.cpp`, gates `test_audio`/`test_phrs`/`test_big_refpack`, `docs/asset-formats/AUDIO.md` | 47/47 BNK + 6,964/6,964 PT members decode. Default rate 22050 (EACSND.DLL `0xe01`). Doc tag 0x82 vs code channels conflict (see DISCREPANCIES §doc). |
| MicroTalk speech decode | 2+4 | `schl.cpp:159-166`, `bnk.cpp:47-55` | Bitstream verified; output gain uncalibrated (RMS 9–11.5k, clipping possible) — open item AUDIO.md §1. |
| CRDl card / MIDx parse | 2 format / 6 runtime | `crd.cpp`, `midx.cpp`, `test_audio.cpp:222-236` | Parsed by tool/tests only; never loaded at runtime. No MIDI synth lane. |
| Device/backend init | 4 | `audio/src/mixer.cpp:39-62` | Original: EACSND.DDLL iSND* over DirectSound, 13 slot bindings (`FUN_0052c948`). Port: OpenAL default device, no selection/rate config. Acceptable backend swap (architecture preserved). |
| Voice pool / priority / steal | 4 | `mixer.cpp` (fixed in pass-49) | Original: 16 mix voices + 16×0x60 SND voice table, type-byte dispatch. Port now caps at 24 concurrent voices (was unbounded). |
| SMIX resampler / pan / voice record grammar | 1 grammar / 6 runtime | `audio/include/wc98/audio/smix_*.hpp`, gate `test_smix_grammar` | Decompiles pass-17/18 faithful; runtime uses only the Q7 gain path (`smixLaneGain`). Mix loops never touch output. |
| SND channel envelope/ADR/pan law | 1 grammar / 6 runtime | `snd_channel.hpp`, `test_voice_layer` | No runtime caller. |
| Scene percent table DAT_00632778 | 1 grammar / fixed wiring (pass-49) | `scene_levels.{hpp,cpp}`; decompiles `analysis/pass49audio/{pct.txt,loaders1.txt}`; winedbg pass-7e `{80,80,80,80,65,80,1}` | Channel roles corrected this pass: idx3 = speech (`FUN_0048fca0`→`FUN_004df8a4` SNDspeechautovol), idx4 = music stream (`FUN_0048ec38`→`FUN_004e6aa0`; consumer `FUN_0048ed9c@0x48ee63`), idx2 = crowd stream (`FUN_0048e550` gates `DAT_006325b0/5b8`→`FUN_004e4d8c`), idx5 = positional stream (`DAT_00632790`+`7a8/7ac`). Runtime now feeds settings rows → percent table → lane gains. |
| FE music (MUSC .ASF) | 3 → fixed rotation (pass-49) | `game_audio.cpp`, `FUN_0048ed9c` (FE list `DAT_00632654`, BE list `DAT_00632670` under `FUN_00446624()==2`), pump `FUN_0048eb98` | Original rotates the built list; port previously played one track looped forever. Now sequential rotation + scene-transition fade (`FUN_0048ec64(0xc8)`=200 ticks). |
| MIDI director (CMIDI.BNK, COMOS MIDx, `FUN_004e3bd4`) | 6 | docs only | 16 tracks × 16 channels, 24-voice pool `DAT_0064bddc`. Port has no MIDI bus; the port `ChannelMix` (CC7 table) was misapplied to speech slots (wrong bus) — noted, left as documented deviation. |
| Speech queue (20×12 B ring) | 1 | `speech_queue.cpp`, `test_voice_layer` | Ring/gate/-13/status codes match; port-invented pending -50; single queue vs two in `FUN_004e2c60`. |
| Commentary trigger/pick | 3 | `commentary.cpp`; `FUN_004893ec` seam; `SpeechEventDispatch` models `FUN_0048f4b0` cases 0..7 | Pick = sequential shared pool (deviation: exe class-queue `DAT_00632428[classWord]` qfind; per-record +0xC tags unmeasured). Phrase content per event = open (DISCREPANCIES §commentary). |
| Commentary gain (16.16 autovol) | 1 API / wired (pass-49) | `commentary.hpp SpeechGain`; sites `0x48fcb7/0x491125`; percent idx3=80 measured | Port now ramps speech to percent-table idx3 at runtime (settings row 162 → `setLevel(3, pct)` → `Commentary::setScenePercent`). `kRampTicks=5` stands in for the runtime-data ramp arg. |
| Name speech (PLRS ~1019 PBK, TEAM ~94 PBK) | 6 | none | Never loaded. The exe name-speech lane is not statically mapped (blocked area). |
| Crowd ambience | 2 / 6 card layers | `game_audio.cpp` CROWD.BNK#2 bed loop (measured: bank has only slots 0-3; #2/#3 identical 3.03 s murmurs) + goal swell GAME00#45 (4.12 s loud-noise burst, profile-selected [H]); `MatchAudio::relevelCrowd` idx2 seam fades the running bed on percent-table change; `FUN_0048e300` shutdown (card `DAT_006325c4`, stream slots `DAT_006325d4`, handles `5cc/5dc`); `crd.cpp` | Bed runs at scene percent idx2 (options row; was unity-always), fade seam wired (gate `test_gameaudio`). Card-driven layering, TRAFF.*, chant layering (CHNT/*.PBK) = missing (DISCREPANCIES A4/A6). CROWD.BNK has no per-event reaction slots — the goal swell uses a GAME00 bank slot. |
| SFX | 4 | `FUN_0051d480` site table `(DAT_0058f3b2)[scene*4]`, loader `FUN_004ac840`, dispatcher `FUN_004ae730` ids 0x1f9..0x1fb, `audsfx.%c` banks | Port: GAME00 slots — kick 23 (Shot), whistle 81 (KickOff + Foul [H]), trill 85 (PeriodEnd/MatchEnd), goal swell 45; CROWD bed 2. Full bank measured by slot probe (`analysis/pass49audio/GAME00_slots.txt`): slots 0..35/42/44/45/46 + 80..85, 36-41/43 empty — there is no slot 53 and no per-scene slot table in the port; roles acoustic-verified where asserted (`test_gameaudio` (b)); no GAME01/PLRS banks, no post-hit sounds (DISCREPANCIES A1). |
| Positional/pan | 6 | none | SMIX L/R pan law + HW3D pos3d (`FUN_005490f0`) not applied to output. |
| Settings volumes | 3 → wired (pass-49) | rows 160 music / 162 commentary / 1186+1187 sfx; `FUN_0046b074` percent writers | Now: row changes write the percent table (music→idx4, commentary→idx3, sfx→idx0) and lanes consume it. Previously only the music row drove the listener gain. |
| Menu↔match transitions | fixed (pass-49) | `FUN_0048ec64(0xc8)` ramp at scene switch | Music fades out (200 ticks) on live-match launch and back in on return; single OpenAL context enforced (was a second context per match — see defects). |
| DCT movie audio | 6 | `schl.cpp:94-98` (INT_EASP.DCT EA-XA) | Never played. |

## 2. Defects fixed this pass (verified by reading the port code)

1. **Commentary event drop ~50%** — `livematch.cpp:1689-1697`: odd presented frames ran `evPos = w.events.size()`, discarding every sim event emitted on odd steps before `commentary.trigger`. Fixed: trigger cursor advances every frame; pump stays 2-frame.
2. **Two OpenAL contexts per process** — `main.cpp:309` kept `feAudio` alive while `runLiveLoop` (`livematch.cpp:984`) opened its own Mixer (`alcMakeContextCurrent` is process-global); after the match, `~Mixer` cleared the context, leaving the FE pump thread contextless (menu music dead, `setMasterGain` no-op). Fixed: live match reuses the FE mixer (standalone `--live-match-smoke` still opens its own — no FE loop in that lane).
3. **Pump thread → `std::terminate`** — `mixer.cpp` jthread had no exception guard; a `FormatError` from `SchlDecoder::next` inside `refill` killed the process. Fixed: per-voice try/catch marks the voice exhausted and clears stale AL errors.
4. **Deadlock loop** — looped stream with zero data blocks: `next()` false → `rewind()` → false forever, holding `mu_` under the pump. Fixed: post-rewind retry once, then `exhausted`.
5. **AL-resource leak + stale error propagation** — `check()` ran after `alGen*`; on throw the Voice was dropped with source+buffers alive, and a stale error flag could fail an unrelated `play()`. Fixed: cleanup-on-throw + `clearStaleError()` entry.
6. **Unbounded voices** — every `play()` allocated a source forever. Fixed: cap 24 (oldest finished one-shots reclaimed).
7. **Scene percent wiring** — runtime never called `setLevel`, so the table stayed `{100×6}` and all lanes ran unity regardless of settings; channel labels were swapped vs the exe (idx2≠music, idx4≠speech). Fixed with evidence (§1 row 8).

## 3. Still-open original-behavior gaps

Tracked as A1–A11 in `AUDIO_DISCREPANCIES.md`: per-scene SFX slot table + `audsfx.%c` banks (A1),
GAME01 shuffle (`FUN_0048f228`, A2), PLRS name banks (A3 — **shipped**, 1020 `P%05d.PBK`,
cataloged at boot since pass-51; playback pending trigger decode), CROWD.CRD card
layering + traffic (A4), CMIDI MIDI director (A5), chant banks (A6 — pool decoded, cataloged),
commentary event→phrase
content mapping (A7 — blocked), MicroTalk gain calibration (A8), DCT audio (A9), pan (A10 —
original has none), doc tag 0x82 conflict (A11).

## 4. Evidence artifacts

- `analysis/pass49audio/pct.txt` — percent-callback decompiles (`FUN_0048ed9c`, `FUN_0048e550`).
- `analysis/pass49audio/loaders1.txt` — `FUN_004ac580` (percent→`DAT_006448c2/c3` volume bytes), `FUN_004ac840` (audsfx install), `FUN_0048f228` (game01 slot shuffle), `FUN_0049067c` (percent setter), `FUN_00490528` (callback installer), `FUN_0048e300` (crowd shutdown lane).
- `analysis/pass7/percent1.txt` — live scene percent writes (pass-7e).

## 5. Pass-51 addendum — match-boot lane catalog (2026-11-19)

Rebuilt the match-boot lane installs from FIFAWC.EXE (not from CD file names — the boot builder
`FUN_00490728` enumerates the lane loaders): chant pool `FUN_0048dfc4` (exe table @0x57eb5c,
113 slots = 74 team-keyed + 39 `CGENER` generic), announcer `FUN_0048fa08` (`A_T/G_T%04d`),
squad player banks `FUN_0048fb10` (`AUDIO\PLRS\P%05d.PBK` — the CD ships 1020 banks; the earlier
"absent" claim was false), anthem `FUN_00490acc` (`ANTH%04d.BNK` slot 0 = nation chant),
stadium/traffic pair `FUN_0048e78c`, teardown `FUN_004907c0`. Full dossier + decompiles:
`analysis/pass16audio/NOTES.md`, `docs/reverse-engineering/AUDIO_RUNTIME.md` pass-51 section.

Port side (verified, gates green): `MatchAudio::boot` installs the percent idx2 notifier, CROWD.CRD
card authority, TRAFF hum loop, ANTH slot-0 one-shot, and decode-counted catalogs of the chant pool
(113), team announcer banks (4) and squad PLRS banks (38 for teams 26/67). Playback of
chant/announcer/name phrases is not wired until the request-handler trigger mapping is decoded
(A3/A6/A7) — catalog = install-state proof, no fabricated playback.

Evidence artifacts: `analysis/pass16audio/NOTES.md`, `/tmp/wc98pass51/{lane_decomp.txt,plrs_fns.txt}`,
probes `bankprobe.cpp`/`slotprobe2.cpp` (results in NOTES.md).

## 6. Pass-77 Phase-1 addendum — refreshed classification + impact ranking (2026-11-20)

Status of §1 rows after passes 50-76 (registers `AUDIO_DISCREPANCIES.md` A1..A11,
`AUDIO_RUNTIME.md` pass sections, gates `test_audio`/`test_phrs`/`test_gameaudio`/`test_sqf`/`test_dct`/`video_play`):

| Subsystem | Class now | Change since pass-49 |
|---|---|---|
| Speech banks / event->phrase id map | 1 | A1 closed: id map `id>>24` bank + slot (pass-62); event tables pass-60 |
| Music director lanes (stream SCHl pumps) | 1 | A2 closed pass-72 (stream chain), A9 closed pass-75 (movie routing wired) |
| Chant layer (CHNT/*.PBK, pumps, rotation, LCG) | 1 | A6 closed: pump + classification + drain rotation + exe LCG mirrored (`MatchAudio::ExeLcg`, pool rotate on cadence) |
| Crowd bed + reaction machine | 2 | A4 closed (card-bound bed, relevel idx2 fade, pump pass-70); card layering runtime still [P] |
| Registry/pumps (bank boot, request handlers) | 1 | A3 closed pass-57 |
| Trigger grammar | 1 | A11 closed pass-58 |
| Speech gain autovol ramp | 1 | A8: ramp modelled 1:1 (`SpeechGain` exact-division gate); class-letter table still [H] |
| Commentary pick/preload lane | 3 | pass-76: presentation preload decoded (ring ring-slot {type,id} -> shipped PBK/SMP files); scheduler lane reclassified chant-lane; live-match per-event phrase content still [H] (A7) |
| Presentation announcer files (AUDIO/TEAM 95 PBK, PBP/SCHEDULE 436 SMP, PBP/STANDING 320 SMP) | 6 | exe lane class-1 decoded (pass-76) but **unported** — no announcer component in port |
| Name speech (PLRS) | 6 | request-handler trigger mapping blocked (memory: referee/EULZ/.SCI list) |
| MIDI director | 6 | unchanged (device-blocked A5) |
| Positional/pan | 6 | unchanged (A10 deviation, original has none on mixer bus) |

Impact ranking (audio half of the §2 list in `GAMEPLAY_AUDIT.md`):

1. Presentation announcer lane port (class-6 gap, assets already mapped by pass-76) — pass-77.
2. Live-match commentary phrase content (A7 [H], D-32) — needs exe pick/tag table recovery.
3. Crowd card-layer runtime application + chant request trigger mapping (A4 [P], A3 residual).
4. MicroTalk gain calibration (A8 class-table [H]).
5. Name-speech lane mapping (blocked), MIDI (device-blocked).
