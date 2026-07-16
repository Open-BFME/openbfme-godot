# Retail static-prop batch planning

`retail_visual_profile.py` turns a validated retail visual-closure report into
a deterministic, payload-free static-prop conversion plan. It does not scan
for filenames, convert assets, edit a content profile, build a pack, or publish
anything. The planner is intentionally usable when the full closure is not
ready: each requested Object type is evaluated independently, and every
ineligible type remains in the report with explicit reasons.

## Required inputs

The planner consumes two already-authored sources of truth:

- an `openbfme.retail-visual-closure` schema-version 1 report;
- the `openbfme.effective-assets-manifest` beside the extracted effective tree.

It verifies both the top-level visual-closure digest and the nested W3D
dependency digest. It also verifies the effective-manifest aggregate, totals,
case-unique paths, and every selected source record. A scanned W3D's size and
SHA-256 must agree with the effective manifest. Absolute, traversing,
noncanonical, duplicate, and case-colliding paths or target IDs are rejected.

## Static eligibility contract

A target Object is eligible only when all of these are proven by the closure:

1. Its definition exists and inheritance is complete.
2. Its resolved model leaves identify exactly one distinct physical W3D. The
   same W3D may appear in several authored states, and a semantic `Model = None`
   state is retained without becoming a physical model.
3. The target requires no hierarchy, animation, particle, or attached-model
   leaf. Missing or ambiguous leaves are never promoted from their candidates.
4. The selected W3D was scanned, has at least one model header, and has no
   scanner warning, animation header, hierarchy header, or unsupported embedded
   model-reference role. Internal HLOD `lod` references are supported.
5. Every texture named in the Object and every texture embedded in that exact
   W3D resolves to one physical DDS, TGA, JPG, or PNG. The planner accepts only
   the exact resolution evidence already recorded by the closure; it never
   searches for or infers a substitute.
6. The effective-assets manifest supplies an exact source archive, offset,
   precedence, byte length, and SHA-256 for the W3D and every texture.

This is deliberately narrower than a general W3D batch. Animated critters,
hierarchical props, buildings with lifecycle models, particles, and files with
unsupported scanner chunks stay in `ineligibleTargets` for their proper
converter path.

## Output contract

The `openbfme.retail-static-prop-plan` document contains:

- eligible and ineligible target Object types;
- one conversion group per distinct physical W3D, including all Object types
  that share it and exact source/hash records;
- one globally deduplicated texture resource per physical texture;
- one `w3d-static` resource per conversion group with exact
  `inputResourceIds`;
- deterministic `objectBindings.models` rows containing `typeName`, the exact
  `sourceVirtualModel`, the stable GLB output, and
  `matchMethod = exact-type-name`;
- placement-independent type counts and a canonical aggregate SHA-256.

The `profileFragment.resources` array satisfies the normal import-profile
resource contract, but the document is still only a fragment. Integration must
merge the resources and model-binding rows into a reviewed private map profile,
then run the ordinary pack build, audit, and runtime gates. No placement count
is consumed or inferred by this stage.

## Generate the private plan

The module currently remains separate from the public importer CLI so this
planning stage cannot accidentally publish a pack. A private one-off invocation
is:

```powershell
$env:PYTHONPATH = "importer"
@'
from pathlib import Path
from openbfme_importer.retail_visual_profile import (
    build_retail_static_prop_plan,
    load_retail_static_prop_plan_inputs,
    write_retail_static_prop_plan,
)

report, manifest = load_retail_static_prop_plan_inputs(
    Path(".private/retail-work/reports/<visual-closure>.json"),
    Path(".private/retail-work/cache/effective-assets/.openbfme/manifest.json"),
)
plan = build_retail_static_prop_plan(report, manifest)
write_retail_static_prop_plan(
    Path(".private/retail-work/reports/<static-prop-plan>.json"), plan
)
'@ | & ".private/retail-work/tools/python-3.12-env/Scripts/python.exe" -
```

Run the focused contract tests with:

```powershell
$env:PYTHONPATH = "importer"
& ".private/retail-work/tools/python-3.12-env/Scripts/python.exe" `
  -m unittest importer.tests.test_retail_visual_profile -v
```

## Current Men/Fords non-road result

On 2026-07-13, the corrected 67-type non-road Fords closure
`retail-visual-closure-0d51ad8d31ca6e6c.json` produced the private plan:

`.private/retail-work/reports/retail-static-prop-plan-0d51ad8d31ca6e6c.json`

The result is placement-independent:

- 67 target Object types considered;
- 38 eligible target types;
- 29 ineligible target types retained with reasons;
- 34 unique W3D conversion groups (the Banyan pair and four Evergreen types
  share exact W3Ds);
- 17 unique texture sources;
- 51 profile resources and 38 exact model-binding rows;
- plan aggregate SHA-256
  `60e08fc7e93b4f7151e4bcb67cc503a7766921622d1bbeca68f79db437673295`.

The plan was not built or published. Its ineligible set correctly retains
ambient logical objects without models, animated animals, CaptureFlag,
multi-model lairs and Inn, hierarchy-bearing props, W3Ds with scanner warnings,
and the ten unresolved animal animation references from the source closure.
