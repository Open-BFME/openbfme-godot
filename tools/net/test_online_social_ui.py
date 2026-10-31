"""Lane ONLINEMENU-1: the FRIENDS page's script (godot/scripts/online_social.gd) against a shell stand-in (godot/tests/online_social_test.gd): the
button's count, the page's tabs and lines, the answers given in the page itself (friend request, invite while in a room, add by code, player actions,
the block note, the recovery code shown once, the status, BACK) and the privacy rows of Options. No network, credentials or retail files."""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")


@pytest.mark.skipif(not shutil.which(GODOT), reason="Godot binary unavailable")
def test_friends_page_script():
    result = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--script", "res://tests/online_social_test.gd"],
                            check=False, capture_output=True, text=True, timeout=180)
    output = result.stdout + result.stderr
    assert result.returncode == 0, output[-4000:]
    assert "SOCIAL UI PASS" in output, output[-4000:]
    # GDScript asserts only print in debug builds: a script error must not pass silently
    assert "SCRIPT ERROR" not in output and "Parse Error" not in output, output[-4000:]
