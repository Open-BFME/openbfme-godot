"""One-shot operator: dump APT PlaceObject placement rows for a retail movie.

Reads ``<movie>.apt``/``.const`` straight from an effective-assets tree and
prints the authored stage transform of every named PlaceObject in the root
timeline (and, with ``--sprites``, inside every sprite character).  This is the
oracle behind the authored HUD layout constants in
``game/src/retail_slice/retail_hud_apt_runtime.gd``; run it to reproduce them.

Usage::

  python tools/dump_apt_placements.py <effective-assets-dir> <MovieName> [--sprites]
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "importer"))

from openbfme_importer.sage_apt import (  # noqa: E402
    parse_apt_constants,
    parse_apt_movie,
)


def _u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def _i32(data: bytes, off: int) -> int:
    return struct.unpack_from("<i", data, off)[0]


def _f32(data: bytes, off: int) -> float:
    return struct.unpack_from("<f", data, off)[0]


def _string(data: bytes, off: int) -> str:
    end = data.find(b"\0", off)
    return data[off:end].decode("cp1252", "replace")


def place_rows(data: bytes, items: list[dict]) -> list[dict]:
    """Decode the fixed 60-byte PlaceObject record used by BFME2 APT v6/v7."""

    rows = []
    for item in items:
        if item.get("kind") != "place-object":
            continue
        off = int(item["sourceOffset"])
        flags = _u32(data, off + 4)
        name_ptr = _u32(data, off + 52)
        rows.append(
            {
                "sourceOffset": off,
                "flags": flags,
                "depth": _i32(data, off + 8),
                "characterId": _i32(data, off + 12),
                "matrix": [_f32(data, off + 16 + i * 4) for i in range(4)],
                "translation": [_f32(data, off + 32 + i * 4) for i in range(2)],
                "name": _string(data, name_ptr)
                if (flags & 0x20) and name_ptr
                else None,
            }
        )
    return rows


def _sprite_frames(data: bytes, count: int, table: int) -> list[dict]:
    frames = []
    for frame_index in range(count):
        item_count = _i32(data, table + frame_index * 8)
        item_table = _u32(data, table + frame_index * 8 + 4)
        if not item_table:
            continue
        items = []
        for index in range(item_count):
            item_pointer = _u32(data, item_table + index * 4)
            if item_pointer and _u32(data, item_pointer) == 3:
                items.append(
                    {"kind": "place-object", "sourceOffset": item_pointer}
                )
        rows = place_rows(data, items)
        if rows:
            frames.append({"frameIndex": frame_index, "places": rows})
    return frames


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("assets")
    parser.add_argument("movie")
    parser.add_argument("--sprites", action="store_true")
    parser.add_argument("--max-exports", type=int, default=4096)
    args = parser.parse_args(argv)

    root = Path(args.assets)
    apt_path = root / f"{args.movie}.apt"
    const_path = root / f"{args.movie}.const"
    data = apt_path.read_bytes()
    constants = parse_apt_constants(const_path.read_bytes(), str(const_path))
    movie = parse_apt_movie(
        data,
        constants,
        str(apt_path),
        max_bytes=4 * 1024 * 1024,
        max_exports=args.max_exports,
    )
    out: dict = {
        "movie": args.movie,
        "sha256": movie["sha256"],
        "stage": {
            "width": movie["root"]["width"],
            "height": movie["root"]["height"],
        },
        "frames": [],
    }
    for timeline in movie["timelines"]:
        rows = place_rows(data, timeline["items"])
        if rows:
            out["frames"].append(
                {"frameIndex": timeline["frameIndex"], "places": rows}
            )
    if args.sprites:
        sprites = []
        for character in movie["characters"]:
            if character.get("kind") != "sprite":
                continue
            frames = _sprite_frames(
                data,
                int(character["frameCount"]),
                int(character["frameTableOffset"]),
            )
            if frames:
                sprites.append(
                    {
                        "characterId": character["characterId"],
                        "sourceOffset": character["sourceOffset"],
                        "frames": frames,
                    }
                )
        out["sprites"] = sprites
    detail = []
    for character in movie["characters"]:
        kind = character.get("kind")
        row = {
            "characterId": character["characterId"],
            "kind": kind,
            "sourceOffset": character.get("sourceOffset"),
        }
        offset = int(character.get("sourceOffset") or 0)
        if kind == "text" and offset:
            row["bounds"] = [_f32(data, offset + 8 + i * 4) for i in range(4)]
            row["alignmentCode"] = _u32(data, offset + 28)
            row["fontHeight"] = _f32(data, offset + 36)
            row["placeholder"] = _string(data, _u32(data, offset + 52))
            row["variableName"] = _string(data, _u32(data, offset + 56))
        elif kind == "button" and offset:
            row["bounds"] = [_f32(data, offset + 12 + i * 4) for i in range(4)]
        elif kind == "shape" and offset:
            row["geometryId"] = character.get("geometryId")
        elif kind == "image" and offset:
            row["textureId"] = character.get("textureId")
        detail.append(row)
    out["characters"] = detail
    exports: dict[str, list[str]] = {}
    for row in movie["exports"]:
        exports.setdefault(str(row["characterId"]), []).append(str(row["symbol"]))
    out["exportsByCharacter"] = exports
    json.dump(out, sys.stdout, indent=1)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
