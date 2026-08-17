# Stage 3 — docs overhaul (Grok lane)

Lane: `orchestration/briefs/stage3-docs-overhaul-grok.md`
Docket (written and committed before any delete): `orchestration/reports/stage3-docs-docket.md` at `b0fb280`
Date: 2026-08-17
HEAD at start: `6db3f7d`. Pack identity: not moved. `check_pack_addresses.py` after the tree mutations: `PACK_ADDRESS_CHECK PASS packs=42 roots=2`.

## Task A — AGENTS.md

Rewrote `AGENTS.md` (101 lines). Layout table now names `orchestration/`, `docs/state/`, and `workspace/` (canonical keys in `workspace/manifest.json`). One retail-material paragraph. Lane table carries the 2026-08-17 baselines (importer 6 failed / 0 errors, Q6; spellbook 218/0; dist 25). Publishing section kept and tightened; Godot path is no longer a machine-absolute. Rules 1–6 kept (rule 6 now points at the single policy paragraph). Rules 7–10 and the work protocol added. DoD baselines live in `docs/state/` and `orchestration/queue.md`.

## Task B — README.md

Cut to 62 lines. Banner, badges, 3-command how-to, pointer to `AGENTS.md`, one retail-material sentence, credits + licence kept as-is (legal text). The ~40-line target cannot be met without cutting that legal block; the non-legal body is ~24 lines. Deleted the firewall / "No EA assets" / "leaking retail" / "Never commit retail" sentences.

## Task C — CONTRIBUTING, DIRECTION, STATUS, SECURITY

- `CONTRIBUTING.md`: 26 lines. Queue, DoD, banned git list, pointer to `AGENTS.md`, policy once.
- `DIRECTION.md`: kept. Owner strategy (north star, ladder, parity meaning, constraints, non-goals) is not recorded elsewhere. Retargeted `STATUS.md` → `docs/state/` + `orchestration/queue.md`. Replaced the one firewall paragraph with the one-sentence policy.
- `STATUS.md`: deleted (`git rm`). Narrative status; successors are `docs/state/` and `orchestration/queue.md`.
- `SECURITY.md`: kept. "private workspace" → `workspace/`.

## Task D — docs/ docket

Full KEEP / FIX-PATHS / DELETE table is in `orchestration/reports/stage3-docs-docket.md` (committed at `b0fb280` before any delete).

Kept both delete-candidates:
- `docs/MILESTONE_CURRENT.md` — `tools/gate-rotwk-systems.ps1` asserts the file exists. Ambiguous + gated → KEEP. Pointer retargeted.
- `docs/BFME2_PARITY.md` — evidence *model*, not superseded by the status ledger or the generated INI coverage file. Ambiguous → KEEP.

Path retargets applied per-file (no sweep): `STATUS.md` pointers, CONTENT_PIPELINE Containment rewritten to the one-sentence policy, ONBOARDING/FAQ/ARCHITECTURE softened the same way, `docs/state/` leftover `.private/` citations → `workspace/`, RETAIL_INI_COVERAGE pinned-Python path only, `docs/README.md` regenerated to match survivors.

## Task E — root de-sprawl + hygiene gate

Moved (tracked): `CODEX_BRIEF.md`, `ORPHAN_RUNNERS_REPORT.md`, `P0-1-RESULT.md`, `P0-2-RESULT.md` → `orchestration/reports/2026-08-15-*`.
Moved + added: `FAILURE_TRIAGE_TABLE.md` → `orchestration/reports/failure-triage-table.md` (Q7 evidence path updated); `PHANTOMS.md` and `gate-wiring-final-brief.md` into `orchestration/reports/` (they carry sweep evidence).
Deleted: `FINAL_STATUS.txt` (one stale line); tracked root logs (`.codex-exec.log`, `.codex-log-spellbooks.txt`, `.commit-msg.txt`, `codex-exec.log`, `gate-orphan-runners-log.txt`, `gate-orphan-test.txt`, `orphan-gate-test.log`); spent one-shot commit wrappers (`commit-armor.bat`, `commit-damage-creation.bat`, `commit-fx-timing.bat`, `do-commit.bat`).
Added the untracked orchestration audit trail (stage1/2 briefs and reports, this brief).

`tools/gate-hygiene.ps1` added and wired as a step in `.github/workflows/ci.yml` next to the publication-boundary job. Allowlist is the 25 legitimate root files that remain.

Hygiene-gate collateral (recorded in the docket, targeted edits): `tools/gate-orphan-runners.ps1` now uses `resolve-godot.ps1` + `workspace\content-packs`; `importer/docs/ring-publish-plan.md` repo-relative; `Test-ReleaseTools.ps1` and `launcher/OpenBFME.Launcher.Tests/Program.cs` build scanner fixtures at runtime so the source does not contain `X:\Users\`; `diag_log.gd` comment reworded; "release firewall" comments retargeted to "publication boundary" in `.gitignore`, CI job name, and the publish/scan scripts. Function name `Assert-DistReleaseFirewall` is unchanged.

## Definition of Done (measured)

### 1. Firewall-phrase grep — PASS

```
git grep -n -iE "bring your own|never commit retail|no EA assets|leak(ing)? retail|release firewall"
```

Hits only in `orchestration/reports/stage3-docs-docket.md` (four quoted-before/after rows). No hits in `docs/patch-notes/` on this tree. README / AGENTS / CONTRIBUTING each contain the retail-material policy once (`Retail-derived files live under`).

### 2. `.private` grep — PASS

```
git grep -n -F ".private"
```

Path-style hits remain only under `orchestration/` (audit trail) and `tools/orphan-runners-manifest.csv` (frozen evidence). All other hits are the known language tokens: `private_plan_sha256` / `private_hash_basis` / `.private()`, `openbfme.private-hud-*` / `openbfme.private-retail-window-capture`, `args.private_root` (CLI dest, not a directory), `battalion.private_parity_mode_active`. No `.private/` or `.private\` path remains outside the excluded trees. `docs/state/` citations were retargeted to `workspace/`.

### 3. Hygiene gate — PASS

```
powershell -ExecutionPolicy Bypass -File tools\gate-hygiene.ps1
HYGIENE_GATE PASS root-files=25 tracked=2568
exit 0
```

### 4. Pack addresses — PASS

```
python tools\check_pack_addresses.py
PACK_ADDRESS_CHECK PASS packs=42 roots=2
exit 0
```

Nothing under `workspace/` was moved.

### 5. Dist pipeline — PASS

```
powershell -ExecutionPolicy Bypass -File tools\Test-DistPipeline.ps1
DIST PIPELINE GATE PASSED - 25 checks
exit 0
```

Re-run after the publication-boundary comment edits; still 25/25.

### 6. Clean tree, explicit-path commits — PASS (after the commits recorded below)

Commits this lane:

| SHA | Subject |
|-----|---------|
| `b0fb280` | `docs(stage3): record docs docket before pruning` |
| `5153762` | `docs(stage3): rewrite agent contract and prune product docs` |
| `aeaf52e` | `chore(stage3): de-sprawl root and add hygiene gate` |
| (this file) | `docs(stage3): write overhaul report` |

Staged by explicit path. No `git add -A`. No reset/restore/clean/stash/amend. No logs or transcripts committed.

### 7. This report — PASS

`orchestration/reports/stage3-docs-overhaul.md`.

## Left undone

- `README.md` is 62 lines, not ~40, because Task B required keeping the acknowledgements + licence paragraph as-is.
- `docs/MILESTONE_CURRENT.md` and `docs/BFME2_PARITY.md` were not deleted (docket: KEEP).
- `args.private_root` / `--private-root` in `m3_pack_expansion.py` and `*.private()` / `private_parity_mode_active` were not renamed. They are language tokens; a mechanical rename is exactly rule 7.
- `Assert-DistReleaseFirewall` keeps its identifier. The DoD regex is the phrase `release firewall`, which that name does not match.
- No queue row existed for this stage-3 lane; Q7 evidence path was updated. Q1–Q9 otherwise untouched.
- Importer suite and spellbook runner were not re-run; this lane did not touch runtime or importer logic. Baselines cited from `orchestration/queue.md` Q6 and stage1-verify round 4.
