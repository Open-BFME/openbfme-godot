"""Lane MODCOMPAT-1: tools/mod/gap_census.py over a made-up corpus, with a stand-in for openbfme_peer (no mod or game bytes)."""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
TOOL = HERE / "gap_census.py"

# the stand-in peer: the census's probe (an unknown faction) gets the "(playable: ...)" list of the package named by its first overlay; a game
# (Beta's map cache lacks Fall Back 8p) writes the report lines of that package, and the mount arguments it was given
FAKE_PEER = r'''#!/usr/bin/env python3
import sys
a = sys.argv[1:]
mounts = [a[i + 1] for i, v in enumerate(a) if v in ("--mod-install", "--mod", "--mod-engine")]
factions = [a[i + 1].split(",")[1] for i, v in enumerate(a) if v == "--slot"]
package = next((m.split("/")[-2] for m in mounts if m.endswith("/install")), "none")
playable = {"Alpha": "FactionMen FactionElves", "Beta": "FactionMordor", "Gamma": ""}[package]
if "FactionNone_Census" in factions:
    sys.stderr.write(f"openbfme_peer: unknown faction FactionNone_Census (playable: {playable})\n")
    sys.exit(1)
lines = {"Alpha": ["unported_module BoredUpdate 4", "unported_action BUILD_BASE_BUILDING 9", "unported_template_module BoredUpdate 30",
                   "unported_template_module GeometryUpgrade 2"],
         "Beta": ["unported_module BoredUpdate 1", "unported_condition PLAYER_ALL_DESTROYED 5", "unported_template_module BoredUpdate 7"]}[package]
if package == "Beta" and "fall back 8p" in a[a.index("--map") + 1]:
    sys.stderr.write("openbfme_peer: the map 'maps/map mp fall back 8p/map mp fall back 8p.map' is not in the map cache\n")
    sys.exit(1)
report = a[a.index("--report") + 1]
open(report, "w").write("\n".join(lines + ["mount " + " ".join(mounts), "sides " + " ".join(factions)]) + "\n")
'''


def package(corpus: Path, name: str, stack: list[str] | None, edits: list[str] = ()) -> None:
    for member in stack or []:
        (corpus / member / "install").mkdir(parents=True, exist_ok=True)
    (corpus / name).mkdir(exist_ok=True)
    record = {"schema": "openbfme.aio-package 2", "name": name, "stack": None if stack is None else [{"folder": m} for m in stack],
              "engine_edits": list(edits)}
    (corpus / name / "openbfme-aio.json").write_text(json.dumps(record), encoding="utf-8")


@pytest.mark.skipif(os.name == "nt", reason="the stand-in peer is a script run through its #! line")
def test_ranks_the_gaps_sizes_the_templates_and_lists_what_did_not_start(tmp_path):
    corpus = tmp_path / "corpus"
    corpus.mkdir()
    package(corpus, "Alpha", ["Alpha", "Alpha_assets"], ["Alpha: game.dat md5 0123456789abcdef0123456789abcdef"])
    package(corpus, "Beta", ["Beta"])
    package(corpus, "Gamma", ["Gamma"])
    package(corpus, "Patch", None)  # a dependency only: played through the packages that stack it
    peer = tmp_path / "peer"
    peer.write_text(FAKE_PEER, encoding="utf-8")
    peer.chmod(0o755)
    out, reports = tmp_path / "census.md", tmp_path / "reports"
    env = dict(os.environ, ROTWK_INSTALL=str(tmp_path), BFME2_INSTALL=str(tmp_path))
    r = subprocess.run([sys.executable, str(TOOL), "--peer", str(peer), "--corpus", str(corpus), "--out", str(out), "--frames", "10",
                        "--reports", str(reports)], capture_output=True, text=True, timeout=120, env=env)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "3 packages, 2 played, 3 gaps, 2 unported classes in templates" in r.stdout, r.stdout
    md = out.read_text(encoding="utf-8")
    # ranked by how many packages hit an item, then by the total
    assert "| 1 | module | `BoredUpdate` | 2 | 5 | Alpha, Beta |" in md, md
    assert "| 2 | action | `BUILD_BASE_BUILDING` | 1 | 9 | Alpha |" in md, md
    assert "| 3 | condition | `PLAYER_ALL_DESTROYED` | 1 | 5 | Beta |" in md, md
    assert "| 1 | `BoredUpdate` | 2 | 37 | Alpha 30, Beta 7 |" in md, md
    assert "| 2 | `GeometryUpgrade` | 1 | 2 | Alpha 2 |" in md, md
    assert "| Beta | played | FactionMordor (idle) vs hard FactionMordor, FactionMordor, FactionMordor on map mp evendim |" in md, md
    assert "| Gamma | did not start | the mod lists no playable faction (openbfme_peer: unknown faction FactionNone_Census (playable: )) |" in md, md
    assert "| Patch |" not in md, md
    # the corpus test's mount: the stack's install folders in priority order, the engine edit's md5; the mod's playable factions in turn
    alpha = (reports / "Alpha.txt").read_text(encoding="utf-8").splitlines()
    assert f"mount {corpus}/Alpha/install {corpus}/Alpha_assets/install 0123456789abcdef0123456789abcdef" in alpha, alpha
    assert "sides FactionMen FactionElves FactionMen FactionElves" in alpha, alpha
