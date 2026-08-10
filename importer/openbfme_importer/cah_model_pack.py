"""Create-a-Hero mesh lane: compiled CaH system -> pack model resources.

The Create-a-Hero system table (``cah_system_compiler``) names, per subclass,
the exact retail meshes the client must show: a battlefield skin, a
creation-screen skin, optional conditional restatements, and a mounted skin --
each paired with the SAGE hierarchy (``Skeleton``) it binds to.  Nothing else
in the importer converts those meshes, because they hang off ``CreateAHero``
rather than off a playable-unit Object, and ``CreateAHero``'s INI closure
cannot be walked by the shared visual-closure lane (retail's own
``createaheroanims.inc`` carries a stray ``End`` that fails CST parsing).

This module closes that gap without inventing a second conversion pipeline:
it derives the mesh list **from the compiled descriptor** and emits ordinary
profile resources for the already-proven ``w3d-hierarchical`` converter.

Documented pack path scheme
---------------------------

``assets/models/cah/<MODEL_ID>.glb``
    One GLB per distinct ``model`` id the compiled system selects, spelled
    exactly as the descriptor spells it (retail's uppercase, e.g.
    ``CHHW_CG_C_SKN.glb``).  This is a published contract with the runtime.

Skeletons (``*_SKL``) are **staged inputs**, not outputs: the converter binds
the hierarchy into the skin's GLB, which is the same shape every reskin unit
model already ships (see ``playable_unit_pack_compiler`` and its
``unit-<slug>-visual-01`` hierarchical resources).

Every id the retail install cannot answer is named and refused (rulebook P7 /
no silent fallbacks): a missing mesh must not become a pack that silently has
no hero art.
"""

from __future__ import annotations

from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import PurePosixPath

#: Published pack directory for Create-a-Hero meshes (runtime contract).
CAH_MODEL_PACK_ROOT = "assets/models/cah"
#: Resource id prefixes.  Both stay inside profile.SLUG_PATTERN.
CAH_MODEL_RESOURCE_PREFIX = "cah-model"
CAH_TEXTURE_RESOURCE_SUFFIX = "textures"

#: Texture-bearing shader material properties, mirroring the retail visual
#: closure lane's own set (``retail_visual_closure._SHADER_TEXTURE_PROPERTY``).
_SHADER_TEXTURE_PROPERTIES = frozenset(
    {
        "DiffuseTexture",
        "NormalMap",
        "SpecMap",
        "DamagedTexture",
        "Texture_0",
        "Texture_1",
    }
)


class CahModelPackError(ValueError):
    """The Create-a-Hero mesh lane cannot ship the referenced meshes."""


@dataclass(frozen=True, slots=True)
class CahModelBinding:
    """One mesh the compiled system selects, with the hierarchy it binds to."""

    model_id: str
    skeleton_id: str
    #: Where in the descriptor this binding was authored (diagnostics only).
    owner: str


@dataclass(frozen=True)
class CahModelPack:
    """Profile resources plus a receipt for the meshes this lane ships."""

    resources: tuple[dict[str, object], ...]
    bindings: tuple[CahModelBinding, ...]
    receipt: dict[str, object]


def _registration(value: Mapping[str, object]) -> Mapping[str, object]:
    """Accept either the runtime envelope or a bare descriptor body."""

    if not isinstance(value, Mapping):
        raise CahModelPackError("CaH system document root is not an object")
    registration = value.get("registration", value)
    if not isinstance(registration, Mapping):
        raise CahModelPackError("CaH system registration is not an object")
    return registration


def _visit_model_node(
    node: object, owner: str, out: list[CahModelBinding]
) -> None:
    if not isinstance(node, Mapping):
        return
    model = node.get("model")
    skeleton = node.get("skeleton")
    if isinstance(model, str) and model.strip():
        if not isinstance(skeleton, str) or not skeleton.strip():
            raise CahModelPackError(
                f"CaH model {model!r} ({owner}) names no skeleton; "
                "a skinned mesh cannot be staged without its hierarchy"
            )
        out.append(CahModelBinding(model.strip(), skeleton.strip(), owner))
    states = node.get("conditionalStates")
    if isinstance(states, Sequence) and not isinstance(states, (str, bytes)):
        for index, state in enumerate(states):
            _visit_model_node(state, f"{owner}.conditionalStates[{index}]", out)
    _visit_model_node(node.get("mounted"), f"{owner}.mounted", out)


def cah_model_bindings(document: Mapping[str, object]) -> tuple[CahModelBinding, ...]:
    """Every (mesh, hierarchy) pair the compiled CaH system selects.

    Derived from the descriptor, never from a hardcoded list: whatever the
    retail table selects is what the pack must carry.
    """

    registration = _registration(document)
    classes = registration.get("classes")
    if not isinstance(classes, Sequence) or isinstance(classes, (str, bytes)):
        raise CahModelPackError("CaH system registration carries no class table")
    bindings: list[CahModelBinding] = []
    for class_row in classes:
        if not isinstance(class_row, Mapping):
            raise CahModelPackError("CaH class row is not an object")
        class_name = str(class_row.get("nameStringId", "")) or str(
            class_row.get("classIndex", "?")
        )
        sub_classes = class_row.get("subClasses")
        if not isinstance(sub_classes, Sequence) or isinstance(
            sub_classes, (str, bytes)
        ):
            continue
        for sub_row in sub_classes:
            if not isinstance(sub_row, Mapping):
                raise CahModelPackError("CaH subclass row is not an object")
            sub_name = str(sub_row.get("nameStringId", "")) or str(
                sub_row.get("subClassIndex", "?")
            )
            models = sub_row.get("models")
            if not isinstance(models, Mapping):
                continue
            for presentation in ("battlefield", "creationScreen"):
                _visit_model_node(
                    models.get(presentation),
                    f"{class_name}/{sub_name}/{presentation}",
                    bindings,
                )
    if not bindings:
        raise CahModelPackError(
            "CaH system selects no meshes; refusing to ship a hero table with no art"
        )
    # One row per distinct mesh id; the first authored owner wins for
    # diagnostics, and a mesh reused by two subclasses converts once.
    unique: dict[str, CahModelBinding] = {}
    for binding in bindings:
        key = binding.model_id.casefold()
        existing = unique.get(key)
        if existing is None:
            unique[key] = binding
            continue
        if existing.skeleton_id.casefold() != binding.skeleton_id.casefold():
            raise CahModelPackError(
                f"CaH mesh {binding.model_id!r} binds two hierarchies: "
                f"{existing.skeleton_id!r} ({existing.owner}) and "
                f"{binding.skeleton_id!r} ({binding.owner})"
            )
    return tuple(
        sorted(unique.values(), key=lambda row: (row.model_id.casefold(), row.model_id))
    )


def cah_w3d_ids(document: Mapping[str, object]) -> tuple[str, ...]:
    """Every W3D id the lane must stage: meshes plus their hierarchies."""

    bindings = cah_model_bindings(document)
    seen: dict[str, str] = {}
    for binding in bindings:
        for identifier in (binding.model_id, binding.skeleton_id):
            seen.setdefault(identifier.casefold(), identifier)
    return tuple(seen[key] for key in sorted(seen))


def resolve_cah_w3d_paths(
    identifiers: Iterable[str], *, w3d_lookup: Callable[[str], str | None]
) -> dict[str, str]:
    """Map each W3D id to its retail virtual path, or refuse by name.

    ``w3d_lookup`` receives the casefolded ``<id>.w3d`` basename and returns
    the resolved virtual path (or ``None``).  Every unresolved id is reported
    together: a partial hero roster is a worse failure than a named refusal.
    """

    resolved: dict[str, str] = {}
    missing: list[str] = []
    for identifier in identifiers:
        virtual_path = w3d_lookup(f"{identifier.casefold()}.w3d")
        if not virtual_path:
            missing.append(identifier)
            continue
        resolved[identifier] = virtual_path
    if missing:
        raise CahModelPackError(
            "the retail install does not carry "
            f"{len(missing)} Create-a-Hero W3D file(s): "
            + ", ".join(sorted(missing, key=str.casefold))
        )
    return resolved


def _resource_id(*parts: str) -> str:
    return "-".join(part for part in parts if part)


def compile_cah_model_pack(
    document: Mapping[str, object],
    *,
    w3d_lookup: Callable[[str], str | None],
    textures_for: Callable[[tuple[str, ...]], Sequence[str]] | None = None,
) -> CahModelPack:
    """Compile profile resources for every mesh the CaH system selects.

    ``textures_for`` receives the staged W3D virtual paths of one mesh (the
    skin and its hierarchy) and returns the retail texture virtual paths those
    files reference; the returned paths become a ``hash-only`` companion
    resource so the Blender adapter can embed them (an unstaged texture makes
    the adapter log ``texture not found``, which the pipeline treats as fatal).
    """

    bindings = cah_model_bindings(document)
    paths = resolve_cah_w3d_paths(cah_w3d_ids(document), w3d_lookup=w3d_lookup)

    resources: list[dict[str, object]] = []
    rows: list[dict[str, object]] = []
    for binding in bindings:
        slug = binding.model_id.casefold()
        model_path = paths[binding.model_id]
        skeleton_path = paths[binding.skeleton_id]
        staged = tuple(
            sorted({model_path, skeleton_path}, key=lambda item: (item.casefold(), item))
        )
        texture_ids: list[str] = []
        raw_textures = tuple(textures_for(staged)) if textures_for is not None else ()
        ordered_textures = sorted(
            dict.fromkeys(raw_textures), key=lambda item: (item.casefold(), item)
        )
        if ordered_textures:
            texture_id = _resource_id(
                CAH_MODEL_RESOURCE_PREFIX, slug, CAH_TEXTURE_RESOURCE_SUFFIX
            )
            texture_ids.append(texture_id)
            resources.append(
                {
                    "id": texture_id,
                    "kind": "texture",
                    "converter": "hash-only",
                    "patterns": ordered_textures,
                    "required": True,
                    "limit": len(ordered_textures),
                    "expected_count": len(ordered_textures),
                }
            )
        resources.append(
            {
                "id": _resource_id(CAH_MODEL_RESOURCE_PREFIX, slug),
                "kind": "model",
                "converter": "w3d-hierarchical",
                "patterns": list(staged),
                "output": f"{CAH_MODEL_PACK_ROOT}/{binding.model_id}.glb",
                "options": {
                    "model": PurePosixPath(model_path).name,
                    "inputResourceIds": texture_ids,
                },
                "required": True,
                "limit": len(staged),
                "expected_count": len(staged),
            }
        )
        rows.append(
            {
                "modelId": binding.model_id,
                "skeletonId": binding.skeleton_id,
                "output": f"{CAH_MODEL_PACK_ROOT}/{binding.model_id}.glb",
                "sourceW3d": model_path,
                "skeletonW3d": skeleton_path,
                "textureCount": len(ordered_textures),
            }
        )
    receipt = {
        "packRoot": CAH_MODEL_PACK_ROOT,
        "modelCount": len(rows),
        "w3dCount": len(paths),
        "models": rows,
    }
    return CahModelPack(tuple(resources), bindings, receipt)


# --------------------------------------------------------------------------- #
# texture closure
# --------------------------------------------------------------------------- #


def _texture_identifiers(metadata: object) -> tuple[str, ...]:
    """Authored texture identifiers a scanned W3D references."""

    found: list[str] = []
    for reference in getattr(metadata, "texture_references", ()) or ():
        identifier = getattr(reference, "identifier", None)
        if isinstance(identifier, str) and identifier:
            found.append(identifier)
    for prop in getattr(metadata, "shader_material_properties", ()) or ():
        name = getattr(prop, "name", None)
        value = getattr(prop, "value", None)
        if (
            isinstance(name, str)
            and name in _SHADER_TEXTURE_PROPERTIES
            and getattr(prop, "property_type_name", None) == "string"
            and isinstance(value, str)
            and value
        ):
            found.append(value)
    return tuple(dict.fromkeys(found))


def cah_texture_resolver(
    *,
    read_w3d: Callable[[str], bytes],
    texture_catalog_paths: Sequence[str],
    scan: Callable[[bytes, str], object] | None = None,
) -> Callable[[tuple[str, ...]], tuple[str, ...]]:
    """Build a ``textures_for`` callable over the retail texture catalog.

    Applies the same authored-TGA -> compiled-DDS bridge the retail visual
    closure uses, including RotWK's space-bearing texture names (an authored
    ``"NAME .TGA"`` has no safe extensionless stem, so its explicit compiled
    ``.dds`` basename is requested instead).  An identifier no catalog path can
    answer is refused by name rather than dropped.
    """

    from .visual_leaf import VisualLeafRequest, diagnose_visual_leaves

    scanner = scan
    if scanner is None:
        from .w3d_metadata import scan_w3d_metadata as _scan

        scanner = _scan
    catalog_paths = tuple(texture_catalog_paths)
    cache: dict[str, tuple[str, ...]] = {}

    def _identifiers(virtual_path: str) -> tuple[str, ...]:
        cached = cache.get(virtual_path)
        if cached is None:
            cached = _texture_identifiers(
                scanner(read_w3d(virtual_path), virtual_path)
            )
            cache[virtual_path] = cached
        return cached

    def textures_for(staged: tuple[str, ...]) -> tuple[str, ...]:
        identifiers: list[str] = []
        for virtual_path in staged:
            identifiers.extend(_identifiers(virtual_path))
        unique = tuple(dict.fromkeys(identifiers))
        if not unique:
            return ()
        requests: list[VisualLeafRequest] = []
        primary: dict[str, int] = {}
        bridged: dict[str, int] = {}
        for identifier in unique:
            primary[identifier] = len(requests)
            requests.append(VisualLeafRequest(identifier, "texture"))
            pure = PurePosixPath(identifier)
            if pure.suffix.casefold() != ".tga":
                continue
            stem = pure.with_suffix("").as_posix()
            compiled = (
                pure.with_suffix(".dds").as_posix()
                if stem.endswith((" ", "."))
                else stem
            )
            bridged[identifier] = len(requests)
            requests.append(VisualLeafRequest(compiled, "texture"))
        batch = diagnose_visual_leaves(catalog_paths, requests)
        resolved: list[str] = []
        missing: list[str] = []
        for identifier in unique:
            candidates = [primary[identifier]]
            if identifier in bridged:
                candidates.append(bridged[identifier])
            for index in candidates:
                resolution = batch.resolutions[index]
                if resolution is None:
                    continue
                resolved.extend(leaf.virtual_path for leaf in resolution.leaves)
                break
            else:
                missing.append(identifier)
        if missing:
            raise CahModelPackError(
                "Create-a-Hero mesh references "
                f"{len(missing)} unresolved texture(s): "
                + ", ".join(sorted(missing, key=str.casefold))
                + f" (staged from {', '.join(staged)})"
            )
        return tuple(
            sorted(dict.fromkeys(resolved), key=lambda item: (item.casefold(), item))
        )

    return textures_for


__all__ = [
    "CAH_MODEL_PACK_ROOT",
    "CAH_MODEL_RESOURCE_PREFIX",
    "CahModelBinding",
    "CahModelPack",
    "CahModelPackError",
    "cah_model_bindings",
    "cah_texture_resolver",
    "cah_w3d_ids",
    "compile_cah_model_pack",
    "resolve_cah_w3d_paths",
]
