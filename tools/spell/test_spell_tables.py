"""The committed SpecialPower name tables match the RotWK binary (runs when ROTWK_INSTALL is set)."""
import os
import subprocess
import sys
from pathlib import Path

import pytest

pytestmark = pytest.mark.skipif(not os.environ.get("ROTWK_INSTALL"), reason="ROTWK_INSTALL not set")


def test_special_power_names_match_binary():
    script = Path(__file__).resolve().parent / "extract_spell_tables.py"
    assert subprocess.run([sys.executable, str(script), "--check"]).returncode == 0
