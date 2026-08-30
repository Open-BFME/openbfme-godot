# Windows onboarding

Set up the repository, establish the exact private source baseline, and run the
smallest trustworthy checks. OpenBFME is a developer alpha; incomplete content
and failing parity gates are expected and must remain visible.

## Read first

- [AGENTS.md](../AGENTS.md) - required contribution and private-data rules
- [DIRECTION.md](../DIRECTION.md) - exact Patch 2.02 v9.7.7 product target
- [Product scope](../contracts/rotwk-202-v9.7.7-product-scope.json) and
  [retail baseline](../contracts/rotwk-202-v9.7.7-baseline.json) - exact
  machine-readable target and source identity
- [ARCHITECTURE.md](ARCHITECTURE.md) - current executable/import paths
- [VERIFICATION.md](VERIFICATION.md) - what each gate can prove
- [ROADMAP.md](ROADMAP.md) and
  [work-items.json](../orchestration/work-items.json) - current work

## Requirements

- Windows 10 or 11, Git, and Windows PowerShell 5.1 or later
- Grok Build CLI with the official Ponytail plugin enabled
- Godot 4.7 for Windows; the console executable is preferred
- Python 3.12 available once so the repository can bootstrap its pinned private
  environment
- a legal BFME2 1.06 installation
- a legal RotWK 2.01 installation used as the underlying layer
- the exact Patch 2.02 official-2 v9.7.7 English overlay
- enough space below `workspace/` for private catalogs, converted assets,
  bundles, logs, and oracle evidence

Retail-derived bytes, converted retail payloads, captures, and raw logs stay
below the Git-ignored `workspace/` tree. Never commit them or put a
machine-absolute path in tracked documentation.

## Glossary

| Term | Meaning |
|---|---|
| **Target** | Exact effective English RotWK Patch 2.02 v9.7.7 game |
| **Layer** | One precedence input: Patch overlay, underlying RotWK installation, or BFME2 base |
| **Baseline** | Pinned source identities and rules accepted by the contracts |
| **Catalog** | Indexed archives and effective winning paths after precedence |
| **Bundle/pack** | Converted private content consumed by Godot |
| **Selection** | Ordered immutable bundles mounted for one run |
| **Fail closed** | Stop with a named error instead of guessing or falling back |
| **Gate** | A check with terminal `PASS`, `FAIL`, or `SKIP`; skip is never success |
| **Oracle** | Reproducible original-game evidence used to judge behavior, visuals, or audio |

## 1. Clone

```bat
git clone https://github.com/Open-BFME/openbfme-godot.git
cd openbfme-godot
```

## 2. Configure Godot and bootstrap tools

```bat
set OPENBFME_GODOT=C:\Path\To\Godot_v4.7-stable_win64_console.exe
run_doctor.bat
run_importer_tests.bat
```

`run_doctor.bat` proves tool availability only. Importer tests prove their
declared public fixtures and may skip private retail cases. Neither proves game
parity.

Godot resolution order is the explicit `OPENBFME_GODOT`/`GODOT_CONSOLE`/
`GODOT_EXE`/`GODOT` environment, a Git-ignored `.tools\godot\` drop, then
`godot` on `PATH`. Failure to resolve Godot is an error.

Install Ponytail into Grok once, then install the repository's fail-closed Git
hooks after the pinned private Python has been bootstrapped:

```bat
grok plugin install DietrichGebert/ponytail --trust
grok plugin enable ponytail
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass ^
  -File tools\Install-PonytailHooks.ps1 -Install
```

Verify the hooks at the start of later sessions:

```bat
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass ^
  -File tools\Install-PonytailHooks.ps1 -Verify
```

The hook invokes the qualified official skill
`/ponytail:ponytail-review`, binds it to the exact staged/outgoing Git
identity, and preserves `git lfs pre-push`. Do not use `--no-verify`, change
`core.hooksPath`, or replace the generated private hooks. Ponytail checks
over-engineering only; all work-item and parity checks remain mandatory.
Commit-producing `cherry-pick`, `revert`, and `rebase` sequencers are outside
the reviewed path and forbidden; apply without committing and finish through
ordinary `git commit`.

## 3. Prepare the pinned three-layer source

Keep the three source folders outside the repository. The baseline preparation
script creates junctions under `workspace\retail-work`; it does not copy or
alter the retail installations.

```bat
set BFME2_INSTALL=C:\Games\BFME2
set ROTWK201_INSTALL=C:\Games\RotWK
set PATCH202_OVERLAY=C:\Games\RotWK-Patch-202-v9.7.7

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass ^
  -File tools\prepare-rotwk-202-baseline.ps1 ^
  -Bfme2Install "%BFME2_INSTALL%" ^
  -Rotwk201Install "%ROTWK201_INSTALL%" ^
  -Patch202Overlay "%PATCH202_OVERLAY%"
```

The resulting source identity must agree with:

- `contracts/rotwk-202-v9.7.7-baseline.json`
- `contracts/rotwk-202-v9.7.7-english-overlay.json`

The preparation script calls the read-only verifier. Re-run that verifier
without changing the junction layout whenever source identity must be checked:

```bat
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe tools\verify-rotwk-202-baseline.py
```

Its `PASS` proves the seven required package files, all 217 archive identities,
and the 53,433-record catalog only. It does not prove conversion or parity.

Do not use `-ReplaceExisting` casually. It archives a nonmatching layered root
and changes private workspace state; inspect the refusal first.

## 4. Check public policy and hygiene

```bat
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe -B tools\check-product-contracts.py --check
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\gate-hygiene.ps1
```

These checks validate policy and repository containment. They do not cook or
select retail bundles.

## 5. Cook and select only through current work

The repository is being retargeted from historical source routes to the pinned
v9.7.7 catalog. Use only the cook/publish command named by the current row in
`orchestration/work-items.json`. The work item's acceptance contract must bind
the source policy, toolchain, recipe, output addresses, and complete selection.

Older `run_rotwk_one_button.bat`, `run_rotwk_systems.bat`, Men/Fords wizard,
and M2 commands may remain useful regression tools. Unless a current work item
explicitly proves them against the v9.7.7 baseline, they are historical
2.01-era/BFME2 routes and must not publish the target selection or support a
2.02 claim.

Only the integration owner publishes or changes `selection.json`.

## 6. Verify and launch an existing exact selection

```bat
set OPENBFME_CONTENT=%CD%\workspace\content-packs
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe -B tools\check_pack_addresses.py --json
run_retail_slice.bat --test
run_game.bat
```

Before accepting a result, confirm the resolved selection path, selection
digest, ordered bundle list, and every verified content address. A launch is
L3 loading evidence at most. See `VERIFICATION.md` for the higher behavior,
visual, audio, and end-to-end requirements.

## Exit and status behavior

- `PASS`: every required assertion and acceptance marker passed with fixed
  identity and no unexpected diagnostics.
- `FAIL`: the command, assertion, identity, diagnostics, timeout, or
  containment contract failed.
- `SKIP`: the check did not evaluate because an input was unavailable; it does
  not satisfy a prerequisite.

Importer commands may use additional nonzero exit codes to distinguish audit
failure, conversion gaps, or publish refusal. Read the named error and preserve
it as evidence. Never add an override merely to make a parity path green.

## Troubleshooting

| Symptom | Response |
|---|---|
| Baseline rejects a folder | Verify you passed each install/overlay root and exact v9.7.7 files; do not substitute another patch |
| Godot is missing | Set `OPENBFME_GODOT` to the 4.7 console executable |
| Catalog identity differs | Stop; compare source layers and contracts before cooking |
| Address check fails | Treat the bundle as mutated; publish a new immutable digest through integration ownership |
| Runtime uses unexpected content | Set `OPENBFME_CONTENT`, isolate user data, and inspect the resolved selection identity |
| Gate emits errors but exits zero | The gate is `FAIL`; preserve stdout/stderr and fix the harness or product error |
| Private input is unavailable | Report `SKIP`; do not convert it to PASS |
| Conversion was interrupted | Re-run the same approved resumable command; do not delete `workspace/` first |

## Contributing

Select no work informally. From a clean `main`, the integration owner uses the
small workflow entry point:

```powershell
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass `
  -File tools\work-item.ps1 ready
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass `
  -File tools\work-item.ps1 create -Id P1-EXAMPLE-001 -Assignee agent-name
```

`create` records literal `ownedPaths`, commits that assignment, and creates the
one sibling lane at `..\open-bfme-lanes\<work-item-id>`. The worker runs
`tools\work-item.ps1 check`, makes one implementation commit, then runs
`tools\work-item.ps1 handoff`. A different reviewer runs `review -LanePath ...
-Reviewer ...`. Every receipt remains private below
`workspace\logs\<work-item-id>\`; only the integration owner merges or changes
canonical status. Follow [AGENTS.md](../AGENTS.md) and
[CONTRIBUTING.md](../CONTRIBUTING.md).
