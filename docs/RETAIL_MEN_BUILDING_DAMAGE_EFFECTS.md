# Exact Men building-damage FX and particle closure

`retail_men_damage_effects.py` closes the definition and render-leaf side of
the five schema-v1 Men building lifecycle contracts. It reads only the current
private completion profile and the manifest-bound effective BFME2 tree. It
does not edit the profile, convert assets, change Godot, publish a pack, or
infer runtime behavior.

## Sealed result

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

## FX-list expansion

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

## Definition-family resolution

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

## Exact current-profile delta

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

## Runtime blockers retained

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

## Rebuild and verify

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
