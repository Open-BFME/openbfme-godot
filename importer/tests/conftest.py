"""Importer test fixtures.

Windows CI runners sometimes mix 8.3 short paths (e.g. RUNNER~1) with long
paths (runneradmin) for the same directory. Path comparisons and
Path.relative_to then fail spuriously. Expand short names after every
resolve() under the importer test process so gates are deterministic.
"""

from __future__ import annotations

import os
from pathlib import Path

from openbfme_importer.paths import expand_windows_long_path


_ORIG_RESOLVE = Path.resolve


def _resolve_expand_short(self: Path, *args, **kwargs):  # type: ignore[no-untyped-def]
    return expand_windows_long_path(_ORIG_RESOLVE(self, *args, **kwargs))


if os.name == "nt":
    Path.resolve = _resolve_expand_short  # type: ignore[method-assign]
