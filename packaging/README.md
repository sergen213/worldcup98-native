# Packaging

- `linux/deploy.sh` — **release path**: builds one AppImage with BOTH `wc98-native` and
  `wc98-launcher` (Qt plugins + qt.conf bundled by linuxdeploy; Vulkan/GL/X/Wayland stay host-side).
  Local: `cmake --preset release && cmake --build --target wc98-native wc98-launcher && bash packaging/linux/deploy.sh build/release dist`.
- `linux/build-appimage.sh` — game-only fallback (custom mksquashfs + pinned appimagetool); no Qt.
  Use when the launcher is not built. `-b <build> -o <out> [-v VERSION]`.
- CI `.github/workflows/build.yml` job `package` runs deploy.sh, then smokes: game via the
  extracted binary (`squashfs-root/usr/bin/wc98-native --smoke 3` under Xvfb) and launcher via
  AppRun's default Exec (`AppImage --self-test`, offscreen). linuxdeploy's AppRun always execs
  the desktop `Exec` (launcher) + appends args — no binary-path dispatch.
- `update.sh`: self-update helper for installed builds.
