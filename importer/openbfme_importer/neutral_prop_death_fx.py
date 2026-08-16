"""Exact FXListDie packaging for passive neutral-prop removal presentation.

This lane deliberately preserves the complete authored FXList nugget sequence.
It reuses the proven ability-FX particle/resource closure, but does not collapse
land/water predicates, offsets, view shake, or sound into a generic effect.
"""

from __future__ import annotations

from collections.abc import Mapping
from copy import deepcopy
import hashlib
import json
from pathlib import Path, PurePosixPath

from .paths import safe_relative_parts
from .retail_ability_fx_ingress import (
    build_ability_fx_closure,
    validate_ability_fx_closure,
)
from .retail_men_damage_effects import parse_fx_lists
from .sage_audio import parse_sage_audio_definitions, resolve_sage_audio_closure


SCHEMA = "openbfme.neutral-prop-death-fx-closure"
SCHEMA_VERSION = 0
RUNTIME_SCHEMA = "openbfme.neutral-prop-death-fx-binding"
RUNTIME_SCHEMA_VERSION = 0


class NeutralPropDeathFxError(ValueError):
    """An authored FXListDie closure is absent, unsafe, or incomplete."""


def _digest(value: object) -> str:
    payload = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def build_audio_sample_index(effective_root: Path | str) -> dict[str, str]:
    root = Path(effective_root).expanduser().resolve()
    manifest_path = root / ".openbfme" / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise NeutralPropDeathFxError("effective-assets manifest is unreadable") from exc
    files = manifest.get("files") if isinstance(manifest, Mapping) else None
    if not isinstance(files, list):
        raise NeutralPropDeathFxError("effective-assets manifest files are invalid")
    by_stem: dict[str, list[str]] = {}
    for row in files:
        path = row.get("path") if isinstance(row, Mapping) else None
        if not isinstance(path, str) or PurePosixPath(path).suffix.casefold() not in {".wav", ".mp3"}:
            continue
        by_stem.setdefault(PurePosixPath(path).stem.casefold(), []).append(path)
    return {key: values[0] for key, values in by_stem.items() if len(values) == 1}


def _death_module(descriptor: Mapping[str, object]) -> Mapping[str, object] | None:
    modules = descriptor.get("moduleContracts", [])
    if not isinstance(modules, list):
        raise NeutralPropDeathFxError("neutral prop moduleContracts are invalid")
    rows = [
        row for row in modules
        if isinstance(row, Mapping) and row.get("module") == "FXListDie"
    ]
    if not rows:
        return None
    if len(rows) != 1:
        raise NeutralPropDeathFxError("neutral prop has duplicate FXListDie modules")
    row = rows[0]
    fields = row.get("fields")
    death = fields.get("DeathFX") if isinstance(fields, Mapping) else None
    if (
        row.get("carrier") != "Behavior"
        or row.get("extraction") not in {"typed", "opaque-authored"}
        or row.get("runtimeStatus") not in {"implemented", "deferred"}
        or not isinstance(death, Mapping)
        or not isinstance(death.get("authored"), str)
        or not death.get("authored")
        or not isinstance(death.get("sourceIni"), str)
        or not death.get("sourceIni")
        or not isinstance(death.get("line"), int)
        or death.get("line", 0) <= 0
    ):
        raise NeutralPropDeathFxError("FXListDie module receipt is invalid")
    return row


def compile_neutral_prop_death_fx(
    descriptor: Mapping[str, object],
    effect_documents: Mapping[str, bytes],
    *,
    texture_index: Mapping[str, str],
    audio_sample_index: Mapping[str, str],
) -> dict[str, object] | None:
    module = _death_module(descriptor)
    if module is None:
        return None
    death = module["fields"]["DeathFX"]  # type: ignore[index]
    fx_id = str(death["authored"])  # type: ignore[index]
    payload = effect_documents.get("data/ini/fxlist.ini")
    if not isinstance(payload, (bytes, bytearray)):
        raise NeutralPropDeathFxError("effective FXList source document is missing")
    record = parse_fx_lists(bytes(payload)).get(fx_id.casefold())
    if not isinstance(record, Mapping):
        raise NeutralPropDeathFxError(f"authored FXList is missing: {fx_id}")
    particle_closure = build_ability_fx_closure(
        effect_documents,
        [fx_id],
        namespace=f"neutral-prop-{descriptor.get('objectId', '')}",
        texture_index=texture_index,
    )
    bindings = particle_closure.get("runtimeBindings")
    if not isinstance(bindings, Mapping):
        raise NeutralPropDeathFxError("death FX particle bindings are invalid")
    if bindings.get("unresolved") != [] or bindings.get("presentableFxListIds") != [fx_id]:
        raise NeutralPropDeathFxError("death FXList resource closure is incomplete")
    fx_rows = bindings.get("fxLists")
    if not isinstance(fx_rows, list) or len(fx_rows) != 1:
        raise NeutralPropDeathFxError("death FXList runtime root is invalid")
    audio_ids = fx_rows[0].get("audioEventIds")
    sound_payload = effect_documents.get("data/ini/soundeffects.ini")
    if not isinstance(audio_ids, list) or not isinstance(sound_payload, (bytes, bytearray)):
        raise NeutralPropDeathFxError("death FX audio source is missing")
    audio_closure = resolve_sage_audio_closure(
        parse_sage_audio_definitions(bytes(sound_payload)),
        [str(value) for value in audio_ids],
    ).neutral()
    audio_resources: list[dict[str, object]] = []
    audio_outputs: list[str] = []
    owner_slug = "".join(
        character if character.isascii() and character.isalnum() else "-"
        for character in str(descriptor.get("objectId", "")).casefold()
    ).strip("-")
    for sample_id in audio_closure["sampleIds"]:
        source = audio_sample_index.get(str(sample_id).casefold())
        if source is None:
            raise NeutralPropDeathFxError(f"death FX audio sample is unresolved: {sample_id}")
        source = "/".join(safe_relative_parts(source))
        sample_slug = "".join(
            character if character.isascii() and character.isalnum() else "-"
            for character in str(sample_id).casefold()
        ).strip("-")
        identifier = f"neutral-prop-{owner_slug}-audio-{sample_slug}"
        output = f"assets/audio/neutral-props/{owner_slug}/{sample_slug}.wav"
        audio_resources.append({
            "id": identifier, "kind": "audio", "converter": "audio",
            "patterns": [source], "output": output,
            "options": {"force_pcm": True}, "required": True,
            "limit": 1, "expected_count": 1,
        })
        audio_outputs.append(output)
    audio_bindings = {str(value): list(audio_outputs) for value in audio_ids}
    binding: dict[str, object] = {
        "schema": RUNTIME_SCHEMA,
        "schemaVersion": RUNTIME_SCHEMA_VERSION,
        "objectId": descriptor.get("objectId"),
        "fxListId": fx_id,
        "moduleReceipt": deepcopy(dict(module)),
        "sourceSpan": deepcopy(record.get("sourceSpan")),
        "authoredNuggets": deepcopy(record.get("sections")),
        "particleBindings": deepcopy(dict(bindings)),
        "particleClosureSha256": particle_closure["aggregateSha256"],
        "audioClosure": audio_closure,
        "audioBindings": audio_bindings,
        "presentationStatus": "sealed-authored-route",
    }
    closure: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "objectId": descriptor.get("objectId"),
        "fxListId": fx_id,
        "resources": [*deepcopy(particle_closure["resources"]), *audio_resources],
        "particleClosure": particle_closure,
        "runtimeBinding": binding,
    }
    closure["aggregateSha256"] = _digest(closure)
    validate_neutral_prop_death_fx(closure)
    return closure


def validate_neutral_prop_death_fx(value: Mapping[str, object]) -> None:
    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise NeutralPropDeathFxError("neutral prop death FX schema is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("aggregateSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise NeutralPropDeathFxError("neutral prop death FX digest is invalid")
    particle = value.get("particleClosure")
    binding = value.get("runtimeBinding")
    resources = value.get("resources")
    if not isinstance(particle, Mapping) or not isinstance(binding, Mapping):
        raise NeutralPropDeathFxError("neutral prop death FX documents are invalid")
    validate_ability_fx_closure(particle)
    if not isinstance(resources, list) or resources[:len(particle.get("resources", []))] != particle.get("resources"):
        raise NeutralPropDeathFxError("neutral prop death FX particle resources drifted")
    fx_id = value.get("fxListId")
    if (
        binding.get("schema") != RUNTIME_SCHEMA
        or binding.get("schemaVersion") != RUNTIME_SCHEMA_VERSION
        or binding.get("objectId") != value.get("objectId")
        or binding.get("fxListId") != fx_id
        or binding.get("presentationStatus") != "sealed-authored-route"
        or binding.get("particleBindings") != particle.get("runtimeBindings")
        or binding.get("particleClosureSha256") != particle.get("aggregateSha256")
    ):
        raise NeutralPropDeathFxError("neutral prop death FX runtime binding drifted")
    module = binding.get("moduleReceipt")
    if not isinstance(module, Mapping) or module.get("module") != "FXListDie":
        raise NeutralPropDeathFxError("neutral prop death FX module binding is invalid")
    fields = module.get("fields")
    death = fields.get("DeathFX") if isinstance(fields, Mapping) else None
    if not isinstance(death, Mapping) or death.get("authored") != fx_id:
        raise NeutralPropDeathFxError("neutral prop death FX identifier drifted")
    audio_closure = binding.get("audioClosure")
    audio_bindings = binding.get("audioBindings")
    if (
        not isinstance(audio_closure, Mapping)
        or not isinstance(audio_bindings, Mapping)
        or audio_closure.get("rootIds") != next(iter(binding["particleBindings"]["fxLists"]), {}).get("audioEventIds")  # type: ignore[index]
        or set(audio_bindings) != set(audio_closure.get("rootIds", []))
    ):
        raise NeutralPropDeathFxError("neutral prop death FX audio binding drifted")
    audio_resource_rows = [row for row in resources if isinstance(row, Mapping) and row.get("kind") == "audio"]
    outputs = sorted(str(row.get("output", "")) for row in audio_resource_rows)
    if not outputs or any(sorted(value) != outputs for value in audio_bindings.values() if isinstance(value, list)):
        raise NeutralPropDeathFxError("neutral prop death FX audio resources are incomplete")
    nuggets = binding.get("authoredNuggets")
    source = binding.get("sourceSpan")
    if not isinstance(nuggets, list) or not nuggets or not isinstance(source, Mapping):
        raise NeutralPropDeathFxError("neutral prop death FX source evidence is absent")
    if not isinstance(source.get("sha256"), str) or len(str(source["sha256"])) != 64:
        raise NeutralPropDeathFxError("neutral prop death FX source digest is invalid")
    allowed = {"particlesystem", "viewshake", "sound", "fxlist", "fxlistatbonepos"}
    particle_names: list[str] = []
    for nugget in nuggets:
        if not isinstance(nugget, Mapping) or str(nugget.get("kind", "")).casefold() not in allowed:
            raise NeutralPropDeathFxError("neutral prop death FX nugget kind is unsupported")
        assignments = nugget.get("assignments")
        span = nugget.get("sourceSpan")
        if not isinstance(assignments, list) or not isinstance(span, Mapping):
            raise NeutralPropDeathFxError("neutral prop death FX nugget evidence is invalid")
        for assignment in assignments:
            if (
                not isinstance(assignment, Mapping)
                or not isinstance(assignment.get("field"), str)
                or not assignment.get("field")
                or not isinstance(assignment.get("value"), str)
                or not isinstance(assignment.get("sourceSpan"), Mapping)
            ):
                raise NeutralPropDeathFxError("neutral prop death FX assignment is invalid")
            if str(nugget.get("kind", "")).casefold() == "particlesystem" and str(assignment.get("field", "")).casefold() == "name":
                particle_names.append(str(assignment["value"]))
    fx_rows = particle.get("runtimeBindings", {}).get("fxLists", [])  # type: ignore[union-attr]
    if not isinstance(fx_rows, list) or len(fx_rows) != 1:
        raise NeutralPropDeathFxError("neutral prop death FX root closure changed")
    if sorted(particle_names, key=str.casefold) != sorted(fx_rows[0].get("particleSystemIds", []), key=str.casefold):
        raise NeutralPropDeathFxError("neutral prop death FX nugget/resource graph drifted")


__all__ = [
    "NeutralPropDeathFxError",
    "RUNTIME_SCHEMA",
    "SCHEMA",
    "compile_neutral_prop_death_fx",
    "build_audio_sample_index",
    "validate_neutral_prop_death_fx",
]
