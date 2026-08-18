# HOTFIX v0.2.4.1 r2 — construction-facts guard

Lane: grok-hotfix-0241. Brief `orchestration/briefs/hotfix-v0241-r2-grok.md`.
Binding review `orchestration/reports/code-review-34e6cfa-hotfix.md`.
Round 1 was stopped before publish; its working-tree edits were kept where
right and rewritten where the review required. No pack builds. No selection
change. Selection sha still `4e3e7024…`, active men
`rotwk-men-vslice/a0fde4ac89596cab4d34beae3ebce0e33aa30323f99a73727706a29c85d315c0`.

**STOP fired: no.**

## Review items

### 1. Separate `has_construction_facts` flag — DONE

`game/src/retail_slice/retail_structure.gd` restores the original
`construction_omitted` semantics (composed-only, status-marker). The phase-list
skip is still `composed and construction_omitted` (now at :711). A new local

```
has_construction_facts := typeof(facts.get("construction")) == TYPE_DICTIONARY
    and not (facts["construction"] as Dictionary).has("status")
```

(via `facts.get`, never an unguarded `facts["construction"]` index) gates
`_valid_construction_facts` + the phase-0 mode cross-check. Commit `a7c9904`.

### 2. `content_db.gd` reverted to HEAD — DONE

Round 1 had widened its construction-omitted branch. Re-edited by hand back
to the HEAD text. `git diff -- game/src/content/content_db.gd` is empty; the
file is not in any hotfix commit. `typeof(construction_value) != TYPE_DICTIONARY
→ return false` is untouched.

### 3. Failing-first evidence is the stderr count — DONE

Debug Godot fails OPEN (function abort → empty error). Return-value assertions
cannot fail pre-fix. Baselines:

| artifact | `Invalid access … 'construction'` / `SCRIPT ERROR` |
|---|---|
| `workspace/logs/q14fin-retail_slice_runner.err` (v0.2.4 ship) | **11** |
| `workspace/logs/hotfix-0241-slice-start-before.err` (r1 before-run) | **8** (two Fords boots; `passed=6 failed=4`) |
| `workspace/logs/hotfix-0241-r2-slice-start.err` (post-fix) | **0** |
| `workspace/logs/hotfix-0241-r2-retail_slice_runner.err` | **0** |
| every other r2 checkpoint `.err` | **0** |

The new runner prints `SLICE_START_ROSTER_HARNESS
require_stderr_clean=SCRIPT_ERROR,Invalid access`. `gate-m2-focused.ps1`
already merges 2>&1 and fails on `$forbiddenDiagnostics` (`SCRIPT ERROR`).
Re-pinned `SLICE_START_ROSTER_RESULT passed=22 failed=0`.

### 4. Two genuinely different surfaces — DONE

Path (a) names `rotwk.map.adorn-river` (a catalog RotWK skirmish map). Path (b)
names `bfme2.map.fords-of-isen-ii`. Log
`workspace/logs/hotfix-0241-r2-slice-start.txt`:

```
SLICE_START_ROSTER_PATH rotwk_skirmish_men ready_ok=true
  active_pack=…/rotwk-men-vslice/a0fde4ac… map=rotwk.map.adorn-river failure=<empty>
SLICE_START_ROSTER_PATH fords_bfme2_men ready_ok=true
  active_pack=…/rotwk-men-vslice/a0fde4ac… map=bfme2.map.fords-of-isen-ii failure=<empty>
PASS surfaces_are_distinct
SLICE_START_ROSTER_RESULT passed=22 failed=0
```

`host_pack` is still `bfme2-men-vslice/7de517bf…` (capability host — the RotWK
men pack has no entry-map surfaces). The live selection is asserted via
`ModLoader.active_pack_root` containing `a0fde4ac`, which is what the review's
"mounted digest" and the brief's "a0fde4ac in the log" name. Each path also
asserts `ready_ok`, non-null simulation, non-empty structure set.

### 5. Fixture erases the key; real doc is the oracle — DONE

`_legacy_objects_json_lifecycle("absent")` now `facts.erase("construction")`.
A `"null"` variant covers present-but-null. Consumer runner + slice-start
runner both load
`bfme2-men-vslice/7de517bf…/data/objects.json` `bfme2.object.men-fortress`
and print:

```
MEN_FORTRESS_FACTS has_construction=false keys=["collapse", "damageStateRule", "maximumHealth"]
MEN_FORTRESS_CONTRACT result=<empty>
```

That is the shipped shape the review read. Bib / routes / rebuild-hole checks
after :730 ran on the real doc and returned empty. Not papered over.

### 6. Mirror strict case — DONE

`disagree_manual`: construction `animationMode=MANUAL` + phase-0 `mode:"none"`
still returns `buildingLifecycle construction facts and phase animation
disagree`. `disagree_none` (NONE facts vs `manual-progress`) still fails too.
Consumer runner `passed=131 failed=0`, 0 SCRIPT ERROR.

### 7. Real men-fortress contract printed — DONE

Printed in both runners (item 5). Result `<empty>`. Nothing after :730 failed
on the legacy doc.

## Checkpoint

Sequential Godot, `OPENBFME_CONTENT=workspace\content-packs`. Logs
`workspace/logs/hotfix-0241-r2-*`.

| runner | expect | measured | STOP? |
|---|---|---|---|
| `slice_start_roster_presentation_runner` | 22/0, two maps, 0 Invalid access | `passed=22 failed=0`; maps `rotwk.map.adorn-river` + `bfme2.map.fords-of-isen-ii`; stderr 0 | no |
| `playable_structure_runtime_consumer_runner` | green + contract print | `passed=131 failed=0`; `MEN_FORTRESS_CONTRACT result=<empty>` | no |
| `retail_spellbook_runner` | 218/0 | `passed=218 failed=0` | no |
| `retail_member_combat_runner` | 115/0 | `passed=115 failed=0` | no |
| `projectile_table_runtime_runner` | 4/0 | `passed=4 failed=0` | no |
| fortress men | 113/1 `men_precompiled_page_selector_fallback_is_named` | `passed=113 failed=1` same name | no |
| fortress angmar | 93/2 `angmar_the_second_fortress_finishes_construction`, `runner_ran_every_section` | `passed=93 failed=2` same names | no |
| `retail_slice_runner` | fail NAMES vs q14fin; no NEW; `.err` Invalid access 11→0 | `passed=370 failed=59`; named FAILs 87→86, **0 new / 1 gone** (`seeded_structures_match_manifest_seed_kinds`); Invalid access **0** | no |
| `retail_lockstep_determinism_runner` | 5/0 | `passed=5 failed=0` | no |
| `retail_state_pin_runner` | `0e4bcdbf…` unchanged | `hash=0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc` + `OK` | no |
| `boot_startup_runner` | 44/0 | `44 checks, 0 failures` | no |

The gone slice FAIL name is a diagnostic line that used to fire while the
unguarded key aborted; the RESULT count is still 370/59 (same as v0.2.4).
Not a new failure.

## Version / dist

`tools/Test-DistPipeline.ps1` → **25/25 PASS**. VERSION pattern extended to
accept a four-part hotfix (`0.2.4.1`).

| item | value |
|---|---|
| commits | `a7c9904` `fix(game):` / `fc0713c` `test(game):` / `3ce4185` `chore(release): v0.2.4.1` / `8a76f1a` `docs(patch-notes):` |
| VERSION | `0.2.4.1` |
| notes | `docs/patch-notes/v0.2.4.1.md` |
| dist | `C:\Users\Jonathan\Desktop\open-bfme\dist\v0.2.4.1` |
| zip | `dist\v0.2.4.1.zip` (12.91 GB) |
| zip sha256 | `6b90f333576e2db18bb7411e556e4a359478ca12b7d1b662b0de0546cc1f2b65` |
| publish | detached WMI pid 38584; **28m 11s**; exit=0; did not refuse |
| self-sufficiency | OK — env-unset census matched (`packs=102`) |
| bundle commit | `8a76f1a` |
| selection | sha `4e3e7024…`; active `rotwk-men-vslice/a0fde4ac…` |

## Shipped-export proof

`OPENBFME_CONTENT=dist\v0.2.4.1\content-packs` (the `run-with-log.bat` env).
Runner `slice_start_roster_presentation_runner.gd`. Log
`workspace/logs/hotfix-0241-r2-shipped-export-runner.txt`.

| surface | map | ready_ok | a0fde4ac mounted from dist | Invalid access | RETAIL SLICE UNAVAILABLE |
|---|---|---|---|---|---|
| RotWK Men skirmish | `rotwk.map.adorn-river` | true | yes | 0 | 0 |
| BFME2 Fords | `bfme2.map.fords-of-isen-ii` | true | yes | 0 | 0 |

`SLICE_START_ROSTER_RESULT passed=22 failed=0`. `MEN_FORTRESS_CONTRACT
result=<empty>`. stderr forbidden count **0**.

Publish used the three AGENTS.md switches. This folder's self-sufficiency
probe **passed** (unlike v0.2.4). The menu will still print build 373
(`a93affd`), 4 commits behind the folder (`8a76f1a`) — Write-BuildInfo run
before the release commits, same as v0.2.4.

## Queue

- Q29 CLOSED (subsumed by Q31).
- Q31 CLOSED.
- Q32 filed: legacy objects.json men docs without construction facts /
  `:2647` shadowing of composed `menfortress.json`. Content-side, next BFME2
  men recook.
- Q33 left untouched (tree sway; filed by a parallel commit `a93affd`).

## Commits

- `a7c9904` `fix(game): skip construction phase cross-check when facts are absent`
- `fc0713c` `test(game): roster presentation gate for RotWK skirmish and Fords`
- `3ce4185` `chore(release): v0.2.4.1`
- `8a76f1a` `docs(patch-notes): v0.2.4.1 alpha hotfix`
- this report (follow-up `docs:` commit)
