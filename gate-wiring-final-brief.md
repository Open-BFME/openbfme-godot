# Task: Finish Orphaned Test Runner Gate Wiring

## Current Status
- 99 orphaned runners verified passing (headless, exit 0, no errors)
- 90 orphaned runners verified failing (from captured logs in %TEMP%)
- 22 orphaned runners not yet tested
- Gate scaffold created but not executable
- Failure logs captured in: `C:\Users\Jonathan\AppData\Local\Temp\openbfme-orphan-runners-20260815-234631\` (376 files)

## Remaining Work (5 Parts)

### Part 1: Finalize and Test Gate Script

**Goal**: Convert `tools\gate-orphan-runners.ps1` into a fully executable gate over the 99 verified-passing runners.

**Requirements**:
- Per-runner PASS/FAIL accounting (print each result as it runs)
- Summary line matching house pattern: `ORPHAN_RUNNER_GATE pass=<N> fail=0`
- Exit code 0 if all pass, nonzero if any fail
- `-Suite` parameter to filter and run slow subsets separately (for second-tier runs)
- Runner grouping by functional suite (UI, handlers, retail, data, etc.)
- Environment: `OPENBFME_CONTENT=C:\Users\Jonathan\Desktop\open-bfme\.private\content-packs`, pinned Godot path

**Procedure**:
1. Rewrite the script to actually invoke each of the 99 runners via: `<godot> --headless --path game --script res://tests/<runner>.gd`
2. Run the complete gate end-to-end with no filters (all 99 runners)
3. Verify exit code 0 and capture the summary line output
4. Commit the finished gate with commit message: "Wire 99 orphaned runners into gate-orphan-runners: verified passing headless execution. Co-Authored-By: Codex Sol <noreply@openai.com>"

**Output to include in final report**: 
- Exit code
- Summary line (e.g., `ORPHAN_RUNNER_GATE pass=99 fail=0`)
- Commit SHA

### Part 2: Test 22 Untested Runners

**Goal**: Complete the sweep by testing the remaining 22 orphaned runners.

**Procedure**:
1. Identify which 22 runners were not yet tested (211 orphans - 99 passing - 90 failing = 22)
2. Run each headless: `<godot> --headless --path game --script res://tests/<runner>.gd`
3. Capture stdout/stderr in `%TEMP%\orphan-runner-sweepB-<runner>.txt`
4. Classify each as: pass, fail, timeout
5. Append results to `tools\orphan-runners-manifest.csv` (add 22 new rows with runner name, index/status)

**Output**: 
- Count of newly-passing runners from the 22
- Count of newly-failing runners from the 22
- Updated manifest CSV

### Part 3: Classify All 90 Failures with Evidence

**Goal**: Bucket failures by root cause using the captured logs (NOT assumptions).

**Procedure**:
1. Read each of the 90 failing runners' stderr/stdout from: `C:\Users\Jonathan\AppData\Local\Temp\openbfme-orphan-runners-20260815-234631\NNN-<runner>-*.stdout.txt` and `.stderr.txt`
2. Extract the first error line (SCRIPT ERROR, Parse Error, ERROR:, etc.)
3. Classify into exactly 3 buckets:
   - **(a) Parse/Load Errors (Mid-Flight Noise)**: Error message names a file that appears to be mid-edit by concurrent lanes (e.g., "Parse JSON failed", "Cannot open ...\data\<recent-pack-file>.json", "Unknown error getting token"). These WILL re-test clean post-settle.
   - **(b) Content/Assertion Failures (Real Regressions)**: Error message shows assertion failure or runtime check failure (e.g., "Assertion failed", "show-sub-object cannot clear a permanent hide", "clip-1 DelayBetweenShots weapon has no <field>"). These are genuine mid-flight defects needing investigation.
   - **(c) Stale Expectations (No Longer Applicable)**: Error message references removed content (e.g., "Scene not found", "Method does not exist", "Undefined node", runner file references .private/ paths that no longer exist). These are genuinely stale.
4. Create a classification table: 3 columns (Bucket, Count, Example Runners).
5. Append the table to `ORPHAN_RUNNERS_REPORT.md` in a new section "Failure Classification".
6. DO NOT delete or weaken any failing runner.

### Part 4: Preserve Evidence Logs

**Goal**: Move captured logs from %TEMP% to .private (persistent, ignored by git).

**Procedure**:
1. Create directory: `.private\scratch\orphan-runners-20260815\`
2. Copy all 376 files from `C:\Users\Jonathan\AppData\Local\Temp\openbfme-orphan-runners-20260815-234631\*` to `.private\scratch\orphan-runners-20260815\`
3. Do NOT commit .private; verify git ignores it.
4. Verify the copied logs are readable (spot-check 3 random files).

### Part 5: Document the 5 Phantom References

**Goal**: Explain exactly what the 5 "phantom" runners are.

**Procedure**:
1. For each phantom (banner_castle_silent_playtest_runner, diagnostics_log_runner, lan_discovery_runner, menu_match_cycle_runner, retail_mp_menu_runner):
   - Check if the runner file exists: `game/tests/<name>.gd`
   - Check where it appears in gates (tools/export-scan.ps1, others)
   - Determine: Is it a file that exists but is marked "NOT WIRED" in code? Or is it a reference to a missing file?
2. Add a new section to `ORPHAN_RUNNERS_REPORT.md`: "Phantom References (Not Actual Wiring)".
3. List each phantom with: filename status, gate references, classification (intentionally-unwired vs. missing file).

## Expected Outcomes

### Updated Manifest CSV
Add 22 new rows for untested runners with their pass/fail status.
Recount totals: runners on disk, wired before (101), newly wired from passing orphans, wired after.

### Updated Report
- Section: "Failure Classification" with 3-bucket table
- Section: "Phantom References" documenting the 5 non-wiring cases
- Cite all evidence logs now at `.private/scratch/orphan-runners-20260815/`

### Commits
- Commit 1: "Wire 99 orphaned runners into gate-orphan-runners..." (executable gate script)
- Commit 2: "Classify 90 orphaned runner failures: mid-flight noise, regressions, stale..." (manifest + report updates)

### Final Report Output
- Gate summary line: `ORPHAN_RUNNER_GATE pass=99 fail=0`
- Before/after counts: "101 wired before, 99 passing orphans added, 200 wired after" (or actual final count)
- Failure classification table (a/b/c buckets with counts)
- Post-settle re-test list (runners from bucket a)
- Phantom reference table

## House Rules (MANDATORY)

- Commit on the current worktree branch; NEVER push; NEVER merge to main; NEVER `git stash` (shared stash stack).
- Godot runs: env `OPENBFME_CONTENT=C:\Users\Jonathan\Desktop\open-bfme\.private\content-packs`; exe `C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe`; redirect runner output to uniquely-named `%TEMP%` files and read the file.
- Python: `C:\Users\Jonathan\Desktop\open-bfme\.private\retail-work\tools\python-3.12.13\python.exe` with `PYTHONPATH` pointing at the worktree's `importer/`.
- Retail INI oracle: `.private\retail-work\editions\rotwk\cache\effective-assets` — NEVER `layered-effective-assets` (contaminated).
- No pack builds/publishes/selection changes.
- Commit messages end: `Co-Authored-By: Codex Sol <noreply@openai.com>`
- An Opus adversarial review gates the merge — write reports for a hostile reviewer; every claim needs a rerunnable command.
