"""Build the APT plan for ANY retail screen movie, not just the Men HUD.

`retail_hud_apt_profile.build_retail_hud_apt_plan` is a Men-HUD instrument: it
pins `_GROUPS`, asserts oracle markers ("188 virtual files", "SGCommandBar",
"window/controlbar.wnd") and validates the palantir external-movie closure. None
of that is wrong - it is the attestation for THAT scene - but it is why a screen
cannot simply be appended to it (queue Q117).

The parser underneath is already movie-agnostic: `sage_apt.parse_apt_movie` emits
exactly the `apt` summary that `retail_hud_apt_convert._movie_from_plan`
consumes, and all 81 screens ship as their own `apt/<movie>.big`, the same shape
the HUD plan already handles. So this module is deliberately thin - it assembles
the plan and attests every byte it reads, and it invents nothing.

Every file is hashed as it is read and the digest goes into the plan, so the
converter's `_verified_source` re-check is a real second opinion rather than a
restatement of the same number: the plan says what the tree held at plan time,
and the convert fails closed if the tree has since moved.
"""

from __future__ import annotations

import hashlib
import re
from pathlib import Path
from typing import Any

from .sage_apt import (
    AptParseError,
    parse_apt_constants,
    parse_apt_dat,
    parse_apt_geometry,
    parse_apt_movie,
    parse_tga_identity,
)

#: A screen is one movie plus its constants, image assignments and geometry.
#: RotWK's largest shell movie is `LivingWorldUI.apt` at 799,258 bytes, so the
#: default APT bound is raised to admit the whole shell rather than only the
#: HUD-sized ones. This is a MEASURED edition fact, not slack.
MAX_SCREEN_APT_BYTES = 1_048_576
MAX_SCREEN_EXPORTS = 8192
#: `MenuExport.apt` exports 1,334 symbols in BFME2 and 4,574 in RotWK.
_GEOMETRY_NAME = re.compile(r"^(\d+)\.ru$")
#: Every screen keeps its atlases where the retail tree puts them:
#: ``art/Textures/apt_<Movie>_<textureId>.tga``.  That is the SAME naming the
#: Men-HUD plan feeds the converter (`_movie_from_plan` keys atlases off the
#: trailing ``_<n>.tga``), so nothing about the shape is screen-specific.
_ATLAS_DIRECTORY = "art/Textures"
#: TGA bound is the one `parse_tga_identity` already enforces (2048x2048x4).
MAX_SCREEN_ATLAS_BYTES = 8 * 1024 * 1024
MAX_SCREEN_ATLASES = 64
#: Measured: the widest screen closure in the tree is 4 movies deep-and-wide.
MAX_SCREEN_CLOSURE = 32


class ScreenAptPlanError(ValueError):
    """A screen movie is absent, malformed, or violates a stated bound."""


def _read(path: Path, limit: int) -> bytes:
    if not path.is_file():
        raise ScreenAptPlanError(f"screen source is missing: {path.name}")
    size = path.stat().st_size
    if size > limit:
        raise ScreenAptPlanError(f"{path.name} exceeds its stated byte bound")
    return path.read_bytes()


def _digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def screen_movie_names(effective_assets_root: Path | str) -> tuple[str, ...]:
    """Every movie in the tree that carries the full .apt/.const/.dat trio."""

    root = Path(effective_assets_root)
    names = []
    for apt in sorted(root.glob("*.apt")):
        stem = apt.stem
        if (root / f"{stem}.const").is_file() and (root / f"{stem}.dat").is_file():
            names.append(stem)
    return tuple(names)


def build_screen_apt_plan(
    effective_assets_root: Path | str, movie: str
) -> dict[str, Any]:
    """Assemble the exact plan `_movie_from_plan` consumes for one screen.

    Raises rather than guessing: a missing constants file, an unparsable
    geometry row or an oversize movie is a refusal, never a partial plan.
    """

    if not movie or not re.fullmatch(r"[A-Za-z0-9_]{1,64}", movie):
        raise ScreenAptPlanError("screen movie name is not a bare identifier")
    root = Path(effective_assets_root)
    apt_path = root / f"{movie}.apt"
    const_path = root / f"{movie}.const"
    dat_path = root / f"{movie}.dat"

    constants_bytes = _read(const_path, MAX_SCREEN_APT_BYTES)
    constants = parse_apt_constants(constants_bytes, const_path.name)
    apt_bytes = _read(apt_path, MAX_SCREEN_APT_BYTES)
    try:
        summary = parse_apt_movie(
            apt_bytes,
            constants,
            apt_path.name,
            max_bytes=MAX_SCREEN_APT_BYTES,
            max_exports=MAX_SCREEN_EXPORTS,
        )
    except AptParseError as error:
        raise ScreenAptPlanError(f"{movie}: {error}") from error
    dat_bytes = _read(dat_path, MAX_SCREEN_APT_BYTES)
    image_map = parse_apt_dat(dat_bytes, dat_path.name)

    geometry: list[dict[str, Any]] = []
    geometry_dir = root / f"{movie}_geometry"
    if geometry_dir.is_dir():
        rows = []
        for entry in geometry_dir.iterdir():
            match = _GEOMETRY_NAME.match(entry.name)
            if match is None:
                raise ScreenAptPlanError(
                    f"{movie} geometry holds a non-RU file: {entry.name}"
                )
            rows.append((int(match.group(1)), entry))
        for geometry_id, entry in sorted(rows):
            data = _read(entry, MAX_SCREEN_APT_BYTES)
            shape = parse_apt_geometry(data, f"{geometry_dir.name}/{entry.name}")
            geometry.append(
                {
                    "virtualPath": f"{geometry_dir.name}/{entry.name}",
                    "geometryId": geometry_id,
                    "sha256": _digest(data),
                    "byteLength": len(data),
                    "shape": shape,
                }
            )

    return {
        "schema": "openbfme.retail-screen-apt-plan",
        "schemaVersion": 0,
        "movie": movie,
        "apt": summary,
        "constants": constants,
        "imageMap": image_map,
        "geometry": geometry,
        "atlases": _screen_atlases(root, movie),
        "flaggedNullClipActions": _flagged_null_clip_actions(
            apt_bytes, summary, apt_path.name
        ),
    }


def _flagged_null_clip_actions(
    data: bytes, summary: dict[str, Any], virtual_path: str
) -> list[dict[str, Any]]:
    """Enumerate the screen's flagged-null PlaceObject records, by identity.

    Retail authors PlaceObject records whose clip-action FLAG is set while the
    pointer is zero.  `retail_hud_apt_convert` fails closed on those unless the
    exact (path, record offset, flags) identity is registered, which is right:
    a null pointer must never be read as "no clip actions" by accident.  Eleven
    screens hit it, so the screen lane measures its own identities the same way
    the strategic closure measured TimeLine's eight - by walking the SAME frame
    tables the converter walks, never by pattern-matching bytes.

    The identity list is evidence, not permission: the plan also hashes the
    .apt, and the converter re-verifies that digest, so a tree that has moved
    is refused before any of these offsets is trusted.
    """

    def u32(offset: int) -> int:
        if not 0 <= offset <= len(data) - 4:
            raise ScreenAptPlanError(f"{virtual_path} pointer is out of bounds")
        return int.from_bytes(data[offset : offset + 4], "little")

    def i32(offset: int) -> int:
        return int.from_bytes(
            data[offset : offset + 4], "little", signed=True
        ) if 0 <= offset <= len(data) - 4 else _out_of_bounds(virtual_path)

    def frame_table_records(table: int, count: int) -> list[dict[str, Any]]:
        rows: list[dict[str, Any]] = []
        for frame_index in range(count):
            header = table + frame_index * 8
            item_count = i32(header)
            item_table = u32(header + 4)
            if not 0 <= item_count <= 16_384:
                raise ScreenAptPlanError(f"{virtual_path} frame item count is out of bounds")
            for item_index in range(item_count):
                pointer = u32(item_table + item_index * 4)
                if u32(pointer) != 3:
                    continue
                flags = u32(pointer + 4)
                if not flags & 0x80 or u32(pointer + 60) != 0:
                    continue
                rows.append(
                    {
                        "virtualPath": virtual_path.casefold(),
                        "recordOffset": pointer,
                        "flags": flags,
                        "sha256": _digest(data[pointer : pointer + 64]),
                    }
                )
        return rows

    root = summary["root"]
    movie_offset = int(root["entryOffset"]) + 8
    records = frame_table_records(u32(movie_offset + 4), i32(movie_offset))
    character_count = i32(movie_offset + 12)
    character_table = u32(movie_offset + 16)
    for character_id in range(character_count):
        entry = summary["characters"][character_id]
        if str(entry.get("kind")) != "sprite":
            continue
        pointer = u32(character_table + character_id * 4)
        if not pointer:
            continue
        records.extend(frame_table_records(u32(pointer + 12), i32(pointer + 8)))
    return sorted(records, key=lambda row: int(row["recordOffset"]))


def _out_of_bounds(virtual_path: str) -> int:
    raise ScreenAptPlanError(f"{virtual_path} pointer is out of bounds")


def _screen_atlases(root: Path, movie: str) -> list[dict[str, Any]]:
    """The screen's own ``apt_<Movie>_<n>.tga`` atlases, hashed and bounded.

    A screen with no atlas is a screen that draws only solid geometry, not a
    broken plan - but a screen whose geometry names an image id the atlases do
    not cover is left to fail loudly downstream as
    ``texture-assignment-unresolved``.  Nothing here fills a gap by guessing.
    """

    directory = root / _ATLAS_DIRECTORY
    if not directory.is_dir():
        raise ScreenAptPlanError(f"atlas directory is missing: {_ATLAS_DIRECTORY}")
    pattern = re.compile(rf"^apt_{re.escape(movie)}_(\d+)\.tga$", re.IGNORECASE)
    found: list[tuple[int, Path]] = []
    for entry in directory.iterdir():
        match = pattern.match(entry.name)
        if match is not None:
            found.append((int(match.group(1)), entry))
    if len(found) > MAX_SCREEN_ATLASES:
        raise ScreenAptPlanError(f"{movie} exceeds its stated atlas count bound")
    atlases: list[dict[str, Any]] = []
    for texture_id, entry in sorted(found):
        data = _read(entry, MAX_SCREEN_ATLAS_BYTES)
        virtual_path = f"{_ATLAS_DIRECTORY}/{entry.name}"
        try:
            parsed = parse_tga_identity(data, virtual_path)
        except AptParseError as error:
            raise ScreenAptPlanError(f"{movie}: {error}") from error
        digest = str(parsed["sha256"])
        stem = entry.stem.casefold().replace("_", "-")
        parsed["textureId"] = texture_id
        parsed["cookedPng"] = (
            f"assets/ui/screens/{movie.casefold()}/{stem}-{digest[:12]}.png"
        )
        atlases.append(parsed)
    return atlases


def build_screen_closure_plans(
    effective_assets_root: Path | str, movie: str
) -> dict[str, Any]:
    """Plan a screen AND every movie it imports, transitively.

    A screen alone is not a scene.  MainMenu draws 20 primitives on its own and
    32 with `MenuFrameAndBg` loaded; ScoreScreen goes from 184 to 770.  The
    converter already resolves imported characters through `movie.imports`, so
    all it needs is the imported movies present in the same dict - the same
    service `MOVIE_CLOSURE` performs for the Palantir, computed rather than
    hardcoded.

    A movie that cannot be planned is NAMED in ``unplannable`` and its reason
    carried with it.  It is never dropped silently: the caller still sees the
    converter's own `unresolved-import-movie` blocker for whatever it leaves
    out, and now it also knows WHY the movie is missing.
    """

    root = Path(effective_assets_root)
    plans: dict[str, dict[str, Any]] = {}
    unplannable: list[dict[str, str]] = []
    pending = [movie]
    while pending:
        name = pending.pop(0)
        if name.casefold() in {key.casefold() for key in plans}:
            continue
        if any(row["movie"].casefold() == name.casefold() for row in unplannable):
            continue
        if len(plans) >= MAX_SCREEN_CLOSURE:
            raise ScreenAptPlanError(f"{movie} closure exceeds its stated bound")
        try:
            plan = build_screen_apt_plan(root, name)
        except (ScreenAptPlanError, AptParseError) as error:
            if name.casefold() == movie.casefold():
                raise
            unplannable.append({"movie": name, "reason": str(error)})
            continue
        plans[name] = plan
        pending.extend(
            str(item["movie"]) for item in plan["apt"].get("imports", [])
        )
    return {
        "schema": "openbfme.retail-screen-closure-plan",
        "schemaVersion": 0,
        "movie": movie,
        "plans": plans,
        "unplannable": sorted(unplannable, key=lambda row: row["movie"].casefold()),
    }
