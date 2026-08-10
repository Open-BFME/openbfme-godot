"""Compile the retail mouse-cursor art into a standalone content pack.

Why this lane is shaped differently from every other art lane:

* Retail cursors are the one art family that never enters a BIG.  Windows loads
  a cursor from the filesystem, so EA shipped ``data/cursors/*.ani`` loose.  The
  install catalog indexes BIG members only, so the cook's resource/converter
  path - which resolves every source through that catalog - cannot reach them.
* A cursor is not a texture the renderer samples; it is handed to the windowing
  system as an ``Image``.  It is also tiny: the retail attack cursor is 32x32
  with six frames, about 4 KB of PNG in total.

So the frames ride the pack's authored JSON document (``data/cursors.json``) as
base64 PNG rather than as cooked pack files.  That keeps the whole lane inside
the proven compose/cook/audit/publish path used by ``publish-music-pack``
instead of bending the shared catalog and converter surface for one sprite.
The binding INI itself is still read through the catalog and pinned as a
``hash-only`` resource, so the pack's provenance names the exact retail
``mouse.ini`` bytes it was compiled from.
"""

from __future__ import annotations

import base64
from dataclasses import dataclass
import hashlib
import io
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping

from .cursor_art import (
    AnimatedCursor,
    CursorFormatError,
    MouseCursorBinding,
    decode_cursor,
    parse_mouse_cursor_bindings,
)


CURSOR_INDEX_RELATIVE = "data/cursors.json"
CURSOR_INDEX_SCHEMA = "openbfme.cursor-index"
CURSOR_INDEX_SCHEMA_VERSION = 1
MOUSE_INI_PATH = "data/ini/mouse.ini"
CURSORS_DIRECTORY = "data/cursors"

# Only the cursors the game actually presents are carried.  Shipping the whole
# 49-entry retail table would put roughly a megabyte of base64 into a document
# nothing reads; every id here is wired on the Godot side.  The five names all
# resolve to the same retail art (``SCCAttack``) - retail gives one sprite five
# intents - so they cost one payload plus four aliases.
DEFAULT_CURSOR_NAMES: tuple[str, ...] = (
    "attackobj",
    "forceattackobj",
    "forceattackground",
    "target",
    "outrange",
)

# Resource-exhaustion bound on the emitted document, not a semantic limit.
MAX_INDEX_PAYLOAD_BYTES = 4 * 1024 * 1024
MAX_CURSOR_FILE_BYTES = 8 * 1024 * 1024


class CursorPackError(ValueError):
    """The cursor lane cannot compile a pack from this retail input."""


@dataclass(frozen=True, slots=True)
class CursorSource:
    """One loose retail cursor file the pack was compiled from."""

    filename: str
    sha256: str
    bytes_read: int


@dataclass(frozen=True, slots=True)
class CursorPlan:
    document: dict[str, Any]
    sources: tuple[CursorSource, ...]
    gaps: tuple[dict[str, str], ...]

    @property
    def cursor_count(self) -> int:
        return sum(
            1
            for value in self.document["cursors"].values()
            if "aliasOf" not in value
        )

    @property
    def alias_count(self) -> int:
        return sum(1 for value in self.document["cursors"].values() if "aliasOf" in value)


def _encode_png(frame_rgba: bytes, width: int, height: int) -> bytes:
    """Encode one RGBA frame as a deterministic PNG.

    Pillow is pinned repo-wide for exactly this reason: an unpinned encoder
    changes the bytes and therefore the bundle digest between machines.
    """

    try:
        from PIL import Image
        import PIL
    except ImportError as exc:  # pragma: no cover - environment guard
        raise CursorPackError("Pillow is required for deterministic cursor output") from exc
    if PIL.__version__ != "12.2.0":
        raise CursorPackError(
            "Pillow 12.2.0 is required for deterministic cursor output; "
            f"found {PIL.__version__}"
        )
    image = Image.frombytes("RGBA", (width, height), frame_rgba)
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", compress_level=9, optimize=False)
    return buffer.getvalue()


def _cursor_entry(cursor: AnimatedCursor, binding: MouseCursorBinding) -> dict[str, Any]:
    frames: list[dict[str, Any]] = []
    for frame in cursor.frames:
        png = _encode_png(frame.rgba, frame.width, frame.height)
        frames.append(
            {
                "png": base64.b64encode(png).decode("ascii"),
                "sha256": hashlib.sha256(png).hexdigest(),
                "bytes": len(png),
            }
        )
    hotspot_x, hotspot_y = (
        binding.hotspot if binding.hotspot is not None else (cursor.hotspot_x, cursor.hotspot_y)
    )
    return {
        "texture": binding.texture,
        "source": binding.source_filename,
        "width": cursor.width,
        "height": cursor.height,
        # mouse.ini may override the hotspot the art carries; retail's INI wins.
        "hotspot": {"x": int(hotspot_x), "y": int(hotspot_y)},
        "hotspotFromIni": binding.hotspot is not None,
        "frames": frames,
        "sequence": list(cursor.sequence),
        "frameSeconds": [round(value, 6) for value in cursor.step_seconds()],
    }


def plan_cursor_pack(
    mouse_ini_bytes: bytes,
    read_cursor_file: Callable[[str], bytes],
    *,
    cursor_names: Iterable[str] = DEFAULT_CURSOR_NAMES,
    provenance: Mapping[str, Any] | None = None,
) -> CursorPlan:
    """Decode the requested retail cursors into a pack document.

    *read_cursor_file* is handed a bare ``data/cursors`` filename and must
    return its bytes or raise ``FileNotFoundError``; the caller owns the loose
    directory so this function stays testable without an install.  A cursor the
    retail install cannot supply, or whose container this decoder does not
    support, becomes a NAMED gap - never a silent drop and never a substitute
    sprite.
    """

    text = mouse_ini_bytes.decode("latin-1")
    bindings = parse_mouse_cursor_bindings(text)
    if not bindings:
        raise CursorPackError("mouse.ini declares no MouseCursor blocks")

    wanted = [name.casefold() for name in cursor_names]
    if not wanted:
        raise CursorPackError("no cursors were requested")

    cursors: dict[str, Any] = {}
    gaps: list[dict[str, str]] = []
    sources: dict[str, CursorSource] = {}
    # One decoded payload per distinct retail art file; every other cursor that
    # names the same art becomes an alias instead of a second copy.
    payload_owner: dict[str, str] = {}

    for name in wanted:
        binding = bindings.get(name)
        if binding is None:
            gaps.append({"cursor": name, "reason": "no-mouse-ini-block"})
            continue
        owner = payload_owner.get(binding.source_filename)
        if owner is not None:
            cursors[name] = {"aliasOf": owner, "texture": binding.texture}
            continue
        try:
            data = read_cursor_file(binding.source_filename)
        except FileNotFoundError:
            gaps.append(
                {
                    "cursor": name,
                    "reason": "retail-source-missing",
                    "source": binding.source_filename,
                }
            )
            continue
        if len(data) > MAX_CURSOR_FILE_BYTES:
            raise CursorPackError(
                f"retail cursor {binding.source_filename} exceeds the read bound"
            )
        try:
            decoded = decode_cursor(data)
        except CursorFormatError as exc:
            gaps.append(
                {
                    "cursor": name,
                    "reason": "unsupported-container",
                    "source": binding.source_filename,
                    "detail": str(exc),
                }
            )
            continue
        cursors[name] = _cursor_entry(decoded, binding)
        payload_owner[binding.source_filename] = name
        sources[binding.source_filename] = CursorSource(
            filename=binding.source_filename,
            sha256=hashlib.sha256(data).hexdigest(),
            bytes_read=len(data),
        )

    if not payload_owner:
        raise CursorPackError(
            "no requested cursor could be decoded from the retail install; "
            "refusing to publish a cursor pack that carries no art"
        )

    document: dict[str, Any] = {
        "schema": CURSOR_INDEX_SCHEMA,
        "schemaVersion": CURSOR_INDEX_SCHEMA_VERSION,
        "cursors": dict(sorted(cursors.items())),
        "gaps": sorted(gaps, key=lambda row: row["cursor"]),
        "provenance": {
            **dict(provenance or {}),
            "mouseIni": MOUSE_INI_PATH,
            "mouseIniSha256": hashlib.sha256(mouse_ini_bytes).hexdigest(),
            "cursorsDirectory": CURSORS_DIRECTORY,
            "sources": [
                {
                    "file": source.filename,
                    "sha256": source.sha256,
                    "bytes": source.bytes_read,
                }
                for source in sorted(sources.values(), key=lambda item: item.filename)
            ],
        },
    }
    return CursorPlan(
        document=document,
        sources=tuple(sorted(sources.values(), key=lambda item: item.filename)),
        gaps=tuple(gaps),
    )


def compose_cursor_profile(
    document: Mapping[str, Any],
    *,
    pack_id: str,
    game: str,
    catalog_identity: str,
) -> dict[str, Any]:
    """Build a standalone import profile for the cursor-only pack."""

    import json

    payload = json.dumps(document, sort_keys=True, separators=(",", ":"))
    if len(payload.encode("utf-8")) > MAX_INDEX_PAYLOAD_BYTES:
        raise CursorPackError(
            "cursor index exceeds the document bound; narrow the cursor list"
        )
    return {
        "format": 1,
        "id": f"{pack_id}-profile",
        "title": f"{game.upper()} mouse cursors",
        "pack": {
            "id": pack_id,
            "version": "0.1",
            "schema": "openbfme.content-pack",
            "schemaVersion": 0,
            # Same band as the music supplement: below the faction slices (900)
            # so a faction pack always wins an id collision.  This pack declares
            # only `cursors` and collides with nothing.
            "priority": 400,
            "vertical_slice_complete": False,
            "capability_maturity": "retail-mouse-cursor-art",
            "dataPolicy": {"externalPathsAllowed": False, "redistributable": False},
            "files": {"cursors": CURSOR_INDEX_RELATIVE},
            "sourceCatalogIdentitySha256": catalog_identity,
        },
        # The binding INI is pinned through the catalog so the pack's provenance
        # names the retail bytes the document was compiled from.  The art itself
        # is loose on disk and is digested inside the document instead.
        "resources": [
            {
                "id": "mouse-ini",
                "kind": "data",
                "converter": "hash-only",
                "patterns": [MOUSE_INI_PATH],
                "required": True,
            }
        ],
        "runtime_data": {CURSOR_INDEX_RELATIVE: dict(document)},
    }


def resolve_cursors_root(install_root: Path) -> Path | None:
    """Find the loose ``data/cursors`` directory of a flat or layered install.

    The RotWK lane hands every command a *layered* install root whose children
    are ``layer-0-rotwk`` / ``layer-1-bfme2`` rather than a game tree, so a flat
    ``<install>/data/cursors`` probe finds nothing there.  Layers are searched in
    sorted order, which is SAGE's own precedence: layer 0 (RotWK) wins over
    layer 1 (BFME2).  Returns ``None`` when the install carries no cursor art at
    all, so the caller can refuse loudly instead of publishing an empty pack.
    """

    root = Path(install_root).expanduser().resolve()
    flat = root / "data" / "cursors"
    if flat.is_dir():
        return flat
    try:
        children = sorted(
            (child for child in root.iterdir() if child.is_dir()),
            key=lambda item: item.name.casefold(),
        )
    except OSError:
        return None
    for child in children:
        if not child.name.casefold().startswith("layer-"):
            continue
        candidate = child / "data" / "cursors"
        if candidate.is_dir():
            return candidate
    return None


def read_loose_cursor(cursors_root: Path) -> Callable[[str], bytes]:
    """Return a reader bound to one ``data/cursors`` directory.

    The filename comes from ``cursor_source_filename``, which already refuses
    anything that is not a bare leaf; the containment check here is the second
    lock rather than the only one.
    """

    root = Path(cursors_root).expanduser().resolve()

    def _read(filename: str) -> bytes:
        candidate = (root / filename).resolve()
        if candidate.parent != root:
            raise CursorPackError(f"cursor source escapes the cursors root: {filename!r}")
        if not candidate.is_file():
            raise FileNotFoundError(filename)
        return candidate.read_bytes()

    return _read


__all__ = [
    "CURSOR_INDEX_RELATIVE",
    "CURSOR_INDEX_SCHEMA",
    "CURSOR_INDEX_SCHEMA_VERSION",
    "CURSORS_DIRECTORY",
    "CursorPackError",
    "CursorPlan",
    "CursorSource",
    "DEFAULT_CURSOR_NAMES",
    "MOUSE_INI_PATH",
    "compose_cursor_profile",
    "plan_cursor_pack",
    "read_loose_cursor",
    "resolve_cursors_root",
]
