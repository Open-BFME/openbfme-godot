#!/usr/bin/env python3
"""Lane GHPR-1: community pull requests land with the release that carries them (docs/RELEASE.md, "Community pull requests").

The public repository is generated from the rebuild's archive branch: each release syncs a tree to the public main (autorelease.sh step 2).
A community pull request against the public main is reviewed, then ported into the archive branch with its author kept; the next sync
carries its changes. So that the contributor's pull request shows as merged (and is not silently overwritten), autorelease.sh merges each
reviewed pull request on GitHub FIRST, right before the sync, after proving that its changes are already in the audited public tree being
synced, and the sync PR's body credits it ("Includes #37 by @login").

The operator's reviewed list is private state, never in the public tree: $OPENBFME_RELEASE_STATE/community-prs.tsv, one line per
reviewed pull request (# comments and blank lines allowed):

    <pr number>\\t<reviewed head sha (40 hex)>\\t<author's GitHub login>\\t<archive commit that carries the port (40 hex)>

A pull request whose port commit is not an ancestor of the released archive sha waits for a later release (printed, not merged). Every
other listed one must pass every check below, or the run stops before anything is written to GitHub (fail closed):
  * GitHub's own record (gh api repos/<repo>/pulls/<n>): the same number, base `main` of the target repository, the reviewed head sha
    (a push after the review means a new review), the listed author, not a draft; open and mergeable (GitHub's `mergeable` true; not yet
    computed counts as no), or already merged with a merge commit on the public main (a run that stopped after merging, or a merge by
    hand); closed without a merge is refused;
  * the head fetched from the public repository (refs/pull/<n>/head) is the reviewed sha;
  * containment: the pull request's whole diff (from its merge base with the public main to its head, binary included, no rename
    detection) applies IN REVERSE, exactly (git apply --check -R, no fuzz), to the public tree being synced. So every line it adds is in
    the tree with its context, every file it adds is there byte for byte, every file it deletes is absent. A port that changed the pull
    request, a later commit that rewrote the same lines, or a path the sync leaves out (archive/, excluded paths) is refused: the operator
    asks the author to update the pull request (or pushes the port to it), reviews the new head and lists that.
Merging (real runs only): every pull request is checked before the first merge; then each open one is merged with a merge commit
(`gh pr merge --merge --match-head-commit <reviewed sha>`, so a push in between makes GitHub refuse), read back (a lost answer is read
back, not retried) and its merge commit must be on the public main with the reviewed head as its second parent. After the sync's own
merge, `record` writes each merged one to $OPENBFME_RELEASE_STATE/community-landed.tsv (the next release skips it).

    python3 tools/release/community_prs.py [--git-dir DIR] --repo OWNER/NAME [--remote origin] [--gh gh] [--state DIR] <command>
      plan --sha SHA                     what a release of SHA would merge and credit, every check run (GitHub read, never written)
      credits --sha SHA --md F --json J  the pending pull requests from the reviewed list (no GitHub): the PR body's lines and the set
      land --json J --sha SHA --tree T --mode real|dry   check every pull request of J against the public tree T, then merge them
                                         (real) or print the merge as WOULD RUN (dry)
      tree --sha SHA                     the sync recipe's public tree of SHA (autorelease.sh --plan's .github policy check)
      record --json J --public C         real runs, after the sync: the merged ones of J (read back, their merge commits ancestors of C)
                                         go to community-landed.tsv
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import autorelease  # noqa: E402  (the sync recipe)

REVIEWED = "community-prs.tsv"
LANDED = "community-landed.tsv"
SHA = re.compile(r"[0-9a-f]{40}")
PR_NUMBER = re.compile(r"[1-9][0-9]{0,6}")
# GitHub's login grammar: letters, digits and single inner hyphens, 1 to 39 characters (it goes into Markdown and a mention: nothing else)
LOGIN = re.compile(r"[A-Za-z0-9](?:[A-Za-z0-9]|-(?=[A-Za-z0-9])){0,38}")
REPO = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
ARCHIVE_LINE = "previous codebase is kept in `archive/` as reference only.\n"
PUBLIC_ARCHIVE_LINE = "previous codebase lives on in this repository's history (tag `legacy-final`).\n"


class Refusal(Exception):
    """a check failed: the run stops before anything (more) is written to GitHub"""


def git(git_dir: str | None, *args: str, input: bytes | None = None, env: dict | None = None, check: bool = True) -> subprocess.CompletedProcess:
    r = subprocess.run(["git"] + (["-C", git_dir] if git_dir else []) + list(args), input=input, capture_output=True,
                       env={**os.environ, **(env or {})})
    if check and r.returncode:
        raise Refusal(f"git {' '.join(args[:3])} failed: {r.stderr.decode(errors='replace').strip()}")
    return r


def out(r: subprocess.CompletedProcess) -> str:
    return r.stdout.decode().strip()


# ---- the lists ------------------------------------------------------------------------------------------------------------------------
def read_reviewed(path: Path) -> list[dict]:
    """the operator's reviewed list: [] when the file does not exist (nothing reviewed; announced by the caller); a malformed line,
    a field outside its grammar or a pull request listed twice is an error"""
    if not path.exists():
        return []
    entries, seen = [], set()
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        f = line.split("\t")
        if len(f) != 4 or not PR_NUMBER.fullmatch(f[0]) or not SHA.fullmatch(f[1]) or not LOGIN.fullmatch(f[2]) or not SHA.fullmatch(f[3]):
            raise Refusal(f"{path.name}:{n} is not `<pr number>\\t<head sha>\\t<login>\\t<archive commit>` (40 lower-case hex shas, a GitHub login)")
        if f[0] in seen:
            raise Refusal(f"{path.name}:{n} lists #{f[0]} twice")
        seen.add(f[0])
        entries.append({"pr": int(f[0]), "head": f[1], "login": f[2], "port": f[3]})
    return entries


def read_landed(path: Path) -> dict[int, str]:
    """{pr: head} of the pull requests an earlier release landed"""
    if not path.exists():
        return {}
    landed = {}
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        f = line.split("\t")
        if len(f) != 6 or not PR_NUMBER.fullmatch(f[0]) or not SHA.fullmatch(f[1]) or int(f[0]) in landed:
            raise Refusal(f"{path.name}:{n} is not a landed record (or lists a pull request twice)")
        landed[int(f[0])] = f[1]
    return landed


def pending(git_dir: str | None, state: Path, sha: str) -> tuple[list[dict], list[str]]:
    """(the pull requests this release lands, notes on the others): reviewed, not landed yet, the port commit an ancestor of sha"""
    reviewed, landed = read_reviewed(state / REVIEWED), read_landed(state / LANDED)
    todo, notes = [], []
    if not (state / REVIEWED).exists():
        notes.append(f"COMMUNITY: no reviewed pull requests ({REVIEWED} does not exist)")
    for e in reviewed:
        if e["pr"] in landed:
            if landed[e["pr"]] != e["head"]:
                raise Refusal(f"#{e['pr']} landed earlier with head {landed[e['pr']][:10]}, and is listed again with {e['head'][:10]}")
            notes.append(f"COMMUNITY: #{e['pr']} landed with an earlier release (remove it from {REVIEWED})")
            continue
        if git(git_dir, "cat-file", "-e", f"{e['port']}^{{commit}}", check=False).returncode:
            raise Refusal(f"#{e['pr']}: its port commit {e['port'][:10]} is not a commit of this repository")
        if git(git_dir, "merge-base", "--is-ancestor", e["port"], sha, check=False).returncode:
            notes.append(f"COMMUNITY: #{e['pr']} waits for a later release (its port {e['port'][:10]} is not in {sha[:10]})")
            continue
        todo.append(e)
    return todo, notes


def credit_lines(prs: list[dict]) -> str:
    """the sync PR body's credits (numbers and logins are grammar-checked, so nothing here needs escaping)"""
    if not prs:
        return ""
    return ("Community pull requests in this release, merged before this sync with their authors' commits:\n\n"
            + "".join(f"- Includes #{p['pr']} by @{p['login']}\n" for p in prs))


# ---- the public tree ------------------------------------------------------------------------------------------------------------------
def public_tree(git_dir: str | None, sha: str) -> str:
    """the sync recipe's public tree of an archive sha: PUBLISH-2's autorelease.sync_tree() (archive/ and sync_exclude.txt left out) once it
    is merged; until then autorelease.sh's own recipe (the sha's tree without archive/, README.md's archive line replaced). `land` checks
    that the tree autorelease.sh built is this one, so the two cannot drift apart unnoticed."""
    if hasattr(autorelease, "sync_tree"):
        return autorelease.sync_tree(git_dir, sha)
    readme = git(git_dir, "cat-file", "blob", f"{sha}:README.md").stdout.decode("utf-8")
    if readme.count(ARCHIVE_LINE) != 1:
        raise Refusal("README.md of the archive sha does not have the archive line exactly once (the sync recipe replaces it)")
    blob = out(git(git_dir, "hash-object", "-w", "--stdin", input=readme.replace(ARCHIVE_LINE, PUBLIC_ARCHIVE_LINE).encode()))
    entries = [e for e in git(git_dir, "ls-tree", "-z", sha).stdout.decode().split("\0") if e and e.split("\t", 1)[1] not in ("archive", "README.md")]
    entries.append(f"100644 blob {blob}\tREADME.md")
    return out(git(git_dir, "mktree", "-z", input="".join(f"{e}\0" for e in entries).encode()))


def containment_problem(git_dir: str | None, base: str, head: str, tree: str) -> str:
    """'' when the diff base..head is contained in tree (it applies in reverse: its lines exactly, one line of context), else what is not"""
    diff = git(git_dir, "-c", "core.quotepath=false", "diff", "--binary", "--full-index", "--no-renames", "--no-ext-diff", "--no-textconv",
               "--src-prefix=a/", "--dst-prefix=b/", base, head).stdout
    if not diff.strip():
        return "it changes nothing"
    with tempfile.TemporaryDirectory() as tmp:
        env = {"GIT_INDEX_FILE": str(Path(tmp, "index"))}
        git(git_dir, "read-tree", tree, env=env)
        # -C1: every line the pull request adds or removes must match exactly, but only one line of context on each side: the archive's
        # port may sit next to a line a later lane changed (#336: a later include three lines below its CMake line refused 7 nightlies)
        r = git(git_dir, "apply", "--cached", "--check", "-R", "-p1", "-C1", "--whitespace=nowarn", "-", input=diff, env=env, check=False)
    if r.returncode:
        lines = [x for x in r.stderr.decode(errors="replace").splitlines() if x.strip()][:6]
        return "its changes are not all in the public tree being synced: " + "; ".join(lines)
    # git apply only warns about a file mode that differs: every path the pull request leaves must be in the tree with its new mode, and
    # every path it deletes absent
    raw = git(git_dir, "diff", "--raw", "-z", "--no-renames", "--no-abbrev", base, head).stdout.decode("utf-8", "replace").split("\0")
    for meta, path in zip(raw[0::2], raw[1::2]):
        if not meta:
            continue
        new_mode, status = meta.split()[1], meta.split()[4]
        r = git(git_dir, "ls-tree", "-z", tree, "--", path)
        have = r.stdout.decode("utf-8", "replace").split(" ", 1)[0] if r.stdout else ""
        if status == "D" and have:
            return f"its changes are not all in the public tree being synced: {path} is deleted by it and present in the tree"
        if status != "D" and have != new_mode:
            return f"its changes are not all in the public tree being synced: {path} has mode {have or 'none'} in the tree, {new_mode} in it"
    return ""


# ---- GitHub ---------------------------------------------------------------------------------------------------------------------------
class GitHub:
    def __init__(self, gh: str, repo: str):
        if not REPO.fullmatch(repo):
            raise Refusal(f"not a repository name: {repo}")
        self.gh, self.repo = gh, repo

    def pull(self, n: int) -> dict:
        r = subprocess.run([self.gh, "api", f"repos/{self.repo}/pulls/{n}"], capture_output=True)
        if r.returncode:
            raise Refusal(f"#{n}: cannot read the pull request from GitHub (fail closed): {r.stderr.decode(errors='replace').strip()[:200]}")
        try:
            p = json.loads(r.stdout)
        except ValueError:
            raise Refusal(f"#{n}: GitHub's answer is not JSON (fail closed)") from None
        if not isinstance(p, dict):
            raise Refusal(f"#{n}: GitHub's answer is not a pull request (fail closed)")
        return p

    def merge(self, n: int, head: str) -> int:
        return subprocess.run([self.gh, "pr", "merge", str(n), "--repo", self.repo, "--merge", "--match-head-commit", head]).returncode


def _field(p: dict, *keys: str):
    for k in keys:
        p = p.get(k) if isinstance(p, dict) else None
    return p


def check_record(e: dict, p: dict, repo: str) -> str:
    """'open' or 'merged' from GitHub's record of the pull request, or Refusal"""
    n = e["pr"]
    if p.get("number") != n:
        raise Refusal(f"#{n}: GitHub answered for another pull request ({p.get('number')!r})")
    base_repo = _field(p, "base", "repo", "full_name")
    if _field(p, "base", "ref") != "main" or not isinstance(base_repo, str) or base_repo.lower() != repo.lower():
        raise Refusal(f"#{n}: it is not a pull request against main of {repo}")
    login = _field(p, "user", "login")
    if not isinstance(login, str) or login.lower() != e["login"].lower():
        raise Refusal(f"#{n}: its author on GitHub is not the reviewed one (@{e['login']})")
    if _field(p, "head", "sha") != e["head"]:
        raise Refusal(f"#{n}: its head on GitHub is not the reviewed sha {e['head'][:10]} (pushed to after the review? review the new head and list it)")
    if p.get("draft") is not False:
        raise Refusal(f"#{n}: it is a draft (or GitHub does not say it is not)")
    state, merged = p.get("state"), p.get("merged")
    if state == "closed" and merged is True:
        if not isinstance(p.get("merge_commit_sha"), str) or not SHA.fullmatch(p["merge_commit_sha"]):
            raise Refusal(f"#{n}: merged on GitHub without a merge commit sha")
        return "merged"
    if state == "open" and merged is False:
        if p.get("mergeable") is not True:
            raise Refusal(f"#{n}: GitHub does not say it can be merged (mergeable: {p.get('mergeable')!r}; null means not computed yet: run again)")
        return "open"
    raise Refusal(f"#{n}: it is {state!r} and not merged on GitHub (closed pull requests are not landed: remove it from {REVIEWED})")


class Lander:
    def __init__(self, git_dir: str | None, gh: GitHub, remote: str):
        self.git_dir, self.gh, self.remote = git_dir, gh, remote

    def fetch_main(self) -> str:
        git(self.git_dir, "fetch", "-q", "--no-tags", self.remote, "main")
        return out(git(self.git_dir, "rev-parse", f"refs/remotes/{self.remote}/main"))

    def fetch_head(self, e: dict) -> None:
        ref = f"refs/community-prs/{e['pr']}"
        git(self.git_dir, "fetch", "-q", "--no-tags", self.remote, f"+refs/pull/{e['pr']}/head:{ref}")
        if out(git(self.git_dir, "rev-parse", ref)) != e["head"]:
            raise Refusal(f"#{e['pr']}: the head fetched from {self.remote} is not the reviewed sha {e['head'][:10]}")

    def check(self, e: dict, tree: str, main: str) -> tuple[str, dict]:
        """('open' | 'merged', GitHub's record) once every check passed, else Refusal"""
        p = self.gh.pull(e["pr"])
        status = check_record(e, p, self.gh.repo)
        self.fetch_head(e)
        if status == "merged":
            mc = p["merge_commit_sha"]
            if git(self.git_dir, "cat-file", "-e", f"{mc}^{{commit}}", check=False).returncode or \
                    git(self.git_dir, "merge-base", "--is-ancestor", mc, main, check=False).returncode:
                raise Refusal(f"#{e['pr']}: its merge commit {mc[:10]} is not on {self.remote}/main")
            base = out(git(self.git_dir, "merge-base", e["head"], f"{mc}^1"))
        else:
            r = git(self.git_dir, "merge-base", e["head"], main, check=False)
            if r.returncode:
                raise Refusal(f"#{e['pr']}: its head shares no history with {self.remote}/main")
            base = out(r)
        why = containment_problem(self.git_dir, base, e["head"], tree)
        if why:
            raise Refusal(f"#{e['pr']} by @{e['login']}: {why}")
        return status, p

    def check_all(self, prs: list[dict], tree: str) -> list[tuple[dict, str]]:
        main = self.fetch_main()
        return [(e, self.check(e, tree, main)[0]) for e in prs]

    def merge(self, e: dict) -> str:
        """merge one checked open pull request; its merge commit, read back"""
        rc = self.gh.merge(e["pr"], e["head"])
        if rc:
            print(f"COMMUNITY: gh pr merge #{e['pr']} failed or its answer was lost: reading it back")
        p = self.gh.pull(e["pr"])
        mc = p.get("merge_commit_sha")
        if p.get("merged") is not True or not isinstance(mc, str) or not SHA.fullmatch(mc) or _field(p, "head", "sha") != e["head"]:
            raise Refusal(f"#{e['pr']} is not merged with the reviewed head (gh pr merge exit {rc})")
        main = self.fetch_main()
        if git(self.git_dir, "merge-base", "--is-ancestor", mc, main, check=False).returncode:
            raise Refusal(f"#{e['pr']}: its merge commit {mc[:10]} is not on {self.remote}/main")
        if out(git(self.git_dir, "rev-parse", f"{mc}^2", check=False)) != e["head"]:
            raise Refusal(f"#{e['pr']}: its merge commit {mc[:10]} does not have the reviewed head as its second parent")
        return mc


def would(*cmd: str) -> None:
    import shlex  # noqa: PLC0415
    print("WOULD RUN: " + " ".join(shlex.quote(c) for c in cmd))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--git-dir")
    ap.add_argument("--repo", required=True)
    ap.add_argument("--remote", default="origin")
    ap.add_argument("--gh", default=os.environ.get("GH", "gh"))
    ap.add_argument("--state", type=Path, default=Path(os.environ.get("OPENBFME_RELEASE_STATE", Path.home() / ".local/state/openbfme-release")))
    sub = ap.add_subparsers(dest="cmd", required=True)
    pl = sub.add_parser("plan")
    pl.add_argument("--sha", required=True)
    cr = sub.add_parser("credits")
    cr.add_argument("--sha", required=True)
    cr.add_argument("--md", type=Path, required=True)
    cr.add_argument("--json", type=Path, required=True)
    la = sub.add_parser("land")
    la.add_argument("--json", type=Path, required=True)
    la.add_argument("--sha", required=True)
    la.add_argument("--tree", required=True)
    la.add_argument("--mode", choices=("real", "dry"), required=True)
    tr = sub.add_parser("tree", help="print the sync recipe's public tree of an archive sha (written to the object store)")
    tr.add_argument("--sha", required=True)
    rc = sub.add_parser("record")
    rc.add_argument("--json", type=Path, required=True)
    rc.add_argument("--public", required=True)
    a = ap.parse_args(argv)
    try:
        return run(a)
    except Refusal as e:
        print(f"COMMUNITY REFUSED: {e}")
        return 1


def run(a) -> int:
    if a.cmd in ("plan", "credits", "land", "tree"):
        a.sha = out(git(a.git_dir, "rev-parse", "--verify", f"{a.sha}^{{commit}}"))
    if a.cmd == "tree":
        print(public_tree(a.git_dir, a.sha))
        return 0
    if a.cmd == "credits":
        prs, notes = pending(a.git_dir, a.state, a.sha)
        for n in notes:
            print(n)
        a.md.write_text(credit_lines(prs), encoding="utf-8")
        a.json.write_text(json.dumps(prs), encoding="utf-8")
        print(f"COMMUNITY: {len(prs)} reviewed pull request(s) to land with this release" + "".join(f", #{p['pr']} by @{p['login']}" for p in prs))
        return 0
    if a.cmd == "plan":
        prs, notes = pending(a.git_dir, a.state, a.sha)
        for n in notes:
            print(n)
        if not prs:
            print("COMMUNITY PLAN: no pull request to merge before the sync")
            return 0
        lander = Lander(a.git_dir, GitHub(a.gh, a.repo), a.remote)
        for e, status in lander.check_all(prs, public_tree(a.git_dir, a.sha)):
            print(f"COMMUNITY PLAN: #{e['pr']} by @{e['login']} (head {e['head'][:10]}): its changes are in the public tree; "
                  + ("would be merged before the sync" if status == "open" else "already merged on GitHub"))
        print("COMMUNITY PLAN: the sync PR's body would say:\n" + credit_lines(prs).rstrip())
        return 0
    prs = json.loads(a.json.read_text(encoding="utf-8"))
    if not isinstance(prs, list) or not all(isinstance(p, dict) and set(p) == {"pr", "head", "login", "port"} for p in prs):
        raise Refusal(f"{a.json.name} is not the credits step's list")
    lander = Lander(a.git_dir, GitHub(a.gh, a.repo), a.remote)
    if a.cmd == "land":
        if not prs:
            print("COMMUNITY: no pull request to merge before the sync")
            return 0
        if public_tree(a.git_dir, a.sha) != a.tree:
            raise Refusal(f"the tree to sync ({a.tree[:10]}) is not the sync recipe's public tree of {a.sha[:10]}")
        checked = lander.check_all(prs, a.tree)   # every pull request passes before the first merge
        for e, status in checked:
            if status == "merged":
                print(f"COMMUNITY: #{e['pr']} by @{e['login']} is already merged on GitHub")
            elif a.mode == "dry":
                would(a.gh, "pr", "merge", str(e["pr"]), "--repo", a.repo, "--merge", "--match-head-commit", e["head"])
            else:
                print(f"COMMUNITY: merged #{e['pr']} by @{e['login']}: {lander.merge(e)}")
        return 0
    # record
    landed_path = a.state / LANDED
    landed = read_landed(landed_path)
    lines = []
    for e in prs:
        if e["pr"] in landed:
            continue
        p = lander.gh.pull(e["pr"])
        mc = p.get("merge_commit_sha")
        if p.get("merged") is not True or not isinstance(mc, str) or not SHA.fullmatch(mc) or \
                git(a.git_dir, "merge-base", "--is-ancestor", mc, a.public, check=False).returncode:
            print(f"COMMUNITY: #{e['pr']} is not merged into {a.public[:10]}: not recorded (it stays in the reviewed list)")
            continue
        lines.append(f"{e['pr']}\t{e['head']}\t{e['login']}\t{mc}\t{a.public}\t{datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')}\n")
    if lines:
        with open(landed_path, "a", encoding="utf-8") as f:
            f.writelines(lines)
    print(f"COMMUNITY: {len(lines)} pull request(s) recorded as landed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
