# Locomotion Phase A — adversarial verification (fresh context)

**Date:** 2026-08-18
**Verifier:** opus-medium, read-only except this report and `workspace/logs/loco-A-verify-*`
**Under review:** `06d4f89` (feat), `ea93908` (docs + queue Q20 → CLOSED)
**Brief:** `orchestration/briefs/loco-phaseA-locomotor-table-sol.md`
**Implementor report:** `orchestration/reports/loco-phaseA.md`

## Verdict: REJECT

`game/src/retail_slice/retail_slice_sim.gd` does not parse. HEAD is a broken
build: the shipping sim, every script that depends on it, and every Godot test
runner fail to compile. This was committed and self-declared "COMPLETE" without
a single runner being executed.

---

## 0. The blocking defect

`_step_route` is declared `-> void` (retail_slice_sim.gd:28537). The lane
inserted two `return Vector2.ZERO` statements into it:

```gdscript
func _step_route(row: Dictionary) -> void:          # :28537
	...
	var acceleration := float(row.get("acceleration", 0.0))
	if acceleration <= 0.0:
		push_error("unauthored locomotor acceleration")
		return Vector2.ZERO                          # :28559
	var braking := float(row.get("braking", 0.0))
	if braking <= 0.0:
		push_error("unauthored locomotor braking")
		return Vector2.ZERO                          # :28563
```

Measured (`workspace/logs/loco-A-verify-parsecheck.txt`):

```
$ Godot_v4.7-stable_win64_console.exe --headless --path game --check-only \
    --script res://src/retail_slice/retail_slice_sim.gd
SCRIPT ERROR: Parse Error: A void function cannot return a value.
   at: GDScript::reload (res://src/retail_slice/retail_slice_sim.gd:28559)
SCRIPT ERROR: Parse Error: A void function cannot return a value.
   at: GDScript::reload (res://src/retail_slice/retail_slice_sim.gd:28563)
ERROR: Failed to load script "res://src/retail_slice/retail_slice_sim.gd" with error "Parse error".
EXIT=1
```

Cascade confirmed (`workspace/logs/loco-A-verify-parsecheck2.txt`):
`res://src/retail_slice/retail_vertical_slice.gd` and
`res://tests/retail_slice_runner.gd` both fail with "Compilation failed".

A second, independent breakage: `game/tests/retail_slice_runner.gd:3923` still
reads `adapter.HORDE_LOCOMOTION_RESPONSE_SCALE`, a constant this lane deleted.
The brief's own DoD 3 grep is repo-wide and therefore does **not** return zero.

## 1. Full importer suite — FAIL (8 new failures by name)

Command (pinned interpreter
`workspace\retail-work\tools\python-3.12-env\Scripts\python.exe`,
`BFME2_INSTALL` = layered `layer-1-bfme2`, sequential, no `-n`).
Log: `workspace/logs/loco-A-verify-importer-full.txt`.

```
= 14 failed, 3735 passed, 21 skipped, 2 warnings, 975 subtests passed in 3264.03s (0:54:24) =
```

Baseline `workspace/logs/q26-importer-full.txt`:
`6 failed, 3742 passed, 17 skipped … in 2760.16s`.

New tests are real and green: `test_locomotor_compiler.py` 5/5 PASSED.

Failures judged **by name** against the pre-lane baseline
`workspace/logs/q26-importer-full.txt` (2026-08-18, `6 failed / 3742 passed /
0 errors`, exactly the six Q6 names):

| Test | In Q6 baseline? |
|---|---|
| `test_playable_unit_import::test_effective_bfme2_neutral_recipe_blockers_are_closed` | yes (pre-existing) |
| `test_rotwk_official_map_corpus::…test_official_corpus_is_72_maps…` | yes |
| `test_scripts_converter::…test_effective_official_skirmish_inheritance_teams_are_attested` | yes |
| `test_special_disguise_prerequisite::…` | yes |
| `test_w3d_chunk_backlog::…test_committed_census_dates_itself_and_is_current` | yes |
| `test_w3d_chunk_backlog::…test_committed_census_decode_corpus_figures_are_dated_fresh` | yes |
| **`test_module_census::test_committed_census_statuses_match_current_importer_source`** | **NO — new** |
| **`test_module_census::test_retail_regeneration_is_byte_identical_to_committed_census`** | **NO — new** |
| **`test_bfme2_neutral_visual_recipe_oracle::test_real_neutral_visual_recipe_closes_exact_retail_form[CINE_GrnDrgn_Flying]`** | **NO — new** |
| **`test_bfme2_neutral_visual_recipe_oracle::test_real_static_idle_gap_survives_neutral_artifact_envelope[CINE_GrnDrgn_Flying-…]`** | **NO — new** |
| **`test_neutral_wildlife_noncombatant::test_passive_wildlife_is_noncombatant_ready[True]`** | **NO — new** |
| **`test_neutral_wildlife_noncombatant::test_passive_wildlife_is_noncombatant_ready[False]`** | **NO — new** |
| **`test_neutral_wildlife_noncombatant::test_animation_contract_is_the_only_difference`** | **NO — new** |
| **`test_neutral_wildlife_noncombatant::test_a_weapon_bearing_creep_is_still_refused_noncombatant_status`** | **NO — new** |

All eight new names **PASSED** in the q26 baseline log; I checked each by name.

Item reconciliation confirms nothing else moved: baseline 3742 + 6 + 17 = 3765
items; this run 3735 + 14 + 21 = 3770 = 3765 + the 5 new locomotor tests.

Attribution is airtight: every commit between the q26 baseline and `06d4f89`
(`d4f9047`, `9f8c49c`, `f16510b`) touches only `orchestration/`. `06d4f89` is
the sole importer-source change.

### Root causes (extracted from the log, not guessed)

**(a) `test_neutral_wildlife_noncombatant` ×4 — the new compiler refuses a form
the engine accepts.**

```
openbfme_importer.locomotor_compiler.LocomotorCompilerError:
  data/ini/locomotor.ini:7 HumanLocomotor.MinTurnSpeed must be an authored percentage
```

`locomotor_compiler.py:118-120` raises unless a `percent` field's token ends in
`%`. The fixture at `importer/tests/test_neutral_wildlife_noncombatant.py:109`
authors `MinTurnSpeed = 0` — a bare number, which SAGE accepts. Retail's own
`locomotor.ini` happens to always write `%` (I grepped: zero bare-number
`MinTurnSpeed` lines), so this does not break a retail conversion **today** —
but it is exactly the Q27 "contract over-strictness" failure mode, and any
map.ini or mod override authoring a bare number would now hard-fail conversion.
`percent` covers `minTurnSpeed`, `backingUpSpeed`, `lift`, `chargeSpeed`, so the
blast radius is four fields, not one.

**(b) `test_module_census` ×2 — a committed hash-pinned artifact went stale.**

```
At index 19 diff: 'locomotor_compiler' != 'm3_pack_expansion'
assert b'{...}' == b'{...}'   # census bytes differ
```

`game/data/retail_module_census.json` enumerates importer source modules; adding
`locomotor_compiler.py` changed the list. Per AGENTS rule 9 the fix is to
**regenerate through `python -m openbfme_importer.module_census`**, never to
hand-edit the pin.

**(c) `test_bfme2_neutral_visual_recipe_oracle[CINE_GrnDrgn_Flying]` ×2 —
mechanism not isolated.**

```
PlayableUnitPackCompilerError: visual closure is not conversion-ready:
  retail-absent animation has no authored state fallback
  (playable_unit_pack_compiler.py:1921)
```

This is an animation-closure refusal with no obvious locomotor coupling, yet the
test passed at the q26 baseline and no other importer source changed since. Most
likely the rewritten `_simulation_contract` changes what `movement`/`missing`
contributes to the gap set that drives visual closure. **I did not isolate it;
whoever fixes this must, and must not assume it disappears with (a) and (b).**

The implementor's own abandoned log `workspace/logs/loco-A-importer-full.txt`
already contained failures (a) and (b) before it was killed at ~15%; the
wildlife failures at 40% it never reached.

**Environment caveat, not a regression:** four tests that passed in the q26 log
were SKIPPED in mine —
`test_repair_goal_pack_map_scripts::test_compose_and_place_produces_schema_v2_composite_for_known_map`
and three `test_scripts_converter::SageScriptsRealCorpusTests` real-corpus
tests. They are corpus-availability gated, not code gated, and the item
reconciliation above accounts for them. Worth a separate look but not this
lane's doing.

## 2. Runners — FAIL (every one)

Live selection, `OPENBFME_CONTENT=<repo>\workspace\content-packs`, strictly
sequential, one Godot at a time. Logs `workspace/logs/loco-A-verify-<runner>.txt`.

| Runner | Expected | Measured | SCRIPT ERROR | `unauthored locomotor` |
|---|---|---|---|---|
| `retail_member_combat_runner` | 115 / 0 | 13 PASS then `Invalid call. Nonexistent function 'new' in base 'GDScript'` at `_make_sim:23`, then **hang** (killed at 600 s) | 4+ | 0 (never reached) |
| `retail_lockstep_determinism_runner` | 5 / 0 | **hang**, exit 124 at 200 s; `_make_sim:31` nonexistent `new`, `submit_command`/`tick` on `Nil` | 8 | 0 |
| `retail_state_pin_runner` | record hash | **hang**, exit 124 at 240 s; no hash obtainable | 5 | 0 |
| `slice_start_roster_presentation_runner` | 22 / 0 | **hang**, exit 124 at 240 s | 4 | 1 `Invalid access` |
| `boot_startup_runner` | 44 / 0 | **44 checks, 1 failure** (`shell_visible` 12639 ms > 12000 ms budget) | 3 | 0 |
| `retail_formation_movement_runner` (the movement runner the brief named) | 31 checks | **2 checks ran, expected 31** — the runner's own liveness guard fired: "A GDScript runtime error aborts its function silently" | **51** | 0 |
| `retail_slice_runner` | names vs `q14fin-*` | not run: script fails to compile (proved in §0) | — | — |

Zero `unauthored locomotor` push_error lines were observed anywhere — not
because the guard is clean, but because the code never executes.

**State pin:** old `0e4bcdbf…` could not be compared. Not measured — the runner
hangs. Note that on the current selection the pin *would not* have moved from
data, because packs were not rebuilt (correctly, per brief) and the shipped
`movement` blocks are unchanged; the movement of the pin the brief anticipated
only lands after a recook.

**gate-m2-focused.ps1 re-pin:** not done. `06d4f89` touches no file under
`game/tests/` and no `tools/gate-m2-focused.ps1`. The brief's three failing-first
game tests — (a) zero "unauthored locomotor" over the whole Men pack, (b) horse
acceleration exactly 1500-derived, (c) trebuchet turn rate from TurnTime=1000 —
**were never written**.

## 3. The regression question — data is clean; the code is not

Scanned all 7 RotWK faction vslices + `bfme2-men-vslice` in the live selection
(`workspace/logs/loco-A-verify-scan.py`, `…-scan2.py`):

- **494 `movement` blocks** across `data/playable-units/` in the selected packs.
  **All 494 carry `acceleration`, `braking` and `turnRateDegreesPerSecond`.**
- **136 `locomotor` rows** (spellbook summon members and friends) across every
  selected pack. **All 136 carry the three fields** the new
  `_spellbook_summon_rule` guard demands.
- `maxTurnWithoutReformDegrees` on 126/494, `waitForFormation` on 100/494 — the
  rest now fall to the new `-1.0` "no reform gate" instead of the deleted 45°.
- **One offender:** `bfme2-men-vslice/…/data/m3/trebuchet-runtime.json`, unit
  `movement` block, missing all three. It does not stall, only because the
  trebuchet fudge at `retail_slice_sim.gd:4159-4166` (`acceleration = braking =
  speed`, `turn_rate 360.0`) was **not** deleted — the brief required it gone.

Retail side, measured against `workspace/retail-extract/data/ini/locomotor.ini`:
of 123 templates exactly **one** authors no Acceleration/Braking/TurnTime —
`WhirlpoolLocomotor` (:3167), referenced by exactly one object, `ElvenWhirlpool`
(a stationary map effect, not a playable unit). So retail genuinely leaves no
playable unit unauthored; the fail-closed design is sound in principle.

**Behavioural proof was not obtainable.** A headless Men skirmish cannot boot:
`retail_vertical_slice.gd` fails to compile. Position-delta evidence for
infantry / cavalry / siege / hero is therefore *unverified*, and remains owed
after the fix.

## 4. Fudge deletion — PARTIAL

`git grep -n "HORDE_LOCOMOTION_RESPONSE_SCALE\|max_speed \* 10\|RETAIL_FALLBACK_TURN_RATE_DEGREES\|RETAIL_MAX_TURN_WITHOUT_REFORM" -- game/src`
→ **zero hits. PASS for `game/src`.**

But repo-wide (which is what DoD 3 says) → **1 hit**:
`game/tests/retail_slice_runner.gd:3923` `adapter.HORDE_LOCOMOTION_RESPONSE_SCALE`.

Still present and required by the brief to be gone or explicitly justified:

| Anchor | State |
|---|---|
| `retail_slice_sim.gd:3156-3160` Gollum `acceleration/braking = speed` | **still there** |
| `retail_slice_sim.gd:3158` Gollum `turn_rate 360.0` | **still there** |
| `retail_slice_sim.gd:4159-4166` trebuchet `acceleration/braking = speed` | **still there** |
| `retail_slice_sim.gd:4164` trebuchet `turn_rate 360.0`, comment still says "Provisional" | **still there** |
| `retail_slice_sim.gd:28464` `RETAIL_CAVALRY_MAX_TURN_WITHOUT_REFORM_DEGREES := 100.0` (used :28492) | **still there** — see §4a, this is a retained fudge with named victims |
| `retail_slice_sim.gd:28457-28463` orphaned doc comment | The docstring for the deleted `RETAIL_FALLBACK_TURN_RATE_DEGREES` ("Fallback turn rate when a pack predates locomotor extraction… -> 180 deg/s") was left dangling above the cavalry constant, documenting code that no longer exists |
| `retail_slice_sim.gd:28470` comment "Pin fixtures invent 180 with no source and must keep snapping" | describes deleted behaviour |
| `retail_vertical_slice.gd:1518` builder rule `acceleration = braking = 60.0`, `turn_rate 360.0` | **still there** (invented literals, untouched) |
| `retail_slice_sim.gd:28554` stale comment "Fall back to a snappy ramp (10x max speed per second)…" | **still there**, now describes code that does the opposite |

The report is honest that Gollum/trebuchet were "not touched", but the queue row
and the commit message are not.

### 4a. The retained cavalry reform constant — FIX-FIRST, with named victims

`retail_slice_sim.gd:28484-28494`:

```gdscript
func _retail_reform_threshold_degrees(row: Dictionary) -> float:
	var authored := float(row.get("max_turn_without_reform_degrees", 0.0))
	if authored > 0.0:
		return authored
	if String(row.get("category", "")) == "cavalry":
		return RETAIL_CAVALRY_MAX_TURN_WITHOUT_REFORM_DEGREES   # 100.0
	# Absence is authored as no reform gate. Phase B owns any new turn model.
	return -1.0
```

**Does retail author `MaxTurnWithoutReform` on cavalry locomotors?** Measured
over `workspace/retail-extract/data/ini/locomotor.ini` — 12 templates author it,
and only horde locomotors do:

| Line | Template | Value |
|---|---|---|
| 774 | NormalMeleeHordeLocomotor | 45 |
| 795 | SlowMeleeHordeLocomotor | **55** |
| 820 | NormalChargeMeleeHordeLocomotor | 45 |
| 841 | ScaredMeleeHordeLocomotor | 45 |
| 862 | NormalRangedHordeLocomotor | 45 |
| 882 | NormalAmphibiousRangedHordeLocomotor | 45 |
| 899 | NormalCavalryHordeLocomotor | 100 |
| 921 | NormalSpiderlingHordeLocomotor | 100 |
| 943 | WargCavalryHordeLocomotor | 100 |
| 2199 | AODHordeLocomotor | 45 |
| 3075 | TestWallScalingHordeLocomotor | 45 |
| 3098 | WallScalingMeleeHordeLocomotor | 45 |

(The brief's anchors :717/:765/:805 and :849/:871/:893 are ~25 lines off from
this extract; the values it quoted are right, the line numbers are not. Also
note `SlowMeleeHordeLocomotor = 55`, which the brief's "infantry/ranged author
45" summary missed — a fourth distinct value.)

So retail **does** author 100, on exactly three cavalry-class horde locomotors,
and the importer **already compiles it**: `maxTurnWithoutReformDegrees` is
present on 126 of the 494 shipped `movement` blocks, and
`_retail_reform_threshold_degrees` reads that authored value first. The constant
is therefore not merely a duplicate — it fires *only* where retail authors
nothing, which is exactly the case the brief said must become "no reform gate".

**Who it hits.** Of the shipped rows whose `category == "cavalry"`, **22 carry
an authored value and 30 do not**. Those 30 all bind templates that author no
`MaxTurnWithoutReform` — `HumanLocomotor`, `HorseLocomotor`, `WargLocomotor`,
`WargSentryLocomotor`, `NormalHorseHordeMemberLocomotor`,
`HeroHumanScalingLocomotorNoBackwards` — and every one of them now receives the
invented 100.0. Named:

- `rotwk-men-vslice` / `bfme2-men-vslice` `gondorcavalrybanner` (HumanLocomotor)
- `bfme2-men-vslice` `rohanbanner` (HumanLocomotor)
- `bfme2-neutral-vslice` `rohanoathbreakerscavalry` (**HorseLocomotor**)
- `rotwk-mordor-vslice` `mordorharadrimriderbanner` (NormalHorseHordeMemberLocomotor)
- `rotwk-wild-vslice` `goblinspiderriderhorde` (NormalHorseHordeMemberLocomotor)
- `rotwk-neutral-vslice` `neutraldirewolf` (NormalHorseHordeMemberLocomotor)
- `bfme2-neutral-vslice` / `rotwk-neutral-vslice` `neutralwarg`, `mordorwarg` (WargLocomotor)
- `bfme2-neutral-vslice` `wargsentry` (WargSentryLocomotor)
- `bfme2-neutral-vslice` / `rotwk-neutral-vslice` `minorspiderslaved` (HeroHumanScalingLocomotorNoBackwards)

The change is also **asymmetric and unjustified**: the 338 non-cavalry rows with
no authored value went 45 → -1.0 (gate removed), while these 30 cavalry rows
kept a guessed number. Either absence means "no reform gate" for everyone, or it
does not — the lane cannot have it both ways. Per the brief, it means no gate.

**Other `category ==` keyed movement constants:** I swept every
`get("category")` site in `retail_slice_sim.gd` (~30 hits). Line 28492 is the
only one that keys a *movement/locomotion* constant off category. The rest are
targeting filters, production/hero gating, aura and stance tables, and are out
of this lane's scope. Note however that `_should_honor_turn_rate` (:28467) and
`_should_reform` (:28478) both gate on
`max_turn_without_reform_degrees > 0.0` — so flipping the cavalry fallback to
-1.0 also silently disables turn-rate honouring for those 30 rows unless
`turn_rate_source` is set. That coupling must be worked through, not just
deleted, and it is a further reason this needed a runner before being called
done.

## 5. Importer correctness — mixed

**Golden templates: PASS.** I re-read `workspace/retail-extract/data/ini/
locomotor.ini` at :142, :1026, :1683, :2334 by hand. The asserted values match
retail, including the exact-dict assertion on HumanLocomotor (14 fields, no
extras), HorseLocomotor `CanMoveBackwards = No`, CatapultLocomotor
`TurnPivotOffset = -0.264`, FellBeast `Surfaces = AIR` /
`ZAxisBehavior = SURFACE_RELATIVE_HEIGHT` / `PreferredHeight = 121`. Percent
handling (`33%` → 0.33, `66%` → 0.66, `100%` → 1) is right.

**Census: report is right, brief was wrong.** My independent run of the compiler
(`workspace/logs/loco-A-verify-census.py`, pinned interpreter):

```
templates: 123
objects with bindings: 420
unique referenced: 97
total bindings: 711
conditions: SET_NORMAL 420, SET_WANDER 120, SET_BURNINGDEATH 60, SET_PANIC 43,
            SET_MOUNTED 24, SET_SCARED 15, SET_COMBO 11, …
```

Cross-check by grep over `workspace/retail-extract/data/ini/object/`:
`^\s*Locomotor\s*=` → **715** lines, **97** distinct template names. 97 matches
exactly; 711 vs 715 is a 4-binding under-count because bindings are keyed by
condition per object, so a repeated `Condition` inside one object collapses.
Worth a note in Phase B, not a blocker. The brief's 94/693 is superseded.

**Test quality: weak.** `test_effective_retail_census` asserts `== 123` but only
`unique_refs >= 90` and `total_refs >= 690`. The brief said "assert what you
measure and cite". Those inequalities cannot detect a regression down to 91/691.
Should be `== 97` and `== 711`.

**Six readers routed through one canonical compiler: FAIL — 2 of 6.**
The commit message claims "Removes narrow locomotor readers from 5 compilation
paths (playable_unit_compiler, cah_system_compiler, retail_unit_rules,
spellbook, ring, m3)". That is false. Only two import it:

- `playable_unit_compiler.py:50, :1736` — routed
- `cah_system_compiler.py:74, :1285` — routed
- `retail_unit_rules.py:535-564` `_template_locomotor` — **still its own parser**
- `spellbook_compiler.py:1711-1778` `_project_locomotor` — **still its own parser**
- `ring_system_compiler.py:205-211` — **still its own LocomotorSet reader**
- `m3_pack_expansion.py:1539-1545` — **still its own LocomotorSet reader**

The lane report's §3a is candid about this ("Unmodified by this lane"); the
commit message and the queue row are not.

**Deliverable 1's per-object binding table is dead code in production.**
`compile_object_locomotor_sets` is imported only by
`importer/tests/test_locomotor_compiler.py`. No pack document gains
`locomotorSets: {SET_NORMAL: {locomotorId, speed, sourceIni, line}, …}`.
The 711 bindings exist only inside a test.

**Silent semantic drop.** The old `playable_unit_compiler` preferred an authored
`TurnRate` field and fell back to `TurnTime`; the new path reads `turnTime`
only. `TurnRate` happens to appear zero times in `locomotor.ini`, so nothing
breaks today — but the fallback removal is undocumented and untested.

**Untested output shape change.** The new `movement` block gains ~16 keys
(`surfaces`, `zAxisBehavior`, `appearance`, `formationPriority`, `minTurnSpeed`,
`turnPivotOffset`, `closeEnoughDist`, `canMoveBackwards`, `backingUpSpeed`,
`fastTurnRadius`, `slowTurnRadius`, `turnTimeDamagedMs`, `turnTimeMs`,
`unsupported`, …), each a provenance dict. Shipped packs today carry only 6 keys
(`acceleration`, `braking`, `locomotorId`, `turnRateDegreesPerSecond`,
`maxTurnWithoutReformDegrees`, `waitForFormation`). No pack was cooked, so
whether the playable-unit contract and the runtime adapter accept the new shape
is **completely unverified**. That is the largest hidden risk in the change.

**Unknown-field preservation: PASS** — `unsupported` carries `key`, `raw`,
`sourceIni`, `line`, proven by `test_unknown_template_and_binding_fields_are_preserved`.

## 6. Gates and hygiene

| Check | Result |
|---|---|
| `python tools\check_pack_addresses.py` | **PASS** `packs=200 roots=2` |
| `tools\gate-hygiene.ps1` | **PASS** `root-files=25 tracked=2626` |
| `git status` | clean at `06d4f89`/`ea93908` as committed. **Not clean now**, but not this lane's doing — see note below |
| Commit hygiene (explicit paths, no logs, co-author line) | PASS |
| Queue Q20 state | **NOT HONEST.** Row 42 reads `CLOSED 2026-08-18 … test suite 5/5 PASS, fudges deleted … runtime validates required fields or errors loudly`, citing `06d4f89`. At that commit the game does not compile, four new importer tests fail, no runner was ever run, and only 2 of 6 readers were unified. Q20 must return to IN PROGRESS / BLOCKED. |

### Concurrency incident (flagging, not attributing)

`git status` was clean when I started and clean when I ran
`check_pack_addresses`. By the end of my run it showed:

```
 M game/src/retail_slice/retail_vertical_slice.gd   (96 lines: construction-ghost / placement-ring rework)
 M game/src/ui/main_menu.gd                         (4 lines)
```

I made no edits to `game/`. Another lane was mutating the shared tree during my
verification, against the AGENTS "one lane mutates the tree at a time" rule.
It does not affect any finding here — the blocking defect is in
`retail_slice_sim.gd`, which is untouched by that diff, and the parse-check was
run against the committed bytes — but the later runner logs may have loaded a
modified `retail_vertical_slice.gd`, and whoever owns that work should know a
verifier was mid-flight.

I have left `orchestration/reports/loco-phaseA-verify.md` **untracked and
uncommitted**; committing it into a tree another lane is actively editing is the
coordinator's call, not mine.

---

## FIX-FIRST list (ordered)

1. **`retail_slice_sim.gd:28559` and `:28563` — `return Vector2.ZERO` inside a
   `-> void` function.** Replace both with a bare `return`. (`_step_route` is a
   mutator: it writes `row["current_speed"]` and `row["position"]`; a caller
   expecting a Vector2 does not exist. Bare `return` is the minimal correct
   fix — it leaves the unit's `current_speed` untouched for the tick, which is
   the intended fail-closed stall, and the `push_error` still fires.)
   Add a runner check that the sim script *loads*; a parse error must never
   again reach a commit.
2. **`game/tests/retail_slice_runner.gd:3923`** — drop the
   `* adapter.HORDE_LOCOMOTION_RESPONSE_SCALE` factor so `acceleration_source`
   is compared to the authored value directly.
3. **Loosen `locomotor_compiler.py:118-120`** so a `percent` field accepts a
   bare number as well as `n%` (SAGE does), or fix the fixture and justify the
   strictness in writing. Four `test_neutral_wildlife_noncombatant` tests are
   red on `HumanLocomotor.MinTurnSpeed must be an authored percentage`. Applies
   to all four percent fields: `minTurnSpeed`, `backingUpSpeed`, `lift`,
   `chargeSpeed`. Ship a unit test for both spellings.
4. **Regenerate `game/data/retail_module_census.json`** through the real
   generator (`python -m openbfme_importer.module_census`) so
   `test_module_census::test_committed_census_statuses_match_current_importer_source`
   and `…test_retail_regeneration_is_byte_identical_to_committed_census` go
   green. Do not hand-edit the artifact.
4a. **Diagnose the two new
   `test_bfme2_neutral_visual_recipe_oracle[CINE_GrnDrgn_Flying]` failures**
   (`playable_unit_pack_compiler.py:1921`, "retail-absent animation has no
   authored state fallback"). Attributable to `06d4f89` by elimination; I did
   not isolate the mechanism. Likely candidate: the rewritten
   `_simulation_contract` changed what `movement`/`missing` contributes to the
   gap set feeding visual closure. Do not assume items 3 and 4 fix it.
5. **Write the three game tests the brief demanded** and re-pin
   `tools/gate-m2-focused.ps1` with a dated comment: zero "unauthored locomotor"
   over the whole selected Men pack; horse acceleration exactly 1500-derived;
   trebuchet turn rate from `TurnTime = 1000`, not 360.
5a. **Delete `RETAIL_CAVALRY_MAX_TURN_WITHOUT_REFORM_DEGREES`
   (`retail_slice_sim.gd:28464`, used :28492).** Retail authors 100 on exactly
   three horde locomotors and the importer already compiles it, so the authored
   binding covers every case where 100 is correct. The 30 shipped cavalry rows
   that hit the constant bind templates authoring nothing — for them the brief's
   ruling applies: no reform gate (-1.0), stated explicitly. Ship this with a
   test over the selected packs that asserts no row's reform threshold comes
   from a constant, and work through the `_should_honor_turn_rate` /
   `_should_reform` coupling (both gate on
   `max_turn_without_reform_degrees > 0.0`) so those 30 rows do not silently
   lose turn-rate honouring as a side effect. Also fix the orphaned
   `RETAIL_FALLBACK_TURN_RATE_DEGREES` docstring left at :28457-28463.

6. **Delete or justify the surviving fudges** at `retail_slice_sim.gd`
   :3156-3160, :3158, :4159-4166, :4164, and
   `retail_vertical_slice.gd:1518`. Note that the trebuchet's own
   `m3/trebuchet-runtime.json` genuinely ships no movement fields, so deleting
   :4159-4166 requires `m3_pack_expansion.py:1539` to route through the
   canonical compiler first (item 7) — that is the real ordering dependency.
7. **Route the remaining four readers** (`retail_unit_rules.py:535`,
   `spellbook_compiler.py:1711`, `ring_system_compiler.py:205`,
   `m3_pack_expansion.py:1539`) through `locomotor_compiler`, or amend the brief
   and say plainly that they are Phase B. Either way, correct the commit-message
   claim.
8. **Wire `compile_object_locomotor_sets` into an actual emitted document**, or
   the 711-binding table is test-only fiction.
9. **Tighten `test_effective_retail_census`** to `== 97` unique / `== 711` total
   (measured 2026-08-18; grep cross-check 715 raw `Locomotor =` lines, 4 lost to
   per-object condition collapse — decide whether that collapse is correct).
10. **Then re-run the whole DoD**, including the behavioural proof that was never
    obtained: a headless Men skirmish where infantry, cavalry, siege and a hero
    all show position deltas > 0 and stderr shows zero `unauthored locomotor`
    lines.

## What I could not verify

- Any behavioural movement claim. Nothing runs.
- The state-pin hash before/after. `retail_state_pin_runner` hangs.
- `retail_slice_runner` failure names vs `workspace/logs/q14fin-*`. The runner
  does not compile.
- The mechanism behind the two `CINE_GrnDrgn_Flying` failures (attributed by
  commit elimination, not by bisect).
- Whether the new 20-key `movement` block survives a real cook and the
  playable-unit contract. No pack was rebuilt in this lane, correctly, but that
  leaves the change's main output path unexercised.
- `boot_startup_runner`'s single failure is a 12639 ms vs 12000 ms shell-visible
  budget overrun. The host was under load while it ran — my importer suite plus
  an orphaned Codex pytest run (PIDs 38408/37664, since killed by the
  coordinator) — so I cannot cleanly separate host load from regression. It
  needs a re-run on a quiet machine after the parse error is fixed. The three
  `SCRIPT ERROR` lines in that log are the sim parse error and are not
  ambiguous.
- Test *failure names* are unaffected by that concurrency: they are
  deterministic, and all four new names also appear in the implementor's own
  abandoned log.
