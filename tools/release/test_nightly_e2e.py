"""Lane NIGHTLY-1: autorelease.sh --nightly end to end in the stand-in mode of test_autorelease.py (same World fixture). Lane NIGHTLY-2: two
nightlies a day; the run's clock is the stand-in's OPENBFME_RELEASE_NIGHTLY_CLOCK (UTC YYYYMMDDHH), 07:00Z on 2026-10-23 unless a test says."""
from __future__ import annotations

import datetime as dt
import json
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from test_autorelease import World, git, run, TARGET  # noqa: E402,F401
from test_autorelease import autorelease, zip_of  # noqa: E402


@pytest.mark.parametrize("packed", [False, True])
def test_signature_sync_word_is_not_audio_but_tagged_audio_is_refused(packed):
    # A fixed reproduction of the random-signature collision, not a filename exemption.
    for header, expected in ((bytes([255, 251, 144, 0]), False), (b"ID3\x00", True)):
        signature = header + bytes(60)
        audit = autorelease.SyncAudit(top=True)
        name = "manifest.json.sig"
        if packed:
            signature = zip_of({"openbfme-v0.4.5-windows-x64/payload.json.sig": signature})
            name = "package.zip"
        audit.blob(name, signature)
        assert any("audio" in problem for problem in audit.problems) == expected, audit.problems

LLM = r'''#!/usr/bin/env python3
import json, sys
text = sys.stdin.read()
part = text.split("The changes:\n", 1)[1].split("\n\nYour previous answer", 1)[0]
todo = json.JSONDecoder().raw_decode(part.strip())[0]
out = []
for t in todo:
    e = {"id": t["id"], "test": ["Start a skirmish.", "Order a unit around a wall."]}
    if t["write"] != "test steps only":
        e.update(visible=True, kind="fixed", area="", text="Units walk around obstacles again.")
    out.append(e)
print(json.dumps({"changes": out}))
'''


@pytest.fixture
def world(tmp_path):
    w = World(tmp_path)
    llm = tmp_path / "llm.py"
    llm.write_text(LLM)
    llm.chmod(0o755)
    w.llm = f"{sys.executable} {llm}"
    # the stand-in build also records AR_NIGHTLY
    orig = (w.standin / "build").read_text()
    rec = f"open({str(tmp_path / 'ar_nightly.log')!r}, 'a').write(os.environ.get('AR_NIGHTLY', 'unset') + '\\n')\n"
    (w.standin / "build").write_text(orig.replace("import os\n", "import os\n" + rec, 1))
    w.ar_log = tmp_path / "ar_nightly.log"
    # a wrapper gh: the commits/<sha>/pulls endpoint answers [] and the prune listing (jq @tsv, which the stand-in cannot do) is answered
    # from the stand-in's state; everything else is the stand-in's
    wrapper = tmp_path / "bin2" / "gh"
    wrapper.parent.mkdir()
    wrapper.write_text(f'''#!{sys.executable}
import json, os, re, subprocess, sys
a = sys.argv[1:]
if a and a[0] == "api" and re.search(r"commits/[0-9a-f]+/pulls", " ".join(a)):
    print("[]"); sys.exit(0)
if a and a[0] == "api" and "@tsv" in " ".join(a):
    s = json.load(open(os.environ["FAKE_GH_STATE"]))
    for r in s["releases"][::-1]:
        print("\\t".join([str(r["id"]), r["tag_name"], "true" if r["draft"] else "false"]))
    sys.exit(0)
sys.exit(subprocess.call([{str(w.gh)!r}] + a))
''')
    wrapper.chmod(0o755)
    w.gh = wrapper
    return w


MORNING = "2026102307"   # the 07:00Z run: v<x.y.z>-nightly.261023100
EVENING = "2026102319"   # the 19:00Z run: v<x.y.z>-nightly.261023200


def nightly_env(w, **extra) -> dict:
    return {"OPENBFME_NIGHTLY_LLM": w.llm, "AR_NIGHTLY_LOG": str(w.ar_log), "OPENBFME_RELEASE_NIGHTLY_CLOCK": MORNING, **extra}


def stable(w) -> str:
    """a first stable release v0.4.5 of a new commit"""
    sha = w.change({"engine/a.cpp": "2\n"}, "Merge lane/x (X-1: units walk around trees instead of through them (RW 0x123456))")
    w.gate(sha)
    r = w.release(sha, "--since", w.first)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    return sha


def nightly(w, sha: str, *args: str, **env):
    return w.release(sha, "--nightly", *args, **nightly_env(w, **env))


def today() -> str:
    """the nightly number of the default clock (MORNING)"""
    return "261023100"


def new_commit(w, n: str = "3") -> str:
    return w.change({"engine/a.cpp": n + "\n"}, "Merge lane/y (Y-1: archers no longer shoot through walls)")


def test_nightly_publishes_a_prerelease(world):
    w = world
    st = stable(w)
    stable_state = (w.state / "last-released").read_text()
    head = new_commit(w)
    assert not (w.state / "gated.tsv").read_text().count(head)   # no gate record for the nightly's sha
    r = nightly(w, head)
    assert r.returncode == 0, r.stdout + r.stderr
    tag = f"v0.4.6-nightly.{today()}"
    assert f"AUTORELEASE OK {tag}" in r.stdout, r.stdout
    rel = w.gh_data()["releases"][-1]
    assert rel["tag_name"] == tag and rel["prerelease"] is True and rel["draft"] is False
    assert rel["name"] == "OpenBFME Nightly 0.4.6 (2026-10-23, morning)", rel["name"]
    assert "made automatically twice a day" in rel["body"], rel["body"]
    assert "## What to test" in rel["body"] and "Units walk around obstacles again." in rel["body"], rel["body"]
    assert "## Thanks to our community" not in rel["body"]
    assert (w.state / "nightly/last-released").read_text().split()[:2] == [head, tag]
    assert (w.state / "last-released").read_text() == stable_state and stable_state.split()[0] == st
    assert w.ar_log.read_text().split() == ["1"] or w.ar_log.read_text().split()[-1] == "1"
    assert w.remote_refs(f"refs/tags/{tag}")
    assert (w.gh_data()["repo"]["homepage"]).endswith("/tag/v0.4.5"), "a nightly leaves the About link"


def test_second_nightly_same_day(world):
    # NIGHTLY-2: the evening run of the same day publishes its own nightly, above the morning's; a third in the same half day is refused
    w = world
    stable(w)
    head = new_commit(w)
    assert nightly(w, head).returncode == 0
    n = len(w.gh_data()["releases"])
    # nothing changed: skipped
    r = nightly(w, head, OPENBFME_RELEASE_NIGHTLY_CLOCK=EVENING)
    assert r.returncode == 0 or "AUTORELEASE SKIP" in r.stdout or "last released" in r.stdout, r.stdout
    assert len(w.gh_data()["releases"]) == n
    # an engine change the same evening: the evening nightly, with the notes since the morning's
    head2 = new_commit(w, "4")
    r = nightly(w, head2, OPENBFME_RELEASE_NIGHTLY_CLOCK=EVENING)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6-nightly.261023200" in r.stdout, r.stdout + r.stderr
    rel = w.gh_data()["releases"][-1]
    assert rel["tag_name"] == "v0.4.6-nightly.261023200" and rel["name"] == "OpenBFME Nightly 0.4.6 (2026-10-23, evening)", rel
    assert (w.state / "nightly/last-released").read_text().split()[:2] == [head2, "v0.4.6-nightly.261023200"]
    assert f"notes since {head}" in r.stdout, r.stdout
    n += 1
    # another change in the same half day (a later evening hour): refused, one nightly per half day
    head3 = new_commit(w, "5")
    r = nightly(w, head3, OPENBFME_RELEASE_NIGHTLY_CLOCK="2026102323")
    assert r.returncode != 0 and "one nightly per half day" in r.stdout + r.stderr, r.stdout + r.stderr
    assert len(w.gh_data()["releases"]) == n
    assert (w.state / "nightly/last-released").read_text().split()[0] == head2
    # the next morning moves on
    r = nightly(w, head3, OPENBFME_RELEASE_NIGHTLY_CLOCK="2026102407")
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6-nightly.261024100" in r.stdout, r.stdout + r.stderr


def test_nightly_clock_is_for_tests_only(world):
    # the clock override is refused outside the stand-in mode (here: no stand-in), before anything happens
    w = world
    stable(w)
    head = new_commit(w)
    r = nightly(w, head, OPENBFME_RELEASE_STANDIN="")
    assert r.returncode != 0 and "OPENBFME_RELEASE_NIGHTLY_CLOCK is for tests" in r.stdout + r.stderr, r.stdout + r.stderr


def test_nightly_skip_when_nothing_changed(world):
    w = world
    stable(w)
    head = new_commit(w)
    assert nightly(w, head).returncode == 0
    docs = w.change({"docs/A.md": "a\n"}, "docs only")
    r = nightly(w, docs)
    assert r.returncode == 0 and "AUTORELEASE SKIP" in r.stdout, r.stdout + r.stderr


def test_nightly_dry_run_leaves_nothing(world):
    w = world
    stable(w)
    head = new_commit(w)
    n = len(w.gh_data()["releases"])
    main = git(w.remote, "rev-parse", "refs/heads/main")
    r = nightly(w, head, "--dry-run")
    assert r.returncode == 0, r.stdout + r.stderr
    assert len(w.gh_data()["releases"]) == n
    assert git(w.repo, "tag", "--list", "*nightly*") == ""
    assert w.remote_refs("refs/tags/*nightly*") == "" and git(w.remote, "rev-parse", "refs/heads/main") == main
    assert not (w.state / "nightly/last-released").exists() and not (w.state / "nightly/intent").exists()


def test_stable_after_nightly_makes_a_new_public_commit(world):
    w = world
    stable(w)
    head = new_commit(w)
    assert nightly(w, head).returncode == 0
    ntag = f"v0.4.6-nightly.{today()}"
    ncommit = git(w.remote, "rev-parse", f"{ntag}^{{commit}}")
    w.gate(head)
    r = w.release(head)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6" in r.stdout, r.stdout + r.stderr
    scommit = git(w.remote, "rev-parse", "v0.4.6^{commit}")
    assert scommit != ncommit
    assert git(w.remote, "rev-parse", f"{scommit}^{{tree}}") == git(w.remote, "rev-parse", f"{ncommit}^{{tree}}")
    assert git(w.remote, "rev-parse", "refs/heads/main") == scommit


def test_prune_keeps_seven(world):
    w = world
    stable(w)
    s = w.gh_data()
    for i in range(9):
        s["releases"].insert(0, {"id": 900 + i, "tag_name": f"v0.4.6-nightly.2026010{i + 1}", "name": "n", "draft": False,
                                 "prerelease": True, "body": "", "assets": []})
    w.gh_state.write_text(json.dumps(s))
    head = new_commit(w)
    r = nightly(w, head)
    assert r.returncode == 0, r.stdout + r.stderr
    nights = [x["tag_name"] for x in w.gh_data()["releases"] if "-nightly." in x["tag_name"]]
    assert len(nights) == 7, nights
    assert f"v0.4.6-nightly.{today()}" in nights and "v0.4.6-nightly.20260101" not in nights and "v0.4.6-nightly.20260109" in nights
    assert any(x["tag_name"] == "v0.4.5" for x in w.gh_data()["releases"])


def test_nightly_works_without_a_nightly_gated_tsv(world):
    w = world
    stable(w)
    head = new_commit(w)
    r = w.release(head, "--nightly", "--dry-run", **nightly_env(w))
    assert r.returncode == 0, r.stdout + r.stderr


def test_a_stable_and_a_nightly_release_never_overlap(world):
    # Sol r1: a nightly published over an unfinished stable release (its tag pushed, publishing failed) stranded it for ever; now either
    # kind stops while the other kind's intent is open, and changes nothing
    w = world
    stable(w)
    head = new_commit(w)
    (w.state / "intent").write_text("sha=" + head + "\nversion=v0.4.6\nstep=tagged\n")
    r = nightly(w, head)
    assert r.returncode != 0 and "an unfinished stable release is open (v0.4.6" in r.stdout + r.stderr, r.stdout + r.stderr
    assert not (w.state / "nightly/intent").exists() and (w.state / "intent").exists()
    (w.state / "intent").unlink()
    (w.state / "nightly").mkdir(exist_ok=True)
    (w.state / "nightly/intent").write_text(f"sha={head}\nversion=v0.4.6-nightly.{today()}\nstep=tagged\n")
    w.gate(head)
    r = w.release(head)
    assert r.returncode != 0 and "an unfinished nightly release is open" in r.stdout + r.stderr, r.stdout + r.stderr
    assert not (w.state / "intent").exists()
