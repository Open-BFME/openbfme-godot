"""Bounded, payload-free parsing for SAGE ``MappedImage`` definitions.

The parser intentionally understands only the fields required to resolve an
atlas crop.  Returned records contain logical identifiers and normalized
relative texture names; source bytes and filesystem paths never escape into
the neutral result.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
import re
from typing import Iterable

from .paths import safe_relative_parts


MAX_MAPPED_IMAGE_SOURCE_BYTES = 16 * 1024 * 1024
MAX_MAPPED_IMAGE_SOURCES = 4_096
MAX_MAPPED_IMAGE_TOTAL_BYTES = 128 * 1024 * 1024
MAX_MAPPED_IMAGE_BLOCKS = 8_192
MAX_MAPPED_IMAGE_REQUESTS = 8_192
MAX_MAPPED_IMAGE_TEXTURE_PATHS = 250_000
MAX_MAPPED_IMAGE_ASSIGNMENTS = 1_024
MAX_MAPPED_IMAGE_DIMENSION = 32_768

_IDENTIFIER = re.compile(
    r"^[A-Za-z0-9_](?:[A-Za-z0-9_. '\-]{0,126}[A-Za-z0-9_.'\-])?$"
)
_HEADER = re.compile(
    r"^MappedImage\s+"
    r"([A-Za-z0-9_](?:[A-Za-z0-9_. '\-]{0,126}[A-Za-z0-9_.'\-])?)\s*$",
    re.IGNORECASE,
)
_ASSIGNMENT = re.compile(r"^([^=\s]+)\s*=\s*(.*?)\s*$")
_COORD_ITEM = re.compile(
    r"([A-Za-z]+)(\s*:\s*|\s+)([^\s,]+)(?:\s*,\s*|\s+|$)",
    re.IGNORECASE,
)


@dataclass(frozen=True, slots=True)
class MappedImageRecord:
    """Canonical, source-neutral atlas metadata for one mapped image."""

    id: str
    texture: str
    texture_width: int
    texture_height: int
    left: int
    top: int
    right: int
    bottom: int

    def neutral(self) -> dict[str, object]:
        """Return a JSON-ready record without source paths or retail bytes."""

        return {
            "id": self.id,
            "texture": self.texture,
            "textureWidth": self.texture_width,
            "textureHeight": self.texture_height,
            "coords": {
                "left": self.left,
                "top": self.top,
                "right": self.right,
                "bottom": self.bottom,
            },
        }


@dataclass(frozen=True, slots=True)
class MappedImageResolution:
    """Exact requested definitions plus explicit source-authored gaps."""

    records: tuple[MappedImageRecord, ...]
    missing_ids: tuple[str, ...]
    ambiguous_ids: tuple[str, ...]


def _decode(source: bytes) -> str:
    if not isinstance(source, bytes):
        raise TypeError("MappedImage source must be bytes")
    if len(source) > MAX_MAPPED_IMAGE_SOURCE_BYTES:
        raise ValueError(
            f"MappedImage source exceeds {MAX_MAPPED_IMAGE_SOURCE_BYTES} byte limit"
        )
    if b"\0" in source:
        raise ValueError("MappedImage source contains a NUL byte")
    return source.decode("cp1252")


def _strip_comment(raw: str) -> str:
    quoted = False
    index = 0
    while index < len(raw):
        character = raw[index]
        if character == '"':
            quoted = not quoted
        elif not quoted and character == ";":
            return raw[:index].strip()
        elif (
            not quoted
            and character == "/"
            and index + 1 < len(raw)
            and raw[index + 1] == "/"
        ):
            return raw[:index].strip()
        index += 1
    return raw.strip()


def _safe_texture(value: str, image_id: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value.startswith('"') and value.endswith('"'):
        value = value[1:-1]
    elif '"' in value:
        raise ValueError(f"MappedImage {image_id!r} has malformed Texture quoting")
    if not value or len(value) > 512:
        raise ValueError(f"MappedImage {image_id!r} has an unsafe Texture path")
    try:
        parts = safe_relative_parts(value)
    except ValueError as exc:
        raise ValueError(
            f"MappedImage {image_id!r} has an unsafe Texture path: {value!r}"
        ) from exc
    normalized = "/".join(parts)
    if len(normalized) > 512:
        raise ValueError(f"MappedImage {image_id!r} has an unsafe Texture path")
    return normalized


def _integer(value: str, label: str, image_id: str) -> int:
    try:
        number = float(value)
    except ValueError as exc:
        raise ValueError(
            f"MappedImage {image_id!r} has invalid {label}: {value!r}"
        ) from exc
    if not math.isfinite(number):
        raise ValueError(f"MappedImage {image_id!r} has nonfinite {label}")
    if not number.is_integer():
        raise ValueError(f"MappedImage {image_id!r} has non-integral {label}")
    return int(number)


def _coords(value: str, image_id: str) -> dict[str, int]:
    expected = {"left", "top", "right", "bottom"}
    result: dict[str, int] = {}
    colonless_count = 0
    position = 0
    while position < len(value):
        match = _COORD_ITEM.match(value, position)
        if match is None:
            raise ValueError(f"MappedImage {image_id!r} has malformed Coords")
        key = match.group(1).casefold()
        if key not in expected:
            raise ValueError(
                f"MappedImage {image_id!r} has unknown Coords field {match.group(1)!r}"
            )
        if key in result:
            raise ValueError(
                f"MappedImage {image_id!r} has duplicate Coords field {match.group(1)!r}"
            )
        if ":" not in match.group(2):
            colonless_count += 1
            if colonless_count > 1:
                raise ValueError(
                    f"MappedImage {image_id!r} has too many colonless Coords fields"
                )
        result[key] = _integer(match.group(3), key, image_id)
        position = match.end()
    missing = sorted(expected - result.keys())
    if missing:
        raise ValueError(
            f"MappedImage {image_id!r} is missing Coords field(s): {', '.join(missing)}"
        )
    return result


def _finish(image_id: str, fields: dict[str, str]) -> MappedImageRecord:
    required = {"texture", "texturewidth", "textureheight", "coords"}
    missing = sorted(required - fields.keys())
    if missing:
        raise ValueError(
            f"MappedImage {image_id!r} is missing required field(s): {', '.join(missing)}"
        )

    texture = _safe_texture(fields["texture"], image_id)
    width = _integer(fields["texturewidth"], "TextureWidth", image_id)
    height = _integer(fields["textureheight"], "TextureHeight", image_id)
    if not 1 <= width <= MAX_MAPPED_IMAGE_DIMENSION:
        raise ValueError(f"MappedImage {image_id!r} has invalid TextureWidth")
    if not 1 <= height <= MAX_MAPPED_IMAGE_DIMENSION:
        raise ValueError(f"MappedImage {image_id!r} has invalid TextureHeight")

    coords = _coords(fields["coords"], image_id)
    if any(coords[key] < 0 for key in coords):
        raise ValueError(f"MappedImage {image_id!r} has negative crop coordinates")
    if coords["right"] <= coords["left"] or coords["bottom"] <= coords["top"]:
        raise ValueError(f"MappedImage {image_id!r} has an inverted or empty crop")
    if coords["right"] > width or coords["bottom"] > height:
        raise ValueError(f"MappedImage {image_id!r} crop is out of bounds")

    return MappedImageRecord(
        id=image_id,
        texture=texture,
        texture_width=width,
        texture_height=height,
        left=coords["left"],
        top=coords["top"],
        right=coords["right"],
        bottom=coords["bottom"],
    )


def _parse_mapped_images(
    source: bytes, *, reject_duplicate_ids: bool, sort_records: bool = True
) -> tuple[MappedImageRecord, ...]:
    records: list[MappedImageRecord] = []
    seen: set[str] = set()
    current_id: str | None = None
    fields: dict[str, str] = {}
    assignment_count = 0

    for line_number, raw in enumerate(_decode(source).splitlines(), 1):
        line = _strip_comment(raw)
        if not line:
            continue
        header = _HEADER.fullmatch(line)
        if current_id is None:
            if header is not None:
                current_id = header.group(1)
                fields = {}
                assignment_count = 0
            elif line.casefold().startswith("mappedimage"):
                raise ValueError(f"malformed MappedImage header at line {line_number}")
            continue

        if header is not None or line.casefold().startswith("mappedimage"):
            nested_id = header.group(1) if header is not None else "malformed header"
            raise ValueError(
                f"unterminated MappedImage {current_id!r} before {nested_id!r}"
            )
        if line.casefold() == "end":
            key = current_id.casefold()
            if reject_duplicate_ids and key in seen:
                raise ValueError(f"duplicate MappedImage definition: {current_id!r}")
            seen.add(key)
            records.append(_finish(current_id, fields))
            if len(records) > MAX_MAPPED_IMAGE_BLOCKS:
                raise ValueError("MappedImage block count exceeds limit")
            current_id = None
            fields = {}
            assignment_count = 0
            continue

        assignment = _ASSIGNMENT.fullmatch(line)
        if assignment is None:
            continue
        assignment_count += 1
        if assignment_count > MAX_MAPPED_IMAGE_ASSIGNMENTS:
            raise ValueError(f"MappedImage {current_id!r} assignment count exceeds limit")
        field = assignment.group(1).casefold()
        if field not in {"texture", "texturewidth", "textureheight", "coords"}:
            continue
        if field in fields:
            raise ValueError(
                f"MappedImage {current_id!r} has duplicate {assignment.group(1)} field"
            )
        fields[field] = assignment.group(2)

    if current_id is not None:
        raise ValueError(f"unterminated MappedImage block: {current_id}")
    if sort_records:
        records.sort(key=lambda record: (record.id.casefold(), record.id))
    return tuple(records)


def parse_mapped_images(source: bytes) -> tuple[MappedImageRecord, ...]:
    """Parse and canonically order all unambiguous ``MappedImage`` blocks."""

    return _parse_mapped_images(source, reject_duplicate_ids=True)


def resolve_mapped_images_partial(
    sources: Iterable[bytes], requested_ids: Iterable[str]
) -> MappedImageResolution:
    """Resolve an exact set while retaining missing/ambiguous diagnostics.

    BFME2 ships a small number of authored references to absent mapped images.
    Callers that can prove those references are runtime-null may classify them;
    all other callers should use :func:`resolve_mapped_images`, which remains
    fail-closed.
    """

    requested = list(requested_ids)
    if not 1 <= len(requested) <= MAX_MAPPED_IMAGE_REQUESTS:
        raise ValueError(
            "MappedImage requests must contain "
            f"1..{MAX_MAPPED_IMAGE_REQUESTS} identifiers"
        )
    requested_keys: dict[str, str] = {}
    for image_id in requested:
        if not isinstance(image_id, str) or not _IDENTIFIER.fullmatch(image_id):
            raise ValueError(f"unsafe MappedImage request identifier: {image_id!r}")
        key = image_id.casefold()
        if key in requested_keys:
            raise ValueError(f"duplicate MappedImage request identifier: {image_id!r}")
        requested_keys[key] = image_id

    selected = list(sources)
    if not 1 <= len(selected) <= MAX_MAPPED_IMAGE_SOURCES:
        raise ValueError(
            f"MappedImage sources must contain 1..{MAX_MAPPED_IMAGE_SOURCES} documents"
        )
    if any(not isinstance(source, bytes) for source in selected):
        raise TypeError("MappedImage sources must be bytes")
    if sum(len(source) for source in selected) > MAX_MAPPED_IMAGE_TOTAL_BYTES:
        raise ValueError("MappedImage source bytes exceed total limit")

    candidates: dict[str, list[MappedImageRecord]] = {}
    for source in selected:
        # A retail document may contain an unrelated duplicate definition.  It
        # is not part of this exact closure, but duplicates of a requested ID
        # remain visible below and therefore fail as ambiguous.
        # sort_records=False keeps document parse order so the retail
        # last-definition-wins rule below is exact.
        for record in _parse_mapped_images(
            source, reject_duplicate_ids=False, sort_records=False
        ):
            key = record.id.casefold()
            if key in requested_keys:
                candidates.setdefault(key, []).append(record)

    resolved: list[MappedImageRecord] = []
    missing: list[str] = []
    ambiguous: list[str] = []
    for key, requested_id in requested_keys.items():
        matches = candidates.get(key, [])
        if not matches:
            missing.append(requested_id)
        else:
            # Duplicate definitions are NOT ambiguous in retail: the engine's
            # INI::parseMappedImageDefinition looks the name up and, when it
            # already exists, re-runs initFromINI on the SAME image object —
            # the last-parsed definition wins (decomp oracle:
            # Code/GameEngine/Source/Common/INI/INIMappedImage.cpp; retail
            # authors this on e.g. BPCastleWall, twice in
            # buildingradialbuttons.ini with different textures). `matches`
            # is in parse order (stable sort on equal keys + source order),
            # so the retail winner is the final element.
            resolved.append(matches[-1])
    resolved.sort(key=lambda record: (record.id.casefold(), record.id))
    missing.sort(key=lambda item: (item.casefold(), item))
    ambiguous.sort(key=lambda item: (item.casefold(), item))
    return MappedImageResolution(tuple(resolved), tuple(missing), tuple(ambiguous))


def resolve_mapped_images(
    sources: Iterable[bytes], requested_ids: Iterable[str]
) -> tuple[MappedImageRecord, ...]:
    """Resolve an exact requested set across documents, failing on gaps."""

    resolution = resolve_mapped_images_partial(sources, requested_ids)
    if resolution.missing_ids:
        raise ValueError(
            f"unresolved MappedImage definition: {resolution.missing_ids[0]!r}"
        )
    if resolution.ambiguous_ids:
        raise ValueError(
            f"ambiguous MappedImage definition: {resolution.ambiguous_ids[0]!r}"
        )
    return resolution.records


def resolve_mapped_image_texture_paths(
    records: Iterable[MappedImageRecord], virtual_paths: Iterable[str]
) -> dict[str, str]:
    """Resolve logical UI textures to BFME2's exact compiled atlas leaves.

    BFME2 mapped-image definitions name the source texture, while the shipped
    archive normally stores its compiled form at
    ``art/compiledtextures/<first-two-chars>/<stem>.dds``.  A small set of
    strategic UI atlases is shipped under the authored extension instead.  An
    exact authored-extension path wins when present; otherwise the exact DDS
    convention is required.  No basename search or extension guessing occurs.
    """

    result, missing = resolve_mapped_image_texture_paths_partial(records, virtual_paths)
    if missing:
        raise ValueError(f"unresolved MappedImage compiled texture: {missing[0]!r}")
    return result


def resolve_mapped_image_texture_paths_partial(
    records: Iterable[MappedImageRecord], virtual_paths: Iterable[str]
) -> tuple[dict[str, str], tuple[str, ...]]:
    """Resolve available atlas leaves in one pass and retain exact gaps.

    Ambiguous or structurally invalid leaves still fail closed.  The partial
    result exists for censuses which must report all source-owned gaps instead
    of aborting at the first absent retail atlas.
    """

    requested: dict[str, str] = {}
    for record in records:
        key = record.texture.casefold()
        requested.setdefault(key, record.texture)
    if len(requested) > MAX_MAPPED_IMAGE_REQUESTS:
        raise ValueError("MappedImage texture request count exceeds limit")

    selected_paths = list(virtual_paths)
    if len(selected_paths) > MAX_MAPPED_IMAGE_TEXTURE_PATHS:
        raise ValueError("MappedImage texture path count exceeds limit")
    catalog_paths: dict[str, set[str]] = {}
    for path in selected_paths:
        normalized = "/".join(safe_relative_parts(path))
        catalog_paths.setdefault(normalized.casefold(), set()).add(normalized)

    result: dict[str, str] = {}
    missing: list[str] = []
    for key in sorted(requested):
        texture = requested[key]
        stem = texture.replace("\\", "/").rsplit("/", 1)[-1].rsplit(".", 1)[0]
        if len(stem) < 2:
            raise ValueError(f"MappedImage texture stem is too short: {texture!r}")
        basename = texture.replace("\\", "/").rsplit("/", 1)[-1]
        exact_authored = f"art/compiledtextures/{stem[:2].casefold()}/{basename}"
        expected_dds = f"art/compiledtextures/{stem[:2].casefold()}/{stem}.dds"
        authored_matches = sorted(
            catalog_paths.get(exact_authored.casefold(), set()),
            key=lambda item: (item.casefold(), item),
        )
        if len(authored_matches) > 1:
            raise ValueError(f"ambiguous MappedImage compiled texture: {texture!r}")
        if authored_matches:
            result[texture] = authored_matches[0]
            continue
        matches = sorted(
            catalog_paths.get(expected_dds.casefold(), set()),
            key=lambda item: (item.casefold(), item),
        )
        if not matches:
            missing.append(texture)
            continue
        if len(matches) != 1:
            raise ValueError(f"ambiguous MappedImage compiled texture: {texture!r}")
        result[texture] = matches[0]
    return result, tuple(sorted(missing, key=lambda item: (item.casefold(), item)))
