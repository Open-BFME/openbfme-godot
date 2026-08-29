# OpenBFME verification contract

- Owner: integration owner
- Owns: evidence levels, gate ordering, status semantics, identity binding, oracle acceptance, and completion declarations
- Does not own: product scope, implementation design, retail payloads, or task assignment
- Target: Rise of the Witch-king Patch 2.02 v9.7.7
- Update trigger: a gate, identity field, oracle recipe, evidence schema, or release-blocking invariant changes

## Governing records

- [2.02 v9.7.7 product scope](../contracts/rotwk-202-v9.7.7-product-scope.json)
- [2.02 v9.7.7 source baseline](../contracts/rotwk-202-v9.7.7-baseline.json)
- [2.02 v9.7.7 English overlay](../contracts/rotwk-202-v9.7.7-english-overlay.json)
- [canonical work-item ledger](../orchestration/work-items.json)
- [runtime and component architecture](ARCHITECTURE.md)

The product-scope contract defines the denominator. The source contracts bind
the private inputs. The work-item ledger binds work to evidence. No README,
receipt, assertion count, historical log, or old selected-pack name overrides
those authorities.

## Evidence identity

Every result is bound to this identity tuple:

```text
Git revision
+ tracked and untracked dirty-state digest
+ product-contract policy digest
+ source-baseline and overlay identity
+ importer/toolchain/recipe identity
+ selection.json byte digest
+ ordered mounted bundle names and verified content addresses
+ runner and oracle recipe version
+ relevant platform and renderer identity
```

A result from another tuple is stale unless the gate proves that none of its
inputs changed. A pack directory name is not sufficient: its bytes must hash to
that address. Tests that can discover durable `user://` content must isolate
Godot user data or assert the resolved selection path and digest.

## Status semantics

Every gate reports exactly one terminal status.

| Status | Meaning | May satisfy a prerequisite? |
|---|---|---:|
| `PASS` | The command completed, all required assertions and acceptance markers were observed, identity stayed fixed, and no unexpected diagnostics occurred. | yes |
| `FAIL` | The command, assertion, acceptance marker, identity, timeout, crash, containment rule, or diagnostic contract failed. | no |
| `SKIP` | The gate did not evaluate because a declared prerequisite or private input was unavailable. | no |

`SKIP` is never success, never partial credit, and never evidence for a complete
claim. A wrapper that passes offline checks while skipping live retail stages
has a `SKIP` result for those stages. Aggregate gates pass only when every
required child gate passes; optional children must be explicitly outside that
claim's denominator.

## Unexpected diagnostics are failures

Exit code zero alone is insufficient. Unless a runner declares an exact
negative-test diagnostic contract, any of the following makes the result fail:

- `ERROR:`, `SCRIPT ERROR`, `Parse Error`, assertion failure, or stack trace;
- engine crash, native crash, timeout, retry exhaustion, or leaked process;
- missing acceptance marker or contradictory PASS/FAIL markers;
- missing resource, unsafe path, RID/allocation leak, orphan, or containment
  diagnostic declared forbidden by that gate;
- fallback, placeholder, synthetic, unsupported, or unconverted behavior on a
  strict retail path; or
- selected-pack, source, recipe, or state-pin drift.

A negative test may expect a diagnostic only when it isolates that case and
pins the exact message class and expected count. Do not globally suppress error
output or whitelist an unrestricted substring. Gate harnesses must inspect both
stdout and stderr before reporting `PASS`.

## Evidence levels

| Level | Evidence | What it proves | What it does not prove |
|---:|---|---|---|
| L0 | policy/contract validation | the declared target and schemas are internally consistent | source presence or implementation |
| L1 | source inventory and provenance | exact 2.02 inputs, precedence, winners, and denominator are known | conversion or runtime use |
| L2 | deterministic conversion and bundle validation | source was converted reproducibly into valid addressed bundles | live consumption or behavior |
| L3 | strict runtime loading/consumption | the selected bundle is mounted and the live consumer uses it without fallback | correct gameplay or presentation |
| L4 | deterministic behavioral tests | commands produce the specified state/events and reproduce defects | fidelity to the original by itself |
| L5 | original-game oracle comparison | declared gameplay, visual, or audio scenarios agree within an approved tolerance | whole-product completion outside those scenarios |
| L6 | end-to-end product qualification | complete matches/modes, persistence, replay/network where applicable, containment, reliability, and release packaging pass | future untested changes |

Asset presence, parser coverage, recognized script vocabulary, dispatchability,
module counts, and successful compilation stop no later than L2 or L3. A 1:1
claim requires L5 evidence across the product-scope denominator and L6 for the
release paths affected.

## Canonical Windows commands

Run commands from the repository root in `cmd.exe` or PowerShell. Prefer the
checked-in wrappers because they resolve the pinned importer environment and
Godot 4.7 contract.

```bat
rem Tool availability only; not a product gate.
run_doctor.bat

rem Public repository hygiene and policy.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\gate-hygiene.ps1
py -3 tools\check-product-contracts.py --check

rem Read-only private L1 source verification (no cook or selection write).
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe tools\verify-rotwk-202-baseline.py

rem Pinned Python 3.12 importer suite.
run_importer_tests.bat

rem Selected immutable-bundle address check.
py -3 tools\check_pack_addresses.py --json

rem Current live Godot retail integration gate.
run_retail_slice.bat --test

rem Developer launch from the exact selected content root.
set OPENBFME_CONTENT=%CD%\workspace\content-packs
run_game.bat
```

For a direct focused Godot runner:

```bat
set OPENBFME_GODOT=C:\Path\To\Godot_v4.7-stable_win64_console.exe
set OPENBFME_CONTENT=%CD%\workspace\content-packs
"%OPENBFME_GODOT%" --headless --path game --script res://tests/<runner>.gd
```

The direct run is accepted only if the runner has a registered evidence
contract and the harness checks exit code, acceptance marker, stdout, stderr,
timeout, process cleanup, and selected content identity.

`tools\gate-rotwk-systems.ps1 -SkipLiveRetail` is a useful offline diagnostic,
but its live-retail rows are `SKIP`; it is not a 2.02 acceptance command.
Publication/cook commands may change the selected pack and are integration-owner
operations, not focused verification commands.

## Public gates

Public CI has no retail installation or private content packs. It can require:

1. publication-boundary and hygiene checks;
2. product/modding contract validation;
3. importer unit, malicious-fixture, archive, schema, and deterministic-fixture
   tests that do not require retail bytes;
4. standalone engine experiment tests, clearly labelled non-shipping;
5. launcher and release-tool tests;
6. code-only Godot import/export checks; and
7. explicitly pack-free Godot diagnostics and deterministic fixtures.

Public green proves the source tree is buildable, contained, and internally
consistent. It cannot prove 2.02 content closure, runtime parity, visual/audio
fidelity, a complete match, or a release-ready private build. A public test that
skips for lack of retail input remains `SKIP` for that evidence lane.

## Private gates

A protected private Windows runner with the pinned retail installation and
conversion tools must perform, in order:

1. verify the baseline and English-overlay source contracts with
   `tools\verify-rotwk-202-baseline.py`;
2. build the effective archive catalog and reference-closure denominator;
3. cook from a clean state and verify deterministic repeat output;
4. publish new immutable addresses and atomically install the full selection;
5. verify every selected bundle's bytes and provenance;
6. launch with isolated user data and assert the resolved selection identity;
7. run focused runtime, deterministic state, save/load, replay, script/AI, and
   complete-match gates with zero unexpected diagnostics;
8. run original-game gameplay, visual, and audio oracle scenarios;
9. qualify required modes, reliability, performance, networking, and packaging;
   and
10. verify code-only/public artifact containment.

Stop on the first failed prerequisite. Never publish a new selection merely to
diagnose an unreviewed change. Preserve logs and artifacts under ignored
`workspace/` paths, bound to the complete evidence identity.

## Oracle acceptance

Every oracle row names the 2.02 source scenario, deterministic setup, map,
players/factions, seed, commands or input sequence, camera/render state when
applicable, expected state/events, tolerance, and reviewer disposition.

Tools may capture, align, hash, and compare evidence. They do not operate the
original game autonomously, invent missing camera state, or approve subjective
visual/audio parity. Required rows with unresolved severity-0 or severity-1
differences fail. Count-only asset or capture checks do not approve fidelity.

## Current known red baseline

These are audit facts from 2026-08-29 at base commit
`a75fc0b8ac8ca4926910123b776e9232b6ff0882`, before the current cleanup. They
are not acceptance thresholds and must not be normalized into success:

| Check | Observed result | Interpretation |
|---|---|---|
| `tools/gate-hygiene.ps1` | `FAIL count=25` | tracked workspace reports, root allowlist drift, and absolute paths made repository hygiene red |
| `run_retail_slice.bat --test` | `passed=369 failed=61` | current selected runtime failed retail acceptance |
| `tools/gate-rotwk-systems.ps1 -SkipLiveRetail` | two offline checks passed; live stages skipped | live product evidence is `SKIP`, not PASS |
| script wiring probe | process exited 0 and printed 111/0 while stderr contained real armor/damage errors | exit-code-only runner admission can false-green |

The current cleanup tree now reports `HYGIENE_GATE PASS`; that repairs only
the first repository-hygiene row above. The 369/61 runtime result and all
product-parity blockers remain red until replaced by current, identity-bound
evidence through their ledger items.

The audited selection mounted 100 packs, led by
`rotwk-men-vslice/5079efbdb8364dd2e5a1070820388d1fcee853e2f32a766965e1b25a7bcb0298`.
Its active manifest declared asset conversion, full-faction completion, oracle
parity, vertical-slice completion, source closure, and denominator closure
false. These facts block complete claims even if an unrelated suite is green.

The old 2.01 product-contract check and historical importer receipts do not
certify the new 2.02 target. Establish a fresh v9.7.7 evidence identity before
using updated numbers.

## Test and work-item admission

Each persistent runner and each row in `orchestration/work-items.json` must
identify:

- target contract and subsystem;
- owned source files and runtime consumer;
- required public/private inputs;
- focused command, timeout, and acceptance marker;
- forbidden and explicitly expected diagnostics;
- evidence level and artifact location; and
- exact completion and regression conditions.

Retain tests for oracle-backed compatibility, determinism/serialization,
schema/protocol/mod contracts, private containment, networking/recovery, a
reproduced defect, or a measured performance budget. Test volume and historical
runner totals are not progress metrics.

## Definition of done

A 2.02 feature is complete only when its effective source is identified, its
references and provenance close, it is deterministically converted, the exact
addressed bundle is selected, the live runtime consumes it without forbidden
fallbacks, behavior agrees with the original-game oracle, required presentation
evidence is approved, and a protected gate prevents regression.

A complete-product claim additionally requires every included domain and every
completeness requirement in the product-scope contract to meet that standard
under one frozen evidence identity.
