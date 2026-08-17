"""The unit-to-block binding converter, exercised on documents that FAIL.

Retail's own 502 object documents are clean: nothing dangles, no block is
authored without a row, no ``ChildObject`` chain loops. That is a good thing
about retail and a bad thing about a test suite, because the paths that NAME a
failure would never run. These tests therefore feed the parser synthetic object
documents in retail's own spelling and assert that each failure is reported by
name rather than smoothed over.

The one property worth more than the rest gets the most tests: a COMMENTED-OUT
line is not a reference. A prior analysis counted retail's 27 commented
auto-resolve lines as live and concluded that 15 leadership blocks dangled. They
do not, and ``test_a_commented_*`` is what keeps that from coming back.

The last test in the file is a REGRESSION PIN against the real catalog. It skips
with a stated reason when the private retail workspace is not present.
"""

from __future__ import annotations

import json
import pathlib
from typing import Any

import pytest

from openbfme_importer import living_world_autoresolve_bindings as bindings

# --- the private retail workspace this pin measures against -------------------

_WANTED = (
    ("the RotWK catalog", pathlib.PurePath("catalog", "rotwk.json")),
    (
        "the auto-resolve bundle",
        pathlib.PurePath("livingworld-autoresolve", "living-world-autoresolve.json"),
    ),
    (
        "the converted living-world document",
        pathlib.PurePath("reports", "rotwk-living-world.json"),
    ),
)


def workspace_root() -> pathlib.Path | None:
    """The nearest ``workspace/retail-work`` that carries ALL THREE inputs.

    A git worktree under ``.claude/worktrees/`` gets its own partial ``workspace``,
    so the search walks upward until it finds one that is complete rather than
    stopping at the first ``workspace`` it sees and skipping for the wrong reason.
    """

    for parent in pathlib.Path(__file__).resolve().parents:
        root = parent / "workspace" / "retail-work"
        if all((root / relative).is_file() for _what, relative in _WANTED):
            return root
    return None


class _Entry:
    def __init__(self, name: str, size: int) -> None:
        self.name = name
        self.archive = "synthetic.big"
        self.offset = 0
        self.size = size
        self.precedence = 0


class _Reader:
    """The catalog surface :func:`build_from_reader` uses, over an in-memory map."""

    def __init__(self, documents: dict[str, str]) -> None:
        self._payloads = {
            key.casefold(): value.encode("latin-1") for key, value in documents.items()
        }
        self._winners = {key: _Entry(key, len(value)) for key, value in self._payloads.items()}

    def resolve(self, virtual_path: str) -> _Entry | None:
        return self._winners.get(virtual_path.casefold())

    def read(self, entry: _Entry) -> bytes:
        return self._payloads[entry.name]


def _autoresolve(**tables: list[str]) -> dict[str, Any]:
    """A minimal document in the sibling converter's schema, declaring names."""

    document: dict[str, Any] = {
        "schema": "openbfme.living-world-autoresolve",
        "schemaVersion": 1,
    }
    for kind in bindings.KINDS:
        document[kind] = {name: {} for name in tables.get(kind, [])}
    return document


def _build(
    text: str,
    *,
    path: str = "data/ini/object/test/synthetic.ini",
    autoresolve: dict[str, Any] | None = None,
    living_world: dict[str, Any] | None = None,
    extra: dict[str, str] | None = None,
) -> dict[str, Any]:
    documents = {path: text}
    documents.update(extra or {})
    return bindings.build_from_reader(
        _Reader(documents),
        autoresolve if autoresolve is not None else _autoresolve(),
        living_world,
    )


# --- comments are not references ---------------------------------------------


def test_a_commented_leadership_is_not_a_reference_in_either_spelling() -> None:
    manifest = _build(
        """
Object Hero_A
\tAutoResolveLeadership = AutoResolve_LurtzBonus
;\tAutoResolveLeadership = AutoResolve_GandalfBonus
//\tAutoResolveLeadership = AutoResolve_ArwenBonus
End
""",
        autoresolve=_autoresolve(leaderships=["AutoResolve_LurtzBonus"]),
    )
    census = manifest["census"]["leaderships"]
    assert census["liveReferences"] == 1
    assert census["liveDistinct"] == 1
    assert census["commentedReferences"] == 2
    assert sorted(census["commentedNames"]) == [
        "AutoResolve_ArwenBonus",
        "AutoResolve_GandalfBonus",
    ]
    # The live one is bound; neither commented one is invented into the object.
    assert manifest["objects"]["Hero_A"]["leadership"] == "AutoResolve_LurtzBonus"
    # And neither commented one dangles, because neither is a reference.
    assert manifest["dangling"]["leaderships"] == []
    assert manifest["danglingIfCommentedCounted"]["leaderships"] == [
        "AutoResolve_ArwenBonus",
        "AutoResolve_GandalfBonus",
    ]


def test_a_trailing_comment_is_not_a_reference_either() -> None:
    manifest = _build(
        """
Object Hero_B
\tAutoResolveBody = AutoResolve_RealBody\t; AutoResolveBody = AutoResolve_GhostBody
\tAutoResolveCombatChain = AutoResolve_RealChain // AutoResolveCombatChain = AutoResolve_GhostChain
End
""",
        autoresolve=_autoresolve(
            bodies=["AutoResolve_RealBody"], combatChains=["AutoResolve_RealChain"]
        ),
    )
    assert manifest["objects"]["Hero_B"]["body"] == "AutoResolve_RealBody"
    assert manifest["objects"]["Hero_B"]["combatChain"] == "AutoResolve_RealChain"
    assert manifest["census"]["bodies"]["commentedNames"] == ["AutoResolve_GhostBody"]
    assert manifest["census"]["combatChains"]["commentedNames"] == ["AutoResolve_GhostChain"]
    assert manifest["dangling"]["bodies"] == []
    assert manifest["dangling"]["combatChains"] == []


def test_a_commented_armor_block_is_scoped_the_way_a_live_one_is() -> None:
    # Retail comments out WHOLE BLOCKS, not just the row. The commented Armor row
    # only counts as a commented reference because its commented AutoResolveArmor
    # opener put it in scope - the same rule the live machine uses.
    manifest = _build(
        """
Object Soldier
\tAutoResolveArmor
\t\tArmor = AutoResolve_LiveArmor
\tEnd
;\tAutoResolveArmor
;\t\tRequiredUpgrades = Upgrade_Heavy
;\t\tArmor = AutoResolve_DeadArmor
;\tEnd
//\tAutoResolveWeapon
//\t\tWeapon = AutoResolve_DeadWeapon
//\tEnd
\tArmorSet
\t\tArmor = SomeRealTimeArmor
\tEnd
End
""",
        autoresolve=_autoresolve(armors=["AutoResolve_LiveArmor"]),
    )
    armors = manifest["census"]["armors"]
    assert armors["liveReferences"] == 1
    assert armors["commentedNames"] == ["AutoResolve_DeadArmor"]
    assert manifest["census"]["weapons"]["commentedNames"] == ["AutoResolve_DeadWeapon"]
    # The RTS-side ArmorSet row is not an auto-resolve reference at all.
    assert [row["block"] for row in manifest["objects"]["Soldier"]["armorSet"]] == [
        "AutoResolve_LiveArmor"
    ]
    assert manifest["dangling"]["armors"] == []
    assert manifest["danglingIfCommentedCounted"]["armors"] == ["AutoResolve_DeadArmor"]


def test_the_two_dangling_pictures_are_both_in_the_manifest_and_differ() -> None:
    manifest = _build(
        """
Object Hero_C
;\tAutoResolveLeadership = AutoResolve_GhostBonus
End
""",
    )
    finding = {entry["id"]: entry for entry in manifest["findings"]}
    assert "commented.lines-are-not-references" in finding
    difference = finding["commented.lines-are-not-references"]["falseDanglingIfCommentedCounted"]
    assert difference == {"leaderships": ["AutoResolve_GhostBonus"]}
    assert manifest["totals"]["dangling"] == 0
    assert manifest["totals"]["danglingIfCommentedCounted"] == 1


# --- inheritance ---------------------------------------------------------------


def test_a_childobject_inherits_its_parents_binding() -> None:
    manifest = _build(
        """
Object Parent
\tAutoResolveUnitType = AutoResolveUnit_Soldier
\tAutoResolveBody = AutoResolve_ParentBody
\tAutoResolveArmor
\t\tArmor = AutoResolve_ParentArmor
\tEnd
End

ChildObject Child Parent
\tDisplayName = OBJECT:Child
End
""",
        autoresolve=_autoresolve(
            bodies=["AutoResolve_ParentBody"], armors=["AutoResolve_ParentArmor"]
        ),
    )
    child = manifest["objects"]["Child"]
    assert child["body"] == "AutoResolve_ParentBody"
    assert child["unitType"] == "AutoResolveUnit_Soldier"
    assert [row["block"] for row in child["armorSet"]] == ["AutoResolve_ParentArmor"]
    assert child["inheritedFrom"] == ["Parent"]
    # The inherited binding is NOT counted a second time in the census: the
    # reference lives in the parent's document, once.
    assert manifest["census"]["bodies"]["liveReferences"] == 1
    assert manifest["totals"]["objectsWithADirectAutoResolveField"] == 1
    assert manifest["totals"]["objectsBoundAfterInheritance"] == 2


def test_a_childobject_field_overrides_the_one_it_inherits() -> None:
    manifest = _build(
        """
Object Parent
\tAutoResolveBody = AutoResolve_ParentBody
\tAutoResolveCombatChain = AutoResolve_Chain
\tAutoResolveArmor
\t\tArmor = AutoResolve_ParentArmor
\tEnd
End

ChildObject Child Parent
\tAutoResolveBody = AutoResolve_ChildBody
\tAutoResolveArmor
\t\tArmor = AutoResolve_ChildArmor
\tEnd
End
""",
        autoresolve=_autoresolve(
            bodies=["AutoResolve_ParentBody", "AutoResolve_ChildBody"],
            combatChains=["AutoResolve_Chain"],
            armors=["AutoResolve_ParentArmor", "AutoResolve_ChildArmor"],
        ),
    )
    child = manifest["objects"]["Child"]
    assert child["body"] == "AutoResolve_ChildBody"
    # Not restated, so still inherited.
    assert child["combatChain"] == "AutoResolve_Chain"
    # A restated armour SET replaces the inherited set wholesale; it does not
    # append to it, because retail evaluates one ordered list.
    assert [row["block"] for row in child["armorSet"]] == ["AutoResolve_ChildArmor"]


def test_inheritance_is_transitive_across_several_levels_and_across_documents() -> None:
    manifest = _build(
        """
Object Grandparent
\tAutoResolveUnitType = AutoResolveUnit_Cavalry
\tAutoResolveBody = AutoResolve_GrandBody
End
""",
        extra={
            "data/ini/object/test/second.ini": """
ChildObject Parent Grandparent
\tAutoResolveCombatChain = AutoResolve_Chain
End

ChildObject Child Parent
\tAutoResolveLeadership = AutoResolve_Bonus
End
""",
        },
        autoresolve=_autoresolve(
            bodies=["AutoResolve_GrandBody"],
            combatChains=["AutoResolve_Chain"],
            leaderships=["AutoResolve_Bonus"],
        ),
    )
    child = manifest["objects"]["Child"]
    assert child["unitType"] == "AutoResolveUnit_Cavalry"
    assert child["body"] == "AutoResolve_GrandBody"
    assert child["combatChain"] == "AutoResolve_Chain"
    assert child["leadership"] == "AutoResolve_Bonus"
    # Nearest ancestor first, and only ancestors that actually contributed.
    assert child["inheritedFrom"] == ["Parent", "Grandparent"]
    assert child["sourceFile"] == "data/ini/object/test/second.ini"


def test_an_ancestor_that_contributes_nothing_is_not_named_in_the_chain() -> None:
    manifest = _build(
        """
Object Grandparent
\tAutoResolveBody = AutoResolve_GrandBody
End

ChildObject Parent Grandparent
\tDisplayName = OBJECT:Parent
End

ChildObject Child Parent
\tDisplayName = OBJECT:Child
End
""",
        autoresolve=_autoresolve(bodies=["AutoResolve_GrandBody"]),
    )
    assert manifest["objects"]["Child"]["inheritedFrom"] == ["Grandparent"]


def test_an_inheritance_cycle_is_reported_by_name_and_never_looped_on() -> None:
    manifest = _build(
        """
ChildObject Ouroboros_A Ouroboros_B
\tAutoResolveBody = AutoResolve_Body
End

ChildObject Ouroboros_B Ouroboros_A
\tAutoResolveCombatChain = AutoResolve_Chain
End
""",
        autoresolve=_autoresolve(
            bodies=["AutoResolve_Body"], combatChains=["AutoResolve_Chain"]
        ),
    )
    cycles = manifest["unresolved"]["inheritanceCycles"]
    assert cycles == ["Ouroboros_A", "Ouroboros_B"]
    reasons = {gap["reason"]: gap for gap in manifest["gaps"]}
    assert "childobject-inheritance-cycle" in reasons
    assert "Ouroboros_A" in reasons["childobject-inheritance-cycle"]["detail"]
    assert "Ouroboros_B" in reasons["childobject-inheritance-cycle"]["detail"]
    # The walk TERMINATED: each object keeps what it authored plus one honest
    # level of inheritance, and the leg that would have recursed forever is
    # dropped and named rather than followed.
    assert manifest["objects"]["Ouroboros_A"]["body"] == "AutoResolve_Body"
    assert manifest["objects"]["Ouroboros_A"]["combatChain"] == "AutoResolve_Chain"
    assert manifest["objects"]["Ouroboros_A"]["inheritedFrom"] == ["Ouroboros_B"]
    assert manifest["objects"]["Ouroboros_B"]["inheritedFrom"] == ["Ouroboros_A"]


def test_a_childobject_whose_parent_is_not_declared_is_named_not_guessed() -> None:
    manifest = _build(
        """
ChildObject Orphaned SomeParentNobodyDeclares
\tAutoResolveBody = AutoResolve_Body
End
""",
        autoresolve=_autoresolve(bodies=["AutoResolve_Body"]),
    )
    assert manifest["unresolved"]["childObjectsWithAnUndeclaredParent"] == ["Orphaned"]
    detail = next(
        gap["detail"]
        for gap in manifest["gaps"]
        if gap["reason"] == "childobject-parent-not-declared"
    )
    assert "SomeParentNobodyDeclares" in detail


# --- the repeated armour and weapon blocks --------------------------------------


def test_an_armor_block_with_no_armor_row_is_reported_not_dropped() -> None:
    manifest = _build(
        """
Object Soldier
\tAutoResolveArmor
\t\tRequiredUpgrades = Upgrade_Heavy
\tEnd
\tAutoResolveArmor
\t\tArmor = AutoResolve_Real
\tEnd
End
""",
        autoresolve=_autoresolve(armors=["AutoResolve_Real"]),
    )
    empty = manifest["unresolved"]["autoResolveBlocksWithoutARow"]
    assert len(empty) == 1
    assert empty[0].startswith("Soldier (")
    gap = next(
        gap for gap in manifest["gaps"] if gap["reason"] == "autoresolve-block-without-a-row"
    )
    assert gap["subject"] == "Soldier"
    assert "AutoResolveArmor" in gap["detail"]
    # The row-less block contributes NO reference and no invented armour.
    assert [row["block"] for row in manifest["objects"]["Soldier"]["armorSet"]] == [
        "AutoResolve_Real"
    ]
    assert manifest["census"]["armors"]["liveReferences"] == 1


def test_upgrade_gates_carry_several_tokens_or_none_at_all() -> None:
    manifest = _build(
        """
Object Soldier
\tAutoResolveArmor
\t\tRequiredUpgrades = Upgrade_A Upgrade_B
\t\tExcludedUpgrades = Upgrade_C Upgrade_D Upgrade_E
\t\tArmor = AutoResolve_Gated
\tEnd
\tAutoResolveArmor
\t\tArmor = AutoResolve_Ungated
\tEnd
End
""",
        autoresolve=_autoresolve(armors=["AutoResolve_Gated", "AutoResolve_Ungated"]),
    )
    gated, ungated = manifest["objects"]["Soldier"]["armorSet"]
    assert gated == {
        "block": "AutoResolve_Gated",
        "requiredUpgrades": ["Upgrade_A", "Upgrade_B"],
        "excludedUpgrades": ["Upgrade_C", "Upgrade_D", "Upgrade_E"],
    }
    assert ungated == {
        "block": "AutoResolve_Ungated",
        "requiredUpgrades": [],
        "excludedUpgrades": [],
    }


def test_block_order_within_an_object_is_retails_own_order() -> None:
    # Load-bearing: retail evaluates these rows top to bottom, so the heavy-armour
    # row authored FIRST must stay first even though it sorts second by name.
    manifest = _build(
        """
Object Soldier
\tAutoResolveArmor
\t\tArmor = AutoResolve_Zulu
\tEnd
\tAutoResolveArmor
\t\tArmor = AutoResolve_Alpha
\tEnd
\tAutoResolveArmor
\t\tArmor = AutoResolve_Mike
\tEnd
\tAutoResolveWeapon
\t\tWeapon = AutoResolve_Zulu
\tEnd
\tAutoResolveWeapon
\t\tWeapon = AutoResolve_Alpha
\tEnd
End
""",
        autoresolve=_autoresolve(
            armors=["AutoResolve_Zulu", "AutoResolve_Alpha", "AutoResolve_Mike"],
            weapons=["AutoResolve_Zulu", "AutoResolve_Alpha"],
        ),
    )
    soldier = manifest["objects"]["Soldier"]
    assert [row["block"] for row in soldier["armorSet"]] == [
        "AutoResolve_Zulu",
        "AutoResolve_Alpha",
        "AutoResolve_Mike",
    ]
    assert [row["block"] for row in soldier["weaponSet"]] == [
        "AutoResolve_Zulu",
        "AutoResolve_Alpha",
    ]


# --- dangling, orphans and unit types --------------------------------------------


def test_a_block_the_bundle_never_declares_dangles_by_name_with_no_substitute() -> None:
    manifest = _build(
        """
Object Soldier
\tAutoResolveBody = AutoResolve_NoSuchBody
\tAutoResolveArmor
\t\tArmor = AutoResolve_NoSuchArmor
\tEnd
End
""",
        autoresolve=_autoresolve(
            bodies=["AutoResolve_DefaultBody"], armors=["AutoResolve_DefaultArmor"]
        ),
    )
    assert manifest["dangling"]["bodies"] == ["AutoResolve_NoSuchBody"]
    assert manifest["dangling"]["armors"] == ["AutoResolve_NoSuchArmor"]
    # The name is carried verbatim; the documented fallback is NOT put in its place.
    assert manifest["objects"]["Soldier"]["body"] == "AutoResolve_NoSuchBody"
    assert [row["block"] for row in manifest["objects"]["Soldier"]["armorSet"]] == [
        "AutoResolve_NoSuchArmor"
    ]


def test_the_documented_fallbacks_are_split_out_of_the_orphan_lists() -> None:
    manifest = _build(
        """
Object Soldier
\tAutoResolveBody = AutoResolve_UsedBody
End
""",
        autoresolve=_autoresolve(
            bodies=["AutoResolve_UsedBody", "AutoResolve_UnusedBody", "AutoResolve_DefaultBody"]
        ),
    )
    assert manifest["orphans"]["bodies"] == ["AutoResolve_DefaultBody", "AutoResolve_UnusedBody"]
    assert manifest["orphansExcludingDocumentedFallbacks"]["bodies"] == [
        "AutoResolve_UnusedBody"
    ]
    assert manifest["documentedFallbacks"]["bodies"] == "AutoResolve_DefaultBody"


def test_a_unit_type_alone_is_authored_data_but_binds_no_block() -> None:
    manifest = _build(
        """
Object Peasants
\tAutoResolveUnitType = AutoResolveUnit_Soldier
End
"""
    )
    assert manifest["unitTypes"] == ["AutoResolveUnit_Soldier"]
    assert manifest["unresolved"]["objectsCarryingAUnitTypeButNoBlock"] == ["Peasants"]
    assert manifest["totals"]["objectsBoundAfterInheritance"] == 0
    assert manifest["totals"]["objectsWithAutoResolveDataAfterInheritance"] == 1


def test_an_objectreskin_is_not_admitted_as_an_object_template() -> None:
    # ObjectReskin restates art, never auto-resolve data. Admitting the reskins
    # would silently change every object count in the manifest.
    manifest = _build(
        """
Object Real
\tAutoResolveBody = AutoResolve_Body
End

ObjectReskin RealSummer Real ReskinArt
\tDisplayName = OBJECT:Real
End
""",
        autoresolve=_autoresolve(bodies=["AutoResolve_Body"]),
    )
    assert manifest["totals"]["objectsParsed"] == 1
    assert "RealSummer" not in manifest["objects"]


# --- coverage ---------------------------------------------------------------------


def test_coverage_names_the_unbound_templates_with_the_distinguishing_reason() -> None:
    manifest = _build(
        """
Object BoundUnit
\tAutoResolveBody = AutoResolve_Body
End

Object UnboundUnit
\tDisplayName = OBJECT:UnboundUnit
End
""",
        autoresolve=_autoresolve(bodies=["AutoResolve_Body"]),
        living_world={
            "playerArmies": [
                {
                    "name": "Army",
                    "entries": [
                        {"thingTemplate": "BoundUnit", "quantity": 1},
                        {"thingTemplate": "UnboundUnit", "quantity": 1},
                        {"thingTemplate": "NoSuchObject", "quantity": 1},
                    ],
                }
            ]
        },
    )
    coverage = manifest["coverage"]
    assert coverage["present"] is True
    assert coverage["totals"] == {"thingTemplates": 3, "bound": 1, "unbound": 2}
    reasons = {row["thingTemplate"]: row["reason"] for row in coverage["unbound"]}
    assert reasons == {
        "NoSuchObject": "object not declared",
        "UnboundUnit": "object declared but carries no auto-resolve binding",
    }


def test_coverage_is_absent_with_a_reason_rather_than_an_empty_section() -> None:
    manifest = _build("Object Nothing\nEnd\n")
    assert manifest["coverage"] == {
        "present": False,
        "reason": "no --living-world document was given",
    }


# --- refusals and determinism ------------------------------------------------------


def test_an_autoresolve_document_of_the_wrong_schema_is_a_refusal() -> None:
    with pytest.raises(bindings.AutoResolveBindingsError) as raised:
        bindings.build_from_reader(
            _Reader({"data/ini/object/test/a.ini": ""}), {"schema": "something.else"}
        )
    assert "openbfme.living-world-autoresolve" in str(raised.value)


def test_a_catalog_with_no_object_documents_is_a_refusal_not_an_empty_manifest() -> None:
    with pytest.raises(bindings.AutoResolveBindingsError) as raised:
        bindings.build_from_reader(_Reader({"data/ini/weapon.ini": ""}), _autoresolve())
    assert "data/ini/object/" in str(raised.value)


def test_converting_twice_yields_byte_identical_json(tmp_path: pathlib.Path) -> None:
    text = """
Object Parent
\tAutoResolveUnitType = AutoResolveUnit_Archer
\tAutoResolveBody = AutoResolve_Body
\tAutoResolveArmor
\t\tRequiredUpgrades = Upgrade_A
\t\tArmor = AutoResolve_Zulu
\tEnd
\tAutoResolveArmor
\t\tArmor = AutoResolve_Alpha
\tEnd
End

ChildObject Child Parent
\tAutoResolveLeadership = AutoResolve_Bonus
End
"""
    document = _autoresolve(
        bodies=["AutoResolve_Body"],
        armors=["AutoResolve_Zulu", "AutoResolve_Alpha", "AutoResolve_Unused"],
        leaderships=["AutoResolve_Bonus"],
    )
    first = tmp_path / "a" / bindings.MANIFEST_NAME
    second = tmp_path / "b" / bindings.MANIFEST_NAME
    for target in (first, second):
        manifest = bindings.build_from_reader(
            _Reader({"data/ini/object/test/synthetic.ini": text}), document
        )
        bindings.write_json_atomic(target, manifest)
    assert first.read_bytes() == second.read_bytes()
    reloaded = json.loads(first.read_text(encoding="utf-8"))
    assert reloaded["schema"] == bindings.SCHEMA
    assert reloaded["schemaVersion"] == bindings.SCHEMA_VERSION


# --- the regression pin against the real catalog -------------------------------------


@pytest.fixture(scope="module")
def real_manifest() -> dict[str, Any]:
    root = workspace_root()
    if root is None:
        pytest.skip(
            "no workspace/retail-work above this file carries all of "
            + ", ".join(str(relative) for _what, relative in _WANTED)
            + "; this pin measures a private retail workspace and cannot be "
            "reconstructed from the repository"
        )
    from openbfme_importer.livingmap_bundle import CatalogReader

    catalog, autoresolve, living_world = (root / relative for _what, relative in _WANTED)
    return bindings.build_from_reader(
        CatalogReader(catalog),
        json.loads(autoresolve.read_text(encoding="utf-8")),
        json.loads(living_world.read_text(encoding="utf-8")),
    )


def test_pin_the_real_catalogs_object_documents_and_object_counts(
    real_manifest: dict[str, Any]
) -> None:
    totals = real_manifest["totals"]
    assert totals["objectDocuments"] == 502
    assert totals["objectsParsed"] == 3846
    assert totals["objectsWithADirectAutoResolveField"] == 129
    assert totals["objectsBoundAfterInheritance"] == 261


def test_pin_the_real_catalogs_live_and_commented_census(
    real_manifest: dict[str, Any]
) -> None:
    census = real_manifest["census"]
    measured = {
        kind: (
            census[kind]["liveReferences"],
            census[kind]["liveDistinct"],
            census[kind]["liveFiles"],
        )
        for kind in bindings.KINDS
    }
    assert measured == {
        "armors": (164, 118, 78),
        "weapons": (177, 128, 78),
        "bodies": (128, 91, 79),
        "combatChains": (119, 6, 70),
        "leaderships": (20, 11, 20),
    }
    commented = {
        kind: (census[kind]["commentedReferences"], census[kind]["commentedDistinct"])
        for kind in bindings.KINDS
    }
    assert commented == {
        "armors": (3, 3),
        "weapons": (3, 3),
        "bodies": (1, 1),
        "combatChains": (1, 1),
        "leaderships": (19, 15),
    }


def test_pin_that_nothing_dangles_once_comments_are_excluded(
    real_manifest: dict[str, Any]
) -> None:
    assert {kind: real_manifest["dangling"][kind] for kind in bindings.KINDS} == {
        kind: [] for kind in bindings.KINDS
    }
    # And exactly what the miscount would have invented, by name.
    assert real_manifest["totals"]["danglingIfCommentedCounted"] == 15
    assert real_manifest["danglingIfCommentedCounted"]["leaderships"] == [
        "AutoResolve_ArwenBonus",
        "AutoResolve_DrogothBonus",
        "AutoResolve_EowynBonus",
        "AutoResolve_GandalfBonus",
        "AutoResolve_GimliBonus",
        "AutoResolve_GloinBonus",
        "AutoResolve_GlorfindelBonus",
        "AutoResolve_KarshBonus",
        "AutoResolve_LegolasBonus",
        "AutoResolve_MouthOfSauronBonus",
        "AutoResolve_NazgulBonus",
        "AutoResolve_SarumanBonus",
        "AutoResolve_ShelobBonus",
        "AutoResolve_ThranduilBonus",
        "AutoResolve_WormtongueBonus",
    ]
    for kind in bindings.KINDS:
        if kind == "leaderships":
            continue
        assert real_manifest["danglingIfCommentedCounted"][kind] == []


def test_pin_the_real_catalogs_orphans(real_manifest: dict[str, Any]) -> None:
    orphans = {kind: len(real_manifest["orphans"][kind]) for kind in bindings.KINDS}
    assert orphans == {
        "armors": 13,
        "weapons": 22,
        "bodies": 11,
        "combatChains": 3,
        "leaderships": 2,
    }
    assert real_manifest["totals"]["declaredBlocks"] == {
        "armors": 131,
        "weapons": 150,
        "bodies": 102,
        "combatChains": 9,
        "leaderships": 13,
    }
    assert real_manifest["totals"]["orphans"] == 51
    assert real_manifest["totals"]["orphansExcludingDocumentedFallbacks"] == 47


def test_pin_the_angmar_finding(real_manifest: dict[str, Any]) -> None:
    finding = next(
        entry
        for entry in real_manifest["findings"]
        if entry["id"] == "angmar.binds-blocks-authored-for-other-factions"
    )
    assert [
        path.rsplit("/", 1)[-1]
        for path in finding["angmarObjectDocumentsWithLiveBindings"]
    ] == [
        "angmarhordes.ini",
        "angmarfortress.ini",
        "angmarhwaldir.ini",
        "angmarkarsh.ini",
        "angmarmorgramir.ini",
        "angmarrogash.ini",
        "angmarthrallmaster.ini",
        "angmartrollsling.ini",
        "angmarwitchking.ini",
    ]
    # They bind Gondor-authored numbers, by name.
    assert "AutoResolve_GondorSoldierArmor" in finding["blocksReferencedThatAreNotAngmarNamed"]
    assert "AutoResolve_GondorFighterHordeBody" in finding["blocksReferencedThatAreNotAngmarNamed"]
    # And they reach NONE of the documented fallbacks.
    assert finding["documentedFallbacksReferenced"] == []
    # While the Angmar-NAMED armour, weapon and body blocks are orphans.
    assert finding["angmarNamedBlocksOrphaned"]["armors"]
    assert finding["angmarNamedBlocksOrphaned"]["weapons"]
    assert finding["angmarNamedBlocksOrphaned"]["bodies"]


def test_pin_the_strategic_coverage(real_manifest: dict[str, Any]) -> None:
    coverage = real_manifest["coverage"]
    assert coverage["totals"] == {"thingTemplates": 105, "bound": 100, "unbound": 5}
    assert {row["thingTemplate"]: row["reason"] for row in coverage["unbound"]} == {
        "DainPlayerArmy": "object not declared",
        "RohanFrodo": "object declared but carries no auto-resolve binding",
        "RohanMerry": "object declared but carries no auto-resolve binding",
        "RohanPippin": "object declared but carries no auto-resolve binding",
        "RohanSam": "object declared but carries no auto-resolve binding",
    }
