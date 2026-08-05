from __future__ import annotations

import json
from pathlib import Path
import struct

from tools.close_goal_prop_bindings import (
    GLB_CHUNK_HEADER,
    GLB_HEADER,
    GLB_JSON_CHUNK,
    _sync_map_resolution,
    _validate_renderable_glb,
)


def _write_glb(path: Path, document: dict[str, object]) -> None:
    payload = json.dumps(document, separators=(",", ":")).encode("utf-8")
    payload += b" " * ((-len(payload)) % 4)
    total = GLB_HEADER.size + GLB_CHUNK_HEADER.size + len(payload)
    path.write_bytes(
        GLB_HEADER.pack(b"glTF", 2, total)
        + GLB_CHUNK_HEADER.pack(len(payload), GLB_JSON_CHUNK)
        + payload
    )


def test_renderable_glb_requires_a_reachable_mesh_node(tmp_path: Path) -> None:
    empty = tmp_path / "empty.glb"
    _write_glb(
        empty,
        {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{}]},
    )
    assert _validate_renderable_glb(empty) == (False, "zero-instantiable-mesh")

    renderable = tmp_path / "renderable.glb"
    _write_glb(
        renderable,
        {
            "asset": {"version": "2.0"},
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "nodes": [{"mesh": 0}],
            "meshes": [{"primitives": [{}]}],
        },
    )
    assert _validate_renderable_glb(renderable) == (True, "ok-mesh-instances=1")


def test_map_resolution_and_conversion_status_are_synchronized(tmp_path: Path) -> None:
    map_path = tmp_path / "map.json"
    map_path.write_text(
        json.dumps(
            {
                "schema": "openbfme.map",
                "objectBindings": "object-bindings.json",
                "objectResolution": {"resolutionStatus": "partial"},
                "conversionStatus": {"objects": "stale"},
            }
        ),
        encoding="utf-8",
    )
    summary = {
        "resolutionStatus": "complete",
        "typeCount": 2,
        "placementCount": 3,
        "resolvedTypeCount": 2,
        "resolvedPlacementCount": 3,
        "boundTypeCount": 1,
        "boundPlacementCount": 2,
        "logicalTypeCount": 1,
        "logicalPlacementCount": 1,
        "unresolvedTypeCount": 0,
        "unresolvedPlacementCount": 0,
    }

    assert _sync_map_resolution(map_path, summary)
    cooked = json.loads(map_path.read_text(encoding="utf-8"))
    assert cooked["objectResolution"] == summary
    assert (
        cooked["conversionStatus"]["objects"]
        == "placements-imported-object-resolution-complete"
    )
    assert not _sync_map_resolution(map_path, summary)
