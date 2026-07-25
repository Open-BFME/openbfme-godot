"""Ingress lane for hero-ability and spellbook FX presentation.

Retail authors every hero ability and spell-book power's on-screen effect as an
``FXList`` id (``FireFX``/``HealFX``/``LevelFX``/``AttributeModifier FX`` and
friends).  The gameplay compilers already carry those ids into the compiled
descriptors, but nothing resolved them into converted art: the only FXLists any
pack ever converted were the structure-lifecycle ones harvested from
``objects.json`` by :mod:`retail_men_damage_effects`.  Ability and power casts
therefore reached the runtime with authored ids and no assets behind them.

This module closes that gap.  Given the effective retail INI view and the FX ids
a descriptor authored, it walks

    FXList -> (nested FXList)* -> ParticleSystem / FXParticleSystem -> texture

and returns the exact conversion resources plus a payload-free runtime binding
document.  It never invents art: an id with no authored ``FXList`` block, a
particle system with no definition in either family, and a ``ParticleName`` whose
render leaf is absent or ambiguous are each recorded as an explicit unresolved
row and contribute no resource.  Cross-family duplicates (a name authored as
both ``ParticleSystem`` and ``FXParticleSystem``) preserve both candidates and
are reported unresolved, matching the sealed Fords precedence blocker.

Resource ids are namespaced by the owning unit/spellbook slug because the
playable-unit profile extension gives every resource exactly one runtime owner.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping, Sequence
from copy import deepcopy
from functools import lru_cache
import hashlib
import json
from pathlib import Path, PurePosixPath

from .paths import safe_relative_parts
from .retail_men_damage_effects import parse_fx_lists
from .sage_particles import ParticleAssignment, ParticleBlock, ParticleDefinition, ParticleEntry, parse_particle_definitions


SCHEMA = "openbfme.ability-fx-closure"
SCHEMA_VERSION = 0
RUNTIME_SCHEMA = "openbfme.ability-fx-bindings"
RUNTIME_SCHEMA_VERSION = 0

FX_LIST_PATH = "data/ini/fxlist.ini"
PARTICLE_PATHS = {
    "ParticleSystem": "data/ini/particlesystem.ini",
    "FXParticleSystem": "data/ini/fxparticlesystem.ini",
}
PARTICLE_FAMILIES = ("ParticleSystem", "FXParticleSystem")

MAX_FX_ROOTS = 512
MAX_FX_CLOSURE = 4096
MAX_TEXTURE_INDEX_ENTRIES = 1_000_000
_RENDER_SUFFIXES = frozenset({".dds", ".tga"})

# Descriptor/runtime fields that carry exactly one authored FXList id.  Every
# one of these is emitted by the playable-unit or spellbook compilers today.
FX_ID_FIELDS = frozenset(
    {
        "firefxid",
        "healfxid",
        "levelfxid",
        "levelupfxid",
        "cursedfxid",
        "fxlistid",
    }
)
# Fields that carry an array of authored FXList ids.
FX_ID_ARRAY_FIELDS = frozenset({"fxids", "fxlists"})

# Retail spells "no effect" several ways; none of them names an FXList.
_NULL_TOKENS = frozenset({"", "none", "null", "no", "nothing"})

# Nested-FXList nugget kinds and the field each one names its child list with.
# ``FXListAtBonePos`` is how retail re-emits a list at a model bone; it is the
# only hop between FX_TelekinesisAtBone and the Wizard Blast wave itself
# (data/ini/fxlist.ini:7965-7973).
_NESTED_FX_SECTIONS = {
    "fxlist": "name",
    "fxlistatbonepos": "fx",
}


def parse_keyed_value(value: str) -> tuple[str, dict[str, str]]:
    """Split a SAGE multi-key value into its bare id and ``KEY:VALUE`` options.

    Retail authors several reference fields as ``<id> KEY:VALUE ...`` and some
    references as ``FX:<id>``.  Two of the ids the ability lane harvests
    (``FX:GandalfLevelUp1FX``) and three of the particle references reached from
    ``FX_GandalfLightningSwordBlastWeapon`` (``LightningStrike FollowBone:Yes``)
    are unresolvable without this.  Evidence:
    ``data/ini/object/goodfaction/structures/men/marketplace.ini:307`` authors
    ``FireFXList = FX:FX_ForgeChimneySmoke BONE:FireSmall01`` and no ``FXList``
    block in ``data/ini/fxlist.ini`` has a colon in its name.
    """

    options: dict[str, str] = {}
    identifier = ""
    for token in str(value).split():
        if ":" in token:
            key, _, item = token.partition(":")
            folded = key.casefold()
            if folded == "fx" and not identifier:
                # ``FX:`` is the reference namespace tag, not an option.
                identifier = item
                continue
            options[folded] = item
            continue
        if not identifier:
            identifier = token
    return identifier, options


class AbilityFxIngressError(ValueError):
    """The authored FX closure cannot be sealed from the retail corpus."""


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False
    ).encode("utf-8")


def _digest(value: object) -> str:
    return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _sorted_unique(values: Iterable[str]) -> list[str]:
    return sorted(set(values), key=lambda value: (value.casefold(), value))


def _slug(value: str) -> str:
    result = "".join(
        character if character.isascii() and character.isalnum() else "-"
        for character in value.casefold()
    )
    result = "-".join(part for part in result.split("-") if part)
    if not result:
        raise AbilityFxIngressError(f"identifier has no safe slug: {value!r}")
    return result


def _resource_id(*parts: str) -> str:
    candidate = "-".join(_slug(part) for part in parts)
    if len(candidate) <= 64:
        return candidate
    digest = hashlib.sha256(candidate.encode()).hexdigest()[:12]
    return f"{candidate[:51].rstrip('-')}-{digest}"


def harvest_fx_ids(document: object) -> list[str]:
    """Return every authored FXList id reachable from one compiled document.

    The walk is field-name driven, so it picks the ids up wherever the
    gameplay compilers put them (ability ``effect`` leaves, weapon fire FX,
    attribute-modifier FX arrays, experience-level grants, spellbook nuggets)
    without this module having to know each lane's document shape.
    """

    found: list[str] = []

    def walk(value: object) -> None:
        if isinstance(value, Mapping):
            for key, item in value.items():
                folded = str(key).casefold()
                if folded in FX_ID_FIELDS and isinstance(item, str):
                    token, _ = parse_keyed_value(item)
                    if token.casefold() not in _NULL_TOKENS:
                        found.append(token)
                elif folded in FX_ID_ARRAY_FIELDS and isinstance(item, list):
                    for entry in item:
                        if not isinstance(entry, str):
                            continue
                        token, _ = parse_keyed_value(entry)
                        if token.casefold() not in _NULL_TOKENS:
                            found.append(token)
                else:
                    walk(item)
        elif isinstance(value, list):
            for item in value:
                walk(item)

    walk(document)
    return _sorted_unique(found)


def build_texture_index(effective_root: Path | str) -> dict[str, str]:
    """Index the manifest-bound render leaves by lowercase file stem.

    Only the effective-assets manifest is trusted: it is the same closure the
    rest of the importer converts from, so a stem that resolves here is
    guaranteed extractable.
    """

    root = Path(effective_root).expanduser().resolve()
    manifest_path = root / ".openbfme" / "manifest.json"
    if not manifest_path.is_file():
        raise AbilityFxIngressError("effective-assets root has no private manifest")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise AbilityFxIngressError("effective-assets manifest is unreadable") from exc
    files = manifest.get("files") if isinstance(manifest, Mapping) else None
    if not isinstance(files, list) or len(files) > MAX_TEXTURE_INDEX_ENTRIES:
        raise AbilityFxIngressError("effective-assets manifest files are invalid")
    by_stem: dict[str, list[str]] = {}
    for row in files:
        if not isinstance(row, Mapping):
            continue
        path = row.get("path")
        if not isinstance(path, str) or not path:
            continue
        pure = PurePosixPath(path)
        if pure.suffix.casefold() not in _RENDER_SUFFIXES:
            continue
        by_stem.setdefault(pure.stem.casefold(), []).append(path)
    # A stem with more than one render leaf is ambiguous; keep it out of the
    # index so the caller records an unresolved row instead of guessing.
    return {
        stem: paths[0]
        for stem, paths in by_stem.items()
        if len(paths) == 1
    }


@lru_cache(maxsize=4)
def _memoized_texture_index(root: str, fingerprint: str) -> tuple[tuple[str, str], ...]:
    return tuple(sorted(build_texture_index(Path(root)).items()))


def texture_index_for(effective_root: Path | str, fingerprint: str) -> dict[str, str]:
    """Process-local memo of :func:`build_texture_index`.

    A faction import resolves render leaves once per object; the manifest has
    tens of thousands of rows, so the index is built once per (root, content
    fingerprint) pair and shared.  ``fingerprint`` is the durable effective-assets
    identity, so a re-extracted tree never reuses a stale index.
    """

    root = str(Path(effective_root).expanduser().resolve())
    return dict(_memoized_texture_index(root, fingerprint))


def _required_document(documents: Mapping[str, bytes], path: str) -> bytes:
    payload = documents.get(path)
    if not isinstance(payload, (bytes, bytearray)):
        raise AbilityFxIngressError(f"effective source document is missing: {path}")
    return bytes(payload)


def _iter_sections(record: Mapping[str, object]) -> Iterable[Mapping[str, object]]:
    """Yield every FXList nugget, including nuggets nested inside nuggets."""

    stack: list[object] = list(record.get("sections", []))  # type: ignore[arg-type]
    while stack:
        section = stack.pop(0)
        if not isinstance(section, Mapping):
            raise AbilityFxIngressError("FXList section is invalid")
        yield section
        stack.extend(section.get("sections", []))  # type: ignore[arg-type]


def _field_values(section: Mapping[str, object], field: str) -> list[str]:
    return [
        str(item.get("value", ""))
        for item in section.get("assignments", [])  # type: ignore[union-attr]
        if isinstance(item, Mapping)
        and str(item.get("field", "")).casefold() == field.casefold()
    ]


def _section_ids(record: Mapping[str, object], kind: str, field: str) -> list[str]:
    """Return the bare ids one nugget family names, keyed tokens stripped."""

    result: list[str] = []
    for section in _iter_sections(record):
        if str(section.get("kind", "")).casefold() != kind.casefold():
            continue
        values = _field_values(section, field)
        if len(values) != 1:
            raise AbilityFxIngressError(
                f"{record.get('fxListId')} {section.get('kind')} needs exactly one {field}"
            )
        identifier, _ = parse_keyed_value(values[0])
        if identifier:
            result.append(identifier)
    return result


def _nested_fx_ids(record: Mapping[str, object]) -> list[str]:
    result: list[str] = []
    for kind, field in _NESTED_FX_SECTIONS.items():
        result.extend(_section_ids(record, kind, field))
    return result


def _walk_assignments(
    entries: Iterable[ParticleEntry],
) -> Iterable[ParticleAssignment]:
    for entry in entries:
        if isinstance(entry, ParticleAssignment):
            yield entry
        elif isinstance(entry, ParticleBlock):
            yield from _walk_assignments(entry.entries)


def _particle_texture_names(definition: ParticleDefinition) -> list[str]:
    return _sorted_unique(
        assignment.value
        for assignment in _walk_assignments(definition.entries)
        if assignment.field.casefold() == "particlename" and assignment.value
    )


# Verbatim authored scalars the presentation layer may read without opening the
# converted definition document.  Deliberately a closed allowlist: every value
# is copied byte-for-byte from the source block whose sha256 the registry row
# already carries, and nothing here is interpreted by the importer.
_PRESENTATION_SCALARS = (
    "isgroundaligned",
    "lifetime",
    "systemlifetime",
    "size",
    "sizerate",
    "color1",
    "color2",
    "burstcount",
    "priority",
)


def _presentation_scalars(definition: ParticleDefinition) -> dict[str, str]:
    scalars: dict[str, str] = {}
    for assignment in _walk_assignments(definition.entries):
        folded = assignment.field.casefold()
        if folded in _PRESENTATION_SCALARS and folded not in scalars:
            scalars[folded] = assignment.value
    return scalars


def _attached_system_names(definition: ParticleDefinition) -> list[str]:
    """Child emitters a definition spawns per particle or on its own events.

    ``GandalfWaveBlastProxy`` is a near-invisible carrier (``Alpha1 = 0.01``)
    whose only visible output is the ``GandalfWaveBlastDust`` system named by
    ``PerParticleAttachedSystem`` (data/ini/fxparticlesystem.ini:30580).  A
    closure that stops at the directly-named systems converts the carrier and
    nothing you can see.
    """

    names: list[str] = []
    for assignment in _walk_assignments(definition.entries):
        if assignment.field.casefold() not in {
            "perparticleattachedsystem",
            "attachedsystem",
        }:
            continue
        identifier, _ = parse_keyed_value(assignment.value)
        if identifier and identifier.casefold() not in _NULL_TOKENS:
            names.append(identifier)
    return _sorted_unique(names)


@lru_cache(maxsize=4)
def _parsed_family(payload: bytes) -> dict[str, ParticleDefinition]:
    by_name: dict[str, ParticleDefinition] = {}
    for definition in parse_particle_definitions(payload):
        # SAGE redefinition is last-wins; the Fords oracle proved that for
        # repeated FXParticleSystem syntax.
        by_name[definition.name.casefold()] = definition
    return by_name


@lru_cache(maxsize=4)
def _parsed_fx_lists(payload: bytes) -> dict[str, dict[str, object]]:
    return parse_fx_lists(payload)


def _index_definitions(
    documents: Mapping[str, bytes],
) -> dict[str, dict[str, ParticleDefinition]]:
    """Parse both particle families once per corpus, not once per object.

    ``fxparticlesystem.ini`` is 1.5 MB / ~1700 definitions; a faction import
    seals a closure for every ability-bearing object, so re-parsing per object
    dominated the lane's cost. The cache key is the source bytes, so a different
    edition or a re-extracted tree never reuses another corpus' index.
    """

    indexed: dict[str, dict[str, ParticleDefinition]] = {}
    for kind, path in PARTICLE_PATHS.items():
        payload = documents.get(path)
        if not isinstance(payload, (bytes, bytearray)):
            # ``particlesystem.ini`` is absent from some lanes' document views.
            # Missing legacy family means fewer candidates, never invented ones.
            indexed[kind] = {}
            continue
        indexed[kind] = _parsed_family(bytes(payload))
    return indexed


def build_ability_fx_closure(
    documents: Mapping[str, bytes],
    fx_ids: Sequence[str],
    *,
    namespace: str,
    texture_index: Mapping[str, str],
) -> dict[str, object]:
    """Seal one owner's authored FX closure into resources and runtime bindings.

    ``namespace`` scopes every emitted resource id to the owning unit or
    spellbook slug, because a playable-unit profile extension requires each
    resource to have exactly one runtime owner.
    """

    namespace_slug = _slug(namespace)
    roots = _sorted_unique(str(value).strip() for value in fx_ids if str(value).strip())
    if len(roots) > MAX_FX_ROOTS:
        raise AbilityFxIngressError("authored FX root set is unreasonably large")

    fx_payload = _required_document(documents, FX_LIST_PATH)
    all_fx_lists = parse_fx_lists(fx_payload)
    definitions_by_kind = _index_definitions(documents)

    unresolved: list[dict[str, object]] = []
    selected_fx: dict[str, dict[str, object]] = {}
    queue = list(roots)
    seen_requests: set[str] = set()
    while queue:
        requested = queue.pop(0)
        key = requested.casefold()
        if key in seen_requests:
            continue
        seen_requests.add(key)
        if len(selected_fx) > MAX_FX_CLOSURE:
            raise AbilityFxIngressError("authored FX closure is unreasonably large")
        record = all_fx_lists.get(key)
        if record is None:
            unresolved.append(
                {
                    "kind": "fx-list",
                    "id": requested,
                    "reason": "no authored FXList block in the retail corpus",
                }
            )
            continue
        canonical = str(record["fxListId"])
        nested = _nested_fx_ids(record)
        particles = _section_ids(record, "ParticleSystem", "Name")
        source = record.get("sourceSpan")
        if not isinstance(source, Mapping) or not isinstance(source.get("sha256"), str):
            raise AbilityFxIngressError(f"FXList source evidence is invalid: {canonical}")
        selected_fx[canonical] = {
            "fxListId": canonical,
            "isAuthoredRoot": key in {value.casefold() for value in roots},
            "particleSystemIds": particles,
            "nestedFxListIds": nested,
            "audioEventIds": _section_ids(record, "Sound", "Name"),
            "hasViewShake": any(
                isinstance(section, Mapping)
                and str(section.get("kind", "")).casefold() == "viewshake"
                for section in record.get("sections", [])  # type: ignore[union-attr]
            ),
            "sourceSpan": {
                "startLine": source.get("startLine"),
                "endLine": source.get("endLine"),
                "byteLength": source.get("byteLength"),
                "sha256": source.get("sha256"),
            },
        }
        queue.extend(nested)

    directly_named = _sorted_unique(
        system
        for record in selected_fx.values()
        for system in record["particleSystemIds"]  # type: ignore[union-attr]
    )
    # Close over child emitters (PerParticleAttachedSystem) so a carrier system
    # never converts without the system that actually draws.
    system_ids: list[str] = []
    seen_systems: set[str] = set()
    system_queue = list(directly_named)
    while system_queue:
        system_id = system_queue.pop(0)
        if system_id.casefold() in seen_systems:
            continue
        seen_systems.add(system_id.casefold())
        system_ids.append(system_id)
        if len(system_ids) > MAX_FX_CLOSURE:
            raise AbilityFxIngressError("authored particle closure is unreasonably large")
        for kind in PARTICLE_FAMILIES:
            definition = definitions_by_kind[kind].get(system_id.casefold())
            if definition is not None:
                system_queue.extend(_attached_system_names(definition))
    system_ids = _sorted_unique(system_ids)

    texture_rows: dict[str, dict[str, object]] = {}
    definition_rows: list[dict[str, object]] = []
    duplicate_ids: list[str] = []
    definition_resources: list[dict[str, object]] = []
    texture_resources: list[dict[str, object]] = []

    for system_id in system_ids:
        candidates: list[dict[str, object]] = []
        authored_families: list[str] = []
        for kind in PARTICLE_FAMILIES:
            definition = definitions_by_kind[kind].get(system_id.casefold())
            if definition is None:
                continue
            authored_families.append(kind)
            texture_resource_ids: list[str] = []
            missing_names: list[str] = []
            for authored in _particle_texture_names(definition):
                leaf = texture_index.get(PurePosixPath(authored).stem.casefold())
                if leaf is None:
                    missing_names.append(authored)
                    continue
                path = "/".join(safe_relative_parts(leaf))
                row = texture_rows.get(path.casefold())
                if row is None:
                    resource_id = _resource_id("fx", namespace_slug, "tex", PurePosixPath(path).stem)
                    row = {
                        "virtualPath": path,
                        "resourceId": resource_id,
                        "authoredIdentifiers": [],
                        "output": f"assets/textures/effects/{namespace_slug}/{resource_id}.png",
                    }
                    texture_rows[path.casefold()] = row
                    texture_resources.append(
                        {
                            "id": resource_id,
                            "kind": "texture",
                            "converter": "texture",
                            "patterns": [path],
                            "output": row["output"],
                            "required": True,
                            "limit": 1,
                            "expected_count": 1,
                        }
                    )
                identifiers = row["authoredIdentifiers"]
                assert isinstance(identifiers, list)
                if authored not in identifiers:
                    identifiers.append(authored)
                if str(row["resourceId"]) not in texture_resource_ids:
                    texture_resource_ids.append(str(row["resourceId"]))
            if missing_names:
                unresolved.append(
                    {
                        "kind": "particle-texture",
                        "id": system_id,
                        "family": kind,
                        "authoredNames": sorted(missing_names, key=str.casefold),
                        "reason": "render leaf is absent or ambiguous in the manifest",
                    }
                )
            if not texture_resource_ids:
                # A definition with no resolvable render leaf has nothing to
                # draw.  Emitting it would let the runtime bind a texture-less
                # emitter, so it is dropped and reported instead.
                continue
            resource_id = _resource_id("fx", namespace_slug, "def", kind, definition.name)
            output = f"effects/particles/{namespace_slug}/{resource_id}.json"
            definition_resources.append(
                {
                    "id": resource_id,
                    "kind": "data",
                    "converter": "sage-particle-definition",
                    "patterns": [PARTICLE_PATHS[kind]],
                    "output": output,
                    "required": True,
                    "limit": 1,
                    "expected_count": 1,
                    "options": {"kind": kind, "name": definition.name},
                }
            )
            candidates.append(
                {
                    "kind": kind,
                    "definitionId": definition.name,
                    "definitionResourceId": resource_id,
                    "definitionOutputJson": output,
                    "sourceVirtualPath": PARTICLE_PATHS[kind],
                    "sourceBlockSha256": definition.source.sha256,
                    "textureResourceIds": texture_resource_ids,
                    "authoredScalars": _presentation_scalars(definition),
                }
            )
        if not candidates:
            unresolved.append(
                {
                    "kind": "particle-system",
                    "id": system_id,
                    "authoredFamilies": authored_families,
                    "reason": (
                        "no authored definition in either particle family"
                        if not authored_families
                        else "authored definition has no convertible render leaf"
                    ),
                }
            )
            continue
        if len(candidates) == 1:
            resolution = {
                "status": "exact-single-authored-family",
                "selectedKind": candidates[0]["kind"],
            }
        else:
            duplicate_ids.append(system_id)
            resolution = {
                "status": "unresolved-cross-family-precedence",
                "selectedKind": None,
            }
        definition_rows.append(
            {
                "particleSystemId": system_id,
                "definitionCandidates": candidates,
                "familyResolution": resolution,
            }
        )

    definition_resources.sort(key=lambda row: str(row["id"]))
    texture_resources.sort(key=lambda row: str(row["id"]))
    unresolved.sort(
        key=lambda row: (str(row["kind"]), str(row["id"]).casefold(), str(row["id"]))
    )

    converted_systems = {
        str(row["particleSystemId"]).casefold(): str(row["particleSystemId"])
        for row in definition_rows
    }
    by_fold = {key.casefold(): key for key in selected_fx}
    for fx_id, record in selected_fx.items():
        # Transitive closure of the converted systems this list reaches, so the
        # presentation layer can bind a cast to real art in one lookup instead
        # of re-walking the nesting itself.
        reached: list[str] = []
        stack = [fx_id]
        seen_fx: set[str] = set()
        while stack:
            current = stack.pop()
            if current.casefold() in seen_fx:
                continue
            seen_fx.add(current.casefold())
            child = selected_fx.get(current)
            if child is None:
                continue
            for system in child["particleSystemIds"]:  # type: ignore[union-attr]
                if str(system).casefold() in converted_systems:
                    reached.append(converted_systems[str(system).casefold()])
            stack.extend(
                by_fold[str(value).casefold()]
                for value in child["nestedFxListIds"]  # type: ignore[union-attr]
                if str(value).casefold() in by_fold
            )
        record["resolvedParticleSystemIds"] = _sorted_unique(reached)

    resolved_fx_ids = [
        fx_id
        for fx_id in sorted(selected_fx, key=lambda value: (value.casefold(), value))
        # An FXList presents only if at least one of its particle systems, or
        # one of its nested lists, produced a converted definition.
        if selected_fx[fx_id]["resolvedParticleSystemIds"]
    ]

    bindings: dict[str, object] = {
        "schema": RUNTIME_SCHEMA,
        "schemaVersion": RUNTIME_SCHEMA_VERSION,
        "authoredFxListIds": roots,
        "fxLists": [
            selected_fx[key]
            for key in sorted(selected_fx, key=lambda value: (value.casefold(), value))
        ],
        "definitionRegistry": [
            candidate
            for row in definition_rows
            for candidate in row["definitionCandidates"]  # type: ignore[union-attr]
        ],
        "familyResolution": {
            "duplicateIdentifierSystemIds": sorted(duplicate_ids, key=str.casefold),
            "crossFamilyPrecedenceProven": False,
        },
        "textures": [
            texture_rows[key] for key in sorted(texture_rows)
        ],
        "presentableFxListIds": resolved_fx_ids,
        "unresolved": unresolved,
    }

    closure: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "namespace": namespace_slug,
        "resources": [*texture_resources, *definition_resources],
        "runtimeBindings": bindings,
        "summary": {
            "authoredFxListIdCount": len(roots),
            "resolvedFxListCount": len(selected_fx),
            "presentableFxListCount": len(resolved_fx_ids),
            "particleSystemIdCount": len(system_ids),
            "convertedDefinitionCount": len(definition_resources),
            "convertedTextureCount": len(texture_resources),
            "duplicateFamilySystemCount": len(duplicate_ids),
            "unresolvedCount": len(unresolved),
        },
    }
    closure["aggregateSha256"] = _digest(closure)
    return closure


def validate_ability_fx_closure(value: Mapping[str, object]) -> None:
    """Reject an FX closure that drifted from the evidence that produced it."""

    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise AbilityFxIngressError("ability FX closure identity is invalid")
    unsigned = dict(value)
    declared = unsigned.pop("aggregateSha256", None)
    if not isinstance(declared, str) or declared != _digest(unsigned):
        raise AbilityFxIngressError("ability FX closure digest is invalid")
    resources = value.get("resources")
    if not isinstance(resources, list):
        raise AbilityFxIngressError("ability FX closure resources are invalid")
    identifiers = [
        str(row.get("id", "")) for row in resources if isinstance(row, Mapping)
    ]
    if len(identifiers) != len(resources):
        raise AbilityFxIngressError("ability FX closure resources are invalid")
    if len({value.casefold() for value in identifiers}) != len(identifiers):
        raise AbilityFxIngressError("ability FX closure resource ids are duplicated")
    bindings = value.get("runtimeBindings")
    if (
        not isinstance(bindings, Mapping)
        or bindings.get("schema") != RUNTIME_SCHEMA
        or bindings.get("schemaVersion") != RUNTIME_SCHEMA_VERSION
    ):
        raise AbilityFxIngressError("ability FX runtime bindings are invalid")
    declared_resources = {identifier.casefold() for identifier in identifiers}
    registry = bindings.get("definitionRegistry")
    if not isinstance(registry, list):
        raise AbilityFxIngressError("ability FX definition registry is invalid")
    for row in registry:
        if not isinstance(row, Mapping):
            raise AbilityFxIngressError("ability FX definition registry row is invalid")
        if str(row.get("definitionResourceId", "")).casefold() not in declared_resources:
            raise AbilityFxIngressError(
                "ability FX definition registry references an unowned resource"
            )
        textures = row.get("textureResourceIds")
        if not isinstance(textures, list) or not textures:
            raise AbilityFxIngressError("ability FX definition has no source texture")
        for texture_id in textures:
            if str(texture_id).casefold() not in declared_resources:
                raise AbilityFxIngressError(
                    "ability FX definition references an unowned texture"
                )


def fx_recipe_parts(
    closure: Mapping[str, object] | None, namespace: str
) -> tuple[list[dict[str, object]], dict[str, object] | None]:
    """Split a sealed closure into recipe resources and runtime bindings.

    Returns ``([], None)`` for a missing closure so every lane that has no
    effective-assets root (unit-only imports, fixtures) keeps producing exactly
    the recipe it produced before this lane existed.
    """

    if closure is None:
        return [], None
    validate_ability_fx_closure(closure)
    if str(closure.get("namespace", "")) != _slug(namespace):
        raise AbilityFxIngressError(
            "ability FX closure namespace does not match its owner"
        )
    resources = [deepcopy(dict(row)) for row in closure["resources"]]  # type: ignore[union-attr]
    bindings = deepcopy(dict(closure["runtimeBindings"]))  # type: ignore[arg-type]
    return resources, bindings


def merge_ability_fx_closures(
    closures: Sequence[Mapping[str, object]],
) -> dict[str, object]:
    """Union several owners' closures, keeping byte-identical rows shared."""

    resources: dict[str, dict[str, object]] = {}
    for closure in closures:
        validate_ability_fx_closure(closure)
        for row in closure["resources"]:  # type: ignore[union-attr]
            assert isinstance(row, Mapping)
            key = str(row["id"]).casefold()
            existing = resources.get(key)
            candidate = deepcopy(dict(row))
            if existing is not None and existing != candidate:
                raise AbilityFxIngressError(
                    f"ability FX resource id collision: {row['id']}"
                )
            resources[key] = candidate
    return {
        "resources": [resources[key] for key in sorted(resources)],
    }


__all__ = [
    "AbilityFxIngressError",
    "FX_ID_ARRAY_FIELDS",
    "FX_ID_FIELDS",
    "RUNTIME_SCHEMA",
    "RUNTIME_SCHEMA_VERSION",
    "SCHEMA",
    "SCHEMA_VERSION",
    "build_ability_fx_closure",
    "build_texture_index",
    "fx_recipe_parts",
    "harvest_fx_ids",
    "parse_keyed_value",
    "texture_index_for",
    "merge_ability_fx_closures",
    "validate_ability_fx_closure",
]
