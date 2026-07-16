# Retail no-motion prop profile

`retail_no_motion_prop_profile.py` closes one deliberately narrow Fords of
Isen II gap: `OrcMeatRack01` is a renderable hierarchical W3D whose embedded
`PMMEATRACK01` animation container has a 101-frame, 30 Hz header but no key,
bit, compressed, motion, morph, or other animation channel. The object INI
does not author an `Animation` state. Treating the header as a looping action
would invent behavior; rejecting the whole model would discard a valid retail
mesh.

The planner therefore emits a normal `w3d-hierarchical` model with:

- `animations: []`
- `provenRootRigidBake: true`
- one exact `provenNoMotionAnimations` declaration for raw
  `PMMEATRACK01`, hierarchy `PMMEATRACK01`, model `PMMEATRACK01`, 101 frames at
  30 Hz
- the exact `PMMeatrack.tga` retail dependency resolved to
  `art/compiledtextures/pm/pmmeatrack.dds`
- one exact `OrcMeatRack01` map binding

This is not a general option for suppressing inconvenient animation failures.
The W3D preprocessor independently proves that the selected animation is a
top-level, header-only container and preserves every retained top-level byte.
Any child chunk, extra animation container, changed identifier, changed
timing, changed hierarchy/model binding, scanner warning, or source hash
mismatch aborts before output.

## Evidence gate

The planner requires all of the following inputs together:

1. the sealed retail visual closure;
2. the sealed static, hierarchical, and animated prop plans;
3. the effective-assets manifest and the private effective-assets directory;
4. the cooked Fords map object document;
5. the independent unresolved-object census.

It verifies the digest chain between the three upstream plans and requires the
same target history at each handoff: static rejects the hierarchy/animation,
hierarchical rejects the animation, and animated rejects it specifically
because the embedded animation has no key channel. It also requires that no
upstream resource or model binding already owns the target.

Fresh source bytes are then checked against the manifest. Fresh W3D metadata
must agree exactly with the census for mesh, model, hierarchy, pivot,
animation, model-reference, file-header, chunk-kind, and warning rows. The
single map placement must still be record 974 with unique ID
`OrcMeatRack01 551`. The exact embedded texture is read and hash-checked too.

The resulting standalone fragment has two resources and two source patterns:

- one texture conversion resource;
- one hierarchical model conversion resource;
- one model binding covering one placement;
- zero emitted animation clips.

Both the plan and generated profile are payload-free JSON. Retail and converted
payloads remain under `.private`.

## Focused verification

```powershell
python -m pytest -q importer/tests/test_retail_no_motion_prop_profile.py importer/tests/test_w3d_no_motion.py importer/tests/test_w3d_no_motion_pipeline.py
python -m ruff check importer/openbfme_importer/retail_no_motion_prop_profile.py importer/tests/test_retail_no_motion_prop_profile.py
python -m ruff format --check importer/openbfme_importer/retail_no_motion_prop_profile.py importer/tests/test_retail_no_motion_prop_profile.py
```

The integration owner should merge `profileFragment.resources` and
`profileFragment.objectBindings.models` only after checking for exact source,
resource-ID, output, and binding collisions. The shared composer remains the
authority for replacing the prior unresolved binding; this planner never
mutates a base profile or publishes a pack itself.
