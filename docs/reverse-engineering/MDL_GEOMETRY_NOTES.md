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
No ctest gate yet (needs tree + GPU); visual proof PNGs `/tmp/wc98port-out/mdl_{wcupf,brdxf}.png`.
Next: venue fly-in animation data (`FE/*F` FSH frame series), in-game player/stadium geometry is a
different container (INGAME .BIG dialects — not this format).
