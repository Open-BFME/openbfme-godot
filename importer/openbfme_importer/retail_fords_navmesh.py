"""Derive a zero-guess per-map navigation and footprint evidence contract.

Historically Fords-of-Isen-II-only; the contract logic is map-agnostic and
``build_map_navigation_contract`` accepts any cooked multiplayer map, with the
per-map authored facts (map id, required named crossings, exact player-start
count, reviewed probe cells) supplied as parameters.
``build_fords_navigation_contract`` preserves the exact historical Fords
behavior and output.

This module does not generate a Godot navmesh.  It attests the exact cooked
SAGE passability/start/water facts, records exact Men roster collision and
formation declarations, and emits deterministic probes that a later runtime
may use as conformance tests.  Anything that needs executable SAGE behavior is
kept as an explicit blocker rather than inferred from geometry.
"""

from __future__ import annotations

import argparse
from collections import deque
import hashlib
import json
import math
from pathlib import Path
import re
from typing import Any, Iterable, Mapping, Sequence

from .util import write_json_atomic


SCHEMA = "openbfme.retail-fords-navigation-contract"
SCHEMA_VERSION = 0
MAX_JSON_BYTES = 16 * 1024 * 1024
MAX_INI_BYTES = 8 * 1024 * 1024
EXPECTED_MAP_ID = "bfme2.map.fords-of-isen-ii"
REQUIRED_FORD_NAMES = ("ford1", "ford2", "ford3")
# Human-reviewed source-passability probe cells, per map.  Fords of Isen II
# carries the one reviewed ford2 corridor cell; other maps start with none.
REVIEWED_PASSABILITY_CELLS: tuple[tuple[tuple[int, int], str], ...] = (
    ((208, 142), "passability-reviewed-ford2-cell"),
)
_MAP_ID_PATTERN = re.compile(r"^[a-z0-9]+(?:\.[a-z0-9-]+)+$")

_UNIT_SPECS = (
    (
        "GondorFighter",
        "infantry",
        "data/ini/object/goodfaction/units/men/gondorfighter.ini",
    ),
    (
        "GondorArcher",
        "infantry",
        "data/ini/object/goodfaction/units/men/gondorarcher.ini",
    ),
    (
        "GondorTowerShieldGuard",
        "infantry",
        "data/ini/object/goodfaction/units/men/gondortowershieldguard.ini",
    ),
    (
        "GondorCavalry",
        "cavalry",
        "data/ini/object/goodfaction/units/men/gondorcavalry.ini",
    ),
)
_HORDE_SPECS = (
    ("GondorFighterHorde", "infantry", "GondorFighter"),
    ("GondorArcherHorde", "infantry", "GondorArcher"),
    (
        "GondorTowerShieldGuardHorde",
        "infantry",
        "GondorTowerShieldGuard",
    ),
    ("GondorKnightHorde", "cavalry", "GondorCavalry"),
)
_HORDE_PATH = "data/ini/object/goodfaction/hordes/men/menhordes.ini"
_BUILDING_SPECS = (
    (
        "men-fortress",
        (
            (
                "MenFortress",
                "data/ini/object/goodfaction/structures/men/fortress.ini",
                "placement-foundation",
            ),
            (
                "MenFortressCitadel",
                "data/ini/object/goodfaction/structures/men/fortress.ini",
                "spawned-citadel",
            ),
        ),
    ),
    (
        "men-barracks",
        (
            (
                "GondorBarracks",
                "data/ini/object/goodfaction/structures/men/barracks.ini",
                "object",
            ),
        ),
    ),
    (
        "men-archery-range",
        (
            (
                "GondorArcherRange",
                "data/ini/object/goodfaction/structures/men/archerrange.ini",
                "object",
            ),
        ),
    ),
    (
        "men-stable",
        (
            (
                "GondorStable",
                "data/ini/object/goodfaction/structures/men/stable.ini",
                "object",
            ),
        ),
    ),
    (
        "men-farm",
        (
            (
                "FarmInterface",
                "data/ini/object/goodfaction/structures/farminterface.ini",
                "inherited-parent",
            ),
            (
                "GondorFarm",
                "data/ini/object/goodfaction/structures/men/farm.ini",
                "child-object",
            ),
        ),
    ),
)

_ASSIGNMENT_RE = re.compile(r"^\s*([A-Za-z][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")
_OBJECT_RE = re.compile(
    r"^(Object|ChildObject)\s+([^\s;]+)(?:\s+([^\s;]+))?\s*$",
    re.IGNORECASE,
)
_POSITION_RE = re.compile(
    r"Position\s*:\s*X\s*:\s*(-?(?:\d+(?:\.\d*)?|\.\d+))\s+"
    r"Y\s*:\s*(-?(?:\d+(?:\.\d*)?|\.\d+))",
    re.IGNORECASE,
)
_UNIT_TYPE_RE = re.compile(r"UnitType\s*:\s*([^\s]+)", re.IGNORECASE)
_RANK_RE = re.compile(r"RankNumber\s*:\s*(\d+)", re.IGNORECASE)


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")


def _canonical_sha256(value: object) -> str:
    return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _file_evidence(path: Path, virtual_path: str) -> dict[str, Any]:
    payload = path.read_bytes()
    return {
        "byteCount": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "virtualPath": virtual_path,
    }


def _load_json(path: Path, label: str) -> dict[str, Any]:
    if not path.is_file():
        raise ValueError(f"missing {label}: {path}")
    if path.stat().st_size > MAX_JSON_BYTES:
        raise ValueError(f"{label} exceeds bounded size")
    try:
        value = json.loads(path.read_text("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid {label}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value


def _strip_comment(line: str) -> str:
    semicolon = line.find(";")
    slash = line.find("//")
    indexes = [index for index in (semicolon, slash) if index >= 0]
    return line[: min(indexes)] if indexes else line


def _read_ini(path: Path, virtual_path: str) -> tuple[str, dict[str, Any]]:
    if not path.is_file():
        raise ValueError(f"missing effective INI: {virtual_path}")
    payload = path.read_bytes()
    if len(payload) > MAX_INI_BYTES:
        raise ValueError(f"effective INI exceeds bounded size: {virtual_path}")
    try:
        text = payload.decode("cp1252")
    except UnicodeDecodeError as exc:  # pragma: no cover - cp1252 is total
        raise ValueError(f"invalid effective INI: {virtual_path}") from exc
    return text, {
        "byteCount": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "virtualPath": virtual_path,
    }


def _object_block(text: str, name: str) -> dict[str, Any]:
    lines = text.splitlines()
    starts: list[tuple[int, re.Match[str]]] = []
    for index, raw in enumerate(lines):
        match = _OBJECT_RE.fullmatch(_strip_comment(raw).strip())
        if match is not None:
            starts.append((index, match))
    selected = [
        item for item in starts if item[1].group(2).casefold() == name.casefold()
    ]
    if len(selected) != 1:
        raise ValueError(
            f"expected exactly one object block {name!r}, got {len(selected)}"
        )
    start, header = selected[0]
    following = [index for index, _match in starts if index > start]
    stop = min(following) if following else len(lines)
    return {
        "declaration": header.group(1),
        "inherits": header.group(3),
        "lineStart": start + 1,
        "lines": lines[start + 1 : stop],
        "name": header.group(2),
    }


def _assignments(block: Mapping[str, Any]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    line_start = int(block["lineStart"])
    for offset, raw in enumerate(block["lines"], start=1):
        clean = _strip_comment(str(raw))
        match = _ASSIGNMENT_RE.fullmatch(clean)
        if match is None:
            continue
        result.append(
            {
                "key": match.group(1),
                "line": line_start + offset,
                "value": match.group(2).strip(),
            }
        )
    return result


def _float(value: str, label: str) -> float:
    try:
        result = float(value)
    except ValueError as exc:
        raise ValueError(f"{label} is not numeric: {value!r}") from exc
    if not math.isfinite(result):
        raise ValueError(f"{label} is not finite")
    return result


def _offset(value: str, label: str) -> dict[str, float]:
    fields: dict[str, float] = {}
    for axis, raw in re.findall(
        r"([XYZ])\s*:\s*(-?(?:\d+(?:\.\d*)?|\.\d+))", value, re.IGNORECASE
    ):
        fields[axis.upper()] = _float(raw, label)
    if set(fields) != {"X", "Y", "Z"}:
        raise ValueError(f"{label} must contain X, Y, and Z")
    return {"x": fields["X"], "y": fields["Y"], "z": fields["Z"]}


def _geometry(block: Mapping[str, Any]) -> list[dict[str, Any]]:
    rows = _assignments(block)
    geometries: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    field_names = {
        "geometryname": "name",
        "geometrymajorradius": "majorRadius",
        "geometryminorradius": "minorRadius",
        "geometryheight": "height",
        "geometryoffset": "offset",
        "geometryactive": "active",
    }
    for row in rows:
        folded = str(row["key"]).casefold()
        if folded in {"geometry", "additionalgeometry"}:
            if current is not None:
                geometries.append(current)
            current = {
                "declaration": ("primary" if folded == "geometry" else "additional"),
                "line": row["line"],
                "shape": str(row["value"]).split()[0].upper(),
            }
            continue
        if current is None or folded not in field_names:
            continue
        output_name = field_names[folded]
        value = str(row["value"])
        if output_name in {"majorRadius", "minorRadius", "height"}:
            current[output_name] = _float(value.split()[0], output_name)
        elif output_name == "offset":
            current[output_name] = _offset(value, "GeometryOffset")
        elif output_name == "active":
            normalized = value.split()[0].casefold()
            if normalized not in {"yes", "no"}:
                raise ValueError(f"unsupported GeometryActive value: {value!r}")
            current[output_name] = normalized == "yes"
        else:
            current[output_name] = value.split()[0]
    if current is not None:
        geometries.append(current)
    for index, row in enumerate(geometries):
        missing = sorted({"shape", "majorRadius", "minorRadius", "height"} - set(row))
        if missing:
            raise ValueError(
                f"geometry {index} in {block['name']} is incomplete: {', '.join(missing)}"
            )
        row.setdefault("active", True)
        row.setdefault("name", None)
        row.setdefault("offset", {"x": 0.0, "y": 0.0, "z": 0.0})
    return geometries


def _last_assignment(block: Mapping[str, Any], key: str) -> str | None:
    values = [
        str(row["value"])
        for row in _assignments(block)
        if str(row["key"]).casefold() == key.casefold()
    ]
    return values[-1] if values else None


def _formation_rows(block: Mapping[str, Any]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for row in _assignments(block):
        if str(row["key"]).casefold() != "rankinfo":
            continue
        value = str(row["value"])
        rank = _RANK_RE.search(value)
        unit_type = _UNIT_TYPE_RE.search(value)
        positions = [
            {"x": _float(x, "RankInfo X"), "y": _float(y, "RankInfo Y")}
            for x, y in _POSITION_RE.findall(value)
        ]
        if rank is None or unit_type is None or not positions:
            raise ValueError(f"unsupported RankInfo at line {row['line']}")
        result.append(
            {
                "line": row["line"],
                "positions": positions,
                "rankNumber": int(rank.group(1)),
                "unitType": unit_type.group(1),
            }
        )
    return result


def _object_evidence(
    text: str,
    name: str,
    virtual_path: str,
    *,
    role: str,
) -> dict[str, Any]:
    block = _object_block(text, name)
    return {
        "declaration": block["declaration"],
        "geometry": _geometry(block),
        "inherits": block["inherits"],
        "kindOf": _last_assignment(block, "KindOf"),
        "lineStart": block["lineStart"],
        "name": block["name"],
        "role": role,
        "sourceVirtualPath": virtual_path,
    }


def _read_passability(
    map_root: Path, terrain: Mapping[str, Any]
) -> tuple[bytes, dict[str, Any]]:
    height = terrain.get("height")
    passability = terrain.get("passability")
    blend = terrain.get("blend")
    if not all(isinstance(value, dict) for value in (height, passability, blend)):
        raise ValueError("terrain document is missing height/passability/blend objects")
    assert isinstance(height, dict)
    assert isinstance(passability, dict)
    assert isinstance(blend, dict)
    width = height.get("width")
    rows = height.get("height")
    border = height.get("borderWidth")
    scale = height.get("horizontalScale")
    if not all(isinstance(value, int) for value in (width, rows, border)):
        raise ValueError("terrain grid dimensions are invalid")
    if not isinstance(scale, (int, float)) or not math.isfinite(float(scale)):
        raise ValueError("terrain horizontal scale is invalid")
    if width <= 1 or rows <= 1 or border <= 0 or border * 2 >= min(width, rows):
        raise ValueError("terrain grid bounds are invalid")
    expected_semantics = {
        "bitOrder": "least-significant-bit-first",
        "meaning": "one-is-impassable",
        "rowPadding": True,
        "sourceExact": True,
    }
    for key, expected in expected_semantics.items():
        if passability.get(key) != expected:
            raise ValueError(f"unsupported passability {key}: {passability.get(key)!r}")
    stride = (width + 7) // 8
    if passability.get("rowStrideBytes") != stride:
        raise ValueError("passability row stride mismatch")
    relative = passability.get("path")
    if not isinstance(relative, str) or Path(relative).name != relative:
        raise ValueError("passability path must be a contained file name")
    path = map_root / relative
    payload = path.read_bytes()
    if len(payload) != stride * rows:
        raise ValueError("passability byte length mismatch")
    computed = sum(
        _cell_is_impassable(payload, stride, x, y)
        for y in range(rows)
        for x in range(width)
    )
    stats = blend.get("gridStats")
    if not isinstance(stats, dict) or stats.get("impassable") != computed:
        raise ValueError("passability popcount does not match terrain metadata")
    return payload, {
        "borderWidth": border,
        "height": rows,
        "horizontalScale": float(scale),
        "impassableCount": computed,
        "path": relative,
        "playableGridMaxInclusive": [width - border, rows - border],
        "playableGridMinInclusive": [border, border],
        "rowStrideBytes": stride,
        "sha256": hashlib.sha256(payload).hexdigest(),
        "width": width,
    }


def _cell_is_impassable(payload: bytes, stride: int, grid_x: int, grid_y: int) -> bool:
    return bool(payload[grid_y * stride + grid_x // 8] & (1 << (grid_x % 8)))


def _component_contract(
    payload: bytes,
    grid: Mapping[str, Any],
    starts: Sequence[Mapping[str, Any]],
) -> dict[str, Any]:
    stride = int(grid["rowStrideBytes"])
    min_x, min_y = (int(value) for value in grid["playableGridMinInclusive"])
    max_x, max_y = (int(value) for value in grid["playableGridMaxInclusive"])
    component_by_cell: dict[tuple[int, int], int] = {}
    component_sizes: list[int] = []
    neighbor_offsets = ((0, -1), (1, 0), (0, 1), (-1, 0))
    for y in range(min_y, max_y + 1):
        for x in range(min_x, max_x + 1):
            key = (x, y)
            if key in component_by_cell or _cell_is_impassable(payload, stride, x, y):
                continue
            component_id = len(component_sizes)
            queue: deque[tuple[int, int]] = deque([key])
            component_by_cell[key] = component_id
            size = 0
            while queue:
                current_x, current_y = queue.popleft()
                size += 1
                for offset_x, offset_y in neighbor_offsets:
                    candidate = (current_x + offset_x, current_y + offset_y)
                    if (
                        candidate in component_by_cell
                        or candidate[0] < min_x
                        or candidate[0] > max_x
                        or candidate[1] < min_y
                        or candidate[1] > max_y
                        or _cell_is_impassable(
                            payload, stride, candidate[0], candidate[1]
                        )
                    ):
                        continue
                    component_by_cell[candidate] = component_id
                    queue.append(candidate)
            component_sizes.append(size)

    start_rows: list[dict[str, Any]] = []
    for start in starts:
        x, y = (int(value) for value in start["gridCellRounded"])
        start_rows.append(
            {
                "componentId": component_by_cell.get((x, y)),
                "gridCell": [x, y],
                "impassable": _cell_is_impassable(payload, stride, x, y),
                "name": start["name"],
            }
        )
    same_component = (
        len(start_rows) >= 2
        and start_rows[0]["componentId"] is not None
        and all(
            row["componentId"] == start_rows[0]["componentId"]
            for row in start_rows[1:]
        )
    )
    return {
        "componentCount": len(component_sizes),
        "componentSizesDescending": sorted(component_sizes, reverse=True),
        "derivation": "four-neighbor-connectivity-over-source-impassability-only",
        "neighborOrderForTests": ["north", "east", "south", "west"],
        "playerStartsShareComponent": same_component,
        "startCells": start_rows,
        "status": "converter-derived-not-sage-routing-oracle",
        "walkableCellCount": sum(component_sizes),
    }


def _start_contract(
    waypoints: Mapping[str, Any], horizontal_scale: float
) -> list[dict[str, Any]]:
    if waypoints.get("schema") != "openbfme.sage-waypoints":
        raise ValueError("unsupported waypoints schema")
    bindings = waypoints.get("playerStartBindings")
    starts = waypoints.get("playerStarts")
    if not isinstance(bindings, list) or not isinstance(starts, dict):
        raise ValueError("waypoints document has invalid player starts")
    result: list[dict[str, Any]] = []
    for binding in sorted(bindings, key=lambda row: int(row["playerIndex"])):
        name = binding.get("waypointName")
        if not isinstance(name, str) or not isinstance(starts.get(name), dict):
            raise ValueError("player-start binding does not resolve")
        row = starts[name]
        sage = row.get("sagePosition")
        godot = row.get("godotPosition")
        if not (
            isinstance(sage, list)
            and len(sage) == 3
            and isinstance(godot, list)
            and len(godot) == 3
        ):
            raise ValueError(f"invalid player-start coordinates: {name}")
        grid_x = int(math.floor(float(sage[0]) / horizontal_scale + 0.5))
        grid_y = int(math.floor(float(sage[1]) / horizontal_scale + 0.5))
        result.append(
            {
                "godotPosition": godot,
                "gridCellRounded": [grid_x, grid_y],
                "name": name,
                "playerIndex": int(binding["playerIndex"]),
                "sagePosition": sage,
                "waypointId": int(binding["waypointId"]),
            }
        )
    return result


def _ford_contract(
    water: Mapping[str, Any],
    payload: bytes,
    grid: Mapping[str, Any],
    required_names: Sequence[str] = REQUIRED_FORD_NAMES,
) -> list[dict[str, Any]]:
    if water.get("schema") != "openbfme.sage-water":
        raise ValueError("unsupported water schema")
    rivers = water.get("rivers")
    if not isinstance(rivers, list):
        raise ValueError("water document has invalid rivers")
    by_name = {str(row.get("name")): row for row in rivers if isinstance(row, dict)}
    if any(name not in by_name for name in required_names):
        raise ValueError("water document is missing a required named ford")
    width = int(grid["width"])
    height = int(grid["height"])
    stride = int(grid["rowStrideBytes"])
    scale = float(grid["horizontalScale"])
    result: list[dict[str, Any]] = []
    for name in required_names:
        source = by_name[name]
        sections = source.get("crossSections")
        if not isinstance(sections, list) or len(sections) < 2:
            raise ValueError(f"named ford has insufficient cross sections: {name}")
        probes: list[dict[str, Any]] = []
        for index, section in enumerate(sections):
            if not isinstance(section, dict):
                raise ValueError(f"invalid ford section: {name}")
            v0 = section.get("sageV0")
            v1 = section.get("sageV1")
            if not (
                isinstance(v0, list)
                and len(v0) == 2
                and isinstance(v1, list)
                and len(v1) == 2
            ):
                raise ValueError(f"invalid ford section endpoints: {name}")
            midpoint = [
                (float(v0[0]) + float(v1[0])) / 2.0,
                (float(v0[1]) + float(v1[1])) / 2.0,
            ]
            cell = [
                int(math.floor(midpoint[0] / scale + 0.5)),
                int(math.floor(midpoint[1] / scale + 0.5)),
            ]
            inside = 0 <= cell[0] < width and 0 <= cell[1] < height
            probes.append(
                {
                    "gridCellRounded": cell,
                    "insideGrid": inside,
                    "sectionIndex": index,
                    "sourceImpassable": (
                        _cell_is_impassable(payload, stride, cell[0], cell[1])
                        if inside
                        else None
                    ),
                    "sourceMidpoint": midpoint,
                }
            )
        result.append(
            {
                "crossSections": sections,
                "id": int(source["id"]),
                "midpointPassabilityProbes": probes,
                "name": name,
                "sourceGeometrySha256": _canonical_sha256(sections),
                "waterHeight": source["waterHeight"],
            }
        )
    return result


def _runtime_observation(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {
            "present": False,
            "status": "not-supplied-not-required-for-source-contract",
        }
    payload = path.read_bytes()
    text = payload.decode("utf-8")
    observed = {
        "astarGrid2D": "AStarGrid2D.new()" in text,
        "diagonalModeNever": "DIAGONAL_MODE_NEVER" in text,
        "fordCorridorDilation": "FORD_CORRIDOR_DILATION_CELLS" in text,
        "manhattanHeuristic": text.count("HEURISTIC_MANHATTAN") >= 2,
        "waterPolygonRasterization": "_rasterize_local_polygon" in text,
    }
    return {
        "byteCount": len(payload),
        "observedImplementationTokens": observed,
        "present": True,
        "sha256": hashlib.sha256(payload).hexdigest(),
        "status": "implementation-specific-not-retail-oracle",
        "virtualPath": "game/src/retail_slice/retail_map_data.gd",
    }


def _cached_ini(
    root: Path,
    cache: dict[str, tuple[str, dict[str, Any]]],
    virtual_path: str,
) -> tuple[str, dict[str, Any]]:
    if virtual_path not in cache:
        cache[virtual_path] = _read_ini(root / Path(virtual_path), virtual_path)
    return cache[virtual_path]


def _roster_contract(
    effective_root: Path,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    cache: dict[str, tuple[str, dict[str, Any]]] = {}
    units: list[dict[str, Any]] = []
    for name, movement_class, virtual_path in _UNIT_SPECS:
        text, _source = _cached_ini(effective_root, cache, virtual_path)
        units.append(
            {
                "movementClass": movement_class,
                **_object_evidence(
                    text, name, virtual_path, role="individual-collision-declaration"
                ),
            }
        )

    horde_text, _source = _cached_ini(effective_root, cache, _HORDE_PATH)
    hordes: list[dict[str, Any]] = []
    for name, movement_class, member_type in _HORDE_SPECS:
        block = _object_block(horde_text, name)
        formation_rows = _formation_rows(block)
        hordes.append(
            {
                "authoredFormationRows": formation_rows,
                "authoredPositionCount": sum(
                    len(row["positions"]) for row in formation_rows
                ),
                "formationDepth": _last_assignment(block, "FormationDepth"),
                "formationWidth": _last_assignment(block, "FormationWidth"),
                "geometry": _geometry(block),
                "kindOf": _last_assignment(block, "KindOf"),
                "lineStart": block["lineStart"],
                "memberType": member_type,
                "movementClass": movement_class,
                "name": block["name"],
                "sourceVirtualPath": _HORDE_PATH,
            }
        )

    buildings: list[dict[str, Any]] = []
    for building_id, definitions in _BUILDING_SPECS:
        rows: list[dict[str, Any]] = []
        for name, virtual_path, role in definitions:
            text, _source = _cached_ini(effective_root, cache, virtual_path)
            rows.append(_object_evidence(text, name, virtual_path, role=role))
        buildings.append(
            {
                "buildingId": building_id,
                "definitions": rows,
                "footprintInterpretation": (
                    "source-geometry-preserved-runtime-state-selection-unproven"
                ),
            }
        )

    sources = sorted(
        (source for _text, source in cache.values()),
        key=lambda row: str(row["virtualPath"]).casefold(),
    )
    return {
        "buildings": buildings,
        "hordes": hordes,
        "units": units,
    }, sources


def _test_vectors(
    payload: bytes,
    grid: Mapping[str, Any],
    starts: Sequence[Mapping[str, Any]],
    fords: Sequence[Mapping[str, Any]],
    reviewed_cells: Sequence[tuple[tuple[int, int], str]] = REVIEWED_PASSABILITY_CELLS,
) -> list[dict[str, Any]]:
    stride = int(grid["rowStrideBytes"])
    vectors: list[dict[str, Any]] = []
    for start in starts:
        x, y = (int(value) for value in start["gridCellRounded"])
        vectors.append(
            {
                "expectedImpassable": _cell_is_impassable(payload, stride, x, y),
                "gridCell": [x, y],
                "id": f"passability-{str(start['name']).casefold().replace('_', '-')}",
                "kind": "exact-source-passability-cell",
            }
        )
    for reviewed_cell, vector_id in reviewed_cells:
        vectors.append(
            {
                "expectedImpassable": _cell_is_impassable(
                    payload, stride, *reviewed_cell
                ),
                "gridCell": list(reviewed_cell),
                "id": vector_id,
                "kind": "exact-source-passability-cell",
            }
        )
    for ford in fords:
        probes = ford["midpointPassabilityProbes"]
        middle = probes[(len(probes) - 1) // 2]
        vectors.append(
            {
                "expectedImpassable": middle["sourceImpassable"],
                "fordName": ford["name"],
                "gridCell": middle["gridCellRounded"],
                "id": f"passability-{ford['name']}-middle-source-section",
                "kind": "source-geometry-midpoint-passability",
                "sectionIndex": middle["sectionIndex"],
            }
        )
    return vectors


def build_fords_navigation_contract(
    map_directory: Path | str,
    effective_assets_root: Path | str,
    *,
    runtime_source: Path | str | None = None,
) -> dict[str, Any]:
    """Build the Fords of Isen II contract (exact historical parameters)."""

    return build_map_navigation_contract(
        map_directory,
        effective_assets_root,
        expected_map_id=EXPECTED_MAP_ID,
        required_crossing_names=REQUIRED_FORD_NAMES,
        expected_player_start_count=2,
        reviewed_passability_cells=REVIEWED_PASSABILITY_CELLS,
        runtime_source=runtime_source,
    )


def build_map_navigation_contract(
    map_directory: Path | str,
    effective_assets_root: Path | str,
    *,
    expected_map_id: str,
    required_crossing_names: Sequence[str] = (),
    expected_player_start_count: int | None = None,
    reviewed_passability_cells: Sequence[tuple[tuple[int, int], str]] = (),
    runtime_source: Path | str | None = None,
) -> dict[str, Any]:
    """Build a deterministic evidence contract without inventing nav semantics.

    The contract logic is map-agnostic: connectivity, starts, water crossings,
    and roster evidence derive entirely from the cooked map documents and the
    retail INI tree.  Per-map facts that cannot be derived — the map identity,
    which named river crossings the authored map requires, how many player
    starts it must declare, and any human-reviewed passability probe cells —
    are explicit parameters rather than module constants.
    """

    if not isinstance(expected_map_id, str) or not _MAP_ID_PATTERN.match(
        expected_map_id
    ):
        raise ValueError(f"invalid map id: {expected_map_id!r}")
    map_slug = expected_map_id.rsplit(".", 1)[-1]
    map_root = Path(map_directory).expanduser().resolve()
    effective_root = Path(effective_assets_root).expanduser().resolve()
    if not map_root.is_dir():
        raise ValueError(f"map directory does not exist: {map_root}")
    if not effective_root.is_dir():
        raise ValueError(f"effective-assets root does not exist: {effective_root}")

    document_names = ("map.json", "terrain.json", "water.json", "waypoints.json")
    documents = {name: _load_json(map_root / name, name) for name in document_names}
    map_document = documents["map.json"]
    terrain = documents["terrain.json"]
    water = documents["water.json"]
    waypoints = documents["waypoints.json"]
    if map_document.get("schema") != "openbfme.map":
        raise ValueError("unsupported map schema")
    if map_document.get("id") != expected_map_id:
        raise ValueError(f"expected {expected_map_id}, got {map_document.get('id')!r}")
    if terrain.get("schema") != "openbfme.sage-terrain":
        raise ValueError("unsupported terrain schema")

    payload, grid = _read_passability(map_root, terrain)
    height = terrain["height"]
    assert isinstance(height, dict)
    starts = _start_contract(waypoints, float(grid["horizontalScale"]))
    if expected_player_start_count is not None:
        if len(starts) != expected_player_start_count:
            raise ValueError(
                f"navigation contract requires {expected_player_start_count} "
                f"player starts, got {len(starts)}"
            )
    elif len(starts) < 2:
        raise ValueError(
            f"multiplayer navigation contract requires at least two player "
            f"starts, got {len(starts)}"
        )
    fords = _ford_contract(water, payload, grid, required_crossing_names)
    components = _component_contract(payload, grid, starts)
    roster, ini_sources = _roster_contract(effective_root)

    waypoint_edges = waypoints.get("edges")
    if not isinstance(waypoint_edges, list):
        raise ValueError("waypoints edges must be an array")
    source_buildability = terrain.get("buildability")
    if not isinstance(source_buildability, dict):
        raise ValueError("terrain buildability evidence is missing")

    map_sources = [
        _file_evidence(map_root / name, f"maps/{map_slug}/{name}")
        for name in document_names
    ]
    map_sources.extend(
        [
            _file_evidence(
                map_root / str(height["heightmap"]["path"]),
                f"maps/{map_slug}/heightmap.r16",
            ),
            _file_evidence(
                map_root / str(grid["path"]),
                f"maps/{map_slug}/impassability.bit",
            ),
        ]
    )
    runtime_path = (
        Path(runtime_source).expanduser().resolve()
        if runtime_source is not None
        else None
    )
    contract: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "mapId": expected_map_id,
        "sourceEvidence": {
            "cookedMapFiles": map_sources,
            "mapSourceBinaryPackaged": map_document.get("sourceBinaryPackaged"),
            "mapSourceSha256": map_document.get("source", {}).get("sha256"),
            "retailIniFiles": ini_sources,
        },
        "coordinateContract": {
            "cellCenterSageWorld": "(grid.x*horizontalScale,grid.y*horizontalScale)",
            "cookedMapTransform": map_document.get("coordinateTransform"),
            "heightSampleOrder": "row-major-y-then-x",
            "roundingForBehavioralProbes": "floor(value/horizontalScale+0.5)",
        },
        "terrainPassability": {
            **grid,
            "meaning": "one-is-impassable",
            "bitOrder": "least-significant-bit-first",
            "rowPadding": True,
            "sourceExact": True,
        },
        "sourceBuildability": {
            "evidence": source_buildability,
            "parityReady": False,
            "status": "not-authored-in-cooked-source-pending-sage-oracle",
        },
        "playerStarts": starts,
        "waterAndFords": {
            "fordCrossingSemantics": "unproven-source-label-and-geometry-only",
            "namedFords": fords,
            "riverCount": len(water.get("rivers", [])),
            "standingWaterAreaCount": len(water.get("standingAreas", [])),
            "standingWaterGeometrySha256": _canonical_sha256(
                water.get("standingAreas", [])
            ),
        },
        "routing": {
            "authoredWaypointEdgeCount": len(waypoint_edges),
            "authoredWaypointEdges": waypoint_edges,
            "authoredWaypointStatus": waypoints.get("routingGraphStatus"),
            "passabilityOnlyConnectivity": components,
            "parityReady": False,
            "status": "no-claim-of-sage-route-choice-or-dynamic-clearance",
        },
        "rosterFootprints": {
            **roster,
            "interpretationPolicy": (
                "exact-ini-declarations-only-no-rasterization-clearance-or-state-guess"
            ),
            "parityReady": False,
        },
        "currentRuntimeObservation": _runtime_observation(runtime_path),
        "behavioralTestVectors": _test_vectors(
            payload, grid, starts, fords, reviewed_passability_cells
        ),
        "blockers": [
            {
                "id": "source-buildability-grid-absent",
                "scope": "building-placement",
                "requiredOracle": "BFME2 placement accept/reject probes or executable rule recovery",
            },
            {
                "id": "water-and-ford-traversal-semantics-unproven",
                "scope": "infantry-and-cavalry-routing",
                "requiredOracle": "BFME2 unit-class crossing probes for each named ford and water edge",
            },
            {
                "id": "unit-and-horde-clearance-rasterization-unproven",
                "scope": "infantry-and-cavalry-routing",
                "requiredOracle": "SAGE pathfinder geometry and LARGE_RECTANGLE_PATHFIND behavior",
            },
            {
                "id": "dynamic-map-object-obstacle-closure-unproven",
                "scope": "routing",
                "requiredOracle": "resolved collision geometry and lifecycle for blocking map objects",
            },
            {
                "id": "building-geometry-state-selection-unproven",
                "scope": "building-footprints",
                "requiredOracle": "SAGE GeometryName upgrade-state and foundation/citadel collision behavior",
            },
            {
                "id": "dynamic-nav-updates-unproven",
                "scope": "routing-and-building-lifecycle",
                "requiredOracle": "BFME2 construction damage destruction and rebuild obstacle probes",
            },
            {
                "id": "route-cost-neighbors-tie-break-and-smoothing-unproven",
                "scope": "routing",
                "requiredOracle": "recorded BFME2 route results for fixed start/destination/unit-class vectors",
            },
        ],
    }
    contract["summary"] = {
        "authoredWaypointEdgeCount": len(waypoint_edges),
        "blockerCount": len(contract["blockers"]),
        "buildingCount": len(roster["buildings"]),
        "hordeCount": len(roster["hordes"]),
        "impassableCount": int(grid["impassableCount"]),
        "namedFordCount": len(fords),
        "parityReady": False,
        "playerStartCount": len(starts),
        "sourceBuildabilityGridPresent": bool(
            source_buildability.get("sourceGridPresent")
        ),
        "unitCount": len(roster["units"]),
    }
    contract["aggregateSha256"] = _canonical_sha256(contract)
    return contract


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map-dir", type=Path, required=True)
    parser.add_argument("--effective-assets", type=Path, required=True)
    parser.add_argument("--runtime-source", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--map-id",
        default=EXPECTED_MAP_ID,
        help="cooked map id (default: the Fords of Isen II contract)",
    )
    parser.add_argument(
        "--expected-player-starts",
        type=int,
        default=None,
        help="exact required player-start count (default: 2 for Fords, "
        "otherwise at-least-two)",
    )
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.map_id == EXPECTED_MAP_ID:
        contract = build_map_navigation_contract(
            args.map_dir,
            args.effective_assets,
            expected_map_id=EXPECTED_MAP_ID,
            required_crossing_names=REQUIRED_FORD_NAMES,
            expected_player_start_count=(
                2
                if args.expected_player_starts is None
                else args.expected_player_starts
            ),
            reviewed_passability_cells=REVIEWED_PASSABILITY_CELLS,
            runtime_source=args.runtime_source,
        )
    else:
        contract = build_map_navigation_contract(
            args.map_dir,
            args.effective_assets,
            expected_map_id=args.map_id,
            expected_player_start_count=args.expected_player_starts,
            runtime_source=args.runtime_source,
        )
    write_json_atomic(args.output, contract)
    print(
        f"{contract['mapId']} navigation contract: "
        f"{contract['summary']['impassableCount']} impassable cells, "
        f"{contract['summary']['namedFordCount']} named fords, "
        f"{contract['summary']['blockerCount']} explicit blockers; "
        f"sha256={contract['aggregateSha256']}"
    )
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
