# World Cup 98 — FIFAWC.EXE First-Pass Static RE Analysis

Date: 2026-11-01 · Analyst: Hermes static pass (objdump/readelf-style custom parser, pefile, string mining)
Scope: `/mnt/1st4TB/WorldCup98/WorldCup98_Extracted/` — game tree extracted from `EA-WorldCup98-Win95.iso` (EN/FR/DE/ES/IT ISO).
Tagging: **CONFIRMED** = quoted offset/string from the binary; **HYPOTHESIS** = inference.
No game file was modified. Analysis artifacts: `/mnt/1st4TB/WorldCup98/analysis/` (rawpe2.py, strings_off.py, pe/raw dumps, per-file offset-tagged string lists).

Integrity (MD5):
```
0948186f3d4a68918073a683594977f2  FIFAWC.EXE
1e9e2f846b6dd8ef1e88614b11868605  EACSND.DLL
fe74e236ba782807e87202224b87ee30  EACSNDD.DLL
87d0e57f137239e1f8cb5a423978d90c  THRASH/D3DA.DLL
cc334b08939705b7023281a1e9df51d4  THRASH/D3DD.DLL
4dbb3f44d9478b4e67d3f0ba249ed866  THRASH/POWERVRA.DLL
d7ab35a0f7674259359be752d6ca67c7  THRASH/POWERVRD.DLL
8798e33c5d4acea220b1bd42ed571846  THRASH/VOODOOA.DLL
743b8e7009431ca9f06da0a842c5ebd8  THRASH/VOODOOD.DLL
b0ea7ce321d4cc6a9d8a4b209d3e7fa5  AUTORUN.EXE
```

> **Header-format note (matters for every offset claim):** all EA/Watcom binaries are PE32 with the *old* (linker 2.x, SizeOfOptional=224) optional header: **section VirtualSize fields are written as 0**; code/data sizes live in the `SizeOfCode/SizeOfInitData/SizeOfUninitData` fields; standard pefile walks of directories/imports partly fail. RVA→file mapping must use next-section VA boundaries (implemented in `analysis/rawpe2.py`).

---

## 1. PE layout — FIFAWC.EXE

`FIFAWC.EXE`, 1,762,304 bytes. i386 (0x14C), PE32, **linker version field 2.18**, ImageBase `0x400000`, SizeOfImage `0x28f000` (pefile) , EntryRVA `0xce328` (inside AUTO), BaseOfCode `0x1000`, BaseOfData `0x158000`, SizeOfCode `0x157000`, SizeOfInitData `0x3e800`, SizeOfUninitData `0xdd600`. File chars 0x0182. Subsystem: **WindowsGUI** (pefile; the raw old-header byte at the modern Subsystem offset reads 0xF000 — header quirk, not the real value). Section Alignment 0x1000, File Alignment 0x200.

| # | Name | VA | VirtSize(field) | RawOff | RawSize | Entropy | Flags | Meaning |
|---|------|----|----|--------|---------|---------|-------|---------|
| 0 | `AUTO` | 0x001000 | 0 | 0x000400 | 0x157000 | 6.61 | 0x60000020 | code (Watcom AUTO segment) |
| 1 | `.idata` | 0x158000 | 0 | 0x157400 | 0x01200 | 5.39 | 0xC0000040 | import tables |
| 2 | `DGROUP` | 0x15a000 | 0 | 0x158600 | 0x3d600 | 4.81 | 0xC0000040 | initialized data |
| 3 | `.bss` | 0x198000 | 0 | 0 (none) | 0xdd600 | – | 0xC0000080 | uninit data |
| 4 | `.reloc` | 0x276000 | 0 | 0x195c00 | 0x18800 | 6.82 | 0x42000040 | base relocs |

Data directories: only `IMPORT` (RVA 0x158000, 4573 B) and `BASERELOC` (RVA 0x276000, 100160 B). **No export dir, no resource dir** (FIFAWC.EXE carries no version info / icons).
**Overlay: 0 bytes** — file size 1,762,304 = last section end (0x195c00+0x18800). CONFIRMED.
Timestamps: PE timestamp `892469305` = **1998-04-13 12:08:25 UTC** (matches file mtime Apr 13 1998 on CD). Symtable ptr 0.

**Linker signature — CONFIRMED Watcom:** AUTO/DGROUP segment names + linker 2.18 (same marker family as FIFA 99's "Watcom linker, segments AUTO/DGROUP, linker 2.x" — see `/mnt/2nd4TB/FIFA99/fifa99-native/docs/RESEARCH.md`), and RTCS banner at **0x000cd731**: `WATCOM C/C++32 Run-Time system. (c) Copyright by WATCOM International Corp. 1988-1995. All rights reserved.` The exact Watcom product release (11.x/12.x) is a **HYPOTHESIS** (linker 2.18 + 1995 RTCS banner ≈ Watcom C/C++ 32A 11.x/12.0 generation).

**Not packed — CONFIRMED:** overlay 0, AUTO entropy 6.61 (normal x86 code), no Petite/UPX/`.cms_t` section, no `WATCOM` banner compression artifacts. The CD `FIFAWC.EXE` is the plain final retail build.

## 2. Import tables — FIFAWC.EXE (173 total; full lists in `analysis/rawpe_out.txt` lines 14–195)

| DLL | # | Highlights |
|-----|---|-----------|
| KERNEL32 | 105 | files/mem/threads; `GetPrivateProfileStringA`, `GetVolumeInformationA`, `CreateMutexA/FindWindowA`(import table), comm API (`GetCommState/SetCommState/PurgeComm/EscapeCommFunction/GetCommModemStatus` = direct serial/modem), `MapViewOfFile` |
| USER32 | 39 | windowing + `SetWindowsHookExA/CallNextHookEx/keybd_event` (keyboard hook), `joy*` via WINMM |
| ADVAPI32 | 6 | registry read only: `RegOpenKeyExA, RegQueryValueExA, RegEnumKeyExA, RegQueryInfoKeyA, RegFlushKey, RegCloseKey` |
| WINMM | 8 | `joyGetDevCapsA, joyGetNumDevs, joyGetPosEx, timeSetEvent, timeKillEvent, timeGetDevCaps, timeBeginPeriod, timeEndPeriod` |
| DDRAW | 2 | **`DirectDrawCreate`, `DirectDrawEnumerateA`** — the exe itself talks DirectDraw |
| GDI32 | 1 | `GetStockObject` |
| WSOCK32 | 12 | all by ordinal (below) |

**WSOCK32 ordinals resolved** (Winsock2/WSOCK32 shim ordinal table, e.g. strontic ws2_32 ordinal map): `#115 WSAStartup, #116 WSACleanup, #23 socket, #2 bind, #3 closesocket, #6 getsockname, #9 htons, #12 inet_ntoa, #18 select, #20 sendto, #21 setsockopt, #17 recvfrom`. → **datagram-style I/O only** (sendto/recvfrom/select; no connect/listen/accept/recv/send) — classic connectionless LAN transport. CONFIRMED ordinal→API set; **HYPOTHESIS:** used for IPX (and/or UDP broadcast) netplay.
No DirectPlay (`dpnet`/`dplobby`) import, no `DPLAYX`, no IPX-specific DLL — transport is hand-rolled over Winsock ordinals.

## 3. Strings mining (file offsets; full lists `analysis/strings/*.str.txt`)

### 3.1 Registry
- **0x158668** & **0x1602d4** `SOFTWARE\EA SPORTS\World Cup 98` (CONFIRMED).
- Config value names next to first key (0x158604–0x15862c): `3D Card`, `Thrash Driver`, `Triple Buffer`, `Hardware Acceleration`; driver-name values `voodooa`, `powervra`, `d3da` (0x15864c–0x158660), vendor tags `3dfx` (0x158688), `ATI Rage Pro` (0x158698).
- Language/CD block (0x160348/0x160354): `Language`, `CD Drive`; language list 0x1602f4–0x16032c: `Dutch English French German Italian Danish Spanish Swedish` (8 languages in the build; ISO is EN-FR-DE-ES-IT).
- Modem/TAPI keys: 0x34130 `System\CurrentControlSet\Services\Class\Modem`; joystick registry probe 0x16875c `System\CurrentControlSet\Control\MediaResources\Joystick`, 0x1687ac `Joystick%d%s`, 0x1687bc `...\MediaProperties\PrivateProperties\Joystick\OEM`.
- Netplay service names (IPX service registry style): 0x159994 `MODEM`, 0x15999c `CONF`, 0x1599a8 `FIFAPW`, 0x1599c0 `GAMELIST`, 0x1599cc `SERVICELIST`, 0x159954 `MACHINE` (game/machine name service registration strings — EA netplay convention). CONFIRMED strings; semantics = HYPOTHESIS.

### 3.2 Thrash loader strings (exe)
- **0x1586a8** `!thrash\%s.dll` (driver dlopen path template, `!`-prefix = load-library helper).
- 0x1586c0–0x158714: 3dfx Glide **environment** strings `SST_VIDEO_24BPP, SST_SLIDETECT, SST_RGAMMA, SST_GGAMMA, SST_BGAMMA, SST_GAMMA` (exe sets Glide env for the voodoo driver).
- 0x165e1c–0x1660d0: full `_THRASH_*@n` GetProcAddress name table (34 names, exact match to the DLL exports, §5).

### 3.3 Data files / formats (CONFIRMED strings, on-disk inventory via `find` in analysis)
- FE: `fe\feflow86\feflow86.fsh` (0x160368), `.\fe\ingame\ingame.fsh` (0x1603ac), `fe\titles\titl*.fsh` per language (titlengl/titlduch/titlfren/titlgerm/titlital/titldani/titlspan/titlswed, 0x160538–0x1605e0), `legaleng.fsh`, `fe\pack\fefonts.big/igfonts.big`, `.\fe\pack\maus.pak` (0x16276c), `fe\pack\layouts\layouts.big / iglay.big` (0x162e90/0x162eac), `.\fe\ukcred.bin…spcred.bin` credits per language (0x1625ac–0x1625ec), `FE` screens: on-CD `FE/FE.LST` lists **96 FE screens** (`numScreens=96`), `FE/FE.MAK` is the art-build Makefile (`.EXTENSIONS: .fsh .bin .mdl .dat .sqf .his .tri .res`).
- Ingame: `ingame\misc\anim.big` (0x158810), turf textures `turfs0p0..999.fsh`, keeper/paker texture patterns `GK%.2d_%c0.fsh`, `RK%.2d_%c0.fsh`, `PK%.3d%c.fsh` (0x158bc8ff), per-stadium `.big` (pari/stet/lyon/mars/mont/toul/bord/nant/prac/semi/fina/clas, 0x159047+), `trophy.big/world.big/classic.big`; stadium per-day textures `FL0_*.DCT` on CD.
- Audio: `audio\team\A_T0026.pbk` (0x4e310), banks `.BNK` (`AUDIO/{ANTH,CRWD,SFXS}/*.BNK`), `.PBK` patch books (`AUDIO/TEAM/A_T*.PBK`, `AUDIO/CRWD/CROWD.PBK`), `.CRD` cards (`CROWD.CRD`, `TRAFF.CRD`), schedule speech `AUDIO/PBP/SCHEDULE/A_SCHnnn.SMP`, music `.ASF` (`AUDIO/MUSC/*.ASF`), commentary phrases `PHRS.INF` (0x16033c) + `AUDIO/PHRS`.
- Misc containers: `.FSH` 411 files, `.PAK` 793, `.BIG` 36, `.TRE` 9 (all AUTORUN text trees), `.SCI` 87, `.Lay` 29, `.RES` 2 (`FE/TEAMHIST.ENG/ZINTER.RES/ZWCUP.RES`), `.MO/.TBI` (COMMON `ANIM.MO`, `AISEQS.TBI`).
- Saves: 0x162780 `.\user\options.sav`; 0x1645c8 `USER\SLOT%.2d.SAV`; 0x164bec `user\Pdat%02d.sav`; replays `user\rplyindx.dat`, `user\rply%04d.dat` (0x1604a4/0x1604b8); net `user\network.dat` (0x47859), `user\dial.dat` (0x161030), `user/modems.cfg` (0x170e9c). `USER/options.sav` on CD (96 B) begins magic `AFIFAFIF` (hex dump analysis/). No `mltset.dat` string (FIFA 97/98 convention) in this exe — **CONFIRMED absence**.

### 3.4 Renderer APIs in exe
`DirectDrawCreate` + `DirectDrawEnumerateA` imports (0x157c.., §2); no `Direct3DCreate*`, no `gl*`/`gr*` symbols in the exe itself — GL/D3D/Direct3D COM live only in the THRASH driver DLLs (§5). `gr*` names appear in **VOODOO***.DLL (below). CONFIRMED.

### 3.5 Audio stack (exe side)
- Loads `eacsnd.dll` via LoadLibrary at runtime; strings 0x16c3e4 `eacsnd.dll`, 0x16c3f0 `iSNDloaddll - COULDN'T LOAD/FIND EACSND.DLL...`, 0x16c494 `EACSND.DLL VERSION MISMATCH. REQUIRED %i.%i, FOUND %i.%i`; 13 `_iSND*@n` GetProcAddress names at 0x16c450–0x16c612 (exact §4 export list).
- DirectSound paths: 0x1689d8 `WARNING: detectdirectsound NOT IMPLEMENTED IN LIBRARY.`; 0x16c7b0 `iSNDplatformplay - DIRECTSOUND 3D CAN ONLY PLAY MONO UNCOMPRESSED SOUNDS.`; 0x16c8a0 `...3D METHOD SET TO DIRECTSOUND...`.
- winmm: exe imports only `joy*/time*`; **no waveOut/midiOut/mixer imports** — playback goes through EACSND→`DSOUND.dll!DirectSoundCreate`. Mixer sources: 0x16dc18 `mix\smixer.c`. Volume model strings: 0x163aad `Music Volume`, `Commentary Volume`, `Game SFX Volume`, `Menu SFX Volume`.
- EA audio-lib error strings (0x16c…). CONFIRMED.

### 3.6 Networking (exe)
- Own packet layer: `cmn\net.c` (0x167d18), `cmn\connect.c` (0x1681d4), `cmn\packet.c` (0x168300), `cmn\sserver.c` (0x16c2a8); messages `openpacketconnection - ...`, `sendpacket - TOO MUCH DATA...`, `packetlayer_timeout` (0x167f34–0x1685c0).
- Netplay service layer: `initnetservice` (0x167db8), `getnetplayers`, `killnetplayer` (0x167e3c/4c), `killnetplayer - ONLY THE GAME SERVER MAY CALL...` (0x1598ec), `DOS IPX compatible` (0x1598a0) → **HYPOTHESIS (strong): IPX transport for LAN netplay**, datagram Winsock confirmed in §2.
- Modem play: TAPI/modem registry + AT strings `S95=47` (0x159a6c), `NO DIALTONE` (0x1596cc), `mnmdial`/`mnmsetup` (0x1633f8/0x163400 = "modem network manager"), `telephon.ini`, `Modem Type/Dial String/Dial Method` (0x163d9d), `resetmodem Failed` (0x1686ac), speeds `28800/33600/57600` (0x159974ff). CONFIRMED strings.

### 3.7 CD check / DRM
- Import `GetVolumeInformationA`; debug string **0x16ba7c `[FILE] GetVolumeInformation failed (%d)`** (EA `cmn\loadfile.c` family) — CD presence via volume query. CONFIRMED strings.
- No `CMS16/cms32_95/CMS1D.DLL` strings, no SecuROM/SAFE strings, no `DeviceIoControl`/IOCTL, no `.petite`/`.cms_t` sections, no CD-serial table. **CONFIRMED absent** → WC98 has **no EA CMS stub** (unlike FIFA 99's packed CD `Fifa99.exe` with `CMS16.DLL/cms32_95.dll`, FIFA99 RESEARCH.md).
- Candidate volume-label/serial check strings: `FIFA: World Cup '98. PC-CD ` (0x16062c, in FE title/screen context) and `FIFA: Road to World Cup. PC-CD ` (0x165ae0, in version-banner context). **HYPOTHESIS:** one of these is the expected volume label/serial banner check.

### 3.8 Language / version banners (module inventory, `$FIFAVER$` — same convention as FIFA 97/98/99)
`$FIFAVER$FEPCV2-0-3` 0x162750, `AUDIOV0-5-9` 0x165984, `COMMV1-4-7` 0x1659a4, `FE_EACV0-4-1` 0x1659c0, `FE_UKV0-2-7` 0x1659dc, `MISCV0-1-10` 0x1659f8, `RENDERV0-4-1` 0x165a14, `VERSIONV0-2-1` 0x165a30, `VIDEOV0-2-9` 0x165a50, `LIBS_EACV0-0-12` 0x165a6c, `LIBS_SNDV0-0-4` 0x165a8c, `LIBS_CMNV0-3-11` 0x165aac, `CAMERAV12-2-10` 0x166b48, `MOTIONV4-1-12` 0x167044, `REPLAYV13-0-5` 0x1672cf, `IGV5-0-16` 0x1672ec, `AUDIOCMNV18-9-9` 0x16731c, `AI-verV13-6-14` 0x168724, `FCDBV10-5-0` 0x16a320, `FCLGV1-1-2` 0x16a33c, `TOURNAMENTV11-7-9` 0x16a358. Display banner `Version %d.%d.%d` (0x160648). On-CD `COMM/VER.DAT` = `$FIFAVER$V1-1-0$`, `AUDIO/VER.DAT` = `FIFAVER$V3-9-2` (VER.DAT files drive per-package data version checks). **No `FEVpatch`-style patch marker** (FIFA99's were `FEVpatch1`, `VERSIONV5-0-2`) → WC98 CD = original build, not patched. CONFIRMED.

### 3.9 Watcom source-path leaks (module map for RE targeting)
Exe DGROUP (debug-build asserts left paths in): `3rash\dyntexture.c`, `3rash\inithal.c`, `win\initgrf.c`, `win\window.c`, `win\inittmr.c`, `win\palette.c`, `win\pageflip.c`, `win\threads.c`, `win\mutex.c`, `win\key.c`, `win\vidmov.c`, `win\startup.c`, `win\fileio.c`, `cmn\memstd.c`, `cmn\loadfile.c`, `cmn\savefile.c`, `cmn\createw.c`, `cmn\systask.c`, `font\textforf.c`, `font\textset.c`, `mix\smixer.c`, `cmn\net.c`, `cmn\sserver.c` (0x16618c–0x16dc18, 50+ leaks total). → engine = shared EA codebase tree `win/ cmn/ font/ 3rash/ mix/` — **same directory vocabulary as the FIFA 97/98/99 family** (FIFA99 docs use the same lib names). CONFIRMED.
No `FUNC:` symbol leaks in FIFAWC.EXE (FIFA99 had some); Watcom RTCS strings only.

## 4. EACSND.DLL / EACSNDD.DLL (full layout)

Both Watcom (linker 2.18, AUTO/.idata/DGROUP/.bss/.edata/.reloc, VS fields 0, ImageBase 0x400000, timestamps 1998-03-26 23:52:10 / :19 UTC; EntryRVA 0x0 = Watcom DLL quirk).
- **EACSND.DLL** (42,496 B; overlay 0): AUTO 0x7400 @RO 0x400; imports `USER32!CharUpperBuffA`, KERNEL32×46 (Watcom CRT set), **`DSOUND.dll!DirectSoundCreate`** — 48 total. Exports (`.edata` RVA 0xd000), DLL name `eacsnd.dll`, ordinals 1–13 (Watcom stdcall-decorated):
  `_iSNDdllversion@0, _iSNDdirectsetfunctions@28, _iSNDdirectcaps@4, _iSNDdirectstart@8, _iSNDdirectstop@0, _iSNDdirectserve@0, _iSNDdirectcreate3dbuf@32, _iSNDdirectremovebuf@4, _iSNDdirectplay3d@28, _iSNDdirectpos3d@12, _iSNDdirectvol@8, _iSNDdirectrate@8, _iSNDdirectstopbuf@4`
  — identical name set the exe imports by name via GetProcAddress (§3.5).
- **EACSNDD.DLL** (84,264 B): same imports + `GetTickCount` (49), same 13 exports (D = debug variant). **Overlay 38,184 B beyond last section end (0xB400)**: appended blob starting with **ELF magic 0x7fELF** (minimal 32-bit i386 ELF stub, e_shoff 0x34, 9 empty shdrs) followed by Watcom debug data — leaks include `D:\audio\lib\win\sdirect.c` (0x9210 & 0x142a0), `guid105.c/guid106.c`, MIDI Win32 type names (`HMIDISTRM, HMIDIOUT, MIDICALLBACK…` 0x0c60bff), `iSNDmixaudio`, `.debug` strings. → audio library source tree `D:\audio\lib\win\`. CONFIRMED (strings); D-vs-A semantics (debug/release) = HYPOTHESIS, supported by RTCS banner `V1.0 WATCOM` debug markers.
- No SBK/DLS/MPD/XA strings in EACSND*. The `.SBK/.MPD/.DLS` files on CD are **DirectX setup payload** (`DIRECTX5/DIRECTX/DRIVERS/*/SYNTHGM.SBK, TMV1.MPD`) for the sound-reinstaller, not game assets. Game audio formats = `.BNK/.PBK/.CRD/.SMP/.ASF/.SCH` (§3.3). CONFIRMED absence of SBK strings in the DLLs.

## 5. THRASH renderer DLLs (D3DA/D3DD, VOODOOA/D, POWERVRA/D)

All six: Watcom linker 2.18, **ImageBase 0x60000000**, segments AUTO/.idata/DGROUP/.bss/.edata/.reloc, overlay beyond last raw end (D3DA 24,894 B; D3DD 327,900 B; VOODOOA 23,536; VOODOOD 57,081; POWERVRA 20,198; POWERVRD 133,507) — each overlay begins with the same **ELF-magic Watcom debug container** (D3DD overlay contains `C:\3rash\d3d\d3dabout.c` + RTCS type-name table `signed char…wchar_t, THRASHTEXTURE, MSGFUNC` + 6 `.debug*` section names; `.obj` path leaks `d3d\obja\d3d*.obj`, `voodoo\obja\voodoo.obj`, `C:\3rash\cmn\stub.c`). CONFIRMED. `THRASH/VER.DAT` = 2 bytes `0d 0a` (no version data).

**Exports — the THRASH driver ABI, identical 34-ordinal API in all 6** (`_THRASH*@n`, ord 1–34):
`about, clearwindow, clip, drawline, drawlinemesh, drawlinestrip, drawpoint, drawpointstrip, drawpointmesh, drawquad, drawquadmesh, drawtri, drawtrifan, drawtrimesh, drawtristrip, flushwindow, idle, init, is, lockwindow, pageflip, readrect, restore, selectdisplay, setstate, settexture, setvideomode, sync, talloc, treset, tupdate, unlockwindow, window, writerect` (full @sizes in `analysis/rawpe_out.txt`). The exe loads `!thrash\%s.dll` with `%s` = registry value (`voodooa`/`powervra`/`d3da`) and binds these 34 names (string table §3.2). CONFIRMED on both sides.

Imports per driver:
- **D3DA/D3DD**: USER32(8), KERNEL32(57), **DDRAW(2): DirectDrawCreate, DirectDrawEnumerateA**. D3D path = DirectDraw COM + D3D2 COM GUIDs (no `Direct3DCreate` import): leak strings `_IID_IDirect3D2, _IID_IDirect3DHALDevice, _IID_IDirect3DRampDevice, _IID_IDirect3DRGBDevice, _IID_IDirect3DMMXDevice, _IID_IDirect3DTexture2`, guid*.c files, and `D3DINIT.C D3D_createD3D() IDirect3D2_CreateDevice failed` (0xd848). → **Direct3D HAL via Direct3D 2.0 COM**. CONFIRMED.
- **VOODOOA/D**: USER32(4), KERNEL32(56 incl. LoadLibrary/FreeLibrary), DDRAW(1). Glide via **runtime load of `glide2x.dll`** (0xd604) with GetProcAddress names `_grGlideInit@0, _grSstQueryHardware@4, _grSstQueryBoards@4, _grGlideShutdown@0, _grSstSelect@4, _grSstWinClose@0, _grSstWinOpen@28, _grChromakeyValue@4, _grChromakeyMode@4, _grSstStatus@0, _grSstVRetraceOn@0, _grSstIdle@0, _grSstIsBusy@0, _grGlideGetVersion@4`; banner `3Dfx Voodoo`, error `VOODOO_readrect - surface locked. COUNT = %d`. No GLIDE2X import/DLL shipped → **HYPOTHESIS:** requires user-installed 3dfx Glide2.x runtime. SST_* env strings live in the exe (§3.2).
- **POWERVRA/D**: USER32(6), KERNEL32(50–58), DDRAW(1). Banner `NEC PowerVR`; errors `POWERVR_setvideomode - %d bpp not supported by PowerVR hardware.`, `POWERVR_talloc - VALID TEXTURE SIZES ARE 32, 64, 128, OR 256.` → PowerVR (PCX1/2) SDK path; card detection in 3DSETUP reads `SOFTWARE\PowerVR\PCX1&2\HWINI\PVRHAL` (3DSETUP strings 0x37aac). CONFIRMED.
- **VER.DAT / driver strings**: no resolution-list strings in drivers (modes enumerated at runtime via DirectDraw/glide queries); driver display name strings: `Direct3D` (D3DA 0xd404), `3Dfx Voodoo`, `NEC PowerVR`.

Companion tooling (context): `SETUP/3DSETUP.EXE` = MSVC 5.10 MFC app ("3DSetup MFC Application") that probes cards (`d3dtest.dll!D3DTEST_GetCards/IsHAL/TestCard`, PowerVR reg key, `WARNING - You have selected a Direct3D video card that is not supported by this game.`), `SETUP/3DDATA.EXE` Watcom (writes 3D config; registry 6 fns), `SETUP/D3DTEST.DLL` 230,912 B. `DIRECTX5/DSETUP*.DLL` = standard DirectX 5 installer (1997-07-14 mtime).

## 6. Other binaries

- **AUTORUN.EXE** (129,024 B): **MSVC linker 5.0**, 1997-09-18, `.text/.rdata/.data/.idata/.rsrc`, overlay 0; imports COMCTL32, WSOCK32 (ordinals: gethostbyname #52, socket #23, recv #16, send #19, connect #4, WSAStartup #115, WSACleanup #116, WSAGetLastError #111, bind #2, closesocket #3, htons #9 → TCP-style client, internet presence/site launch), MSACM32 (acmDriverAdd/Remove), MSVFW32 (ICInstall/ICRemove — installs codecs/VFW), WINMM PlaySoundA, ole32 CoCreateInstance. Resources: VERSION + dialogs + BITMAP(COMMDLG_*); VERSION strings (UTF-16, .rsrc): `CompanyName = Origin Systems Inc.`, `FileDescription = Gateway: An Autoplay Program for Win95`, `OriginalFilename = gateway.exe`, `FileVersion 1,0,0,1 / ProductVersion 2,0,0,15`, Comments `More flexible version of the Origin Gateway program`, © 1996. → EA shipped repurposed **Origin Gateway** as the autorun shell; it installs DirectX/codecs (`DIRECTX5/`, `.TRE` per-language text: ENGLISH/FRENCH/GERMAN/SPANISH/DUTCH/SWEDISH/DANISH/GENERAL). CONFIRMED.
- `SETUP/AXDIST.EXE` (803,680) = InstallShield-era dist shell (not deeply parsed).

## 7. Verdict

| Claim | Tag | Evidence |
|---|---|---|
| Engine family = FIFA'97/98/99 line (EA "FIFA engine", same codebase as FIFA 99) | CONFIRMED | Watcom 2.18 + AUTO/DGROUP; `$FIFAVER$` module banners incl. `RENDER/LIBS_CMN/FE_EAC/FE_UK` (FIFA99 has same family); source-path leaks `win/ cmn/ font/ 3rash/ mix/` match FIFA99 lib names; `THRASH` renderer-ABI + `EACSND iSND*` API = the FIFA 97-99 stack; `VER.DAT $FIFAVER$` data-pkg checks. WC98 modules `IG/AI/CAMERA/MOTION/TOURNAMENT/FCDB` = sim pipeline. |
| Packing/protection | CONFIRMED not packed | overlay 0 on exe; AUTO entropy 6.61; no `.petite/.cms_t` sections. FIFA 99 CD exe WAS Petite+CMS; WC98 is not. |
| CD protection | CONFIRMED light | only `GetVolumeInformationA` + `[FILE] GetVolumeInformation failed (%d)` (0x16ba7c) + `CD Drive` registry key; no CMS/SecuROM/IOCTL strings. Exact label/serial algorithm unresolved (offline static can't confirm compare target; candidate labels §3.7). |
| Renderer | CONFIRMED | exe → DirectDrawCreate; per-GPU THRASH driver DLLs selected by registry `Thrash Driver` (`voodooa`=Glide2 x3dfx via glide2x.dll, `powervra`=PowerVR PCX, `d3da`=Direct3D2 COM HAL); 34-fn `_THRASH_*` ABI; `3D Card/Hardware Acceleration/Triple Buffer` registry keys; 3DSETUP card gate + DX5 installer. |
| Audio | CONFIRMED | `eacsnd.dll` runtime load with version check; 13 `iSND*` DirectSound3D exports; EACSND→`DSOUND!DirectSoundCreate`; EA audio lib `D:\audio\lib\win\sdirect.c`; bank/card formats .BNK/.PBK/.CRD/.SMP; no winmm wave/midi path in exe. |
| Multiplayer | CONFIRMED (structure) / HYPOTHESIS (transport) | own packet layer `cmn\net.c/packet.c/sserver.c`, `initnetservice/getnetplayers/killnetplayer`, `GAMELIST/MACHINE/FIFAPW` service strings, `DOS IPX compatible`; WSOCK32 datagram set (sendto/recvfrom/select, no connect). Full Internet (TCP) play: **not present** in FIFAWC (only AUTORUN connects); dial-up modem play fully implemented (TAPI/AT/modems.cfg). DirectPlay: absent. |
| Save/config layout | CONFIRMED | `SOFTWARE\EA SPORTS\World Cup 98` keys (3D Card, Thrash Driver, Triple Buffer, Hardware Acceleration, Language, CD Drive); `user\options.sav` (magic `AFIFAFIF`), `SLOT%.2d.SAV`, `Pdat%02d.sav`, `rply*.dat`, `network.dat`, `dial.dat`. |
| Build identity | CONFIRMED | FIFAWC.EXE built 1998-04-13 12:08 UTC; drivers Mar 1998 (3dfx 03-06, PVR 03-10, D3D 03-20); AUTORUN Sep 1997 (Origin Gateway 1996); no post-release patch marker. |
| Debug goldmine for port | CONFIRMED | THRASH DLLs (A and D) + EACSNDD carry appended **ELF-magic** (i386 ET_EXEC stub, 9 stubbed shdrs) Watcom debug blobs (20KB–328KB) with C source paths (`C:\3rash\d3d\*.c`, `D:\audio\lib\win\sdirect.c`), RTCS type tables (`signed char…wchar_t, THRASHTEXTURE`), func/type names; `SETUP/3DDATA.EXE` overlay uses **NB09** magic (Watcom symbol format, leaks `E:\proj\projects\3ddata\3ddata.c`). FIFAWC.EXE itself has NO overlay (0 bytes) and no FUNC: leaks. |

### Next-pass recommendations
1. Ghidra headless (`/usr/bin/ghidra analyzeHeadless` → new project `/mnt/1st4TB/WorldCup98/ghidra`) of FIFAWC.EXE; import D3DD ELF debug blob as auxiliary symbol source if loadable.
2. Trace `_THRASH_*` call sites in exe (name table 0x165e1c) and `iSND*` (0x16c450) for driver/audio contracts; parse `options.sav`/`SLOT*.SAV` layout.
3. Format RE priority: `.FSH` (textures/palettes), `.BIG` (archive), `.PBK/.BNK/.CRD` (EA audio), `.LST/.MAK` tooling on CD (FE.MAK documents art build).

*All section/import/export/string facts reproducible via `analysis/rawpe2.py` and `analysis/strings/`.*

---

Companion raw artifacts (import dumps, offset-annotated strings, rawpe2.py) live
in analysis/ (not distributed): FIFAWC.EXE strings contain copyrighted content.
