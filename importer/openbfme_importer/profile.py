"""Importer profile parsing and catalog closure resolution."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
from typing import Any

from .catalog import CatalogEntry, InstallCatalog
from .paths import safe_relative_parts


ALLOWED_KINDS = {
    "data",
    "map",
    "model",
    "skeleton",
    "animation",
    "texture",
    "ui",
    "music",
    "audio",
}
ALLOWED_CONVERTERS = {
    "copy",
    "hash-only",
    "text",
    "texture",
    "texture-crop",
    "texture-atlas-crops",
    "audio",
    "w3d-model",
    "w3d-animation",
    "w3d-bundle",
    "w3d-hierarchical",
    "w3d-static",
    "map",
    "sage-map",
    "sage-apt-runtime",
    "retail-unit-rules",
    "sage-particle-definition",
    "sage-scripts",
    "sage-terrain-materials",
}
SLUG_PATTERN = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
# Resource-exhaustion bound on profile JSON parsing, not a semantic limit on
# how much a pack may contain. A single-faction slice profile is ~5-6 MB, so
# the old 16 MiB ceiling silently capped a pack at roughly three factions; one
# composed from all six BFME2 factions is ~22 MB and a cross-faction skirmish
# needs every side in the same pack. 64 MiB keeps the guard meaningful against
# a hostile file while leaving headroom for a full six-faction compose.
MAX_PROFILE_BYTES = 64 * 1024 * 1024
# The exact Fords closure alone is now larger than the original provisional
# 256-rule ceiling once neutral lifecycles and particle definitions are kept as
# independent, auditable resources.  Keep a hard bound, but size it for one
# complete retail map/faction expansion instead of forcing unrelated leaves
# into ambiguous wildcard owners. The byte bound remains independent.
# Raised from 4_096 once packs stopped being single-faction. That ceiling was
# sized for "one complete retail map/faction expansion" and a single faction
# already spends ~3_167 of it (Men), so any two-faction compose blew it —
# which is why a cross-faction skirmish was never cookable. All six BFME2
# factions compose to ~12_570. Still a hard bound, just sized for the pack
# shape the project actually needs. The byte bound remains independent.
MAX_RESOURCES = 32_768
MAX_PATTERNS_PER_RESOURCE = 256
MAX_PATH_LENGTH = 512
W3D_DEPENDENCY_CONVERTERS = {
    "w3d-bundle",
    "w3d-hierarchical",
    "w3d-static",
}
W3D_INPUT_RESOURCE_IDS_OPTION = "inputResourceIds"
W3D_EXCLUDED_OPTIONAL_MESHES_OPTION = "excludedOptionalMeshes"
W3D_PROVEN_ROOT_RIGID_BAKE_OPTION = "provenRootRigidBake"
W3D_PROVEN_PIVOT_ONLY_MODEL_OPTION = "provenPivotOnlyModel"
W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION = "provenNoMotionAnimations"
W3D_TEXTURE_OVERRIDES_OPTION = "textureOverrides"
W3D_RETAIL_ABSENT_TEXTURES_OPTION = "retailAbsentTextures"
W3D_SOURCE_VARIANT_OF_OPTION = "sourceVariantOf"
MAX_W3D_OPTIONAL_MESH_EXCLUSIONS = 64
MAX_W3D_TEXTURE_OVERRIDES = 16
MAX_W3D_RETAIL_ABSENT_TEXTURES = 16
MAX_W3D_NO_MOTION_ANIMATIONS = 16
W3D_CLEAN_MESH_IDENTIFIER_PATTERN = re.compile(
    r"^[a-z0-9](?:[a-z0-9_]{0,126}[a-z0-9])?$"
)
W3D_TEXTURE_BASENAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
W3D_TEXTURE_SUFFIXES = {".bmp", ".dds", ".jpeg", ".jpg", ".png", ".tga"}
MAX_TERRAIN_MATERIAL_SYMBOLS = 4_096
TERRAIN_MATERIAL_SYMBOL_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
MAX_TEXTURE_ATLAS_CROPS = 64
TEXTURE_ATLAS_LOGICAL_NAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
TEXTURE_ATLAS_OUTPUT_PART_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
PARTICLE_DEFINITION_KINDS = frozenset({"ParticleSystem", "FXParticleSystem"})
PARTICLE_DEFINITION_NAME_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_.+:-]{0,255}$")


def _reject_json_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON number is not allowed: {value}")


def normalize_excluded_optional_meshes(value: Any) -> list[str]:
    """Validate exact adapter clean-name identifiers and return canonical order."""

    if (
        not isinstance(value, list)
        or len(value) > MAX_W3D_OPTIONAL_MESH_EXCLUSIONS
        or any(not isinstance(identifier, str) for identifier in value)
    ):
        raise ValueError(
            f"{W3D_EXCLUDED_OPTIONAL_MESHES_OPTION} must be an array of at most "
            f"{MAX_W3D_OPTIONAL_MESH_EXCLUSIONS} strings"
        )
    if len(value) != len(set(value)):
        raise ValueError(f"{W3D_EXCLUDED_OPTIONAL_MESHES_OPTION} contains duplicates")
    for identifier in value:
        if not W3D_CLEAN_MESH_IDENTIFIER_PATTERN.fullmatch(identifier):
            raise ValueError(
                f"{W3D_EXCLUDED_OPTIONAL_MESHES_OPTION} contains an invalid clean mesh "
                f"identifier: {identifier!r}"
            )
    return sorted(value)


def normalize_w3d_texture_overrides(value: Any) -> list[dict[str, str]]:
    """Validate exact job-local W3D texture aliases and return canonical records."""

    if not isinstance(value, list) or not 1 <= len(value) <= MAX_W3D_TEXTURE_OVERRIDES:
        raise ValueError(
            f"{W3D_TEXTURE_OVERRIDES_OPTION} must be an array of 1.."
            f"{MAX_W3D_TEXTURE_OVERRIDES} records"
        )

    normalized: list[dict[str, str]] = []
    for record in value:
        if not isinstance(record, dict) or set(record) != {
            "authored",
            "target",
            "source",
        }:
            raise ValueError(
                f"{W3D_TEXTURE_OVERRIDES_OPTION} records must contain exactly "
                "authored, target, and source"
            )
        canonical: dict[str, str] = {}
        for field in ("authored", "target", "source"):
            basename = record[field]
            try:
                basename_parts = (
                    safe_relative_parts(basename) if isinstance(basename, str) else ()
                )
            except ValueError:
                basename_parts = ()
            if (
                not isinstance(basename, str)
                or not W3D_TEXTURE_BASENAME_PATTERN.fullmatch(basename)
                or len(basename_parts) != 1
                or Path(basename).suffix.casefold() not in W3D_TEXTURE_SUFFIXES
            ):
                raise ValueError(
                    f"{W3D_TEXTURE_OVERRIDES_OPTION} {field} must be a safe "
                    "supported texture basename"
                )
            canonical[field] = basename.casefold()

        if Path(canonical["authored"]).stem != Path(canonical["target"]).stem:
            raise ValueError(
                f"{W3D_TEXTURE_OVERRIDES_OPTION} authored and target stems must match"
            )
        if Path(canonical["target"]).suffix != Path(canonical["source"]).suffix:
            raise ValueError(
                f"{W3D_TEXTURE_OVERRIDES_OPTION} target and source suffixes must match"
            )
        if canonical["target"] == canonical["source"]:
            raise ValueError(
                f"{W3D_TEXTURE_OVERRIDES_OPTION} target and source must be distinct"
            )
        normalized.append(canonical)

    normalized.sort(key=lambda item: (item["target"], item["source"], item["authored"]))
    authored = [item["authored"] for item in normalized]
    targets = [item["target"] for item in normalized]
    sources = [item["source"] for item in normalized]
    if len(set(authored)) != len(authored) or len(set(targets)) != len(targets):
        raise ValueError(
            f"{W3D_TEXTURE_OVERRIDES_OPTION} contains duplicate authored or target names"
        )
    if set(targets) & set(sources):
        raise ValueError(
            f"{W3D_TEXTURE_OVERRIDES_OPTION} cannot chain target and source names"
        )
    return normalized


def normalize_retail_absent_textures(value: Any) -> list[str]:
    """Validate scanner-recorded retail-absent texture basenames."""

    if (
        not isinstance(value, list)
        or len(value) > MAX_W3D_RETAIL_ABSENT_TEXTURES
        or any(not isinstance(basename, str) for basename in value)
    ):
        raise ValueError(
            f"{W3D_RETAIL_ABSENT_TEXTURES_OPTION} must be an array of at most "
            f"{MAX_W3D_RETAIL_ABSENT_TEXTURES} strings"
        )
    if len(value) != len(set(value)):
        raise ValueError(f"{W3D_RETAIL_ABSENT_TEXTURES_OPTION} contains duplicates")
    for basename in value:
        try:
            basename_parts = (
                safe_relative_parts(basename) if isinstance(basename, str) else ()
            )
        except ValueError:
            basename_parts = ()
        if (
            not isinstance(basename, str)
            or not W3D_TEXTURE_BASENAME_PATTERN.fullmatch(basename)
            or len(basename_parts) != 1
            or Path(basename).suffix.casefold() not in W3D_TEXTURE_SUFFIXES
        ):
            raise ValueError(
                f"{W3D_RETAIL_ABSENT_TEXTURES_OPTION} must contain only safe "
                "supported texture basenames"
            )
    return sorted(value, key=str.casefold)


def normalize_w3d_no_motion_animations(value: Any) -> list[dict[str, Any]]:
    """Validate exact header-only animation declarations and canonicalize order."""

    if (
        not isinstance(value, list)
        or not 1 <= len(value) <= MAX_W3D_NO_MOTION_ANIMATIONS
    ):
        raise ValueError(
            f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION} must be an array of 1.."
            f"{MAX_W3D_NO_MOTION_ANIMATIONS} records"
        )

    normalized: list[dict[str, Any]] = []
    folded_ids: set[str] = set()
    required = {
        "identifier",
        "hierarchyIdentifier",
        "frameCount",
        "frameRate",
        "compressed",
        "modelIdentifier",
    }
    for record in value:
        if not isinstance(record, dict):
            raise ValueError(
                f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION} records must be objects"
            )
        fields = set(record)
        if fields not in (required, required | {"flavor"}):
            raise ValueError(
                f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION} records have unsupported fields"
            )
        canonical: dict[str, Any] = {}
        for field in ("identifier", "hierarchyIdentifier", "modelIdentifier"):
            identifier = record[field]
            if not isinstance(identifier, str) or not identifier or "\0" in identifier:
                raise ValueError(
                    f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION}.{field} is invalid"
                )
            try:
                encoded = identifier.encode("cp1252")
            except UnicodeEncodeError as exc:
                raise ValueError(
                    f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION}.{field} is not CP1252"
                ) from exc
            if len(encoded) > 16:
                raise ValueError(
                    f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION}.{field} exceeds 16 bytes"
                )
            canonical[field] = identifier
        key = canonical["identifier"].casefold()
        if key in folded_ids:
            raise ValueError(
                f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION} contains duplicate identifiers"
            )
        folded_ids.add(key)
        for field in ("frameCount", "frameRate"):
            number = record[field]
            if isinstance(number, bool) or not isinstance(number, int) or number <= 0:
                raise ValueError(
                    f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION}.{field} must be a positive integer"
                )
            canonical[field] = number
        compressed = record["compressed"]
        if type(compressed) is not bool:
            raise ValueError(
                f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION}.compressed must be a boolean"
            )
        canonical["compressed"] = compressed
        if compressed:
            flavor = record.get("flavor")
            if (
                isinstance(flavor, bool)
                or not isinstance(flavor, int)
                or not 0 <= flavor <= 0xFFFF
            ):
                raise ValueError(
                    f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION}.flavor must be a 16-bit integer"
                )
            canonical["flavor"] = flavor
        elif "flavor" in record:
            raise ValueError(
                f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION}.flavor is valid only for compressed headers"
            )
        normalized.append(canonical)
    return sorted(normalized, key=lambda item: item["identifier"])


def _validate_w3d_input_dependencies(resources: list["ResourceRule"]) -> None:
    """Validate optional, profile-local raw-input closure declarations."""

    by_id = {resource.id: resource for resource in resources}
    graph: dict[str, tuple[str, ...]] = {}
    for resource in resources:
        if W3D_INPUT_RESOURCE_IDS_OPTION not in resource.options:
            continue
        if resource.converter not in W3D_DEPENDENCY_CONVERTERS:
            raise ValueError(
                f"resource {resource.id!r} uses {W3D_INPUT_RESOURCE_IDS_OPTION} "
                "without a W3D bundle converter"
            )
        raw_dependencies = resource.options[W3D_INPUT_RESOURCE_IDS_OPTION]
        if (
            not isinstance(raw_dependencies, list)
            or len(raw_dependencies) > MAX_RESOURCES
            or any(not isinstance(value, str) for value in raw_dependencies)
        ):
            raise ValueError(
                f"resource {resource.id!r} {W3D_INPUT_RESOURCE_IDS_OPTION} must be "
                f"an array of at most {MAX_RESOURCES} resource ids"
            )
        if len(raw_dependencies) != len(set(raw_dependencies)):
            raise ValueError(
                f"resource {resource.id!r} {W3D_INPUT_RESOURCE_IDS_OPTION} contains duplicates"
            )
        dependencies = tuple(raw_dependencies)
        for dependency_id in dependencies:
            if not SLUG_PATTERN.fullmatch(dependency_id):
                raise ValueError(
                    f"resource {resource.id!r} has an invalid input resource id: {dependency_id!r}"
                )
            if dependency_id == resource.id:
                raise ValueError(
                    f"resource {resource.id!r} cannot list itself in {W3D_INPUT_RESOURCE_IDS_OPTION}"
                )
            if dependency_id not in by_id:
                raise ValueError(
                    f"resource {resource.id!r} references unknown input resource {dependency_id!r}"
                )
        graph[resource.id] = dependencies

    states: dict[str, int] = {}

    def visit(resource_id: str) -> None:
        state = states.get(resource_id, 0)
        if state == 1:
            raise ValueError("W3D input resource dependency cycle detected")
        if state == 2:
            return
        states[resource_id] = 1
        for dependency_id in graph.get(resource_id, ()):
            visit(dependency_id)
        states[resource_id] = 2

    for resource_id in sorted(graph):
        visit(resource_id)


def _validate_w3d_source_variants(resources: list["ResourceRule"]) -> None:
    """Allow one source to cook into distinct, explicitly proven W3D outputs."""

    by_id = {resource.id: resource for resource in resources}
    for resource in resources:
        if W3D_SOURCE_VARIANT_OF_OPTION not in resource.options:
            continue
        owner_id = resource.options[W3D_SOURCE_VARIANT_OF_OPTION]
        if not isinstance(owner_id, str) or not SLUG_PATTERN.fullmatch(owner_id):
            raise ValueError(
                f"resource {resource.id!r} has an invalid "
                f"{W3D_SOURCE_VARIANT_OF_OPTION} resource id"
            )
        if resource.converter not in W3D_DEPENDENCY_CONVERTERS:
            raise ValueError(
                f"resource {resource.id!r} uses {W3D_SOURCE_VARIANT_OF_OPTION} "
                "without a W3D bundle converter"
            )
        owner = by_id.get(owner_id)
        if owner is None or owner is resource:
            raise ValueError(
                f"resource {resource.id!r} references an unknown or self "
                f"{W3D_SOURCE_VARIANT_OF_OPTION}: {owner_id!r}"
            )
        if W3D_SOURCE_VARIANT_OF_OPTION in owner.options:
            raise ValueError("W3D source variants cannot form chains")
        if (
            owner.kind != resource.kind
            or owner.converter != resource.converter
            or owner.patterns != resource.patterns
            or owner.options.get("model") != resource.options.get("model")
        ):
            raise ValueError(
                f"resource {resource.id!r} does not exactly share its W3D "
                f"source contract with {owner_id!r}"
            )
        if (
            owner.output is None
            or resource.output is None
            or owner.output.casefold() == resource.output.casefold()
        ):
            raise ValueError(
                f"resource {resource.id!r} W3D source variant must have a "
                "distinct concrete output"
            )
        if W3D_TEXTURE_OVERRIDES_OPTION not in resource.options:
            raise ValueError(
                f"resource {resource.id!r} W3D source variant has no proven "
                f"{W3D_TEXTURE_OVERRIDES_OPTION} transformation"
            )


def _validate_terrain_material_options(resource: "ResourceRule") -> None:
    if resource.converter != "sage-terrain-materials":
        return
    if resource.output is None:
        raise ValueError(
            f"resource {resource.id!r} sage-terrain-materials requires an output directory"
        )
    unsupported = sorted(set(resource.options) - {"symbols"})
    if unsupported:
        raise ValueError(
            f"resource {resource.id!r} sage-terrain-materials has unsupported option(s): "
            + ", ".join(unsupported)
        )
    symbols = resource.options.get("symbols")
    if (
        not isinstance(symbols, list)
        or not 1 <= len(symbols) <= MAX_TERRAIN_MATERIAL_SYMBOLS
        or any(not isinstance(symbol, str) for symbol in symbols)
    ):
        raise ValueError(
            f"resource {resource.id!r} sage-terrain-materials options.symbols must be "
            f"an array of 1..{MAX_TERRAIN_MATERIAL_SYMBOLS} strings"
        )
    folded: set[str] = set()
    for symbol in symbols:
        if not TERRAIN_MATERIAL_SYMBOL_PATTERN.fullmatch(symbol):
            raise ValueError(
                f"resource {resource.id!r} has an unsafe terrain material symbol: {symbol!r}"
            )
        key = symbol.casefold()
        if key in folded:
            raise ValueError(
                f"resource {resource.id!r} has duplicate terrain material symbols"
            )
        folded.add(key)


def _validate_particle_definition_options(resource: "ResourceRule") -> None:
    if resource.converter != "sage-particle-definition":
        return
    if (
        len(resource.patterns) != 1
        or resource.limit != 1
        or resource.expected_count != 1
    ):
        raise ValueError(
            f"resource {resource.id!r} sage-particle-definition requires exactly "
            "one pattern with limit=1 and expected_count=1"
        )
    if (
        resource.output is None
        or Path(resource.output).suffix.casefold() != ".json"
        or "{" in resource.output
        or "}" in resource.output
    ):
        raise ValueError(
            f"resource {resource.id!r} sage-particle-definition requires a .json output"
        )
    if set(resource.options) != {"kind", "name"}:
        raise ValueError(
            f"resource {resource.id!r} sage-particle-definition options must contain "
            "exactly kind and name"
        )
    kind = resource.options["kind"]
    name = resource.options["name"]
    if kind not in PARTICLE_DEFINITION_KINDS:
        raise ValueError(
            f"resource {resource.id!r} has an unsupported particle definition kind"
        )
    if not isinstance(name, str) or not PARTICLE_DEFINITION_NAME_PATTERN.fullmatch(
        name
    ):
        raise ValueError(
            f"resource {resource.id!r} has an unsafe particle definition name"
        )


def normalize_texture_atlas_crops(
    crops: Any,
    output_directory: str,
    *,
    context: str = "texture-atlas-crops",
) -> list[dict[str, Any]]:
    """Validate and canonically order a bounded atlas crop declaration."""

    if not isinstance(crops, list) or not 1 <= len(crops) <= MAX_TEXTURE_ATLAS_CROPS:
        raise ValueError(
            f"{context} options.crops must be "
            f"an array of 1..{MAX_TEXTURE_ATLAS_CROPS} crop records"
        )

    logical_names: set[str] = set()
    output_names: set[str] = set()
    normalized: list[dict[str, Any]] = []
    for crop_record in crops:
        if not isinstance(crop_record, dict) or set(crop_record) != {
            "logicalName",
            "output",
            "crop",
        }:
            raise ValueError(
                f"{context} crop records must "
                "contain exactly logicalName, output, and crop"
            )
        logical_name = crop_record["logicalName"]
        if not isinstance(
            logical_name, str
        ) or not TEXTURE_ATLAS_LOGICAL_NAME_PATTERN.fullmatch(logical_name):
            raise ValueError(f"{context} has an unsafe texture atlas logicalName")
        logical_key = logical_name.casefold()
        if logical_key in logical_names:
            raise ValueError(
                f"{context} has duplicate texture atlas logicalName values"
            )
        logical_names.add(logical_key)

        output_name = crop_record["output"]
        if not isinstance(output_name, str) or len(output_name) > MAX_PATH_LENGTH:
            raise ValueError(f"{context} has an unsafe texture atlas crop output")
        try:
            output_parts = safe_relative_parts(output_name)
        except ValueError as exc:
            raise ValueError(
                f"{context} has an unsafe texture atlas crop output"
            ) from exc
        if (
            any(
                not TEXTURE_ATLAS_OUTPUT_PART_PATTERN.fullmatch(part)
                for part in output_parts
            )
            or not output_name.casefold().endswith(".png")
            or len(output_directory.rstrip("/\\") + "/" + output_name) > MAX_PATH_LENGTH
        ):
            raise ValueError(f"{context} has an unsafe texture atlas crop output")
        output_key = "/".join(part.casefold() for part in output_parts)
        if output_key in output_names:
            raise ValueError(f"{context} has duplicate texture atlas crop outputs")
        output_names.add(output_key)

        crop = crop_record["crop"]
        if not (
            isinstance(crop, list)
            and len(crop) == 4
            and all(
                isinstance(value, int) and not isinstance(value, bool) and value >= 0
                for value in crop
            )
            and crop[2] > 0
            and crop[3] > 0
        ):
            raise ValueError(
                f"{context} crop must be "
                "[x,y,width,height] with nonnegative integer coordinates and positive dimensions"
            )
        normalized.append(
            {
                "logicalName": logical_name,
                "output": "/".join(output_parts),
                "crop": list(crop),
            }
        )

    return sorted(
        normalized,
        key=lambda item: (item["logicalName"].casefold(), item["output"].casefold()),
    )


def _validate_texture_atlas_crop_options(resource: "ResourceRule") -> None:
    if resource.converter != "texture-atlas-crops":
        return
    if resource.expected_count != 1:
        raise ValueError(
            f"resource {resource.id!r} texture-atlas-crops requires expected_count=1"
        )
    if resource.output is None:
        raise ValueError(
            f"resource {resource.id!r} texture-atlas-crops requires an output directory"
        )
    output_directory_parts = safe_relative_parts(resource.output)
    if any(
        not TEXTURE_ATLAS_OUTPUT_PART_PATTERN.fullmatch(part)
        for part in output_directory_parts
    ):
        raise ValueError(
            f"resource {resource.id!r} has an unsafe texture atlas output directory"
        )
    unsupported = sorted(set(resource.options) - {"crops"})
    if unsupported:
        raise ValueError(
            f"resource {resource.id!r} texture-atlas-crops has unsupported option(s): "
            + ", ".join(unsupported)
        )
    resource.options["crops"] = normalize_texture_atlas_crops(
        resource.options.get("crops"),
        resource.output,
        context=f"resource {resource.id!r} texture-atlas-crops",
    )


def _validate_hierarchical_w3d_options(resource: "ResourceRule") -> None:
    if (
        W3D_PROVEN_ROOT_RIGID_BAKE_OPTION in resource.options
        and resource.converter != "w3d-hierarchical"
    ):
        raise ValueError(
            f"resource {resource.id!r} uses "
            f"{W3D_PROVEN_ROOT_RIGID_BAKE_OPTION} without w3d-hierarchical"
        )
    if (
        W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION in resource.options
        and resource.converter != "w3d-hierarchical"
    ):
        raise ValueError(
            f"resource {resource.id!r} uses "
            f"{W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION} without w3d-hierarchical"
        )
    if (
        W3D_PROVEN_PIVOT_ONLY_MODEL_OPTION in resource.options
        and resource.converter != "w3d-hierarchical"
    ):
        raise ValueError(
            f"resource {resource.id!r} uses "
            f"{W3D_PROVEN_PIVOT_ONLY_MODEL_OPTION} without w3d-hierarchical"
        )
    if resource.converter != "w3d-hierarchical":
        return
    model = resource.options.get("model")
    if not isinstance(model, str) or not model.strip():
        raise ValueError(
            f"resource {resource.id!r} w3d-hierarchical requires options.model"
        )
    animations = resource.options.get("animations", [])
    if animations != []:
        raise ValueError(
            f"resource {resource.id!r} w3d-hierarchical forbids animations"
        )
    required_equipment = resource.options.get("required_equipment", [])
    if required_equipment != []:
        raise ValueError(
            f"resource {resource.id!r} w3d-hierarchical forbids required equipment"
        )
    root_rigid_bake = resource.options.get(W3D_PROVEN_ROOT_RIGID_BAKE_OPTION, False)
    if not isinstance(root_rigid_bake, bool):
        raise ValueError(
            f"resource {resource.id!r} w3d-hierarchical "
            f"{W3D_PROVEN_ROOT_RIGID_BAKE_OPTION} must be a boolean"
        )
    pivot_only = resource.options.get(W3D_PROVEN_PIVOT_ONLY_MODEL_OPTION, False)
    if not isinstance(pivot_only, bool):
        raise ValueError(
            f"resource {resource.id!r} w3d-hierarchical "
            f"{W3D_PROVEN_PIVOT_ONLY_MODEL_OPTION} must be a boolean"
        )
    if pivot_only and root_rigid_bake:
        raise ValueError(
            f"resource {resource.id!r} w3d-hierarchical cannot combine "
            f"{W3D_PROVEN_PIVOT_ONLY_MODEL_OPTION} and "
            f"{W3D_PROVEN_ROOT_RIGID_BAKE_OPTION}"
        )
    if W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION in resource.options:
        resource.options[W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION] = (
            normalize_w3d_no_motion_animations(
                resource.options[W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION]
            )
        )


@dataclass(frozen=True, slots=True)
class ResourceRule:
    id: str
    kind: str
    patterns: tuple[str, ...]
    required: bool
    converter: str
    output: str | None
    limit: int
    expected_count: int
    options: dict[str, Any]


@dataclass(frozen=True, slots=True)
class ImportProfile:
    source_sha256: str
    id: str
    title: str
    pack_id: str
    pack_version: str
    pack_metadata: dict[str, Any]
    resources: tuple[ResourceRule, ...]
    runtime_data: dict[str, Any]

    @classmethod
    def load(cls, path: Path | str) -> "ImportProfile":
        source = Path(path).expanduser().resolve()
        if source.stat().st_size > MAX_PROFILE_BYTES:
            raise ValueError(
                f"profile exceeds {MAX_PROFILE_BYTES} byte limit: {source}"
            )
        payload = source.read_bytes()
        try:
            value = json.loads(
                payload.decode("utf-8"), parse_constant=_reject_json_constant
            )
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ValueError(f"invalid profile JSON in {source}: {exc}") from exc
        if not isinstance(value, dict):
            raise ValueError(f"profile root must be an object in {source}")
        if value.get("format") != 1:
            raise ValueError(f"unsupported profile format in {source}")
        raw_resources = value.get("resources", [])
        if (
            not isinstance(raw_resources, list)
            or not 1 <= len(raw_resources) <= MAX_RESOURCES
        ):
            raise ValueError(f"profile must contain 1..{MAX_RESOURCES} resources")
        pack_value = value.get("pack")
        if not isinstance(pack_value, dict):
            raise ValueError("profile pack must be an object")
        runtime_data = value.get("runtime_data", {})
        if not isinstance(runtime_data, dict):
            raise ValueError("profile runtime_data must be an object")
        resources: list[ResourceRule] = []
        ids: set[str] = set()
        for item in raw_resources:
            if not isinstance(item, dict):
                raise ValueError("profile resource must be an object")
            resource_id = str(item["id"])
            if not SLUG_PATTERN.fullmatch(resource_id):
                raise ValueError(
                    f"resource id must be a bounded lowercase slug: {resource_id!r}"
                )
            if resource_id in ids:
                raise ValueError(f"duplicate resource id: {resource_id}")
            ids.add(resource_id)
            kind = str(item["kind"])
            converter = str(item.get("converter", "copy"))
            if kind not in ALLOWED_KINDS:
                raise ValueError(f"unsupported resource kind {kind!r}")
            if converter not in ALLOWED_CONVERTERS:
                raise ValueError(f"unsupported converter {converter!r}")
            raw_patterns = item.get("patterns", [])
            if (
                not isinstance(raw_patterns, list)
                or len(raw_patterns) > MAX_PATTERNS_PER_RESOURCE
            ):
                raise ValueError(
                    f"resource {resource_id!r} patterns must be an array of at most {MAX_PATTERNS_PER_RESOURCE}"
                )
            patterns = tuple(
                str(pattern).replace("\\", "/") for pattern in raw_patterns
            )
            if not patterns:
                raise ValueError(f"resource {resource_id!r} has no patterns")
            if len({pattern.casefold() for pattern in patterns}) != len(patterns):
                raise ValueError(f"resource {resource_id!r} has duplicate patterns")
            for pattern in patterns:
                if len(pattern) > MAX_PATH_LENGTH:
                    raise ValueError(f"resource {resource_id!r} pattern is too long")
                safe_relative_parts(pattern)
            limit = int(item.get("limit", 1))
            if limit < 1 or limit > 10_000:
                raise ValueError(f"resource {resource_id!r} has invalid limit")
            raw_expected_count = item.get("expected_count", 0)
            if converter == "texture-atlas-crops" and (
                isinstance(raw_expected_count, bool)
                or not isinstance(raw_expected_count, int)
                or raw_expected_count != 1
            ):
                raise ValueError(
                    f"resource {resource_id!r} texture-atlas-crops requires expected_count=1"
                )
            expected_count = int(raw_expected_count)
            if expected_count < 0 or expected_count > 10_000:
                raise ValueError(f"resource {resource_id!r} has invalid expected_count")
            if expected_count and limit < expected_count:
                raise ValueError(
                    f"resource {resource_id!r} limit is smaller than expected_count"
                )
            output = str(item["output"]) if item.get("output") else None
            if output is not None:
                if len(output) > MAX_PATH_LENGTH or any(
                    character in output for character in '<>"|?*'
                ):
                    raise ValueError(
                        f"resource {resource_id!r} has an unsafe output path"
                    )
                safe_relative_parts(output)
            options = item.get("options", {})
            if not isinstance(options, dict):
                raise ValueError(f"resource {resource_id!r} options must be an object")
            options = dict(options)
            if W3D_EXCLUDED_OPTIONAL_MESHES_OPTION in options:
                if converter not in W3D_DEPENDENCY_CONVERTERS:
                    raise ValueError(
                        f"resource {resource_id!r} uses "
                        f"{W3D_EXCLUDED_OPTIONAL_MESHES_OPTION} without a W3D bundle converter"
                    )
                options[W3D_EXCLUDED_OPTIONAL_MESHES_OPTION] = (
                    normalize_excluded_optional_meshes(
                        options[W3D_EXCLUDED_OPTIONAL_MESHES_OPTION]
                    )
                )
            if W3D_TEXTURE_OVERRIDES_OPTION in options:
                if converter not in W3D_DEPENDENCY_CONVERTERS:
                    raise ValueError(
                        f"resource {resource_id!r} uses "
                        f"{W3D_TEXTURE_OVERRIDES_OPTION} without a W3D bundle converter"
                    )
                if W3D_INPUT_RESOURCE_IDS_OPTION not in options:
                    raise ValueError(
                        f"resource {resource_id!r} uses "
                        f"{W3D_TEXTURE_OVERRIDES_OPTION} without an explicit "
                        f"{W3D_INPUT_RESOURCE_IDS_OPTION} closure"
                    )
                options[W3D_TEXTURE_OVERRIDES_OPTION] = normalize_w3d_texture_overrides(
                    options[W3D_TEXTURE_OVERRIDES_OPTION]
                )
            if W3D_RETAIL_ABSENT_TEXTURES_OPTION in options:
                if converter not in W3D_DEPENDENCY_CONVERTERS:
                    raise ValueError(
                        f"resource {resource_id!r} uses "
                        f"{W3D_RETAIL_ABSENT_TEXTURES_OPTION} without a W3D bundle converter"
                    )
                options[W3D_RETAIL_ABSENT_TEXTURES_OPTION] = (
                    normalize_retail_absent_textures(
                        options[W3D_RETAIL_ABSENT_TEXTURES_OPTION]
                    )
                )
            resources.append(
                ResourceRule(
                    id=resource_id,
                    kind=kind,
                    patterns=patterns,
                    required=bool(item.get("required", True)),
                    converter=converter,
                    output=output,
                    limit=limit,
                    expected_count=expected_count,
                    options=options,
                )
            )
        _validate_w3d_input_dependencies(resources)
        _validate_w3d_source_variants(resources)
        for resource in resources:
            _validate_hierarchical_w3d_options(resource)
            _validate_particle_definition_options(resource)
            _validate_terrain_material_options(resource)
            _validate_texture_atlas_crop_options(resource)
        profile_id = str(value["id"])
        pack_id = str(pack_value["id"])
        if not SLUG_PATTERN.fullmatch(profile_id):
            raise ValueError(
                f"profile id must be a bounded lowercase slug: {profile_id!r}"
            )
        if not SLUG_PATTERN.fullmatch(pack_id):
            raise ValueError(f"pack id must be a bounded lowercase slug: {pack_id!r}")
        return cls(
            source_sha256=hashlib.sha256(payload).hexdigest(),
            id=profile_id,
            title=str(value.get("title", value["id"])),
            pack_id=pack_id,
            pack_version=str(pack_value.get("version", "0.1.0")),
            pack_metadata=dict(pack_value),
            resources=tuple(resources),
            runtime_data=dict(runtime_data),
        )


@dataclass(frozen=True, slots=True)
class ResolvedResource:
    rule: ResourceRule
    entries: tuple[CatalogEntry, ...]
    missing_patterns: tuple[str, ...]
    count_error: str | None


@dataclass(frozen=True, slots=True)
class ResolvedProfile:
    profile: ImportProfile
    resources: tuple[ResolvedResource, ...]

    @property
    def missing_required(self) -> tuple[str, ...]:
        return tuple(
            item.rule.id
            for item in self.resources
            if item.rule.required
            and (
                not item.entries
                or item.missing_patterns
                or item.count_error is not None
            )
        )

    @property
    def selected_entries(self) -> tuple[CatalogEntry, ...]:
        unique: dict[tuple[str, str], CatalogEntry] = {}
        for resource in self.resources:
            for entry in resource.entries:
                unique[(entry.archive.casefold(), entry.name.casefold())] = entry
        return tuple(
            sorted(
                unique.values(),
                key=lambda item: (item.archive.casefold(), item.name.casefold()),
            )
        )


def resolve_profile(profile: ImportProfile, catalog: InstallCatalog) -> ResolvedProfile:
    resources: list[ResolvedResource] = []
    for rule in profile.resources:
        matches: dict[tuple[str, str], CatalogEntry] = {}
        missing_patterns: list[str] = []
        for pattern in rule.patterns:
            if any(character in pattern for character in "*?["):
                found = catalog.search(pattern)
            else:
                exact = catalog.resolve_exact(pattern)
                found = [exact] if exact else []
            if not found:
                missing_patterns.append(pattern)
            for entry in found:
                matches[(entry.archive.casefold(), entry.name.casefold())] = entry
        ordered = sorted(
            matches.values(),
            key=lambda item: (item.name.casefold(), item.archive.casefold()),
        )
        count_error = None
        if rule.expected_count and len(ordered) != rule.expected_count:
            count_error = (
                f"expected {rule.expected_count} matches, found {len(ordered)}"
            )
        resources.append(
            ResolvedResource(
                rule,
                tuple(ordered[: rule.limit]),
                tuple(missing_patterns),
                count_error,
            )
        )
    return ResolvedProfile(profile, tuple(resources))


def profile_path(value: str, profiles_root: Path) -> Path:
    direct = Path(value).expanduser()
    if direct.is_file():
        return direct.resolve()
    candidate = profiles_root / f"{value}.json"
    if candidate.is_file():
        return candidate.resolve()
    raise FileNotFoundError(f"import profile not found: {value}")
