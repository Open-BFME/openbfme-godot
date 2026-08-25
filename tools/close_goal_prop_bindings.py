"""Close cooked map prop bindings in an existing private content pack.

This is intentionally a pack-repair tool, not another map cooker.  It reads the
already-cooked ``objects.json`` documents, plans bindings from the retail
effective-assets tree, reuses byte-identical local content-pack outputs when
available, converts only missing planned outputs, and rewrites only each map's
``object-bindings.json`` and synchronizes the matching ``map.json`` resolution
summary.  Terrain and selection metadata are never touched.

Model bindings fail closed: a planned binding is retained only when its exact
planned GLB is present, passes the runtime's GLB container checks, and declares
at least one scene-reachable mesh instance.  Particle/effect carrier GLBs with
no mesh are valid conversion containers, but they are not renderable bindings.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import sys
import time
import traceback
from types import SimpleNamespace
from typing import Any, Mapping


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.catalog import InstallCatalog  # noqa: E402
from openbfme_importer.map_prop_bindings import (  # noqa: E402
    build_map_prop_binding_plan,
    load_effective_assets_manifest,
    partition_placement_types,
    pseudo_type_classification,
)
from openbfme_importer.pipeline import ImportPipeline  # noqa: E402
from openbfme_importer.sage_map import _object_binding_inventory  # noqa: E402
from openbfme_importer.util import write_json_atomic  # noqa: E402


SCHEMA = "openbfme.goal-prop-binding-closure"
SCHEMA_VERSION = 0
SUPPORTED_OUTPUT_CONVERTERS = frozenset(
    {"texture", "w3d-static", "w3d-hierarchical", "w3d-bundle"}
)
W3D_KINDS = {
    "w3d-static": "static",
    "w3d-hierarchical": "hierarchical",
    "w3d-bundle": "animated",
}
GLB_HEADER = struct.Struct("<4sII")
GLB_CHUNK_HEADER = struct.Struct("<II")
GLB_JSON_CHUNK = 0x4E4F534A
MAX_GLB_JSON_BYTES = 64 * 1024 * 1024
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _canonical_digest(value: Any) -> str:
    payload = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _write_json_retry(path: Path, value: Any) -> None:
    """Tolerate transient Windows sharing violations on report replacement."""

    for attempt in range(20):
        try:
            write_json_atomic(path, value)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.05 * (attempt + 1))


def _safe_output(root: Path, relative: str) -> Path:
    pure = PurePosixPath(relative)
    if (
        pure.is_absolute()
        or not pure.parts
        or any(part in {"", ".", ".."} for part in pure.parts)
    ):
        raise ValueError(f"unsafe pack-relative output: {relative!r}")
    target = root.joinpath(*pure.parts)
    target.resolve().relative_to(root.resolve())
    return target


def _validate_output(path: Path, converter: str) -> tuple[bool, str]:
    try:
        if not path.is_file() or path.is_symlink():
            return False, "missing-or-nonordinary-output"
        size = path.stat().st_size
        if converter in W3D_KINDS:
            if size < 20:
                return False, "glb-too-small"
            with path.open("rb") as handle:
                header = handle.read(GLB_HEADER.size)
            if len(header) != GLB_HEADER.size:
                return False, "glb-truncated-header"
            magic, version, declared_size = GLB_HEADER.unpack(header)
            if magic != b"glTF" or version != 2 or declared_size != size:
                return False, "invalid-glb-container-header"
        elif converter == "texture":
            if size < len(PNG_SIGNATURE):
                return False, "png-too-small"
            with path.open("rb") as handle:
                if handle.read(len(PNG_SIGNATURE)) != PNG_SIGNATURE:
                    return False, "invalid-png-signature"
        else:
            return False, f"unsupported-output-converter:{converter}"
    except OSError as exc:
        return False, f"output-validation-error:{type(exc).__name__}:{exc}"
    return True, "ok"


def _validate_renderable_glb(path: Path) -> tuple[bool, str]:
    """Require a scene-reachable mesh declaration for a renderable binding.

    The ordinary materializer accepts geometry-empty GLBs because some W3D
    support carriers are honestly hierarchy/animation-only.  A map ``bound``
    record has a stronger contract: Godot must be able to instantiate at least
    one mesh-bearing scene node.  Inspect only the standard glTF scene graph
    here; the runtime remains the final loader/mesh authority.
    """

    valid, reason = _validate_output(path, "w3d-static")
    if not valid:
        return False, reason
    try:
        size = path.stat().st_size
        with path.open("rb") as handle:
            header = handle.read(GLB_HEADER.size)
            if len(header) != GLB_HEADER.size:
                return False, "glb-truncated-header"
            _magic, _version, _declared_size = GLB_HEADER.unpack(header)
            chunk_header = handle.read(GLB_CHUNK_HEADER.size)
            if len(chunk_header) != GLB_CHUNK_HEADER.size:
                return False, "glb-truncated-json-chunk-header"
            json_length, chunk_type = GLB_CHUNK_HEADER.unpack(chunk_header)
            if (
                chunk_type != GLB_JSON_CHUNK
                or json_length <= 0
                or json_length > MAX_GLB_JSON_BYTES
                or GLB_HEADER.size + GLB_CHUNK_HEADER.size + json_length > size
            ):
                return False, "invalid-glb-json-chunk"
            raw = handle.read(json_length)
        document = json.loads(raw)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError):
        return False, "invalid-glb-json-document"
    if not isinstance(document, dict):
        return False, "invalid-glb-json-root"
    meshes = document.get("meshes", [])
    nodes = document.get("nodes", [])
    scenes = document.get("scenes", [])
    scene_index = document.get("scene", 0)
    if (
        not isinstance(meshes, list)
        or not isinstance(nodes, list)
        or not isinstance(scenes, list)
        or type(scene_index) is not int
        or scene_index < 0
        or scene_index >= len(scenes)
        or not isinstance(scenes[scene_index], dict)
    ):
        return False, "invalid-glb-scene-graph"
    roots = scenes[scene_index].get("nodes", [])
    if not isinstance(roots, list):
        return False, "invalid-glb-scene-roots"
    pending = list(roots)
    reachable: set[int] = set()
    while pending:
        node_index = pending.pop()
        if (
            type(node_index) is not int
            or node_index < 0
            or node_index >= len(nodes)
            or not isinstance(nodes[node_index], dict)
        ):
            return False, "invalid-glb-scene-node"
        if node_index in reachable:
            continue
        reachable.add(node_index)
        children = nodes[node_index].get("children", [])
        if not isinstance(children, list):
            return False, "invalid-glb-scene-children"
        pending.extend(children)
    mesh_instance_count = 0
    for node_index in reachable:
        mesh_index = nodes[node_index].get("mesh")
        if mesh_index is None:
            continue
        if (
            type(mesh_index) is not int
            or mesh_index < 0
            or mesh_index >= len(meshes)
            or not isinstance(meshes[mesh_index], dict)
        ):
            return False, "invalid-glb-mesh-reference"
        primitives = meshes[mesh_index].get("primitives", [])
        if isinstance(primitives, list) and primitives:
            mesh_instance_count += 1
    if mesh_instance_count <= 0:
        return False, "zero-instantiable-mesh"
    return True, f"ok-mesh-instances={mesh_instance_count}"


def _sync_map_resolution(map_path: Path, summary: Mapping[str, Any]) -> bool:
    """Make map.json repeat the exact cooked binding summary and status."""

    value = json.loads(map_path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"invalid cooked map document: {map_path}")
    if value.get("objectBindings") != "object-bindings.json":
        raise ValueError(f"cooked map has unexpected objectBindings path: {map_path}")
    conversion_status = value.get("conversionStatus")
    if not isinstance(conversion_status, dict):
        raise ValueError(f"cooked map has invalid conversionStatus: {map_path}")
    expected_resolution = dict(summary)
    expected_status = (
        "placements-imported-object-resolution-"
        + str(expected_resolution["resolutionStatus"])
    )
    changed = (
        value.get("objectResolution") != expected_resolution
        or conversion_status.get("objects") != expected_status
    )
    value["objectResolution"] = expected_resolution
    conversion_status["objects"] = expected_status
    value["conversionStatus"] = conversion_status
    if changed:
        _write_json_retry(map_path, value)
    return changed


def _atomic_copy(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + ".prop-closure-copying")
    temporary.unlink(missing_ok=True)
    shutil.copy2(source, temporary)
    os.replace(temporary, target)


def _objects_document(path: Path) -> tuple[list[dict[str, Any]], str]:
    value = json.loads(path.read_text(encoding="utf-8"))
    objects = value.get("objects") if isinstance(value, dict) else None
    if not isinstance(objects, list) or not all(isinstance(row, dict) for row in objects):
        raise ValueError(f"invalid cooked objects document: {path}")
    for index, row in enumerate(objects):
        if "typeName" not in row or "roadType" not in row:
            raise ValueError(f"cooked object {index} lacks typeName/roadType: {path}")
    return objects, _sha256(path)


def _plan_fingerprint(
    maps: list[tuple[str, Path, list[dict[str, Any]], str]],
    effective_assets: Path,
) -> str:
    planner_files = [
        ROOT / "importer" / "openbfme_importer" / "map_prop_bindings.py",
        ROOT / "importer" / "openbfme_importer" / "retail_visual_closure.py",
        ROOT / "importer" / "openbfme_importer" / "retail_visual_profile.py",
        ROOT / "importer" / "openbfme_importer" / "retail_hierarchical_profile.py",
        ROOT / "importer" / "openbfme_importer" / "retail_animated_prop_profile.py",
        ROOT
        / "importer"
        / "openbfme_importer"
        / "playable_structure_pack_compiler.py",
    ]
    manifest = effective_assets / ".openbfme" / "manifest.json"
    return _canonical_digest(
        {
            "maps": [{"slug": slug, "objectsSha256": digest} for slug, _, _, digest in maps],
            "effectiveAssetsManifestSha256": _sha256(manifest),
            "plannerFiles": {
                path.relative_to(ROOT).as_posix(): _sha256(path) for path in planner_files
            },
        }
    )


def _fallback_plan(objects: list[dict[str, Any]], error: str) -> dict[str, Any]:
    parsed = SimpleNamespace(objects=objects)
    _targets, pseudo, unsafe = partition_placement_types(parsed)
    logical = [
        {"typeName": name, "classification": pseudo_type_classification(name)}
        for name in pseudo
    ]
    visual_names = sorted(
        {
            str(row["typeName"])
            for row in objects
            if int(row["roadType"]) == 0 and not str(row["typeName"]).startswith("*")
        },
        key=lambda value: (value.casefold(), value),
    )
    return {
        "objectBindings": {"logical": logical, "models": []},
        "resources": [],
        "evidence": {
            "plannerError": error,
            "unsafeTypeNames": unsafe,
            "unboundTypeNames": visual_names,
            "boundTypeCount": 0,
        },
    }


def _plan_maps(
    maps: list[tuple[str, Path, list[dict[str, Any]], str]],
    effective_assets: Path,
    manifest: Mapping[str, Any],
    cache_root: Path,
    fingerprint: str,
) -> list[dict[str, Any]]:
    cache_root.mkdir(parents=True, exist_ok=True)
    state_path = cache_root / "state.json"
    state: dict[str, Any] = {}
    if state_path.is_file():
        try:
            candidate = json.loads(state_path.read_text(encoding="utf-8"))
            if candidate.get("fingerprint") == fingerprint:
                state = candidate
        except (OSError, ValueError, json.JSONDecodeError):
            state = {}
    next_index = int(state.get("nextIndex", 0)) if state else 0
    owners = {
        str(key): str(value)
        for key, value in dict(state.get("textureOwners", {})).items()
    }
    plans: list[dict[str, Any]] = []
    for index in range(next_index):
        slug = maps[index][0]
        path = cache_root / f"{index:03d}-{slug}.json"
        if not path.is_file():
            next_index = 0
            owners = {}
            plans = []
            break
        plans.append(json.loads(path.read_text(encoding="utf-8")))

    for index, (slug, _objects_path, objects, _digest) in enumerate(maps):
        if index < next_index:
            print(f"PLAN [{index + 1}/{len(maps)}] {slug} cache-hit", flush=True)
            continue
        started = __import__("time").monotonic()
        candidate_owners = dict(owners)
        try:
            plan = build_map_prop_binding_plan(
                SimpleNamespace(objects=objects),
                effective_assets_root=effective_assets,
                effective_assets_manifest=manifest,
                map_pattern=f"maps/{slug}/map.map",
                output_root=f"maps/{slug}",
                texture_owners=candidate_owners,
            )
            owners = candidate_owners
            status = "ok"
        except Exception as exc:  # fail the affected map closed, keep the corpus moving
            detail = f"{type(exc).__name__}: {exc}"
            plan = _fallback_plan(objects, detail[:1000])
            plan["plannerTracebackTail"] = traceback.format_exc()[-4000:]
            status = f"fail-closed:{detail[:240]}"
        plans.append(plan)
        _write_json_retry(cache_root / f"{index:03d}-{slug}.json", plan)
        _write_json_retry(
            state_path,
            {
                "fingerprint": fingerprint,
                "nextIndex": index + 1,
                "textureOwners": owners,
            },
        )
        evidence = plan.get("evidence", {})
        elapsed = __import__("time").monotonic() - started
        print(
            f"PLAN [{index + 1}/{len(maps)}] {slug} {status} "
            f"models={len(plan['objectBindings'].get('models', []))} "
            f"logical={len(plan['objectBindings'].get('logical', []))} "
            f"unbound={len(evidence.get('unboundTypeNames', []))} "
            f"resources={len(plan.get('resources', []))} seconds={elapsed:.1f}",
            flush=True,
        )
    return plans


def _collect_resources(plans: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    resources: dict[str, dict[str, Any]] = {}
    output_owner: dict[str, str] = {}
    for plan in plans:
        for raw in plan.get("resources", []):
            resource = dict(raw)
            resource_id = str(resource.get("id", ""))
            if not resource_id:
                raise ValueError("planned resource is missing id")
            prior = resources.get(resource_id)
            if prior is not None and _canonical_digest(prior) != _canonical_digest(resource):
                # Animated planning may move a shared hierarchy/animation closure
                # into a hash-only input owner when a later map has multiple skins
                # using it. Static planning can likewise discover an additional
                # exact embedded texture through another Object definition that
                # uses the same W3D. The physical output identity is unchanged;
                # canonicalize the corpus resource to the union of every proven
                # retail source/input instead of picking a map-local variant.
                prior_base = dict(prior)
                resource_base = dict(resource)
                prior_patterns = list(prior_base.pop("patterns", []))
                resource_patterns = list(resource_base.pop("patterns", []))
                prior_base.pop("limit", None)
                resource_base.pop("limit", None)
                prior_base.pop("expected_count", None)
                resource_base.pop("expected_count", None)
                prior_options = dict(prior_base.get("options", {}))
                resource_options = dict(resource_base.get("options", {}))
                prior_inputs = list(prior_options.pop("inputResourceIds", []))
                resource_inputs = list(resource_options.pop("inputResourceIds", []))
                prior_base["options"] = prior_options
                resource_base["options"] = resource_options
                if (
                    str(resource.get("converter", "")) not in W3D_KINDS
                    or _canonical_digest(prior_base) != _canonical_digest(resource_base)
                ):
                    raise ValueError(
                        f"conflicting duplicate planned resource: {resource_id}"
                    )
                merged_patterns = sorted(
                    {*prior_patterns, *resource_patterns},
                    key=lambda value: (str(value).casefold(), str(value)),
                )
                merged_inputs = sorted(
                    {*prior_inputs, *resource_inputs},
                    key=lambda value: (str(value).casefold(), str(value)),
                )
                merged = dict(prior)
                merged["patterns"] = merged_patterns
                merged["limit"] = len(merged_patterns)
                merged["expected_count"] = len(merged_patterns)
                merged_options = dict(merged.get("options", {}))
                merged_options["inputResourceIds"] = merged_inputs
                merged["options"] = merged_options
                resources[resource_id] = merged
                resource = merged
            else:
                resources.setdefault(resource_id, resource)
            output = resource.get("output")
            if isinstance(output, str) and output:
                folded = output.casefold()
                prior_id = output_owner.get(folded)
                if prior_id is not None and prior_id != resource_id:
                    prior_resource = resources[prior_id]
                    equivalent = dict(prior_resource)
                    equivalent["id"] = resource_id
                    if _canonical_digest(equivalent) != _canonical_digest(resource):
                        raise ValueError(
                            f"conflicting planned output {output}: {prior_id}, {resource_id}"
                        )
                output_owner.setdefault(folded, resource_id)
    return resources


def _assets_directories(content_packs_root: Path) -> list[Path]:
    return sorted(
        (path for path in content_packs_root.rglob("assets") if path.is_dir()),
        key=lambda path: str(path).casefold(),
    )


def _reuse_candidate(
    output: str,
    converter: str,
    target: Path,
    assets_directories: list[Path],
    preferred_pack: Path | None,
) -> tuple[Path | None, str]:
    pure = PurePosixPath(output)
    if not pure.parts or pure.parts[0].casefold() != "assets":
        return None, "output-not-under-assets"
    tail = pure.parts[1:]
    candidates: list[Path] = []
    for assets in assets_directories:
        candidate = assets.joinpath(*tail)
        if candidate.resolve() == target.resolve():
            continue
        ok, _reason = _validate_output(candidate, converter)
        if ok:
            candidates.append(candidate)
    if not candidates:
        return None, "no-local-output-at-planned-path"
    if preferred_pack is not None:
        preferred_root = preferred_pack.resolve()
        candidates.sort(
            key=lambda path: (
                0 if path.resolve().is_relative_to(preferred_root) else 1,
                str(path).casefold(),
            )
        )
    else:
        candidates.sort(key=lambda path: str(path).casefold())
    chosen = candidates[0]
    chosen_hash = _sha256(chosen)
    identical_count = sum(_sha256(path) == chosen_hash for path in candidates)
    return chosen, f"same-path-candidates={len(candidates)} identical={identical_count}"


def _retail_source(effective_assets: Path, virtual: str) -> Path | None:
    pure = PurePosixPath(virtual)
    if pure.is_absolute() or any(part in {"", ".", ".."} for part in pure.parts):
        return None
    source = effective_assets.joinpath(*pure.parts)
    try:
        source.resolve().relative_to(effective_assets.resolve())
    except ValueError:
        return None
    return source if source.is_file() else None


def _source_closure(
    resource: Mapping[str, Any],
    resources: Mapping[str, Mapping[str, Any]],
    effective_assets: Path,
) -> tuple[list[Path], list[str]]:
    selected: list[Mapping[str, Any]] = [resource]
    options = resource.get("options", {})
    dependency_ids = options.get("inputResourceIds", []) if isinstance(options, dict) else []
    missing: list[str] = []
    for dependency_id_value in dependency_ids:
        dependency_id = str(dependency_id_value)
        dependency = resources.get(dependency_id)
        if dependency is None:
            missing.append(f"missing-planned-dependency:{dependency_id}")
        else:
            selected.append(dependency)
    sources: dict[str, Path] = {}
    for selected_resource in selected:
        patterns = selected_resource.get("patterns", [])
        if not isinstance(patterns, list):
            missing.append(f"invalid-patterns:{selected_resource.get('id')}")
            continue
        for pattern_value in patterns:
            pattern = str(pattern_value)
            source = _retail_source(effective_assets, pattern)
            if source is None:
                missing.append(f"retail-absent-source:{pattern}")
            else:
                sources[str(source).casefold()] = source
    return sorted(sources.values(), key=lambda path: str(path).casefold()), missing


def _materialize(
    resources: dict[str, dict[str, Any]],
    pack: Path,
    effective_assets: Path,
    content_packs_root: Path,
    preferred_pack: Path | None,
    state_root: Path,
    jobs: int,
    materialization_state_path: Path,
) -> tuple[dict[str, dict[str, Any]], dict[str, Any]]:
    assets_directories = _assets_directories(content_packs_root)
    state: dict[str, Any] = {"resources": {}}
    plan_digest = _canonical_digest(resources)
    if materialization_state_path.is_file():
        try:
            loaded = json.loads(materialization_state_path.read_text(encoding="utf-8"))
            if loaded.get("planDigest") == plan_digest:
                state = loaded
        except (OSError, ValueError, json.JSONDecodeError):
            pass
    state["planDigest"] = plan_digest
    state.setdefault("resources", {})
    results: dict[str, dict[str, Any]] = {}
    pending: list[tuple[str, dict[str, Any], Path]] = []

    output_resources = [
        (resource_id, resource)
        for resource_id, resource in sorted(resources.items())
        if isinstance(resource.get("output"), str) and resource.get("output")
    ]
    for position, (resource_id, resource) in enumerate(output_resources, start=1):
        converter = str(resource.get("converter", ""))
        output = str(resource["output"])
        target = _safe_output(pack, output)
        prior = dict(state["resources"].get(resource_id, {}))
        ok, validation = _validate_output(target, converter)
        if ok and prior.get("output") == output and prior.get("sha256") == _sha256(target):
            result = prior
            result["status"] = str(prior.get("status", "converted"))
            results[resource_id] = result
            print(
                f"MATERIALIZE [{position}/{len(output_resources)}] {resource_id} "
                f"resume-{result['status']}",
                flush=True,
            )
            continue

        candidate, reuse_detail = _reuse_candidate(
            output, converter, target, assets_directories, preferred_pack
        )
        if candidate is not None:
            _atomic_copy(candidate, target)
            ok, validation = _validate_output(target, converter)
            if ok:
                result = {
                    "status": "copied",
                    "output": output,
                    "converter": converter,
                    "source": str(candidate),
                    "sha256": _sha256(target),
                    "detail": reuse_detail,
                }
                results[resource_id] = result
                state["resources"][resource_id] = result
                _write_json_retry(materialization_state_path, state)
                print(
                    f"MATERIALIZE [{position}/{len(output_resources)}] "
                    f"{resource_id} copied",
                    flush=True,
                )
                continue
        if converter not in SUPPORTED_OUTPUT_CONVERTERS:
            result = {
                "status": "failed",
                "output": output,
                "converter": converter,
                "reason": f"unsupported-output-converter:{converter}",
            }
            results[resource_id] = result
            state["resources"][resource_id] = result
            _write_json_retry(materialization_state_path, state)
            continue
        pending.append((resource_id, resource, target))

    catalog = InstallCatalog.load(state_root / "catalog" / "rotwk.json")
    pipeline = ImportPipeline(
        catalog,
        state_root,
        game="rotwk",
        conversion_jobs=jobs,
    )

    texture_pending = [row for row in pending if row[1].get("converter") == "texture"]

    def convert_texture(row: tuple[str, dict[str, Any], Path]) -> tuple[str, dict[str, Any]]:
        resource_id, resource, target = row
        patterns = resource.get("patterns", [])
        if not isinstance(patterns, list) or len(patterns) != 1:
            return resource_id, {
                "status": "failed",
                "output": str(resource.get("output", "")),
                "converter": "texture",
                "reason": "texture-resource-requires-exactly-one-source",
            }
        source = _retail_source(effective_assets, str(patterns[0]))
        if source is None:
            return resource_id, {
                "status": "failed",
                "output": str(resource["output"]),
                "converter": "texture",
                "reason": f"retail-absent-source:{patterns[0]}",
            }
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            pipeline._convert_cached_media(
                source,
                "texture",
                dict(resource.get("options", {})),
                target,
                relative_output=str(resource["output"]),
                source_sha256=_sha256(source),
            )
            valid, reason = _validate_output(target, "texture")
            if not valid:
                raise RuntimeError(reason)
            return resource_id, {
                "status": "converted",
                "output": str(resource["output"]),
                "converter": "texture",
                "source": str(patterns[0]),
                "sha256": _sha256(target),
            }
        except Exception as exc:
            return resource_id, {
                "status": "failed",
                "output": str(resource["output"]),
                "converter": "texture",
                "reason": f"conversion-failed:{type(exc).__name__}:{exc}"[:1000],
            }

    if texture_pending:
        print(f"CONVERT textures={len(texture_pending)} workers={min(jobs, len(texture_pending))}", flush=True)
        with ThreadPoolExecutor(max_workers=min(jobs, len(texture_pending))) as pool:
            futures = {pool.submit(convert_texture, row): row[0] for row in texture_pending}
            completed = 0
            for future in as_completed(futures):
                resource_id, result = future.result()
                results[resource_id] = result
                state["resources"][resource_id] = result
                completed += 1
                if completed % 25 == 0 or completed == len(texture_pending):
                    print(f"CONVERT textures-done={completed}/{len(texture_pending)}", flush=True)
                    _write_json_retry(materialization_state_path, state)

    model_pending = [row for row in pending if row[1].get("converter") in W3D_KINDS]
    model_jobs: list[tuple[int, list[Path], str | None, dict[str, Any], Path, str, str, str]] = []
    model_index_to_id: dict[int, str] = {}
    for resource_id, resource, _target in model_pending:
        sources, missing = _source_closure(resource, resources, effective_assets)
        if missing:
            result = {
                "status": "failed",
                "output": str(resource["output"]),
                "converter": str(resource["converter"]),
                "reason": "; ".join(sorted(set(missing)))[:1000],
            }
            results[resource_id] = result
            state["resources"][resource_id] = result
            continue
        index = len(model_jobs)
        model_index_to_id[index] = resource_id
        model_jobs.append(
            (
                index,
                sources,
                str(resource["output"]),
                dict(resource.get("options", {})),
                pack,
                "goal-prop-binding-closure",
                resource_id,
                W3D_KINDS[str(resource["converter"])],
            )
        )
    _write_json_retry(materialization_state_path, state)
    if model_jobs:
        print(f"CONVERT models={len(model_jobs)} workers={jobs}", flush=True)
        try:
            outputs, errors = pipeline._convert_w3d_resources(model_jobs)
        except Exception as exc:
            outputs = {}
            errors = {index: exc for index in model_index_to_id}
        for index, resource_id in model_index_to_id.items():
            resource = resources[resource_id]
            target = _safe_output(pack, str(resource["output"]))
            if index in outputs:
                valid, reason = _validate_output(target, str(resource["converter"]))
                if valid:
                    result = {
                        "status": "converted",
                        "output": str(resource["output"]),
                        "converter": str(resource["converter"]),
                        "sha256": _sha256(target),
                    }
                else:
                    result = {
                        "status": "failed",
                        "output": str(resource["output"]),
                        "converter": str(resource["converter"]),
                        "reason": f"converted-output-invalid:{reason}",
                    }
            else:
                exc = errors.get(index, RuntimeError("converter-returned-no-result"))
                result = {
                    "status": "failed",
                    "output": str(resource["output"]),
                    "converter": str(resource["converter"]),
                    "reason": f"conversion-failed:{type(exc).__name__}:{exc}"[:1000],
                }
            results[resource_id] = result
            state["resources"][resource_id] = result
            print(f"CONVERT model {resource_id} {result['status']}", flush=True)
            _write_json_retry(materialization_state_path, state)

    missing_results = [resource_id for resource_id, _ in output_resources if resource_id not in results]
    if missing_results:
        raise RuntimeError("materializer omitted resources: " + ", ".join(missing_results))
    counts = Counter(str(result["status"]) for result in results.values())
    failures = [
        {"id": resource_id, **result}
        for resource_id, result in sorted(results.items())
        if result.get("status") == "failed"
    ]
    summary = {
        "declared": len(resources),
        "supportInputOnly": sum(not resource.get("output") for resource in resources.values()),
        "planned": len(output_resources),
        "copied": counts["copied"],
        "converted": counts["converted"],
        "failed": counts["failed"],
        "failures": failures,
        "byConverter": dict(
            sorted(Counter(str(resource.get("converter", "")) for _, resource in output_resources).items())
        ),
    }
    return results, summary


def _unbound_reasons(plan: Mapping[str, Any]) -> dict[str, list[str]]:
    evidence = plan.get("evidence", {})
    reasons: dict[str, list[str]] = defaultdict(list)
    planner_error = evidence.get("plannerError") if isinstance(evidence, dict) else None
    if planner_error:
        for name in evidence.get("unboundTypeNames", []):
            reasons[str(name)].append(f"planner-error:{planner_error}")
    stages = evidence.get("stages", {}) if isinstance(evidence, dict) else {}
    lifecycle = stages.get("lifecycle-visual", {}) if isinstance(stages, dict) else {}
    for row in lifecycle.get("rejections", []) if isinstance(lifecycle, dict) else []:
        if isinstance(row, dict) and row.get("typeName"):
            reasons[str(row["typeName"])].append(str(row.get("reason", "planner-unproven")))
    for name in evidence.get("unsafeTypeNames", []) if isinstance(evidence, dict) else []:
        reasons[str(name)].append("unsafe-type-name")
    for name in evidence.get("unboundTypeNames", []) if isinstance(evidence, dict) else []:
        reasons.setdefault(str(name), ["planner-unproven-retail-visual"])
    return dict(reasons)


def _write_inventories_and_stats(
    maps: list[tuple[str, Path, list[dict[str, Any]], str]],
    plans: list[dict[str, Any]],
    resources: dict[str, dict[str, Any]],
    results: dict[str, dict[str, Any]],
    pack: Path,
    effective_assets: Path,
    resource_summary: dict[str, Any],
    fingerprint: str,
    top_n: int,
) -> dict[str, Any]:
    output_to_result = {
        str(resources[resource_id].get("output", "")).casefold(): result
        for resource_id, result in results.items()
    }
    map_rows: list[dict[str, Any]] = []
    status_types: Counter[str] = Counter()
    status_placements: Counter[str] = Counter()
    unbound_placements: Counter[str] = Counter()
    unbound_maps: Counter[str] = Counter()
    unbound_reason_sets: dict[str, set[str]] = defaultdict(set)
    renderable_validation_cache: dict[str, tuple[bool, str]] = {}
    empty_mesh_type_names: set[str] = set()
    empty_mesh_maps: set[str] = set()
    empty_mesh_type_rows = 0
    empty_mesh_placements = 0
    synchronized_map_documents = 0

    for (slug, objects_path, objects, objects_digest), plan in zip(maps, plans, strict=True):
        bindings = dict(plan.get("objectBindings", {}))
        logical = list(bindings.get("logical", []))
        planned_models = list(bindings.get("models", []))
        retained_models: list[dict[str, Any]] = []
        resource_failed_types: dict[str, str] = {}
        map_empty_mesh_type_rows = 0
        map_empty_mesh_placements = 0
        for row in planned_models:
            glb = str(row.get("glb", ""))
            result = output_to_result.get(glb.casefold())
            target = _safe_output(pack, glb)
            validation_key = str(target.resolve()).casefold()
            cached_validation = renderable_validation_cache.get(validation_key)
            if cached_validation is None:
                cached_validation = _validate_renderable_glb(target)
                renderable_validation_cache[validation_key] = cached_validation
            valid, validation = cached_validation
            if result is not None and result.get("status") in {"copied", "converted"} and valid:
                retained_models.append(dict(row))
            else:
                reason = (
                    str(result.get("reason", result.get("status", "resource-failed")))
                    if result is not None
                    else "planned-glb-resource-missing"
                )
                if not valid:
                    reason = f"{reason}; output-invalid:{validation}"
                type_name = str(row.get("typeName", ""))
                resource_failed_types[type_name] = reason
                if validation == "zero-instantiable-mesh":
                    placement_count = sum(
                        1
                        for item in objects
                        if int(item["roadType"]) == 0
                        and str(item["typeName"]) == type_name
                    )
                    empty_mesh_type_names.add(type_name)
                    empty_mesh_maps.add(slug)
                    empty_mesh_type_rows += 1
                    empty_mesh_placements += placement_count
                    map_empty_mesh_type_rows += 1
                    map_empty_mesh_placements += placement_count
        object_bindings = {"logical": logical, "models": retained_models}
        nonroad = [row for row in objects if int(row["roadType"]) == 0]
        inventory = _object_binding_inventory(nonroad, object_bindings)
        binding_path = objects_path.with_name("object-bindings.json")
        _write_json_retry(binding_path, inventory)

        summary = dict(inventory["summary"])
        if _sync_map_resolution(objects_path.with_name("map.json"), summary):
            synchronized_map_documents += 1
        for status in ("bound", "logical", "unresolved"):
            status_types[status] += int(summary[f"{status}TypeCount"])
            status_placements[status] += int(summary[f"{status}PlacementCount"])
        planner_reasons = _unbound_reasons(plan)
        for record in inventory["records"]:
            if record.get("status") != "unresolved":
                continue
            name = str(record["typeName"])
            count = int(record["placementCount"])
            unbound_placements[name] += count
            unbound_maps[name] += 1
            reasons = list(planner_reasons.get(name, []))
            if name in resource_failed_types:
                reasons.append(f"resource-failed:{resource_failed_types[name]}")
            if not reasons:
                reasons.append("planner-unproven-retail-visual")
            unbound_reason_sets[name].update(reasons)
        map_rows.append(
            {
                "slug": slug,
                "objects": objects_path.relative_to(pack).as_posix(),
                "objectsSha256": objects_digest,
                "placementCount": int(summary["placementCount"]),
                "boundPlacementCount": int(summary["boundPlacementCount"]),
                "boundTypeCount": int(summary["boundTypeCount"]),
                "logicalPlacementCount": int(summary["logicalPlacementCount"]),
                "logicalTypeCount": int(summary["logicalTypeCount"]),
                "unresolvedPlacementCount": int(summary["unresolvedPlacementCount"]),
                "unresolvedTypeCount": int(summary["unresolvedTypeCount"]),
                "plannedModelTypeCount": len(planned_models),
                "retainedModelTypeCount": len(retained_models),
                "emptyMeshDemotedTypeCount": map_empty_mesh_type_rows,
                "emptyMeshDemotedPlacementCount": map_empty_mesh_placements,
                **(
                    {"plannerError": str(plan["evidence"]["plannerError"])}
                    if isinstance(plan.get("evidence"), dict)
                    and plan["evidence"].get("plannerError")
                    else {}
                ),
            }
        )
        print(
            f"INVENTORY {slug} bound={summary['boundPlacementCount']} "
            f"logical={summary['logicalPlacementCount']} "
            f"unresolved={summary['unresolvedPlacementCount']}",
            flush=True,
        )

    top = [
        {
            "typeName": name,
            "placementCount": count,
            "mapCount": unbound_maps[name],
            "reasons": sorted(unbound_reason_sets[name]),
        }
        for name, count in unbound_placements.most_common(top_n)
    ]
    total_placements = sum(status_placements.values())
    return {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "pack": str(pack),
        "effectiveAssets": str(effective_assets),
        "planFingerprint": fingerprint,
        "mapCount": len(map_rows),
        "maps": map_rows,
        "totalPlacements": total_placements,
        "bound": {
            "placements": status_placements["bound"],
            "types": status_types["bound"],
        },
        "logical": {
            "placements": status_placements["logical"],
            "types": status_types["logical"],
        },
        "unresolved": {
            "placements": status_placements["unresolved"],
            "types": status_types["unresolved"],
        },
        "falseBoundDemotions": {
            "reason": "zero-instantiable-mesh",
            "placements": empty_mesh_placements,
            "mapTypeRows": empty_mesh_type_rows,
            "mapCount": len(empty_mesh_maps),
            "typeCount": len(empty_mesh_type_names),
            "typeNames": sorted(empty_mesh_type_names),
            "maps": sorted(empty_mesh_maps),
        },
        "synchronizedMapDocumentCount": synchronized_map_documents,
        "unboundTypeNamesTopN": top,
        "resources": resource_summary,
        "policy": {
            "substitutesAllowed": False,
            "logicalPseudoTypesRemainNonRenderable": True,
            "failedModelResourcesRemainUnresolved": True,
            "emptyMeshBindingsRemainUnresolved": True,
            "mapResolutionMirrorsBindingSummary": True,
            "terrainTouched": False,
            "selectionMetadataTouched": False,
        },
    }


def _write_summary(path: Path, stats: Mapping[str, Any]) -> None:
    resources = stats["resources"]
    demotions = stats["falseBoundDemotions"]
    lines = [
        "# SOL prop-binding closure",
        "",
        f"- Cooked map directories processed: {stats['mapCount']}",
        f"- Total non-road placements: {stats['totalPlacements']}",
        f"- Bound: {stats['bound']['placements']} placements / {stats['bound']['types']} map-type rows",
        f"- Logical: {stats['logical']['placements']} placements / {stats['logical']['types']} map-type rows",
        f"- Unresolved: {stats['unresolved']['placements']} placements / {stats['unresolved']['types']} map-type rows",
        f"- Empty-mesh claims demoted: {demotions['placements']} placements / {demotions['mapTypeRows']} map-type rows / {demotions['mapCount']} maps",
        f"- map.json resolution summaries synchronized: {stats['synchronizedMapDocumentCount']} changed documents",
        f"- Resources: {resources['planned']} planned, {resources['copied']} copied, {resources['converted']} converted, {resources['failed']} failed",
        "- Art policy: exact retail-backed planned outputs only; no substitutes or invented meshes.",
        "- Mutation boundary: object-bindings.json, map.json resolution/status fields, plus planned assets/reports only; terrain and selection metadata untouched.",
        "",
        "Verification gates are recorded after the closure run.",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    # NOTE (Q88, 2026-08-25): the default --pack and --preferred-reuse-pack
    # bundles were deleted by the content-pack prune; this one-shot repair was
    # already applied before that. To re-run, pass --pack/--preferred-reuse-pack
    # explicitly against a live bundle.
    parser.add_argument(
        "--pack",
        type=Path,
        default=ROOT
        / "workspace"
        / "content-packs"
        / "rotwk-skirmish-maps-private"
        / "goal-official-72",
    )
    parser.add_argument(
        "--effective-assets",
        type=Path,
        default=ROOT
        / "workspace"
        / "retail-work"
        / "editions"
        / "rotwk"
        / "cache"
        / "effective-assets",
    )
    parser.add_argument(
        "--content-packs-root",
        type=Path,
        default=ROOT / "workspace" / "content-packs",
    )
    parser.add_argument(
        "--preferred-reuse-pack",
        type=Path,
        default=ROOT
        / "workspace"
        / "content-packs"
        / "rotwk-skirmish-maps-private"
        / "bc6ab08983077de7d58bb99c620a75a7abb7f5eb55dcd13426cc64c156f8c039",
    )
    parser.add_argument(
        "--state-root",
        type=Path,
        default=ROOT / "workspace" / "retail-work",
    )
    parser.add_argument("--stats", required=True, type=Path)
    parser.add_argument("--summary", required=True, type=Path)
    parser.add_argument("--cache-root", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=max(1, min(12, (os.cpu_count() or 2) - 2)))
    parser.add_argument("--top-n", type=int, default=50)
    args = parser.parse_args(argv)

    if args.jobs < 1:
        raise SystemExit("--jobs must be at least 1")
    pack = args.pack.expanduser().resolve()
    effective_assets = args.effective_assets.expanduser().resolve()
    content_packs_root = args.content_packs_root.expanduser().resolve()
    preferred_pack = args.preferred_reuse_pack.expanduser().resolve()
    state_root = args.state_root.expanduser().resolve()
    stats_path = args.stats.expanduser().resolve()
    summary_path = args.summary.expanduser().resolve()
    cache_root = args.cache_root.expanduser().resolve()
    if not (pack / "pack.json").is_file():
        raise SystemExit(f"pack.json is missing: {pack}")
    if not effective_assets.is_dir():
        raise SystemExit(f"effective-assets root is missing: {effective_assets}")
    os.environ["OPENBFME_IMPORT_ROOT"] = str(state_root)

    maps: list[tuple[str, Path, list[dict[str, Any]], str]] = []
    for objects_path in sorted(
        (pack / "maps").glob("*/objects.json"),
        key=lambda path: path.parent.name.casefold(),
    ):
        objects, digest = _objects_document(objects_path)
        maps.append((objects_path.parent.name, objects_path, objects, digest))
    if not maps:
        raise SystemExit(f"no cooked map objects found under {pack / 'maps'}")

    manifest = load_effective_assets_manifest(effective_assets)
    fingerprint = _plan_fingerprint(maps, effective_assets)
    plans = _plan_maps(
        maps,
        effective_assets,
        manifest,
        cache_root / "plans",
        fingerprint,
    )
    resources = _collect_resources(plans)
    results, resource_summary = _materialize(
        resources,
        pack,
        effective_assets,
        content_packs_root,
        preferred_pack if preferred_pack.is_dir() else None,
        state_root,
        args.jobs,
        cache_root / "materialization.json",
    )
    stats = _write_inventories_and_stats(
        maps,
        plans,
        resources,
        results,
        pack,
        effective_assets,
        resource_summary,
        fingerprint,
        args.top_n,
    )
    _write_json_retry(stats_path, stats)
    _write_summary(summary_path, stats)
    print(f"STATS {stats_path}")
    print(f"SUMMARY {summary_path}")
    print(
        "PROP_BINDING_CLOSURE_RESULT "
        f"maps={stats['mapCount']} bound={stats['bound']['placements']} "
        f"logical={stats['logical']['placements']} "
        f"unresolved={stats['unresolved']['placements']} "
        f"resources={resource_summary['planned']} "
        f"copied={resource_summary['copied']} "
        f"converted={resource_summary['converted']} "
        f"failed={resource_summary['failed']}",
        flush=True,
    )
    return 0 if int(stats["bound"]["placements"]) > 0 else 5


if __name__ == "__main__":
    raise SystemExit(main())
