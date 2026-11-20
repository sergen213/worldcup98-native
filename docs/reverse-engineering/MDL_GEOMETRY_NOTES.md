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
renderer fns); `analysis/pass106venue/NOTES.md`. Port render lane still open: nothing in app/
consumes `venueTable` (FE screens are still 2D LAY composites), the decoded semantics
above are its spec, nothing invented.
In-game player/stadium geometry is a different container (INGAME .BIG dialects — not this format).
