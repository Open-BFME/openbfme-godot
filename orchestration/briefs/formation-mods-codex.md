# FORMATION-MODS — Q41 continuation

Owner: `codex-formation-mods`

Compile every retail-authored `Category = FORMATION` modifier row reachable from a horde's `HORDE_TOGGLE_FORMATION` command onto that horde's playable-unit document. Apply and remove those compiled rows deterministically through the existing lockstep formation toggle command. Unsupported sim kinds must produce named receipts. Extend failing-first importer and runtime tests, preserve the retail state pin, run the requested sequential gates, and write the hostile-review report to `workspace/orchestration/fable-wave/castle-lanes/report-formation-mods.md`. No pack build, publish, selection change, or faction recook is authorized; the report must state that the importer half requires a faction recook to reach players.

## Definition of Done

- Complete active `Category = FORMATION` list table with retail source line citations, all rows, and horde/button references.
- Failing-first real-tree importer pytest proves every toggle-reachable formation modifier pair is compiled.
- Formation ON applies every compiled supported modifier and records named receipts for unsupported kinds; OFF restores baseline stats.
- `retail_state_pin_runner` stays exactly `0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc`.
- `retail_formation_toggle_runner`, `retail_formation_movement_runner`, and `retail_member_combat_runner` are green; slice failure names match `workspace/logs/v027fin-retail_slice_runner.txt` exactly.
- Every report claim carries a rerunnable command and `workspace/logs/` path; explicit-path commit ends with the required co-author trailer.

## HOUSE RULES (binding)

- Read AGENTS.md first; rules 1-10 are binding (rule 7: no find-replace sweeps; targeted edits only). Work at repo top level on the current branch; NEVER push; NEVER `git stash`/`reset`/`restore`/`clean`/`--amend`; git add by explicit path only.
- Godot runs: env OPENBFME_CONTENT=<repo>\workspace\content-packs; exe via tools\resolve-godot.bat (fallback C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe); redirect runner output to files under workspace\logs\ and read the file. Run Godot runners sequentially.
- Python: <repo>\workspace\retail-work\tools\python-3.12-env\Scripts\python.exe; BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2. Run pytest sequentially (no -n) — the machine has exhausted process handles under parallel load.
- Long test runs (importer suite, Godot sweeps) run in the FOREGROUND of the codex session and are polled to completion. Never leave a detached pytest/Godot child running when the session ends: on exit, `Get-Process python,Godot* | Where-Object CommandLine -match 'pytest|--script'` from your launch must be empty — kill your own strays. Orphaned suites stacked with the next lane's suite have exhausted Windows process creation (0xC0000142) three times on 2026-08-18.
- Retail INI oracle: workspace\retail-extract\data\ini (twin: workspace\retail-work\editions\rotwk\cache\effective-assets) — NEVER layered-effective-assets (contaminated).
- No pack builds/publishes/selection changes unless the brief explicitly authorizes them. Never edit a hash pin to make a check pass; hash-pinned artifacts are oracles.
- Commit messages end: Co-Authored-By: Codex Sol <noreply@openai.com>
- A fresh-context adversarial verifier re-runs your Definition of Done before acceptance — write reports for a hostile reviewer; every claim needs a rerunnable command and a log path.
