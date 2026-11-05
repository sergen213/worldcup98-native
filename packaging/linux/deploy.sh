#!/usr/bin/env bash
# Release packaging path: ONE AppImage shipping wc98-native (game) + wc98-launcher (Qt 6 GUI),
# adapted from the proven sibling fifa99-native/packaging/linux/deploy.sh. build-appimage.sh
# stays as the game-only fallback; deploy.sh is the release path once proven.
#
# Exact local run on Arch (Qt 6 installed):
#   cmake --preset release && cmake --build --target wc98-native wc98-launcher
#   bash packaging/linux/deploy.sh build/release dist
# Produces dist/wc98-native-linux-x86_64-<VERSION>.AppImage (+ .sha256, .version).
#
# The repo has no cmake install() rules, so binaries are placed by hand (paths mirror
# build-appimage.sh). linuxdeploy bundles the non-system library closure (SDL3/OpenAL/Qt 6 +
# platform plugins) and rewrites RPATHs to $ORIGIN; the Vulkan loader, GL/EGL, X11/Wayland core
# and glibc stay on the host. Pinned tools are cached under $XDG_CACHE_HOME/wc98-packaging and
# run extracted (no FUSE needed), same policy as build-appimage.sh's appimagetool pin.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
# Pinned 2026-11-03 (continuous build). Bump URL+SHA together when rotating.
LINUXDEPLOY_SHA256="df908b19f1ed1cdafa79508d415aa12d6c56ce4d56d4149aa8c09286718706e5"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage"
APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"

BUILD=${1:-$REPO_ROOT/build/release}
OUT_DIR=${2:-$REPO_ROOT/dist}
[ -d "$BUILD" ] || { echo "deploy: no build dir $BUILD (cmake --preset release)" >&2; exit 1; }
BUILD=$(realpath "$BUILD"); OUT_DIR=$(realpath -m "$OUT_DIR")

GAME=$BUILD/app/wc98-native
LAUNCHER=$BUILD/launcher/wc98-launcher
[ -x "$GAME" ] && [ -x "$LAUNCHER" ] || { echo "deploy: missing $GAME or $LAUNCHER (build both targets)" >&2; exit 1; }

VERSION=${VERSION:-$(sed -n 's/^[[:space:]]*VERSION \([0-9][0-9.]*\).*/\1/p' "$REPO_ROOT/CMakeLists.txt" | head -1)}
[ -n "$VERSION" ] || { echo "deploy: no VERSION; pass env VERSION=" >&2; exit 1; }

# qtpaths6: PATH, Arch /usr/lib/qt6/bin, Ubuntu multiarch.
QTPATHS=$(command -v qtpaths6 || true)
[ -n "$QTPATHS" ] || QTPATHS=/usr/lib/qt6/bin/qtpaths6
[ -x "$QTPATHS" ] || QTPATHS=/usr/lib/x86_64-linux-gnu/qt6/bin/qtpaths6
[ -x "$QTPATHS" ] || { echo "deploy: qtpaths6 not found (install Qt 6)" >&2; exit 1; }

fetch_cached() { # fetch_cached <url> <sha256> <dest>: download+verify unless cached and matching
    local url=$1 sha=$2 dest=$3
    if [ -f "$dest" ] && [ "$(sha256sum "$dest" | cut -d' ' -f1)" = "$sha" ]; then return; fi
    command -v curl >/dev/null || { echo "deploy: curl required to fetch $url" >&2; exit 1; }
    mkdir -p "$(dirname "$dest")"
    echo "deploy: fetching $(basename "$dest")"
    curl -fsSL -o "$dest.tmp" "$url" && mv "$dest.tmp" "$dest"
    [ "$(sha256sum "$dest" | cut -d' ' -f1)" = "$sha" ] || { echo "deploy: SHA-256 mismatch for $dest" >&2; exit 1; }
}
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/wc98-packaging
fetch_cached "$LINUXDEPLOY_URL" "$LINUXDEPLOY_SHA256" "$CACHE/linuxdeploy-x86_64.AppImage"
fetch_cached "$APPIMAGETOOL_URL" "$APPIMAGETOOL_SHA256" "$CACHE/appimagetool-x86_64-1.9.1.AppImage"
# curl downloads land 0644; the tools are exec'd directly (extracted mode).
chmod +x "$CACHE"/*.AppImage 2>/dev/null || true

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
APPDIR=$WORK/AppDir
NAME_G=wc98-native
NAME_L=wc98-launcher
PKG=$REPO_ROOT/packaging/linux

mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/applications"
install -m755 "$GAME" "$APPDIR/usr/bin/$NAME_G"
install -m755 "$LAUNCHER" "$APPDIR/usr/bin/$NAME_L"

# Primary (root) desktop entry = launcher; AppRun execs its Exec line. The game entry lives in
# usr/share/applications and keeps working from inside the AppImage (usr/bin on PATH, $ORIGIN rpath).
install -m644 "$PKG/$NAME_L.desktop" "$APPDIR/$NAME_L.desktop"
install -m644 "$PKG/$NAME_L.desktop" "$APPDIR/usr/share/applications/"
install -m644 "$PKG/$NAME_G.desktop" "$APPDIR/usr/share/applications/"

# Generic artwork (pitch motif) reused for both entries; scale with ImageMagick when available.
for name in $NAME_L $NAME_G; do
    install -m644 "$PKG/$NAME_G.png" "$APPDIR/$name.png"
    for s in 16 32 48 64 128 256 512; do
        mkdir -p "$APPDIR/icons/hicolor/${s}x${s}/apps" "$APPDIR/usr/share/icons/hicolor/${s}x${s}/apps"
        if command -v convert >/dev/null; then
            convert "$PKG/$NAME_G.png" -resize "${s}x${s}" "$APPDIR/icons/hicolor/${s}x${s}/apps/$name.png"
        else
            cp "$PKG/$NAME_G.png" "$APPDIR/icons/hicolor/${s}x${s}/apps/$name.png"
        fi
        cp "$APPDIR/icons/hicolor/${s}x${s}/apps/$name.png" "$APPDIR/usr/share/icons/hicolor/${s}x${s}/apps/"
    done
done

# Qt plugins the launcher needs: X11/Wayland/offscreen platforms, Wayland shell + decorations,
# SVG icons, compose input (not adwaita decorations: they pull in GTK/cairo). qt.conf points Qt
# at usr/plugins relative to usr/bin. Missing plugins are skipped except xcb (hard requirement).
qtplugins=${QT_PLUGIN_DIR:-$("$QTPATHS" --query QT_INSTALL_PLUGINS)}
plugin_args=()
for plugin in platforms/libqxcb.so platforms/libqwayland.so platforms/libqoffscreen.so \
    wayland-shell-integration/libxdg-shell.so wayland-decoration-client/libbradient.so \
    iconengines/libqsvgicon.so imageformats/libqsvg.so \
    platforminputcontexts/libcomposeplatforminputcontextplugin.so; do
    [ -f "$qtplugins/$plugin" ] || continue
    mkdir -p "$APPDIR/usr/plugins/$(dirname "$plugin")"
    cp "$qtplugins/$plugin" "$APPDIR/usr/plugins/$plugin"
    plugin_args+=(--deploy-deps-only "$APPDIR/usr/plugins/$plugin")
done
[ -f "$APPDIR/usr/plugins/platforms/libqxcb.so" ] || { echo "deploy: Qt xcb plugin not in $qtplugins" >&2; exit 1; }
printf '[Paths]\nPrefix = ..\nPlugins = plugins\n' > "$APPDIR/usr/bin/qt.conf"

# Run pinned tools extracted (no FUSE). linuxdeploy's AppRun is an ELF symlink; appimagetool's is a script.
# NO_STRIP=1: linuxdeploy's bundled binutils cannot read current .relr.dyn sections.
LDX=$WORK/ld; ATX=$WORK/at
mkdir -p "$LDX" "$ATX"
( cd "$LDX" && ARCH=x86_64 "$CACHE/linuxdeploy-x86_64.AppImage" --appimage-extract >/dev/null )
( cd "$ATX" && ARCH=x86_64 "$CACHE/appimagetool-x86_64-1.9.1.AppImage" --appimage-extract >/dev/null )
NO_STRIP=1 "$LDX/squashfs-root/AppRun" --appdir "$APPDIR" \
    -e "$APPDIR/usr/bin/$NAME_G" -e "$APPDIR/usr/bin/$NAME_L" \
    -d "$APPDIR/usr/share/applications/$NAME_L.desktop" \
    -i "$APPDIR/icons/hicolor/256x256/apps/$NAME_L.png" \
    --exclude-library 'libvulkan.so*' "${plugin_args[@]}"

# The host Vulkan loader must stay in charge (the game dlopen()s it).
if find "$APPDIR" -name 'libvulkan.so*' | grep -q .; then
    echo "deploy: refusing to package: the Vulkan loader must come from the host" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/$NAME_G-linux-x86_64-$VERSION.AppImage"
rm -f "$OUT"
ARCH=x86_64 sh "$ATX/squashfs-root/AppRun" --no-appstream "$APPDIR" "$OUT"
( cd "$(dirname "$OUT")" && sha256sum -- "$(basename "$OUT")" >"$(basename "$OUT").sha256" )
printf 'v%s\n' "$VERSION" >"$OUT.version"
echo "built: $OUT"
file "$OUT"
