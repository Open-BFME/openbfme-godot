# Retail structure lifecycle

How retail authors structure lifecycle — construction, damage states, destruction
— across SAGE state predicates, FXLists, particle systems and AudioEvents, plus
the profile schema and Godot presenter contract that consume it.

The worked examples are the Men (Gondor) structures and the neutral Fords
buildings, because those were converted first. The authoring patterns they
document apply to every faction's structures.

> **Consolidation note.** This document absorbs several former standalone
> documents, listed under their own headings below and preserved verbatim.
> Counts, blocker totals and hashes inside those sections are snapshots taken
> when that investigation was written and are **not** current status; they are
> kept because the surrounding evidence depends on them. Live gate results
> live only in [STATUS.md](../STATUS.md).

---

<!-- merged from docs/RETAIL_MEN_BUILDING_LIFECYCLE.md -->

## Exact Men building lifecycle closure

`retail_building_lifecycle.py` is the payload-free evidence pass for the five
structures in the Men-versus-Men Fords slice. It does not convert or publish
retail content. It proves the authored SAGE state predicates, exact W3D and
texture leaves, effective runtime modules, placement expressions, and the
remaining handoff gaps before a multi-state Godot binding is implemented.

### Retail result

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

### Completion-profile v1 handoff

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

### Base lifecycle models

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

### Critical conversion findings

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

### Godot handoff contract

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

### Deterministic private report

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

---

<!-- merged from docs/RETAIL_MEN_BUILDING_DAMAGE_EFFECTS.md -->

## Exact Men building-damage FX and particle closure

`retail_men_damage_effects.py` closes the definition and render-leaf side of
the five schema-v1 Men building lifecycle contracts. It reads only the current
private completion profile and the manifest-bound effective BFME2 tree. It
does not edit the profile, convert assets, change Godot, publish a pack, or
infer runtime behavior.

### Sealed result

The audit accounts for all of the authored lifecycle references:

- 5 Men lifecycle objects;
- 17 entering-state FX bindings resolving to 7 unique `FXList` definitions;
- 88 `ParticleSysBone` attachment records in exactly 29 source state groups;
- 14 direct attachment system IDs plus 7 additional IDs reached through the
  FX lists, for 21 unique particle-system IDs;
- 31 exact definition candidates: 11 single-family IDs and 10 dual-family IDs;
- 13 exact texture leaves and no W3D render leaves.

The deterministic contract aggregate is
`2659886f2a53c82a1f76f88399e68866dfa4911fe43fc6f08fdd1d3d354dcd3d`.
The two formatted contract files are byte-identical with SHA-256
`7584d58bc0f5f06e9bf92bd4b0be5657da6d2483c56d27ebc79bd62bab53ada9`.
The source effective-assets aggregate is
`fbf3c7e03ed5fe841cd7b71c3b2ac7dac712f0ece212bdc3621c7c4e99053568`.

Every selected FX list and particle definition records its exact virtual path,
winning BIG archive, archive offset, precedence, byte span, line span, and
SHA-256. Every scalar emission/control assignment is retained with its nested
section scope and source span. No INI bytes or texture payloads are embedded in
the report.

### FX-list expansion

| FX list | Particle-system edges | Other exact side effects |
| --- | --- | --- |
| `FX_BoilingOilAttack` | none | `MenFortressBoilingOilAttackMS` |
| `FX_BuildingDamaged` | `BuildingDamaged` | none |
| `FX_BuildingReallyDamaged` | `BuildingDamaged`, `RDTMediumExplosionLight`, `RDTMediumExplosion` | none |
| `FX_FortressCollapse` | `PCTFortressDust`, `BuildingDamagedBig`, `FortressExplosion` | `BuildingSink`, view shake |
| `FX_FortressDamaged` | `BuildingDamagedBig`, `FortressExplosion` | `FirePlumeSweetenerMS`, view shake |
| `FX_FortressReallyDamaged` | `BuildingDamagedBig`, `FortressExplosion` | `FirePlumeSweetenerMS`, view shake |
| `FX_StructureMediumCollapse` | `PCTMediumDust`, `RDTMediumExplosionLight`, `RDTMediumExplosion` | `BuildingSink`, view shake |

None of these seven definitions contains a nested `FXList` edge. The contract
still parses and reports nested FX-list edges fail-closed so a future retail
closure cannot silently omit one.

### Definition-family resolution

These systems have one authored family and can select it exactly:

- FX only: `BuildingContructDustCastles`, `BuildingDamaged`,
  `BuildingDamagedBig`, `FortressExplosion`, `FueltheFiresEmbers`,
  `MenFortressProxy`, `MenFortressSpray`, `MenFortressSpray02`,
  `MenFortressSteam`, `PCTFortressDust`, and `trollCageDust`.

These ten IDs exist in both `ParticleSystem.ini` and
`FXParticleSystem.ini`: `BuildingContructDust`, `FireBuildingLarge`,
`FireBuildingMedium`, `FireBuildingSmall`, `PCTMediumDust`,
`RDTMediumExplosion`, `RDTMediumExplosionLight`, `SmokeBuildingLarge`,
`SmokeBuildingMedium`, and `SmokeBuildingMediumRubble`.

The already-sealed retail oracle proves that object-draw and FX-list consumers
use one unqualified manager namespace and that repeated FX syntax is
last-definition-wins. It does **not** prove cross-family precedence or whether
the legacy subsystem is active. The contract therefore preserves both exact
candidates for all ten IDs and selects neither. File order in
`SubsystemLegend.ini` is recorded but explicitly does not prove load order.

### Exact current-profile delta

The sealed 349-resource preintegration profile contained 13 of the 31 required
definition conversions and 7 of the 13 required texture conversions. It also
contained three of the seven FX-list registry records.

The minimal proposal adds exactly 24 resources:

- 18 `sage-particle-definition` conversions for
  `BuildingContructDustCastles`, `BuildingDamagedBig`, both families of
  `FireBuildingLarge`, both families of `FireBuildingMedium`, both families of
  `FireBuildingSmall`, `FortressExplosion`, `FueltheFiresEmbers`,
  `MenFortressProxy`, `MenFortressSpray`, `MenFortressSpray02`,
  `MenFortressSteam`, `PCTFortressDust`, both families of
  `SmokeBuildingMedium`, and `trollCageDust`;
- 6 texture conversions for `excloud06hires.dds`, `exexplo03.dds`,
  `exfire01.tga`, `exfirescroll3.dds`, `exwater01.dds`, and `exwater04.dds`;
- 0 W3D resources.

The runtime-data proposal appends the 18 missing definition-registry
records, upserts the four missing FX-list records (`FX_BoilingOilAttack` and
the three Fortress lists), binds the five Men lifecycle objects, and adds the
four newly exposed dual-family IDs to the existing unresolved-family set. It
is marked `proposal-only-not-integrated` because the evidence generator never
mutates a profile.

The completion composer now validates this contract as a required input and
attaches exactly the 24 resources, 18 definition-registry records, four
FX-list records, and four new unresolved-family IDs. It deliberately does not
copy the proposal's five object bindings: the authoritative schema-v1
lifecycle objects already contain all 88 attachment records and 17 FX
bindings. The integrated private profile contains 373 resources and retains
all ten Men dual-family systems as unresolved.

The dedicated proposal is written privately to
`.private/scratch/men-damage-effects/profile-fragment-proposal.json`; the same
proposal is embedded in each contract for provenance.

### Runtime blockers retained

Definition closure is not rendered parity. Four blockers remain explicit:

1. Cross-family precedence is unresolved for the ten dual-family IDs.
2. Exact dynamic-emitter parameter execution is not implemented or rendered.
3. Follow-bone transforms and condition-driven emitter start/stop timing are
   not implemented; all 88 bindings are sealed without guessing those rules.
4. FX-list offset, sound, view-shake, and authored section-order execution is
   not implemented.

In particular, the 88-record closure includes exact construction, damage,
rubble, upgrade, door, and boiling-oil condition blocks carried by the schema.
It must not be flattened into a generic building-fire effect.

### Rebuild and verify

The following evidence command is the closure-generation step and must run
against the preintegration completion profile. The checked private
`contract-a.json` and `contract-b.json` were produced before the 24-resource
delta was applied. The current completion profile is now postintegration;
rerunning the audit against it intentionally produces a new zero-owner-delta
audit, not the sealed composer input. Use the required
`--men-damage-effects-contract` completion command in
`RETAIL_SLICE_PROFILE_COMPOSITION.md` to reproduce the integrated profile.

From the repository root, before integration or when intentionally refreshing
the ownership baseline:

```powershell
$env:PYTHONPATH = "importer"
$py = ".private/retail-work/tools/python-3.12-env/Scripts/python.exe"
& $py -m openbfme_importer.retail_men_damage_effects `
  .private/retail-work/cache/effective-assets `
  .private/retail-work/profiles/men-fords-v0-complete.generated.json `
  .private/scratch/men-damage-effects/contract-a.json `
  --profile-fragment-output `
  .private/scratch/men-damage-effects/profile-fragment-proposal.json
& $py -m openbfme_importer.retail_men_damage_effects `
  .private/retail-work/cache/effective-assets `
  .private/retail-work/profiles/men-fords-v0-complete.generated.json `
  .private/scratch/men-damage-effects/contract-b.json
```

Then run the focused public-code gates:

```powershell
$env:PYTHONPATH = "importer"
python -m pytest importer/tests/test_retail_men_damage_effects.py -q
python -m ruff check importer/openbfme_importer/retail_men_damage_effects.py `
  importer/tests/test_retail_men_damage_effects.py
python -m ruff format --check `
  importer/openbfme_importer/retail_men_damage_effects.py `
  importer/tests/test_retail_men_damage_effects.py
```

---

<!-- merged from docs/RETAIL_MEN_BUILDING_DAMAGE_AUDIO.md -->

## Retail Men building-lifecycle audio closure

`retail_men_damage_audio.py` seals the nine audio identifiers carried by the
five schema-v1 Men building-lifecycle contracts. It is an evidence and profile
planning pass only: it does not cook media, publish a pack, or claim SAGE mixer
parity. The completion composer now requires its sealed contract and performs
the narrow profile integration.

### Result

All 9 identifiers have one exact, case-matching `AudioEvent` definition and all
79 referenced media leaves resolve uniquely. The definitions come from the
effective `ini.big` winner at precedence 91. Seven structure Foley events and
the two voice events have no parent syntax, no competing effective definition,
and no lower-precedence catalog copy of their defining INI file.

| Identifier | Effective source and half-open file byte span | Samples | Exact source controls |
|---|---|---:|---|
| `BuildingBigConstructionLoop` | `data/ini/soundeffects.ini` `[235602,236334)` | 22 body + 5 attack + 3 decay | volume 80; pitch shift -25..-15; limit 3; priority low; loop; world/shrouded/everyone |
| `BuildingSink` | `data/ini/soundeffects.ini` `[516294,516577)` | 3 | volume 110; volume shift -10; pitch shift -3..3; limit 3; priority low; world/shrouded/everyone |
| `GondorArcheryRangeArrowQuiver` | `data/ini/soundeffects.ini` `[220374,220782)` | 10 | volume 27; volume shift -10; pitch shift -10..10; range 100..300; limit 3; priority lowest |
| `GondorArcheryRangeBows` | `data/ini/soundeffects.ini` `[221181,221504)` | 6 | volume 27; volume shift -10; pitch shift -5..5; range 100..300; limit 3; priority lowest |
| `GondorArcheryRangeDrawBow` | `data/ini/soundeffects.ini` `[220784,221179)` | 10 | volume 36; two authored `VolumeShift = -10` rows; pitch shift -5..5; range 100..300; limit 3; priority lowest |
| `GondorArcheryRangeHits1` | `data/ini/soundeffects.ini` `[221506,221806)` | 4 | volume 23; pitch shift -5..5; range 100..300; play percent 35; limit 3; priority lowest |
| `GondorArcheryRangeHits2` | `data/ini/soundeffects.ini` `[221808,222108)` | 4 | volume 23; pitch shift -5..5; range 100..300; play percent 35; limit 3; priority lowest |
| `GondorArcheryRangeVoiceFire` | `data/ini/voice.ini` `[500006,500268)` | 3 | volume 54; range 100..300; play percent 6; priority lowest; world/shrouded/everyone/voice |
| `GondorArcheryRangeVoiceAim` | `data/ini/voice.ini` `[500270,500627)` | 9 | volume 54; range 100..300; play percent 6; priority lowest; world/shrouded/everyone/voice |

No event authors `Pitch` or `Delay`. Every event authors `SubmixSlider =
SoundFX`. The private contract retains every parameter in source order instead
of collapsing duplicates, plus the exact block/file hashes, inclusive source
lines, archive-relative byte spans, archive offset, precedence, every sample
filename, media hash, size, and archive provenance.

### Exact profile delta

The existing registry already contains all nine event rows and 71 of the 79
unique samples. The only missing leaves are the attack/decay envelope of
`BuildingBigConstructionLoop`:

- attack: `CUBuild_consL1a`, `CUBuild_consL1b`, `CUBuild_consL1c`,
  `CUBuild_consL1d`, `CUBuild_consL2w`;
- decay: `CUBuild_consL3a`, `CUBuild_consL3b`, `CUBuild_consL3c`.

Those eight WAVs total 426,762 source bytes. None was owned by the 348-resource
pre-integration profile or named in `data/audio_events.json.samples`. The
contract therefore proposes exactly one eight-pattern PCM-audio resource under
`assets/audio/men-building-lifecycle/` and eight sample-registry merge rows.
`retail_fords_completion_profile.py` validates the sealed aggregate,
9-definition/79-leaf closure, precedence evidence, exact resource, and exact
registry delta before integrating it. It never duplicates the 71 owned leaves
or the nine existing event rows.

### Semantics boundary

This pass proves authored fields and media membership, not how `game.dat`
evaluates them. A parity runtime still needs an oracle for random-pool selection
and seed, duplicate-field evaluation, pitch/volume units, range attenuation,
world and shroud audience gating, priority/limit/play-percent arbitration,
attack-body-decay loop stitching and stop timing, and submix/mastering behavior.
The duplicate `VolumeShift` in `GondorArcheryRangeDrawBow` is intentionally
retained twice; choosing first-wins, last-wins, or cumulative behavior would be
a guess.

### Reproduce

```powershell
$env:PYTHONPATH = 'importer'
.private\retail-work\tools\python-3.12-env\Scripts\python.exe `
  -m openbfme_importer.retail_men_damage_audio `
  --effective-assets-root .private\retail-work\cache\effective-assets `
  --manifest .private\retail-work\cache\effective-assets\.openbfme\manifest.json `
  --catalog .private\retail-work\catalog\bfme2.json `
  --complete-profile .private\scratch\men-damage-audio\preintegration-profile.json `
  --output .private\scratch\men-damage-audio\contract-a.json

C:\Users\Jonathan\AppData\Local\Programs\Python\Python312\python.exe `
  -m pytest importer\tests\test_retail_men_damage_audio.py -q
C:\Users\Jonathan\AppData\Local\Programs\Python\Python312\python.exe `
  -m ruff check importer\openbfme_importer\retail_men_damage_audio.py `
  importer\tests\test_retail_men_damage_audio.py
```

Two independently emitted private contracts must be byte-identical before the
fragment is considered for integration. The sealed pre-integration profile is
SHA-256 `ea1b56ca9906d7cfa63f4c8949f236dd039b72ca1b03d6b624113944be19f91b`;
the contract aggregate is
`148e8089f3754899bb4a933fff61a4bdd5693320a8e6cfb15b6258dceebf206e`.

---

<!-- merged from docs/RETAIL_NEUTRAL_LIFECYCLE_PROFILE.md -->

## Fords neutral lifecycle conversion plan

`retail_neutral_lifecycle_profile.py` generates the private conversion batch for
the eight neutral lifecycle structures currently placed on Fords of Isen II:

- two `CaveTrollLair` placements;
- two `Inn` placements; and
- four `WargLair` placements.

The planner consumes the sealed unresolved-object census, the exact neutral
simulation-facts handoff, and the effective-assets tree. It verifies every
selected file against the census or simulation source evidence before emitting a
plan. It neither publishes retail data nor changes the shared slice profile.

### Exact output contract

The current contract is intentionally pinned:

| Item | Exact count |
|---|---:|
| Lifecycle structure types | 3 |
| Fords placements | 8 |
| GLB conversion resources | 22 |
| Unique W3D inputs | 24 |
| Model/weather texture inputs | 22 |
| Audio sample inputs | 40 |
| Total profile resources | 84 |
| Normalized lifecycle/model states | 26 |
| Source-authored no-render phases | 6 |

Each texture and audio leaf owns one exact pattern in one `hash-only` resource.
This is deliberate: a downstream composer can reuse an identical owner or reject
a collision without having to split a partially overlapping group. W3D resources
depend only on the exact one-source texture resources used by that conversion.
Snow replacements remain in the closure as evidence, but are not attached to the
normal-weather Fords conversions.

The twenty-two GLBs are:

- CaveTrollLair: construction, intact, damaged, really damaged, collapse, bib,
  and the `CaveTrollLairHole` visible rebuild-hole rubble;
- Inn: construction, intact idle, damaged, really damaged, collapse, rubble,
  post-rubble, bib; and
- WargLair: construction, intact, damaged, really damaged, collapse, bib, and
  the `WargLairHole` visible rebuild-hole rubble.

CaveTrollLair and WargLair retain the original object's authored `RUBBLE`,
`POST_RUBBLE`, and `POST_COLLAPSE` no-render conditions. They also spawn distinct,
visible rebuild-hole objects: `NBTrollLair_R` and `NBWargLair_R`. Each hole has
500 HP, zero health regeneration, fades in over exactly 2.0 seconds, and persists
until rebuild or explicit destruction. This preserves both objects instead of
replacing a no-render condition with an invented body state. Inn keeps the source
distinction between `POST_RUBBLE` (`GBGenRubble`) and `POST_COLLAPSE` (`NBInn_R`)
without converting a duplicate GLB.

### Generate the private plan

From the repository root with the private importer environment:

```powershell
$env:PYTHONPATH = "importer"
& .private\retail-work\tools\python-3.12-env\Scripts\python.exe `
  -m openbfme_importer.retail_neutral_lifecycle_profile `
  .private\scratch\fords-unresolved-census\census.json `
  .private\scratch\neutral-simulation-facts\handoff.json `
  .private\retail-work\cache\effective-assets `
  .private\scratch\neutral-lifecycle-profile\plan.json `
  --profile-output .private\scratch\neutral-lifecycle-profile\profile.json
```

The standalone profile is parsed through the real `ImportProfile` loader during
generation. The plan also records a canonical digest and a selected-source
aggregate, but never copies retail bytes into tracked files.

### Integration contract

`profileFragment.resources` can be merged into the composed private profile after
normal source-owner collision checks. `profileFragment.objectBindings.structures`
contains exactly three explicit records with:

- the source `typeName`;
- the intact `sourceVirtualModel`;
- the intact GLB path;
- the runtime `objectId`; and
- `matchMethod: exact-type-name`.

These bindings are lifecycle structures, not generic prop bindings. Map cooking
must route all eight placements to the structure path and must not also emit them
as ordinary GLB props or unresolved markers.

`structureLifecycles` is presentation metadata for lifecycle schema version 1. It
preserves source conditions, GLB/no-render mode, clip mode, next phase, audio event
IDs, direct particle attachment IDs, entering/collapse FX-list IDs, direct health
and threshold facts, and the separate rebuild-hole object states. Animation
completion is never simulation authority.

The proven health contract is:

| Type | Maximum health | Damaged threshold | Really-damaged threshold |
|---|---:|---:|---:|
| CaveTrollLair | 2000 | 1000 | 500 |
| Inn | 3000 | 2000 | 1000 |
| WargLair | 2000 | 1000 | 500 |

All eight map placements are proven to start at 100% health in the intact phase.
All three bibs are unconditional `W3DFloorDraw` modules: `StartHidden` is not
authored and `HideIfModelConditions` is empty, so `duringConstruction` is `true`.

### Deliberate blockers

The direct maximum-health and threshold fields are now bound. The SAGE-ancestor
inclusive threshold convention remains qualified until a BFME2 executable/oracle
check, rather than being silently treated as BFME2 proof.

Two death-timing gaps remain deliberately unassigned:

- BFME2's exact Cave/Warg `StructureCollapseUpdate` completion frame and original
  object removal ordering are not proven. The qualified Generals derivation is
  retained as evidence but is not promoted to runtime truth.
- Inn has no `StructureCollapseUpdate` and omits `KeepObjectDie.CollapsingTime`.
  Its D3 reachability and exact rubble/post-rubble timing remain blocked.

The separate exact particle plan now converts the selected definitions. This
standalone neutral plan still needs completion-composer cross-selection to attach
those converted definitions to its particle and FX-list IDs. The exact
Inn-to-CaptureFlag engine association rule remains unproven; the current pairs are
spatial/ID-adjacency evidence only.

Therefore this plan proves source selection and conversion intent, not completed
1:1 runtime parity. Integration still needs:

1. BFME2 oracle/executable proof for the two death-timing gaps above;
2. completion-composer handoff to the implemented and tested Godot lifecycle-v1
   route, including separate rebuild-hole objects;
3. cross-plan selection of the already-converted exact particle definitions;
4. real GLB conversion reports and Godot containment/preflight; and
5. rendered/audio comparison against the original game.

### Focused verification

```powershell
$env:PYTHONPATH = "importer"
python -m pytest importer/tests/test_retail_neutral_lifecycle_profile.py -q
python -m ruff check `
  importer/openbfme_importer/retail_neutral_lifecycle_profile.py `
  importer/tests/test_retail_neutral_lifecycle_profile.py
python -m ruff format --check `
  importer/openbfme_importer/retail_neutral_lifecycle_profile.py `
  importer/tests/test_retail_neutral_lifecycle_profile.py
```

The tests use repository-authored fixture bytes. They cover deterministic output,
the 22-from-24 model contract, one-source evidence ownership, `ImportProfile`
parsing, explicit no-render plus rebuild-hole states, exact health/bib facts,
census/simulation tampering, source-byte tampering, audio route tampering, and
semantic plan tampering.

---

<!-- merged from docs/RETAIL_MEN_BUILDING_PROFILE.md -->

## Men building lifecycle profile

`importer/profiles/men-fords-v0.json` describes the reachable non-snow,
non-upgrade base lifecycle for the five Men structures in the Fords vertical
slice. The contract replaces the former false presentation in which every
`intact.glb` was built from a retail `*_A.w3d` construction model.

The v0 contract remains the stable asset-composition input. The private
completion-profile composer now validates the sealed retail lifecycle report
and upgrades exactly those five runtime objects to schema version 1. It does
not modify the tracked base profile.

The evidence source is the sealed private report
`.private/retail-work/reports/retail-men-building-lifecycle.json`, whose
content digest is
`3a2f8d15ebfbf1b058a9b7efc7d1a386e9b8994964eb78fd81f56f286ba9d321`.
The tracked profile contains only virtual source names and runtime metadata;
retail and converted payloads remain under `.private`.

### Runtime contract

In the base input, every structure object has
`presentation.buildingLifecycle.schema =
openbfme.building-lifecycle-presentation` and `schemaVersion = 0`.
`presentation.model` is exactly the lifecycle's `paths.intact` value.

In the generated completion profile, each becomes schema version 1 with eight
ordered phases and resource-owned `visual.glb` bindings. The exact completion
aggregate is
`926603db27e34c47e49c60f5179d8454d0e0c4d7a4cca5d12069e1d103880a59`.
The composer reproduces that aggregate and the full profile byte-for-byte in
two clean passes. The generated profile SHA-256 is
`ea1b56ca9906d7cfa63f4c8949f236dd039b72ca1b03d6b624113944be19f91b`.

| Structure | Max health | Damaged at or below | Really damaged at or below |
| --- | ---: | ---: | ---: |
| Men Fortress | 7500 | 2500 | 1250 |
| Men Farm | 2000 | 1333 | 667 |
| Men Barracks | 3000 | 2000 | 1000 |
| Men Archery Range | 3000 | 2000 | 1000 |
| Men Stable | 3000 | 2000 | 1000 |

The six core path keys are `construction`, `intact`, `damaged`,
`reallyDamaged`, `rubble`, and `bib`. Ordinary structures have a distinct GLB
for every key. The fortress's damaged path deliberately reuses its intact GLB
and records the exact unresolved `GBFortress1D` texture substitution. The
converter now has a proven, fail-closed texture-override facility, but this
base profile does not pretend it is active until an integration owner adds the
distinct damaged conversion resource and changes the runtime path.

All five bibs are hidden during construction. The four ordinary floor draw
modules explicitly hide under `AWAITING_CONSTRUCTION` and
`PARTIALLY_CONSTRUCTED`; the fortress construction controller and operational
citadel are separate Objects, and the controller does not own the operational
bib.

Construction clips are marked `manual-progress`, intact idle clips are
`loop-random`, and D2/D3 clips are one-shot. The fortress has no base intact
idle clip. The exact source groups are:

| Structure | Construction | Intact | D1 | D2 | D3 | Bib |
| --- | --- | --- | --- | --- | --- | --- |
| Fortress | `GBFortress_A + _ASK + _ABL` | `GBFortress` | intact plus converter-ready, not-yet-wired `GBFortress1D` override | `GBFortress_D2 + _D2SK + _D2AN` | `GBFortress_D3 + _D3SK + _D3AN` | `GBFortress_Bib` |
| Farm | `GBFarm_A + _ASKL + _ABLD` | `GBFarm_SKN + _SKL + _IDLA` | `GBFarm_D1` | `GBFarm_D2 + _D2SK + _D2AN` | `GBFarm_D3 + _D3SK + _D3AN` | `GBFarm_Bib` |
| Barracks | `GBBarracks_A + _ASKL + _ABLD` | `GBBarracks_SKN + _2SKL + _2IDA + _2IDB` | `GBBarracks_D1` | `GBBarracks_D2 + _D2SK + _D2AN` | `GBBarracks_D3 + _D3SK + _D3AN` | `GBBarracks_Bib` |
| Archery range | `GBArcheryN_A + _ASKL + _ABLD` | `GBArcheryN_SKN + _SKL + _IDLA` | `GBArcheryN_D1` | `GBArcheryN_D2 + _D2SK + _D2AN` | `GBArcheryN_D3 + _D3SK + _D3AN` | `GBArcheryN_Bib` |
| Stable | `GBStable_A + _ASKL + _ABLD` | `GBStable_SKN + _SKL + _IDLA` | `GBStable_D1` | `GBStable_D2 + _D2SK + _D2AN` | `GBStable_D3 + _D3SK + _D3AN` | `GBStable_Bib` |

The profile has 29 core phase resources selecting 68 exact W3D leaves. The
construction, intact, D2, and D3 animated groups use `w3d-bundle`; D1 and bib
groups use `w3d-hierarchical`. A live metadata audit proves that every bundle's
model, hierarchy, and clips name the same retail hierarchy.

### Texture closure

The 29 primary model W3Ds reference 43 unique physical texture leaves. Every
leaf is selected by exactly one profile resource, and every model's
`inputResourceIds` stages all of its embedded texture dependencies. Existing
unit texture owners are reused for shared `GUArcher`, `GUManAtArms`, arrow, and
fire-sequence leaves. The structure texture owners use exact paths rather than
the former broad wildcards, so snow and fortress-upgrade textures are no longer
silently pulled into this base contract.

### Exact fortress texture override contract

W3D bundle, hierarchical, and static resources may declare a bounded
`options.textureOverrides` array. Each record contains exactly three safe
basenames:

```json
"textureOverrides": [
  {
    "authored": "gbfortress1.tga",
    "target": "gbfortress1.dds",
    "source": "gbfortress1d.dds"
  }
]
```

The option is rejected on other converters and without an explicit
`inputResourceIds` closure. Records are case-folded and canonically sorted;
authored/target stems and target/source suffixes must match, target/source must
be distinct, targets must be unique, and target-to-source chains are forbidden.

The coordinator structurally reads classic W3D texture-name chunks and string
texture properties inside shader-material chunks. It requires every model
reference sharing the target stem to equal the declared authored reference.
For `GBFortress.w3d`, this proves two exact `GBFortress1.tga` shader references.
Both the original target and replacement source must exist in the selected
closure and have different hashes. Only the job-local staged target is
atomically replaced, and a complete before/after closure hash proves no other
input changed. Cached retail data and the pinned plugin are never modified.

After Blender returns, the pipeline also requires its log to name the exact
staged target and parses the GLB itself. Exactly one authored image must be an
embedded PNG with at least one base-color material consumer. The report records
source, staged, encoded-image, and decoded-RGBA hashes; consumer indices;
dimensions; alpha equality; and maximum RGB delta. Non-DDS sources must decode
bit-identically. The pinned Blender and Pillow DDS decoders differ by up to two
integer RGB levels, so DDS sources require exact dimensions and alpha and a
maximum RGB delta of 2. Any larger mismatch fails the resource.

The private real-asset proof is under
`.private/scratch/fortress-texture-override`. Its repeated clean conversions
were byte-deterministic, retained identical geometry and all non-target images,
and changed only the intended base-color image. The production report remains
honest: it records both unequal decoded digests and the observed decoder delta;
it does not call those pixels bit-identical. Proving encoded PNG digest equality
independently would require running a second clean Blender conversion for every
override, which the production pipeline does not currently duplicate.

A live BFME2 catalog audit against `F:\BFME2` currently reports:

- 40,216 catalog entries;
- 80 profile resources and 315 resolved selections;
- zero missing required resources;
- zero duplicate physical catalog ownership;
- 32 converted building phase/component resources, 71 exact building W3D selections, and
  zero building output collisions;
- all 29 declared model texture closures equal the sealed lifecycle evidence.

### Fortress door boundary

Only the reachable closed, construction, and rubble door sources are retained:

| State | Source resource | Retail W3D |
| --- | --- | --- |
| Closed | `men-fortress-door-closed-source` | `GBFDoor_DRC.w3d` |
| Construction | `men-fortress-door-construction-source` | `GBFDoor_A.w3d` |
| Rubble | `men-fortress-door-rubble-source` | `GBFDoor_D3.w3d` |

Each file contains model, hierarchy, and animation headers in the same W3D.
The adapter imports each file exactly once, proves the exact armature-object
plus armature-data action pair created by the pinned plugin, and requires the
glTF exporter to merge those channels into exactly one named animation. The
three profile resources now emit `door-closed.glb`, `door-construction.glb`,
and `door-rubble.glb`; their runtime records have contained paths and
`status: ready`. Opening, damaged-opening, snow, upgrade-overlay, and unrelated
door variants are not in this slice contract.

### Honest remaining gaps

The lifecycle metadata explicitly retains these unresolved boundaries:

- the converter can now build and validate the fortress damaged material, but
  the distinct damaged resource and runtime path are not yet wired in the base
  profile;
- post-rubble/post-collapse removal, authored state FX and particles, and
  opaque draw scripts are not bound by this asset-only pass;
- rendered oracle comparison is still required for the closed, construction,
  and rubble door timing and placement.

Fresh metadata scans also retain unsupported secondary vertex/normal chunk
warnings in `GBFarm_SKN`, `GBBarracks_SKN`, `GBFarm_D3`, and `GBStable_D3`.
Those warnings are real converter risks and must not be downgraded to make a
full pack build pass. The profile validation and source-closure gates prove the
input contract, not successful GLB conversion for those four models.

### Focused verification

Run from the repository root:

```powershell
$env:PYTHONPATH = "importer"
& .private/retail-work/tools/python-3.12-env/Scripts/python.exe `
  -m unittest tests.test_men_fords_building_profile `
  tests.test_w3d_texture_overrides -v
ruff check importer/tests/test_men_fords_building_profile.py
ruff check importer/openbfme_importer/profile.py `
  importer/openbfme_importer/pipeline.py `
  importer/tests/test_w3d_texture_overrides.py
ruff format --check importer/openbfme_importer/profile.py `
  importer/openbfme_importer/pipeline.py `
  importer/tests/test_w3d_texture_overrides.py
```

Before selecting or handing off a private retail pack, run the repository's
full retail pipeline gate. This profile change alone is not a completed pack,
runtime lifecycle selector, or rendered 1:1 proof.

---

<!-- merged from docs/RETAIL_MEN_BUILDING_RUNTIME.md -->

## Private Men building lifecycle runtime

The Men structure presenter now consumes authored lifecycle resources instead
of stretching one construction model and hiding it at zero health. It is a
strict private-pack gate: no retail file is added to the repository, and a
missing, escaping, malformed, or unloadable required resource produces a
visible contract failure rather than procedural masonry.

### Required object contract

Each of the five selected bundle objects is a `structure` whose
`presentation.model` is the true intact model. Its
`presentation.buildingLifecycle` must use this v0 shape:

```json
{
  "schema": "openbfme.building-lifecycle-presentation",
  "schemaVersion": 0,
  "maxHealth": 3000,
  "damagedHealth": 1999,
  "reallyDamagedHealth": 999,
  "paths": {
    "construction": "assets/.../construction.glb",
    "intact": "assets/.../intact.glb",
    "damaged": "assets/.../damaged.glb",
    "reallyDamaged": "assets/.../really-damaged.glb",
    "rubble": "assets/.../rubble.glb",
    "bib": "assets/.../bib.glb"
  },
  "bibDuringConstruction": false,
  "unresolved": []
}
```

The numeric example only illustrates field types. The converter owns the exact
evidenced thresholds for each object. Runtime validation requires strictly
ordered thresholds and requires the entity's `maximum_health` to equal the
contract's `maxHealth`.

Fortress additionally requires all three authored door resources:

```json
{
  "components": {
    "door": {
      "construction": {"path": "assets/.../door-construction.glb"},
      "closed": {"path": "assets/.../door-closed.glb"},
      "rubble": {"path": "assets/.../door-rubble.glb"}
    }
  }
}
```

A source-only or blocked record with a null `path` is useful conversion
evidence, but is not a loadable runtime completion. Every body, bib, and
fortress-door path must be a safe pack-relative `.glb`, resolve inside the
selected pack, exist, and pass GLB header preflight. `presentation.model` must
equal `paths.intact`.

An optional `clips` object binds imported animation names without runtime name
guessing. Supported modes are `none`, `manual-progress`, `loop`, `loop-random`,
and `once`. `none` requires an empty names list. A construction entry must be
`manual-progress`; its declared clip is paused and sought from authoritative
`construction_progress`. Declared intact, really-damaged, and rubble clips are
played on phase entry when present in the authored scene.

### Deterministic state selection

The selector uses this precedence:

1. `construction_progress < 1` selects `construction`.
2. `health <= 0` selects retained `rubble`.
3. `health <= reallyDamagedHealth` selects `reallyDamaged`.
4. `health <= damagedHealth` selects `damaged`.
5. All other values select `intact`.

The bib is shown for completed living phases, hidden with rubble, and hidden
during construction unless `bibDuringConstruction` is true. Fortress door
selection is deliberately limited to construction, closed, and rubble; the
runtime does not invent opening or closing states.

All paths are preflighted before the presenter is accepted, while phase scenes
are instantiated lazily. The intact body is loaded once to establish its AABB.
One uniform scale and one vertical offset derived from that AABB are applied to
the common body/bib/door parent. Individual rubble, bib, door, and construction
models remain at identity scale, so their authored relative proportions are
preserved. Team tinting still happens on per-instance duplicated meshes and
materials through the explicit AssetFactory GLB helper; cached source scenes
remain immutable.

Runtime diagnostics are available through `lifecycle_state()` and the
`building_lifecycle_state` node metadata. They include current phase, active
body/bib/door paths, active declared animation, shared transform, retained
rubble state, and the first contract error.

### Health values and focused gate

The deterministic slice now uses the BFME2 base values requested by the
building contract:

- fortress: 7500
- farm: 2000
- barracks: 3000
- archery range: 3000
- stable: 3000

Run the legal-safe lifecycle gate from the repository root:

```powershell
& C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe `
  --headless --path game `
  --script res://tests/retail_structure_lifecycle_runner.gd
```

The runner covers all five health boundaries, construction precedence and
manual scrubbing, retained rubble, bib and fortress-door rules, exact
simulation maxima, shared intact-derived scaling, and malformed/missing/null
fail-closed behavior using repository-authored fixture nodes. It does not claim
that the private lifecycle GLBs have been converted; selected-pack validation
and rendered original-game comparison remain separate gates.

### Source-proven damage runtime closure

Schema version 1 is the authoritative Men damage route. It preserves the same
five exact object IDs and thresholds, adds explicit `collapsing`, `post-rubble`,
and `post-collapse` phases, and consumes only identifiers declared by the
selected pack. The five Men structures derive only these phase entries from
authoritative simulation facts:

1. construction progress below one selects `construction`;
2. the inclusive damaged threshold selects `damaged`;
3. the inclusive really-damaged threshold selects `really-damaged`;
4. zero health enters `collapsing`.

Zero health does not start a guessed timer. Once collapsing begins, ordinary
simulation sync retains collapsing, rubble, or a terminal phase. Advancing to
rubble or to post-rubble requires an explicit authoritative phase change. The
presenter reports the source timing blocker when automatic advancement is
requested.

For all five Men sources, `POST_RUBBLE` and `POST_COLLAPSE` author `Model=None`.
The runtime therefore hides the body and BIB in these terminal phases; it does
not invent a post-rubble GLB. The declared `SmokeBuildingMediumRubble` route is
retained as an identifier request.

Before a manifested selected-pack v1 structure is accepted, its non-null audio
event IDs, entering/collapse FX-list IDs, and particle-system IDs are checked
against the selected pack's audio definitions and Fords particle binding
document. Unknown IDs fail the structure contract. Known unresolved particle
family selections remain explicit route blockers and produce no fallback
emitter.

`RetailVerticalSlice` listens for phase-entry route requests. Exact audio IDs
are passed to `RetailSliceAudio`; missing events are rejected without replacing
them with a generic sound. FX and particle requests are passed to the Fords
battlefield and recorded. They remain non-rendering while the selected particle
contract reports cross-family precedence gaps or while a dynamic emitter
translation is not implemented.

Run the damage-route gate:

```powershell
& C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe `
  --headless --path game `
  --script res://tests/retail_structure_damage_effects_runner.gd
```

The gate covers all five exact threshold transitions, collapse entry, terminal
no-render states, exact declared audio/FX/particle IDs, unknown-ID rejection,
and the absence of invented collapse/post-rubble timing.

### Blocker delta

Closed by this runtime change:

- The presenter can represent all authored Men damage phases, including
  no-render `POST_RUBBLE` and `POST_COLLAPSE`.
- Exact Men thresholds drive construction, damaged, really-damaged, and
  collapse entry without changing the neutral lifecycle's explicit authority.
- Selected-pack lifecycle route identifiers now have a fail-closed registry
  boundary and deterministic dispatch seam.
- Exact declared structure audio can reach the retail audio router.

Still open and intentionally not guessed:

- The generated Men completion profile currently carries the older v0
  lifecycle. The importer/composer must publish source-proven v1 contracts
  before this closure is active in the private pack.
- Exact collapse-completion and post-rubble transition timing are absent from
  the retail data evidence. Runtime requires authoritative phase events.
- `SmokeBuildingMediumRubble`, `RDTMediumExplosion`,
  `RDTMediumExplosionLight`, and related identifiers retain unresolved
  ParticleSystem-versus-FXParticleSystem precedence.
- Dynamic structure FX/particle emitter translation remains disabled. Requests
  are preserved with exact blockers; no synthetic emitter is created.
- Completed-pack conversion, material/render comparison, effect timing, audio
  timing, and original-game visual proof remain required before 1:1 parity.

---

