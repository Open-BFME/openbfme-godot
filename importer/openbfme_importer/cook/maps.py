"""Convert one strict SAGE cooked-map directory into openbfme.map.v1."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import sys
import tempfile
from decimal import Decimal
from pathlib import Path
from typing import Any, Mapping, Sequence

from ..sage_map import convert_sage_map

SCHEMA = "openbfme.map.v1"
_PLAYER_START = re.compile(r"^Player_(\d+)_Start$", re.IGNORECASE)
_PLAYER_OWNER = re.compile(r"(?:^|/)(Player_)(\d+)(?:_|/|$)", re.IGNORECASE)
_RING = ((200, 0), (100, 173), (-100, 173), (-200, 0), (-100, -173), (100, -173))


class MapCookError(ValueError):
    """The cooked source cannot be represented without guessing required facts."""


def _load(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"), parse_float=Decimal)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise MapCookError(f"cannot read {path}: {exc}") from exc


def _decimal_text(value: Decimal) -> str:
    text = format(value, "f")
    if "." in text:
        text = text.rstrip("0").rstrip(".")
    return text or "0"


def _canonical(value: Any) -> str:
    if value is None:
        return "null"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, Decimal):
        return _decimal_text(value)
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    if isinstance(value, list):
        return "[" + ",".join(_canonical(item) for item in value) + "]"
    if isinstance(value, Mapping):
        return "{" + ",".join(
            json.dumps(str(key), ensure_ascii=False) + ":" + _canonical(value[key])
            for key in sorted(value, key=lambda item: str(item))
        ) + "}"
    raise MapCookError(f"unsupported JSON value {type(value).__name__}")


def _write(path: Path, value: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(_canonical(value) + "\n", encoding="utf-8", newline="\n")


def _require_object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise MapCookError(f"{label} must be an object")
    return value


def _require_array(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise MapCookError(f"{label} must be an array")
    return value


def _integer(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, Decimal)):
        raise MapCookError(f"{label} must be an integer")
    number = int(value)
    if Decimal(number) != Decimal(value):
        raise MapCookError(f"{label} must be an integer")
    return number


def _number(value: Any, label: str) -> int | Decimal:
    if isinstance(value, bool) or not isinstance(value, (int, Decimal)):
        raise MapCookError(f"{label} must be numeric")
    return value


def _source_path(root: Path, map_data: Mapping[str, Any], sha256: str) -> str:
    for parent in (root, *root.parents):
        manifest = parent / "provenance" / "manifest.json"
        if manifest.is_file():
            data = _load(manifest)
            manifest_data = _require_object(data, "manifest")
            records = manifest_data.get("sources", manifest_data.get("entries", []))
            for record in _require_array(records, "manifest entries"):
                if not isinstance(record, Mapping):
                    continue
                source = record.get("source", record)
                if isinstance(source, Mapping) and source.get("sha256") == sha256:
                    candidate = source.get("virtual_path")
                    if isinstance(candidate, str) and candidate:
                        return candidate.replace("\\", "/")
            break
    identity = map_data.get("id")
    return str(identity) if isinstance(identity, str) and identity else root.name + ".map"


def _point(values: Any, label: str) -> tuple[int | Decimal, int | Decimal]:
    row = _require_array(values, label)
    if len(row) < 2:
        raise MapCookError(f"{label} must contain x and y")
    return _number(row[0], label + ".x"), _number(row[1], label + ".y")


def _starts(waypoints: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    rows = _require_array(waypoints.get("waypoints"), "waypoints.waypoints")
    for row_value in rows:
        row = _require_object(row_value, "waypoint")
        name = row.get("name")
        if not isinstance(name, str):
            raise MapCookError("waypoint.name must be a string")
        match = _PLAYER_START.match(name)
        if not match:
            continue
        source_index = _integer(row.get("playerIndex", int(match.group(1))), "waypoint.playerIndex")
        index = source_index - 1 if source_index > 0 else source_index
        x, y = _point(row.get("sagePosition"), f"waypoint {name}.sagePosition")
        result[str(index)] = {"x": x, "y": y, "facing": 0}
    if not result:
        raw = _require_object(waypoints.get("playerStarts"), "waypoints.playerStarts")
        for name, values in raw.items():
            match = _PLAYER_START.match(str(name))
            if not match:
                continue
            x, y = _point(values, f"waypoints.playerStarts.{name}")
            result[str(int(match.group(1)) - 1)] = {"x": x, "y": y, "facing": 0}
    if not result:
        raise MapCookError("map has no authored player starts")
    return dict(sorted(result.items(), key=lambda item: int(item[0])))


def _waypoint_map(waypoints: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for row_value in _require_array(waypoints.get("waypoints"), "waypoints.waypoints"):
        row = _require_object(row_value, "waypoint")
        name = row.get("name")
        if not isinstance(name, str) or not name:
            raise MapCookError("waypoint.name must be a non-empty string")
        x, y = _point(row.get("sagePosition"), f"waypoint {name}.sagePosition")
        result[name] = {"x": x, "y": y}
    return result


def _objects(document: Mapping[str, Any]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for value in _require_array(document.get("objects"), "objects.objects"):
        row = _require_object(value, "object")
        template = row.get("typeName")
        if not isinstance(template, str) or not template:
            raise MapCookError("object.typeName must be a non-empty string")
        position = _require_array(row.get("sagePosition"), f"object {template}.sagePosition")
        if len(position) < 3:
            raise MapCookError(f"object {template}.sagePosition must contain x, y, z")
        properties = _require_object(row.get("properties", {}), f"object {template}.properties")
        original = properties.get("originalOwner", "")
        original_owner = original if isinstance(original, str) else str(original)
        owner = original_owner.split("/", 1)[0]
        result.append({
            "template": template,
            "x": _number(position[0], f"object {template}.x"),
            "y": _number(position[1], f"object {template}.y"),
            "z": _number(row.get("worldZ", position[2]), f"object {template}.z"),
            "angle": _number(row.get("sageAngleRadians", 0), f"object {template}.angle"),
            "owner": owner,
            "original_owner": original_owner,
            "properties": dict(properties),
        })
    return result


def _plots(starts: Mapping[str, Mapping[str, Any]], objects: list[dict[str, Any]]) -> list[dict[str, Any]]:
    authored: dict[int, list[dict[str, Any]]] = {}
    for obj in objects:
        template = str(obj["template"]).casefold()
        if not any(token in template for token in ("buildingplot", "expansionpad", "foundationplot")):
            continue
        match = _PLAYER_OWNER.search(str(obj["original_owner"]))
        if match:
            authored.setdefault(int(match.group(2)) - 1, []).append(obj)
    result: list[dict[str, Any]] = []
    for key, start in sorted(starts.items(), key=lambda item: int(item[0])):
        base_index = int(key)
        rows = authored.get(base_index, [])
        if rows:
            for index, row in enumerate(rows):
                result.append({"base_index": base_index, "index": index, "x": row["x"], "y": row["y"], "kind": "structure"})
            continue
        for index, (dx, dy) in enumerate(_RING):
            result.append({
                "base_index": base_index, "index": index,
                "x": start["x"] + dx, "y": start["y"] + dy,
                "kind": "resource" if index % 2 else "structure",
            })
    return result


def build_map_document(root: Path | str, *, source_path: str | None = None) -> dict[str, Any]:
    cooked = Path(root)
    if cooked.is_file():
        cooked = cooked.parent
    map_data = _require_object(_load(cooked / "map.json"), "map.json")
    terrain = _require_object(_load(cooked / str(map_data.get("terrain", "terrain.json"))), "terrain.json")
    height = _require_object(terrain.get("height"), "terrain.height")
    passability = _require_object(terrain.get("passability"), "terrain.passability")
    objects_document = _require_object(_load(cooked / str(map_data.get("objects", "objects.json"))), "objects.json")
    waypoint_document = _require_object(_load(cooked / str(map_data.get("waypoints", "waypoints.json"))), "waypoints.json")
    source = _require_object(map_data.get("source"), "map.source")
    source_sha = source.get("sha256")
    if not isinstance(source_sha, str) or not re.fullmatch(r"[0-9a-f]{64}", source_sha):
        raise MapCookError("map.source.sha256 must be lowercase sha256")
    width = _integer(height.get("width"), "terrain.height.width")
    grid_height = _integer(height.get("height"), "terrain.height.height")
    cell_size = _integer(height.get("horizontalScale"), "terrain.height.horizontalScale")
    row_stride = _integer(passability.get("rowStrideBytes"), "terrain.passability.rowStrideBytes")
    height_bytes = (cooked / str(_require_object(height.get("heightmap"), "terrain.height.heightmap").get("path"))).read_bytes()
    passability_bytes = (cooked / str(passability.get("path"))).read_bytes()
    if len(height_bytes) != width * grid_height * 2:
        raise MapCookError("height binary length does not match its shape")
    if len(passability_bytes) != row_stride * grid_height:
        raise MapCookError("passability binary length does not match its shape")
    starts = _starts(waypoint_document)
    object_rows = _objects(objects_document)
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "source": {"path": source_path or _source_path(cooked, map_data, source_sha), "sha256": source_sha},
        "world": {"width": width * cell_size, "height": grid_height * cell_size, "cell_size": cell_size},
        "height_grid": {"width": width, "height": grid_height, "encoding": "uint16-little-endian-row-major", "data_base64": base64.b64encode(height_bytes).decode("ascii")},
        "passability_grid": {"width": width, "height": grid_height, "encoding": "one-is-impassable-lsb-first-row-padded", "row_stride_bytes": row_stride, "data_base64": base64.b64encode(passability_bytes).decode("ascii")},
        "start_positions": starts,
        "waypoints": _waypoint_map(waypoint_document),
        "objects": object_rows,
        "plots": _plots(starts, object_rows),
    }
    water_path = map_data.get("water")
    if isinstance(water_path, str) and (cooked / water_path).is_file():
        water = _require_object(_load(cooked / water_path), "water.json")
        polygons = []
        for area_value in _require_array(water.get("standingAreas", []), "water.standingAreas"):
            area = _require_object(area_value, "water area")
            polygons.append([{"x": x, "y": y} for x, y in (_point(point, "water point") for point in _require_array(area.get("sagePoints"), "water.sagePoints"))])
        for river_value in _require_array(water.get("rivers", []), "water.rivers"):
            river = _require_object(river_value, "river")
            sections = [
                _require_object(value, "river cross section")
                for value in _require_array(river.get("crossSections"), "river.crossSections")
            ]
            if len(sections) < 2:
                continue
            left = [_point(section.get("sageV0"), "river.sageV0") for section in sections]
            right = [_point(section.get("sageV1"), "river.sageV1") for section in reversed(sections)]
            polygons.append([{"x": x, "y": y} for x, y in left + right])
        result["water"] = {"impassable": False, "polygons": polygons}
    validate_map_document(result)
    return result


def validate_map_document(document: Mapping[str, Any]) -> None:
    required = ("schema", "source", "world", "height_grid", "passability_grid", "start_positions", "waypoints", "objects", "plots")
    missing = [key for key in required if key not in document]
    if missing:
        raise MapCookError("map-v1 missing required keys: " + ", ".join(missing))
    if document.get("schema") != SCHEMA:
        raise MapCookError(f"map-v1 schema must be {SCHEMA}")
    for key in ("source", "world", "height_grid", "passability_grid", "start_positions", "waypoints"):
        _require_object(document[key], key)
    for key in ("objects", "plots"):
        _require_array(document[key], key)


def convert_cooked_map(root: Path | str, output: Path | str, *, source_path: str | None = None) -> dict[str, Any]:
    document = build_map_document(root, source_path=source_path)
    _write(Path(output), document)
    return document


def convert_map(path: Path | str, output: Path | str) -> dict[str, Any]:
    source = Path(path)
    if source.suffix.casefold() != ".map":
        return convert_cooked_map(source, output)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix="openbfme-map-v1-") as raw:
        cooked = Path(raw) / "cooked"
        convert_sage_map(source, cooked)
        return convert_cooked_map(cooked, output, source_path=source.as_posix())


def convert_from_pack(pack: Path | str, name: str, output: Path | str) -> dict[str, Any]:
    root = Path(pack)
    catalog = _require_object(_load(root / "data" / "maps.json"), "data/maps.json")
    wanted = name.casefold()
    matches = []
    for value in _require_array(catalog.get("maps"), "maps"):
        row = _require_object(value, "map catalog row")
        identifiers = {str(row.get(key, "")).casefold() for key in ("id", "displayName", "map")}
        identifiers.add(Path(str(row.get("map", ""))).parent.name.casefold())
        if wanted in identifiers:
            matches.append(row)
    if len(matches) != 1:
        raise MapCookError(f"--name {name!r} matched {len(matches)} maps")
    return convert_cooked_map(root / str(matches[0]["map"]), output)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--map", type=Path)
    source.add_argument("--from-pack", type=Path)
    parser.add_argument("--name", help="Catalog id, display name, or slug for --from-pack")
    parser.add_argument("--out", required=True, type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.from_pack is not None:
            if not args.name:
                raise MapCookError("--from-pack requires --name")
            convert_from_pack(args.from_pack, args.name, args.out)
        else:
            if args.name:
                raise MapCookError("--name is only valid with --from-pack")
            convert_map(args.map, args.out)
    except (MapCookError, OSError, ValueError) as exc:
        print(f"MAP_COOK_FAIL {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
