"""Compile per-projectile ART for every projectile a pack's units resolve to.

The projectile *identity* lane compiles
``registration.gameplay.simulation.resolved.combat.projectileObjectId`` for
every faction.  Until this module existed the identity had nowhere to resolve
its art: the runtime bound arrow visuals through the single literal pack key
``gondorArcherProjectile`` shipped only by the BFME2 Men host pack, so a
Mordor archer borrowed the Good arrow from a foreign pack and a RotWK-only
mount had no arrow art at all.

Everything here is derived from the retail oracle.  The projectile object ids
come from the compiled runtime documents (never a hardcoded list), and each
one's Draw modules are read from the object INI the retail authors wrote them
in, through the same typed visual closure the unit/structure recipes use.
"""

from __future__ import annotations

import hashlib
import json
import re
from collections.abc import Iterable, Mapping, Sequence
from pathlib import Path
from typing import Any

from .retail_visual_closure import build_retail_visual_closure
from .sage_cst import SageBlock, SageObject, parse_sage_document


PROJECTILE_ART_SCHEMA = "openbfme.projectile-art-runtime"
PROJECTILE_ART_SCHEMA_VERSION = 0
PROJECTILE_ART_RUNTIME_PATH = "data/projectile-art.json"
PROJECTILE_ART_PACK_KEY = "projectileArt"
TEXTURE_RESOURCE_ID = "projectile-art-textures"
TEXTURE_OUTPUT_TEMPLATE = "assets/textures/projectiles/{stem}.png"
TEXTURE_OUTPUT_DIRECTORY = "assets/textures/projectiles"

#: Draw module classes whose art this compiler understands.  Anything else on a
#: projectile object is recorded as an unsupported draw rather than dropped.
_STREAK_KIND = "w3dstreakdraw"
_MODEL_KIND_SUFFIX = "modeldraw"

_MAX_PROJECTILE_IDS = 512
_MAX_DOCUMENT_BYTES = 16 * 1024 * 1024
_COLOR = re.compile(
    r"^R\s*:\s*(\d{1,3})\s+G\s*:\s*(\d{1,3})\s+B\s*:\s*(\d{1,3})(?:\s+A\s*:\s*(\d{1,3}))?$",
    re.IGNORECASE,
)
_YES = frozenset({"yes", "true"})
_NO = frozenset({"no", "false"})


class ProjectileArtCompilerError(ValueError):
    """A projectile's retail art cannot be compiled without guessing."""


def collect_projectile_object_ids(runtime_data: Mapping[str, Any]) -> list[str]:
    """Every projectile Object id the composed runtime documents resolve to.

    Data-driven by construction: the ids are read out of the compiled
    documents, so a pack ships art for exactly the projectiles its own units
    and structures fire and nothing else.
    """

    found: dict[str, str] = {}
    for document in runtime_data.values():
        if not isinstance(document, Mapping):
            continue
        registration = document.get("registration")
        if not isinstance(registration, Mapping):
            continue
        gameplay = registration.get("gameplay")
        if not isinstance(gameplay, Mapping):
            continue
        candidates: list[Any] = [gameplay.get("combat")]
        simulation = gameplay.get("simulation")
        if isinstance(simulation, Mapping):
            resolved = simulation.get("resolved")
            if isinstance(resolved, Mapping):
                candidates.append(resolved.get("combat"))
        for combat in candidates:
            if not isinstance(combat, Mapping):
                continue
            value = combat.get("projectileObjectId")
            if isinstance(value, str) and value.strip():
                found.setdefault(value.strip().casefold(), value.strip())
    return [found[key] for key in sorted(found)]


def compile_projectile_art(
    projectile_object_ids: Iterable[str],
    effective_assets_root: Path | str,
    *,
    catalog: object | None = None,
    catalog_identity_sha256: str | None = None,
) -> dict[str, Any]:
    """Compile the retail art binding for each projectile Object id.

    Returns ``{"runtime", "resources", "runtimePath", "packFileKey",
    "summary"}``.  ``runtime`` is the pack document the presentation layer
    resolves art by projectile id from; ``resources`` are the profile resource
    rows that convert the textures it names.
    """

    ids = _validated_ids(projectile_object_ids)
    if not ids:
        raise ProjectileArtCompilerError("no projectile object ids were supplied")
    root = Path(effective_assets_root)
    closure = build_retail_visual_closure(
        root,
        ids,
        catalog=catalog,
        catalog_identity_sha256=catalog_identity_sha256,
    )
    missing = [str(value) for value in closure.get("missingDefinitions", [])]
    if missing:
        raise ProjectileArtCompilerError(
            "retail authors no projectile object named: " + ", ".join(sorted(missing))
        )
    objects = {
        str(row["name"]).casefold(): row
        for row in closure["objects"]
        if isinstance(row, Mapping) and isinstance(row.get("name"), str)
    }
    exact_leaves = [row for row in closure["exactLeaves"] if isinstance(row, Mapping)]
    semantic_leaves = [
        row for row in closure["semanticLeaves"] if isinstance(row, Mapping)
    ]
    sources: dict[str, tuple[SageObject, ...]] = {}
    entries: list[dict[str, Any]] = []
    texture_paths: dict[str, str] = {}
    model_gaps: list[str] = []
    unsupported: list[dict[str, str]] = []
    for projectile_object_id in ids:
        row = objects.get(projectile_object_id.casefold())
        if row is None:
            raise ProjectileArtCompilerError(
                f"visual closure produced no object row for {projectile_object_id}"
            )
        draws: list[dict[str, Any]] = []
        for draw in row.get("drawModules", []):
            if not isinstance(draw, Mapping):
                raise ProjectileArtCompilerError(
                    f"{projectile_object_id} draw module row is invalid"
                )
            kind = str(draw.get("moduleKind", ""))
            folded = kind.casefold()
            if folded == _STREAK_KIND:
                draws.append(
                    _streak_draw(
                        projectile_object_id,
                        draw,
                        root,
                        sources,
                        exact_leaves,
                        texture_paths,
                    )
                )
            elif folded.endswith(_MODEL_KIND_SUFFIX):
                model_draw = _model_draw(
                    projectile_object_id, draw, exact_leaves, semantic_leaves
                )
                draws.append(model_draw)
                if model_draw.get("model") is not None:
                    model_gaps.append(projectile_object_id)
            else:
                unsupported.append(
                    {"projectileObjectId": projectile_object_id, "moduleKind": kind}
                )
        entries.append(
            {
                "projectileObjectId": str(row["name"]),
                "objectKind": str(row.get("objectKind", "Object")),
                "source": _object_source(row),
                "draws": draws,
            }
        )
    runtime: dict[str, Any] = {
        "schema": PROJECTILE_ART_SCHEMA,
        "schemaVersion": PROJECTILE_ART_SCHEMA_VERSION,
        "projectiles": entries,
        # Rock/axe projectiles author a W3D model instead of a streak texture.
        # Their model is recorded above and named here so a consumer can tell
        # "no art shipped" from "art not authored" -- it is never dropped.
        "unconvertedModelProjectileObjectIds": sorted(set(model_gaps)),
        "unsupportedDrawModules": unsupported,
        "sourceClosureSha256": str(closure["aggregateSha256"]),
    }
    runtime["runtimeSha256"] = _canonical_sha256(runtime)
    resources: list[dict[str, Any]] = []
    if texture_paths:
        patterns = sorted(texture_paths)
        resources.append(
            {
                "id": TEXTURE_RESOURCE_ID,
                "kind": "texture",
                "converter": "texture",
                "patterns": patterns,
                "output": TEXTURE_OUTPUT_TEMPLATE,
                "required": True,
                "limit": len(patterns),
                "expected_count": len(patterns),
            }
        )
    return {
        "runtime": runtime,
        "resources": resources,
        "runtimePath": PROJECTILE_ART_RUNTIME_PATH,
        "packFileKey": PROJECTILE_ART_PACK_KEY,
        "summary": {
            "projectileCount": len(entries),
            "streakDrawCount": sum(
                1 for entry in entries for draw in entry["draws"]
                if draw["kind"].casefold() == _STREAK_KIND
            ),
            "textureCount": len(texture_paths),
            "unconvertedModelProjectileCount": len(
                runtime["unconvertedModelProjectileObjectIds"]
            ),
            "unsupportedDrawModuleCount": len(unsupported),
        },
    }


def validate_projectile_art_runtime(document: Mapping[str, Any]) -> None:
    """Fail closed on a projectile-art document this lane did not produce."""

    if (
        document.get("schema") != PROJECTILE_ART_SCHEMA
        or document.get("schemaVersion") != PROJECTILE_ART_SCHEMA_VERSION
    ):
        raise ProjectileArtCompilerError("unsupported projectile art schema")
    entries = document.get("projectiles")
    if not isinstance(entries, list) or not entries:
        raise ProjectileArtCompilerError("projectile art document has no projectiles")
    declared = document.get("runtimeSha256")
    unsigned = {key: value for key, value in document.items() if key != "runtimeSha256"}
    if not isinstance(declared, str) or declared != _canonical_sha256(unsigned):
        raise ProjectileArtCompilerError("projectile art digest is invalid")
    seen: set[str] = set()
    for entry in entries:
        if not isinstance(entry, Mapping):
            raise ProjectileArtCompilerError("projectile art row is invalid")
        identifier = entry.get("projectileObjectId")
        if not isinstance(identifier, str) or not identifier.strip():
            raise ProjectileArtCompilerError("projectile art row has no object id")
        if identifier.casefold() in seen:
            raise ProjectileArtCompilerError(
                f"projectile art row is duplicated: {identifier}"
            )
        seen.add(identifier.casefold())
        if not isinstance(entry.get("draws"), list):
            raise ProjectileArtCompilerError(
                f"projectile art row has no draw list: {identifier}"
            )


def _validated_ids(values: Iterable[str]) -> list[str]:
    unique: dict[str, str] = {}
    for value in values:
        if not isinstance(value, str):
            raise ProjectileArtCompilerError("projectile object id must be a string")
        text = value.strip()
        if not text or len(text) > 128 or any(character.isspace() for character in text):
            raise ProjectileArtCompilerError(
                f"projectile object id is not a bounded token: {value!r}"
            )
        unique.setdefault(text.casefold(), text)
    if len(unique) > _MAX_PROJECTILE_IDS:
        raise ProjectileArtCompilerError("projectile object id count exceeds the limit")
    return [unique[key] for key in sorted(unique)]


def _object_source(row: Mapping[str, Any]) -> dict[str, Any]:
    source = row.get("source")
    if not isinstance(source, Mapping):
        raise ProjectileArtCompilerError(
            f"visual closure object row has no source: {row.get('name')}"
        )
    return {
        "virtualPath": str(source["virtualPath"]),
        "line": int(source["line"]),
    }


def _provenance(draw: Mapping[str, Any], projectile_object_id: str) -> Mapping[str, Any]:
    provenance = draw.get("provenance")
    if not isinstance(provenance, Mapping):
        raise ProjectileArtCompilerError(
            f"{projectile_object_id} draw module has no provenance"
        )
    return provenance


def _draw_source(provenance: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "definingObject": str(provenance["definingObject"]),
        "virtualPath": str(provenance["virtualPath"]),
        "line": int(provenance["line"]),
    }


def _scope_root(provenance: Mapping[str, Any]) -> str:
    scope = provenance.get("scopePath")
    if not isinstance(scope, Sequence) or not scope:
        raise ProjectileArtCompilerError("draw provenance has no scope path")
    return str(scope[0])


def _streak_draw(
    projectile_object_id: str,
    draw: Mapping[str, Any],
    root: Path,
    sources: dict[str, tuple[SageObject, ...]],
    exact_leaves: Sequence[Mapping[str, Any]],
    texture_paths: dict[str, str],
) -> dict[str, Any]:
    provenance = _provenance(draw, projectile_object_id)
    block = _authored_block(root, sources, provenance, "W3DStreakDraw")
    scope = _scope_root(provenance)
    defining = str(provenance["definingObject"])
    # Retail authors W3DStreakDraw partially: the Mirkwood silverthorn streaks
    # omit Additive, and retail's own comment on GoodFactionArrow records the
    # engine default ("Additive = No ; Yes by default"). Record which fields the
    # source actually authored so a default is never mistaken for evidence.
    authored = [
        key
        for key in ("Length", "Width", "NumSegments", "Color", "Additive")
        if block.values(key)
    ]
    row: dict[str, Any] = {
        "kind": "W3DStreakDraw",
        "instanceTag": str(draw.get("instanceTag") or ""),
        "length": _integer(block, "Length", projectile_object_id),
        "width": _integer(block, "Width", projectile_object_id),
        "numSegments": _optional_integer(block, "NumSegments", projectile_object_id, 1),
        "additive": _optional_boolean(block, "Additive", projectile_object_id, True),
        "color": _color(block, projectile_object_id),
        "authoredFields": authored,
        "source": _draw_source(provenance),
    }
    identifiers = block.values("Texture")
    if len(identifiers) != 1:
        raise ProjectileArtCompilerError(
            f"{projectile_object_id} W3DStreakDraw does not author exactly one Texture"
        )
    row["texture"] = _texture_asset(
        projectile_object_id,
        defining,
        scope,
        identifiers[0],
        exact_leaves,
        texture_paths,
    )
    weather: dict[str, str] = {}
    for value in block.values("WeatherTexture"):
        tokens = value.split()
        if len(tokens) != 2:
            raise ProjectileArtCompilerError(
                f"{projectile_object_id} WeatherTexture is not '<condition> <texture>'"
            )
        condition, identifier = tokens[0].upper(), tokens[1]
        if condition in weather:
            raise ProjectileArtCompilerError(
                f"{projectile_object_id} authors {condition} weather texture twice"
            )
        weather[condition] = _texture_asset(
            projectile_object_id,
            defining,
            scope,
            identifier,
            exact_leaves,
            texture_paths,
        )
    row["weatherTextures"] = weather
    return row


def _model_draw(
    projectile_object_id: str,
    draw: Mapping[str, Any],
    exact_leaves: Sequence[Mapping[str, Any]],
    semantic_leaves: Sequence[Mapping[str, Any]],
) -> dict[str, Any]:
    provenance = _provenance(draw, projectile_object_id)
    scope = _scope_root(provenance)
    models = [
        str(leaf["identifier"])
        for leaf in exact_leaves
        if str(leaf.get("usage", "")) == "model"
        and str(leaf.get("targetObject", "")).casefold()
        == projectile_object_id.casefold()
        and _leaf_scope_root(leaf) == scope
    ]
    # `Model = NONE` is an authored decision (the arrow is streak-only), not a
    # missing asset; the closure reports it as a semantic leaf.
    semantic = [
        str(leaf["identifier"])
        for leaf in semantic_leaves
        if str(leaf.get("usage", "")) == "model"
        and str(leaf.get("targetObject", "")).casefold()
        == projectile_object_id.casefold()
        and _leaf_scope_root(leaf) == scope
    ]
    if len(set(model.casefold() for model in models)) > 1:
        raise ProjectileArtCompilerError(
            f"{projectile_object_id} authors multiple models in one draw scope"
        )
    return {
        "kind": str(draw.get("moduleKind", "")),
        "instanceTag": str(draw.get("instanceTag") or ""),
        "model": models[0] if models else None,
        # The W3D->GLB conversion for model-bearing projectiles (rocks, axes)
        # is not part of this lane; the authored id above is the honest record.
        "modelAsset": None,
        "authoredNoModel": bool(semantic) and not models,
        "source": _draw_source(provenance),
    }


def _leaf_scope_root(leaf: Mapping[str, Any]) -> str:
    provenance = leaf.get("provenance")
    if not isinstance(provenance, Mapping):
        return ""
    scope = provenance.get("scopePath")
    if not isinstance(scope, Sequence) or not scope:
        return ""
    return str(scope[0])


def _texture_asset(
    projectile_object_id: str,
    defining_object: str,
    scope: str,
    identifier: str,
    exact_leaves: Sequence[Mapping[str, Any]],
    texture_paths: dict[str, str],
) -> str:
    matches = [
        leaf
        for leaf in exact_leaves
        if str(leaf.get("targetObject", "")).casefold()
        == projectile_object_id.casefold()
        and _leaf_scope_root(leaf) == scope
        and str(leaf.get("identifier", "")).casefold() == identifier.casefold()
    ]
    if len(matches) != 1:
        raise ProjectileArtCompilerError(
            f"{projectile_object_id} texture {identifier!r} did not resolve to one "
            "retail leaf"
        )
    physical = matches[0].get("physicalVirtualPaths")
    if not isinstance(physical, Sequence) or len(physical) != 1:
        raise ProjectileArtCompilerError(
            f"{projectile_object_id} texture {identifier!r} has no single physical path"
        )
    virtual_path = str(physical[0])
    stem = virtual_path.rsplit("/", 1)[-1].rsplit(".", 1)[0].casefold()
    if not stem:
        raise ProjectileArtCompilerError(
            f"{projectile_object_id} texture {identifier!r} has no file stem"
        )
    output = f"{TEXTURE_OUTPUT_DIRECTORY}/{stem}.png"
    existing = texture_paths.get(virtual_path)
    if existing is not None and existing != output:
        raise ProjectileArtCompilerError(
            f"projectile texture {virtual_path!r} has two outputs"
        )
    # Two projectiles legitimately share one retail texture (all three arrow
    # objects author EXArrowStreak01); one conversion, one output.
    texture_paths[virtual_path] = output
    _ = defining_object
    return output


def _authored_block(
    root: Path,
    sources: dict[str, tuple[SageObject, ...]],
    provenance: Mapping[str, Any],
    expected_kind: str,
) -> SageBlock:
    virtual_path = str(provenance["virtualPath"])
    line = int(provenance["line"])
    defining = str(provenance["definingObject"])
    objects = sources.get(virtual_path.casefold())
    if objects is None:
        physical = root.joinpath(*virtual_path.split("/"))
        if not physical.is_file() or physical.stat().st_size > _MAX_DOCUMENT_BYTES:
            raise ProjectileArtCompilerError(
                f"projectile art source is missing or oversized: {virtual_path}"
            )
        objects = parse_sage_document(physical.read_bytes(), virtual_path).objects
        sources[virtual_path.casefold()] = objects
    for candidate in objects:
        if candidate.name.casefold() != defining.casefold():
            continue
        for block in candidate.blocks:
            if block.line == line and block.kind.casefold() == expected_kind.casefold():
                return block
    raise ProjectileArtCompilerError(
        f"{expected_kind} for {defining} is not authored at {virtual_path}:{line}"
    )


def _one_value(block: SageBlock, key: str, projectile_object_id: str) -> str:
    values = block.values(key)
    if len(values) != 1:
        raise ProjectileArtCompilerError(
            f"{projectile_object_id} W3DStreakDraw does not author exactly one {key}"
        )
    return values[0].strip()


def _integer(block: SageBlock, key: str, projectile_object_id: str) -> int:
    value = _one_value(block, key, projectile_object_id)
    try:
        return int(value)
    except ValueError as exc:
        raise ProjectileArtCompilerError(
            f"{projectile_object_id} {key} is not an integer: {value!r}"
        ) from exc


def _optional_integer(
    block: SageBlock, key: str, projectile_object_id: str, default: int
) -> int:
    if not block.values(key):
        return default
    return _integer(block, key, projectile_object_id)


def _optional_boolean(
    block: SageBlock, key: str, projectile_object_id: str, default: bool
) -> bool:
    if not block.values(key):
        return default
    return _boolean(block, key, projectile_object_id)


def _boolean(block: SageBlock, key: str, projectile_object_id: str) -> bool:
    value = _one_value(block, key, projectile_object_id).casefold()
    if value in _YES:
        return True
    if value in _NO:
        return False
    raise ProjectileArtCompilerError(
        f"{projectile_object_id} {key} is not a yes/no flag: {value!r}"
    )


def _color(block: SageBlock, projectile_object_id: str) -> dict[str, int]:
    if not block.values("Color"):
        return {"r": 255, "g": 255, "b": 255}
    value = _one_value(block, "Color", projectile_object_id)
    match = _COLOR.fullmatch(value)
    if match is None:
        raise ProjectileArtCompilerError(
            f"{projectile_object_id} Color is not an R/G/B row: {value!r}"
        )
    channels = {
        "r": int(match.group(1)),
        "g": int(match.group(2)),
        "b": int(match.group(3)),
    }
    if match.group(4) is not None:
        channels["a"] = int(match.group(4))
    for name, channel in channels.items():
        if not 0 <= channel <= 255:
            raise ProjectileArtCompilerError(
                f"{projectile_object_id} Color {name} is out of range"
            )
    return channels


def _canonical_sha256(value: Any) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


__all__ = [
    "PROJECTILE_ART_PACK_KEY",
    "PROJECTILE_ART_RUNTIME_PATH",
    "PROJECTILE_ART_SCHEMA",
    "PROJECTILE_ART_SCHEMA_VERSION",
    "ProjectileArtCompilerError",
    "TEXTURE_OUTPUT_TEMPLATE",
    "TEXTURE_RESOURCE_ID",
    "collect_projectile_object_ids",
    "compile_projectile_art",
    "validate_projectile_art_runtime",
]
