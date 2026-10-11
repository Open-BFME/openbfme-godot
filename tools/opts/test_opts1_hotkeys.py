"""Lane OPTS-1: the OpenBFME display hotkeys are not retail (owner, 2026-10-10) and must not take a key from a retail command. Their registry is
HOTKEYS in godot/scripts/fps_overlay.gd: F11 (the frame-rate counter, any modifier) and Alt+Enter (full screen). The retail-file test reads every
CommandMap.ini of the RotWK 2.01 / BFME2 1.06 mount (Data\\INI\\CommandMap.ini and the language archive's CommandMap.ini, the two files GameClient's
"CommandMap.ini" subsystem entry resolves to, MetaEvent.h / stop S-281) and checks that no CommandMap record uses those keys: no record with
Key = KEY_F11, and none with Key = KEY_ENTER / KEY_KPENTER and a Modifiers value that holds ALT. Runs only when ROTWK_INSTALL and BFME2_INSTALL are set.
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "fx"))

needs_installs = pytest.mark.skipif(not (os.environ.get("ROTWK_INSTALL") and os.environ.get("BFME2_INSTALL")),
                                    reason="ROTWK_INSTALL / BFME2_INSTALL not set")

OVERLAY = ROOT / "godot" / "scripts" / "fps_overlay.gd"


def registry() -> list[dict]:
    text = OVERLAY.read_text(encoding="utf-8")
    block = text[text.index("const HOTKEYS := ["):]
    block = block[:block.index("\n]")]
    out = []
    for m in re.finditer(r'\{"name": "(\w+)", "key": (KEY_\w+), "alt": (true|false), "commandmap": "(KEY_\w+)", "modifiers": "(\w+)"\}', block):
        out.append({"name": m.group(1), "godot_key": m.group(2), "alt": m.group(3) == "true", "commandmap": m.group(4), "modifiers": m.group(5)})
    return out


def records(text: str) -> list[dict]:
    """CommandMap blocks as {name, Key, Modifiers, Transition} (comments after ';' or '//' removed)."""
    lines = []
    for line in text.splitlines():
        line = line.split(";", 1)[0].split("//", 1)[0].strip()
        if line:
            lines.append(line)
    out, cur = [], None
    for line in lines:
        head = line.split()
        if head[0].lower() == "commandmap" and len(head) >= 2:
            cur = {"name": head[1]}
        elif cur is not None and head[0].lower() == "end":
            out.append(cur)
            cur = None
        elif cur is not None and "=" in line:
            k, v = (s.strip() for s in line.split("=", 1))
            cur[k.lower()] = v.split()[0] if v else ""
    return out


def test_registry_names_the_two_keys():
    reg = registry()
    assert [(r["name"], r["commandmap"], r["modifiers"]) for r in reg] == [
        ("OPENBFME_TOGGLE_FPS", "KEY_F11", "any"),
        ("OPENBFME_TOGGLE_FULLSCREEN", "KEY_ENTER", "ALT"),
    ]
    assert [r["godot_key"] for r in reg] == ["KEY_F11", "KEY_ENTER"]


@needs_installs
def test_hotkeys_are_free_in_retail_commandmap():
    import bigfs

    mount = bigfs.Mount()
    files = sorted(k for k in mount.files if k.replace("/", "\\").lower().split("\\")[-1] == "commandmap.ini")
    assert len(files) >= 2, files  # Data\INI\CommandMap.ini and the language archive's
    all_records = []
    for f in files:
        all_records += records(mount.read(f).decode("latin-1"))
    assert len(all_records) > 50  # the retail command set, not an empty parse
    keys = {r.get("key") for r in all_records}
    assert "KEY_F12" in keys and "KEY_ENTER" in keys  # the parse reads the keys (neighbours of ours that retail does use)
    for hk in registry():
        clash = []
        for r in all_records:
            key = r.get("key", "")
            mods = r.get("modifiers", "NONE").upper()
            if hk["commandmap"] == "KEY_F11" and key == "KEY_F11":
                clash.append(r)
            if hk["commandmap"] == "KEY_ENTER" and key in ("KEY_ENTER", "KEY_KPENTER") and "ALT" in mods:
                clash.append(r)
        assert not clash, f"{hk['name']} takes the key of {clash}"
