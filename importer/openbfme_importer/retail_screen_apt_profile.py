"""Emit the catalog-resolvable profile that cooks retail screens into a pack.

`retail_shell_apt_profile` is the main-menu instrument: one resource, one
pinned six-movie closure, and hardcoded oracle markers (`EXPECTED_ARCHIVES`,
`EXPECTED_PAYLOAD_BYTES`, `EXPECTED_SOURCE_AGGREGATE_SHA256`) that ARE the
attestation for that scene.  Screens cannot work that way - there are 62 of
them and each has its own closure - so this module attests differently: it
states, per screen, exactly which virtual paths it consumes and what their
bytes hashed to at plan time, and the converter re-checks every one.

WHICH SCREENS.  A screen is admitted only if it actually reconstructs into a
scene.  The 22 that do not are NOT quietly dropped: they are listed in the
plan's `refused` block with the reason, and most of them are libraries
(`libInGameUI`, `MenuFrameAndBg`, `GameWindowGadgets`) that exist to be
imported rather than shown.  A plan that silently shipped 62 of 84 would read
as complete; one that names the other 22 can be argued with.
"""

from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import tempfile
from typing import Any, Iterable, Mapping

from .profile import ImportProfile
from .retail_screen_apt_convert import (
    ScreenAptConvertError,
    build_screen_scene,
    screen_bundle_virtual_paths,
)
from .retail_screen_apt_plan import ScreenAptPlanError, screen_movie_names
from .sage_apt import canonical_sha256

SCREEN_APT_PLAN_SCHEMA = "openbfme.retail-screen-apt-plan"
SCREEN_APT_PLAN_SCHEMA_VERSION = 0
SCREEN_APT_CONVERTER = "sage-apt-screen-runtime"

#: Bound on how many screens one profile may declare. The tree holds 84 movies
#: and 62 cook; the ceiling is stated so a malformed tree cannot fan a profile
#: out without saying so.
MAX_PROFILE_SCREENS = 128


class ScreenAptProfileError(ValueError):
    """A screen profile cannot be assembled from the evidence available."""


def screen_runtime_path(movie: str) -> str:
    return f"data/ui/screens/{movie.casefold()}/scene-contract.json"


def screen_profile_resource(
    movie: str,
    patterns: Iterable[str],
    *,
    expected_source_aggregate_sha256: str | None = None,
) -> dict[str, Any]:
    """Return one screen's bundle resource for an ``ImportProfile``."""

    rows = sorted(set(patterns), key=str.casefold)
    if not rows:
        raise ScreenAptProfileError(f"{movie} declares no sources")
    resource: dict[str, Any] = {
        "id": f"screen-apt-{movie.casefold()}",
        "kind": "ui",
        "patterns": rows,
        "required": True,
        "converter": SCREEN_APT_CONVERTER,
        "output": screen_runtime_path(movie),
        "options": {"movie": movie},
        "limit": len(rows),
        "expected_count": len(rows),
    }
    if expected_source_aggregate_sha256 is not None:
        resource["options"]["expectedSourceAggregateSha256"] = (
            expected_source_aggregate_sha256
        )
    return resource


def build_retail_screen_apt_plan(
    effective_assets_root: Path | str,
    movies: Iterable[str] | None = None,
) -> dict[str, Any]:
    """Plan every screen that reconstructs, and NAME every one that does not.

    Each admitted screen contributes a resource whose options carry the source
    aggregate the cook produced at plan time, so the converter's own re-check
    is a second opinion rather than a restatement: if the tree moves, the pack
    build fails loudly instead of shipping different pixels.
    """

    root = Path(effective_assets_root)
    names = tuple(movies) if movies is not None else screen_movie_names(root)
    if not names:
        raise ScreenAptProfileError("no screen movies were found")
    if len(names) > MAX_PROFILE_SCREENS:
        raise ScreenAptProfileError("screen profile exceeds its stated bound")

    resources: list[dict[str, Any]] = []
    screens: list[dict[str, Any]] = []
    refused: list[dict[str, str]] = []
    for movie in names:
        try:
            contract = build_screen_scene(root, movie)
            patterns = screen_bundle_virtual_paths(root, movie)
        except (ScreenAptConvertError, ScreenAptPlanError) as error:
            refused.append({"movie": movie, "reason": str(error)})
            continue
        aggregate = str(contract["sourceAggregateSha256"])
        resources.append(
            screen_profile_resource(
                movie,
                patterns,
                expected_source_aggregate_sha256=aggregate,
            )
        )
        selection = contract["frameSelection"]
        screens.append(
            {
                "movie": movie,
                "closure": list(contract["closure"]),
                "frame": int(selection["frame"]),
                "frameLabel": selection["label"],
                "frameRule": str(selection["rule"]),
                "sourceAggregateSha256": aggregate,
                "sourceCount": len(patterns),
                "runtimePath": screen_runtime_path(movie),
                "drawCount": int(contract["totals"]["draws"]),
                "blockerCount": int(contract["totals"]["blockers"]),
            }
        )
    if not resources:
        raise ScreenAptProfileError("no screen reconstructed into a scene")

    plan: dict[str, Any] = {
        "schema": SCREEN_APT_PLAN_SCHEMA,
        "schemaVersion": SCREEN_APT_PLAN_SCHEMA_VERSION,
        "policy": {
            "scope": "bfme2-retail-screen-apt-closures",
            "universalFlashRuntime": False,
            "executesActionScript": False,
            "substitutesAllowed": False,
            "genericArtAllowed": False,
            "retailPayloadInPlan": False,
            "frameStateIsAuthored": True,
        },
        "screens": sorted(screens, key=lambda row: str(row["movie"]).casefold()),
        "refused": sorted(refused, key=lambda row: row["movie"].casefold()),
        "profileFragment": {"resources": resources},
        "summary": {
            "movieCount": len(names),
            "screenCount": len(screens),
            "refusedCount": len(refused),
            "drawCount": sum(int(row["drawCount"]) for row in screens),
            "sourceCount": sum(int(row["sourceCount"]) for row in screens),
        },
    }
    plan["aggregateSha256"] = canonical_sha256(plan)
    return plan


def generated_import_profile(
    plan: Mapping[str, Any],
    *,
    profile_id: str = "bfme2-screen-apt-generated",
    pack_id: str = "bfme2-screen-apt-private",
) -> dict[str, Any]:
    """Return the catalog-resolvable source profile for the screen closures."""

    document = dict(plan)
    declared = document.get("aggregateSha256")
    basis = dict(document)
    basis.pop("aggregateSha256", None)
    if declared != canonical_sha256(basis):
        raise ScreenAptProfileError("screen APT plan aggregate digest mismatch")
    fragment = document.get("profileFragment")
    if not isinstance(fragment, Mapping):
        raise ScreenAptProfileError("screen APT plan lacks a profile fragment")
    resources = fragment.get("resources")
    if not isinstance(resources, list) or not resources:
        raise ScreenAptProfileError("screen APT profile fragment is empty")
    identities = [row.get("id") for row in resources]
    if len(set(identities)) != len(identities):
        raise ScreenAptProfileError("screen APT resource identities collide")

    profile = {
        "format": 1,
        "id": profile_id,
        "title": "Private BFME II retail screen exact APT source closures",
        "pack": {
            "id": pack_id,
            "version": "1.06-plan-v0",
            "dataPolicy": {
                "externalPathsAllowed": False,
                "redistributable": False,
            },
        },
        "resources": deepcopy(resources),
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-screen-apt-generated-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(profile), encoding="utf-8")
        ImportProfile.load(path)
    return profile
