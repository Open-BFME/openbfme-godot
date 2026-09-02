from __future__ import annotations

import json
import os
from pathlib import Path

import pytest

from openbfme_importer.native_content import (
    NativeMapSource,
    SELECTION_SCHEMA,
    _corpus_report_covers_request,
    _corpus_sources,
    build_native_documents,
    ensure_map_sources,
)


FIXTURES = Path(__file__).parent / "fixtures"


def _write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, sort_keys=True), encoding="utf-8")


def _cooked_map(root: Path) -> None:
    root.mkdir()
    _write_json(
        root / "map.json",
        {
            "schema": "openbfme.map",
            "id": "test.native-map",
            "source": {"sha256": "7" * 64},
            "terrain": "terrain.json",
            "objects": "objects.json",
            "waypoints": "waypoints.json",
        },
    )
    _write_json(
        root / "terrain.json",
        {
            "height": {
                "width": 2,
                "height": 2,
                "horizontalScale": 10,
                "heightmap": {"path": "heightmap.r16"},
            },
            "passability": {"path": "impassability.bit", "rowStrideBytes": 1},
        },
    )
    (root / "heightmap.r16").write_bytes(bytes(range(8)))
    (root / "impassability.bit").write_bytes(b"\0\0")
    _write_json(
        root / "waypoints.json",
        {
            "waypoints": [
                {
                    "name": "Player_1_Start",
                    "playerIndex": 1,
                    "sagePosition": [5, 5, 0],
                },
                {
                    "name": "Player_2_Start",
                    "playerIndex": 2,
                    "sagePosition": [15, 15, 0],
                },
            ],
            "playerStarts": {
                "Player_1_Start": [5, 5, 0],
                "Player_2_Start": [15, 15, 0],
            },
        },
    )
    _write_json(root / "objects.json", {"objects": []})


def test_fixture_cook_selection_and_idempotence(tmp_path: Path) -> None:
    ini_root = FIXTURES / "cook" / "objects"
    cooked = tmp_path / "cooked-map"
    _cooked_map(cooked)
    content = tmp_path / "content-packs"
    sources = [
        NativeMapSource(
            "Native Test Map",
            "native-test-map",
            2,
            cooked,
            "multiplayer",
            "maps/map mp native test/map mp native test.map",
        )
    ]

    first = build_native_documents(ini_root, sources, content)
    selection_path = content / "native" / "selection.json"
    selection_first = selection_path.read_bytes()
    selection = json.loads(selection_first)

    assert first.bundle_written
    assert first.maps_written == 1
    assert first.selection_written
    assert selection == {
        "schema": SELECTION_SCHEMA,
        "version": 1,
        "active": first.active,
        "bundle": f"native/{first.active}/bundle-v1.json",
        "maps": [
            {
                "name": "Native Test Map",
                "slug": "native-test-map",
                "path": f"native/{first.active}/maps/native-test-map.map-v1.json",
                "players": 2,
                "kind": "multiplayer",
            }
        ],
    }
    bundle = json.loads(
        (content / "native" / first.active / "bundle-v1.json").read_text(
            encoding="utf-8"
        )
    )
    map_document = json.loads(
        (
            content
            / "native"
            / first.active
            / "maps"
            / "native-test-map.map-v1.json"
        ).read_text(encoding="utf-8")
    )
    assert bundle["source"]["effective_tree_sha256"] == first.active
    assert map_document["schema"] == "openbfme.map.v1"
    assert len(map_document["start_positions"]) == 2

    second = build_native_documents(ini_root, sources, content)

    assert second.active == first.active
    assert not second.bundle_written
    assert second.maps_written == 0
    assert not second.selection_written
    assert selection_path.read_bytes() == selection_first

    bundle_path = content / "native" / first.active / "bundle-v1.json"
    bundle_path.write_text(
        json.dumps({"schema": bundle["schema"], "source": bundle["source"]}),
        encoding="utf-8",
    )
    map_path = (
        content
        / "native"
        / first.active
        / "maps"
        / "native-test-map.map-v1.json"
    )
    map_path.write_text(
        json.dumps(
            {"schema": map_document["schema"], "source": map_document["source"]}
        ),
        encoding="utf-8",
    )

    repaired = build_native_documents(ini_root, sources, content)

    assert repaired.bundle_written
    assert repaired.maps_written == 1


def test_limited_corpus_report_is_not_complete_for_all(tmp_path: Path) -> None:
    state = tmp_path / "state"
    report = state / "reports" / "rotwk-map-cook-corpus.json"
    report.parent.mkdir(parents=True)
    report.write_text(
        json.dumps(
            {
                "schemaVersion": 3,
                "eligibleMapCount": 7,
                "mapCount": 2,
                "maps": [],
            }
        ),
        encoding="utf-8",
    )

    assert _corpus_report_covers_request(state, 2)
    assert not _corpus_report_covers_request(state, None)


def test_corpus_source_preserves_punctuation_in_cache_slug(tmp_path: Path) -> None:
    state = tmp_path / "state"
    slug = "cin-amon-sul---castle-pan"
    cooked = state / "editions" / "rotwk" / "cache" / "native-cooked-maps" / slug
    cooked.parent.mkdir(parents=True)
    _cooked_map(cooked)
    report = state / "reports" / "rotwk-map-cook-corpus.json"
    report.parent.mkdir(parents=True)
    report.write_text(
        json.dumps(
            {
                "maps": [
                    {
                        "path": "maps/cin amon sul - castle pan/cin amon sul - castle pan.map",
                        "slug": slug,
                        "kind": "campaign",
                        "verdict": "under-two-player-starts",
                        "playerStarts": 1,
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    sources = _corpus_sources(state)

    assert len(sources) == 1
    assert sources[0].slug == slug
    assert sources[0].cooked_root == cooked.resolve()


def test_corpus_backed_map_source_when_private_install_is_available(tmp_path: Path) -> None:
    install_text = os.environ.get("ROTWK_INSTALL", "").strip()
    state_text = os.environ.get("OPENBFME_IMPORT_ROOT", "").strip()
    if not install_text or not state_text:
        pytest.skip("private RotWK install/state root is absent")
    install = Path(install_text)
    state = Path(state_text)
    if not (install / "game.dat").is_file():
        pytest.skip("private RotWK install is absent")

    tools_root = Path(__file__).resolve().parents[2] / "tools"
    import sys

    sys.path.insert(0, str(tools_root))
    try:
        from rotwk_layered_install import layered_rotwk_install
    finally:
        sys.path.remove(str(tools_root))
    layered = layered_rotwk_install(state)
    if layered is None:
        pytest.skip("private layered RotWK install is absent")

    sources, _ran = ensure_map_sources(layered, state, 1)

    assert len(sources) == 1
    assert (sources[0].cooked_root / "map.json").is_file()
