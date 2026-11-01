# Renderer architecture (Vulkan-first)

Design derived from confirmed evidence — `docs/reverse-engineering/EXECUTABLE.md`
(THRASH ABI), inventory (asset formats), FIFA 99 native renderer
experience (validated against WC98 assets: skinning, stadiums, pitch
lines, cameras, HUD — `docs/asset-formats/REUSABILITY.md`).

## The original renderer as reference semantics

The exe talks to GPU back-ends through the **THRASH driver ABI**
(34 `GetProcAddress` names: init / setvideomode / settexture / talloc /
drawtri ...): an immediate-mode, texture-atlas + triangle-list
renderer. The original pipeline (software path, reference for visual
semantics):

- Native surface: **640x480 16-bit**, palette-based FSH art (565 +
  palettes; 0x2D/0x29 record variants + global fallback for WC98).
- World: pitch (line textures + geometry from stadium `.BIG`s),
  stadium shells, crowd (CRD zones for audio, art in FSH), adboards,
  goals/nets, players (PCMD meshes + MO skeletal anim, kit textures
  `GK%/COM_/HHIG/HMED_x0.FSH`, `PK%.3d%c` per-team kits), ball,
  shadows (blob/`SHADOW.PC`), 8 named cameras, HUD from FE FSH art +
  FFN fonts.
- Frame pacing: software/D3D ~85 fps cap, Glide 63 (reference caps
  preserved in the `original` preset; uncapped is an option that must
  never speed up simulation).

## Layering (renderer contains no game logic)

```
scene model (from simulation+animation) → renderer/submit
renderer/         Vulkan 1.3 core (RADV NAVI31 dev GPU), no fixed
                  pipeline; GL backend only if needed as fallback
  resources/      FSH/GIMX → VK images (decode at load; DXT/BC only
                  via conversion pipeline), FFN → font atlas, SQF
                  (EULZ squish) when implemented
  geometry/       PCMD meshes + GIMX skinning (GPU skin parity with
                  FIFA 99 native: hdrSize 116, 14-quat key triplets)
  stadium/        pitch lines, stands, boards, goals/nets
  hud/            FE art + FFN text; 640x480 logical canvas,
                  integer-scaled + letterboxed for original preset
  video/          .DCT EA-TREM decoder → video frames (pending RE)
presentation/     FE screens (FE-PACK LAY layouts + art; typed layer
                  RE pending), 29/29 LAY grammar already parses
platform/         SDL3 window (Wayland), swapchain, vsync modes
```

## Presets

- `original`: 640x480 pixel-accurate target (nearest upscale),
  palette-accurate FSH decode, no filtering changes, 85/63 fps cap
  option, camera set = original 8.
- `enhanced` (opt-in, later): high-res render, AF/AA, widescreen
  (FOV-preserving), filtered textures, shadow upgrade. All enhancements
  strictly rendering-side; tick/sim timing frozen (parity tests).

## Validation

Offscreen render tests + golden-image comparisons vs reference
captures from the original game (Wine research capture rig), per
project test rules; visual regression harness pattern proven in
fifa99-native (`docs/VISUAL_REGRESSION.md` equivalent).
