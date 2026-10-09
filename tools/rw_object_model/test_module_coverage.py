# OpenBFME. GPL-3.0.
# Lane MODULES-1: module_coverage.py joins a registry table with a census histogram (synthetic inputs, no retail data).

import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import module_coverage  # noqa: E402


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def test_statuses_and_totals(tmp_path):
    table = tmp_path / "t.tsv"
    write(table, "class\ttype\tstatus\ttyped\tai\tdrawkind\n"
                 "ActiveBody\tBehavior\tported\t1\t0\t-\n"
                 "AnimalAIUpdate\tBehavior\tbase-ai\t0\t1\t-\n"
                 "Foo\tBehavior\tparsed-only\t1\t0\t-\n"
                 "Bar\tBehavior\tmissing\t0\t0\t-\n"
                 "W3DModelDraw\tDraw\tparsed-only\t1\t0\t0\n"
                 "W3DLaserDraw\tDraw\tmissing\t0\t0\t5\n")
    census = tmp_path / "c.json"
    hist = [
        {"class": "ActiveBody", "category": "Body", "objects": 10, "declarations": 9},
        {"class": "AnimalAIUpdate", "category": "Behavior", "objects": 3, "declarations": 3},
        {"class": "Foo", "category": "Behavior", "objects": 2, "declarations": 2},
        {"class": "Bar", "category": "Behavior", "objects": 5, "declarations": 5},
        {"class": "W3DModelDraw", "category": "Draw", "objects": 7, "declarations": 7},
        {"class": "W3DLaserDraw", "category": "Draw", "objects": 1, "declarations": 1},
        {"class": "Unknown", "category": "Behavior", "objects": 1, "declarations": 1},
    ]
    write(census, json.dumps({"objects": {"total_templates": 20, "module_histogram": hist}}))
    out = tmp_path / "m.md"
    assert module_coverage.main(["--table", str(table), "--census", str(census), "--out", str(out)]) == 0
    text = out.read_text(encoding="utf-8")
    assert "| ported | 2 | 17 |" in text          # ActiveBody + the drawn draw class
    assert "| base AI | 1 | 3 |" in text
    assert "| parsed only | 1 | 2 |" in text
    assert "| missing | 2 | 6 |" in text          # Bar + the effect draw that is not drawn
    assert "| not registered | 1 | 1 |" in text
    # sorted by templates
    assert text.index("ActiveBody") < text.index("W3DModelDraw") < text.index("| Bar |")
