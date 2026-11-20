#!/usr/bin/env bash
# Phase-11 self-test JSON schema gate: run the scripted self-test lane and validate the
# structured report against the goal schema (test id, subsystem, expected event, actual
# event, selected asset, playback timestamp, measured result, tolerance, pass/fail,
# diagnostic, skipped). The exit code of the self-test is NOT asserted - step verdicts
# belong to self_test_gameplay; this gate only pins the JSON shape written by app/selftest.cpp.
#
# Usage: selftest_json_schema.sh <path/to/wc98-native> <tree>
set -u
BIN="${1:?wc98-native path}"
TREE="${2:?game tree}"
OUT=/tmp/wc98port-out/selftest-schema.json
mkdir -p /tmp/wc98port-out
"$BIN" --self-test "$OUT" --tree "$TREE" >/dev/null 2>&1 || true
if [ ! -s "$OUT" ]; then
    echo "selftest JSON not written (tree unavailable?)"
    exit 2
fi
python3 - "$OUT" <<'EOF' || exit 1
import json, sys
d = json.load(open(sys.argv[1]))
assert d.get("version") == 2, f"version {d.get('version')!r} != 2"
keys = ("step", "subsystem", "expectedEvent", "actualEvent", "selectedAsset",
        "playbackTimestampMs", "measuredResult", "tolerance", "passed", "diagnostic",
        "skipped")
assert isinstance(d.get("steps"), list) and d["steps"], "no steps"
for s in d["steps"]:
    for k in keys:
        assert k in s, f"step {s.get('step')!r}: missing key {k!r}"
    assert s["subsystem"] in ("audio", "gameplay", "presentation"), s["step"]
    assert isinstance(s["passed"], bool), s["step"]
    assert isinstance(s["skipped"], bool), s["step"]
    assert not (s["skipped"] and not s["passed"]), f"step {s['step']}: skipped but not passed"
    assert isinstance(s["playbackTimestampMs"], int), s["step"]
n = len(d["steps"])
audio = sum(1 for s in d["steps"] if s["subsystem"] == "audio")
print(f"schema OK steps={n} audio={audio} gameplay={sum(1 for s in d['steps'] if s['subsystem'] == 'gameplay')} presentation={sum(1 for s in d['steps'] if s['subsystem'] == 'presentation')}")
EOF
exit 0
