"""Sealed presentation prerequisites for retail ``SpecialDisguiseUpdate``.

This module intentionally does not type or execute the gameplay module.  It
packages the additional model forms and the complete FX/audio/particle graph
that an eventual runtime implementation must have before it may activate the
DISGUISED state.  The child Object is retained only as a presentation identity;
it is never admitted as an authoritative unit.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from copy import deepcopy
import hashlib
import json
from pathlib import Path, PurePosixPath

from .neutral_prop_death_fx import build_audio_sample_index
from .paths import safe_relative_parts
from .retail_ability_fx_ingress import (
    build_ability_fx_closure,
    validate_ability_fx_closure,
)
from .sage_audio import (
    SageAudioDefinitions,
    parse_sage_audio_definitions,
    resolve_sage_audio_closure,
)
from .sage_cst import SageBlock, SageObject, parse_sage_document


SCHEMA = "openbfme.special-disguise-presentation-prerequisite"
SCHEMA_VERSION = 0
RUNTIME_STATUS = "sealed-deferred-no-runtime-activation"

_REQUIRED_FIELDS = (
    "SpecialPowerTemplate",
    "UnpackTime",
    "PreparationTime",
    "PersistentPrepTime",
    "PackTime",
    "OpacityTarget",
    "DisguiseAsTemplate",
    "DisguisedAsTemplate_EnemyPerspective",
    "DisguiseFX",
    "ForceMountedWhenDisguising",
)
_OPTIONAL_FIELDS = (
    "AwardXPForTriggering",
    "TriggerAttributeModifier",
    "AttributeModifierDuration",
)


class SpecialDisguisePrerequisiteError(ValueError):
    """A disguise prerequisite cannot be sealed without guessing."""


def _digest(value: object) -> str:
    return hashlib.sha256(
        json.dumps(
            value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def _objects(documents: Mapping[str, bytes], source_path: str) -> tuple[SageObject, ...]:
    payload = documents.get(source_path)
    if not isinstance(payload, (bytes, bytearray)):
        raise SpecialDisguisePrerequisiteError(
            f"SpecialDisguiseUpdate source is missing: {source_path}"
        )
    return parse_sage_document(bytes(payload), source_path).objects


def _owner(descriptor: Mapping[str, object], documents: Mapping[str, bytes]) -> SageObject | None:
    evidence = descriptor.get("runtimeModuleEvidence")
    if not isinstance(evidence, list):
        raise SpecialDisguisePrerequisiteError("runtime module evidence is invalid")
    rows = [
        row for row in evidence
        if isinstance(row, Mapping) and row.get("kind") == "SpecialDisguiseUpdate"
    ]
    if not rows:
        return None
    if len(rows) != 1 or rows[0].get("consumed") is not False:
        raise SpecialDisguisePrerequisiteError(
            "SpecialDisguiseUpdate evidence is duplicated or already consumed"
        )
    source_path = str(rows[0].get("sourceIni", ""))
    object_id = str(descriptor.get("objectId", ""))
    matches = [
        row for row in _objects(documents, source_path)
        if row.name.casefold() == object_id.casefold()
    ]
    if len(matches) != 1:
        raise SpecialDisguisePrerequisiteError(
            "SpecialDisguiseUpdate owner does not resolve exactly once"
        )
    return matches[0]


def _blocks(owner: SageObject, kind: str) -> list[SageBlock]:
    return [row for row in owner.blocks if row.kind.casefold() == kind.casefold()]


def _field(block: SageBlock, name: str, *, required: bool = True) -> dict[str, object] | None:
    rows = [row for row in block.assignments if row.key.casefold() == name.casefold()]
    if not rows:
        if required:
            raise SpecialDisguisePrerequisiteError(
                f"{block.kind} is missing required field {name}"
            )
        return None
    if len(rows) != 1:
        raise SpecialDisguisePrerequisiteError(
            f"{block.kind} duplicates field {name}"
        )
    row = rows[0]
    return {
        "authored": row.value.strip(),
        "sourceIni": row.source_virtual_path,
        "line": row.line,
    }


def _token(receipt: Mapping[str, object]) -> str:
    value = str(receipt.get("authored", "")).split()
    if not value:
        raise SpecialDisguisePrerequisiteError("authored identifier is empty")
    return value[0]


def _module_receipt(block: SageBlock, fields: Mapping[str, object]) -> dict[str, object]:
    value = {
        "kind": block.kind,
        "instanceTag": block.instance_tag or "",
        "sourceIni": block.source_virtual_path,
        "line": block.line,
        "fields": deepcopy(dict(fields)),
    }
    value["semanticSha256"] = _digest(value)
    return value


def _source_line_receipts(
    documents: Mapping[str, bytes], tokens: Sequence[str], paths: Sequence[str]
) -> list[dict[str, object]]:
    receipts: list[dict[str, object]] = []
    for token in tokens:
        matches: list[dict[str, object]] = []
        for path in paths:
            payload = documents.get(path)
            if not isinstance(payload, (bytes, bytearray)):
                continue
            for line_number, raw in enumerate(bytes(payload).splitlines(), 1):
                text = raw.decode("cp1252").strip()
                if token.casefold() not in text.casefold():
                    continue
                matches.append({
                    "identifier": token,
                    "sourceIni": path,
                    "line": line_number,
                    "lineSha256": hashlib.sha256(raw).hexdigest(),
                })
        if not matches:
            raise SpecialDisguisePrerequisiteError(
                f"referenced disguise prerequisite is absent: {token}"
            )
        receipts.extend(matches)
    return sorted(
        receipts,
        key=lambda row: (
            str(row["identifier"]).casefold(), str(row["sourceIni"]).casefold(),
            int(row["line"]),
        ),
    )


def special_disguise_visual_targets(
    descriptor: Mapping[str, object], documents: Mapping[str, bytes]
) -> tuple[str, ...]:
    """Return presentation-only Object targets named by the authored module."""

    owner = _owner(descriptor, documents)
    if owner is None:
        return ()
    modules = _blocks(owner, "SpecialDisguiseUpdate")
    if len(modules) != 1:
        raise SpecialDisguisePrerequisiteError(
            "SpecialDisguiseUpdate does not resolve exactly once"
        )
    child = _token(_field(modules[0], "DisguiseAsTemplate"))  # type: ignore[arg-type]
    hostile = _token(
        _field(modules[0], "DisguisedAsTemplate_EnemyPerspective")  # type: ignore[arg-type]
    )
    return tuple(sorted({child, hostile}, key=str.casefold))


def _leaf_role(row: Mapping[str, object], owner_id: str, child_id: str) -> str | None:
    target = str(row.get("targetObject", ""))
    conditions = {str(value).casefold() for value in row.get("conditions", [])}
    if target.casefold() == child_id.casefold():
        return "non-owner-disguised-presentation"
    if target.casefold() != owner_id.casefold():
        return None
    if "disguised" in conditions:
        return "owner-disguised-presentation"
    if "mounted" in conditions:
        return "owner-mounted-presentation"
    if row.get("kind") == "model" and not conditions:
        return "owner-base-presentation"
    return None


def _audio_definitions(documents: Mapping[str, bytes]) -> SageAudioDefinitions:
    parsed = []
    for path in ("data/ini/soundeffects.ini", "data/ini/voice.ini"):
        payload = documents.get(path)
        if not isinstance(payload, (bytes, bytearray)):
            raise SpecialDisguisePrerequisiteError(
                f"disguise audio definition source is missing: {path}"
            )
        parsed.append(parse_sage_audio_definitions(bytes(payload)))
    merged = SageAudioDefinitions(
        tuple(item for group in parsed for item in group.events),
        tuple(item for group in parsed for item in group.multisounds),
        tuple(item for group in parsed for item in group.tracks),
    )
    # ``resolve_sage_audio_closure`` applies the engine's effective last-wins
    # identity rule. RotWK intentionally repeats unrelated legacy events across
    # the two global tables, so rejecting the whole corpus for an unreachable
    # duplicate would be stricter than retail and would hide the exact selected
    # Eowyn event receipt.
    return merged


def build_special_disguise_prerequisite(
    descriptor: Mapping[str, object],
    documents: Mapping[str, bytes],
    visual_closure: Mapping[str, object],
    *,
    game: str,
    texture_index: Mapping[str, str],
    effective_root: Path | str,
) -> dict[str, object] | None:
    """Seal resources and evidence while keeping disguise runtime-deferred."""

    owner = _owner(descriptor, documents)
    if owner is None:
        return None
    game_key = game.strip().casefold()
    if game_key not in {"bfme2", "rotwk"}:
        raise SpecialDisguisePrerequisiteError("disguise edition is invalid")
    # The faction compiler's bounded Object document view intentionally omits
    # global particle/audio/script tables.  Read only the exact manifest-bound
    # prerequisite documents from the already-selected effective root.
    effective_path = Path(effective_root).expanduser().resolve()
    prerequisite_documents = dict(documents)
    for path in (
        "data/ini/fxparticlesystem.ini",
        "data/ini/particlesystem.ini",
        "data/ini/soundeffects.ini",
        "data/ini/voice.ini",
        "data/scripts/scripts.lua",
        "data/scripts/scriptevents.xml",
    ):
        source = effective_path / PurePosixPath(path)
        if source.is_file():
            prerequisite_documents[path] = source.read_bytes()
    modules = _blocks(owner, "SpecialDisguiseUpdate")
    if len(modules) != 1:
        raise SpecialDisguisePrerequisiteError(
            "SpecialDisguiseUpdate does not resolve exactly once"
        )
    module = modules[0]
    fields: dict[str, object] = {
        name: _field(module, name) for name in _REQUIRED_FIELDS
    }
    for name in _OPTIONAL_FIELDS:
        receipt = _field(module, name, required=False)
        if receipt is not None:
            fields[name] = receipt
    modifier = fields.get("TriggerAttributeModifier")
    duration = fields.get("AttributeModifierDuration")
    if (modifier is None) != (duration is None):
        raise SpecialDisguisePrerequisiteError(
            "disguise attribute modifier and duration must be authored together"
        )
    # Canonical RotWK 2.01 has the same no-modifier row as BFME2. The
    # quarantined Unofficial-2.02 overlay authors Rider2Tracker/2000 and .9.
    # Preserve either exact source shape, but never infer it from the edition.

    template = _token(fields["SpecialPowerTemplate"])  # type: ignore[arg-type]
    paired = []
    for block in _blocks(owner, "SpecialPowerModule"):
        receipt = _field(block, "SpecialPowerTemplate", required=False)
        if receipt is not None and _token(receipt).casefold() == template.casefold():
            paired.append(block)
    if len(paired) != 1:
        raise SpecialDisguisePrerequisiteError("paired disguise SpecialPowerModule is ambiguous")
    initiate = _field(paired[0], "InitiateFX")
    unpause = []
    for block in _blocks(owner, "UnpauseSpecialPowerUpgrade"):
        receipt = _field(block, "SpecialPowerTemplate", required=False)
        if receipt is not None and _token(receipt).casefold() == template.casefold():
            unpause.append(block)
    if len(unpause) != 1:
        raise SpecialDisguisePrerequisiteError("paired disguise unpause module is ambiguous")
    trigger_upgrade = _field(unpause[0], "TriggeredBy")
    dismount = [
        block for block in _blocks(owner, "ToggleMountedSpecialAbilityUpdate")
        if (_field(block, "CancelDisguiseWhenDismounting", required=False) or {}).get("authored", "").casefold() == "yes"
    ]
    if not dismount:
        raise SpecialDisguisePrerequisiteError("disguise has no authored dismount cancellation")

    child_id = _token(fields["DisguiseAsTemplate"])  # type: ignore[arg-type]
    hostile_id = _token(fields["DisguisedAsTemplate_EnemyPerspective"])  # type: ignore[arg-type]
    target_rows = visual_closure.get("targets")
    exact_rows = visual_closure.get("exactLeaves")
    if not isinstance(target_rows, list) or not isinstance(exact_rows, list):
        raise SpecialDisguisePrerequisiteError("disguise visual closure is invalid")
    resolved_targets = {
        str(row.get("name", "")).casefold()
        for row in target_rows if isinstance(row, Mapping) and row.get("status") == "resolved"
    }
    for required_target in (str(descriptor["objectId"]), child_id, hostile_id):
        if required_target.casefold() not in resolved_targets:
            raise SpecialDisguisePrerequisiteError(
                f"disguise presentation target is unresolved: {required_target}"
            )
    leaf_bindings = []
    for row in exact_rows:
        if not isinstance(row, Mapping) or row.get("kind") not in {"model", "animation"}:
            continue
        role = _leaf_role(row, str(descriptor["objectId"]), child_id)
        if role is None:
            continue
        physical = row.get("physicalVirtualPaths")
        if not isinstance(physical, list) or not physical:
            raise SpecialDisguisePrerequisiteError("disguise visual leaf has no physical source")
        binding = {
            "role": role,
            "kind": row.get("kind"),
            "identifier": row.get("identifier"),
            "conditions": deepcopy(row.get("conditions", [])),
            "physicalVirtualPaths": deepcopy(physical),
            "provenance": deepcopy(row.get("provenance")),
            "leafSha256": _digest(row),
        }
        binding["bindingSha256"] = _digest(binding)
        leaf_bindings.append(binding)
    roles = {str(row["role"]) for row in leaf_bindings}
    required_roles = {
        "owner-base-presentation", "owner-mounted-presentation",
        "owner-disguised-presentation", "non-owner-disguised-presentation",
    }
    if not required_roles <= roles:
        raise SpecialDisguisePrerequisiteError(
            "disguise visual forms are incomplete: " + ", ".join(sorted(required_roles - roles))
        )
    leaf_bindings.sort(key=lambda row: (
        str(row["role"]), str(row["kind"]), str(row["identifier"]).casefold(),
        tuple(str(value).casefold() for value in row["conditions"]),
    ))

    fx_ids = [_token(initiate), _token(fields["DisguiseFX"])]  # type: ignore[arg-type]
    critical_blocks = [
        block for block in _blocks(owner, "SpecialPowerModule")
        if _field(block, "TriggerFX", required=False) is not None
        and _field(block, "SpecialPowerTemplate", required=False) is not None
        and _token(_field(block, "SpecialPowerTemplate"))  # type: ignore[arg-type]
        .casefold() == "specialabilitydisguisecancel"
    ]
    # The optional 2.02 layer adds a SpecialAbilityDisguiseCancel trigger and
    # FX_EowynCriticalStrike; canonical RotWK 2.01 does not.  Bind it only
    # when the selected source lineage authors it.  Edition identity alone is
    # not evidence that the extra effect exists.
    if len(critical_blocks) > 1:
        raise SpecialDisguisePrerequisiteError(
            "disguise cancel FX module is ambiguous"
        )
    if critical_blocks:
        fx_ids.append(_token(_field(critical_blocks[0], "TriggerFX")))  # type: ignore[arg-type]
    fx_ids = sorted(set(fx_ids), key=str.casefold)
    particles = build_ability_fx_closure(
        prerequisite_documents, fx_ids, namespace=str(descriptor["objectId"]),
        texture_index=texture_index,
    )
    bindings = particles.get("runtimeBindings")
    if (
        not isinstance(bindings, Mapping)
        or bindings.get("unresolved") != []
    ):
        raise SpecialDisguisePrerequisiteError("disguise FX/particle closure is incomplete")
    fx_rows = bindings.get("fxLists")
    if (
        not isinstance(fx_rows, list)
        or sorted(
            (str(row.get("fxListId", "")) for row in fx_rows if isinstance(row, Mapping)),
            key=str.casefold,
        ) != fx_ids
    ):
        raise SpecialDisguisePrerequisiteError("disguise FX bindings are invalid")
    audio_ids = sorted({
        str(value) for row in fx_rows if isinstance(row, Mapping)
        for value in row.get("audioEventIds", [])
    }, key=str.casefold)
    audio_closure = resolve_sage_audio_closure(
        _audio_definitions(prerequisite_documents), audio_ids
    ).neutral()
    sample_index = build_audio_sample_index(effective_root)
    audio_resources = []
    audio_outputs: dict[str, str] = {}
    owner_slug = str(descriptor["objectId"]).casefold()
    for sample_id in audio_closure["sampleIds"]:
        source = sample_index.get(str(sample_id).casefold())
        if source is None:
            raise SpecialDisguisePrerequisiteError(
                f"disguise audio sample is unresolved: {sample_id}"
            )
        source = "/".join(safe_relative_parts(source))
        fingerprint = hashlib.sha256(source.casefold().encode()).hexdigest()[:16]
        resource_id = f"unit-{owner_slug}-disguise-audio-{fingerprint}"
        output = f"assets/audio/units/{owner_slug}/disguise/{fingerprint}.wav"
        audio_resources.append({
            "id": resource_id, "kind": "audio", "converter": "audio",
            "patterns": [source], "output": output,
            "options": {"force_pcm": True}, "required": True,
            "limit": 1, "expected_count": 1,
        })
        audio_outputs[str(sample_id)] = output

    reference_tokens = [template, child_id, hostile_id, _token(trigger_upgrade)]
    if modifier is not None:
        reference_tokens.append(_token(modifier))  # type: ignore[arg-type]
        reference_tokens.extend([
            "Upgrade_EowynConditionDisguised",
            "Upgrade_EowynConditionStealthed",
            "Upgrade_EowynConditionNotStealthed",
            "OnDisguised", "OnDisguiseCanceled", "OnDisguiseAttackEnemy",
        ])
    reference_receipts = _source_line_receipts(
        prerequisite_documents, reference_tokens,
        (
            "data/ini/specialpower.ini", "data/ini/upgrade.ini",
            "data/ini/attributemodifier.ini", "data/scripts/scripts.lua",
            "data/scripts/scriptevents.xml",
            module.source_virtual_path,
        ),
    )
    closure: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "edition": game_key,
        "objectId": descriptor["objectId"],
        "descriptorSha256": descriptor["descriptorSha256"],
        "runtimeStatus": RUNTIME_STATUS,
        "presentationOnly": True,
        "authoritativeEntityRegistration": False,
        "moduleReceipt": _module_receipt(module, fields),
        "pairedModuleReceipts": [
            _module_receipt(paired[0], {
                "SpecialPowerTemplate": _field(paired[0], "SpecialPowerTemplate"),
                "InitiateFX": initiate,
            }),
            _module_receipt(unpause[0], {
                "SpecialPowerTemplate": _field(unpause[0], "SpecialPowerTemplate"),
                "TriggeredBy": trigger_upgrade,
            }),
            *[
                _module_receipt(block, {
                    "CancelDisguiseWhenDismounting": _field(
                        block, "CancelDisguiseWhenDismounting"
                    )
                }) for block in dismount
            ],
        ],
        "presentationIdentities": {
            "ownerObjectId": descriptor["objectId"],
            "nonOwnerDisguiseTemplateId": child_id,
            "hostilePerspectiveTemplateId": hostile_id,
        },
        "visualLeafBindings": leaf_bindings,
        "requiredVisualLeafSha256s": sorted(
            {str(row["leafSha256"]) for row in leaf_bindings}
        ),
        "fxListIds": fx_ids,
        "particleClosure": particles,
        "audioClosure": audio_closure,
        "audioSampleOutputs": audio_outputs,
        "referenceReceipts": reference_receipts,
        "resources": [*deepcopy(particles["resources"]), *audio_resources],
    }
    closure["aggregateSha256"] = _digest(closure)
    validate_special_disguise_prerequisite(closure)
    return closure


def validate_special_disguise_prerequisite(value: Mapping[str, object]) -> None:
    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise SpecialDisguisePrerequisiteError("disguise prerequisite identity is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("aggregateSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise SpecialDisguisePrerequisiteError("disguise prerequisite digest is invalid")
    if (
        value.get("edition") not in {"bfme2", "rotwk"}
        or value.get("runtimeStatus") != RUNTIME_STATUS
        or value.get("presentationOnly") is not True
        or value.get("authoritativeEntityRegistration") is not False
    ):
        raise SpecialDisguisePrerequisiteError("disguise prerequisite activation guard drifted")
    identities = value.get("presentationIdentities")
    if (
        not isinstance(identities, Mapping)
        or identities.get("ownerObjectId") != value.get("objectId")
        or not isinstance(identities.get("nonOwnerDisguiseTemplateId"), str)
        or not isinstance(identities.get("hostilePerspectiveTemplateId"), str)
    ):
        raise SpecialDisguisePrerequisiteError("disguise presentation identities are invalid")
    leaves = value.get("visualLeafBindings")
    declared_leaf_digests = value.get("requiredVisualLeafSha256s")
    if (
        not isinstance(leaves, list) or not leaves
        or not isinstance(declared_leaf_digests, list)
        or sorted({str(row.get("leafSha256", "")) for row in leaves if isinstance(row, Mapping)})
        != declared_leaf_digests
    ):
        raise SpecialDisguisePrerequisiteError("disguise visual bindings are invalid")
    for leaf in leaves:
        if not isinstance(leaf, Mapping):
            raise SpecialDisguisePrerequisiteError("disguise visual binding is invalid")
        unsigned_leaf = dict(leaf)
        binding_digest = unsigned_leaf.pop("bindingSha256", None)
        if (
            not isinstance(binding_digest, str)
            or binding_digest != _digest(unsigned_leaf)
            or not isinstance(leaf.get("leafSha256"), str)
            or len(str(leaf["leafSha256"])) != 64
        ):
            raise SpecialDisguisePrerequisiteError(
                "disguise visual binding digest is invalid"
            )
    roles = {str(row.get("role", "")) for row in leaves if isinstance(row, Mapping)}
    if not {
        "owner-base-presentation", "owner-mounted-presentation",
        "owner-disguised-presentation", "non-owner-disguised-presentation",
    } <= roles:
        raise SpecialDisguisePrerequisiteError("disguise visual roles are incomplete")
    particles = value.get("particleClosure")
    if not isinstance(particles, Mapping):
        raise SpecialDisguisePrerequisiteError("disguise particle closure is invalid")
    validate_ability_fx_closure(particles)
    resources = value.get("resources")
    if (
        not isinstance(resources, list)
        or resources[:len(particles.get("resources", []))] != particles.get("resources")
    ):
        raise SpecialDisguisePrerequisiteError("disguise resources drifted")
    audio = value.get("audioClosure")
    outputs = value.get("audioSampleOutputs")
    if (
        not isinstance(audio, Mapping) or not isinstance(outputs, Mapping)
        or set(outputs) != set(audio.get("sampleIds", []))
        or not audio.get("rootIds")
    ):
        raise SpecialDisguisePrerequisiteError("disguise audio closure is incomplete")
    module = value.get("moduleReceipt")
    fields = module.get("fields") if isinstance(module, Mapping) else None
    if (
        not isinstance(fields, Mapping)
        or _token(fields.get("DisguiseAsTemplate", {}))
        != identities.get("nonOwnerDisguiseTemplateId")
        or _token(fields.get("DisguisedAsTemplate_EnemyPerspective", {}))
        != identities.get("hostilePerspectiveTemplateId")
    ):
        raise SpecialDisguisePrerequisiteError("disguise module identity binding drifted")


__all__ = [
    "RUNTIME_STATUS", "SCHEMA", "SCHEMA_VERSION",
    "SpecialDisguisePrerequisiteError",
    "build_special_disguise_prerequisite",
    "special_disguise_visual_targets",
    "validate_special_disguise_prerequisite",
]
