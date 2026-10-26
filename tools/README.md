# Python tool tests

From the repository root on Linux/macOS with a POSIX shell and Python 3.13
(the release scanner requires Python 3.11 or newer):

```sh
python3.13 -m venv .venv
. .venv/bin/activate
python -m pip install -r tools/requirements-test.txt
python -m pip install capstone pefile
python -m pytest -n 8 --dist worksteal -q -p no:cacheprovider tools --durations=50 -ra
```

Use `-n auto` to use all available CPUs, or an explicit worker count on a shared
runner. Work stealing redistributes queued tests from slow workers: release
integration tests take much longer than the many prerequisite-only checks. Do
not use `--dist loadfile`, which serializes the large release test modules.
Omit `-n` and `--dist` for a serial debugging run. No tests are deselected by
this command. The public PR check's `tools/release/pr_pytest.py` is deliberately
a smaller subset; it is not a replacement for the full `tools` suite.

Godot, Node, platform-specific native builds, reference audio fixtures and
privately installed retail games enable additional existing tests. The
`capstone` and `pefile` packages above are needed for binary-analysis tests;
they are not in `tools/requirements-test.txt`. Missing prerequisites are
reported by `-ra`; never add game data or generated retail-format fixtures to
the repository.

Godot-dependent release tests skip with a specific reason when its executable
is absent; the release tools themselves still fail closed without Godot.
The historical bridge grammar probes also need the exact published launcher
trees documented in `docs/WINDOWS.md`; their skip messages name the missing
tree and the tag to fetch. Current-source probes and Python bridge rules do
not depend on that history.
Completing the suite within its time budget is not the same as passing it:
retain the nonzero exit status and investigate every reported failure.

## CI must check exit status

Run the command directly, or preserve both timeout and pytest failures when
capturing output (Linux Bash, with GNU `timeout` and `tee`, after activating
the environment above):

```sh
set -o pipefail
timeout 600 python -m pytest -n 8 --dist worksteal -q -p no:cacheprovider tools \
  --durations=50 -ra --junitxml=pytest-tools.xml 2>&1 | tee pytest-tools.log
```

Any nonzero exit status is a failed gate, including timeout (124), collection
errors and worker crashes. Grepping the log for `failed`/`error` misses killed
runs. Keep the JUnit report and log as CI artifacts.

## Parallel-test conventions

- Mutable repositories, homes, keys and service state belong to each test's
  `tmp_path`. Session fixtures may cache expensive setup only if every test
  receives an independent copy, including Git objects (not hardlinks).
- Bind test servers to port `0`; never reserve a fixed shared port or write
  generated output to the source tree.
- Prefer readiness events or bounded polling to guessed startup/cleanup sleeps.
  Deliberately slow responses used to test timeout/throttling behavior must
  retain their delays.
- Collection runs independently on every worker. Any archive bytes in
  parametrization must be reproducible: gzip `mtime=0`, fixed ZIP entry
  `date_time`, and uncompressed tar followed by gzip with `mtime=0`.

To check collection, run `python -m pytest --collect-only -q tools` twice at
least two seconds apart and compare the lines beginning with `tools/` and
containing `::`. Ignore only pytest's elapsed-time summary, not test IDs.
