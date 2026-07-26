# Men building lifecycle profile

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

## Runtime contract

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

## Texture closure

The 29 primary model W3Ds reference 43 unique physical texture leaves. Every
leaf is selected by exactly one profile resource, and every model's
`inputResourceIds` stages all of its embedded texture dependencies. Existing
unit texture owners are reused for shared `GUArcher`, `GUManAtArms`, arrow, and
fire-sequence leaves. The structure texture owners use exact paths rather than
the former broad wildcards, so snow and fortress-upgrade textures are no longer
silently pulled into this base contract.

## Exact fortress texture override contract

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

A live BFME2 catalog audit against `<BFME2>` currently reports:

- 40,216 catalog entries;
- 80 profile resources and 315 resolved selections;
- zero missing required resources;
- zero duplicate physical catalog ownership;
- 32 converted building phase/component resources, 71 exact building W3D selections, and
  zero building output collisions;
- all 29 declared model texture closures equal the sealed lifecycle evidence.

## Fortress door boundary

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

## Honest remaining gaps

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

## Focused verification

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
