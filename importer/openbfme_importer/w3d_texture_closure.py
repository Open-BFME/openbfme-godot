"""Fail-closed W3D-to-native-texture closure planning.

The planner joins four already-produced evidence surfaces without opening or
copying a file: a :class:`W3DCatalogReport`, the exact W3D staged-input
mapping, a sealed effective-assets manifest, and a sealed texture-only native
corpus manifest.  The returned private plan contains the relative copy paths
needed by the pinned Blender plugin.  Its separate ``neutral`` surface omits
authored identifiers and all private paths.

This module deliberately proves neither that a copy happened nor that a GLB
or rendered frame is correct.  A model with any unresolved dependency is
retained with terminal reason codes and contributes no copy instruction.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
import hashlib
import json
from pathlib import PurePosixPath
import re
from typing import Any, Iterable, Mapping

from .paths import safe_relative_parts
from .w3d_catalog import W3DCatalogReport, W3DCatalogSource, catalog_source_id
from .w3d_metadata import W3DMetadata


EFFECTIVE_MANIFEST_SCHEMA = "openbfme.effective-assets-manifest"
EFFECTIVE_MANIFEST_VERSION = 0
NATIVE_TEXTURE_MANIFEST_SCHEMA = "openbfme.native-corpus"
NATIVE_TEXTURE_MANIFEST_VERSION = 0
TEXTURE_CLOSURE_SCHEMA = "openbfme.w3d-texture-closure-evidence"
TEXTURE_CLOSURE_VERSION = 1
TEXTURE_FORCED_TERMINAL_SCHEMA = "openbfme.w3d-texture-forced-terminals"
TEXTURE_FORCED_TERMINAL_VERSION = 0
TEXTURE_FORCED_TERMINAL_REASON_PREFIX = "texture-closure"
TEXTURE_DEPENDENCY_POLICY = (
    "opensage-blender-plugin-2de84023cb632a79a853b2a52f97c8002ed85142-texture-loads-v1"
)

MAX_DOCUMENT_BYTES = 64 * 1024 * 1024
MAX_MANIFEST_FILES = 250_000
MAX_MANIFEST_BYTES = 64 * 1024 * 1024 * 1024
MAX_STAGED_PATH_LENGTH = 512

_SHA256_CHARACTERS = frozenset("0123456789abcdef")
_SOURCE_ID = re.compile(r"^src-[0-9a-f]{32}$")
_REASON_CODE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
_COPY_INSTRUCTION_ID = re.compile(r"^texcopy-[0-9a-f]{40}$")
_WINDOWS_FORBIDDEN_FILENAME_CHARACTERS = frozenset('<>"|?*')
_TEXTURE_EXTENSIONS = frozenset(
    {".bmp", ".dds", ".jpeg", ".jpg", ".pcx", ".png", ".tga"}
)
_CONDITIONAL_SHADER_TEXTURE_PROPERTIES = frozenset(
    {"DiffuseTexture", "NormalMap", "SpecMap"}
)
_UNCONDITIONAL_SHADER_TEXTURE_PROPERTIES = frozenset(
    {"DamagedTexture", "Texture_0", "Texture_1"}
)
_PINNED_DAZZLE_TEXTURE = "SunDazzle.tga"


class W3DTextureClosureError(ValueError):
    """Raised when the four evidence inputs cannot be coherently joined."""


@dataclass(frozen=True, slots=True)
class W3DTextureCopyInstruction:
    """One deduplicated private native-PNG copy instruction."""

    instruction_id: str
    source_output_path: str
    source_output_sha256: str
    source_output_bytes: int
    destination_path: str
    model_source_ids: tuple[str, ...]
    reference_evidence_sha256s: tuple[str, ...]
    definition_sha256: str

    def private(self) -> dict[str, object]:
        return {
            "instructionId": self.instruction_id,
            "sourceOutputPath": self.source_output_path,
            "sourceOutputSha256": self.source_output_sha256,
            "sourceOutputBytes": self.source_output_bytes,
            "destinationPath": self.destination_path,
            "modelSourceIds": list(self.model_source_ids),
            "referenceEvidenceSha256s": list(self.reference_evidence_sha256s),
            "definitionSha256": self.definition_sha256,
        }

    def neutral(self) -> dict[str, object]:
        return {
            "instructionId": self.instruction_id,
            "sourceOutputSha256": self.source_output_sha256,
            "sourceOutputBytes": self.source_output_bytes,
            "destinationIdentitySha256": _canonical_sha256(
                {"destinationPath": self.destination_path}
            ),
            "modelSourceIds": list(self.model_source_ids),
            "referenceEvidenceSha256s": list(self.reference_evidence_sha256s),
            "definitionSha256": self.definition_sha256,
        }


@dataclass(frozen=True, slots=True)
class W3DModelTextureClosure:
    """Texture accounting for one physical W3D model source."""

    model_source_id: str
    model_source_sha256: str
    staged_model_path: str
    texture_reference_count: int
    resolved_reference_count: int
    instruction_ids: tuple[str, ...]
    reference_evidence_sha256s: tuple[str, ...]
    terminal_reasons: tuple[str, ...]

    @property
    def texture_free(self) -> bool:
        return self.texture_reference_count == 0

    @property
    def complete(self) -> bool:
        return not self.terminal_reasons

    def private(self) -> dict[str, object]:
        return {
            **self.neutral(),
            "stagedModelPath": self.staged_model_path,
        }

    def neutral(self) -> dict[str, object]:
        return {
            "modelSourceId": self.model_source_id,
            "modelSourceSha256": self.model_source_sha256,
            "textureReferenceCount": self.texture_reference_count,
            "resolvedReferenceCount": self.resolved_reference_count,
            "textureFree": self.texture_free,
            "instructionIds": list(self.instruction_ids),
            "referenceEvidenceSha256s": list(self.reference_evidence_sha256s),
            "terminalReasons": list(self.terminal_reasons),
            "complete": self.complete,
        }


@dataclass(frozen=True, slots=True)
class W3DTextureClosurePlan:
    """Immutable private plan and its payload-free evidence seal."""

    models: tuple[W3DModelTextureClosure, ...]
    copy_instructions: tuple[W3DTextureCopyInstruction, ...]
    catalog_input_sha256: str
    catalog_metadata_sha256: str
    effective_manifest_sha256: str
    effective_manifest_aggregate_sha256: str
    native_manifest_sha256: str
    native_texture_identity_sha256: str
    edition_seal: str | None
    source_seal: str | None
    private_plan_sha256: str
    evidence_sha256: str

    @property
    def complete(self) -> bool:
        return all(model.complete for model in self.models)

    @property
    def texture_free_model_count(self) -> int:
        return sum(model.texture_free for model in self.models)

    @property
    def terminal_model_count(self) -> int:
        return sum(not model.complete for model in self.models)

    def _bindings(self) -> dict[str, object]:
        result: dict[str, object] = {
            "catalogInputSha256": self.catalog_input_sha256,
            "catalogMetadataSha256": self.catalog_metadata_sha256,
            "effectiveManifestSha256": self.effective_manifest_sha256,
            "effectiveManifestAggregateSha256": (
                self.effective_manifest_aggregate_sha256
            ),
            "nativeManifestSha256": self.native_manifest_sha256,
            "nativeTextureIdentitySha256": self.native_texture_identity_sha256,
            "textureDependencyPolicy": TEXTURE_DEPENDENCY_POLICY,
        }
        if self.edition_seal is not None:
            result["editionSeal"] = self.edition_seal
        if self.source_seal is not None:
            result["sourceSeal"] = self.source_seal
        return result

    def private_hash_basis(self) -> dict[str, object]:
        return {
            "schema": "openbfme.w3d-texture-closure-private-plan",
            "schemaVersion": TEXTURE_CLOSURE_VERSION,
            "bindings": self._bindings(),
            "models": [model.private() for model in self.models],
            "copyInstructions": [item.private() for item in self.copy_instructions],
            "claims": {
                "filesCopied": False,
                "glbConversionComplete": False,
                "renderParityProven": False,
            },
        }

    def _neutral(self, *, include_evidence_hash: bool) -> dict[str, object]:
        hashes = {**self._bindings(), "privatePlanSha256": self.private_plan_sha256}
        if include_evidence_hash:
            hashes["evidenceSha256"] = self.evidence_sha256
        return {
            "schema": TEXTURE_CLOSURE_SCHEMA,
            "schemaVersion": TEXTURE_CLOSURE_VERSION,
            "summary": {
                "modelCount": len(self.models),
                "textureFreeModelCount": self.texture_free_model_count,
                "terminalModelCount": self.terminal_model_count,
                "copyInstructionCount": len(self.copy_instructions),
                "textureClosureComplete": self.complete,
                "filesCopied": False,
                "glbConversionComplete": False,
                "renderParityProven": False,
            },
            "hashes": hashes,
            "models": [model.neutral() for model in self.models],
            "copyInstructions": [item.neutral() for item in self.copy_instructions],
        }

    def evidence_hash_basis(self) -> dict[str, object]:
        return self._neutral(include_evidence_hash=False)

    def neutral(self) -> dict[str, object]:
        return self._neutral(include_evidence_hash=True)


def _validate_model_evidence_for_forced_terminals(
    models: object,
) -> tuple[W3DModelTextureClosure, ...]:
    if not isinstance(models, tuple):
        raise W3DTextureClosureError("texture plan model evidence must be a tuple")
    validated: list[W3DModelTextureClosure] = []
    source_ids: list[str] = []
    for model in models:
        if not isinstance(model, W3DModelTextureClosure):
            raise W3DTextureClosureError("texture plan model evidence is malformed")
        if _SOURCE_ID.fullmatch(model.model_source_id) is None:
            raise W3DTextureClosureError(
                "texture plan model source ID is not canonical"
            )
        if not _is_sha256(model.model_source_sha256):
            raise W3DTextureClosureError(
                "texture plan model source SHA-256 is not canonical"
            )
        if (
            not isinstance(model.staged_model_path, str)
            or len(model.staged_model_path) > MAX_STAGED_PATH_LENGTH
            or PurePosixPath(
                _safe_canonical_path(
                    model.staged_model_path, "texture plan staged model"
                )
            ).suffix.casefold()
            != ".w3d"
        ):
            raise W3DTextureClosureError(
                "texture plan staged model path is not canonical"
            )
        if (
            not _is_int(model.texture_reference_count)
            or not _is_int(model.resolved_reference_count)
            or model.resolved_reference_count > model.texture_reference_count
        ):
            raise W3DTextureClosureError("texture plan model counts are invalid")
        if (
            not isinstance(model.instruction_ids, tuple)
            or model.instruction_ids != tuple(sorted(set(model.instruction_ids)))
            or any(
                not isinstance(value, str)
                or _COPY_INSTRUCTION_ID.fullmatch(value) is None
                for value in model.instruction_ids
            )
        ):
            raise W3DTextureClosureError(
                "texture plan model instruction evidence is not canonical"
            )
        if (
            not isinstance(model.reference_evidence_sha256s, tuple)
            or model.reference_evidence_sha256s
            != tuple(sorted(set(model.reference_evidence_sha256s)))
            or any(not _is_sha256(value) for value in model.reference_evidence_sha256s)
        ):
            raise W3DTextureClosureError(
                "texture plan model reference evidence is not canonical"
            )
        reasons = model.terminal_reasons
        if (
            not isinstance(reasons, tuple)
            or reasons != tuple(sorted(set(reasons)))
            or any(
                not isinstance(reason, str) or _REASON_CODE.fullmatch(reason) is None
                for reason in reasons
            )
        ):
            raise W3DTextureClosureError(
                "texture plan model terminal reasons are not canonical"
            )
        if reasons and model.instruction_ids:
            raise W3DTextureClosureError(
                "terminal texture model retains copy instructions"
            )
        if not reasons and (
            model.resolved_reference_count != model.texture_reference_count
        ):
            raise W3DTextureClosureError(
                "complete texture model has unresolved references"
            )
        source_ids.append(model.model_source_id)
        validated.append(model)
    if source_ids != sorted(source_ids):
        raise W3DTextureClosureError(
            "texture plan model evidence is not canonically ordered"
        )
    if len(set(source_ids)) != len(source_ids):
        raise W3DTextureClosureError("texture plan repeats model source evidence")
    return tuple(validated)


def texture_closure_forced_terminals(
    plan: W3DTextureClosurePlan,
) -> tuple[dict[str, tuple[str, ...]], str]:
    """Bridge texture terminals into :func:`plan_w3d_batches` arguments.

    Every texture reason is retained with a domain prefix, preventing it from
    being mistaken for a planner-native terminal.  The returned seal binds the
    complete neutral texture evidence (including its private-plan identity)
    and the exact canonical mapping.  Even a complete plan therefore produces
    a nonempty attestation seal alongside its empty mapping.
    """

    if not isinstance(plan, W3DTextureClosurePlan):
        raise TypeError("texture forced-terminal bridge requires a texture plan")
    models = _validate_model_evidence_for_forced_terminals(plan.models)
    required_hashes = (
        plan.catalog_input_sha256,
        plan.catalog_metadata_sha256,
        plan.effective_manifest_sha256,
        plan.effective_manifest_aggregate_sha256,
        plan.native_manifest_sha256,
        plan.native_texture_identity_sha256,
        plan.private_plan_sha256,
        plan.evidence_sha256,
    )
    if any(not _is_sha256(value) for value in required_hashes) or any(
        value is not None and not _is_sha256(value)
        for value in (plan.edition_seal, plan.source_seal)
    ):
        raise W3DTextureClosureError("texture plan hashes are not canonical")
    try:
        expected_private = _canonical_sha256(plan.private_hash_basis())
        expected_evidence = _canonical_sha256(plan.evidence_hash_basis())
    except (AttributeError, TypeError, ValueError) as exc:
        raise W3DTextureClosureError("texture plan evidence is malformed") from exc
    if plan.private_plan_sha256 != expected_private:
        raise W3DTextureClosureError("texture private-plan SHA-256 is invalid")
    if plan.evidence_sha256 != expected_evidence:
        raise W3DTextureClosureError("texture evidence SHA-256 is invalid")

    mapping: dict[str, tuple[str, ...]] = {}
    rows: list[dict[str, object]] = []
    for model in models:
        if not model.terminal_reasons:
            continue
        reasons = tuple(
            f"{TEXTURE_FORCED_TERMINAL_REASON_PREFIX}-{reason}"
            for reason in model.terminal_reasons
        )
        if not reasons or any(
            _REASON_CODE.fullmatch(reason) is None for reason in reasons
        ):
            raise W3DTextureClosureError(
                "derived texture forced-terminal reasons are invalid"
            )
        mapping[model.model_source_id] = reasons
        rows.append(
            {
                "modelSourceId": model.model_source_id,
                "reasonCodes": list(reasons),
            }
        )
    basis = {
        "schema": TEXTURE_FORCED_TERMINAL_SCHEMA,
        "schemaVersion": TEXTURE_FORCED_TERMINAL_VERSION,
        "texturePlan": plan.neutral(),
        "forcedTerminalReasons": rows,
    }
    return mapping, _canonical_sha256(basis)


@dataclass(frozen=True, slots=True)
class _EffectiveFile:
    path: str
    archive: str
    size: int
    sha256: str


@dataclass(frozen=True, slots=True)
class _EffectiveManifest:
    sha256: str
    aggregate_sha256: str
    files: tuple[_EffectiveFile, ...]


@dataclass(frozen=True, slots=True)
class _NativeEntry:
    source_path: str
    source_sha256: str
    source_bytes: int
    output_path: str
    output_sha256: str
    output_bytes: int


@dataclass(frozen=True, slots=True)
class _NativeManifest:
    sha256: str
    identity_sha256: str
    entries: tuple[_NativeEntry, ...]
    reclassified_paths: frozenset[str]


@dataclass(frozen=True, slots=True)
class _Reference:
    normalized: str
    stem: str
    evidence_sha256: str


@dataclass(frozen=True, slots=True)
class _TextureDependency:
    identifier: object
    evidence_sha256: str | None


@dataclass(frozen=True, slots=True)
class _Requirement:
    model_source_id: str
    reference_evidence_sha256: str
    entry: _NativeEntry
    destination_path: str


def _canonicalize_requirement_directories(
    requirements: dict[str, list[_Requirement]],
) -> None:
    """Give case-alias directory references one deterministic physical spelling."""

    component_spellings: dict[tuple[str, ...], set[str]] = {}
    for items in requirements.values():
        for item in items:
            parts = PurePosixPath(item.destination_path).parts
            for index, component in enumerate(parts[:-1]):
                key = tuple(part.casefold() for part in parts[: index + 1])
                component_spellings.setdefault(key, set()).add(component)
    canonical_components = {
        key: min(values, key=lambda value: (value.casefold(), value))
        for key, values in component_spellings.items()
    }
    for model_id, items in requirements.items():
        canonical: list[_Requirement] = []
        for item in items:
            parts = PurePosixPath(item.destination_path).parts
            directory = [
                canonical_components[
                    tuple(part.casefold() for part in parts[: index + 1])
                ]
                for index in range(len(parts) - 1)
            ]
            destination = PurePosixPath(*directory, parts[-1]).as_posix()
            canonical.append(replace(item, destination_path=destination))
        requirements[model_id] = canonical


@dataclass(frozen=True, slots=True)
class _TextureResolutionIndex:
    """Case-folded lookup tables preserving the manifest's canonical order."""

    exact: dict[str, _EffectiveFile]
    path_stems: dict[str, tuple[_EffectiveFile, ...]]
    leaves: dict[str, tuple[_EffectiveFile, ...]]
    stems: dict[str, tuple[_EffectiveFile, ...]]


class _DuplicateJsonKey(ValueError):
    pass


def _canonical_json_bytes(
    value: object, *, pretty: bool = False, ensure_ascii: bool = True
) -> bytes:
    options: dict[str, object] = {
        "ensure_ascii": ensure_ascii,
        "allow_nan": False,
        "sort_keys": True,
    }
    if pretty:
        options["indent"] = 2
    else:
        options["separators"] = (",", ":")
    return (json.dumps(value, **options) + "\n").encode("utf-8")


def _canonical_sha256(value: object) -> str:
    return hashlib.sha256(_canonical_json_bytes(value)).hexdigest()


def _catalog_sha256(value: object) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    ).hexdigest()


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.casefold()
        and all(character in _SHA256_CHARACTERS for character in value)
    )


def _is_int(value: object, *, minimum: int = 0) -> bool:
    return not isinstance(value, bool) and isinstance(value, int) and value >= minimum


def _object_without_duplicate_keys(
    pairs: Iterable[tuple[str, object]],
) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateJsonKey(key)
        result[key] = value
    return result


def _reject_constant(value: str) -> object:
    raise ValueError(f"non-finite JSON constant is forbidden: {value}")


def _document(
    value: Mapping[str, object] | bytes, label: str, *, ensure_ascii: bool
) -> tuple[dict[str, Any], bytes]:
    if isinstance(value, bytes):
        if not 1 <= len(value) <= MAX_DOCUMENT_BYTES:
            raise W3DTextureClosureError(f"{label} exceeds its size bound")
        try:
            parsed = json.loads(
                value.decode("utf-8"),
                object_pairs_hook=_object_without_duplicate_keys,
                parse_constant=_reject_constant,
            )
        except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
            raise W3DTextureClosureError(f"{label} is invalid JSON") from exc
        if not isinstance(parsed, dict):
            raise W3DTextureClosureError(f"{label} root must be an object")
        canonical = _canonical_json_bytes(
            parsed, pretty=True, ensure_ascii=ensure_ascii
        )
        if value != canonical:
            raise W3DTextureClosureError(f"{label} encoding is not canonical")
        return parsed, canonical
    if not isinstance(value, Mapping):
        raise TypeError(f"{label} must be a mapping or canonical JSON bytes")
    try:
        raw = _canonical_json_bytes(value, pretty=True, ensure_ascii=ensure_ascii)
        parsed = json.loads(raw.decode("utf-8"))
    except (TypeError, ValueError) as exc:
        raise W3DTextureClosureError(f"{label} is not canonical JSON data") from exc
    if len(raw) > MAX_DOCUMENT_BYTES or not isinstance(parsed, dict):
        raise W3DTextureClosureError(f"{label} exceeds its size bound")
    return parsed, raw


def _safe_canonical_path(value: object, label: str) -> str:
    if not isinstance(value, str):
        raise W3DTextureClosureError(f"{label} path is invalid")
    try:
        parts = safe_relative_parts(value)
        canonical = "/".join(parts)
    except (TypeError, ValueError) as exc:
        raise W3DTextureClosureError(f"{label} path is unsafe") from exc
    if (
        canonical != value
        or "\\" in value
        or any(
            character in _WINDOWS_FORBIDDEN_FILENAME_CHARACTERS
            for part in parts
            for character in part
        )
    ):
        raise W3DTextureClosureError(f"{label} path is not canonical")
    return value


def _case_unique_canonical(paths: list[str], label: str) -> None:
    if paths != sorted(paths, key=lambda item: (item.casefold(), item)):
        raise W3DTextureClosureError(f"{label} inventory is not canonical")
    if len({item.casefold() for item in paths}) != len(paths):
        raise W3DTextureClosureError(f"{label} inventory case-collides")
    folded = {item.casefold() for item in paths}
    for path in folded:
        parts = path.split("/")
        if any("/".join(parts[:index]) in folded for index in range(1, len(parts))):
            raise W3DTextureClosureError(
                f"{label} inventory has a file/directory collision"
            )


def _effective_aggregate(files: Iterable[_EffectiveFile]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(item.path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item.size).encode("ascii"))
        digest.update(b"\0")
        digest.update(item.sha256.encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _validate_effective_manifest(
    value: Mapping[str, object] | bytes,
    expected_seal: str | None,
) -> _EffectiveManifest:
    document, raw = _document(value, "effective-assets manifest", ensure_ascii=False)
    if set(document) != {
        "aggregate_sha256",
        "catalog",
        "files",
        "install",
        "schema",
        "schema_version",
        "totals",
    }:
        raise W3DTextureClosureError("effective-assets manifest shape is invalid")
    if (
        document.get("schema") != EFFECTIVE_MANIFEST_SCHEMA
        or document.get("schema_version") != EFFECTIVE_MANIFEST_VERSION
        or isinstance(document.get("schema_version"), bool)
    ):
        raise W3DTextureClosureError("effective-assets manifest schema is unsupported")
    catalog = document.get("catalog")
    if not isinstance(catalog, dict) or set(catalog) != {
        "archive_count",
        "entry_count",
        "format",
        "identity_sha256",
    }:
        raise W3DTextureClosureError("effective-assets catalog seal is invalid")
    if (
        not _is_int(catalog.get("archive_count"))
        or not _is_int(catalog.get("entry_count"))
        or not _is_int(catalog.get("format"), minimum=1)
        or not _is_sha256(catalog.get("identity_sha256"))
    ):
        raise W3DTextureClosureError("effective-assets catalog seal is invalid")
    install = document.get("install")
    if (
        not isinstance(install, dict)
        or set(install) != {"identity_sha256", "root"}
        or not _is_sha256(install.get("identity_sha256"))
        or not isinstance(install.get("root"), str)
        or not install.get("root")
    ):
        raise W3DTextureClosureError("effective-assets install seal is invalid")
    raw_files = document.get("files")
    if not isinstance(raw_files, list) or len(raw_files) > MAX_MANIFEST_FILES:
        raise W3DTextureClosureError("effective-assets inventory is invalid")
    files: list[_EffectiveFile] = []
    for index, item in enumerate(raw_files):
        if not isinstance(item, dict) or set(item) != {
            "archive",
            "offset",
            "path",
            "precedence",
            "sha256",
            "size",
        }:
            raise W3DTextureClosureError(
                f"effective-assets file entry {index} shape is invalid"
            )
        path = _safe_canonical_path(item.get("path"), "effective-assets file")
        archive = _safe_canonical_path(item.get("archive"), "effective-assets archive")
        if path.split("/", 1)[0].casefold() == ".openbfme":
            raise W3DTextureClosureError("effective-assets uses reserved metadata path")
        if (
            not _is_int(item.get("offset"))
            or not _is_int(item.get("precedence"))
            or not _is_int(item.get("size"))
            or not _is_sha256(item.get("sha256"))
        ):
            raise W3DTextureClosureError(
                f"effective-assets file entry {index} metadata is invalid"
            )
        files.append(
            _EffectiveFile(path, archive, int(item["size"]), str(item["sha256"]))
        )
    paths = [item.path for item in files]
    _case_unique_canonical(paths, "effective-assets")
    totals = document.get("totals")
    total_bytes = sum(item.size for item in files)
    if (
        not isinstance(totals, dict)
        or totals != {"bytes": total_bytes, "files": len(files)}
        or total_bytes > MAX_MANIFEST_BYTES
    ):
        raise W3DTextureClosureError("effective-assets totals are invalid")
    # ``entry_count`` describes the pre-precedence archive catalog, while
    # ``files`` contains only effective winners.  It may therefore be larger.
    if catalog["entry_count"] < len(files):
        raise W3DTextureClosureError("effective-assets catalog totals are invalid")
    aggregate = document.get("aggregate_sha256")
    if not _is_sha256(aggregate) or aggregate != _effective_aggregate(files):
        raise W3DTextureClosureError("effective-assets aggregate SHA-256 is invalid")
    seal = hashlib.sha256(raw).hexdigest()
    if expected_seal is not None and expected_seal != seal:
        raise W3DTextureClosureError("effective-assets manifest seal mismatches")
    return _EffectiveManifest(seal, str(aggregate), tuple(files))


def _native_request_sha256(
    source_manifest_sha256: str,
    source_aggregate_sha256: str,
    conversion: Mapping[str, object],
    sources: list[dict[str, object]],
) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.native-corpus-request",
            "schemaVersion": 0,
            "families": ["texture"],
            "sourceManifestSha256": source_manifest_sha256,
            "sourceManifestAggregateSha256": source_aggregate_sha256,
            "conversion": conversion,
            "sources": sources,
        }
    )


def _validate_native_manifest(
    value: Mapping[str, object] | bytes,
    effective: _EffectiveManifest,
    expected_seal: str | None,
) -> _NativeManifest:
    document, raw = _document(
        value, "native texture corpus manifest", ensure_ascii=True
    )
    if set(document) != {
        "schema",
        "schemaVersion",
        "selection",
        "totals",
        "entries",
        "reclassified",
        "outputs",
        "identitySha256",
    }:
        raise W3DTextureClosureError("native texture manifest shape is invalid")
    if (
        document.get("schema") != NATIVE_TEXTURE_MANIFEST_SCHEMA
        or document.get("schemaVersion") != NATIVE_TEXTURE_MANIFEST_VERSION
        or isinstance(document.get("schemaVersion"), bool)
    ):
        raise W3DTextureClosureError("native texture manifest schema is unsupported")
    selection = document.get("selection")
    if not isinstance(selection, dict) or set(selection) != {
        "families",
        "sourceManifestSha256",
        "sourceManifestAggregateSha256",
        "requestSha256",
        "conversion",
    }:
        raise W3DTextureClosureError("native texture selection is invalid")
    if selection.get("families") != ["texture"] or selection.get("conversion") != {}:
        raise W3DTextureClosureError("native corpus is not texture-only")
    if (
        selection.get("sourceManifestSha256") != effective.sha256
        or selection.get("sourceManifestAggregateSha256") != effective.aggregate_sha256
        or not _is_sha256(selection.get("requestSha256"))
    ):
        raise W3DTextureClosureError("native texture source seal mismatches")
    raw_outputs = document.get("outputs")
    raw_entries = document.get("entries")
    raw_reclassified = document.get("reclassified")
    if not all(
        isinstance(value, list)
        for value in (raw_outputs, raw_entries, raw_reclassified)
    ):
        raise W3DTextureClosureError("native texture inventories are invalid")
    assert isinstance(raw_outputs, list)
    assert isinstance(raw_entries, list)
    assert isinstance(raw_reclassified, list)
    if (
        len(raw_outputs) > MAX_MANIFEST_FILES
        or len(raw_entries) + len(raw_reclassified) > MAX_MANIFEST_FILES
    ):
        raise W3DTextureClosureError("native texture inventory exceeds its bound")

    outputs: dict[str, dict[str, object]] = {}
    output_paths: list[str] = []
    for index, item in enumerate(raw_outputs):
        if not isinstance(item, dict) or set(item) != {
            "path",
            "bytes",
            "sha256",
            "nativeFamily",
            "evidence",
        }:
            raise W3DTextureClosureError(
                f"native texture output {index} shape is invalid"
            )
        path = _safe_canonical_path(item.get("path"), "native texture output")
        digest = item.get("sha256")
        if (
            not _is_int(item.get("bytes"))
            or not _is_sha256(digest)
            or item.get("nativeFamily") != "png"
            or not isinstance(item.get("evidence"), dict)
            or path != f"objects/sha256/{str(digest)[:2]}/{digest}.png"
        ):
            raise W3DTextureClosureError(
                f"native texture output {index} metadata is invalid"
            )
        output_paths.append(path)
        outputs[path.casefold()] = item
    _case_unique_canonical(output_paths, "native texture output")

    effective_by_path = {item.path.casefold(): item for item in effective.files}
    entries: list[_NativeEntry] = []
    entry_paths: list[str] = []
    request_sources: list[dict[str, object]] = []
    referenced_outputs: set[str] = set()
    for index, item in enumerate(raw_entries):
        if not isinstance(item, dict) or set(item) != {
            "sourcePath",
            "sourceArchive",
            "sourceExtension",
            "sourceBytes",
            "sourceSha256",
            "family",
            "outputPath",
            "outputBytes",
            "outputSha256",
            "nativeFamily",
            "evidence",
        }:
            raise W3DTextureClosureError(
                f"native texture source {index} shape is invalid"
            )
        source_path = _safe_canonical_path(
            item.get("sourcePath"), "native texture source"
        )
        source_archive = _safe_canonical_path(
            item.get("sourceArchive"), "native texture archive"
        )
        output_path = _safe_canonical_path(
            item.get("outputPath"), "native texture output reference"
        )
        extension = item.get("sourceExtension")
        if (
            not isinstance(extension, str)
            or extension != PurePosixPath(source_path).suffix.casefold()
            or extension not in _TEXTURE_EXTENSIONS
            or item.get("family") != "texture"
            or item.get("nativeFamily") != "png"
            or not _is_int(item.get("sourceBytes"))
            or not _is_sha256(item.get("sourceSha256"))
            or not _is_int(item.get("outputBytes"))
            or not _is_sha256(item.get("outputSha256"))
            or not isinstance(item.get("evidence"), dict)
        ):
            raise W3DTextureClosureError(
                f"native texture source {index} metadata is invalid"
            )
        source = effective_by_path.get(source_path.casefold())
        if source is None or source.path != source_path:
            raise W3DTextureClosureError(
                "native texture source is absent from effective assets"
            )
        if (
            source.archive != source_archive
            or source.size != item["sourceBytes"]
            or source.sha256 != item["sourceSha256"]
        ):
            raise W3DTextureClosureError(
                "native/effective texture source SHA-256 mismatches"
            )
        output = outputs.get(output_path.casefold())
        if (
            output is None
            or output["path"] != output_path
            or output["bytes"] != item["outputBytes"]
            or output["sha256"] != item["outputSha256"]
            or output["nativeFamily"] != item["nativeFamily"]
            or output["evidence"] != item["evidence"]
        ):
            raise W3DTextureClosureError(
                "native texture source/output mapping mismatches"
            )
        entry_paths.append(source_path)
        referenced_outputs.add(output_path.casefold())
        entries.append(
            _NativeEntry(
                source_path,
                str(item["sourceSha256"]),
                int(item["sourceBytes"]),
                output_path,
                str(item["outputSha256"]),
                int(item["outputBytes"]),
            )
        )
        request_sources.append(
            {
                "path": source_path,
                "bytes": item["sourceBytes"],
                "sha256": item["sourceSha256"],
                "family": "texture",
                "extension": extension,
                "disposition": "media-conversion",
            }
        )
    _case_unique_canonical(entry_paths, "native texture source")

    reclassified_paths: list[str] = []
    reclassified_bytes = 0
    for index, item in enumerate(raw_reclassified):
        if not isinstance(item, dict) or set(item) != {
            "sourcePath",
            "sourceBytes",
            "sourceSha256",
            "originalFamily",
            "originalExtension",
            "classification",
            "detectedKind",
            "evidenceSha256",
        }:
            raise W3DTextureClosureError(
                f"native texture reclassification {index} shape is invalid"
            )
        source_path = _safe_canonical_path(
            item.get("sourcePath"), "native texture reclassification"
        )
        extension = item.get("originalExtension")
        if (
            item.get("originalFamily") != "texture"
            or item.get("classification") != "map-payload"
            or item.get("detectedKind") not in {"uncompressed", "ear-refpack"}
            or not isinstance(extension, str)
            or extension != PurePosixPath(source_path).suffix.casefold()
            or extension not in _TEXTURE_EXTENSIONS
            or not _is_int(item.get("sourceBytes"))
            or not _is_sha256(item.get("sourceSha256"))
            or not _is_sha256(item.get("evidenceSha256"))
        ):
            raise W3DTextureClosureError(
                f"native texture reclassification {index} metadata is invalid"
            )
        source = effective_by_path.get(source_path.casefold())
        if (
            source is None
            or source.path != source_path
            or source.size != item["sourceBytes"]
            or source.sha256 != item["sourceSha256"]
        ):
            raise W3DTextureClosureError(
                "native/effective reclassified texture source mismatches"
            )
        reclassified_paths.append(source_path)
        reclassified_bytes += int(item["sourceBytes"])
        request_sources.append(
            {
                "path": source_path,
                "bytes": item["sourceBytes"],
                "sha256": item["sourceSha256"],
                "family": "texture",
                "extension": extension,
                "disposition": "map-payload",
                "detectedKind": item["detectedKind"],
                "evidenceSha256": item["evidenceSha256"],
            }
        )
    _case_unique_canonical(reclassified_paths, "native texture reclassification")
    if {item.casefold() for item in entry_paths} & {
        item.casefold() for item in reclassified_paths
    }:
        raise W3DTextureClosureError("native texture source has two dispositions")
    request_sources.sort(
        key=lambda item: (str(item["path"]).casefold(), str(item["path"]))
    )
    expected_request = _native_request_sha256(
        effective.sha256,
        effective.aggregate_sha256,
        {},
        request_sources,
    )
    if selection["requestSha256"] != expected_request:
        raise W3DTextureClosureError("native texture request SHA-256 is invalid")
    if referenced_outputs != set(outputs):
        raise W3DTextureClosureError(
            "native texture output inventory is not exactly referenced"
        )
    totals = document.get("totals")
    expected_totals = {
        "candidateFileCount": len(entries) + len(reclassified_paths),
        "candidateBytes": sum(item.source_bytes for item in entries)
        + reclassified_bytes,
        "convertedFileCount": len(entries),
        "convertedBytes": sum(item.source_bytes for item in entries),
        "reclassifiedFileCount": len(reclassified_paths),
        "reclassifiedBytes": reclassified_bytes,
        "outputFileCount": len(outputs),
        "outputBytes": sum(int(item["bytes"]) for item in outputs.values()),
    }
    if totals != expected_totals or expected_totals["outputBytes"] > MAX_MANIFEST_BYTES:
        raise W3DTextureClosureError("native texture totals are invalid")
    basis = {key: value for key, value in document.items() if key != "identitySha256"}
    identity = document.get("identitySha256")
    if not _is_sha256(identity) or identity != _canonical_sha256(basis):
        raise W3DTextureClosureError("native texture identity SHA-256 is invalid")
    seal = hashlib.sha256(raw).hexdigest()
    if expected_seal is not None and expected_seal != seal:
        raise W3DTextureClosureError("native texture manifest seal mismatches")
    return _NativeManifest(
        seal,
        str(identity),
        tuple(entries),
        frozenset(path.casefold() for path in reclassified_paths),
    )


def _validate_catalog(report: W3DCatalogReport) -> dict[str, W3DCatalogSource]:
    if not isinstance(report, W3DCatalogReport):
        raise TypeError("W3D texture closure report must be a W3DCatalogReport")
    expected_input = _catalog_sha256(
        {
            "schema": "openbfme.w3d-catalog-input",
            "schemaVersion": 0,
            "sources": [source.input_neutral() for source in report.sources],
        }
    )
    if report.input_sha256 != expected_input:
        raise W3DTextureClosureError("W3D catalog input SHA-256 is invalid")
    if report.metadata_sha256 != _catalog_sha256(report.metadata_hash_basis()):
        raise W3DTextureClosureError("W3D catalog metadata SHA-256 is invalid")
    if report.total_input_bytes != sum(source.byte_length for source in report.sources):
        raise W3DTextureClosureError("W3D catalog input totals are invalid")
    by_path: dict[str, W3DCatalogSource] = {}
    for source in report.sources:
        path = source.canonical_virtual_path
        if (
            path is None
            or not _is_int(source.byte_length)
            or not _is_sha256(source.source_sha256)
        ):
            raise W3DTextureClosureError("W3D catalog has an unusable source")
        canonical = _safe_canonical_path(path, "W3D catalog source")
        if PurePosixPath(canonical).suffix.casefold() != ".w3d":
            raise W3DTextureClosureError("W3D catalog source is not a W3D path")
        if canonical.casefold() in by_path:
            raise W3DTextureClosureError("W3D catalog source paths case-collide")
        by_path[canonical.casefold()] = source
    metadata_seen: set[str] = set()
    for metadata in report.files:
        source = by_path.get(metadata.virtual_path.casefold())
        if (
            source is None
            or source.canonical_virtual_path != metadata.virtual_path
            or source.byte_length != metadata.byte_length
            or source.source_sha256 != metadata.source_sha256
            or metadata.virtual_path.casefold() in metadata_seen
        ):
            raise W3DTextureClosureError(
                "W3D catalog metadata/source binding is invalid"
            )
        metadata_seen.add(metadata.virtual_path.casefold())
    return by_path


def _validate_staged_inputs(
    staged_inputs: Mapping[str, str],
    sources: Mapping[str, W3DCatalogSource],
) -> dict[str, str]:
    if not isinstance(staged_inputs, Mapping):
        raise TypeError("W3D staged inputs must be a mapping")
    exact_sources = {
        source.canonical_virtual_path: source
        for source in sources.values()
        if source.canonical_virtual_path is not None
    }
    if set(staged_inputs) != set(exact_sources):
        raise W3DTextureClosureError("W3D staged-input paths mismatch the catalog")
    result: dict[str, str] = {}
    staged_paths: list[str] = []
    for source_path in sorted(exact_sources, key=lambda item: (item.casefold(), item)):
        value = staged_inputs[source_path]
        if not isinstance(value, str) or len(value) > MAX_STAGED_PATH_LENGTH:
            raise W3DTextureClosureError("W3D staged-input path is invalid")
        staged = _safe_canonical_path(value, "W3D staged input")
        if PurePosixPath(staged).suffix.casefold() != ".w3d":
            raise W3DTextureClosureError("W3D staged-input path is not a W3D")
        result[source_path] = staged
        staged_paths.append(staged)
    _case_unique_canonical(
        sorted(staged_paths, key=lambda item: (item.casefold(), item)),
        "W3D staged input",
    )
    return result


def _normalize_reference(identifier: object) -> _Reference | None:
    if (
        not isinstance(identifier, str)
        or not identifier
        or identifier != identifier.strip()
        or len(identifier) > MAX_STAGED_PATH_LENGTH
        or "\0" in identifier
    ):
        return None
    normalized = identifier.replace("\\", "/")
    try:
        parts = safe_relative_parts(normalized)
        canonical = "/".join(parts)
    except (TypeError, ValueError):
        return None
    if canonical != normalized or any(
        character in _WINDOWS_FORBIDDEN_FILENAME_CHARACTERS
        for part in parts
        for character in part
    ):
        return None
    stem = PurePosixPath(normalized).stem
    if not stem:
        return None
    try:
        if "/".join(safe_relative_parts(f"{stem}.png")) != f"{stem}.png":
            return None
    except (TypeError, ValueError):
        return None
    return _Reference(
        normalized,
        stem,
        _canonical_sha256(
            {
                "schema": "openbfme.w3d-texture-reference",
                "schemaVersion": 0,
                "identifier": identifier,
            }
        ),
    )


def _dependency_evidence(
    *,
    kind: str,
    chunk_header_offset: int,
    chunk_payload_offset: int,
    parent_chunk_header_offset: int | None,
    property_name: str | None = None,
    property_type: int | None = None,
) -> str:
    basis: dict[str, object] = {
        "schema": "openbfme.w3d-importer-texture-dependency",
        "schemaVersion": 0,
        "policy": TEXTURE_DEPENDENCY_POLICY,
        "kind": kind,
        "chunkHeaderOffset": chunk_header_offset,
        "chunkPayloadOffset": chunk_payload_offset,
        "parentChunkHeaderOffset": parent_chunk_header_offset,
    }
    if property_name is not None:
        basis["propertyName"] = property_name
    if property_type is not None:
        basis["propertyType"] = property_type
    return _canonical_sha256(basis)


def _texture_dependencies(
    metadata: W3DMetadata,
) -> tuple[tuple[_TextureDependency, ...], frozenset[str]]:
    """Enumerate every texture load made by the pinned importer policy."""

    dependencies = [
        _TextureDependency(reference.identifier, None)
        for reference in metadata.texture_references
    ]
    reasons: set[str] = set()
    header_counts: dict[int, int] = {}
    for header in metadata.shader_material_headers:
        owner = header.material_chunk_header_offset
        if owner is not None:
            header_counts[owner] = header_counts.get(owner, 0) + 1
    conditional = _CONDITIONAL_SHADER_TEXTURE_PROPERTIES
    unconditional = _UNCONDITIONAL_SHADER_TEXTURE_PROPERTIES
    for prop in metadata.shader_material_properties:
        name = prop.name
        if name not in conditional and name not in unconditional:
            continue
        if name in conditional and prop.value == "":
            continue
        owner = prop.material_chunk_header_offset
        owner_count = header_counts.get(owner, 0) if owner is not None else 0
        if owner_count == 0:
            reasons.add("unbound-shader-texture-property")
        elif owner_count > 1:
            reasons.add("ambiguous-shader-texture-property-owner")
        provenance = prop.provenance
        dependencies.append(
            _TextureDependency(
                prop.value,
                _dependency_evidence(
                    kind="shader-property",
                    chunk_header_offset=provenance.chunk_header_offset,
                    chunk_payload_offset=provenance.chunk_payload_offset,
                    parent_chunk_header_offset=provenance.parent_chunk_header_offset,
                    property_name=name,
                    property_type=prop.property_type,
                ),
            )
        )
    for chunk in metadata.chunks:
        if chunk.chunk_id != 0x00000900:
            continue
        dependencies.append(
            _TextureDependency(
                _PINNED_DAZZLE_TEXTURE,
                _dependency_evidence(
                    kind="implicit-dazzle",
                    chunk_header_offset=chunk.header_offset,
                    chunk_payload_offset=chunk.payload_offset,
                    parent_chunk_header_offset=chunk.parent_header_offset,
                ),
            )
        )
    return tuple(dependencies), frozenset(reasons)


def _without_extension(path: str) -> str:
    candidate = PurePosixPath(path)
    return str(candidate.with_suffix("")) if candidate.suffix else path


def _texture_resolution_index(
    textures: tuple[_EffectiveFile, ...],
) -> _TextureResolutionIndex:
    exact: dict[str, _EffectiveFile] = {}
    path_stems: dict[str, list[_EffectiveFile]] = {}
    leaves: dict[str, list[_EffectiveFile]] = {}
    stems: dict[str, list[_EffectiveFile]] = {}
    for item in textures:
        exact[item.path.casefold()] = item
        path_stems.setdefault(_without_extension(item.path).casefold(), []).append(item)
        candidate = PurePosixPath(item.path)
        leaves.setdefault(candidate.name.casefold(), []).append(item)
        stems.setdefault(candidate.stem.casefold(), []).append(item)
    return _TextureResolutionIndex(
        exact=exact,
        path_stems={key: tuple(value) for key, value in path_stems.items()},
        leaves={key: tuple(value) for key, value in leaves.items()},
        stems={key: tuple(value) for key, value in stems.items()},
    )


def _resolve_effective_texture(
    reference: _Reference,
    index: _TextureResolutionIndex,
) -> tuple[_EffectiveFile | None, str | None]:
    direct = index.exact.get(reference.normalized.casefold())
    if direct is not None:
        return direct, None
    reference_without_extension = _without_extension(reference.normalized).casefold()
    path_stem_matches = index.path_stems.get(reference_without_extension, ())
    if len(path_stem_matches) == 1:
        return path_stem_matches[0], None
    if len(path_stem_matches) > 1:
        return None, "ambiguous-texture-source"
    reference_leaf = PurePosixPath(reference.normalized).name.casefold()
    reference_stem = PurePosixPath(reference.normalized).stem.casefold()
    fallback_by_path = {
        item.path.casefold(): item
        for item in (
            *index.leaves.get(reference_leaf, ()),
            *index.stems.get(reference_stem, ()),
        )
    }
    fallback = tuple(
        fallback_by_path[key]
        for key in sorted(fallback_by_path, key=lambda value: (value.casefold(), value))
    )
    if len(fallback) == 1:
        return fallback[0], None
    if len(fallback) > 1:
        return None, "ambiguous-texture-source"
    return None, "missing-texture-source"


def plan_w3d_texture_closure(
    report: W3DCatalogReport,
    staged_inputs: Mapping[str, str],
    effective_assets_manifest: Mapping[str, object] | bytes,
    native_texture_manifest: Mapping[str, object] | bytes,
    *,
    effective_manifest_sha256: str | None = None,
    native_manifest_sha256: str | None = None,
    edition_seal: str | None = None,
    source_seal: str | None = None,
) -> W3DTextureClosurePlan:
    """Plan exact PNG copies beside staged W3D models without copying bytes.

    Manifest SHA arguments are optional expected seals for callers that retained
    the raw manifest digest.  ``edition_seal`` and ``source_seal`` are optional
    opaque SHA-256 bindings (for example, an overlay identity and W3D stage
    identity).  They are preserved in both plan hashes but are not interpreted.
    """

    for label, value in (
        ("effective manifest SHA-256", effective_manifest_sha256),
        ("native manifest SHA-256", native_manifest_sha256),
        ("edition seal", edition_seal),
        ("source seal", source_seal),
    ):
        if value is not None and not _is_sha256(value):
            raise W3DTextureClosureError(f"{label} is invalid")

    sources = _validate_catalog(report)
    staged = _validate_staged_inputs(staged_inputs, sources)
    effective = _validate_effective_manifest(
        effective_assets_manifest, effective_manifest_sha256
    )
    native = _validate_native_manifest(
        native_texture_manifest, effective, native_manifest_sha256
    )
    effective_by_path = {item.path.casefold(): item for item in effective.files}
    for source in sources.values():
        assert source.canonical_virtual_path is not None
        item = effective_by_path.get(source.canonical_virtual_path.casefold())
        if (
            item is None
            or item.path != source.canonical_virtual_path
            or item.size != source.byte_length
            or item.sha256 != source.source_sha256
        ):
            raise W3DTextureClosureError(
                "W3D catalog source mismatches the effective-assets manifest"
            )

    textures = tuple(
        item
        for item in effective.files
        if PurePosixPath(item.path).suffix.casefold() in _TEXTURE_EXTENSIONS
    )
    texture_index = _texture_resolution_index(textures)
    native_by_source = {item.source_path.casefold(): item for item in native.entries}

    model_rows: dict[str, dict[str, Any]] = {}
    requirements: dict[str, list[_Requirement]] = {}
    for metadata in report.files:
        if not metadata.model_headers and not metadata.mesh_headers:
            continue
        source = sources[metadata.virtual_path.casefold()]
        assert source.canonical_virtual_path is not None
        model_id = catalog_source_id(source)
        dependencies, dependency_reasons = _texture_dependencies(metadata)
        reasons = set(dependency_reasons)
        resolved = 0
        evidence_hashes: list[str] = []
        selected: list[_Requirement] = []
        model_parent = PurePosixPath(staged[source.canonical_virtual_path]).parent
        for dependency in dependencies:
            reference = _normalize_reference(dependency.identifier)
            if reference is None:
                reasons.add("unsafe-texture-identifier")
                continue
            if dependency.evidence_sha256 is not None:
                reference = replace(
                    reference,
                    evidence_sha256=dependency.evidence_sha256,
                )
            evidence_hashes.append(reference.evidence_sha256)
            texture, reason = _resolve_effective_texture(reference, texture_index)
            if reason is not None or texture is None:
                reasons.add(reason or "missing-texture-source")
                continue
            native_entry = native_by_source.get(texture.path.casefold())
            if texture.path.casefold() in native.reclassified_paths:
                reasons.add("texture-source-reclassified")
                continue
            if native_entry is None:
                reasons.add("texture-conversion-missing")
                continue
            if (
                native_entry.source_sha256 != texture.sha256
                or native_entry.source_bytes != texture.size
            ):
                raise W3DTextureClosureError(
                    "resolved native/effective texture SHA-256 mismatches"
                )
            # The pinned Blender importer resolves the texture identifier
            # relative to the imported W3D, including any safe subdirectories
            # carried by that identifier.  Preserve that relative layout while
            # replacing only the source suffix with the converted PNG suffix.
            # Flattening to ``reference.stem`` leaves the exact converted file
            # present but unreachable and makes the importer synthesize a
            # generated placeholder instead.
            reference_png = PurePosixPath(reference.normalized).with_suffix(".png")
            destination = (model_parent / reference_png).as_posix()
            if len(destination) > MAX_STAGED_PATH_LENGTH:
                reasons.add("unsafe-texture-destination")
                continue
            selected.append(
                _Requirement(
                    model_id,
                    reference.evidence_sha256,
                    native_entry,
                    destination,
                )
            )
            resolved += 1
        if reasons:
            selected = []
        requirements[model_id] = selected
        model_rows[model_id] = {
            "sourceSha256": source.source_sha256,
            "stagedPath": staged[source.canonical_virtual_path],
            "referenceCount": len(dependencies),
            "resolvedCount": resolved,
            "evidence": tuple(sorted(set(evidence_hashes))),
            "reasons": reasons,
        }

    # The importer resolves these paths case-insensitively on the retail
    # Windows conversion host, while the sealed job root must have one exact
    # directory spelling.  Canonicalize directory aliases before collision
    # accounting without changing any referenced path component otherwise.
    _canonicalize_requirement_directories(requirements)

    destination_groups: dict[str, list[_Requirement]] = {}
    for items in requirements.values():
        for item in items:
            destination_groups.setdefault(item.destination_path.casefold(), []).append(
                item
            )
    collided_models: set[str] = set()
    for items in destination_groups.values():
        if len({item.entry.output_sha256 for item in items}) > 1:
            collided_models.update(item.model_source_id for item in items)
    for model_id in collided_models:
        model_rows[model_id]["reasons"].add("texture-destination-collision")
        requirements[model_id] = []

    grouped: dict[tuple[str, str], list[_Requirement]] = {}
    for model_id, items in requirements.items():
        if model_rows[model_id]["reasons"]:
            continue
        for item in items:
            grouped.setdefault(
                (item.destination_path.casefold(), item.entry.output_sha256), []
            ).append(item)
    copies: list[W3DTextureCopyInstruction] = []
    instruction_by_model: dict[str, list[str]] = {key: [] for key in model_rows}
    for _, items in sorted(grouped.items()):
        exemplar = min(
            items,
            key=lambda item: (
                item.destination_path.casefold(),
                item.destination_path,
                item.entry.output_path,
            ),
        )
        model_ids = tuple(sorted({item.model_source_id for item in items}))
        reference_hashes = tuple(
            sorted({item.reference_evidence_sha256 for item in items})
        )
        basis = {
            "sourceOutputPath": exemplar.entry.output_path,
            "sourceOutputSha256": exemplar.entry.output_sha256,
            "sourceOutputBytes": exemplar.entry.output_bytes,
            "destinationPath": exemplar.destination_path,
            "modelSourceIds": list(model_ids),
            "referenceEvidenceSha256s": list(reference_hashes),
        }
        definition = _canonical_sha256(basis)
        instruction = W3DTextureCopyInstruction(
            instruction_id=f"texcopy-{definition[:40]}",
            source_output_path=exemplar.entry.output_path,
            source_output_sha256=exemplar.entry.output_sha256,
            source_output_bytes=exemplar.entry.output_bytes,
            destination_path=exemplar.destination_path,
            model_source_ids=model_ids,
            reference_evidence_sha256s=reference_hashes,
            definition_sha256=definition,
        )
        copies.append(instruction)
        for model_id in model_ids:
            instruction_by_model[model_id].append(instruction.instruction_id)
    copies.sort(key=lambda item: item.instruction_id)

    models = tuple(
        W3DModelTextureClosure(
            model_source_id=model_id,
            model_source_sha256=str(row["sourceSha256"]),
            staged_model_path=str(row["stagedPath"]),
            texture_reference_count=int(row["referenceCount"]),
            resolved_reference_count=int(row["resolvedCount"]),
            instruction_ids=tuple(sorted(instruction_by_model[model_id])),
            reference_evidence_sha256s=tuple(row["evidence"]),
            terminal_reasons=tuple(sorted(row["reasons"])),
        )
        for model_id, row in sorted(model_rows.items())
    )
    provisional = W3DTextureClosurePlan(
        models=models,
        copy_instructions=tuple(copies),
        catalog_input_sha256=report.input_sha256,
        catalog_metadata_sha256=report.metadata_sha256,
        effective_manifest_sha256=effective.sha256,
        effective_manifest_aggregate_sha256=effective.aggregate_sha256,
        native_manifest_sha256=native.sha256,
        native_texture_identity_sha256=native.identity_sha256,
        edition_seal=edition_seal,
        source_seal=source_seal,
        private_plan_sha256="",
        evidence_sha256="",
    )
    with_private = replace(
        provisional,
        private_plan_sha256=_canonical_sha256(provisional.private_hash_basis()),
    )
    return replace(
        with_private,
        evidence_sha256=_canonical_sha256(with_private.evidence_hash_basis()),
    )


# Concise alias for integration beside ``plan_w3d_batches``.
plan_texture_closure = plan_w3d_texture_closure
