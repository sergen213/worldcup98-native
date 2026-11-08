#!/bin/sh
# Build a self-contained Type-2 AppImage from a built tree.
# Usage: build-appimage.sh [-b BUILD_DIR] [-o OUT_DIR] [-v VERSION]
# Pattern borrowed from sibling projects (IKV/Kabus 22/Pusu): custom AppRun +
# appimagetool, GPU stack (libvulkan/libGL/libEGL/dri) never bundled — the host
# loader and drivers must stay in charge. Libraries outside /usr (e.g. a
# source-built SDL3 in /usr/local on CI) are bundled; system libs are not.
set -eu

# shellcheck disable=SC1007  # CPATH= prefix assignment is the intended idiom
REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage"
APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"

BUILD=$REPO_ROOT/build/release
OUT_DIR=$REPO_ROOT/dist
VERSION=""

die() { echo "build-appimage: $*" >&2; exit 1; }

while getopts b:o:v:h opt; do
    case $opt in
        b) BUILD=$OPTARG ;;
        o) OUT_DIR=$OPTARG ;;
        v) VERSION=$OPTARG ;;
        h) sed -n '2,9p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done

BIN=$BUILD/app/wc98-native
[ -x "$BIN" ] || die "binary not found: $BIN (cmake --preset release && cmake --build --target wc98-native)"
[ -z "$VERSION" ] && VERSION=$(sed -n 's/^[[:space:]]*VERSION \([0-9][0-9.]*\).*/\1/p' "$REPO_ROOT/CMakeLists.txt" | head -1)
[ -n "$VERSION" ] || die "could not read project VERSION from CMakeLists.txt; pass -v"

command -v mksquashfs >/dev/null || die "mksquashfs (squashfs-tools) is required"
if ! { command -v curl >/dev/null && command -v sha256sum >/dev/null; }; then
    die "curl and sha256sum are required"
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
APPDIR=$WORK/AppDir
NAME=wc98-native
OUT="$OUT_DIR/$NAME-linux-x86_64-$VERSION.AppImage"

mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/applications" \
         "$APPDIR/icons/hicolor/256x256/apps" "$WORK/atool"

install -m755 "$BIN" "$APPDIR/usr/bin/$NAME"
install -m644 "$REPO_ROOT/packaging/linux/$NAME.desktop" "$APPDIR/$NAME.desktop"
install -m644 "$REPO_ROOT/packaging/linux/$NAME.desktop" "$APPDIR/usr/share/applications/"

# Generic artwork (pitch motif), not original game art. One 512 master; scale
# with ImageMagick when available, else ship the master under each size label.
ICON_SRC=$REPO_ROOT/packaging/linux/$NAME.png
install -m644 "$ICON_SRC" "$APPDIR/$NAME.png"
if command -v convert >/dev/null; then
    for s in 16 32 48 64 128 256 512; do
        mkdir -p "$APPDIR/icons/hicolor/${s}x${s}/apps" "$APPDIR/usr/share/icons/hicolor/${s}x${s}/apps"
        convert "$ICON_SRC" -resize "${s}x${s}" "$APPDIR/icons/hicolor/${s}x${s}/apps/$NAME.png"
        cp "$APPDIR/icons/hicolor/${s}x${s}/apps/$NAME.png" "$APPDIR/usr/share/icons/hicolor/${s}x${s}/apps/"
    done
else
    for s in 16 32 48 64 128 256 512; do
        mkdir -p "$APPDIR/icons/hicolor/${s}x${s}/apps" "$APPDIR/usr/share/icons/hicolor/${s}x${s}/apps"
        cp "$ICON_SRC" "$APPDIR/icons/hicolor/${s}x${s}/apps/$NAME.png"
        cp "$ICON_SRC" "$APPDIR/usr/share/icons/hicolor/${s}x${s}/apps/"
    done
fi

cat >"$APPDIR/AppRun" <<'EOF'
#!/bin/sh
# HERE = AppDir root (works for the mounted AppImage and --appimage-extract).
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/wc98-native" "$@"
EOF
chmod 755 "$APPDIR/AppRun"

# Bundle the dependency closure of libraries that live outside /usr (and
# outside /lib, which is /usr/lib on Arch). The graphics stack stays host-side.
enqueue() { ldd "$1" 2>/dev/null | awk '$3 ~ /^\// {print $3}' >>"$WORK/libs.todo"; }
: >"$WORK/libs.todo"
enqueue "$BIN"
while [ -s "$WORK/libs.todo" ]; do
    line=$(head -n1 "$WORK/libs.todo")
    tail -n +2 "$WORK/libs.todo" >"$WORK/libs.todo.n" && mv "$WORK/libs.todo.n" "$WORK/libs.todo"
    real=$(readlink -f "$line")
    case $real in
        /usr/*|/lib/*) continue ;;  # system: glibc, libstdc++, drivers, libvulkan, X, Wayland...
    esac
    [ -e "$APPDIR/usr/lib/$(basename "$real")" ] && continue
    echo "bundling $real"
    install -m644 "$real" "$APPDIR/usr/lib/"
    enqueue "$real"   # its private deps (only non-/usr ones get copied)
done

# Pinned appimagetool, cached; run extracted so no FUSE is needed to build.
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/wc98-packaging
TOOL=$CACHE/appimagetool-x86_64-1.9.1.AppImage
if [ ! -f "$TOOL" ] || [ "$(sha256sum "$TOOL" | cut -d' ' -f1)" != "$APPIMAGETOOL_SHA256" ]; then
    mkdir -p "$CACHE"
    curl -fL --retry 10 --retry-delay 15 --connect-timeout 15 -o "$TOOL.part" "$APPIMAGETOOL_URL"
    [ "$(sha256sum "$TOOL.part" | cut -d' ' -f1)" = "$APPIMAGETOOL_SHA256" ] || die "appimagetool sha256 mismatch"
    mv -f "$TOOL.part" "$TOOL"
fi
chmod +x "$TOOL"
( cd "$WORK/atool" && ARCH=x86_64 "$TOOL" --appimage-extract >/dev/null )
mkdir -p "$OUT_DIR"
ARCH=x86_64 sh "$WORK/atool/squashfs-root/AppRun" --no-appstream "$APPDIR" "$OUT"

( cd "$(dirname "$OUT")" && sha256sum -- "$(basename "$OUT")" >"$(basename "$OUT").sha256" )
printf 'v%s\n' "$VERSION" >"$OUT.version"
echo "built: $OUT"
file "$OUT"
