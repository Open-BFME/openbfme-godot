# Stage 3 — fresh-context adversarial verification

Verifier: Opus 5, fresh context, read-only (no repo files mutated, no commits).
Date: 2026-08-17. Range verified: `6db3f7d..63c3840`
(`b0fb280`, `5153762`, `aeaf52e`, `63c3840`).
Brief: `orchestration/briefs/stage3-docs-overhaul-grok.md`
Implementor report: `orchestration/reports/stage3-docs-overhaul.md`
Docket: `orchestration/reports/stage3-docs-docket.md`

**Verdict: ACCEPT**, with named unverified residues (section 8). No FIX-FIRST item.

---

## 1. DoD 1–6 re-run by the verifier

### DoD 1 — firewall prose — PASS

```
$ git grep -n -iE "bring your own|never commit retail|no EA assets|leak(ing)? retail|release firewall"
orchestration/briefs/stage3-docs-overhaul-grok.md:70
orchestration/briefs/stage3-docs-overhaul-grok.md:123
orchestration/reports/stage3-docs-docket.md:88
orchestration/reports/stage3-docs-docket.md:91
orchestration/reports/stage3-docs-docket.md:92
orchestration/reports/stage3-docs-docket.md:93
orchestration/reports/stage3-docs-overhaul.md:14
orchestration/reports/stage3-docs-overhaul.md:42
orchestration/reports/stage3-docs-overhaul.md:49
orchestration/reports/stage3-docs-overhaul.md:112
```

All 10 hits are under `orchestration/` (audit trail). Zero in `docs/patch-notes/`
on this tree. Zero in `README.md` / `AGENTS.md` / `CONTRIBUTING.md` / `DIRECTION.md`
/ `docs/`.

Policy-sentence count (`Retail-derived files live under`): README 1 (L22–24),
AGENTS 1 (L22), CONTRIBUTING 1 (L24–26), DIRECTION 1 (L14–16). AGENTS rule 6
points at the single paragraph rather than restating it. **At most once each: PASS.**
The README licence block still carries the "must not redistribute retail or
converted retail content" sentence — that is the legal text the brief said to
keep as-is, and it does not match the DoD regex.

### DoD 2 — `.private` — PASS

```
$ git grep -n -F ".private" | wc -l
801
```

Distribution: `tools/orphan-runners-manifest.csv` 604 (frozen evidence, allowed),
`orchestration/` 111 (audit trail, allowed), `importer/` 76, `game/` 9.

Path-form check — the one that matters:

```
$ git grep -n -E '\.private[/\\]' | grep -v '^docs/patch-notes/' | grep -v '^orchestration/' | grep -v '^tools/orphan-runners-manifest.csv'
<no output; exit 1>
```

Every remaining `importer/` and `game/` hit is a language token, confirmed by
inspection: `plan.private_plan_sha256`, `item.private()`, `args.private_root`,
`battalion.private_parity_mode_active`, `openbfme.private-hud-*`,
`openbfme.private-retail-window-capture`. **No `.private/` or `.private\` path
survives anywhere outside the two allowed trees.**

### DoD 3 — hygiene gate — PASS

```
$ powershell -ExecutionPolicy Bypass -File tools\gate-hygiene.ps1
HYGIENE_GATE PASS root-files=25 tracked=2582
exit 0
```

(The implementor's report quotes `tracked=2568`; that run predated the final
report commit. Re-run at HEAD: 2582, still PASS. Not a defect.)

### DoD 4 — pack addresses — PASS

```
$ python tools\check_pack_addresses.py
PACK_ADDRESS_CHECK PASS packs=42 roots=2
exit 0
```

### DoD 5 — dist pipeline — PASS

```
$ powershell -ExecutionPolicy Bypass -File tools\Test-DistPipeline.ps1
...
DIST PIPELINE GATE PASSED - 25 checks
exit 0
```

All 25 named checks PASS, including `the real firewall passes on this checkout`
and the three guard-refusal cases. The "publication boundary" comment rewrites in
`Publish-DistBuild.ps1` / `dist-pipeline-common.ps1` / `Test-DistPipeline.ps1`
did not disturb the suite.

### DoD 6 — clean tree, commit shape — PASS

```
$ git status --porcelain
<empty>
```

Commit subjects: `docs(stage3): record docs docket before pruning`,
`docs(stage3): rewrite agent contract and prune product docs`,
`chore(stage3): de-sprawl root and add hygiene gate`,
`docs(stage3): write overhaul report`. Prefixes correct. No logs or transcripts
committed (the four `.log`/`.txt` transcripts were removed, not added).

---

## 2. AGENTS.md content review — PASS

101 lines (brief: ~100–130).

- **Layout table** (L8–18): `game/ importer/ engine/ launcher/ tools/ contracts/
  orchestration/ docs/state/ workspace/`. No `.private` anywhere in the file.
- **Retail material** (L20–22): one calm paragraph, no fear language, names
  `workspace/manifest.json` and its five keys. I read
  `C:\Users\Jonathan\Desktop\open-bfme\workspace\manifest.json`: it contains
  exactly `retailInstall`, `pinnedPython`, `packsRoot`, `retailExtract`,
  `logsRoot` — the doc matches the artifact.
- **Lane table** (L26–34): pinned interpreter
  `workspace\retail-work\tools\python-3.12-env\Scripts\python.exe` and
  `BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`
  — both verified to exist on disk and both match `workspace/manifest.json`
  (`pinnedPython`, `retailInstall`). Baselines cited: importer 6 failed / 0 errors
  (traceable to `orchestration/queue.md` Q6), spellbook 218/0 (traceable to
  `orchestration/reports/stage1-verify.md` rounds 3/4), dist 25 checks (I re-ran:
  25).
- **Publishing** (L36–62): VERSION bump → `Write-BuildInfo.ps1` → patch notes →
  `Publish-DistBuild.ps1`, plus the three-switch table
  (`-Godot` / `-AllowEnvDependentContent` / `-AllowPackOwnedWotrData`) and
  `-AllowMissingWotrData`. Substance kept, paths are `workspace\`-relative.
- **Rules** (L64–88): original six present (packs sealed / bare pytest lies /
  contract seals / zero grep hits ≠ dead / no silent fallbacks / workspace is
  ordinary), plus the four new ones: 7 sweeps corrupt identifiers, 8 judge
  failure-by-failure, 9 hash-pinned artifacts are oracles, 10 self-reports are
  unproven until a fresh-context verifier re-runs the DoD. **All ten present.**
- **Work protocol** (L90–97): queue claim, briefs/reports, logs to
  `workspace/logs/`, one lane at a time, explicit-path `git add`, banned-git list.
- **Definition of done** (L99–101): present; baselines pointed at `docs/state/`
  and `orchestration/queue.md`.

**Path existence sweep**: I checked all 44 distinct paths cited by AGENTS.md and
README.md against the filesystem. **Every one exists** — including
`.tools/godot/` resolution, `tools/seal_published_packs.py`,
`tools/release/Publish-FirstPlaytestRc.ps1`, `game/src/script/handlers/`,
`workspace/logs/`, `docs/assets/openbfme-readme-banner.png`.

**One wording deviation** (accepted, not a fix): the brief's rule 7 quoted the
literal corrupted identifier. AGENTS.md L82 renders it as "a dotted-private
`_root` form" instead, because the literal would trip the new hygiene gate the
same lane added. Rational accommodation; slightly less vivid.

### README.md — PASS (with a declared miss)

62 lines. Brief said "~40 lines"; the parent's tolerance was ~40–60. The
acknowledgements + licence block the brief ordered kept as-is is 25 of those
lines; the non-legal body is ~24. The implementor declared this in its own
"Left undone" section. Banner line kept, badges kept, 3-command how-to, pointer
to AGENTS.md and DIRECTION.md, OpenSAGE / BlenderPlugin credits and the Unlicense
paragraph verbatim, one policy sentence.

### CONTRIBUTING.md — PASS

26 lines (brief: ≤30). Pick work via `orchestration/queue.md`, DoD, banned git
list + no branches/worktrees + logs to `workspace/logs/`, pointer to AGENTS.md,
policy sentence once. No sermon.

---

## 3. Code files touched by a docs lane

`git diff 6db3f7d..HEAD -- game/ importer/ engine/ launcher/` returns exactly
three files, two of which are the ones under review plus a markdown doc.

| File | Change | Verdict |
|---|---|---|
| `game/src/core/diag_log.gd` | one `##` comment line reworded (it had contained `C:\Users\somebody\...`) | **Comment only. PASS** |
| `importer/docs/ring-publish-plan.md` | three absolute `C:\Users\Jonathan\...` PowerShell variable assignments → repo-relative | **Doc string only. PASS** |
| `tools/onboard.py` | one `print()` string: `STATUS.md` → `orchestration/queue.md and docs/state/` | **String only. PASS** |
| `launcher/OpenBFME.Launcher.Tests/Program.cs` | test fixture literal `@"D:\Users\someone\packs"` → `"D:" + "\\Users\\" + "someone\\packs"` (and same for the expected value) | **Constant-folded to the identical string. PASS** |

I additionally verified the launcher change beyond reasoning:

```
$ dotnet build launcher/OpenBFME.Launcher.Tests/OpenBFME.Launcher.Tests.csproj
Build succeeded. 0 Warning(s) 0 Error(s)
```

`Redaction.Scrub` itself is untouched; the assertion still compares
`D:\Users\someone\packs` against `D:\Users\<user>\packs`.

### Extra code files the parent did not list, which I audited anyway

- `tools/release/Test-ReleaseTools.ps1` — fixture literal rebuilt at runtime. I
  evaluated the new expression in PowerShell: it produces
  `developer path C:\Users\SensitiveUser\project`, `-eq` the old literal returns
  `True`. Windows-only equivalence (`DirectorySeparatorChar` would be `/` on
  Linux), and the release-tools job is `windows-latest`. **PASS.**
- `tools/gate-retail.ps1`, `tools/publish-durable-pack.ps1`,
  `tools/publish-to-release-remote.ps1`, `tools/t0_gate.py`, `.gitignore`,
  `.github/workflows/ci.yml` job name — comment/display-string only. **PASS.**
- `tools/export-scan.ps1` — removed the `"STATUS.md"` allowlist key. Correct and
  necessary consequence of deleting STATUS.md; leaving it would have been dead
  allowlist. **PASS.**
- **`tools/gate-orphan-runners.ps1` — the one real behaviour change in the range.**
  A hardcoded `C:\Users\Jonathan\Downloads\godot47\...` Godot path and a hardcoded
  `OPENBFME_CONTENT` absolute became `. resolve-godot.ps1` +
  `Resolve-OpenBfmeGodot -RepoRoot $repo -PreferConsole` and
  `Join-Path $repo 'workspace\content-packs'`. This was pre-declared in the
  docket as gate collateral. Two hazards checked:
  1. `tools/resolve-godot.ps1` deliberately refuses maintainer-machine paths
     (AGENTS.md switch table says as much), so this could have broken the gate on
     this machine. It does not: resolution returns
     `C:\Users\Jonathan\Desktop\open-bfme\.tools\godot\Godot_v4.7-stable_win64_console.exe`,
     which exists.
  2. `resolve-godot.ps1` sets `Set-StrictMode -Version Latest` at script scope, and
     dot-sourcing leaks that into the caller. I smoke-tested the whole prologue
     with `-Suite bogus`, which forces a throw after all setup and suite-table
     construction and before any Godot launch: it reached line 168 and threw the
     intended `Unknown suite(s): bogus...`. No strict-mode fallout.
  The 99-runner gate itself was **not** re-run (see residues).

No file in the range changes logic. **No FAIL.**

---

## 4. Deletions beyond the brief

Method: for each name, `git grep -F -l <name>` on `HEAD` and on `6db3f7d`,
excluding `orchestration/` and `docs/patch-notes/` from both sides.

| Deleted file | Refs at 6db3f7d | Refs at HEAD | Verdict |
|---|---|---|---|
| `codex-exec.log` | none | none | debris (6.4 MB agent transcript) |
| `.codex-exec.log` | none | none | debris |
| `.codex-log-spellbooks.txt` | none | none | debris |
| `.commit-msg.txt` | none | none | debris (temp commit message) |
| `commit-armor.bat` | none | none | spent one-shot commit wrapper |
| `commit-damage-creation.bat` | none | none | same |
| `commit-fx-timing.bat` | none | none | same |
| `do-commit.bat` | none | none | same |
| `gate-orphan-runners-log.txt` | none | none | debris (460 KB log) |
| `gate-orphan-test.txt` | none | none | debris |
| `orphan-gate-test.log` | none | none | debris |

**Zero of the eleven was referenced by any script, doc, or workflow, before or
after.** All were logs or spent one-shot `git commit` wrappers. Clean.

### STATUS.md — justified DELETE

At `6db3f7d`, STATUS.md was referenced by **16 tracked files**: `CONTRIBUTING.md`,
`DIRECTION.md`, `README.md`, `docs/ARCHITECTURE.md`, `docs/CONTENT_PIPELINE.md`,
`docs/FAQ.md`, `docs/MILESTONE_CURRENT.md`, `docs/ONBOARDING.md`,
`docs/OPENSAGE_GAP_MATRIX.md`, `docs/README.md`, `docs/RELEASE_POLICY.md`,
`docs/ROTWK_SYSTEMS_PATH.md`, `docs/THIRD_PARTY.md`, `docs/VERIFICATION.md`,
`tools/export-scan.ps1`, `tools/onboard.py`.

At HEAD: **zero**. Every referrer was retargeted (or, for export-scan, had its
allowlist key removed) rather than left dangling. A full relative-markdown-link
existence sweep over every tracked `.md` outside `orchestration/` and
`docs/patch-notes/` returns **no broken links**.

Content check against the docket's "covered by docs/state or queue.md" claim
(`git show 6db3f7d:STATUS.md`):

- Self-described "Volatile. Prefer focused gate output over anything written here."
- "Current surface" entry table → covered by the AGENTS.md lane table and README.
- "Known open product work" (converter-gap burn-down, multi-map smoke, campaigns/
  WOTR, hardened MP, installer) → covered by `DIRECTION.md` system ladder and
  `orchestration/queue.md`.
- **Minor loss, named:** the "Code inventory" bullet list of `tools/rotwk_*.py`
  scripts is not reproduced anywhere. It was explicitly labelled "not gate
  evidence" and is regenerable with `ls tools/rotwk_*`. I do not consider this
  worth a FIX-FIRST.

Moved (not deleted): `CODEX_BRIEF.md`, `ORPHAN_RUNNERS_REPORT.md`,
`P0-1-RESULT.md`, `P0-2-RESULT.md` → `orchestration/reports/2026-08-15-*.md` via
`git mv` (rename detection confirms 100% similarity, 0 line changes). The only
referrer of any of them was `ORPHAN_RUNNERS_REPORT.md` citing itself.
`FAILURE_TRIAGE_TABLE.md`, `PHANTOMS.md`, `gate-wiring-final-brief.md` were
untracked → moved into `orchestration/reports/` and added; queue Q7's evidence
path was updated to `orchestration/reports/failure-triage-table.md` (verified in
the queue diff). `FINAL_STATUS.txt` deleted with a stated reason; gone from disk.

---

## 5. tools/gate-hygiene.ps1 — PASS, and proven to fail closed

Read in full (137 lines).

- **Allowlist vs reality**: the allowlist has 25 entries; the repo root has
  exactly 25 files; the gate reports `root-files=25` and passes. I listed the root
  independently — the two sets are identical, with no phantom allowlist entries.
- **CI wiring**: `.github/workflows/ci.yml` adds
  `- name: Root hygiene (allowlist, lane debris, machine paths) / run: ./tools/gate-hygiene.ps1`
  as the last step of the `publication-boundary` job, next to `export-scan.ps1`.
  Confirmed in the diff.
- **Exclusions**: `Test-HygieneExcludedPath` returns true only for
  `docs/patch-notes/`, `orchestration/`, and `tools/orphan-runners-manifest.csv`
  — **exactly the three the brief approved, no more.**
- **Are the exclusions load-bearing or cosmetic?** Load-bearing, and only just:
  the raw `[A-Za-z]:\\Users\\` grep returns 25 hits at HEAD and **all 25 are under
  `orchestration/`**. The raw `.private/` / `.private\` greps likewise hit only
  `orchestration/`. So nothing outside the audit trail is being hidden.
- **Would it actually fail?** I did not take this on reasoning alone, and I
  created no files in the repo. I built two throwaway git repos under `$TEMP`,
  ran the gate against them with `-RepoRoot`, and deleted them:

```
HYGIENE_GATE FAIL count=3
  root-file-not-allowlisted: note.txt
  root-file-not-allowlisted: STRAY.md
  tracked-lane-debris: sub/.lane-debris.log
EXIT=1
```

```
HYGIENE_GATE FAIL count=2
  machine-absolute-path: AGENTS.md:1:path C:\Users\bob\x and dotted .private/foo
  retired-workspace-dirname: AGENTS.md:1:path C:\Users\bob\x and dotted .private/foo
EXIT=1
```

**All three checks (a), (b), (c) demonstrably fail closed with exit 1.** Nothing
left behind in the repo.

Two non-blocking narrownesses worth a future ticket, both faithful to the brief
as written:

1. `$RootAllowlist -notcontains $file.Name` is case-insensitive in PowerShell, so
   a root `Readme.md` alongside `README.md` would be allowed on a case-sensitive
   checkout.
2. The machine-path regex is `[A-Za-z]:\\Users\\` only — it will not catch
   `C:/Users/...` or `/home/<user>/...`. `export-scan.ps1` remains the broader
   scanner; this gate is the narrow, fast complement the brief asked for.

Check (a) inspects the working tree, not the index, so it also catches untracked
root debris locally — a deliberate strength, and in CI it degrades gracefully to
a tracked-root check.

---

## 6. Docs docket — PASS

`orchestration/reports/stage3-docs-docket.md` was committed at `b0fb280`, i.e.
**before** the delete/rewrite commits, as the brief required.

- `docs/` has exactly 25 entries on disk; the docket table has exactly 25 rows and
  the names match one-for-one.
- Every DELETE decision states a successor or a debris reason: STATUS.md →
  `docs/state/` + `orchestration/queue.md`; `FINAL_STATUS.txt` → one stale line;
  the eleven root files → logs / spent commit wrappers.
- `docs/MILESTONE_CURRENT.md` KEPT with reason — and the reason is verifiable:
  `tools/gate-rotwk-systems.ps1:36` contains
  `Assert-File (Join-Path $repoRoot "docs\MILESTONE_CURRENT.md")`. Deleting it
  would have broken a live gate. Correct call.
- `docs/BFME2_PARITY.md` KEPT with reason (owns the evidence *model*;
  `state/parity-ledger.md` is a snapshot, `RETAIL_INI_COVERAGE.md` is generated
  counts). Reasonable; brief said ambiguous → KEEP.

**Spot-check of KEEP+fix-paths docs** (3 requested; I did 5 plus a global sweep):

| Doc | `.private` | `X:\Users\` | stale `STATUS.md` | Notes |
|---|---|---|---|---|
| `docs/ARCHITECTURE.md` | none | none | none | Validation line → `docs/state/` + queue; containment sentence → the one-sentence policy |
| `docs/CONTENT_PIPELINE.md` | none | none | none | Containment section rewritten; benchmark pointer → `docs/state/` |
| `docs/ONBOARDING.md` | none | none | none | "Do not commit it" → policy sentence; troubleshooting row retargeted |
| `docs/FAQ.md` | none | none | none | "no EA game files" removed; two links retargeted |
| `docs/RETAIL_INI_COVERAGE.md` | none | none | none | Only the pinned-Python path touched (`python-3.12-env\Scripts\python.exe`); **no generated number altered** — confirmed by diff (2 lines) |

`docs/README.md` doc map: I cross-checked it both ways — every doc it links
exists, and every `docs/*.md` survivor is listed. No orphans, no dangling entries.

`docs/state/` diffs are **path-retargets only** — I read all three diffs
(`missing-physical-cook-report.md`, `parity-ledger.md`, `playtest-program.md`):
seven `.private/…` → `workspace/…` substitutions and nothing else. **No number,
count, digest, or status token was edited** — which matters, because these are
ledgers.

---

## 7. Implementor-report honesty check

The report's claims match the artifacts, with two trivial imprecisions worth
recording (neither is a DoD failure):

1. DoD 1 prose says hits are "only in `stage3-docs-docket.md`". There are also
   hits in the brief and in the report itself — all still under `orchestration/`,
   so the DoD still passes.
2. DoD 3 quotes `tracked=2568`; HEAD gives `tracked=2582` because the report
   commit itself added files. Re-run at HEAD passes.

The "Left undone" section is accurate and complete: README at 62 lines,
MILESTONE_CURRENT/BFME2_PARITY kept, `args.private_root` / `--private-root` /
`private_parity_mode_active` / `Assert-DistReleaseFirewall` identifiers left
alone (correctly — renaming them is exactly what rule 7 warns against), no queue
row existed for the lane, and importer + spellbook baselines cited rather than
re-run. That last admission is the honest one and I confirm its premise: the diff
touches **no** importer or game logic (only `diag_log.gd`'s comment and a
markdown file under `importer/docs/`).

---

## 8. What I did NOT verify — residues

Named explicitly so nobody treats this ACCEPT as broader than it is:

1. **`tools/gate-orphan-runners.ps1` full 99-runner run.** I proved godot
   resolution succeeds on this machine and that the script's prologue survives
   the inherited `Set-StrictMode -Version Latest` (via a `-Suite bogus` fast
   throw). I did not spend the ~hour to run all 99. This is the only file in the
   range with a behaviour change; residual risk is low but non-zero.
2. **Importer suite (baseline 6 failed / 0 errors).** Not re-run (~35 min). Zero
   importer logic changed in the range, so I accept the cited baseline.
3. **Spellbook runner (218/0).** Not re-run. Same reasoning; no game logic changed.
4. **`tools/release/Test-ReleaseTools.ps1` suite.** Not executed. I proved the
   rewritten fixture string is byte-identical on Windows.
5. **Launcher test *execution*.** The project builds clean; I did not run the
   test binary. The assertion's inputs and expected value are provably unchanged.
6. **CI in situ.** The hygiene step is wired correctly in `ci.yml` and the script
   passes locally and fails closed in isolation; I did not trigger a GitHub run.

---

## Verdict

**ACCEPT.**

DoD 1–6 independently re-run and all PASS. AGENTS.md is a genuine single agent
contract with all ten rules, correct pinned-interpreter and `BFME2_INSTALL`
paths that match `workspace/manifest.json`, and 44/44 cited paths present on
disk. README and CONTRIBUTING are within scope (README 62 lines vs a ~40 target,
declared and defensible — the excess is the legal block the brief ordered kept).
No code file in the range changes logic; the single behavioural change
(`gate-orphan-runners.ps1` godot resolution) was pre-declared, is required by the
new gate, and I confirmed it resolves and survives strict mode. All eleven
undocumented deletions were genuinely unreferenced debris, and STATUS.md's
deletion cleared all 16 of its referrers with zero dangling links. The hygiene
gate's allowlist matches root exactly, its exclusions are exactly the three the
brief approved, and I proved all three of its checks exit 1 on violations without
creating a single file in the repo.
