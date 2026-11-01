"""Lane GHPR-1: reviewed community pull requests land with the release that carries them (tools/release/community_prs.py, the hooks in
autorelease.sh), the public .github policy (public_workflows.py) and the public pull request audit (pr_audit.py).

The releases run against test_autorelease.py's throwaway world: a fake archive repository with copies of the release scripts, a local bare
repository as the public remote (a contributor's pull request is a real branch pushed there as refs/pull/<n>/head), gh_stand_in.py as gh
(its "pulls" hold GitHub's record of each pull request) and the stand-in build. Never the real repository or GitHub."""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import autorelease  # noqa: E402
import community_prs  # noqa: E402
import public_workflows  # noqa: E402
from test_autorelease import GIT_ENV, TARGET, World, commit, git, run, world_template  # noqa: E402, F401

DAISY = {"GIT_AUTHOR_NAME": "Daisy", "GIT_AUTHOR_EMAIL": "daisy@example.invalid"}
LOGIN = "daisyhasacat"


@pytest.fixture
def empty_world(tmp_path, world_template):
    return world_template.copy(tmp_path)


@pytest.fixture(scope="module")
def released_world_template(tmp_path_factory, world_template):
    template = world_template.copy(tmp_path_factory.mktemp("community-released"))
    first_release(template)
    return template


@pytest.fixture
def world(tmp_path, released_world_template):
    return released_world_template.copy(tmp_path)


def gh_state(world: World) -> dict:
    return json.loads(world.gh_state.read_text()) if world.gh_state.exists() else {"prs": {}, "releases": [], "next": 100}


def set_pull(world: World, n: int, **fields) -> None:
    s = gh_state(world)
    s.setdefault("pulls", {}).setdefault(str(n), {}).update(fields)
    world.gh_state.write_text(json.dumps(s, indent=1))


def first_release(world: World) -> str:
    """a first release, so the public main holds the sync tree a contributor branches from"""
    sha = world.change({"engine/a.cpp": "2\n"}, "X-1: units walk around trees instead of through them")
    world.gate(sha)
    r = world.release(sha, "--since", world.first)
    assert r.returncode == 0, r.stdout + r.stderr
    return sha


def open_pr(world: World, n: int, files: dict[str, str | None], login: str = LOGIN, base: str | None = None) -> str:
    """a contributor's pull request: a commit by Daisy on top of the public main (or `base`), pushed as refs/pull/<n>/head; GitHub's record
    of it in the stand-in. None deletes a file."""
    clone = world.tmp / f"contrib-{n}"
    if not clone.exists():
        git(world.tmp, "clone", "-q", str(world.remote), str(clone))
    git(clone, "fetch", "-q", "origin")
    git(clone, "checkout", "-q", "-B", f"patch-{n}", base or "origin/main")
    for name, data in files.items():
        if data is None:
            git(clone, "rm", "-q", name)
            continue
        (clone / name).parent.mkdir(parents=True, exist_ok=True)
        (clone / name).write_text(data)
        git(clone, "add", name)
    r = subprocess.run(["git", "-C", str(clone), "commit", "-q", "--allow-empty", "-m", f"a contribution (#{n})"], capture_output=True, text=True,
                       env={**os.environ, **GIT_ENV, **DAISY})
    assert r.returncode == 0, r.stderr
    head = git(clone, "rev-parse", "HEAD")
    git(clone, "push", "-q", "-f", "origin", f"HEAD:refs/pull/{n}/head")
    set_pull(world, n, number=n, head=head, login=login, state="open", merged=False, mergeable=True, draft=False, merge_commit_sha=None)
    return head


def port(world: World, n: int) -> str:
    """the pull request brought into the archive branch with its author kept (a cherry-pick)"""
    git(world.repo, "fetch", "-q", "origin", f"refs/pull/{n}/head")
    git(world.repo, "cherry-pick", "FETCH_HEAD")
    return git(world.repo, "rev-parse", "HEAD")


def review(world: World, n: int, head: str, port_sha: str, login: str = LOGIN) -> None:
    world.state.mkdir(exist_ok=True)
    with open(world.state / "community-prs.tsv", "a") as f:
        f.write(f"{n}\t{head}\t{login}\t{port_sha}\n")


def ready(world: World, n: int = 37, files=None) -> tuple[str, str]:
    """on a released world, pull request #n opened, ported, gated and reviewed: (its head, the archive sha that carries it)"""
    head = open_pr(world, n, files or {"engine/a.cpp": "2\ncontrib\n", "docs/new.md": "hello\n"})
    p = port(world, n)
    # the development line moved on beside the port (the usual case: the sync then has more to bring than the pull request)
    sha = world.change({"engine/b.cpp": "b\n"}, "W-1: the game also changed")
    world.gate(sha)
    review(world, n, head, p)
    return head, sha


def gh_log_from(world: World, start: int) -> list[str]:
    return world.gh_log.read_text().splitlines()[start:] if world.gh_log.exists() else []


def log_len(world: World) -> int:
    return len(world.gh_log.read_text().splitlines()) if world.gh_log.exists() else 0


def sync_prs(world: World) -> list[dict]:
    """RELCAND-1: the sync commits on the public main, oldest first ({merge: the commit, body: its message}); there is no sync PR any
    more: the candidate's commit is pushed as the public main, and its message carries what the PR body said"""
    found = []
    for c in reversed(git(world.remote, "log", "--first-parent", "--format=%H", "refs/heads/main").split()):
        body = git(world.remote, "log", "-1", "--format=%B", c)
        if body.startswith("Sync the rebuild"):
            found.append({"merge": c, "body": body})
    return found


@pytest.mark.parametrize("conflict", [False, True], ids=["clean", "conflict"])
def test_the_github_standin_performs_a_real_three_way_merge(empty_world, conflict):
    world = empty_world
    seed = world.tmp / "seed"
    lines = [f"line {n}\n" for n in range(20)]
    commit(seed, {"README.md": "".join(lines)}, "a shared base")
    git(seed, "push", "-q", str(world.remote), "main")
    theirs = lines.copy()
    theirs[1] = "the contribution\n"
    head = open_pr(world, 37, {"README.md": "".join(theirs)})
    ours = lines.copy()
    ours[1 if conflict else 18] = "the main branch moved on\n"
    base = commit(seed, {"README.md": "".join(ours)}, "main advanced after the pull request opened")
    git(seed, "push", "-q", str(world.remote), "main")
    before = gh_state(world)
    refs = git(world.remote, "show-ref")
    worktrees = git(world.remote, "worktree", "list", "--porcelain")
    r = run(world.gh, "pr", "merge", "37", "--merge", "--match-head-commit", head, env=world.env())
    assert git(world.remote, "worktree", "list", "--porcelain") == worktrees, "no temporary worktree remains"
    if conflict:
        assert r.returncode == 1 and "CONFLICT (content)" in r.stderr, r.stdout + r.stderr
        assert gh_state(world) == before and git(world.remote, "show-ref") == refs, "a conflict changes no GitHub record or ref"
    else:
        assert r.returncode == 0, r.stdout + r.stderr
        pull = gh_state(world)["pulls"]["37"]
        merged = pull["merge_commit_sha"]
        assert pull["merged"] is True and pull["state"] == "closed"
        assert git(world.remote, "rev-parse", "refs/heads/main") == merged
        assert git(world.remote, "rev-list", "--parents", "-1", merged).split() == [merged, base, head]
        ours[1] = theirs[1]
        assert git(world.remote, "show", f"{merged}:README.md") == "".join(ours).strip(), "both sides' edits survive the merge"


# ---- the flow, end to end ---------------------------------------------------------------------------------------------------------------
def test_a_reviewed_pull_request_is_merged_first_and_credited(world):
    head, sha = ready(world)
    start = log_len(world)
    r = world.release(sha)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6" in r.stdout, r.stdout + r.stderr
    pull = gh_state(world)["pulls"]["37"]
    assert pull["merged"] is True and pull["state"] == "closed"
    mc = pull["merge_commit_sha"]
    # merged on GitHub with a merge commit whose second parent is the contributor's own commit (their authorship kept), before the sync
    assert git(world.remote, "rev-parse", f"{mc}^2") == head
    assert git(world.remote, "log", "-1", "--format=%an <%ae>", head) == "Daisy <daisy@example.invalid>"
    log = gh_log_from(world, start)
    merge_at = [i for i, x in enumerate(log) if x.startswith("pr merge 37 ")]
    assert len(merge_at) == 1 and not any(x.startswith("pr create ") for x in log), log
    assert f"--merge --match-head-commit {head}" in log[merge_at[0]]
    # the sync goes on top of that merge, its tree the audited public tree (the sync recipe's, the same community_prs.public_tree builds)
    sync = sync_prs(world)[-1]
    assert subprocess.run(["git", "-C", str(world.remote), "merge-base", "--is-ancestor", mc, sync["merge"]]).returncode == 0
    assert git(world.remote, "rev-parse", f"{sync['merge']}^{{tree}}") == community_prs.public_tree(str(world.repo), sha)
    assert git(world.remote, "rev-parse", "refs/heads/main") == sync["merge"] and git(world.remote, "rev-parse", f"{sync['merge']}^") == mc
    # the sync commit credits it (RELCAND-1: the sync PR's body before)
    assert f"- Includes #37 by @{LOGIN}" in sync["body"], sync["body"]
    landed = (world.state / "community-landed.tsv").read_text().split("\t")
    assert landed[:5] == ["37", head, LOGIN, mc, sync["merge"]], landed
    # the next release neither merges it again nor credits it
    nxt = world.change({"engine/a.cpp": "2\ncontrib\n3\n"}, "Y-1: archers no longer shoot through walls")
    world.gate(nxt)
    start = log_len(world)
    r = world.release(nxt)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.7" in r.stdout, r.stdout + r.stderr
    assert not any(x.startswith("pr merge 37") for x in gh_log_from(world, start))
    assert "Includes #37" not in sync_prs(world)[-1]["body"]
    assert "COMMUNITY: #37 landed with an earlier release" in r.stdout


def test_a_release_that_is_only_the_pull_request_tags_its_merge(world):
    """when the public main holds the release's whole tree once the pull request is merged, there is no sync PR (nothing left to bring):
    the release tags the pull request's merge commit, and the pull request is recorded as landed"""
    head = open_pr(world, 37, {"engine/a.cpp": "2\ncontrib\n"})
    sha = port(world, 37)
    world.gate(sha)
    review(world, 37, head, sha)
    n = len(sync_prs(world))
    r = world.release(sha)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6" in r.stdout, r.stdout + r.stderr
    mc = gh_state(world)["pulls"]["37"]["merge_commit_sha"]
    assert "already has the tree of" in r.stdout and len(sync_prs(world)) == n
    assert git(world.repo, "ls-remote", str(world.remote), "refs/tags/v0.4.6^{}").split()[0] == mc
    assert (world.state / "community-landed.tsv").read_text().split("\t")[:5] == ["37", head, LOGIN, mc, mc]


def test_plan_runs_every_check_and_writes_nothing_to_github(world):
    head, sha = ready(world)
    before = json.dumps(gh_state(world), sort_keys=True)
    start = log_len(world)
    r = world.release(sha, "--plan")
    assert r.returncode == 0, r.stdout + r.stderr
    assert f"COMMUNITY PLAN: #37 by @{LOGIN} (head {head[:10]}): its changes are in the public tree; would be merged before the sync" in r.stdout
    assert f"- Includes #37 by @{LOGIN}" in r.stdout and "PUBLIC WORKFLOWS ok" in r.stdout
    calls = gh_log_from(world, start)
    assert calls == ["api repos/Test/repo/pulls/37"], calls   # one read, nothing else
    assert json.dumps(gh_state(world), sort_keys=True) == before
    assert not (world.state / "community-landed.tsv").exists() and world.remote_refs("refs/heads/sync/*") == ""


def test_a_dry_run_prints_the_merge_and_runs_it_not(world):
    head, sha = ready(world)
    start = log_len(world)
    main_before = git(world.remote, "rev-parse", "refs/heads/main")
    r = world.release(sha, "--dry-run")
    assert r.returncode == 0 and "AUTORELEASE DRY RUN OK" in r.stdout, r.stdout + r.stderr
    line = [x for x in r.stdout.splitlines() if "pr merge 37" in x]
    assert len(line) == 1 and line[0].startswith("WOULD RUN: ") and f"--merge --match-head-commit {head}" in line[0], line
    assert not any(x.startswith("pr ") for x in gh_log_from(world, start))
    assert gh_state(world)["pulls"]["37"]["state"] == "open" and git(world.remote, "rev-parse", "refs/heads/main") == main_before
    assert [b for b in (world.tmp / "work").glob("*/commit-msg.txt") if f"- Includes #37 by @{LOGIN}" in b.read_text()], "the dry run's sync commit message"
    assert not (world.state / "community-landed.tsv").exists()


# ---- every refusal, by name (plan mode: the same checks a real run makes before its first write) ----------------------------------
def _moved(world, head, sha):
    clone = world.tmp / "contrib-37"
    (clone / "docs/new.md").write_text("hello again\n")
    git(clone, "commit", "-q", "-am", "pushed after the review")
    git(clone, "push", "-q", "-f", "origin", "HEAD:refs/pull/37/head")
    set_pull(world, 37, head=git(clone, "rev-parse", "HEAD"))


def _fetched_head_differs(world, head, sha):
    git(world.tmp / "contrib-37", "push", "-q", "-f", "origin", "HEAD~1:refs/pull/37/head")


def _port_differs(world, head, sha):
    # the archive's port is not the pull request: the same file with other words
    world.change({"engine/a.cpp": "2\ncontrib!\n"}, "the port changed the line")


def _rewritten_later(world, head, sha):
    world.change({"engine/a.cpp": "2\nsomething else\n"}, "a later commit rewrote the contributed line")


def _file_removed_later(world, head, sha):
    git(world.repo, "rm", "-q", "docs/new.md")
    git(world.repo, "commit", "-q", "-m", "a later commit removed the contributed file")


CASES = {
    "head moved": (_moved, {}, "its head on GitHub is not the reviewed sha"),
    "fetched head": (_fetched_head_differs, {}, "the head fetched from origin is not the reviewed sha"),
    "author": (None, {"login": "someoneelse"}, "its author on GitHub is not the reviewed one"),
    "closed": (None, {"state": "closed"}, "it is 'closed' and not merged on GitHub"),
    "draft": (None, {"draft": True}, "it is a draft"),
    "draft unknown": (None, {"draft": None}, "it is a draft"),
    "mergeable null": (None, {"mergeable": None}, "GitHub does not say it can be merged"),
    "mergeable false": (None, {"mergeable": False}, "GitHub does not say it can be merged"),
    "base branch": (None, {"base": "dev"}, "it is not a pull request against main of Test/repo"),
    "base repo": (None, {"repo": "Other/repo"}, "it is not a pull request against main of Test/repo"),
    "another number": (None, {"answer_number": 38}, "GitHub answered for another pull request"),
    "merged no sha": (None, {"state": "closed", "merged": True, "merge_commit_sha": None}, "merged on GitHub without a merge commit sha"),
    "merged elsewhere": (None, {"state": "closed", "merged": True, "merge_commit_sha": "1" * 40}, "is not on origin/main"),
    "port differs": (_port_differs, {}, "its changes are not all in the public tree being synced"),
    "rewritten later": (_rewritten_later, {}, "its changes are not all in the public tree being synced"),
    "file removed later": (_file_removed_later, {}, "its changes are not all in the public tree being synced"),
}


@pytest.mark.parametrize("case", sorted(CASES))
def test_every_check_refuses_by_name(world, case):
    head, sha = ready(world)
    change, fields, why = CASES[case]
    if change:
        change(world, head, sha)
    if fields:
        set_pull(world, 37, **fields)
    tip = git(world.repo, "rev-parse", "HEAD")
    world.gate(tip)
    start = log_len(world)
    r = world.release(tip, "--plan")
    assert r.returncode != 0 and "COMMUNITY REFUSED" in r.stdout and why in r.stdout, r.stdout + r.stderr
    assert all(x.startswith("api repos/Test/repo/pulls/37") for x in gh_log_from(world, start)), gh_log_from(world, start)


@pytest.mark.parametrize("files,why", [
    ({"archive/note.txt": "x\n"}, "its changes are not all in the public tree"),   # a path the sync leaves out
    ({"engine/a.cpp": "2\n"}, "it changes nothing"),                                 # an empty pull request
])
def test_a_pull_request_the_public_tree_cannot_hold_is_refused(world, files, why):
    head = open_pr(world, 37, files)
    if "archive/note.txt" in files:
        port(world, 37)
    sha = world.change({"engine/b.cpp": "b\n"}, "W-1: something to release")
    world.gate(sha)
    review(world, 37, head, sha)
    r = world.release(sha, "--plan")
    assert r.returncode != 0 and why in r.stdout, r.stdout + r.stderr


def test_a_github_read_failure_fails_closed(world):
    head, sha = ready(world)
    r = world.release(sha, "--plan", FAKE_GH_FAULT="pull-read-fail")
    assert r.returncode != 0 and "#37: cannot read the pull request from GitHub (fail closed)" in r.stdout, r.stdout


# ---- real runs: refusals write nothing, races and lost answers ------------------------------------------------------------------------
def _nothing_written(world, main_before: str, n_prs: int) -> None:
    assert git(world.remote, "rev-parse", "refs/heads/main") == main_before
    assert world.remote_refs("refs/heads/sync/*") == "" and len(sync_prs(world)) == n_prs
    assert not (world.state / "community-landed.tsv").exists()


def test_a_refusal_in_a_real_run_pushes_nothing(world):
    head, sha = ready(world)
    tip = world.change({"engine/a.cpp": "2\ncontrib!\n"}, "the port changed the line")
    world.gate(tip)
    main_before, n = git(world.remote, "rev-parse", "refs/heads/main"), len(sync_prs(world))
    r = world.release(tip)
    assert r.returncode != 0 and "a reviewed community pull request cannot land: nothing synced" in r.stdout, r.stdout + r.stderr
    assert gh_state(world)["pulls"]["37"]["state"] == "open"
    _nothing_written(world, main_before, n)


def test_every_pull_request_is_checked_before_the_first_merge(world):
    head, sha = ready(world)
    h38 = open_pr(world, 38, {"docs/other.md": "other\n"})
    # #38 is reviewed but its port is not what it changed
    tip = world.change({"docs/other.md": "other, changed in the port\n"}, "port of #38, changed")
    review(world, 38, h38, tip)
    world.gate(tip)
    main_before, n = git(world.remote, "rev-parse", "refs/heads/main"), len(sync_prs(world))
    r = world.release(tip)
    assert r.returncode != 0 and "#38 by @daisyhasacat: its changes are not all in the public tree" in r.stdout, r.stdout
    assert gh_state(world)["pulls"]["37"]["state"] == "open", "#37 passed its checks but nothing is merged while #38 fails"
    _nothing_written(world, main_before, n)


def test_a_push_just_before_the_merge_is_refused_by_github(world):
    head, sha = ready(world)
    main_before, n = git(world.remote, "rev-parse", "refs/heads/main"), len(sync_prs(world))
    r = world.release(sha, FAKE_GH_FAULT="community-push-before-merge")
    assert r.returncode != 0 and "#37 is not merged with the reviewed head" in r.stdout, r.stdout + r.stderr
    assert gh_state(world)["pulls"]["37"]["state"] == "open"
    _nothing_written(world, main_before, n)


def test_a_lost_merge_answer_is_read_back(world):
    head, sha = ready(world)
    r = world.release(sha, FAKE_GH_FAULT="community-merge-lost")
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6" in r.stdout, r.stdout + r.stderr
    assert "gh pr merge #37 failed or its answer was lost: reading it back" in r.stdout
    assert gh_state(world)["pulls"]["37"]["merged"] is True


def test_a_refused_merge_stops_the_run(world):
    head, sha = ready(world)
    main_before, n = git(world.remote, "rev-parse", "refs/heads/main"), len(sync_prs(world))
    r = world.release(sha, FAKE_GH_FAULT="community-merge-refused")
    assert r.returncode != 0 and "#37 is not merged with the reviewed head" in r.stdout, r.stdout
    _nothing_written(world, main_before, n)


def test_a_pull_request_merged_by_hand_is_credited_not_merged_again(world):
    head, sha = ready(world)
    rr = run(world.gh, "pr", "merge", "37", "--repo", TARGET, "--merge", env=world.env())
    assert rr.returncode == 0, rr.stderr
    start = log_len(world)
    r = world.release(sha)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "COMMUNITY: #37 by @daisyhasacat is already merged on GitHub" in r.stdout
    assert not any(x.startswith("pr merge 37") for x in gh_log_from(world, start))
    assert f"- Includes #37 by @{LOGIN}" in sync_prs(world)[-1]["body"]
    assert (world.state / "community-landed.tsv").read_text().startswith(f"37\t{head}\t")


def test_a_port_outside_the_release_waits(world):
    head = open_pr(world, 37, {"docs/new.md": "hello\n"})
    git(world.repo, "checkout", "-q", "-b", "side")
    side = port(world, 37)
    git(world.repo, "checkout", "-q", "archive-legacy-codebase")
    sha = world.change({"engine/a.cpp": "3\n"}, "Z-1: something else for players")
    world.gate(sha)
    review(world, 37, head, side)
    r = world.release(sha)
    assert r.returncode == 0, r.stdout + r.stderr
    assert f"COMMUNITY: #37 waits for a later release (its port {side[:10]} is not in {sha[:10]})" in r.stdout
    assert gh_state(world)["pulls"]["37"]["state"] == "open" and "Includes #37" not in sync_prs(world)[-1]["body"]


def test_a_resumed_run_after_the_sync_merges_nothing_more(world):
    """the run stops after its push (RELCAND-1: publishing fails): the resumed run neither merges a pull request reviewed in between nor
    records it"""
    h37 = open_pr(world, 37, {"engine/c.cpp": "c\n"})
    port(world, 37)
    h38 = open_pr(world, 38, {"engine/d.cpp": "d\n"})
    sha = port(world, 38)
    world.gate(sha)
    review(world, 37, h37, sha)
    r = world.release(sha, FAKE_GH_FAULT="tag-read-fail")
    assert r.returncode != 0 and gh_state(world)["pulls"]["37"]["merged"] is True, r.stdout + r.stderr
    assert "Includes #37" in sync_prs(world)[-1]["body"] and "#38" not in sync_prs(world)[-1]["body"]
    review(world, 38, h38, sha)
    r = world.release(sha)
    assert r.returncode == 0 and "AUTORELEASE OK" in r.stdout, r.stdout + r.stderr
    assert "AUTORELEASE community: resumed after the sync, nothing to merge" in r.stdout
    assert "COMMUNITY: #38 is not merged into" in r.stdout
    assert gh_state(world)["pulls"]["38"]["state"] == "open"
    assert [x.split("\t")[0] for x in (world.state / "community-landed.tsv").read_text().splitlines()] == ["37"]


# ---- the lists ---------------------------------------------------------------------------------------------------------------------------
@pytest.mark.parametrize("line", [
    "37\t" + "a" * 40 + "\tdaisyhasacat",                                  # a field missing
    "37\t" + "A" * 40 + "\tdaisyhasacat\t" + "b" * 40,                     # an upper-case sha
    "37\t" + "a" * 39 + "\tdaisyhasacat\t" + "b" * 40,                     # a short sha
    "037\t" + "a" * 40 + "\tdaisyhasacat\t" + "b" * 40,                    # a leading zero
    "37\t" + "a" * 40 + "\tdaisy](https://x)\t" + "b" * 40,                # Markdown in the login
    "37\t" + "a" * 40 + "\t-daisy\t" + "b" * 40,                           # a login GitHub would not give
    "37\t" + "a" * 40 + "\tdaisy--cat\t" + "b" * 40,
    "37\t" + "a" * 40 + "\t" + "d" * 40 + "\t" + "b" * 40,                 # 40 characters
    "37 " + "a" * 40 + " daisyhasacat " + "b" * 40,                       # spaces, not tabs
])
def test_a_malformed_reviewed_line_is_refused(tmp_path, line):
    (tmp_path / "community-prs.tsv").write_text(line + "\n")
    with pytest.raises(community_prs.Refusal, match="is not `<pr number>"):
        community_prs.read_reviewed(tmp_path / "community-prs.tsv")


def test_the_reviewed_list_grammar(tmp_path):
    good = "37\t" + "a" * 40 + "\tdaisy-has-a-cat\t" + "b" * 40
    (tmp_path / "community-prs.tsv").write_text("# reviewed\n\n" + good + "\n")
    assert community_prs.read_reviewed(tmp_path / "community-prs.tsv") == [{"pr": 37, "head": "a" * 40, "login": "daisy-has-a-cat", "port": "b" * 40}]
    (tmp_path / "community-prs.tsv").write_text(good + "\n" + good.replace("a" * 40, "c" * 40) + "\n")
    with pytest.raises(community_prs.Refusal, match="lists #37 twice"):
        community_prs.read_reviewed(tmp_path / "community-prs.tsv")
    assert community_prs.read_reviewed(tmp_path / "missing.tsv") == []


def test_a_landed_pull_request_listed_with_another_head_is_refused(world):
    head, sha = ready(world)
    (world.state / "community-landed.tsv").write_text(f"37\t{'e' * 40}\t{LOGIN}\t{'f' * 40}\t{'f' * 40}\t2026-10-18T00:00:00Z\n")
    r = world.release(sha, "--plan")
    assert r.returncode != 0 and "#37 landed earlier with head eeeeeeeeee, and is listed again" in r.stdout, r.stdout


def test_a_port_commit_that_is_not_here_is_refused(world):
    head = open_pr(world, 37, {"engine/c.cpp": "c\n"})
    sha = port(world, 37)
    world.gate(sha)
    review(world, 37, head, "9" * 40)
    r = world.release(sha, "--plan")
    assert r.returncode != 0 and "its port commit 9999999999 is not a commit of this repository" in r.stdout, r.stdout


def test_land_refuses_a_tree_that_is_not_the_sync_recipes(world):
    head, sha = ready(world)
    work = world.tmp / "j.json"
    work.write_text(json.dumps([{"pr": 37, "head": head, "login": LOGIN, "port": sha}]))
    other = git(world.repo, "rev-parse", f"{sha}^{{tree}}")   # the archive's own tree, archive/ included
    r = run(sys.executable, HERE / "community_prs.py", "--git-dir", world.repo, "--repo", TARGET, "--gh", world.gh, "--state", world.state,
            "land", "--json", work, "--sha", sha, "--tree", other, "--mode", "dry", env=world.env())
    assert r.returncode == 1 and "is not the sync recipe's public tree" in r.stdout, r.stdout + r.stderr


# ---- containment, unit ---------------------------------------------------------------------------------------------------------------
@pytest.fixture
def repo(tmp_path):
    d = tmp_path / "r"
    git(tmp_path, "init", "-q", "-b", "main", str(d))
    return d


def _commit(repo: Path, files: dict[str, bytes | str | None], mode: dict[str, int] | None = None) -> str:
    for name, data in files.items():
        p = repo / name
        if data is None:
            git(repo, "rm", "-q", name)
            continue
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data if isinstance(data, bytes) else data.encode())
        git(repo, "add", name)
    for name, m in (mode or {}).items():
        os.chmod(repo / name, m)
        git(repo, "add", name)
    git(repo, "commit", "-q", "--allow-empty", "-m", "c")
    return git(repo, "rev-parse", "HEAD")


def _tree(repo: Path, base: str, files: dict[str, bytes | str | None]) -> str:
    git(repo, "checkout", "-q", "--detach", base)
    c = _commit(repo, files)
    return git(repo, "rev-parse", f"{c}^{{tree}}")


LONG = "".join(f"line {i}\n" for i in range(40))


@pytest.mark.parametrize("tree_files,ok", [
    ({}, True),                                                                                  # exactly the pull request
    ({"a.txt": LONG.replace("line 3\n", "line 3 contributed\n").replace("line 35\n", "line 35 later\n")}, True),  # later edit elsewhere
    ({"z.txt": "unrelated\n"}, True),                                                            # other files changed
    ({"a.txt": LONG.replace("line 3\n", "line 3 rewritten\n")}, False),                          # the same line rewritten
    ({"a.txt": LONG.replace("line 4\n", "line 4 changed\n").replace("line 3\n", "line 3 contributed\n")}, False),  # its context changed
    ({"new.bin": b"\x00\x01other"}, False),                                                      # an added binary file differs
    ({"new.bin": None}, False),                                                                  # an added file missing
    ({"gone.txt": "back\n"}, False),                                                             # a deleted file present again
])
def test_containment(repo, tree_files, ok):
    base = _commit(repo, {"a.txt": LONG, "gone.txt": "old\n", "x.sh": "#!/bin/sh\n"})
    head = _commit(repo, {"a.txt": LONG.replace("line 3\n", "line 3 contributed\n"), "gone.txt": None, "new.bin": b"\x00\x01\x02bin"},
                   mode={"x.sh": 0o755})
    tree = _tree(repo, head, tree_files)
    why = community_prs.containment_problem(str(repo), base, head, tree)
    assert (why == "") == ok, why


def test_containment_needs_the_mode_change(repo):
    base = _commit(repo, {"x.sh": "#!/bin/sh\n"})
    head = _commit(repo, {}, mode={"x.sh": 0o755})
    assert community_prs.containment_problem(str(repo), base, head, git(repo, "rev-parse", f"{head}^{{tree}}")) == ""
    assert community_prs.containment_problem(str(repo), base, head, git(repo, "rev-parse", f"{base}^{{tree}}")) != ""
    assert community_prs.containment_problem(str(repo), base, base, git(repo, "rev-parse", f"{base}^{{tree}}")) == "it changes nothing"


def test_credit_lines():
    assert community_prs.credit_lines([]) == ""
    text = community_prs.credit_lines([{"pr": 37, "login": "daisyhasacat"}, {"pr": 36, "login": "a-b"}])
    assert text.endswith("- Includes #37 by @daisyhasacat\n- Includes #36 by @a-b\n")
    assert autorelease.public_text_problems("pr-body.md", text) == []


# ---- the public .github policy ---------------------------------------------------------------------------------------------------------
PR_YML = (REPO / ".github/workflows/pr.yml").read_text()


def _tree_of(repo: Path, files: dict[str, str], links: dict[str, str] | None = None) -> str:
    entries = []
    for name, text in files.items():
        oid = subprocess.run(["git", "-C", str(repo), "hash-object", "-w", "--stdin"], input=text.encode(), capture_output=True,
                             check=True).stdout.decode().strip()
        entries.append((name, "100644", oid))
    for name, target in (links or {}).items():
        oid = subprocess.run(["git", "-C", str(repo), "hash-object", "-w", "--stdin"], input=target.encode(), capture_output=True,
                             check=True).stdout.decode().strip()
        entries.append((name, "120000", oid))
    idx = repo / "idx"
    env = {**os.environ, "GIT_INDEX_FILE": str(idx)}
    if idx.exists():
        idx.unlink()
    for name, mode, oid in entries:
        subprocess.run(["git", "-C", str(repo), "update-index", "--add", "--cacheinfo", f"{mode},{oid},{name}"], env=env, check=True)
    return subprocess.run(["git", "-C", str(repo), "write-tree"], env=env, capture_output=True, check=True).stdout.decode().strip()


def test_the_real_workflow_passes_the_policy():
    assert public_workflows.workflow_problems(".github/workflows/pr.yml", PR_YML) == []
    assert "pull_request:" in PR_YML and "permissions:\n  contents: read\n" in PR_YML


@pytest.mark.parametrize("old,new,why", [
    ("  pull_request:\n", "  pull_request_target:\n", "the trigger pull_request_target"),
    ("  workflow_dispatch:\n", "  workflow_run:\n    workflows: [x]\n", "the trigger workflow_run"),
    ("  workflow_dispatch:\n", "  issue_comment:\n", "the trigger issue_comment"),
    ("  merge_group:\n", "  pull_request_target:\n", "the trigger pull_request_target"),
    ("  workflow_dispatch:\n", "  schedule:\n    - cron: '0 0 * * *'\n", "the trigger schedule is not one of"),
    ("on:\n  pull_request:\n", "on: [push, pull_request_target]\nx:\n  pull_request:\n", "the trigger pull_request_target"),
    ("on:\n  pull_request:\n", "on: [push]\nx:\n  pull_request:\n", "`on:` is not written as a block"),
    ("permissions:\n  contents: read\n", "permissions:\n  contents: write\n", "a write permission"),
    ("permissions:\n  contents: read\n", "permissions: read-all\n", "the top-level permissions are not exactly `contents: read`"),
    ("permissions:\n  contents: read\n", "", "the top-level permissions are not exactly `contents: read`"),
    ("permissions:\n  contents: read\n", "permissions:\n  contents: read\n  pull-requests: read\n", "the top-level permissions are not exactly"),
    ("    runs-on: ubuntu-24.04\n    timeout-minutes: 5\n", "    runs-on: ubuntu-24.04\n    permissions:\n      issues: write\n    timeout-minutes: 5\n", "a write permission"),
    ("    timeout-minutes: 5\n", "    timeout-minutes: 5\n    permissions:\n      id-token: none\n", "an id-token permission"),
    ("python3 tools/release/pr_pytest.py", "python3 tools/release/pr_pytest.py ${{ secrets.TOKEN }}", "a secrets reference"),
    ("python3 tools/release/pr_pytest.py", "echo ${{ secrets['X'] }}", "a secrets reference"),
    ("actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1", "actions/checkout@v7", "an action not pinned by its full commit sha"),
    ("actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1", "actions/checkout@3d3c42e5", "an action not pinned"),
    ("actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1", "docker://alpine:3", "an action not pinned"),
    ("runs-on: ubuntu-24.04\n    timeout-minutes: 5", "runs-on: self-hosted\n    timeout-minutes: 5", "a self-hosted runner"),
    ("runs-on: ubuntu-24.04\n    timeout-minutes: 5", "runs-on: [ubuntu-24.04, gpu]\n    timeout-minutes: 5", "runs-on is not one GitHub-hosted runner"),
    ("runs-on: ubuntu-24.04\n    timeout-minutes: 5", "runs-on: the-build-box\n    timeout-minutes: 5", "runs-on is not one GitHub-hosted runner"),
])
def test_each_workflow_rule_catches_its_mutation(old, new, why):
    assert old in PR_YML, old
    probs = public_workflows.workflow_problems(".github/workflows/pr.yml", PR_YML.replace(old, new, 1))
    assert any(why in p for p in probs), probs


# lane PUBLICCI-1: every run of the public pr.yml failed as a "workflow file issue" (a plain `run:` value holding ": ") while the text
# rules above passed it; the policy now loads each workflow as YAML
OLD_LINE_88 = 'run: engine/build/openbfme_tests --test-case-exclude="audio decode: MP3 *,audio decode: corrupted MP3 fixtures*"'


def test_the_real_workflow_parses_and_the_old_unparsable_line_is_refused():
    assert public_workflows.yaml_problems(".github/workflows/pr.yml", PR_YML) == []
    old = '        run: |\n          engine/build/openbfme_tests\n'
    assert old in PR_YML
    probs = public_workflows.workflow_problems(".github/workflows/pr.yml", PR_YML.replace(old, "        " + OLD_LINE_88 + "\n", 1))
    assert len(probs) == 1 and "not valid YAML (mapping values are not allowed here)" in probs[0], probs
    assert probs[0].startswith(".github/workflows/pr.yml:"), probs
    for bad, why in (("- a\n- b\n", "the YAML is not a mapping"), ("", "the YAML is not a mapping"),
                     ("on:\n  push:\n\tx: 1\n", "not valid YAML"), ("a: [1\n", "not valid YAML")):
        probs = public_workflows.yaml_problems("w.yml", bad)
        assert len(probs) == 1 and why in probs[0], (bad, probs)


def test_without_pyyaml_the_yaml_check_fails_instead_of_skipping(monkeypatch):
    monkeypatch.setitem(sys.modules, "yaml", None)   # makes `import yaml` raise ImportError
    probs = public_workflows.workflow_problems(".github/workflows/pr.yml", PR_YML)
    assert probs == [".github/workflows/pr.yml: PyYAML is not installed, so the file cannot be checked to parse as YAML (pip install PyYAML)"]


def test_the_ci_check_needs_every_other_job():
    """CI is the one check name the repository's rules require: it must need (and its script must expect) exactly the other jobs, so a
    job added without it, or left out of it, fails here and not silently on GitHub"""
    import yaml
    jobs = yaml.safe_load(PR_YML)["jobs"]
    ci = jobs["ci"]
    assert ci["name"] == "CI" and ci["if"] == "always()"
    others = set(jobs) - {"ci"}
    assert set(ci["needs"]) == others and len(ci["needs"]) == len(others)
    script = ci["steps"][0]["run"]
    want = re.search(r"want = (\{[^}]*\})", script).group(1)
    assert eval(want) == others, want
    assert 'v["result"] != "success"' in script   # skipped and cancelled fail too
    names = [j.get("name") for j in jobs.values()]
    assert len(set(names)) == len(names) and names.count("CI") == 1


def test_the_ci_script_fails_on_every_non_success(tmp_path):
    import yaml
    script = yaml.safe_load(PR_YML)["jobs"]["ci"]["steps"][0]["run"]
    body = script.split("<<'EOF'\n", 1)[1].rsplit("EOF", 1)[0]
    f = tmp_path / "ci.py"
    f.write_text(body)
    names = ("lint", "gate", "fixtures", "linux", "windows", "macos")

    def ci(results):
        env = {**os.environ, "RESULTS": json.dumps({k: {"result": v, "outputs": {}} for k, v in results.items()})}
        return subprocess.run([sys.executable, str(f)], env=env, capture_output=True, text=True)
    r = ci({n: "success" for n in names})
    assert r.returncode == 0 and r.stdout.strip() == "CI ok", r.stdout
    for n in names:
        for bad in ("failure", "cancelled", "skipped"):
            r = ci({**{m: "success" for m in names}, n: bad})
            assert r.returncode == 1 and f"CI FAILED: {n}: {bad}" in r.stdout, (n, bad, r.stdout)
        r = ci({m: "success" for m in names if m != n})
        assert r.returncode == 1 and f"not {sorted(names)}" in r.stdout, r.stdout
    assert ci({}).returncode == 1


def test_the_github_folder_holds_approved_files_only(repo):
    ok = {".github/workflows/pr.yml": PR_YML, ".github/pull_request_template.md": "x\n", "README.md": "r\n"}
    assert public_workflows.tree_problems(str(repo), _tree_of(repo, ok)) == []
    for extra in (".github/dependabot.yml", ".github/FUNDING.yml", ".github/CODEOWNERS", ".github/workflows/x.yaml",
                  ".github/workflows/sub/y.yml", ".github/ISSUE_TEMPLATE/bug.md", ".github/workflows/Upper.yml"):
        probs = public_workflows.tree_problems(str(repo), _tree_of(repo, {**ok, extra: "on:\n"}))
        assert probs == [f"{extra}: not an approved file of the public GitHub folder (public_workflows.py APPROVED)"], (extra, probs)
    probs = public_workflows.tree_problems(str(repo), _tree_of(repo, ok, links={".github/workflows/l.yml": "../../x"}))
    assert any("not an approved file" in p for p in probs), probs
    bad = PR_YML.replace("  pull_request:\n", "  pull_request_target:\n")
    probs = public_workflows.tree_problems(str(repo), _tree_of(repo, {**ok, ".github/workflows/second.yml": bad}))
    assert any(p.startswith(".github/workflows/second.yml") and "pull_request_target" in p for p in probs), probs


# ---- the public pull request audit and the public top level -----------------------------------------------------------------------------
def _public_tree_of_head() -> str:
    """the public tree of this checkout: the sync recipe's in the archive, the checkout's own in the public repository (no archive/)"""
    if "archive" in git(REPO, "ls-tree", "--name-only", "HEAD").split():
        return community_prs.public_tree(str(REPO), "HEAD")
    return git(REPO, "rev-parse", "HEAD^{tree}")


def test_the_public_tree_holds_the_contributor_files():
    tree = _public_tree_of_head()
    names = set(git(REPO, "ls-tree", "--name-only", tree).split())
    assert {".github", "CONTRIBUTING.md"} <= names and "archive" not in names
    assert {".github", "CONTRIBUTING.md"} <= autorelease.PUBLIC_TOP


def test_pr_audit_passes_the_real_public_tree_and_refuses_an_empty_one(tmp_path):
    r = run(sys.executable, HERE / "pr_audit.py", "--git-dir", REPO, "--tree", _public_tree_of_head())
    assert r.returncode == 0 and "PR AUDIT ok" in r.stdout and "the built-in rules only" in r.stdout, r.stdout + r.stderr
    empty = subprocess.run(["git", "-C", str(REPO), "mktree"], input=b"", capture_output=True, check=True).stdout.decode().strip()
    r = run(sys.executable, HERE / "pr_audit.py", "--git-dir", REPO, "--tree", empty)
    assert r.returncode == 1 and "PR AUDIT: the tree has no README.md" in r.stdout and "PR AUDIT FAILED" in r.stdout, r.stdout
    r = run(sys.executable, HERE / "pr_audit.py", "--git-dir", REPO, "--tree", "0" * 40)
    assert r.returncode == 1 and "is not a tree here" in r.stdout


MINIMAL = {"README.md": b"r\n", "LICENSE": b"l\n", "CONTRIBUTING.md": b"c\n", ".github/workflows/pr.yml": PR_YML.encode(),
           "docs/a.md": b"d\n", "engine/a.cpp": b"1\n", "godot/a.gd": b"1\n", "launcher/a.gd": b"1\n", "tools/a.py": b"1\n"}


def _minimal() -> str:
    tree = subprocess.run(["git", "-C", str(REPO), "mktree"], input=b"", capture_output=True, check=True).stdout.decode().strip()
    for path, data in MINIMAL.items():
        tree = _with(tree, path, data)
    return tree


def test_pr_audit_passes_the_minimal_public_tree():
    r = run(sys.executable, HERE / "pr_audit.py", "--git-dir", REPO, "--tree", _minimal())
    assert r.returncode == 0 and "PR AUDIT ok" in r.stdout, r.stdout + r.stderr
    for core in community_prs_core():
        tree = _minimal_without(core)
        r = run(sys.executable, HERE / "pr_audit.py", "--git-dir", REPO, "--tree", tree)
        assert r.returncode == 1 and f"the tree has no {core}" in r.stdout, (core, r.stdout)


def community_prs_core() -> tuple[str, ...]:
    import pr_audit  # noqa: PLC0415
    return pr_audit.CORE


def _minimal_without(core: str) -> str:
    tree = subprocess.run(["git", "-C", str(REPO), "mktree"], input=b"", capture_output=True, check=True).stdout.decode().strip()
    for path, data in MINIMAL.items():
        if path != core and not path.startswith(core + "/"):
            tree = _with(tree, path, data)
    return tree


def _with(tree: str, path: str, data: bytes) -> str:
    """the tree with one more file (via a temporary index)"""
    with __import__("tempfile").TemporaryDirectory() as tmp:
        env = {**os.environ, "GIT_INDEX_FILE": str(Path(tmp, "i"))}
        subprocess.run(["git", "-C", str(REPO), "read-tree", tree], env=env, check=True)
        oid = subprocess.run(["git", "-C", str(REPO), "hash-object", "-w", "--stdin"], input=data, capture_output=True, check=True).stdout.decode().strip()
        subprocess.run(["git", "-C", str(REPO), "update-index", "--add", "--cacheinfo", f"100644,{oid},{path}"], env=env, check=True)
        return subprocess.run(["git", "-C", str(REPO), "write-tree"], env=env, capture_output=True, check=True).stdout.decode().strip()


@pytest.mark.parametrize("path,data,why", [
    (".github/workflows/leak.yml", PR_YML.replace("ubuntu-24.04", "self-hosted", 1).encode(), "a self-hosted runner"),
    (".github/notes.md", b"notes\n", "not an approved file of the public GitHub folder"),
    (".github/pull_request_template.md", ("see /" + "home/someone/game for the files\n").encode(), "developer-machine path in .github/pull_request_template.md"),
    ("CONTRIBUTING.md", b"RIFF\x00\x00\x00\x00WAVEfmt ", "retail-format bytes"),
    (".github/workflows/x.big", b"BIGF\x00\x00", "retail-format file"),
    (".circleci/config.yml", b"x\n", "not a public top-level entry: .circleci"),
])
def test_nothing_private_reaches_the_public_tree_through_the_new_files(path, data, why):
    tree = _with(_minimal(), path, data)
    r = run(sys.executable, HERE / "pr_audit.py", "--git-dir", REPO, "--tree", tree)
    assert r.returncode == 1 and why in r.stdout, r.stdout


# ---- the public pytest subset fails closed ----------------------------------------------------------------------------------------------
@pytest.mark.parametrize("files,ok,why", [
    ({"test_a.py": "def test_a():\n    assert True\n"}, True, "1 of 1 tests passed in 1 files"),
    ({"test_a.py": "def test_a():\n    assert False\n"}, False, "PR PYTEST FAILED"),
    ({"test_a.py": "import pytest\n\ndef test_a():\n    pytest.skip('no games')\n"}, False, "no test of test_a.py passed"),
    ({"test_a.py": "X = 1\n"}, False, "no test of test_a.py passed"),
    ({"test_a.py": "import not_a_module_here\n\ndef test_a():\n    pass\n"}, False, "PR PYTEST FAILED"),
    ({}, False, "listed test files missing: test_a.py"),
])
def test_pr_pytest_fails_closed(tmp_path, monkeypatch, capfd, files, ok, why):
    import pr_pytest  # noqa: PLC0415
    for name, text in files.items():
        (tmp_path / name).write_text(text)
    (tmp_path / "pytest.ini").write_text("[pytest]\n")
    monkeypatch.setattr(pr_pytest, "REPO", tmp_path)
    monkeypatch.setattr(pr_pytest, "SUBSET", ["test_a.py"])
    rc = pr_pytest.main([])
    out = capfd.readouterr().out
    assert (rc == 0) == ok and why in out, out


def test_pr_pytest_refuses_an_empty_list(monkeypatch, capfd):
    import pr_pytest  # noqa: PLC0415
    monkeypatch.setattr(pr_pytest, "SUBSET", [])
    assert pr_pytest.main([]) == 1 and "(the list is empty)" in capfd.readouterr().out


def test_pr_pytest_lists_real_files():
    import pr_pytest  # noqa: PLC0415
    assert pr_pytest.SUBSET and all((REPO / f).is_file() for f in pr_pytest.SUBSET) and len(set(pr_pytest.SUBSET)) == len(pr_pytest.SUBSET)


def test_a_candidate_that_fails_leaves_its_merges_and_the_next_run_credits_them(world):
    """RELCAND-1: the reviewed pull requests are merged on GitHub before the candidate (the one write before the push: the pull request's
    own, already public commits). A candidate that fails (a tampered package) pushes nothing else; the next run takes the merged one as
    already merged, lands the one reviewed in between, credits both in its sync commit and records both, under the same version"""
    h37 = open_pr(world, 37, {"engine/c.cpp": "c\n"})
    port(world, 37)
    h38 = open_pr(world, 38, {"engine/d.cpp": "d\n"})
    port(world, 38)
    sha = world.change({"engine/b.cpp": "b\n"}, "W-1: the game also changed")   # so the sync has more to bring than the merges
    world.gate(sha)
    review(world, 37, h37, sha)
    n = len(sync_prs(world))
    r = world.release(sha, STANDIN_TAMPERED="1")
    assert r.returncode != 0 and gh_state(world)["pulls"]["37"]["merged"] is True, r.stdout + r.stderr
    mc37 = gh_state(world)["pulls"]["37"]["merge_commit_sha"]
    assert git(world.remote, "rev-parse", "refs/heads/main") == mc37 and len(sync_prs(world)) == n, "no sync commit was pushed"
    assert not world.remote_refs("refs/tags/v0.4.6") and not (world.state / "intent").exists()
    assert not (world.state / "community-landed.tsv").exists()
    review(world, 38, h38, sha)
    r = world.release(sha)
    assert r.returncode == 0 and "AUTORELEASE OK v0.4.6" in r.stdout, r.stdout + r.stderr
    assert f"COMMUNITY: #37 by @{LOGIN} is already merged on GitHub" in r.stdout
    sync = sync_prs(world)[-1]
    assert "Includes #37" in sync["body"] and "Includes #38" in sync["body"]
    mc38 = gh_state(world)["pulls"]["38"]["merge_commit_sha"]
    assert git(world.remote, "rev-parse", f"{sync['merge']}^") == mc38 and git(world.remote, "rev-parse", f"{mc38}^") == mc37
    assert [x.split("\t")[0] for x in (world.state / "community-landed.tsv").read_text().splitlines()] == ["37", "38"]
