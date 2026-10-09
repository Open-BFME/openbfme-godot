# Claude contributor note

The rebuild targets RotWK 2.01 retail, loading the user's original game
files at runtime. `docs/PLAN.md` holds the standing engineering rules and
acceptance gates; follow it. `archive/` holds the previous codebase: read it
for reference, never extend it.

- Game files, extracted retail data and decomp clones live under the
  ignored `workspace/` (decomps: `workspace/reference/open-bfme-1`,
  `workspace/reference/open-bfme-2`). Never commit retail-format bytes.
- `tools/precommit.py` is the commit gate; install it as the pre-commit
  hook and never bypass it.
- Use Windows-native commands and stage explicit paths.

## Build and test (rebuild scaffold)

- `build.bat` - configures `engine/` with CMake+Ninja under MSVC (vcvars64 via
  vswhere) and builds the `openbfme` GDExtension into `godot/bin/` plus
  `engine/build/openbfme_tests.exe`. `build.bat Debug` for a debug build. It also
  (incrementally) builds the test helper executables in `tools/` (x87_oracle,
  retail_oracle and the engine drivers); the tests fail with "stale helper: ... rebuild
  with ..." when one is older than its sources.
- `run_tests.bat` - C++ unit tests (doctest), then the headless Godot smoke test
  `godot/tests/smoke_test.gd`. Needs `GODOT` (the 4.7 `_console.exe`); with
  `ROTWK_INSTALL`/`BFME2_INSTALL` unset the smoke test prints SKIP and the script
  exits 77. Exit 0 = everything passed.
- Engine code with no Godot dependency goes in `openbfme_core` (unit-testable);
  Godot-facing classes go in `engine/src/GodotDevice/`. Keep ZH class/file names
  and cite the ZH or decompile file a port comes from.
- No fallbacks: a missing file, unknown archive or failed parse is an error that
  reaches the report/test, never a silent default.
