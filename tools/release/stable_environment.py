#!/usr/bin/env python3
"""Lane RELGH-3: the protected GitHub environment that holds the stable Actions key, as code (docs/RELEASE.md, "Stable signing on GitHub
Actions"). tools/release/stable_environment.json is the committed configuration:
  environment          the environment's name (stable-signing); the release workflow's sign-stable job is the only job that names it
                       (tools/release/public_workflows.py)
  secret               its one secret, the stable Actions key (OPENBFME_STABLE_SIGNING_KEY)
  key_label            the label in launcher/release_keys.pub of the key that secret must hold (never the list's first entry: the
                       offline Deck key, deck-2026, never enters GitHub)
  reviewers            the required reviewers: the owner's GitHub login, users only (a team's members cannot be read with the run's token)
  prevent_self_review  true: whoever dispatched the run cannot approve it (so the release account that dispatches can never approve)
  can_admins_bypass    false: no administrator can deploy without the review
  deployment_branches  ["main"]: a run on any other branch cannot enter the environment
This tool refuses a configuration that weakens any of those rules (prevent_self_review false, can_admins_bypass true, other branches).

    stable_environment.py check [--repo R] [--config FILE] [--dispatcher LOGIN | --dispatcher-is-gh-user]
        GitHub's record of the environment is exactly the configuration: its reviewers, prevent_self_review, can_admins_bypass, the
        branch policy (custom branches, exactly `main`), no other protection rule but a wait timer; with a dispatcher, that login is not
        a reviewer (it could approve the run it starts through the API: the self-approval loophole); --dispatcher-is-gh-user takes the
        login of this machine's gh (autorelease.sh: the account that will dispatch)
    stable_environment.py approval --run-id N [--repo R] [--config FILE] [--triggering-actor LOGIN]
        (the sign-stable job, before the key step) the run's deployment review: approved for the environment by a configured reviewer
        who is not the run's triggering actor ($GITHUB_TRIGGERING_ACTOR), and no rejection
    stable_environment.py apply [--repo R] [--config FILE] [--execute]
        (the owner's admin login) creates or updates the environment to the configuration: the reviewers (by their user ids),
        prevent_self_review, can_admins_bypass, the custom branch policy with exactly `main`; without --execute it prints the calls only.
        The secret is never set here (docs/RELEASE.md: `gh secret set` from the key file, on the owner's go)
Every read that fails, and every answer that is not exactly the expected one, is a failure (fail closed). GH (default gh) is the GitHub
CLI; the tests use tools/release/gh_stand_in.py.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CONFIG = HERE / "stable_environment.json"
TARGET = "Open-BFME/openbfme-godot"
NAME = re.compile(r"[a-z0-9][a-z0-9-]{0,62}")
LOGIN = re.compile(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})")
SECRET = re.compile(r"[A-Z][A-Z0-9_]{0,99}")
LABEL = re.compile(r"[a-z0-9-]{1,32}")
KNOWN_RULES = {"required_reviewers", "branch_policy", "wait_timer"}


class Fail(Exception):
    pass


def load_config(text: str | None = None, path: Path = CONFIG) -> dict:
    """the configuration, checked: anything that weakens a rule is refused here, before GitHub is asked"""
    try:
        c = json.loads(path.read_text(encoding="utf-8") if text is None else text)
    except (OSError, ValueError) as e:
        raise Fail(f"the environment configuration cannot be read ({e.__class__.__name__})") from None
    want = {"comment", "environment", "secret", "key_label", "reviewers", "prevent_self_review", "can_admins_bypass", "deployment_branches"}
    if not isinstance(c, dict) or set(c) != want:
        raise Fail(f"the environment configuration has the keys {sorted(c) if isinstance(c, dict) else '?'}, not {sorted(want)}")
    if not isinstance(c["environment"], str) or not NAME.fullmatch(c["environment"]):
        raise Fail("the environment's name is not a plain name")
    if c["environment"] == "nightly-signing":
        raise Fail("the stable key's environment is not the nightly one")
    if not isinstance(c["secret"], str) or not SECRET.fullmatch(c["secret"]):
        raise Fail("the secret's name is not an upper-case name")
    if not isinstance(c["key_label"], str) or not LABEL.fullmatch(c["key_label"]):
        raise Fail("key_label is not a key list label")
    r = c["reviewers"]
    if not isinstance(r, list) or not r or len(r) > 6 or not all(isinstance(x, str) and LOGIN.fullmatch(x) for x in r) \
            or len({x.lower() for x in r}) != len(r):
        raise Fail("reviewers is not a list of 1 to 6 distinct GitHub logins")
    if c["prevent_self_review"] is not True:
        raise Fail("prevent_self_review must be true (whoever dispatches a stable release must never approve it)")
    if c["can_admins_bypass"] is not False:
        raise Fail("can_admins_bypass must be false (no administrator deploys without the review)")
    if c["deployment_branches"] != ["main"]:
        raise Fail("deployment_branches must be exactly [\"main\"]")
    return c


def gh() -> str:
    return os.environ.get("GH", "gh")


def gh_json(*args: str):
    r = subprocess.run([gh(), "api", *args], capture_output=True, text=True, timeout=120)
    if r.returncode:
        raise Fail(f"gh api {args[-1]} failed: {(r.stderr or r.stdout).strip()[-300:]}")
    try:
        return json.loads(r.stdout) if r.stdout.strip() else None
    except ValueError:
        raise Fail(f"gh api {args[-1]} answered no JSON") from None


def environment_problems(cfg: dict, env, policies) -> list[str]:
    """GitHub's environment record `env` (GET repos/R/environments/<name>) and its branch policies `policies`
    (GET .../deployment-branch-policies) against the configuration; [] when they are exactly it"""
    name = cfg["environment"]
    if not isinstance(env, dict) or env.get("name") != name:
        return [f"GitHub has no environment {name}"]
    p = []
    if env.get("can_admins_bypass") is not False:
        p.append(f"{name}: administrators can bypass its protection rules (can_admins_bypass {env.get('can_admins_bypass')!r}): "
                 "turn off 'Allow administrators to bypass configured protection rules'")
    if env.get("deployment_branch_policy") != {"protected_branches": False, "custom_branch_policies": True}:
        p.append(f"{name}: its deployment branches are not 'Selected branches and tags' ({env.get('deployment_branch_policy')!r})")
    rules = env.get("protection_rules")
    if not isinstance(rules, list):
        return p + [f"{name}: no protection rules"]
    types = [r.get("type") if isinstance(r, dict) else None for r in rules]
    if any(t not in KNOWN_RULES for t in types):
        p.append(f"{name}: a protection rule this check does not know ({sorted(set(map(str, types)) - KNOWN_RULES)})")
    if types.count("branch_policy") != 1:
        p.append(f"{name}: {types.count('branch_policy')} branch policy rules, not 1")
    rev = [r for r in rules if isinstance(r, dict) and r.get("type") == "required_reviewers"]
    if len(rev) != 1:
        p.append(f"{name}: {len(rev)} required-reviewers rules, not 1 (the owner must approve every stable release)")
    else:
        r = rev[0]
        if r.get("prevent_self_review") is not True:
            p.append(f"{name}: 'Prevent self-review' is off: the account that dispatches a run could approve it")
        got = []
        for x in r.get("reviewers") or []:
            login = (x.get("reviewer") or {}).get("login") if isinstance(x, dict) else None
            if not isinstance(x, dict) or x.get("type") != "User" or not isinstance(login, str):
                p.append(f"{name}: a reviewer that is not a user ({x.get('type') if isinstance(x, dict) else x!r})")
                continue
            got.append(login)
        if sorted(g.lower() for g in got) != sorted(w.lower() for w in cfg["reviewers"]):
            p.append(f"{name}: the required reviewers are {sorted(got)}, not {sorted(cfg['reviewers'])}")
    for r in rules:
        if isinstance(r, dict) and r.get("type") == "wait_timer" and not isinstance(r.get("wait_timer"), int):
            p.append(f"{name}: a wait timer without minutes")
    bp = policies.get("branch_policies") if isinstance(policies, dict) else None
    if not isinstance(bp, list) or sorted((b.get("name"), b.get("type")) for b in bp if isinstance(b, dict)) != [("main", "branch")] \
            or len(bp) != 1:
        got = [(b.get("name"), b.get("type")) for b in bp if isinstance(b, dict)] if isinstance(bp, list) else bp
        p.append(f"{name}: its deployment branch policies are {got!r}, not exactly the branch main")
    return p


def dispatcher_problems(cfg: dict, login: str) -> list[str]:
    if not isinstance(login, str) or not LOGIN.fullmatch(login):
        return [f"the dispatching account {login!r} is not a GitHub login"]
    if login.lower() in {r.lower() for r in cfg["reviewers"]}:
        return [f"the account that dispatches the release ({login}) is a required reviewer of {cfg['environment']}: its token could "
                "approve the run it starts (with prevent_self_review GitHub refuses that approval, so the release could never be "
                "approved). Dispatch with the release account, which is not a reviewer (docs/RELEASE.md, the switch-over)"]
    return []


def read_environment(repo: str, cfg: dict) -> tuple:
    name = cfg["environment"]
    try:
        env = gh_json(f"repos/{repo}/environments/{name}")
    except Fail as e:
        if "404" in str(e):
            raise Fail(f"GitHub has no environment {name} in {repo} (the owner creates it: docs/RELEASE.md, the switch-over)") from None
        raise
    return env, gh_json(f"repos/{repo}/environments/{name}/deployment-branch-policies")


def check(repo: str, cfg: dict, dispatcher: str | None = None) -> None:
    probs = environment_problems(cfg, *read_environment(repo, cfg))
    if dispatcher is not None:
        probs += dispatcher_problems(cfg, dispatcher)
    if probs:
        raise Fail("; ".join(probs))


def approval_problems(cfg: dict, approvals, triggering: str) -> list[str]:
    """GET repos/R/actions/runs/<id>/approvals: an approval of the environment by a configured reviewer who did not trigger the run"""
    name = cfg["environment"]
    if not isinstance(approvals, list):
        return ["the run's deployment reviews are not a list"]
    mine = [a for a in approvals if isinstance(a, dict) and any(isinstance(e, dict) and e.get("name") == name
                                                                for e in a.get("environments") or [])]
    if any(a.get("state") != "approved" for a in mine):
        return [f"a review of {name} on this run is not an approval ({sorted({str(a.get('state')) for a in mine})})"]
    reviewers = {r.lower() for r in cfg["reviewers"]}
    ok = [a for a in mine if str((a.get("user") or {}).get("login", "")).lower() in reviewers]
    if not ok:
        return [f"no configured reviewer approved {name} on this run (reviews by "
                f"{sorted(str((a.get('user') or {}).get('login')) for a in mine)})"]
    if any(str((a.get("user") or {}).get("login", "")).lower() == triggering.lower() for a in ok):
        return [f"the run was approved by the account that triggered it ({triggering}): a self-approval"]
    return []


def cmd_check(a) -> None:
    cfg = load_config(path=Path(a.config))
    who = a.dispatcher
    if a.dispatcher_is_gh_user:
        u = gh_json("user")
        who = u.get("login") if isinstance(u, dict) else None
    check(a.repo, cfg, who)
    print(f"ENVIRONMENT ok: {cfg['environment']} of {a.repo} (reviewers {', '.join(cfg['reviewers'])}, prevent self-review, no admin "
          f"bypass, branch main{'' if who is None else f'; the dispatcher {who} is no reviewer'})")


def cmd_approval(a) -> None:
    cfg = load_config(path=Path(a.config))
    rid = a.run_id or os.environ.get("GITHUB_RUN_ID", "")
    if not re.fullmatch(r"[0-9]{1,20}", str(rid)):
        raise Fail("the run id is not a number")
    trig = a.triggering_actor or os.environ.get("GITHUB_TRIGGERING_ACTOR", "")
    if not LOGIN.fullmatch(trig):
        raise Fail("the run's triggering actor is unknown")
    probs = dispatcher_problems(cfg, trig) + approval_problems(cfg, gh_json(f"repos/{a.repo}/actions/runs/{rid}/approvals"), trig)
    if probs:
        raise Fail("; ".join(probs))
    print(f"APPROVAL ok: run {rid} (triggered by {trig}) was approved for {cfg['environment']} by its reviewer")


def cmd_apply(a) -> None:
    cfg = load_config(path=Path(a.config))
    name = cfg["environment"]
    ids = []
    for login in cfg["reviewers"]:
        u = gh_json(f"users/{login}")
        if not isinstance(u, dict) or not isinstance(u.get("id"), int) or str(u.get("login")).lower() != login.lower():
            raise Fail(f"GitHub has no user {login}")
        ids.append(u["id"])
    body = {"wait_timer": 0, "prevent_self_review": True, "can_admins_bypass": False,
            "reviewers": [{"type": "User", "id": i} for i in ids],
            "deployment_branch_policy": {"protected_branches": False, "custom_branch_policies": True}}
    calls = [("PUT", f"repos/{a.repo}/environments/{name}", body)]
    if not a.execute:
        for m, path, b in calls:
            print(f"WOULD {m} {path} {json.dumps(b, sort_keys=True)}")
        print(f"WOULD set the branch policies of {name} to exactly: main (branch)")
        return
    for m, path, b in calls:
        r = subprocess.run([gh(), "api", "--method", m, path, "--input", "-"], input=json.dumps(b), capture_output=True, text=True, timeout=120)
        if r.returncode:
            raise Fail(f"{m} {path} failed: {(r.stderr or r.stdout).strip()[-300:]}")
    pol = gh_json(f"repos/{a.repo}/environments/{name}/deployment-branch-policies")
    have = pol.get("branch_policies", []) if isinstance(pol, dict) else []
    for b in have:
        if (b.get("name"), b.get("type")) != ("main", "branch"):
            gh_json("--method", "DELETE", f"repos/{a.repo}/environments/{name}/deployment-branch-policies/{b.get('id')}")
    if not any((b.get("name"), b.get("type")) == ("main", "branch") for b in have):
        gh_json("--method", "POST", f"repos/{a.repo}/environments/{name}/deployment-branch-policies", "-f", "name=main", "-f", "type=branch")
    check(a.repo, cfg)
    print(f"APPLIED {name} of {a.repo}; read back: it is the configuration. The secret {cfg['secret']} is not set here")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter, allow_abbrev=False)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    w = c.add_mutually_exclusive_group()
    w.add_argument("--dispatcher")
    w.add_argument("--dispatcher-is-gh-user", action="store_true")
    p = sub.add_parser("approval")
    p.add_argument("--run-id")
    p.add_argument("--triggering-actor")
    y = sub.add_parser("apply")
    y.add_argument("--execute", action="store_true")
    for s in (c, p, y):
        s.add_argument("--repo", default=TARGET)
        s.add_argument("--config", default=str(CONFIG))
    a = ap.parse_args(argv)
    try:
        {"check": cmd_check, "approval": cmd_approval, "apply": cmd_apply}[a.cmd](a)
    except Fail as e:
        print(f"STABLE ENVIRONMENT FAIL ({a.cmd}): {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
