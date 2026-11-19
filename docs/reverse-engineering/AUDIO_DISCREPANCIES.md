# AUDIO_DISCREPANCIES — open audio discrepancies (pass-49, 2026-11-19)

Companion to `AUDIO_AUDIT.md` (fixed defects live in its §2) and `AUDIO_RUNTIME.md` (mechanism
dossiers). FIFAWC.EXE + shipped `AUDIO/` tree are the sole authority. Classes:
**Open-recoverable** = original behavior/mechanism decoded enough to install, awaiting data or a
small decode step; **Open-blocked** = original data unreachable on this CD (Wine lane dead);
**Deviation** = port runs differently than the exe, documented, kept deliberately.

| # | Item | Class | Evidence / state | Port behavior today | Action |
|---|------|-------|------------------|---------------------|--------|
| A1 | Per-scene SFX slot table + `audsfx.%c` banks | Open-recoverable | `FUN_004ac840` installs `audsfx.%c` per scene (analysis/pass49audio/loaders1.txt); slot numbers come from the scene handler, which is scene-LAY driven; full GAME00.BNK slot inventory measured (`analysis/pass49audio/GAME00_slots.txt`: slots 0..35/42/44/45/46 + 80..85, 36-41/43 empty; CROWD.BNK has only 0..3) | GAME00.BNK slots wired statically in `MatchAudio::trigger` (kick 23, whistle 81 KickOff/Foul, trill 85 period edges, goal swell 45) | Decode slot source per scene handler; roles for unused slots stay unassigned (no fabricated mappings) |
| A2 | GAME01 music-slot shuffle | Open-blocked (data) | `FUN_0048f228` decodes: two stream slots `DAT_00632698/9c`, kill/stop loop — swaps live match music slot | Port keeps one MUSC stream per match; no shuffle | Needs the `GAME01.ASF`-era slot registry values; scene handler RE slice |
| A3 | PLRS player-name speech banks | Closed (pass-51) | Earlier "not shipped" claim was wrong: `AUDIO/PLRS/` ships **1020 `P%05d.PBK` banks** (tree scan; FCDB roster ids resolve 19/22 for teams 26/67 - reserves have no bank). Match-boot installer `FUN_0048fb10` loads `AUDIO\PLRS\P%05d.PBK` (@0x566bc4) for every non-zero squad id (u16 at slot+2) of both teams; each bank parses with 1..2 name phrases (`/tmp` probe) | `MatchAudio::catalogPlayers` catalogs the squad banks at boot (decode-counted; playback of name phrases still disabled until the id→phrase trigger mapping is decoded) | Name-speech playback: decode the announcer request handler that picks PLRS phrases |
| A4 | CROWD.CRD card layering + per-stadium traffic | Open-recoverable | CRD container parsed (`formats` crowd lane); card selection driven by stadium/handle code | `MatchAudio` runs CROWD.BNK slot 2 loop (idx2 lane + fade seam) + goal swell GAME00#45; no per-card layering | Decode CRD card→slot routing table; install multi-card ambience |
| A5 | CMIDI MIDI music director | Open-blocked (device) | `CMIDI.DLL` + `MUSC/*.MID` shipped; MIDI synth absent on Linux CI | Menu music plays SCHl ASF streams only; match has no MIDI | Route: choose ASF/MID per scene as the exe; MIDI render needs a software synth — out of scope, logged |
| A6 | Chant banks (CHANT/`.EAX` chant layer) | Open-recoverable | Chant pool install decoded (pass-51): `FUN_0048dfc4` allocates the 113-slot lane (`iVar1 < 0x71`); bank names = exe table @0x57eb5c (74 team-keyed `C0<team*10><variant>` entries, cross-checked: all exist on the CD) + 39 `CGENER%02d` generic files; per-chant loader `FUN_0048d53c` (`AUDIO\CHNT\%s` @0x566a78, `table[req+4]`) | `MatchAudio::catalogChants` catalogs all 113 CD banks at boot (decode-counted, gate `match-audio-lanes`); chant playback not wired — Class 'a' speech gain only (commentary EA-XA) | Bind chant clips to crowd reactions once A1/A4 routing decoded |
| A7 | Commentary event→phrase content mapping | Open-blocked | trigger-gate band decoded (0x452af0..0x458cf0, 46 callers); per-record `+0xC` slot tags preload from an **unmeasured table** (FCLG probe negative, analysis/pass7/pick*.txt) | Sequential shared-pool pick (deviation, `AUDIO_AUDIT.md` §1 row commentary) — first event gets phrase 0, etc.; content-per-event unverified | Recover slot-tag table source; per-event queue keyed by tag |
| A8 | MicroTalk gain calibration | Open-recoverable | class-letter gain table is port Hypothesis (`Commentary` §gain, mid-domain 0x40); exe autovol ramp `SNDspeechautovol`/`DAT_00588d6c` unmodelled | 'c' = 0.5f, 'a' = 0.5f fixed | Measure autovol ramp live (DOSBox-X lane blocked); calibrate against exe float domain |
| A9 | DCT video audio track | Open-recoverable | `.DCT` video lane has its own audio frames (`VIDEO_DCT_NOTES.md`) | Video plays decoded audio frames; not routed through Mixer lanes | Wire DCT audio through scene lanes when video presentation lands |
| A10 | Positional audio / stereo pan | Deviation (absent) | exe SMIX bus = 2-channel stereo, no per-source pan found in EACSND/SMIX exports; mixer has no pan API | mono→both-channel upmix, no panning | Do not add panning — original has none (`AUDIO_AUDIT.md` §1) |
| A11 | Doc tag `0x82` channels conflict | Open-recoverable | `docs/asset-formats/AUDIO.md` tag table vs `formats/src/schl.cpp` channel derivation disagree for tag 0x82 streams | Code path wins (decode observed on real streams) | Re-derive tag from a 0x82-bearing stream sample; fix doc or code |

Fixed-this-pass entries (event-drop seam, dual OpenAL contexts, pump exception guard, loop
zero-data deadlock, AL-resource leak, voice cap, percent wiring, music rotation, fade seam,
crowd reaction gating, SFX slot extension) are recorded in `AUDIO_AUDIT.md` §2, not here.

Verification anchors: `test_gameaudio` lanes (a)–(d), `test_commentary_live`, `test_voice`,
`fe_*` gates, `interactive_match`; full pass-49 evidence in `analysis/pass49audio/`.
