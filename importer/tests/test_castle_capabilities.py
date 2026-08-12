"""Per-map castle/siege capability derivation (lane L1).

The retail object index registers every ``Object``, ``ChildObject`` and
``ObjectReskin`` definition site under ``data/ini/object/**`` and resolves
``ChildObject``/``ObjectReskin`` parent inheritance.  Joining that index
against a compiled map's placed ``typeName`` rows derives the per-map
capability set that replaces the old one-size-fits-all five-blocker seal
(docs/castle-siege-design.md, sections 1.2 and 5/L1).

Detection keys are retail-authored data, not heuristics:

- gate          = ``BLOCKING_GATE``/``WALL_GATE`` KindOf or a
                  ``GateOpenAndCloseBehavior`` module
- garrison      = a ``HordeGarrisonContain``/``GarrisonContain`` module
                  (NEVER the ``GARRISON`` KindOf:
                  retail's ``Erebortower2`` carries the KindOf and an
                  exit-garrison command set but authors no contain module and
                  can never hold anyone; and never a bare ``*Contain`` suffix
                  either: keeps and thrones author
                  ``CitadelSlaughterHordeContain``, which is not a garrison)
- walk-on-top   = ``WALK_ON_TOP_OF_WALL`` KindOf
- scaleable     = ``SCALEABLE_WALL`` KindOf
- wall-mounted  = ``WALL_UPGRADE``/``WALL_HUB`` KindOf
"""

from __future__ import annotations

import pytest

from openbfme_importer.castle_capabilities import (
    CAPABILITY_VOCABULARY,
    CastleCapabilityError,
    build_retail_object_index,
    castle_siege_contract_v2,
    derive_map_capabilities,
    validate_capability_subset,
)


_DOCUMENTS = {
    "data/ini/object/test/walls.ini": b"""
Object TestWalkableWall
  KindOf = STRUCTURE IMMOBILE WALK_ON_TOP_OF_WALL SCALEABLE_WALL
End

Object TestGateParent
  KindOf = STRUCTURE IMMOBILE BLOCKING_GATE WALL_GATE
  Behavior = GateOpenAndCloseBehavior ModuleTag_GATE
    OpenByDefault = No
  End
End

ChildObject TestInheritedGate TestGateParent
End

ChildObject TestRedaclaredGate TestGateParent
  KindOf = STRUCTURE IMMOBILE BLOCKING_GATE WALL_GATE CAN_ATTACK
End

ObjectReskin TestReskinWall TestWalkableWall
End

Object TestBehaviorOnlyGate
  KindOf = STRUCTURE IMMOBILE
  Behavior = GateOpenAndCloseBehavior ModuleTag_GATE
    OpenByDefault = Yes
  End
End

Object TestGarrisonTower
  KindOf = STRUCTURE IMMOBILE GARRISON
  Behavior = HordeGarrisonContain ModuleTag_hordeGarrison
    ContainMax = 3
  End
End

Object TestFakeGarrison
  KindOf = STRUCTURE IMMOBILE GARRISON
  Behavior = CastleBehavior ModuleTag_castle
  End
End

Object TestCitadelKeep
  KindOf = STRUCTURE IMMOBILE GARRISON
  Behavior = CitadelSlaughterHordeContain ModuleTag_slaughter
    ContainMax = 1
  End
End

Object TestWallUpgrade
  KindOf = STRUCTURE IMMOBILE WALL_UPGRADE
End

Object TestWallHub
  KindOf = STRUCTURE IMMOBILE WALL_HUB
End

Object TestPlainScenery
  KindOf = STRUCTURE IMMOBILE
End
""",
    # Documents outside data/ini/object/** must not contribute definitions.
    "data/ini/gamedata.ini": b"""
Object NotAnObjectCorpusRow
  KindOf = STRUCTURE
End
""",
}


def test_vocabulary_is_the_six_named_capabilities_in_canonical_order() -> None:
    assert CAPABILITY_VOCABULARY == (
        "walkable-walls",
        "scaleable-walls",
        "defendable-gates",
        "wall-garrisons",
        "wall-mounted-defenses",
        "skirmish-ai-libraries",
    )


def test_index_registers_all_three_definition_keywords() -> None:
    index = build_retail_object_index(_DOCUMENTS)
    assert index["testwalkablewall"].definition == "Object"
    assert index["testinheritedgate"].definition == "ChildObject"
    assert index["testreskinwall"].definition == "ObjectReskin"
    assert "notanobjectcorpusrow" not in index


def test_index_preserves_authored_name_case_and_source() -> None:
    index = build_retail_object_index(_DOCUMENTS)
    assert index["testwalkablewall"].name == "TestWalkableWall"
    assert index["testwalkablewall"].source_virtual_path == (
        "data/ini/object/test/walls.ini"
    )


def test_childobject_inherits_parent_kindof_and_modules() -> None:
    info = build_retail_object_index(_DOCUMENTS)["testinheritedgate"]
    assert "BLOCKING_GATE" in info.kind_of
    assert "WALL_GATE" in info.kind_of
    assert "GateOpenAndCloseBehavior" in info.behaviors


def test_childobject_own_kindof_replaces_parent() -> None:
    # The Carn Dum pattern: the ChildObject re-declares its full KindOf line
    # directly, so no parent token leaks in and nothing is lost either.
    info = build_retail_object_index(_DOCUMENTS)["testredaclaredgate"]
    assert "CAN_ATTACK" in info.kind_of
    assert "BLOCKING_GATE" in info.kind_of


def test_objectreskin_inherits_parent_surface() -> None:
    info = build_retail_object_index(_DOCUMENTS)["testreskinwall"]
    assert "WALK_ON_TOP_OF_WALL" in info.kind_of
    assert "SCALEABLE_WALL" in info.kind_of


def test_unresolved_parent_fails_closed() -> None:
    documents = {
        "data/ini/object/test/broken.ini": b"""
ChildObject TestOrphan TestMissingParent
  KindOf = STRUCTURE
End
"""
    }
    with pytest.raises(CastleCapabilityError, match="TestMissingParent"):
        build_retail_object_index(documents)


def test_plain_object_editor_annotation_is_not_a_parent() -> None:
    # Retail demo leftovers author ``Object MoriaDebrisPileA (Rocks)`` and
    # ``Object Water Plane``: on a plain ``Object`` header the second token is
    # an editor annotation, never a template.  Exactly three such rows exist in
    # the corpus, all plain ``Object``; ``ChildObject``/``ObjectReskin``
    # parents still fail closed when unresolvable (test above).
    documents = {
        "data/ini/object/test/demo.ini": b"""
Object TestWater Plane
  KindOf = INERT
End
"""
    }
    index = build_retail_object_index(documents)
    assert index["testwater"].definition == "Object"
    assert index["testwater"].kind_of == ("INERT",)


def test_empty_corpus_fails_closed() -> None:
    with pytest.raises(CastleCapabilityError):
        build_retail_object_index({})


def test_garrison_detection_keys_on_the_contain_module_never_the_kindof() -> None:
    index = build_retail_object_index(_DOCUMENTS)
    derivation = derive_map_capabilities(
        index, ["TestGarrisonTower", "TestFakeGarrison"]
    )
    assert derivation.garrisons == 1


def test_citadel_slaughter_contain_is_not_a_garrison() -> None:
    # Every castle-map keep/throne/stronghold authors CitadelSlaughterHordeContain
    # (WOR_EreborThrone, AngForStronghold, ...).  A bare ``*Contain`` suffix rule
    # overcounts each of those as a garrison; the real garrison modules are
    # HordeGarrisonContain and GarrisonContain only.
    index = build_retail_object_index(_DOCUMENTS)
    derivation = derive_map_capabilities(index, ["TestCitadelKeep"])
    assert derivation.garrisons == 0


def test_gate_detection_uses_kindof_or_the_gate_module() -> None:
    index = build_retail_object_index(_DOCUMENTS)
    derivation = derive_map_capabilities(
        index, ["TestInheritedGate", "TestBehaviorOnlyGate"]
    )
    assert derivation.gates == 2


def test_derivation_counts_placements_not_unique_types() -> None:
    index = build_retail_object_index(_DOCUMENTS)
    derivation = derive_map_capabilities(
        index, ["TestWalkableWall"] * 7 + ["TestWallUpgrade", "TestWallHub"]
    )
    assert derivation.walk_on_top == 7
    assert derivation.scaleable == 7
    assert derivation.wall_mounted == 2


def test_derivation_is_case_insensitive_and_lists_unresolved_uniquely() -> None:
    index = build_retail_object_index(_DOCUMENTS)
    derivation = derive_map_capabilities(
        index,
        [
            "testwalkablewall",
            "SomeMapOnlyProp",
            "somemaponlyprop",
            "AnotherProp",
        ],
    )
    assert derivation.walk_on_top == 1
    assert derivation.unresolved == ("AnotherProp", "SomeMapOnlyProp")


def test_derivation_reports_no_false_capabilities_on_plain_scenery() -> None:
    index = build_retail_object_index(_DOCUMENTS)
    derivation = derive_map_capabilities(index, ["TestPlainScenery"])
    assert derivation.gates == 0
    assert derivation.garrisons == 0
    assert derivation.walk_on_top == 0
    assert derivation.scaleable == 0
    assert derivation.wall_mounted == 0
    assert derivation.required == ()


def test_required_capabilities_follow_the_vocabulary_order() -> None:
    index = build_retail_object_index(_DOCUMENTS)
    derivation = derive_map_capabilities(
        index,
        [
            "TestGarrisonTower",
            "TestInheritedGate",
            "TestWalkableWall",
            "TestWallUpgrade",
        ],
    )
    assert derivation.required == (
        "walkable-walls",
        "scaleable-walls",
        "defendable-gates",
        "wall-garrisons",
        "wall-mounted-defenses",
    )


def test_validate_capability_subset_accepts_canonical_subsets() -> None:
    assert validate_capability_subset(["walkable-walls", "defendable-gates"]) == (
        "walkable-walls",
        "defendable-gates",
    )


@pytest.mark.parametrize(
    "required",
    (
        [],
        ["not-a-capability"],
        ["defendable-gates", "walkable-walls"],  # canonical order violated
        ["walkable-walls", "walkable-walls"],
        ["walkable-walls", 17],
    ),
)
def test_validate_capability_subset_fails_closed(required: object) -> None:
    with pytest.raises(CastleCapabilityError):
        validate_capability_subset(required)


def test_castle_siege_contract_v2_shape() -> None:
    contract = castle_siege_contract_v2(["defendable-gates", "wall-garrisons"])
    assert contract == {
        "version": 2,
        "family": "retail-castle-siege-skirmish",
        "gameplayStatus": "blocked-named-gaps",
        "admissionPolicy": "document-loadable-lobby-visible-gameplay-fails-closed",
        "required": ["defendable-gates", "wall-garrisons"],
    }


def test_castle_siege_contract_v2_rejects_a_noncanonical_required() -> None:
    with pytest.raises(CastleCapabilityError):
        castle_siege_contract_v2(["wall-garrisons", "defendable-gates"])
