# <Lane name> — <one-line goal> (<implementor>)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md first (rules 1-10
binding). Claim row Q<N> in orchestration/queue.md (owner=<name>) as your first
commit. Exclusive tree access; no branches/worktrees; git add by explicit path;
BANNED: git add -A / reset / restore / clean / stash / amend / find-replace
sweeps. Long output → workspace/logs/<lane>-*.txt (poll by mtime; a tool timeout
is not evidence). Pinned interpreter
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe;
BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2.
Sequential pytest / sequential Godot. Do NOT rebuild/publish/select packs
unless this brief says so.

## Why
<2-5 lines: the parity gap or defect, in player terms, and why now.>

## Oracle facts (anchors verified <date> at commit <sha>)
- Retail: `workspace/retail-extract/data/ini/<file>:<line>` — quote the fields.
- Code today: `game/src/...:<line>` / `importer/openbfme_importer/...:<line>`.
- Prior reports/briefs to read: orchestration/reports/<...>.md
- Traps that apply (from AGENTS.md rules or earlier lanes).

## Deliverable
<Numbered, concrete. Name files. Say what NOT to touch. State semantics
decisions explicitly (e.g. "taper = linear to (100-t)% at edge; cite").>

## Tests — failing-first
<Which runner/pytest module gains which named checks; expected counts; which
pins move and where they are re-pinned (repo-root files only).>

## Definition of Done (verbatim outputs in the report)
1. <command> → <expected>
2. FULL importer suite (sequential) → judged failure-by-NAME vs baseline
   (orchestration/queue.md Q6 names); zero new.
3. <runners with pins>
4. `python tools\check_pack_addresses.py` PASS; `tools\gate-hygiene.ps1` PASS;
   `git status --porcelain` clean; commits prefixed `<type>(<scope>):`, explicit
   paths, no logs.
5. Report orchestration/reports/<lane>.md (use TEMPLATE-report.md).
