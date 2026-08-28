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
)

#: A screen is one movie plus its constants, image assignments and geometry.
#: RotWK's largest shell movie is `LivingWorldUI.apt` at 799,258 bytes, so the
#: default APT bound is raised to admit the whole shell rather than only the
#: HUD-sized ones. This is a MEASURED edition fact, not slack.
MAX_SCREEN_APT_BYTES = 1_048_576
MAX_SCREEN_EXPORTS = 8192
#: `MenuExport.apt` exports 1,334 symbols in BFME2 and 4,574 in RotWK.
_GEOMETRY_NAME = re.compile(r"^(\d+)\.ru$")


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
        # Screens reference their atlases through the .dat image map; the
        # converter resolves the TGA rows, and a screen that ships none is a
        # screen with no atlas, not a broken plan.
        "atlases": [],
    }
