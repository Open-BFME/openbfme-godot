# Synthetic no-game coverage

Base: `c4ee740e5b8cf1f060aec0fad9ad4c9c5bb1ae64`. Toolchain: GCC/gcov 11.4.0, gcovr 5.0, Debug, `--coverage -O0 -g`.

- [Results, regression fixes and test counts](results.md)
- Generated before/after reports and tables are not retained in this tree. The percentages in [results.md](results.md) record the original run; regenerate reports locally with the commands below to inspect detailed counts.
- Retained inputs: [selection](selected.json), [retail-only classification](retail-only.json). Simulation classification comes from the configured build's `sim_manifest.json`.

## Scope and selection

Only `engine/src/` is included. `engine/thirdparty/`, staged Lua/vendor sources in the build directory, system headers, and the test sources themselves are outside that filter. Any nested `vendor` or `thirdparty` directory is also excluded. The reconstructed engine libraries under `engine/src/Libraries/` remain in scope; they are not the pristine third-party dependency trees.

The eight files are ranked by **baseline uncovered executable lines**, after excluding sources in the generated simulation manifest, all `GameLogic`, and the standalone retail gameplay runner. Synthetic APT movies, gadgets, profiles, maps/INIs, in-memory HTTP responses and generated MPEG/VP6 bitstreams exercise their real public entry points. No game bytes, extracted assets, external service credentials, private saves or network services are required by the new cases. The HUD fixture loads a minimal generated map/INI archive, not an installed game.

The standalone peer runner is measured at zero and listed separately, not treated as a no-game coverage gap: `main` mounts the retail world before every successful operation. Its argument-rejection path is possible without retail, but it is not the unit-testable client/format surface ranked here. No other whole file is called retail-only merely because its current tests skip. For example, HUD and movie decoders are synthetic-testable despite their ordinary use with game assets.

Generated tables retain both raw overall coverage and an adjusted overall excluding the explicitly listed retail-only runner. Per-directory rows contain **direct** members, not recursive totals, so summing them does not double count. Percentages are calculated from aggregate covered/total counts, never averaged. Per-file rows include headers instantiated in project code. Files not compiled by this no-Godot configuration are listed as **unmeasured**, not misleadingly reported as 0%. Branches are GCC/gcovr decision arcs with **both** `--exclude-throw-branches` and `--exclude-unreachable-branches` applied to both measurements, not a platform-independent semantic branch count. Added guards, newly instantiated header code, and gcov's classification of newly executed exception edges can change denominators.

Simulation gaps are findings only. The generated manifest and `sim_audit.py` classify the selection; no simulation source or pin is modified.

## Reproduction

Run from the repository root with CMake, Ninja, GCC/gcov, gcovr, and Python available. Existing integration tests can additionally need Node 22 and Python for their local fixture servers. The new coverage cases use deterministic in-memory transports instead.

First measure the base revision in a separate checkout/build using the same configuration, no-game test command, and gcovr filters below. The base predates the coverage option, so instrument it explicitly with `-DCMAKE_C_FLAGS='--coverage -O0 -g' -DCMAKE_CXX_FLAGS='--coverage -O0 -g' -DCMAKE_EXE_LINKER_FLAGS=--coverage` instead of `-DOPENBFME_COVERAGE=ON`. Save its gcovr JSON summary as `before-summary.json` in the working directory used for the comparison. This baseline is a required local input; it is not shipped with these notes. The original run's base revision is recorded above.

```sh
cmake -S engine -B engine/build-coverage -G Ninja \
  -DOPENBFME_BUILD_GODOT=OFF -DOPENBFME_COVERAGE=ON -DOPENBFME_LAME=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build engine/build-coverage --target openbfme_tests -j 4

# Clean counters before a measurement; never combine exploratory/mutation runs.
find engine/build-coverage -name '*.gcda' -delete
env -u ROTWK_INSTALL -u BFME2_INSTALL -u RW_GAME_DAT -u OPENBFME_FOUNDATION_SAVE \
  -u CAMP1_SURVEY -u CAMP1_PROBE \
  engine/build-coverage/openbfme_tests --reporters=xml --duration=true --no-colors=true \
  --test-case-exclude='audio decode: MP3 *,audio decode: corrupted MP3 fixtures*' > no-game.xml
# A known pre-existing host-libm comparison failure is documented in results.md.
gcovr --root . --filter 'engine/src/' --exclude '(^|/)(vendor|thirdparty)/' \
  --exclude-throw-branches --exclude-unreachable-branches \
  --json-summary after-summary.json engine/build-coverage
python3 tools/doctest_counts.py no-game.xml --inactive-cases docs/coverage-1/inactive-cases.json
python3 tools/coverage_report.py --before before-summary.json --after after-summary.json \
  --manifest engine/build-coverage/sim_manifest.json --selected docs/coverage-1/selected.json \
  --retail-only docs/coverage-1/retail-only.json --source-root engine/src --output tables.md
python3 tools/sim/sim_audit.py --build engine/build-coverage --ast=require --jobs 4
python3 -m pytest -q tools/test_coverage_report.py tools/test_doctest_counts.py
```

The existing MP3 golden-fixture cases are excluded because their pinned encoder/reference fixture toolchain is not provisioned. Their excluded count is included in skips, not passes. The new MP3 tests generate valid MPEG Layer III frames from bits in memory, including nonzero Huffman-coded samples, reservoir, intensity-stereo and short/mixed-window cases.

Use `--test-case='coverage1:*'` for only the eight new sets. All other cases then appear as filter skips in doctest XML; those are not skips within the selected sets.

`OPENBFME_COVERAGE` is **OFF by default**. A separate configure without the option confirms no `--coverage` compile or link flags. No test runner, sanitizer default, simulation compiler contract or normal build is silently changed.

## Honest counts

Many older tests print `SKIP` then return, which doctest counts as success. `tools/doctest_counts.py` reconciles them against doctest's final summary and reports them as skipped. Five additional zero-assertion early returns are manually audited in [inactive-cases.json](inactive-cases.json): three require retail inputs and two are disabled campaign diagnostics. That inventory fails closed if a named case disappears or starts executing assertions. Other unexpected zero-assertion successes remain explicitly visible for review.

Coverage reports do not claim retail rendering, actual installed-movie compatibility, real online backends, private saves, or golden MP3 fidelity was tested. Remaining unexercised non-simulation lines are still reported as gaps, even when difficult to synthesize.
