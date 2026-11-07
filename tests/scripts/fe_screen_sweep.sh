#!/usr/bin/env bash
# fe_screen sweep gate: EVERY shipped menu-layout member - LAYOUTS.BIG, IGLAY.BIG, and the loose
# FE/INGAME/*.LAY copies - through the wc98-native --fe-screen capture lane. Each invocation must
# exit 0 (the app returns non-zero on a trivial PNG < 1 KiB), proving every screen renders a real
# frame for the parity comparison process (docs/reverse-engineering/MENU_PARITY.md §6).
#
# Usage: fe_screen_sweep.sh <path/to/wc98-native> <tree> [extract-tool]
set -u
BIN="${1:?wc98-native path}"
TREE="${2:?game tree}"
EXTRACT="${3:-}"
OUT=/tmp/wc98port-out/sweep
mkdir -p "$OUT"

names_file=$(mktemp)
dump_dir=$(mktemp -d)
if [ -n "$EXTRACT" ] && [ -x "$EXTRACT" ]; then
    for big in "FE/PACK/LAYOUTS/LAYOUTS.BIG" "FE/PACK/LAYOUTS/IGLAY.BIG"; do
        [ -f "$TREE/$big" ] || continue
        "$EXTRACT" "$TREE/$big" "$dump_dir" >/dev/null 2>&1 || true
    done
    find "$dump_dir" -type f \( -iname '*.lay' \) | sed -E 's#.*/##; s/\.[Ll][Aa][Yy]$//' | sort -u > "$names_file"
fi
# Loose in-game layouts (FE/INGAME) by file name.
find "$TREE/FE/INGAME" -maxdepth 1 -iname '*.LAY' 2>/dev/null | sed -E 's#.*/##; s/\.[Ll][Aa][Yy]$//' | sort -u >> "$names_file"
sort -u "$names_file" -o "$names_file"

fail=0
count=0
while IFS= read -r name; do
    [ -n "$name" ] || continue
    count=$((count + 1))
    if ! "$BIN" --fe-screen "$name" --tree "$TREE" >/dev/null 2>&1; then
        echo "SWEEP-FAIL: $name"
        fail=$((fail + 1))
    fi
done < "$names_file"

echo "fe_screen sweep: $count layouts, $fail failures"
rm -f "$names_file"; rm -rf "$dump_dir"
[ "$fail" -eq 0 ] && [ "$count" -gt 60 ]
