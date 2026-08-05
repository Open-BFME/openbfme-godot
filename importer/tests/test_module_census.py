"""Tests for the retail module-type census and its coverage report.

The census is the denominator for behaviour-coverage work, so these tests are
about honesty: counting, dedup, classification completeness, refusals never
counted as consumption, and byte-identical regeneration.
"""

from __future__ import annotations

import json
import subprocess
import sys
import textwrap
from pathlib import Path

import pytest

from openbfme_importer import sage_cst
from openbfme_importer.catalog import CatalogEntry
from openbfme_importer.paths import default_state_root
from openbfme_importer.module_census import (
    MODULE_CARRIER_KEYS,
    MODULE_CLASS_MEANINGS,
    MODULE_CLASSIFICATION,
    ModuleCensusError,
    ModuleSupport,
    build_module_census,
    census_catalog_paths,
    census_json_bytes,
    collect_tree_usage,
    effective_ini_entries,
    generate_retail_module_census,
    module_kind_from_assignment,
    object_family,
    scan_module_support,
)

ROOT = Path(__file__).resolve().parents[2]
CENSUS_PATH = ROOT / "game" / "data" / "retail_module_census.json"
REPORT_TOOL = ROOT / "tools" / "module-coverage-report.py"

_RETAIL_STATE_ROOT = default_state_root()
_RETAIL_CATALOGS = census_catalog_paths(_RETAIL_STATE_ROOT)
_RETAIL_AVAILABLE = all(path.is_file() for path in _RETAIL_CATALOGS.values())


def _committed_census() -> dict:
    return json.loads(CENSUS_PATH.read_text(encoding="utf-8"))


# ---------------------------------------------------------------------------
# Module declaration recognition
# ---------------------------------------------------------------------------


def test_module_carrier_keys_match_the_cst_parser() -> None:
    """The census and the CST parser must agree on what declares a module."""

    assert MODULE_CARRIER_KEYS == sage_cst._MODULE_CARRIERS


@pytest.mark.parametrize(
    ("key", "value", "expected"),
    [
        ("Behavior", "AutoHealBehavior ModuleTag_08", "AutoHealBehavior"),
        ("Body", "ActiveBody ModuleTag_02", "ActiveBody"),
        ("Draw", "W3DScriptedModelDraw ModuleTag_01", "W3DScriptedModelDraw"),
        # Retail double-equals typo: ``Draw = = W3DScriptedModelDraw ...``.
        ("Draw", "= W3DScriptedModelDraw ModuleTag_01", "W3DScriptedModelDraw"),
        # Carrier keys count even without a ModuleTag token.
        ("behavior", "PhysicsBehavior", "PhysicsBehavior"),
        # Non-carrier keys count only with the ModuleTag shape.
        ("SomethingElse", "FancyModule ModuleTag_77", "FancyModule"),
        ("SomethingElse", "FancyModule OtherToken", None),
        ("MaxHealth", "1200", None),
        ("Draw", "=", None),
        ("Behavior", "= = =", None),
    ],
)
def test_module_kind_from_assignment(key: str, value: str, expected: str | None) -> None:
    assert module_kind_from_assignment(key, value) == expected


def test_object_family_labels() -> None:
    assert object_family("data/ini/object/goodfaction/units/men/x.ini") == (
        "object/goodfaction"
    )
    assert object_family("data/ini/default/object.ini") == "default"
    assert object_family("data/ini/crate.ini") == "crate.ini"
    assert object_family("somewhere/else.ini") == "other"


# ---------------------------------------------------------------------------
# Counting, dedup, artifacts
# ---------------------------------------------------------------------------

_SYNTHETIC_DOC = textwrap.dedent(
    """\
    ;; synthetic census fixture - not retail content
    Object FixtureSoldier
        Draw = W3DScriptedModelDraw ModuleTag_01
        End
        Body = ActiveBody ModuleTag_02
            MaxHealth = 100
        End
        Behavior = AutoHealBehavior ModuleTag_03
        End
        Behavior = AutoHealBehavior ModuleTag_04
        End
    End

    ChildObject FixtureVeteran FixtureSoldier
        Behavior = autohealbehavior ModuleTag_05
        End
    End
    """
).encode("ascii")

_SYNTHETIC_DOC_TWO = textwrap.dedent(
    """\
    Object Fixture'Wall
        Body = StructureBody ModuleTag_01
        End
    End
    """
).encode("ascii")


def test_collect_tree_usage_counts_sites_objects_and_families() -> None:
    tree = collect_tree_usage(
        [
            ("data/ini/object/goodfaction/units/soldier.ini", _SYNTHETIC_DOC),
            ("data/ini/object/civilian/walls.ini", _SYNTHETIC_DOC_TWO),
        ]
    )
    assert tree.document_count == 2
    assert tree.object_count == 3
    assert tree.declaration_sites == 6

    heal = tree.members["autohealbehavior"]
    assert heal.sites == 3
    # Distinct objects, not sites: FixtureSoldier declares it twice.
    assert heal.objects == {"fixturesoldier", "fixtureveteran"}
    assert heal.carriers == {"behavior"}
    assert heal.families == {"object/goodfaction"}
    # Display spelling is the most frequent authored spelling.
    assert heal.display_name == "AutoHealBehavior"

    # Object names with apostrophes are real retail objects and must count.
    wall = tree.members["structurebody"]
    assert wall.objects == {"fixture'wall"}
    assert wall.families == {"object/civilian"}


def test_collect_tree_usage_skips_pseudo_object_artifacts() -> None:
    # Retail commandbutton.ini authors ``Object = X`` inside flat blocks; the
    # object splitter parses that as a pseudo-object named "=".  Nothing in
    # such a block may be counted.
    source = textwrap.dedent(
        """\
        CommandButton Command_Fixture
            Object = FixtureSoldier
            Behavior = FakePseudoBehavior ModuleTag_01
        End
        """
    ).encode("ascii")
    tree = collect_tree_usage([("data/ini/commandbutton.ini", source)])
    assert tree.object_count == 0
    assert tree.declaration_sites == 0


def test_collect_tree_usage_fails_loudly_on_unparsable_input() -> None:
    with pytest.raises(ModuleCensusError, match="unparsable INI document"):
        collect_tree_usage([("data/ini/broken.ini", b"\0")])


def test_effective_ini_entries_dedup_filter_and_order() -> None:
    entries = [
        # Duplicate virtual path: the lower-precedence archive must win.
        CatalogEntry("ini.big", "data/ini/object/a.ini", 0, 10, 5),
        CatalogEntry("_patch106.big", "DATA/INI/OBJECT/A.INI", 0, 11, 0),
        CatalogEntry("ini.big", "data/ini/zed.inc", 0, 5, 5),
        # Filtered: not INI/INC, or outside data/ini/.
        CatalogEntry("ini.big", "data/ini/object/a.txt", 0, 5, 5),
        CatalogEntry("w3d.big", "art/w3d/model.w3d", 0, 5, 6),
    ]
    selected = effective_ini_entries(entries)
    assert [(entry.name, entry.archive) for entry in selected] == [
        ("DATA/INI/OBJECT/A.INI", "_patch106.big"),
        ("data/ini/zed.inc", "ini.big"),
    ]


# ---------------------------------------------------------------------------
# Measured support (AST scan)
# ---------------------------------------------------------------------------


def _write_fake_package(root: Path) -> Path:
    package = root / "fake_importer"
    package.mkdir()
    (package / "__init__.py").write_text("", encoding="utf-8")
    (package / "faction_import.py").write_text(
        textwrap.dedent(
            '''\
            """Root pipeline module."""
            from .used import consume

            def run():
                from .lazy import late
                return consume() + late()
            '''
        ),
        encoding="utf-8",
    )
    (package / "m3_pack_expansion.py").write_text("", encoding="utf-8")
    (package / "pipeline.py").write_text("", encoding="utf-8")
    (package / "used.py").write_text(
        textwrap.dedent(
            '''\
            """Docstring mentioning FakeDocOnlyUpdate must never count."""

            _FAKE_UNIMPLEMENTED_KINDS = {
                "fakerefusedpower": "needs the fake runtime system",
            }

            def consume():
                return "fakethingbehavior"
            '''
        ),
        encoding="utf-8",
    )
    (package / "lazy.py").write_text(
        "def late():\n    return 'FakeLazyUpdate'\n", encoding="utf-8"
    )
    (package / "orphan.py").write_text(
        "UNREACHED = 'FakeOrphanUpdate'\n", encoding="utf-8"
    )
    return package


def test_scan_module_support_measures_the_pipeline_closure(tmp_path: Path) -> None:
    package = _write_fake_package(tmp_path)
    vocabulary = [
        "FakeThingBehavior",
        "FakeLazyUpdate",
        "FakeOrphanUpdate",
        "FakeRefusedPower",
        "FakeDocOnlyUpdate",
    ]
    support = scan_module_support(vocabulary, package_dir=package)

    assert support.consumer_files == (
        "faction_import",
        "lazy",
        "m3_pack_expansion",
        "pipeline",
        "used",
    )
    # Lazy (function-body) imports are part of the closure; orphans are not.
    assert set(support.consumed) == {"fakethingbehavior", "fakelazyupdate"}
    assert support.consumed["fakethingbehavior"] == ("used.py",)
    # Refusal-container entries are refusals, never consumption.
    assert set(support.refused) == {"fakerefusedpower"}
    assert support.refusal_reasons["fakerefusedpower"] == (
        "needs the fake runtime system",
    )
    assert support.status("fakethingbehavior") == "consumed"
    assert support.status("fakerefusedpower") == "refused"
    # Docstrings and unreachable modules never count.
    assert support.status("fakedoconlyupdate") == "unhandled"
    assert support.status("fakeorphanupdate") == "unhandled"


def test_scan_module_support_rejects_missing_roots(tmp_path: Path) -> None:
    package = tmp_path / "empty_pkg"
    package.mkdir()
    (package / "__init__.py").write_text("", encoding="utf-8")
    with pytest.raises(ModuleCensusError, match="root modules missing"):
        scan_module_support(["X"], package_dir=package)


# ---------------------------------------------------------------------------
# Census assembly
# ---------------------------------------------------------------------------


def _fake_support(consumed: dict[str, tuple[str, ...]] = {}) -> ModuleSupport:
    return ModuleSupport(
        consumer_files=("faction_import",),
        consumed=dict(consumed),
        refused={},
        refusal_reasons={},
    )


def test_build_module_census_orders_members_and_zero_fills_trees() -> None:
    tree_a = collect_tree_usage(
        [("data/ini/object/goodfaction/units/soldier.ini", _SYNTHETIC_DOC)]
    )
    tree_b = collect_tree_usage(
        [
            ("data/ini/object/goodfaction/units/soldier.ini", _SYNTHETIC_DOC),
            ("data/ini/object/civilian/walls.ini", _SYNTHETIC_DOC_TWO),
        ]
    )
    census = build_module_census(
        {"bfme2-retail": tree_a, "rotwk-retail": tree_b},
        _fake_support({"autohealbehavior": ("playable_unit_compiler.py",)}),
    )
    names = [member["name"] for member in census["members"]]
    # Heaviest total first; single-tree member zero-filled, not dropped.
    assert names == ["AutoHealBehavior", "ActiveBody", "W3DScriptedModelDraw", "StructureBody"]
    structure = census["members"][3]
    assert structure["declarationSites"] == {"bfme2-retail": 0, "rotwk-retail": 1}
    assert structure["objectCount"] == {"bfme2-retail": 0, "rotwk-retail": 1}
    heal = census["members"][0]
    assert heal["status"] == "consumed"
    assert heal["consumedBy"] == ["playable_unit_compiler.py"]
    assert census["trees"]["rotwk-retail"]["declarationSites"] == 6
    # Every member must carry its A-E classification from the table.
    for member in census["members"]:
        letter, note = MODULE_CLASSIFICATION[member["name"].casefold()]
        assert member["classification"] == letter
        assert member["classificationNote"] == note


def test_build_module_census_is_deterministic() -> None:
    def build() -> bytes:
        tree = collect_tree_usage(
            [
                ("data/ini/object/goodfaction/units/soldier.ini", _SYNTHETIC_DOC),
                ("data/ini/object/civilian/walls.ini", _SYNTHETIC_DOC_TWO),
            ]
        )
        return census_json_bytes(
            build_module_census({"bfme2-retail": tree, "rotwk-retail": tree}, _fake_support())
        )

    first, second = build(), build()
    assert first == second
    assert b"\r" not in first
    assert first.endswith(b"\n")


def test_build_module_census_refuses_unclassified_kinds() -> None:
    source = b"Object X\n Behavior = TotallyUnknownModule ModuleTag_01\nEnd\n"
    tree = collect_tree_usage([("data/ini/object/goodfaction/x.ini", source)])
    with pytest.raises(ModuleCensusError, match="TotallyUnknownModule"):
        build_module_census({"bfme2-retail": tree}, _fake_support())


# ---------------------------------------------------------------------------
# The committed census document
# ---------------------------------------------------------------------------


def test_classification_table_and_committed_census_agree() -> None:
    """Every committed member is classified; every table entry is used."""

    census = _committed_census()
    member_names = {member["name"].casefold() for member in census["members"]}
    table_names = set(MODULE_CLASSIFICATION)
    assert member_names == table_names
    for member in census["members"]:
        letter, note = MODULE_CLASSIFICATION[member["name"].casefold()]
        assert member["classification"] == letter
        assert member["classificationNote"] == note
        assert letter in MODULE_CLASS_MEANINGS
        assert note


def test_committed_census_statuses_match_current_importer_source() -> None:
    """Statuses are measurements; they must track the live pipeline source.

    If this fails after an importer change, a module type gained or lost
    named consumption: regenerate the census rather than editing it.
    """

    census = _committed_census()
    vocabulary = [member["name"] for member in census["members"]]
    support = scan_module_support(vocabulary)
    assert list(support.consumer_files) == census["supportedScan"]["consumerFiles"]
    for member in census["members"]:
        folded = member["name"].casefold()
        assert member["status"] == support.status(folded), member["name"]
        assert member["consumedBy"] == list(support.consumed.get(folded, ())), (
            member["name"]
        )
        assert member["refusedBy"] == list(support.refused.get(folded, ())), (
            member["name"]
        )
        assert member["refusalReasons"] == list(
            support.refusal_reasons.get(folded, ())
        ), member["name"]


def test_committed_census_serialization_is_canonical() -> None:
    """LF-terminated and byte-identical under re-serialization."""

    data = CENSUS_PATH.read_bytes()
    assert b"\r" not in data
    assert data.endswith(b"\n")
    assert census_json_bytes(json.loads(data.decode("utf-8"))) == data


def test_committed_census_totals_are_coherent() -> None:
    census = _committed_census()
    trees = sorted(census["trees"])
    assert trees == ["bfme2-retail", "rotwk-retail"]
    for tree in trees:
        member_sum = sum(m["declarationSites"][tree] for m in census["members"])
        assert member_sum == census["trees"][tree]["declarationSites"]
        distinct = sum(
            1 for m in census["members"] if m["declarationSites"][tree] > 0
        )
        assert distinct == census["trees"][tree]["distinctModuleKinds"]
    for member in census["members"]:
        assert sum(member["declarationSites"].values()) > 0
        if member["status"] == "consumed":
            assert member["consumedBy"]
        if member["status"] == "refused":
            assert member["refusedBy"] and not member["consumedBy"]


# ---------------------------------------------------------------------------
# Coverage report tool
# ---------------------------------------------------------------------------


def _run_report(*argv: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(REPORT_TOOL), *argv],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def test_coverage_report_runs_and_separates_refusals() -> None:
    result = _run_report()
    assert result.returncode == 0, result.stderr
    assert "NOT coverage" in result.stdout
    assert "BACKLOG" in result.stdout
    for tree in ("bfme2-retail", "rotwk-retail"):
        assert tree in result.stdout

    # The printed per-status site totals must match the census exactly:
    # folding refusals or unhandled members into progress must be caught by
    # numbers, not by prose.
    census = _committed_census()
    lines = result.stdout.splitlines()
    for tree in sorted(census["trees"]):
        start = next(index for index, line in enumerate(lines) if line.startswith(f"{tree}:"))
        expected = {
            status: sum(
                member["declarationSites"][tree]
                for member in census["members"]
                if member["status"] == status
            )
            for status in ("consumed", "refused", "unhandled")
        }
        for offset, status in enumerate(("consumed", "refused", "unhandled"), start=1):
            row = lines[start + offset].split()
            assert row[0] == status
            assert int(row[1]) == expected[status], (tree, status)


def test_coverage_report_fails_on_incoherent_census(tmp_path: Path) -> None:
    census = _committed_census()
    # A consumed member without evidence is exactly the kind of silent
    # overstatement the report must refuse to aggregate.
    census["members"][0]["status"] = "consumed"
    census["members"][0]["consumedBy"] = []
    tampered = tmp_path / "census.json"
    tampered.write_text(json.dumps(census), encoding="utf-8")
    result = _run_report("--census", str(tampered))
    assert result.returncode == 1
    assert "INCOHERENT" in result.stderr


def test_coverage_report_fails_on_totals_mismatch(tmp_path: Path) -> None:
    census = _committed_census()
    census["members"][0]["declarationSites"]["bfme2-retail"] += 1
    tampered = tmp_path / "census.json"
    tampered.write_text(json.dumps(census), encoding="utf-8")
    result = _run_report("--census", str(tampered))
    assert result.returncode == 1
    assert "INCOHERENT" in result.stderr


# ---------------------------------------------------------------------------
# Real retail corpus (skips cleanly without the private workspace)
# ---------------------------------------------------------------------------


@pytest.mark.skipif(
    not _RETAIL_AVAILABLE,
    reason="retail catalogs are not available in this workspace",
)
def test_retail_regeneration_is_byte_identical_to_committed_census() -> None:
    # Regenerate from the state root whose catalogs the skip guard actually
    # probed at import time. Re-resolving it here instead would let any
    # earlier in-process CLI run decide which corpus this assertion measures.
    first = census_json_bytes(generate_retail_module_census(_RETAIL_STATE_ROOT))
    second = census_json_bytes(generate_retail_module_census(_RETAIL_STATE_ROOT))
    assert first == second
    assert first == CENSUS_PATH.read_bytes()


def test_generate_refuses_partial_corpus(tmp_path: Path) -> None:
    with pytest.raises(ModuleCensusError, match="retail catalogs unavailable"):
        generate_retail_module_census(state_root=tmp_path)
