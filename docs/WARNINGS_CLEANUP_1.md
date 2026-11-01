# Compiler warning audit

Base: `c4ee740e5b8cf1f060aec0fad9ad4c9c5bb1ae64`.
All three builds are Release, include the Godot extension, and use
`-Wall -Wextra -Wshadow -Wconversion` for C and C++.
`-Wshadow` and the conversion-warning family are report-only.
No default warning flags or error thresholds were changed.

## Counts

These are emitted diagnostics across clean full builds (including repeated
header diagnostics, generated bindings, and third-party code). Every diagnostic
line was parsed; no warning lines were discarded.

| Compiler | Wall/extra/default before | After | Report-only before/after | All before | All after | Unique before | Unique after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Clang 14.0.0 | 8,327 | 8,323 | 116,734 | 125,061 | 125,057 | 4,442 | 4,438 |
| GCC 11.4.0 | 8,216 | 8,202 | 48,295 | 56,511 | 56,497 | 3,432 | 3,418 |
| llvm-mingw 20261006 / Clang 23.1.3 | 8,227 | 8,223 | 118,445 | 126,672 | 126,668 | 4,331 | 4,327 |

The complete deduplicated CSV inventory is intentionally not included in this
repository; the counts above summarize the audit without that detailed inventory.
The unique-diagnostic totals above additionally distinguish line, column, and
diagnostic message. Build and system paths are normalized.

## Changes and triage

- `meshmdlio.cpp`: do not read integer/float parser temporaries after a failed
  `take32`. Malformed shader-material properties still return the same error.
  A generated-at-runtime test covers every truncated length of integer, float,
  and two/three/four-component vector properties.
- `GodotAptPlayer.cpp`: preserve the existing bulk copies and single-precision
  size guards; additionally require trivially copyable types. Cast only the
  three `memcpy` destinations to `void *` to suppress GCC's class-copy warning
  without altering the fast path or changing global compiler flags.
- Tests: compare spell-store state text rather than string-literal addresses;
  parenthesize a conditional assertion so doctest decomposes the intended
  expression; size/format a bone suffix without narrowing or truncation;
  check child-process log redirection; initialize tooltip coordinates; name
  the Purchased state in inventory diagnostics.
- The remaining non-simulation warnings were reviewed. Unused entities,
  aggregate omissions with default initialization, compiler-specific pragmas,
  and safe existing enum/arithmetic expressions were left alone. In particular,
  LiveFX handles its two omitted switch values with an earlier return.
  Parenthesization warnings in two test predicates match their stated intent.
  Vendor/generated warnings were not mass-suppressed.
- Warnings in simulation translation units **and their audited project
  headers** are **sim: not touched**. This includes the GameWindow destructor
  warning in its simulation-consumed header. Unclassified headers are also
  left untouched.

## Simulation boundary

`tools/sim/sim_audit.py` passed before and after, including the libclang 18.1.1
AST pass: 1,101 project compile commands, 12 link commands, 352 simulation
sources, 292 excluded sources, and no contract problems.
The manifest plus AST traversal protected 821 source/header paths. Intersecting
`git diff --name-only` against that set yielded **zero changed files**.
SHA-256 checks against the saved pre-change hashes of all 352 simulation
sources also yielded **zero differences**. No policy, manifest generator,
baseline, simulation flag, or shared simulation header was edited.

Unchanged emitted diagnostics labelled **sim: not touched**:
Clang 6,008; GCC 1,419; llvm-mingw 5,406.

## No-game test results

Both game-install variables were unset. The synthetic MP3 fixtures were
generated outside the checkout against mpg123 1.33.1 and supplied through
`OPENBFME_AUDIO_FIXTURES`. The new truncated-property regression was included
in both the production-before and production-after suites.

| Compiler/run | Cases passed | Failed | Skipped | Assertions passed | Failed |
| --- | ---: | ---: | ---: | ---: | ---: |
| GCC before | 3,844 | 0 | 20 | 998,184 | 0 |
| GCC after, final | 3,844 | 0 | 20 | 998,171 | 0 |
| Clang before | 3,843 | 1 | 20 | 998,162 | 2 |
| Clang after, final | 3,843 | 1 | 20 | 998,160 | 2 |

Case outcomes are identical before/after. Exact assertion-count identity is
not established: repeated unchanged after-runs also produce differing totals
(GCC: 998,162 and 998,171 passed; Clang: 998,161 and 998,160 passed).
No tests were disabled to obtain these results.

Clang's one failure is pre-existing: the off-limits deterministic-libm test at
`engine/tests/test_deterministic_libm.cpp:290` compares NaN bits with host
glibc. It has the same two failed assertions before and after; it was not
modified. GCC passes the full suite. llvm-mingw is compile-only; no Windows
test execution or retail/UI testing is claimed.

The shader-material subset passes before/after under Valgrind: two cases,
157 assertions, zero errors and zero leaks. All three final compiler builds
pass. The pre-commit gate and whitespace check also pass.

## Reproduction

Configure each Linux compiler with Ninja, Release, the four warning flags
above in `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS`, `OPENBFME_LAME=OFF`, and Godot
enabled. Run a clean full build, not only the changed targets, when comparing
diagnostic occurrence counts. Use `tools/release/build_windows.sh --no-export`
with the same flags in its existing CMake cache for llvm-mingw. The pinned
toolchain is downloaded by that script.

Run `openbfme_tests --no-colors --order-by=name` with both game-install
variables unset and `OPENBFME_AUDIO_FIXTURES` pointing to the generated
fixtures. Run `python3 tools/sim/sim_audit.py --build <clang-build>` for the
full classification/flags/link/AST audit.
