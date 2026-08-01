"""Plan exact animated Fords prop conversions from sealed retail evidence.

This is the third, deliberately narrow visual-planning pass.  Static and
zero-clip hierarchical targets must already be covered by their respective
sealed plans.  Base-profile logical bindings are removed from the physical
candidate set.  What remains is promoted only when one exact model, one exact
hierarchy, every authored animation, and every texture can be proven from the
effective retail tree and satisfy the existing ``w3d-bundle`` contract.

The planner reads retail bytes only below the caller-provided private effective
asset root.  Its output contains hashes and virtual paths, never payload bytes.
It does not convert, publish, or change a content-pack selection.
"""

from __future__ import annotations

from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
from typing import Any, Mapping

from .paths import safe_relative_parts
from .profile import ImportProfile
from .retail_hierarchical_profile import (
    HIERARCHICAL_PROP_PLAN_SCHEMA,
    HIERARCHICAL_PROP_PLAN_SCHEMA_VERSION,
    _validate_placement_document,
    build_retail_hierarchical_prop_plan,
)
from .retail_visual_profile import (
    STATIC_PROP_PLAN_SCHEMA,
    STATIC_PROP_PLAN_SCHEMA_VERSION,
    _canonical_sha256,
    _case_unique,
    _mapping,
    _reason,
    _safe_object_id,
    _source_record,
    _text,
    _unique_reasons,
    _validate_declared_digest,
    _validate_effective_manifest,
    _validate_visual_closure,
    build_retail_static_prop_plan,
)
from .util import write_json_atomic
from .w3d_metadata import W3DMetadata, scan_w3d_metadata
from .w3d_secondary_skin import (
    W3DSecondarySkinError,
    strip_proven_redundant_secondary_skin_streams,
)


ANIMATED_PROP_PLAN_SCHEMA = "openbfme.retail-animated-prop-plan"
ANIMATED_PROP_PLAN_SCHEMA_VERSION = 0

_TEXTURE_LEAF_KINDS = frozenset({"texture", "shadow"})
_TEXTURE_SUFFIXES = frozenset({".dds", ".tga", ".jpg", ".png"})
_SUPPORTED_W3D_MODEL_REFERENCE_ROLES = frozenset({"lod"})
_ANIMATION_CHANNEL_CHUNK_NAMES = frozenset(
    {
        "animation-channel",
        "animation-bit-channel",
        "compressed-animation-channel",
        "compressed-bit-channel",
        "compressed-animation-motion-channel",
    }
)
_MAX_PROFILE_RESOURCES = 256
_MAX_VERIFIED_SOURCE_BYTES = 512 * 1024 * 1024
_MAX_GLOBAL_W3D_CORPUS_BYTES = 2 * 1024 * 1024 * 1024
_MAX_GLOBAL_W3D_CORPUS_FILES = 100_000
_SECONDARY_SKIN_CHUNK_IDS = frozenset({0x00000C00, 0x00000C01})

# These are not filename substitutions.  They are exact TransitionState rows
# authored by BFME2 that name no physical action anywhere in the complete,
# manifest-attested retail W3D corpus.  SAGE treats such transition states as
# immediate state changes.  Every field below is checked before that source-
# native no-clip meaning is admitted, and every physical header name is then
# proven absent from the complete corpus before the plan can be emitted.
_SOURCE_NATIVE_NO_CLIP_TRANSITIONS: dict[str, dict[str, str]] = {
    "Bear": {
        "CUBear_SKL.CUBear_IDLE": "TRANS_AlertToIdle",
        "CUBear_SKL.CUBear_IDLC": "TRANS_IdleToAlert",
    },
    "Duck": {
        "CUDuck_SKL.CUDuck_ANTA": "TRANS_MovingToIdle",
        "CUDuck_SKL.CUDuck_ANTB": "TRANS_IdletoMoving",
    },
    "Rabbit": {
        "CURabbit1_SKL.CURabbit1_IDLD": "TRANS_AlertToIdle",
        "CURabbit1_SKL.CURabbit1_IDLC": "TRANS_IdleToAlert",
    },
    "Raccoon": {
        "CURaccoon_SKL.CURaccoon_ANTA": "TRANS_AlertToIdle",
        "CURaccoon_SKL.CURaccoon_ANTB": "TRANS_IdleToAlert",
    },
    "Wolf": {
        "CUWolf_SKL.CUWolf_SITA": "TRANS_MovingToIdle",
        "CUWolf_SKL.CUWolf_ATNA": "TRANS_IdletoMoving",
    },
}


def _validate_upstream_plans(
    visual_closure_report: Mapping[str, Any],
    static_prop_plan: Mapping[str, Any],
    hierarchical_prop_plan: Mapping[str, Any],
    effective_assets_manifest: Mapping[str, Any],
    map_objects_document: Mapping[str, Any],
) -> tuple[str, str, set[str], set[str], dict[str, list[Mapping[str, Any]]]]:
    static = _mapping(static_prop_plan, "retail static-prop plan")
    if static.get("schema") != STATIC_PROP_PLAN_SCHEMA:
        raise ValueError("unsupported retail static-prop plan schema")
    if static.get("schemaVersion") != STATIC_PROP_PLAN_SCHEMA_VERSION:
        raise ValueError("unsupported retail static-prop plan schema version")
    static_digest = _validate_declared_digest(
        static, "aggregateSha256", "retail static-prop plan"
    )
    expected_static = build_retail_static_prop_plan(
        visual_closure_report, effective_assets_manifest
    )
    if dict(static) != expected_static:
        raise ValueError(
            "retail static-prop plan does not exactly match the validated inputs"
        )

    hierarchical = _mapping(hierarchical_prop_plan, "retail hierarchical-prop plan")
    if hierarchical.get("schema") != HIERARCHICAL_PROP_PLAN_SCHEMA:
        raise ValueError("unsupported retail hierarchical-prop plan schema")
    if hierarchical.get("schemaVersion") != HIERARCHICAL_PROP_PLAN_SCHEMA_VERSION:
        raise ValueError("unsupported retail hierarchical-prop plan schema version")
    hierarchical_digest = _validate_declared_digest(
        hierarchical,
        "aggregateSha256",
        "retail hierarchical-prop plan",
    )
    expected_hierarchical = build_retail_hierarchical_prop_plan(
        visual_closure_report,
        static,
        effective_assets_manifest,
        map_objects_document,
    )
    if dict(hierarchical) != expected_hierarchical:
        raise ValueError(
            "retail hierarchical-prop plan does not exactly match the validated inputs"
        )

    static_eligible = {
        _safe_object_id(item.get("targetObject"), "static eligible target id")
        for item in static.get("eligibleTargets", [])
    }
    hierarchical_eligible = {
        _safe_object_id(item.get("targetObject"), "hierarchical eligible target id")
        for item in hierarchical.get("eligibleTargets", [])
    }
    if static_eligible & hierarchical_eligible:
        raise ValueError("static and hierarchical eligible target sets overlap")
    hierarchical_reasons: dict[str, list[Mapping[str, Any]]] = {}
    for position, value in enumerate(hierarchical.get("rejectedTargets", [])):
        item = _mapping(value, f"hierarchical rejected target {position}")
        target = _safe_object_id(
            item.get("targetObject"), "hierarchical rejected target id"
        )
        reasons = item.get("reasons")
        if not isinstance(reasons, list) or not reasons:
            raise ValueError(f"hierarchical rejected target {target!r} has no reasons")
        hierarchical_reasons[target] = [
            _mapping(reason, f"hierarchical target {target!r} reason")
            for reason in reasons
        ]
    return (
        static_digest,
        hierarchical_digest,
        static_eligible,
        hierarchical_eligible,
        hierarchical_reasons,
    )


def _validate_base_profile_semantics(
    raw: Mapping[str, Any], targets: list[str]
) -> tuple[str, dict[str, str], dict[str, Any]]:
    profile = _mapping(raw, "base import profile")
    if profile.get("format") != 1:
        raise ValueError("unsupported base import profile format")
    profile_id = _text(profile.get("id"), "base import profile id")
    resources = profile.get("resources")
    if not isinstance(resources, list):
        raise ValueError("base import profile resources must be an array")
    map_resources = [
        _mapping(value, f"base import profile resource {position}")
        for position, value in enumerate(resources)
        if isinstance(value, Mapping) and value.get("converter") == "sage-map"
    ]
    if len(map_resources) != 1:
        raise ValueError(
            "base import profile must contain exactly one sage-map resource"
        )
    options = _mapping(map_resources[0].get("options"), "sage-map options")
    bindings = _mapping(options.get("objectBindings"), "sage-map objectBindings")
    logical_rows = bindings.get("logical")
    if not isinstance(logical_rows, list):
        raise ValueError("sage-map logical bindings must be an array")

    target_cases = _case_unique(targets, "visual-closure target id")
    all_names: list[str] = []
    logical_targets: dict[str, str] = {}
    classification_counts: Counter[str] = Counter()
    for position, value in enumerate(logical_rows):
        item = _mapping(value, f"logical binding {position}")
        if set(item) != {"typeName", "classification"}:
            raise ValueError(f"logical binding {position} has unsupported fields")
        type_name = _text(item.get("typeName"), f"logical binding {position} typeName")
        classification = _text(
            item.get("classification"),
            f"logical binding {position} classification",
        )
        if len(type_name) > 512 or "\0" in type_name:
            raise ValueError(f"logical binding {position} typeName is unsafe")
        all_names.append(type_name)
        classification_counts[classification] += 1
        exact_target = target_cases.get(type_name.casefold())
        if exact_target is not None:
            if exact_target != type_name:
                raise ValueError(
                    "base-profile logical target case does not match visual closure: "
                    f"{type_name!r} != {exact_target!r}"
                )
            logical_targets[exact_target] = classification
    _case_unique(all_names, "base-profile logical binding typeName")
    digest = _canonical_sha256(profile)
    return (
        digest,
        logical_targets,
        {
            "profileId": profile_id,
            "profileAggregateSha256": digest,
            "sageMapResourceId": _text(
                map_resources[0].get("id"), "sage-map resource id"
            ),
            "logicalBindingCount": len(logical_rows),
            "visualTargetLogicalBindingCount": len(logical_targets),
            "classificationCounts": [
                {"classification": name, "count": classification_counts[name]}
                for name in sorted(classification_counts)
            ],
        },
    )


class _PrivateSourceReader:
    """Verify and scan exact manifest-bound files below one private root."""

    def __init__(
        self,
        root: Path | str,
        sources: Mapping[str, Mapping[str, Any]],
    ) -> None:
        self.root = Path(root).expanduser().resolve()
        if not self.root.is_dir():
            raise ValueError("effective-assets root is missing or is not a directory")
        self.sources = sources
        self.path_cases = _case_unique(list(sources), "effective-assets source path")
        self._bytes: dict[str, bytes] = {}
        self._scans: dict[str, W3DMetadata] = {}

    def resolve_case(self, virtual_path: str) -> str | None:
        return self.path_cases.get(virtual_path.casefold())

    def resolve_unique_basename(self, basename: str) -> tuple[str | None, list[str]]:
        matches = sorted(
            (
                path
                for path in self.sources
                if PurePosixPath(path).name.casefold() == basename.casefold()
            ),
            key=lambda value: (value.casefold(), value),
        )
        return (matches[0] if len(matches) == 1 else None), matches

    def _read_verified(self, virtual_path: str) -> bytes:
        source = self.sources.get(virtual_path)
        if source is None:
            raise ValueError(
                f"verified source is absent from effective-assets manifest: {virtual_path}"
            )
        parts = safe_relative_parts(virtual_path)
        path = (self.root / Path(*parts)).resolve()
        try:
            path.relative_to(self.root)
        except ValueError as exc:
            raise ValueError(
                "effective-assets source escaped its private root"
            ) from exc
        if not path.is_file():
            raise ValueError(f"effective-assets source is missing: {virtual_path}")
        expected_size = int(source["size"])
        if expected_size > _MAX_VERIFIED_SOURCE_BYTES:
            raise ValueError(
                f"effective-assets source exceeds read bound: {virtual_path}"
            )
        if path.stat().st_size != expected_size:
            raise ValueError(
                f"effective-assets source byte length drift: {virtual_path}"
            )
        payload = path.read_bytes()
        if hashlib.sha256(payload).hexdigest() != source["sha256"]:
            raise ValueError(f"effective-assets source SHA-256 drift: {virtual_path}")
        return payload

    def read(self, virtual_path: str) -> bytes:
        cached = self._bytes.get(virtual_path)
        if cached is not None:
            return cached
        payload = self._read_verified(virtual_path)
        self._bytes[virtual_path] = payload
        return payload

    def scan(self, virtual_path: str) -> W3DMetadata:
        cached = self._scans.get(virtual_path)
        if cached is None:
            cached = scan_w3d_metadata(self.read(virtual_path), virtual_path)
            self._scans[virtual_path] = cached
        return cached

    def evidence(self) -> dict[str, Any]:
        paths = sorted(self._bytes, key=lambda value: (value.casefold(), value))
        return {
            "policy": "exact-manifest-source-below-private-effective-assets-root",
            "sourceCount": len(paths),
            "byteLength": sum(len(self._bytes[path]) for path in paths),
            "sources": [_source_record(path, self.sources) for path in paths],
        }

    def prove_ascii_identifiers_absent(
        self,
        logical_identifiers: Mapping[str, str],
    ) -> dict[str, Any]:
        """Prove exact ASCII header names absent from every winning W3D.

        Corpus reads are manifest-attested but deliberately not retained in
        the normal conversion-source cache.  The returned evidence contains
        only identifiers, counts, byte lengths, and hashes.
        """

        if not logical_identifiers:
            raise ValueError("source-native no-clip proof has no identifiers")
        header_to_logical: dict[str, list[str]] = {}
        for logical, header in sorted(
            logical_identifiers.items(), key=lambda item: (item[0].casefold(), item[0])
        ):
            if (
                not logical
                or not header
                or not logical.isascii()
                or not header.isascii()
            ):
                raise ValueError("source-native no-clip identifier is not ASCII")
            header_to_logical.setdefault(header.casefold(), []).append(logical)
        if len(header_to_logical) != len(logical_identifiers):
            raise ValueError("source-native no-clip header identifiers are not unique")

        corpus_paths = sorted(
            (
                path
                for path in self.sources
                if PurePosixPath(path).suffix.casefold() == ".w3d"
            ),
            key=lambda value: (value.casefold(), value),
        )
        if not corpus_paths:
            raise ValueError("effective-assets manifest has no W3D corpus")
        if len(corpus_paths) > _MAX_GLOBAL_W3D_CORPUS_FILES:
            raise ValueError("effective-assets W3D corpus exceeds file-count bound")
        corpus_bytes = sum(int(self.sources[path]["size"]) for path in corpus_paths)
        if corpus_bytes > _MAX_GLOBAL_W3D_CORPUS_BYTES:
            raise ValueError("effective-assets W3D corpus exceeds byte bound")

        corpus_digest = hashlib.sha256()
        needles = {
            header: header.encode("ascii").upper() for header in header_to_logical
        }
        hits: list[tuple[str, str]] = []
        for virtual_path in corpus_paths:
            source = self.sources[virtual_path]
            corpus_digest.update(virtual_path.encode("utf-8"))
            corpus_digest.update(b"\0")
            corpus_digest.update(str(source["size"]).encode("ascii"))
            corpus_digest.update(b"\0")
            corpus_digest.update(str(source["sha256"]).encode("ascii"))
            corpus_digest.update(b"\n")
            uppercase_payload = self._read_verified(virtual_path).upper()
            for header, needle in needles.items():
                if needle in uppercase_payload:
                    hits.append((virtual_path, header))
        if hits:
            hit_headers = sorted(
                {header for _, header in hits},
                key=lambda value: (value.casefold(), value),
            )
            raise ValueError(
                "source-native no-clip header unexpectedly exists in retail W3D "
                "corpus: " + ", ".join(hit_headers)
            )

        rows = [
            {
                "logicalIdentifier": logical,
                "headerIdentifier": logical_identifiers[logical],
            }
            for logical in sorted(
                logical_identifiers, key=lambda value: (value.casefold(), value)
            )
        ]
        proof: dict[str, Any] = {
            "method": "case-insensitive-exact-ascii-byte-scan-of-all-manifest-w3ds",
            "w3dCorpusAggregateSha256": corpus_digest.hexdigest(),
            "w3dFileCount": len(corpus_paths),
            "w3dByteLength": corpus_bytes,
            "identifiers": rows,
            "hitCount": 0,
        }
        proof["proofSha256"] = _canonical_sha256(proof)
        return proof


def _fresh_header_ids(metadata: W3DMetadata) -> dict[str, Any]:
    headers = metadata.file_headers()
    return {
        "virtualPath": metadata.virtual_path,
        "modelIds": list(headers.model_ids),
        "hierarchyIds": list(headers.hierarchy_ids),
        "animationIds": list(headers.animation_ids),
    }


def _source_native_no_clip_rows(
    target: str,
    unresolved: list[Mapping[str, Any]],
) -> tuple[list[dict[str, Any]], frozenset[int]]:
    expected = _SOURCE_NATIVE_NO_CLIP_TRANSITIONS.get(target)
    if expected is None:
        return [], frozenset()
    accepted: list[tuple[int, dict[str, Any]]] = []
    for position, leaf in enumerate(unresolved):
        identifier = str(leaf.get("identifier"))
        condition = expected.get(identifier)
        provenance = leaf.get("provenance")
        if condition is None or not isinstance(provenance, Mapping):
            continue
        scope_path = provenance.get("scopePath")
        if not isinstance(scope_path, list):
            continue
        candidates = leaf.get("candidates", [])
        physical_paths = leaf.get("physicalVirtualPaths", [])
        if (
            leaf.get("targetObject") != target
            or leaf.get("status") != "missing"
            or leaf.get("kind") != "animation"
            or leaf.get("usage") != "animation"
            or leaf.get("reason") != "missing W3D animation reference"
            or leaf.get("conditions") != [condition]
            or leaf.get("lifecyclePhases") != ["intact"]
            or provenance.get("definingObject") != target
            or f"TransitionState {condition}" not in scope_path
            or candidates not in (None, [])
            or physical_paths not in (None, [])
            or "." not in identifier
        ):
            continue
        accepted.append(
            (
                position,
                {
                    "identifier": identifier,
                    "headerIdentifier": identifier.rsplit(".", 1)[1],
                    "condition": condition,
                    "semantics": "immediate-no-clip",
                    "evidence": "complete-manifest-w3d-corpus-absence-proof",
                },
            )
        )
    accepted_identifiers = {row["identifier"] for _, row in accepted}
    if len(accepted) != len(expected) or accepted_identifiers != set(expected):
        return [], frozenset()
    rows = sorted(
        (row for _, row in accepted),
        key=lambda item: (str(item["identifier"]).casefold(), str(item["identifier"])),
    )
    return rows, frozenset(position for position, _ in accepted)


def _has_clean_secondary_skin_streams(metadata: W3DMetadata) -> bool:
    """True when validated dual-bone streams are present and nothing warned.

    The metadata scanner now decodes ``vertices-2``/``normals-2`` instead of
    warning about them, so presence is read from the chunk records.  The
    no-other-warnings requirement is unchanged from the historical contract:
    the secondary-skin normalization proof is only attempted for sources whose
    scan is otherwise clean.
    """

    return not metadata.warnings and any(
        chunk.chunk_id in _SECONDARY_SKIN_CHUNK_IDS for chunk in metadata.chunks
    )


def _scan_closure_source(
    virtual_path: str,
    closure: Mapping[str, Any],
    reader: _PrivateSourceReader,
) -> W3DMetadata:
    metadata = reader.scan(virtual_path)
    sealed = closure["scanned"].get(virtual_path)
    if sealed is None:
        raise ValueError(
            f"exact visual W3D was not sealed by the closure: {virtual_path}"
        )
    if sealed.get("headerIds") != _fresh_header_ids(metadata):
        raise ValueError(
            f"fresh W3D headers disagree with visual closure: {virtual_path}"
        )
    if sealed.get("warnings") != [warning.neutral() for warning in metadata.warnings]:
        raise ValueError(
            f"fresh W3D warnings disagree with visual closure: {virtual_path}"
        )
    if sealed.get("modelReferences") != [
        reference.neutral() for reference in metadata.model_references
    ]:
        raise ValueError(
            f"fresh W3D model references disagree with visual closure: {virtual_path}"
        )
    return metadata


def _metadata_evidence(
    virtual_path: str,
    metadata: W3DMetadata,
    sources: Mapping[str, Mapping[str, Any]],
) -> dict[str, Any]:
    return {
        "source": _source_record(
            virtual_path,
            sources,
            expected_sha256=metadata.source_sha256,
            expected_size=metadata.byte_length,
        ),
        "headerIds": _fresh_header_ids(metadata),
        "modelHierarchyIds": sorted(
            {
                header.hierarchy_identifier
                for header in metadata.model_headers
                if header.hierarchy_identifier
            },
            key=lambda value: (value.casefold(), value),
        ),
        "animationHeaders": [
            {
                "identifier": header.identifier,
                "hierarchyIdentifier": header.hierarchy_identifier,
                "frameCount": header.frame_count,
                "frameRate": header.frame_rate,
                "compressed": header.compressed,
            }
            for header in metadata.animation_headers
        ],
        "meshHeaderCount": len(metadata.mesh_headers),
        "hierarchyPivotCount": len(metadata.hierarchy_pivots),
        "animationChannelChunkCount": sum(
            1
            for chunk in metadata.chunks
            if chunk.chunk_name in _ANIMATION_CHANNEL_CHUNK_NAMES
        ),
        "warnings": [warning.neutral() for warning in metadata.warnings],
    }


def _animated_group_slug(
    prefix: str,
    model_path: str,
    hierarchy_path: str,
    animation_paths: tuple[str, ...],
) -> str:
    stem = (
        "".join(
            character if character.isalnum() else "-"
            for character in PurePosixPath(model_path).stem.casefold()
        ).strip("-")
        or "asset"
    )
    basis = "\n".join((model_path, hierarchy_path, *animation_paths))
    suffix = hashlib.sha256(basis.casefold().encode("utf-8")).hexdigest()[:12]
    available = 63 - len(prefix) - len(suffix) - 2
    return f"{prefix}-{stem[:available].rstrip('-')}-{suffix}"


def _validate_generated_profile(resources: list[dict[str, Any]]) -> bool:
    if not resources:
        return False
    payload = {
        "format": 1,
        "id": "animated-prop-fragment-validation",
        "pack": {"id": "animated-prop-fragment-validation-pack"},
        "resources": resources,
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-animated-profile-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(
            json.dumps(
                payload,
                sort_keys=True,
                ensure_ascii=False,
                separators=(",", ":"),
                allow_nan=False,
            ),
            encoding="utf-8",
        )
        parsed = ImportProfile.load(path)
    if len(parsed.resources) != len(resources):
        raise ValueError("generated animated ImportProfile changed resource count")
    return True


def build_retail_animated_prop_plan(
    visual_closure_report: Mapping[str, Any],
    static_prop_plan: Mapping[str, Any],
    hierarchical_prop_plan: Mapping[str, Any],
    effective_assets_manifest: Mapping[str, Any],
    map_objects_document: Mapping[str, Any],
    base_profile: Mapping[str, Any],
    effective_assets_root: Path | str,
) -> dict[str, Any]:
    """Return a deterministic plan for exact, converter-compatible animations."""

    report = _mapping(visual_closure_report, "retail visual closure")
    manifest = _mapping(effective_assets_manifest, "effective-assets manifest")
    sources, manifest_evidence = _validate_effective_manifest(manifest)
    closure = _validate_visual_closure(report, sources)
    (
        static_digest,
        hierarchical_digest,
        static_eligible,
        hierarchical_eligible,
        hierarchical_reasons,
    ) = _validate_upstream_plans(
        report,
        _mapping(static_prop_plan, "retail static-prop plan"),
        _mapping(hierarchical_prop_plan, "retail hierarchical-prop plan"),
        manifest,
        _mapping(map_objects_document, "SAGE map objects document"),
    )
    targets = sorted(closure["targets"], key=lambda value: (value.casefold(), value))
    placement_counts, placement_source = _validate_placement_document(
        _mapping(map_objects_document, "SAGE map objects document"), targets
    )
    base_digest, logical_targets, base_evidence = _validate_base_profile_semantics(
        _mapping(base_profile, "base import profile"), targets
    )
    physical_overlap = set(logical_targets) & (static_eligible | hierarchical_eligible)
    if physical_overlap:
        raise ValueError(
            "base-profile logical binding overlaps a physically planned target: "
            + ", ".join(sorted(physical_overlap))
        )
    if (static_eligible | hierarchical_eligible | set(hierarchical_reasons)) != set(
        targets
    ):
        raise ValueError("upstream visual plans do not partition closure targets")

    candidates = [
        target
        for target in targets
        if target not in static_eligible
        and target not in hierarchical_eligible
        and target not in logical_targets
        and placement_counts[target] > 0
    ]
    reader = _PrivateSourceReader(effective_assets_root, sources)
    eligible: list[dict[str, Any]] = []
    rejected: list[dict[str, Any]] = []
    target_metadata: dict[str, dict[str, W3DMetadata]] = {}
    eligible_no_clip_identifiers: dict[str, str] = {}

    for target in candidates:
        reasons: list[dict[str, Any]] = []
        target_record = closure["targetRecords"][target]
        if target_record.get("status") != "resolved":
            reasons.append(_reason("missing-object-definition"))
        object_summary = closure["objects"].get(target)
        if object_summary is None:
            reasons.append(_reason("missing-object-summary"))
        elif not object_summary.get("inheritanceComplete"):
            reasons.append(_reason("incomplete-object-inheritance"))
        for diagnostic in closure["diagnosticsByTarget"][target]:
            reasons.append(
                _reason(
                    "object-graph-diagnostic",
                    diagnosticCode=str(diagnostic.get("code", "unknown")),
                )
            )

        model_paths: set[str] = set()
        hierarchy_paths: set[str] = set()
        animation_paths: set[str] = set()
        animation_identifiers: dict[str, set[str]] = {}
        texture_paths: set[str] = set()
        leaves = closure["leavesByTarget"][target]
        unresolved = [
            leaf
            for leaf in leaves
            if leaf.get("status") in {"missing", "ambiguous", "invalid"}
        ]
        source_native_no_clips, accepted_no_clip_positions = (
            _source_native_no_clip_rows(target, unresolved)
        )
        blocking_unresolved = [
            leaf
            for position, leaf in enumerate(unresolved)
            if position not in accepted_no_clip_positions
        ]
        if blocking_unresolved:
            reasons.append(
                _reason(
                    "unresolved-visual-reference",
                    referenceCount=len(blocking_unresolved),
                    identifiers=sorted(
                        {str(item.get("identifier")) for item in blocking_unresolved},
                        key=lambda value: (value.casefold(), value),
                    ),
                    statuses=sorted(
                        {str(item.get("status")) for item in blocking_unresolved}
                    ),
                )
            )

        for leaf in leaves:
            kind = str(leaf.get("kind"))
            status = leaf.get("status")
            identifier = str(leaf.get("identifier"))
            if status == "resolved" and kind in {"model", "hierarchy", "animation"}:
                paths = list(leaf.get("physicalVirtualPaths", []))
                if (
                    len(paths) != 1
                    or PurePosixPath(paths[0]).suffix.casefold() != ".w3d"
                ):
                    reasons.append(
                        _reason(
                            f"{kind}-reference-not-one-exact-w3d",
                            identifier=identifier,
                            physicalPathCount=len(paths),
                        )
                    )
                    continue
                path = paths[0]
                if kind == "model":
                    model_paths.add(path)
                elif kind == "hierarchy":
                    hierarchy_paths.add(path)
                else:
                    animation_paths.add(path)
                    animation_identifiers.setdefault(path, set()).add(identifier)
            elif status == "resolved" and kind in _TEXTURE_LEAF_KINDS:
                paths = list(leaf.get("physicalVirtualPaths", []))
                if (
                    len(paths) != 1
                    or PurePosixPath(paths[0]).suffix.casefold()
                    not in _TEXTURE_SUFFIXES
                ):
                    reasons.append(
                        _reason(
                            "texture-reference-not-one-exact-image",
                            identifier=identifier,
                            physicalPathCount=len(paths),
                        )
                    )
                else:
                    texture_paths.add(paths[0])
            elif status == "semantic":
                if not (kind == "model" and identifier.casefold() == "none"):
                    reasons.append(
                        _reason(
                            "unsupported-semantic-visual-reference",
                            kind=kind,
                            identifier=identifier,
                        )
                    )
            elif status == "resolved" and kind not in {
                "model",
                "hierarchy",
                "animation",
                *_TEXTURE_LEAF_KINDS,
            }:
                reasons.append(_reason("unsupported-visual-leaf-kind", kind=kind))

        if not model_paths:
            reasons.append(_reason("no-physical-model-w3d"))
        elif len(model_paths) > 1:
            reasons.extend(
                [
                    _reason(
                        "multiple-physical-model-w3ds",
                        physicalVirtualPaths=sorted(
                            model_paths, key=lambda value: (value.casefold(), value)
                        ),
                    ),
                    _reason("multiple-model-lifecycle-not-supported"),
                ]
            )
        sealed_embedded_animation = False
        if not animation_paths and len(model_paths) == 1:
            sealed_model = closure["scanned"].get(next(iter(model_paths)))
            sealed_embedded_animation = bool(
                sealed_model is not None
                and sealed_model["headerIds"].get("animationIds")
            )
        if not animation_paths and not sealed_embedded_animation:
            reasons.append(_reason("no-authored-animation-w3d"))

        prerequisite_reasons = _unique_reasons(reasons)
        if prerequisite_reasons:
            rejected.append(
                {
                    "targetObject": target,
                    "placementCount": placement_counts[target],
                    "reasons": prerequisite_reasons,
                    "hierarchicalPlanReasons": deepcopy(
                        hierarchical_reasons.get(target, [])
                    ),
                }
            )
            continue

        model_path = next(iter(model_paths))
        model = _scan_closure_source(model_path, closure, reader)
        secondary_skin_mode = _has_clean_secondary_skin_streams(model)
        if model.warnings:
            reasons.append(
                _reason("model-w3d-scanner-warnings", warningCount=len(model.warnings))
            )
        if not model.mesh_headers:
            reasons.append(_reason("model-w3d-has-no-mesh-header"))
        elif any(
            header.vertex_count < 1 or header.face_count < 1
            for header in model.mesh_headers
        ):
            reasons.append(_reason("model-w3d-has-empty-mesh-header"))
        model_chunk_names = {chunk.chunk_name for chunk in model.chunks}
        missing_mesh_chunks = sorted({"vertices", "triangles"} - model_chunk_names)
        if missing_mesh_chunks:
            reasons.append(
                _reason(
                    "model-w3d-incomplete-mesh-payload",
                    missingChunkNames=missing_mesh_chunks,
                )
            )
        if not model.model_headers:
            reasons.append(_reason("model-w3d-has-no-model-header"))
        embedded_animation_mode = bool(model.animation_headers) and not animation_paths
        if model.animation_headers and animation_paths:
            reasons.append(
                _reason(
                    "model-w3d-mixes-embedded-and-split-animation-headers",
                    headerCount=len(model.animation_headers),
                )
            )
        if sealed_embedded_animation != embedded_animation_mode:
            reasons.append(_reason("embedded-animation-evidence-does-not-match-model"))
        unsupported_roles = sorted(
            {
                reference.role
                for reference in model.model_references
                if reference.role not in _SUPPORTED_W3D_MODEL_REFERENCE_ROLES
            }
        )
        if unsupported_roles:
            reasons.append(
                _reason("model-w3d-unsupported-reference-role", roles=unsupported_roles)
            )
        model_hierarchy_ids = sorted(
            {
                header.hierarchy_identifier
                for header in model.model_headers
                if header.hierarchy_identifier
            },
            key=lambda value: (value.casefold(), value),
        )
        _case_unique(model_hierarchy_ids, "model hierarchy id")
        if len(model_hierarchy_ids) != 1:
            reasons.append(
                _reason(
                    "model-w3d-hierarchy-id-not-unique",
                    hierarchyIds=model_hierarchy_ids,
                )
            )
            hierarchy_id = None
        else:
            hierarchy_id = model_hierarchy_ids[0]

        sealed_embedded = closure["embeddedByW3d"].get(model_path, [])
        scanned_texture_counts = Counter(
            reference.identifier.casefold() for reference in model.texture_references
        )
        sealed_texture_counts = Counter(
            str(item.get("identifier")).casefold() for item in sealed_embedded
        )
        if scanned_texture_counts != sealed_texture_counts:
            reasons.append(_reason("embedded-texture-evidence-does-not-match-model"))
        for dependency in sealed_embedded:
            if dependency.get("status") != "resolved":
                reasons.append(
                    _reason(
                        "embedded-texture-unresolved",
                        identifier=str(dependency.get("identifier")),
                        status=str(dependency.get("status")),
                    )
                )
                continue
            paths = list(dependency.get("physicalVirtualPaths", []))
            if (
                len(paths) != 1
                or PurePosixPath(paths[0]).suffix.casefold() not in _TEXTURE_SUFFIXES
            ):
                reasons.append(
                    _reason(
                        "embedded-texture-not-one-exact-image",
                        identifier=str(dependency.get("identifier")),
                        physicalPathCount=len(paths),
                    )
                )
            else:
                texture_paths.add(paths[0])

        animation_metadata: dict[str, W3DMetadata] = {}
        for animation_path in sorted(
            animation_paths, key=lambda value: (value.casefold(), value)
        ):
            metadata = _scan_closure_source(animation_path, closure, reader)
            animation_metadata[animation_path] = metadata
            if metadata.warnings:
                reasons.append(
                    _reason(
                        "animation-w3d-scanner-warnings",
                        physicalVirtualPath=animation_path,
                        warningCount=len(metadata.warnings),
                    )
                )
            if (
                metadata.model_ids
                or metadata.hierarchy_ids
                or metadata.texture_references
            ):
                reasons.append(
                    _reason(
                        "animation-w3d-is-not-animation-only",
                        physicalVirtualPath=animation_path,
                    )
                )
            if len(metadata.animation_headers) != 1:
                reasons.append(
                    _reason(
                        "animation-w3d-header-count-not-one",
                        physicalVirtualPath=animation_path,
                        headerCount=len(metadata.animation_headers),
                    )
                )
                continue
            header = metadata.animation_headers[0]
            exact_ids = {
                value.casefold() for value in metadata.file_headers().animation_ids
            }
            missing_ids = sorted(
                (
                    identifier
                    for identifier in animation_identifiers[animation_path]
                    if identifier.casefold() not in exact_ids
                ),
                key=lambda value: (value.casefold(), value),
            )
            if missing_ids:
                reasons.append(
                    _reason(
                        "authored-animation-id-does-not-match-w3d-header",
                        physicalVirtualPath=animation_path,
                        identifiers=missing_ids,
                    )
                )
            if hierarchy_id is not None and (
                header.hierarchy_identifier.casefold() != hierarchy_id.casefold()
            ):
                reasons.append(
                    _reason(
                        "animation-hierarchy-does-not-match-model",
                        physicalVirtualPath=animation_path,
                        animationHierarchyId=header.hierarchy_identifier,
                        modelHierarchyId=hierarchy_id,
                    )
                )
            if header.frame_count < 1 or header.frame_rate < 1:
                reasons.append(
                    _reason(
                        "animation-w3d-has-invalid-timing",
                        physicalVirtualPath=animation_path,
                    )
                )
            if not any(
                chunk.chunk_name in _ANIMATION_CHANNEL_CHUNK_NAMES
                and chunk.available_payload_size > 0
                for chunk in metadata.chunks
            ):
                reasons.append(
                    _reason(
                        "animation-w3d-has-no-key-channel",
                        physicalVirtualPath=animation_path,
                    )
                )

        if embedded_animation_mode:
            if len(model.animation_headers) != 1:
                reasons.append(
                    _reason(
                        "embedded-animation-header-count-not-one",
                        headerCount=len(model.animation_headers),
                    )
                )
            else:
                embedded_header = model.animation_headers[0]
                if hierarchy_id is not None and (
                    embedded_header.hierarchy_identifier.casefold()
                    != hierarchy_id.casefold()
                ):
                    reasons.append(
                        _reason(
                            "embedded-animation-hierarchy-does-not-match-model",
                            animationHierarchyId=(embedded_header.hierarchy_identifier),
                            modelHierarchyId=hierarchy_id,
                        )
                    )
                if embedded_header.frame_count < 1 or embedded_header.frame_rate < 1:
                    reasons.append(_reason("embedded-animation-has-invalid-timing"))
                if not any(
                    chunk.chunk_name in _ANIMATION_CHANNEL_CHUNK_NAMES
                    and chunk.available_payload_size > 0
                    for chunk in model.chunks
                ):
                    reasons.append(_reason("embedded-animation-has-no-key-channel"))

        hierarchy_path: str | None = None
        hierarchy_metadata: W3DMetadata | None = None
        secondary_skin_proof: dict[str, Any] | None = None
        if hierarchy_id is not None:
            if embedded_animation_mode:
                hierarchy_path = model_path
                hierarchy_metadata = model
                if hierarchy_paths and hierarchy_paths != {model_path}:
                    reasons.append(
                        _reason(
                            "authored-hierarchy-path-does-not-match-embedded-model",
                            authoredPaths=sorted(hierarchy_paths),
                            modelPath=model_path,
                        )
                    )
                embedded_hierarchy_ids = list(model.hierarchy_ids)
                if (
                    len(embedded_hierarchy_ids) != 1
                    or embedded_hierarchy_ids[0].casefold() != hierarchy_id.casefold()
                ):
                    reasons.append(
                        _reason(
                            "embedded-hierarchy-header-does-not-match-model",
                            modelHierarchyId=hierarchy_id,
                            hierarchyHeaderIds=embedded_hierarchy_ids,
                        )
                    )
                if not model.hierarchy_pivots:
                    reasons.append(_reason("embedded-hierarchy-has-no-pivots"))
            else:
                inferred = PurePosixPath(model_path).parent / (
                    hierarchy_id.casefold() + ".w3d"
                )
                inferred_path = reader.resolve_case(inferred.as_posix())
                if inferred_path is None:
                    inferred_path, basename_matches = reader.resolve_unique_basename(
                        hierarchy_id + ".w3d"
                    )
                    if inferred_path is None:
                        reasons.append(
                            _reason(
                                (
                                    "exact-model-hierarchy-w3d-missing"
                                    if not basename_matches
                                    else "exact-model-hierarchy-w3d-ambiguous"
                                ),
                                hierarchyId=hierarchy_id,
                                candidates=basename_matches,
                            )
                        )
                if inferred_path is not None:
                    hierarchy_path = inferred_path
                    if hierarchy_paths and hierarchy_paths != {hierarchy_path}:
                        reasons.append(
                            _reason(
                                "authored-hierarchy-path-does-not-match-model-header",
                                authoredPaths=sorted(hierarchy_paths),
                                inferredPath=hierarchy_path,
                            )
                        )
                    if hierarchy_path in closure["scanned"]:
                        hierarchy_metadata = _scan_closure_source(
                            hierarchy_path, closure, reader
                        )
                    else:
                        hierarchy_metadata = reader.scan(hierarchy_path)
                    if hierarchy_metadata.warnings:
                        reasons.append(
                            _reason(
                                "hierarchy-w3d-scanner-warnings",
                                warningCount=len(hierarchy_metadata.warnings),
                            )
                        )
                    hierarchy_ids = list(hierarchy_metadata.hierarchy_ids)
                    if (
                        len(hierarchy_ids) != 1
                        or hierarchy_ids[0].casefold() != hierarchy_id.casefold()
                    ):
                        reasons.append(
                            _reason(
                                "hierarchy-w3d-header-does-not-match-model",
                                modelHierarchyId=hierarchy_id,
                                hierarchyHeaderIds=hierarchy_ids,
                            )
                        )
                    if not hierarchy_metadata.hierarchy_pivots:
                        reasons.append(_reason("hierarchy-w3d-has-no-pivots"))
                    if (
                        hierarchy_metadata.model_ids
                        or hierarchy_metadata.animation_ids
                        or hierarchy_metadata.texture_references
                    ):
                        reasons.append(_reason("hierarchy-w3d-is-not-hierarchy-only"))

        if secondary_skin_mode and hierarchy_path and hierarchy_metadata:
            try:
                secondary_skin_proof = strip_proven_redundant_secondary_skin_streams(
                    reader.read(model_path), reader.read(hierarchy_path)
                ).proof.neutral()
            except W3DSecondarySkinError:
                reasons.append(_reason("secondary-skin-stream-proof-failed"))

        reasons = _unique_reasons(reasons)
        if reasons:
            rejected.append(
                {
                    "targetObject": target,
                    "placementCount": placement_counts[target],
                    "reasons": reasons,
                    "hierarchicalPlanReasons": deepcopy(
                        hierarchical_reasons.get(target, [])
                    ),
                }
            )
            continue
        assert hierarchy_id is not None
        assert hierarchy_path is not None
        assert hierarchy_metadata is not None
        for texture_path in sorted(
            texture_paths, key=lambda value: (value.casefold(), value)
        ):
            reader.read(texture_path)
        if embedded_animation_mode:
            animation_source_mode = "model-embedded"
            animation_path_tuple = (model_path,)
            authored_animation_identifiers = [
                {
                    "physicalVirtualPath": model_path,
                    "identifiers": sorted(
                        model.file_headers().animation_ids,
                        key=lambda value: (value.casefold(), value),
                    ),
                }
            ]
            animation_metadata[model_path] = model
        else:
            animation_source_mode = "split-w3d"
            animation_path_tuple = tuple(
                sorted(animation_paths, key=lambda value: (value.casefold(), value))
            )
            authored_animation_identifiers = [
                {
                    "physicalVirtualPath": path,
                    "identifiers": sorted(
                        animation_identifiers[path],
                        key=lambda value: (value.casefold(), value),
                    ),
                }
                for path in animation_path_tuple
            ]
        texture_path_tuple = tuple(
            sorted(texture_paths, key=lambda value: (value.casefold(), value))
        )
        eligible_row: dict[str, Any] = {
            "targetObject": target,
            "placementCount": placement_counts[target],
            "modelVirtualPath": model_path,
            "hierarchyVirtualPath": hierarchy_path,
            "hierarchyId": hierarchy_id,
            "animationSourceMode": animation_source_mode,
            "animationVirtualPaths": list(animation_path_tuple),
            "authoredAnimationIdentifiers": authored_animation_identifiers,
            "sourceNativeNoClipTransitions": source_native_no_clips,
            "textureVirtualPaths": list(texture_path_tuple),
        }
        if secondary_skin_proof is not None:
            eligible_row["secondarySkinProof"] = secondary_skin_proof
        eligible.append(eligible_row)
        for row in source_native_no_clips:
            logical_identifier = str(row["identifier"])
            header_identifier = str(row["headerIdentifier"])
            previous = eligible_no_clip_identifiers.setdefault(
                logical_identifier, header_identifier
            )
            if previous != header_identifier:
                raise ValueError("source-native no-clip header mapping is inconsistent")
        target_metadata[target] = {
            "model": model,
            "hierarchy": hierarchy_metadata,
            **{
                f"animation:{path}": animation_metadata[path]
                for path in animation_path_tuple
            },
        }

    no_clip_absence_proof = (
        reader.prove_ascii_identifiers_absent(eligible_no_clip_identifiers)
        if eligible_no_clip_identifiers
        else None
    )

    all_texture_paths = sorted(
        {path for target in eligible for path in target["textureVirtualPaths"]},
        key=lambda value: (value.casefold(), value),
    )
    texture_resources: dict[str, dict[str, Any]] = {}
    for path in all_texture_paths:
        resource_id = _animated_group_slug("animated-prop-texture", path, path, ())
        texture_resources[path] = {
            "id": resource_id,
            "kind": "texture",
            "patterns": [path],
            "required": True,
            "converter": "hash-only",
            "limit": 1,
            "expected_count": 1,
        }

    grouped: dict[tuple[Any, ...], list[str]] = {}
    eligible_by_target = {str(item["targetObject"]): item for item in eligible}
    for item in eligible:
        key = (
            item["modelVirtualPath"],
            item["hierarchyVirtualPath"],
            tuple(item["animationVirtualPaths"]),
            tuple(item["textureVirtualPaths"]),
        )
        grouped.setdefault(key, []).append(str(item["targetObject"]))

    closure_owners: dict[tuple[str, tuple[str, ...]], list[tuple[Any, ...]]] = {}
    for key in grouped:
        model_path = str(key[0])
        hierarchy_path = str(key[1])
        animation_paths = tuple(str(path) for path in key[2])
        dependency_paths = {hierarchy_path, *animation_paths} - {model_path}
        if dependency_paths:
            closure_owners.setdefault((hierarchy_path, animation_paths), []).append(key)

    shared_w3d_resources: dict[tuple[str, tuple[str, ...]], dict[str, Any]] = {}
    shared_w3d_evidence: list[dict[str, Any]] = []
    for closure_key in sorted(closure_owners, key=lambda value: str(value).casefold()):
        owners = closure_owners[closure_key]
        if len(owners) < 2:
            continue
        hierarchy_path, animation_paths = closure_key
        patterns = sorted(
            {hierarchy_path, *animation_paths},
            key=lambda value: (value.casefold(), value),
        )
        resource_id = _animated_group_slug(
            "animated-prop-shared-w3d",
            hierarchy_path,
            hierarchy_path,
            animation_paths,
        )
        resource = {
            "id": resource_id,
            "kind": "data",
            "patterns": patterns,
            "required": True,
            "converter": "hash-only",
            "limit": len(patterns),
            "expected_count": len(patterns),
        }
        shared_w3d_resources[closure_key] = resource
        shared_w3d_evidence.append(
            {
                "resourceId": resource_id,
                "sourceCount": len(patterns),
                "sources": [_source_record(path, sources) for path in patterns],
                "consumerModelVirtualPaths": sorted(
                    {str(owner[0]) for owner in owners},
                    key=lambda value: (value.casefold(), value),
                ),
            }
        )

    model_resources: list[dict[str, Any]] = []
    conversion_groups: list[dict[str, Any]] = []
    binding_rows: list[dict[str, Any]] = []
    for key in sorted(grouped, key=lambda value: str(value).casefold()):
        model_path = str(key[0])
        hierarchy_path = str(key[1])
        animation_paths = tuple(str(path) for path in key[2])
        texture_paths = tuple(str(path) for path in key[3])
        group_targets = sorted(
            grouped[key], key=lambda value: (value.casefold(), value)
        )
        animation_source_modes = {
            str(eligible_by_target[target]["animationSourceMode"])
            for target in group_targets
        }
        if len(animation_source_modes) != 1:
            raise ValueError("animated conversion group mixes animation source modes")
        animation_source_mode = next(iter(animation_source_modes))
        group_no_clip_transitions = [
            {
                "targetObject": target,
                **deepcopy(transition),
            }
            for target in group_targets
            for transition in eligible_by_target[target][
                "sourceNativeNoClipTransitions"
            ]
        ]
        group_secondary_skin_proofs = [
            eligible_by_target[target]["secondarySkinProof"]
            for target in group_targets
            if "secondarySkinProof" in eligible_by_target[target]
        ]
        if group_secondary_skin_proofs and len(group_secondary_skin_proofs) != len(
            group_targets
        ):
            raise ValueError(
                "animated conversion group has partial secondary-skin proof"
            )
        if group_secondary_skin_proofs and any(
            proof != group_secondary_skin_proofs[0]
            for proof in group_secondary_skin_proofs[1:]
        ):
            raise ValueError("animated conversion group mixes secondary-skin proofs")
        resource_id = _animated_group_slug(
            "animated-prop-model", model_path, hierarchy_path, animation_paths
        )
        output_stem = resource_id.removeprefix("animated-prop-model-")
        output = f"assets/models/props-animated/{output_stem}.glb"
        closure_key = (hierarchy_path, animation_paths)
        shared_w3d_resource = shared_w3d_resources.get(closure_key)
        shared_w3d_patterns = (
            set(shared_w3d_resource["patterns"])
            if shared_w3d_resource is not None
            else set()
        )
        patterns = sorted(
            {model_path, hierarchy_path, *animation_paths} - shared_w3d_patterns,
            key=lambda value: (value.casefold(), value),
        )
        if model_path not in patterns:
            raise ValueError("shared W3D input resource claimed a model source")
        input_resource_ids = [texture_resources[path]["id"] for path in texture_paths]
        if shared_w3d_resource is not None:
            input_resource_ids.insert(0, str(shared_w3d_resource["id"]))
        model_options: dict[str, Any] = {
            "model": PurePosixPath(model_path).name,
            "animations": [PurePosixPath(path).name for path in animation_paths],
            "required_equipment": [],
            "inputResourceIds": input_resource_ids,
        }
        if group_no_clip_transitions:
            model_options["sourceNativeNoClipTransitions"] = group_no_clip_transitions
        model_resource = {
            "id": resource_id,
            "kind": "model",
            "patterns": patterns,
            "required": True,
            "converter": "w3d-bundle",
            "output": output,
            "limit": len(patterns),
            "expected_count": len(patterns),
            "options": model_options,
        }
        model_resources.append(model_resource)
        exemplar = group_targets[0]
        metadata = target_metadata[exemplar]
        conversion_groups.append(
            {
                "modelResourceId": resource_id,
                "targetObjects": group_targets,
                "placementCount": sum(
                    placement_counts[target] for target in group_targets
                ),
                "outputGlb": output,
                "animationSourceMode": animation_source_mode,
                "sharedW3dInputResourceIds": (
                    [str(shared_w3d_resource["id"])]
                    if shared_w3d_resource is not None
                    else []
                ),
                "sourceNativeNoClipTransitions": group_no_clip_transitions,
                "modelEvidence": _metadata_evidence(
                    model_path, metadata["model"], sources
                ),
                "hierarchyEvidence": _metadata_evidence(
                    hierarchy_path, metadata["hierarchy"], sources
                ),
                "animationEvidence": [
                    _metadata_evidence(path, metadata[f"animation:{path}"], sources)
                    for path in animation_paths
                ],
                "textureSources": [
                    _source_record(path, sources) for path in texture_paths
                ],
                **(
                    {"secondarySkinProof": group_secondary_skin_proofs[0]}
                    if group_secondary_skin_proofs
                    else {}
                ),
            }
        )
        for target in group_targets:
            binding_rows.append(
                {
                    "typeName": target,
                    "sourceVirtualModel": model_path,
                    "glb": output,
                    "matchMethod": "exact-type-name",
                }
            )

    resources = [
        *[texture_resources[path] for path in all_texture_paths],
        *[
            shared_w3d_resources[key]
            for key in sorted(
                shared_w3d_resources, key=lambda value: str(value).casefold()
            )
        ],
        *model_resources,
    ]
    if len(resources) > _MAX_PROFILE_RESOURCES:
        raise ValueError("animated-prop profile fragment exceeds resource limit")
    _case_unique([str(item["id"]) for item in resources], "profile resource id")
    _case_unique(
        [str(item["output"]) for item in model_resources],
        "profile model output",
    )
    # One owner per retail source path. Distinct animated groups can both need
    # the same W3D (e.g. cupig_anta as one unit's model and another's hierarchy).
    # First claimer keeps the pattern; later resources depend on it via
    # inputResourceIds instead of re-declaring the path.
    pattern_owners: dict[str, str] = {}
    for resource in resources:
        resource_id = str(resource["id"])
        kept_patterns: list[str] = []
        for pattern in list(resource["patterns"]):
            key = str(pattern).casefold()
            previous = pattern_owners.get(key)
            if previous is None:
                pattern_owners[key] = resource_id
                kept_patterns.append(str(pattern))
                continue
            if previous == resource_id:
                kept_patterns.append(str(pattern))
                continue
            options = resource.setdefault("options", {})
            if not isinstance(options, dict):
                options = {}
                resource["options"] = options
            input_ids = options.setdefault("inputResourceIds", [])
            if not isinstance(input_ids, list):
                input_ids = []
                options["inputResourceIds"] = input_ids
            if previous not in {str(value) for value in input_ids}:
                input_ids.insert(0, previous)
        resource["patterns"] = kept_patterns
        if "limit" in resource:
            resource["limit"] = len(kept_patterns)
        if "expected_count" in resource:
            resource["expected_count"] = len(kept_patterns)
        if not kept_patterns and str(resource.get("kind")) == "model":
            raise ValueError(
                "animated model resource lost every source pattern after "
                f"shared-path dedupe: {resource_id}"
            )
    profile_validated = _validate_generated_profile(resources)

    eligible_names = {str(item["targetObject"]) for item in eligible}
    rejected_names = {str(item["targetObject"]) for item in rejected}
    if eligible_names | rejected_names != set(candidates):
        raise ValueError("animated candidate target partition is incomplete")
    if eligible_names & rejected_names:
        raise ValueError("animated candidate target partitions overlap")
    logical_exclusions = [
        {
            "targetObject": target,
            "classification": logical_targets[target],
            "placementCount": placement_counts[target],
        }
        for target in sorted(
            logical_targets, key=lambda value: (value.casefold(), value)
        )
    ]
    candidate_placements = sum(placement_counts[target] for target in candidates)
    animated_placements = sum(placement_counts[target] for target in eligible_names)
    static_placements = sum(placement_counts[target] for target in static_eligible)
    hierarchical_placements = sum(
        placement_counts[target] for target in hierarchical_eligible
    )
    logical_placements = sum(placement_counts[target] for target in logical_targets)
    target_placements = sum(placement_counts.values())
    cumulative_physical = static_eligible | hierarchical_eligible | eligible_names
    reason_counts = Counter(
        str(reason["code"]) for target in rejected for reason in target["reasons"]
    )
    source_native_no_clip_count = sum(
        len(item["sourceNativeNoClipTransitions"]) for item in eligible
    )
    embedded_animation_target_count = sum(
        item["animationSourceMode"] == "model-embedded" for item in eligible
    )
    secondary_skin_proof_target_count = sum(
        "secondarySkinProof" in item for item in eligible
    )

    plan: dict[str, Any] = {
        "schema": ANIMATED_PROP_PLAN_SCHEMA,
        "schemaVersion": ANIMATED_PROP_PLAN_SCHEMA_VERSION,
        "sourceEvidence": {
            "visualClosureAggregateSha256": closure["reportDigest"],
            "w3dDependencyAggregateSha256": closure["dependencyDigest"],
            "staticPropPlanAggregateSha256": static_digest,
            "hierarchicalPropPlanAggregateSha256": hierarchical_digest,
            "baseProfileAggregateSha256": base_digest,
            "effectiveAssets": manifest_evidence,
            "baseProfileSemantics": base_evidence,
            "mapObjects": placement_source,
            "privateReadBoundary": reader.evidence(),
            **(
                {"sourceNativeNoClipAbsenceProof": no_clip_absence_proof}
                if no_clip_absence_proof is not None
                else {}
            ),
        },
        "policy": {
            "selection": (
                "exact-one-model-one-hierarchy-complete-available-animation-closure"
            ),
            "substitutesAllowed": False,
            "lifecycleOrMultipleModelTargetsAllowed": False,
            "embeddedSingleActionAllowed": True,
            "sourceNativeNoClipTransitionsRequireCompleteCorpusAbsenceProof": True,
            "secondarySkinStreamsRequireSemanticEquivalenceProof": True,
            "sharedW3dSourceOwnership": (
                "one-hash-only-data-resource-per-exact-reused-action-closure"
            ),
            "inferredHierarchyRule": (
                "exact-model-header-id-in-model-directory-or-unique-global-basename"
            ),
            "converter": "w3d-bundle",
            "roadsCountedAsPlacements": False,
            "profileFragmentValidatedByImportProfile": profile_validated,
        },
        "logicalExclusions": logical_exclusions,
        "candidateTargets": candidates,
        "eligibleTargets": eligible,
        "rejectedTargets": rejected,
        "sharedW3dInputs": shared_w3d_evidence,
        "conversionGroups": conversion_groups,
        "profileFragment": {
            "resources": resources,
            "objectBindings": {
                "models": sorted(
                    binding_rows,
                    key=lambda item: (
                        str(item["typeName"]).casefold(),
                        str(item["typeName"]),
                    ),
                )
            },
        },
        "placementCoverage": {
            "targetPlacementCount": target_placements,
            "staticPlanEligiblePlacementCount": static_placements,
            "hierarchicalPlanEligiblePlacementCount": hierarchical_placements,
            "logicalExcludedPlacementCount": logical_placements,
            "animatedCandidatePlacementCount": candidate_placements,
            "animatedBatchPlacementCount": animated_placements,
            "animatedRejectedPlacementCount": candidate_placements
            - animated_placements,
            "cumulativePhysicalPlannedPlacementCount": sum(
                placement_counts[target] for target in cumulative_physical
            ),
            "remainingUnplannedPhysicalPlacementCount": candidate_placements
            - animated_placements,
            "byCandidate": [
                {
                    "targetObject": target,
                    "placementCount": placement_counts[target],
                    "coverageStage": (
                        "animated" if target in eligible_names else "uncovered"
                    ),
                }
                for target in candidates
            ],
        },
        "summary": {
            "visualClosureTargetTypeCount": len(targets),
            "logicalExcludedTargetTypeCount": len(logical_targets),
            "animatedCandidateTargetTypeCount": len(candidates),
            "eligibleTargetTypeCount": len(eligible),
            "rejectedTargetTypeCount": len(rejected),
            "conversionGroupCount": len(conversion_groups),
            "uniqueModelSourceCount": len(conversion_groups),
            "uniqueTextureSourceCount": len(all_texture_paths),
            "sharedW3dInputResourceCount": len(shared_w3d_resources),
            "sharedW3dInputSourceCount": sum(
                len(item["patterns"]) for item in shared_w3d_resources.values()
            ),
            "profileResourceCount": len(resources),
            "objectBindingModelRowCount": len(binding_rows),
            "sourceNativeNoClipTransitionCount": source_native_no_clip_count,
            "embeddedAnimationTargetTypeCount": embedded_animation_target_count,
            "secondarySkinProofTargetTypeCount": secondary_skin_proof_target_count,
            "animatedCandidatePlacementCount": candidate_placements,
            "animatedBatchPlacementCount": animated_placements,
            "remainingUnplannedPhysicalPlacementCount": candidate_placements
            - animated_placements,
            "rejectionReasonCounts": [
                {"code": code, "targetCount": reason_counts[code]}
                for code in sorted(reason_counts)
            ],
        },
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    return plan


def generated_import_profile(
    plan: Mapping[str, Any],
    *,
    profile_id: str = "men-fords-v0-animated-props-generated",
    pack_id: str = "bfme2-men-vslice-animated-props-private",
) -> dict[str, Any]:
    """Return a standalone, non-publishing ImportProfile for the fragment."""

    document = _mapping(plan, "retail animated-prop plan")
    if document.get("schema") != ANIMATED_PROP_PLAN_SCHEMA:
        raise ValueError("unsupported retail animated-prop plan schema")
    _validate_declared_digest(document, "aggregateSha256", "retail animated-prop plan")
    fragment = _mapping(document.get("profileFragment"), "profileFragment")
    resources = fragment.get("resources")
    if not isinstance(resources, list) or not resources:
        raise ValueError("animated-prop plan has no importable resources")
    profile = {
        "format": 1,
        "id": profile_id,
        "title": "Private BFME II Fords exact animated-prop batch",
        "pack": {
            "id": pack_id,
            "version": "1.06-plan-v0",
            "dataPolicy": {
                "externalPathsAllowed": False,
                "redistributable": False,
            },
        },
        "resources": deepcopy(resources),
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-animated-generated-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(profile), encoding="utf-8")
        ImportProfile.load(path)
    return profile


def load_retail_animated_prop_plan_inputs(
    visual_closure_path: Path | str,
    static_prop_plan_path: Path | str,
    hierarchical_prop_plan_path: Path | str,
    effective_assets_manifest_path: Path | str,
    map_objects_path: Path | str,
    base_profile_path: Path | str,
) -> tuple[dict[str, Any], ...]:
    """Load bounded JSON inputs; semantic and byte validation occurs in build."""

    def load(path: Path | str, label: str, max_bytes: int) -> dict[str, Any]:
        source = Path(path).expanduser().resolve()
        if not source.is_file() or source.stat().st_size > max_bytes:
            raise ValueError(f"{label} is missing or exceeds {max_bytes} bytes")
        try:
            value = json.loads(source.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ValueError(f"invalid {label}: {exc}") from exc
        if not isinstance(value, dict):
            raise ValueError(f"{label} root must be an object")
        return value

    maximum = 64 * 1024 * 1024
    return (
        load(visual_closure_path, "retail visual closure", maximum),
        load(static_prop_plan_path, "retail static-prop plan", maximum),
        load(
            hierarchical_prop_plan_path,
            "retail hierarchical-prop plan",
            maximum,
        ),
        load(effective_assets_manifest_path, "effective-assets manifest", maximum),
        load(map_objects_path, "SAGE map objects", maximum),
        load(base_profile_path, "base import profile", maximum),
    )


def write_retail_animated_prop_plan(path: Path | str, plan: Mapping[str, Any]) -> None:
    """Atomically write one sealed payload-free animated-prop plan."""

    document = _mapping(plan, "retail animated-prop plan")
    if document.get("schema") != ANIMATED_PROP_PLAN_SCHEMA:
        raise ValueError("cannot write an unsupported animated-prop plan schema")
    _validate_declared_digest(document, "aggregateSha256", "retail animated-prop plan")
    write_json_atomic(Path(path), deepcopy(dict(document)))


def write_generated_import_profile(
    path: Path | str, profile: Mapping[str, Any]
) -> None:
    """Validate and atomically write a standalone generated ImportProfile."""

    document = _mapping(profile, "generated animated import profile")
    with tempfile.TemporaryDirectory(prefix="openbfme-animated-write-") as raw:
        check = Path(raw) / "profile.json"
        check.write_text(json.dumps(document), encoding="utf-8")
        ImportProfile.load(check)
    write_json_atomic(Path(path), deepcopy(dict(document)))


__all__ = [
    "ANIMATED_PROP_PLAN_SCHEMA",
    "ANIMATED_PROP_PLAN_SCHEMA_VERSION",
    "build_retail_animated_prop_plan",
    "generated_import_profile",
    "load_retail_animated_prop_plan_inputs",
    "write_generated_import_profile",
    "write_retail_animated_prop_plan",
]
