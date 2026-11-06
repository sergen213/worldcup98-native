# VIDEO module — .DCT / mTCD decode (Ghidra pass 8, 2026-11-06)

Full format documentation: `docs/asset-formats/VIDEO.md`. This note records
the executable-side evidence trail. Raw dumps:
`analysis/pass8/dct_decomp{1..36}.txt`, `dct_headers.txt`, `dct_xref*.txt`,
`dct_video_range.txt`, `dct_d3da_dec1.txt`, `dct_proto.txt`.

## Module surface (CONFIRMED)

- The VIDEO module functions sit in the 0x490c00–0x491600 range: frame
  presenter `FUN_00490c48`, stream/framing walker `FUN_00490e14`, decode
  driver `FUN_00490ee8` (size ~205), block decoder `FUN_0049110c`
  (size ~826 — the SRE VLC/IDCT worker), and tail consumer `FUN_00491514`.
  Decompiled bodies in `dct_decomp{2,4,7,10..12,14,22,24,32}.txt`.
- D3DA.dll carries a parallel decoder lane (`FUN_600039f0`,
  `dct_d3da_dec1.txt`) + texture feed `dct_d3da_settexture.txt` — the
  DirectDraw blit path that fed `_THRASH settexture` strings.
- Container facts (SCHl @0, mTCD chunks, LE sizes) are shared with the audio
  SCHl parser (`formats/src/schl.cpp`); `dct_headers.txt` prints the first
  four mTCD records of SEQ_OPEN/INT_FIFA/FL0_PARI showing the fixed
  `40 01 f0 00` = 320x240 LE pair at +12, BE u16 delay `0x0038` at +18.

## Codec semantics (byte-verified; CONFIRMED)

The port's decoder was pinned by full-stream byte-level verification rather
than a complete decompile of the Watcom worker (register-heavy IDCT loop):

- 2-bit cell header / 6 blocks per cell with 1-bit present flags, luma 4x4
  quadrants + V/U chroma 4x4 (`b=4` V, `b=5` U).
- EA scan = MPEG-1 direct zigzag with the (10,17) pair swapped; EOB word
  `10` resolves before longer prefix codes; escape `000001`+run(5)+level(8
  signed); coefficients dequantized `level*matrix[i]/8` floor.
- Float reference IDCT + 128 bias; padding tail stops the frame.

Verification: Python oracle decoded all 1,017 INT_FIFA frames (982 with data)
with sample-for-sample agreement against the C++ port (frame 0 `Y[0][0]=77`);
renders at frames 0/400 of INT_FIFA and SEQ_OPEN show coherent video
(`analysis/pass8/dct_proto.txt` describes the oracle walk).

## Hypothesis

- Motion field (9 bits after a `11` cell header) is skipped in the port; the
  exe's inter reconstruction (`FUN_0049110c` region, previous-frame blend) is
  not implemented. Intra-only output is complete for intro/presentation
  sequences; flyover P-cells may drift without MC.
- The +18 BE-u16 delay (56 ms = 17.9 fps) is inferred from value agreement
  across all sampled records, not from a playback-code constant.
