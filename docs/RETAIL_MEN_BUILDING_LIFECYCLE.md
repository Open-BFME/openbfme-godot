# Exact Men building lifecycle closure

`retail_building_lifecycle.py` is the payload-free evidence pass for the five
structures in the Men-versus-Men Fords slice. It does not convert or publish
retail content. It proves the authored SAGE state predicates, exact W3D and
texture leaves, effective runtime modules, placement expressions, and the
remaining handoff gaps before a multi-state Godot binding is implemented.

## Retail result

The current BFME2 effective tree resolves all six required source Objects:

- `MenFortress`, the placement/construction/unpack controller
- `MenFortressCitadel`, the operational fortress
- `GondorFarm`, inheriting `FarmInterface`
- `GondorBarracks`
- `GondorArcherRange`
- `GondorStable`

The sealed report contains five runtime structures, six source Objects, 176
authored visual state blocks, 122 effective Body/Behavior/ClientBehavior
modules, 263 visual references, and 152 unique bound files. Of the visual
references, 242 resolve to exact physical leaves and 21 are authored SAGE
`None` semantics; none are missing, ambiguous, or invalid. The 152 unique files
total 32,503,119 bytes. All 376 unique W3D-embedded texture references in the
underlying visual closure resolve; the lifecycle grouping has 757 usages
because the same exact model can be selected by multiple predicates.

The current sealed report content digest is
`3a2f8d15ebfbf1b058a9b7efc7d1a386e9b8994964eb78fd81f56f286ba9d321`.
The deterministic formatted JSON file SHA-256 is
`d8d923ddc013088e01c03f7017b07399065a75408374b1ed7f15f0ebf7778b49`.

## Completion-profile v1 handoff

`retail_men_lifecycle_profile.py` now validates that sealed digest and upgrades
only the five Men objects while the completion profile is composed. The base
v0 profile remains the stable asset input. The generated completion profile
contains five `openbfme.building-lifecycle-presentation` schema-version 1
contracts with 40 phases total: construction, intact, damaged,
really-damaged, collapsing, rubble, post-rubble, and post-collapse for each
structure.

The v1 contract aggregate is
`926603db27e34c47e49c60f5179d8454d0e0c4d7a4cca5d12069e1d103880a59`.
It includes ten explicit no-render terminal phases, 17 authored entering-state
FX bindings, all 88 exact `ParticleSysBone` attachment records found in 29
particle-bearing visual-state blocks, and nine exact audio bindings: the
fortress construction and collapse loops plus seven archery-range
animation-frame events. Every GLB path resolves back to its existing profile
resource and every W3D input is present either as a sealed bound asset or an
exact hierarchy identifier in the source report. Neutral and all other runtime
objects are byte-unchanged by the upgrade.

The fortress terminal visual is sourced from authored
`DestroyObjectWhenDone = Yes`; the four ordinary structures use authored
`POST_RUBBLE` and `POST_COLLAPSE` `Model = None`. The fortress
controller-to-citadel BSE handoff is copied as source-proven evidence rather
than inferred from names.

The contract deliberately retains 40 blocker entries. The report does not
prove total collapse timing, the BFME2 executable's inclusive threshold
boundary convention, effect or particle execution, audio definition/mixing
closure, manual construction scrubbing, opaque draw scripts, or rendered
parity. No timing, effect, particle, or audio identifier is synthesized to
close those gaps.

## Base lifecycle models

This table is the non-snow, non-upgrade base path. The report retains every
additional snow, fortress upgrade, door, banner, floor, subobject, particle,
FX, and script predicate rather than collapsing them into this summary.

| Structure | Intact | Construction | Damaged | Really damaged | Rubble | Floor/bib |
| --- | --- | --- | --- | --- | --- | --- |
| Fortress placement controller | authored `None`; `GBFortress` only for phantom/world-builder presentation | `GBFortress_A` + `GBFortress_ASK.GBFortress_ABL`; construction door `GBFDoor_A` | not authored on this controller | not authored on this controller | `GBFortress_D3` + `GBFortress_D3SK.GBFortress_D3AN` | none |
| Fortress operational citadel | `GBFortress` plus `GBFDoor_DRC` and conditional overlays | `MenFortressCitadel` is not the construction controller | `GBFortress` with `GBFortress1D`; damaged door variants | `GBFortress_D2` + `GBFortress_D2SK.GBFortress_D2AN`; damaged door variants | `GBFortress_D3` + `GBFortress_D3SK.GBFortress_D3AN`; `GBFDoor_D3` | `GBFortress_Bib` |
| Farm | `GBFarm_SKN` + `GBFarm_SKL.GBFarm_IDLA` | `GBFarm_A` + `GBFarm_ASKL.GBFarm_ABLD` | `GBFarm_D1` | `GBFarm_D2` + `GBFarm_D2SK.GBFarm_D2AN` | `GBFarm_D3` + `GBFarm_D3SK.GBFarm_D3AN` | `GBFarm_Bib` |
| Barracks | `GBBarracks_SKN` + two authored idle clips | `GBBarracks_A` + `GBBarracks_ASKL.GBBarracks_ABLD` | `GBBarracks_D1` | `GBBarracks_D2` + `GBBarracks_D2SK.GBBarracks_D2AN` | `GBBarracks_D3` + `GBBarracks_D3SK.GBBarracks_D3AN` | `GBBarracks_Bib` |
| Archery range | `GBArcheryN_SKN` + `GBArcheryN_SKL.GBArcheryN_IDLA` | `GBArcheryN_A` + `GBArcheryN_ASKL.GBArcheryN_ABLD` | `GBArcheryN_D1` | `GBArcheryN_D2` + `GBArcheryN_D2SK.GBArcheryN_D2AN` | `GBArcheryN_D3` + `GBArcheryN_D3SK.GBArcheryN_D3AN` | `GBArcheryN_Bib` |
| Stable | `GBStable_SKN` + `GBStable_SKL.GBStable_IDLA` | `GBStable_A` + `GBStable_ASKL.GBStable_ABLD` | `GBStable_D1` | `GBStable_D2` + `GBStable_D2SK.GBStable_D2AN` | `GBStable_D3` + `GBStable_D3SK.GBStable_D3AN` | `GBStable_Bib` |

`POST_RUBBLE` and `POST_COLLAPSE` are authored as `Model = None` for the four
ordinary structures and the operational fortress door. They still carry
particle/FX and collapse-module semantics. Hiding a model at zero health is
therefore not enough to reproduce the authored destruction sequence.

## Critical conversion findings

The current base profile names all five outputs `intact.glb` but selects
`GBFortress_A`, `GBFarm_A`, `GBBarracks_A`, `GBArcheryN_A`, and `GBStable_A`.
The retail INIs prove that every `_A` model in that list is a construction
model. The intact models are the `_SKN` variants for farm, barracks, archery
range, and stable, while the operational fortress uses `GBFortress`.

The current Godot structure presentation also accepts only one
`assets/models/structures/men-*/intact.glb`. It scales that node vertically for
construction and hides it at zero health. It does not select the authored D1,
D2, D3, floor, door, animation, texture-override, particle, or post-rubble
states. A visually faithful building pass therefore requires a lifecycle
bundle and state selector; relabeling the existing `_A` conversion cannot close
the gate.

The fortress is a two-Object flow. `MenFortress` proves
`CastleToUnpackForFaction = Men Fortress_Men`, `InstantUnpack = Yes`, and the
construction W3Ds. `MenFortressCitadel` proves the operational and damage
states. The bounded BSE reader now parses
`bases/fortress_men/fortress_men.bse`: its `CastleTemplates` property key is
exactly `Fortress_Men`, its seven authored entries include exactly one
`MenFortressCitadel`, and every entry resolves to a type in the file's
`ObjectsList`. The lifecycle report therefore records this handoff as
`source-proven-template-to-operational-object-link` with payload-free hashes,
offsets, phases, and priorities instead of inferring it from names.

Damage thresholds are likewise not authored as numeric health percentages in
these selected visual blocks. The predicates are exact (`DAMAGED`,
`REALLYDAMAGED`, `RUBBLE`), but the health-to-condition transition policy must
come from evidenced SAGE body/runtime semantics or oracle captures. Do not use
arbitrary percentages as a parity claim.

## Godot handoff contract

For each structure, the next conversion/runtime pass must:

1. Convert the exact intact, construction, D1, D2, D3, floor/bib, and referenced
   animation W3Ds as distinct authored resources. Keep explicit texture
   substitutions tied to their original condition sets.
2. Scrub `AnimationMode = MANUAL` construction clips from authoritative build
   progress. The report finds 12 manual construction-animation states.
3. Switch health lifecycle only from an evidenced SAGE threshold policy. Play
   the one-shot D2/D3 clips and retain the 17 entering-state FX predicates and
   29 particle-system bindings for later exact effect conversion.
4. Honor floor visibility predicates during construction and remove the render
   model only at the authored post-rubble/post-collapse state.
5. Keep fortress door and optional upgrade overlays condition-driven. The
   citadel has nine effective draw modules and cannot be flattened to one mesh
   without losing authored behavior.
6. Treat the 20 opaque draw scripts as unsupported until their subobject and
   sound commands are interpreted. The report stores source lines, statement
   counts, and digests, not guessed behavior.
7. Add rendered original-game comparisons for construction progress, D1, D2,
   collapse, rubble dwell, post-rubble, doors, snow, and fortress upgrades
   before claiming 1:1 building damage.

## Deterministic private report

The report is written only under `.private/retail-work/reports` and contains no
model, texture, animation, or INI payload bytes. Rebuild it from the repository
root with:

```powershell
$env:PYTHONPATH = "importer"
& .private/retail-work/tools/python-3.12-env/Scripts/python.exe `
  -m openbfme_importer.retail_building_lifecycle `
  .private/retail-work/cache/effective-assets `
  .private/retail-work/reports/retail-men-building-lifecycle.json
```

Run the focused fixture and style gates with:

```powershell
$env:PYTHONPATH = "importer"
& .private/retail-work/tools/python-3.12-env/Scripts/python.exe `
  -m unittest tests.test_retail_building_lifecycle -v
ruff check importer/openbfme_importer/retail_building_lifecycle.py `
  importer/tests/test_retail_building_lifecycle.py
ruff format --check importer/openbfme_importer/retail_building_lifecycle.py `
  importer/tests/test_retail_building_lifecycle.py
```

A ready evidence report is not a successful GLB conversion, runtime state
binding, or rendered parity proof. Those remain separate fail-closed gates.
