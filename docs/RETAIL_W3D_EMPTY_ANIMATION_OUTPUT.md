# Retail W3D empty animation output

## Scope

This repair is limited to the Blender-side W3D adapter boundary. It does not
turn a missing animation into a static asset and does not synthesize animation
channels.

## Exact retail evidence

The generated completion profile resource
`men-fortress-door-closed-source` requests one self-contained W3D bundle:

- model: `gbfdoor_drc.w3d`
- animation: `gbfdoor_drc.w3d`
- output: `assets/models/structures/men-fortress/door-closed.glb`

The generated plan resolves it to `art/w3d/gb/gbfdoor_drc.w3d` in `w3d.big`,
offset `369627071`, size `6300`. The prepared source SHA-256 is
`f82cc85ba9d7e19d2f676bc230249942ac948b37c656ab8b13979723060177f8`.

Retail lifecycle evidence identifies `GBFDoor_DRC` as the default, damaged,
and really-damaged closed-door model. The separate `GBFDoor_DRCA` source owns
the `DOOR_1_CLOSING` animation (`GBFDoor_DRCA.GBFDoor_DRCA`). Therefore a
fabricated transform animation on `GBFDoor_DRC` would contradict the source.

The pinned importer seals `GBFDoor_DRC` as:

- one logical clip named `gbfdoor_drc`
- two owned actions: one object and one armature action
- shape `visibility-only`
- zero transform, material, and unsupported curves
- three visibility curves with three total keys
- zero NLA transform tracks

Blender consequently exports the skeletal door geometry without a glTF
`animations` array. That is truthful output, not evidence of missing transform
motion.

## Fail-closed rule

An absent glTF `animations` array is accepted only when every captured source
action shape is sealed as `visibility-only`, has zero transform/material/
unsupported curves, and has a non-empty exact visibility-channel payload whose
channel count matches the sealed shape. Those exact keys are retained in the
existing `openbfme.w3d-visibility-only-animations` root extras contract.

If any sealed shape requires a transform animation, absence of the glTF array
still raises `animation GLB has no required transform animations`. An empty or
inconsistent visibility proof also fails. No animation object, channel,
sampler, key, or motion is fabricated.

## Direct reproduction

The focused scratch reproduction uses copies of the prepared job input and the
pinned OpenSAGE plugin under `.private/scratch/w3d-door-closed-repair`; it does
not mutate or smoke-test the pinned plugin checkout.

```powershell
& .private/retail-work/tools/blender-4.2.0-windows-x64/blender.exe `
  --factory-startup -noaudio --background --python-use-system-env `
  --python-exit-code 1 --python importer/blender/w3d_to_glb.py -- `
  --plugin-root .private/scratch/w3d-door-closed-repair/plugin/OpenSAGE.BlenderPlugin `
  --model .private/scratch/w3d-door-closed-repair/input/gbfdoor_drc.w3d `
  --asset-kind animated `
  --output .private/scratch/w3d-door-closed-repair/repaired.glb `
  --animations .private/scratch/w3d-door-closed-repair/input/gbfdoor_drc.w3d `
  --required-equipment --excluded-optional-meshes
```

The repaired GLB is `831700` bytes with SHA-256
`08728c9240cba89c8b70f4206e37a10e51b9755fbefb95f97d16e2566c3de2ea`.
Three isolated conversions produced this same byte-for-byte digest.
It has two meshes, one skin, five nodes, no `animations` member, and one exact
visibility-only sidecar containing three channels and three keys. The adapter
report records zero exported animations/channels/samplers, two skeletal mesh
nodes, 32 triangles, and 56 vertices.

## Release locks

- Blender executable SHA-256:
  `80fb653019a0afb3bda0947ec74e84dc0a94d0d388f9b3849433c0e1a4efdabe`
- Blender tree SHA-256 required by bootstrap:
  `81e0cfb0d56ff5e33c2c562b13cc88257b9b34e072efa7ae054a6c87f13f2aa4`
- OpenSAGE plugin commit:
  `2de84023cb632a79a853b2a52f97c8002ed85142`
- Generated completion profile SHA-256:
  `cc5af254e0787cf135bd1cf8574b94dd19991741a6eda6ccc346aa304b78c588`
- Repaired adapter SHA-256:
  `3fba8cc0a3cfdb785b126a3e3bbe2de624dbc260661a504fd1c663fb2bcd2e28`
- Coordinator validator SHA-256:
  `7a2967bb9b325a7f1c2f8f49391565b4a36878392e3f19f7c6aeac2c08e3cbc9`
- Focused coordinator test SHA-256:
  `1f5b8b231a035d648f98a1625f3ea2495e9b6fad1a2bd9706d7af8891f5fb85f`

Focused verification:

```text
python -m pytest importer/tests/test_w3d_action_shapes.py importer/tests/test_w3d_pipeline.py -q
29 passed, 48 subtests passed

python -m ruff check importer/openbfme_importer/pipeline.py importer/tests/test_w3d_pipeline.py
All checks passed!

python -m py_compile importer/openbfme_importer/pipeline.py importer/tests/test_w3d_pipeline.py
PASS
```

The coordinator now accepts embedded/split core counts of zero animations, zero
channels, and zero samplers only when the sealed action shapes require zero
transform animations. Skin and skeletal-mesh proof remain mandatory. If any
transform animation is expected, its exact animation count and non-empty
channel/sampler proof remain mandatory.

The mixed CaptureFlag case is independently locked: `capflag_dn`,
`capflag_sup`, and `capflag_up` require three exported transform clips, while
`capflag_sdn` is the one visibility-only sidecar. The coordinator continues to
require `3 + 1 == 4` requested logical animations, all 36 visibility channels
and keys, 81 emitted core channels/samplers, and the skeletal export proof.
