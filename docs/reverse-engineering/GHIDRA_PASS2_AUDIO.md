# Ghidra Pass 2 — audio decoder map (addresses CONFIRMED, structure from decompiles)

Scope: FIFAWC.EXE audio stack (EACSND.DLL/EACSNDD.DLL re-decompiled: they are DirectSound
*wrappers* only — 170/173 small functions, no codec loops; all sample decoding lives in
FIFAWC.EXE). Raw decompiles in `analysis/` (non-distributed, contain copyrighted bytes);
this doc records addresses + structure only.

## Export layer (EACSND.DLL, imagebase 0x400000)
`iSNDdirectstart` @0x402010, `iSNDdirectserve` @0x4022f4, `iSNDdirectstop` @0x402060,
`iSNDdirectvol/pos3d/rate` @0x402600/0x402694/0x402780 — thin wrappers dispatching into the
game; the playback state machine and codecs are in the exe (debug-blob symbols confirmed in
EACSNDD: `D:\audio\lib\win\sdirect.c`).

## FIFAWC.EXE audio functions (0x42B7xx–0x42C7xx cluster + iSND glue 0x431xxx)

| Function | Size | Role (decompile-confirmed) |
|---|---|---|
| `FUN_0042bb54` | 684 B | PT header parser: tags 0x82 channels / 0x83 codec / 0x85 samples / 0x88 hdrSize — the PT v2 reader (WC98 `PT\0\2`) |
| `FUN_0042be18` | 1,644 B | start path: format select (codec tag → decoder vector), buffer sizing, DirectSound submit |
| `FUN_0042c320` | 160 B | serve/refill: consumes decoded ring, calls decoder (loop @0x42c353) |
| `FUN_0042c450` | 1,208 B | **MicroTalk 10:1 CELP decoder** (structure below) |
| `FUN_0042c2e0`, `FUN_0042c528`, `FUN_0042c5a8` | 316/128/168 B | decoder helpers (state advance, synthesis stages) |
| `FUN_00405738` | 232 B | EA-XA ADPCM decoder: nibble→delta table @0x6a0470 (32-entry step index table, step ≤ 24576, 14-bit clamp) — matches our ported `EaXaDecoder` semantics |
| `FUN_00431494`/`FUN_004314d8` | 60/356 B | iSNDdirectserve entry + serve loop (decoded 10:1 blocks) |

## MicroTalk 10:1 structure (FUN_0042c450, decompiled)

- Frame = 4 subframes × 45 samples (180 samples = 22.5 ms @ 8 kHz).
- Per subframe: `bits(6)` codebook index + `bits(10)` gain/pitch refinement; combined
  code `(c6 << 6) + c10` indexes the excitation codebook at `.data:0x73e9c8`
  (short-code arrays, 4-short stride, base offset 200), scaled by the 10-bit gain via the
  gain table @0x73e820.
- Long-term (pitch) + short-term synthesis: 16-tap LPC; LSP/cos tables @0x73e160;
  coefficient buffers in the 0x1000-byte decoder state struct at +0x1E0 (480) and
  +0x280 (640); excitation ring at +0x54/+0xA0/+0xE8 (45-sample windows).
- Measured payload ratio confirms the layout: 16 bits per 45 samples (0.356 bits/sample
  coded + frame parameters ≈ 1.44 bits/sample; measured 1.34–1.57 across 401 .SMP).

## Port implications (open item, ranked #1 for audio)

Bit-exact WC98 speech needs this CELP path ported: (1) codebook/LSP/gain tables must be
loaded at runtime from the user's FIFAWC.EXE `.data` (offsets above) — tables are
copyrighted EA data and must not be committed to this repo; (2) 6+10-bit code stream reader
over the 144-bit block region, 180-sample frames; (3) 16-tap LPC + long-term synthesis.
Interim gate: `test_audio` pins the measured FIFA99-framing ratio 249/4,901 with layout
gates (160-bit blocks, 15-bit header, 72 self-terminating samples per block).

## Method

`DecompTargets.java` (scriptPath /tmp, not committed): decompile the 0x42B7xx–0x42C7xx
cluster + PT parser + serve path from the saved analysis (`ghidra/wc98` project).
