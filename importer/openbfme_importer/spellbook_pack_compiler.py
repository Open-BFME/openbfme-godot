"""Descriptor-driven spellbook conversion recipe and runtime document compiler.

The spellbook lane has no W3D stage: its pack payload is the button art
(texture-atlas crops) and audio samples referenced by the resolved power tree.
This module joins one spellbook descriptor to those source-backed leaves and
fails closed on any unresolved binding; it never substitutes generic art or
silence.
"""

from __future__ import annotations

from collections.abc import Mapping
from copy import deepcopy
import hashlib
import json

from .playable_unit_pack_compiler import _resource_id, _safe_path, _slug
from .spellbook_compiler import (
    SpellbookCompilerError,
    validate_spellbook_descriptor,
)


SCHEMA = "openbfme.spellbook-pack-recipe"
SCHEMA_VERSION = 0
RUNTIME_SCHEMA = "openbfme.spellbook-runtime"
RUNTIME_SCHEMA_VERSION = 0


class SpellbookPackCompilerError(ValueError):
    """A source-backed spellbook cannot produce one bounded pack recipe."""


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def _digest(value: object) -> str:
    return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _rows(value: object, label: str) -> list[Mapping[str, object]]:
    if not isinstance(value, list) or any(
        not isinstance(row, Mapping) for row in value
    ):
        raise SpellbookPackCompilerError(f"{label} is invalid")
    return list(value)


def _text_ids(descriptor: Mapping[str, object]) -> list[str]:
    requirements = descriptor.get("requirements")
    if not isinstance(requirements, Mapping):
        raise SpellbookPackCompilerError("spellbook descriptor requirements are invalid")
    result: dict[str, list[str]] = {}
    for family in ("mappedImages", "audio", "strings"):
        values = requirements.get(family)
        if not isinstance(values, list) or any(
            not isinstance(item, str) or not item for item in values
        ):
            raise SpellbookPackCompilerError(
                f"spellbook descriptor {family} requirements are invalid"
            )
        result[family] = sorted(values, key=str.casefold)
    return result["mappedImages"], result["audio"], result["strings"]


def _image_leaves(
    descriptor: Mapping[str, object], slug: str, image_ids: list[str]
) -> tuple[list[dict[str, object]], dict[str, str], dict[str, dict[str, int]]]:
    presentation = descriptor.get("presentation")
    if not isinstance(presentation, Mapping):
        raise SpellbookPackCompilerError("spellbook descriptor presentation is invalid")
    images = presentation.get("resolvedImages")
    if not isinstance(images, Mapping):
        raise SpellbookPackCompilerError("spellbook descriptor images are invalid")
    by_texture: dict[str, list[tuple[str, Mapping[str, object]]]] = {}
    for identifier in image_ids:
        row = images.get(identifier)
        if not isinstance(row, Mapping):
            raise SpellbookPackCompilerError(
                f"spellbook button image is unresolved: {identifier}"
            )
        texture = row.get("compiledTextureVirtualPath")
        if not isinstance(texture, str) or not texture:
            raise SpellbookPackCompilerError(
                f"spellbook button image has no compiled texture: {identifier}"
            )
        by_texture.setdefault(texture.casefold(), []).append((identifier, row))

    resources: list[dict[str, object]] = []
    bindings: dict[str, str] = {}
    metadata: dict[str, dict[str, int]] = {}
    for texture_key in sorted(by_texture):
        rows = sorted(by_texture[texture_key], key=lambda item: item[0].casefold())
        texture_path = _safe_path(rows[0][1]["compiledTextureVirtualPath"], "icon texture")
        fingerprint = hashlib.sha256(texture_key.encode()).hexdigest()[:8]
        output_directory = f"assets/ui/spellbook/{slug}"
        crops: list[dict[str, object]] = []
        for identifier, row in rows:
            coords = row.get("coords")
            if not isinstance(coords, Mapping):
                raise SpellbookPackCompilerError(
                    f"spellbook button image has no crop evidence: {identifier}"
                )
            try:
                left = int(coords["left"])
                top = int(coords["top"])
                right = int(coords["right"])
                bottom = int(coords["bottom"])
            except (KeyError, TypeError, ValueError) as exc:
                raise SpellbookPackCompilerError(
                    f"spellbook button image crop is invalid: {identifier}"
                ) from exc
            if right <= left or bottom <= top:
                raise SpellbookPackCompilerError(
                    f"spellbook button image crop is invalid: {identifier}"
                )
            output_name = f"{_slug(identifier)}-{hashlib.sha256(identifier.casefold().encode()).hexdigest()[:8]}.png"
            crops.append(
                {
                    "logicalName": _resource_id("image", identifier),
                    "output": output_name,
                    "crop": [left, top, right - left, bottom - top],
                }
            )
            bindings[identifier] = f"{output_directory}/{output_name}"
            metadata[identifier] = {"width": right - left, "height": bottom - top}
        resources.append(
            {
                "id": _resource_id("spellbook", slug, "icons", fingerprint),
                "kind": "ui",
                "converter": "texture-atlas-crops",
                "patterns": [texture_path],
                "output": output_directory,
                "options": {"crops": crops},
                "required": True,
                "limit": 1,
                "expected_count": 1,
            }
        )
    return resources, bindings, metadata


def _audio_leaves(
    descriptor: Mapping[str, object], slug: str, audio_ids: list[str]
) -> tuple[list[dict[str, object]], dict[str, list[str]], dict[str, str]]:
    presentation = descriptor.get("presentation")
    if not isinstance(presentation, Mapping):
        raise SpellbookPackCompilerError("spellbook descriptor presentation is invalid")
    audio = presentation.get("resolvedAudio")
    if not isinstance(audio, Mapping):
        raise SpellbookPackCompilerError("spellbook descriptor audio is invalid")
    resources: list[dict[str, object]] = []
    bindings: dict[str, list[str]] = {}
    resolution: dict[str, str] = {}
    resources_by_source: dict[str, str] = {}
    for identifier in audio_ids:
        samples = audio.get(identifier)
        if not isinstance(samples, list) or any(
            not isinstance(path, str) or not path for path in samples
        ):
            raise SpellbookPackCompilerError(
                f"spellbook audio binding is unresolved: {identifier}"
            )
        outputs: list[str] = []
        for sample in samples:
            source = _safe_path(sample, f"spellbook audio sample {identifier}")
            source_key = source.casefold()
            output = resources_by_source.get(source_key, "")
            if not output:
                fingerprint = hashlib.sha256(source_key.encode()).hexdigest()[:16]
                output = f"assets/audio/spellbook/{slug}/{fingerprint}.wav"
                resources.append(
                    {
                        "id": _resource_id("spellbook", slug, "audio", fingerprint),
                        "kind": "audio",
                        "converter": "audio",
                        "patterns": [source],
                        "output": output,
                        "options": {"force_pcm": True},
                        "required": True,
                        "limit": 1,
                        "expected_count": 1,
                    }
                )
                resources_by_source[source_key] = output
            outputs.append(output)
        bindings[identifier] = sorted(set(outputs), key=str.casefold)
        resolution[identifier] = "samples" if outputs else "authored-silent"
    return resources, bindings, resolution


def compile_spellbook_pack_recipe(descriptor: Mapping[str, object]) -> dict[str, object]:
    """Compile one spellbook's source-backed pack recipe or fail closed."""

    try:
        validate_spellbook_descriptor(descriptor)
    except SpellbookCompilerError as exc:
        raise SpellbookPackCompilerError(str(exc)) from exc
    spellbook = descriptor["spellBook"]
    assert isinstance(spellbook, Mapping)
    object_id = str(spellbook["objectId"])
    slug = _slug(object_id)
    image_ids, audio_ids, string_ids = _text_ids(descriptor)
    presentation = descriptor["presentation"]
    assert isinstance(presentation, Mapping)
    strings = presentation.get("resolvedStrings")
    if not isinstance(strings, Mapping):
        raise SpellbookPackCompilerError("spellbook descriptor strings are invalid")
    missing_strings = [
        identifier for identifier in string_ids if identifier not in strings
    ]
    if missing_strings:
        raise SpellbookPackCompilerError(
            "spellbook string bindings are unresolved: " + ", ".join(missing_strings)
        )

    image_resources, image_bindings, image_metadata = _image_leaves(
        descriptor, slug, image_ids
    )
    audio_resources, audio_bindings, audio_resolution = _audio_leaves(
        descriptor, slug, audio_ids
    )

    recipe: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "target": deepcopy(dict(descriptor["target"])),  # type: ignore[arg-type]
        "spellBookObjectId": object_id,
        "slug": slug,
        "descriptorSha256": descriptor["descriptorSha256"],
        "resources": [*image_resources, *audio_resources],
        "runtimeRegistration": {
            "spellBook": deepcopy(dict(spellbook)),
            "sciences": deepcopy(descriptor["sciences"]),
            "powers": deepcopy(descriptor["powers"]),
            "leaves": deepcopy(descriptor["leaves"]),
            "imageBindings": image_bindings,
            "imageBindingMetadata": image_metadata,
            "stringBindings": {
                key: strings[key] for key in sorted(string_ids, key=str.casefold)
            },
            "audioBindings": audio_bindings,
            "audioResolution": audio_resolution,
        },
    }
    recipe["recipeSha256"] = _digest(recipe)
    return recipe


def validate_spellbook_pack_recipe(value: Mapping[str, object]) -> None:
    """Reject any spellbook pack recipe that drifted from its evidence."""

    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise SpellbookPackCompilerError("spellbook recipe identity is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("recipeSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise SpellbookPackCompilerError("spellbook recipe digest is invalid")
    if value["slug"] != _slug(str(value.get("spellBookObjectId", ""))):
        raise SpellbookPackCompilerError(
            "spellbook recipe slug does not match its object id"
        )
    resources = _rows(value.get("resources"), "spellbook recipe resources")
    identifiers = [str(row.get("id", "")) for row in resources]
    if len({item.casefold() for item in identifiers}) != len(identifiers):
        raise SpellbookPackCompilerError("spellbook recipe resource ids are invalid")
    runtime = value.get("runtimeRegistration")
    if not isinstance(runtime, Mapping):
        raise SpellbookPackCompilerError("spellbook runtime registration is invalid")
    powers = _rows(runtime.get("powers"), "spellbook runtime powers")
    if not powers:
        raise SpellbookPackCompilerError("spellbook runtime registration has no powers")
    for bindings in ("imageBindings", "audioBindings", "stringBindings"):
        if not isinstance(runtime.get(bindings), Mapping):
            raise SpellbookPackCompilerError(
                f"spellbook runtime {bindings} are invalid"
            )


def compose_spellbook_runtime_document(
    descriptor: Mapping[str, object], recipe: Mapping[str, object]
) -> dict[str, object]:
    """Join one spellbook descriptor and pack recipe into a runtime document."""

    try:
        validate_spellbook_descriptor(descriptor)
    except SpellbookCompilerError as exc:
        raise SpellbookPackCompilerError(str(exc)) from exc
    validate_spellbook_pack_recipe(recipe)
    if str(descriptor["descriptorSha256"]) != str(recipe["descriptorSha256"]):
        raise SpellbookPackCompilerError(
            "spellbook descriptor and pack recipe identities differ"
        )
    registration = recipe["runtimeRegistration"]
    assert isinstance(registration, Mapping)
    resources = _rows(recipe.get("resources"), "spellbook recipe resources")
    document: dict[str, object] = {
        "schema": RUNTIME_SCHEMA,
        "schemaVersion": RUNTIME_SCHEMA_VERSION,
        "target": deepcopy(dict(descriptor["target"])),  # type: ignore[arg-type]
        "spellBookObjectId": recipe["spellBookObjectId"],
        "slug": recipe["slug"],
        "descriptorSha256": descriptor["descriptorSha256"],
        "recipeSha256": recipe["recipeSha256"],
        "registration": {
            "spellBook": deepcopy(dict(registration["spellBook"])),  # type: ignore[arg-type]
            "powerTree": {
                "sciences": deepcopy(registration["sciences"]),
                "powers": deepcopy(registration["powers"]),
            },
            "leaves": deepcopy(dict(registration["leaves"])),  # type: ignore[arg-type]
            "presentation": {
                "imageBindings": deepcopy(dict(registration["imageBindings"])),  # type: ignore[arg-type]
                "imageBindingMetadata": deepcopy(dict(registration["imageBindingMetadata"])),  # type: ignore[arg-type]
                "stringBindings": deepcopy(dict(registration["stringBindings"])),  # type: ignore[arg-type]
                "audioBindings": deepcopy(dict(registration["audioBindings"])),  # type: ignore[arg-type]
                "audioResolution": deepcopy(dict(registration["audioResolution"])),  # type: ignore[arg-type]
            },
            "resourceIds": sorted(str(row["id"]) for row in resources),
        },
    }
    document["runtimeSha256"] = _digest(document)
    return document


__all__ = [
    "RUNTIME_SCHEMA",
    "RUNTIME_SCHEMA_VERSION",
    "SCHEMA",
    "SCHEMA_VERSION",
    "SpellbookPackCompilerError",
    "compile_spellbook_pack_recipe",
    "compose_spellbook_runtime_document",
    "validate_spellbook_pack_recipe",
]
