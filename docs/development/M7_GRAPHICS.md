# M7 graphics: original preset fidelity + widescreen support audit (2026-11-03)

Inputs: `docs/reverse-engineering/GHIDRA_PASS3_RENDERING.md` (THRASH ABI, registry, scene modes),
`renderer/include/wc98/renderer/renderer.hpp` PresentMode/Aspect/Layout (implemented), launcher
Graphics page (`wc98-launcher --page graphics`), camera metrics in `presentation/src/camera.cpp`.

## Original preset — what the 1998 game actually fixed, and where it lands natively
| Original mechanism | Evidence | Native mapping | Status |
|---|---|---|---|
| Resolution **640×480 @16 bit**, no mode-setting API in THRASH ABI (34 exports never set a display mode; `!thrash\%s.dll` loads into the existing DDraw surface) | PASS3 §THRASH, GHIDRA_PASS1 registry section | `PresentMode::Original`: scene target 640×480, nearest-neighbour upscale, centred 4:3 (`computeLayout`, renderer.cpp L71-74) | Implemented; default = Original (authentic first run) |
| Registry `HKLM\SOFTWARE\EA Sports\World Cup 98` values: `3D Card`, `Thrash Driver`, `Triple Buffer`, `Hardware Acceleration`, `Language`, `CD_Drive` | STATUS.md Confirmed §Registry; exe readers 0x438e38 (Language/CD_Drive) | Language → options.sav lang byte + launcher selector; CD_Drive → tree path (launcher Game files); driver/3D Card = backend choice, meaningless under native Vulkan (single path); Triple Buffer/HW Accel ≈ present-mode quality knobs → `Aspect`+`msaa` family in settings (`[graphics]` keys) | Mapped; no registry reads needed natively |
| Software renderer fallback `-no3dx` flag (in-exe rasterizer when no valid driver string) | PASS3 item list ("launcher flag → registry value removal") | Native always GPU; authentic look preserved by Original mode. No port of the software rasterizer (YAGNI) | Documented decision |
| 3dfx `SST_*` env/config knobs | PASS3 §3dfx | Not applicable (Glide path not ported) | N/A |
| Scene-mode byte `DAT_00640fb4` (demo/scene palettes back16..back23) | PASS3 §palette selector | FE scene backgrounds slice (later; not resolution-related) | Open, tracked in PASS3 doc |

## Widescreen support audit — measured
- Renderer: full plumbing exists — `RenderSettings{mode, aspect, msaa}` → `Layout{sceneW/H, ui, dest}`;
  Enhanced+Widescreen renders the scene at output size with the UI kept in a centred 4:3 virtual
  640×480 area (`l.ui = fit4x3(out)`), so original art never stretches. Gate: `render_demo_*` PNGs all
  run through `computeLayout`.
- Camera projection is authored 4:3: `verticalFovDeg4x3(fovHalf16) = 2·atan(tan(half)·3/4)` — vFOV is
  derived from the exe's horizontal FOU metric assuming 4:3. Under `Aspect::Widescreen` this keeps the
  full horizontal field (Hor+), vertical grows; goal/camera framing constants (`kPitchZones`,
  `Proj_screenHeight`) stay valid because they are world-space, not pixel-space. No FOV code change
  needed for Hor+; a "vertical-locked" option is NOT provided and would crop the original horizontal
  view (rejected: hides gameplay the 1998 game showed).
- Match demo renders offscreen at exactly 640×480 (`app/match_demo.cpp` L115) = Original-mode geometry;
  frame gate asserts it. FSH/LAY art is pixel-authored for 640×480 → Original path uses them verbatim;
  Enhanced path scales the UI blit (SpriteBatch viewport), verified by `render_demo_lay` min-shapes gate.
- Launcher exposes all three axes: presentation mode, aspect, MSAA (`--screenshot-pages` proof page 03).

## Verdict
Original-preset authenticity = default and proven (640×480 nearest pillarbox); widescreen is supported
end-to-end (renderer layout + Hor+ camera math + launcher UI) with no known geometry blockers.
Remaining M7 graphics work: HUD port (presentation gap list in MILESTONE6.md) will consume `Layout::ui`
and must be authored at the 640×480 virtual size — recorded there, not a preset blocker.
