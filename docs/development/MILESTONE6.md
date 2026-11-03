# Milestone 6 — exhibition-match vertical slice

Goal: one real match, start to rendered frame. The ported simulation (`wc98_simulation`) drives a
seeded exhibition; `presentation::MatchScene` turns each tick's `MatchSnapshot` into the original
World Cup 98 match art on the Vulkan renderer; `wc98-match-demo` captures three PNGs headless and
CTest gate `match_demo_frames` proves it on any machine holding the CD tree.

All asset paths below are relative to `$WC98_TREE/` (the extracted original tree; never committed).

## Slice contract

| layer | piece | state |
|-------|-------|-------|
| presentation | `MatchScene::build()` — statics (grass/runoff/stadium/goals/flags) + per-frame players/ball/shadows/lines, LOD budgets, texture cache | done (`presentation/src/match_scene.cpp`, ~660 L) |
| presentation | player art: skinned LOD chain `INGAME/PLAYER/MODELS/{SHI_10,HIG_30,MED_70,LOW_90}.PC`, one `PlayerRig` per model, palette slots canonical via `kNames` order | done |
| presentation | animation: sim-owned clips (`COMMON/AISEQS.TBI` + `COMMON/ANIM.MO`) and rig (`HIG_30.PC`) through `sim::loadMatchAnimData`; pose from instance `keyA + frac`, mirror flag, quantized keys in Original mode | done |
| presentation | kits: per-player atlas from FCDB appearance (`kits::KitTextures`), face composites (`hifa` layered), keeper `GK##_<lod>.FSH`, referee `PK999B` | done (missing tags -> magenta checker, see gaps) |
| presentation | pitch: `TURFS0P<col>.PAK` member `TT01` tiled grass + darker runoff at stand dims; live size from snapshot (`w.pitch.width/length`) | done |
| presentation | line markings: white alpha quads y=0.01 — boundary, halfway, centre circle, penalty/goal areas, spot ticks; constants from sim `pitch.hpp` (0x65F324.. fields) | done (arcs/spots simplified, see gaps) |
| presentation | ball: camera-facing billboard, 12 roll frames `MISC16.FSH bl00..bl11`, frame = f(rollPhase, heading−cameraYaw) via the f99 octant table; ground shadow `MISC8 mshd` | done |
| presentation | shadows: `SHADOW.PC` flat skinned mesh sheared toward a fixed light (`shadowMatrix` 0x4147F0 port, k=2 clamp kept), alpha-150 dark tint, distance hysteresis | done |
| presentation | goals/nets/flags: `MODELS2.PAK net.pc/net_b/_l/_r.pc/cflag.pc` + `MISC8 post/net2/net3/flag/pole`, scaled to goalHeight 0x98 (152 units) at z = ±length/2 | done (uniform scale, see gaps) |
| presentation | stadium: `INGAME/STADIUM/<BASE>___<D|N>.BIG` four bowl sides + `stadium0.fsh` atlas, kStadiumScale 415/4, SOFTIMAGE x-mirror convention | partial (base-name map untraced; demo picks STDE) |
| app | `wc98-match-demo <prefix> [--tree DIR]`: SimRunner seed 0x2E7A, teams FCDB index {0, 26} (ids 26/67 — both ship PK###H+A kits, measured), keyboard device 1 bound to the kickoff-taker side via `Input_assignController`; captures `_kickoff.png` t=1 s, `_midplay.png` t=60 s (open play), `_sprint.png` t=120 s with W pad bit 0x100 held from t≈58 s; prints per-tick speed/possession/phase telemetry | done, frames verified on AMD RX 7900 XTX |
| tests | gate `match_demo_frames`, SKIP_RETURN_CODE 2 when tree/assets absent; exit 3 on Vulkan validation messages | registered |

## Measured evidence (probes, all via /tmp scripts against $WC98_TREE)

- FCDB team table @0x204 in `COMMON/FCDB.DBI`: count 56, record size 68; index 0 → id 26,
  index 26 → id 67 (id = LE word0 & 0x7FF).
- `INGAME/PLAYER/MODELS` ships loose per-family files: SHI_10/30/70/90, HIG_30/70/90, MED_*, LOW_*
  (+SHADOW.PC flat mesh, material type 0 "Z0"); node counts 28 (SHI_10/HIG_30), 27 (MED_70), 16 (LOW_90).
- `MISC8.FSH` tags measured: rpit 180x120, ball 64x64, mshd 32x32, lshd 16x32, post 16x16, net2 16x16,
  net3 32x16, flag 16x8, pole 5x5. `MISC16.FSH`: bl00..bl11 32x32 (type 0xED).
- `TURFS0P<col>.PAK` members TTT1/TTT2/TT01/TT02/DIRT, 64x64 type-0x7B; member payload inside the
  PAK is plain SHPI. `MODELS2.PAK` is a RefPack'd BIGF with net.pc/net_b/_l/_r.pc/cflag.pc/innet*.pc.
- `STDE___D.BIG` members: stad_n/s/e/w.pc + stadium0.fsh (SHPI plain inside BIGF).
- Kit atlas quirk: model materials sample `hjbx`, atlases ship `hjbk`; `kits.cpp` mirrors the tag so
  the normal path resolves. GK sets on disk are only GK00..GK05 x {30,70,90}.

## Hypotheses (explicit; none load-bearing for the gate)

1. **LOD chain** SHI>HIG>MED>LOW at suffixes 10/30/70/90 mirrors f99's slo/low/med/shi families
   (WC98 has no SLO file); thresholds/budgets are the f99 `Lod_init` 0x4256D0 values mirrored.
2. **Stadium base table** kStadiumBases = observed directory bases; FCDB team byte → base mapping is
   untraced in FIFAWC.EXE (demo passes stadium 0 → STDE day).
3. **Centre circle R = 480 units** (10 yd by the laws); f99's line table drew 384 — WC98 table untraced.
4. **Keeper kit selection** `teamId % 6` over the six on-disk GK sets; original selection table untraced.
5. **Home/away atlas H/A** per team (f99 convention, kits.hpp note); the 1998 selection code is untraced.
6. **attackDir mapping** `TeamState::side == 0 → +z` (field comment "attack direction"; sign from sim tests).
7. **Ball radius 5 units / sprite sizes 9-11**: f99 DAT_006691d4 sibling value; WC98 field read via the
   ported `ball.hpp` struct at the same exe address — pending a direct WC98 trace.
8. **Sun/floodlight position** (10800, 23040, 8000): f99 Shadow_init day value / 4; WC98 light table untraced.
9. kGrassTile 96 units and shadow alpha 150: eye-balled from test renders (documented knobs).

## Known gaps

- **HUD not ported**: scoreboard, radar, shirt-number labels, controller markers — `HudView` fields
  are filled by the demo but nothing draws them. Next slice.
- Magenta checker placeholders for material tags no atlas carries (logged once per key:tag; count is
  a health metric). `hifa` face composite exists so outfield faces bind; bald/edge cases unverified.
- Goal nets scale uniformly to crossbar height; goal-mouth width (`goalWidth` 0x1c8) not fitted per
  post model — side nets may overhang by eye. Penalty spots are ticks, corner arcs omitted.
- Stadium bowl: fixed kStadiumScale 415/4; if the scaled bowl intersects a small pitch, shrink/skip
  is a follow-up (visual check pending on real frames).
- Fog/sky constants only (no RPARMS.DAT equivalent in WC98); night lighting = darker clear colour.
- No snapshot interpolation in the demo (alpha=1 single snapshot per capture; `interpolate()` unit-tested).
- Controller 0 binds the kickoff-taker side (team 1) as a keyboard pad; second controller / CPU difficulty plumbing untouched.
- **Kickoff/restart strike gap**: the restart machinery runs (`CutScene_play` is a no-script prologue at rules.cpp 2334; seq3/seq4 line-up completes) but no pad word reaches `SetPiece_begin` (input.cpp 1184), so phase 0 persists. The demo fires the exe's own "restart taken" event (`Match_dispatchEvent(w, 0xb)` → Match_onEvent case 11, rules.cpp 965) after a 5 s timeout; play then runs live (ball advances, possession observed). With an all-zero pad the AI holder stalls play again at ~t≈130-150 s for seed 0x2E7A — captures sit inside the live window. Root-causing set-piece input + human-side support is a simulation-layer slice.

## Next slices

1. HUD compositor over the match frame (scoreboard LAY art + radar from HudView).
2. Stadium byte → base trace in FIFAWC.EXE (string xref of `STDE`/`_D.BIG` loaders) and bowl-fit test.
3. Ball/net fit: goalWidth-aware post placement; ball radius direct WC98 exe confirmation.
4. Present-mode parity: real-time loop with audio (commentary index work feeds it), windowed SDL path.
