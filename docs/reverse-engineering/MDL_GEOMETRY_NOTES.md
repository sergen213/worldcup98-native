# FE GEOM/SKEL .MDL Model Format (M4 slice 3, 2026-11-02)

Parser: `formats/{include/wc98/formats/,src/}mdl.{hpp,cpp}`; viewer:
`tools/model-view`. The 12 FE `.MDL` files (`FE/*F.MDL`) pair with same-stem FSH art.
No FIFA 99 counterpart (research-inbox/F99_REUSABILITY_MATRIX.md §6) — layout below is
derived from WC98 bytes only; every field the parser trusts is validated by offset
identity, so a wrong hypothesis throws `FormatError` instead of rendering garbage.

## Confirmed layout (validated on all 12 files by construction: they parse + render)
```
0x00 "GEOM" | "SKEL"          (LPOLYBF.MDL is SKEL; rest GEOM)
0x04 u32 nameCount            texture-name table entries
0x08 u32 objectCount          role unverified (opaque per-object blocks skipped)
0x0c u32 = 20                 constant on all 12 files, meaning unknown
0x10 u32 = 0x14+12*nameCount  derived offset, not trusted
0x14 u32 = 0
0x18 name records, stride 12: [u32 colour(RGB bytes 0..2)][char[4] FSH tag]["0000"=none][u32 flag]
… per-object configuration blocks (opaque; skipped) …
draw-list group header, stride 36:
   [?next][f32 ?][0][faceCount][vertexCount]
   [facesOff == header+36][vertsOff == facesOff+40*faceCount]
   [vertsEnd == vertsOff+16*vertexCount][end]
faces, stride 40: [u32 textureIndex][3× u32 vertexIndex][6× f32 uv (per-corner, top-left origin)]
positions, stride 16: [f32 x][f32 y][f32 z][f32 w=0]
```
Group discovery = scan for the three self-consistent offsets + full face/UV/index
validation; no count guessing.

## Rendered evidence (view tool output, 2026-11-02)
| file | groups | names→FSH bitmaps | faces | bbox | visual verdict |
|---|---|---|---|---|---|
| `WCUPF.MDL` | 2 | 21 → 9 | 416 | 13.3×28.0×12.6 | FIFA World Cup trophy: globe in two hands, green malachite base bands — textures aligned (PNG) |
| `BRDXF.MDL` | 8 | 43 → 7 | 370 | 142.8×26.3×196.5 | stadium bowl around a green pitch; outer ring panels splay outward (presentation fly-in state? unverified) |

## Unknowns
- `flag` in name records (1 on most named textures); group header word 0 (`next?`) and f32 field;
  `objectCount` block semantics; per-object blocks' role (camera/transform?); Z-up vs Y-up
  (bbox says +Y up, height 28 ≈ trophy scale units).
- Vertex colours: INVENTORY.md §"GEOM/SKEL" mentions `b2 b2 b2 00` records — those live in the
  skipped object blocks, not in draw lists; shading there is texture-only (verified visually).

## Gate
`test_mdl` gates the geometry census + venue pool (synthetic GEOM checks, 12-model
census, `venueChecks` pool table — path pointers and the +0x0c callback column are
re-read from the shipped `FIFAWC.EXE` bytes at raw 0x17c4c8 and compared against
`venueTable`, so the mirror is exe-bound, not self-asserting). Visual proof PNGs
`/tmp/wc98port-out/mdl_{wcupf,brdxf}.png`.
Venue pool table + material loader **decoded (pass-105)**: the exe venue pool table at DGROUP VA 0x57dec8
(12 entries x stride 0x5c, slot order pdpf..lpolybf) carries `{pak, mdl, fsh}` path
triples + a callback slot (only lpolybf = scene-handler frame `FUN_0047b9a4`, case
dispatcher that reserves a record and loads pak entries) + a
once-guard handle; loader `FUN_00483418(EAX=idx)` loads the RefPack'd SHPI `.PAK`,
maps the MDL name records to per-venue material slots (type 0 packed RGB, type 1
white + case-insensitive pak-name lookup `FUN_004df695`), and applies vertex-colour
scales; venue index provenance = schedule entry record word +0xa high half
(`sar eax,0x10` at 0x4587e8). Mirror `formats::venueTable`/`parseVenuePak`, gate
`test_mdl venueChecks` (12/12 triples, SHPI parse, callback==1, tag coverage 12/12).
Fly-in **transform semantics decoded (pass-106)**: per-entry fields are the static
fly-in transform — f32 position `[+0x28..0x30]`, f32 euler rotation `[+0x38..0x40]`
(consumed by `FUN_0047d2f0` sin/cos matrix build, rad const `_DAT_00566324`), f32 scale
`[+0x4c]` (`FUN_0047cf1c`), i32 bbox offsets `[+0x50/+0x54]` (written by `FUN_00484404`,
read by the polygon rasterizer `FUN_00482d7c` bbox fit `DAT_0062f308..32c` + flat/textured
poly fill, material colour unpacked per gfx mode `DAT_0057fdd4` 15/16 bpp); mode enum
`[+0x20]` = 0 static, 1/2 animated global-float-delta lane (`DAT_0057e340` while
`DAT_0057e318==1`), >2 back-face silhouette collector `FUN_00483ae0`. The `+0x0c`
callback is **invoked**, not stored: `FUN_00484424` calls it when `idx < 0xc && arg > 9`,
`FUN_00483978` (unload) calls it before resetting `+0x10..0x18`. There is no keyframe
table — animation is the global accumulator + per-frame matrix recompose. Evidence
`analysis/pass106venue/t1.txt` (xrefs), `t2.txt` (disasm 0x484170+), `t3.txt` (18
renderer fns); `analysis/pass106venue/NOTES.md`.
Render lane CLOSED (pass-108): `presentation/{include/wc98/presentation/,src/}venue.{hpp,cpp}`
mirrors the loader material lane (FUN_00483418: packed-RGB or named-texture records, venue
.PAK first then companion `FE/<TAG>.FSH`, both tables loaded), the matrix compose
(`venueMatrix` = translate*euler(turns*2pi)*scale, `LAB_004840d0` stack order, euler multiply
order [H]), the accumulator (`VenueFrame::advance`, 0.003/0.03 doubles), and the
flat+textured rasterizer semantics through the GPU mesh pipeline. File uv is [0,1]
(lensf probe: range 0..1) — the exe extent multiply belongs to its texel-space software
sampler; the GPU sampler keeps normalized uv. App lane: `FeFlow::enableVenue(slot)` wires the
venue draws into the feflow35 board frame (`--fe-venue N`), gate `fe_venue_wc35` proves the
load; the venue is not yet visible on the board — the port composites it under the LAY back's
opaque full-screen collage and the exe draw order/viewport vs the LAY back (FUN_00482d7c vs
the scene blit) is [H], visibility OPEN; the exe venue index provenance is the schedule record
word +0xa high half (0x4587e8) and the port model has no fixture schedule feed, so the slot is
port-side [H]. Gates `render_demo_venue_lensf`/`render_demo_venue_brdxf`
(bitmap + frame-variance floors) prove the render lane with textured captures;
`analysis/pass108venue/NOTES.md`.
Dispatch lane (pass-109, corrected): the wrapper lane stores a raster stub into
the loaded record's `+0x28` (`mov [eax+0x28], stub` against the `FUN_0047a3e8`
record loads for `eras`/`csam`/`pucw`); frame runner `FUN_00444b28` invokes the
installed callbacks through `FUN_0046bee0` while walking `*DAT_00602518` head
->next. Callback gate (aligned disasm 0x444c40/0x444ca1): the `+0x28` call runs
when `+0x18` mask `0x08` (bit 3) is **CLEAR**; when `DAT_00601890 != 0` it also
requires `+0x19` bit 0. Frame-step head `(*DAT_005c6574)()` = `LAB_004190b4` is
a conditional `_THRASH_window@4(2)` call (`DAT_00598020` gate), frame clock
otherwise — not a blit/present. The frame closes with `_THRASH_pageflip@0`
(`[0x632a80]` at 0x444c8b, gated `FUN_0043ab8c != 3` / `FUN_00418fa0 != 0`)
**after** the callback loop, then `_THRASH_clearwindow@0` (0x444c91), so record
rasters land before the flip. Slot identities are the capstone-verified
re-pairing of the `FUN_004916b0` GetProcAddress binder (each store takes the
eax of its **preceding** getproc call — full 34-slot table in
`analysis/pass109venue/disasm_stub.txt`): `[0x632a78] = window@4` (stub target),
`[0x632a80] = pageflip@0`, `[0x632a6c] = clip@16`, `[0x632ab0] = clearwindow@0`,
`[0x632ac0] = about@0` (inithal driver-presence slot, GHIDRA_PASS3 §1.2), and
`writerect@20 = [0x632a54]` with **zero** indirect call sites. Venue visibility
stays OPEN [H]: the LAY back-composite record identity and its list position
versus the venue records are not statically recoverable.

Venue fog/camera lane (pass-117fog, 2026-11-20): the venue row-`+0x0c` lane gate is the
**fog-slot install** in the venue scene builder `FUN_0041a184` (`0x41a680`):
`if ([0x571504+venue<<5] == 0 || ([0x60cc06] & DAT_0057d2bc) != 0) DAT_00571710 = 0; else
alloc slot (FUN_00409af8/FUN_0040de38) + FUN_0041da7c(1,0,0) + FUN_0041a134`
(`FUN_0041a134` = `FUN_0040a150/00415a20/0040aabc`). Fog handle global `DAT_00571710`; fields
slot`+0/+4/+8` = fixed-point fog coefficients written per frame by handler `0x41be20`
(Watcom `eax`/`edx`/`ebx`): compares object coords against `(height[0x641314]+0x1f0)<<16` and
`(width[0x641310]+0x130)<<16` viewport halves and the `0x400000` (64.0) thresholds, writes
`0xb333` (0.7<<16) coefficients + sign flips (`neg`) keyed on sign of slot fields. Scene frame
handler `FUN_0041b4ac` consumes near/far slot`+0x1c/+0x20`: `DAT_0059bd84 < slot+0x1c ->
DAT_0057172c=1`, `slot+0x20 < DAT_0059bd84 -> DAT_00571730=1`, raster mode `DAT_005c65ac`
{0,1,2} from camera heights vs `slot+0x1c-64/slot+0x20+64` and `_DAT_00571724/0x571728`;
`if (DAT_00571710 != 0)` adds a `FUN_00415ef8` raster pass call. Fog **color palette**:
stride-3 RGB tables at `DAT_00571e48` (sky/fog blues: `80b8f1, 6c9fe9, 7189ac, a1a8b7, a1a8b7,
000a32, 000a32, 1f2843, 6a7280, 6a7280, 000000, 000000`), `DAT_00571e6c` (white×10 then
`000000`, `b52600`), `DAT_00571e90` (pitch greens `4c7231, 6d7833, 29511d×3, 44662c, 336c2e,
274d1c`); selected index `idx = mask ? 0xb : ([0x5c65b0] ? 10 : ([0x5c65b8] ? DAT_005716c0+5 :
DAT_005716c0))`, copied 9 bytes -> raster globals `DAT_005c65bc..0x5c65c4` (green@bc, white@bf,
blue@c2). `DAT_005716c0` is the venue-pair index written in the boot venue lane (`0x419f3a/
0x419f56`, from `FUN_00422634` after `ebx`), beside `[0x5c65b4] = venue row+0x08` and flags
`[0x5c65b0]/[0x5c65b8]` (`0x419fa7/0x41a002`). Per-frame handler `FUN_0041abac` sets object
type byte (`+8` 2->3/4) and installs the raster callback cells record`+0x24 = &DAT_00571b8c`
(name-matched) / `&DAT_00571c8c`; `FUN_0041da7c` `"%s"`-`_%s` name-match pass writes the match
tag to entry`+0x1c` (stride-0x30 records, types 1/2/6). Fly-around extras gated on row`+0x08`
allocate `DAT_005716f0..0x57170c` + bbox-scaled model slots `DAT_00571734..0x571740` with clip
bounds `DAT_00571754..0x571760 = ±4*[0x6412d0]/[0x6412cc]/2` — row+0x08 is 0 in the shipped
image, so that arm is image-dead [H]. Venue visibility conclusion unchanged (pass-109): fog
lane proves venue objects are drawn with per-venue fog only while `DAT_00571710 != 0`; the LAY
back order is still unrecovered. Notes `analysis/pass117fog/NOTES.md`.

In-game player/stadium geometry is a different container (INGAME .BIG dialects — not this format).
