"""Module invocation of the CLI must actually run the CLI.

On 2026-08-05, ``python -m openbfme_importer.cli update-selection-entry ...``
exited 0 having done NOTHING: cli.py defined main() but never called it under
``__main__``, so seven selection.json updates were silently dropped while
every caller saw success. A command line that performs no action must never
exit 0 in silence — these tests pin the module entrypoint so the regression
goes red instead of invisible.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

# The subprocess must resolve the openbfme_importer package regardless of the
# directory pytest was invoked from, so anchor cwd to the importer root.
_IMPORTER_ROOT = Path(__file__).resolve().parents[1]


def _run_module_cli(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "openbfme_importer.cli", *args],
        capture_output=True,
        text=True,
        timeout=120,
        cwd=_IMPORTER_ROOT,
    )


def test_module_invocation_help_runs_main() -> None:
    # --help must print the argparse usage text and exit 0. Before the
    # __main__ hook existed this produced NO output at all, which is exactly
    # the silent no-op this test exists to catch.
    result = _run_module_cli("--help")
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip(), (
        "python -m openbfme_importer.cli --help produced no output: the "
        "module entrypoint is not calling main()"
    )
    assert "update-selection-entry" in result.stdout, (
        "help text does not list subcommands; main() did not run argparse"
    )


def test_module_invocation_bad_subcommand_fails_loudly() -> None:
    # An unknown subcommand must be a NONZERO exit with a complaint on
    # stderr. Exit 0 here means arguments are being ignored wholesale.
    result = _run_module_cli("definitely-not-a-subcommand")
    assert result.returncode != 0, (
        "unknown subcommand exited 0: module invocation is a silent no-op"
    )
    assert result.stderr.strip() or result.stdout.strip(), (
        "unknown subcommand produced no diagnostic at all"
    )
