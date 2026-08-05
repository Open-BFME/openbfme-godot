"""Interface-art compiler lane: retail atlas crops -> shipped pack PNGs.

Retail authors every icon and portrait as a rectangle inside a compiled texture
atlas.  This lane reads the ``MappedImage`` definitions from the retail oracle,
cuts each referenced rectangle out of its atlas **verbatim** (rulebook S4: exact
pixels, no resampling, no restyling, no recolouring) and writes it as a PNG into
a content pack, together with an index the Godot ContentDB can consume.

Documented pack path scheme
---------------------------

``assets/ui/interface-art/<atlas-digest>/<image-slug>-<image-digest>.png``
    ``<atlas-digest>`` is ``sha256(atlas virtual path, casefolded)[:12]`` and
    ``<image-slug>-<image-digest>`` is the same ``<slug>-<sha256(id)[:8]>``
    convention the unit/structure/spellbook pack compilers already publish, so
    a consumer can resolve an image either through the index or from its id
    alone.

``data/interface-art/index.json``
    ``{"schema": "openbfme.interface-art-index", "schemaVersion": 1,
       "scope": ..., "images": {<MappedImage id>: <pack-relative png>},
       "atlases": [...], "gaps": [{"id": ..., "reason": ...}]}``
    Declared from ``pack.json`` as ``files.interfaceArt``.

Every reference the lane cannot ship is recorded in ``gaps`` with a reason.
Nothing is skipped silently (rulebook P7).

Run it standalone::

    python -m openbfme_importer.interface_art_lane --scope men --out DIR
"""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path

from .interface_art import (
    DEFAULT_ORACLE_ROOT,
    PACK_IMAGE_ROOT,
    PACK_INDEX_RELATIVE,
    PACK_INDEX_SCHEMA,
    InterfaceArtError,
    atlas_digest,
    collect_image_references,
    compiled_atlas_path,
    image_key,
    load_mapped_images,
    pack_image_filename,
    scope_tokens,
)

#: Pillow is pinned for byte-deterministic PNG output; the pipeline enforces the
#: same version, so the lane cannot silently produce differently encoded images.
REQUIRED_PILLOW_VERSION = "12.2.0"

PACK_INDEX_SCHEMA_VERSION = 1


class InterfaceArtLaneError(InterfaceArtError):
    """The interface-art lane cannot emit the requested images."""


@dataclass(frozen=True, slots=True)
class CropPlan:
    """One image cut from one atlas."""

    image_id: str
    output_name: str
    left: int
    top: int
    width: int
    height: int

    @property
    def rectangle(self) -> tuple[int, int, int, int]:
        return (self.left, self.top, self.width, self.height)


@dataclass(frozen=True, slots=True)
class AtlasPlan:
    """Every crop taken from one compiled atlas."""

    texture: str
    source_relative: str
    digest: str
    output_directory: str
    crops: tuple[CropPlan, ...]


@dataclass(frozen=True, slots=True)
class LaneGap:
    """A reference the lane provably cannot ship, named with its reason."""

    image_id: str
    reason: str
    owners: tuple[str, ...] = ()


@dataclass(frozen=True)
class LanePlan:
    scope: str
    oracle_root: str
    atlases: tuple[AtlasPlan, ...]
    gaps: tuple[LaneGap, ...]

    @property
    def image_count(self) -> int:
        return sum(len(atlas.crops) for atlas in self.atlases)


@dataclass(frozen=True)
class LaneResult:
    written_count: int
    index: Mapping[str, str]
    gaps: tuple[LaneGap, ...]
    index_path: str


def plan_interface_art(
    oracle_root: Path = DEFAULT_ORACLE_ROOT, *, scope: str = "all"
) -> LanePlan:
    """Resolve every referenced UI image to an exact atlas rectangle."""

    oracle_root = Path(oracle_root)
    if not oracle_root.is_dir():
        raise InterfaceArtLaneError(f"retail oracle root does not exist: {oracle_root}")

    mapped, ambiguous = load_mapped_images(oracle_root)
    references = collect_image_references(
        oracle_root, object_path_tokens=scope_tokens(scope), known_image_ids=mapped
    )
    if not references:
        raise InterfaceArtLaneError(
            f"no UI image references found under {oracle_root} (scope={scope})"
        )
    ambiguous_keys = {image_key(value) for value in ambiguous}

    canonical: dict[str, str] = {}
    owners: dict[str, list[str]] = {}
    for reference in references:
        key = image_key(reference.image_id)
        canonical.setdefault(key, reference.image_id)
        owners.setdefault(key, []).append(
            f"{reference.kind}:{reference.owner}.{reference.field}"
        )

    grouped: dict[str, list[CropPlan]] = {}
    textures: dict[str, str] = {}
    atlas_cache: dict[str, Path | None] = {}
    gaps: list[LaneGap] = []
    for key in sorted(canonical):
        image_id = canonical[key]
        owner_names = tuple(sorted(set(owners[key])))
        if key in ambiguous_keys:
            gaps.append(LaneGap(image_id, "ambiguous-mapped-image", owner_names))
            continue
        record = mapped.get(key)
        if record is None:
            gaps.append(LaneGap(image_id, "no-mapped-image", owner_names))
            continue
        texture_key = record.texture.casefold()
        if texture_key not in atlas_cache:
            atlas_cache[texture_key] = compiled_atlas_path(oracle_root, record.texture)
        source = atlas_cache[texture_key]
        if source is None:
            gaps.append(LaneGap(image_id, "retail-atlas-absent", owner_names))
            continue
        textures[texture_key] = record.texture
        grouped.setdefault(texture_key, []).append(
            CropPlan(
                image_id=image_id,
                output_name=pack_image_filename(image_id),
                left=record.left,
                top=record.top,
                width=record.right - record.left,
                height=record.bottom - record.top,
            )
        )

    atlases: list[AtlasPlan] = []
    for texture_key in sorted(grouped):
        texture = textures[texture_key]
        source = atlas_cache[texture_key]
        assert source is not None  # guarded above
        relative = source.relative_to(oracle_root).as_posix()
        digest = atlas_digest(relative)
        atlases.append(
            AtlasPlan(
                texture=texture,
                source_relative=relative,
                digest=digest,
                output_directory=f"{PACK_IMAGE_ROOT}/{digest}",
                crops=tuple(
                    sorted(
                        grouped[texture_key],
                        key=lambda crop: (crop.image_id.casefold(), crop.image_id),
                    )
                ),
            )
        )

    return LanePlan(
        scope=scope,
        oracle_root=oracle_root.as_posix(),
        atlases=tuple(atlases),
        gaps=tuple(
            sorted(gaps, key=lambda gap: (gap.image_id.casefold(), gap.image_id))
        ),
    )


def _require_pillow():
    try:
        from PIL import Image
        import PIL
    except ImportError as exc:  # pragma: no cover - environment guard
        raise InterfaceArtLaneError(
            "Pillow is required for deterministic interface-art conversion"
        ) from exc
    if PIL.__version__ != REQUIRED_PILLOW_VERSION:
        raise InterfaceArtLaneError(
            f"Pillow {REQUIRED_PILLOW_VERSION} is required for deterministic "
            f"texture output; found {PIL.__version__}"
        )
    return Image


def emit_interface_art(
    plan: LanePlan, oracle_root: Path, out_dir: Path
) -> LaneResult:
    """Write every planned crop as a PNG plus the consumable index."""

    Image = _require_pillow()
    oracle_root = Path(oracle_root)
    out_dir = Path(out_dir)

    index: dict[str, str] = {}
    written = 0
    for atlas in plan.atlases:
        source = oracle_root / atlas.source_relative
        if not source.is_file():
            raise InterfaceArtLaneError(f"atlas source is missing: {source}")
        target_root = out_dir / atlas.output_directory
        target_root.mkdir(parents=True, exist_ok=True)
        with Image.open(source) as opened:
            width, height = opened.size
            for crop in atlas.crops:
                if (
                    crop.left + crop.width > width
                    or crop.top + crop.height > height
                ):
                    raise InterfaceArtLaneError(
                        f"interface-art crop {crop.image_id!r} is outside "
                        f"{atlas.texture} ({width}x{height})"
                    )
            converted = opened.convert("RGBA")
            for crop in atlas.crops:
                target = target_root / crop.output_name
                converted.crop(
                    (
                        crop.left,
                        crop.top,
                        crop.left + crop.width,
                        crop.top + crop.height,
                    )
                ).save(target, format="PNG", compress_level=9, optimize=False)
                index[crop.image_id] = f"{atlas.output_directory}/{crop.output_name}"
                written += 1

    manifest_path = out_dir / PACK_INDEX_RELATIVE
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(
            {
                "schema": PACK_INDEX_SCHEMA,
                "schemaVersion": PACK_INDEX_SCHEMA_VERSION,
                "scope": plan.scope,
                "atlases": [
                    {
                        "texture": atlas.texture,
                        "source": atlas.source_relative,
                        "digest": atlas.digest,
                        "imageCount": len(atlas.crops),
                    }
                    for atlas in plan.atlases
                ],
                "images": dict(sorted(index.items(), key=lambda item: item[0].casefold())),
                "gaps": [
                    {
                        "id": gap.image_id,
                        "reason": gap.reason,
                        "owners": list(gap.owners),
                    }
                    for gap in plan.gaps
                ],
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    return LaneResult(
        written_count=written,
        index=dict(index),
        gaps=plan.gaps,
        index_path=PACK_INDEX_RELATIVE,
    )


def _parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.interface_art_lane",
        description="Crop retail UI atlases into a content pack, verbatim.",
    )
    parser.add_argument("--oracle-root", type=Path, default=DEFAULT_ORACLE_ROOT)
    parser.add_argument("--scope", default="all")
    parser.add_argument("--out", type=Path, required=True, help="output pack directory")
    parser.add_argument("--plan-only", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = _parse_args(argv)
    try:
        plan = plan_interface_art(args.oracle_root, scope=args.scope)
        print(
            f"scope={plan.scope} atlases={len(plan.atlases)} "
            f"images={plan.image_count} gaps={len(plan.gaps)}"
        )
        reasons: dict[str, int] = {}
        for gap in plan.gaps:
            reasons[gap.reason] = reasons.get(gap.reason, 0) + 1
        for reason, count in sorted(reasons.items()):
            print(f"  gap {reason}: {count}")
        if args.plan_only:
            return 0
        result = emit_interface_art(plan, args.oracle_root, args.out)
    except InterfaceArtError as exc:
        print(f"interface-art lane ERROR: {exc}", file=sys.stderr)
        return 2
    print(f"wrote {result.written_count} PNGs into {args.out}")
    print(f"index: {args.out / result.index_path}")
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    raise SystemExit(main())
