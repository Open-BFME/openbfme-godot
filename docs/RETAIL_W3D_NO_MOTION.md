# Source-proven header-only W3D animations

`openbfme_importer.w3d_no_motion` provides a deliberately narrow transformer
for retail W3Ds that contain an animation header but no motion data. It is not
an animation approximation and it is not currently integrated into the retail
pipeline.

## Accepted contract

`strip_proven_header_only_animations` accepts caller-owned W3D bytes, a safe
virtual `.w3d` path, and one exact `W3DNoMotionExpectation` for every animation
container in the file:

```python
from openbfme_importer.w3d_no_motion import (
    W3DNoMotionExpectation,
    strip_proven_header_only_animations,
)

result = strip_proven_header_only_animations(
    source,
    virtual_path="art/w3d/example.w3d",
    expectations=(
        W3DNoMotionExpectation(
            identifier="EXAMPLE",
            hierarchy_identifier="EXAMPLE",
            frame_count=101,
            frame_rate=30,
            compressed=False,
            model_identifier="EXAMPLE",
        ),
    ),
)
transformed = result.output_bytes()
proof = result.proof.neutral()
```

For a compressed animation, set `compressed=True` and provide its exact
16-bit `flavor` value. Frame count and frame rate must be finite positive
integers and must match the source header exactly.

The transformer succeeds only when all of the following are true:

- every raw (`0x00000200`) or compressed (`0x00000280`) animation container is
  top-level, selected exactly once, and carries the W3D container flag;
- each selected container contains exactly one 44-byte header of the matching
  kind and no other child;
- no raw channel, bit channel, compressed channel, compressed bit channel,
  compressed motion channel, morph animation, nested animation, or orphaned
  animation-related chunk exists anywhere else in the file;
- identifier, hierarchy identifier, frame count, frame rate, compression mode,
  and flavor match the explicit expectation with exact spelling;
- embedded hierarchy and HLOD model headers exactly match the expected
  hierarchy/model bindings, without duplicate or extra headers; and
- rescanning the output proves zero remaining animation headers and unchanged
  model, hierarchy, pivot, model-reference, and mesh identity records.

An unsupported or ambiguous source raises `W3DNoMotionError` before transformed
bytes are returned. The implementation has fixed source-size, chunk-count, and
nesting-depth limits.

## Mutation and proof boundary

The mutation is only a concatenation of the original top-level byte ranges
after omitting the proven containers. No retained chunk is decoded, rewritten,
or size-repaired. The canonical payload-free proof records input/output hashes
and lengths, removed container hashes/counts/bytes, exact header metadata,
identity hashes/counts, and its own `proofSha256`. It contains no W3D payload or
filesystem source path.

## PMMEATRACK01 retail backtest

The private proof at `.private/scratch/w3d-no-motion/REPORT.json` seals the
effective BFME2 source `art/w3d/pm/pmmeatrack01.w3d`:

- input: 32,466 bytes,
  SHA-256 `7f07ac4f7a8eb3812e06353a3ea54b9bebc0d5ccfef22893bf643e44b66cb0bf`;
- removed: one 60-byte raw animation container containing only the
  `PMMEATRACK01` header, bound exactly to hierarchy/model `PMMEATRACK01`, at
  101 frames and 30 fps;
- output: 32,406 bytes,
  SHA-256 `5fc73634b0c7506b8a662089eb2371bd3b2b906959a418493d6a169971923211`;
- output rescan: zero animation headers, zero warnings, and unchanged model,
  hierarchy, pivot, model-reference, and mesh identities; and
- proof digest:
  `9931d8209cf6c22bec97bd6dac340cee295f9198b51d829486521e4173e58fe8`.

The private transformed W3D remains under
`.private/scratch/w3d-no-motion/pmmeatrack01.no-motion.w3d`; neither it nor its
source is tracked.

## Verification

Run the focused legal-safe suite and style checks:

```powershell
$env:PYTHONPATH = "importer"
.private\retail-work\tools\python-3.12-env\Scripts\python.exe `
  -m unittest importer.tests.test_w3d_no_motion -v
ruff check importer\openbfme_importer\w3d_no_motion.py `
  importer\tests\test_w3d_no_motion.py
ruff format --check importer\openbfme_importer\w3d_no_motion.py `
  importer\tests\test_w3d_no_motion.py
```
