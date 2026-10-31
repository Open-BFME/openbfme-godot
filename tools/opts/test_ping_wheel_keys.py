"""The ping wheel (not retail) must not take a key from a retail command. Its key choices are BINDINGS in godot/scripts/ping_wheel.gd; the keyboard
choices are checked against the RotWK 2.01 / BFME2 1.06 mount (Data\\INI\\CommandMap.ini and the language archive's CommandMap.ini, the two files the
game reads, as tools/opts/test_opts1_hotkeys.py reads them, and the game's string file):
  * G held (the default) shares G with the game by time: a tap is handed to the game (the CommandMap's STANCE_HOLDGROUND and the command buttons
    whose hotkey is G), only a held G is the wheel's. The test checks that retail does use G alone, the reason the default is a hold and not a press;
  * Alt+G: no CommandMap record uses KEY_G with ALT. A command button's hotkey never fires with Alt held (RotWK's HotKeyTranslator,
    RW 0x75B0E1), so the buttons that use G are not a clash;
  * J: no CommandMap record uses KEY_J with any modifier, and no command button hotkey is J: a button's hotkey is the letter after '&' in its label's
    string (the CONTROLBAR: labels), so no such string has '&J'.
The retail tests run only when ROTWK_INSTALL and BFME2_INSTALL are set.
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "fx"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_opts1_hotkeys import records  # noqa: E402  (the CommandMap block reader)

needs_installs = pytest.mark.skipif(not (os.environ.get("ROTWK_INSTALL") and os.environ.get("BFME2_INSTALL")),
                                    reason="ROTWK_INSTALL / BFME2_INSTALL not set")

WHEEL = ROOT / "godot" / "scripts" / "ping_wheel.gd"
OPTIONS = ROOT / "godot" / "scripts" / "game_options.gd"
SCREEN = ROOT / "godot" / "scripts" / "openbfme_options_screen.gd"


def bindings() -> dict[str, dict]:
    text = WHEEL.read_text(encoding="utf-8")
    block = text[text.index("const BINDINGS := {"):]
    block = block[:block.index("\n}")]
    out = {}
    pattern = (r'"(\w+)": \{"label": "([^"]+)", (?:"key": (KEY_\w+), "alt": (true|false), (?:"hold": (true)|"commandmap": "(KEY_\w+)")'
               r'|"button": (MOUSE_BUTTON_\w+))\}')
    for m in re.finditer(pattern, block):
        out[m.group(1)] = {"label": m.group(2), "godot_key": m.group(3), "alt": m.group(4) == "true", "hold": m.group(5) == "true",
                           "commandmap": m.group(6), "button": m.group(7)}
    return out


def test_bindings_match_the_option():
    b = bindings()
    assert list(b) == ["g", "altg", "j", "mouse4", "mouse5"]
    assert (b["g"]["godot_key"], b["g"]["alt"], b["g"]["hold"], b["g"]["commandmap"]) == ("KEY_G", False, True, None)
    assert (b["altg"]["godot_key"], b["altg"]["alt"], b["altg"]["commandmap"]) == ("KEY_G", True, "KEY_G")
    assert (b["j"]["godot_key"], b["j"]["alt"], b["j"]["commandmap"]) == ("KEY_J", False, "KEY_J")
    assert b["mouse4"]["button"] == "MOUSE_BUTTON_XBUTTON1" and b["mouse5"]["button"] == "MOUSE_BUTTON_XBUTTON2"
    options = OPTIONS.read_text(encoding="utf-8")
    m = re.search(r'"OpenBFMEPingKey": \{"default": "(\w+)", "choices": \[([^\]]*)\]\}', options)
    assert m, "game_options.gd EXTRAS has no OpenBFMEPingKey"
    assert m.group(1) == "g"
    assert re.findall(r'"(\w+)"', m.group(2)) == list(b)
    assert re.search(r'"OpenBFMEPingWheel": \{"default": "yes", "choices": \["yes", "no"\]\}', options)
    screen = SCREEN.read_text(encoding="utf-8")
    m = re.search(r'const PING_KEY_ITEMS := \[(.*)\]\n', screen)
    assert m and re.findall(r'\["(\w+)", "[^"]+"\]', m.group(1)) == list(b)


def hotkey_letters(text: str) -> dict[str, list[str]]:
    """letter -> the CONTROLBAR: labels whose string has it after '&' ('&&' is a plain '&')."""
    out: dict[str, list[str]] = {}
    label = None
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith("//"):
            continue
        if label is None:
            label = s
            continue
        if s.upper() == "END":
            label = None
            continue
        if label.upper().startswith("CONTROLBAR:"):
            for m in re.finditer(r"(?<!&)&([A-Za-z0-9])", s.replace("&&", "")):
                out.setdefault(m.group(1).upper(), []).append(label)
    return out


def test_hotkey_letters_reader():
    sample = 'CONTROLBAR:Foo\n"&Attack &&Move"\nEND\nCONTROLBAR:Bar\n"S&top"\nEND\nOBJECT:Baz\n"&Y"\nEND\n'
    assert hotkey_letters(sample) == {"A": ["CONTROLBAR:Foo"], "T": ["CONTROLBAR:Bar"]}


@needs_installs
def test_keys_are_free_in_retail():
    import bigfs

    mount = bigfs.Mount()
    maps = sorted(k for k in mount.files if k.replace("/", "\\").lower().split("\\")[-1] == "commandmap.ini")
    assert len(maps) >= 2, maps
    all_records = []
    for f in maps:
        all_records += records(mount.read(f).decode("latin-1"))
    assert len(all_records) > 50
    keys = {r.get("key") for r in all_records}
    assert "KEY_G" in keys and "KEY_H" in keys  # the parse reads the keys (G alone is STANCE_HOLDGROUND, a neighbour of ours that retail does use)
    b = bindings()
    clash = [r for r in all_records if r.get("key") == "KEY_G" and "ALT" in r.get("modifiers", "NONE").upper()]
    assert not clash, f"Alt+G is a CommandMap record's: {clash}"
    held = [r for r in all_records if r.get("key") == "KEY_G" and r.get("modifiers", "NONE").upper() == "NONE"]
    assert held, "retail binds no G alone: the held G could be a plain press"
    clash = [r for r in all_records if r.get("key") == "KEY_J"]
    assert not clash, f"J is a CommandMap record's: {clash}"
    assert {k for k, v in b.items() if v["commandmap"]} == {"altg", "j"}
    assert "G" in hotkey_letters(mount.read(sorted(k for k in mount.files if k.replace("/", "\\").lower() == "data\\lotr.str")[0]).decode("latin-1"))
    tables = sorted(k for k in mount.files if k.replace("/", "\\").lower() == "data\\lotr.str")
    assert tables, "the game's string file (data\\lotr.str) is not in the mount"
    letters = hotkey_letters(mount.read(tables[0]).decode("latin-1"))
    assert len(letters) > 15 and "A" in letters and "G" in letters  # the parse reads the hotkeys (Attack Move, Hold Ground ...)
    assert "J" not in letters, f"command buttons use J: {letters['J'][:10]}"
