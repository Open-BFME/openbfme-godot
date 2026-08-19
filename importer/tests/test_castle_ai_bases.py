from __future__ import annotations

from pathlib import Path
import struct

import pytest

from openbfme_importer.castle_ai_bases import (
    compile_castle_ai_bases,
    resolve_ai_base_template,
)


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = ROOT / "workspace" / "retail-work"
if (
    not (PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets").is_dir()
    and ROOT.parent.name == "worktrees"
):
    PRIVATE_ROOT = ROOT.parents[2] / "workspace" / "retail-work"
EFFECTIVE_ASSETS = PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets"
SKIRMISH_AI_DATA = EFFECTIVE_ASSETS / "data" / "ini" / "default" / "skirmishaidata.ini"

oracle_present = pytest.mark.skipif(
    not SKIRMISH_AI_DATA.is_file() or not (EFFECTIVE_ASSETS / "bases").is_dir(),
    reason="pure RotWK skirmish AI/BSE oracle is not present",
)

SIX_FULLY_BOUND_CASTLE_MAPS = (
    "map wor minas tirith.map",
    "map wor helms deep.map",
    "map wor erebor.map",
    "map wor dol guldur.map",
    "map wor grey havens.map",
    "map wor ang carn dum.map",
)
THREE_GENERIC_FALLBACK_CASTLE_MAPS = (
    "map wor isengard.map",
    "map wor black gate.map",
    "map wor minas morgul.map",
)


@oracle_present
def test_six_castle_maps_compile_all_eight_retail_ai_base_bindings() -> None:
    source = SKIRMISH_AI_DATA.read_bytes()
    for map_name in SIX_FULLY_BOUND_CASTLE_MAPS:
        document = compile_castle_ai_bases(source, EFFECTIVE_ASSETS, map_name)
        assert document["schema"] == "openbfme.castle-ai-bases"
        assert document["gameMapToUseOn"].casefold() == map_name.casefold()
        assert document["layoutCount"] == 8
        assert {row["side"] for row in document["layouts"]} == {
            "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar", "Arnor"
        }
        assert sum(len(row["sites"]) for row in document["layouts"]) > 0


@oracle_present
def test_carn_dum_map_tokens_resolve_case_insensitively() -> None:
    document = compile_castle_ai_bases(
        SKIRMISH_AI_DATA.read_bytes(), EFFECTIVE_ASSETS, "MAP WOR ANG CARN DUM.MAP"
    )
    assert document["layoutCount"] == 8
    assert {row["binding"]["gameMapToUseOn"] for row in document["layouts"]} == {
        "MAP WOR ANG Carn Dum.map",
        "MAP WOR ANG Carn DUM.map",
        "map wor ANG Carn Dum.map",
    }
    assert all(row["source"]["virtualPath"].startswith("bases/") for row in document["layouts"])


@oracle_present
def test_three_unbound_castle_maps_emit_explicit_generic_fallback() -> None:
    source = SKIRMISH_AI_DATA.read_bytes()
    for map_name in THREE_GENERIC_FALLBACK_CASTLE_MAPS:
        document = compile_castle_ai_bases(source, EFFECTIVE_ASSETS, map_name)
        assert document["layoutCount"] == 0
        assert document["layouts"] == []
        assert document["fallback"] == "generic-any"


def test_base_template_lookup_is_case_insensitive_on_case_sensitive_filesystems(
    tmp_path: Path,
) -> None:
    directory = tmp_path / "BaSeS" / "AI Base - MotW - Carn Dum"
    directory.mkdir(parents=True)
    physical = directory / "AI BASE - MOTW - CARN DUM.BSE"
    physical.write_bytes(b"CkMp" + struct.pack("<I", 0))

    resolved, virtual = resolve_ai_base_template(
        tmp_path, "ai base - motw - carn dum"
    )

    assert resolved == physical
    assert virtual == physical.relative_to(tmp_path).as_posix()
