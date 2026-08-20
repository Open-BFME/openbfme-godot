# AI-SWEEP — castle map AI coverage proof

Status: **RED / partial**. Commit-under-test started from `688a796`; the active maps pack resolved from `selection.json` was `rotwk-playable-maps-private/abc27325672bd5712d7785e23d5ff858133c86056f173032deb8c1de9141d4d1`.

The sequential ten-map runner proved base construction and attack orders on nine maps. Minas Tirith built five structures and its porter travelled to real sites, but it issued no attack order within the unchanged 4,000-tick budget. A focused route census found zero reachable hostile battalions or structures: every hostile target returned `no-bounded-route`. This is the Q51 missing layered castle-navigation runtime, so the assertion remains strict and the lane is not reported green.

## Per-map verdicts

| Map | Layout | Built | Attacked | Observed site sources | Runtime fix applied |
|---|---|---:|---:|---|---|
| Carn Dum | map-specific | 3 | Yes (1 order) | authored AIBase; generic navigation cell | No — coverage only |
| Erebor | map-specific | 3 | Yes (1 order) | authored build plots; authored AIBase | No — coverage only |
| Helm's Deep | map-specific | 4 | Yes (1 order) | authored build plots | No — coverage only |
| Minas Tirith | map-specific | 5 | **No (0 orders, RED)** | authored build plots; authored AIBase; generic navigation cell | No — rejected route was not disguised as an attack |
| Dol Guldur | map-specific | 2 | Yes (1 order) | authored build plots | No — coverage only |
| Grey Havens | map-specific | 3 | Yes (1 order) | authored build plots; authored AIBase | No — coverage only |
| Fornost | map-specific | 3 | Yes (1 order) | authored build plot; authored AIBase | No — coverage only |
| Isengard | generic fallback | 3 | Yes (1 order) | authored build plots after generic layout selection | No — coverage only |
| Black Gate | generic fallback | 4 | Yes (1 order) | generic navigation cells | No — coverage only |
| Minas Morgul | generic fallback | 3 | Yes (1 order) | authored build plots after generic layout selection | No — coverage only |

Every map printed its `CASTLE_SKIRMISH_AI GATE` rows where gates exist. The full result was `passed=104 failed=1 checks=105`; Minas Tirith was the sole failure. The isolated route census reproduced it at `passed=11 failed=1 checks=12`.

## Foundation-exemption proof

`game/tests/castle_foundation_exemption_unit_test.gd` seeds two synthetic `castle_fixture` placements through the same fixture-sim pattern as `castle_wall_defense_runner.gd`. Both compiled one-unit footprints contain the authored site point. Exactly one fixture is exempted: the foundation-role fixture. The neighboring wall-role fixture is not exempted. Result: `passed=3 failed=0`.

No runtime change was required for `_authored_site_foundation_fixture_contains`; this lane makes the previously unreachable branch executable under a sealed synthetic fixture.

## Gate results

| Gate | Result | Log |
|---|---|---|
| Foundation exemption | 3/0 | `workspace/logs/ai-sweep-foundation-exemption.txt` |
| Ten-map castle AI | **104/1** (Minas Tirith attack order) | `workspace/logs/ai-sweep-castle-skirmish-ai-10-maps.txt` |
| Minas Tirith route census | **11/1**, all hostile targets `no-bounded-route` | `workspace/logs/ai-sweep-minas-tirith-route-diagnostic.txt` |
| Retail state pin | `0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc OK` | `workspace/logs/ai-sweep-retail-state-pin.txt` |
| Castle gate | 41/0 | `workspace/logs/ai-sweep-castle-gate.txt` |
| Castle garrison | 36/0 | `workspace/logs/ai-sweep-castle-garrison.txt` |
| Retail slice failure names | baseline 87, actual 87, new 0, lost 0 | `workspace/logs/ai-sweep-retail-slice.txt` |

`retail_slice_runner` itself remains red at `passed=371 failed=59` / acceptance minimum 374, matching the named pre-existing baseline surface. It printed its result and acceptance line, then the Windows console process remained after an `0xC0000005` shutdown; this lane terminated only that runner's two PIDs. An unrelated concurrent `zz_verify_gp_probe.gd` runner was identified and left untouched.

## Re-run commands

Run from the repository top level in PowerShell. Set `OPENBFME_GODOT` to the Godot 4.7 console executable first; the resolver intentionally refuses maintainer-machine paths. These commands derive the main checkout from the worktree's common Git directory without embedding a personal path.

```powershell
$commonGit = git rev-parse --path-format=absolute --git-common-dir
$mainRepo = Split-Path -Parent $commonGit
$env:OPENBFME_CONTENT = Join-Path $mainRepo 'workspace\content-packs'
$godot = $env:OPENBFME_GODOT
if (-not $godot) { throw 'Set OPENBFME_GODOT to Godot 4.7 console' }
New-Item -ItemType Directory -Force workspace\logs | Out-Null
```

Foundation unit proof:

```powershell
& $godot --headless --path game --script res://tests/castle_foundation_exemption_unit_test.gd *> workspace\logs\ai-sweep-foundation-exemption.txt
```

All ten castle maps, sequentially inside one runner:

```powershell
Remove-Item Env:OPENBFME_CASTLE_AI_MAP -ErrorAction SilentlyContinue
& $godot --headless --path game --script res://tests/castle_skirmish_ai_runner.gd *> workspace\logs\ai-sweep-castle-skirmish-ai-10-maps.txt
```

One map can be isolated without changing the runner. Substitute any `MAP_CASES` id:

```powershell
$env:OPENBFME_CASTLE_AI_MAP = 'rotwk.map.wor-minas-tirith'
& $godot --headless --path game --script res://tests/castle_skirmish_ai_runner.gd *> workspace\logs\ai-sweep-minas-tirith.txt
Remove-Item Env:OPENBFME_CASTLE_AI_MAP
```

Required regression gates (run separately and sequentially):

```powershell
& $godot --headless --path game --script res://tests/retail_state_pin_runner.gd *> workspace\logs\ai-sweep-retail-state-pin.txt
& $godot --headless --path game --script res://tests/castle_gate_runner.gd *> workspace\logs\ai-sweep-castle-gate.txt
& $godot --headless --path game --script res://tests/castle_garrison_runner.gd *> workspace\logs\ai-sweep-castle-garrison.txt
& $godot --headless --path game --script res://tests/retail_slice_runner.gd *> workspace\logs\ai-sweep-retail-slice.txt
```

For the retail-slice comparison, extract only lines beginning `RETAIL_SLICE FAIL `, remove the prefix and everything from the first ` (` onward, trim whitespace, sort unique, and compare against `$mainRepo\workspace\logs\v026fin-retail_slice_runner.txt`. The recorded comparison is 87/87 with zero new and zero lost names.

## What is not done

- The requested proof is not complete for Minas Tirith: it builds, but no hostile target is reachable and no attack order is issued.
- Q51 layered wall/ramp navigation is not implemented here; that is the prerequisite for an honest Minas Tirith attack proof.
- The runner does not assert combat damage, only real construction plus issued attack orders, matching the existing L7 contract.
- No pack was built, published, selected, modified, unsealed, or re-addressed.
- The state pin was not re-minted and no hash pin was edited.
- No fresh-context adversarial verifier has accepted this lane yet.


## Verifier conditions applied (2026-08-19)

- The lane's originally attached sweep log predated the final runner edits (no
  ROUTE_TARGETS line, stale CLASSIFICATION lines). Canonical evidence is the
  fresh-context verifier's re-run: `workspace/logs/verify-as-sweep10.txt`
  (104/1, sole red = Minas Tirith attack) plus `verify-as-mtroute*.txt`.
- The Minas Tirith red is WIDER than an AI gap: the verifier's flood-fill
  proves the Player_1 seat sits in a 7,500-cell pocket disconnected from the
  main 89k-cell component holding every opponent, and the map's one gate
  fixture is navigation-inert (cell impassable, no portal). A HUMAN seated at
  Player_1 on this lobby-selectable map has no ground route to any enemy
  either. This is the Q51 navigation-layer prerequisite (or an importer
  passability over-block at the gate — not yet attributed), not an AI defect.
- Known weaker assertion: the "attack order" check counts one routed combat
  order (a strategic advance qualifies); it does not assert combat damage.
