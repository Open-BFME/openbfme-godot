"""Prove the BFME2 Men HUD frame choice without executing ActionScript.

The oracle is intentionally narrow.  It accepts only the already-attested
five-bundle HUD closure, validates every source byte against that plan, and
decodes the finite timeline and bytecode records that choose the Palantir
frame and the initial side-command-bar state.  Anything outside that finite
surface remains an explicit blocker instead of becoming a guessed frame.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path
from typing import Any, Mapping

from .sage_apt import canonical_sha256, parse_apt_constants, parse_apt_movie


PLAN_SCHEMA = "openbfme.retail-hud-apt-plan"
OUTPUT_SCHEMA = "openbfme.retail-hud-frame-selection"
EXPECTED_SOURCE_COUNT = 188
EXPECTED_SOURCE_BYTES = 7_004_515
EXPECTED_WND_SHA256 = (
    "a509730457224a111af8022df6d0ef373fcaa5d91a102bc15bccf5fc1a54ced6"
)


class HudFrameSelectionError(ValueError):
    """Raised when the exact retail selection proof no longer matches."""


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical_bytes(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


class _Reader:
    def __init__(self, data: bytes, label: str) -> None:
        self.data = data
        self.label = label

    def require(self, offset: int, size: int, context: str) -> None:
        if offset < 0 or size < 0 or offset > len(self.data) - size:
            raise HudFrameSelectionError(
                f"{self.label} {context} range {offset}+{size} is out of bounds"
            )

    def u32(self, offset: int, context: str) -> int:
        self.require(offset, 4, context)
        return struct.unpack_from("<I", self.data, offset)[0]

    def i32(self, offset: int, context: str) -> int:
        self.require(offset, 4, context)
        return struct.unpack_from("<i", self.data, offset)[0]

    def f32(self, offset: int, context: str) -> float:
        self.require(offset, 4, context)
        value = struct.unpack_from("<f", self.data, offset)[0]
        if not math.isfinite(value):
            raise HudFrameSelectionError(f"{self.label} {context} is not finite")
        return value

    def string(self, offset: int, context: str) -> str:
        if not 0 <= offset < len(self.data):
            raise HudFrameSelectionError(f"{self.label} {context} is out of bounds")
        end = self.data.find(b"\0", offset, min(len(self.data), offset + 1025))
        if end < 0:
            raise HudFrameSelectionError(f"{self.label} {context} is unterminated")
        try:
            return self.data[offset:end].decode("cp1252")
        except UnicodeDecodeError as exc:
            raise HudFrameSelectionError(
                f"{self.label} {context} is not CP1252"
            ) from exc


def _safe_source(root: Path, virtual_path: str) -> Path:
    destination = (root / Path(*virtual_path.replace("\\", "/").split("/"))).resolve()
    try:
        destination.relative_to(root.resolve())
    except ValueError as exc:
        raise HudFrameSelectionError(
            f"source escaped the effective-assets root: {virtual_path}"
        ) from exc
    if not destination.is_file() or destination.is_symlink():
        raise HudFrameSelectionError(f"source is missing or linked: {virtual_path}")
    return destination


def _validate_plan(
    plan_path: Path, asset_root: Path
) -> tuple[dict[str, Any], dict[str, bytes], list[dict[str, Any]]]:
    plan_bytes = plan_path.read_bytes()
    try:
        plan = json.loads(plan_bytes)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise HudFrameSelectionError("HUD APT plan is not canonical JSON") from exc
    if plan.get("schema") != PLAN_SCHEMA or plan.get("schemaVersion") != 0:
        raise HudFrameSelectionError("HUD APT plan schema changed")
    expected_plan_sha = plan.get("aggregateSha256")
    unsigned = dict(plan)
    unsigned.pop("aggregateSha256", None)
    if expected_plan_sha != canonical_sha256(unsigned):
        raise HudFrameSelectionError("HUD APT plan aggregate changed")

    evidence = plan.get("sourceEvidence")
    if not isinstance(evidence, Mapping):
        raise HudFrameSelectionError("HUD APT source evidence is missing")
    groups = evidence.get("groups")
    if not isinstance(groups, list) or len(groups) != 5:
        raise HudFrameSelectionError("HUD APT five-bundle closure changed")

    sources: dict[str, bytes] = {}
    inventory: list[dict[str, Any]] = []
    for group in groups:
        if not isinstance(group, Mapping) or not isinstance(group.get("files"), list):
            raise HudFrameSelectionError("HUD APT group contract changed")
        rows: list[dict[str, Any]] = []
        for item in group["files"]:
            if not isinstance(item, Mapping):
                raise HudFrameSelectionError("HUD APT file contract changed")
            virtual_path = str(item.get("virtualPath", ""))
            key = virtual_path.casefold()
            if not virtual_path or key in sources:
                raise HudFrameSelectionError(
                    f"duplicate or empty HUD APT source: {virtual_path!r}"
                )
            data = _safe_source(asset_root, virtual_path).read_bytes()
            row = {
                "virtualPath": virtual_path,
                "byteLength": len(data),
                "sha256": _sha(data),
            }
            if row["byteLength"] != item.get("byteLength") or row["sha256"] != item.get(
                "sha256"
            ):
                raise HudFrameSelectionError(
                    f"HUD APT source identity changed: {virtual_path}"
                )
            sources[key] = data
            rows.append(row)
            inventory.append(row)
        rows.sort(key=lambda row: (str(row["virtualPath"]).casefold(), row["virtualPath"]))
        manifest = hashlib.sha256(
            b"".join(
                str(row["virtualPath"]).encode("utf-8")
                + b"\0"
                + str(row["byteLength"]).encode("ascii")
                + b"\0"
                + str(row["sha256"]).encode("ascii")
                + b"\n"
                for row in rows
            )
        ).hexdigest()
        if (
            len(rows) != group.get("fileCount")
            or sum(int(row["byteLength"]) for row in rows) != group.get("payloadBytes")
            or manifest != group.get("manifestSha256")
        ):
            raise HudFrameSelectionError(
                f"HUD APT group manifest changed: {group.get('resourceId')}"
            )

    if len(inventory) != EXPECTED_SOURCE_COUNT or sum(
        int(row["byteLength"]) for row in inventory
    ) != EXPECTED_SOURCE_BYTES:
        raise HudFrameSelectionError("HUD APT closure totals changed")
    inventory.sort(
        key=lambda row: (str(row["virtualPath"]).casefold(), row["virtualPath"])
    )

    wnd = evidence.get("effectiveAssets", {}).get("controlbarWnd", {})
    if not isinstance(wnd, Mapping):
        raise HudFrameSelectionError("controlbar WND evidence changed")
    wnd_path = str(wnd.get("path", ""))
    wnd_data = _safe_source(asset_root, wnd_path).read_bytes()
    if (
        wnd.get("sha256") != EXPECTED_WND_SHA256
        or _sha(wnd_data) != EXPECTED_WND_SHA256
        or len(wnd_data) != wnd.get("size")
    ):
        raise HudFrameSelectionError("controlbar WND identity changed")
    sources[wnd_path.casefold()] = wnd_data
    return plan, sources, inventory


def _frame_table(reader: _Reader, offset: int, count: int) -> list[list[dict[str, Any]]]:
    if not 0 <= count <= 8192:
        raise HudFrameSelectionError("frame count exceeds bounds")
    reader.require(offset, count * 8, "frame table")
    frames: list[list[dict[str, Any]]] = []
    for frame_index in range(count):
        header = offset + frame_index * 8
        item_count = reader.i32(header, "frame item count")
        item_table = reader.u32(header + 4, "frame item table")
        if not 0 <= item_count <= 16_384:
            raise HudFrameSelectionError("frame item count exceeds bounds")
        reader.require(item_table, item_count * 4, "frame item pointer table")
        rows: list[dict[str, Any]] = []
        for item_index in range(item_count):
            item = reader.u32(item_table + item_index * 4, "frame item pointer")
            kind = reader.u32(item, "frame item kind")
            if kind == 1:
                rows.append(
                    {
                        "kind": "action",
                        "sourceOffset": item,
                        "instructionsOffset": reader.u32(item + 4, "action pointer"),
                    }
                )
            elif kind == 2:
                rows.append(
                    {
                        "kind": "label",
                        "sourceOffset": item,
                        "name": reader.string(
                            reader.u32(item + 4, "label name pointer"), "label name"
                        ),
                        "flags": reader.u32(item + 8, "label flags"),
                        "frameId": reader.u32(item + 12, "label frame id"),
                    }
                )
            elif kind == 3:
                flags = reader.u32(item + 4, "place flags")
                if flags & ~0xFF:
                    raise HudFrameSelectionError("place-object flags changed")
                row: dict[str, Any] = {
                    "kind": "place",
                    "sourceOffset": item,
                    "flags": flags,
                    "depth": reader.i32(item + 8, "place depth"),
                    "characterId": reader.i32(item + 12, "place character"),
                    "translation": [
                        reader.f32(item + 32, "place x"),
                        reader.f32(item + 36, "place y"),
                    ],
                }
                if flags & 0x20:
                    row["name"] = reader.string(
                        reader.u32(item + 52, "place name pointer"), "place name"
                    )
                rows.append(row)
            elif kind == 4:
                rows.append(
                    {
                        "kind": "remove",
                        "sourceOffset": item,
                        "depth": reader.i32(item + 4, "remove depth"),
                    }
                )
            elif kind == 5:
                rows.append({"kind": "background", "sourceOffset": item})
            else:
                raise HudFrameSelectionError(
                    f"unknown frame-item kind {kind} at {item}"
                )
        frames.append(rows)
    return frames


def _root_frames(
    data: bytes, movie: Mapping[str, Any], label: str
) -> list[list[dict[str, Any]]]:
    reader = _Reader(data, label)
    root = movie.get("root")
    if not isinstance(root, Mapping):
        raise HudFrameSelectionError(f"{label} root contract changed")
    movie_offset = int(root["entryOffset"]) + 8
    count = reader.i32(movie_offset, "root frame count")
    table = reader.u32(movie_offset + 4, "root frame table")
    if count != root.get("frameCount"):
        raise HudFrameSelectionError(f"{label} root frame count changed")
    return _frame_table(reader, table, count)


def _sprite_frames(
    data: bytes, movie: Mapping[str, Any], character_id: int, label: str
) -> list[list[dict[str, Any]]]:
    characters = movie.get("characters")
    if not isinstance(characters, list) or not 0 <= character_id < len(characters):
        raise HudFrameSelectionError(f"{label} character table changed")
    character = characters[character_id]
    if not isinstance(character, Mapping) or character.get("kind") != "sprite":
        raise HudFrameSelectionError(
            f"{label} character {character_id} is not the expected sprite"
        )
    reader = _Reader(data, label)
    offset = int(character["sourceOffset"])
    count = reader.i32(offset + 8, "sprite frame count")
    table = reader.u32(offset + 12, "sprite frame table")
    if count != character.get("frameCount"):
        raise HudFrameSelectionError(f"{label} sprite frame count changed")
    return _frame_table(reader, table, count)


def _labels(frames: list[list[dict[str, Any]]]) -> dict[str, int]:
    result: dict[str, int] = {}
    for frame_index, rows in enumerate(frames):
        for row in rows:
            if row["kind"] != "label":
                continue
            name = str(row["name"])
            if name in result or row["frameId"] != frame_index:
                raise HudFrameSelectionError(f"duplicate or displaced frame label: {name}")
            result[name] = frame_index
    return result


def _require_script_range(
    data: bytes, start: int, end: int, expected_sha256: str, label: str
) -> dict[str, Any]:
    if not 0 <= start < end <= len(data):
        raise HudFrameSelectionError(f"{label} byte range is out of bounds")
    digest = _sha(data[start:end])
    if digest != expected_sha256:
        raise HudFrameSelectionError(f"{label} byte range changed")
    return {
        "byteRange": [start, end],
        "byteLength": end - start,
        "sha256": digest,
    }


def _constant(entries: list[dict[str, Any]], index: int) -> tuple[int, Any]:
    if not 0 <= index < len(entries):
        raise HudFrameSelectionError(f"constant index {index} is out of bounds")
    row = entries[index]
    return int(row["type"]), row.get("value")


def _assert_palantir_bytecode(
    data: bytes, entries: list[dict[str, Any]]
) -> dict[str, Any]:
    reader = _Reader(data, "Palantir.apt")

    # The root frame-0 action's second constant pool is exactly constants 50..144.
    action = 359_864
    if data[action] != 0x88:
        raise HudFrameSelectionError("Palantir frame-state constant pool moved")
    count = reader.u32(action + 4, "frame-state pool count")
    pointer = reader.u32(action + 8, "frame-state pool pointer")
    pool = [reader.u32(pointer + index * 4, "frame-state pool entry") for index in range(count)]
    if pool != list(range(50, 145)):
        raise HudFrameSelectionError("Palantir frame-state constant pool changed")

    function = 361_168
    if data[function] != 0x8E:
        raise HudFrameSelectionError("SetPalantirFrameState opcode moved")
    aligned = (function + 4) & ~3
    name = reader.string(reader.u32(aligned, "function name"), "function name")
    parameter_count = reader.u32(aligned + 4, "function parameter count")
    parameter_table = reader.u32(aligned + 12, "function parameter table")
    body_size = reader.i32(aligned + 16, "function body size")
    body_start = aligned + 28
    if (
        name != "SetPalantirFrameState"
        or parameter_count != 1
        or body_start != 361_200
        or body_size != 564
        or reader.string(
            reader.u32(parameter_table + 4, "state parameter name"),
            "state parameter name",
        )
        != "state"
    ):
        raise HudFrameSelectionError("SetPalantirFrameState header changed")

    expected_constants = {
        50: "_fadeIn",
        63: "_show",
        66: "_hide",
        90: "_good",
        91: "_evil",
        92: "_double",
        93: "_goodSingle",
        94: "_evilSingle",
        95: "_single",
        105: "PalantirFrame",
    }
    for index, value in expected_constants.items():
        if _constant(entries, index) != (1, value):
            raise HudFrameSelectionError(f"Palantir constant {index} changed")
    expected_registers = {
        183: 4,
        184: 4,
        185: 4,
        186: 4,
        209: 4,
        210: 1,
    }
    for index, value in expected_registers.items():
        if _constant(entries, index) != (4, value):
            raise HudFrameSelectionError(f"Palantir register constant {index} changed")

    # Finite instruction anchors.  They prove the accepted states and that the
    # original state register (4), not a guessed suffix, is passed to the
    # PalantirFrame.gotoAndPlay call.
    anchors = {
        361_241: (0x96, 183),
        361_252: (0xA2, 40),  # local pool[40] -> _good
        361_265: (0x96, 184),
        361_276: (0xA2, 41),  # _evil
        361_336: (0x96, 185),
        361_348: (0xA2, 43),  # _goodSingle
        361_361: (0x96, 186),
        361_372: (0xA2, 44),  # _evilSingle
        361_708: (0x96, 209),  # state register 4
        361_721: (0x96, 210),  # preloaded root register 1
        361_732: (0xAF, 55),  # PalantirFrame
        361_734: (0xB2, 1),  # gotoAndPlay
    }
    for offset, (opcode, value) in anchors.items():
        if data[offset] != opcode:
            raise HudFrameSelectionError(
                f"Palantir selection opcode changed at {offset}"
            )
        if opcode == 0x96:
            aligned_operand = (offset + 4) & ~3
            if reader.u32(aligned_operand, "PushData count") != 1:
                raise HudFrameSelectionError("Palantir PushData count changed")
            values = reader.u32(aligned_operand + 4, "PushData pointer")
            actual = reader.u32(values, "PushData value")
        else:
            actual = data[offset + 1]
        if actual != value:
            raise HudFrameSelectionError(
                f"Palantir selection operand changed at {offset}"
            )

    initial_function = 364_685
    if data[initial_function] != 0x8E:
        raise HudFrameSelectionError("InitialSetup opcode moved")
    initial_aligned = (initial_function + 4) & ~3
    initial_name = reader.string(
        reader.u32(initial_aligned, "InitialSetup name"), "InitialSetup name"
    )
    initial_size = reader.i32(initial_aligned + 16, "InitialSetup body size")
    initial_start = initial_aligned + 28
    if initial_name != "InitialSetup" or initial_start != 364_716 or initial_size != 317:
        raise HudFrameSelectionError("InitialSetup function changed")
    initial_anchors = {
        364_971: (0xA2, 44),  # third pool[44] -> global constant 341 -> _good
        364_984: (0xAF, 45),  # PalantirFrame
        364_986: (0xB2, 46),  # gotoAndPlay
    }
    for offset, expected in initial_anchors.items():
        if (data[offset], data[offset + 1]) != expected:
            raise HudFrameSelectionError(
                f"InitialSetup selection anchor changed at {offset}"
            )
    if _constant(entries, 341) != (1, "_good"):
        raise HudFrameSelectionError("InitialSetup _good constant changed")

    return {
        "setPalantirFrameState": {
            "functionHeaderOffset": function,
            "functionName": name,
            "parameter": "state",
            "acceptedStates": ["_evil", "_evilSingle", "_good", "_goodSingle"],
            "palantirFrameCall": {
                "object": "PalantirFrame",
                "method": "gotoAndPlay",
                "argument": "state",
                "argumentRegister": 4,
                "instructionOffsets": [361_708, 361_721, 361_732, 361_734],
            },
            "body": _require_script_range(
                data,
                body_start,
                body_start + body_size,
                "4353242509133ac3c8b57145e82780bdfbc791902cebcd8a5d5cd2df0a42e3ae",
                "SetPalantirFrameState body",
            ),
        },
        "initialSetup": {
            "functionHeaderOffset": initial_function,
            "functionName": initial_name,
            "selectedState": "_good",
            "target": "PalantirFrame",
            "method": "gotoAndPlay",
            "instructionOffsets": [364_971, 364_984, 364_986],
            "body": _require_script_range(
                data,
                initial_start,
                initial_start + initial_size,
                "55735eb6de14ebf8e03267e14bb52feea4ff51b6dca64d3bba731a450a8e74d6",
                "InitialSetup body",
            ),
        },
    }


def _palantir_contract(
    data: bytes,
    constants_data: bytes,
    export_data: bytes,
    export_constants_data: bytes,
) -> dict[str, Any]:
    constants = parse_apt_constants(constants_data, "Palantir.const")
    movie = parse_apt_movie(data, constants, "Palantir.apt")
    export_constants = parse_apt_constants(
        export_constants_data, "PalantirExport.const"
    )
    export_movie = parse_apt_movie(
        export_data, export_constants, "PalantirExport.apt"
    )
    frames = _sprite_frames(data, movie, 105, "Palantir.apt")
    labels = _labels(frames)
    expected_labels = {
        "_hide": 0,
        "_goodSingle": 9,
        "_good": 19,
        "_evilSingle": 29,
        "_evil": 39,
    }
    if labels != expected_labels:
        raise HudFrameSelectionError("PalantirFrame labels changed")

    imports = {
        int(row["characterId"]): (str(row["movie"]), str(row["symbol"]))
        for row in movie["imports"]
    }
    expected_imports = {
        101: ("PalantirExport", "PalantirFrame_GoodSingle"),
        102: ("PalantirExport", "PalantirFrame_GoodDouble"),
        103: ("PalantirExport", "PalantirFrame_EvilSingle"),
        104: ("PalantirExport", "PalantirFrame_EvilDouble"),
    }
    if {key: imports.get(key) for key in expected_imports} != expected_imports:
        raise HudFrameSelectionError("Palantir frame imports changed")

    placements: dict[str, int] = {}
    for state, frame_index in expected_labels.items():
        if state == "_hide":
            continue
        rows = [row for row in frames[frame_index] if row["kind"] == "place"]
        if len(rows) != 1:
            raise HudFrameSelectionError(f"Palantir state {state} placement changed")
        placements[state] = int(rows[0]["characterId"])
    expected_placements = {
        "_goodSingle": 101,
        "_good": 102,
        "_evilSingle": 103,
        "_evil": 104,
    }
    if placements != expected_placements:
        raise HudFrameSelectionError("Palantir frame placement mapping changed")

    exports: dict[str, set[int]] = {}
    for row in export_movie["exports"]:
        exports.setdefault(str(row["symbol"]), set()).add(int(row["characterId"]))
    expected_exports = {
        "PalantirFrame_GoodSingle": {22},
        "PalantirFrame_GoodDouble": {19},
        "PalantirFrame_EvilSingle": {16},
        "PalantirFrame_EvilDouble": {13},
    }
    if {key: exports.get(key) for key in expected_exports} != expected_exports:
        raise HudFrameSelectionError("PalantirExport symbol mapping changed")

    bytecode = _assert_palantir_bytecode(data, constants["entries"])
    state_rows = []
    for state in ("_good", "_goodSingle", "_evil", "_evilSingle"):
        local_character = placements[state]
        symbol = imports[local_character][1]
        exported_character = next(iter(exports[symbol]))
        state_rows.append(
            {
                "state": state,
                "frameIndex": labels[state],
                "localImportCharacterId": local_character,
                "importMovie": "PalantirExport",
                "importSymbol": symbol,
                "exportCharacterId": exported_character,
            }
        )
    return {
        "source": {
            "aptSha256": _sha(data),
            "constSha256": _sha(constants_data),
            "exportAptSha256": _sha(export_data),
            "exportConstSha256": _sha(export_constants_data),
        },
        "palantirFrame": {
            "rootPlacementName": "PalantirFrame",
            "rootPlacementCharacterId": 105,
            "labels": expected_labels,
            "states": state_rows,
        },
        "bytecode": bytecode,
        "initialSelection": {
            "state": "_good",
            "variant": "good-double",
            "frameIndex": 19,
            "localImportCharacterId": 102,
            "importSymbol": "PalantirFrame_GoodDouble",
            "exportCharacterId": 19,
            "reason": "InitialSetup passes the exact _good state to PalantirFrame.gotoAndPlay",
        },
    }


def _side_command_bar_contract(
    data: bytes, constants_data: bytes, wnd_data: bytes
) -> dict[str, Any]:
    constants = parse_apt_constants(constants_data, "InGameSideCommandBar.const")
    movie = parse_apt_movie(data, constants, "InGameSideCommandBar.apt")
    frames = _root_frames(data, movie, "InGameSideCommandBar.apt")
    labels = _labels(frames)
    expected_labels = {"_hide": 1, "_fadeIn": 11, "_fadeOut": 31}
    if labels != expected_labels:
        raise HudFrameSelectionError("InGameSideCommandBar labels changed")
    placements = [row for row in frames[0] if row["kind"] == "place"]
    if len(placements) != 1:
        raise HudFrameSelectionError("InGameSideCommandBar initial placement changed")
    placement = placements[0]
    expected_translation = [1048.300048828125, 361.29998779296875]
    if (
        placement.get("name") != "ButtonSet"
        or placement.get("characterId") != 21
        or placement.get("translation") != expected_translation
    ):
        raise HudFrameSelectionError("InGameSideCommandBar initial transform changed")

    # Frames 1..9 contain no display-list mutation.  Frame 10 executes Stop,
    # so uncalled authored playback settles while the ButtonSet remains at the
    # exact off-screen frame-0 translation.
    if any(
        row["kind"] in {"place", "remove"}
        for rows in frames[1:11]
        for row in rows
    ):
        raise HudFrameSelectionError("InGameSideCommandBar hidden run changed")
    frame_ten_actions = [row for row in frames[10] if row["kind"] == "action"]
    if len(frame_ten_actions) != 1 or frame_ten_actions[0]["instructionsOffset"] != 9380:
        raise HudFrameSelectionError("InGameSideCommandBar stop action moved")
    if data[9380:9382] != b"\x07\x00":
        raise HudFrameSelectionError("InGameSideCommandBar stop bytecode changed")

    try:
        wnd_text = wnd_data.decode("ascii")
    except UnicodeDecodeError as exc:
        raise HudFrameSelectionError("controlbar WND is not ASCII") from exc
    required_wnd_fragments = (
        'NAME = "ControlBar.wnd:ControlBarParent";',
        'NAME = "ControlBar.wnd:CommandWindow";',
        "STATUS = ENABLED+HIDDEN+SEE_THRU;",
    )
    if any(fragment not in wnd_text for fragment in required_wnd_fragments):
        raise HudFrameSelectionError("controlbar WND visibility contract changed")

    return {
        "source": {
            "aptSha256": _sha(data),
            "constSha256": _sha(constants_data),
            "controlbarWndSha256": _sha(wnd_data),
        },
        "labels": expected_labels,
        "initialPlacement": {
            "frameIndex": 0,
            "name": "ButtonSet",
            "characterId": 21,
            "translation": expected_translation,
        },
        "authoredPlayback": {
            "initialFrame": 0,
            "hiddenLabelFrame": 1,
            "settledFrame": 10,
            "settledInstruction": "Stop",
            "stopInstructionRange": [9380, 9382],
            "displayListMutationFrames1Through10": 0,
        },
        "initialSelection": {
            "state": "hidden-offscreen",
            "visible": False,
            "reason": (
                "authored playback reaches Stop before _fadeIn and preserves the "
                "off-screen ButtonSet translation"
            ),
        },
        "wnd": {
            "commandWindowInitialStatus": ["ENABLED", "HIDDEN", "SEE_THRU"],
            "aptVisibilityTransition": "external FadeIn/FadeOut call required",
        },
    }


def build_hud_frame_selection(
    plan_path: Path | str, asset_root: Path | str, output_path: Path | str
) -> dict[str, Any]:
    """Build the deterministic, payload-free HUD selection contract."""

    plan_file = Path(plan_path)
    root = Path(asset_root)
    plan, sources, inventory = _validate_plan(plan_file, root)

    def source(name: str) -> bytes:
        try:
            return sources[name.casefold()]
        except KeyError as exc:
            raise HudFrameSelectionError(f"required HUD source is missing: {name}") from exc

    palantir = _palantir_contract(
        source("Palantir.apt"),
        source("Palantir.const"),
        source("PalantirExport.apt"),
        source("PalantirExport.const"),
    )
    side = _side_command_bar_contract(
        source("InGameSideCommandBar.apt"),
        source("InGameSideCommandBar.const"),
        source("window/controlbar.wnd"),
    )
    contract: dict[str, Any] = {
        "schema": OUTPUT_SCHEMA,
        "schemaVersion": 0,
        "source": {
            "aptPlanAggregateSha256": plan["aggregateSha256"],
            "aptPlanFileSha256": _sha(plan_file.read_bytes()),
            "fiveBundleFileCount": len(inventory),
            "fiveBundlePayloadBytes": sum(
                int(row["byteLength"]) for row in inventory
            ),
            "fiveBundleInventorySha256": canonical_sha256(inventory),
            "controlbarWndSha256": EXPECTED_WND_SHA256,
        },
        "palantir": palantir,
        "inGameSideCommandBar": side,
        "runtimeContract": {
            "menGoodDefault": "_good",
            "menGoodDefaultVariant": "good-double",
            "menGoodSingleState": "_goodSingle",
            "sideCommandBarDefault": "hidden-offscreen",
            "unknownStatePolicy": "fail-closed",
            "syntheticFallbackAllowed": False,
        },
        "blockers": [
            {
                "code": "men-alignment-to-state-call-is-external",
                "detail": (
                    "APT proves the _good and _goodSingle state mappings, but the "
                    "engine caller decides which state to pass after InitialSetup."
                ),
            },
            {
                "code": "side-command-visibility-is-selection-driven",
                "detail": (
                    "APT starts hidden; a selected-unit/runtime FadeIn call is required "
                    "before the side command bar becomes visible."
                ),
            },
        ],
        "summary": {
            "selectionContractReady": True,
            "initialPalantirVariant": "good-double",
            "initialSideCommandBarVisible": False,
            "parityReady": False,
            "blockerCount": 2,
        },
    }
    contract["aggregateSha256"] = canonical_sha256(contract)
    encoded = _canonical_bytes(contract)
    destination = Path(output_path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_bytes() != encoded:
        destination.write_bytes(encoded)
    return contract


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apt-plan", type=Path, required=True)
    parser.add_argument("--asset-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    contract = build_hud_frame_selection(
        args.apt_plan, args.asset_root, args.output
    )
    print(
        json.dumps(
            {
                "output": str(args.output),
                "aggregateSha256": contract["aggregateSha256"],
                "summary": contract["summary"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
