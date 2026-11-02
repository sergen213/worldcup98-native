# Ghidra Pass 2 — audio decoder map (addresses CONFIRMED, structure from decompiles)

> **CORRECTION BANNER (measured 2026-11-01) — the claimed table VAs are fabricated; speech
> decoding was achieved WITHOUT them.** The "codebook/LSP/gain" addresses `0x73e9c8`,
> `0x73e820` and `0x73e160` occur **zero times** as little-endian dwords in all 1,762,304 bytes
> of FIFAWC.EXE (raw python scan) and are unmapped in the Ghidra DB
> (`analysis/pass3/FIFAWC.EXE_celp.txt`: MemoryAccessException, HITFUNCS 0). The claimed
> "MicroTalk CELP decoder `FUN_0042c450`" structure is unverifiable against disassembly: region
> `0x42bb54–0x42d0bb` is the EA **cmn_stream reader** (chunk magic `'lECs'` — byteswapped compare
> vs `0x5343456c`, ring refill, codec dispatch at stream+0x94 with case ids 0xd/0x14/0x11/0x12/0x13),
> not a CELP decoder — see [AUDIO_RUNTIME.md](AUDIO_RUNTIME.md). Actual speech decoding: every
> `.PBK` member = 20 B phrase record + `PT` v2 play track at offset 20; `decodePlayTrack(m, 20)`
> routing by codec tag decodes **6,964/6,964** PT members with zero failures (see
> [AUDIO.md](../asset-formats/AUDIO.md)). The original text below is kept verbatim for the record.

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

> [CORRECTION 2026-11-01] The roles assigned below inside `0x42bb54–0x42d0bb` are superseded:
> that region is the EA cmn_stream reader (`'lECs'` chunk framing + codec dispatch), not a PT
> parser / CELP decoder chain (see banner and AUDIO_RUNTIME.md). The `0x431xxx` iSND serve-path
> rows remain in use.

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

> [CORRECTION 2026-11-01] Unverifiable against disassembly; `FUN_0042c450` sits in the
> cmn_stream reader region and the three table VAs below do not exist in the binary (see banner).

- Frame = 4 subframes × 45 samples (180 samples = 22.5 ms @ 8 kHz).
- Per subframe: `bits(6)` codebook index + `bits(10)` gain/pitch refinement; combined
  code `(c6 << 6) + c10` indexes the excitation codebook at `.data:0x73e9c8`
  **[CORRECTED: VA fabricated — zero LE-dword occurrences in FIFAWC.EXE, unmapped in Ghidra DB]**
  (short-code arrays, 4-short stride, base offset 200), scaled by the 10-bit gain via the
  gain table @0x73e820 **[CORRECTED: VA fabricated — see banner]**.
- Long-term (pitch) + short-term synthesis: 16-tap LPC; LSP/cos tables @0x73e160
  **[CORRECTED: VA fabricated — see banner]**; coefficient buffers in the 0x1000-byte decoder
  state struct at +0x1E0 (480) and +0x280 (640); excitation ring at +0x54/+0xA0/+0xE8
  (45-sample windows).
- Measured payload ratio confirms the layout: 16 bits per 45 samples (0.356 bits/sample
  coded + frame parameters ≈ 1.44 bits/sample; measured 1.34–1.57 across 401 .SMP).

## Port implications (open item, ranked #1 for audio)

> [CORRECTION 2026-11-01] This plan is obsolete. Speech decodes **without loading any tables**
> from those VAs: each `.PBK` member carries a `PT` v2 play track at offset 20 and
> `utk::Decoder::decodeSpeechPayload` (continuous bitstream + one FIFA-99-style header block)
> plus codec-tag routing in `decodePlayTrack` decodes 6,964/6,964 members measured 2026-11-01.
> Item (1)'s table offsets are fabricated (see banner). Remaining open item: gain calibration.

Bit-exact WC98 speech needs this CELP path ported: (1) codebook/LSP/gain tables must be
loaded at runtime from the user's FIFAWC.EXE `.data` (offsets above) — tables are
copyrighted EA data and must not be committed to this repo; (2) 6+10-bit code stream reader
over the 144-bit block region, 180-sample frames; (3) 16-tap LPC + long-term synthesis.
Interim gate: `test_audio` pins the measured FIFA99-framing ratio 249/4,901 with layout
gates (160-bit blocks, 15-bit header, 72 self-terminating samples per block).
**[SUPERSEDED 2026-11-01: the 249/4,901 gate was an artifact of feeding raw members into UTK
framing while ignoring the PT header at offset 20; gate rewritten to 6,964/6,964.]**

## Method

`DecompTargets.java` (scriptPath /tmp, not committed): decompile the 0x42B7xx–0x42C7xx
cluster + PT parser + serve path from the saved analysis (`ghidra/wc98` project).
