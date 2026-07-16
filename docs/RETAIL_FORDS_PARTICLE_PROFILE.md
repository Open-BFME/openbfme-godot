# Fords particle/effects profile

`retail_particle_profile.py` is the exact private planning boundary for the
particle-bearing objects still needed by the Fords of Isen II slice:

- `CaveTrollLair`: 2 placements;
- `Inn`: 2 placements;
- `WargLair`: 4 placements;
- `WtrRiplsSmall`: 7 placements.

It consumes the sealed unresolved-object census, the effective-assets manifest,
the retail-binary particle-family oracle, and the matching private effective
tree. It does not publish a pack or select a content pack. Retail and converted
payloads remain below `.private`.

## Exact output

Against the current BFME2 1.06 census, the planner emits:

| Output | Count |
|---|---:|
| Exact target types | 4 |
| Exact Fords placements | 15 |
| Particle-system identifiers | 10 |
| `ParticleSystem` definition resources | 7 |
| `FXParticleSystem` definition resources | 10 |
| Total `sage-particle-definition` resources | 17 |
| Deduplicated exact texture-to-PNG resources | 9 |
| Hash-only `WtrRiplsSmall` W3D anchor resources | 1 |
| Total ImportProfile resources | 27 |
| Direct `ParticleSysBone` attachments | 11 |
| Object-to-FX-list roots | 13 |
| Unique FX lists | 4 |

Every definition resource has one exact source pattern, `limit=1`,
`expected_count=1`, and exact `kind` plus `name` options. The importer therefore
serializes only the selected definition block, not the full retail INI. Every
texture is converted once per exact physical source. The ripple anchor is
`art/w3d/p_/p_wtrriplssmall.w3d`, retained as a hash-only source dependency, and
its authored attachment bone is `waterRippleBone`.

The generated standalone profile writes the binding contract at
`effects/fords-particle-bindings.json`. That document records exact type names,
placement counts, attachment bones, attachment options, state/condition tokens,
FX-list triggers, definition candidates, texture resource IDs, source spans, and
hashes.

## Family-resolution finding

Seven identifiers have both a legacy and an FX definition:

- `BuildingContructDust`;
- `PCTMediumDust`;
- `RDTMediumExplosion`;
- `RDTMediumExplosionLight`;
- `SmokeBuildingLarge`;
- `SmokeBuildingMediumRubble`;
- `WaterRipplesSmall`.

The private BFME2 1.06 retail-binary oracle establishes the strongest honest
runtime contract now available:

- Object Draw `ParticleSysBone` and FXList `ParticleSystem` consumers call the
  same name lookup in global `0xDFDD04`, registered as
  `TheFXParticleSystemManager`;
- consumer references carry an unqualified system name, so there is one proven
  runtime namespace rather than per-family consumer namespaces;
- repeated declarations using `FXParticleSystem` syntax are proven
  last-definition-wins in retail;
- whether the legacy subsystem is active and which declaration wins a
  cross-family duplicate remain unresolved.

Both authored definitions and their family/source provenance are therefore
always retained. Six duplicate identifiers still carry this runtime resolution:

```json
{
  "status": "unresolved-cross-family-precedence",
  "selectedKind": null
}
```

`WaterRipplesSmall` is the sole explicit provisional selection:

```json
{
  "status": "provisional-explicit-runtime-selection",
  "selectedKind": "FXParticleSystem",
  "crossFamilyPrecedenceProven": false,
  "generalizesToOtherDuplicateIdentifiers": false,
  "visibleFieldsMateriallyEquivalent": true,
  "materialDiscriminator": "priority/culling"
}
```

That choice is bounded to the ripple: retail explicitly registers the FX
manager and the two ripple definitions have materially equivalent visible
fields, differing at priority/culling (`CRITICAL` versus
`VERY_LOW_OR_ABOVE`). It is not a retail precedence proof and must not be copied
to the other six duplicates. The plan summary therefore records exactly one
provisional runtime selection and six unresolved duplicate selections.

For an ID present in only one family (`BuildingDamaged`, `UntamedAllegiance`, and
`UntamedAllegiance2`), the sole exact authored family is recorded without making
a collision decision.

An authoritative close still requires a controlled retail `-mod` A/B oracle
that changes one family at a time with an unmistakable visible discriminator.

## Fail-closed checks

The planner rejects the input or private tree when any of these drift:

- census schema, version, or aggregate digest;
- effective-manifest schema, aggregate, identities, totals, or path case;
- census/manifest identity linkage;
- oracle source hashes/sizes, BFME2 executable version, claim grades, binary
  manager/lookup addresses, Water probe, or converter guidance;
- exact four target names and Fords placement counts;
- direct attachment order, bone, options, state family, or condition tokens;
- FX-root order, stage, state family, or condition tokens;
- definition family/source pairing;
- definition block line range, byte length, SHA-256, scalar-assignment count, or
  nested-block count;
- `ParticleName` order or exact compiled-texture stem resolution;
- FX-list block line range, length, hash, system edges, audio edge, or view shake;
- object-definition, particle-definition, FX-list, texture, or W3D source bytes;
- ripple W3D identity or anchor binding;
- generated resource IDs, outputs, or ImportProfile validity.

The private read boundary is included in the sealed report as paths, sizes, and
hashes only. No source bytes are included in the tracked planner or test fixture.

## Composer integration

Merge `profileFragment.resources` into the full profile and merge
`profileFragment.runtimeData` at `profileFragment.runtimeDataPath`.

There is one intentional source-pattern reuse rule: the seven legacy definition
resources all read `data/ini/particlesystem.ini`, and the ten FX definition
resources all read `data/ini/fxparticlesystem.ini`. This is required because the
strict converter selects one named block per resource. A composer may admit this
reuse only when all of the following are true:

- converter is exactly `sage-particle-definition`;
- source pattern is the one source required by the declared family;
- each `(kind, name)` pair is unique;
- every output JSON path is unique;
- the plan aggregate and source evidence remain valid.

Do not weaken the general duplicate-pattern check for other converters.

The binding document is an exact conversion handoff, not a claim that Godot
already renders the effects. The remaining runtime work is:

- implement the normalized particle-definition interpreter;
- run the family-isolated retail A/B experiment before selecting any of the six
  unresolved duplicate identifiers or treating the ripple choice as general;
- translate the four FX-list nugget parameters, not only their exact system/audio
  edges and hashed source spans;
- attach emitters to the named bones and lifecycle conditions;
- compare rendered timing, scale, blend, color, and lifetime against the retail
  game oracle.

No generic smoke, dust, or ripple fallback is acceptable in private parity mode.

## Focused verification

```powershell
$env:PYTHONPATH = "importer"
python -m pytest importer/tests/test_retail_particle_profile.py -q
python -m pytest importer/tests/test_sage_particles.py `
  importer/tests/test_sage_particle_pipeline.py -q
python -m ruff check importer/openbfme_importer/retail_particle_profile.py `
  importer/tests/test_retail_particle_profile.py
```

The real private plan can be produced without changing the shared composer:

```python
import json
from pathlib import Path

from openbfme_importer.retail_particle_profile import (
    build_retail_fords_particle_plan,
    generated_import_profile,
    write_generated_import_profile,
    write_retail_fords_particle_plan,
)

census = json.loads(
    Path(".private/scratch/fords-unresolved-census/census.json").read_text()
)
manifest = json.loads(
    Path(
        ".private/retail-work/cache/effective-assets/.openbfme/manifest.json"
    ).read_text()
)
oracle = json.loads(
    Path(".private/scratch/particle-family-oracle/evidence.json").read_text()
)
root = Path(".private/retail-work/cache/effective-assets")
plan = build_retail_fords_particle_plan(census, manifest, oracle, root)
profile = generated_import_profile(plan)
write_retail_fords_particle_plan(
    ".private/scratch/fords-particle-profile/plan.json", plan
)
write_generated_import_profile(
    ".private/scratch/fords-particle-profile/profile.json", profile
)
```
