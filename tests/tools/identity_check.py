#!/usr/bin/env python3
# game_data_identity gate: the runtime GameData view (wc98-gamecheck) and the
# extraction export (wc98-dbexplorer) must describe the SAME records - team
# identities index-by-index, roster player ids, and resolved player names.
import json
import sys


def fix(s: str) -> str:
    # dbexplorer's jstr() escapes UTF-8 payload bytes as \u00XX (latin-1 view);
    # re-decode to recover the converter's UTF-8 text. ASCII input passes through.
    try:
        return s.encode("latin-1").decode("utf-8")
    except (UnicodeEncodeError, UnicodeDecodeError):
        return s


def main(db_path: str, game_path: str) -> int:
    db = json.load(open(db_path))
    g = json.load(open(game_path))
    assert g["teams"] == len(db["teams"]), f"team count {g['teams']} != {len(db['teams'])}"
    assert g["players"] == len(db["players"]), f"player count {g['players']} != {len(db['players'])}"
    assert g["unresolved"] == 0, f"gamecheck has {g['unresolved']} unresolved roster slots"
    for row in g["teamRows"]:
        t = db["teams"][row["index"]]
        assert row["id"] == t["id"], f"index {row['index']}: id {row['id']} != {t['id']}"
        assert row["fullName"] == t["name"], f"index {row['index']}: name mismatch"
        assert row["abbreviation"] == t["abbreviation"], f"index {row['index']}: abbrev mismatch"
        db_ids = [pid for pid in t.get("players", []) if pid != 0]
        game_ids = [r["id"] for r in row["roster"]]
        # gamecheck dumps at most the first 3 slots; compare the prefix.
        assert game_ids == db_ids[: len(game_ids)], f"index {row['index']}: roster ids differ"
        for r in row["roster"][:3]:
            p = next(x for x in db["players"] if x["id"] == r["id"])
            if r["name"] != fix(p["name"]):
                raise AssertionError("player %d: game '%s' != export '%s'" % (r["id"], r["name"], p["name"]))
    print(f"identity OK: {g['teams']} teams, {g['rosterRefs']} roster refs, "
          f"kit home={g['kitHomeFound']} away={g['kitAwayFound']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
