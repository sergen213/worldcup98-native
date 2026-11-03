#!/bin/sh
# Self-updater for the World Cup 98 native AppImage.
# Checks GitHub releases of sergen213/worldcup98-native, verifies SHA-256 of
# the downloaded asset against its .sha256 sidecar, smoke-tests the new build
# (`--smoke 3`), then atomically swaps it in; keeps the previous file for
# rollback. It NEVER touches game files, saves or config (~/.config/wc98-native).
#
# Usage: update.sh [--check] /path/to/wc98-native-linux-x86_64-<ver>.AppImage
# Exit codes: 0 up to date (or updated) · 10 --check: update available · 1 failure.
set -eu

REPO=sergen213/worldcup98-native
API="https://api.github.com/repos/$REPO/releases/latest"
ASSET_PREFIX=wc98-native-linux-x86_64

die() { echo "update: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null || die "missing tool: $1"; }
need curl; need sha256sum

CHECK=0
if [ "${1:-}" = --check ]; then CHECK=1; shift; fi
[ $# -eq 1 ] && [ -f "$1" ] || { sed -n '3,9p' "$0"; exit 2; }
# shellcheck disable=SC1007  # CPATH= prefix assignment is the intended idiom
TARGET=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)/$(basename -- "$1")
case $TARGET in *"$ASSET_PREFIX-"*.AppImage) ;; *) die "not an AppImage asset name: $TARGET" ;; esac

gh_curl() { # gh_curl <out|-> <url>; adds GITHUB_TOKEN auth when present
    if [ "$1" = - ]; then
        if [ -n "${GITHUB_TOKEN:-}" ]; then curl -fsSL -H "Authorization: Bearer $GITHUB_TOKEN" "$2"
        else curl -fsSL "$2"; fi
    elif [ -n "${GITHUB_TOKEN:-}" ]; then curl -fsSL -o "$1" -H "Authorization: Bearer $GITHUB_TOKEN" "$2"
    else curl -fsSL -o "$1" "$2"; fi
}
json=$(gh_curl - "$API") || die "cannot reach $API"

latest=$(printf '%s\n' "$json" | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1)
[ -n "$latest" ] || die "no tag_name in API response (rate-limited?)"

# Installed version from the .version sidecar written at build time.
current=unknown
[ -f "$TARGET.version" ] && current=$(cat "$TARGET.version")

ver=${latest#v}
asset="$ASSET_PREFIX-$ver.AppImage"
url=$(printf '%s\n' "$json" | sed -n 's/.*"browser_download_url"[[:space:]]*:[[:space:]]*"\([^"]*'"$asset"'\)".*/\1/p' | head -1)
[ -n "$url" ] || die "release $latest has no asset named $asset"

echo "installed: $current · latest: $latest ($asset)"
if [ "$current" = "$latest" ]; then echo "up to date"; exit 0; fi
[ "$CHECK" = 1 ] && { echo "update available"; exit 10; }

# Download into the target's directory so the final mv is an atomic rename.
work=$(dirname -- "$TARGET")/.wc98-update.$$
mkdir -p "$work"; trap 'rm -rf "$work"' EXIT
gh_curl "$work/$asset" "$url"
gh_curl "$work/$asset.sha256" "$url.sha256"
( cd "$work" && sha256sum -c -- "$asset.sha256" ) || die "SHA-256 verification failed; keeping $current"

chmod +x "$work/$asset"
# Verify the new build presents frames before committing to it.
smoke="APPIMAGE_EXTRACT_AND_RUN=1"
[ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null && smoke="xvfb-run -a $smoke"
if ! sh -c "$smoke \"\$work/\$asset\" --smoke 3"; then
    die "new build failed --smoke 3; keeping $current (nothing was replaced)"
fi
mv -f "$TARGET" "$TARGET.prev"                       # keep previous for rollback
mv -f "$work/$asset" "$TARGET"                       # atomic swap on same filesystem
printf '%s\n' "$latest" >"$TARGET.version"
cp "$work/$asset.sha256" "$TARGET.sha256"
trap - EXIT; rm -rf "$work"

echo "updated $current -> $latest"
echo "previous build kept at $TARGET.prev — delete it once satisfied."
