"""Lane MACACT-1: the release platforms built on GitHub Actions (autorelease.sh's OPENBFME_RELEASE_MAC_BUILDER / _LINUX_BUILDER /
_WINDOWS_BUILDER=actions, actions_build.py, .github/workflows/release-package.yml). The releases run as in test_autorelease.py: throwaway
repositories, a local bare repository as the public remote (the staging branch is pushed there), gh_stand_in.py as gh (its Actions runs
build with the stand-in at once), a throwaway key. Never the real repository or GitHub."""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import actions_build  # noqa: E402
import public_workflows  # noqa: E402
from test_autorelease import World, check_mac_release, git, pr_merge  # noqa: E402

WORKFLOW = REPO / ".github/workflows/release-package.yml"


@pytest.fixture(scope="session")
def world_template(tmp_path_factory):
    # the World's archive carries the workflow GitHub builds with (actions_build.py stage refuses a commit without it)
    return World(tmp_path_factory.mktemp("actions-world"))


@pytest.fixture
def world(tmp_path, world_template):
    return world_template.copy(tmp_path)


ALL = {"OPENBFME_RELEASE_MAC_BUILDER": "actions", "OPENBFME_RELEASE_LINUX_BUILDER": "actions", "OPENBFME_RELEASE_WINDOWS_BUILDER": "actions"}


def gh_state(world) -> dict:
    return world.gh_data()


def nothing_published(world) -> bool:
    """no release, no tag, no open intent, the public main where it was, and no staging branch left"""
    gh = gh_state(world)
    return (gh.get("releases", []) == [] and world.remote_refs("refs/tags/*") == "" and not (world.state / "intent").exists()
            and git(world.remote, "rev-parse", "refs/heads/main") == world.seed_main and world.remote_refs("refs/heads/staging/*") == "")


def built(world) -> list[dict]:
    return [json.loads(x) for x in world.actions_log.read_text().splitlines()] if world.actions_log.exists() else []


def released(world, **env):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    return world.release(sha, "--since", world.first, **env)


def test_the_mac_package_built_on_actions_is_released(world):
    # Sol r1: an operator's push.followTags must not carry the candidate's local tag along with the staging branch
    git(world.repo, "config", "push.followTags", "true")
    r = released(world, OPENBFME_RELEASE_MAC_BUILDER="actions")
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    public = pr_merge(world)
    check_mac_release(world, world.tmp / "work/v0.4.5/pkg", "v0.4.5", public)
    (rel,) = gh_state(world)["releases"]
    assert "openbfme-v0.4.5-macos-arm64.zip" in {a["name"] for a in rel["assets"]}
    # GitHub built the exact public commit that was then published, while its tag was still on the Deck only
    (b,) = built(world)
    assert b == {"platform": "macos", "version": "v0.4.5", "head": public, "remote_tag": ""}
    (run,) = gh_state(world)["runs"].values()
    assert run["head_sha"] == public and run["head_branch"] == "staging/v0.4.5" and run["_inputs"]["commit"] == public
    assert re.fullmatch(r"[0-9a-f]{32}", run["_inputs"]["request"])
    assert world.remote_refs("refs/heads/staging/*") == "", "the staging branch is deleted after the build"
    assert f"ACTIONS BUILD OK macos v0.4.5 {public}" in r.stdout
    assert f"AUTORELEASE GitHub Actions builds of v0.4.5: macos https://github.com/Test/repo/actions/runs/{run['id']}" in r.stdout
    log = world.gh_log.read_text()
    assert log.index("actions/workflows/release-package.yml/dispatches") < log.index("releases"), "built before anything is published"


def test_all_three_platforms_on_actions_and_the_libraries_compared(world):
    r = released(world, **ALL)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert sorted(b["platform"] for b in built(world)) == ["linux", "macos", "windows"]
    assert "AUTORELEASE actions-compare: openbfme.linux.template_debug.x86_64.so of the build host is byte for byte GitHub Actions' build" in r.stdout
    assert ("AUTORELEASE actions-compare: openbfme.windows.template_debug.x86_64.dll of the build host is GitHub Actions' build byte for "
            "byte but for the CodeView GUID") in r.stdout
    assert world.remote_refs("refs/heads/staging/*") == ""
    assert len(gh_state(world)["runs"]) == 3


@pytest.mark.parametrize("platform,lib", [("LINUX", "openbfme.linux.template_debug.x86_64.so"),
                                          ("WINDOWS", "openbfme.windows.template_debug.x86_64.dll")])
def test_a_github_library_that_differs_from_the_hosts_fails_the_release(world, platform, lib):
    r = released(world, **{f"OPENBFME_RELEASE_{platform}_BUILDER": "actions"}, STANDIN_ACTIONS_FAULT="lib-differs")
    assert r.returncode == 1, r.stdout + r.stderr
    assert f"AUTORELEASE FAIL at step actions-compare (v0.4.5): the build host's {lib}" in r.stdout
    assert "is not GitHub Actions' build of the same commit" in r.stdout
    assert nothing_published(world)


@pytest.mark.parametrize("fault,why", [
    ("actions-run-fail", "concluded failure"),
    ("actions-wrong-sha", "the run built 0000000000000000000000000000000000000000, not the candidate"),
    ("actions-wrong-actor", "the run's actor is not the release account tester"),
    ("actions-rerun", "attempt 2 (a re-run)"),
    ("actions-wrong-title", "the run's title is"),
    ("actions-two-artifacts", "has 2 artifacts, not exactly one"),
    ("actions-wrong-artifact-name", "is not 'openbfme-v0.4.5-macos-arm64-"),
    ("actions-digest-mismatch", "not the artifact's digest"),
    ("actions-partial", "the artifact is not exactly the package files (missing ['README-MAC.txt']"),
    ("actions-extra-file", "extra ['notes.txt']"),
    ("actions-folder-entry", "extra ['sub/']"),
    ("actions-job-extra", "only 'macOS arm64 package' may run"),
    ("actions-no-run-id", "GitHub did not return the dispatched run's id"),
    ("actions-dispatch-fail", "dispatching the macos build on GitHub Actions failed"),
])
def test_a_wrong_or_failed_github_build_fails_the_release(world, fault, why):
    """no fallback to the Mac host, nothing published, the staging branch deleted (also when the failure is in the middle)"""
    r = released(world, OPENBFME_RELEASE_MAC_BUILDER="actions", FAKE_GH_FAULT=fault)
    assert r.returncode == 1, r.stdout + r.stderr
    assert why in r.stdout + r.stderr, r.stdout + r.stderr
    assert re.search(r"AUTORELEASE FAIL at step (mac-build|actions-dispatch) \(v0\.4\.5\)", r.stdout), r.stdout
    assert nothing_published(world), fault
    assert not (world.tmp / "work/v0.4.5/pkg/manifest-macos.json").exists(), "nothing was signed"


def test_a_run_that_never_finishes_times_out_and_is_cancelled(world):
    r = released(world, OPENBFME_RELEASE_MAC_BUILDER="actions", FAKE_GH_FAULT="actions-hang", OPENBFME_RELEASE_ACTIONS_TIMEOUT="1")
    assert r.returncode == 1 and "did not finish within 1 s (cancelled)" in r.stdout, r.stdout + r.stderr
    (run,) = gh_state(world)["runs"].values()
    assert (run["status"], run["conclusion"]) == ("completed", "cancelled")
    assert nothing_published(world)


def test_a_failure_elsewhere_cancels_the_github_build_and_deletes_the_staging_branch(world):
    r = released(world, OPENBFME_RELEASE_MAC_BUILDER="actions", FAKE_GH_FAULT="actions-hang", STANDIN_BUILD_FAIL="1")
    assert r.returncode == 1 and "AUTORELEASE FAIL at step build" in r.stdout, r.stdout + r.stderr
    (run,) = gh_state(world)["runs"].values()
    assert run["conclusion"] == "cancelled" and "cancelled" in r.stdout
    assert nothing_published(world)


def test_a_dispatch_whose_answer_is_lost_is_found_by_its_request_and_cancelled(world):
    """Sol r1: GitHub made the run but the answer never came: the release fails and the cleanup finds the run by the request id that was
    recorded before the dispatch, and cancels it"""
    r = released(world, OPENBFME_RELEASE_MAC_BUILDER="actions", FAKE_GH_FAULT="actions-dispatch-lost,actions-hang")
    assert r.returncode == 1 and "AUTORELEASE FAIL at step actions-dispatch" in r.stdout, r.stdout + r.stderr
    (run,) = gh_state(world)["runs"].values()
    assert (run["status"], run["conclusion"]) == ("completed", "cancelled"), run
    assert nothing_published(world)


def test_a_bad_mac_package_from_actions_is_refused_like_a_host_one(world):
    r = released(world, OPENBFME_RELEASE_MAC_BUILDER="actions", STANDIN_MAC_FAULT="wrong-key")
    assert r.returncode == 1 and "AUTORELEASE FAIL at step deck-verify" in r.stdout, r.stdout + r.stderr
    assert "the Mac launcher's build_info.json has release_key" in r.stdout + r.stderr
    assert nothing_published(world)


def test_an_unknown_workflow_stops_before_anything_is_pushed(world):
    r = released(world, OPENBFME_RELEASE_MAC_BUILDER="actions", FAKE_GH_FAULT="actions-workflow-missing")
    assert r.returncode == 1 and "AUTORELEASE FAIL at step actions-check" in r.stdout, r.stdout + r.stderr
    assert nothing_published(world) and "runs" not in gh_state(world)
    assert "dispatches" not in world.gh_log.read_text()


def test_a_leftover_staging_branch_is_replaced(world):
    seed = git(world.remote, "rev-parse", "refs/heads/main")
    git(world.remote, "update-ref", "refs/heads/staging/v0.4.5", seed)
    r = released(world, OPENBFME_RELEASE_MAC_BUILDER="actions")
    assert r.returncode == 0 and "replacing a leftover refs/heads/staging/v0.4.5" in r.stdout, r.stdout + r.stderr
    assert built(world)[0]["head"] == pr_merge(world) and world.remote_refs("refs/heads/staging/*") == ""


def test_the_dry_run_pushes_only_the_staging_branch_and_deletes_it(world):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: something new for players to see")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, "--dry-run", OPENBFME_RELEASE_MAC_BUILDER="actions")
    assert r.returncode == 0 and "AUTORELEASE DRY RUN OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert "the staging branch staging/v0.4.5 is pushed for real" in r.stdout
    (b,) = built(world)
    assert b["platform"] == "macos" and b["remote_tag"] == ""
    assert nothing_published(world)


@pytest.mark.parametrize("var", ["OPENBFME_RELEASE_MAC_BUILDER", "OPENBFME_RELEASE_LINUX_BUILDER", "OPENBFME_RELEASE_WINDOWS_BUILDER"])
def test_the_builder_is_host_or_actions(world, var):
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1")
    world.gate(sha)
    r = world.release(sha, "--since", world.first, **{var: "github"})
    if var == "OPENBFME_RELEASE_MAC_BUILDER":   # RELGH-2: any value but actions is refused
        assert r.returncode == 2 and f"{var} is actions" in r.stderr
    else:
        assert r.returncode == 2 and f"{var} is host or actions" in r.stderr
    assert nothing_published(world)


def test_skip_mac_wins_over_the_mac_builder(world):
    r = released(world, OPENBFME_RELEASE_MAC_BUILDER="actions", OPENBFME_RELEASE_SKIP_MAC="1")
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert not built(world) and "dispatches" not in world.gh_log.read_text()


def test_the_default_is_actions_for_the_mac(world):
    """RELGH-2: the variable unset builds macOS on the Actions stand-in; Linux and Windows stay on the build host"""
    r = released(world)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.5" in r.stdout, r.stdout + r.stderr
    assert [b["platform"] for b in built(world)] == ["macos"] and "actions/workflows/release-package.yml/dispatches" in world.gh_log.read_text()


def test_the_mac_build_host_is_refused(world):
    """RELGH-2: MAC_BUILDER=host is exit 2 with nothing pushed (no staging branch, no tag, no build)"""
    r = released(world, OPENBFME_RELEASE_MAC_BUILDER="host")
    assert r.returncode == 2 and "OPENBFME_RELEASE_MAC_BUILDER is actions" in r.stderr, r.stdout + r.stderr
    assert nothing_published(world) and not built(world)


def test_unstage_refuses_a_staging_branch_someone_moved(world, tmp_path):
    seed = git(world.remote, "rev-parse", "refs/heads/main")
    other = git(world.remote, "commit-tree", f"{seed}^{{tree}}", "-p", seed, "-m", "someone else's")
    git(world.remote, "update-ref", "refs/heads/staging/v0.4.5", other)
    args = ["--git-dir", str(world.repo), "--repo", "Test/repo", "--remote", "origin", "--gh", str(world.gh), "unstage",
            "--commit", seed, "--version", "v0.4.5"]
    with pytest.raises(SystemExit) as e:
        actions_build.main(args)
    assert "someone moved it" in str(e.value)
    assert world.remote_refs("refs/heads/staging/v0.4.5").split()[0] == other


def test_stage_refuses_a_commit_without_the_workflow(world):
    seed = git(world.remote, "rev-parse", "refs/heads/main")
    git(world.repo, "fetch", "-q", "origin", "main")
    with pytest.raises(SystemExit) as e:
        actions_build.main(["--git-dir", str(world.repo), "--repo", "Test/repo", "--remote", "origin", "--gh", str(world.gh), "stage",
                            "--commit", seed, "--version", "v0.4.5"])
    assert "has no .github/workflows/release-package.yml" in str(e.value)
    assert world.remote_refs("refs/heads/staging/*") == ""


# ---- the workflow itself ---------------------------------------------------------------------------------------------------------------
def test_the_workflow_passes_the_public_policy_and_is_dispatch_or_release_call_only():
    text = WORKFLOW.read_text()
    assert public_workflows.workflow_problems(".github/workflows/release-package.yml", text) == []
    import yaml
    doc = yaml.safe_load(text)
    # RELSIGN-1: workflow_call too, for release.yml's builds (test_release_workflow.py: the policy allows it in this file alone)
    assert sorted(doc[True]) == ["workflow_call", "workflow_dispatch"], "no push / pull_request trigger"
    assert doc["permissions"] == {"contents": "read"}
    # CRASHACT-1: the one secret, the crash service's client key, declared for workflow_call and handed to the macOS package step alone
    assert text.count("secrets.") == 1 and "          CRASH_CLIENT_KEY: ${{ secrets.CRASH_CLIENT_KEY }}" in text, "the crash key alone"
    assert doc[True]["workflow_call"]["secrets"] == {"CRASH_CLIENT_KEY": {"required": False}}
    keyed = [(j, st["name"]) for j, job in doc["jobs"].items() for st in job.get("steps", []) if "secrets." in str(st)]
    assert keyed == [("macos", "Build the package (package_macos.py)")]
    assert "actions/cache" not in text, "no caches"
    assert set(doc[True]["workflow_dispatch"]["inputs"]) == {"platform", "commit", "version", "request"}


def test_the_workflow_matches_what_actions_build_accepts():
    import yaml
    doc = yaml.safe_load(WORKFLOW.read_text())
    assert doc["run-name"] == "Release package ${{ inputs.platform }} ${{ inputs.version }} (request ${{ inputs.request }})"
    stems = {"macos": "macos-arm64", "linux": "linux-x64", "windows": "windows-x64"}
    for platform, job in doc["jobs"].items():
        assert job["name"] == actions_build.job_name(platform) and job["if"] == f"inputs.platform == '{platform}'"
        (up,) = [s for s in job["steps"] if str(s.get("uses", "")).startswith("actions/upload-artifact@")]
        assert up["with"]["name"] == f"openbfme-${{{{ inputs.version }}}}-{stems[platform]}-${{{{ inputs.request }}}}"
        # the inputs reach the scripts through the environment only (no ${{ }} inside a run: block)
        for s in job["steps"]:
            assert "${{" not in s.get("run", ""), (platform, s.get("name"))
    assert doc["jobs"]["macos"]["runs-on"] == "macos-15"
    assert "pdb" not in json.dumps(doc["jobs"]["windows"]["steps"][-1]).lower(), "the private PDB is never uploaded"
