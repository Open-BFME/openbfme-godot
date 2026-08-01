from __future__ import annotations

import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
from unittest import mock

import pytest

from openbfme_importer.catalog import CatalogEntry
from openbfme_importer.pipeline import ImportPipeline
from openbfme_importer.profile import (
    ImportProfile,
    ResourceRule,
    ResolvedResource,
    canonical_multiplayer_map_runtime_slug,
    is_canonical_multiplayer_map_virtual_path,
)
from openbfme_importer.util import write_json_atomic


MAP_PATH = "maps/map mp fixture/map mp fixture.map"
LIBRARY_PATHS = [
    "libraries/ai_initialize/ai_initialize.map",
    "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map",
]


def _document(identity: str, *, library: bool) -> dict:
    players = (
        [{"index": 0, "name": "Player"}]
        if library
        else [{"index": 0, "name": "PlyrCivilian"}]
    )
    return {
        "schema": "openbfme.map-scripts",
        "schemaVersion": 1,
        "source": {
            "container": "map",
            "sourceBytes": 1,
            "sourceSha256": identity,
        },
        "world": {
            "available": True,
            "players": players,
            "teams": [],
        },
        "scripts": [],
    }


def _resource(entries: tuple[CatalogEntry, ...]) -> ResolvedResource:
    return ResolvedResource(
        ResourceRule(
            id="map-fixture-scripts",
            kind="map",
            patterns=(MAP_PATH, *LIBRARY_PATHS),
            required=True,
            converter="sage-script-composite",
            output="maps/fixture/scripts.json",
            limit=3,
            expected_count=3,
            options={
                "mapVirtualPath": MAP_PATH,
                "libraryVirtualPaths": LIBRARY_PATHS,
            },
        ),
        entries,
        (),
        None,
    )


def _pipeline(game: str = "bfme2") -> ImportPipeline:
    pipeline = object.__new__(ImportPipeline)
    pipeline.game = SimpleNamespace(id=game)
    return pipeline


def test_script_composite_emits_exact_ordered_three_source_bundle() -> None:
    paths = [MAP_PATH, *LIBRARY_PATHS]
    entries = tuple(
        CatalogEntry("maps.big", path, index, 1, 0)
        for index, path in enumerate(reversed(paths))
    )
    documents = {
        b"map": _document("1" * 64, library=False),
        b"initialize": _document("2" * 64, library=True),
        b"inherit": _document("3" * 64, library=True),
    }
    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw)
        extracted = {}
        payloads = {
            MAP_PATH: b"map",
            LIBRARY_PATHS[0]: b"initialize",
            LIBRARY_PATHS[1]: b"inherit",
        }
        for entry in entries:
            source = root / f"source-{entry.offset}.map"
            source.write_bytes(payloads[entry.name])
            extracted[(entry.archive.casefold(), entry.name.casefold())] = {
                "source_path": source,
            }

        with mock.patch(
            "openbfme_importer.sage_scripts.map_scripts_document",
            side_effect=lambda payload, *, container: documents[payload],
        ):
            output = ImportPipeline._convert_script_composite_bundle(
                _pipeline(),
                _resource(entries),
                extracted,
                "maps/fixture/scripts.json",
                {
                    "mapVirtualPath": MAP_PATH.upper(),
                    "libraryVirtualPaths": LIBRARY_PATHS,
                },
                root / "pack",
            )

        assert output == [root / "pack" / "maps" / "fixture" / "scripts.json"]
        document = json.loads(output[0].read_text(encoding="utf-8"))
        assert document["schemaVersion"] == 2
        assert document["source"]["game"] == "bfme2"
        assert document["source"]["map"]["virtualPath"] == MAP_PATH
        assert document["source"]["map"]["sourceBytes"] == 1
        assert document["source"]["map"]["sourceSha256"] == "1" * 64
        assert [
            row["virtualPath"] for row in document["source"]["libraries"]
        ] == LIBRARY_PATHS
        assert [
            row["sourceSha256"] for row in document["source"]["libraries"]
        ] == ["2" * 64, "3" * 64]
        assert [
            row["sourceBytes"] for row in document["source"]["libraries"]
        ] == [1, 1]
        assert [
            row["identity"] for row in document["libraryTemplates"]
        ] == ["2" * 64, "3" * 64]


def test_script_composite_refuses_output_collision() -> None:
    entries = tuple(
        CatalogEntry("maps.big", path, index, 1, 0)
        for index, path in enumerate([MAP_PATH, *LIBRARY_PATHS])
    )
    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw)
        target = root / "pack" / "maps" / "fixture" / "scripts.json"
        target.parent.mkdir(parents=True)
        target.write_text("{}\n", encoding="utf-8")

        with pytest.raises(ValueError, match="collides with pack output"):
            ImportPipeline._convert_script_composite_bundle(
                _pipeline(),
                _resource(entries),
                {},
                "maps/fixture/scripts.json",
                {
                    "mapVirtualPath": MAP_PATH,
                    "libraryVirtualPaths": LIBRARY_PATHS,
                },
                root / "pack",
            )


def test_script_composite_refuses_any_other_library_closure() -> None:
    entries = tuple(
        CatalogEntry("maps.big", path, index, 1, 0)
        for index, path in enumerate([MAP_PATH, *LIBRARY_PATHS])
    )
    with tempfile.TemporaryDirectory() as raw:
        with pytest.raises(ValueError, match="ordered ai_initialize"):
            ImportPipeline._convert_script_composite_bundle(
                _pipeline(),
                _resource(entries),
                {},
                "maps/fixture/scripts.json",
                {
                    "mapVirtualPath": MAP_PATH,
                    "libraryVirtualPaths": [LIBRARY_PATHS[1], LIBRARY_PATHS[0]],
                },
                Path(raw) / "pack",
            )


def test_script_composite_profile_refuses_later_output_owner() -> None:
    composite = {
        "id": "map-fixture-scripts",
        "kind": "map",
        "converter": "sage-script-composite",
        "patterns": [MAP_PATH, *LIBRARY_PATHS],
        "output": "maps/fixture/scripts.json",
        "limit": 3,
        "expected_count": 3,
        "options": {
            "mapVirtualPath": MAP_PATH,
            "libraryVirtualPaths": LIBRARY_PATHS,
        },
    }
    collision = {
        "id": "later-output-owner",
        "kind": "data",
        "converter": "copy",
        "patterns": ["data/later.json"],
        "output": "MAPS/FIXTURE/SCRIPTS.JSON",
        "limit": 1,
        "expected_count": 1,
    }
    with tempfile.TemporaryDirectory() as raw:
        path = Path(raw) / "profile.json"
        write_json_atomic(
            path,
            {
                "format": 1,
                "id": "script-composite-collision",
                "pack": {"id": "script-composite-collision"},
                "resources": [composite, collision],
            },
        )
        with pytest.raises(ValueError, match="output collides"):
            ImportProfile.load(path)


@pytest.mark.parametrize("converter", ["copy", "sage-map", "sage-scripts"])
def test_script_composite_profile_reserves_map_scripts_output(
    converter: str,
) -> None:
    bypass = {
        "id": "non-composite-scripts-owner",
        "kind": "data",
        "converter": converter,
        "patterns": ["data/not-a-script-source.bin"],
        "output": "MAPS/FIXTURE/SCRIPTS.JSON",
        "limit": 1,
        "expected_count": 1,
    }
    with tempfile.TemporaryDirectory() as raw:
        path = Path(raw) / "profile.json"
        write_json_atomic(
            path,
            {
                "format": 1,
                "id": "script-composite-owner-bypass",
                "pack": {"id": "script-composite-owner-bypass"},
                "resources": [bypass],
            },
        )
        with pytest.raises(ValueError, match="reserved for sage-script-composite"):
            ImportProfile.load(path)


@pytest.mark.parametrize(
    "output",
    [
        "./maps/fixture/scripts.json",
        "maps/fixture/./scripts.json",
        r"maps\fixture\scripts.json",
    ],
)
def test_profile_refuses_noncanonical_resource_output_alias(output: str) -> None:
    with tempfile.TemporaryDirectory() as raw:
        path = Path(raw) / "profile.json"
        write_json_atomic(
            path,
            {
                "format": 1,
                "id": "noncanonical-output",
                "pack": {"id": "noncanonical-output"},
                "resources": [
                    {
                        "id": "alias",
                        "kind": "data",
                        "converter": "copy",
                        "patterns": ["data/source.bin"],
                        "output": output,
                    }
                ],
            },
        )
        with pytest.raises(ValueError, match="non-canonical output path"):
            ImportProfile.load(path)


@pytest.mark.parametrize(
    "runtime_path",
    [
        "./maps/fixture/scripts.json",
        "maps/fixture/./scripts.json",
        r"maps\fixture\scripts.json",
        "maps/fixture/scripts.json",
    ],
)
def test_profile_refuses_runtime_data_script_output_injection(
    runtime_path: str,
) -> None:
    with tempfile.TemporaryDirectory() as raw:
        path = Path(raw) / "profile.json"
        write_json_atomic(
            path,
            {
                "format": 1,
                "id": "runtime-script-injection",
                "pack": {"id": "runtime-script-injection"},
                "runtime_data": {runtime_path: {}},
                "resources": [
                    {
                        "id": "source",
                        "kind": "data",
                        "converter": "hash-only",
                        "patterns": ["data/source.bin"],
                    }
                ],
            },
        )
        expected = (
            "non-canonical output path"
            if runtime_path != "maps/fixture/scripts.json"
            else "cannot own a map scripts.json"
        )
        with pytest.raises(ValueError, match=expected):
            ImportProfile.load(path)


def test_profile_refuses_resource_runtime_data_canonical_collision() -> None:
    with tempfile.TemporaryDirectory() as raw:
        path = Path(raw) / "profile.json"
        write_json_atomic(
            path,
            {
                "format": 1,
                "id": "runtime-output-collision",
                "pack": {"id": "runtime-output-collision"},
                "runtime_data": {"data/shared.json": {}},
                "resources": [
                    {
                        "id": "source",
                        "kind": "data",
                        "converter": "copy",
                        "patterns": ["data/source.bin"],
                        "output": "DATA/SHARED.JSON",
                    }
                ],
            },
        )
        with pytest.raises(ValueError, match="collides with runtime_data"):
            ImportProfile.load(path)


def test_profile_refuses_nonstr_resource_output() -> None:
    with tempfile.TemporaryDirectory() as raw:
        path = Path(raw) / "profile.json"
        write_json_atomic(
            path,
            {
                "format": 1,
                "id": "nonstr-output",
                "pack": {"id": "nonstr-output"},
                "resources": [
                    {
                        "id": "source",
                        "kind": "data",
                        "converter": "copy",
                        "patterns": ["data/source.bin"],
                        "output": ["maps", "fixture", "scripts.json"],
                    }
                ],
            },
        )
        with pytest.raises(ValueError, match="output path must be a string"):
            ImportProfile.load(path)


@pytest.mark.parametrize(
    "map_virtual_path",
    [
        "",
        "maps/map mp fixture/not-a-map.bin",
        "LIBRARIES/AI_INITIALIZE/AI_INITIALIZE.MAP",
        "maps/map good campaign/map good campaign.map",
        "maps/cinematics/opening/opening.map",
        "maps/livingworld/warofthering.map",
        "map mp fixture.map",
        "maps/map mp fixture/map mp different.map",
        "maps/map mp fixture/extra/map mp fixture.map",
        r"maps\map mp fixture\map mp fixture.map",
    ],
)
def test_script_composite_profile_refuses_invalid_or_duplicate_map_source(
    map_virtual_path: str,
) -> None:
    composite = {
        "id": "map-fixture-scripts",
        "kind": "map",
        "converter": "sage-script-composite",
        # Keep the generic pattern contract valid so this specifically
        # exercises the composite mapVirtualPath validation.
        "patterns": [MAP_PATH, *LIBRARY_PATHS],
        "output": "maps/fixture/scripts.json",
        "limit": 3,
        "expected_count": 3,
        "options": {
            "mapVirtualPath": map_virtual_path,
            "libraryVirtualPaths": LIBRARY_PATHS,
        },
    }
    with tempfile.TemporaryDirectory() as raw:
        path = Path(raw) / "profile.json"
        write_json_atomic(
            path,
            {
                "format": 1,
                "id": "invalid-script-composite-source",
                "pack": {"id": "invalid-script-composite-source"},
                "resources": [composite],
            },
        )
        with pytest.raises(ValueError, match="exact ordered map"):
            ImportProfile.load(path)


@pytest.mark.parametrize(
    ("virtual_path", "accepted"),
    [
        (MAP_PATH, True),
        ("MAPS/MAP MP FIXTURE/MAP MP FIXTURE.MAP", True),
        ("maps/map mp Fixture/map mp fixture.map", True),
        (
            "MaPs/MaP MP Mixed Case Name/mAp Mp mIxEd CaSe NaMe.MaP",
            True,
        ),
        ("maps/map mp fixture/map mp other.map", False),
        ("maps/map fixture/map fixture.map", False),
        ("maps/map mp fixture.map", False),
        ("maps/map mp fixture/map mp fixture.scb", False),
        (r"maps/map mp bad\name/map mp bad\name.map", False),
        ("maps/map mp under_score/map mp under_score.map", False),
        ("maps/map mp double  space/map mp double  space.map", False),
        ("maps/map mp  edge/map mp  edge.map", False),
        ("maps/map mp edge /map mp edge .map", False),
        ("maps/map mp ./map mp ..map", False),
        ("maps/map mp bad:name/map mp bad:name.map", False),
    ],
)
def test_script_composite_multiplayer_path_contract(
    virtual_path: str,
    accepted: bool,
) -> None:
    assert is_canonical_multiplayer_map_virtual_path(virtual_path) is accepted


@pytest.mark.parametrize(
    ("virtual_path", "runtime_slug"),
    [
        (MAP_PATH, "fixture"),
        ("MAPS/MAP MP FIXTURE/MAP MP FIXTURE.MAP", "fixture"),
        (
            "MaPs/MaP MP Mixed Case Name/mAp Mp mIxEd CaSe NaMe.MaP",
            "mixed-case-name",
        ),
        ("maps/map mp fixture/map mp other.map", None),
        ("maps/map mp double  space/map mp double  space.map", None),
    ],
)
def test_reviewer_k_shared_map_path_grammar_derives_runtime_slug(
    virtual_path: str,
    runtime_slug: str | None,
) -> None:
    assert canonical_multiplayer_map_runtime_slug(virtual_path) == runtime_slug


@pytest.mark.parametrize(
    "output",
    [
        "maps/wrong-slug/scripts.json",
        "MAPS/FIXTURE/SCRIPTS.JSON",
        "maps/fixture/SCRIPTS.JSON",
    ],
)
def test_reviewer_k_profile_refuses_source_output_slug_or_case_mismatch(
    output: str,
) -> None:
    composite = {
        "id": "map-fixture-scripts",
        "kind": "map",
        "converter": "sage-script-composite",
        "patterns": [MAP_PATH, *LIBRARY_PATHS],
        "output": output,
        "limit": 3,
        "expected_count": 3,
        "options": {
            "mapVirtualPath": MAP_PATH,
            "libraryVirtualPaths": LIBRARY_PATHS,
        },
    }
    with tempfile.TemporaryDirectory() as raw:
        path = Path(raw) / "profile.json"
        write_json_atomic(
            path,
            {
                "format": 1,
                "id": "source-output-coupling",
                "pack": {"id": "source-output-coupling"},
                "resources": [composite],
            },
        )
        with pytest.raises(ValueError, match="output must be exactly"):
            ImportProfile.load(path)


@pytest.mark.parametrize(
    "output",
    [
        "maps/wrong-slug/scripts.json",
        "MAPS/FIXTURE/SCRIPTS.JSON",
        "maps/fixture/SCRIPTS.JSON",
    ],
)
def test_reviewer_l_pipeline_refuses_source_output_slug_or_case_mismatch(
    output: str,
) -> None:
    entries = tuple(
        CatalogEntry("maps.big", path, index, 1, 0)
        for index, path in enumerate([MAP_PATH, *LIBRARY_PATHS])
    )
    with tempfile.TemporaryDirectory() as raw:
        with pytest.raises(ValueError, match="output must be exactly"):
            ImportPipeline._convert_script_composite_bundle(
                _pipeline(),
                _resource(entries),
                {},
                output,
                {
                    "mapVirtualPath": MAP_PATH,
                    "libraryVirtualPaths": LIBRARY_PATHS,
                },
                Path(raw) / "pack",
            )


@pytest.mark.parametrize(
    "map_virtual_path",
    [
        "maps/map good campaign/map good campaign.map",
        "maps/cinematics/opening/opening.map",
        "maps/livingworld/warofthering.map",
        "map mp fixture.map",
        "maps/map mp fixture/map mp different.map",
    ],
)
def test_script_composite_pipeline_refuses_non_multiplayer_map_paths(
    map_virtual_path: str,
) -> None:
    entries = tuple(
        CatalogEntry("maps.big", path, index, 1, 0)
        for index, path in enumerate([MAP_PATH, *LIBRARY_PATHS])
    )
    with tempfile.TemporaryDirectory() as raw:
        with pytest.raises(ValueError, match="canonical multiplayer"):
            ImportPipeline._convert_script_composite_bundle(
                _pipeline(),
                _resource(entries),
                {},
                "maps/fixture/scripts.json",
                {
                    "mapVirtualPath": map_virtual_path,
                    "libraryVirtualPaths": LIBRARY_PATHS,
                },
                Path(raw) / "pack",
            )
