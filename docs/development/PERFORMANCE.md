# Performance measurements (goal.txt §11)

Measured 2026-11-06 on the dev box: CachyOS, AMD Ryzen (X3D class), Radeon RX 7900 XTX, RADV.
Method: `wc98-native --live-match-smoke 300` (release build, real game tree) driven from a
Python harness using `subprocess` + `getrusage(RUSAGE_CHILDREN)`; gate asserts run headfully
under Xvfb on the same GPU.

## Interactive live match (`--live-match-smoke 300`)

| Metric | Value | Evidence |
|---|---|---|
| Presentation rate | ~50 fps (engine cadence: 300 frames presented while sim clock advanced 6 s) | gate line `clock 0 -> 6`, `ran 300 frames` |
| Simulation tick rate | 50 Hz by design (1/50 s engine cadence, camera/sim paced per frame) | `app/livematch.cpp` loop; scene cite FUN_004479d0 cadence |
| Wall time (300-frame smoke incl. startup) | 20.1 s (~14 s asset load + shader pipeline, ~6 s live loop at real-time pace) | harness `wall 20.14s` |
| CPU usage | ~2.5 % of one core during the live loop (utime+stime 0.5 s / 20 s wall; loop is sleep-paced) | harness rusage |
| Peak RSS | 167 MB | `ru_maxrss` |
| Input latency | ≤ one frame period (~20 ms): pad/keyboard word polled once per presented frame, applied on the next sim tick | live loop structure |

## Frame pacing reference (original engine)

- Software/D3D mode cap ~85 fps; Glide mode cap 63 fps (`docs/architecture/RENDERER.md`,
  `docs/STATUS.md`); the port's authentic preset keeps the original presentation, no cap emulation
  in the live loop (enhanced path is uncapped vsync'd by SDL present).

## Not measured / limitations

- GPU utilization: not sampled (no amdgpu_top/ppu-tools gate on CI box; `ponytail`: add when a
  VR/CI capture job wants it - read sysfs `gpu_busy_percent` around the smoke run).
- Audio synchronization drift: commentary pump runs every 2 render frames against the 50 Hz sim
  clock (gate asserts phrase start at exact tick, `test_commentary`); end-to-end ALSA/PipeWire
  buffer latency not instrumented.
