"""Lane SDK-2: the vendored Luau 0.744 is pristine and the patch layer's diff is current (tools/luau_patch_check.py)."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def test_luau_vendoring_is_pristine_and_the_patch_is_current():
    r = subprocess.run([sys.executable, str(HERE / "luau_patch_check.py")], capture_output=True, text=True, timeout=120)
    assert r.returncode == 0 and r.stdout.strip().endswith("luau vendoring ok"), r.stdout + r.stderr
