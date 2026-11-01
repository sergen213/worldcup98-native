# Third-Party Notices

## Ported first-party code

The `formats/` parsers, `tools/` research utilities and most of `tests/` are
ported from **fifa99-native**, this project author's own analysis codebase for
the sibling title (same author, sergen213). Every ported file carries a
"Derived from fifa99-native (same author), ported for World Cup 98" header.
The port is MIT-licensed under this repository's `LICENSE`; the WC98-specific
schema deltas implemented on top of it are documented in
`research-inbox/F99_REUSABILITY_MATRIX.md` and cited in code comments.

## fifa98edit (MIT)

The 1998 FCDB record layouts — the 68-byte / 17-word team record (kit styles
and colours, bank, stadium, tactics, strategy, nation, roster size), the
1998 player record (role, look, transfer price, jersey) and the 17-skill
nibble list with its 39..99 step-4 scale — were read from the community
World Cup 98 / FIFA Roadmap editor:

```
fifa98edit — https://github.com/ma-akad/fifa98edit
MIT License, Copyright (c) 2020 ma-akad
```

Used as reference (cloned under `analysis/fifa98edit`); no code from it is
shipped in this repository — the parser is a C++ reimplementation from that
layout description.

## Build dependencies (system packages, not bundled)

- **libpng** (zlib license) — PNG export in `formats/src/png.cpp`.
- **OpenSSL** (Apache-2.0) — SHA-256 in `tools/inventory`.

## Community / documentation sources

Format knowledge derived from public community sources is cited in
`docs/research/`, `docs/asset-formats/` and `research-inbox/`;
reimplementations are written from this project's own analysis of the
original files.

## Game data

The original World Cup 98 game data (executables, assets, music, commentary)
is copyright Electronic Arts, is NOT distributed here, is NOT committed to
this repository (no game bytes, no hexdumps of game files), and is supplied
by the user from a legally obtained copy. Data-driven tests read a local tree
via the `WC98_TREE` environment variable and skip (CTest code 77) when it is
absent.
