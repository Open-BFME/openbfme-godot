"""Pins the FX INI scan: the committed golden counts (engine/tests/data/fx/fx_ini_counts.json, which the C++ parser tests compare against) must equal
a fresh scan of the retail archives, and the scanner itself must count a hand-written snippet correctly. The archive case runs only when ROTWK_INSTALL
and BFME2_INSTALL are set."""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import fx_ini_scan  # noqa: E402

SNIPPET_PS = """
FXParticleSystem One
  System
    Priority = ALWAYS_RENDER
  End
  Color = DefaultColor
    Color1 = R:255 G:0 B:0 0
  End
  Draw = DefaultDraw
  End
End
FXParticleSystem Two ; comment
  System
  End
End
"""

SNIPPET_FXLIST = """
FXList A
  ParticleSystem
    Name = One
  End
  Sound
    Name = S
  End
End
FXList B
  CullingInfo = Sphere
End
"""


def test_scanner_counts_a_snippet():
    ps = fx_ini_scan.scan_particle_systems(SNIPPET_PS)
    assert [b["name"] for b in ps["blocks"]] == ["One", "Two"]
    assert ps["tail_depth"] == 0
    assert ("Color", "DefaultColor") in ps["blocks"][0]["classes"]
    assert ("Draw", "DefaultDraw") in ps["blocks"][0]["classes"]
    fl = fx_ini_scan.scan_fxlists(SNIPPET_FXLIST)
    assert [b["name"] for b in fl["blocks"]] == ["A", "B"]
    assert fl["blocks"][0]["nuggets"] == ["ParticleSystem", "Sound"]
    assert fl["blocks"][0]["fields"]["Sound.Name"] == 1
    assert fl["blocks"][1]["listFields"]["CullingInfo"] == 1


@pytest.mark.skipif(not (os.environ.get("ROTWK_INSTALL") and os.environ.get("BFME2_INSTALL")), reason="ROTWK_INSTALL / BFME2_INSTALL not set")
def test_golden_counts_match_the_archives():
    from bigfs import Mount

    golden = json.loads((HERE.parent.parent / "engine" / "tests" / "data" / "fx" / "fx_ini_counts.json").read_text())
    fresh = fx_ini_scan.summarize(Mount())
    assert fresh == golden
    assert fresh["fxParticleSystem"]["blocks"] == 1911
    assert fresh["fxList"]["blocks"] == 846
