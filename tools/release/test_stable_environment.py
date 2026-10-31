"""Lane RELGH-3: tools/release/stable_environment.py, the environment stable-signing as code (its configuration, the check of GitHub's
record, the dispatcher and approval rules, apply) against tools/release/gh_stand_in.py."""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import stable_environment as se  # noqa: E402

CFG = se.load_config()


def test_the_committed_configuration():
    assert CFG["environment"] == "stable-signing" and CFG["secret"] == "OPENBFME_STABLE_SIGNING_KEY"
    assert CFG["reviewers"] == ["Ancalgonn"] and CFG["prevent_self_review"] is True and CFG["can_admins_bypass"] is False
    assert CFG["deployment_branches"] == ["main"]
    # the key it names is the second entry of the committed list, never the first (the Deck key)
    import release_keys
    keys = release_keys.load(HERE.parents[1] / "launcher/release_keys.pub")
    labels = [lb for lb, _ in keys]
    assert labels[0] == "deck-2026" and CFG["key_label"] in labels[1:]


@pytest.mark.parametrize("field,value,why", [
    ("prevent_self_review", False, "prevent_self_review must be true"),
    ("prevent_self_review", "true", "prevent_self_review must be true"),
    ("can_admins_bypass", True, "can_admins_bypass must be false"),
    ("can_admins_bypass", None, "can_admins_bypass must be false"),
    ("deployment_branches", ["main", "release"], "deployment_branches must be exactly"),
    ("deployment_branches", ["*"], "deployment_branches must be exactly"),
    ("reviewers", [], "reviewers is not a list"),
    ("reviewers", ["Ancalgonn", "ancalgonn"], "reviewers is not a list"),
    ("reviewers", ["a b"], "reviewers is not a list"),
    ("reviewers", "Ancalgonn", "reviewers is not a list"),
    ("environment", "nightly-signing", "is not the nightly one"),
    ("environment", "Stable Signing", "is not a plain name"),
    ("secret", "openbfme", "is not an upper-case name"),
    ("key_label", "Stable 2026", "key_label is not"),
])
def test_a_configuration_that_weakens_a_rule_is_refused(field, value, why):
    c = dict(CFG, **{field: value})
    with pytest.raises(se.Fail, match=why):
        se.load_config(json.dumps(c))


def test_a_configuration_with_more_or_fewer_keys_is_refused():
    with pytest.raises(se.Fail, match="has the keys"):
        se.load_config(json.dumps(dict(CFG, wait_timer=0)))
    c = dict(CFG)
    del c["secret"]
    with pytest.raises(se.Fail, match="has the keys"):
        se.load_config(json.dumps(c))
    with pytest.raises(se.Fail, match="cannot be read"):
        se.load_config("{not json")


def record(reviewers=("Ancalgonn",), prevent=True, bypass=False, rules=()):
    return {"name": "stable-signing", "can_admins_bypass": bypass,
            "deployment_branch_policy": {"protected_branches": False, "custom_branch_policies": True},
            "protection_rules": [{"type": "required_reviewers", "prevent_self_review": prevent,
                                  "reviewers": [{"type": "User", "reviewer": {"login": r}} for r in reviewers]},
                                 {"type": "branch_policy"}, *rules]}


MAIN = {"total_count": 1, "branch_policies": [{"id": 1, "name": "main", "type": "branch"}]}


def test_the_exact_record_passes_and_a_wait_timer_is_allowed():
    assert se.environment_problems(CFG, record(), MAIN) == []
    assert se.environment_problems(CFG, record(reviewers=("ancalgonn",)), MAIN) == [], "GitHub logins are case-insensitive"
    assert se.environment_problems(CFG, record(rules=({"type": "wait_timer", "wait_timer": 5},)), MAIN) == []


@pytest.mark.parametrize("env,pol,why", [
    (record(bypass=True), MAIN, "administrators can bypass"),
    (dict(record(), can_admins_bypass=None), MAIN, "administrators can bypass"),
    (record(prevent=False), MAIN, "'Prevent self-review' is off"),
    (record(reviewers=()), MAIN, "the required reviewers are [], not ['Ancalgonn']"),
    (record(reviewers=("Ancalgonn", "someone")), MAIN, "the required reviewers are"),
    (dict(record(), protection_rules=[{"type": "branch_policy"}]), MAIN, "0 required-reviewers rules"),
    (dict(record(), protection_rules=record()["protection_rules"][:1]), MAIN, "0 branch policy rules"),
    (record(rules=({"type": "required_reviewers", "prevent_self_review": True, "reviewers": []},)), MAIN, "2 required-reviewers rules"),
    (record(rules=({"type": "wait_timer"},)), MAIN, "a wait timer without minutes"),
    (dict(record(), deployment_branch_policy=None), MAIN, "are not 'Selected branches and tags'"),
    (dict(record(), deployment_branch_policy={"protected_branches": True, "custom_branch_policies": False}), MAIN, "Selected branches"),
    (record(), {"branch_policies": []}, "not exactly the branch main"),
    (record(), {"branch_policies": [{"name": "main", "type": "branch"}, {"name": "v*", "type": "tag"}]}, "not exactly the branch main"),
    (record(), {"branch_policies": [{"name": "*", "type": "branch"}]}, "not exactly the branch main"),
    (record(), None, "not exactly the branch main"),
    (None, MAIN, "GitHub has no environment stable-signing"),
    (dict(record(), name="stable"), MAIN, "GitHub has no environment stable-signing"),
])
def test_every_difference_is_named(env, pol, why):
    probs = se.environment_problems(CFG, env, pol)
    assert any(why in p for p in probs), probs


def test_the_dispatcher_must_not_be_a_reviewer():
    assert se.dispatcher_problems(CFG, "release-bot") == []
    for who in ("Ancalgonn", "ANCALGONN"):
        assert "is a required reviewer" in se.dispatcher_problems(CFG, who)[0]
    for who in ("", None, "a b", "x" * 50):
        assert "is not a GitHub login" in se.dispatcher_problems(CFG, who)[0]


def approval(login="Ancalgonn", state="approved", env="stable-signing"):
    return {"state": state, "comment": "", "environments": [{"name": env}], "user": {"login": login}}


@pytest.mark.parametrize("approvals,trig,why", [
    ([approval()], "release-bot", None),
    ([approval(), approval(env="nightly-signing", login="x")], "release-bot", None),
    ([], "release-bot", "no configured reviewer approved"),
    ([approval(login="someone")], "release-bot", "no configured reviewer approved"),
    ([approval(env="other")], "release-bot", "no configured reviewer approved"),
    ([approval(), approval(state="rejected")], "release-bot", "is not an approval"),
    ([approval()], "Ancalgonn", "a self-approval"),
    ({"x": 1}, "release-bot", "not a list"),
])
def test_the_approval(approvals, trig, why):
    probs = se.approval_problems(CFG, approvals, trig)
    assert (probs == []) if why is None else any(why in p for p in probs), probs


# ---- the CLI against the stand-in GitHub -------------------------------------------------------------------------------------------------

@pytest.fixture()
def gh(tmp_path):
    state = tmp_path / "gh.json"
    state.write_text(json.dumps({"prs": {}, "releases": [], "next": 100}))
    exe = tmp_path / "bin" / "gh"
    exe.parent.mkdir()
    exe.write_text(f"#!/bin/bash\nexec {sys.executable} {HERE / 'gh_stand_in.py'} \"$@\"\n")
    exe.chmod(0o755)
    return {"GH": str(exe), "FAKE_GH_STATE": str(state), "FAKE_GH_LOG": str(tmp_path / "gh.log"), "state": state}


def cli(gh, *args, **env):
    e = {**os.environ, **{k: v for k, v in gh.items() if k != "state"}, **env}
    return subprocess.run([sys.executable, str(HERE / "stable_environment.py"), *args], capture_output=True, text=True, env=e, timeout=120)


def test_apply_prints_then_creates_exactly_the_configuration(gh):
    r = cli(gh, "apply", "--repo", "Test/repo")
    assert r.returncode == 0 and "WOULD PUT repos/Test/repo/environments/stable-signing" in r.stdout, r.stderr
    assert '"can_admins_bypass": false' in r.stdout and '"prevent_self_review": true' in r.stdout
    assert "environments" not in json.loads(gh["state"].read_text()), "a plan writes nothing"
    # an environment made by hand with a stray branch rule: apply replaces it with exactly main
    s = json.loads(gh["state"].read_text())
    s["environments"] = {"stable-signing": {"record": record(prevent=False, bypass=True),
                                            "branch_policies": [{"id": 9, "name": "dev", "type": "branch"}]}}
    gh["state"].write_text(json.dumps(s))
    r = cli(gh, "check", "--repo", "Test/repo")
    assert r.returncode == 1 and "Prevent self-review" in r.stderr and "administrators can bypass" in r.stderr
    r = cli(gh, "apply", "--repo", "Test/repo", "--execute")
    assert r.returncode == 0 and "APPLIED stable-signing" in r.stdout and "is not set here" in r.stdout, r.stdout + r.stderr
    env = json.loads(gh["state"].read_text())["environments"]["stable-signing"]
    assert [(b["name"], b["type"]) for b in env["branch_policies"]] == [("main", "branch")]
    assert "secret" not in Path(gh["FAKE_GH_LOG"]).read_text().lower(), "the secret is never set by this tool"
    r = cli(gh, "check", "--repo", "Test/repo", "--dispatcher", "release-bot")
    assert r.returncode == 0 and "ENVIRONMENT ok" in r.stdout and "the dispatcher release-bot is no reviewer" in r.stdout, r.stderr


def test_check_with_this_machines_gh_account(gh):
    cli(gh, "apply", "--repo", "Test/repo", "--execute")
    r = cli(gh, "check", "--repo", "Test/repo", "--dispatcher-is-gh-user", FAKE_GH_USER="Ancalgonn")
    assert r.returncode == 1 and "(Ancalgonn) is a required reviewer" in r.stderr
    r = cli(gh, "check", "--repo", "Test/repo", "--dispatcher-is-gh-user", FAKE_GH_USER="release-bot")
    assert r.returncode == 0, r.stderr
    r = cli(gh, "check", "--repo", "Test/repo", FAKE_GH_FAULT="env-read-fail")
    assert r.returncode == 1 and "failed" in r.stderr


def test_approval_cli(gh):
    s = json.loads(gh["state"].read_text())
    s["approvals"] = {"77": [approval()]}
    gh["state"].write_text(json.dumps(s))
    r = cli(gh, "approval", "--repo", "Test/repo", GITHUB_RUN_ID="77", GITHUB_TRIGGERING_ACTOR="release-bot")
    assert r.returncode == 0 and "APPROVAL ok: run 77" in r.stdout, r.stderr
    r = cli(gh, "approval", "--repo", "Test/repo", GITHUB_RUN_ID="78", GITHUB_TRIGGERING_ACTOR="release-bot")
    assert r.returncode == 1 and "no configured reviewer approved" in r.stderr
    r = cli(gh, "approval", "--repo", "Test/repo", GITHUB_RUN_ID="77", GITHUB_TRIGGERING_ACTOR="Ancalgonn")
    assert r.returncode == 1 and "is a required reviewer" in r.stderr
    r = cli(gh, "approval", "--repo", "Test/repo", GITHUB_RUN_ID="x", GITHUB_TRIGGERING_ACTOR="release-bot")
    assert r.returncode == 1 and "not a number" in r.stderr
