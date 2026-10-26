"""Lane SIGNKMS-1: the release workflow (.github/workflows/release.yml), its one exception in the public workflow policy
(public_workflows.py), and the request checks of actions_release.py (plan, checkout-is-tag, dispatch, wait) with stand-in git and gh."""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import actions_release  # noqa: E402
import public_workflows  # noqa: E402

WF = REPO / ".github/workflows/release.yml"
TEXT = WF.read_text(encoding="utf-8")
PATH = public_workflows.RELEASE_WORKFLOW


def test_release_workflow_passes_the_policy():
    assert public_workflows.workflow_problems(PATH, TEXT) == []


def test_the_exception_is_for_the_release_workflow_alone():
    probs = public_workflows.workflow_problems(".github/workflows/other.yml", TEXT)
    assert any("an id-token permission" in p for p in probs)
    assert any("a write permission" in p for p in probs)


@pytest.mark.parametrize("old,new,why", [
    ("  workflow_dispatch:\n", "  push:\n  workflow_dispatch:\n", "workflow_dispatch alone"),
    ("  workflow_dispatch:\n", "  pull_request:\n  workflow_dispatch:\n", "workflow_dispatch alone"),
    ("          - stable\n", "          - stable\n          - production\n", "outside the nightly / stable environments"),
    ("    environment: ${{ inputs.kind }}\n", "", "outside the nightly / stable environments"),
    ("    environment: ${{ inputs.kind }}\n", "    environment: production\n", "outside the nightly / stable environments"),
    ("  plan:\n", "  plan:\n    permissions:\n      contents: write\n", "jobs with their own permissions"),
    ("      contents: write\n      id-token: write\n", "      contents: write\n      id-token: write\n      packages: write\n",
     "permissions are not exactly"),
    ("      contents: write\n      id-token: write\n", "      id-token: write\n", "permissions are not exactly"),
    ("permissions:\n  contents: read\n", "permissions:\n  contents: read\n  actions: read\n", "top-level permissions"),
    # the one secret (the role's ARN, masked in the logs): that line in the signing job alone
    ("      GH_TOKEN: ${{ github.token }}\n", "      GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}\n", "a secrets reference"),
    ("    timeout-minutes: 10\n", "    timeout-minutes: 10\n    env:\n      SIGNING_ROLE_ARN: ${{ secrets.OPENBFME_SIGNING_ROLE_ARN }}\n",
     "a secrets reference"),
    ("      SIGNING_ROLE_ARN: ${{ secrets.OPENBFME_SIGNING_ROLE_ARN }}\n", "      SIGNING_ROLE_ARN: ${{ secrets.OTHER }}\n",
     "is not the secret OPENBFME_SIGNING_ROLE_ARN"),
    ("      SIGNING_ROLE_ARN: ${{ secrets.OPENBFME_SIGNING_ROLE_ARN }}\n", "      SIGNING_ROLE_ARN: ${{ vars.OPENBFME_SIGNING_ROLE_ARN }}\n",
     "is not the secret OPENBFME_SIGNING_ROLE_ARN"),
])
def test_the_policy_refuses_a_changed_shape(old, new, why):
    assert old in TEXT
    probs = public_workflows.workflow_problems(PATH, TEXT.replace(old, new, 1))
    assert any(why in p for p in probs), probs


def test_no_input_is_expanded_into_a_script():
    """workflow injection: `${{ inputs.* }}` / `${{ github.event.* }}` never appear inside a run: block (the inputs go through env)"""
    import yaml
    doc = yaml.safe_load(TEXT)
    for name, job in doc["jobs"].items():
        for step in job.get("steps", []):
            assert "${{" not in step.get("run", ""), (name, step.get("name"))


def test_only_the_sign_job_can_reach_aws_or_write():
    import yaml
    doc = yaml.safe_load(TEXT)
    for name, job in doc["jobs"].items():
        if name == "sign":
            assert job["permissions"] == {"contents": "write", "id-token": "write"}
            assert job["environment"] == "${{ inputs.kind }}"
            assert set(job["needs"]) == {"plan", "macos", "package"}
        else:
            assert "permissions" not in job and "environment" not in job
            assert "vars." not in json.dumps(job) and "github.token" not in json.dumps(job)
    assert doc["jobs"]["sign"]["env"]["GH_TOKEN"] == "${{ github.token }}"
    # Sol r1: the role's ARN names the AWS account: a (masked) secret, never a variable printed with the job's environment
    assert doc["jobs"]["sign"]["env"]["SIGNING_ROLE_ARN"] == "${{ secrets.OPENBFME_SIGNING_ROLE_ARN }}"
    assert TEXT.count("secrets.") == 1
    assert "cache" not in TEXT.lower().replace("ccache", "")   # no cache another workflow could have written


def test_every_action_is_pinned():
    for line in TEXT.splitlines():
        if "uses:" in line:
            m = public_workflows.USES.match(line)
            assert m and (public_workflows.PINNED.fullmatch(m.group(1)) or m.group(1) == "./.github/workflows/release-package.yml"), line


# ---- RELSIGN-1: the builds are release-package.yml's jobs (one definition for both paths) -----------------------------------------------

PKG_PATH = public_workflows.REUSABLE_WORKFLOW
PKG_TEXT = (REPO / PKG_PATH).read_text(encoding="utf-8")


def test_the_release_builds_call_the_package_workflow():
    import yaml
    doc = yaml.safe_load(TEXT)
    for job, platform in (("macos", "macos"), ("linux-lib", "linux"), ("windows-lib", "windows")):
        j = doc["jobs"][job]
        assert j["uses"] == "./.github/workflows/release-package.yml" and j["needs"] == "plan"
        assert j["with"] == {"platform": platform, "commit": "${{ inputs.commit }}", "version": "${{ inputs.tag }}",
                             "request": "${{ inputs.request }}"}
        assert set(j) == {"name", "needs", "uses", "with"}   # no secrets, no permissions: contents: read from the top level
    # the downloads name release-package.yml's artifacts (the version and the request in the name)
    for stem in ("linux-x64", "windows-x64", "macos-arm64"):
        assert f"name: openbfme-${{{{ inputs.tag }}}}-{stem}-${{{{ inputs.request }}}}" in TEXT
    pkg = yaml.safe_load(PKG_TEXT)
    on = pkg.get("on", pkg.get(True))
    assert set(on) == {"workflow_dispatch", "workflow_call"} and set(on["workflow_call"]) == {"inputs"}
    assert set(on["workflow_call"]["inputs"]) == set(on["workflow_dispatch"]["inputs"]) == {"platform", "commit", "version", "request"}
    assert pkg["permissions"] == {"contents": "read"} and "secrets." not in PKG_TEXT and "secrets:" not in PKG_TEXT
    for name, job in pkg["jobs"].items():
        assert "permissions" not in job and "environment" not in job
        checkout = next(s for s in job["steps"] if str(s.get("uses", "")).startswith("actions/checkout@"))
        assert checkout["with"]["ref"] == "${{ inputs.commit }}" and checkout["with"]["persist-credentials"] is False
        check = job["steps"][0]["run"]
        # dispatched: on staging/<version> at the commit; called: by release.yml on main only (its plan checked tag and commit)
        assert '"$GITHUB_REPOSITORY/.github/workflows/release.yml@refs/heads/main"' in check
        assert '"$GITHUB_REPOSITORY/.github/workflows/release-package.yml@refs/heads/staging/$IN_VERSION"' in check
        assert '[ "$GITHUB_SHA" = "$IN_COMMIT" ]' in check and "${{" not in check


def test_the_package_workflow_passes_the_policy():
    assert public_workflows.workflow_problems(PKG_PATH, PKG_TEXT) == []


@pytest.mark.parametrize("path,text,old,new,why", [
    # workflow_call only in release-package.yml, beside workflow_dispatch, declaring inputs alone
    ("other", "pkg", "", "", "the trigger workflow_call"),
    ("pkg", "pkg", "  workflow_dispatch:\n", "  push:\n  workflow_dispatch:\n", "workflow_dispatch and workflow_call alone"),
    ("pkg", "pkg", "  workflow_call:\n", "  workflow_call:\n    secrets:\n      X:\n        required: false\n", "a secrets key"),
    ("pkg", "pkg", "  workflow_call:\n", "  workflow_call:\n    outputs:\n      x:\n        value: y\n", "declares more than inputs"),
    # a calling job: release.yml alone, release-package.yml alone, no secrets / permissions of its own
    ("release", "release", "    uses: ./.github/workflows/release-package.yml\n    with:\n      platform: macos\n",
     "    uses: ./.github/workflows/release-package.yml\n    secrets: inherit\n    with:\n      platform: macos\n", "a secrets key"),
    ("release", "release", "    uses: ./.github/workflows/release-package.yml\n    with:\n      platform: linux\n",
     "    uses: ./.github/workflows/pr.yml\n    with:\n      platform: linux\n", "not ./.github/workflows/release-package.yml"),
    ("release", "release", "    uses: ./.github/workflows/release-package.yml\n    with:\n      platform: linux\n",
     "    uses: Open-BFME/openbfme-godot/.github/workflows/release-package.yml@" + "0" * 40 + "\n    with:\n      platform: linux\n",
     "not ./.github/workflows/release-package.yml"),
    ("release", "release", "  windows-lib:\n    name: Windows\n", "  windows-lib:\n    name: Windows\n    permissions:\n      contents: read\n",
     "jobs with their own permissions"),
    ("other", "release", "", "", "calls a workflow (only the release workflow may)"),
])
def test_the_policy_refuses_other_calls(path, text, old, new, why):
    src = {"pkg": PKG_TEXT, "release": TEXT}[text]
    assert old in src
    p = {"pkg": PKG_PATH, "release": PATH, "other": ".github/workflows/other.yml"}[path]
    probs = public_workflows.workflow_problems(p, src.replace(old, new, 1) if old else src)
    assert any(why in x for x in probs), probs


# ---- actions_release.py plan / checkout-is-tag -----------------------------------------------------------------------------------------

def git(d: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(d), *args], capture_output=True, text=True, check=True).stdout.strip()


@pytest.fixture()
def repo(tmp_path, monkeypatch):
    d = tmp_path / "r"
    d.mkdir()
    git(d, "init", "-q", "-b", "main")
    git(d, "config", "user.email", "t@example.invalid")
    git(d, "config", "user.name", "t")
    (d / "a.txt").write_text("1\n")
    git(d, "add", "a.txt")
    git(d, "commit", "-q", "-m", "one")
    c1 = git(d, "rev-parse", "HEAD")
    git(d, "tag", "-a", "-m", "v0.4.13", "v0.4.13", c1)
    git(d, "tag", "-a", "-m", "n", "v0.4.14-nightly.20261023", c1)
    (d / "a.txt").write_text("2\n")
    git(d, "commit", "-q", "-am", "two")
    head = git(d, "rev-parse", "HEAD")
    git(d, "tag", "-a", "-m", "n", "v0.4.14-nightly.261023200", head)   # NIGHTLY-2: the evening nightly of 2026-10-23
    git(d, "checkout", "-q", "-b", "side", c1)
    (d / "b.txt").write_text("x\n")
    git(d, "add", "b.txt")
    git(d, "commit", "-q", "-m", "side")
    side = git(d, "rev-parse", "HEAD")
    git(d, "tag", "-a", "-m", "v0.4.15", "v0.4.15", side)
    git(d, "checkout", "-q", "main")
    monkeypatch.chdir(d)
    env = {"GITHUB_EVENT_NAME": "workflow_dispatch", "GITHUB_REF": "refs/heads/main", "GITHUB_REPOSITORY": actions_release.TARGET,
           "GITHUB_SHA": head, "IN_KIND": "stable", "IN_TAG": "v0.4.13", "IN_COMMIT": c1, "IN_REQUEST": "ab" * 16}
    for k, v in env.items():
        monkeypatch.setenv(k, v)
    return {"dir": d, "c1": c1, "head": head, "side": side}


def plan(capsys) -> tuple[int, str]:
    rc = actions_release.main(["plan"])
    cap = capsys.readouterr()
    return rc, cap.out + cap.err


def test_plan_accepts_a_tagged_commit_on_main(repo, capsys):
    rc, out = plan(capsys)
    assert rc == 0 and "PLAN ok: stable v0.4.13" in out


@pytest.mark.parametrize("tag", ["v0.4.14-nightly.20261023", "v0.4.14-nightly.261023200"])
def test_plan_accepts_a_nightly(repo, capsys, monkeypatch, tag):
    monkeypatch.setenv("IN_KIND", "nightly")
    monkeypatch.setenv("IN_TAG", tag)
    monkeypatch.setenv("IN_COMMIT", repo["c1"] if tag.endswith(".20261023") else repo["head"])
    rc, out = plan(capsys)
    assert rc == 0 and f"PLAN ok: nightly {tag}" in out, out


@pytest.mark.parametrize("tag", ["v0.4.14-nightly.261023300", "v0.4.14-nightly.261023210", "v0.4.14-nightly.251023100",
                                 "v0.4.14-nightly.261323100", "v0.4.14-nightly.202610231", "v0.4.14-nightly.7"])
def test_plan_refuses_a_nightly_number(repo, capsys, monkeypatch, tag):
    monkeypatch.setenv("IN_KIND", "nightly")
    monkeypatch.setenv("IN_TAG", tag)
    rc, out = plan(capsys)
    assert rc == 1 and "not a nightly release tag" in out, out


@pytest.mark.parametrize("var,value,why", [
    ("GITHUB_REF", "refs/heads/side", "not refs/heads/main"),
    ("GITHUB_REF", "refs/tags/v0.4.13", "not refs/heads/main"),
    ("GITHUB_EVENT_NAME", "push", "not workflow_dispatch"),
    ("GITHUB_REPOSITORY", "someone/fork", "not Open-BFME/openbfme-godot"),
    ("IN_KIND", "production", "not nightly or stable"),
    ("IN_KIND", "nightly", "not a nightly release tag"),          # a plain tag signed in the nightly environment: refused
    ("IN_TAG", "v0.4.14-nightly.20261023", "not a stable release tag"),   # a nightly tag in the stable environment
    ("IN_TAG", "v0.4.14-nightly.261023200", "not a stable release tag"),
    ("IN_TAG", "v0.4.13; curl evil", "not a stable release tag"),
    ("IN_TAG", "v0.4.99", "names nothing here"),
    ("IN_COMMIT", "A" * 40, "not 40 lower-case hex"),
    ("IN_REQUEST", "short", "request id"),
])
def test_plan_refuses(repo, capsys, monkeypatch, var, value, why):
    monkeypatch.setenv(var, value)
    rc, out = plan(capsys)
    assert rc == 1 and why in out, out


def test_plan_refuses_a_tag_naming_another_commit(repo, capsys, monkeypatch):
    monkeypatch.setenv("IN_COMMIT", repo["head"])
    rc, out = plan(capsys)
    assert rc == 1 and "names" in out


def test_plan_refuses_a_commit_that_is_not_on_main(repo, capsys, monkeypatch):
    monkeypatch.setenv("IN_TAG", "v0.4.15")
    monkeypatch.setenv("IN_COMMIT", repo["side"])
    rc, out = plan(capsys)
    assert rc == 1 and "is not on main" in out


def test_checkout_is_tag(repo, capsys, monkeypatch):
    git(repo["dir"], "checkout", "-q", "--detach", repo["c1"])
    named = git(repo["dir"], "describe", "--tags")     # two tags on c1: git describe names one; the other is refused
    other = "v0.4.14-nightly.20261023" if named == "v0.4.13" else "v0.4.13"
    monkeypatch.setenv("IN_KIND", "nightly" if "nightly" in other else "stable")
    monkeypatch.setenv("IN_TAG", other)
    assert actions_release.main(["checkout-is-tag"]) == 1
    assert "describes as" in capsys.readouterr().err
    git(repo["dir"], "tag", "-d", named)
    assert actions_release.main(["checkout-is-tag"]) == 0
    (repo["dir"] / "a.txt").write_text("changed\n")
    assert actions_release.main(["checkout-is-tag"]) == 1
    assert "has changes" in capsys.readouterr().err


def test_checkout_is_not_the_commit(repo, capsys):
    assert actions_release.main(["checkout-is-tag"]) == 1     # HEAD is main's head, not the tag's commit
    assert "the checkout is" in capsys.readouterr().err


# ---- the Deck's side: dispatch and wait (stand-in gh) --------------------------------------------------------------------------------

def stand_in_gh(tmp_path: Path, state: dict) -> Path:
    """a tiny gh: `workflow run` records the dispatch; `api .../runs` and `api .../releases` answer from the state file"""
    sf = tmp_path / "gh-state.json"
    sf.write_text(json.dumps(state))
    exe = tmp_path / "gh"
    exe.write_text(f"""#!/usr/bin/env python3
import json, sys
s = json.load(open({str(sf)!r}))
a = sys.argv[1:]
s.setdefault("calls", []).append(a)
json.dump(s, open({str(sf)!r}, "w"))
if a[:2] == ["workflow", "run"]:
    sys.exit(s.get("dispatch_rc", 0))
if a[0] == "api" and "/actions/workflows/" in a[1]:
    print(json.dumps({{"workflow_runs": s.get("runs", [])}})); sys.exit(0)
if a[0] == "api" and a[1].split("?")[0].endswith("/releases"):
    print(json.dumps(s.get("releases", []))); sys.exit(0)
sys.exit(2)
""")
    exe.chmod(0o755)
    return sf


def test_dispatch_passes_the_inputs_as_fields(tmp_path, monkeypatch, capsys):
    sf = stand_in_gh(tmp_path, {})
    monkeypatch.setenv("GH", str(tmp_path / "gh"))
    out = tmp_path / "req.json"
    assert actions_release.main(["dispatch", "--tag", "v0.4.13", "--commit", "c" * 40, "--kind", "stable", "--out", str(out)]) == 0
    req = json.loads(out.read_text())
    assert len(req["request"]) == 32
    call = json.loads(sf.read_text())["calls"][0]
    assert call[:3] == ["workflow", "run", "release.yml"] and "--ref" in call and call[call.index("--ref") + 1] == "main"
    assert f"request={req['request']}" in call and "kind=stable" in call and "tag=v0.4.13" in call
    assert actions_release.main(["dispatch", "--tag", "v0.4.13", "--commit", "c" * 40, "--kind", "nightly", "--out", str(out)]) == 1


def run_record(req: str, **kw) -> dict:
    r = {"id": 7, "status": "completed", "conclusion": "success", "head_branch": "main", "event": "workflow_dispatch",
         "path": ".github/workflows/release.yml", "display_title": f"Release v0.4.13 (stable, request {req})"}
    r.update(kw)
    return r


@pytest.mark.parametrize("runs,releases,ok,why", [
    ("ok", "one", True, ""),
    ("failed", "one", False, "ended failure"),
    ("other-branch", "one", False, "not the release workflow dispatched on main"),
    ("twice", "one", False, "2 runs name the request"),
    ("ok", "none", False, "0 draft releases"),
    ("ok", "published", False, "0 draft releases"),
    ("ok", "other-request", False, "0 draft releases"),
    ("pending", "one", False, "still in_progress"),
])
def test_wait(tmp_path, monkeypatch, capsys, runs, releases, ok, why):
    req = "ef" * 16
    rs = {"ok": [run_record(req)], "failed": [run_record(req, conclusion="failure")],
          "other-branch": [run_record(req, head_branch="side")], "twice": [run_record(req), run_record(req, id=8)],
          "pending": [run_record(req, status="in_progress", conclusion=None)]}[runs]
    body = f"x\n\nopenbfme-release-request: {req}\n"
    rel = {"one": [{"id": 55, "tag_name": "v0.4.13", "draft": True, "body": body}], "none": [],
           "published": [{"id": 55, "tag_name": "v0.4.13", "draft": False, "body": body}],
           "other-request": [{"id": 55, "tag_name": "v0.4.13", "draft": True, "body": body.replace(req, "00" * 16)}]}[releases]
    stand_in_gh(tmp_path, {"runs": rs, "releases": rel})
    monkeypatch.setenv("GH", str(tmp_path / "gh"))
    rf = tmp_path / "req.json"
    rf.write_text(json.dumps({"request": req, "tag": "v0.4.13", "kind": "stable", "commit": "c" * 40, "run_id": None}))
    rc = actions_release.main(["wait", "--request-file", str(rf), "--timeout", "0", "--poll", "0"])
    cap = capsys.readouterr()
    if ok:
        assert rc == 0 and cap.out.strip() == "55"
        assert json.loads(rf.read_text())["run_id"] == 7
    else:
        assert rc == 1 and why in cap.err, cap.err


# ---- compare and draft (the release folders of test_signing.py: KMS-sealed, manifests with .sig2) --------------------------------------

from test_signing import built  # noqa: E402,F401  (the module's fixture)


def stand_in(tmp_path: Path, built: dict) -> dict:
    gh = tmp_path / "bin" / "gh"
    gh.parent.mkdir(exist_ok=True)
    gh.write_text(f"#!/bin/bash\nexec {sys.executable} {HERE / 'gh_stand_in.py'} \"$@\"\n")
    gh.chmod(0o755)
    return {"GH": str(gh), "FAKE_GH_STATE": str(tmp_path / "gh.json"), "FAKE_GH_REMOTE": str(built["remote"]),
            "FAKE_GH_LOG": str(tmp_path / "gh.log")}


def copy_folder(src: Path, dst: Path) -> Path:
    import shutil
    shutil.copytree(src, dst)
    return dst


def test_compare_ignores_the_seal_and_finds_a_changed_package(built, tmp_path, monkeypatch, capsys):
    import seal_package
    import ed25519
    version = "v0.4.13"
    act = built["folders"][version]
    cand = copy_folder(act, tmp_path / "cand")
    # the candidate is sealed with a throwaway key: unsealed, its archives are the workflow's byte for byte
    (tmp_path / "u").mkdir()
    for a in seal_package.combined_archives(cand):
        seal_package.unseal(a, tmp_path / "u" / a.name)
        (tmp_path / "u" / a.name).replace(a)
    seal_package.seal(cand, ed25519.sign.__globals__["_sha512"](b"throwaway")[:32], actions_release.TARGET)
    args = ["compare", "--candidate", str(cand), "--actions", str(act), "--version", version]
    assert actions_release.main(args) == 0
    assert "DIFFERS" not in capsys.readouterr().out
    # one changed byte in an unsealed archive (the launcher-only one) or a missing archive: refused
    victim = next(p for p in cand.iterdir() if p.name.startswith("openbfme-launcher-"))
    victim.write_bytes(victim.read_bytes() + b"\0")
    assert actions_release.main(args) == 1
    cap = capsys.readouterr()
    assert f"DIFFERS: {victim.name}" in cap.out and "not the tested candidate" in cap.err
    monkeypatch.setenv("OPENBFME_RELEASE_ACTIONS_COMPARE", "report")
    assert actions_release.main(args) == 0 and "COMPARE WARNING" in capsys.readouterr().out
    monkeypatch.setenv("OPENBFME_RELEASE_ACTIONS_COMPARE", "require")
    victim.unlink()
    assert actions_release.main(args) == 1 and "the candidate's archives are" in capsys.readouterr().err


def test_draft_uploads_reads_back_and_never_publishes(built, tmp_path, monkeypatch, capsys):
    version = "v0.4.13"
    pkg = copy_folder(built["folders"][version], tmp_path / "pkg")
    for k, v in stand_in(tmp_path, built).items():
        monkeypatch.setenv(k, v)
    for k, v in {"IN_KIND": "stable", "IN_TAG": version, "IN_COMMIT": "c" * 40, "IN_REQUEST": "ab" * 16, "GITHUB_RUN_ID": "77"}.items():
        monkeypatch.setenv(k, v)
    assert actions_release.main(["draft", "--pkg", str(pkg)]) == 0, capsys.readouterr().err
    state = json.loads((tmp_path / "gh.json").read_text())
    (rel,) = state["releases"]
    assert rel["draft"] is True and rel["prerelease"] is False and rel["tag_name"] == version
    assert f"openbfme-release-request: {'ab' * 16}" in rel["body"]
    assert sorted(a["name"] for a in rel["assets"]) == sorted(p.name for p in pkg.iterdir())
    log = (tmp_path / "gh.log").read_text()
    assert "draft=false" not in log, "the workflow never publishes"
    # a second run for the same tag creates nothing
    assert actions_release.main(["draft", "--pkg", str(pkg)]) == 1
    assert "exists already" in capsys.readouterr().err
    assert len(json.loads((tmp_path / "gh.json").read_text())["releases"]) == 1


def test_draft_refuses_a_corrupted_upload_and_a_stray_file(built, tmp_path, monkeypatch, capsys):
    version = "v0.4.13-nightly.20261022"
    pkg = copy_folder(built["folders"][version], tmp_path / "pkg")
    for k, v in stand_in(tmp_path, built).items():
        monkeypatch.setenv(k, v)
    for k, v in {"IN_KIND": "nightly", "IN_TAG": version, "IN_COMMIT": "c" * 40, "IN_REQUEST": "cd" * 16}.items():
        monkeypatch.setenv(k, v)
    (pkg / "notes.txt").write_text("not a release asset\n")
    assert actions_release.main(["draft", "--pkg", str(pkg)]) == 1
    assert "the release's assets are" in capsys.readouterr().err
    (pkg / "notes.txt").unlink()
    monkeypatch.setenv("FAKE_GH_FAULT", "upload-corrupt")
    assert actions_release.main(["draft", "--pkg", str(pkg)]) == 1
    assert "another size or digest" in capsys.readouterr().err
    (rel,) = json.loads((tmp_path / "gh.json").read_text())["releases"]
    assert rel["draft"] is True and rel["prerelease"] is True


# ---- autorelease.sh with OPENBFME_RELEASE_SIGNER=actions, end to end (stand-in gh runs a stand-in release workflow) ----------------------

WORKFLOW_HOOK = '''#!{python}
"""the release workflow's stand-in: what release.yml leaves behind (a run record and a KMS-signed draft), from the same stand-in packages"""
import hashlib, json, os, subprocess, sys
from pathlib import Path
Path(os.environ["FAKE_WORKFLOW_WORK"]).mkdir(parents=True, exist_ok=True)
sys.stderr = open(Path(os.environ["FAKE_WORKFLOW_WORK"]) / "hook.err", "a")
sys.path.insert(0, {here!r})
from test_autorelease import package_dir, mac_package_dir
from test_signing import EnvelopeSigner, kms_env_sign
import make_macos_manifest, seal_package
tag, commit, req, kind = (os.environ[k] for k in ("IN_TAG", "IN_COMMIT", "IN_REQUEST", "IN_KIND"))
fault = os.environ.get("FAKE_WORKFLOW_FAULT", "")
state = Path(os.environ["FAKE_GH_STATE"])
remote = os.environ["FAKE_GH_REMOTE"]
work = Path(os.environ["FAKE_WORKFLOW_WORK"]) / req
s = json.loads(state.read_text()) if state.exists() else {{"prs": {{}}, "releases": [], "next": 100}}
run = {{"id": 900 + len(s.get("release_runs", [])), "status": "completed", "conclusion": "failure" if fault == "fail" else "success",
       "head_branch": os.environ["GH_REF"], "event": "workflow_dispatch", "path": ".github/workflows/release.yml",
       "display_title": f"Release {{tag}} ({{kind}}, request {{req}})", "html_url": "https://example.invalid/run"}}
s.setdefault("release_runs", []).append(run)
state.write_text(json.dumps(s))
if fault == "fail":
    sys.exit(0)
co = work / "checkout" / "launcher"
co.mkdir(parents=True)
for f in ("release_key.pub", "release_keys.pub"):
    (co / f).write_bytes(subprocess.run(["git", "-C", remote, "show", f"{{commit}}:launcher/{{f}}"], capture_output=True, check=True).stdout)
d = package_dir(work / "pkg", tag)
if fault == "different-build":
    victim = next(p for p in d.iterdir() if p.name.startswith("openbfme-launcher-"))
    victim.write_bytes(victim.read_bytes() + b"x")
    sums = "".join(f"{{hashlib.sha256(p.read_bytes()).hexdigest()}}  {{p.name}}\\n" for p in sorted(d.iterdir()) if p.name.startswith("openbfme-"))
    (d / f"SHA256SUMS-{{tag}}.txt").write_text(sums)
mac = mac_package_dir(work / "checkout", tag, work / "mac", commit=commit)
(d / mac.name).write_bytes(mac.read_bytes())
with open(d / f"SHA256SUMS-{{tag}}.txt", "a") as f:
    f.write(f"{{hashlib.sha256(mac.read_bytes()).hexdigest()}}  {{mac.name}}\\n")
(d / f"BUILDINFO-{{tag}}.txt").unlink()
seal_package.seal(d, EnvelopeSigner(), os.environ["FAKE_TARGET"])
subprocess.run([sys.executable, {here!r} + "/make_manifest.py", str(d), "--repo", os.environ["FAKE_TARGET"], "--commit", commit,
                "--date", __import__("datetime").datetime.now(__import__("datetime").timezone.utc).strftime("%Y-%m-%d"), "--allow-untagged"],
               check=True, capture_output=True)
date = json.loads((d / "manifest.json").read_text())["date"]
sealed = work / "mac.sha256"
sealed.write_text(f"{{hashlib.sha256((d / mac.name).read_bytes()).hexdigest()}}  {{mac.name}}\\n")
(d / "manifest-macos.json").write_bytes(make_macos_manifest.dumps(make_macos_manifest.build(
    tag, os.environ["FAKE_TARGET"], commit, date, archive=d / mac.name, checksum=sealed)))
for m in ("manifest.json", "manifest-macos.json"):
    (d / f"{{m}}.sig2").write_bytes(kms_env_sign(m, (d / m).read_bytes()))
    if os.environ.get("FAKE_WORKFLOW_PHASE") == "kms":   # phase kms: the KMS signature in both slots
        (d / f"{{m}}.sig").write_bytes(kms_env_sign(m, (d / m).read_bytes()))
s = json.loads(state.read_text())
store = Path(str(state) + ".d")
store.mkdir(exist_ok=True)
rel = {{"id": s["next"], "tag_name": tag, "name": f"OpenBFME {{tag}}", "draft": True, "prerelease": kind == "nightly",
       "body": f"signed on GitHub Actions\\n\\nopenbfme-release-request: {{req}}\\n", "assets": [],
       "author": {{"login": "github-actions[bot]"}}, "created_at": "2026-10-22T05:00:00Z"}}
s["next"] += 1
for p in sorted(d.iterdir()):
    aid = s["next"]; s["next"] += 1
    (store / str(aid)).write_bytes(p.read_bytes())
    rel["assets"].append({{"id": aid, "name": p.name, "size": p.stat().st_size,
                          "digest": "sha256:" + hashlib.sha256(p.read_bytes()).hexdigest(), "state": "uploaded"}})
s["releases"].append(rel)
state.write_text(json.dumps(s))
'''


@pytest.fixture
def actions_world(tmp_path):
    from test_autorelease import World
    from test_signing import KMS
    w = World(tmp_path / "w")
    deck = (w.repo / "launcher/release_key.pub").read_text().strip()
    w.kms_commit = w.change({"launcher/release_keys.pub": f"{deck}  deck-2026\n{KMS.hex()}  kms-2026\n"}, "SIGNKMS-1: trust the KMS key")
    hook = tmp_path / "workflow-hook"
    hook.write_text(WORKFLOW_HOOK.format(python=sys.executable, here=str(HERE)))
    hook.chmod(0o755)
    w.hook = hook
    return w


def actions_env(w, **extra) -> dict:
    from test_autorelease import TARGET as T
    return {"OPENBFME_RELEASE_SIGNER": "actions", "FAKE_GH_WORKFLOW": str(w.hook), "FAKE_WORKFLOW_WORK": str(w.tmp / "wf"),
            "FAKE_TARGET": T, "FAKE_GH_AUTHOR": "tester", **extra}


def test_actions_signer_end_to_end(actions_world):
    import release_keys
    from test_signing import DECK_SECRET  # noqa: F401  (the world's Deck key is its own; only the KMS key is the test's)
    w = actions_world
    sha = w.change({"engine/a.cpp": "2\n"}, "Merge lane/x (X-1: units walk around trees instead of through them)")
    w.gate(sha)
    r = w.release(sha, "--since", w.first, **actions_env(w))
    err = w.tmp / "wf" / "hook.err"
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-1000:] + (err.read_text() if err.exists() else "")
    assert "AUTORELEASE OK v0.4.5" in r.stdout and "COMPARE identical" in r.stdout
    gh = w.gh_data()
    (d,) = gh["dispatches"]
    assert d["workflow"] == "release.yml" and d["ref"] == "main" and d["inputs"]["kind"] == "stable" and d["inputs"]["tag"] == "v0.4.5"
    (rel,) = gh["releases"]
    assert rel["draft"] is False and rel["author"]["login"] == "github-actions[bot]"
    names = sorted(a["name"] for a in rel["assets"])
    assert {"manifest.json.sig", "manifest.json.sig2", "manifest-macos.json.sig", "manifest-macos.json.sig2"} <= set(names)
    # the published signatures: .sig the Deck key's raw one (old launchers), .sig2 the KMS envelope one; the notes are the audited ones
    store = Path(str(w.gh_state) + ".d")
    blob = {a["name"]: (store / str(a["id"])).read_bytes() for a in rel["assets"]}
    keys = release_keys.load(w.repo / "launcher/release_keys.pub")
    for m in ("manifest.json", "manifest-macos.json"):
        assert release_keys.verify_detail(keys, m, blob[m], blob[m + ".sig"]) == ("deck-2026", "raw")
        assert release_keys.verify_detail(keys, m, blob[m], blob[m + ".sig2"]) == ("kms-2026", "envelope")
    assert "Units walk around trees instead of through them" in rel["body"] and "openbfme-release-request" not in rel["body"]
    assert not (w.state / "intent").exists() and not (w.state / "intent.request").exists()


@pytest.mark.parametrize("fault,why", [
    ("fail", "ended failure"),
    ("different-build", "not the candidate this run tested"),
])
def test_actions_signer_refuses_and_publishes_nothing(actions_world, fault, why):
    w = actions_world
    sha = w.change({"engine/a.cpp": "2\n"}, "Merge lane/x (X-1: a change)")
    w.gate(sha)
    r = w.release(sha, "--since", w.first, **actions_env(w, FAKE_WORKFLOW_FAULT=fault))
    assert r.returncode != 0 and why in r.stdout + r.stderr, r.stdout[-3000:] + r.stderr[-2000:]
    assert all(rel["draft"] for rel in w.gh_data()["releases"]), "nothing published"
    assert (w.state / "intent").exists(), "the intent stays open for the next run"


def test_actions_signer_dry_run_prints_the_steps(actions_world):
    w = actions_world
    sha = w.change({"engine/a.cpp": "2\n"}, "Merge lane/x (X-1: a change)")
    w.gate(sha)
    r = w.release(sha, "--since", w.first, "--dry-run", **actions_env(w))
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-2000:]
    for step in ("actions_release.py dispatch", "actions_release.py wait", "cosign_release.py", "actions_release.py compare",
                 "--resume-id"):
        assert step in r.stdout
    assert "dispatches" not in w.gh_data(), "a dry run dispatches nothing"


def test_actions_signer_in_phase_kms_needs_no_deck_key(actions_world):
    import release_keys
    w = actions_world
    sha = w.change({"engine/a.cpp": "2\n", "tools/release/signing_policy.json":
                    json.dumps({"phase": "kms", "kms_label": "kms-2026", "legacy_label": "deck-2026"}) + "\n"},
                   "Merge lane/x (X-1: a change) and the cut-over to phase kms")
    w.gate(sha)
    env = w.env(**actions_env(w, FAKE_WORKFLOW_PHASE="kms"))
    del env["OPENBFME_RELEASE_KEY"]
    from test_autorelease import TARGET as T, run as arun, skip_on_macos
    skip_on_macos()
    r = arun("bash", w.repo / "tools/release/autorelease.sh", sha, "--repo", T, "--remote", "origin", "--since", w.first, env=env)
    err = w.tmp / "wf" / "hook.err"
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-1000:] + (err.read_text() if err.exists() else "")
    assert "no Deck key on this machine" in r.stdout and "AUTORELEASE OK v0.4.5" in r.stdout
    (rel,) = w.gh_data()["releases"]
    store = Path(str(w.gh_state) + ".d")
    blob = {a["name"]: (store / str(a["id"])).read_bytes() for a in rel["assets"]}
    keys = release_keys.load(w.repo / "launcher/release_keys.pub")
    assert release_keys.verify_detail(keys, "manifest.json", blob["manifest.json"], blob["manifest.json.sig"]) == ("kms-2026", "envelope")
    # the Deck path (signer deck) still needs its key
    r = arun("bash", w.repo / "tools/release/autorelease.sh", sha, "--repo", T, "--remote", "origin", "--plan",
             env={k: v for k, v in env.items() if k != "OPENBFME_RELEASE_SIGNER"})
    assert r.returncode == 2 and "OPENBFME_RELEASE_KEY is not set" in r.stdout + r.stderr


def test_actions_signer_reconciles_a_published_release_after_a_lost_readback(actions_world):
    """Sol r2: the published workflow release is reconciled against actions-pkg/ and the trusted list, not the candidate's pkg/"""
    w = actions_world
    sha = w.change({"engine/a.cpp": "2\n"}, "Merge lane/x (X-1: a change)")
    w.gate(sha)
    r = w.release(sha, "--since", w.first, **actions_env(w, FAKE_GH_FAULT="final-readback-fail"))
    assert r.returncode != 0 and "does not read back as published" in r.stdout, r.stdout[-2000:]
    (rel,) = w.gh_data()["releases"]
    assert rel["draft"] is False and (w.state / "intent").exists()
    r = w.release(sha, **actions_env(w))
    # reconciled and recorded; the run then stops on its own sha (released already), as on the Deck path
    assert "AUTORELEASE reconciled" in r.stdout and "was published" in r.stdout, r.stdout[-3000:]
    assert not (w.state / "intent").exists() and (w.state / "last-released").read_text().split()[:2] == [sha, "v0.4.5"]


def test_compare_accepts_a_windows_zip_whose_dll_differs_only_in_its_codeview_guid():
    """RELSIGN-1: the release workflow rebuilds the DLL with release-package.yml's job; MACACT-1 measured it the build host's byte for byte
    but for the CodeView GUID (pdb_tools.same_build). The game's Windows ZIP may differ in exactly that, member by member; nothing else"""
    import io
    import zipfile
    from test_pdb_tools import pe

    def z(members: dict) -> bytes:
        b = io.BytesIO()
        with zipfile.ZipFile(b, "w") as f:
            for n, data in members.items():
                f.writestr(zipfile.ZipInfo(n, (2026, 10, 22, 0, 0, 0)), data)
        return b.getvalue()

    dll = "openbfme-v0.4.13-windows-x64/openbfme.windows.template_debug.x86_64.dll"
    host = {"openbfme-v0.4.13-windows-x64/game.pck": b"pck", dll: pe(guid=b"\x01" * 16, extra=b"code")}
    gh_ = dict(host, **{dll: pe(guid=b"\x02" * 16, extra=b"code")})
    name = "openbfme-v0.4.13-windows-x64.zip"
    assert actions_release.same_build_archive(name, z(host), z(gh_)) is None
    assert "differ outside the CodeView GUID" in actions_release.same_build_archive(name, z(host), z(dict(gh_, **{dll: pe(extra=b"cod3")})))
    assert "game.pck differs" in actions_release.same_build_archive(name, z(host), z(dict(gh_, **{"openbfme-v0.4.13-windows-x64/game.pck": b"pcK"})))
    assert "member lists differ" in actions_release.same_build_archive(name, z(host), z(dict(gh_, extra=b"x")))
    # only the game's Windows ZIP: the launcher's, the Linux and the Mac archives must be the same bytes
    for other in ("openbfme-launcher-v0.4.13-windows-x64.zip", "openbfme-v0.4.13-linux-x64.tar.gz", "openbfme-v0.4.13-macos-arm64.zip"):
        assert actions_release.same_build_archive(other, z(host), z(gh_)) == "not the same bytes"
