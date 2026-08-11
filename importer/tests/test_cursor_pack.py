"""Cursor pack planning over synthetic, project-authored cursor art."""

from __future__ import annotations

import base64
import json

import pytest

from openbfme_importer.cursor_pack import (
    CURSOR_INDEX_RELATIVE,
    CURSOR_INDEX_SCHEMA,
    CursorPackError,
    compose_cursor_profile,
    plan_cursor_pack,
    read_loose_cursor,
    resolve_cursors_root,
)
from openbfme_importer.profile import ImportProfile

from .test_cursor_art import _ani, _solid_frame

RED = (255, 0, 0)
GREEN = (0, 255, 0)

MOUSE_INI = b"""
MouseCursor Arrow
  Texture = SCCPointer.cur
  HotSpot = X:2 Y:2
End

MouseCursor AttackObj
  Texture = SCCAttack
End

MouseCursor ForceAttackObj
  Texture = SCCAttack
End

MouseCursor Beam
  Texture = Beam
End
"""


def _install() -> dict[str, bytes]:
    return {
        "sccattack.ani": _ani(
            [_solid_frame(RED), _solid_frame(GREEN)], sequence=[0, 1, 0], rates=[7, 7, 7]
        ),
        "sccpointer.cur": _solid_frame(RED),
        "beam.ani": b"NOTRIFF" + b"\x00" * 64,
    }


def _reader(files: dict[str, bytes]):
    def _read(name: str) -> bytes:
        if name not in files:
            raise FileNotFoundError(name)
        return files[name]

    return _read


def test_plan_carries_one_payload_and_aliases_the_shared_art() -> None:
    plan = plan_cursor_pack(
        MOUSE_INI,
        _reader(_install()),
        cursor_names=["attackobj", "forceattackobj"],
    )
    cursors = plan.document["cursors"]
    assert set(cursors) == {"attackobj", "forceattackobj"}
    assert cursors["forceattackobj"] == {"aliasOf": "attackobj", "texture": "SCCAttack"}
    assert plan.cursor_count == 1
    assert plan.alias_count == 1
    entry = cursors["attackobj"]
    assert entry["sequence"] == [0, 1, 0]
    # Rounded to microseconds so the document bytes stay stable across hosts.
    assert entry["frameSeconds"] == [round(7 / 60, 6)] * 3
    assert len(entry["frames"]) == 2
    assert (entry["width"], entry["height"]) == (2, 2)
    # The art's own hotspot wins when mouse.ini does not override it.
    assert entry["hotspot"] == {"x": 1, "y": 1}
    assert entry["hotspotFromIni"] is False
    for frame in entry["frames"]:
        payload = base64.b64decode(frame["png"])
        assert payload[:8] == b"\x89PNG\r\n\x1a\n"
        assert len(payload) == frame["bytes"]


def test_plan_prefers_the_ini_hotspot_over_the_art_hotspot() -> None:
    plan = plan_cursor_pack(MOUSE_INI, _reader(_install()), cursor_names=["arrow"])
    entry = plan.document["cursors"]["arrow"]
    assert entry["hotspot"] == {"x": 2, "y": 2}
    assert entry["hotspotFromIni"] is True


def test_plan_names_gaps_instead_of_dropping_or_substituting() -> None:
    files = _install()
    del files["sccpointer.cur"]
    plan = plan_cursor_pack(
        MOUSE_INI,
        _reader(files),
        cursor_names=["attackobj", "arrow", "beam", "nosuchcursor"],
    )
    assert set(plan.document["cursors"]) == {"attackobj"}
    reasons = {row["cursor"]: row["reason"] for row in plan.document["gaps"]}
    assert reasons == {
        "arrow": "retail-source-missing",
        "beam": "unsupported-container",
        "nosuchcursor": "no-mouse-ini-block",
    }


def test_plan_refuses_a_pack_that_would_carry_no_art() -> None:
    with pytest.raises(CursorPackError, match="carries no art"):
        plan_cursor_pack(MOUSE_INI, _reader({}), cursor_names=["attackobj"])


def test_plan_records_the_retail_source_digests() -> None:
    files = _install()
    plan = plan_cursor_pack(MOUSE_INI, _reader(files), cursor_names=["attackobj"])
    provenance = plan.document["provenance"]
    assert provenance["mouseIni"] == "data/ini/mouse.ini"
    assert len(provenance["mouseIniSha256"]) == 64
    assert [row["file"] for row in provenance["sources"]] == ["sccattack.ani"]
    assert provenance["sources"][0]["bytes"] == len(files["sccattack.ani"])


def test_plan_is_deterministic() -> None:
    first = plan_cursor_pack(MOUSE_INI, _reader(_install()), cursor_names=["attackobj"])
    second = plan_cursor_pack(MOUSE_INI, _reader(_install()), cursor_names=["attackobj"])
    assert json.dumps(first.document, sort_keys=True) == json.dumps(
        second.document, sort_keys=True
    )


def test_composed_profile_is_a_loadable_supplemental_pack(tmp_path) -> None:
    plan = plan_cursor_pack(MOUSE_INI, _reader(_install()), cursor_names=["attackobj"])
    composed = compose_cursor_profile(
        plan.document, pack_id="rotwk-cursors-vslice", game="rotwk", catalog_identity="a" * 64
    )
    assert composed["pack"]["files"] == {"cursors": CURSOR_INDEX_RELATIVE}
    assert composed["pack"]["dataPolicy"]["redistributable"] is False
    assert composed["runtime_data"][CURSOR_INDEX_RELATIVE]["schema"] == CURSOR_INDEX_SCHEMA
    path = tmp_path / "cursors.json"
    path.write_text(json.dumps(composed), encoding="utf-8")
    profile = ImportProfile.load(path)
    assert [rule.converter for rule in profile.resources] == ["hash-only"]
    assert profile.resources[0].patterns == ("data/ini/mouse.ini",)


def test_resolve_cursors_root_handles_flat_and_layered_installs(tmp_path) -> None:
    flat = tmp_path / "flat"
    (flat / "data" / "cursors").mkdir(parents=True)
    assert resolve_cursors_root(flat) == (flat / "data" / "cursors").resolve()

    layered = tmp_path / "layered"
    (layered / "layer-1-bfme2" / "data" / "cursors").mkdir(parents=True)
    (layered / "layer-0-rotwk" / "data" / "cursors").mkdir(parents=True)
    # Layer 0 (RotWK) must win over layer 1 (BFME2), as SAGE layering does.
    assert (
        resolve_cursors_root(layered)
        == (layered / "layer-0-rotwk" / "data" / "cursors").resolve()
    )

    empty = tmp_path / "empty"
    empty.mkdir()
    assert resolve_cursors_root(empty) is None


def test_loose_reader_refuses_to_escape_the_cursor_directory(tmp_path) -> None:
    root = tmp_path / "cursors"
    root.mkdir()
    (root / "sccattack.ani").write_bytes(b"payload")
    (tmp_path / "secret.ani").write_bytes(b"nope")
    read = read_loose_cursor(root)
    assert read("sccattack.ani") == b"payload"
    with pytest.raises(CursorPackError, match="escapes the cursors root"):
        read("../secret.ani")
    with pytest.raises(FileNotFoundError):
        read("missing.ani")


def test_default_cursor_names_carry_the_retail_move_family() -> None:
    """The bronze move arrows must be in the shipped set, not just the attack ones.

    Playtest report: "right-click move doesn't use the bronze arrows from the
    game base".  Root cause was this whitelist, not the runtime: every mounted
    pack carried exactly one payload (``SCCAttack``) aliased five ways, because
    only the attack family was ever named here.

    Oracle is retail's own binding table, ``data/ini/mouse.ini`` in the pure
    RotWK 2.01 effective-assets tree::

        MouseCursor Move           Image/Texture = SCCMove        (:64)
        MouseCursor AttackMove     Image/Texture = SCCAttMov      (:69)
        MouseCursor SetRallyPoint  Image/Texture = SCCRallyPnt    (:174)
        MouseCursor Waypoint       Image/Texture = SCCWaypoint    (:219)
        MouseCursor Normal/Arrow   Image/Texture = SCCPointer.cur (:36/:42)

    These names are lowercased ``MouseCursor`` block names, which is the key
    space ``parse_mouse_cursor_bindings`` emits and ``plan_cursor_pack``
    selects on.
    """
    from openbfme_importer.cursor_pack import DEFAULT_CURSOR_NAMES

    # The attack family must not be lost while widening.
    for name in ("attackobj", "forceattackobj", "forceattackground", "target", "outrange"):
        assert name in DEFAULT_CURSOR_NAMES

    # The move family the playtest report needs.
    for name in ("move", "attackmove", "setrallypoint", "waypoint", "normal", "arrow"):
        assert name in DEFAULT_CURSOR_NAMES, f"retail authors MouseCursor {name} but the pack would not carry it"

    # Every name is lowercase and unique, or plan_cursor_pack cannot match the
    # parsed mouse.ini keys.
    assert len(set(DEFAULT_CURSOR_NAMES)) == len(DEFAULT_CURSOR_NAMES)
    assert all(name == name.lower() for name in DEFAULT_CURSOR_NAMES)


def test_plan_carries_the_move_cursor_when_mouse_ini_binds_it(tmp_path) -> None:
    """End to end over the planner: a ``Move`` binding becomes a real payload.

    Guards the half of the whitelist change the constant test cannot see - that
    ``plan_cursor_pack`` actually resolves ``SCCMove`` to ``sccmove.ani`` and
    emits it as its OWN payload rather than an alias of the attack sprite.
    """
    mouse_ini = b"""
MouseCursor AttackObj
  Texture = SCCAttack
End

MouseCursor Move
  Texture = SCCMove
End
"""
    files = {
        "sccattack.ani": _ani([_solid_frame(RED)], sequence=None, rates=None),
        "sccmove.ani": _ani([_solid_frame(GREEN)], sequence=None, rates=None),
    }
    plan = plan_cursor_pack(mouse_ini, _reader(files), cursor_names=["attackobj", "move"])
    cursors = plan.document["cursors"]

    assert "move" in cursors, "the Move binding did not reach the compiled index"
    assert "aliasOf" not in cursors["move"], "Move must be its own payload, not an attack alias"
    assert cursors["move"]["texture"] == "SCCMove"
    assert cursors["move"]["source"] == "sccmove.ani"
    assert plan.cursor_count == 2, "Move must cost its own payload"
    # ...and it is genuinely different art from the attack cursor.
    assert cursors["move"]["frames"][0]["png"] != cursors["attackobj"]["frames"][0]["png"]
    assert not plan.gaps
