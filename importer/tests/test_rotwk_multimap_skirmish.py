"""Offline coverage for the RotWK multimap binder handoff."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path

from openbfme_importer import cli


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "rotwk_multimap_skirmish.py"


def _load_tool():
    spec = importlib.util.spec_from_file_location("rotwk_multimap_skirmish", TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_planned_model_binding_is_nonempty_and_survives_pack_cook(tmp_path: Path) -> None:
    mod = _load_tool()
    profile = {
        "resources": [
            {
                "converter": "sage-map",
                "output": "maps/mock-map",
                "options": {
                    "objectBindings": {
                        "logical": [],
                        "models": [
                            {
                                "typeName": "RetailTree",
                                "sourceVirtualModel": "art/w3d/retailtree.w3d",
                                "glb": "assets/models/props/retailtree.glb",
                                "matchMethod": "exact-type-name",
                            }
                        ],
                    }
                },
            }
        ]
    }
    inventory = mod.profile_binding_inventory(profile)
    assert inventory["modelCount"] == 1

    binding_path = tmp_path / "maps" / "mock-map" / "object-bindings.json"
    binding_path.parent.mkdir(parents=True)
    binding_path.write_text(
        json.dumps(
            {
                "records": [
                    {"typeName": "RetailTree", "status": "bound"},
                    {"typeName": "CaptureFlag", "status": "unresolved"},
                ]
            }
        ),
        encoding="utf-8",
    )
    proof = mod.verify_pack_binding_inventory(tmp_path, inventory)
    assert proof["ok"] is True
    assert proof["checkedMapCount"] == 1


def test_pack_proof_fails_when_planned_binding_is_dropped(tmp_path: Path) -> None:
    mod = _load_tool()
    planned = {
        "maps": {
            "maps/mock-map": {
                "boundTypeNames": ["RetailTree"],
            }
        }
    }
    binding_path = tmp_path / "maps" / "mock-map" / "object-bindings.json"
    binding_path.parent.mkdir(parents=True)
    binding_path.write_text(
        json.dumps({"records": [{"typeName": "RetailTree", "status": "unresolved"}]}),
        encoding="utf-8",
    )
    proof = mod.verify_pack_binding_inventory(tmp_path, planned)
    assert proof["ok"] is False
    assert proof["missingSample"] == ["maps/mock-map:RetailTree"]


def test_cli_exposes_map_limit_and_publish_without_select() -> None:
    parser = cli.build_parser()
    map_args = parser.parse_args(
        [
            "generate-map-profile",
            "--install",
            "retail",
            "--game",
            "rotwk",
            "--map-set",
            "skirmish",
            "--map-limit",
            "10",
        ]
    )
    assert map_args.map_limit == 10
    build_args = parser.parse_args(
        ["build", "--install", "retail", "--no-select"]
    )
    assert build_args.no_select is True
