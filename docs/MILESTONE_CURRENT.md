# Current objective: systems-first RotWK iteration

**Owner:** integration owner / project owner  
**Owns:** the active systems-iteration objective agents and humans should advance  
**Does not own:** full product scope (see `DIRECTION.md`) or volatile gate numbers (see `STATUS.md`)  
**Update trigger:** the in-flight major system changes  
**Validation:** focused checks named for the active system; product policy in `contracts/rotwk-201-product-scope.json`

## Product context (read this first)

- **Parity baseline:** Rise of the Witch-king **2.01** (RotWK), not BFME2-only.
- **Development model:** **systems-first iterative** — finish major reusable systems
  against RotWK data; do **not** treat Men/Fords vertical-slice freeze as the strategy.
- **Full scope ladder:** `DIRECTION.md` system ladder (pipeline → maps → assets →
  sim → AI → shell → campaigns → WOTR → button last).

## Active systems-iteration objective

**Name:** RotWK major systems factory (in flight)  
**Primary systems (pull from top):**

1. RotWK content pipeline (default edition, pack publish/mount, fail-closed convert) — **operator path live** (`run_rotwk_systems.bat`)
2. Map cook IR + official skirmish map corpus cook/gap matrix — **live** (`rotwk_map_cook_corpus.py` + ledger %)
3. Asset closure + batch conversion (converter-gap burn-down) — **faction convert batch** (`rotwk_faction_convert_batch.py`, `-ConvertFactions`)
4. Object binding (type → model/logical; multi-map) — **binding factory** (`rotwk_binding_factory.py`)
5. Navigation / buildability generation (multi-map) — partial (connectivity probe in map cook)
6. Simulation core driven from pack descriptors — **next** (shell still boots selected pack entry map)
7. Script/AI packaging + runtime coverage growth
8. Skirmish shell over RotWK factions/maps — **partial** (`list_catalog_maps`; multi-map via `rotwk_multimap_skirmish.py` + pack `mapCatalog`)
9. One-button convert+play — **entry** `run_rotwk_one_button.bat` (convert path; `--multi-map` / `--publish` / `--launch`)

**Iteration rule:** one primary system per task packet; attach a focused automated
check; prefer proving generality on **≥2 maps or ≥2 factions** when claiming the
system is general; record results in `STATUS.md`.

**Done enough for a system:** reusable on RotWK data, fail-closed on gaps, checked
in CI or focused runners, not “looks fine on one snowflake profile only.”

**Not the objective:** one-button convert-all-maps UX before systems 1–8 are real;
BFME2 freeze before RotWK; inventing retail behavior; silent placeholder art in
parity mode.

## What agents must not do

- Reject RotWK, Angmar, multi-map, or multi-faction systems work as “out of slice.”
- Optimize solely for historical M2 Men/Fords acceptance unless the task packet
  explicitly names that legacy gate.
- Edit `selection.json` or publish packs without integration-owner authority.
- Weaken assertions to greening.

## Historical M2 Men/Fords contract (tooling legacy only)

> The following sections preserve the former M2 Men-versus-Men Fords vertical-slice
> acceptance contract and **required oracle capture IDs** so
> `importer/tests/test_m2_gate_script.py` and `tools/gate-m2-men-fords.ps1` keep a
> stable document surface. **They are not the active product strategy.**
> Retiring them requires retiring that gate tooling first.

### Historical identity rule

All M2 evidence targeted one tuple:

```text
git revision + dirty-state digest + profile SHA-256 + bundle SHA-256
```

`vertical_slice_complete` remained false until that legacy acceptance.

### Historical M2 scope (superseded as strategy)

- BFME2 1.06 Men versus Men on Fords of Isen II (legacy gate only).
- Four Men battalions and five structures as in the original M2 contract.
- Not the definition of current RotWK systems completion.

### Required oracle IDs

```text
map-overview
ford-north
ford-center
ford-south
player-base
enemy-base
unit-soldier-idle
unit-soldier-move
unit-soldier-attack
unit-soldier-death
unit-archer-idle
unit-archer-move
unit-archer-attack
unit-archer-death
unit-tower-guard-idle
unit-tower-guard-move
unit-tower-guard-attack
unit-tower-guard-death
unit-knight-idle
unit-knight-move
unit-knight-attack
unit-knight-death
structure-fortress-construction
structure-fortress-intact
structure-fortress-damaged
structure-fortress-rubble
structure-farm-construction
structure-farm-intact
structure-farm-damaged
structure-farm-rubble
structure-barracks-construction
structure-barracks-intact
structure-barracks-damaged
structure-barracks-rubble
structure-archery-range-construction
structure-archery-range-intact
structure-archery-range-damaged
structure-archery-range-rubble
structure-stable-construction
structure-stable-intact
structure-stable-damaged
structure-stable-rubble
hud-default
hud-unit-selected
hud-production
hud-victory
hud-defeat
```

`tools/m2-oracle-common.ps1` owns the executable capture-ID list. This document
and that list must match exactly for the legacy gate.

### Historical final M2 declaration

Only the integration owner runs:

```bat
run_m2_acceptance.bat -IntegrationOwnerPublish
```

That command does not define RotWK systems-first product completion.
