#!/usr/bin/env python3
"""Lane NIGHTLY-1 (owner, 2026-10-21): the nightly build's notes for players, and the nightly releases kept on GitHub.

"build the pulled in PRs every night with patch notes and what to test for for people". Twice a day (lane NIGHTLY-2, owner 2026-10-23:
07:00 and 19:00 UTC) autorelease.sh --nightly publishes the archive head as a GitHub pre-release v<x>.<y>.<z>-nightly.<YYMMDD><1|2>00
(autorelease.py nightly-version; 1 the morning build, 2 the evening one) whose notes say, in plain words, what is new since the last
nightly (the morning's or the evening's, whichever was published last) and how to test each change ("What to test"), community pull requests first and credited by their GitHub
handle (owner, 15:30Z: "Thanks to @handle: ..."). docs/RELEASE.md, "Nightly builds".

    python3 tools/release/nightly_notes.py [--git-dir DIR] <command>
      context --from A --to B --repo OWNER/NAME [--gh gh] --out CTX      the changes of A..B (first-parent), deterministic: their
                  Release-note / Test lines, whether they touch what a player runs, the subjects of the branch each merge brought in, and
                  the community pull requests they carry (a `(cherry picked from commit X)` line whose X GitHub links to a pull request of
                  OWNER/NAME: its number, title and author's login, read from GitHub; a lookup that fails other than "no such commit" is an
                  error, never a silent "no credit")
      draft --context CTX --out DRAFT                                    the plain-language text and the test steps of the changes, written
                  by the operator's model command ($OPENBFME_NIGHTLY_LLM, e.g. "claude -p --model sonnet": the prompt on stdin, JSON out),
                  then checked (check_draft); a draft that fails the checks is sent back once with the problems, then the run fails
      render --context CTX --draft DRAFT --version V ... --out NOTES     the notes (Markdown, the launcher shows them as text)
      prune --repo OWNER/NAME [--gh gh] --keep N [--dry-run]             delete the published nightly releases older than the newest N
                  (the releases only: their tags stay, the repository's ruleset forbids deleting tags, GH013)
The override rule: a change whose merge commit has `Release-note:` lines is described by exactly those lines (`Release-note: none` hides
it); `Test:` lines there are its test steps. The model writes only what those lines do not say. Every text the model writes passes the
jargon rule of the patch notes (autorelease.py JARGON: no lane ids, gate words, addresses, file names or code names) and the length limits
here; the whole notes then pass autorelease.py audit-text with the private denylist like every published text.
Why prune: every launcher reads ONE page of the release list (repos/<repo>/releases?per_page=30, updater.gd); nightlies left to pile up
would push the stable releases off that page, so at most KEEP (7, owner) nightlies stay.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import autorelease as ar  # noqa: E402

KEEP = 7
KINDS = tuple(k for k, _ in ar.NOTE_KINDS)
MAX_TEXT = 200
MAX_STEP = 240
MAX_STEPS = 6
MAX_INNER = 60
TEST_LINE = re.compile(r"^Test:\s*(.*)$", re.I)
CHERRY = re.compile(r"^\(cherry picked from commit ([0-9a-f]{40})\)$", re.M)
LOGIN = re.compile(r"[A-Za-z0-9](?:[A-Za-z0-9]|-(?=[A-Za-z0-9])){0,38}")
# a model text must read as plain words: no markup a page would render, no mention, issue reference or link (credits are added here)
NOT_PLAIN = re.compile(r"[`*_\[\]<>|~@#\\]|://|www\.")


def git(git_dir: str | None, *args: str) -> str:
    return ar.git(git_dir, *args)


# ---- context ------------------------------------------------------------------------------------------------------------------------
def test_lines(body: str) -> list[str]:
    return [m.group(1).strip() for m in (TEST_LINE.match(ln.strip()) for ln in body.splitlines()) if m and m.group(1).strip()]


def inner_commits(git_dir: str | None, sha: str) -> list[dict]:
    """the commits a merge brought in (its second parent's side), newest first, at most MAX_INNER; [] for a commit that is not a merge"""
    parents = git(git_dir, "rev-list", "--parents", "-n", "1", sha).split()[1:]
    if len(parents) < 2:
        return []
    out = git(git_dir, "log", f"--max-count={MAX_INNER}", "--format=%H%x1f%s%x1f%b%x1e", f"{parents[0]}..{parents[1]}")
    res = []
    for rec in out.split("\x1e"):
        rec = rec.strip("\n")
        if rec:
            h, subject, body = rec.split("\x1f", 2)
            res.append({"sha": h, "subject": subject, "body": body})
    return res


class GitHub:
    def __init__(self, gh: str, repo: str, attempts: int = 4, backoff: float = 5.0):
        self.gh, self.repo, self.cache = gh, repo, {}
        self.attempts, self.backoff = max(1, attempts), backoff

    def pulls_of(self, sha: str) -> list[dict]:
        """the pull requests of the repository GitHub links to commit sha; [] when GitHub does not know the commit (an internal cherry-pick)"""
        if sha in self.cache:
            return self.cache[sha]
        # a nightly reads one commit per call (hundreds after a batch); GitHub sometimes answers with an empty body ("unexpected end of
        # JSON input", nightly 2026-10-22): try again, and fail closed only when every attempt fails
        for attempt in range(self.attempts):
            r = subprocess.run([self.gh, "api", f"repos/{self.repo}/commits/{sha}/pulls"], capture_output=True, text=True)
            if not r.returncode:
                try:
                    pulls = json.loads(r.stdout)
                except json.JSONDecodeError:
                    error = "invalid JSON (expected a JSON array)"
                else:
                    if isinstance(pulls, list):
                        break
                    error = "expected a JSON array"
            elif re.search(r"HTTP (404|422)", r.stderr):
                self.cache[sha] = []
                return []
            else:
                error = r.stderr.strip()[:200]
            if attempt + 1 < self.attempts:
                time.sleep(self.backoff * (attempt + 1))
        else:
            raise SystemExit(f"nightly_notes: cannot read the pull requests of commit {sha[:10]} from GitHub (fail closed): "
                             f"{error}")
        out = []
        for p in pulls:
            login = str((p.get("user") or {}).get("login", ""))
            base = (p.get("base") or {}).get("repo") or {}
            if base.get("full_name") != self.repo or not LOGIN.fullmatch(login) or not isinstance(p.get("number"), int):
                continue
            out.append({"pr": p["number"], "login": login, "title": str(p.get("title", ""))[:200]})
        self.cache[sha] = out
        return out


def build_context(git_dir: str | None, a: str, b: str, github: GitHub) -> dict:
    changes = []
    for c in ar.first_parent_commits(git_dir, a, b):
        notes = ar.release_note_texts(c["body"])
        inner = inner_commits(git_dir, c["sha"])
        prs: dict[int, dict] = {}
        for e in [c] + inner:
            for x in sorted(set(CHERRY.findall(e["body"]))):
                for p in github.pulls_of(x):
                    q = prs.setdefault(p["pr"], dict(p, subjects=[]))
                    if e is not c and e["subject"] not in q["subjects"]:
                        q["subjects"].append(e["subject"])
        changes.append({
            "sha": c["sha"],
            "subject": c["subject"],
            "notes": notes,
            "hidden": any(not t or t.lower() == "none" for t in notes),
            "tests": test_lines(c["body"]),
            "player": any(f.startswith(ar.PLAYER_PATHS) and not f.startswith(ar.TEST_PATHS) for f in c["files"]),
            "files": c["files"][:40],
            "inner": [e["subject"] for e in inner],
            "community": [prs[n] for n in sorted(prs)],
        })
    return {"from": a, "to": b, "changes": changes}


# ---- the model's draft ----------------------------------------------------------------------------------------------------------------
def needs(ch: dict) -> str:
    """what the model must write for a change: 'none' (hidden or nothing a player runs), 'steps' (its Release-note lines say what; no Test
    lines), 'all' (text and steps) or 'judge' (it may also say a player cannot notice it)"""
    if ch["hidden"] or (not ch["player"] and not ch["notes"]):
        return "none"
    if ch["notes"]:
        return "none" if ch["tests"] else "steps"
    return "judge"


def units(ctx: dict) -> list[dict]:
    """what the notes describe, in the merges' order: a change, or (owner 15:30Z: community pull requests credited one by one) each
    community pull request of a change that has no Release-note lines; id, need (needs(); 'all': text and steps, always shown), credits"""
    out = []
    for ch in ctx["changes"]:
        if ch["community"] and not ch["notes"] and not ch["hidden"]:
            out += [{"id": f"{ch['sha']}:pr{p['pr']}", "change": ch, "need": "all", "credits": [p], "pr": p} for p in ch["community"]]
        else:
            out.append({"id": ch["sha"], "change": ch, "need": needs(ch), "credits": ch["community"], "pr": None})
    return out


def prompt(ctx: dict, problems: list[str] = ()) -> str:
    todo = []
    for u in units(ctx):
        ch, need = u["change"], u["need"]
        if need == "none":
            continue
        what = {"steps": "test steps only", "all": "kind, area, text and test steps"}.get(need, "visible, kind, area, text and test steps")
        if u["pr"]:
            todo.append({"id": u["id"], "write": what, "community_pull_request_title": u["pr"]["title"], "commits": u["pr"]["subjects"][:40]})
        else:
            todo.append({"id": u["id"], "write": what, "release_notes": ch["notes"], "merge_subject": ch["subject"],
                         "commits": ch["inner"][:40], "files": ch["files"]})
    rules = f"""You write the player-facing part of the patch notes of a nightly test build of OpenBFME, a free reimplementation of the game
The Lord of the Rings: The Battle for Middle-earth II / The Rise of the Witch-king that runs on the player's own game files. Readers are
players and testers, not developers.

For every change below, return one JSON object in "changes" with its "id" and:
- "visible": false only when a player cannot notice the change at all (internal tests, tooling); then nothing else.
- "kind": one of {", ".join(KINDS)}; "area": one of {", ".join(ar.AREAS)}, or "" if none fits. (Only when asked to write them.)
- "text": one plain English sentence (at most {MAX_TEXT} characters) saying what changed for the player. (Only when asked.)
- "test": 1 to {MAX_STEPS} concrete steps a tester can follow in the game to check the change, each at most {MAX_STEP} characters,
  e.g. "Start a skirmish as Isengard.", "Build a lumber mill and check it earns resources." Name menus, factions, units and
  buildings as the game shows them.
When "release_notes" is given, those lines already say what changed: write only "test" for them.
Never mention: internal names or ids (lane names, ticket or test ids like ABC-1 or S-123), memory addresses, file or function names,
code identifiers, branches, commits, gates, reviews, machines, people's names or user names, or links. No Markdown, no @ or #.
Return ONLY the JSON: {{"changes": [...]}}."""
    text = rules + "\n\nThe changes:\n" + json.dumps(todo, indent=1, ensure_ascii=False)
    if problems:
        text += "\n\nYour previous answer had these problems; fix them and return the whole JSON again:\n" + "\n".join(f"- {p}" for p in problems)
    return text


def _plain_problem(what: str, t) -> str:
    if not isinstance(t, str) or not t.strip():
        return f"{what} is empty or not text"
    t = t.strip()
    if ar.JARGON.search(t):
        return f"{what} has a word players do not know (an id, code name, file name or internal term): {ar.JARGON.search(t).group(0)!r}"
    if NOT_PLAIN.search(t):
        return f"{what} has markup, a mention, a link or a reference ({NOT_PLAIN.search(t).group(0)!r})"
    if any(not ch.isprintable() for ch in t):
        return f"{what} has control characters"
    return ""


def check_draft(ctx: dict, draft) -> list[str]:
    """every problem of the model's draft (an empty list: it can be rendered)"""
    if not isinstance(draft, dict) or not isinstance(draft.get("changes"), list):
        return ['the answer is not {"changes": [...]}']
    want = {u["id"]: u for u in units(ctx) if u["need"] != "none"}
    got: dict[str, dict] = {}
    p = []
    for e in draft["changes"]:
        if not isinstance(e, dict) or e.get("id") not in want:
            p.append(f"an entry names no change that was asked for: {str(e.get('id') if isinstance(e, dict) else e)[:60]!r}")
            continue
        if e["id"] in got:
            p.append(f"{e['id'][:16]} is answered twice")
        got[e["id"]] = e
    for uid, u in want.items():
        sha = uid[:10] + uid[40:]
        e = got.get(uid)
        if e is None:
            p.append(f"{sha} has no entry")
            continue
        need = u["need"]
        if need == "judge" and not isinstance(e.get("visible"), bool):
            p.append(f"{sha}: visible is not true or false")
            continue
        if need == "judge" and not e["visible"]:
            continue
        if need in ("judge", "all"):
            if e.get("kind") not in KINDS:
                p.append(f"{sha}: kind is not one of {', '.join(KINDS)}")
            if e.get("area", "") not in ar.AREAS + ("",):
                p.append(f"{sha}: area is not one of the areas")
            w = _plain_problem(f"{sha}: text", e.get("text"))
            if w:
                p.append(w)
            elif len(e["text"].strip()) > MAX_TEXT:
                p.append(f"{sha}: text is longer than {MAX_TEXT} characters")
        steps = e.get("test")
        if not isinstance(steps, list) or not 1 <= len(steps) <= MAX_STEPS:
            p.append(f"{sha}: test is not a list of 1 to {MAX_STEPS} steps")
            continue
        for i, s in enumerate(steps, 1):
            w = _plain_problem(f"{sha}: test step {i}", s)
            if w:
                p.append(w)
            elif len(s.strip()) > MAX_STEP:
                p.append(f"{sha}: test step {i} is longer than {MAX_STEP} characters")
    return p


def _json_of(answer: str):
    i, j = answer.find("{"), answer.rfind("}")
    if i < 0 or j < i:
        return None
    try:
        return json.loads(answer[i:j + 1])
    except json.JSONDecodeError:
        return None


def run_model(command: str, text: str) -> str:
    r = subprocess.run(shlex.split(command), input=text, capture_output=True, text=True, timeout=900)
    if r.returncode:
        raise SystemExit(f"nightly_notes: the model command failed (exit {r.returncode}): {r.stderr.strip()[-300:]}")
    return r.stdout


def make_draft(ctx: dict, command: str, tries: int = 2) -> dict:
    if not any(u["need"] != "none" for u in units(ctx)):
        return {"changes": []}
    problems: list[str] = []
    for _ in range(tries):
        draft = _json_of(run_model(command, prompt(ctx, problems)))
        problems = check_draft(ctx, draft) if draft is not None else ["the answer holds no JSON object"]
        if not problems:
            return draft
    raise SystemExit("nightly_notes: the model's draft still fails the checks after a second try (nothing is published):\n"
                     + "\n".join(f"  {x}" for x in problems[:20]))


# ---- render ---------------------------------------------------------------------------------------------------------------------------
def entries(ctx: dict, draft: dict) -> list[dict]:
    """the notes' changes in page order (community ones first, then the order of the merges): kind, area, text lines, test steps, credits"""
    problems = check_draft(ctx, draft)
    if problems:
        raise SystemExit("nightly_notes: the draft does not fit this context:\n" + "\n".join(f"  {x}" for x in problems[:20]))
    got = {e["id"]: e for e in draft["changes"]}
    out = []
    for u in units(ctx):
        ch = u["change"]
        if ch["hidden"] or (u["need"] == "none" and not ch["notes"]):
            continue
        e = got.get(u["id"], {})
        if ch["notes"]:
            items = [ar.parse_note(t) for t in ch["notes"]]
        elif u["need"] == "all" or e.get("visible"):
            items = [(e["kind"], e.get("area", ""), e["text"].strip())]
        else:
            continue
        steps = ch["tests"] or [s.strip() for s in e.get("test", [])]
        out.append({"items": [(k, a, ar.neutral(t)) for k, a, t in items], "test": [ar.neutral(s) for s in steps],
                    "credits": u["credits"]})
    return [x for x in out if x["credits"]] + [x for x in out if not x["credits"]]


def credit_text(credits: list[dict]) -> str:
    """"Thanks to @login (#12)": logins and numbers come from GitHub's record and are grammar-checked (the only mentions and references the
    notes hold: the contributors are meant to see them)"""
    by: dict[str, list[int]] = {}
    for p in credits:
        if not LOGIN.fullmatch(p["login"]) or not isinstance(p["pr"], int):
            raise SystemExit("nightly_notes: a credit is not a GitHub login and pull request number")
        by.setdefault(p["login"], []).append(p["pr"])
    return "Thanks to " + ", ".join(f"@{who} ({', '.join(f'#{n}' for n in sorted(set(prs)))})" for who, prs in by.items())


def render(ctx: dict, draft: dict, version: str, known_issues: list, repo: str, commit: str, date: str, legacy_launchers: bool,
           mac: bool, mac_manifest: bool) -> tuple[str, list[str], list[str]]:
    """(notes, the plain item lines, every source text) of a nightly"""
    ar.nightly_date(version)
    es = entries(ctx, draft)
    md = lambda t: ar.markdown(ar.neutral(t))  # noqa: E731
    when = ar._date_text(date) if date else ""
    out = [f"# OpenBFME {ar.display_name(version)}", "", f"{when} · nightly build for testers" if when else "Nightly build for testers", "",
           "A nightly build holds everything merged since the last one. It is made automatically twice a day and only lightly tested, so "
           "expect new bugs. OpenBFME is in alpha. For fewer surprises, use the Stable channel (launcher: Settings, Channel).", "",
           "Install or update it with the OpenBFME Launcher on the Nightly channel. It uses your installed The Rise of the Witch-king 2.01 "
           "and The Battle for Middle-earth II 1.06, or downloads them for you.", ""]
    out += ar.download_section(version, repo, legacy_launchers, mac, mac_manifest)
    # the community's changes first, each in the notes' own plain words (the pull requests' titles are their authors' working titles)
    community = [p for e in es for p in e["credits"]]
    if community:
        out += ["## Thanks to our community", ""] + [f"- {credit_text(e['credits'])}: {ar.markdown(e['items'][0][2])}" for e in es if e["credits"]] + [""]
    titles = ar.TITLES[ar.DEFAULT_TITLES]
    for kind in KINDS:
        rows = []
        for e in es:
            for k, a, t in e["items"]:
                if k == kind:
                    rows.append(f"- {ar.markdown(t)}" + (f" {credit_text(e['credits'])}." if e["credits"] else ""))
        if rows:
            out += [f"## {md(titles[kind])}", ""] + rows + [""]
    if not es:
        out += ["No changes you will notice in the game: this build updates internal parts.", ""]
    tests = [e for e in es if e["test"]]
    if tests:
        out += ["## What to test", ""]
        for n, e in enumerate(tests, 1):
            head = e["items"][0][2] + (f" ({len(e['items']) - 1} more change{'s' if len(e['items']) > 2 else ''} here)" if len(e["items"]) > 1 else "")
            out += [f"{n}. {ar.markdown(head)}" + (f" {credit_text(e['credits'])}." if e["credits"] else "")]
            out += [f"   - {ar.markdown(s)}" for s in e["test"]] + [""]
    out += ["## Found a problem?", "",
            f"Open an issue on GitHub or tell us on Discord. Say which build you played, {md(ar.display_name(version))}, what you did "
            "and what happened.", ""]
    issues = [("", ar.neutral(i)) if isinstance(i, str) else (i.get("area", ""), ar.neutral(i["text"])) for i in known_issues]
    if issues:
        out += ["## Known issues", ""] + ar._grouped(issues)
    if commit:
        out += [f"Built from commit {md(commit[:10])}" + (f" ({md(date)})." if date else ".")]
    lines = [t for e in es for _, _, t in e["items"]]
    sources = [t for ch in ctx["changes"] for t in ch["notes"] + ch["tests"]] + [p["title"] for p in community] + \
              [x for e in draft["changes"] for x in [e.get("text", "")] + list(e.get("test", [])) if isinstance(x, str) and x]
    return "\n".join(out).rstrip() + "\n", lines, sources


# ---- prune --------------------------------------------------------------------------------------------------------------------------------
def prune(gh: str, repo: str, keep: int, dry: bool) -> int:
    r = subprocess.run([gh, "api", "--paginate", f"repos/{repo}/releases?per_page=100",
                        "--jq", ".[] | [.id, .tag_name, .draft] | @tsv"], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"nightly_notes: cannot list the releases of {repo} (fail closed): {r.stderr.strip()[:200]}")
    nightlies = []
    for line in r.stdout.splitlines():
        rid, tag, draft = line.split("\t")
        if ar.line_of(tag) == "nightly" and draft == "false":
            nightlies.append((ar.version_key(tag), int(rid), tag))
    nightlies.sort(reverse=True)
    for _, rid, tag in nightlies[keep:]:
        if dry:
            print(f"WOULD RUN: {gh} api --method DELETE repos/{repo}/releases/{rid}   ({tag}; the tag stays)")
            continue
        d = subprocess.run([gh, "api", "--method", "DELETE", f"repos/{repo}/releases/{rid}"], capture_output=True, text=True)
        if d.returncode:
            raise SystemExit(f"nightly_notes: deleting the release {tag} failed: {d.stderr.strip()[:200]}")
        print(f"NIGHTLY prune: deleted the release {tag} (its tag stays)")
    print(f"NIGHTLY prune: {min(len(nightlies), keep)} nightly releases kept")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--git-dir")
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("context")
    c.add_argument("--from", dest="a", required=True)
    c.add_argument("--to", dest="b", required=True)
    c.add_argument("--repo", required=True)
    c.add_argument("--gh", default=os.environ.get("GH", "gh"))
    c.add_argument("--out", type=Path, required=True)
    d = sub.add_parser("draft")
    d.add_argument("--context", type=Path, required=True)
    d.add_argument("--out", type=Path, required=True)
    r = sub.add_parser("render")
    r.add_argument("--context", type=Path, required=True)
    r.add_argument("--draft", type=Path, required=True)
    r.add_argument("--version", required=True)
    r.add_argument("--known-issues", type=Path, default=HERE / "known_issues.json")
    r.add_argument("--repo", default=ar.DEFAULT_REPO)
    r.add_argument("--commit", default="")
    r.add_argument("--date", default="")
    r.add_argument("--no-legacy-launchers", action="store_true")
    r.add_argument("--mac", action="store_true")
    r.add_argument("--mac-manifest", action="store_true")
    r.add_argument("--out", type=Path, required=True)
    r.add_argument("--title-out", type=Path)
    r.add_argument("--plain-items-out", type=Path)
    r.add_argument("--sources-out", type=Path)
    p = sub.add_parser("prune")
    p.add_argument("--repo", required=True)
    p.add_argument("--gh", default=os.environ.get("GH", "gh"))
    p.add_argument("--keep", type=int, default=KEEP)
    p.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    if a.cmd == "context":
        ctx = build_context(a.git_dir, a.a, a.b, GitHub(a.gh, a.repo))
        a.out.write_text(json.dumps(ctx, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        n = sum(1 for u in units(ctx) if u["need"] != "none")
        print(f"NIGHTLY context: {len(ctx['changes'])} changes, {n} for the model, "
              f"{len({p['pr'] for ch in ctx['changes'] for p in ch['community']})} community pull requests")
        return 0
    if a.cmd == "draft":
        command = os.environ.get("OPENBFME_NIGHTLY_LLM", "")
        if not command:
            print("nightly_notes: OPENBFME_NIGHTLY_LLM is not set (the operator's model command)", file=sys.stderr)
            return 2
        draft = make_draft(json.loads(a.context.read_text(encoding="utf-8")), command)
        a.out.write_text(json.dumps(draft, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"NIGHTLY draft: {len(draft['changes'])} changes written")
        return 0
    if a.cmd == "render":
        ctx = json.loads(a.context.read_text(encoding="utf-8"))
        draft = json.loads(a.draft.read_text(encoding="utf-8"))
        text, lines, sources = render(ctx, draft, a.version, ar.load_known_issues(a.known_issues), a.repo, a.commit, a.date,
                                      not a.no_legacy_launchers, a.mac, a.mac_manifest)
        a.out.write_text(text, encoding="utf-8")
        if a.title_out:
            a.title_out.write_text(ar.title_summary(lines) + "\n", encoding="utf-8")
        if a.plain_items_out:
            a.plain_items_out.write_text("".join(f"- {x}\n" for x in lines), encoding="utf-8")
        if a.sources_out:
            a.sources_out.write_text("".join(f"{s}\n" for s in sources), encoding="utf-8")
        print(f"NOTES {a.out}: {len(lines)} items")
        return 0
    return prune(a.gh, a.repo, a.keep, a.dry_run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
