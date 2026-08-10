"""Publish the interface-art lane through the ordinary pack pipeline.

``interface_art_lane`` already resolves every referenced ``MappedImage`` to an
exact atlas rectangle and can cut it with Pillow.  Nothing published it: no
mounted pack carried ``data/interface-art/index.json``, so the Godot
``ContentDB`` consumer (``content_db.gd::_load_interface_art_index``) had
nothing to load and every retail icon fell back to a placeholder.

This module turns a :class:`~.interface_art_lane.LanePlan` into ordinary
profile artifacts instead of running Pillow itself:

* one ``texture-atlas-crops`` resource per source atlas (the pipeline's own
  pinned-Pillow, provenance-tracked, cache-aware converter -- the same one the
  unit/structure pack compilers already use for their button atlases), and
* the ``data/interface-art/index.json`` runtime document, whose ``images``
  paths are constructed from the very same crop outputs.

An atlas the cook catalog cannot resolve is never silently dropped: its images
become ``gaps`` rows with reason ``atlas-not-in-cook-catalog``.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from .interface_art import PACK_INDEX_RELATIVE, PACK_INDEX_SCHEMA
from .interface_art_lane import LanePlan, PACK_INDEX_SCHEMA_VERSION

#: pack.json ``files`` key the Godot consumer honours when present.
INTERFACE_ART_PACK_KEY = "interfaceArt"
INTERFACE_ART_RUNTIME_PATH = PACK_INDEX_RELATIVE
#: profile.MAX_TEXTURE_ATLAS_CROPS -- one resource may declare at most this
#: many crops, so a dense atlas is emitted as several resources over the same
#: source and output directory.
MAX_CROPS_PER_RESOURCE = 64
#: Reason recorded for an atlas the retail cook catalog does not carry.
CATALOG_ABSENT_REASON = "atlas-not-in-cook-catalog"


class InterfaceArtPackError(ValueError):
    """The interface-art plan cannot be expressed as pack artifacts."""


@dataclass(frozen=True)
class InterfaceArtPack:
    """Profile resources plus the index document a pack must ship."""

    resources: tuple[dict[str, object], ...]
    document: dict[str, object]
    receipt: dict[str, object]


def _logical_name(output_name: str) -> str:
    """``<slug>-<digest8>.png`` -> ``<slug>-<digest8>``.

    ``profile.TEXTURE_ATLAS_LOGICAL_NAME_PATTERN`` forbids spaces, and retail
    authors a handful of ids that contain one (``Ruler-Right End``); the lane's
    own filename slug is already safe and unique per id, so it is the logical
    name too.
    """

    return output_name[: -len(".png")] if output_name.casefold().endswith(".png") else output_name


def compile_interface_art_pack(
    plan: LanePlan, *, catalog_has: Callable[[str], bool] | None = None
) -> InterfaceArtPack:
    """Compile one lane plan into pack resources plus its index document."""

    if not isinstance(plan, LanePlan):
        raise InterfaceArtPackError("interface-art plan is not a LanePlan")

    resources: list[dict[str, object]] = []
    images: dict[str, str] = {}
    atlas_rows: list[dict[str, object]] = []
    gaps: list[dict[str, object]] = [
        {"id": gap.image_id, "reason": gap.reason, "owners": list(gap.owners)}
        for gap in plan.gaps
    ]
    absent_atlases: list[str] = []

    for atlas in plan.atlases:
        if catalog_has is not None and not catalog_has(atlas.source_relative):
            absent_atlases.append(atlas.source_relative)
            for crop in atlas.crops:
                gaps.append(
                    {
                        "id": crop.image_id,
                        "reason": CATALOG_ABSENT_REASON,
                        "owners": [atlas.source_relative],
                    }
                )
            continue
        crops = list(atlas.crops)
        chunks = [
            crops[start : start + MAX_CROPS_PER_RESOURCE]
            for start in range(0, len(crops), MAX_CROPS_PER_RESOURCE)
        ]
        for index, chunk in enumerate(chunks):
            resources.append(
                {
                    "id": f"interface-art-{atlas.digest}-{index:02d}",
                    "kind": "ui",
                    "converter": "texture-atlas-crops",
                    "patterns": [atlas.source_relative],
                    "output": atlas.output_directory,
                    "options": {
                        "crops": [
                            {
                                "logicalName": _logical_name(crop.output_name),
                                "output": crop.output_name,
                                "crop": [crop.left, crop.top, crop.width, crop.height],
                            }
                            for crop in chunk
                        ]
                    },
                    "required": True,
                    "limit": 1,
                    "expected_count": 1,
                }
            )
        for crop in crops:
            images[crop.image_id] = f"{atlas.output_directory}/{crop.output_name}"
        atlas_rows.append(
            {
                "texture": atlas.texture,
                "source": atlas.source_relative,
                "digest": atlas.digest,
                "imageCount": len(crops),
            }
        )

    document: dict[str, object] = {
        "schema": PACK_INDEX_SCHEMA,
        "schemaVersion": PACK_INDEX_SCHEMA_VERSION,
        "scope": plan.scope,
        "atlases": atlas_rows,
        "images": dict(
            sorted(images.items(), key=lambda item: (item[0].casefold(), item[0]))
        ),
        "gaps": sorted(
            gaps,
            key=lambda row: (
                str(row["id"]).casefold(),
                str(row["id"]),
                str(row["reason"]),
            ),
        ),
    }
    receipt = {
        "runtimePath": INTERFACE_ART_RUNTIME_PATH,
        "packFileKey": INTERFACE_ART_PACK_KEY,
        "scope": plan.scope,
        "atlasCount": len(atlas_rows),
        "imageCount": len(images),
        "gapCount": len(document["gaps"]),
        "catalogAbsentAtlases": sorted(absent_atlases, key=str.casefold),
    }
    return InterfaceArtPack(tuple(resources), document, receipt)


def interface_art_image_ids(document: object) -> frozenset[str]:
    """Casefolded ids a published index resolves (used by gates and tests)."""

    if not isinstance(document, dict):
        return frozenset()
    images = document.get("images")
    if not isinstance(images, dict):
        return frozenset()
    return frozenset(str(key).casefold() for key in images)


__all__ = [
    "CATALOG_ABSENT_REASON",
    "INTERFACE_ART_PACK_KEY",
    "INTERFACE_ART_RUNTIME_PATH",
    "InterfaceArtPack",
    "InterfaceArtPackError",
    "compile_interface_art_pack",
    "interface_art_image_ids",
]
