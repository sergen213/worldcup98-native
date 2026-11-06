# VIDEO — EA `.DCT` movies (`VIDEO/*.DCT`)

Status: **CONFIRMED** container + intra codec (2026-11-06, Ghidra pass 8 +
byte-level decode verification). Evidence: `analysis/pass8/dct_decomp*.txt`
(VIDEO module functions `FUN_00490c48`, `FUN_00490e14`, `FUN_00490ee8`,
`FUN_0049110c`, `FUN_00491514`; D3DA.dll decoder lane `FUN_600039f0`),
`analysis/pass8/dct_headers.txt` (mTCD table walk of 4 files),
`dct_proto.txt`/`dct_video_range.txt`. Codec:
`formats/include/wc98/formats/dct.hpp` + `formats/src/dct.cpp`
(+ `dct_tables.inc`, generated from FFmpeg `libavcodec/mpeg12data.c`).
Gates: `tests/unit/test_dct.cpp` (all 29 tree videos, 9,915 frames),
`video_play` (`wc98-video-play --gate VIDEO/INT_FIFA.DCT` →
`VIDEO frames=1017 size=320x240 fps=17.9`).

## Container (CONFIRMED)

The file starts with the same `SCHl` container as the audio streams
(`docs/asset-formats/AUDIO.md`): `"SCHl"` BE magic + LE u32 header size
(0x28 here). The body is a sequence of chunks: BE fourcc + LE u32
self-size. For videos every chunk is **`mTCD`** (motion-TCD, one frame per
chunk); 29 files carry 47–1017 frames each (`INT_FIFA.DCT` = 1,017).

mTCD record layout (CONFIRMED, `dct_headers.txt`): 16-byte header then the
bitstream payload. Header fields observed:

| Offset | Field | Value in every tree file |
| --- | --- | --- |
| +0x00 | u32 | 0 (frame flags) |
| +00x04 | `40 01 f0 00` | width/height as LE u16 pairs: 0x0140=320, 0x00f0=240 at +12/+14 |
| +0x12 (18) | BE u16 | presentation delay centis: `0x0038` = 56 ms → 17.9 fps (**Hypothesis**: cadence field; value identical across all sampled frames of INT_*/SEQ_/FL* files, matches observed playback smoothness) |
| +0x14 (20) | u16 | per-frame sequence/flags (increments through the stream) |

Payload = bytes from record offset 16. Each 16-bit word is byte-swapped
(SRE layout); the bitstream is MSB-first inside each swapped word, and a
64-bit preamble is skipped before the macroblock data.

## Codec: EA SRE (PlayStation MDEC family) — CONFIRMED

Per frame, after the 64-bit preamble, an `width/8 × height/8` grid of
**cells** follows in raster order. Each cell:

1. **2-bit cell header**. `0b11` = inter cell: a 9-bit motion-field follows
   and is skipped (the port keeps the previous luma — no motion
   compensation; see Hypothesis below). Other values = intra cell.
2. Six blocks, each prefixed by one flag bit (`0` = block absent, keep
   plane value):
   - blocks 0–3: **luma 4×4 sub-blocks**, placed at quadrant
     `ox=(b%2)*4`, `oy=(b//2)*4` of the cell's 8×8 area;
   - block 4 = **V chroma**, block 5 = **U chroma**, each one 4×4 written
     to the 4:2:0 chroma plane at `[cell_y*4][cell_x*4]` (160×120 for
     320×240). Placement byte-verified on INT_FIFA.DCT frame renders.

Coefficient chain per block (MPEG-1 style, tables in `dct_tables.inc`):

- **EOB** = codeword `10`, checked *before* longer codes with prefix `10`
  (EA table is non-canonical; the exe decoder resolves longest-match).
- **Escape** = `000001` + 5-bit run + 8-bit signed level (MDEC-style),
  consumed as 6+5+8 bits.
- Remaining VLC codes give (run, signed level) from the MPEG-1
  `ff_mpeg1_vlc_table`.
- Coefficients land at scan position `i` with the **EA scan table** =
  MPEG-1 direct zigzag with the `(10,17)` pair swapped, dequantized as
  `level * matrix[i] / 8` (floor; raster matrix
  `{8,16,19,26,37,56,60,63,…}`). Run overflow consumes the code and drops
  the coefficient. Stream padding/unmapped tail stops decoding for that
  frame; the partial plane is kept.

Plane reconstruction: float reference IDCT (JPEG spec scaling), luma and
chroma biased by +128, clipped to 0..255; absent blocks leave the plane
value (luma initializes to 0 = black). RGB conversion at playback uses
BT.601-style `Y + 1.402(V-128) / Y - 0.344(U-128) - 0.714(V-128)` from the
prototype renders (`dct_proto.txt`).

## Verification (CONFIRMED, generalizes across the tree)

`test_dct` decodes **all** frames of all 29 `.DCT` files: INT_FIFA.DCT
yields 1,017 frames with deterministic sample checksums over two runs and
non-zero luma in >half the frames (Python oracle cross-check: 982/1017
frames carry data; frame-0 samples match exactly, e.g. `Y[0][0]=77`).

## Playback

`wc98-native --video-play FILE.DCT` (`app/video.cpp`) uploads each decoded
frame as an RGBA texture through the Vulkan renderer and presents at the
BE-u16 record delay (56 ms). The gate mode (`wc98-video-play --gate`) runs
headless and prints the frame stat line.

## Open / Hypothesis

- Inter cells: the 9-bit motion field is skipped, not applied (no MC). The
  exe's inter path (`FUN_0049110c` region) reconstructs from the previous
  frame; the port renders intra-only approximation, which is visually
  complete for I-frame-heavy intro streams. **Hypothesis** for P-cell
  fidelity in flyovers.
- The cadence field at +18 (56 ms) is not directly tied to a playback-code
  constant in the decomp dumps; **Hypothesis**-tagged by value agreement.
