# Retail slice profile composition

`retail_slice_profile.py` composes the private Men-versus-Men Fords of Isen II
conversion profile. It is a deterministic metadata operation only: it does
not extract assets, build a content pack, publish a pack, or change content
selection.

## Exact inputs

The current composition consumes six already-validated inputs:

1. `importer/profiles/men-fords-v0.json`
2. `.private/retail-work/profiles/men-fords-v0-roads.generated.json`
3. `.private/retail-work/profiles/men-command-leaves.generated.json`
4. `.private/retail-work/reports/retail-static-prop-plan-0d51ad8d31ca6e6c.json`
5. `.private/retail-work/reports/retail-hierarchical-prop-plan-0d51ad8d31ca6e6c.json`
6. `.private/retail-work/reports/retail-animated-prop-plan-0d51ad8d31ca6e6c.json`

It also requires the real BFME2 catalog. Composition without catalog
resolution is intentionally unsupported. Pattern uniqueness alone cannot
prove that two different wildcard or exact rules do not select the same
physical `(archive, name)` entry.

The generated profile has the explicit profile ID
`men-fords-v0-full-generated`, while preserving the selected pack ID
`bfme2-men-vslice`.

## Deletion before addition

The composer removes twenty obsolete base resources by exact ID and exact
source-pattern list. Any missing ID, renamed path, changed case, or changed
pattern list fails the operation instead of broadening the deletion:

- the old PTGrass15 model and texture rules, replaced by the static plan;
- four semantic groups wholly owned by the faction leaf profile;
- all six legacy Gondor Soldier voice groups, replaced by the enumerated
  faction audio closure;
- eight legacy portrait, command, and training-icon rules whose source atlases
  are owned by the faction UI closure.

`gondor-fighter-definitions` is narrowed from nine sources to the five not
owned by the faction semantic closure: `armor.ini`, `gamedata.ini`,
`locomotor.ini`, `music.ini`, and `weapon.ini`. The four removed paths are
checked against the exact faction resource patterns.

The six deleted audio rules contain wildcards, so literal pattern comparison
is insufficient. Their 35 real catalog selections are resolved first and each
must be owned by the faction audio profile before deletion is accepted.

## Roads and prop bindings

The Road profile must be the base profile plus exactly five Road texture
resources, one exact `road-materials.json` runtime document, and only the
`roadMaterials` addition to the Fords map metadata. The expected Road IDs are:

- `Footprints`
- `FtPrintDrkGr02`
- `FtPrintGrass02`
- `FtprintsDrk`
- `FtprintsDrk02`

The static plan contributes 53 resources and 38 exact type-name bindings. The
hierarchical plan contributes 16 planned resources and six more exact
bindings. Three hierarchical texture rules are deliberately not duplicated:

| Exact retail source | Reused owner |
| --- | --- |
| `art/compiledtextures/gb/gbbarracks_n.dds` | `men-structure-shared-material-textures` |
| `art/compiledtextures/gb/gbfarm.dds` | `men-farm-material-textures` |
| `art/compiledtextures/pr/prgrey.dds` | `static-prop-texture-prgrey-791db9ad131c` |

The affected hierarchical `inputResourceIds` are rewritten to those existing
owners. This keeps one physical catalog owner per source while still staging
the raw texture for every dependent model conversion. The hierarchy batch
therefore adds 13 resources, not 16. The meshless `WtrflHaze` and
`WtrRiplsSmall` HLODs remain explicit rejected diagnostics instead of empty
GLBs.

The animated plan contributes three exact W3D bundles plus four raw texture
resources for the only currently eligible animated map props:

- `Fish`: 13 exact placements;
- `CaptureFlag`: two exact placements;
- `Egret`: two exact placements.

`art/compiledtextures/sh/shadowi.tga` is already owned by
`static-prop-texture-shadowi-6536093a930f`. The animated shadow resource is
therefore removed and the Egret bundle dependency is rewritten to that static
owner. The animated batch adds six resources rather than seven while retaining
all 17 planned placements.

The Fords `objectBindings.models` list is replaced, not appended blindly. Its
final value is the exact 38 static rows, the exact six hierarchical rows,
and the exact three animated rows, with 47 case-insensitively unique type
names.

## Faction UI and runtime data

All 85 faction resources are retained exactly. The three faction runtime
documents are copied without semantic edits:

- `data/audio_events.json`
- `data/strings.json`
- `data/ui_manifest.json`

The base Soldier audio runtime document is intentionally replaced by the
faction document. The other two paths are new. `pack.files` receives the exact
`audioEvents`, `strings`, and `uiManifest` entries.

The four member objects receive paths from these exact mapped-image rows:

| Object | Portrait | Command icon |
| --- | --- | --- |
| `bfme2.object.gondor-fighter` | `UPGondor_Soldier` | `WOR_GondorSoldier` |
| `bfme2.object.gondor-archer` | `UPGondor_Archer` | `WOR_GondorArcher` |
| `bfme2.object.gondor-tower-guard` | `UPGondor_TowerGuard` | `WOR_GondorTowerGuard` |
| `bfme2.object.gondor-knight` | `UPGondor_Knight` | `WOR_GondorKnights` |

Each mapped-image path must be an actual crop output of the faction resource
that owns its exact compiled atlas.

The three faction documents currently carry `complete: false`. That value is
preserved deliberately. The documents are exact and usable for the scoped
roster, but their producer does not claim full-faction asset conversion or
original-game oracle parity. Composition must never turn that conservative
provenance flag into a completion claim.

## Collision and acceptance gates

Before writing, the composer enforces all of the following:

- authoritative `ImportProfile.load` acceptance;
- no case-insensitive resource-ID collision;
- no case-insensitive source-pattern collision;
- no case-insensitive declared or concrete output collision;
- no case-insensitive runtime path collision;
- no case-insensitive map-binding type collision;
- no resource output equal to a runtime document path;
- no duplicate resolved `(archive, name)` owner;
- no missing required pattern or expected-count mismatch;
- no more than 512 resources (the bounded ImportProfile ceiling);
- exactly five Roads and 47 prop bindings;
- exact preservation of the faction runtime documents and their completeness
  flags.

The current real-catalog result is:

- 222 resources;
- 1,954 unique selected retail files;
- 1,729 declared/concrete cooked outputs;
- zero duplicate catalog selections;
- zero source, output, runtime, or binding collisions.

## Reproduce the private profile

From the repository root, using the pinned private Python environment:

```powershell
$env:PYTHONPATH = 'importer'
.private\retail-work\tools\python-3.12-env\Scripts\python.exe `
  -m openbfme_importer.retail_slice_profile `
  --base importer/profiles/men-fords-v0.json `
  --roads .private/retail-work/profiles/men-fords-v0-roads.generated.json `
  --faction .private/retail-work/profiles/men-command-leaves.generated.json `
  --static-plan .private/retail-work/reports/retail-static-prop-plan-0d51ad8d31ca6e6c.json `
  --hierarchical-plan .private/retail-work/reports/retail-hierarchical-prop-plan-0d51ad8d31ca6e6c.json `
  --animated-plan .private/retail-work/reports/retail-animated-prop-plan-0d51ad8d31ca6e6c.json `
  --catalog .private/retail-work/catalog/bfme2.json `
  --output .private/retail-work/profiles/men-fords-v0-full.generated.json `
  --report .private/retail-work/reports/men-fords-v0-full-composition.generated.json
```

The report records every input digest, exact prune, dependency reuse, resource
count, runtime merge, icon remap, selected-file count, and the final profile
SHA-256. The generated profile/report stay under `.private`; neither contains
permission to publish or select a pack.

## Late completion overlay

`retail_fords_completion_profile.py` composes the exact late closures onto the
sealed full profile without reopening its earlier decisions:

- header-only/no-motion prop correction;
- three neutral structure lifecycles;
- Fords particle and FX definitions;
- seven Fords ambient-audio roots and 57 exact samples;
- nine Men building-lifecycle audio events and all 79 exact samples, adding
  only the eight previously absent construction-loop attack/decay leaves;
- the exact Men building FX/particle definition closure: 18 missing
  definition projections, six missing textures, and four missing FX-list
  records, while preserving all ten dual-family Men systems as unresolved;
- Gondor Archer streak, arrow model, impact mappings, and 100 audio leaves;
- exact Fords AFTERNOON lighting/fog/shadow/cloud/macro environment evidence;
- the 261-source Men HUD APT runtime bundle, including all four formerly
  missing unconditional external-movie archives, plus the exact retail Albertus MT
  font winner as a separate one-source byte-preserving resource;
- the 10-type/26-placement animated-prop runtime contract.

The current deterministic result has zero missing required inputs; volatile
resource, projection, and file counts remain in generated reports. The profile SHA-256 is
`0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d`.
Two independent generations were byte-identical. Unsupported renderer/gameplay
semantics remain named blockers and `vertical_slice_complete` remains false.

Reproduce it from the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = '.private\retail-work\tools\python-3.12-env\Scripts\python.exe'
& $python -m openbfme_importer.retail_fords_completion_profile `
  --base-profile .private\retail-work\profiles\men-fords-v0-full.generated.json `
  --base-report .private\retail-work\reports\men-fords-v0-full-composition.generated.json `
  --no-motion-plan .private\scratch\no-motion-prop-profile\plan.json `
  --neutral-plan .private\scratch\neutral-lifecycle-profile\plan.json `
  --particle-plan .private\scratch\fords-particle-profile\plan.json `
  --ambient-audio-plan .private\scratch\fords-ambient-audio-profile\plan.json `
  --archer-projectile-plan .private\scratch\archer-projectile-profile\plan.json `
  --fords-environment-plan .private\scratch\fords-environment-profile\plan-a.json `
  --hud-apt-plan .private\scratch\hud-external-movies-profile\plan-a.json `
  --animated-runtime-contract .private\scratch\animated-prop-runtime-contract\contract.json `
  --men-damage-audio-contract .private\scratch\men-damage-audio\contract-a.json `
  --men-damage-effects-contract .private\scratch\men-damage-effects\contract-a.json `
  --catalog .private\retail-work\catalog\bfme2.json `
  --output .private\retail-work\profiles\men-fords-v0-complete.generated.json `
  --report .private\retail-work\reports\men-fords-v0-complete-composition.generated.json
```
