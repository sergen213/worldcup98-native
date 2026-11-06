# World Cup 98 (PC, EA Sports, 1998) — External Research Dossier

Compiled: 2026-11-01 (web-only research; no game-file inspection performed here).
Scope rule: **WC98** = "World Cup 98 / FIFA World Cup 98" (this project). **RTWC98** = "FIFA: Road to World Cup 98" (1997 sibling). RTWC98 facts are kept separate; engine-family carryover into WC98 is marked as hypothesis.
Notation: [S#] = source URL (see §Sources). Claims marked (RTWC98) are facts about the sibling game, NOT confirmed for WC98.

---

## 1. Confirmed findings (WC98-specific)

### Identity, lineage, release
- Developed by EA Canada, published EA Sports; first official FIFA World Cup game after EA obtained FIFA rights (exclusive for 1998 WC outside Japan, acquired 1996). Platforms: Windows, PS1, N64, Game Boy. [S1]
- Release: Europe 30 Apr 1998, NA 19 May 1998, UK 22 May 1998, Japan 5 Nov 1998 (PS1 port by Electronic Arts Square as "FIFA World Cup 98: France Sōshūhen"). [S1][S2]
- **"The game engine is based on that of FIFA: Road to World Cup 98"**, with minor gameplay improvements (in-game strategy changing, player positioning). PC version uses DirectX / a 3D engine (previous WC games were 2D bird's-eye). [S1]
- A 1998 GamesDomain review (reproduced on MyAbandonware) confirms WC98 = RTWC98 engine iteration: "the minor improvements and enhancements in game design do not offer enough over the game's all-too-recent predecessor", "graphics polished… support for D3D cards has been added out of the box", "play-by-play scripts more varied", improved player control/shooting feel. [S3]
- Multi-language retail: MyAbandonware ISO lists EN/FR/DE/ES/IT on one 390 MB download; alt names include "Coupe du Monde 98", "Frankreich 98", "Copa del Mundo: Francia 98". Matches our multi-language InstallShield ISO. [S3]
- Community consensus: "World Cup 98 has the least problem running on modern PC" (vs RTWC98) when nGlide installer + compatibility fix are applied. [S4]

### Game modes & rules
- **40 teams**: every 1998 WC qualifier (32) + 8 non-qualifiers: Australia, Canada, China, Greece, Portugal, Republic of Ireland, Russia, Sweden. All with authentic kits (goalkeepers get generic kits). [S1]
- **World Cup mode**: real 1998 groups (or random groups from the 40); each match in a recreation of the real venue. Group games do NOT go to extra time or shootouts; knockout matches do. Man-of-the-match / clean-sheet captions; Golden Boot + FIFA Fair Play Award captions at tournament end. [S1]
- **Friendly mode**: any included team; on a draw the player chooses: end as draw, golden-goal extra time, or shootout. [S1]
- **Squad customisation**: national squads editable from a reserves pool (as in RTWC98). [S1]
- **World Cup Classics**: 15 classic WC matches; the 1982 final unlocked by winning World Cup mode, then each unlocks the next in order. Period-accurate kits (except goalies), hairstyles, names. Commentary by Kenneth Wolstenholme (this mode only). 1950/1954/1966 finals rendered **black-and-white**; 1930 & 1938 matches in **sepia**. Substitutions disabled for matches predating the introduction of substitutions — including the 1970 final. Known historical inaccuracies: cards shown for pre-1970 matches; brown leather ball used from 1970 on. [S1]
- **UK version**: commentary by John Motson & Chris Waddle; match intros by Des Lynam & Gary Lineker. [S1] (The GamesDomain review mentions "play-by-play man John Motson".) [S3]
- **Music**: intro song is Chumbawamba "Tubthumping"; all in-game music licensed (various artists). [S1][S2]
- Modes: single-player + multiplayer (taxonomy; see §1 PC technical for what "multiplayer" concretely means on PC). [S2]

### PC rendering stack (matches our FIFAWC.EXE + thrash/ DLL findings)
- Renderers: **Software, Direct3D (v5), Glide (3dfx)**. Only **640x480**. Vsync on by default in D3D/software; **Glide mode capped at 63 FPS**. Disc-check DRM. 32-bit executable (LAA flag may be needed on modern Windows). [S2]
- GALAHs 3D Patch README (WC98-specific) documents the driver layout in detail:
  - `thrash\voodooa.dll` = Glide2x (Voodoo 1 / Voodoo Rush); `thrash\voodoo2a.dll` = Glide3x (Voodoo 2/3/4/5, Banshee); Direct3D driver `thrash\d3da.dll` (the patch renames it `dxa.dll`); `3DSetup.exe` + `3DSetup.dll/.ini` config utility; default install dir `C:\Program Files\EA SPORTS\World Cup 98`. [S5]
  - Engine constraints: **frame rate limited to 85 fps**; **resolution 640x480 "programmed into wc.exe and cannot be easily modified"** (800x600 present in 3DSetup "for experimentation"); **16-bit colour depth**; triple-buffering supported on capable cards; requires DirectX 6+ (tested on DX 8.0a/9.0b, Win95/98/ME/XP). [S5]
  - nGlide/Zeus forum confirms: "It should be **voodooa.dll**" (the replacement target), PCGW's "voodoo2a.dll" in its table stems from a user typo; 3DSetup.exe itself fails to launch on modern Windows (0x0000142) and must NOT be run: "You simply can't run 3D Setup. You just need to run a game right after installation of the patch." [S6][S7]

### Install / 64-bit / registry
- Original setup is 16-bit → cannot run on 64-bit Windows. PCGW workaround: InstallShield 3 32-bit Generic Installer; copy CD contents to temp folder, run `setup32.exe` from `SETUP\<language>`; dismiss repeated "Error: Can not detect hardware!"; change destination path. **CD-check registry key: `HKLM\Software\Wow6432Node\EA SPORTS\World Cup 98`, value `CD Drive`** must point at a valid (physical/virtual) drive. [S2]
- nGlide path: install nGlide, download WC98 compatibility fix (fifawc98_patch.zip), replace `voodooa.dll` in `<game>\thrash`, apply `fifawc98.reg` (edits `HKLM\SOFTWARE\EA SPORTS\World Cup 98`; add `WOW6432Node` on 64-bit), run `PatchInstall.bat` → registers a "FIFA World Cup 98 Patch" SDB compatibility entry. [S2][S6][S8]
- Black-screen fix: run `FIFAWC.EXE -no3dx` for software rendering (software renderer "extremely glitchy on new systems"); recommended fix is the nGlide path. [S2]
- Sound issues: fix via Creative ALchemy Universal profile. [S2]
- Save data: `<path-to-game>\USER`. Configuration file location: listed as none/blank on PCGW. [S2]
- Input: DirectInput controllers supported; "works well with Xbox 360 controllers". Keyboard remapping: no. [S2]
- System requirements (PCGW): Windows 95/98, Pentium 166 MHz, 16 MB RAM, 120 MB HDD, SVGA True Color. (RTWC98: Pentium 100, 150 MB — WC98 raised CPU floor, lowered disk.) [S2][S9]
- Localization: English UI/audio; community Czech translation exists. [S2]

### Community patches (WC98)
- **World Cup 98 Ultimate Patch** (moddb, Sports Sim Labs & Co, 2018): `wc98ultimatepatch.zip`, 2.61 MB, MD5 `5d878dc72af87bce1c48fdfd47c428a0`; "Solution suite for running World Cup 98 on Microsoft Windows 7/8/8.1/10"; tags: dgvoodoo, matts nhl, galahs patch. Full expanded description not publicly indexed. Uploader contact admin@sportsimlabs.com; sportsimlabs.com is now a parked Namecheap page (dead site). [S10][S11]
- **FIFAWC.EXE 3rd-Party Loader** (moddb, 2017): replacement FIFAWC.EXE, 1.68 MB, MD5 `1a23c1c46547078cb36985a1c79a3adc`; "Available for World Cup 98, NOT FOR FIFA 98!"; credits "Galah for 3D Patch. Others for direct game file loader."; contact admin@sportsimlabs.com. [S12]
- **GALAHs FIFA WC98 3D Patch v1.02** (PDF, fifa.galahs.com.au): new Voodoo/Voodoo2 thrash drivers, latest EA 3DSetup utility; "100% compatible with all 3D Accelerated graphic cards (D3D and 3dfx)"; self-extracting installer; uninstall = restore original `voodooa.dll`, `voodoo2a.dll`, `dxa.dll`, `3DSetup.exe`, `3DSetup.dll/.ini` from CD. [S5]
- **nGlide compatibility fix** (zeus-software): `fifawc98_patch.zip` = replacement `voodooa.dll` + `fifawc98.reg` + SDB patch (PatchInstall.bat). Forum thread documents behaviour (see rendering stack above). [S6][S8]
- **nGlide fix contents VERIFIED 2026-11-06** (`research-inbox/raw/fifawc98_patch/`, from zeus-software.com/files/nglide/fifawc98_patch.zip): `fifawc98.reg` sets `HKLM\SOFTWARE\EA Sports\World Cup 98` = {`3D Card`="3Dfx Voodoo", `Thrash Driver`="voodoo", `Group`="3Dfx", `D3D Device`=0, `Triple Buffer`=1, `Hardware Acceleration`=1, `Thrash Resolution`="640x480"} — forces the Glide path; registry model matches our STATUS findings. `fifawc.sdb` (sdbinst) = app compat entry "Fifa World Cup 98 Patch" for fifawc.exe: shims **SingleProcAffinity, GlobalMemoryStatusLie, EmulateDirectDrawSync**. `thrash/VOODOOA.DLL` shipped by the patch is nGlide's own Glide2x wrapper (131072 B, 1999-09-17) replacing EA's ISO file (91120 B, 1998-03-05); not an EA binary. Confirms engine hard-lock 640x480 + single-core affinity + DDraw sync emulation as the stability surface a native port must cover natively.
- **Audio extractor**: "FIFA 98 and World Cup 98 Audio Extractor" (soccergaming, 2026, by adams06) — a Windows tool extracting audio from the PC versions of **both FIFA 98 and World Cup 98**; title tags the "(PBK)" bank format; download on Google Drive. [S13]
- ISO availability: archive.org `wcuppccd` ("FIFA World Cup 98 - France (PC)", 548 MB; disc root folder `WCUPPCD`), MyAbandonware 390 MB ISO, old-games.com 304 MB. [S14][S3][S15]
- Reception context: PC version 76% GameRankings; praised as "best soccer game on PC" (PC Zone 91%, Computer Games Strategy Plus 5 stars). [S1]

---

## 2. FIFA RTWC98 facts (separate — engine-family reference only)

- PC release 17 Jun 1997. PC folders (from PCGW manual-install recipe): root `eacsnd.dll`, `fifa98.ico`, `fifartwc.exe`; data folders `AUDIO`, `COMM`, `COMMON`, `FEPACK`, `INGAME`. CD-check via `INSTALL.DAT` + `key.reg`. [S9]
- **Multiplayer (PC)**: "NETWORK PLAY: up to 20 players on 8 PCs" — requires "**IPX compliant network**", 2–8 PCs, up to 4 players/PC; "**MODEM PLAY: up to 8 players on 2 PCs**"; F9 chat / F10 disconnect hotkeys; community "FIFA Online" internet play existed (contributor Matt Holme). PCGW: local play 4 players (mouse usable), LAN play native 20 players. (All RTWC98.) [S16][S9]
- D3D support added by **official patch 1.30** (4 Feb 1998); 1.40 fixes 3dfx Voodoo2 issues. WC98 shipped D3D out of the box (no official-patch dependency). [S9][S3]
- RTWC98 music: Blur "Song 2" + The Crystal Method. Keyboard defaults (RTWC98 FAQ): arrows = run, S = pass, D = shoot/tackle, A = lob/slide, W = sprint, Q = pass/lunge, Alt/Ctrl = skill moves. 40,000-word commentary script, 172 teams, 16 stadiums. (RTWC98.) [S9][S16]
- RTWC98 does not work on systems with >1 GB RAM (community report). [S4]
- **fifa98edit** (Python 3 editor for RTWC98): reads/writes **`FCDB.DBI` / `FCDBPENG.DBI`** (player/team database) and **`TMMNG.DB`** (team-management file: tactics, lineups, set-piece takers — pitch-position binary not fully deciphered by the author); reads jersey textures from `INGAME\PLAYER\TEXTURES\PLYRKITS\JERSnn.FSH` (+ `3DTEXHI.FSH`, `HIHEAD.FSH`); 17 player attributes (aggression, acceleration, attack bias, agility, ball, awareness, fitness, creativity, passing, heading, reaction, pass bias, shot power, shot bias, speed, shot accuracy, tackle) on values 39–99 in steps of 4, plus Average/Index fields in a fixed-width record layout; can export whole DB to JSON; adding nations requires writing **RefPack-compressed** flag graphics via KUDr's `compressd.exe`/`decompressd.exe`. [S17][S18][S19]
- Community: ".dbi file format (used) up to 2003" — a user's claim in the fifa98edit thread (FEd98 was the contemporary editor). [S20]
- RefPack: EA's proprietary compression, documented at Niotso wiki, used across many EA titles (into the Sims 4); KUDr's tool (repo `MicaelJarniac/RefPack-Tool`, VS solution named **`big.sln`**) + `gibbed/Gibbed.RefPack` are the reference implementations. [S21]
- BIG archive family: BIG/BIGF/BIGH/BIG4 are EA formats derived from EA IFF 85; specs/docs: OpenSAGE docs (16-byte BIG header), Thyme wiki (BIG in Redwood/Blizzard games), `unvivtool`, `withmorten/big4f` (pack/unpack BIG4 + BIGF), `GHFear/RW4ArchiveTool` explicitly unpacks "**BIG (BIGF) Archive**" from EA Games/EA Sports titles. [S22][S23][S24][S25][S26]
- EA Sports data stack corroboration (MVP Baseball 2005, PSP): "ISO image > **EA BIGF archive** > **RefPack compressed CSV tables**" — the BIGF→RefPack→table-data layering is standard for EA Sports. [S27]
- RTWC98 PS1/other: FIFA 98 demo on archive.org (`FIFAWC98`); emulator compat reports exist (Ymir, beetle-psx). [S28][S29]
- 1997 FIFA source-code leak (Kotaku story): community hunt (assemblergames/betaarchive) found only **Actua Soccer / UEFA Euro '96** source leaked on Joystick #71 (FR, May 1996) coverdisk; JayFoxRox concluded the FIFA leak "is a hoax or simple misunderstanding". → **No FIFA/WC98 source code in the wild.** [S30]

---

## 3. Strongly supported hypotheses (WC98; external evidence, not WC98-confirmed)

1. **WC98 uses the same DBI/FSH/RefPack data family as RTWC98** (FCDB*.DBI, FSH kit/texture files, RefPack-compressed graphics, TMMNG.DB for lineups). Basis: Wikipedia "engine based on RTWC98" [S1]; GamesDomain "patch-like" successor review [S3]; our file set already shows FCDB/FSH; fifa98edit's RTWC98 paths (FCDB.DBI, JERSnn.FSH under INGAME\PLAYER\TEXTURES\PLYRKITS) [S17]; identical 3DSetup/thrash tooling. Start RE by adapting fifa98edit's parser to WC98's FCDB and verifying offsets.
2. **WC98 audio = EACSND.DLL driving PBK banks** (RTWC98 uses EACSND.DLL [S9]; adams06's extractor covers "FIFA 98 **and** World Cup 98" audio, tagged "(PBK)" [S13]; Reddit extractor for FIFA 98: "works very well with '.bnk' files… these seem to use a '.PBK' file format" [S31]). WC98 ships the same EACSND.DLL (context) → commentary/SFX extraction path: parse PBK index + samples; community extractor is a working starting point.
3. **WC98 PC has LAN-capable code (likely RTWC98's IPX stack)**. Basis: FIFAWC.EXE imports WSOCK32 (context); PCGW categorises the WC98 page under "Local multiplayer games" **and "Self-hosting"** [S2]; sibling engine ships IPX 20-player LAN + 8-player modem [S16][S9]. NOT confirmed: PCGW's WC98 LAN/online table icons show no native support entries — likely LAN stripped, local-only on PC.
4. **Ultimate Patch internals**: ships a dgVoodoo2-derived wrapper + Galah 3D-patch components + registry/compat fixes, replacing FIFAWC.EXE/thrash DLLs (its moddb tags are dgvoodoo/galahs patch/matts nhl — "matts nhl" is the Sports Sim Labs companion project pattern [S10][S11]). Contents unverified (download behind moddb gate).
5. **640x480 / 16-bit / 85 fps are hard engine limits of the WC98 EXE** (Galah docs: "programmed into wc.exe") — implies a reimplementation must render at 4:3 640x480 pixel-accurate, 16-bit palettized assets. [S5]
6. **Glide renderer targets the 3dfx Glide2/Glide3 API surface exactly** (voodooa=Glide2x, voodoo2a=Glide3x) — a Linux reimplementation's reference render path can be a Glide-semantics surface; nGlide proves the API mapping. [S5][S6][S8]
7. **Commentary is script-driven sampled speech** (RTWC98 advertises a 40,000-word commentary script (RTWC98) [S16]; WC98 review notes "play-by-play scripts a bit more varied" and criticises commentary depth "at FIFA 96 level" [S3]) → expect many short speech samples keyed to events, not streaming voice.

---

## 4. Unverified hypotheses (weak/thin evidence)

- WC98 keyboard/controller defaults equal RTWC98's (arrows/S/D/A/W; controller A=lob, B=?, etc.) — RTWC98 FAQ only; no WC98 PC manual scan found. [S16]
- WC98 Classic matches use fixed historical lineups/scripts beyond kits+narration (no source beyond Wikipedia's kit/narration notes). [S1]
- WC98 supports 2 players on one keyboard/mouse split-screen as local play definition (RTWC98 PCGW note says local play supports 4 players with mouse as a pad [S9]; WC98 unlisted).
- `-no3dx` is the only documented CLI flag (PCGW [S2]); other family flags (e.g., RTWC98-era launch flags) untested for WC98.
- The `dxa.dll` rename (Galah [S5]) implies D3D renderer DLL is loaded from `thrash\` by filename registry key — registry-driven renderer selection mechanism is inferred, not documented.
- PBK bank container details (index entry format, EAM header) from Reddit thread only partially accessible (search snippet); needs primary thread read + hands-on verification with adams06's extractor. [S31][S13]
- WC98's MIDx files are General MIDI music (our naming; no external doc; RTWC98 used standard audio folder with EA formats).
- THRASH renderer DLL interface is the same ABI in FIFA97/98/99/WC98/WC2002 (nGlide patches exist for fifa99 + WC2002 + WC98 [S8]) — same family tooling suggests shared ABI, not proven.
- GamesDomain claims WC98 has "only 32 World Cup qualifiers and a few special also-rans" and "no league play" (it counted ~32–40; Wikipedia says 40 — Wikipedia is authoritative). [S3][S1]

## 5. Unknowns / gaps to resolve (targeted next steps)

1. **WC98 PC manual**: no scan found. Searched archive.org (only N64 RTWC98, GB World Cup 98 via gamegear, Winning Eleven 3 WC98, Saturn RTWC98 manuals exist), MobyGames additional-material (404), general web. → keyboard/controller defaults, documented multiplayer options, install/serial, tournament rule text unconfirmed. Try: manualworld/ManualsLib, MobyGames screenshots section, forum PM to MyAbandonware contributors, physical rip.
2. **WC98 multiplayer truth**: is there ANY menu for network/modem/serial? PCGW table shows no native LAN/online; categories say local + self-hosting. Partial resolution 2026-11-06 (import + string probes): FIFAWC.EXE imports WSOCK32.dll by ordinal only — 12 slots {2,3,6,9,12,17,18,20,21,23,115,116} (Watcom strips names). String table: `DOS IPX compatible` (IPX detect), `MODEM`/`MODEMS`, `user/modems.cfg`, `user\network.dat`, netplay console verbs `getnetplayers` / `killnetplayer - ONLY THE GAME SERVER MAY CALL...` → the RTWC98 IPX/modem **netplay stack is linked intact** into the retail WC98 exe (server/kill/get player commands, modem config file, network.dat save). Menu/UI presence unmeasured (FE scene scan pending); PCGW "no native LAN" describes shipped menus, not binary. Resolve via Ghidra xrefs to WSOCK IAT slots + FE scene enumeration [internal task].
3. **fifawc98_patch.zip contents** RESOLVED 2026-11-06 — downloaded + diffed; see "Community patches" VERIFIED entry (reg keys, SDB shims SingleProcAffinity/GlobalMemoryStatusLie/EmulateDirectDrawSync, nGlide voodooa replacement). [S8]
4. **Ultimate Patch contents** — moddb direct download is behind a Cloudflare JS challenge (curl hits `cdn-cgi/challenge-platform`, 2026-11-06); md5 `5d878dc7…` on record; remains black box until fetched with a browser. [S10]
5. **WC98 official EA patch**: RESOLVED (negative) 2026-11-06 — no official EA patch exists; ModDB WC98-family file list contains only community artifacts (Ultimate Patch, 3rd-party loader), PCGW documents none. ISO `FIFAWC.EXE` build stamp 1998-04-13 = final gold master. [S2][S10]
6. **Exact FSH/FCDB/DBI binary layouts for WC98**: fifa98edit targets RTWC98; validate field offsets (esp. hair/face/skin, kits, formations) against WC98 FCDB; TMMNG.DB tactics encoding incompletely understood even for RTWC98 [S17][S18].
7. **PBK/BNK/EAM format spec**: no public spec found; Reddit + adams06 tool are the only known material. [S31][S13]
8. **Wolstenholme Classics audio assets**: on which files, same PBK path? unverified.
9. **Stadium models**: 1998 French venues recreated [S1][S3]; storage format/location (BIGF? INGAME folder layout) undocumented externally.
10. **FIFA Online / NetWorld services for WC98**: none found; RTWC98 only. [S16]
11. **No prior WC98 reimplementation exists** — searches for "world cup 98 reimplementation", "wc98", "fifa 98 native" on GitHub found only: fifa98edit (editor), RefPack/BIG tools, GALAHs/Sports Sim Labs patches, emulator compat issues (Ymir, beetle-psx), an unrelated modern browser football clone (`modelence/open-soccer`), and a `worldcup98-dataset` (real-world tournament stats, not the game). → we are first; no WC98-specific source to reuse. [S17][S21][S22][S23][S24][S25][S26][S29][S32][S33]

---

## Sources

- [S1] Wikipedia — World Cup 98 (video game): https://en.wikipedia.org/wiki/World_Cup_98_(video_game)
- [S2] PCGamingWiki — FIFA World Cup 98: https://www.pcgamingwiki.com/wiki/FIFA_World_Cup_98 (saved copy read fully; categories incl. "Local multiplayer games", "Self-hosting")
- [S3] MyAbandonware — World Cup 98 (games metadata, 5-lang ISO, GamesDomain review, user play reports): https://www.myabandonware.com/game/world-cup-98-g5d
- [S4] SoccerGaming Q&A thread (incl. World Cup 98 PC; modern-PC behaviour): https://soccergaming.com/forums/threads/fifa-98-road-to-world-cup-q-a-thread-world-cup-98-incl.182225/
- [S5] GALAHs FIFA WC98 3D Patch v1.02 README (PDF): https://fifa.galahs.com.au/fifawc98_3dpatch.pdf
- [S6] Zeus Software forum thread t=712 (WC98 black screen; voodooa.dll correction; no-3DSetup rule): https://www.zeus-software.com/forum/viewtopic.php?t=712
- [S7] PCGW nGlide section (WC98 page) — voodoo2a.dll typo lineage per [S6]: https://www.pcgamingwiki.com/wiki/FIFA_World_Cup_98#nGlide_wrapper
- [S8] nGlide compatibility list, WC98 entry: https://www.zeus-software.com/downloads/nglide/compatibility
- [S9] PCGamingWiki — FIFA: Road to World Cup 98: https://www.pcgamingwiki.com/wiki/FIFA:_Road_to_World_Cup_98
- [S10] ModDB — World Cup 98 Ultimate Patch: https://www.moddb.com/games/fifa-road-to-world-cup-98/downloads/world-cup-98-ultimate-patch
- [S11] sportsimlabs.com (now parked Namecheap domain): http://www.sportsimlabs.com/
- [S12] ModDB — FIFAWC.EXE 3rd-Party Loader: https://www.moddb.com/games/fifa-road-to-world-cup-98/downloads/fifawcexe-3rd-party-loader
- [S13] SoccerGaming — FIFA 98 and World Cup 98 Audio Extractor (adams06, 2026): https://soccergaming.com/forums/threads/fifa-98-and-world-cup-98-audio-extractor.6475947/
- [S14] archive.org — wcuppccd (WC98 PC ISO): https://archive.org/details/wcuppccd
- [S15] Old-Games — FIFA World Cup 98: https://www.old-games.com/download/6236/fifa-world-cup-98
- [S16] GameFAQs FAQ — FIFA: Road to World Cup 98 (PC, EDysinger; IPX/modem sections, keyboard map; saved copy at gamefaqs.gamespot.com-57dd2d5869.md): https://gamefaqs.gamespot.com/pc/197317-fifa-road-to-world-cup-98/faqs/2252
- [S17] GitHub — ma-akad/fifa98edit README: https://github.com/ma-akad/fifa98edit
- [S18] GitHub raw — fifa98edit __init__.py (FCDB.DBI/FCDBPENG.DBI, JERSnn.FSH paths, TMMNG.DB, RefPack bins): https://raw.githubusercontent.com/ma-akad/fifa98edit/master/__init__.py
- [S19] GitHub raw — fifa98edit average_fifa.py (17 attributes, 39–99 scale, per-role distributions): https://raw.githubusercontent.com/ma-akad/fifa98edit/master/average_fifa.py
- [S20] SoccerGaming — New Editor for FIFA RTWC 98 (thread + page 2; ".dbi … up to 2003", TMMNG.DB, RefPack flags): https://www.soccergaming.com/forums/threads/new-editor-for-fifa-rtwc-98.6468491/ and https://soccergaming.com/forums/threads/new-editor-for-fifa-rtwc-98.6468491/page-2
- [S21] GitHub — MicaelJarniac/RefPack-Tool (KUDr; Niotso wiki RefPack spec; Gibbed.RefPack): https://github.com/MicaelJarniac/RefPack-Tool ; spec: http://wiki.niotso.org/RefPack
- [S22] OpenSAGE docs — BIG file format: https://github.com/OpenSAGE/Docs/blob/master/file-formats/big/index.rst
- [S23] Thyme wiki — BIG-File-Format: https://github.com/TheAssemblyArmada/Thyme-Wiki/blob/master/BIG-File-Format.md
- [S24] GitHub — bfut/unvivtool (BIGF/BIGH/BIG4 derived from EA IFF 85): https://github.com/bfut/unvivtool
- [S25] GitHub — GHFear/RW4ArchiveTool (EA Sports BIGF unpack): https://github.com/GHFear/RW4ArchiveTool
- [S26] GitHub — withmorten/big4f (BIG4/BIGF pack/unpack): https://github.com/withmorten/big4f
- [S27] GitHub — hiitsgabe/console_utilities sports-rom-patcher.md (EA BIGF > RefPack > CSV tables, MVP Baseball 2005): https://github.com/hiitsgabe/console_utilities/blob/main/docs/sports-rom-patcher.md
- [S28] archive.org — FIFA 98 RTWC Demo: https://archive.org/details/FIFAWC98
- [S29] GitHub — beetle-psx-libretro issue #235 (RTWC98 emulation): https://github.com/libretro/beetle-psx-libretro/issues/235
- [S30] ASSEMBLERgames — ~1997 FIFA source-code leak (hoax assessment; Actua Soccer Euro96 leak confirmed): https://www.assemblergames.org/viewtopic.php?t=375
- [S31] Reddit — Extracting speech/commentary from FIFA 98 (RTWC98; .bnk/.PBK knowledge; fetch blocked, snippet-quoted): https://www.reddit.com/r/retrogaming/comments/1e0zu0w/extracting_speech_commentary_from_fifa_98_road_to/
- [S32] GitHub — modelence/open-soccer (unrelated modern clone): https://github.com/modelence/open-soccer
- [S33] GitHub — nimamahmoudi/worldcup98-dataset (real-world stats, not the game): https://github.com/nimamahmoudi/worldcup98-dataset

---

All external claims carry source URLs. WC98-specific facts are kept separate from
FIFA RTWC 98 family reference facts; carryovers are flagged hypotheses.
