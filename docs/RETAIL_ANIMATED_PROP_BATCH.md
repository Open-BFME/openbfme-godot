# Exact animated Fords prop batch

`retail_animated_prop_profile.py` is the fail-closed planning pass after the
static and zero-clip hierarchical prop planners. It does not convert or
publish retail content. It produces a private report and a standalone private
ImportProfile whose resources can later be composed into the main slice.

## Scope and result

The current Fords evidence has 67 visual-closure target types. The static pass
covers 38 types, the hierarchical pass covers six, and the base map profile
classifies seven more as logical ambient-audio emitters. The animated pass
therefore evaluates 16 remaining physical target types and 48 placements.

The exact animated pass currently promotes:

| Target | Placements | Model | Physical clips | No-clip transitions | Secondary-skin proof |
| --- | ---: | --- | ---: | ---: | --- |
| `Bear` | 1 | `CUBear_SKN` | 5 | 2 | yes |
| `CaptureFlag` | 2 | `CAPFLAG_SKN` | 4 | 0 | no |
| `Duck` | 2 | `CUDuck_SKN` | 5 | 2 | yes |
| `Egret` | 2 | `CUEgret_SKN` | 2 | 0 | no |
| `ElkFemale` | 1 | `CUELKF_SKN` | 7 | 0 | yes |
| `ElkMale` | 1 | `CUELK_SKN` | 7 | 0 | yes |
| `Fish` | 13 | `CUTuna_SKN` | 2 | 0 | no |
| `Rabbit` | 1 | `CURabbit1_SKN` | 4 | 2 | yes |
| `Raccoon` | 2 | `CURaccoon_SKN` | 4 | 2 | yes |
| `Wolf` | 1 | `CUWolf_SKN` | 4 | 2 | no |

This adds ten exact type bindings and 26 placements. The fragment contains ten
`w3d-bundle` model resources, 11 deduplicated texture-input resources, and one
shared `data`/`hash-only` W3D input resource. It is validated by the real
`ImportProfile` parser. The current plan aggregate is
`03d06180d7a9fd9cd64430d4644df160327d41a63c542d6c894ab9d7ed859194`.

`ElkFemale` and `ElkMale` use the same `NUHORSE_SKL` plus seven exact horse
actions. Those eight W3Ds have one source owner:
`animated-prop-shared-w3d-nuhorse-skl-4678722e3886` (`kind: data`,
`converter: hash-only`). Each elk model resource owns only its skin W3D and
declares the shared resource through `inputResourceIds`. The generated fragment
rejects any source pattern owned by more than one resource.

The ten transition names on Bear, Duck, Rabbit, Raccoon, and Wolf are not
filename guesses. The planner accepts only the exact authored TransitionState
rows and proves the ten physical header names absent from every one of the
12,751 manifest-attested retail W3Ds (1,049,935,816 bytes). That absence proof
is sealed into both the plan and the model-resource options as source-native
`immediate-no-clip` behavior.

Six target types and 22 placements remain deliberately unplanned:

- `WargLair`, `CaveTrollLair`, and `Inn`: multiple construction, intact,
  damage, collapse, rubble, and floor/bib models require a lifecycle-aware
  multi-model runtime binding. A single GLB binding would not be 1:1.
- `OrcMeatRack01`: its model contains one embedded 101-frame, 30-FPS animation
  header but no animation-channel chunks. The embedded-action adapter requires
  at least one exported channel, sampler, curve, and key, so this header-only
  no-motion action cannot yet pass the converter contract. It remains rejected
  until that exact case has a source-proven adapter policy.
- `WtrRiplsSmall` and `WtrflHaze`: these are particle/no-render water objects,
  not authored animated-model bundles. They belong to the particle/runtime
  path rather than a forced GLB binding.

## Conversion contract

An animated target is eligible only when all of these are true:

1. It is not already covered by the exact static or hierarchical plan and is
   not an exact base-profile logical binding.
2. It has one physical model W3D and either a complete set of split physical
   action W3Ds or one converter-compatible embedded action.
3. Fresh bytes below the private effective-assets root match the sealed
   manifest size and SHA-256.
4. The model has mesh/model headers and only supported `lod` references. A
   scanner warning is fatal except for the exact paired secondary skin streams,
   which require a separate semantic-equivalence proof before promotion.
5. The model header declares exactly one hierarchy ID. A split bundle uses the
   exact matching W3D in the model directory or the only manifest W3D with that
   basename. This is an authored header relationship, not a filename-near guess.
   An embedded bundle instead requires exactly one matching hierarchy header
   and nonempty pivots in the model itself.
6. Each split animation resolves to one animation-only W3D with one header, at
   least one key-channel chunk, positive timing, and the same hierarchy ID as
   the model. An embedded action must likewise have one matching header,
   positive timing, and a physical key channel.
7. An unresolved transition is accepted only if its target, logical name,
   condition, provenance, and source semantics match the closed no-clip policy,
   the complete expected transition set is present, and every corresponding
   physical header name is absent from the complete manifest W3D corpus.
8. Every explicit and model-embedded texture resolves to one exact image and
   its effective-tree bytes match the manifest.

The generated model resource selects model, skeleton, and animation W3Ds in a
single exact pattern set. Its `options.model` and `options.animations` use the
actual source basenames expected by the existing Blender adapter. Exact
source-native no-clip transitions are carried as inert, sealed planning
metadata; they are not fabricated animation files. A reused exact skeleton and
action closure is instead owned once by a shared `data`/`hash-only` input
resource and staged through `inputResourceIds`. Texture resources use the same
dependency mechanism so the original DDS/TGA bytes are staged beside the W3Ds;
the resulting GLB remains the runtime render asset.

## Rebuild the private plan

Run from the repository root with the pinned private Python environment and
`PYTHONPATH=importer`:

```powershell
$env:PYTHONPATH = "importer"
@'
from openbfme_importer.retail_animated_prop_profile import (
    build_retail_animated_prop_plan,
    generated_import_profile,
    load_retail_animated_prop_plan_inputs,
    write_generated_import_profile,
    write_retail_animated_prop_plan,
)

inputs = load_retail_animated_prop_plan_inputs(
    ".private/retail-work/reports/retail-visual-closure-0d51ad8d31ca6e6c.json",
    ".private/retail-work/reports/retail-static-prop-plan-0d51ad8d31ca6e6c.json",
    ".private/retail-work/reports/retail-hierarchical-prop-plan-0d51ad8d31ca6e6c.json",
    ".private/retail-work/cache/effective-assets/.openbfme/manifest.json",
    ".private/retail-work/packs/bfme2-men-vslice/maps/fords-of-isen-ii/objects.json",
    "importer/profiles/men-fords-v0.json",
)
plan = build_retail_animated_prop_plan(
    *inputs,
    ".private/retail-work/cache/effective-assets",
)
write_retail_animated_prop_plan(
    ".private/retail-work/reports/retail-animated-prop-plan-0d51ad8d31ca6e6c.json",
    plan,
)
write_generated_import_profile(
    ".private/retail-work/profiles/men-fords-v0-animated-props.generated.json",
    generated_import_profile(plan),
)
'@ | & .private/retail-work/tools/python-3.12-env/Scripts/python.exe -
```

Run the focused legal-safe fixture tests with:

```powershell
$env:PYTHONPATH = "importer"
& .private/retail-work/tools/python-3.12-env/Scripts/python.exe `
  -m unittest tests.test_retail_animated_prop_profile -v
ruff check importer/openbfme_importer/retail_animated_prop_profile.py `
  importer/tests/test_retail_animated_prop_profile.py
```

The standalone generated profile is a planning/build input only. A successful
plan is not a successful GLB conversion, runtime animation binding, or rendered
parity proof. Those remain separate fail-closed gates.
