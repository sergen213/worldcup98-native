# fepack lazy loader (`fepack\%s.all` -> FE/PACK/<scene>.pak)

Decoded 2026-11-07 (pass 13). Full dossier: `analysis/pass13/FEPACK_LOADER.md`.

The original engine opens per-scene window-object packs lazily (`FUN_0047a12c`):
if a scene slot has no pack handle, it formats `"fepack\%s.all"` from the scene name
and lets `FUN_004798dc` redirect it: the `"fe"`/`"Fe"`/`"FEE"` fragment is swapped
against `\fe\pack\%s` and the trailing extension bytes rewritten to `pak`, producing
`FE\PACK\<scene>.pak`. **No `.all` file exists anywhere in the shipped tree** — it is a
virtual path, not an archive format.

The per-scene PAK files are plain SHPI archives (same container as FSH; RefPack-compressed
payloads allowed), one entry tag per shape: e.g. `FE/PACK/FEFLOW15/CHK1.PAK` -> tag `chk1`,
`FE/PACK/GLOBAL/CHEATART.PAK` -> tag `pict`. The pack FILE STEM is the widget's `fname`
key for code-opened objects (`test.fsh,1,3` animseq sequences, checkbox rows).

Port resolution: `ScreenArt::indexTree` indexes every `.fsh`/`.pak` under the game root by
lowercased stem, so all lazy-loader targets resolve today with no loader component.
One alias is required: layouts referencing `..\global\button.fsh` (which exists nowhere)
resolve to `FE/INGAME/INGAME.FSH` tags (`btnn/btnd/btn2`) via the explicit
`button -> ingame` stem alias in `ScreenArt::file`.
