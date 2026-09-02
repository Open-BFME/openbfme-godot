"""Bounded, non-executing readers for SAGE APT HUD source records.

This module deliberately stops at a structural contract.  It never evaluates
ActionScript.  Byte ranges whose semantics are not implemented are retained as
length-and-SHA256 handoffs so later converter work can be exact without this
foundation pretending to be a Flash runtime.
"""

from __future__ import annotations

import hashlib
import math
import re
import struct
from typing import Any, Iterable


APT_MAGIC = b"Apt Data"
CONST_MAGIC = b"Apt constant file"
CHARACTER_SIGNATURE = 0x09876543

MAX_APT_BYTES = 512 * 1024
MAX_CONST_BYTES = 64 * 1024
MAX_DAT_BYTES = 4 * 1024
MAX_GEOMETRY_BYTES = 64 * 1024
MAX_WND_BYTES = 512 * 1024
MAX_CONSTANTS = 4096
MAX_CHARACTERS = 4096
MAX_FRAMES = 8192
MAX_FRAME_ITEMS = 16_384
MAX_IMPORTS = 64
MAX_EXPORTS = 4096
MAX_STRING_BYTES = 1024
MAX_WINDOWS = 256
MAX_WINDOW_DEPTH = 16

_SAFE_MOVIE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$")
# Import/export symbol identities are never used as filesystem paths, and the
# retail BFME2 shell authors at least one symbol with a trailing space
# (``MenuExport::ButtonMed_Down``).  Symbols therefore admit interior spaces
# while movie names -- which do resolve to source files -- stay strict.
_SAFE_SYMBOL = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.\- ]{0,127}$")
_SAFE_CALLBACK = re.compile(r"^[A-Za-z_][A-Za-z0-9_:.-]{0,127}$")
_SAFE_WND_NAME = re.compile(r"^[A-Za-z0-9_.:-]{0,255}$")
_FLOAT = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][-+]?\d+)?"


class AptParseError(ValueError):
    """Raised when a retail APT source violates the bounded static contract."""


def canonical_sha256(value: object) -> str:
    """Return the canonical compact-JSON digest used by APT plan contracts."""

    import json

    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class _Reader:
    def __init__(self, data: bytes, label: str) -> None:
        self.data = data
        self.label = label

    def require(self, offset: int, size: int, context: str) -> None:
        if offset < 0 or size < 0 or offset > len(self.data) - size:
            raise AptParseError(
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
        result = struct.unpack_from("<f", self.data, offset)[0]
        if not math.isfinite(result):
            raise AptParseError(f"{self.label} {context} is not finite")
        return result

    def string(self, offset: int, context: str) -> str:
        if not 0 <= offset < len(self.data):
            raise AptParseError(f"{self.label} {context} string offset is out of bounds")
        end_limit = min(len(self.data), offset + MAX_STRING_BYTES + 1)
        end = self.data.find(b"\0", offset, end_limit)
        if end < 0:
            raise AptParseError(
                f"{self.label} {context} lacks a bounded NUL terminator"
            )
        try:
            return self.data[offset:end].decode("cp1252")
        except UnicodeDecodeError as exc:
            raise AptParseError(f"{self.label} {context} is not CP1252") from exc

    def list_header(
        self, offset: int, maximum: int, context: str
    ) -> tuple[int, int]:
        count = self.i32(offset, f"{context} count")
        pointer = self.u32(offset + 4, f"{context} pointer")
        if not 0 <= count <= maximum:
            raise AptParseError(f"{self.label} {context} count {count} exceeds bounds")
        if count:
            self.require(pointer, count * 4, f"{context} table")
        elif pointer > len(self.data):
            raise AptParseError(f"{self.label} empty {context} pointer is out of bounds")
        return count, pointer


def parse_apt_constants(data: bytes, virtual_path: str) -> dict[str, Any]:
    """Parse one bounded APT constant table without interpreting script."""

    if len(data) > MAX_CONST_BYTES or len(data) < 32:
        raise AptParseError(f"{virtual_path} violates CONST size bounds")
    reader = _Reader(data, virtual_path)
    if data[:17] != CONST_MAGIC:
        raise AptParseError(f"{virtual_path} has unsupported CONST magic")
    if data[17:20] != b"\x1a\x00\x00":
        raise AptParseError(f"{virtual_path} has unsupported CONST sentinel")
    apt_entry_offset = reader.u32(20, "APT entry offset")
    count = reader.u32(24, "constant count")
    header_size = reader.u32(28, "header size")
    if header_size != 32 or count > MAX_CONSTANTS:
        raise AptParseError(f"{virtual_path} has invalid constant header")
    reader.require(32, count * 8, "constant records")

    entries: list[dict[str, Any]] = []
    strings: list[str] = []
    for index in range(count):
        offset = 32 + index * 8
        kind = reader.u32(offset, f"constant {index} type")
        raw = reader.u32(offset + 4, f"constant {index} value")
        row: dict[str, Any] = {"index": index, "type": kind}
        if kind == 1:
            value: Any = reader.string(raw, f"constant {index}")
            row["value"] = value
            strings.append(value)
        elif kind == 3:
            if raw != 0:
                raise AptParseError(f"{virtual_path} None constant {index} is nonzero")
            row["value"] = None
        elif kind == 4 or kind == 8:
            row["value"] = raw
        elif kind == 5:
            if raw not in (0, 1):
                raise AptParseError(f"{virtual_path} boolean constant is not 0/1")
            row["value"] = bool(raw)
        elif kind == 6:
            value = struct.unpack("<f", struct.pack("<I", raw))[0]
            if not math.isfinite(value):
                raise AptParseError(f"{virtual_path} float constant is not finite")
            row["value"] = value
        elif kind == 7:
            row["value"] = struct.unpack("<i", struct.pack("<I", raw))[0]
        else:
            # Property (2) and any unproven kind are retained, never evaluated.
            row.update(
                {
                    "rawValue": raw,
                    "semantics": "unsupported-constant-kind-preserved",
                }
            )
        entries.append(row)

    return {
        "virtualPath": virtual_path,
        "byteLength": len(data),
        "sha256": _sha(data),
        "aptEntryOffset": apt_entry_offset,
        "constantCount": count,
        "entries": entries,
        "strings": sorted(set(strings), key=lambda value: (value.casefold(), value)),
    }


def parse_apt_dat(data: bytes, virtual_path: str) -> dict[str, Any]:
    """Parse the exact image-to-atlas assignment grammar used by AptToBigc."""

    if len(data) > MAX_DAT_BYTES:
        raise AptParseError(f"{virtual_path} violates DAT size bounds")
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as exc:
        raise AptParseError(f"{virtual_path} DAT is not ASCII") from exc
    mappings: list[dict[str, Any]] = []
    seen: set[int] = set()
    for line_number, raw_line in enumerate(text.splitlines(), 1):
        line = raw_line.split(";", 1)[0].strip()
        if not line:
            continue
        match = re.fullmatch(r"(\d+)\s*(=|->)\s*(.+)", line)
        if not match:
            raise AptParseError(f"{virtual_path}:{line_number} unsupported DAT record")
        image_id = int(match.group(1))
        values = match.group(3).split()
        if len(values) not in (1, 4) or any(
            re.fullmatch(r"-?\d+", value) is None for value in values
        ):
            raise AptParseError(f"{virtual_path}:{line_number} unsupported DAT assignment")
        numbers = [int(value) for value in values]
        texture_id = numbers[0] if len(numbers) == 1 else image_id
        if image_id in seen:
            raise AptParseError(f"{virtual_path} duplicate DAT image id {image_id}")
        seen.add(image_id)
        row: dict[str, Any] = {
            "imageId": image_id,
            "operator": match.group(2),
            "textureId": texture_id,
        }
        if len(numbers) == 4:
            bounds = numbers
            if bounds[2] <= 0 or bounds[3] <= 0 or min(bounds[:2]) < 0:
                raise AptParseError(f"{virtual_path} invalid DAT atlas bounds")
            row["bounds"] = bounds
        mappings.append(row)
    mappings.sort(key=lambda item: int(item["imageId"]))
    return {
        "virtualPath": virtual_path,
        "byteLength": len(data),
        "sha256": _sha(data),
        "mappingCount": len(mappings),
        "mappings": mappings,
    }


def _finite_number(value: str, context: str) -> float:
    try:
        result = float(value)
    except ValueError as exc:
        raise AptParseError(f"invalid geometry number in {context}") from exc
    if not math.isfinite(result):
        raise AptParseError(f"non-finite geometry number in {context}")
    return result


def parse_apt_geometry(data: bytes, virtual_path: str) -> dict[str, Any]:
    """Validate one RU geometry file and return a payload-free shape summary."""

    if not data or len(data) > MAX_GEOMETRY_BYTES:
        raise AptParseError(f"{virtual_path} violates geometry size bounds")
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as exc:
        raise AptParseError(f"{virtual_path} geometry is not ASCII") from exc
    style: str | None = None
    style_count = 0
    triangle_count = 0
    line_count = 0
    image_ids: set[int] = set()
    coordinates: list[tuple[float, float]] = []
    for line_number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        parts = [part.strip() for part in line[1:].split(":")]
        if line.startswith("c"):
            if line != "c":
                raise AptParseError(f"{virtual_path}:{line_number} malformed clear")
            style = None
        elif line.startswith("s"):
            if not parts:
                raise AptParseError(f"{virtual_path}:{line_number} empty style")
            style = parts[0]
            expected = {"s": 5, "l": 6, "tc": 12}.get(style)
            if expected is None or len(parts) != expected:
                raise AptParseError(f"{virtual_path}:{line_number} unsupported style")
            for value in parts[1:]:
                _finite_number(value, f"{virtual_path}:{line_number}")
            if style == "tc":
                image = int(parts[5])
                if image < 0:
                    raise AptParseError(f"{virtual_path}:{line_number} negative image id")
                image_ids.add(image)
            style_count += 1
        elif line.startswith("t"):
            if style not in ("s", "tc") or len(parts) != 6:
                raise AptParseError(f"{virtual_path}:{line_number} triangle outside style")
            values = [_finite_number(v, f"{virtual_path}:{line_number}") for v in parts]
            coordinates.extend(zip(values[0::2], values[1::2], strict=True))
            triangle_count += 1
        elif line.startswith("l"):
            if style != "l" or len(parts) != 4:
                raise AptParseError(f"{virtual_path}:{line_number} line outside style")
            values = [_finite_number(v, f"{virtual_path}:{line_number}") for v in parts]
            coordinates.extend(zip(values[0::2], values[1::2], strict=True))
            line_count += 1
        else:
            raise AptParseError(f"{virtual_path}:{line_number} unknown geometry record")
        if triangle_count + line_count > 65_536:
            raise AptParseError(f"{virtual_path} geometry primitive limit exceeded")
    bounds = None
    if coordinates:
        xs = [item[0] for item in coordinates]
        ys = [item[1] for item in coordinates]
        bounds = [min(xs), min(ys), max(xs), max(ys)]
    return {
        "virtualPath": virtual_path,
        "byteLength": len(data),
        "sha256": _sha(data),
        "styleCount": style_count,
        "triangleCount": triangle_count,
        "lineCount": line_count,
        "imageIds": sorted(image_ids),
        "bounds": bounds,
    }


_CHARACTER_NAMES = {
    1: "shape",
    2: "text",
    3: "font",
    4: "button",
    5: "sprite",
    6: "sound",
    7: "image",
    8: "morph",
    9: "movie",
    10: "static-text",
    11: "none",
    12: "video",
}
_FRAME_ITEM_NAMES = {
    1: "action-script",
    2: "frame-label",
    3: "place-object",
    4: "remove-object",
    5: "background-color",
    8: "init-action-script",
}


def _handoff(data: bytes, start: int, end: int, reason: str) -> dict[str, Any]:
    if not 0 <= start < end <= len(data):
        raise AptParseError("invalid opaque handoff range")
    return {
        "byteRange": [start, end],
        "byteLength": end - start,
        "sha256": _sha(data[start:end]),
        "semantics": reason,
    }


def _pointer_spans(data: bytes, pointers: Iterable[int]) -> dict[int, int]:
    ordered = sorted({pointer for pointer in pointers if 0 < pointer < len(data)})
    return {
        start: (ordered[index + 1] if index + 1 < len(ordered) else len(data))
        for index, start in enumerate(ordered)
    }


def parse_apt_movie(
    data: bytes,
    constants: dict[str, Any],
    virtual_path: str,
    *,
    max_bytes: int = MAX_APT_BYTES,
    max_exports: int = MAX_EXPORTS,
) -> dict[str, Any]:
    """Decode safe root identities and classify timelines without execution.

    ``max_bytes`` exists because the bound is a per-closure statement, not a
    format property: the BFME2 HUD/shell movies all fit 512 KiB, but RotWK's
    ``LivingWorldUI.apt`` is 799,258 bytes.  A caller that admits a larger
    movie must say so explicitly; the default keeps every existing closure
    exactly as strict as before.

    ``max_exports`` is the same statement about the export table.  BFME2's
    ``MenuExport.apt`` exports 1,334 symbols; RotWK's rewrites the same
    library with 4,574.  That is a MEASURED edition difference, not slack in
    the format, so the RotWK strategic closure raises the bound explicitly
    and every other caller keeps the 4,096 default untouched.
    """

    if max_bytes > 4 * 1024 * 1024:
        raise AptParseError(f"{virtual_path} declared APT bound exceeds 4 MiB")
    if not 1 <= max_exports <= 16 * 1024:
        raise AptParseError(f"{virtual_path} declared export bound is out of range")
    if len(data) > max_bytes or len(data) < 12:
        raise AptParseError(f"{virtual_path} violates APT size bounds")
    if data[:8] != APT_MAGIC or data[8:9] != b":" or data[10:12] != b"\x1a\x00":
        raise AptParseError(f"{virtual_path} has unsupported APT magic")
    if data[9:10] not in (b"6", b"7"):
        raise AptParseError(f"{virtual_path} has unsupported APT version")
    reader = _Reader(data, virtual_path)
    entry = constants.get("aptEntryOffset")
    if not isinstance(entry, int):
        raise AptParseError(f"{virtual_path} constant contract lacks entry offset")
    reader.require(entry, 60, "root movie header")
    if reader.u32(entry, "root type") != 9:
        raise AptParseError(f"{virtual_path} root is not a movie")
    if reader.u32(entry + 4, "root signature") != CHARACTER_SIGNATURE:
        raise AptParseError(f"{virtual_path} root signature changed")
    movie = entry + 8
    frame_count, frame_table = reader.list_header(movie, MAX_FRAMES, "frames")
    unknown = reader.u32(movie + 8, "movie unknown field")
    character_count, character_table = reader.list_header(
        movie + 12, MAX_CHARACTERS, "characters"
    )
    width = reader.u32(movie + 20, "movie width")
    height = reader.u32(movie + 24, "movie height")
    milliseconds = reader.u32(movie + 28, "movie frame duration")
    import_count, import_table = reader.list_header(movie + 32, MAX_IMPORTS, "imports")
    export_count, export_table = reader.list_header(movie + 40, max_exports, "exports")
    if not 1 <= width <= 8192 or not 1 <= height <= 8192 or not 1 <= milliseconds <= 60_000:
        raise AptParseError(f"{virtual_path} movie dimensions/rate violate bounds")

    imports: list[dict[str, Any]] = []
    reader.require(import_table, import_count * 16, "import records")
    for index in range(import_count):
        offset = import_table + index * 16
        movie_name = reader.string(reader.u32(offset, "import movie"), "import movie")
        symbol = reader.string(reader.u32(offset + 4, "import name"), "import name")
        if not _SAFE_MOVIE.fullmatch(movie_name) or not _SAFE_SYMBOL.fullmatch(symbol):
            raise AptParseError(f"{virtual_path} import contains an unsafe identity")
        imports.append(
            {
                "movie": movie_name,
                "symbol": symbol,
                "characterId": reader.u32(offset + 8, "import character"),
                "pointer": reader.u32(offset + 12, "import pointer"),
            }
        )
    # The invariant that matters is that every import binds a DISTINCT local
    # character slot, because that id is what `_resolve` looks up.  Importing
    # the same symbol twice is authored and legal: MpGameSetup binds
    # `GameWindowGadgets::ComboBox` to slots 21 and 210, and it is the only
    # movie in the 84-movie tree that does so - while ZERO movies duplicate a
    # character id.  Rejecting the symbol pair took the multiplayer game-setup
    # screen out of the lane over a legal authoring pattern.
    if len({int(row["characterId"]) for row in imports}) != len(imports):
        raise AptParseError(f"{virtual_path} has duplicate import character ids")
    imports.sort(key=lambda row: (row["movie"].casefold(), row["symbol"].casefold()))

    export_rows: list[dict[str, Any]] = []
    reader.require(export_table, export_count * 8, "export records")
    for index in range(export_count):
        offset = export_table + index * 8
        name = reader.string(reader.u32(offset, "export name"), "export name")
        if not _SAFE_SYMBOL.fullmatch(name):
            raise AptParseError(f"{virtual_path} export contains an unsafe identity")
        export_rows.append(
            {
                "sourceIndex": index,
                "symbol": name,
                "characterId": reader.u32(offset + 4, "export character"),
            }
        )
    grouped_exports: dict[str, list[dict[str, Any]]] = {}
    for row in export_rows:
        key = str(row["symbol"]).casefold()
        grouped_exports.setdefault(key, []).append(row)
    exports = sorted(
        export_rows,
        key=lambda row: (str(row["symbol"]).casefold(), int(row["sourceIndex"])),
    )
    duplicate_exports = [
        {
            "symbol": rows[0]["symbol"],
            "occurrenceCount": len(rows),
            "characterIdsInSourceOrder": [row["characterId"] for row in rows],
            "semantics": "source-order-preserved-runtime-selection-unresolved",
        }
        for rows in grouped_exports.values()
        if len(rows) > 1
    ]
    duplicate_exports.sort(key=lambda row: str(row["symbol"]).casefold())

    character_pointers = [
        reader.u32(character_table + index * 4, f"character pointer {index}")
        for index in range(character_count)
    ]
    all_frame_item_pointers: list[int] = []
    frame_headers: list[tuple[int, int, list[int]]] = []
    total_items = 0
    reader.require(frame_table, frame_count * 8, "inline frame records")
    for frame_index in range(frame_count):
        pointer = frame_table + frame_index * 8
        item_count, item_table = reader.list_header(
            pointer, MAX_FRAME_ITEMS, f"frame {frame_index} items"
        )
        total_items += item_count
        if total_items > MAX_FRAME_ITEMS:
            raise AptParseError(f"{virtual_path} total frame-item bound exceeded")
        items = [
            reader.u32(item_table + index * 4, f"frame {frame_index} item pointer")
            for index in range(item_count)
        ]
        all_frame_item_pointers.extend(items)
        frame_headers.append((frame_index, pointer, items))

    known_pointers = (
        character_pointers
        + all_frame_item_pointers
        + [frame_table, character_table, import_table, export_table, entry]
    )
    spans = _pointer_spans(data, known_pointers)
    characters: list[dict[str, Any]] = []
    for character_id, pointer in enumerate(character_pointers):
        if pointer == 0:
            characters.append({"characterId": character_id, "kind": "null"})
            continue
        reader.require(pointer, 8, f"character {character_id} header")
        kind = reader.u32(pointer, f"character {character_id} kind")
        signature = reader.u32(pointer + 4, f"character {character_id} signature")
        if signature != CHARACTER_SIGNATURE:
            raise AptParseError(f"{virtual_path} character signature changed")
        row: dict[str, Any] = {
            "characterId": character_id,
            "kind": _CHARACTER_NAMES.get(kind, f"unknown-{kind}"),
            "sourceOffset": pointer,
        }
        if kind == 1:
            reader.require(pointer, 28, "shape character")
            row["geometryId"] = reader.u32(pointer + 24, "shape geometry")
        elif kind == 7:
            reader.require(pointer, 12, "image character")
            row["textureId"] = reader.u32(pointer + 8, "image texture")
        elif kind == 5:
            nested_count, nested_table = reader.list_header(
                pointer + 8, MAX_FRAMES, "sprite frames"
            )
            row.update({"frameCount": nested_count, "frameTableOffset": nested_table})
        elif kind != 9 or pointer != entry:
            end = spans.get(pointer, len(data))
            if end <= pointer:
                raise AptParseError(f"{virtual_path} invalid character span")
            row["handoff"] = _handoff(
                data, pointer, end, "character-semantics-not-yet-converted"
            )
        characters.append(row)

    timelines: list[dict[str, Any]] = []
    item_spans = _pointer_spans(data, known_pointers)
    unsupported_kinds: set[int] = set()
    script_count = 0
    for frame_index, pointer, items in frame_headers:
        item_rows: list[dict[str, Any]] = []
        for item_pointer in items:
            if item_pointer == 0:
                item_rows.append({"kind": "null"})
                continue
            kind = reader.u32(item_pointer, "frame item kind")
            name = _FRAME_ITEM_NAMES.get(kind, f"unknown-{kind}")
            if kind not in _FRAME_ITEM_NAMES:
                unsupported_kinds.add(kind)
            if kind in (1, 8):
                script_count += 1
            end = item_spans.get(item_pointer, len(data))
            if end <= item_pointer:
                raise AptParseError(f"{virtual_path} invalid frame-item span")
            item_rows.append(
                {
                    "kind": name,
                    "sourceOffset": item_pointer,
                    "handoff": _handoff(
                        data,
                        item_pointer,
                        end,
                        (
                            "action-bytecode-not-executed"
                            if kind in (1, 8)
                            else "timeline-runtime-semantics-not-yet-converted"
                        ),
                    ),
                }
            )
        timelines.append(
            {
                "frameIndex": frame_index,
                "sourceOffset": pointer if pointer else None,
                "items": item_rows,
            }
        )

    return {
        "virtualPath": virtual_path,
        "byteLength": len(data),
        "sha256": _sha(data),
        "aptVersion": int(chr(data[9])),
        "root": {
            "entryOffset": entry,
            "unknownField": unknown,
            "width": width,
            "height": height,
            "millisecondsPerFrame": milliseconds,
            "frameCount": frame_count,
            "characterCount": character_count,
            "importCount": import_count,
            "exportCount": export_count,
        },
        "imports": imports,
        "exports": exports,
        "duplicateExports": duplicate_exports,
        "characters": characters,
        "timelines": timelines,
        "unsupportedSemantics": {
            "actionScriptExecution": "forbidden",
            "actionScriptHandoffCount": script_count,
            "unknownFrameItemKinds": sorted(unsupported_kinds),
            "timelineRuntime": "not-implemented-fail-closed",
        },
    }


def parse_tga_identity(data: bytes, virtual_path: str) -> dict[str, Any]:
    """Validate the bounded uncompressed 32-bit HUD atlas header."""

    if len(data) < 18 or len(data) > 8 * 1024 * 1024:
        raise AptParseError(f"{virtual_path} violates TGA size bounds")
    id_length, color_map_type, image_type = struct.unpack_from("<BBB", data, 0)
    width, height, depth, descriptor = struct.unpack_from("<HHBB", data, 12)
    if color_map_type != 0 or image_type != 2 or depth != 32:
        raise AptParseError(f"{virtual_path} is not an uncompressed 32-bit TGA")
    if not 1 <= width <= 2048 or not 1 <= height <= 2048:
        raise AptParseError(f"{virtual_path} TGA dimensions violate bounds")
    expected = 18 + id_length + width * height * 4
    footer = b"\x00" * 8 + b"TRUEVISION-XFILE.\x00"
    if len(data) == expected + len(footer) and data[-len(footer) :] == footer:
        has_footer = True
    elif len(data) == expected:
        has_footer = False
    else:
        raise AptParseError(f"{virtual_path} TGA payload length is inconsistent")
    return {
        "virtualPath": virtual_path,
        "byteLength": len(data),
        "sha256": _sha(data),
        "width": width,
        "height": height,
        "pixelFormat": "bgra8",
        "originTop": bool(descriptor & 0x20),
        "alphaBits": descriptor & 0x0F,
        "tga20Footer": has_footer,
    }


def _parse_rect(statement: str, context: str) -> dict[str, list[int]]:
    match = re.fullmatch(
        r"SCREENRECT\s*=\s*UPPERLEFT:\s*(-?\d+)\s+(-?\d+),\s*"
        r"BOTTOMRIGHT:\s*(-?\d+)\s+(-?\d+),\s*"
        r"CREATIONRESOLUTION:\s*(\d+)\s+(\d+)\s*;",
        statement,
        flags=re.IGNORECASE,
    )
    if not match:
        raise AptParseError(f"{context} malformed SCREENRECT")
    values = [int(value) for value in match.groups()]
    if any(value <= 0 or value > 16_384 for value in values[4:]):
        raise AptParseError(f"{context} authored resolution is out of bounds")
    if values[2] < values[0] or values[3] < values[1]:
        raise AptParseError(f"{context} inverted SCREENRECT")
    return {
        "upperLeft": values[:2],
        "bottomRight": values[2:4],
        "creationResolution": values[4:],
    }


def parse_wnd_layout(data: bytes, virtual_path: str) -> dict[str, Any]:
    """Read the bounded controlbar WND tree without invoking callbacks."""

    if not data or len(data) > MAX_WND_BYTES:
        raise AptParseError(f"{virtual_path} violates WND size bounds")
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as exc:
        raise AptParseError(f"{virtual_path} WND is not ASCII") from exc
    lines = text.splitlines()
    if not lines or lines[0].strip() != "FILE_VERSION = 2;":
        raise AptParseError(f"{virtual_path} is not WND version 2")
    windows: list[dict[str, Any]] = []
    stack: list[int] = []
    pending_child = False
    statement = ""
    selected = {
        "WINDOWTYPE",
        "SCREENRECT",
        "NAME",
        "STATUS",
        "STYLE",
        "SYSTEMCALLBACK",
        "INPUTCALLBACK",
        "TOOLTIPCALLBACK",
        "DRAWCALLBACK",
    }

    def apply(raw: str) -> None:
        if not stack:
            return
        key = raw.split("=", 1)[0].strip().upper()
        if key not in selected:
            return
        current = windows[stack[-1]]
        if key == "SCREENRECT":
            current["screenRect"] = _parse_rect(raw, f"{virtual_path} window")
            return
        value = raw.split("=", 1)[1].strip().removesuffix(";").strip()
        if key in {"NAME", "SYSTEMCALLBACK", "INPUTCALLBACK", "TOOLTIPCALLBACK", "DRAWCALLBACK"}:
            if len(value) < 2 or value[0] != '"' or value[-1] != '"':
                raise AptParseError(f"{virtual_path} malformed {key}")
            value = value[1:-1]
        if key == "NAME":
            if value and not _SAFE_WND_NAME.fullmatch(value):
                raise AptParseError(f"{virtual_path} unsafe window name")
            current["name"] = value
        elif key.endswith("CALLBACK"):
            canonical = None if value.casefold() == "[none]" else value
            if canonical is not None and not _SAFE_CALLBACK.fullmatch(canonical):
                raise AptParseError(f"{virtual_path} unsafe callback identity")
            current.setdefault("callbacks", {})[
                {
                    "SYSTEMCALLBACK": "system",
                    "INPUTCALLBACK": "input",
                    "TOOLTIPCALLBACK": "tooltip",
                    "DRAWCALLBACK": "draw",
                }[key]
            ] = canonical
        elif key in ("STATUS", "STYLE"):
            values = sorted(
                {part.strip() for part in value.split("+") if part.strip()},
                key=str.casefold,
            )
            current[key.casefold()] = values
        else:
            current["windowType"] = value

    for line_number, raw_line in enumerate(lines[1:], 2):
        stripped = raw_line.strip()
        if not stripped:
            continue
        if statement:
            statement += " " + stripped
            if ";" in stripped:
                apply(statement)
                statement = ""
            continue
        if stripped == "CHILD":
            pending_child = True
        elif stripped == "WINDOW":
            if len(windows) >= MAX_WINDOWS or len(stack) >= MAX_WINDOW_DEPTH:
                raise AptParseError(f"{virtual_path} window tree exceeds bounds")
            parent = stack[-1] if pending_child and stack else None
            index = len(windows)
            windows.append({"index": index, "parentIndex": parent})
            stack.append(index)
            pending_child = False
        elif stripped == "END":
            if not stack:
                raise AptParseError(f"{virtual_path}:{line_number} unmatched END")
            stack.pop()
        elif "=" in stripped:
            key = stripped.split("=", 1)[0].strip().upper()
            if key in selected:
                statement = stripped
                if ";" in stripped:
                    apply(statement)
                    statement = ""
    if statement or stack or not windows:
        raise AptParseError(f"{virtual_path} has an incomplete window tree")
    required = {"windowType", "screenRect", "name", "status", "style", "callbacks"}
    for row in windows:
        if not required.issubset(row):
            raise AptParseError(f"{virtual_path} window {row['index']} lacks core fields")
    callbacks = sorted(
        {
            callback
            for window in windows
            for callback in window["callbacks"].values()
            if callback is not None
        },
        key=lambda value: (value.casefold(), value),
    )
    return {
        "virtualPath": virtual_path,
        "byteLength": len(data),
        "sha256": _sha(data),
        "fileVersion": 2,
        "authoredResolution": [800, 600],
        "windowCount": len(windows),
        "callbacks": callbacks,
        "windows": windows,
        "callbackSemantics": "identities-only-not-invoked",
    }


__all__ = [
    "AptParseError",
    "canonical_sha256",
    "parse_apt_constants",
    "parse_apt_dat",
    "parse_apt_geometry",
    "parse_apt_movie",
    "parse_tga_identity",
    "parse_wnd_layout",
]
