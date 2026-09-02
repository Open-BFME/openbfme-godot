from __future__ import annotations

import hashlib
import json
from pathlib import Path

from openbfme_importer.cook.screens import (
    SCHEMA,
    _cook_wnd,
    _failure_class,
    _slug,
    _write_if_changed,
)


WND = (
    b"FILE_VERSION = 2;\nWINDOW\n"
    b"WINDOWTYPE = USER;\n"
    b"SCREENRECT = UPPERLEFT: 0 0, BOTTOMRIGHT: 800 600, CREATIONRESOLUTION: 800 600;\n"
    b'NAME = "Fixture.wnd:Root";\nSTATUS = ENABLED;\nSTYLE = USER;\n'
    b'SYSTEMCALLBACK = "[None]";\nINPUTCALLBACK = "[None]";\n'
    b'TOOLTIPCALLBACK = "[None]";\nDRAWCALLBACK = "[None]";\nEND\n'
)


def test_wnd_cooks_to_screen_v1_without_actionscript(tmp_path: Path) -> None:
    source = tmp_path / "fixture.wnd"
    source.write_bytes(WND)

    document = _cook_wnd(source, "window/fixture.wnd")

    assert document["schema"] == SCHEMA
    assert document["kind"] == "wnd"
    assert document["source"]["sha256"] == hashlib.sha256(WND).hexdigest()
    assert document["window"]["windowCount"] == 1
    assert document["actionScripts"] == []
    assert document["vmConstants"] == {}


def test_screen_output_identity_and_write_are_deterministic(tmp_path: Path) -> None:
    assert _slug("window/menus/shellgameloadscreen.wnd") == _slug(
        "window/menus/shellgameloadscreen.wnd"
    )
    target = tmp_path / "screen.json"
    payload = json.dumps({"schema": SCHEMA}, sort_keys=True).encode()
    assert _write_if_changed(target, payload)
    assert not _write_if_changed(target, payload)


def test_failure_classes_name_required_screen_failures() -> None:
    assert _failure_class(ValueError("Movie.const has unsupported CONST magic")) == "unparseable-const"
    assert _failure_class(ValueError("geometry 7 unresolved")) == "missing-geometry"
    assert _failure_class(ValueError("texture assignment unresolved")) == "missing-texture-reference"
    assert _failure_class(ValueError("unsupported opcode 0x62")) == "unsupported-opcode"
