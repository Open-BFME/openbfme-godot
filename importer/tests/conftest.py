"""Importer test fixtures.

Windows CI runners sometimes mix 8.3 short paths (e.g. RUNNER~1) with long
paths (runneradmin) for the same directory. Path comparisons and
Path.relative_to then fail spuriously. Expand short names after every
resolve() under the importer test process so gates are deterministic.

This module also refuses to run the suite off the pinned interpreter. The
importer pins Python and Pillow byte-exactly (bootstrap.py) because several
converters are digest-deterministic; running under an ambient PATH Python
turns those pins into ~29 phantom failures that look like a real baseline.
That is not hypothetical -- it manufactured a bogus "32 failed" baseline in
the arrow-art lane (PATH Python 3.13 / Pillow 12.3.0 instead of the pinned
3.12.13 / 12.2.0, whose true baseline is 3 failures).
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

from openbfme_importer.bootstrap import PYTHON_VERSION
from openbfme_importer.paths import expand_windows_long_path


#: Set to 1/true/yes to run off the pinned interpreter anyway. Deliberately
#: loud and explicit: an unset escape hatch is what lets drift go unnoticed.
UNPINNED_OPT_OUT = "OPENBFME_ALLOW_UNPINNED_PYTHON"
PINNED_PILLOW_VERSION = "12.2.0"


def _pinned_interpreter_drift() -> str:
    """Describe how this interpreter differs from the pinned one, if at all."""

    problems: list[str] = []
    actual_python = sys.version.split()[0]
    if actual_python != PYTHON_VERSION:
        problems.append(f"Python {actual_python} (pinned {PYTHON_VERSION})")
    try:
        import PIL

        if PIL.__version__ != PINNED_PILLOW_VERSION:
            problems.append(
                f"Pillow {PIL.__version__} (pinned {PINNED_PILLOW_VERSION})"
            )
    except ImportError:
        problems.append("Pillow is not importable")
    return ", ".join(problems)


def pytest_configure(config: pytest.Config) -> None:
    drift = _pinned_interpreter_drift()
    if not drift:
        return
    allowed = os.environ.get(UNPINNED_OPT_OUT, "").strip().casefold() in {
        "1",
        "true",
        "yes",
    }
    message = (
        f"importer tests are running off the pinned interpreter: {drift}. "
        "Digest-deterministic converters (cursor/texture-atlas/native-corpus) "
        "will fail for environment reasons and any pass/fail count taken from "
        "this run is not a baseline. Use "
        ".private/retail-work/tools/python-3.12-env/Scripts/python.exe (or run "
        "tools/bootstrap-importer-python.ps1)."
    )
    if not allowed:
        raise pytest.UsageError(f"{message} Set {UNPINNED_OPT_OUT}=1 to override.")
    print(f"\nWARNING: {message} Continuing because {UNPINNED_OPT_OUT} is set.\n")


_ORIG_RESOLVE = Path.resolve


def _resolve_expand_short(self: Path, *args, **kwargs):  # type: ignore[no-untyped-def]
    return expand_windows_long_path(_ORIG_RESOLVE(self, *args, **kwargs))


if os.name == "nt":
    Path.resolve = _resolve_expand_short  # type: ignore[method-assign]
