# Stage 3 — docs overhaul + AGENTS.md rewrite + root de-sprawl (Grok lane)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Stages 1 (rename `.private`→`workspace/`)
and 2a (ledgers→docs/state/, orchestration/queue.md, purge) are done and
verified — read orchestration/reports/stage1-verify.md (round 4) and
orchestration/reports/stage2-verify.md for the ground truth. Exclusive tree
access. Rules: no branches/worktrees; stage by explicit path; BANNED: git
add -A, git reset/restore/clean/stash/amend, sed/find-replace sweeps across
files. Every edit is a deliberate per-file edit.

## Owner intent (why this lane exists)
Agents have been paralysed by "asset firewall" prose repeated across README,
CONTRIBUTING, AGENTS.md, DIRECTION.md, docs/*. Retail-derived material lives in
`workspace/` (git-ignored, CI-scanned) — git enforces the boundary, humans and
agents should not have to think about it. Docs must say this ONCE, in one
sentence, and otherwise treat `workspace/` as a normal first-class local
dependency. Also: docs drift. A doc survives only if a tool generates it or a
gate enforces it; narrative status docs get deleted in favour of docs/state/.

## Task A — Rewrite AGENTS.md (the single agent contract, ~100–130 lines)
Keep the good bones of the current file; produce a fresh version with sections:
1. **What this is** (2 lines) + **Layout table** — game/, importer/, engine/,
   launcher/, tools/, contracts/, orchestration/ (tracked: queue.md, briefs/,
   reports/), docs/state/ (tracked live ledgers), workspace/ (git-ignored local
   retail material + packs + toolchain + logs; canonical paths in
   workspace/manifest.json). NO `.private` anywhere.
2. **Retail material — one paragraph, no fear language**: retail-derived files
   live under workspace/; use them freely; git ignores workspace/ and the
   publication-boundary CI scans tracked files for retail bytes and
   machine-absolute paths — that is the whole policy. Point to
   workspace/manifest.json for retailInstall / pinnedPython / packsRoot /
   retailExtract / logsRoot.
3. **Run the right lane** table (keep from current, paths updated): importer
   tests via run_importer_tests.bat with pinned interpreter
   workspace\retail-work\tools\python-3.12-env\Scripts\python.exe and
   BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2;
   Godot runners; gates; run_game.bat; check_pack_addresses.py. Current
   baselines: importer 6 failed / 0 errors (2026-08-17, all pre-existing —
   see orchestration/queue.md Q6); spellbook runner 218/0; dist gate 25 checks.
4. **Publishing a build** — keep the current section's substance (VERSION bump,
   Write-BuildInfo, patch notes, Publish-DistBuild + the three switches table)
   but tighten; paths → workspace.
5. **The rules that have actually bitten us** — keep all six existing rules
   (paths updated), and ADD these, learned 2026-08-17:
   7. Mechanical find-replace sweeps corrupt identifiers (`workspace_root` was
      rewritten to `.private_root` twice). Targeted edits only; oracle for a
      disputed name is `git show <pre-change-commit>:<path>`.
   8. Aggregate test counts hide regressions — judge failure-by-failure against
      the named baseline. py_compile cannot catch a runtime NameError; run the
      tests.
   9. Hash-pinned artifacts (generated profiles, evidence CSVs, contract
      policy_digest) are oracles: never edit the pin to match the file; regenerate
      through the real pipeline or restore the file. Reseal contracts with the
      recipe in tools/check-product-contracts.py.
   10. Implementor self-reports are unproven until a fresh-context verifier
      re-runs the Definition of Done.
6. **Work protocol**: claim a row in orchestration/queue.md before starting;
   brief in orchestration/briefs/, report in orchestration/reports/; ALL logs
   to workspace/logs/ (root `.lane-*` is git-ignored and forbidden); one lane
   mutates the tree at a time; git add by explicit path; the banned-git-commands
   list. State which pack/commit numbers came from.
7. **Definition of done** (keep the current paragraph; baselines now live in
   docs/state/ and orchestration/queue.md, not workspace/playtest-program.md).

## Task B — README.md: cut to ~40 lines
What it is (Godot 4.7 remake of BFME2/RotWK, single-player skirmish focus,
7 factions), how to run (3 commands), layout pointer to AGENTS.md, credits
(OpenSAGE etc. — keep the acknowledgements + licence paragraph as-is, it is
legal text), ONE sentence on retail material (as in Task A §2). Delete every
other firewall/"never commit retail" sentence. Keep the banner image line.

## Task C — CONTRIBUTING.md, DIRECTION.md, STATUS.md, SECURITY.md
- CONTRIBUTING.md: rewrite to ≤30 lines: how to pick work (queue.md), the DoD,
  the banned git list, pointer to AGENTS.md. Remove firewall sermon.
- DIRECTION.md, STATUS.md: read them; if their content is narrative status
  that docs/state/ or queue.md now covers, DELETE them (git rm) and note it. If
  DIRECTION.md holds owner strategy not recorded elsewhere, keep it, fix its
  paths, and trim firewall prose.
- SECURITY.md: keep (GitHub-recognised file); only fix stale paths.

## Task D — docs/ pruning by the "generated-or-gated" rule
For each of the 25 entries in docs/ decide KEEP / FIX-PATHS / DELETE and
record the decision in orchestration/reports/stage3-docs-docket.md BEFORE
acting. Guidance:
- KEEP + fix paths: ARCHITECTURE, SIMULATION_PROTOCOL, TOPOLOGY, MODDING,
  THIRD_PARTY, RELEASE_POLICY, RELEASE_CYCLE, BUILD_AND_RELEASE, VERIFICATION,
  ONBOARDING, FAQ, LAUNCHER_*, HARDLINK_ISOLATION, ROTWK_SYSTEMS_PATH,
  CONTENT_PIPELINE (rewrite its firewall section to the one-sentence policy),
  OPENSAGE_GAP_MATRIX, RETAIL_INI_COVERAGE (tool-generated — do not hand-edit
  numbers; just fix paths), castle-siege-design, patch-notes/, state/, assets/,
  docs/README.md (doc map — regenerate its list to match what survives).
- Candidates to DELETE (verify first that they are stale narrative and a live
  successor exists): MILESTONE_CURRENT.md, BFME2_PARITY.md (if superseded by
  docs/state/parity-ledger.md + RETAIL_INI_COVERAGE.md).
- Anything ambiguous → KEEP. Deleting is cheap later; resurrecting is not.

## Task E — root de-sprawl
Root today has debris. Handle:
- Tracked reports/briefs that belong in orchestration/: CODEX_BRIEF.md,
  ORPHAN_RUNNERS_REPORT.md, P0-1-RESULT.md, P0-2-RESULT.md → `git mv` into
  orchestration/reports/ (add a date/stage prefix if helpful).
- Untracked root debris: FAILURE_TRIAGE_TABLE.md (evidence for queue Q7 —
  move + `git add` as orchestration/reports/failure-triage-table.md and update
  the Q7 evidence path in queue.md); PHANTOMS.md, FINAL_STATUS.txt,
  gate-wiring-final-brief.md (orphan-runner sweep leftovers — move into
  orchestration/reports/ if they carry evidence, else delete; say which).
- Untracked orchestration/briefs/stage1-*.md, stage2-triage-kimi.md,
  stage3-docs-overhaul-grok.md and orchestration/reports/stage1-*.md:
  `git add` them — they are the audit trail.
- Add tools/gate-hygiene.ps1: fails if (a) any file exists at repo root not in
  an allowlist written into the script (enumerate what actually exists at root
  and is legitimate: AGENTS.md, README.md, CONTRIBUTING.md, SECURITY.md,
  LICENSE*, VERSION, .gitignore, .gitattributes, the *.bat runners, DIRECTION.md
  if kept, project config files, etc.), (b) any tracked path matches `\.lane-`,
  or (c) any tracked text file contains a machine-absolute path
  (`[A-Za-z]:\\Users\\`) or the string `.private/` / `.private\`, excluding
  docs/patch-notes/, orchestration/, and tools/orphan-runners-manifest.csv.
  Print offenders; exit 1 on any. Wire it as a step in .github/workflows/ci.yml
  next to the existing publication-boundary job. Run it locally: it must PASS
  on your final tree.

## Definition of Done (verbatim outputs in your report)
1. `git grep -n -iE "bring your own|never commit retail|no EA assets|leak(ing)? retail|release firewall"` over tracked files → hits only in docs/patch-notes/ (historical) and orchestration/ (audit trail). README/AGENTS/CONTRIBUTING each contain the retail-material policy at most ONCE.
2. `git grep -n -F ".private"` → hits only in docs/patch-notes/, orchestration/, and the known language tokens (`private_plan_sha256`, `openbfme.private-hud-*`, tools/orphan-runners-manifest.csv).
3. `powershell -ExecutionPolicy Bypass -File tools\gate-hygiene.ps1` → PASS, exit 0.
4. `python tools\check_pack_addresses.py` → PASS packs=42 roots=2 (proves nothing under workspace moved).
5. `powershell -ExecutionPolicy Bypass -File tools\Test-DistPipeline.ps1` → still PASSES 25 checks (proves the publish path docs/scripts still agree).
6. `git status --porcelain` empty. Commits prefixed `docs(stage3):` / `chore(stage3):`, staged by explicit path, no logs/transcripts.
7. Report orchestration/reports/stage3-docs-overhaul.md: per task what changed, the docs docket decisions, DoD outputs, anything left undone and why.
