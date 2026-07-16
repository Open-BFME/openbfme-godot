# Retail zero-clip hierarchical prop batch

`retail_hierarchical_profile.py` is the bounded follow-on to the static-prop
planner. The static planner correctly rejects every W3D with a hierarchy; this
planner admits only the narrower class that the importer can convert with
`w3d-hierarchical` without inventing animation state.

It produces a payload-free plan only. It does not copy retail files, invoke
Blender, build a pack, publish a pack, or edit map bindings.

## Selection contract

A target is eligible only when all of the following are proven by the sealed
visual closure, static plan, effective-assets manifest, and cooked map object
document:

- it was not already accepted by the static-prop plan;
- its Object definition and inheritance closure are complete and diagnostic
  free;
- all visual references for that target resolve exactly;
- it has one unique physical model W3D, not a filename neighbour or
  substitute;
- it has no authored animation, particle, or attached-model dependency;
- the model W3D was scanned, has model and hierarchy headers, has no animation
  headers, and produced no scanner warning;
- every embedded and SAGE-authored texture resolves to one exact physical
  image;
- there is at least one supported `lod` model reference, and every such
  reference names an exact render subobject present in the W3D model headers;
- any authored hierarchy W3D is the model W3D itself; external hierarchy
  bundles remain excluded;
- model and texture byte lengths and SHA-256 values agree with the effective
  manifest.

The input static plan is recomputed from the closure and manifest and must be
byte-for-byte equivalent as canonical JSON. Re-sealing a modified intermediate
plan does not make it acceptable. Closure and nested W3D-dependency digests,
manifest aggregate identity, unsafe paths, target/path case collisions, map
target case drift, and map object ordering are validated before selection.

Every rejected target remains in `rejectedTargets` with deterministic reason
records. The profile fragment is run through the real `ImportProfile` parser;
each model resource explicitly declares empty animation and equipment arrays
and exact local texture-resource dependencies.

### Proven root-rigid exception

The scanner can prove a narrower source shape in which every supported render
reference is an `lod` reference at `boneIndex = 0`. OpenSAGE intentionally
omits that source root pivot, so these files import as rigid meshes parented to
one empty armature carrier. Only those exact groups receive
`options.provenRootRigidBake = true`; model names never participate in the
decision.

The Blender adapter accepts the corresponding CLI flag only for a hierarchical
conversion. It then requires zero animation actions, one empty unparented
armature carrier, at least one retained render mesh, every retained mesh
rigidly parented to that carrier, and no vertex groups, modifiers, bone parent,
or other deform ambiguity. It captures every mesh world matrix, unparents the
meshes, restores and verifies those matrices, removes the carrier, and verifies
the matrices again. Any mismatch is a conversion failure.

The adapter reports the requested/applied state, removed-carrier count,
baked-mesh count, transform proof, and deformation proof. The pipeline accepts
zero exported bones and skeletons only when the profile requested the bake and
that complete report agrees. A missing/false option, an unexpected applied
flag, or an incomplete proof still fails closed. Normal multi-pivot
hierarchies continue to require exactly one nonempty exported skeleton.

## Python use

There is deliberately no new CLI command for this batch. The existing importer
surface does not need to grow before the conversion contract has proved useful.

```python
from openbfme_importer.retail_hierarchical_profile import (
    build_retail_hierarchical_prop_plan,
    load_retail_hierarchical_prop_plan_inputs,
    write_retail_hierarchical_prop_plan,
)

inputs = load_retail_hierarchical_prop_plan_inputs(
    ".private/retail-work/reports/retail-visual-closure-0d51ad8d31ca6e6c.json",
    ".private/retail-work/reports/retail-static-prop-plan-0d51ad8d31ca6e6c.json",
    ".private/retail-work/cache/effective-assets/.openbfme/manifest.json",
    ".private/retail-work/packs/bfme2-men-vslice-roads-private/maps/"
    "fords-of-isen-ii/objects.json",
)
plan = build_retail_hierarchical_prop_plan(*inputs)
write_retail_hierarchical_prop_plan(
    ".private/retail-work/reports/"
    "retail-hierarchical-prop-plan-0d51ad8d31ca6e6c.json",
    plan,
)
```

Run the focused contract tests with:

```powershell
$env:PYTHONPATH = "importer"
& ".private\retail-work\tools\python-3.12-env\Scripts\python.exe" `
  -m unittest importer.tests.test_retail_hierarchical_profile -v
```

## Fords of Isen II correction, 2026-07-13

The earlier eight-group private report predates the render-subobject and
root-rigid checks and is superseded. Regenerate it from the current sealed
visual closure before composing another pack; its old digest and counts are
not current evidence.

The corrected classification is evidence-driven:

- `PMClotheLine01`, `RockGrey22`, `RockGrey23`, and `RockGrey24` each have only
  supported `lod` render references at `boneIndex = 0`, so their generated
  resources explicitly request the proven root-rigid bake;
- `FarmTemplate` and `FireCampfireNight` have render references on non-root
  pivots and remain normal nonempty hierarchical conversions;
- `WtrflHaze` and `WtrRiplsSmall` have hierarchy/model headers but no model
  references or render subobjects, so the planner now rejects them with
  `model-w3d-has-no-supported-render-subobject` instead of fabricating or
  requesting a mesh conversion.

Focused private scratch conversions of the four proven root-rigid files
produced one retained mesh each, zero exported bones, zero exported skeletons,
and complete adapter bake proofs. These scratch results validate the adapter;
they are not published content-pack evidence.

`OrcMeatRack01` is not in this batch. Its exact model has hierarchy headers but
also two animation headers, so treating it as a zero-clip hierarchy would
discard authored behavior. It remains rejected with
`model-w3d-contains-animation-headers` until the animated conversion path owns
it.

## Godot integration notes

When this plan is composed into a private profile, preserve its generated
resource IDs, `inputResourceIds`, outputs, and exact `objectBindings.models`
rows. Normal model conversion must report hierarchical asset kind, one
nonempty skeleton, and zero clips. A proven root-rigid resource instead must
report the exact requested bake proof and zero exported skeletons. A converter
failure, missing texture, unexpected clip, or contract mismatch is a hard
incomplete-pack result, not permission to retry as `w3d-static`.

After conversion, instantiate the generated bindings at the exact cooked positions
and yaw values. Keep the W3D hierarchy even when the object has no gameplay
animation: it can control authored pivots, HLOD subobjects, visibility pieces,
and material placement. The only flattening exception is the explicitly
proven pivot-zero carrier described above. In particular, do not flatten the
farm or campfire hierarchy merely because its animation header list is empty.

This report is conversion readiness evidence, not a claim that the assets are
already bound or rendered. Before calling the batch integrated, build and
audit the composed private pack, run the focused prop/runtime gates, inspect
rendered Fords captures, and then run `run_retail_pipeline_tests.bat`.
