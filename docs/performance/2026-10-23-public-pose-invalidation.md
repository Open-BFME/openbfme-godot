# W3D pose and MultiMesh invalidation

Public base: `b12bb2c85e488051e4fd7a84a24fce1e62493c75`
([upstream main](https://github.com/Open-BFME/openbfme-godot/commit/b12bb2c85e488051e4fd7a84a24fce1e62493c75)).
The original standalone patch changed only the instancer's buffer invalidation.
This branch now also merges the accepted palette-upload work from
[#92](https://github.com/Open-BFME/openbfme-godot/pull/92) at
`56db945054c72f6a3258ae5d747c2952d7af4214`.

The combined pose loop records changed palette rows before checking per-draw
visibility/fade; pose evaluation does not unconditionally dirty the model's draw
buffers. Instance creation initializes both the animated-palette flag and the
per-draw consumed-state cache. Starting animation on a static instance retains
#92's palette relocation and its required model-wide buffer rewrite.

Bone matrices are read through the palette. A stationary unsorted draw can retain
its instance buffer when its consumed pivot visibility/fade are unchanged. Sorted
draws still invalidate on pose, camera and node changes (and without a camera).
Instance properties, additions, removals and capacity changes retain model-wide
invalidation. A consumed-state cache is refreshed before membership filters,
including hidden subobjects, opacity zero, invisible pivots and fade zero.

A bone query may evaluate a culled pose outside the update's work list. Queries
alone must retain the baseline's submitted state. Deduplicated queried IDs are
therefore compared when the baseline would next consume that model: a property
rewrite, any actual pose evaluation in that model, or its sorted camera/node
trigger. Partial draw rewrites must consume other pending queries too. Removed IDs
are ignored and cleared on the ensuing full rewrite. No lifetime-instance scan is
added. The queued traversal costs O(Q*D + Q), with at most Q*U state comparisons
for Q queued IDs and U unaffected unsorted draws out of D draws. Pose comparisons
and per-instance/per-draw cache storage also add CPU/memory cost; this is not a
claim of zero overhead or universal FPS improvement.

## Reproduce the CPU/API regression

Use this combined public checkout, its pinned godot-cpp submodule,
the Godot Linux SDK `godot-2023.08.x-4` (GCC 13.2.0), and official Godot 4.7.2.
`SDK`, `GODOT`, `ROTWK_INSTALL` and `BFME2_INSTALL` below are builder-supplied paths.
The retail mounts must pass the repository's normal archive policy. Even the
synthetic tests currently need these authorized mounts for normal setup and
`housecolor.ini`; they are not an asset-free standalone test.

```sh
B=$(mktemp -d /tmp/openbfme-public-pose-XXXXXXXX)
export TMPDIR="$B/tmp" XDG_DATA_HOME="$B/data" XDG_CACHE_HOME="$B/cache"
export XDG_CONFIG_HOME="$B/config" XDG_STATE_HOME="$B/state" XDG_RUNTIME_DIR="$B/runtime"
mkdir -p "$TMPDIR" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
unset DISPLAY WAYLAND_DISPLAY
"$SDK/bin/cmake" -S engine -B "$B/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DOPENBFME_LAME=OFF -DGODOTCPP_API_VERSION=4.7 \
  -DOPENBFME_GODOT_BIN="$B/bin" \
  -DCMAKE_C_COMPILER="$SDK/bin/x86_64-godot-linux-gnu-gcc" \
  -DCMAKE_CXX_COMPILER="$SDK/bin/x86_64-godot-linux-gnu-g++" \
  -DCMAKE_MAKE_PROGRAM="$SDK/bin/ninja" \
  -DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER=
"$SDK/bin/cmake" --build "$B/build" -j2 --target openbfme
python3 -B godot/tests/pose_buffer_regression.py --build-dir "$B/build" \
  --baseline-ref 56db945054c72f6a3258ae5d747c2952d7af4214 \
  --scratch "$B/regression" --godot "$GODOT" --negative-controls
```

Do not run the release build script unchanged for this experiment: it writes to
`godot/bin`. All outputs above belong to the new temporary root. The runner never
invokes Ninja, uses at most two compiler processes, snapshots/hashes link inputs,
and recompiles the public baseline and candidate instancers into separate
libraries. Other objects are reused from the explicitly identified input build;
this diagnostic relink alone is not a fresh whole-extension build.

`PoseBufferFixtureSource.h` is injected only into scratch instancers before asset
manager construction. It reads runtime-generated, textureless synthetic W3D from
a directory and delegates other reads to the mounted archive source. There is no
production mount option or fixture environment API. No generated/retail bytes are
committed. Missing mount variables cause runner SKIP/77; invalid mounts, missing
models, parse errors, script errors and snapshot differences fail explicitly.

The tests compare independent baseline/candidate **actual MultiMesh resource**
bytes and visible counts against prepared buffers and order, then compare full
capacity bytes (including unused tails), order, palettes and cached reports
between variants. They cover both query/cache rejection sequences, queried-A /
pose-only-B consumption, partial draw changes, sorted/unsorted draws, half/zero
fade, hidden/opacity filters, query-only retention and deduplication, different
models, removals/fresh IDs/palette-block reuse, capacity/palette growth, camera,
node and no-camera triggers. The unchanged retail culling test runs on each
variant too. Two fault-injected adaptations independently disable consumed-cache
synchronization and queried-pose consumption; these must fail the corresponding
regressions. They are negative controls, not historical private binaries.

## Original standalone recorded validation

2026-10-23, Linux x86-64, official Godot `4.7.2.stable.official.ed1daf0bf`,
godot-cpp `507ed9d840c01a3c5b2a39af8bb4000bfac30bf5`, Godot API 4.7.
SDK GCC 13.2.0, CMake 3.27.1, Ninja `1.11.1.git.kitware.jobserver-1`.
The pre-existing SDK archive was rehashed against
`b89f173b1f2f2f35f3090bc4efad732f1c0c1c1206e5480cfcb141977010bc94`;
Compiler/linker, sysroot header/libc, Python, API and Godot runtime inputs were
separately hashed (binding generator Python 3.12; runner Python 3.14).

- Fresh configured production extension: PASS, 1,728 steps with `-j2`, all 1,719
  object files and five archives created in the new output tree. No old extension
  object, library, shared Ninja tree or private source dependency was reused.
- Differential: PASS, 53 main, six actual-frustum and 58 query-trigger snapshots
  exactly equal (117 total). Candidate: 2,336 synthetic checks; public baseline:
  2,180. Unchanged retail culling: 17 checks on each diagnostic library and 17
  checks on the actual uninstrumented production library, all PASS.
- Negative controls: consumed-cache synchronization fault produces four assertion
  failures; queried-pose consumption fault produces 20, including the ten direct
  failures from the five queried-A/pose-only-B variants. Both exit 1 as required.
- Measured `set_buffer` submissions: mixed bone-only update, five calls / 1,280
  bytes to one / 256; 100 stationary opaque instances, two / 12,800 to zero / zero;
  the same population with capacity 150, two / 19,200 to zero / zero.
- ELF/package audit: PASS, GLIBC requirement 2.18, sole defined dynamic export
  `openbfme_library_init`; no source/build/SDK path leaks. Native ELF64 x86-64,
  dynamically requiring only libc, libm, libpthread and the loader.
- All 1,725 production objects/archives/library hashes remain unchanged after
  diagnostic validation. Missing mount environment explicitly returns SKIP/77.

Production library SHA-256:
`1f1e0869e31400508b704182e23bff8a99eec3e59d453811b94edefb656e9c42`.
Actual production source engine ID:
`96888716bbfd2431d3a0bd504906aadddf3e0e5c0c2d841dd1c8cdf70798e103`.
This was built **before the source commit**: its honest build record names public
base `b12bb2c85e488051e4fd7a84a24fce1e62493c75`, version
`v0.4.12-nightly.20261022-10-gb12bb2c85e-dirty`, `dirty=1`, x86-32 OFF and empty
ID options. Its engine ID matches the original standalone production sources.
It is validation evidence, not a clean stamped release artifact or proof of the
eventual commit hash. Diagnostic relinks inherit that record; their baseline identity comes from
the recorded public source blobs and compile commands, not the embedded stamp.

Local evidence uses a newly created temporary root; `summary.json`, build logs,
`regression-final/result.json`, per-command logs, source/tool/link-input hash
manifests and before/after output manifests are retained there for independent
review. Raw local paths and generated binaries are deliberately not in this
public patch. An initial test-only no-camera comparison-count expectation was
corrected from zero to one; all 117 resource/palette snapshots already matched.
Production sources did not change during validation.
Headless dummy rendering provides CPU/API evidence only. It does not establish
GPU image equivalence, driver behavior, frame timing, PCIe bandwidth, Windows or
other-platform correctness. Submitted buffer byte counts describe API volume.

## Combined validation against #92

The combined production extension passed a fresh 1,728-step SDK build with
`-j1`. All 1,725 object/archive/library hashes stayed unchanged through the
diagnostic checks. The three committed suites matched #92 exactly across all
117 snapshots; another 701 adversarial query/lifecycle snapshots and ten
dirty-row/growth snapshots also matched. The merged variant passed 11,750
synthetic checks, with 17 retail-culling checks on each diagnostic variant and
on the uninstrumented production extension. Both fault controls still fail in
the intended cases (four consumed-cache and 20 query-consumption assertions).

The original static-to-animation assertion failed on both variants before it
was updated: #92 relocates this three-pivot block by nine texels, requiring both
draw records to be rewritten. All 53 original main-suite snapshots already
matched. Dirty-span observers additionally verified bone-only updates, the
already-dirty-model path, separated rows, relocation and atlas growth. A separate
22-check ASan/UBSan harness exercised the unchanged queue source with a delayed
render-thread API fake; this does not validate the real RenderingDevice.

These are headless CPU/API checks. The original standalone artifact hashes above
identify the earlier build, not this combined build. The combined build's source
engine ID is `4ece456b3b42283835026b7375fef40f0d749067e1462516a9446679c5f93db3`;
its pre-commit stamp honestly reports `bbaa8a438a`, `dirty=1`. Existing numeric,
provenance and pose-culling stops remain; no GPU or timing acceptance is claimed.
