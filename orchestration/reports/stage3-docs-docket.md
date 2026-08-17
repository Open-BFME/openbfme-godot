# Stage 3 docs docket

Recorded 2026-08-17 before any delete, move, or rewrite. Rule: anything
ambiguous is KEEP. Deleting is cheap later; resurrecting is not.

## docs/ (25 entries)

| # | Path | Verdict | Reason |
|---|------|---------|--------|
| 1 | docs/ARCHITECTURE.md | FIX-PATHS | Live architecture contract. Retarget `STATUS.md` → `docs/state/` + `orchestration/queue.md`. Soften the one containment sentence to the one-sentence policy. |
| 2 | docs/SIMULATION_PROTOCOL.md | KEEP | Protocol doc; no stale path or firewall sermon found. |
| 3 | docs/TOPOLOGY.md | KEEP | Load-bearing map of public vs workspace buckets. Already uses `workspace/`. Inventory numbers are identifiers, not hand-edits. |
| 4 | docs/MODDING.md | KEEP | Mod-pack direction; no stale path hit. |
| 5 | docs/THIRD_PARTY.md | FIX-PATHS | Legal/toolchain ledger. Retarget `STATUS.md` pointer only. |
| 6 | docs/RELEASE_POLICY.md | FIX-PATHS | Release containment contract. Retarget `STATUS.md` pointer only. |
| 7 | docs/RELEASE_CYCLE.md | KEEP | Cycle policy; no stale path hit. |
| 8 | docs/BUILD_AND_RELEASE.md | KEEP | Windows release workflow; no `.private` / firewall-phrase hit. |
| 9 | docs/VERIFICATION.md | FIX-PATHS | Gate-ordering contract. Retarget `STATUS.md` → `docs/state/` + queue. Keep `MILESTONE_CURRENT.md` pointer (file stays). |
| 10 | docs/ONBOARDING.md | FIX-PATHS | Operator setup. Retarget `STATUS.md`; replace "Do not commit it" with the one-sentence retail-material policy. |
| 11 | docs/FAQ.md | FIX-PATHS | Human FAQ. Retarget `STATUS.md`; drop "no EA game files" / "never commit workspace" sermon; one-sentence policy. |
| 12 | docs/LAUNCHER_LINUX_WINE.md | KEEP | Launcher operator note; no stale path hit. |
| 13 | docs/LAUNCHER_UX_AND_RELEASE_READINESS.md | KEEP | Launcher UX contract; no stale path hit. |
| 14 | docs/HARDLINK_ISOLATION.md | KEEP | Gate-backed isolation note. |
| 15 | docs/ROTWK_SYSTEMS_PATH.md | FIX-PATHS | Operator commands. Retarget `STATUS.md` only. |
| 16 | docs/CONTENT_PIPELINE.md | FIX-PATHS | Live pipeline contract. Rewrite the Containment section to the one-sentence policy; retarget `STATUS.md`. |
| 17 | docs/OPENSAGE_GAP_MATRIX.md | FIX-PATHS | Cross-project checklist. Retarget `STATUS.md`. Keep `MILESTONE_CURRENT.md` pointer. |
| 18 | docs/RETAIL_INI_COVERAGE.md | FIX-PATHS | Tool-generated numbers — not hand-edited. Only the pinned-Python path (`cpython-3.12.13` → `python-3.12-env\Scripts\python.exe`). |
| 19 | docs/castle-siege-design.md | KEEP | Design note; no stale path hit. |
| 20 | docs/patch-notes/ | KEEP | Historical. DoD 1–2 allow residual firewall / `.private` language here. |
| 21 | docs/state/ | FIX-PATHS | Live ledgers. Keep content. Replace leftover `.private/` path citations with `workspace/` so DoD 2 and the hygiene gate pass. No number edits. |
| 22 | docs/assets/ | KEEP | Banner image. |
| 23 | docs/README.md | FIX-PATHS | Doc map — regenerate after the rest of this docket is applied. |
| 24 | docs/MILESTONE_CURRENT.md | KEEP | Candidate DELETE, but `tools/gate-rotwk-systems.ps1` asserts the file exists and DIRECTION/VERIFICATION/OPENSAGE still point at it. Ambiguous + gated → KEEP. Fix its `STATUS.md` pointer only. |
| 25 | docs/BFME2_PARITY.md | KEEP | Candidate DELETE. Not superseded: this file owns the evidence *model* (lanes, claim profiles, fail-closed rules). `docs/state/parity-ledger.md` is a status snapshot; `RETAIL_INI_COVERAGE.md` is generated counts. Complementary, not a successor. Ambiguous → KEEP. |

## Root product docs (Task C)

| Path | Verdict | Reason |
|------|---------|--------|
| DIRECTION.md | KEEP + FIX-PATHS | Owner strategy (north star, systems-first ladder, parity meaning, permanent constraints, non-goals) is not recorded elsewhere. Retarget `STATUS.md`; trim the one firewall paragraph to the one-sentence policy. |
| STATUS.md | DELETE | Narrative status. Live successors: `docs/state/` and `orchestration/queue.md`. |
| SECURITY.md | FIX-PATHS | GitHub-recognised. Only "private workspace" → `workspace/`. |
| CONTRIBUTING.md | REWRITE | Task C: ≤30 lines, queue/DoD/banned-git, pointer to AGENTS.md. |
| README.md | REWRITE | Task B: ~40 lines. |
| AGENTS.md | REWRITE | Task A: single agent contract. |

## Root debris (Task E) — recorded before any move/delete

| Path | Tracked? | Verdict | Reason |
|------|----------|---------|--------|
| CODEX_BRIEF.md | yes | MOVE | `git mv` → `orchestration/reports/2026-08-15-codex-brief.md` |
| ORPHAN_RUNNERS_REPORT.md | yes | MOVE | `git mv` → `orchestration/reports/2026-08-15-orphan-runners-report.md` |
| P0-1-RESULT.md | yes | MOVE | `git mv` → `orchestration/reports/2026-08-15-p0-1-result.md` |
| P0-2-RESULT.md | yes | MOVE | `git mv` → `orchestration/reports/2026-08-15-p0-2-result.md` |
| FAILURE_TRIAGE_TABLE.md | no | MOVE + ADD | → `orchestration/reports/failure-triage-table.md`; update queue Q7 |
| PHANTOMS.md | no | MOVE + ADD | Five-row evidence table for the orphan-runner sweep → `orchestration/reports/phantoms.md` |
| gate-wiring-final-brief.md | no | MOVE + ADD | Leftover brief, still carries 99/90/22 counts and procedure → `orchestration/reports/gate-wiring-final-brief.md` |
| FINAL_STATUS.txt | no | DELETE | One stale line; no evidence a live successor does not already hold |
| .codex-exec.log | yes | DELETE (`git rm`) | Agent transcript; logs belong under `workspace/logs/` and must not be tracked |
| .codex-log-spellbooks.txt | yes | DELETE (`git rm`) | Same |
| .commit-msg.txt | yes | DELETE (`git rm`) | Temp commit message |
| codex-exec.log | yes | DELETE (`git rm`) | Agent transcript |
| gate-orphan-runners-log.txt | yes | DELETE (`git rm`) | Log |
| gate-orphan-test.txt | yes | DELETE (`git rm`) | Log |
| orphan-gate-test.log | yes | DELETE (`git rm`) | Log |
| commit-armor.bat | yes | DELETE (`git rm`) | Spent one-shot commit wrapper; contains a machine-absolute `cd` |
| commit-damage-creation.bat | yes | DELETE (`git rm`) | Same |
| commit-fx-timing.bat | yes | DELETE (`git rm`) | Same |
| do-commit.bat | yes | DELETE (`git rm`) | Same |

Untracked orchestration audit trail (git add, do not delete):
`orchestration/briefs/stage1-fixes.md`, `stage1-fixes-r2.md`, `stage1-fixes-r3.md`,
`stage2-triage-kimi.md`, `stage3-docs-overhaul-grok.md`,
`orchestration/reports/stage1-fixes-r2.md`, `stage1-fixes-r3.md`,
`stage1-verify.md`, `stage2-verify.md`.

## Hygiene-gate collateral (must change or the new gate fails)

These are not docs-prune targets. They are recorded here so the edits are
deliberate and not a sweep:

| Path | Edit |
|------|------|
| tools/gate-orphan-runners.ps1 | Replace hardcoded `C:\Users\...` Godot and content paths with `resolve-godot.ps1` + `workspace\content-packs` |
| importer/docs/ring-publish-plan.md | Replace machine-absolute paths with repo-relative `workspace\...` |
| tools/release/Test-ReleaseTools.ps1 | Build the scanner fixture path at runtime so the source file does not contain `C:\Users\` |
| game/src/core/diag_log.gd | Reword the redaction comment so it does not contain `C:\Users\` |
| tools/gate-retail.ps1 | Comment: "release firewall" → "publication boundary" |
| tools/Publish-DistBuild.ps1 | Same (function name `Assert-DistReleaseFirewall` is unchanged) |
| tools/Test-DistPipeline.ps1 | Same |
| tools/t0_gate.py | Comment: drop "release firewall" |
| .gitignore | Two comments: drop "release firewall" |
| .github/workflows/ci.yml | Job display name: drop "release firewall"; add hygiene step |
| tools/export-scan.ps1 | Drop the allowlist key for deleted `STATUS.md` |
| tools/onboard.py | Retarget the `STATUS.md` help line |
