"""Unit tests for the stage-piece art pass of the HUD APT converter.

The static scene subset flattens only the bounded InitialSetup frame, so
every named dock piece authored hidden at frame 0 shipped no pixels.  The
stage-piece pass emits each named root placement's DISTINCT authored content
states as stage-space draws; these tests prove the flattening, the exact
affine placement composition, the tween-dedup state signature, and the named
receipts for artless pieces — all on synthetic movies, no retail install.
"""

from __future__ import annotations

from types import SimpleNamespace

from openbfme_importer.retail_hud_apt_convert import (
    _Transform,
    _compose_authored,
    _geometry_primitives,
    _stage_pieces_contract,
)


def _place(
    depth: int,
    character_id: int,
    *,
    offset: int,
    name: str | None = None,
    translation: tuple[float, float] = (0.0, 0.0),
    matrix: tuple[float, float, float, float] = (1.0, 0.0, 0.0, 1.0),
) -> dict:
    flags = 0x02 | 0x04 | 0x08
    row = {
        "kind": "place-object",
        "sourceOffset": offset,
        "flags": flags | (0x20 if name else 0),
        "depth": depth,
        "characterId": character_id,
        "matrix": list(matrix),
        "translation": list(translation),
        "tint": [1.0, 1.0, 1.0, 1.0],
        "additive": [0.0, 0.0, 0.0, 0.0],
        "ratio": 0.0,
        "clipDepth": 0,
    }
    if name:
        row["name"] = name
    return row


def _label(name: str, frame_id: int, *, offset: int) -> dict:
    return {
        "kind": "frame-label",
        "sourceOffset": offset,
        "name": name,
        "frameId": frame_id,
        "flags": 0,
    }


def _movie(name: str, characters: list, frames: list, **extra) -> SimpleNamespace:
    return SimpleNamespace(
        name=name,
        characters=characters,
        frames=frames,
        imports=extra.get("imports", {}),
        exports=extra.get("exports", {}),
        geometry=extra.get("geometry", {}),
        image_map=extra.get("image_map", {}),
        atlases=extra.get("atlases", {}),
    )


_TEXTURED_GEOMETRY = {
    7: [
        {
            "style": "tc",
            "values": [255.0, 255.0, 255.0, 255.0, 3.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0],
            "primitives": [
                [(0.0, 0.0), (0.0, 64.0), (64.0, 64.0)],
            ],
        }
    ]
}
_ATLASES = {
    9: {
        "width": 128,
        "height": 128,
        "cookedPng": "assets/ui/palantir/atlases/apt-palantir-9-abcdefabcdef.png",
        "sha256": "ab" * 32,
    }
}


def _palantir_with_hidden_piece() -> dict:
    """Root frame 0 places `RadarBacking`, a sprite hidden at frame 0 whose
    `_show` frame (5) places a textured shape."""

    characters = [
        {"kind": "null"},  # 0 unused
        {"kind": "shape", "characterId": 1, "geometryId": 7},
        {
            "kind": "sprite",
            "characterId": 2,
            "frames": [
                [_label("_hide", 0, offset=100)],
                [],
                [],
                [],
                [],
                [
                    _label("_show", 5, offset=110),
                    _place(1, 1, offset=120, translation=(4.0, 6.0)),
                ],
            ],
        },
    ]
    movie = _movie(
        "Palantir",
        characters,
        [
            [
                _place(
                    3,
                    2,
                    offset=10,
                    name="RadarBacking",
                    translation=(100.0, 500.0),
                    matrix=(0.5, 0.0, 0.0, 0.5),
                )
            ]
        ],
        geometry=_TEXTURED_GEOMETRY,
        image_map={3: 9},
        atlases=_ATLASES,
    )
    return {"palantir": movie}


def test_hidden_sprite_piece_ships_labeled_state_art() -> None:
    pieces = _stage_pieces_contract(_palantir_with_hidden_piece())
    assert len(pieces) == 1
    piece = pieces[0]
    assert piece["name"] == "RadarBacking"
    assert piece["labels"] == {"_hide": 0, "_show": 5}
    assert piece["artless"] is False
    assert piece["placement"]["translation"] == [100.0, 500.0]
    hidden, shown = piece["states"]
    assert hidden["labels"] == ["_hide"] and hidden["draws"] == []
    assert shown["firstFrameIndex"] == 5 and shown["labels"] == ["_show"]
    (draw,) = shown["draws"]
    assert draw["kind"] == "textured-triangle"
    assert draw["atlas"].endswith("apt-palantir-9-abcdefabcdef.png")
    # exact affine: piece scale 0.5 applies to the child's (4, 6) offset too:
    # stage origin = 100 + 4*0.5, 500 + 6*0.5.
    assert draw["points"][0] == [102.0, 503.0]
    # and the 64x64 shape spans 32 stage units under the 0.5 piece scale.
    assert draw["points"][2] == [134.0, 535.0]


def test_artless_piece_keeps_named_receipt() -> None:
    characters = [
        {"kind": "null"},
        {"kind": "sprite", "characterId": 1, "frames": [[], []]},
        {
            "kind": "sprite",
            "characterId": 2,
            "frames": [
                [_label("_hide", 0, offset=100)],
                [_place(1, 1, offset=120, name="Anchor")],
            ],
        },
    ]
    movies = {
        "palantir": _movie(
            "Palantir",
            characters,
            [[_place(15, 2, offset=10, name="EmptyGlobe")]],
        )
    }
    (piece,) = _stage_pieces_contract(movies)
    assert piece["artless"] is True
    (state_hidden, state_shown) = piece["states"]
    assert state_shown["receipts"] == [
        {
            "code": "sprite-has-no-populated-frame",
            "movie": "Palantir",
            "characterId": 1,
            "path": "piece:1:Palantir/EmptyGlobe/1:Anchor",
        }
    ]


def test_state_signature_dedupes_pure_tween_motion() -> None:
    characters = [
        {"kind": "null"},
        {"kind": "shape", "characterId": 1, "geometryId": 7},
        {
            "kind": "sprite",
            "characterId": 2,
            "frames": [
                [_place(1, 1, offset=100, translation=(0.0, 0.0))],
                # a MOVE row (flag 0x01, no character bit) only retranslates.
                [
                    {
                        "kind": "place-object",
                        "sourceOffset": 140,
                        "flags": 0x01 | 0x04,
                        "depth": 1,
                        "characterId": -1,
                        "matrix": [1.0, 0.0, 0.0, 1.0],
                        "translation": [9.0, 9.0],
                        "tint": [1.0, 1.0, 1.0, 1.0],
                        "additive": [0.0, 0.0, 0.0, 0.0],
                        "ratio": 0.0,
                        "clipDepth": 0,
                    }
                ],
            ],
        },
    ]
    movies = {
        "palantir": _movie(
            "Palantir",
            characters,
            [[_place(1, 2, offset=10, name="Swirl")]],
            geometry=_TEXTURED_GEOMETRY,
            image_map={3: 9},
            atlases=_ATLASES,
        )
    }
    (piece,) = _stage_pieces_contract(movies)
    # frame 1 only moves the same character: one authored content state.
    assert len(piece["states"]) == 1
    assert piece["states"][0]["firstFrameIndex"] == 0


def test_unresolved_import_and_missing_texture_stay_named_receipts() -> None:
    characters = [
        {"kind": "null"},  # imported from a movie outside the closure
        {"kind": "shape", "characterId": 1, "geometryId": 44},
        {
            "kind": "sprite",
            "characterId": 2,
            "frames": [
                [
                    _place(1, 0, offset=100),
                    _place(2, 1, offset=130),
                ]
            ],
        },
    ]
    movies = {
        "palantir": _movie(
            "Palantir",
            characters,
            [[_place(1, 2, offset=10, name="Ornament")]],
            imports={0: ("MissingMovie", "MissingSymbol")},
            geometry={
                44: [
                    {
                        "style": "tc",
                        "values": [255.0] * 4 + [5.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0],
                        "primitives": [[(0.0, 0.0), (0.0, 1.0), (1.0, 1.0)]],
                    }
                ]
            },
        )
    }
    (piece,) = _stage_pieces_contract(movies)
    (state,) = piece["states"]
    codes = sorted(receipt["code"] for receipt in state["receipts"])
    assert codes == ["texture-assignment-unresolved", "unresolved-import-movie"]
    assert piece["artless"] is True


def _button_record(
    record_index: int,
    states: list[str],
    character_id: int,
    *,
    depth: int,
    translation: tuple[float, float] = (0.0, 0.0),
    matrix: tuple[float, float, float, float] = (1.0, 0.0, 0.0, 1.0),
    color: tuple[float, float, float, float] = (1.0, 1.0, 1.0, 1.0),
) -> dict:
    mask = 0
    for flag, name in ((0x01, "up"), (0x02, "over"), (0x04, "down"), (0x08, "hit")):
        if name in states:
            mask |= flag
    return {
        "recordIndex": record_index,
        "sourceOffset": 900 + record_index * 64,
        "stateMask": mask,
        "states": list(states),
        "characterId": character_id,
        "depth": depth,
        "matrix": list(matrix),
        "translation": list(translation),
        "color": list(color),
        "unknown": [0.0, 0.0, 0.0, 0.0],
    }


def test_button_piece_ships_its_up_state_art() -> None:
    """`PalantirBack` (the rope/ornament behind radar and dish) is a BUTTON
    character; its static art lives in the button's up-state records.  The
    collector flattens exactly the up state — the hit-only record's invisible
    polygon must not become a draw — with record matrix/translation composed
    like any display row."""

    characters = [
        {"kind": "null"},
        {"kind": "shape", "characterId": 1, "geometryId": 7},
        {
            "kind": "button",
            "characterId": 2,
            "records": [
                _button_record(0, ["up", "over", "down"], 1, depth=2, translation=(4.0, 6.0)),
                _button_record(1, ["hit"], 1, depth=3),
            ],
            "actions": [],
        },
    ]
    movies = {
        "palantir": _movie(
            "Palantir",
            characters,
            [
                [
                    _place(
                        7,
                        2,
                        offset=10,
                        name="PalantirBack",
                        translation=(100.0, 500.0),
                        matrix=(0.5, 0.0, 0.0, 0.5),
                    )
                ]
            ],
            geometry=_TEXTURED_GEOMETRY,
            image_map={3: 9},
            atlases=_ATLASES,
        )
    }
    (piece,) = _stage_pieces_contract(movies)
    assert piece["name"] == "PalantirBack"
    assert piece["artless"] is False
    (state,) = piece["states"]
    assert state["receipts"] == []
    # ONE draw: the up-state record only. The hit-only record ships nothing.
    (draw,) = state["draws"]
    assert draw["kind"] == "textured-triangle"
    # exact affine: piece scale 0.5 applies to the record's (4, 6) offset.
    assert draw["points"][0] == [102.0, 503.0]
    assert draw["path"].endswith("/up:0")


def test_button_without_up_state_is_a_named_receipt() -> None:
    characters = [
        {"kind": "null"},
        {"kind": "shape", "characterId": 1, "geometryId": 7},
        {
            "kind": "button",
            "characterId": 2,
            "records": [_button_record(0, ["hit"], 1, depth=2)],
            "actions": [],
        },
    ]
    movies = {
        "palantir": _movie(
            "Palantir",
            characters,
            [[_place(7, 2, offset=10, name="HitOnly")]],
            geometry=_TEXTURED_GEOMETRY,
            image_map={3: 9},
            atlases=_ATLASES,
        )
    }
    (piece,) = _stage_pieces_contract(movies)
    assert piece["artless"] is True
    (state,) = piece["states"]
    assert [receipt["code"] for receipt in state["receipts"]] == [
        "button-has-no-up-state-art"
    ]


def test_compose_authored_transforms_child_translation() -> None:
    parent = _Transform(matrix=(0.5, 0.0, 0.0, 0.25), translation=(10.0, 20.0))
    child = _Transform(matrix=(2.0, 0.0, 0.0, 2.0), translation=(8.0, 4.0))
    composed = _compose_authored(parent, child)
    assert composed.translation == (14.0, 21.0)
    assert composed.matrix == (1.0, 0.0, 0.0, 0.5)
    # sealed combine kept its historical translation semantics; the authored
    # composition is the one that scales child offsets.
    assert parent.combine(child).translation == (18.0, 24.0)


def test_geometry_primitives_solid_rows_match_sealed_schema() -> None:
    movie = SimpleNamespace(
        name="Palantir",
        geometry={
            1: [
                {
                    "style": "s",
                    "values": [255.0, 0.0, 0.0, 255.0],
                    "primitives": [[(0.0, 0.0), (0.0, 1.0), (1.0, 1.0)]],
                }
            ]
        },
        image_map={},
        atlases={},
    )
    rows, receipts = _geometry_primitives(movie, 1, _Transform())
    assert receipts == []
    (row,) = rows
    assert row["kind"] == "solid-triangle"
    assert row["color"] == [1.0, 0.0, 0.0, 1.0]
    assert "displayOrder" not in row and "path" not in row
