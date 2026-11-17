# `[multitext]` section format — FE/TEAMHIST.<lang> `.HIS` cells (WC98)

Evidence: shipped `.HIS` cell payloads (`FE/TEAMHIST.ENG`, `.FRE`, `.GER`, …), token strings
measured byte-exact in the blobs; consumer grammar from the FE multitext widget
(objecttype 35 panes `mtex/mttr/mtxl/mtxr`, screens feflow31/34/88 — handler decode
`analysis/pass20records/NOTES.md` §4). Port implementation:
`presentation/src/fe_screen.cpp::paintMultitext`; gated by `tests/unit/test_multitext.cpp`
(`multitext_grammar`) + FE gates `fe_records31/34/88`.

Legend: **[C]** = confirmed from cited blob content. **[P]** = pixel metrics not verifiable
(Windows capture lane dead).

## Container

A `.HIS` cell payload is a stream of text runs interleaved with `{token}` directives;
raw `\n` bytes act as `{n}`. **[C]** Tokens observed in shipped blobs:

| token | semantics |
|---|---|
| `{n}` | flush line, pen -> `{lmargin}` |
| `{t}` | advance to next tab stop |
| `{tabs lA lB …}` | widget-relative tab stop list |
| `{lmargin N}` | left indent (pen reset target) |
| `{centre}` / `{left}` | line alignment (centre = shift run to widget centre) |
| `{wrap}` / `{nowrap}` | word-wrap at widget right edge |
| `{font N}` | face id 0..10 (FeText face table, exe font table 0x57d2c0) |
| `{colour N}` | FE text palette: 0 = white, 1 = yellow (HILITE) |

Raw text between tokens is drawn at the pen; space-delimited words are the wrap units.
**[C]** all token spellings appear verbatim in `FE/TEAMHIST.ENG` general/trivia cells.

## Port model

Line runs accumulate at a widget-relative pen; `{n}` flushes (applying `{centre}` shift),
advancing the pen to `{lmargin}`; `{wrap}` breaks at word boundaries when the pen passes
the widget right edge; tab stops are widget-relative. **[P]** engine pixel metrics
(exact advance widths, line leading) ride the `FfnFont` metrics, not verified against
the Windows renderer.

`{colour 1}` maps to the 255,224,96 yellow used by the FE hilite painter; the exact
palette entry is [P].
