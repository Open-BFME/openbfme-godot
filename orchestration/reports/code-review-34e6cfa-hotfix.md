# Adversarial code review — 34e6cfa + the v0.2.4.1 hotfix lane

Reviewer: opus-medium (read-only). Date: 2026-08-18. HEAD at review time: `6d89d0d`.
Grok's lane had NOT committed when this review closed (working tree: new
`game/tests/slice_start_roster_presentation_runner.gd`, modified
`playable_structure_runtime_consumer_runner.gd`, `gate-m2-focused.ps1`,
`queue.md`, `play-smoke-harness.md`; **`retail_structure.gd` and
`content_db.gd` are untouched**). Part B therefore reviews the *tests* Grok
has written plus the brief's proposed fix. No Godot was run for this review —
every number below comes from pack bytes and from logs the v0.2.4 checkpoint
already wrote.

---

## A. Commit 34e6cfa — "fix(importer): preserve retail static construction"

### A1. Root cause — CONFIRMED, with one material correction

Confirmed against pack bytes
(`workspace/content-packs/bfme2-men-vslice/7de517bf…/data/objects.json`):

```
bfme2.object.men-fortress  schema=openbfme.building-lifecycle-presentation  schemaVersion=1
  simulationFacts keys = ['collapse', 'damageStateRule', 'maximumHealth']   ← no 'construction' key AT ALL
  evidenceProfile        = <absent>            → composed == false
  phases[0]              = {"phase":"construction","animation":{"clip":"gbfortress_abl","mode":"manual-progress"}}
```

`bfme2.object.men-fortress` is in `MEN_LIFECYCLE_OBJECT_IDS`, so facts
validation runs `_validate_v1_men_simulation_facts`, which **never requires
`construction`** (retail_structure.gd:807-840 — collapse identity only). The
doc is legal. 34e6cfa then added, at retail_structure.gd:721:

```gdscript
if not construction_omitted:
    var construction: Dictionary = facts["construction"]   # unguarded
```

`construction_omitted` is only ever computed for `composed` docs (`:687-693`),
so for the Men lane it is always `false` and the index is always taken.

**Correction to the brief's diagnosis.** The brief says the chain is
"Invalid-access → null → expected 'none' → mismatch → fail-closed". That is
what happens in the **exported release build** (what the owner ran). In the
**debug/console Godot the gates use**, the error *aborts the enclosing
function* (the repo documents this behaviour itself in
`retail_slice_runner.gd:410-414`), `validate_lifecycle_contract` returns
nothing, the caller's `var contract_error: String = …` ends up empty, and the
check **fails OPEN**. Proof, from the v0.2.4 ship checkpoint itself:

| v0.2.4 checkpoint artifact | `Invalid access … 'construction'` lines | what the runner reported |
|---|---|---|
| `workspace/logs/q14fin-retail_slice_runner.err` | **11** (10 × `_configure_contract`, 1 × `_validate_retail_structure_lifecycle`) | `RETAIL_SLICE PASS slice_ready` |
| `workspace/logs/q14fin-fortress_command_surface_runner-men.err` | **4** | ran, known-fails only |
| `workspace/logs/playtest-v024/botmatch-men-vs-men.txt` | 3 | playtest reported green |
| `workspace/logs/playtest-v024/botmatch-ai-ladder.txt` | 3 | ” |
| `workspace/logs/playtest-v024/trebuchet-capture.txt` | 3 | ” |
| `workspace/logs/q13verify-retail_slice_runner.txt` | 11 | ” |
| `workspace/logs/q14fin-verify-boot.txt` (dist verify) | 0 — menu only, never enters the slice | PASS |

One backtrace, verbatim, from the release checkpoint:

```
SCRIPT ERROR: Invalid access to property or key 'construction' on a base object of type 'Dictionary'.
   at: _validate_v1_lifecycle_contract (res://src/retail_slice/retail_structure.gd:721)
       [1] validate_lifecycle_contract (retail_structure.gd:561)
       [2] _validate_retail_structure_lifecycle (retail_vertical_slice.gd:1399)
       [3] _load_required_presentation_definitions (retail_vertical_slice.gd:1129)
       [4] _initialize_content_and_match (retail_vertical_slice.gd:494)
```

This is the whole story of the escape: **the bug was printed 15 times during
the ship checkpoint, into `.err` files nobody read, by runners that reported
PASS because the fail-closed check fails open under the debug binary.** It
turns into a hard stop only in the exported build, which is the only place it
had never been exercised.

Why every Men match is affected (content-side root cause, brief's Q32):
`retail_vertical_slice.gd:2647-2652` — `_project_manifest_structure_definitions`
refuses to project the composed `playable-structures/menfortress.json` when a
bundle object for the same id already carries a `buildingLifecycle`. Both men
packs' `MenFortress` slugs to `bfme2.object.men-fortress`
(`playable_unit_runtime_adapter.gd::_runtime_id`), and `bfme2-men-vslice`'s
legacy `objects.json` registers that id. So the legacy 3-key doc shadows the
good composed doc in **both** RotWK Men and BFME2 Men matches, regardless of
which pack is active.

### A2. Was the fail-closed check added without a runner on the real path?

Not exactly — and the distinction matters for the hotfix's DoD.

- `_load_required_presentation_definitions` is reached by any runner that
  instantiates `res://scenes/retail_vertical_slice.tscn`. `retail_slice_runner.gd`
  (in `tools/gate-retail.ps1`) does exactly that, on the **live selection**
  (log header: `active=…/rotwk-men-vslice/a0fde4ac…`), and it **did** run at
  v0.2.4. `fortress_command_surface_runner` reaches the same validator through
  `RetailStructure.configure` → `_configure_contract`.
- `cah_match_runner.gd` (gate-retail) is the only runner that calls
  `_load_required_presentation_definitions()` *by name* — but on a detached
  probe slice with a hand-built manifest, so it tolerates a named structure
  refusal by design.
- So the brief's "the fortress surface runner + trebuchet playtest enter the
  slice by paths that skip the fail-closed roster validation" is **wrong**.
  They do not skip it. They execute it and it fails open. There is no runner
  gap in *coverage*; there is a gap in *observability* (stderr) and in
  *build mode* (debug vs export).
- 34e6cfa's own runner (`playable_structure_runtime_consumer_runner.gd`) was
  edited in the same commit and only ever validates fixtures the commit
  authored — self as oracle, exactly what rule "oracles are external retail
  data, never self" forbids.

**What should have gated 34e6cfa**

1. A stderr gate on every Godot runner: `SCRIPT ERROR` / `Invalid access` count
   must be zero (or an explicit named allowlist). This one guard alone would
   have red-flagged 15 lines at ship time. Today `.err` is written and never
   read, and `retail_slice_runner`'s own liveness comment proves the team knows
   an aborting error yields a silently green run.
2. A validation test whose input is the **shipped `objects.json` documents**
   loaded from `OPENBFME_CONTENT`, not a fixture authored by the same commit.
   The 243-doc corpus is scannable in under a second (see A3).
3. A rule for any new fail-closed check: enumerate the live corpus the check
   will newly judge and state the pass/fail split in the commit message.
4. Export-build coverage for at least one slice boot: debug and release Godot
   disagree about what an unguarded key read does, and only release is what the
   owner runs.

### A3. Every lifecycle doc in the live selection (selection `4e3e7024…` set)

Walked all 100 selected packs (`data/objects.json` objects[], every
`registration.presentation.buildingLifecycle` under `data/playable-structures/`,
`data/neutral*`): **243 lifecycle documents**. Scanner and raw dump:
`workspace/logs/rev34e6_scan.py`, `workspace/logs/rev34e6_lifecycles.json`.

Corpus shape:

| construction facts | evidenceProfile | phases[0] | count |
|---|---|---|---|
| **absent** | absent (Men lane) | construction / manual-progress | **5** |
| present (MANUAL + clip) | absent (Men lane) | construction / manual-progress | 3 |
| present (MANUAL + clip) | composed-structure-runtime | construction / manual-progress | 157 |
| `status:` omission marker | composed-structure-runtime | intact / none | 77 |
| `status:` omission marker | composed-structure-runtime | intact / loop-random | 1 |

The only failing docs — all five in one file, all reachable only through the
Men lane:

| pack | doc | object id | construction | phase0.mode | fails before hotfix | after hotfix | reachable today |
|---|---|---|---|---|---|---|---|
| bfme2-men-vslice/7de517bf… | data/objects.json | `bfme2.object.men-fortress` | **absent** | manual-progress | **YES** (aborts in debug / hard-fails in export) | pass | **YES** — `fortress` kind in every Men match, both editions |
| bfme2-men-vslice/7de517bf… | data/objects.json | `bfme2.object.men-farm` | absent | manual-progress | YES | pass | only via the legacy tiny-pack `DEFAULT_STRUCTURE_OBJECT_IDS` table (empty `faction_pack_roots`) |
| bfme2-men-vslice/7de517bf… | data/objects.json | `bfme2.object.men-barracks` | absent | manual-progress | YES | pass | as above |
| bfme2-men-vslice/7de517bf… | data/objects.json | `bfme2.object.men-archery-range` | absent | manual-progress | YES | pass | as above |
| bfme2-men-vslice/7de517bf… | data/objects.json | `bfme2.object.men-stable` | absent | manual-progress | YES | pass | as above |
| every other pack (238 docs) | — | — | present-and-agreeing, or `status` marker | — | no | no | — |

**Answer to the owner's question:** no other faction trips this check. Elves,
Dwarves, Isengard, Mordor, Wild, Angmar and the neutral packs are 100%
consistent (157 MANUAL docs whose phase-0 is `manual-progress`, 78 `status`
docs whose phase-0 is `intact`). The runtime guard fully unblocks play for
the construction cross-check. It does **not** address the id-shadowing that
makes Men present through the impoverished legacy doc in the first place
(brief's Q32) — that is cosmetic/parity, not a blocker.

Cross-check of 34e6cfa's *other* new strictness: I also replayed the
content_db `else` branch (`buildTimeSeconds > 0` and (`MANUAL`+clip or
`NONE`+null)) against all 235 `registration`-shaped docs — **0 rejected**. So
`content_db.gd` is not implicated in this outage at all; every one of the 15
recorded errors is `retail_structure.gd:721`.

### A4. Other unguarded accesses introduced by 34e6cfa

- `retail_structure.gd:721` `var construction: Dictionary = facts["construction"]`
  — **the one real defect**. Only unguarded `[]` on an optional key in the diff.
- `retail_structure.gd:722-723` `phases[0] as Dictionary` /
  `.get("animation") as Dictionary` — safe *today* only because the phase-count
  check and `_validate_v1_phase_row` already proved `phases[0]` is a Dictionary
  with a Dictionary `animation`. Both are re-derivations of facts the loop
  above already established; if the phase loop is ever reordered they become
  `Invalid call on base Nil`. Prefer reusing the validated row.
- `content_db.gd:3632-3641` — same two casts, same latent shape; there the
  preceding `typeof(construction_value) != TYPE_DICTIONARY → return false` means
  no key access can be unguarded.
- Pre-existing `facts["construction"]` at `:778` and `:864` are both guarded by
  a `typeof(...) != TYPE_DICTIONARY` loop immediately above. Not new, not a bug.

**Verdict on 34e6cfa: the code change is defensible; the commit is not.** A
fail-closed check landed with (a) an unguarded key read on a key its own Men
branch declares optional, (b) fixtures authored by the same commit as its only
oracle, and (c) no enumeration of the 243 live documents it started judging.
The 5 failing documents were discoverable in one second of scripting.

---

## B. The hotfix lane as of this review — FIX-FIRST

The runtime fix (`retail_structure.gd`, `content_db.gd`) has **not landed**, so
the guard itself cannot be reviewed. What exists is the test scaffolding, and
it has a structural problem that will make the lane *claim* success it has not
earned.

### B1. Blocking — the new tests cannot fail on HEAD (no failing-first evidence)

Both new tests assert on **return values**, and under the debug Godot the gates
use, the pre-fix code path aborts and yields an empty error string. Concretely:

- `slice_start_roster_presentation_runner.gd` asserts
  `_load_required_presentation_definitions() == ""`, `failure_reason` free of
  `structure_retail_visuals` / `missing-node` / the disagree text, and
  `structure_node.contract_error == ""`. Every one of those is already true on
  HEAD in a debug build — that is exactly why `q14fin-retail_slice_runner.txt`
  printed `PASS slice_ready` while its `.err` held 11 errors.
- `playable_structure_runtime_consumer_runner.gd`'s new
  `absent_error == ""` check passes on HEAD for the same reason.

The only debug-observable symptom of this bug is the **stderr line**. So:

1. The new runner must assert **zero** `Invalid access to property or key
   'construction'` / `SCRIPT ERROR` lines, which requires the harness to
   capture stderr (today `run-runner` writes `.err` separately and nothing
   reads it). Either merge `2>&1` into the runner log and grep in-process, or
   add the count assertion to `gate-m2-focused.ps1` on the `.err` file.
2. Failing-first evidence should be the stderr count on HEAD
   (`11` in `q14fin-retail_slice_runner.err` is already a recorded, dated
   pre-fix artifact — usable as the baseline) plus `0` after the fix.
3. If Grok pastes a "runner FAILS on HEAD" transcript that fails on the
   *return-value* assertions, that transcript is suspect — ask what changed.

### B2. Blocking — the runner's two "surfaces" are the same run

`retail_slice_ids.gd:36` — `const MAP_ID := "bfme2.map.fords-of-isen-ii"`, and
`_resolve_slice_map_id()` returns `MAP_ID` when nothing is set. The runner's
path (a) passes `map_id == ""` → resolves to Fords; path (b) passes Fords.
**Both paths boot BFME2 Fords with faction men.** The brief's requirement (a),
"rotwk skirmish Men host", is not covered, and `passed=10` is 5 checks run
twice over an identical configuration. Fix: path (a) must name a `rotwk.map.*`
id (and assert `slice.selected_pack_root` ends with the rotwk-men digest
`a0fde4ac…`), path (b) must assert `String(slice.map_id) == FORDS_MAP`.

### B3. Blocking — the runner can be green on a slice that never booted

`_run_path` never asserts `slice.ready_ok`. If the boot dies for an unrelated
reason (watchdog, map resolution, missing pack), `failure_reason` will not
contain any of the three substrings the runner greps for, the post-hoc
`_load_required_presentation_definitions()` call may still return `""`, and
`_structure_retail_visual_failures` returns an empty array because
`simulation` is null — **5/5 PASS on a dead slice**. Add `ready_ok == true`
as the first check of each path, and assert `simulation != null` /
`structure_ids().size() > 0` before trusting an empty failure list.

Related, same function: it also never asserts that the live selection was
actually mounted. With `OPENBFME_CONTENT` unset it will silently test the
durable/stale pack and pass (AGENTS.md rule 5). Assert the mounted active pack
digest.

### B4. Blocking — the objects.json fixture does not reproduce the shipped shape

`_legacy_objects_json_lifecycle()` sets `facts["construction"] = null` with the
comment *"objects.json authors the key as JSON null, not as a missing key."*
That is **factually wrong**; I read the bytes:

```
simulationFacts keys = ['collapse', 'damageStateRule', 'maximumHealth']
'construction' in simulationFacts  →  False
```

The key is **absent**. Present-but-null and absent are different GDScript
paths (`facts["construction"]` on a present-null key raises no *Invalid access*
at all — it raises on the typed assignment instead). The fixture must
`facts.erase("construction")` to be the shipped shape, and should cover both
absent and explicit-null.

Better still, and cheap: validate the **real document**. Load
`bfme2-men-vslice/…/data/objects.json` from the mounted selection and run
`validate_lifecycle_contract` on the actual `bfme2.object.men-fortress`
lifecycle. That is an external oracle; the current fixture is hand-grown (note
the synthetic `collapse` block and `effects` entries Grok had to bolt on to get
it through the Men lane) and drifts from the thing that broke.

### B5. Non-blocking but required before the guard lands

- **Do not widen `construction_omitted` into the phase-list computation.**
  `retail_structure.gd:702` is `if composed and construction_omitted and
  candidate_phase == "construction": continue`. If the hotfix redefines
  `construction_omitted` to include absent-construction and *also* drops the
  `composed and` prefix there, the five Men docs will suddenly be expected to
  have **no** construction phase, `phases.size()` will mismatch, and the outage
  changes message instead of ending. Use a separate flag (e.g.
  `construction_cross_check_skipped`) and leave `:702` byte-identical.
- **The strict check must survive when facts exist.** The brief's semantics
  (skip only when `facts.get("construction")` is null/absent, or is a
  Dictionary carrying `status`) is correctly narrow: 157 MANUAL docs + 3
  non-composed neutral docs still get cross-checked, and my corpus scan says
  all 160 pass. A doc with a *real* construction Dictionary that disagrees must
  still return the error — Grok's `disagree` fixture covers that direction
  (NONE facts vs manual-progress phase); add the mirror (MANUAL facts vs `none`
  phase), which is the polarity 34e6cfa actually intended to catch.
- **`content_db.gd` needs no functional change.** All 15 recorded errors are
  `retail_structure.gd:721`; the content_db twin already rejects a
  non-Dictionary `construction` before any key access, and 0 of 235 live
  registration docs fail its new strictness. If Grok touches it, the one thing
  it must **not** do is relax
  `if typeof(construction_value) != TYPE_DICTIONARY: return false` — that is
  pre-34e6cfa behaviour and relaxing it would let unauthored playable-structure
  docs into the registry silently.
- **Post-fix, code after line 730 runs on the Men docs for the first time
  since 34e6cfa** (bib, routes, rebuild-hole checks were never reached because
  the abort happened at 721). They passed pre-34e6cfa, so they should pass —
  but this must be *observed*, not assumed: print the actual return of
  `validate_lifecycle_contract` on the real men-fortress doc.
- Calling `_load_required_presentation_definitions()` a second time on a
  booted slice (runner line ~84) mutates slice state
  (`validated_battalion_capabilities.clear()`, re-classifies factions in the
  cross-team pass). Prefer asserting `ready_ok` + `failure_reason` from the
  boot itself and keep the direct call as a secondary diagnostic.
- `gate-m2-focused.ps1` pins `SLICE_START_ROSTER_RESULT passed=10 failed=0`.
  That number changes with B2/B3 (more checks per path); re-pin deliberately.

### B6. What the fix does *not* need to loosen

34e6cfa's genuine intent — "an explicitly static (`NONE`/null) construction
must validate, and a mixed NONE/MANUAL pairing must not" — is preserved by the
brief's guard: `_valid_construction_facts` still runs for every doc that
authors construction facts. Nothing in the proposed fix weakens the static
construction contract. The only semantics relaxed is "a lifecycle lane that
never required construction facts (Men) is no longer asked to prove them",
which is the correct reading of `_validate_v1_men_simulation_facts`.

---

## Verdicts

**34e6cfa — should have been gated by:** a zero-`SCRIPT ERROR` assertion on
every Godot runner (would have caught it 15 times at ship), a validator test
driven by the shipped `objects.json` rather than same-commit fixtures, a
corpus enumeration in the commit message (243 docs, 5 fail), and one
export-build slice boot. The code delta itself is defensible; the verification
around it was self-referential.

**Hotfix — FIX-FIRST.** Exact list:

1. Runner must assert **0** `Invalid access … 'construction'` / `SCRIPT ERROR`
   lines with stderr captured; use the pre-fix count (11 in
   `q14fin-retail_slice_runner.err`) as the failing-first baseline, because the
   return-value assertions pass on HEAD.
2. Path (a) must actually be a RotWK skirmish (`rotwk.map.*` + asserted
   `a0fde4ac…` pack root); today both paths are BFME2 Fords.
3. Assert `slice.ready_ok`, a non-null `simulation`, a non-empty structure set,
   and the mounted selection digest — the runner is currently green on a dead
   slice.
4. Fixture must `erase("construction")` (the shipped key is absent, not null),
   and should validate the real `objects.json` document as its oracle.
5. Keep `retail_structure.gd:702` as `composed and construction_omitted`; put
   the new semantics in a separate flag.
6. Leave `content_db.gd` functionally alone; in particular keep the
   `typeof(construction_value) != TYPE_DICTIONARY → return false` rejection.
7. Add the mirror strict case (construction `MANUAL` + phase-0 `none` still
   fails) and print the real men-fortress contract result post-fix.

Not verified by me: nothing was run. The claims above rest on pack bytes,
34e6cfa's diff, the current sources, and the v0.2.4 checkpoint logs named
inline. The v0.2.4 `retail_slice_runner` was already red on its own acceptance
line (`passed=370 failed=59`, `min_passed=374`) before this bug is counted —
that pre-existing redness is not attributed to 34e6cfa here.
