"""Lane NIGHTLY-1: the nightly versions (autorelease.py) and the nightly notes (nightly_notes.py)."""
from __future__ import annotations

import datetime as dt
import json
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import autorelease as ar  # noqa: E402
import nightly_notes as nn  # noqa: E402

SHA = "a" * 40
SHB = "b" * 40
SHC = "c" * 40


def test_nightly_version_is_the_next_plain_release_with_the_half_day():
    # NIGHTLY-2: <YYMMDD><1 morning | 2 evening>00 from the run's UTC clock YYYYMMDDHH
    tags = ["refs/tags/v0.4.11", "refs/tags/v0.4.9", "refs/tags/v0.4.0-alpha.5", "refs/tags/v0.4.12-nightly.20261022"]
    assert ar.nightly_version(tags, "2026102307") == "v0.4.12-nightly.261023100"
    tags.append("refs/tags/v0.4.12-nightly.261023100")
    assert ar.nightly_version(tags, "2026102319") == "v0.4.12-nightly.261023200"
    tags.append("refs/tags/v0.4.12-nightly.261023200")
    assert ar.nightly_version(tags, "2026102400") == "v0.4.12-nightly.261024100"
    # the same half day again is refused (one per half day), an earlier one too
    for c in ("2026102312", "2026102323", "2026102311", "2026102207"):
        with pytest.raises(SystemExit, match="one nightly per half day"):
            ar.nightly_version(tags, c)
    # the stable release v0.4.12 is above its nightlies; the next nightly is of v0.4.13
    tags.append("refs/tags/v0.4.12")
    assert ar.nightly_version(tags, "2026102407") == "v0.4.13-nightly.261024100"
    assert ar.next_version(tags) == "v0.4.13"
    for bad in ("20261023", "2026102324", "2026132307", "2026103207", "2025102307", "2026-10-23T07", "202610230", "20261023077"):
        with pytest.raises(SystemExit):
            ar.nightly_version(tags, bad)


def test_nightly_numbers_sort_in_run_order():
    # every NIGHTLY-2 number is above every NIGHTLY-1 date (the shipped v0.4.12-nightly.20261022 first), and in run order
    runs = ["v0.4.12-nightly.20261022", "v0.4.12-nightly.20261023", "v0.4.12-nightly.20991231"]
    day = dt.date(2026, 10, 23)
    while day.year < 2100:
        for hour in (7, 19):
            runs.append(f"v0.4.12-nightly.{ar.nightly_number(day.strftime('%Y%m%d') + f'{hour:02d}')}")
        day += dt.timedelta(days=3)
    assert all(ar.TAG.fullmatch(t) for t in runs)
    assert sorted(runs, key=ar.version_key) == runs and len(set(runs)) == len(runs)
    assert ar.version_key("v0.4.11") < ar.version_key(runs[0]) and ar.version_key(runs[-1]) < ar.version_key("v0.4.12")


def shipped_nightly_text(core: str, n: int) -> str:
    """semver.gd nightly_text() as NIGHTLY-1 shipped it (v0.4.12-nightly.20261022 and every launcher before NIGHTLY-2)"""
    y, mo, d = n // 10000, (n // 100) % 100, n % 100
    if n >= 10000000 and 1 <= mo <= 12 and 1 <= d <= 31:
        return f"Nightly {core} ({y:04d}-{mo:02d}-{d:02d})"
    return f"Nightly {core} build {n}"


def test_a_shipped_launcher_never_shows_a_wrong_date():
    # every half day to 2099: an installed NIGHTLY-1 launcher shows "Nightly 0.4.13 build <n>", the new names show the date and the half
    day = dt.date(2026, 1, 1)
    while day.year < 2100:
        for hour, half in ((0, "morning"), (11, "morning"), (12, "evening"), (23, "evening")):
            n = ar.nightly_number(day.strftime("%Y%m%d") + f"{hour:02d}")
            assert shipped_nightly_text("0.4.13", int(n)) == f"Nightly 0.4.13 build {n}"
            assert ar.display_name(f"v0.4.13-nightly.{n}") == f"Nightly 0.4.13 ({day.isoformat()}, {half})"
        day += dt.timedelta(days=1)
    assert shipped_nightly_text("0.4.12", 20261022) == "Nightly 0.4.12 (2026-10-22)"


def test_nightly_names_lines_and_intents():
    v = "v0.4.12-nightly.20261022"
    assert ar.display_name(v) == "Nightly 0.4.12 (2026-10-22)"
    assert ar.release_title(v) == "OpenBFME Nightly 0.4.12 (2026-10-22)"
    assert ar.channel_of(v) == "preview"
    assert ar.line_of(v) == "nightly" and ar.line_of("v0.4.12-nightly.20261399") == ""
    assert ar.version_key("v0.4.11") < ar.version_key(v) < ar.version_key("v0.4.12")
    assert ar.intent_version_ok(v, "", ["refs/tags/v0.4.11", f"refs/tags/{v}"])
    # a stable release (or a later nightly) tagged meanwhile ends a resumed nightly
    assert not ar.intent_version_ok(v, "", ["refs/tags/v0.4.12"])
    assert not ar.intent_version_ok(v, "", ["refs/tags/v0.4.12-nightly.20261023"])
    assert not ar.intent_version_ok(v, "", ["refs/tags/v0.4.12-nightly.261023100"])
    with pytest.raises(SystemExit):
        ar.nightly_date("v0.4.12")
    # NIGHTLY-2's numbers
    e = "v0.4.12-nightly.261023200"
    assert ar.display_name(e) == "Nightly 0.4.12 (2026-10-23, evening)"
    assert ar.release_title("v0.4.12-nightly.261023100") == "OpenBFME Nightly 0.4.12 (2026-10-23, morning)"
    assert ar.nightly_date(e) == "2026-10-23" and ar.channel_of(e) == "preview" and ar.line_of(e) == "nightly"
    assert ar.intent_version_ok(e, "", ["refs/tags/v0.4.12-nightly.261023100", f"refs/tags/{e}"])
    assert not ar.intent_version_ok("v0.4.12-nightly.261023100", "", ["refs/tags/v0.4.12-nightly.261023200"])
    for bad in ("261023300", "261023000", "261023210", "261023101", "261323100", "261000100", "251023100", "202610231", "2610231"):
        assert ar.line_of(f"v0.4.12-nightly.{bad}") == "", bad
        with pytest.raises(SystemExit):
            ar.display_name(f"v0.4.12-nightly.{bad}")


def test_tester_readme_titles():
    import readme_testers as rt
    assert rt.title_of("v0.4.12-nightly.20261022", "Linux") == "OpenBFME Nightly 0.4.12 (2026-10-22) for Linux"
    assert rt.title_of("v0.4.13-nightly.261023200", "Linux") == "OpenBFME Nightly 0.4.13 (2026-10-23, evening) for Linux"
    assert rt.title_of("v0.4.13-nightly.261023100", "Windows") == "OpenBFME Nightly 0.4.13 (2026-10-23, morning) for Windows"
    assert rt.title_of("v0.4.13-nightly.261023300", "Linux") == "OpenBFME v0.4.13-nightly.261023300 - alpha build for Linux"


def change(sha, notes=(), tests=(), player=True, community=(), inner=()):
    return {"sha": sha, "subject": "Merge lane/x: something", "notes": list(notes),
            "hidden": any(not t or t.lower() == "none" for t in notes), "tests": list(tests), "player": player,
            "files": ["godot/scripts/x.gd"] if player else ["tools/x.py"], "inner": list(inner), "community": list(community)}


PR = {"pr": 67, "login": "daisyhasacat", "title": "Fix road corners", "subjects": ["Fix road corners"]}
PR2 = {"pr": 69, "login": "daisyhasacat", "title": "Restore terrain sunlight", "subjects": []}


def ctx_of(*changes):
    return {"from": "x", "to": "y", "changes": list(changes)}


def good_draft(ctx):
    out = []
    for u in nn.units(ctx):
        if u["need"] == "none":
            continue
        e = {"id": u["id"], "test": ["Start a skirmish as Isengard.", "Build a lumber mill and check it earns resources."]}
        if u["need"] != "steps":
            e.update(visible=True, kind="fixed", area="Buildings", text="Lumber mills earn resources again.")
        out.append(e)
    return {"changes": out}


def test_units_split_community_merges_and_follow_the_override_rules():
    ctx = ctx_of(change(SHA, notes=["Fixed [Gameplay]: Arrows hit."], tests=["Shoot arrows."]),
                 change(SHB, community=[PR, PR2]),
                 change(SHC, player=False),
                 change("d" * 40, notes=["none"]),
                 change("e" * 40, notes=["Added: A thing."]))
    us = {u["id"]: u["need"] for u in nn.units(ctx)}
    assert us == {SHA: "none", f"{SHB}:pr67": "all", f"{SHB}:pr69": "all", SHC: "none", "d" * 40: "none", "e" * 40: "steps"}
    p = nn.prompt(ctx)
    assert f"{SHB}:pr67" in p and SHA not in p and SHC not in p   # nothing asked for the changes the merge lines already describe


def test_check_draft_refuses_jargon_markup_and_gaps():
    ctx = ctx_of(change(SHA), change(SHB, notes=["Added: A thing."]))
    assert nn.check_draft(ctx, good_draft(ctx)) == []
    for bad_text in ("Fixed S-1234 in the sim.", "Ported RW 0x6DA95E.", "Updated game.gd.", "Thanks @someone.", "See `code`.",
                     "The LANE-1 gate passed.", "callFunction now works."):
        d = good_draft(ctx)
        d["changes"][0]["text"] = bad_text
        assert nn.check_draft(ctx, d), bad_text
    d = good_draft(ctx)
    d["changes"].pop()
    assert any("has no entry" in p for p in nn.check_draft(ctx, d))
    d = good_draft(ctx)
    d["changes"].append({"id": SHC, "test": ["x x x"]})
    assert any("names no change" in p for p in nn.check_draft(ctx, d))
    d = good_draft(ctx)
    d["changes"][0]["test"] = []
    assert nn.check_draft(ctx, d)
    d = good_draft(ctx)
    d["changes"][0]["test"] = ["Step."] * (nn.MAX_STEPS + 1)
    assert nn.check_draft(ctx, d)
    assert nn.check_draft(ctx, {"nope": 1}) and nn.check_draft(ctx, None)
    # a change judged invisible needs nothing else
    d = good_draft(ctx)
    d["changes"][0] = {"id": SHA, "visible": False}
    assert nn.check_draft(ctx, d) == []


def test_make_draft_retries_once_with_the_problems_then_fails(monkeypatch):
    ctx = ctx_of(change(SHA))
    answers = [json.dumps({"changes": [{"id": SHA, "visible": True, "kind": "fixed", "area": "", "text": "S-1 fixed.", "test": ["Do it now."]}]}),
               json.dumps(good_draft(ctx))]
    prompts = []
    monkeypatch.setattr(nn, "run_model", lambda cmd, text: (prompts.append(text), answers.pop(0))[1])
    assert nn.make_draft(ctx, "model") == good_draft(ctx)
    assert "previous answer had these problems" in prompts[1]
    answers[:] = ["no json", "still none"]
    with pytest.raises(SystemExit):
        nn.make_draft(ctx, "model")


def test_render_puts_the_community_first_and_credits_it():
    ctx = ctx_of(change(SHA, notes=["Fixed [Gameplay]: Arrows hit their target."], tests=["Shoot arrows at a wall."]),
                 change(SHB, community=[PR, PR2]))
    d = good_draft(ctx)
    for e in d["changes"]:
        if e["id"].endswith("pr69"):
            e["text"] = "Terrain sunlight is brighter again."
    text, lines, sources = nn.render(ctx, d, "v0.4.12-nightly.20261022", ["A known issue."], "Open-BFME/openbfme-godot", SHA, "2026-10-22",
                                     True, True, True)
    assert text.startswith("# OpenBFME Nightly 0.4.12 (2026-10-22)\n")
    assert "## Thanks to our community" in text and "- Thanks to @daisyhasacat (#67): Lumber mills earn resources again." in text
    test = text[text.index("## What to test"):]
    assert test.index("Thanks to @daisyhasacat (#67)") < test.index("Arrows hit their target")
    assert "   - Shoot arrows at a wall." in test        # Test: lines are the steps of their change
    assert "Fix road corners" not in text              # the pull requests' own titles are not published
    assert "openbfme-v0.4.12-nightly.20261022-windows-x64.zip" in text and "## Known issues" in text
    assert "Arrows hit their target." in lines and "Fix road corners" in sources
    assert nn.credit_text([PR, PR2]) == "Thanks to @daisyhasacat (#67, #69)"
    with pytest.raises(SystemExit):
        nn.credit_text([{"pr": 1, "login": "bad login"}])


def test_render_refuses_a_draft_of_another_range():
    ctx = ctx_of(change(SHA))
    with pytest.raises(SystemExit):
        nn.render(ctx, good_draft(ctx_of(change(SHB))), "v0.4.12-nightly.20261022", [], "Open-BFME/openbfme-godot", "", "", True, False, False)


def test_prune_keeps_the_newest_nightlies_only(tmp_path, monkeypatch, capsys):
    rels = [(i, f"v0.4.12-nightly.202610{d:02d}", "false") for i, d in enumerate(range(10, 20))] + \
           [(100, "v0.4.11", "false"), (101, "v0.4.12-nightly.20261020", "true")]
    deleted = []

    class R:
        def __init__(self, out="", rc=0):
            self.stdout, self.stderr, self.returncode = out, "", rc

    def run(cmd, capture_output=True, text=True):
        if "--paginate" in cmd:
            return R("".join(f"{i}\t{t}\t{d}\n" for i, t, d in rels))
        deleted.append(cmd[-1])
        return R()
    monkeypatch.setattr(nn.subprocess, "run", run)
    nn.prune("gh", "o/r", 7, dry=True)
    assert deleted == [] and capsys.readouterr().out.count("WOULD RUN") == 3
    nn.prune("gh", "o/r", 7, dry=False)
    assert deleted == ["repos/o/r/releases/2", "repos/o/r/releases/1", "repos/o/r/releases/0"]   # the stable release and the draft stay


def test_pulls_of_retries_an_empty_github_answer_and_fails_closed_when_it_persists(monkeypatch):
    class R:
        def __init__(self, out="", err="", rc=0):
            self.stdout, self.stderr, self.returncode = out, err, rc
    answers = [R(err="unexpected end of JSON input", rc=1), R(err="unexpected end of JSON input", rc=1),
               R(out='[{"number": 69, "user": {"login": "daisyhasacat"}, "title": "t", "base": {"repo": {"full_name": "o/r"}}}]')]
    monkeypatch.setattr(nn.subprocess, "run", lambda cmd, capture_output=True, text=True: answers.pop(0))
    assert nn.GitHub("gh", "o/r", attempts=4, backoff=0).pulls_of(SHA) == [{"pr": 69, "login": "daisyhasacat", "title": "t"}]
    monkeypatch.setattr(nn.subprocess, "run", lambda cmd, capture_output=True, text=True: R(err="unexpected end of JSON input", rc=1))
    with pytest.raises(SystemExit):
        nn.GitHub("gh", "o/r", attempts=3, backoff=0).pulls_of(SHB)
    monkeypatch.setattr(nn.subprocess, "run", lambda cmd, capture_output=True, text=True: R(err="HTTP 422: No commit found", rc=1))
    assert nn.GitHub("gh", "o/r", attempts=3, backoff=0).pulls_of(SHB) == []   # an internal cherry-pick: GitHub does not know it


@pytest.mark.parametrize("body", ["", " \n", "{}", "null", "false", "0", '"no PRs"', "["])
def test_pulls_of_retries_non_array_bodies_and_does_not_cache_failures(monkeypatch, body):
    good = json.dumps([{"number": 69, "user": {"login": "daisyhasacat"}, "title": "t", "base": {"repo": {"full_name": "o/r"}}}])
    answers = [body, body, good]
    calls, sleeps = [], []

    def run(cmd, **kwargs):
        calls.append(cmd)
        return nn.subprocess.CompletedProcess(cmd, 0, answers.pop(0), "")

    monkeypatch.setattr(nn.subprocess, "run", run)
    monkeypatch.setattr(nn.time, "sleep", sleeps.append)
    github = nn.GitHub("gh", "o/r", attempts=3, backoff=2)
    expected = [{"pr": 69, "login": "daisyhasacat", "title": "t"}]
    assert github.pulls_of(SHA) == expected
    assert calls == [["gh", "api", f"repos/o/r/commits/{SHA}/pulls"]] * 3
    assert sleeps == [2, 4]
    assert github.pulls_of(SHA) == expected and len(calls) == 3

    answers[:] = [body] * 3
    with pytest.raises(SystemExit, match="fail closed.*JSON array"):
        github.pulls_of(SHB)
    assert SHB not in github.cache and len(calls) == 6 and not answers
    answers[:] = [good]
    assert github.pulls_of(SHB) == expected and len(calls) == 7


@pytest.mark.parametrize("body, rc, err", [("[]", 0, ""), ("", 1, "HTTP 404: Not Found"), ("", 1, "HTTP 422: No commit found")])
def test_pulls_of_caches_valid_no_pr_answers_without_retry(monkeypatch, body, rc, err):
    calls = []

    def run(cmd, **kwargs):
        calls.append(cmd)
        return nn.subprocess.CompletedProcess(cmd, rc, body, err)

    monkeypatch.setattr(nn.subprocess, "run", run)
    monkeypatch.setattr(nn.time, "sleep", lambda seconds: pytest.fail("a valid no-PR answer must not retry"))
    github = nn.GitHub("gh", "o/r")
    assert github.pulls_of(SHA) == [] and github.pulls_of(SHA) == []
    assert len(calls) == 1


@pytest.mark.parametrize("attempts", [0, -2])
@pytest.mark.parametrize("body", ["[]", "{}"])
def test_pulls_of_makes_one_attempt_for_nonpositive_attempts(monkeypatch, attempts, body):
    calls = []

    def run(cmd, **kwargs):
        calls.append(cmd)
        return nn.subprocess.CompletedProcess(cmd, 0, body, "")

    monkeypatch.setattr(nn.subprocess, "run", run)
    monkeypatch.setattr(nn.time, "sleep", lambda seconds: pytest.fail("a single attempt must not sleep"))
    github = nn.GitHub("gh", "o/r", attempts=attempts)
    if body == "[]":
        assert github.pulls_of(SHA) == []
    else:
        with pytest.raises(SystemExit, match="fail closed"):
            github.pulls_of(SHA)
    assert len(calls) == 1
