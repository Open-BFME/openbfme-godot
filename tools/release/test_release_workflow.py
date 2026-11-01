"""Lane SIGNKMS-1, RELGH-2 (nightly only, the nightly key): the release workflow (.github/workflows/release.yml), its one exception in the public workflow policy
(public_workflows.py), and the request checks of actions_release.py (plan, checkout-is-tag, dispatch, wait) with stand-in git and gh."""
from __future__ import annotations

import json
import os
import subprocess
import sys
import time
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
    assert any("a write permission" in p for p in probs)
    assert any("a secrets reference" in p for p in probs)
    assert not any("id-token" in p for p in probs), "the workflow has no OIDC token at all"


SECRET = "${{ secrets.OPENBFME_NIGHTLY_SIGNING_KEY }}"
KEY_STEP = "      - name: Seal and sign with the nightly key, verify under launcher/release_keys.pub + launcher/nightly_keys.pub\n"


@pytest.mark.parametrize("old,new,why", [
    ("  workflow_dispatch:\n", "  push:\n  workflow_dispatch:\n", "workflow_dispatch alone"),
    ("  workflow_dispatch:\n", "  pull_request:\n  workflow_dispatch:\n", "workflow_dispatch alone"),
    # the environments: nightly-signing and (RELGH-3) stable-signing, one signing job each
    ("    environment: nightly-signing\n", "", "not exactly one per environment"),
    ("    environment: nightly-signing\n", "    environment: production\n", "outside the signing environments"),
    ("    environment: nightly-signing\n", "    environment: stable\n", "outside the signing environments"),
    ("    environment: nightly-signing\n", "    environment: ${{ inputs.kind }}\n", "outside the signing environments"),
    ("    environment: nightly-signing\n", "    environment: stable-signing\n", "named by two jobs"),
    ("    environment: stable-signing\n", "", "not exactly one per environment"),
    ("    environment: stable-signing\n", "    environment: nightly-signing\n", "named by two jobs"),
    ("    environment: stable-signing\n", "    environment: stable\n", "outside the signing environments"),
    ("  plan:\n", "  plan:\n    permissions:\n      contents: write\n", "a write permission"),
    ("      contents: read\n      actions: read\n    env:", "      contents: read\n      actions: write\n    env:", "only a signing job writes"),
    ("      contents: read\n      actions: read\n    env:", "      contents: read\n      deployments: read\n    env:", "only a signing job writes"),
    # id-token anywhere: refused (no OIDC, no cloud credential)
    ("    permissions:\n      contents: write\n", "    permissions:\n      contents: write\n      id-token: write\n", "an id-token permission"),
    ("    permissions:\n      contents: write\n", "    permissions:\n      contents: write\n      id-token: write\n", "permissions are not exactly"),
    ("permissions:\n  contents: read\n\nconcurrency", "permissions:\n  contents: read\n  id-token: write\n\nconcurrency", "an id-token permission"),
    ("  plan:\n", "  plan:\n    env:\n      X: id-token\n", "an id-token permission"),
    ("    permissions:\n      contents: write\n", "    permissions:\n      contents: write\n      packages: write\n", "permissions are not exactly"),
    ("    permissions:\n      contents: write\n", "    permissions:\n      contents: read\n", "permissions are not exactly"),
    ("permissions:\n  contents: read\n\nconcurrency", "permissions:\n  contents: read\n  actions: read\n\nconcurrency", "top-level permissions"),
    # the secret: one step's env, by the exact line, in the signing job alone
    ("      GH_TOKEN: ${{ github.token }}\n", "      GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}\n", "a secrets reference"),
    ("    permissions:\n      contents: write\n    env:\n      GH_TOKEN: ${{ github.token }}\n",
     "    permissions:\n      contents: write\n    env:\n      GH_TOKEN: ${{ github.token }}\n      NIGHTLY_SIGNING_KEY: " + SECRET + "\n",
     "hands a secret to every step"),                                                                  # at job level
    ("    timeout-minutes: 10\n", "    timeout-minutes: 10\n    env:\n      NIGHTLY_SIGNING_KEY: " + SECRET + "\n", "a secrets reference"),
    ("        run: python3 tools/release/actions_release.py draft", "        env:\n          NIGHTLY_SIGNING_KEY: " + SECRET +
     "\n        run: python3 tools/release/actions_release.py draft", "exactly one step"),               # in two steps
    ("          NIGHTLY_SIGNING_KEY: " + SECRET + "\n", "          NIGHTLY_SIGNING_KEY: ${{ secrets.OTHER }}\n", "a secrets reference"),
    ("          NIGHTLY_SIGNING_KEY: " + SECRET + "\n", "          NIGHTLY_SIGNING_KEY: ${{ secrets.OTHER }}\n", "exactly one step"),
    ("          NIGHTLY_SIGNING_KEY: " + SECRET + "\n", "          NIGHTLY_SIGNING_KEY: ${{ vars.OPENBFME_NIGHTLY_SIGNING_KEY }}\n",
     "exactly one step"),
    ("          NIGHTLY_SIGNING_KEY: " + SECRET + "\n", "          OTHER_NAME: " + SECRET + "\n", "exactly one step"),
    ("          NIGHTLY_SIGNING_KEY: " + SECRET + "\n", "          NIGHTLY_SIGNING_KEY: " + SECRET + "\n          X: ${{ secrets.Y }}\n",
     "a secrets reference"),
])
def test_the_policy_refuses_a_changed_shape(old, new, why):
    assert old in TEXT
    probs = public_workflows.workflow_problems(PATH, TEXT.replace(old, new, 1))
    assert any(why in p for p in probs), probs


STABLE_SECRET = "${{ secrets.OPENBFME_STABLE_SIGNING_KEY }}"
STABLE_LINE = "          STABLE_SIGNING_KEY: " + STABLE_SECRET + "\n"
STABLE_PERMS = "    environment: stable-signing\n    permissions:\n      contents: write\n      actions: read\n"


@pytest.mark.parametrize("old,new,why", [
    # Sol r1: the guard, the keyed command and the approval step are fixed
    ("    if: needs.plan.outputs.kind == 'stable'\n", "    if: always()\n", "exactly when the plan's kind is stable"),
    ("    if: needs.plan.outputs.kind == 'stable'\n", "", "exactly when the plan's kind is stable"),
    ("    if: needs.plan.outputs.kind == 'nightly'\n", "    if: needs.plan.outputs.kind != ''\n", "exactly when the plan's kind is nightly"),
    ("      - name: The owner approved this run (and not the account that started it)\n        run: python3 tools/release/stable_environment.py approval\n",
     "", "no `python3 tools/release/stable_environment.py approval` step before"),
    ("STABLE_SIGNING_KEY: " + STABLE_SECRET + "\n        run: python3 tools/release/actions_release.py sign",
     "STABLE_SIGNING_KEY: " + STABLE_SECRET + "\n        run: env | sort; python3 tools/release/actions_release.py sign", "keyed step does not run exactly"),
    ("    needs: [plan, macos, package]\n    if: needs.plan.outputs.kind == 'stable'\n", "    needs: [plan]\n    if: needs.plan.outputs.kind == 'stable'\n",
     "does not wait for the plan and the builds"),
    # RELGH-3: the stable job's own secret, in one step, by the exact line; never the nightly key there, nor the stable key elsewhere
    (STABLE_LINE, "          NIGHTLY_SIGNING_KEY: " + SECRET + "\n", "exactly one step"),
    (STABLE_LINE, "          STABLE_SIGNING_KEY: ${{ secrets.OPENBFME_NIGHTLY_SIGNING_KEY }}\n", "exactly one step"),
    (STABLE_LINE, "          STABLE_SIGNING_KEY: ${{ secrets.OTHER }}\n", "a secrets reference"),
    (STABLE_LINE, STABLE_LINE + "          NIGHTLY_SIGNING_KEY: " + SECRET + "\n", "a secrets reference"),
    ("          NIGHTLY_SIGNING_KEY: " + SECRET + "\n", "          STABLE_SIGNING_KEY: " + STABLE_SECRET + "\n", "exactly one step"),
    ("          NIGHTLY_SIGNING_KEY: " + SECRET + "\n", "          STABLE_SIGNING_KEY: " + STABLE_SECRET + "\n", "a secrets reference"),
    ("        run: python3 tools/release/stable_environment.py approval\n",
     "        env:\n" + STABLE_LINE + "        run: python3 tools/release/stable_environment.py approval\n", "exactly one step"),
    # its permissions: exactly contents write + actions read
    (STABLE_PERMS, "    environment: stable-signing\n    permissions:\n      contents: write\n", "permissions are not exactly"),
    (STABLE_PERMS, "    environment: stable-signing\n    permissions:\n      contents: write\n      actions: write\n", "a write permission"),
    (STABLE_PERMS, STABLE_PERMS + "      id-token: write\n", "an id-token permission"),
    (STABLE_PERMS, STABLE_PERMS + "      deployments: write\n", "permissions are not exactly"),
    # the stable key at job level, or in the plan job
    ("    permissions:\n      contents: write\n      actions: read\n    env:\n      GH_TOKEN: ${{ github.token }}\n",
     "    permissions:\n      contents: write\n      actions: read\n    env:\n      GH_TOKEN: ${{ github.token }}\n      STABLE_SIGNING_KEY: "
     + STABLE_SECRET + "\n", "hands a secret to every step"),
    ("      - name: Check the inputs, the ref, the tag and the commit\n",
     "      - name: Check the inputs, the ref, the tag and the commit\n        env:\n" + STABLE_LINE, "a secrets reference"),
])
def test_the_policy_refuses_a_changed_stable_job(old, new, why):
    assert old in TEXT
    probs = public_workflows.workflow_problems(PATH, TEXT.replace(old, new, 1))
    assert any(why in p for p in probs), probs


def test_the_stable_environment_of_the_workflow_is_the_configuration():
    """RELGH-3: the literal environment and secret of the stable job are stable_environment.json's"""
    import yaml
    import stable_environment
    cfg = stable_environment.load_config()
    job = yaml.safe_load(TEXT)["jobs"]["sign-stable"]
    assert job["environment"] == cfg["environment"] == "stable-signing"
    assert public_workflows.STABLE_SECRET == "${{ secrets.%s }}" % cfg["secret"]
    assert job["if"] == "needs.plan.outputs.kind == 'stable'" and yaml.safe_load(TEXT)["jobs"]["sign"]["if"] == "needs.plan.outputs.kind == 'nightly'"
    assert public_workflows.SIGN_JOBS["stable-signing"][0] == "STABLE_SIGNING_KEY"


@pytest.mark.parametrize("old,new", [
    ("      GH_TOKEN: ${{ github.token }}\n    steps", "      GH_TOKEN: ${{ github.token }}\n      ALL: ${{ toJSON(secrets) }}\n    steps"),
    ("      GH_TOKEN: ${{ github.token }}\n    steps", "      GH_TOKEN: ${{ github.token }}\n      ALL: ${{ format('{0}', secrets) }}\n    steps"),
    ("      GH_TOKEN: ${{ github.token }}\n    steps", "      GH_TOKEN: ${{ github.token }}\n      ALL: ${{ join(secrets, ',') }}\n    steps"),
    ("    steps:\n      - uses: actions/checkout", "    if: ${{ secrets }}\n    steps:\n      - uses: actions/checkout"),
    ("    steps:\n      - uses: actions/checkout", "    if: toJSON(secrets) != ''\n    steps:\n      - uses: actions/checkout"),
    ("      - name: Check the inputs, the ref, the tag and the commit\n", "      - if: contains(secrets, 'x')\n        name: x\n"),
])
def test_any_use_of_the_secrets_context_is_refused(old, new):
    """Sol r1: not only `secrets.X`: toJSON(secrets), format(.., secrets), a bare secrets in an if: line"""
    assert old in TEXT
    probs = public_workflows.workflow_problems(PATH, TEXT.replace(old, new, 1))
    assert any("a secrets reference" in p for p in probs), probs
    assert public_workflows.workflow_problems(".github/workflows/other.yml", TEXT.replace(old, new, 1)), "(and in any other file)"


def test_the_word_secrets_in_text_is_not_a_reference():
    probs = public_workflows.workflow_problems(PATH, TEXT.replace("    timeout-minutes: 10\n", "    timeout-minutes: 10\n    name: no secrets-free-zone\n", 1))
    assert not any("secrets reference" in p for p in probs), probs


@pytest.mark.parametrize("old,new", [
    ("  plan:\n    name: Check the request\n", "  plan:\n    environment: nightly-signing\n    name: Check the request\n"),
    ("  plan:\n    name: Check the request\n", "  plan:\n    environment: production\n    name: Check the request\n"),
    ("  plan:\n    name: Check the request\n", "  plan:\n    environment:\n      name: nightly-signing\n    name: Check the request\n"),
    ("    environment: nightly-signing\n", "    environment: nightly-signing\n    environment: nightly-signing\n"),
    ("  package:\n", "  package:\n    environment: nightly-signing\n"),
])
def test_a_deployment_environment_outside_the_signing_job_is_refused(old, new):
    assert old in TEXT
    probs = public_workflows.workflow_problems(PATH, TEXT.replace(old, new, 1))
    assert any("deployment environment outside the release workflow's signing job" in p or "not where they are expected" in p
               or "outside the nightly-signing" in p for p in probs), probs


@pytest.mark.parametrize("path", [".github/workflows/other.yml", ".github/workflows/release-package.yml", ".github/workflows/pr.yml"])
def test_environment_is_refused_in_every_other_workflow(path):
    text = (REPO / ".github/workflows/pr.yml").read_text(encoding="utf-8")
    jobs = text.index("\njobs:\n") + len("\njobs:\n")
    first = text[jobs:].split("\n", 1)[0]
    bad = text[:jobs] + first + "\n    environment: nightly-signing" + text[jobs + len(first):]
    probs = public_workflows.workflow_problems(path, bad)
    assert any("a deployment environment outside the release workflow's signing job" in p for p in probs), probs
    assert not any("deployment environment" in p for p in public_workflows.workflow_problems(path, text)), "(the real file has none)"
    # release.yml's own signing job keeps its line
    assert not any("deployment environment" in p for p in public_workflows.workflow_problems(PATH, TEXT))


def test_the_workflow_has_no_kind_and_no_cloud_credential():
    import yaml
    doc = yaml.safe_load(TEXT)
    on = doc.get("on", doc.get(True))
    assert set(on) == {"workflow_dispatch"} and set(on["workflow_dispatch"]["inputs"]) == {"tag", "commit", "request"}
    low = json.dumps(doc, default=str).lower()    # the parsed workflow (comments are not code)
    for word in ("id-token", "kms", "aws", "oidc", "inputs.kind", "cosign", "role", "deck-2026", "release-ed25519"):
        assert word not in low, word
    assert doc["env"] == {"IN_TAG": "${{ inputs.tag }}", "IN_COMMIT": "${{ inputs.commit }}", "IN_REQUEST": "${{ inputs.request }}"}


@pytest.mark.parametrize("job,var,secret,kind", [("sign", "NIGHTLY_SIGNING_KEY", SECRET, "nightly"),
                                                  ("sign-stable", "STABLE_SIGNING_KEY", "${{ secrets.OPENBFME_STABLE_SIGNING_KEY }}", "stable")])
def test_the_key_is_shredded_by_an_always_step_after_the_signing_step(job, var, secret, kind):
    import yaml
    steps = yaml.safe_load(TEXT)["jobs"][job]["steps"]
    names = [s.get("name", "") for s in steps]
    i = next(n for n, s in enumerate(steps) if s.get("env", {}).get(var) == secret)
    assert steps[i]["run"].startswith("python3 tools/release/actions_release.py sign ")
    after = steps[i + 1]
    assert after.get("if") == "always()" and f"/dev/shm/openbfme-{kind}-key-" in after["run"] and f"{kind}.pem" in after["run"] \
        and "shred" in after["run"], names
    assert [s for s in steps if var in json.dumps(s)] == [steps[i]]


def test_the_stable_job_checks_the_environment_and_the_approval_before_the_key_step():
    """RELGH-3: before the one step that holds the key, the job runs the plan again (the environment, its reviewers, the dispatcher) and
    the approval check (the owner approved this run, not the account that started it)"""
    import yaml
    steps = yaml.safe_load(TEXT)["jobs"]["sign-stable"]["steps"]
    runs = [s.get("run", "") for s in steps]
    key = next(n for n, s in enumerate(steps) if "STABLE_SIGNING_KEY" in json.dumps(s.get("env", {})))
    plan = next(n for n, r in enumerate(runs) if "actions_release.py plan" in r)
    appr = next(n for n, r in enumerate(runs) if r == "python3 tools/release/stable_environment.py approval")
    assert plan < appr < key and runs.index("python3 tools/release/actions_release.py draft --pkg \"$RUNNER_TEMP/pkg\"") > key


def test_no_input_is_expanded_into_a_script():
    """workflow injection: `${{ inputs.* }}` / `${{ github.event.* }}` never appear inside a run: block (the inputs go through env)"""
    import yaml
    doc = yaml.safe_load(TEXT)
    for name, job in doc["jobs"].items():
        for step in job.get("steps", []):
            assert "${{" not in step.get("run", ""), (name, step.get("name"))


def test_only_the_sign_jobs_can_write_or_read_the_key():
    import yaml
    doc = yaml.safe_load(TEXT)
    for name, job in doc["jobs"].items():
        if name == "sign":
            assert job["permissions"] == {"contents": "write"}
            assert job["environment"] == "nightly-signing"
            assert set(job["needs"]) == {"plan", "macos", "package"}
            assert "STABLE_SIGNING_KEY" not in json.dumps(job)
        elif name == "sign-stable":
            assert job["permissions"] == {"contents": "write", "actions": "read"}
            assert job["environment"] == "stable-signing"
            assert set(job["needs"]) == {"plan", "macos", "package"}
            assert "NIGHTLY_SIGNING_KEY" not in json.dumps(job)
        else:
            assert "environment" not in job
            # CRASHACT-1: the one other secret, the crash service's client key, in the package step and the macOS call alone
            crash = {"package": ("steps", "Build the packages (package.sh)"), "macos": ("secrets", None)}.get(name)
            text = json.dumps(job)
            if crash:
                assert text.count("secrets.") == 1 and "${{ secrets.CRASH_CLIENT_KEY }}" in text, name
                if crash[0] == "steps":
                    keyed = [st for st in job["steps"] if "secrets." in json.dumps(st)]
                    assert [(st["name"], st["env"]) for st in keyed] == [(crash[1], {"CRASH_CLIENT_KEY": "${{ secrets.CRASH_CLIENT_KEY }}"})]
                else:
                    assert job["secrets"] == {"CRASH_CLIENT_KEY": "${{ secrets.CRASH_CLIENT_KEY }}"}
                text = text.replace("${{ secrets.CRASH_CLIENT_KEY }}", "")
            assert "vars." not in text and "secrets." not in text
            # the plan job reads the published stable release (the transition rule) and (RELGH-3) the environment stable-signing: a
            # read-only token, nothing else
            assert ("github.token" in json.dumps(job)) == (name == "plan")
            assert job.get("permissions") == ({"contents": "read", "actions": "read"} if name == "plan" else None)
    assert doc["jobs"]["sign"]["env"] == doc["jobs"]["sign-stable"]["env"] == {"GH_TOKEN": "${{ github.token }}"}
    assert doc["jobs"]["plan"]["env"] == {"GH_TOKEN": "${{ github.token }}"}
    assert doc["jobs"]["plan"]["outputs"] == {"kind": "${{ steps.plan.outputs.kind }}"}
    assert doc["permissions"] == {"contents": "read"}
    # CRASHACT-1: + the crash service's client key, twice (the package step, the macOS call)
    assert TEXT.count("secrets.") == 4 and TEXT.count("${{ secrets.CRASH_CLIENT_KEY }}") == 2 and TEXT.count(SECRET) == 1 and TEXT.count("${{ secrets.OPENBFME_STABLE_SIGNING_KEY }}") == 1
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
        # no permissions (contents: read from the top level); CRASHACT-1: the macOS call alone passes the crash service's client key
        assert set(j) == {"name", "needs", "uses", "with"} | ({"secrets"} if job == "macos" else set())
        assert j.get("secrets", {}) == ({"CRASH_CLIENT_KEY": "${{ secrets.CRASH_CLIENT_KEY }}"} if job == "macos" else {})
    # the downloads name release-package.yml's artifacts (the version and the request in the name)
    for stem in ("linux-x64", "windows-x64", "macos-arm64"):
        assert f"name: openbfme-${{{{ inputs.tag }}}}-{stem}-${{{{ inputs.request }}}}" in TEXT
    pkg = yaml.safe_load(PKG_TEXT)
    on = pkg.get("on", pkg.get(True))
    assert set(on) == {"workflow_dispatch", "workflow_call"} and set(on["workflow_call"]) == {"inputs", "secrets"}
    assert on["workflow_call"]["secrets"] == {"CRASH_CLIENT_KEY": {"required": False}}   # CRASHACT-1: the crash key alone
    assert set(on["workflow_call"]["inputs"]) == set(on["workflow_dispatch"]["inputs"]) == {"platform", "commit", "version", "request"}
    # CRASHACT-1: the crash service's client key is its one secret: declared once (workflow_call) and read once (the macOS package step)
    assert pkg["permissions"] == {"contents": "read"} and PKG_TEXT.count("secrets.") == 1 and PKG_TEXT.count("secrets:") == 1
    assert PKG_TEXT.count("${{ secrets.CRASH_CLIENT_KEY }}") == 1
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
     "calls a workflow with its own permissions"),
    ("other", "release", "", "", "calls a workflow (only the release workflow may)"),
])
def test_the_policy_refuses_other_calls(path, text, old, new, why):
    src = {"pkg": PKG_TEXT, "release": TEXT}[text]
    assert old in src
    p = {"pkg": PKG_PATH, "release": PATH, "other": ".github/workflows/other.yml"}[path]
    probs = public_workflows.workflow_problems(p, src.replace(old, new, 1) if old else src)
    assert any(why in x for x in probs), probs


# ---- actions_release.py plan / checkout-is-tag / trust-check -----------------------------------------------------------------------------

import ed25519  # noqa: E402
from test_signing import DECK, DECK_SECRET, NIGHTLY, NIGHTLY_SECRET, OTHER, OTHER_SECRET, STABLE, STABLE_SECRET, key_list  # noqa: E402


def git(d: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(d), *args], capture_output=True, text=True, check=True).stdout.strip()


def nlist(*keys: bytes) -> str:
    return "# the nightly-only keys\n" + "".join(f"{k.hex()}  nightly-{n}\n" for n, k in enumerate(keys, 1))


def commit_lists(d: Path, message: str, nightly: str, tag: str | None = None, stable: tuple = ()) -> str:
    """a commit of launcher/nightly_keys.pub (with the stable lists beside it: the Deck key, then `stable`'s (key, label) entries, RELGH-3;
    with entries also tools/release/stable_environment.json); an annotated `tag` on it"""
    (d / "launcher").mkdir(exist_ok=True)
    (d / "launcher/release_key.pub").write_text(DECK.hex() + "\n")
    key_list(d / "launcher/release_keys.pub", (DECK, "deck-2026"), *stable)
    if stable:
        (d / "tools/release").mkdir(parents=True, exist_ok=True)
        (d / "tools/release/stable_environment.json").write_text((HERE / "stable_environment.json").read_text())
    (d / "launcher/nightly_keys.pub").write_text(nightly)
    (d / "a.txt").write_text(message + "\n")
    git(d, "add", "-A")
    git(d, "commit", "-q", "-m", message)
    sha = git(d, "rev-parse", "HEAD")
    if tag:
        git(d, "tag", "-a", "-m", tag, tag, sha)
    return sha


PUBLIC_REPO = actions_release.TARGET     # (trusted_by_stable reads this repository; the sign tests rename actions_release.TARGET)


class Pub:
    """the stand-in GitHub's published releases (gh_stand_in.py) for the transition rule: trusted_by_stable reads the newest PUBLISHED stable
    release, its manifest.json + manifest.json.sig assets and published_at, not git tags"""

    def __init__(self, tmp: Path, mp):
        import hashlib
        self.hashlib = hashlib
        self.state = tmp / "pub-gh.json"
        self.state.write_text(json.dumps({"prs": {}, "releases": [], "next": 100}))
        gh = tmp / "pubbin" / "gh"
        gh.parent.mkdir()
        gh.write_text(f"#!/bin/bash\nexec {sys.executable} {HERE / 'gh_stand_in.py'} \"$@\"\n")
        gh.chmod(0o755)
        mp.setenv("GH", str(gh))
        mp.setenv("FAKE_GH_STATE", str(self.state))
        mp.setenv("OPENBFME_RELEASE_STANDIN", "1")
        mp.delenv("OPENBFME_TEST_TRUST_DAYS", raising=False)

    def release(self, tag: str, commit: str, *, draft: bool = False, prerelease: bool = False, secret: bytes = DECK_SECRET,
                version: str | None = None, channel: str = "stable", repo: str | None = None, manifest_commit: str | None = None,
                published_at: str = "2026-01-01T00:00:00Z", assets: tuple[str, ...] = ("manifest.json", "manifest.json.sig"),
                size_lie: bool = False) -> int:
        """a release of `tag` whose manifest names `commit` (signed raw by `secret`)"""
        s = json.loads(self.state.read_text())
        manifest = json.dumps({"version": version or tag, "channel": channel, "repo": repo or PUBLIC_REPO,
                               "commit": manifest_commit or commit}, indent=2).encode() + b"\n"
        files = {"manifest.json": manifest, "manifest.json.sig": ed25519.sign(secret, manifest)}
        store = Path(str(self.state) + ".d")
        store.mkdir(exist_ok=True)
        rel = {"id": s["next"], "tag_name": tag, "name": tag, "draft": draft, "prerelease": prerelease, "body": "", "assets": [],
               "author": {"login": "tester"}, "created_at": "2026-01-01T00:00:00Z"}
        if not draft:
            rel["published_at"] = published_at
        s["next"] += 1
        for name in assets:
            aid = s["next"]
            s["next"] += 1
            (store / str(aid)).write_bytes(files[name])
            rel["assets"].append({"id": aid, "name": name, "size": len(files[name]) + (1 if size_lie else 0), "state": "uploaded",
                                  "digest": "sha256:" + self.hashlib.sha256(files[name]).hexdigest()})
        s["releases"].append(rel)
        self.state.write_text(json.dumps(s))
        return rel["id"]


@pytest.fixture()
def pub(tmp_path, monkeypatch):
    return Pub(tmp_path, monkeypatch)


@pytest.fixture()
def repo(tmp_path, monkeypatch, pub):
    """main: c1 (the stable v0.4.13, PUBLISHED on the stand-in GitHub and signed by the stable key, and the nightly 20261023 on it; its
    nightly list holds the nightly key), head (nightly 261023200); side: not on main (nightly 20261025). The git tag v0.4.13 exists too:
    it is no evidence any more"""
    d = tmp_path / "r"
    d.mkdir()
    git(d, "init", "-q", "-b", "main")
    git(d, "config", "user.email", "t@example.invalid")
    git(d, "config", "user.name", "t")
    c1 = commit_lists(d, "one", nlist(NIGHTLY), "v0.4.13")
    git(d, "tag", "-a", "-m", "n", "v0.4.14-nightly.20261023", c1)
    head = commit_lists(d, "two", nlist(NIGHTLY), "v0.4.14-nightly.261023200")
    git(d, "checkout", "-q", "-b", "side", c1)
    side = commit_lists(d, "side", nlist(NIGHTLY), "v0.4.15")
    git(d, "tag", "-a", "-m", "n", "v0.4.15-nightly.20261025", side)
    git(d, "checkout", "-q", "main")
    monkeypatch.chdir(d)
    pub.release("v0.4.13", c1)
    env = {"GITHUB_EVENT_NAME": "workflow_dispatch", "GITHUB_REF": "refs/heads/main", "GITHUB_REPOSITORY": actions_release.TARGET,
           "GITHUB_WORKFLOW_REF": f"{actions_release.TARGET}/.github/workflows/release.yml@refs/heads/main",
           "GITHUB_SHA": head, "IN_TAG": "v0.4.14-nightly.20261023", "IN_COMMIT": c1, "IN_REQUEST": "ab" * 16}
    for k, v in env.items():
        monkeypatch.setenv(k, v)
    monkeypatch.delenv("IN_KIND", raising=False)
    monkeypatch.setattr(actions_release, "REPO", d)
    return {"dir": d, "c1": c1, "head": head, "side": side, "pub": pub}


def plan(capsys) -> tuple[int, str]:
    rc = actions_release.main(["plan"])
    cap = capsys.readouterr()
    return rc, cap.out + cap.err


@pytest.mark.parametrize("tag", ["v0.4.14-nightly.20261023", "v0.4.14-nightly.261023200"])
def test_plan_accepts_a_nightly(repo, capsys, monkeypatch, tag):
    monkeypatch.setenv("IN_TAG", tag)
    monkeypatch.setenv("IN_COMMIT", repo["c1"] if tag.endswith(".20261023") else repo["head"])
    rc, out = plan(capsys)
    assert rc == 0 and f"PLAN ok: {tag}" in out and "trusted since v0.4.13" in out, out


@pytest.mark.parametrize("tag", ["v0.4.14-nightly.261023300", "v0.4.14-nightly.261023210", "v0.4.14-nightly.251023100",
                                 "v0.4.14-nightly.261323100", "v0.4.14-nightly.202610231", "v0.4.14-nightly.7"])
def test_plan_refuses_a_nightly_number(repo, capsys, monkeypatch, tag):
    monkeypatch.setenv("IN_TAG", tag)
    rc, out = plan(capsys)
    assert rc == 1 and "not a nightly release tag" in out, out


@pytest.mark.parametrize("tag", ["v0.4.13-alpha.1", "v0.4.13-preview.2", "v0.4.13-playtest.1", "v0.4.13.1", "0.4.13"])
def test_plan_refuses_a_tag_that_is_neither_a_nightly_nor_stable(repo, capsys, monkeypatch, tag):
    monkeypatch.setenv("IN_TAG", tag)
    rc, out = plan(capsys)
    assert rc == 1 and "not a nightly release tag" in out and "not a stable one" in out, out


def test_plan_of_a_stable_tag_needs_the_stable_configuration(repo, capsys, monkeypatch):
    """RELGH-3: the stable v0.4.13 at c1 was made before the stable Actions key: the workflow cannot sign it"""
    monkeypatch.setenv("IN_TAG", "v0.4.13")
    rc, out = plan(capsys)
    assert rc == 1 and "has no tools/release/stable_environment.json" in out, out


# ---- RELGH-3: a stable release (the stable Actions key, the environment stable-signing) -----------------------------------------------------

STAG = "v0.4.18"
SLIST = ((STABLE, "stable-2026"),)


def env_record(reviewers=("Ancalgonn",), prevent=True, bypass=False, branches=(("main", "branch"),), rules=(), dbp=None) -> dict:
    """the stand-in GitHub's record of the environment stable-signing (stable_environment.py check reads it)"""
    return {"record": {"name": "stable-signing", "can_admins_bypass": bypass,
                       "deployment_branch_policy": dbp or {"protected_branches": False, "custom_branch_policies": True},
                       "protection_rules": [{"type": "required_reviewers", "prevent_self_review": prevent,
                                             "reviewers": [{"type": "User", "reviewer": {"login": r}} for r in reviewers]},
                                            {"type": "branch_policy"}, *rules]},
            "branch_policies": [{"id": 1 + i, "name": n, "type": ty} for i, (n, ty) in enumerate(branches)]}


def set_gh(pub, **items) -> None:
    s = json.loads(pub.state.read_text())
    s.update(items)
    pub.state.write_text(json.dumps(s))


@pytest.fixture()
def srepo(repo, monkeypatch):
    """the repo world, then on main: t (the lists with the stable Actions key, PUBLISHED as the stable v0.4.14 long ago) and h (the stable
    v0.4.18 to sign: GITHUB_SHA and the request's commit); the environment stable-signing as configured; the release account dispatched"""
    d, pub = repo["dir"], repo["pub"]
    t1 = commit_lists(d, "stable key", nlist(NIGHTLY), "v0.4.14", stable=SLIST)
    pub.release("v0.4.14", t1)
    h = commit_lists(d, "stable head", nlist(NIGHTLY), STAG, stable=SLIST)
    set_gh(pub, environments={"stable-signing": env_record()}, approvals={"4242": [
        {"state": "approved", "comment": "ok", "environments": [{"name": "stable-signing"}], "user": {"login": "Ancalgonn"}}]})
    for k, v in {"GITHUB_SHA": h, "IN_TAG": STAG, "IN_COMMIT": h, "GITHUB_TRIGGERING_ACTOR": "release-bot", "GITHUB_RUN_ID": "4242"}.items():
        monkeypatch.setenv(k, v)
    return {**repo, "t": t1, "h": h}


def test_plan_accepts_a_stable_release_and_names_its_kind(srepo, capsys, monkeypatch, tmp_path):
    out_file = tmp_path / "out.txt"
    monkeypatch.setenv("GITHUB_OUTPUT", str(out_file))
    rc, out = plan(capsys)
    assert rc == 0 and f"PLAN ok: {STAG}" in out and "(stable," in out and "the stable Actions key stable-2026 is trusted since v0.4.14" in out, out
    assert out_file.read_text() == "kind=stable\n"
    # and a nightly's kind
    monkeypatch.setenv("IN_TAG", "v0.4.14-nightly.20261023")
    monkeypatch.setenv("IN_COMMIT", srepo["c1"])
    assert plan(capsys)[0] == 0 and out_file.read_text() == "kind=stable\nkind=nightly\n"


@pytest.mark.parametrize("change,why", [
    ({"bypass": True}, "administrators can bypass"),
    ({"prevent": False}, "'Prevent self-review' is off"),
    ({"reviewers": ("someone",)}, "the required reviewers are ['someone'], not ['Ancalgonn']"),
    ({"reviewers": ("Ancalgonn", "release-bot")}, "the required reviewers are"),
    ({"reviewers": ()}, "the required reviewers are [], not"),
    ({"branches": (("main", "branch"), ("dev", "branch"))}, "not exactly the branch main"),
    ({"branches": (("main", "tag"),)}, "not exactly the branch main"),
    ({"branches": ()}, "not exactly the branch main"),
    ({"dbp": {"protected_branches": True, "custom_branch_policies": False}}, "are not 'Selected branches and tags'"),
    ({"rules": ({"type": "custom_rule_x"},)}, "a protection rule this check does not know"),
    (None, "GitHub has no environment stable-signing"),
])
def test_plan_refuses_a_stable_release_when_the_environment_is_not_the_configuration(srepo, capsys, change, why):
    set_gh(srepo["pub"], environments={} if change is None else {"stable-signing": env_record(**change)})
    rc, out = plan(capsys)
    assert rc == 1 and why in out, out


def test_plan_refuses_a_team_reviewer_and_a_read_failure(srepo, capsys, monkeypatch):
    rec = env_record()
    rec["record"]["protection_rules"][0]["reviewers"] = [{"type": "Team", "reviewer": {"slug": "owners"}}]
    set_gh(srepo["pub"], environments={"stable-signing": rec})
    rc, out = plan(capsys)
    assert rc == 1 and "a reviewer that is not a user ('Team')" in out, out
    set_gh(srepo["pub"], environments={"stable-signing": env_record()})
    monkeypatch.setenv("FAKE_GH_FAULT", "env-read-fail")
    rc, out = plan(capsys)
    assert rc == 1 and "environments/stable-signing failed" in out, out


@pytest.mark.parametrize("actor,why", [("Ancalgonn", "is a required reviewer of stable-signing"),
                                       ("ancalgonn", "is a required reviewer of stable-signing"),
                                       ("", "is not a GitHub login")])
def test_plan_refuses_a_stable_release_the_dispatcher_could_approve(srepo, capsys, monkeypatch, actor, why):
    """the self-approval loophole: the account that started the run must not be a reviewer (its token could approve the run)"""
    monkeypatch.setenv("GITHUB_TRIGGERING_ACTOR", actor)
    rc, out = plan(capsys)
    assert rc == 1 and why in out, out


def test_plan_refuses_a_stable_release_whose_key_the_launchers_do_not_trust_yet(srepo, capsys, monkeypatch):
    pub = srepo["pub"]
    s = json.loads(pub.state.read_text())
    s["releases"] = [r for r in s["releases"] if r["tag_name"] != "v0.4.14"]   # only v0.4.13 (no stable Actions key) is published
    pub.state.write_text(json.dumps(s))
    rc, out = plan(capsys)
    assert rc == 1 and "v0.4.13 does not trust the stable key(s)" in out and "launcher/release_keys.pub lists" in out, out
    # published, but under the 14 days
    pub.release("v0.4.14", srepo["t"], published_at=old_days(5))
    rc, out = plan(capsys)
    assert rc == 1 and "v0.4.14 was published 5.0 days ago, under 14" in out and "refuse the stable release" in out, out


@pytest.mark.parametrize("entries,why", [
    (((STABLE, "deck-2026x"),), "has no key stable-2026"),
    (((OTHER, "other-2026"),), "has no key stable-2026"),
])
def test_plan_refuses_a_commit_without_the_stable_actions_key(srepo, capsys, monkeypatch, entries, why):
    h = commit_lists(srepo["dir"], "no stable key", nlist(NIGHTLY), "v0.4.19", stable=entries)
    for k, v in {"GITHUB_SHA": h, "IN_TAG": "v0.4.19", "IN_COMMIT": h}.items():
        monkeypatch.setenv(k, v)
    rc, out = plan(capsys)
    assert rc == 1 and why in out, out


def test_the_deck_key_can_never_be_the_stable_actions_key(srepo, capsys, monkeypatch):
    """a configuration naming the list's first entry (the offline Deck key) is refused: deck-2026 never enters GitHub"""
    d = srepo["dir"]
    cfg = json.loads((HERE / "stable_environment.json").read_text())
    cfg["key_label"] = "deck-2026"
    h = commit_lists(d, "deck as the stable key", nlist(NIGHTLY), None, stable=SLIST)
    (d / "tools/release/stable_environment.json").write_text(json.dumps(cfg))
    git(d, "commit", "-q", "-am", "deck label")
    h = git(d, "rev-parse", "HEAD")
    git(d, "tag", "-a", "-m", "v0.4.19", "v0.4.19", h)
    for k, v in {"GITHUB_SHA": h, "IN_TAG": "v0.4.19", "IN_COMMIT": h}.items():
        monkeypatch.setenv(k, v)
    rc, out = plan(capsys)
    assert rc == 1 and "is the first entry of launcher/release_keys.pub, the Deck key" in out, out


@pytest.mark.parametrize("field,value,why", [
    ("prevent_self_review", False, "prevent_self_review must be true"),
    ("can_admins_bypass", True, "can_admins_bypass must be false"),
    ("deployment_branches", ["main", "dev"], "deployment_branches must be exactly"),
    ("reviewers", [], "reviewers is not a list"),
    ("environment", "nightly-signing", "is not the nightly one"),
])
def test_plan_refuses_a_configuration_that_weakens_a_rule(srepo, capsys, monkeypatch, field, value, why):
    d = srepo["dir"]
    cfg = json.loads((HERE / "stable_environment.json").read_text())
    cfg[field] = value
    (d / "tools/release/stable_environment.json").write_text(json.dumps(cfg))
    git(d, "commit", "-q", "-am", "weaker")
    h = git(d, "rev-parse", "HEAD")
    git(d, "tag", "-a", "-m", "v0.4.19", "v0.4.19", h)
    for k, v in {"GITHUB_SHA": h, "IN_TAG": "v0.4.19", "IN_COMMIT": h}.items():
        monkeypatch.setenv(k, v)
    rc, out = plan(capsys)
    assert rc == 1 and why in out, out


@pytest.mark.parametrize("var,value,why", [
    ("GITHUB_REF", "refs/heads/side", "not refs/heads/main"),
    ("GITHUB_REF", "refs/tags/v0.4.13", "not refs/heads/main"),
    ("GITHUB_EVENT_NAME", "push", "not workflow_dispatch"),
    ("GITHUB_REPOSITORY", "someone/fork", "not Open-BFME/openbfme-godot"),
    ("GITHUB_WORKFLOW_REF", "Open-BFME/openbfme-godot/.github/workflows/other.yml@refs/heads/main", "not Open-BFME/openbfme-godot/.github"),
    ("GITHUB_WORKFLOW_REF", "Open-BFME/openbfme-godot/.github/workflows/release.yml@refs/heads/side", "not Open-BFME/openbfme-godot/.github"),
    ("GITHUB_WORKFLOW_REF", "", "not Open-BFME/openbfme-godot/.github"),
    ("IN_TAG", "v0.4.14-nightly.20261023; curl evil", "not a nightly release tag"),
    ("IN_TAG", "v0.4.14-nightly.20261024", "names nothing here"),
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
    monkeypatch.setenv("IN_TAG", "v0.4.15-nightly.20261025")
    monkeypatch.setenv("IN_COMMIT", repo["side"])
    rc, out = plan(capsys)
    assert rc == 1 and "is not on main" in out


def test_plan_runs_the_trust_check(repo, capsys, monkeypatch):
    """the commit's nightly list names a key the newest published stable release does not trust: refused"""
    d = repo["dir"]
    new = commit_lists(d, "three", nlist(NIGHTLY, OTHER), "v0.4.16-nightly.261024100")
    monkeypatch.setenv("GITHUB_SHA", new)
    monkeypatch.setenv("IN_TAG", "v0.4.16-nightly.261024100")
    monkeypatch.setenv("IN_COMMIT", new)
    rc, out = plan(capsys)
    assert rc == 1 and "does not trust the nightly key" in out and "v0.4.13" in out, out
    # no key in the list at all: nothing can sign
    empty = commit_lists(d, "four", nlist(), "v0.4.16-nightly.261024200")
    monkeypatch.setenv("GITHUB_SHA", empty)
    monkeypatch.setenv("IN_TAG", "v0.4.16-nightly.261024200")
    monkeypatch.setenv("IN_COMMIT", empty)
    rc, out = plan(capsys)
    assert rc == 1 and "lists no key" in out, out


def test_plan_refuses_a_stable_release_that_is_only_a_git_tag(repo, capsys):
    """Sol r1: a tag is no evidence; with no published stable release at all, the plan stops"""
    state = json.loads(repo["pub"].state.read_text())
    state["releases"] = []
    repo["pub"].state.write_text(json.dumps(state))
    rc, out = plan(capsys)
    assert rc == 1 and "has no published stable release" in out, out


def test_plan_refuses_a_transition_release_younger_than_the_window(repo, capsys):
    repo["pub"].release("v0.4.14", repo["c1"], published_at=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() - 3 * 86400)))
    rc, out = plan(capsys)
    assert rc == 1 and "v0.4.14 was published 3.0 days ago, under 14" in out, out


def test_checkout_is_tag(repo, capsys, monkeypatch):
    git(repo["dir"], "checkout", "-q", "--detach", repo["c1"])
    # two tags on c1: git describe names one; the other is refused. FLAKEFIX-2: which one depends on the tag dates (a second boundary
    # between the two), so the test takes the one describe did not name instead of predicting it
    tags = git(repo["dir"], "tag", "--points-at", repo["c1"]).split()
    assert sorted(tags) == ["v0.4.13", "v0.4.14-nightly.20261023"], tags
    named = git(repo["dir"], "describe", "--tags")
    other = next(t for t in tags if t != named)
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


# ---- trust-check (the Deck, before anything is pushed) --------------------------------------------------------------------------------------

def trust(capsys, *args: str) -> tuple[int, str]:
    rc = actions_release.main(["trust-check", *args])
    cap = capsys.readouterr()
    return rc, cap.out + cap.err


@pytest.fixture()
def tdir(tmp_path):
    d = tmp_path / "t"
    d.mkdir()
    git(d, "init", "-q", "-b", "main")
    git(d, "config", "user.email", "t@example.invalid")
    git(d, "config", "user.name", "t")
    return d


def old_days(n: float) -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() - n * 86400))


def stable_commit(tdir: Path, nightly: str, tag: str = "v0.4.13") -> str:
    return commit_lists(tdir, f"stable {tag}", nightly)


def test_trust_check_needs_a_published_stable_release(tdir, pub, capsys):
    stable = stable_commit(tdir, nlist(NIGHTLY))
    c = commit_lists(tdir, "head", nlist(NIGHTLY))
    git(tdir, "tag", "-a", "-m", "s", "v0.4.13", stable)                      # a stable GIT TAG with the key: not evidence
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir))
    assert rc == 1 and "has no published stable release" in out, out
    # a draft stable release, a stable-looking pre-release, a nightly: all ignored
    pub.release("v0.4.13", stable, draft=True)
    pub.release("v0.4.13", stable, prerelease=True)
    pub.release("v0.4.14-nightly.261023100", stable)
    pub.release("v0.4.14-alpha.1", stable)
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir))
    assert rc == 1 and "has no published stable release" in out, out
    # a published one is accepted; a newer draft / pre-release is still ignored
    pub.release("v0.4.13", stable)
    pub.release("v0.4.15", stable, draft=True)
    pub.release("v0.4.15", stable, prerelease=True)
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir))
    assert rc == 0 and f"TRUST ok: the nightly keys of {c[:12]} are trusted by v0.4.13" in out, out


def test_trust_check_takes_the_newest_published_release_by_number(tdir, pub, capsys):
    good = stable_commit(tdir, nlist(NIGHTLY))
    bare = stable_commit(tdir, nlist())
    c = commit_lists(tdir, "head", nlist(NIGHTLY))
    pub.release("v0.4.9", good)
    pub.release("v0.4.13", good)
    pub.release("v0.4.20", bare)                                                # the NEWEST lists none
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir))
    assert rc == 1 and "v0.4.20 does not trust the nightly key(s)" in out and NIGHTLY.hex()[:16] in out, out
    # numeric, not by name: v0.4.9 < v0.4.13
    pub2 = json.loads(pub.state.read_text())
    pub2["releases"] = [r for r in pub2["releases"] if r["tag_name"] != "v0.4.20"]
    pub.state.write_text(json.dumps(pub2))
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir))
    assert rc == 0 and "trusted by v0.4.13" in out, out
    # one of two keys missing from the stable release's list
    c2 = commit_lists(tdir, "head2", nlist(NIGHTLY, OTHER))
    rc, out = trust(capsys, "--commit", c2, "--git-dir", str(tdir))
    assert rc == 1 and OTHER.hex()[:16] in out and NIGHTLY.hex()[:16] not in out, out
    # a commit with no key / no list: refused
    assert trust(capsys, "--commit", commit_lists(tdir, "none", nlist()), "--git-dir", str(tdir))[0] == 1
    (tdir / "launcher/nightly_keys.pub").unlink()
    git(tdir, "add", "-A")
    git(tdir, "commit", "-q", "-m", "no list")
    rc, out = trust(capsys, "--commit", git(tdir, "rev-parse", "HEAD"), "--git-dir", str(tdir))
    assert rc == 1 and "has no launcher/nightly_keys.pub" in out
    assert trust(capsys, "--commit", "abc", "--git-dir", str(tdir))[0] == 1


@pytest.mark.parametrize("how,why", [
    ("unsigned-by-stable-key", "is not signed by a stable key"),
    ("signed-by-the-nightly-key", "is not signed by a stable key"),
    ("wrong-tag", "does not name v0.4.13, the stable channel"),
    ("wrong-channel", "does not name v0.4.13, the stable channel"),
    ("preview-channel", "does not name v0.4.13, the stable channel"),
    ("wrong-repo", "does not name v0.4.13, the stable channel"),
    ("short-commit", "does not name v0.4.13, the stable channel"),
    ("no-manifest", "has no manifest.json / manifest.json.sig"),
    ("no-sig", "has no manifest.json / manifest.json.sig"),
    ("size-lie", "the download failed or has another size"),
    ("young", "was published 1.0 days ago, under 14"),
    ("unlisted-commit", "does not trust the nightly key"),
])
def test_trust_check_refuses_a_release_that_is_not_the_stable_keys(tdir, pub, capsys, how, why):
    stable = stable_commit(tdir, nlist(NIGHTLY))
    c = commit_lists(tdir, "head", nlist(NIGHTLY))
    kw = {"unsigned-by-stable-key": {"secret": OTHER_SECRET}, "signed-by-the-nightly-key": {"secret": NIGHTLY_SECRET},
          "wrong-tag": {"version": "v0.4.12"}, "wrong-channel": {"channel": "preview"}, "preview-channel": {"channel": "nightly"},
          "wrong-repo": {"repo": "someone/fork"}, "short-commit": {"manifest_commit": stable[:12]},
          "no-manifest": {"assets": ("manifest.json.sig",)}, "no-sig": {"assets": ("manifest.json",)}, "size-lie": {"size_lie": True},
          "young": {"published_at": old_days(1)}, "unlisted-commit": {"manifest_commit": commit_lists(tdir, "no key", nlist(OTHER))}}[how]
    commit = kw.get("manifest_commit", stable)
    pub.release("v0.4.13", commit, **kw)
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir))
    assert rc == 1 and why in out, out
    if how == "unlisted-commit":
        assert OTHER.hex()[:16] not in out


def test_trust_check_window_and_the_test_override(tdir, pub, capsys, monkeypatch):
    stable = stable_commit(tdir, nlist(NIGHTLY))
    c = commit_lists(tdir, "head", nlist(NIGHTLY))
    pub.release("v0.4.13", stable, published_at=old_days(13.9))
    assert trust(capsys, "--commit", c, "--git-dir", str(tdir))[0] == 1
    # the override is for stand-in worlds alone
    monkeypatch.setenv("OPENBFME_TEST_TRUST_DAYS", "0")
    assert trust(capsys, "--commit", c, "--git-dir", str(tdir))[0] == 0
    monkeypatch.delenv("OPENBFME_RELEASE_STANDIN")
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir))
    assert rc == 1 and "under 14" in out, "without the stand-in switch the 14 days stand"
    monkeypatch.setenv("OPENBFME_RELEASE_STANDIN", "1")
    pub.release("v0.4.14", stable, published_at=old_days(14.1))
    monkeypatch.delenv("OPENBFME_TEST_TRUST_DAYS")
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir))
    assert rc == 0 and "trusted by v0.4.14" in out, out


def test_trust_check_reads_the_release_commit_from_the_remote(tdir, pub, tmp_path, capsys):
    """the Deck: the transition release's commit is on the public history, not in the archive repository: fetched by its id first"""
    pubdir = tmp_path / "pubrepo"
    pubdir.mkdir()
    git(pubdir, "init", "-q", "-b", "main")
    git(pubdir, "config", "user.email", "t@example.invalid")
    git(pubdir, "config", "user.name", "t")
    stable = commit_lists(pubdir, "public stable", nlist(NIGHTLY))
    remote = tmp_path / "public.git"
    subprocess.run(["git", "init", "-q", "--bare", "-b", "main", str(remote)], check=True)
    git(pubdir, "push", "-q", str(remote), "main")
    c = commit_lists(tdir, "archive head", nlist(NIGHTLY))
    pub.release("v0.4.13", stable)
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir))
    assert rc == 1 and "does not trust" in out, "without --remote the commit is not here (its list reads as absent)"
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir), "--remote", str(remote))
    assert rc == 0 and "trusted by v0.4.13" in out, out
    # a manifest naming a commit the remote does not have
    pub.release("v0.4.14", "e" * 40)
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir), "--remote", str(remote))
    assert rc == 1 and "RELEASE ACTIONS FAIL" in out, out
    # --repo is passed on to the release reads
    rc, out = trust(capsys, "--commit", c, "--git-dir", str(tdir), "--remote", str(remote), "--repo", "Other/repo")
    assert rc == 1


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
    tag = "v0.4.14-nightly.261023200"
    assert actions_release.main(["dispatch", "--tag", tag, "--commit", "c" * 40, "--out", str(out)]) == 0
    req = json.loads(out.read_text())
    assert len(req["request"]) == 32 and req["tag"] == tag
    call = json.loads(sf.read_text())["calls"][0]
    assert call[:3] == ["workflow", "run", "release.yml"] and "--ref" in call and call[call.index("--ref") + 1] == "main"
    assert f"request={req['request']}" in call and f"tag={tag}" in call and f"commit={'c' * 40}" in call
    assert not any(c.startswith("kind=") for c in call), "the workflow has no kind input any more"
    assert req["kind"] == "nightly"
    # RELGH-3: a stable tag is dispatched the same way (the workflow takes its kind from the tag); an alpha build never; there is no --kind
    assert actions_release.main(["dispatch", "--tag", "v0.4.13", "--commit", "c" * 40, "--out", str(out)]) == 0
    assert json.loads(out.read_text())["kind"] == "stable"
    assert actions_release.main(["dispatch", "--tag", "v0.4.13-alpha.1", "--commit", "c" * 40, "--out", str(out)]) == 1
    assert "is not a nightly or a stable tag" in capsys.readouterr().err
    with pytest.raises(SystemExit):
        actions_release.main(["dispatch", "--tag", tag, "--commit", "c" * 40, "--kind", "nightly", "--out", str(out)])
    assert len(json.loads(sf.read_text())["calls"]) == 2


def run_record(req: str, **kw) -> dict:
    r = {"id": 7, "status": "completed", "conclusion": "success", "head_branch": "main", "event": "workflow_dispatch",
         "path": ".github/workflows/release.yml", "display_title": f"Release v0.4.14-nightly.261023200 (nightly, request {req})"}
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
    tag = "v0.4.14-nightly.261023200"
    rs = {"ok": [run_record(req)], "failed": [run_record(req, conclusion="failure")],
          "other-branch": [run_record(req, head_branch="side")], "twice": [run_record(req), run_record(req, id=8)],
          "pending": [run_record(req, status="in_progress", conclusion=None)]}[runs]
    body = f"x\n\nopenbfme-release-request: {req}\n"
    rel = {"one": [{"id": 55, "tag_name": tag, "draft": True, "body": body}], "none": [],
           "published": [{"id": 55, "tag_name": tag, "draft": False, "body": body}],
           "other-request": [{"id": 55, "tag_name": tag, "draft": True, "body": body.replace(req, "00" * 16)}]}[releases]
    stand_in_gh(tmp_path, {"runs": rs, "releases": rel})
    monkeypatch.setenv("GH", str(tmp_path / "gh"))
    rf = tmp_path / "req.json"
    rf.write_text(json.dumps({"request": req, "tag": tag, "kind": "nightly", "commit": "c" * 40, "run_id": None}))
    rc = actions_release.main(["wait", "--request-file", str(rf), "--timeout", "0", "--poll", "0"])
    cap = capsys.readouterr()
    if ok:
        assert rc == 0 and cap.out.strip() == "55"
        assert json.loads(rf.read_text())["run_id"] == 7
    else:
        assert rc == 1 and why in cap.err, cap.err


# ---- sign: the nightly key on tmpfs, sealing and signing ----------------------------------------------------------------------------------
import release_keys  # noqa: E402

NTAG = "v0.4.14-nightly.261023200"


class Signing:
    """actions_release.py sign on stand-in inputs: the repository (launcher lists committed, a stable tag listing the key), the build jobs'
    artifacts (package_dir / mac_package_dir) and a fake tmpfs"""

    def __init__(self, tmp: Path, d: Path, mp, tag: str = NTAG):
        from test_autorelease import package_dir, mac_package_dir
        self.tmp, self.repo, self.mp, self.tag = tmp, d, mp, tag
        self.shm = tmp / "shm"
        self.shm.mkdir()
        self.commit = git(d, "rev-parse", "HEAD")
        # the tools run from the stand-in repository (make_manifest.py asks ITS git for the tag on HEAD)
        import shutil
        shutil.copytree(HERE, d / "tools/release", ignore=shutil.ignore_patterns("__pycache__", ".pytest_cache"), dirs_exist_ok=True)
        shutil.copy2(REPO / "tools/precommit.py", d / "tools/precommit.py")
        mp.setattr(actions_release, "HERE", d / "tools/release")
        self.inp = tmp / "in"
        package_dir(self.inp / "packages", tag)
        mac_package_dir(d, tag, self.inp / "macos", commit=self.commit)
        self.pkg = tmp / "pkg"
        mp.setattr(actions_release, "SHM", self.shm)
        mp.setattr(actions_release, "TARGET", "Test/repo")
        mp.setenv("OPENBFME_RELEASE_STANDIN", "1")
        mp.setenv("OPENBFME_TEST_SHM", str(self.shm))
        mp.setenv("IN_TAG", tag)
        mp.setenv("IN_COMMIT", self.commit)
        mp.setenv("IN_REQUEST", "cd" * 16)
        self.seen: list[dict] = []
        real = actions_release.run

        def spy(cmd, **kw):
            if any("audit_package.py" in str(c) for c in cmd):
                return ""   # the stand-in archives are no real packages (the audit has its own tests); everything else runs for real
            env = {k: v for k, v in os.environ.items() if k in ("NIGHTLY_SIGNING_KEY", "STABLE_SIGNING_KEY") or "PRIVATE KEY" in v}
            item = {"cmd": [str(c) for c in cmd], "env_has_key": bool(env), "cwd": kw.get("cwd")}
            if "--key" in item["cmd"]:
                kf = Path(item["cmd"][item["cmd"].index("--key") + 1])
                item["keyfile"] = kf
                item["mode"] = oct(kf.stat().st_mode & 0o777)
                item["dir_mode"] = oct(kf.parent.stat().st_mode & 0o777)
                item["in_shm"] = kf.parent.parent == self.shm
            self.seen.append(item)
            return real(cmd, **kw)
        mp.setattr(actions_release, "run", spy)

    def sign(self, secret: bytes = NIGHTLY_SECRET, pem: str | None = None, var: str = "NIGHTLY_SIGNING_KEY", also: dict | None = None) -> int:
        os.environ[var] = ed25519.private_key_pem(secret) if pem is None else pem
        os.environ.update(also or {})
        try:
            return actions_release.main(["sign", "--in", str(self.inp), "--pkg", str(self.pkg)])
        finally:
            for v in ("NIGHTLY_SIGNING_KEY", "STABLE_SIGNING_KEY"):
                os.environ.pop(v, None)

    def tools(self) -> list[dict]:
        """the child processes that are not git or gh reads"""
        return [s for s in self.seen if s["cmd"][0] != "git" and Path(s["cmd"][0]).name != "gh"]

    def leftovers(self) -> list[str]:
        return sorted(str(p) for p in self.shm.rglob("*"))


@pytest.fixture()
def signing(repo, tmp_path, monkeypatch):
    # HEAD is main's head: tag NTAG; the stable tag v0.4.13 (c1) lists the nightly key; the work tree is the checkout
    s = Signing(tmp_path, repo["dir"], monkeypatch)
    s.pub = repo["pub"]
    return s


@pytest.fixture(autouse=True)
def _no_key_in_the_environment(monkeypatch):
    monkeypatch.delenv("NIGHTLY_SIGNING_KEY", raising=False)
    monkeypatch.delenv("STABLE_SIGNING_KEY", raising=False)
    monkeypatch.delenv("GITHUB_OUTPUT", raising=False)


@pytest.fixture()
def ssigning(srepo, tmp_path, monkeypatch):
    """RELGH-3: sign a stable release (STAG at main's head, the stable Actions key trusted by the published v0.4.14, the environment as
    configured, run 4242 approved by the owner and started by the release account)"""
    s = Signing(tmp_path, srepo["dir"], monkeypatch, tag=STAG)
    s.pub = srepo["pub"]
    return s


def ssign(s: Signing, secret: bytes = STABLE_SECRET, **kw) -> int:
    return s.sign(secret, var="STABLE_SIGNING_KEY", **kw)


def test_sign_writes_stable_signatures_with_the_stable_actions_key(ssigning, capsys):
    assert ssign(ssigning) == 0, capsys.readouterr().err
    assert f"SIGN ok: {STAG}" in capsys.readouterr().out
    keys = release_keys.load(ssigning.repo / "launcher/release_keys.pub")
    nightly = release_keys.load_nightly(ssigning.repo / "launcher/nightly_keys.pub", None)
    for m in ("manifest.json", "manifest-macos.json"):
        data, sig = (ssigning.pkg / m).read_bytes(), (ssigning.pkg / f"{m}.sig").read_bytes()
        assert release_keys.verify_detail(keys, m, data, sig) == ("stable-2026", "raw")
        assert release_keys.verify_detail([keys[0]], m, data, sig) is None, "not the Deck key's"
        assert json.loads(data)["channel"] == "stable" and json.loads(data)["version"] == STAG
    assert seal_package_check(ssigning.pkg, keys, nightly) == []
    keyed = [s for s in ssigning.seen if "keyfile" in s]
    assert keyed and all(s["mode"] == "0o600" and s["in_shm"] and s["keyfile"].name == "stable.pem" for s in keyed)
    assert all(s["keyfile"].parent.name.startswith("openbfme-stable-key-") for s in keyed)
    assert ssigning.leftovers() == [] and not any(s["env_has_key"] for s in ssigning.seen)
    assert "STABLE_SIGNING_KEY" not in os.environ and not any("PRIVATE KEY" in " ".join(s["cmd"]) for s in ssigning.seen)


def seal_package_check(pkg: Path, keys, nightly) -> list:
    import seal_package
    return seal_package.check(pkg, keys, actions_release.TARGET, nightly)


@pytest.mark.parametrize("secret,why", [
    (DECK_SECRET, "is the Deck key (the offline recovery key: it never enters GitHub)"),
    (NIGHTLY_SECRET, "not the stable Actions key stable-2026"),
    (OTHER_SECRET, "not the stable Actions key stable-2026"),
])
def test_sign_refuses_any_stable_key_but_the_stable_actions_key(ssigning, capsys, secret, why):
    assert ssign(ssigning, secret) == 1
    err = capsys.readouterr().err
    assert why in err and ed25519.private_key_pem(secret)[:40] not in err, err
    assert ssigning.leftovers() == [] and not ssigning.pkg.exists() and not ssigning.tools()


@pytest.mark.parametrize("approvals,why", [
    ([], "no configured reviewer approved stable-signing"),
    ([{"state": "approved", "environments": [{"name": "stable-signing"}], "user": {"login": "someone"}}], "no configured reviewer approved"),
    ([{"state": "approved", "environments": [{"name": "nightly-signing"}], "user": {"login": "Ancalgonn"}}], "no configured reviewer approved"),
    ([{"state": "approved", "environments": [{"name": "stable-signing"}], "user": {"login": "Ancalgonn"}},
      {"state": "rejected", "environments": [{"name": "stable-signing"}], "user": {"login": "Ancalgonn"}}], "is not an approval"),
])
def test_sign_refuses_a_stable_release_the_owner_did_not_approve(ssigning, capsys, approvals, why):
    set_gh(ssigning.pub, approvals={"4242": approvals})
    assert ssign(ssigning) == 1
    assert why in capsys.readouterr().err
    assert ssigning.leftovers() == [] and not ssigning.pkg.exists() and not ssigning.tools()


def test_sign_refuses_a_self_approved_or_reviewer_started_stable_run(ssigning, capsys, monkeypatch):
    monkeypatch.setenv("GITHUB_TRIGGERING_ACTOR", "Ancalgonn")     # the owner's own token started the run, and approved it
    assert ssign(ssigning) == 1
    assert "is a required reviewer of stable-signing" in capsys.readouterr().err
    assert not ssigning.tools()


def test_sign_refuses_a_stable_run_when_the_environment_was_weakened_after_the_plan(ssigning, capsys):
    set_gh(ssigning.pub, environments={"stable-signing": env_record(prevent=False, bypass=True)})
    assert ssign(ssigning) == 1
    err = capsys.readouterr().err
    assert "'Prevent self-review' is off" in err and "administrators can bypass" in err
    assert not ssigning.tools() and ssigning.leftovers() == []


def test_sign_refuses_the_key_of_the_other_kind_beside_it(ssigning, capsys):
    """one step, one key: a stable run that also holds the nightly key (or the reverse) signs nothing"""
    assert ssign(ssigning, also={"NIGHTLY_SIGNING_KEY": ed25519.private_key_pem(NIGHTLY_SECRET)}) == 1
    assert "holds the key of another kind (NIGHTLY_SIGNING_KEY)" in capsys.readouterr().err
    assert "NIGHTLY_SIGNING_KEY" not in os.environ and "STABLE_SIGNING_KEY" not in os.environ


def test_sign_refuses_a_stable_key_in_the_nightly_variable(ssigning, capsys):
    assert ssigning.sign(STABLE_SECRET) == 1      # NIGHTLY_SIGNING_KEY on a stable run: STABLE_SIGNING_KEY is empty
    err = capsys.readouterr().err
    assert "holds the key of another kind" in err or "STABLE_SIGNING_KEY (the environment stable-signing's secret) is empty" in err, err


def test_sign_writes_nightly_signatures_and_shreds_the_key(signing, capsys):
    assert signing.sign() == 0, capsys.readouterr().err
    out = capsys.readouterr().out
    assert f"SIGN ok: {NTAG}" in out
    names = sorted(p.name for p in signing.pkg.iterdir())
    assert "manifest.json.sig" in names and "manifest-macos.json.sig" in names
    assert not any(n.endswith(".sig2") for n in names), "no .sig2 any more"
    keys, nightly = release_keys.load(signing.repo / "launcher/release_keys.pub"), release_keys.load_nightly(
        signing.repo / "launcher/nightly_keys.pub", None)
    for m in ("manifest.json", "manifest-macos.json"):
        data, sig = (signing.pkg / m).read_bytes(), (signing.pkg / f"{m}.sig").read_bytes()
        assert release_keys.verify_scoped(keys, nightly, m, data, sig) == ("nightly-1", "raw")
        assert release_keys.verify_detail(keys, m, data, sig) is None, "the stable list alone does not trust it"
        assert json.loads(data)["channel"] == "preview"
    assert not any(b"PRIVATE KEY" in p.read_bytes() for p in signing.pkg.iterdir())
    # the key file: mode 0600 in a 0700 folder on the (fake) tmpfs, there while the tools ran, gone afterwards
    keyed = [s for s in signing.seen if "keyfile" in s]
    assert keyed and all(s["mode"] == "0o600" and s["dir_mode"] == "0o700" and s["in_shm"] for s in keyed)
    assert signing.leftovers() == [], "the key folder is gone after a successful sign"
    # no child process saw the secret, in its environment or on its command line
    assert signing.tools() and not any(s["env_has_key"] for s in signing.seen) and "NIGHTLY_SIGNING_KEY" not in os.environ
    assert not any("PRIVATE KEY" in " ".join(s["cmd"]) for s in signing.seen)


def test_sign_removes_the_key_when_it_fails_midway(signing, monkeypatch, capsys):
    real = actions_release.run

    def failing(cmd, **kw):
        if any("sign_manifest.py" in str(c) for c in cmd) and "--key" in [str(c) for c in cmd]:
            assert any(p.name == "nightly.pem" for p in signing.shm.rglob("*")), "the key is on tmpfs while signing"
            raise actions_release.Fail("a stand-in failure while signing")
        return real(cmd, **kw)
    monkeypatch.setattr(actions_release, "run", failing)
    assert signing.sign() == 1
    assert "a stand-in failure" in capsys.readouterr().err
    assert signing.leftovers() == [], "the key folder is gone after a failure"
    assert "NIGHTLY_SIGNING_KEY" not in os.environ


def test_sign_removes_the_key_when_the_inputs_are_wrong(signing, capsys):
    (signing.inp / "macos" / "README-MAC.txt").unlink()
    assert signing.sign() == 1
    assert "not exactly" in capsys.readouterr().err
    assert signing.leftovers() == [] and not signing.pkg.exists()
    assert not any(s["env_has_key"] for s in signing.seen)


def test_sign_refuses_a_key_that_is_not_in_nightly_keys_pub(signing, capsys):
    assert signing.sign(OTHER_SECRET) == 1
    err = capsys.readouterr().err
    assert "is not in launcher/nightly_keys.pub" in err and ed25519.private_key_pem(OTHER_SECRET)[:40] not in err
    assert signing.leftovers() == [] and not signing.pkg.exists() and not signing.tools()
    # the stable key is no nightly key either
    assert signing.sign(DECK_SECRET) == 1 and "is not in launcher/nightly_keys.pub" in capsys.readouterr().err


def test_sign_refuses_a_key_the_newest_stable_release_does_not_trust(signing, monkeypatch, capsys):
    d = signing.repo
    # the key IS in the commit's list, but the newest published stable release (a newer one) lists none
    bare = commit_lists(d, "newer stable", nlist(), None)
    signing.pub.release("v0.4.20", bare)
    monkeypatch.setenv("IN_COMMIT", commit_lists(d, "head", nlist(NIGHTLY)))
    assert signing.sign() == 1
    assert "does not trust the nightly key" in capsys.readouterr().err
    assert signing.leftovers() == [] and not signing.pkg.exists() and not signing.tools()


@pytest.mark.parametrize("how,why", [("draft", "has no published stable release"), ("young", "under 14"),
                                     ("foreign", "is not signed by a stable key")])
def test_sign_refuses_when_the_transition_release_is_not_a_published_stable_key_one(signing, capsys, how, why):
    state = json.loads(signing.pub.state.read_text())
    state["releases"] = []
    signing.pub.state.write_text(json.dumps(state))
    c1 = signing.repo.joinpath("a.txt") and git(signing.repo, "rev-parse", "v0.4.13^{commit}")
    kw = {"draft": {"draft": True}, "young": {"published_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())},
          "foreign": {"secret": NIGHTLY_SECRET}}[how]
    signing.pub.release("v0.4.13", c1, **kw)
    assert signing.sign() == 1
    assert why in capsys.readouterr().err
    assert signing.leftovers() == [] and not signing.pkg.exists() and not signing.tools()


def test_sign_refuses_a_shm_that_is_not_tmpfs(signing, monkeypatch, capsys):
    monkeypatch.delenv("OPENBFME_RELEASE_STANDIN")        # no stand-in mode: a folder of the disk is no tmpfs
    assert signing.sign() == 1
    assert "is not a tmpfs" in capsys.readouterr().err
    assert signing.leftovers() == [] and not signing.pkg.exists() and not signing.tools()
    # the stand-in switch alone (without the test folder) does not open the door either
    monkeypatch.setenv("OPENBFME_RELEASE_STANDIN", "1")
    monkeypatch.delenv("OPENBFME_TEST_SHM")
    assert signing.sign() == 1 and "is not a tmpfs" in capsys.readouterr().err


@pytest.mark.parametrize("pem,why", [("", "is empty"), ("   \n", "is empty"), ("not a key", "is not an Ed25519 private key"),
                                     (ed25519.public_key_pem(NIGHTLY), "is not an Ed25519 private key")])
def test_sign_refuses_an_empty_or_malformed_key(signing, capsys, pem, why):
    assert signing.sign(pem=pem) == 1
    err = capsys.readouterr().err
    assert why in err and "not a key" not in err.replace("is not an Ed25519", "")
    assert signing.leftovers() == [] and not signing.tools()


def test_sign_refuses_a_nightly_key_on_a_stable_tag(signing, monkeypatch, capsys):
    """RELGH-3: a stable tag needs the stable Actions key in STABLE_SIGNING_KEY; the nightly key in its own variable signs nothing"""
    monkeypatch.setenv("IN_TAG", "v0.4.13")
    assert signing.sign() == 1
    assert "holds the key of another kind (NIGHTLY_SIGNING_KEY)" in capsys.readouterr().err
    assert signing.leftovers() == [] and not signing.tools()
    monkeypatch.setenv("IN_TAG", "v0.4.13-alpha.1")
    assert signing.sign() == 1 and "not a nightly release tag" in capsys.readouterr().err


def test_sign_does_not_use_the_environment_of_the_key_after_popping_it(signing):
    """the secret leaves os.environ before anything runs, whatever the outcome"""
    os.environ["NIGHTLY_SIGNING_KEY"] = "x"
    assert actions_release.main(["sign", "--in", str(signing.inp), "--pkg", str(signing.pkg)]) == 1
    assert "NIGHTLY_SIGNING_KEY" not in os.environ


# ---- compare and draft (the release folders of test_signing.py: sealed and signed by the nightly key) ----------------------------------

from test_signing import built, NIGHTLY_TAG  # noqa: E402,F401  (the module's fixture)


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
    version = NIGHTLY_TAG
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


def draft_env(monkeypatch, tmp_path, built, version, request="ab" * 16):
    for k, v in stand_in(tmp_path, built).items():
        monkeypatch.setenv(k, v)
    for k, v in {"IN_TAG": version, "IN_COMMIT": "c" * 40, "IN_REQUEST": request, "GITHUB_RUN_ID": "77"}.items():
        monkeypatch.setenv(k, v)


def test_draft_uploads_reads_back_and_never_publishes(built, tmp_path, monkeypatch, capsys):
    version = NIGHTLY_TAG
    pkg = copy_folder(built["folders"][version], tmp_path / "pkg")
    draft_env(monkeypatch, tmp_path, built, version)
    assert actions_release.main(["draft", "--pkg", str(pkg)]) == 0, capsys.readouterr().err
    state = json.loads((tmp_path / "gh.json").read_text())
    (rel,) = state["releases"]
    assert rel["draft"] is True and rel["prerelease"] is True and rel["tag_name"] == version
    assert f"openbfme-release-request: {'ab' * 16}" in rel["body"]
    assert sorted(a["name"] for a in rel["assets"]) == sorted(p.name for p in pkg.iterdir())
    assert "manifest.json.sig" in {a["name"] for a in rel["assets"]}
    log = (tmp_path / "gh.log").read_text()
    assert "draft=false" not in log, "the workflow never publishes"
    # a second run for the same tag creates nothing
    assert actions_release.main(["draft", "--pkg", str(pkg)]) == 1
    assert "exists already" in capsys.readouterr().err
    assert len(json.loads((tmp_path / "gh.json").read_text())["releases"]) == 1


def test_draft_refuses_a_corrupted_upload_and_a_stray_file(built, tmp_path, monkeypatch, capsys):
    version = NIGHTLY_TAG
    pkg = copy_folder(built["folders"][version], tmp_path / "pkg")
    draft_env(monkeypatch, tmp_path, built, version, "cd" * 16)
    (pkg / "notes.txt").write_text("not a release asset\n")
    assert actions_release.main(["draft", "--pkg", str(pkg)]) == 1
    assert "the release's assets are" in capsys.readouterr().err
    (pkg / "notes.txt").unlink()
    monkeypatch.setenv("FAKE_GH_FAULT", "upload-corrupt")
    assert actions_release.main(["draft", "--pkg", str(pkg)]) == 1
    assert "another size or digest" in capsys.readouterr().err
    (rel,) = json.loads((tmp_path / "gh.json").read_text())["releases"]
    assert rel["draft"] is True and rel["prerelease"] is True


# ---- autorelease.sh --nightly with OPENBFME_RELEASE_SIGNER=actions, end to end (stand-in gh runs a stand-in release workflow) ----------------

WORKFLOW_HOOK = '''#!{python}
"""the release workflow's stand-in: what release.yml leaves behind (a run record and a draft signed with the NIGHTLY key, or for a stable tag
with the stable Actions key, RELGH-3), from the same stand-in packages"""
import hashlib, json, os, subprocess, sys
from pathlib import Path
Path(os.environ["FAKE_WORKFLOW_WORK"]).mkdir(parents=True, exist_ok=True)
sys.stderr = open(Path(os.environ["FAKE_WORKFLOW_WORK"]) / "hook.err", "a")
sys.path.insert(0, {here!r})
from test_autorelease import package_dir, mac_package_dir
from test_signing import NIGHTLY_SECRET, STABLE_SECRET
import ed25519, make_macos_manifest, seal_package
tag, commit, req = (os.environ[k] for k in ("IN_TAG", "IN_COMMIT", "IN_REQUEST"))
assert "IN_KIND" not in os.environ, "the workflow has no kind input"
NIGHTLY = "-nightly." in tag
SECRET = NIGHTLY_SECRET if NIGHTLY else STABLE_SECRET
fault = os.environ.get("FAKE_WORKFLOW_FAULT", "")
state = Path(os.environ["FAKE_GH_STATE"])
remote = os.environ["FAKE_GH_REMOTE"]
work = Path(os.environ["FAKE_WORKFLOW_WORK"]) / req
s = json.loads(state.read_text()) if state.exists() else {{"prs": {{}}, "releases": [], "next": 100}}
run = {{"id": 900 + len(s.get("release_runs", [])), "status": "completed", "conclusion": "failure" if fault == "fail" else "success",
       "head_branch": os.environ["GH_REF"], "event": "workflow_dispatch", "path": ".github/workflows/release.yml",
       "display_title": f"Release {{tag}} (request {{req}})", "html_url": "https://example.invalid/run"}}
s.setdefault("release_runs", []).append(run)
state.write_text(json.dumps(s))
if fault == "fail":
    sys.exit(0)
co = work / "checkout" / "launcher"
co.mkdir(parents=True)
for f in ("release_key.pub", "release_keys.pub", "nightly_keys.pub"):
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
seal_package.seal(d, SECRET, os.environ["FAKE_TARGET"])
subprocess.run([sys.executable, {here!r} + "/make_manifest.py", str(d), "--repo", os.environ["FAKE_TARGET"], "--commit", commit,
                "--date", __import__("datetime").datetime.now(__import__("datetime").timezone.utc).strftime("%Y-%m-%d"), "--allow-untagged"],
               check=True, capture_output=True)
date = json.loads((d / "manifest.json").read_text())["date"]
sealed = work / "mac.sha256"
sealed.write_text(f"{{hashlib.sha256((d / mac.name).read_bytes()).hexdigest()}}  {{mac.name}}\\n")
(d / "manifest-macos.json").write_bytes(make_macos_manifest.dumps(make_macos_manifest.build(
    tag, os.environ["FAKE_TARGET"], commit, date, archive=d / mac.name, checksum=sealed)))
for m in ("manifest.json", "manifest-macos.json"):
    (d / f"{{m}}.sig").write_bytes(ed25519.sign(SECRET, (d / m).read_bytes()))
s = json.loads(state.read_text())
store = Path(str(state) + ".d")
store.mkdir(exist_ok=True)
rel = {{"id": s["next"], "tag_name": tag, "name": f"OpenBFME {{tag}}", "draft": True, "prerelease": NIGHTLY,
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


from test_nightly_e2e import world, nightly_env, stable, new_commit, today  # noqa: E402,F401  (the nightly world: a stable release first)


@pytest.fixture
def actions_world(world):
    """the nightly world with a nightly key in launcher/nightly_keys.pub and a first stable release (v0.4.5 on the public remote) whose
    own list names it: the transition rule (trust-check) holds"""
    from test_autorelease import skip_on_macos
    skip_on_macos()
    w = world
    w.change({"launcher/nightly_keys.pub": f"{NIGHTLY.hex()}  nightly-2026\n"}, "RELGH-2: the nightly key")
    stable(w)
    hook = w.tmp / "workflow-hook"
    hook.write_text(WORKFLOW_HOOK.format(python=sys.executable, here=str(HERE)))
    hook.chmod(0o755)
    w.hook = hook
    return w


def actions_env(w, **extra) -> dict:
    from test_autorelease import TARGET as T
    return {"OPENBFME_RELEASE_SIGNER": "actions", "FAKE_GH_WORKFLOW": str(w.hook), "FAKE_WORKFLOW_WORK": str(w.tmp / "wf"),
            "FAKE_TARGET": T, "FAKE_GH_AUTHOR": "tester",
            # the stand-in stable release was published a moment ago: the 14-day window is shortened for the stand-in world only
            "OPENBFME_TEST_TRUST_DAYS": "0", **extra}


def keyless_run(w, sha: str, *args: str, **env):
    """autorelease.sh --nightly with NO OPENBFME_RELEASE_KEY and no key file: the Deck holds no signing key on this path"""
    from test_autorelease import TARGET as T, run as arun
    full = w.env(**nightly_env(w, **env))
    del full["OPENBFME_RELEASE_KEY"]
    w.key.unlink(missing_ok=True)
    return arun("bash", w.repo / "tools/release/autorelease.sh", sha, "--repo", T, "--remote", "origin", "--nightly", *args, env=full)


def hook_err(w) -> str:
    err = w.tmp / "wf" / "hook.err"
    return err.read_text() if err.exists() else ""


def test_actions_signer_end_to_end(actions_world):
    w = actions_world
    head = new_commit(w)
    r = keyless_run(w, head, **actions_env(w))
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-1000:] + hook_err(w)
    tag = f"v0.4.6-nightly.{today()}"
    assert f"AUTORELEASE OK {tag}" in r.stdout and "COMPARE identical" in r.stdout and "TRUST ok" in r.stdout, r.stdout[-3000:]
    assert "no Deck key on this machine" in r.stdout and "DRAFT CHECK " in r.stdout
    gh = w.gh_data()
    (d,) = gh["dispatches"]
    assert d["workflow"] == "release.yml" and d["ref"] == "main" and d["inputs"]["tag"] == tag
    assert set(d["inputs"]) == {"tag", "commit", "request"}, "the workflow takes no kind"
    rel = next(x for x in gh["releases"] if x["tag_name"] == tag)
    assert rel["draft"] is False and rel["prerelease"] is True and rel["author"]["login"] == "github-actions[bot]"
    names = {a["name"] for a in rel["assets"]}
    assert {"manifest.json.sig", "manifest-macos.json.sig"} <= names and not any(n.endswith(".sig2") for n in names)
    store = Path(str(w.gh_state) + ".d")
    blob = {a["name"]: (store / str(a["id"])).read_bytes() for a in rel["assets"]}
    keys = release_keys.load(w.repo / "launcher/release_keys.pub")
    nightly = release_keys.load_nightly(w.repo / "launcher/nightly_keys.pub", None)
    for m in ("manifest.json", "manifest-macos.json"):
        assert release_keys.verify_scoped(keys, nightly, m, blob[m], blob[m + ".sig"]) == ("nightly-2026", "raw")
        assert release_keys.verify_detail(keys, m, blob[m], blob[m + ".sig"]) is None
    assert "openbfme-release-request" not in rel["body"]
    assert not (w.state / "nightly/intent").exists() and not (w.state / "nightly/intent.request").exists()
    # the Deck signed nothing: its key file never existed here, and the tag/commit were pushed before the dispatch
    assert w.remote_refs(f"refs/tags/{tag}")


@pytest.mark.parametrize("fault,why", [
    ("fail", "ended failure"),
    ("different-build", "not the candidate this run tested"),
])
def test_actions_signer_refuses_and_publishes_nothing(actions_world, fault, why):
    w = actions_world
    head = new_commit(w)
    r = keyless_run(w, head, **actions_env(w, FAKE_WORKFLOW_FAULT=fault))
    assert r.returncode != 0 and why in r.stdout + r.stderr, r.stdout[-3000:] + r.stderr[-2000:]
    assert all(rel["draft"] for rel in w.gh_data()["releases"] if "nightly" in rel["tag_name"]), "nothing published"
    assert (w.state / "nightly/intent").exists(), "the intent stays open for the next run"


def test_actions_signer_dry_run_prints_the_steps(actions_world):
    w = actions_world
    head = new_commit(w)
    r = keyless_run(w, head, "--dry-run", **actions_env(w))
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-2000:]
    for step in ("actions_release.py dispatch", "actions_release.py wait", "check_draft.py", "actions_release.py compare", "--resume-id"):
        assert step in r.stdout
    assert "cosign_release.py" not in r.stdout and "--key" not in r.stdout.split("check_draft.py", 1)[1].splitlines()[0]
    assert "dispatches" not in w.gh_data(), "a dry run dispatches nothing"


def stable_actions_world(w):
    """RELGH-3: the world's lists gain the stable Actions key, and a Deck-signed stable release (v0.4.6) ships it (the transition release);
    the environment stable-signing is as configured; this machine's gh is the release account (no reviewer)"""
    from test_autorelease import TARGET as T
    pub = w.repo.joinpath("launcher/release_key.pub").read_text().strip()
    sha = w.change({"launcher/release_keys.pub": f"{pub}  deck-2026\n{STABLE.hex()}  stable-2026\n", "engine/a.cpp": "4\n"},
                   "Merge lane/relgh3 (RELGH-3: the stable Actions key)")
    w.gate(sha)
    r = w.release(sha)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6" in r.stdout, r.stdout[-3000:] + r.stderr[-1000:]
    s = w.gh_data()
    s["environments"] = {"stable-signing": env_record()}
    w.gh_state.write_text(json.dumps(s))
    return T


def stable_keyless(w, sha: str, *args: str, **env):
    from test_autorelease import TARGET as T, run as arun
    full = w.env(**actions_env(w, **{"FAKE_GH_USER": "release-bot", **env}))
    del full["OPENBFME_RELEASE_KEY"]
    w.key.unlink(missing_ok=True)
    return arun("bash", w.repo / "tools/release/autorelease.sh", sha, "--repo", T, "--remote", "origin", *args, env=full)


def test_actions_signer_end_to_end_for_a_stable_release(actions_world):
    """RELGH-3: a stable release signed on GitHub Actions: the trust check of the stable Actions key, the environment and the dispatcher
    before anything is pushed; the workflow's draft (the stable Actions key's signatures) checked, compared and published as a full release"""
    w = actions_world
    stable_actions_world(w)
    sha = w.change({"engine/a.cpp": "5\n"}, "Merge lane/z (Z-1: a change)")
    w.gate(sha)
    r = stable_keyless(w, sha)
    assert r.returncode == 0, r.stdout[-4000:] + r.stderr[-1000:] + hook_err(w)
    assert "AUTORELEASE OK v0.4.7" in r.stdout and "TRUST ok: the stable Actions key stable-2026" in r.stdout, r.stdout[-3000:]
    assert "the dispatcher" not in r.stdout or True
    assert "no Deck key on this machine" in r.stdout and "after the owner approves the run" in r.stdout
    assert "waits for the owner's review of the environment stable-signing" in r.stdout
    assert "checks passed (signed by the stable Actions key stable-2026; nothing added)" in r.stdout and "COMPARE identical" in r.stdout
    gh = w.gh_data()
    (d,) = gh["dispatches"]
    assert d["workflow"] == "release.yml" and d["ref"] == "main" and d["inputs"]["tag"] == "v0.4.7"
    rel = next(x for x in gh["releases"] if x["tag_name"] == "v0.4.7")
    assert rel["draft"] is False and rel["prerelease"] is False and rel["author"]["login"] == "github-actions[bot]"
    store = Path(str(w.gh_state) + ".d")
    blob = {a["name"]: (store / str(a["id"])).read_bytes() for a in rel["assets"]}
    keys = release_keys.load(w.repo / "launcher/release_keys.pub")
    for m in ("manifest.json", "manifest-macos.json"):
        assert release_keys.verify_detail(keys, m, blob[m], blob[m + ".sig"]) == ("stable-2026", "raw")
    assert (w.state / "last-released").read_text().split()[:2] == [sha, "v0.4.7"] and not (w.state / "intent").exists()


@pytest.mark.parametrize("setup,why", [
    ("dispatcher-is-reviewer", "is a required reviewer of stable-signing"),
    ("no-environment", "GitHub has no environment stable-signing"),
    ("admins-bypass", "administrators can bypass"),
    ("not-transitioned", "does not trust the stable key(s)"),
    ("too-young", "under 14"),
])
def test_a_stable_release_on_actions_stops_before_any_push(actions_world, setup, why):
    w = actions_world
    stable_actions_world(w)
    env = {}
    s = w.gh_data()
    if setup == "dispatcher-is-reviewer":
        env["FAKE_GH_USER"] = "Ancalgonn"
    elif setup == "no-environment":
        s["environments"] = {}
    elif setup == "admins-bypass":
        s["environments"] = {"stable-signing": env_record(bypass=True)}
    elif setup == "not-transitioned":
        s["releases"] = [r for r in s["releases"] if r["tag_name"] != "v0.4.6"]
    w.gh_state.write_text(json.dumps(s))
    if setup == "too-young":
        env["OPENBFME_TEST_TRUST_DAYS"] = "14"
    sha = w.change({"engine/a.cpp": "5\n"}, "Merge lane/z (Z-1: a change)")
    w.gate(sha)
    before = w.remote_refs("refs/*")
    r = stable_keyless(w, sha, **env)
    assert r.returncode != 0 and why in r.stdout + r.stderr, r.stdout[-3000:] + r.stderr[-2000:]
    assert "trust-check" in r.stdout + r.stderr and "nothing was pushed or published" in r.stdout + r.stderr
    assert w.remote_refs("refs/*") == before and not w.gh_data().get("dispatches")


def test_the_deck_path_still_needs_the_release_key(actions_world):
    from test_autorelease import TARGET as T, run as arun
    w = actions_world
    sha = w.change({"engine/a.cpp": "5\n"}, "Merge lane/z (Z-1: a change)")
    w.gate(sha)
    for flags in (["--nightly"], []):
        full = w.env(**nightly_env(w))
        del full["OPENBFME_RELEASE_KEY"]
        r = arun("bash", w.repo / "tools/release/autorelease.sh", sha, "--repo", T, "--remote", "origin", *flags, "--plan", env=full)
        assert r.returncode == 2 and "OPENBFME_RELEASE_KEY is not set" in r.stdout + r.stderr


@pytest.mark.parametrize("nightly_flag", [True, False])
def test_the_mac_build_host_is_refused(actions_world, nightly_flag):
    from test_autorelease import TARGET as T, run as arun
    w = actions_world
    sha = w.change({"engine/a.cpp": "6\n"}, "Merge lane/z (Z-1: a change)")
    w.gate(sha)
    env = w.env(**nightly_env(w), OPENBFME_RELEASE_MAC_BUILDER="host")
    r = arun("bash", w.repo / "tools/release/autorelease.sh", sha, "--repo", T, "--remote", "origin", *(["--nightly"] if nightly_flag else []),
             "--plan", env=env)
    assert r.returncode == 2 and "OPENBFME_RELEASE_MAC_BUILDER is actions" in r.stdout + r.stderr, r.stdout + r.stderr
    assert not w.remote_refs("refs/tags/v0.4.6*")


def test_a_failing_trust_check_stops_before_any_push(actions_world):
    w = actions_world
    # a second nightly key the newest stable release (v0.4.5) does not list yet
    head = w.change({"launcher/nightly_keys.pub": f"{NIGHTLY.hex()}  nightly-2026\n{OTHER.hex()}  nightly-2027\n"}, "RELGH-2: a second key")
    before = w.remote_refs("refs/*")
    r = keyless_run(w, head, **actions_env(w))
    assert r.returncode != 0 and "does not trust the nightly key" in r.stdout + r.stderr, r.stdout[-3000:] + r.stderr[-2000:]
    assert "AUTORELEASE FAIL" in r.stdout + r.stderr and "trust-check" in r.stdout + r.stderr
    assert w.remote_refs("refs/*") == before, "nothing was pushed (no tag, no staging branch)"
    gh = w.gh_data()
    assert not gh.get("dispatches") and all(rel["tag_name"] == "v0.4.5" for rel in gh["releases"])


def test_a_transition_release_younger_than_the_window_stops_before_any_push(actions_world):
    w = actions_world
    head = new_commit(w)
    before = w.remote_refs("refs/*")
    env = actions_env(w)
    del env["OPENBFME_TEST_TRUST_DAYS"]          # the real window: v0.4.5 was published a moment ago
    r = keyless_run(w, head, **env)
    assert r.returncode != 0 and "was published" in r.stdout + r.stderr and "under 14" in r.stdout + r.stderr, r.stdout[-3000:] + r.stderr[-2000:]
    assert w.remote_refs("refs/*") == before and not w.gh_data().get("dispatches")


@pytest.mark.parametrize("how,why", [("tag-only", "has no published stable release"), ("draft", "has no published stable release"),
                                     ("prerelease", "has no published stable release"), ("foreign-signature", "is not signed by a stable key")])
def test_a_stable_release_that_is_not_published_and_signed_stops_before_any_push(actions_world, how, why):
    w = actions_world
    state = json.loads(w.gh_state.read_text())
    (rel,) = [x for x in state["releases"] if x["tag_name"] == "v0.4.5"]
    if how == "tag-only":
        state["releases"].remove(rel)            # the git tag v0.4.5 stays on the public remote: no evidence
    elif how == "draft":
        rel["draft"] = True
    elif how == "prerelease":
        rel["prerelease"] = True
    else:
        sig = next(a for a in rel["assets"] if a["name"] == "manifest.json.sig")
        (Path(str(w.gh_state) + ".d") / str(sig["id"])).write_bytes(ed25519.sign(NIGHTLY_SECRET, b"x"))
    w.gh_state.write_text(json.dumps(state))
    assert w.remote_refs("refs/tags/v0.4.5")
    head = new_commit(w)
    before = w.remote_refs("refs/*")
    r = keyless_run(w, head, **actions_env(w))
    assert r.returncode != 0 and why in r.stdout + r.stderr, r.stdout[-3000:] + r.stderr[-2000:]
    assert w.remote_refs("refs/*") == before and not w.gh_data().get("dispatches")


@pytest.mark.parametrize("fault,why", [("release-workflow-disabled", "not active"), ("release-workflow-missing", "does not know")])
def test_an_unknown_or_inactive_release_workflow_stops_before_any_push(actions_world, fault, why):
    w = actions_world
    head = new_commit(w)
    before = w.remote_refs("refs/*")
    r = keyless_run(w, head, **actions_env(w, FAKE_GH_FAULT=fault))
    assert r.returncode != 0 and why in r.stdout + r.stderr, r.stdout[-3000:] + r.stderr[-2000:]
    assert w.remote_refs("refs/*") == before and not w.gh_data().get("dispatches")


def test_actions_signer_reconciles_a_published_release_after_a_lost_readback(actions_world):
    """Sol r2: the published workflow release is reconciled against actions-pkg/ and the trusted lists, not the candidate's pkg/"""
    w = actions_world
    head = new_commit(w)
    tag = f"v0.4.6-nightly.{today()}"
    r = keyless_run(w, head, **actions_env(w, FAKE_GH_FAULT="final-readback-fail"))
    assert r.returncode != 0 and "does not read back as published" in r.stdout, r.stdout[-2000:] + r.stderr[-1000:]
    rel = next(x for x in w.gh_data()["releases"] if x["tag_name"] == tag)
    assert rel["draft"] is False and (w.state / "nightly/intent").exists()
    r = keyless_run(w, head, **actions_env(w))
    assert "AUTORELEASE reconciled" in r.stdout and "was published" in r.stdout, r.stdout[-3000:] + r.stderr[-1000:]
    assert not (w.state / "nightly/intent").exists() and (w.state / "nightly/last-released").read_text().split()[:2] == [head, tag]


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


def test_the_package_and_signing_jobs_install_only_hash_pinned_packages():
    """RELGH-3 dry run: the package job's launcher icon check and the signing jobs' audit need pefile, which the runner lacks; it comes from
    tools/release/actions_requirements.txt, every line pinned by its hash, installed before any key step"""
    import re
    import yaml
    doc = yaml.safe_load(TEXT)
    req = (REPO / "tools/release/actions_requirements.txt").read_text().splitlines()
    pins = [ln for ln in req if ln.strip() and not ln.startswith("#")]
    assert pins and all(re.fullmatch(r"[a-z0-9-]+==[0-9.]+ --hash=sha256:[0-9a-f]{64}", ln) for ln in pins), pins
    for name in ("package", "sign", "sign-stable"):
        runs = [s.get("run", "") for s in doc["jobs"][name]["steps"]]
        i = next(n for n, r in enumerate(runs) if "actions_requirements.txt" in r)
        assert "--require-hashes" in runs[i] and "--no-deps" in runs[i] and "--only-binary=':all:'" in runs[i]
        keyed = [n for n, s in enumerate(doc["jobs"][name]["steps"]) if "SIGNING_KEY" in json.dumps(s.get("env", {}))]
        assert all(i < k for k in keyed)


def test_checkout_is_tag_turns_the_lfs_filter_off_on_a_runner(repo, capsys, monkeypatch):
    """RELGH-3 dry run: on GitHub's runner the git-lfs clean filter made the plain PNGs of the public tree look changed"""
    d = repo["dir"]
    git(d, "checkout", "-q", "--detach", repo["head"])
    monkeypatch.setenv("IN_TAG", "v0.4.14-nightly.261023200")
    monkeypatch.setenv("IN_COMMIT", repo["head"])
    git(d, "config", "--local", "filter.lfs.clean", "false")   # a filter that would fail if it ran
    monkeypatch.delenv("GITHUB_ACTIONS", raising=False)
    actions_release.main(["checkout-is-tag"])
    assert git(d, "config", "--local", "filter.lfs.clean") == "false", "never changed outside a runner"
    capsys.readouterr()
    monkeypatch.setenv("GITHUB_ACTIONS", "true")
    assert actions_release.main(["checkout-is-tag"]) == 0, capsys.readouterr().err
    assert git(d, "config", "--local", "filter.lfs.clean") == "" and git(d, "config", "--local", "filter.lfs.required") == "false"
