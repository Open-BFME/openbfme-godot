"""Fast, synthetic-fixture tests for the interface-art reference judge.

These tests never read the retail oracle. They build a miniature effective-assets
tree and a miniature content pack so the judge's PASS *and* its deliberate FAIL
paths are both exercised in milliseconds (rulebook P4/P5).
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.interface_art import (
    InterfaceArtError,
    collect_image_references,
    judge_interface_art,
    load_mapped_images,
    lookup_shipped_image,
    pack_image_filename,
    resolve_selection_pack_dirs,
    scan_shipped_images,
)


COMMANDBUTTON_INI = """
CommandButton Command_ConstructGondorBarracks
  Command = DOZER_CONSTRUCT
  Object = GondorBarracks
  ButtonImage = BGBarracks
  TextLabel = CONTROLBAR:BuildBarracks
End

CommandButton Command_TrainGondorSoldier
  Command = UNIT_BUILD
  Object = GondorFighterHorde
  ButtonImage = UIGondorSoldier
End

CommandButton Command_NoImageAtAll
  Command = TOGGLE_OVERCHARGE
End

; Retail toggle buttons list one image PER TOGGLE STATE on a single line, e.g.
; data/ini/commandbutton.ini "CommandButton Command_ToggleStance" whose
; TextLabel/ButtonImage rows both carry three space-separated values.
CommandButton Command_ToggleGondorStance
  Command = TOGGLE_STANCE
  ButtonImage = BGBarracks UIGondorSoldier
End

; ... while a few MappedImage ids genuinely contain a space, e.g.
; data/ini/mappedimages/handcreated/handcreatedmappedimages.ini
; "MappedImage Ruler-Right End". A whole-line match must win over splitting.
CommandButton Command_ShowRuler
  Command = TOGGLE_OVERCHARGE
  ButtonImage = Ruler-Right End
End
"""

COMMANDSET_INI = """
CommandSet GondorBarracksCommandSet
  1 = Command_TrainGondorSoldier
  2 = Command_NoImageAtAll
End
"""

STRUCTURE_INI = """
Object GondorBarracks
  SelectPortrait = BPGBarracks
  CommandSet = GondorBarracksCommandSet
End
"""

UNIT_INI = """
Object GondorFighterHorde
  SelectPortrait = UPGondorSoldier
End
"""

MAPPED_IMAGES_INI = """
MappedImage BGBarracks
  Texture = SCUserInterface_001.tga
  TextureWidth = 512
  TextureHeight = 512
  Coords = Left:0 Top:0 Right:64 Bottom:64
End

MappedImage BPGBarracks
  Texture = SCUserInterface_001.tga
  TextureWidth = 512
  TextureHeight = 512
  Coords = Left:64 Top:0 Right:128 Bottom:64
End

MappedImage UIGondorSoldier
  Texture = SCUserInterface_001.tga
  TextureWidth = 512
  TextureHeight = 512
  Coords = Left:128 Top:0 Right:192 Bottom:64
End

MappedImage Ruler-Right End
  Texture = SCUserInterface_001.tga
  TextureWidth = 512
  TextureHeight = 512
  Coords = Left:192 Top:0 Right:256 Bottom:64
End

MappedImage UPGondorSoldier
  Texture = SCNeverCompiled_001.tga
  TextureWidth = 512
  TextureHeight = 512
  Coords = Left:0 Top:64 Right:64 Bottom:128
End
"""


REFERENCED_IDS = (
    "BGBarracks",
    "BPGBarracks",
    "Ruler-Right End",
    "UIGondorSoldier",
    "UPGondorSoldier",
)


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="cp1252")


@pytest.fixture()
def oracle(tmp_path: Path) -> Path:
    root = tmp_path / "effective-assets"
    _write(root / "data/ini/commandbutton.ini", COMMANDBUTTON_INI)
    _write(root / "data/ini/commandset.ini", COMMANDSET_INI)
    _write(
        root / "data/ini/object/goodfaction/structures/men/barracks.ini", STRUCTURE_INI
    )
    _write(root / "data/ini/object/goodfaction/units/gondor/soldier.ini", UNIT_INI)
    _write(
        root / "data/ini/mappedimages/aptimages/unitcommands.ini", MAPPED_IMAGES_INI
    )
    # Only one of the two named textures is actually compiled: the judge must
    # report the other as a retail-source gap by name, never silently drop it.
    atlas = root / "art/compiledtextures/sc/scuserinterface_001.dds"
    atlas.parent.mkdir(parents=True, exist_ok=True)
    _save_atlas(atlas)
    return root


def _save_atlas(path: Path, size: tuple[int, int] = (512, 512)) -> None:
    from PIL import Image

    image = Image.new("RGBA", size)
    for x in range(size[0]):
        for y in range(0, size[1], 97):
            image.putpixel((x, y), (x % 256, y % 256, 7, 255))
    image.save(path, format="PNG")


def _make_pack(root: Path, image_ids: tuple[str, ...]) -> Path:
    pack = root / "pack" / "digest"
    directory = pack / "assets/ui/interface-art/aaaaaaaaaaaa"
    directory.mkdir(parents=True, exist_ok=True)
    index = {}
    for image_id in image_ids:
        name = pack_image_filename(image_id)
        (directory / name).write_bytes(b"\x89PNG\r\n\x1a\n")
        index[image_id] = f"assets/ui/interface-art/aaaaaaaaaaaa/{name}"
    manifest = pack / "data/interface-art/index.json"
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text(
        json.dumps(
            {
                "schema": "openbfme.interface-art-index",
                "schemaVersion": 1,
                "images": index,
            },
            indent=2,
            sort_keys=True,
        ),
        encoding="utf-8",
    )
    selection = root / "selection.json"
    selection.write_text(
        json.dumps(
            {
                "schema": "openbfme.pack-selection",
                "schemaVersion": 0,
                "activePack": "pack/digest",
                "supplementalPacks": [],
            }
        ),
        encoding="utf-8",
    )
    return selection


def test_pack_image_filename_matches_the_shipped_pack_convention() -> None:
    # The convention already in the published packs:
    # assets/ui/structures/gondorarcherrange/<atlas>/bgarcheryrange-e6a50364.png
    assert pack_image_filename("BGArcheryRange") == "bgarcheryrange-e6a50364.png"
    assert pack_image_filename("bgarcheryrange") == "bgarcheryrange-e6a50364.png"


def test_collect_image_references_covers_buttons_portraits_and_commandsets(
    oracle: Path,
) -> None:
    references = collect_image_references(oracle)
    ids = sorted({reference.image_id for reference in references})
    assert ids == sorted(REFERENCED_IDS)
    fields = {(reference.field, reference.image_id) for reference in references}
    assert ("ButtonImage", "BGBarracks") in fields
    assert ("SelectPortrait", "BPGBarracks") in fields
    # Command_NoImageAtAll has no ButtonImage; it must not invent one.
    assert all(reference.image_id for reference in references)
    owners = {reference.owner for reference in references}
    assert "Command_ConstructGondorBarracks" in owners
    assert "GondorBarracks" in owners


def test_commandset_scope_restricts_the_universe(oracle: Path) -> None:
    scoped = collect_image_references(oracle, object_path_tokens=("structures/men",))
    ids = sorted({reference.image_id for reference in scoped})
    # GondorBarracks is in scope, and its CommandSet pulls in the train button.
    assert ids == ["BPGBarracks", "UIGondorSoldier"]


def test_multi_state_button_image_lines_expand_into_one_id_per_state(
    oracle: Path,
) -> None:
    references = collect_image_references(oracle)
    toggle = sorted(
        reference.image_id
        for reference in references
        if reference.owner == "Command_ToggleGondorStance"
    )
    assert toggle == ["BGBarracks", "UIGondorSoldier"]
    # A whole-line match wins: this id really does contain a space.
    ruler = [
        reference.image_id
        for reference in references
        if reference.owner == "Command_ShowRuler"
    ]
    assert ruler == ["Ruler-Right End"]


def test_load_mapped_images_indexes_every_block(oracle: Path) -> None:
    index, ambiguous = load_mapped_images(oracle)
    assert ambiguous == ()
    assert sorted(index) == sorted(name.casefold() for name in REFERENCED_IDS)
    assert index["bgbarracks"].texture == "SCUserInterface_001.tga"


def test_judge_fails_when_nothing_is_shipped(oracle: Path, tmp_path: Path) -> None:
    selection = _make_pack(tmp_path / "packs", ())
    report = judge_interface_art(
        oracle_root=oracle, selection_path=selection, packs_root=tmp_path / "packs"
    )
    assert report.unresolved_count == 5
    assert sorted(report.unresolved_ids) == sorted(REFERENCED_IDS)
    assert report.exit_code == 1
    assert "unresolved=5" in report.summary_line()


def test_judge_names_the_retail_source_gap_separately(
    oracle: Path, tmp_path: Path
) -> None:
    selection = _make_pack(
        tmp_path / "packs",
        ("BGBarracks", "BPGBarracks", "Ruler-Right End", "UIGondorSoldier"),
    )
    report = judge_interface_art(
        oracle_root=oracle, selection_path=selection, packs_root=tmp_path / "packs"
    )
    # UPGondorSoldier names an atlas retail never compiled: still unresolved,
    # but classified so the gap cannot be confused with a converter failure.
    assert report.unresolved_ids == ("UPGondorSoldier",)
    assert report.reasons["UPGondorSoldier"] == "retail-atlas-absent"
    assert report.exit_code == 1


def test_judge_passes_only_when_every_reference_ships(
    oracle: Path, tmp_path: Path
) -> None:
    selection = _make_pack(tmp_path / "packs", REFERENCED_IDS)
    report = judge_interface_art(
        oracle_root=oracle,
        selection_path=selection,
        packs_root=tmp_path / "packs",
        source_null_allowlist=(),
    )
    assert report.unresolved_count == 0
    assert report.exit_code == 0
    assert report.referenced_id_count == 5


def test_judge_catches_one_deliberately_removed_image_by_name(
    oracle: Path, tmp_path: Path
) -> None:
    """Rulebook P5: break the judge on purpose, one image at a time."""

    packs = tmp_path / "packs"
    selection = _make_pack(packs, REFERENCED_IDS)
    good = judge_interface_art(
        oracle_root=oracle, selection_path=selection, packs_root=packs
    )
    assert good.exit_code == 0

    victim = packs / "pack/digest/assets/ui/interface-art/aaaaaaaaaaaa"
    (victim / pack_image_filename("UIGondorSoldier")).unlink()
    manifest = packs / "pack/digest/data/interface-art/index.json"
    payload = json.loads(manifest.read_text(encoding="utf-8"))
    payload["images"].pop("UIGondorSoldier")
    manifest.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")

    broken = judge_interface_art(
        oracle_root=oracle, selection_path=selection, packs_root=packs
    )
    assert broken.unresolved_ids == ("UIGondorSoldier",)
    assert broken.reasons["UIGondorSoldier"] == "not-shipped"
    assert broken.exit_code == 1


def test_judge_detects_an_index_entry_whose_png_is_missing(
    oracle: Path, tmp_path: Path
) -> None:
    packs = tmp_path / "packs"
    selection = _make_pack(packs, REFERENCED_IDS)
    directory = packs / "pack/digest/assets/ui/interface-art/aaaaaaaaaaaa"
    (directory / pack_image_filename("BGBarracks")).unlink()
    report = judge_interface_art(
        oracle_root=oracle, selection_path=selection, packs_root=packs
    )
    assert "BGBarracks" in report.unresolved_ids
    assert report.reasons["BGBarracks"] == "index-entry-missing-file"


def test_source_null_allowlist_must_be_named_and_is_reported(
    oracle: Path, tmp_path: Path
) -> None:
    selection = _make_pack(
        tmp_path / "packs",
        ("BGBarracks", "BPGBarracks", "Ruler-Right End", "UIGondorSoldier"),
    )
    report = judge_interface_art(
        oracle_root=oracle,
        selection_path=selection,
        packs_root=tmp_path / "packs",
        source_null_allowlist=("UPGondorSoldier",),
    )
    assert report.unresolved_count == 0
    assert report.waived_ids == ("UPGondorSoldier",)
    assert report.exit_code == 0
    assert "waived=1" in report.summary_line()

    with pytest.raises(InterfaceArtError, match="allowlist"):
        judge_interface_art(
            oracle_root=oracle,
            selection_path=selection,
            packs_root=tmp_path / "packs",
            # Allowlisting an image that is actually shippable is forbidden:
            # a waiver may only cover a genuine retail-source gap (rulebook P6).
            source_null_allowlist=("BGBarracks",),
        )


def test_resolve_selection_pack_dirs_reports_every_mount(tmp_path: Path) -> None:
    packs = tmp_path / "packs"
    selection = _make_pack(packs, ())
    dirs = resolve_selection_pack_dirs(selection, packs)
    assert [directory.name for directory in dirs] == ["digest"]


def test_scan_shipped_images_falls_back_to_the_hash_suffix_convention(
    tmp_path: Path,
) -> None:
    """Legacy packs predate the index; their hashed filenames still resolve."""

    pack = tmp_path / "legacy"
    directory = pack / "assets/ui/structures/gondorbarracks/70ef40df7273"
    directory.mkdir(parents=True, exist_ok=True)
    (directory / pack_image_filename("BGBarracks")).write_bytes(b"\x89PNG\r\n\x1a\n")
    shipped = scan_shipped_images((pack,))
    path, reason = lookup_shipped_image(shipped, "BGBarracks")
    assert reason == ""
    assert path.endswith(pack_image_filename("BGBarracks"))
    missing_path, missing_reason = lookup_shipped_image(shipped, "BPGBarracks")
    assert (missing_path, missing_reason) == ("", "not-shipped")
