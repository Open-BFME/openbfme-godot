"""Blender-side adapter: OpenSAGE W3D model + clips -> one Godot-ready GLB.

Executed only by the pinned portable Blender process.  It intentionally has no
retail path defaults and writes only the single coordinator-provided output.
"""

from __future__ import annotations

import argparse
from collections import Counter
import copy as copy_module
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
from typing import Any, Callable, Iterable

import bpy


ADAPTER_REPORT_SCHEMA = "openbfme.w3d-adapter-report"
ADAPTER_REPORT_VERSION = 2

# RotWK 2.01's battlefield Uruk-family skins carry a rigid BARREL subobject
# whose HLOD binding points one past the end of this authored hierarchy. Keep
# the complete pivot oracle here so this retail compatibility path cannot turn
# into a general out-of-range fallback.
_RETAIL_URUK_BATTLEFIELD_PIVOTS = (
    "ROOTTRANSFORM",
    "B_SWORD",
    "B_HAND_R",
    "B_BOW",
    "B_HAND_L",
    "ROOT DUMMY",
    "B_WAIST",
    "BAT_RIBS",
    "B_NECK",
    "BAT_HEAD",
    "B_PTAIL1",
    "B_PTAIL2",
    "B_DREADS1",
    "B_DREADS2",
    "BAT_CLAVR",
    "BAT_UARMR",
    "BAT_FARMR",
    "B_HANDR",
    "BAT_CLAVL",
    "BAT_UARML",
    "B_FARML",
    "B_HANDL",
    "BAT_THIGHR",
    "B_CALFR",
    "B_HEELR",
    "B_FOOTR",
    "B_FCLOTH1",
    "B_FCLOTH2",
    "B_BCLOTH1",
    "B_BCLOTH2",
    "BAT_THIGHL",
    "B_CALFL",
    "B_HEELL",
    "B_FOOTL",
)
_RETAIL_ORPHAN_HLOD_PROPERTY = "openbfme_retail_orphan_hlod_exclusion"
_ACTIVE_RETAIL_ORPHAN_HLOD_EXCLUSIONS: list[dict[str, Any]] = []

_W3D_CONVERSION_FAILURE_PHASES = frozenset(
    {
        "scene-reset",
        "model-import",
        "embedded-model-import",
        "model-file-read",
        "model-chunk-header-read",
        "model-mesh-read",
        "model-hierarchy-read",
        "model-hlod-read",
        "model-animation-read",
        "model-compressed-animation-read",
        "model-box-read",
        "model-dazzle-read",
        "model-hierarchy-dependency-validation",
        "model-scene-collection",
        "model-scene-mesh-create",
        "model-scene-rig-create",
        "model-scene-mesh-bind",
        "model-scene-animation-create",
        "model-animation-setup",
        "model-animation-channel-processing",
        "model-animation-bone-resolution",
        "model-animation-channel-decode",
        "model-animation-keyframe-write",
        "model-animation-action-finalization",
        "model-animation-frame-reset",
        "model-load-complete",
        "model-direct-load-dispatch",
        "model-direct-load-result",
        "model-operator-dispatch",
        "model-operator-result",
        "animation-output-capture-setup",
        "animation-output-capture-restore",
        "animation-output-capture-accounting",
        "model-import-validation",
        "request-validation",
        "rig-validation",
        "rig-resolution",
        "action-validation",
        "geometry-validation",
        "material-validation",
        "additive-material-discovery",
        "additive-material-graph-validation",
        "additive-material-pixel-read",
        "additive-material-alpha-derivation",
        "additive-material-image-duplication",
        "additive-material-pixel-write",
        "additive-material-round-trip",
        "additive-material-alpha-link",
        "presentation-validation",
        "skin-validation",
        "attachment-validation",
        "attachment-canonicalization",
        "mesh-object-type-validation",
        "mesh-helper-filter-validation",
        "mesh-box-ambiguity-validation",
        "mesh-equipment-classification",
        "required-equipment-validation",
        "render-proof",
        "scene-validation",
        "animation-import",
        "post-animation-validation",
        "animation-sidecar-mesh-strip",
        "attachment-restoration",
        "render-revalidation",
        "generated-image-validation",
        "shader-material-validation",
        "animation-export-preparation",
        "export",
        "glb-validation",
        "report-validation",
    }
)
_W3D_CONVERSION_FAILURE_KINDS = frozenset(
    {
        "assertion",
        "memory",
        "timeout",
        "os",
        "key",
        "type",
        "value",
        "runtime",
        "application",
        "control-flow",
    }
)
_W3D_CONVERSION_PHASE_ERROR_MESSAGE = (
    "W3D conversion failed with sanitized phase evidence"
)


class W3DConversionPhaseError(RuntimeError):
    """Expose only bounded converter failure evidence to the batch host."""

    __slots__ = ("_evidence",)

    def __init__(self, failure_phase: str, failure_kind: str) -> None:
        if (
            failure_phase not in _W3D_CONVERSION_FAILURE_PHASES
            or failure_kind not in _W3D_CONVERSION_FAILURE_KINDS
        ):
            raise ValueError("invalid sanitized W3D conversion failure evidence")
        super().__init__(_W3D_CONVERSION_PHASE_ERROR_MESSAGE)
        object.__setattr__(self, "_evidence", (failure_phase, failure_kind))

    def __setattr__(self, name: str, value: Any) -> None:
        if name in {"_evidence", "failure_phase", "failure_kind"}:
            raise AttributeError("W3D conversion failure evidence is read-only")
        super().__setattr__(name, value)

    @property
    def failure_phase(self) -> str:
        return self._evidence[0]

    @property
    def failure_kind(self) -> str:
        return self._evidence[1]


class _W3DConversionPhaseCheckpoint:
    """Track the current converter phase without retaining private inputs."""

    __slots__ = ("_failure_phase",)

    def __init__(self) -> None:
        self._failure_phase = "model-import"

    @property
    def failure_phase(self) -> str:
        return self._failure_phase

    def set(self, failure_phase: str) -> None:
        if failure_phase not in _W3D_CONVERSION_FAILURE_PHASES:
            raise ValueError("invalid W3D conversion phase checkpoint")
        self._failure_phase = failure_phase


def _set_optional_phase_checkpoint(
    phase_checkpoint: _W3DConversionPhaseCheckpoint | None,
    failure_phase: str,
) -> None:
    if phase_checkpoint is not None:
        phase_checkpoint.set(failure_phase)


class _NoopModelImportPhaseScope:
    """Testing-only scope used before the pinned plugin is initialized."""

    __slots__ = ("_checkpoint",)

    def __init__(self, checkpoint: _W3DConversionPhaseCheckpoint) -> None:
        self._checkpoint = checkpoint

    def __enter__(self) -> _NoopModelImportPhaseScope:
        return self

    def __exit__(
        self,
        _error_type: type[BaseException] | None,
        _error: BaseException | None,
        _traceback: Any,
    ) -> bool:
        return False

    def raise_if_failed(self) -> None:
        return None

    def invoke(self, source: Path) -> Any:
        self._checkpoint.set("model-operator-dispatch")
        try:
            result = bpy.ops.import_mesh.westwood_w3d(filepath=str(source))
        except BaseException:
            self._checkpoint.set("model-operator-dispatch")
            raise
        self._checkpoint.set("model-operator-result")
        return result


class _SilentPinnedW3DImportContext:
    """Provide the pinned loader's reporting surface without retaining messages."""

    __slots__ = ("file_format", "filepath")

    def __init__(self, source: Path) -> None:
        # Match the pinned operator's actual instance state. Its execute method
        # assigns a local ``file_format`` variable and leaves this property empty.
        self.file_format = ""
        self.filepath = str(source)

    @staticmethod
    def info(_message: object) -> None:
        return None

    @staticmethod
    def warning(_message: object) -> None:
        return None

    @staticmethod
    def error(_message: object) -> None:
        return None


class _PinnedModelImportPhaseScope:
    """Install scoped, identity-checked checkpoints around pinned importer calls."""

    __slots__ = (
        "_animation_import",
        "_checkpoint",
        "_import_utils",
        "_import_w3d",
        "_captured_failure_phase",
        "_entered",
        "_restorations",
        "_used",
    )

    def __init__(
        self,
        checkpoint: _W3DConversionPhaseCheckpoint,
        *,
        import_w3d_module: Any = None,
        import_utils_module: Any = None,
        animation_import_module: Any = None,
    ) -> None:
        self._checkpoint = checkpoint
        self._import_w3d = import_w3d_module
        self._import_utils = import_utils_module
        self._animation_import = animation_import_module
        self._captured_failure_phase: str | None = None
        self._entered = False
        self._restorations: list[tuple[Any, str, Any, Any, bool]] = []
        self._used = False

    def _capture_failure(self, phase: str) -> None:
        if self._captured_failure_phase is None:
            self._captured_failure_phase = phase

    def _wrap_callable(
        self,
        owner: Any,
        name: str,
        phase: str,
        *,
        static: bool = False,
    ) -> None:
        original = getattr(owner, name, None)
        if not callable(original):
            self._checkpoint.set("model-import-validation")
            raise RuntimeError("pinned W3D importer phase target is unavailable")

        def phased(*args: Any, **kwargs: Any) -> Any:
            self._checkpoint.set(phase)
            try:
                return original(*args, **kwargs)
            except BaseException:
                self._capture_failure(phase)
                raise

        replacement: Any = staticmethod(phased) if static else phased
        setattr(owner, name, replacement)
        if getattr(owner, name, None) is not phased:
            self._checkpoint.set("model-import-validation")
            raise RuntimeError("pinned W3D importer phase wrapper was not installed")
        self._restorations.append((owner, name, original, phased, static))

    def _wrap_load(self) -> None:
        owner = self._import_w3d
        original = getattr(owner, "load", None)
        if not callable(original):
            self._checkpoint.set("model-import-validation")
            raise RuntimeError("pinned W3D importer load entrypoint is unavailable")

        def phased_load(*args: Any, **kwargs: Any) -> Any:
            self._checkpoint.set("model-file-read")
            try:
                result = original(*args, **kwargs)
            except BaseException:
                self._capture_failure(self._checkpoint.failure_phase)
                raise
            if result != {"FINISHED"}:
                phase = "model-hierarchy-dependency-validation"
                self._checkpoint.set(phase)
                self._capture_failure(phase)
                raise RuntimeError("pinned W3D importer did not finish")
            self._checkpoint.set("model-load-complete")
            return result

        setattr(owner, "load", phased_load)
        if getattr(owner, "load", None) is not phased_load:
            self._checkpoint.set("model-import-validation")
            raise RuntimeError("pinned W3D importer load wrapper was not installed")
        self._restorations.append((owner, "load", original, phased_load, False))

    def _wrap_animation_create(self) -> None:
        owner = self._import_utils
        animation_import = self._animation_import
        original = getattr(owner, "create_animation", None)
        if not callable(original) or original is not getattr(
            animation_import, "create_animation", None
        ):
            self._checkpoint.set("model-import-validation")
            raise RuntimeError("pinned W3D animation importer binding is invalid")

        def phased_create_animation(
            context: Any,
            rig: Any,
            animation: Any,
            hierarchy: Any,
        ) -> None:
            self._checkpoint.set("model-scene-animation-create")
            if animation is None:
                return
            try:
                self._checkpoint.set("model-animation-setup")
                animation_import.setup_animation(animation)
                self._checkpoint.set("model-animation-channel-processing")
                if isinstance(animation, animation_import.CompressedAnimation):
                    animation_import.process_channels(
                        context,
                        hierarchy,
                        animation.time_coded_channels,
                        rig,
                        animation_import.apply_timecoded,
                    )
                    animation_import.process_channels(
                        context,
                        hierarchy,
                        animation.adaptive_delta_channels,
                        rig,
                        animation_import.apply_adaptive_delta,
                    )
                    animation_import.process_motion_channels(
                        context,
                        hierarchy,
                        animation.motion_channels,
                        rig,
                    )
                else:
                    animation_import.process_channels(
                        context,
                        hierarchy,
                        animation.channels,
                        rig,
                        animation_import.apply_uncompressed,
                    )
                self._checkpoint.set("model-animation-action-finalization")
                if (
                    rig is not None
                    and rig.animation_data is not None
                    and rig.animation_data.action is not None
                ):
                    rig.animation_data.action.name = animation.header.name
                elif (
                    rig is not None
                    and rig.data is not None
                    and rig.data.animation_data is not None
                    and rig.data.animation_data.action is not None
                ):
                    rig.data.animation_data.action.name = animation.header.name
                self._checkpoint.set("model-animation-frame-reset")
                animation_import.bpy.context.scene.frame_set(0)
            except BaseException:
                self._capture_failure(self._checkpoint.failure_phase)
                raise

        setattr(owner, "create_animation", phased_create_animation)
        if getattr(owner, "create_animation", None) is not phased_create_animation:
            self._checkpoint.set("model-import-validation")
            raise RuntimeError("pinned W3D animation phase wrapper was not installed")
        self._restorations.append(
            (
                owner,
                "create_animation",
                original,
                phased_create_animation,
                False,
            )
        )

    def _restore(self) -> None:
        mismatched = False
        for owner, name, original, replacement, static in reversed(self._restorations):
            if getattr(owner, name, None) is not replacement:
                mismatched = True
                continue
            setattr(owner, name, staticmethod(original) if static else original)
            if getattr(owner, name, None) is not original:
                mismatched = True
        self._restorations.clear()
        self._entered = False
        if mismatched:
            self._checkpoint.set("model-import-validation")
            raise RuntimeError("pinned W3D importer phase wrappers were not restored")

    def __enter__(self) -> _PinnedModelImportPhaseScope:
        if self._used or self._entered or self._restorations:
            raise RuntimeError("pinned W3D importer phase scope cannot be reused")
        self._used = True
        provided_modules = (
            self._import_w3d is not None,
            self._import_utils is not None,
            self._animation_import is not None,
        )
        if any(provided_modules) and not all(provided_modules):
            self._checkpoint.set("model-import-validation")
            raise RuntimeError("pinned W3D importer modules must be injected together")
        if not any(provided_modules):
            from io_mesh_w3d import import_utils  # type: ignore
            from io_mesh_w3d.common.utils import animation_import  # type: ignore
            from io_mesh_w3d.w3d import import_w3d  # type: ignore

            self._import_w3d = import_w3d
            self._import_utils = import_utils
            self._animation_import = animation_import
        self._entered = True
        try:
            self._wrap_callable(self._import_w3d, "load_file", "model-file-read")
            self._wrap_callable(
                self._import_w3d,
                "read_chunk_head",
                "model-chunk-header-read",
            )
            for class_name, phase in (
                ("Mesh", "model-mesh-read"),
                ("Hierarchy", "model-hierarchy-read"),
                ("HLod", "model-hlod-read"),
                ("Animation", "model-animation-read"),
                ("CompressedAnimation", "model-compressed-animation-read"),
                ("CollisionBox", "model-box-read"),
                ("Dazzle", "model-dazzle-read"),
            ):
                owner = getattr(self._import_w3d, class_name, None)
                if owner is None:
                    self._checkpoint.set("model-import-validation")
                    raise RuntimeError("pinned W3D importer reader is unavailable")
                self._wrap_callable(owner, "read", phase, static=True)
            self._wrap_callable(
                self._import_w3d, "create_data", "model-scene-collection"
            )
            for name, phase in (
                ("get_collection", "model-scene-collection"),
                ("create_mesh", "model-scene-mesh-create"),
                ("create_box", "model-scene-mesh-create"),
                ("create_dazzle", "model-scene-mesh-create"),
                ("get_or_create_skeleton", "model-scene-rig-create"),
                ("rig_mesh", "model-scene-mesh-bind"),
                ("rig_box", "model-scene-mesh-bind"),
                ("rig_object", "model-scene-mesh-bind"),
            ):
                self._wrap_callable(self._import_utils, name, phase)
            for name, phase in (
                ("setup_animation", "model-animation-setup"),
                ("process_channels", "model-animation-channel-processing"),
                (
                    "process_motion_channels",
                    "model-animation-channel-processing",
                ),
                ("get_bone", "model-animation-bone-resolution"),
                ("apply_timecoded", "model-animation-channel-decode"),
                (
                    "apply_motion_channel_time_coded",
                    "model-animation-channel-decode",
                ),
                (
                    "apply_motion_channel_adaptive_delta",
                    "model-animation-channel-decode",
                ),
                ("apply_adaptive_delta", "model-animation-channel-decode"),
                ("apply_uncompressed", "model-animation-channel-decode"),
                ("set_translation", "model-animation-keyframe-write"),
                ("set_rotation", "model-animation-keyframe-write"),
                ("set_visibility", "model-animation-keyframe-write"),
            ):
                self._wrap_callable(self._animation_import, name, phase)
            self._wrap_animation_create()
            self._wrap_load()
        except BaseException:
            try:
                self._restore()
            except BaseException:
                self._checkpoint.set("model-import-validation")
                self._captured_failure_phase = "model-import-validation"
                raise RuntimeError(
                    "pinned W3D importer phase wrapper cleanup failed"
                ) from None
            raise
        return self

    def __exit__(
        self,
        _error_type: type[BaseException] | None,
        _error: BaseException | None,
        _traceback: Any,
    ) -> bool:
        self._restore()
        if _error is not None and self._captured_failure_phase is not None:
            self._checkpoint.set(self._captured_failure_phase)
        return False

    def raise_if_failed(self) -> None:
        if self._captured_failure_phase is None:
            return
        self._checkpoint.set(self._captured_failure_phase)
        raise RuntimeError("pinned W3D importer failed within a sanitized phase")

    def invoke(self, source: Path) -> Any:
        self._checkpoint.set("model-direct-load-dispatch")
        try:
            result = self._import_w3d.load(_SilentPinnedW3DImportContext(source))
        except BaseException:
            if self._captured_failure_phase is not None:
                self._checkpoint.set(self._captured_failure_phase)
            else:
                self._checkpoint.set("model-direct-load-dispatch")
            raise
        self._checkpoint.set("model-direct-load-result")
        return result


def _w3d_conversion_failure_kind(error: BaseException) -> str:
    """Classify a failure without inspecting or rendering its payload."""

    if isinstance(error, AssertionError):
        return "assertion"
    if isinstance(error, MemoryError):
        return "memory"
    if isinstance(error, TimeoutError):
        return "timeout"
    if isinstance(error, OSError):
        return "os"
    if isinstance(error, KeyError):
        return "key"
    if isinstance(error, TypeError):
        return "type"
    if isinstance(error, ValueError):
        return "value"
    if isinstance(error, RuntimeError):
        return "runtime"
    if isinstance(error, Exception):
        return "application"
    return "control-flow"


def parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin-root", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument(
        "--asset-kind",
        choices=("animated", "hierarchical", "static"),
        default="animated",
    )
    parser.add_argument("--animations", type=Path, nargs="*", default=[])
    parser.add_argument("--required-equipment", nargs="*", default=[])
    parser.add_argument("--excluded-optional-meshes", nargs="*", default=[])
    parser.add_argument("--retail-absent-textures", nargs="*", default=[])
    parser.add_argument("--proven-root-rigid-bake", action="store_true")
    parser.add_argument("--proven-pivot-only-model", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args(argv)


def clean_name(value: str) -> str:
    return re.sub(r"[^a-z0-9_]+", "_", value.casefold()).strip("_")


def _w3d_fixed_string(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", "replace")


def _walk_w3d_chunks(source: bytes, start: int, end: int):
    position = start
    while position + 8 <= end:
        chunk_id, raw_size = struct.unpack_from("<II", source, position)
        declared = raw_size & W3D_CHUNK_SIZE_MASK
        has_subchunks = bool(raw_size & W3D_SUBCHUNK_FLAG)
        payload_offset = position + 8
        available = min(declared, end - payload_offset)
        yield chunk_id, payload_offset, available, has_subchunks
        if has_subchunks:
            yield from _walk_w3d_chunks(source, payload_offset, payload_offset + available)
        position = payload_offset + available


def w3d_hidden_mesh_names(source: bytes) -> frozenset[str]:
    """Return mesh names whose W3D attributes include HIDDEN (0x00001000)."""

    names: set[str] = set()
    for chunk_id, payload_offset, available, _has_sub in _walk_w3d_chunks(
        source, 0, len(source)
    ):
        if chunk_id != W3D_MESH_HEADER_CHUNK or available < 36:
            continue
        _version, attributes, raw_name, _container = struct.unpack_from(
            "<II16s16s", source, payload_offset
        )
        if attributes & W3D_MESH_FLAG_HIDDEN:
            name = _w3d_fixed_string(raw_name)
            if name:
                names.add(name)
    return frozenset(names)


def w3d_hierarchy_pivots(source: bytes) -> list[dict[str, Any]]:
    """Return hierarchy pivot records (name, parent, local Z-up translation)."""

    pivots: list[dict[str, Any]] = []
    for chunk_id, payload_offset, available, _has_sub in _walk_w3d_chunks(
        source, 0, len(source)
    ):
        if chunk_id != W3D_HIERARCHY_PIVOTS_CHUNK:
            continue
        record_size = 60
        count, _remainder = divmod(available, record_size)
        for index in range(count):
            offset = payload_offset + index * record_size
            raw_name, parent_index, tx, ty, tz = struct.unpack_from(
                "<16sifff", source, offset
            )
            name = _w3d_fixed_string(raw_name)
            pivots.append(
                {
                    "name": name,
                    "index": index,
                    "parentIndex": parent_index,
                    "translation": (float(tx), float(ty), float(tz)),
                }
            )
    return pivots


def apply_w3d_hidden_extras(mesh_objects: list[Any], hidden_names: Iterable[str]) -> int:
    """Stamp extras.hidden=true onto objects/meshes whose W3D name is HIDDEN."""

    folded = {str(name).casefold() for name in hidden_names if str(name)}
    if not folded:
        return 0
    marked = 0
    for item in mesh_objects:
        labels = [
            str(getattr(item, "name", "")),
            str(getattr(getattr(item, "data", None), "name", "")),
        ]
        tokens: set[str] = set()
        for label in labels:
            if not label:
                continue
            tokens.add(label.casefold())
            if "." in label:
                tokens.add(label.rsplit(".", 1)[-1].casefold())
        if not tokens.intersection(folded):
            continue
        try:
            item["hidden"] = True
            data = getattr(item, "data", None)
            if data is not None:
                data["hidden"] = True
        except (AttributeError, TypeError, ValueError) as exc:
            raise RuntimeError("could not stamp W3D hidden extras") from exc
        marked += 1
    return marked



def normalize_optional_mesh_exclusions(value: Any) -> list[str]:
    if (
        not isinstance(value, list)
        or len(value) > MAX_OPTIONAL_MESH_EXCLUSIONS
        or any(not isinstance(identifier, str) for identifier in value)
    ):
        raise ValueError(
            f"excluded optional meshes must be an array of at most "
            f"{MAX_OPTIONAL_MESH_EXCLUSIONS} strings"
        )
    if len(value) != len(set(value)):
        raise ValueError("excluded optional meshes contain duplicates")
    for identifier in value:
        if not CLEAN_MESH_IDENTIFIER_PATTERN.fullmatch(identifier):
            raise ValueError(
                f"excluded optional mesh is not an exact clean identifier: {identifier!r}"
            )
    return sorted(value)


def normalize_retail_absent_textures(value: Any) -> list[str]:
    """Validate scanner-recorded retail-absent texture basenames."""

    if (
        not isinstance(value, list)
        or len(value) > MAX_RETAIL_ABSENT_TEXTURES
        or any(not isinstance(basename, str) for basename in value)
    ):
        raise ValueError(
            f"retail-absent textures must be an array of at most "
            f"{MAX_RETAIL_ABSENT_TEXTURES} strings"
        )
    if len(value) != len(set(value)):
        raise ValueError("retail-absent textures contain duplicates")
    for basename in value:
        if (
            not TEXTURE_BASENAME_PATTERN.fullmatch(basename)
            or Path(basename).name != basename
            or Path(basename).suffix.casefold() not in TEXTURE_SUFFIXES
        ):
            raise ValueError(
                f"retail-absent texture is not a safe texture basename: {basename!r}"
            )
    return sorted(value)


def clear_retail_absent_textures(
    tolerated: list[str], *, staging_root: Path
) -> dict[str, list[str]]:
    """Unlink generated placeholders for recorded retail-absent textures.

    The pinned importer substitutes a generated color-grid image when a W3D
    references a texture that is absent from the staged closure. Retail ships
    models whose referenced texture was never published; the visual closure
    records each such reference as a ``retail-absent-texture`` exclusion. Only
    a generated placeholder whose authored name matches a recorded exclusion
    may be unlinked — every other generated image stays for the placeholder
    validation to reject. The material keeps all remaining channels; no
    substitute texture is invented.

    A recorded exclusion does not always become a placeholder. The pinned
    importer resolves exactly one texture per material pass
    (``tx_stages[0].tx_ids[0][0]``) and warns that it supports only one
    texture stage per pass, so a mesh that authors more textures than its
    passes reference — RotWK's ``mu_banr_w.w3d`` authors three across two
    passes — leaves the surplus name untouched by the import. Nothing was
    created, so nothing can leak, and the exclusion is returned as
    ``unreferenced`` evidence rather than aborting the conversion.

    Both remaining shapes stay fail-closed, because each would mean the
    recorded closure disagrees with the staged reality:

    * the excluded basename is present in the staged input directory, so it
      is not a retail-absent texture at all;
    * an image with that name survives the placeholder sweep, so the import
      resolved it from a real payload.
    """

    tolerated_stems = {
        Path(basename).stem.casefold(): basename for basename in tolerated
    }
    unmatched = dict(tolerated_stems)
    cleared: list[str] = []
    for image in list(getattr(bpy.data, "images", []) or []):
        if getattr(image, "source", None) != "GENERATED":
            continue
        image_name = str(getattr(image, "name", ""))
        stem = Path(image_name).stem.casefold()
        if stem not in tolerated_stems:
            continue
        for material in list(getattr(bpy.data, "materials", []) or []):
            node_tree = getattr(material, "node_tree", None)
            nodes = getattr(node_tree, "nodes", None)
            if nodes is None:
                continue
            for node in list(nodes):
                if getattr(node, "image", None) is image:
                    nodes.remove(node)
        bpy.data.images.remove(image)
        unmatched.pop(stem, None)
        cleared.append(image_name)
    if unmatched:
        staged_stems = {
            path.stem.casefold()
            for path in staging_root.iterdir()
            if path.is_file() and path.suffix.casefold() in TEXTURE_SUFFIXES
        }
        surviving_stems = {
            Path(str(getattr(image, "name", ""))).stem.casefold()
            for image in list(getattr(bpy.data, "images", []) or [])
        }
        for stem, basename in sorted(unmatched.items()):
            if stem in staged_stems:
                raise RuntimeError(
                    "retail-absent texture exclusion names a staged texture: "
                    f"{basename}"
                )
            if stem in surviving_stems:
                raise RuntimeError(
                    "retail-absent texture exclusion resolved to an imported "
                    f"texture: {basename}"
                )
    unreferenced = sorted(unmatched.values())
    if unreferenced:
        print(
            "OPENBFME_W3D_RETAIL_ABSENT_TEXTURE_UNREFERENCED "
            + json.dumps(unreferenced, sort_keys=True)
        )
    return {"cleared": sorted(cleared), "unreferenced": unreferenced}


def validate_asset_kind_request(
    asset_kind: str,
    animations: list[Any],
    required_equipment: list[str],
    *,
    proven_root_rigid_bake: bool = False,
    proven_pivot_only_model: bool = False,
) -> None:
    if asset_kind not in {"animated", "hierarchical", "static"}:
        raise ValueError(f"unsupported W3D asset kind: {asset_kind}")
    if asset_kind == "animated" and not animations:
        raise ValueError("animated W3D conversion requires at least one animation")
    if asset_kind != "animated" and animations:
        raise ValueError(f"{asset_kind} W3D conversion does not accept animations")
    if asset_kind != "animated" and required_equipment:
        raise ValueError(
            f"{asset_kind} W3D conversion does not accept required equipment"
        )
    if proven_root_rigid_bake and asset_kind != "hierarchical":
        raise ValueError(
            "proven root-rigid bake is supported only for hierarchical W3D conversion"
        )
    if proven_pivot_only_model and asset_kind != "hierarchical":
        raise ValueError(
            "proven pivot-only model is supported only for hierarchical W3D conversion"
        )
    if proven_pivot_only_model and proven_root_rigid_bake:
        raise ValueError(
            "proven pivot-only model cannot combine with proven root-rigid bake"
        )


RENDERABLE_W3D_OBJECT_TYPE = "MESH"
SUPPORTED_EQUIPMENT_ROLES = {"right-hand-weapon", "left-hand-shield"}
ATTACHMENT_MATRIX_TOLERANCE = 1.0e-6
CANONICAL_BONE_SEPARATION_RATIO = 0.80
ADDITIVE_BLEND_ENUM = 1
OPAQUE_SOURCE_BLEND_ENUM = 1
OPAQUE_DESTINATION_BLEND_ENUM = 0
ADDITIVE_ALPHA_EPSILON = 1.0e-8
ADDITIVE_PIXEL_ROUND_TRIP_TOLERANCE = (1.0 / 255.0) + 1.0e-6
# Byte color attributes quantize through sRGB bytes; the worst-case linear
# round-trip error of one exact conversion is bounded by two byte steps.
ADDITIVE_VERTEX_COLOR_ROUND_TRIP_TOLERANCE = (2.0 / 255.0) + 1.0e-6
SHADER_BOOLEAN_PROPERTY_TYPE = 7
SHADER_BOOLEAN_COMPATIBILITY_PROPERTIES = {
    "AlphaBlendingEnable": "openbfme_w3d_alpha_blending_enable",
    "FogEnable": "openbfme_w3d_fog_enable",
}
# RotWK 2.01 authors "None" as its explicit no-texture sentinel in shader
# material string properties. A scan of all 14,475 effective-assets W3Ds finds
# 119 sentinel references across 35 files, but only 9 of them sit in a
# property the pinned plugin ever resolves: NormalMap in 8 files (bbbags,
# cah_skull, cahero_gondor04/06/07, esbtemple, psupplies04, livingmap) and
# Texture_1 in gumaarms_sknl. The other 110 references are WaterPCATexture1/2/3,
# PCAFrothTexture and PCANoiseTexture on 26 water models, which the plugin
# never dispatches at all, so they cannot produce a placeholder. Exactly one
# of the nine, esbtemple.w3d, is reachable from any current profile.
RETAIL_W3D_NO_TEXTURE_SENTINEL = "None"
# The pinned plugin's find_texture dispatch set: the exact property names
# whose handler in material_import.create_material_from_shader_material
# (lines 147-199) calls find_texture. Every other name falls through to its
# "shader property not implemented" path and never touches the loader, so
# sanitizing anything outside this set would be a change without a cause.
# DamagedTexture and SpecMap are derived from that dispatch set rather than
# observed: neither occurs as a string property anywhere in retail.
SHADER_TEXTURE_PROPERTIES = frozenset(
    {
        "DamagedTexture",
        "DiffuseTexture",
        "NormalMap",
        "SpecMap",
        "Texture_0",
        "Texture_1",
    }
)
_ACTIVE_SHADER_TEXTURE_SENTINEL_DROPS: list[dict[str, Any]] = []
# The pinned importer deliberately omits the source root pivot, so a hierarchy
# whose only pivot is this one imports as an armature with zero bones.
OMITTED_ROOT_PIVOT_NAME = "ROOTTRANSFORM"
W3D_MESH_HEADER_CHUNK = 0x0000001F
W3D_HIERARCHY_PIVOTS_CHUNK = 0x00000102
W3D_MESH_FLAG_HIDDEN = 0x00001000
W3D_SUBCHUNK_FLAG = 0x80000000
W3D_CHUNK_SIZE_MASK = 0x7FFFFFFF
_ACTIVE_ROOT_RIGID_INERT_VERTEX_GROUPS: list[dict[str, Any]] = []
MAX_OPTIONAL_MESH_EXCLUSIONS = 64
CLEAN_MESH_IDENTIFIER_PATTERN = re.compile(r"^[a-z0-9](?:[a-z0-9_]{0,126}[a-z0-9])?$")
MAX_RETAIL_ABSENT_TEXTURES = 16
TEXTURE_BASENAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
TEXTURE_SUFFIXES = {".bmp", ".dds", ".jpeg", ".jpg", ".png", ".tga"}
REDUNDANT_KEYFRAME_WARNING = (
    b"Warning: Due to the setting 'Only Insert Needed', "
    b"1 keyframe(s) have not been inserted."
)
MAX_ANIMATION_IMPORT_CAPTURE_BYTES = 128 * 1024 * 1024
ANIMATION_IMPORT_CAPTURE_CHUNK_BYTES = 64 * 1024
HELPER_LABEL_MARKERS = (
    "aabox",
    "aggregate",
    "boundingbox",
    "collision",
    "collider",
    "helper",
    "hitbox",
    "obbox",
    "physicsproxy",
    "proxy",
    "shadowmesh",
    "shadowproxy",
    "triggervolume",
    "volume",
    "volumeproxy",
)
WEAPON_LABEL_MARKERS = ("blade", "sword", "weapon")
SHIELD_LABEL_MARKERS = ("buckler", "shield")
RIGHT_EXPLICIT_ATTACHMENT_MARKERS = (
    "bsword",
    "swordbone",
    "weaponr",
)
RIGHT_GENERIC_HAND_MARKERS = (
    "handr",
    "rhand",
    "righthand",
)
RIGHT_HAND_MARKERS = RIGHT_EXPLICIT_ATTACHMENT_MARKERS + RIGHT_GENERIC_HAND_MARKERS
LEFT_EXPLICIT_ATTACHMENT_MARKERS = (
    "bshield",
    "shieldbone",
    "shieldl",
)
LEFT_GENERIC_HAND_MARKERS = (
    "handl",
    "lefthand",
    "lhand",
)
LEFT_HAND_MARKERS = LEFT_EXPLICIT_ATTACHMENT_MARKERS + LEFT_GENERIC_HAND_MARKERS
ATTACHMENT_PROOF_METHODS = {
    "custom-attachment",
    "dominant-weight-group",
    "parent-bone",
    "rest-pose-proximity",
    "source-equipment-pivot",
    "weighted-hand-dominance",
    "weighted-hand-group",
}


def _flush_process_output() -> None:
    """Flush Blender's Python streams before changing process descriptors."""

    for stream in (sys.stdout, sys.stderr):
        try:
            stream.flush()
        except (AttributeError, OSError, ValueError):
            pass


def _write_all_fd(file_descriptor: int, payload: bytes | memoryview) -> None:
    remaining = memoryview(payload)
    while remaining:
        try:
            written = os.write(file_descriptor, remaining)
        except InterruptedError:
            continue
        if written <= 0:
            raise OSError("file-descriptor write made no progress")
        remaining = remaining[written:]


def _is_redundant_keyframe_warning_line(line: bytes) -> bool:
    return line in {
        REDUNDANT_KEYFRAME_WARNING + b"\n",
        REDUNDANT_KEYFRAME_WARNING + b"\r\n",
    }


def filter_redundant_keyframe_warning_bytes(payload: bytes) -> tuple[bytes, int]:
    """Remove only complete, byte-exact redundant keyframe warning lines."""

    filtered: list[bytes] = []
    suppressed = 0
    for line in payload.splitlines(keepends=True):
        if _is_redundant_keyframe_warning_line(line):
            suppressed += 1
        else:
            filtered.append(line)
    return b"".join(filtered), suppressed


class _AnimationImportStreamCapture:
    def __init__(self, path: Path, destination_fd: int) -> None:
        self.path = path
        self.destination_fd = destination_fd


class AnimationImportOutputLedger:
    """Hold bounded animation-import output until the whole adapter job succeeds."""

    def __init__(
        self,
        *,
        temp_dir: Path | None = None,
        max_bytes: int = MAX_ANIMATION_IMPORT_CAPTURE_BYTES,
    ) -> None:
        if type(max_bytes) is not int or max_bytes < 1:
            raise ValueError(
                "animation import capture limit must be a positive integer"
            )
        self._temp_dir = Path(
            tempfile.gettempdir() if temp_dir is None else temp_dir
        ).resolve()
        if not self._temp_dir.is_dir():
            raise FileNotFoundError(self._temp_dir)
        self._max_bytes = max_bytes
        self._captured_bytes = 0
        self._suppressed_total = 0
        self._destination_fds: tuple[int, int] | None = None
        self._records: list[tuple[_AnimationImportStreamCapture, ...]] = []
        self._finished = False

    def _ensure_destinations(self) -> tuple[int, int]:
        if self._destination_fds is not None:
            return self._destination_fds
        stdout_fd = os.dup(1)
        try:
            stderr_fd = os.dup(2)
        except BaseException:
            os.close(stdout_fd)
            raise
        self._destination_fds = (stdout_fd, stderr_fd)
        return self._destination_fds

    @staticmethod
    def _close_file_descriptors(file_descriptors: Iterable[int]) -> None:
        for file_descriptor in file_descriptors:
            try:
                os.close(file_descriptor)
            except OSError:
                pass

    def capture(
        self,
        operation: Callable[[], Any],
        *,
        operation_phase: str,
        phase_checkpoint: _W3DConversionPhaseCheckpoint | None = None,
    ) -> Any:
        """Run one W3D animation import with stdout and stderr redirected."""

        if operation_phase not in {"embedded-model-import", "animation-import"}:
            raise ValueError("animation import capture operation phase is invalid")
        if phase_checkpoint is not None:
            phase_checkpoint.set("animation-output-capture-setup")
        if self._finished:
            raise RuntimeError("animation import output ledger is already closed")
        destinations = self._ensure_destinations()
        streams: list[_AnimationImportStreamCapture] = []
        capture_fds: list[int] = []
        redirected: list[int] = []
        try:
            for index, destination_fd in enumerate(destinations):
                capture_fd, raw_path = tempfile.mkstemp(
                    prefix=f"openbfme-w3d-animation-{index}-",
                    suffix=".log",
                    dir=self._temp_dir,
                )
                capture = _AnimationImportStreamCapture(Path(raw_path), destination_fd)
                streams.append(capture)
                capture_fds.append(capture_fd)

            _flush_process_output()
            for target_fd, capture_fd in zip((1, 2), capture_fds, strict=True):
                os.dup2(capture_fd, target_fd)
                redirected.append(target_fd)
        except BaseException:
            for target_fd in redirected:
                os.dup2(destinations[target_fd - 1], target_fd)
            self._close_file_descriptors(capture_fds)
            for capture in streams:
                capture.path.unlink(missing_ok=True)
            raise
        else:
            self._close_file_descriptors(capture_fds)

        self._records.append(tuple(streams))
        operation_completed = False
        if phase_checkpoint is not None:
            phase_checkpoint.set(operation_phase)
        try:
            result = operation()
            operation_completed = True
        finally:
            if operation_completed and phase_checkpoint is not None:
                phase_checkpoint.set("animation-output-capture-restore")
            _flush_process_output()
            os.dup2(destinations[0], 1)
            os.dup2(destinations[1], 2)

        if phase_checkpoint is not None:
            phase_checkpoint.set("animation-output-capture-accounting")
        # The exact redundant-keyframe warning class the success replay
        # suppresses can dwarf all real output (one fortress build clip emits
        # >180 MB of it). Compact it out before accounting so the bound
        # measures real output while the exact suppressed count is retained.
        for capture in streams:
            payload = capture.path.read_bytes()
            filtered, suppressed = filter_redundant_keyframe_warning_bytes(payload)
            if suppressed:
                self._suppressed_total += suppressed
                capture.path.write_bytes(filtered)
        self._captured_bytes += sum(capture.path.stat().st_size for capture in streams)
        if self._captured_bytes > self._max_bytes:
            raise RuntimeError(
                "animation import output exceeded the bounded job capture"
            )
        return result

    def _captures(self) -> Iterable[_AnimationImportStreamCapture]:
        for record in self._records:
            yield from record

    @staticmethod
    def _count_suppressed(path: Path) -> int:
        with path.open("rb") as stream:
            return sum(
                1 for line in stream if _is_redundant_keyframe_warning_line(line)
            )

    @staticmethod
    def _replay_filtered(capture: _AnimationImportStreamCapture) -> None:
        with capture.path.open("rb") as stream:
            for line in stream:
                if not _is_redundant_keyframe_warning_line(line):
                    _write_all_fd(capture.destination_fd, line)

    @staticmethod
    def _replay_raw(capture: _AnimationImportStreamCapture) -> None:
        with capture.path.open("rb") as stream:
            while chunk := stream.read(ANIMATION_IMPORT_CAPTURE_CHUNK_BYTES):
                _write_all_fd(capture.destination_fd, chunk)

    def _cleanup(self) -> None:
        for capture in self._captures():
            capture.path.unlink(missing_ok=True)
        if self._destination_fds is not None:
            self._close_file_descriptors(self._destination_fds)
            self._destination_fds = None
        self._finished = True

    def replay_success(self) -> int:
        """Replay filtered output and return the exact suppressed line count."""

        if self._finished:
            raise RuntimeError("animation import output ledger is already closed")
        captures = list(self._captures())
        for capture in captures:
            self._replay_filtered(capture)
        suppressed = self._suppressed_total
        self._cleanup()
        return suppressed

    def replay_failure(self) -> None:
        """Replay all captured bytes without filtering after any job failure."""

        if self._finished:
            return
        first_error: BaseException | None = None
        for capture in self._captures():
            try:
                self._replay_raw(capture)
            except BaseException as error:
                if first_error is None:
                    first_error = error
        self._cleanup()
        if first_error is not None:
            raise RuntimeError("animation import output replay failed") from first_error


def _compact_label(value: Any) -> str:
    return re.sub(r"[^a-z0-9]+", "", str(value).casefold())


def _contains_marker(values: Iterable[Any], markers: Iterable[str]) -> bool:
    compact = [_compact_label(value) for value in values]
    return any(marker in value for value in compact for marker in markers)


def _custom_items(owner: Any) -> list[tuple[str, Any]]:
    try:
        return [
            (str(key), owner[key]) for key in sorted(owner.keys(), key=str.casefold)
        ]
    except (AttributeError, KeyError, TypeError):
        return []


def _w3d_object_type(item: Any) -> str | None:
    for owner in (getattr(item, "data", None), item):
        if owner is None:
            continue
        value = getattr(owner, "object_type", None)
        if value in (None, ""):
            for key, candidate in _custom_items(owner):
                if clean_name(key) == "object_type":
                    value = candidate
                    break
        if value not in (None, ""):
            return re.sub(r"[^A-Z0-9_]+", "_", str(value).upper()).strip("_")
    return None


def _custom_value_is_enabled(value: Any) -> bool:
    if value is None or value is False or value == 0:
        return False
    if isinstance(value, str) and clean_name(value) in {
        "",
        "false",
        "mesh",
        "none",
        "off",
    }:
        return False
    return True


def _non_render_reasons(item: Any) -> list[str]:
    """Return safe reason enums when a Blender mesh is W3D helper geometry."""

    reasons: set[str] = set()
    object_type = _w3d_object_type(item)
    if object_type is not None and object_type != RENDERABLE_W3D_OBJECT_TYPE:
        reasons.add("non-render-object-type")

    labels = [
        getattr(item, "name", ""),
        getattr(getattr(item, "data", None), "name", ""),
    ]
    if _contains_marker(labels, HELPER_LABEL_MARKERS):
        reasons.add("helper-semantic")

    for owner in (item, getattr(item, "data", None)):
        if owner is None:
            continue
        for key, value in _custom_items(owner):
            if not _custom_value_is_enabled(value):
                continue
            if key == _RETAIL_ORPHAN_HLOD_PROPERTY:
                reasons.add("retail-orphan-hlod-binding")
                continue
            # W3D ``userText`` is free-form authoring metadata. Retail render
            # meshes commonly contain disabled fields such as
            # ``Proxy_Geometry = <none>`` and ``Disable_Collisions = 0``.
            # Treat only an enabled helper-labelled property *key* as typed
            # helper evidence; scanning arbitrary values deletes real art.
            if _contains_marker((key,), HELPER_LABEL_MARKERS):
                reasons.add("custom-helper-semantic")
    return sorted(reasons)


def _dominant_weight_labels(item: Any) -> list[str]:
    groups = list(getattr(item, "vertex_groups", []) or [])
    names = {
        int(getattr(group, "index", index)): str(getattr(group, "name", ""))
        for index, group in enumerate(groups)
    }
    weights: Counter[int] = Counter()
    total = 0.0
    for vertex in getattr(getattr(item, "data", None), "vertices", []) or []:
        for assignment in getattr(vertex, "groups", []) or []:
            weight = float(getattr(assignment, "weight", 0.0))
            if weight <= 0.0:
                continue
            group_index = int(getattr(assignment, "group", -1))
            weights[group_index] += weight
            total += weight
    if total <= 0.0:
        return []
    # A rigid weapon/shield is overwhelmingly bound to its attachment bone.
    # Merely mentioning a hand among the many groups on a body skin is not proof.
    return sorted(
        (
            names[index]
            for index, weight in weights.items()
            if index in names and weight / total >= 0.75
        ),
        key=str.casefold,
    )


def _weighted_hand_labels(item: Any) -> list[str]:
    """Return hand-labelled deform groups with material influence.

    Equipment meshes in the retail W3D can be softly skinned across hand,
    forearm, and accessory bones, so no single group reaches the rigid 75%
    threshold above. A hand-named group carrying at least 2% of the mesh's
    aggregate deform weight is still direct rig evidence, while a mere unused
    group name is not.
    """

    groups = list(getattr(item, "vertex_groups", []) or [])
    names = {
        int(getattr(group, "index", index)): str(getattr(group, "name", ""))
        for index, group in enumerate(groups)
    }
    weights: Counter[int] = Counter()
    total = 0.0
    for vertex in getattr(getattr(item, "data", None), "vertices", []) or []:
        for assignment in getattr(vertex, "groups", []) or []:
            weight = float(getattr(assignment, "weight", 0.0))
            if weight <= 0.0:
                continue
            group_index = int(getattr(assignment, "group", -1))
            weights[group_index] += weight
            total += weight
    if total <= 0.0:
        return []
    return sorted(
        (
            names[index]
            for index, weight in weights.items()
            if index in names
            and weight / total >= 0.02
            and _contains_marker(
                (names[index],), RIGHT_HAND_MARKERS + LEFT_HAND_MARKERS
            )
        ),
        key=str.casefold,
    )


def _hand_weight_shares(item: Any) -> tuple[float, float]:
    groups = list(getattr(item, "vertex_groups", []) or [])
    names = {
        int(getattr(group, "index", index)): str(getattr(group, "name", ""))
        for index, group in enumerate(groups)
    }
    total = 0.0
    right = 0.0
    left = 0.0
    for vertex in getattr(getattr(item, "data", None), "vertices", []) or []:
        for assignment in getattr(vertex, "groups", []) or []:
            weight = float(getattr(assignment, "weight", 0.0))
            if weight <= 0.0:
                continue
            total += weight
            label = names.get(int(getattr(assignment, "group", -1)), "")
            if _contains_marker((label,), RIGHT_HAND_MARKERS):
                right += weight
            if _contains_marker((label,), LEFT_HAND_MARKERS):
                left += weight
    if total <= 0.0:
        return 0.0, 0.0
    return right / total, left / total


def _custom_attachment_labels(item: Any) -> list[str]:
    values: list[str] = []
    for owner in (item, getattr(item, "data", None)):
        if owner is None:
            continue
        for key, value in _custom_items(owner):
            compact_key = _compact_label(key)
            if any(
                marker in compact_key
                for marker in ("attach", "bone", "parent", "pivot", "socket")
            ):
                if isinstance(value, (str, int)) and _custom_value_is_enabled(value):
                    values.append(str(value))
    return sorted(set(values), key=str.casefold)


def _is_skinned(item: Any) -> bool:
    return bool(getattr(item, "vertex_groups", [])) and any(
        getattr(modifier, "type", "") == "ARMATURE"
        for modifier in (getattr(item, "modifiers", []) or [])
    )


def _is_box_geometry(item: Any) -> bool:
    """Detect an axis-aligned box in object-local space without using its name."""

    coordinates: set[tuple[float, float, float]] = set()
    for vertex in getattr(getattr(item, "data", None), "vertices", []) or []:
        coordinate = getattr(vertex, "co", None)
        if coordinate is None:
            return False
        try:
            values = tuple(round(float(coordinate[index]), 6) for index in range(3))
        except (IndexError, TypeError, ValueError):
            return False
        coordinates.add(values)
    if len(coordinates) != 8:
        return False
    axes = [{coordinate[index] for coordinate in coordinates} for index in range(3)]
    if any(len(axis) != 2 for axis in axes):
        return False
    expected = {(x, y, z) for x in axes[0] for y in axes[1] for z in axes[2]}
    item.data.calc_loop_triangles()
    return coordinates == expected and len(item.data.loop_triangles) == 12


def _rest_pose_item_centroid(item: Any) -> Any:
    vertices = list(getattr(getattr(item, "data", None), "vertices", []) or [])
    if not vertices or not hasattr(item, "matrix_world"):
        return None
    center = None
    try:
        for vertex in vertices:
            world = item.matrix_world @ vertex.co
            center = world.copy() if center is None else center + world
        center /= float(len(vertices))
    except (AttributeError, ReferenceError, RuntimeError, TypeError, ValueError):
        return None
    try:
        if not all(math.isfinite(float(center[index])) for index in range(3)):
            return None
    except (IndexError, TypeError, ValueError):
        return None
    return center


def _rest_pose_bone_distance(center: Any, rig: Any, bone: Any) -> float | None:
    try:
        local_points = (
            bone.head_local,
            bone.tail_local,
            (bone.head_local + bone.tail_local) * 0.5,
        )
        distance = min(
            (center - (rig.matrix_world @ point)).length for point in local_points
        )
        return float(distance) if math.isfinite(float(distance)) else None
    except (AttributeError, ReferenceError, RuntimeError, TypeError, ValueError):
        return None


def _rest_pose_hand_attachment(item: Any, rig: Any) -> str:
    """Infer a hand only when mesh geometry is materially closer in rest pose.

    Some OpenSAGE imports preserve an accessory as an unparented render mesh and
    therefore lose the W3D hierarchy label. The mesh transform and armature rest
    pose still share model space. This is accepted only when both hands exist and
    the accessory centroid is at least 20% closer to one hand than the other.
    """

    if rig is None:
        return ""
    center = _rest_pose_item_centroid(item)
    if center is None:
        return ""

    hands: dict[str, list[float]] = {"right-hand": [], "left-hand": []}
    for bone in getattr(getattr(rig, "data", None), "bones", []) or []:
        name = str(getattr(bone, "name", ""))
        attachment = ""
        if _contains_marker((name,), RIGHT_HAND_MARKERS):
            attachment = "right-hand"
        elif _contains_marker((name,), LEFT_HAND_MARKERS):
            attachment = "left-hand"
        if not attachment:
            continue
        distance = _rest_pose_bone_distance(center, rig, bone)
        if distance is not None:
            hands[attachment].append(distance)
    if not hands["right-hand"] or not hands["left-hand"]:
        return ""
    right_distance = min(hands["right-hand"])
    left_distance = min(hands["left-hand"])
    if right_distance < left_distance * 0.80:
        return "right-hand"
    if left_distance < right_distance * 0.80:
        return "left-hand"
    return ""


def _select_canonical_hand_bone(item: Any, rig: Any, attachment: str) -> Any:
    if attachment == "right-hand":
        explicit_markers = RIGHT_EXPLICIT_ATTACHMENT_MARKERS
        generic_markers = RIGHT_GENERIC_HAND_MARKERS
    else:
        explicit_markers = LEFT_EXPLICIT_ATTACHMENT_MARKERS
        generic_markers = LEFT_GENERIC_HAND_MARKERS
    center = _rest_pose_item_centroid(item)
    if center is None:
        raise RuntimeError("required rigid equipment has no finite rest-pose centroid")
    explicit: list[tuple[float, str, int, Any]] = []
    generic: list[tuple[float, str, int, Any]] = []
    for index, bone in enumerate(
        getattr(getattr(rig, "data", None), "bones", []) or []
    ):
        name = str(getattr(bone, "name", ""))
        target = None
        if _contains_marker((name,), explicit_markers):
            target = explicit
        elif _contains_marker((name,), generic_markers):
            target = generic
        if target is None:
            continue
        distance = _rest_pose_bone_distance(center, rig, bone)
        if distance is not None:
            target.append((distance, clean_name(name), index, bone))
    scored = explicit if explicit else generic
    if not scored:
        raise RuntimeError(
            "required rigid equipment has no canonical hand-bone candidate"
        )
    scored.sort(key=lambda value: (value[0], value[1], value[2]))
    if len(scored) > 1:
        nearest = scored[0][0]
        runner_up = scored[1][0]
        if not nearest < runner_up * CANONICAL_BONE_SEPARATION_RATIO:
            raise RuntimeError(
                "matching hand bones are not materially separated in rest pose"
            )
    return scored[0][3]


def _safe_attachment_diagnostics(item: Any, rig: Any) -> dict[str, Any]:
    """Payload-free facts suitable for a failed local conversion report."""

    parent_labels = []
    if getattr(item, "parent_type", "") == "BONE" and getattr(item, "parent_bone", ""):
        parent_labels.append(str(item.parent_bone))
    dominant_labels = _dominant_weight_labels(item)
    weighted_labels = _weighted_hand_labels(item)
    custom_labels = _custom_attachment_labels(item)
    bones = (
        list(getattr(getattr(rig, "data", None), "bones", []) or [])
        if rig is not None
        else []
    )
    bone_labels = [str(getattr(bone, "name", "")) for bone in bones]
    right_share, left_share = _hand_weight_shares(item)
    return {
        "skinned": _is_skinned(item),
        "vertex_group_count": len(list(getattr(item, "vertex_groups", []) or [])),
        "parent_right": _contains_marker(parent_labels, RIGHT_HAND_MARKERS),
        "parent_left": _contains_marker(parent_labels, LEFT_HAND_MARKERS),
        "dominant_right": _contains_marker(dominant_labels, RIGHT_HAND_MARKERS),
        "dominant_left": _contains_marker(dominant_labels, LEFT_HAND_MARKERS),
        "weighted_right": _contains_marker(weighted_labels, RIGHT_HAND_MARKERS),
        "weighted_left": _contains_marker(weighted_labels, LEFT_HAND_MARKERS),
        "right_hand_weight_share": round(right_share, 6),
        "left_hand_weight_share": round(left_share, 6),
        "custom_right": _contains_marker(custom_labels, RIGHT_HAND_MARKERS),
        "custom_left": _contains_marker(custom_labels, LEFT_HAND_MARKERS),
        "rig_right_candidate_count": sum(
            1 for label in bone_labels if _contains_marker((label,), RIGHT_HAND_MARKERS)
        ),
        "rig_left_candidate_count": sum(
            1 for label in bone_labels if _contains_marker((label,), LEFT_HAND_MARKERS)
        ),
        "rest_pose_attachment": _rest_pose_hand_attachment(item, rig) or "ambiguous",
    }


def _equipment_classification(item: Any, rig: Any = None) -> tuple[str, str, list[str]]:
    mesh_labels = [
        getattr(item, "name", ""),
        getattr(getattr(item, "data", None), "name", ""),
    ]
    material_labels = [
        getattr(material, "name", "")
        for material in (getattr(getattr(item, "data", None), "materials", []) or [])
        if material is not None
    ]
    parent_labels = []
    if getattr(item, "parent_type", "") == "BONE" and getattr(item, "parent_bone", ""):
        parent_labels.append(str(item.parent_bone))
    dominant_labels = _dominant_weight_labels(item)
    weighted_hand_labels = _weighted_hand_labels(item)
    custom_labels = _custom_attachment_labels(item)

    role_proofs: dict[str, set[str]] = {
        "right-hand-weapon": set(),
        "left-hand-shield": set(),
    }
    for labels, method in (
        (mesh_labels, "mesh-semantic"),
        (material_labels, "material-semantic"),
    ):
        if _contains_marker(labels, WEAPON_LABEL_MARKERS):
            role_proofs["right-hand-weapon"].add(method)
        if _contains_marker(labels, SHIELD_LABEL_MARKERS):
            role_proofs["left-hand-shield"].add(method)

    attachment_proofs: dict[str, set[str]] = {"right-hand": set(), "left-hand": set()}
    for labels, method in (
        (parent_labels, "parent-bone"),
        (dominant_labels, "dominant-weight-group"),
        (weighted_hand_labels, "weighted-hand-group"),
        (custom_labels, "custom-attachment"),
    ):
        if _contains_marker(labels, RIGHT_HAND_MARKERS):
            attachment_proofs["right-hand"].add(method)
        if _contains_marker(labels, LEFT_HAND_MARKERS):
            attachment_proofs["left-hand"].add(method)

    weapon_hint = bool(role_proofs["right-hand-weapon"])
    shield_hint = bool(role_proofs["left-hand-shield"])

    # HLOD references can bind a rigid render mesh to a dedicated authored
    # equipment pivot rather than directly to a hand.  For example, retail
    # Men-at-Arms binds FORGED_BLADE to the FORGED_BLADE pivot and BAT_SHIELD
    # to the BAT_SHIELD pivot.  Those pivots are animated source hierarchy,
    # not weak name guesses.  Preserve them instead of reparenting the mesh to
    # a nearby generic hand and baking a large rest-pose offset that drifts as
    # soon as animation begins.  The role must still be proven independently
    # by mesh/material semantics, so a pivot label alone cannot invent gear.
    if weapon_hint and not shield_hint and _contains_marker(
        parent_labels, WEAPON_LABEL_MARKERS
    ):
        attachment_proofs["right-hand"].add("source-equipment-pivot")
    if shield_hint and not weapon_hint and _contains_marker(
        parent_labels, SHIELD_LABEL_MARKERS
    ):
        attachment_proofs["left-hand"].add("source-equipment-pivot")

    right_hint = bool(attachment_proofs["right-hand"])
    left_hint = bool(attachment_proofs["left-hand"])
    if not right_hint and not left_hint:
        rest_attachment = _rest_pose_hand_attachment(item, rig)
        if rest_attachment:
            attachment_proofs[rest_attachment].add("rest-pose-proximity")
            right_hint = bool(attachment_proofs["right-hand"])
            left_hint = bool(attachment_proofs["left-hand"])
    if weapon_hint and shield_hint:
        raise RuntimeError("render mesh has ambiguous weapon and shield semantics")
    if (weapon_hint or shield_hint) and right_hint and left_hint:
        right_share, left_share = _hand_weight_shares(item)
        if weapon_hint and right_share >= max(0.02, left_share * 1.5):
            attachment_proofs["right-hand"].add("weighted-hand-dominance")
            attachment_proofs["left-hand"].clear()
            left_hint = False
        elif shield_hint and left_share >= max(0.02, right_share * 1.5):
            attachment_proofs["left-hand"].add("weighted-hand-dominance")
            attachment_proofs["right-hand"].clear()
            right_hint = False
        else:
            raise RuntimeError(
                "render mesh has ambiguous left-hand and right-hand attachment semantics: "
                + json.dumps(
                    {
                        "weapon_hint": weapon_hint,
                        "shield_hint": shield_hint,
                        "right_proof_methods": sorted(attachment_proofs["right-hand"]),
                        "left_proof_methods": sorted(attachment_proofs["left-hand"]),
                        "attachment_facts": _safe_attachment_diagnostics(item, rig),
                    },
                    sort_keys=True,
                )
            )
    if weapon_hint:
        if not right_hint or left_hint:
            raise RuntimeError(
                "weapon-like render mesh has no proven right-hand attachment: "
                + json.dumps(_safe_attachment_diagnostics(item, rig), sort_keys=True)
            )
        proof = role_proofs["right-hand-weapon"] | attachment_proofs["right-hand"]
        return "right-hand-weapon", "right-hand", sorted(proof)
    if shield_hint:
        if not left_hint or right_hint:
            raise RuntimeError(
                "shield-like render mesh has no proven left-hand attachment: "
                + json.dumps(_safe_attachment_diagnostics(item, rig), sort_keys=True)
            )
        proof = role_proofs["left-hand-shield"] | attachment_proofs["left-hand"]
        return "left-hand-shield", "left-hand", sorted(proof)
    return "character-mesh", "skeletal" if _is_skinned(item) else "scene", []


def build_mesh_inventory(
    mesh_objects: list[Any],
    required_equipment: Iterable[str],
    rig: Any = None,
    *,
    phase_checkpoint: _W3DConversionPhaseCheckpoint | None = None,
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]]]:
    _set_optional_phase_checkpoint(phase_checkpoint, "required-equipment-validation")
    required = sorted(set(str(value) for value in required_equipment))
    unsupported = sorted(set(required) - SUPPORTED_EQUIPMENT_ROLES)
    if unsupported:
        raise ValueError(
            "unsupported required equipment semantics: " + ", ".join(unsupported)
        )

    ordered = sorted(
        mesh_objects,
        key=lambda item: (
            clean_name(str(getattr(item, "name", ""))),
            clean_name(str(getattr(getattr(item, "data", None), "name", ""))),
        ),
    )
    inventory: list[dict[str, Any]] = []
    for index, item in enumerate(ordered):
        _set_optional_phase_checkpoint(phase_checkpoint, "mesh-object-type-validation")
        object_type = _w3d_object_type(item)
        if object_type != RENDERABLE_W3D_OBJECT_TYPE:
            raise RuntimeError(
                "W3D plugin could not prove a remaining mesh is render geometry"
            )
        _set_optional_phase_checkpoint(
            phase_checkpoint, "mesh-helper-filter-validation"
        )
        if _non_render_reasons(item):
            raise RuntimeError(
                "non-render W3D helper geometry remained after filtering"
            )
        item.data.calc_loop_triangles()
        _set_optional_phase_checkpoint(
            phase_checkpoint, "mesh-box-ambiguity-validation"
        )
        # Shape is not source semantics.  The pinned importer assigns W3D mesh
        # chunks ``MESH`` and collision-box chunks a distinct object type; a
        # legitimate render mesh may itself be an eight-vertex box.  The exact
        # source type and helper checks above are the deletion authority.
        _set_optional_phase_checkpoint(
            phase_checkpoint, "mesh-equipment-classification"
        )
        if required:
            role, attachment, proof_methods = _equipment_classification(item, rig)
        else:
            role = "character-mesh"
            attachment = "skeletal" if _is_skinned(item) else "scene"
            proof_methods = []
        inventory.append(
            {
                "index": index,
                "semantic_role": role,
                "attachment": attachment,
                "proof_methods": proof_methods,
                "vertices": len(item.data.vertices),
                "triangles": len(item.data.loop_triangles),
                "material_slots": len(item.data.materials),
                "skinned": _is_skinned(item),
            }
        )

    _set_optional_phase_checkpoint(phase_checkpoint, "required-equipment-validation")
    equipment: dict[str, dict[str, Any]] = {}
    for role, attachment in (
        ("right-hand-weapon", "right-hand"),
        ("left-hand-shield", "left-hand"),
    ):
        members = [item for item in inventory if item["semantic_role"] == role]
        if role in required and not members:
            raise RuntimeError(f"required equipment semantic was not proven: {role}")
        if members:
            equipment[role] = {
                "attachment": attachment,
                "mesh_indices": [item["index"] for item in members],
                "mesh_count": len(members),
                "proof_methods": sorted(
                    {method for item in members for method in item["proof_methods"]}
                ),
            }
    return inventory, equipment


def _required_unattached_rigid_equipment_role(
    item: Any,
    required: set[str],
    rig: Any,
) -> tuple[str, str] | None:
    """Resolve only a source-named required rigid role with no attachment facts."""

    mesh_labels = [
        getattr(item, "name", ""),
        getattr(getattr(item, "data", None), "name", ""),
    ]
    material_labels = [
        getattr(material, "name", "")
        for material in (getattr(getattr(item, "data", None), "materials", []) or [])
        if material is not None
    ]
    labels = [*mesh_labels, *material_labels]
    weapon_hint = _contains_marker(labels, WEAPON_LABEL_MARKERS)
    shield_hint = _contains_marker(labels, SHIELD_LABEL_MARKERS)
    if weapon_hint == shield_hint:
        return None
    role, attachment = (
        ("right-hand-weapon", "right-hand")
        if weapon_hint
        else ("left-hand-shield", "left-hand")
    )
    if role not in required or _is_skinned(item):
        return None

    facts = _safe_attachment_diagnostics(item, rig)
    conflicting_boolean_facts = (
        "parent_right",
        "parent_left",
        "dominant_right",
        "dominant_left",
        "weighted_right",
        "weighted_left",
        "custom_right",
        "custom_left",
    )
    if (
        any(bool(facts[key]) for key in conflicting_boolean_facts)
        or float(facts["right_hand_weight_share"]) != 0.0
        or float(facts["left_hand_weight_share"]) != 0.0
        or facts["rest_pose_attachment"] != "ambiguous"
    ):
        return None
    return role, attachment


def canonicalize_required_rigid_attachments(
    mesh_objects: list[Any], required_equipment: Iterable[str], rig: Any
) -> int:
    """Promote unique rest-pose-only rigid equipment to an explicit bone parent."""

    required = set(str(value) for value in required_equipment)
    if not required:
        return 0
    canonicalized = 0
    for item in mesh_objects:
        required_unattached_role = False
        try:
            role, attachment, proof_methods = _equipment_classification(item, rig)
        except RuntimeError:
            unresolved = _required_unattached_rigid_equipment_role(
                item,
                required,
                rig,
            )
            if unresolved is None:
                raise
            role, attachment = unresolved
            proof_methods = []
            required_unattached_role = True
        if role not in required:
            continue
        attachment_methods = set(proof_methods) & ATTACHMENT_PROOF_METHODS
        if (
            not required_unattached_role
            and attachment_methods != {"rest-pose-proximity"}
        ):
            continue
        if _is_skinned(item):
            raise RuntimeError(
                "required skinned equipment cannot use rigid attachment promotion"
            )
        bone = _select_canonical_hand_bone(item, rig, attachment)
        if not hasattr(item, "matrix_world"):
            raise RuntimeError("required rigid equipment has no world transform")
        world_transform = _copy_private_transform(item.matrix_world)
        world_matrix = _finite_matrix_elements(world_transform)
        if world_matrix is None or world_matrix[0] != (4, 4):
            raise RuntimeError("required rigid equipment world transform is not finite")
        try:
            item.parent = rig
            item.parent_type = "BONE"
            item.parent_bone = str(getattr(bone, "name", ""))
            item.matrix_world = _copy_private_transform(world_transform)
        except (
            AttributeError,
            ReferenceError,
            RuntimeError,
            TypeError,
            ValueError,
        ) as exc:
            raise RuntimeError(
                "could not canonicalize required rigid attachment"
            ) from exc
        restored_parent = getattr(item, "parent", None)
        if (
            restored_parent is None
            or _runtime_identity(restored_parent) != _runtime_identity(rig)
            or str(getattr(item, "parent_type", "")) != "BONE"
            or str(getattr(item, "parent_bone", "")) != str(getattr(bone, "name", ""))
            or not _private_transforms_close(item.matrix_world, world_transform)
        ):
            raise RuntimeError(
                "canonical rigid attachment did not preserve its world transform"
            )
        promoted_role, promoted_attachment, promoted_proofs = _equipment_classification(
            item, rig
        )
        if (
            promoted_role != role
            or promoted_attachment != attachment
            or "parent-bone" not in promoted_proofs
        ):
            raise RuntimeError(
                "canonical rigid attachment semantic revalidation failed"
            )
        canonicalized += 1
    return canonicalized


def _canonical_fingerprint_value(value: Any, *, depth: int = 0) -> Any:
    """Convert selected Blender values to stable JSON without emitting them."""

    if depth > 8:
        return {"type": type(value).__name__}
    if value is None or isinstance(value, (bool, int, str)):
        return value
    if isinstance(value, float):
        return {"float": value.hex()}
    if isinstance(value, bytes):
        return {"bytes_sha256": hashlib.sha256(value).hexdigest(), "size": len(value)}
    if isinstance(value, dict):
        return {
            str(key): _canonical_fingerprint_value(item, depth=depth + 1)
            for key, item in sorted(
                value.items(), key=lambda pair: str(pair[0]).casefold()
            )
        }
    to_list = getattr(value, "to_list", None)
    if callable(to_list):
        return _canonical_fingerprint_value(to_list(), depth=depth + 1)
    if isinstance(value, (list, tuple)):
        return [_canonical_fingerprint_value(item, depth=depth + 1) for item in value]
    try:
        return [
            _canonical_fingerprint_value(item, depth=depth + 1) for item in list(value)
        ]
    except TypeError:
        return {"type": type(value).__name__}


def _custom_fingerprint(owner: Any) -> dict[str, Any]:
    return {
        key: _canonical_fingerprint_value(value) for key, value in _custom_items(owner)
    }


def _digest_fingerprint_payload(value: Any) -> str:
    encoded = json.dumps(
        value, ensure_ascii=True, separators=(",", ":"), sort_keys=True
    )
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def _runtime_identity(value: Any) -> tuple[str, int]:
    """Identify one live Blender datablock without putting identity in metadata."""

    as_pointer = getattr(value, "as_pointer", None)
    if callable(as_pointer):
        return "blender", int(as_pointer())
    return "python", id(value)


def _same_runtime_identity(left: Any, right: Any) -> bool:
    """Compare Blender RNA wrappers by their stable underlying pointer."""

    return _runtime_identity(left) == _runtime_identity(right)


def _split_shader_material_compatibility_properties(
    shader_material: Any,
) -> tuple[Any, dict[str, bool], list[str]]:
    """Remove only two source-proven bools unsupported by the pinned plugin.

    The untouched pinned importer still sees every other shader property, so
    an unknown or newly introduced property continues to fail closed. The
    filtered copy retains source property order for all supported properties.

    The retail no-texture sentinel is removed on the same terms and reported
    back to the caller, which records it: dropping it edits the retail
    material graph, so it may not happen silently.
    """

    properties = list(getattr(shader_material, "properties", []) or [])
    retained: list[Any] = []
    compatibility: dict[str, bool] = {}
    dropped_sentinels: list[str] = []
    for prop in properties:
        name = str(getattr(prop, "name", ""))
        if (
            name in SHADER_TEXTURE_PROPERTIES
            and type(getattr(prop, "value", None)) is str
            and prop.value == RETAIL_W3D_NO_TEXTURE_SENTINEL
        ):
            # "None" is retail's no-texture declaration, not a basename. Drop
            # exactly that value from exactly the texture-valued properties;
            # any other value — including "none" or "None.tga" — still reaches
            # the pinned importer and still fails closed if it cannot resolve.
            dropped_sentinels.append(name)
            continue
        if name not in SHADER_BOOLEAN_COMPATIBILITY_PROPERTIES:
            retained.append(prop)
            continue
        if name in compatibility:
            raise RuntimeError(f"duplicate W3D shader compatibility property: {name}")
        value = getattr(prop, "value", None)
        if (
            getattr(prop, "type", None) != SHADER_BOOLEAN_PROPERTY_TYPE
            or type(value) is not bool
        ):
            raise RuntimeError(
                f"W3D shader compatibility property is not an exact boolean: {name}"
            )
        compatibility[name] = value
    if not compatibility and not dropped_sentinels:
        return shader_material, {}, []
    filtered = copy_module.copy(shader_material)
    filtered.properties = retained
    return filtered, compatibility, dropped_sentinels


def _set_exact_material_boolean(material: Any, key: str, value: bool) -> None:
    if type(value) is not bool:
        raise RuntimeError("W3D shader compatibility value is not boolean")
    existing = {name: candidate for name, candidate in _custom_items(material)}
    if key in existing and existing[key] is not value:
        raise RuntimeError("shared W3D material has conflicting shader semantics")
    try:
        material[key] = value
    except (AttributeError, KeyError, ReferenceError, RuntimeError, TypeError) as exc:
        raise RuntimeError(
            "could not preserve W3D shader compatibility property"
        ) from exc


def _shader_material_compatibility_importer(original: Any) -> Any:
    """Wrap the pinned importer without modifying its attested tool tree."""

    if not callable(original):
        raise RuntimeError("pinned shader material importer is unavailable")

    def compatible_import(context: Any, name: str, shader_material: Any) -> Any:
        (
            filtered,
            compatibility,
            dropped_sentinels,
        ) = _split_shader_material_compatibility_properties(shader_material)
        for property_name in dropped_sentinels:
            record = {
                "material": str(name),
                "property": property_name,
                "value": RETAIL_W3D_NO_TEXTURE_SENTINEL,
                "reason": "retail-no-texture-sentinel",
            }
            if record in _ACTIVE_SHADER_TEXTURE_SENTINEL_DROPS:
                continue
            _ACTIVE_SHADER_TEXTURE_SENTINEL_DROPS.append(record)
            print(
                "OPENBFME_W3D_SHADER_TEXTURE_SENTINEL_DROPPED "
                + json.dumps(record, sort_keys=True)
            )
        material, principled = original(context, name, filtered)
        for source_name, value in compatibility.items():
            custom_name = SHADER_BOOLEAN_COMPATIBILITY_PROPERTIES[source_name]
            _set_exact_material_boolean(material, custom_name, value)
        if "AlphaBlendingEnable" in compatibility:
            # The complete BFME2 retail corpus contains 29 typed instances of
            # this property (25 false, four true), with no co-occurring
            # AlphaTestEnable, BlendMode, or Opacity property. Preserve its
            # exact binary choice in glTF alpha mode as well as material extras.
            material.blend_method = (
                "BLEND" if compatibility["AlphaBlendingEnable"] else "OPAQUE"
            )
        return material, principled

    return compatible_import


def install_shader_material_compatibility_shim() -> None:
    """Install one process-local compatibility wrapper at both plugin call sites."""

    from io_mesh_w3d.common.utils import material_import, mesh_import  # type: ignore

    original = material_import.create_material_from_shader_material
    compatible = _shader_material_compatibility_importer(original)
    material_import.create_material_from_shader_material = compatible
    # mesh_import uses ``from material_import import *`` and therefore owns a
    # separate function binding that must be replaced explicitly.
    mesh_import.create_material_from_shader_material = compatible


def retail_orphan_hlod_exclusion(
    hierarchy: Any, sub_object: Any
) -> dict[str, Any] | None:
    """Recognize byte-proven RotWK Uruk-family orphan HLOD bindings.

    ``CHSS_UK_U_SKN`` authors ``BARREL`` at bone index 34, but its declared
    ``CHSS_UK_U_SKL`` contains exactly 34 pivots (valid indices 0..33) and no
    BARREL pivot. ``CHSS_UT_U_SKN`` carries the identical BARREL vertex stream
    and the same orphan binding against that hierarchy. A sibling Troll skin
    proves BARREL needs a non-identity authored pivot, so binding either orphan
    to ROOTTRANSFORM would visibly misplace it. Exclude only these complete
    retail fingerprints; every other out-of-range reference continues into the
    pinned importer and fails closed.
    """

    header = getattr(hierarchy, "header", None)
    hierarchy_name = str(
        getattr(header, "name", "") or getattr(hierarchy, "name", "")
    )
    raw_pivots = getattr(hierarchy, "pivots", None)
    if raw_pivots is None:
        return None
    try:
        pivots = tuple(str(getattr(item, "name", "")) for item in raw_pivots)
    except TypeError:
        return None
    authored_index = getattr(sub_object, "bone_index", None)
    identifier = str(getattr(sub_object, "identifier", ""))
    if (
        hierarchy_name != "CHSS_UK_U_SKL"
        or pivots != _RETAIL_URUK_BATTLEFIELD_PIVOTS
        or type(authored_index) is not int
        or authored_index != len(pivots)
        or identifier
        not in {
            "CHSS_UK_U_SKN.BARREL",
            "CHSS_UT_U_SKN.BARREL",
        }
    ):
        return None
    return {
        "hierarchy": hierarchy_name,
        "sub_object": identifier,
        "authored_pivot_index": authored_index,
        "pivot_count": len(pivots),
        "disposition": "excluded",
        "reason": "retail-hlod-references-missing-pivot",
    }


def _retail_hlod_rig_object_exclusion_importer(original: Any) -> Any:
    if not callable(original):
        raise RuntimeError("pinned HLOD rigid-object binder is unavailable")

    def compatible_rig_object(
        obj: Any, hierarchy: Any, rig: Any, sub_object: Any
    ) -> Any:
        raw_pivots = getattr(hierarchy, "pivots", None)
        try:
            pivot_count = len(raw_pivots) if raw_pivots is not None else 0
        except TypeError:
            return original(obj, hierarchy, rig, sub_object)
        authored_index = getattr(sub_object, "bone_index", None)
        if type(authored_index) is int and 0 <= authored_index < pivot_count:
            return original(obj, hierarchy, rig, sub_object)
        proof = retail_orphan_hlod_exclusion(hierarchy, sub_object)
        if proof is None:
            return original(obj, hierarchy, rig, sub_object)
        try:
            obj[_RETAIL_ORPHAN_HLOD_PROPERTY] = json.dumps(proof, sort_keys=True)
        except (
            AttributeError,
            KeyError,
            ReferenceError,
            RuntimeError,
            TypeError,
        ) as exc:
            raise RuntimeError(
                "retail orphan HLOD exclusion could not be marked"
            ) from exc
        if proof not in _ACTIVE_RETAIL_ORPHAN_HLOD_EXCLUSIONS:
            _ACTIVE_RETAIL_ORPHAN_HLOD_EXCLUSIONS.append(proof)
            print(
                "OPENBFME_W3D_RETAIL_ORPHAN_HLOD_EXCLUDED "
                + json.dumps(proof, sort_keys=True)
            )
        # The pinned importer cannot resolve the missing pivot. The custom
        # marker is consumed by remove_non_render_geometry before export, so an
        # unplaced render mesh cannot leak into the GLB.
        return None

    return compatible_rig_object


def install_retail_hlod_exclusion_shim() -> None:
    """Patch both pinned-plugin bindings without changing its attested tree."""

    from io_mesh_w3d.common.utils import helpers, mesh_import  # type: ignore

    original = helpers.rig_object
    compatible = _retail_hlod_rig_object_exclusion_importer(original)
    helpers.rig_object = compatible
    # mesh_import uses ``from helpers import *`` and owns a separate binding.
    mesh_import.rig_object = compatible


def collect_shader_material_compatibility(
    materials: Iterable[Any],
) -> dict[str, Any]:
    """Return canonical, payload-free proof for mapped retail shader booleans."""

    rows: list[dict[str, Any]] = []
    seen_names: set[str] = set()
    unique: dict[tuple[str, int], Any] = {}
    for material in materials:
        if material is not None:
            unique[_runtime_identity(material)] = material
    for material in sorted(
        unique.values(),
        key=lambda item: (
            str(getattr(item, "name", "")).casefold(),
            str(getattr(item, "name", "")),
        ),
    ):
        material_name = str(getattr(material, "name", ""))
        custom = {name: value for name, value in _custom_items(material)}
        flags: dict[str, bool] = {}
        for source_name, custom_name in SHADER_BOOLEAN_COMPATIBILITY_PROPERTIES.items():
            if custom_name not in custom:
                continue
            value = custom[custom_name]
            if type(value) is not bool:
                raise RuntimeError(
                    "preserved W3D shader compatibility property changed type"
                )
            flags[source_name] = value
        if not flags:
            continue
        folded = material_name.casefold()
        if not material_name or folded in seen_names:
            raise RuntimeError("W3D shader compatibility material name is ambiguous")
        seen_names.add(folded)
        rows.append({"material": material_name, "properties": flags})
    alpha_count = sum(int("AlphaBlendingEnable" in row["properties"]) for row in rows)
    fog_count = sum(int("FogEnable" in row["properties"]) for row in rows)
    return {
        "mapped_materials": rows,
        "mapped_material_count": len(rows),
        "mapped_property_count": alpha_count + fog_count,
        "alpha_blending_enable_count": alpha_count,
        "fog_enable_count": fog_count,
        "source_flags_preserved": True,
    }


def _preserved_shader_enum(material: Any, property_name: str) -> int | None:
    """Read one exact preserved W3D shader enum without coercing heuristics."""

    shader = getattr(material, "shader", None)
    if shader is None or not hasattr(shader, property_name):
        return None
    value = getattr(shader, property_name)
    if isinstance(value, bool):
        raise RuntimeError("preserved W3D shader enum is not exact")
    if isinstance(value, int):
        return value
    # Blender EnumProperty exposes the plugin's source integer identifier as
    # a canonical decimal string. No other string or numeric coercion counts.
    if isinstance(value, str) and re.fullmatch(r"0|[1-9][0-9]*", value):
        return int(value)
    raise RuntimeError("preserved W3D shader enum is not exact")


def _material_has_proven_additive_blend(material: Any) -> bool:
    source = _preserved_shader_enum(material, "src_blend")
    destination = _preserved_shader_enum(material, "dest_blend")
    if source is None and destination is None:
        return False
    if source is None or destination is None:
        raise RuntimeError("preserved W3D shader blend proof is incomplete")
    return source == ADDITIVE_BLEND_ENUM and destination == ADDITIVE_BLEND_ENUM


def normalize_proven_opaque_materials(materials: Iterable[Any]) -> dict[str, Any]:
    """Disconnect texture alpha only for exact W3D ONE/ZERO opaque states.

    Blender 4.2's glTF exporter derives alphaMode from the Principled alpha
    node graph rather than ``blend_method``.  The pinned W3D plugin connects
    every stage-0 texture alpha channel, so ordinary opaque W3D materials were
    incorrectly exported as BLEND.  The preserved shader enums are the source
    of truth: src=ONE, dst=ZERO, alpha_test=DISABLE is an opaque replacement.
    Additive, alpha-blended, alpha-tested, and incomplete states are untouched.
    """

    rows: list[dict[str, Any]] = []
    unique: dict[tuple[str, int], Any] = {}
    for material in materials:
        if material is not None:
            unique[_runtime_identity(material)] = material
    ordered = sorted(
        unique.values(),
        key=lambda item: (
            str(getattr(item, "name", "")).casefold(),
            str(getattr(item, "name", "")),
        ),
    )
    for material in ordered:
        source = _preserved_shader_enum(material, "src_blend")
        destination = _preserved_shader_enum(material, "dest_blend")
        if source is None and destination is None:
            continue
        if source is None or destination is None:
            raise RuntimeError("preserved W3D shader blend proof is incomplete")
        alpha_test = _preserved_shader_enum(material, "alpha_test")
        if (
            source != OPAQUE_SOURCE_BLEND_ENUM
            or destination != OPAQUE_DESTINATION_BLEND_ENUM
            or alpha_test not in {None, 0}
        ):
            continue
        if not bool(getattr(material, "use_nodes", False)):
            raise RuntimeError("proven opaque W3D material has no exportable node graph")
        node_tree = getattr(material, "node_tree", None)
        if node_tree is None:
            raise RuntimeError("proven opaque W3D material has no exportable node graph")
        principled_nodes = [
            node
            for node in list(getattr(node_tree, "nodes", []) or [])
            if str(getattr(node, "type", "")) == "BSDF_PRINCIPLED"
        ]
        if len(principled_nodes) != 1:
            raise RuntimeError("proven opaque W3D material has an ambiguous surface shader")
        alpha_input = _socket_by_name(
            getattr(principled_nodes[0], "inputs", None), "Alpha"
        )
        if alpha_input is None:
            raise RuntimeError("proven opaque W3D material lacks an alpha input")
        links = getattr(node_tree, "links", None)
        if links is None:
            raise RuntimeError("proven opaque W3D material has no exportable node links")
        alpha_identity = _runtime_identity(alpha_input)
        incoming = [
            link
            for link in list(links)
            if _runtime_identity(getattr(link, "to_socket", None)) == alpha_identity
        ]
        for link in incoming:
            try:
                links.remove(link)
            except (AttributeError, ReferenceError, RuntimeError, TypeError, ValueError) as exc:
                raise RuntimeError(
                    "proven opaque W3D material alpha link could not be removed"
                ) from exc
        try:
            alpha_input.default_value = 1.0
            material.blend_method = "OPAQUE"
        except (AttributeError, ReferenceError, RuntimeError, TypeError, ValueError) as exc:
            raise RuntimeError(
                "proven opaque W3D material could not be normalized"
            ) from exc
        rows.append(
            {
                "material": str(getattr(material, "name", "")),
                "source_blend": source,
                "destination_blend": destination,
                "alpha_test": 0 if alpha_test is None else alpha_test,
                "removed_alpha_links": len(incoming),
            }
        )
    return {
        "normalized_materials": rows,
        "normalized_material_count": len(rows),
        "removed_alpha_link_count": sum(
            int(row["removed_alpha_links"]) for row in rows
        ),
        "source_blend_state_preserved": True,
    }


def _socket_by_name(sockets: Any, name: str) -> Any:
    getter = getattr(sockets, "get", None)
    if callable(getter):
        socket = getter(name)
        if socket is not None:
            return socket
    try:
        return sockets[name]
    except (KeyError, TypeError):
        pass
    matches = [
        socket
        for socket in list(sockets or [])
        if str(getattr(socket, "name", "")) == name
    ]
    return matches[0] if len(matches) == 1 else None


def _additive_alpha_pixels(pixels: Iterable[Any]) -> tuple[list[float], dict[str, int]]:
    """Preserve ONE+ONE RGB contribution as normalized RGB plus alpha."""

    source = list(pixels)
    if not source or len(source) % 4 != 0:
        raise RuntimeError("additive material image has an invalid RGBA pixel buffer")
    converted: list[float] = []
    changed_alpha = 0
    transparent = 0
    visible = 0
    for offset in range(0, len(source), 4):
        channels = []
        for value in source[offset : offset + 4]:
            try:
                channel = float(value)
            except (TypeError, ValueError) as exc:
                raise RuntimeError(
                    "additive material image has invalid pixel data"
                ) from exc
            if not math.isfinite(channel):
                raise RuntimeError("additive material image has non-finite pixel data")
            channels.append(min(1.0, max(0.0, channel)))
        red, green, blue, source_alpha = channels
        intensity = max(red, green, blue)
        if intensity <= ADDITIVE_ALPHA_EPSILON:
            output_rgb = (0.0, 0.0, 0.0)
            output_alpha = 0.0
        else:
            output_rgb = (red / intensity, green / intensity, blue / intensity)
            # W3D ONE+ONE blending contributes source RGB directly; source
            # alpha does not attenuate that RGB term. Conventional alpha blend
            # therefore needs alpha=intensity so normalized_rgb * alpha
            # exactly reconstructs the authored additive RGB contribution.
            output_alpha = intensity
        converted.extend((*output_rgb, output_alpha))
        if abs(output_alpha - source_alpha) > ADDITIVE_ALPHA_EPSILON:
            changed_alpha += 1
        if output_alpha < 1.0 - ADDITIVE_ALPHA_EPSILON:
            transparent += 1
        if output_alpha > ADDITIVE_ALPHA_EPSILON:
            visible += 1
    if changed_alpha < 1:
        raise RuntimeError("additive material conversion did not change image alpha")
    if transparent < 1:
        raise RuntimeError(
            "additive material conversion produced no transparent pixels"
        )
    if visible < 1:
        raise RuntimeError("additive material conversion produced no visible pixels")
    return converted, {
        "changed_alpha_pixels": changed_alpha,
        "transparent_pixels": transparent,
        "visible_pixels": visible,
    }


def _verify_additive_pixel_round_trip(
    actual_pixels: Iterable[Any],
    expected_pixels: Iterable[Any],
    *,
    tolerance: float = ADDITIVE_PIXEL_ROUND_TRIP_TOLERANCE,
) -> list[float]:
    actual = list(actual_pixels)
    expected = list(expected_pixels)
    if len(actual) != len(expected):
        raise RuntimeError("additive material image alpha did not round trip")
    verified: list[float] = []
    for actual_value, expected_value in zip(actual, expected):
        try:
            channel = float(actual_value)
            target = float(expected_value)
        except (TypeError, ValueError) as exc:
            raise RuntimeError(
                "additive material image alpha did not round trip"
            ) from exc
        if (
            not math.isfinite(channel)
            or not math.isfinite(target)
            or channel < 0.0
            or channel > 1.0
            or abs(channel - target) > tolerance
        ):
            raise RuntimeError("additive material image alpha did not round trip")
        verified.append(channel)
    return verified


def _convert_proven_additive_vertex_material(
    material: Any,
    *,
    principled: Any,
    base_color: Any,
    alpha_input: Any,
    links: Any,
    phase_checkpoint: _W3DConversionPhaseCheckpoint | None = None,
) -> dict[str, int]:
    """Convert a textureless ONE+ONE material whose color source is geometry.

    Some retail additive materials reference no stage-0 texture at all; their
    contribution is authored per vertex (the mesh color layer) or as the
    material's constant base color. The same exact alpha derivation used for
    additive images applies: alpha carries the source intensity and RGB is
    normalized so ``normalized_rgb * alpha`` reconstructs the authored
    additive contribution. Anything ambiguous (mixed color sources, shared
    meshes, conflicting alpha inputs) stays fail-closed.
    """

    _set_optional_phase_checkpoint(
        phase_checkpoint, "additive-material-discovery"
    )
    meshes = [
        item
        for item in list(getattr(bpy.data, "objects", []) or [])
        if getattr(item, "type", None) == "MESH"
        and any(
            getattr(slot, "material", None) is material
            for slot in list(getattr(item, "material_slots", []) or [])
        )
    ]
    if not meshes:
        raise RuntimeError("proven additive material has no render mesh")
    for item in meshes:
        slot_materials = [
            getattr(slot, "material", None)
            for slot in list(getattr(item, "material_slots", []) or [])
            if getattr(slot, "material", None) is not None
        ]
        if any(slot_material is not material for slot_material in slot_materials):
            raise RuntimeError(
                "proven additive vertex material shares its render mesh"
            )
    default_value = list(getattr(base_color, "default_value", []) or [])
    if len(default_value) != 4:
        raise RuntimeError("proven additive material base color is unavailable")
    try:
        base_channels = [float(value) for value in default_value]
    except (TypeError, ValueError) as exc:
        raise RuntimeError(
            "proven additive material base color is unavailable"
        ) from exc
    if any(not math.isfinite(value) for value in base_channels):
        raise RuntimeError("proven additive material base color is not finite")

    color_attributes = []
    for item in meshes:
        attributes = getattr(getattr(item, "data", None), "color_attributes", None)
        active = getattr(attributes, "active_color", None) if attributes else None
        color_attributes.append(active)
    if any(attribute is not None for attribute in color_attributes) and any(
        attribute is None for attribute in color_attributes
    ):
        raise RuntimeError(
            "proven additive material has an ambiguous vertex color source"
        )

    report = {
        "converted_materials": 1,
        "duplicated_images": 0,
        "changed_alpha_pixels": 0,
        "transparent_pixels": 0,
        "visible_pixels": 0,
    }
    if all(attribute is not None for attribute in color_attributes):
        if any(channel > ADDITIVE_ALPHA_EPSILON for channel in base_channels[:3]):
            raise RuntimeError(
                "proven additive material has an ambiguous color source"
            )
        layer_names = {getattr(attribute, "name", "") for attribute in color_attributes}
        if len(layer_names) != 1 or not next(iter(layer_names)):
            raise RuntimeError(
                "proven additive material has an ambiguous vertex color layer"
            )
        _set_optional_phase_checkpoint(
            phase_checkpoint, "additive-material-alpha-derivation"
        )
        for attribute in color_attributes:
            data = getattr(attribute, "data", None)
            count = len(data) if data is not None else 0
            if count < 1:
                raise RuntimeError(
                    "proven additive material vertex color layer is empty"
                )
            buffer = [0.0] * (count * 4)
            data.foreach_get("color", buffer)
            converted, pixel_report = _additive_alpha_pixels(buffer)
            data.foreach_set("color", converted)
            round_trip = [0.0] * (count * 4)
            data.foreach_get("color", round_trip)
            _verify_additive_pixel_round_trip(
                round_trip,
                converted,
                tolerance=ADDITIVE_VERTEX_COLOR_ROUND_TRIP_TOLERANCE,
            )
            for key in (
                "changed_alpha_pixels",
                "transparent_pixels",
                "visible_pixels",
            ):
                report[key] += pixel_report[key]
        try:
            base_color.default_value = (1.0, 1.0, 1.0, 1.0)
        except (AttributeError, TypeError, ValueError) as exc:
            raise RuntimeError(
                "proven additive material vertex color source could not be bound"
            ) from exc
        _set_optional_phase_checkpoint(
            phase_checkpoint, "additive-material-image-duplication"
        )
        node_tree = getattr(material, "node_tree", None)
        nodes = getattr(node_tree, "nodes", None)
        if nodes is None:
            raise RuntimeError(
                "proven additive material has no exportable node graph"
            )
        try:
            color_node = nodes.new("ShaderNodeVertexColor")
            color_node.layer_name = next(iter(layer_names))
        except (AttributeError, RuntimeError, TypeError) as exc:
            raise RuntimeError(
                "proven additive material vertex color node could not be created"
            ) from exc
        color_output = _socket_by_name(getattr(color_node, "outputs", None), "Color")
        if color_output is None:
            raise RuntimeError(
                "proven additive material vertex color node has no color output"
            )
        try:
            links.new(color_output, base_color)
        except (AttributeError, RuntimeError, TypeError) as exc:
            raise RuntimeError(
                "proven additive material vertex color could not be connected"
            ) from exc
        alpha_output = _socket_by_name(getattr(color_node, "outputs", None), "Alpha")
    else:
        # Constant-color textureless additive material: derive one exact
        # alpha value from the authored base color (black contributes
        # nothing and becomes fully transparent; no visibility guard applies
        # to a single exact constant).
        _set_optional_phase_checkpoint(
            phase_checkpoint, "additive-material-alpha-derivation"
        )
        red, green, blue = (
            min(1.0, max(0.0, value)) for value in base_channels[:3]
        )
        intensity = max(red, green, blue)
        if intensity <= ADDITIVE_ALPHA_EPSILON:
            normalized = (0.0, 0.0, 0.0)
        else:
            normalized = (red / intensity, green / intensity, blue / intensity)
        try:
            base_color.default_value = (*normalized, 1.0)
            alpha_input.default_value = intensity
        except (AttributeError, TypeError, ValueError) as exc:
            raise RuntimeError(
                "proven additive material constant color could not be normalized"
            ) from exc
        report["changed_alpha_pixels"] = int(
            abs(intensity - base_channels[3]) > ADDITIVE_ALPHA_EPSILON
        )
        report["transparent_pixels"] = int(
            intensity < 1.0 - ADDITIVE_ALPHA_EPSILON
        )
        report["visible_pixels"] = int(intensity > ADDITIVE_ALPHA_EPSILON)
        return report

    _set_optional_phase_checkpoint(phase_checkpoint, "additive-material-alpha-link")
    if alpha_output is None:
        raise RuntimeError("proven additive material image has no alpha output")
    alpha_input_identity = _runtime_identity(alpha_input)
    incoming_alpha = [
        link
        for link in list(links)
        if _runtime_identity(getattr(link, "to_socket", None)) == alpha_input_identity
    ]
    if incoming_alpha:
        if len(incoming_alpha) != 1 or (
            not _same_runtime_identity(
                getattr(incoming_alpha[0], "from_node", None), color_node
            )
            or not _same_runtime_identity(
                getattr(incoming_alpha[0], "from_socket", None), alpha_output
            )
        ):
            raise RuntimeError("proven additive material has an ambiguous alpha input")
    else:
        try:
            links.new(alpha_output, alpha_input)
        except (AttributeError, RuntimeError, TypeError) as exc:
            raise RuntimeError(
                "additive material alpha could not be connected"
            ) from exc
    return report


def _convert_proven_additive_material(
    material: Any,
    *,
    phase_checkpoint: _W3DConversionPhaseCheckpoint | None = None,
) -> dict[str, int]:
    _set_optional_phase_checkpoint(
        phase_checkpoint, "additive-material-graph-validation"
    )
    if not bool(getattr(material, "use_nodes", False)):
        raise RuntimeError("proven additive material has no exportable node graph")
    node_tree = getattr(material, "node_tree", None)
    if node_tree is None:
        raise RuntimeError("proven additive material has no exportable node graph")
    nodes = list(getattr(node_tree, "nodes", []) or [])
    image_nodes = [
        node
        for node in nodes
        if str(getattr(node, "type", "")) == "TEX_IMAGE"
        and getattr(node, "image", None) is not None
    ]
    principled_nodes = [
        node for node in nodes if str(getattr(node, "type", "")) == "BSDF_PRINCIPLED"
    ]
    if len(principled_nodes) != 1:
        raise RuntimeError("proven additive material has an ambiguous surface shader")
    principled = principled_nodes[0]
    base_color = _socket_by_name(getattr(principled, "inputs", None), "Base Color")
    alpha_input = _socket_by_name(getattr(principled, "inputs", None), "Alpha")
    if base_color is None or alpha_input is None:
        raise RuntimeError("proven additive material lacks required shader inputs")
    links = getattr(node_tree, "links", None)
    if links is None:
        raise RuntimeError("proven additive material has no exportable node links")
    base_color_identity = _runtime_identity(base_color)
    image_nodes_by_identity = {_runtime_identity(node): node for node in image_nodes}
    direct_color_nodes = {
        _runtime_identity(getattr(link, "from_node", None)): image_nodes_by_identity[
            _runtime_identity(getattr(link, "from_node", None))
        ]
        for link in list(links)
        if _runtime_identity(getattr(link, "to_socket", None)) == base_color_identity
        and _runtime_identity(getattr(link, "from_node", None))
        in image_nodes_by_identity
    }
    candidates = (
        list(direct_color_nodes.values()) if direct_color_nodes else image_nodes
    )
    if len(candidates) != 1:
        if not image_nodes:
            return _convert_proven_additive_vertex_material(
                material,
                principled=principled,
                base_color=base_color,
                alpha_input=alpha_input,
                links=links,
                phase_checkpoint=phase_checkpoint,
            )
        raise RuntimeError("proven additive material has an ambiguous color image")
    image_node = candidates[0]
    source_image = image_node.image
    _set_optional_phase_checkpoint(phase_checkpoint, "additive-material-pixel-read")
    try:
        source_pixels = list(source_image.pixels[:])
    except (AttributeError, ReferenceError, RuntimeError, TypeError) as exc:
        raise RuntimeError(
            "proven additive material image pixels are unavailable"
        ) from exc
    _set_optional_phase_checkpoint(
        phase_checkpoint, "additive-material-alpha-derivation"
    )
    converted_pixels, pixel_report = _additive_alpha_pixels(source_pixels)

    _set_optional_phase_checkpoint(
        phase_checkpoint, "additive-material-image-duplication"
    )
    duplicated = int(int(getattr(source_image, "users", 0)) > 1)
    target_image = source_image
    if duplicated:
        try:
            target_image = source_image.copy()
        except (AttributeError, ReferenceError, RuntimeError, TypeError) as exc:
            raise RuntimeError(
                "shared additive material image could not be duplicated"
            ) from exc
        source_image_identity = _runtime_identity(source_image)
        for node in image_nodes:
            if _runtime_identity(getattr(node, "image", None)) == source_image_identity:
                node.image = target_image
    _set_optional_phase_checkpoint(phase_checkpoint, "additive-material-pixel-write")
    try:
        target_pixels = target_image.pixels
        writer = getattr(target_pixels, "foreach_set", None)
        if callable(writer):
            writer(converted_pixels)
        else:
            target_pixels[:] = converted_pixels
        target_image.update()
        _set_optional_phase_checkpoint(phase_checkpoint, "additive-material-round-trip")
        round_trip_pixels = list(target_image.pixels[:])
    except (AttributeError, ReferenceError, RuntimeError, TypeError, ValueError) as exc:
        raise RuntimeError(
            "additive material image alpha could not be written"
        ) from exc
    verified_pixels = _verify_additive_pixel_round_trip(
        round_trip_pixels, converted_pixels
    )
    if not any(
        verified_pixels[index] < 1.0 - ADDITIVE_ALPHA_EPSILON
        for index in range(3, len(verified_pixels), 4)
    ):
        raise RuntimeError("additive material image has no verified transparent pixels")
    if not any(
        verified_pixels[index] > ADDITIVE_ALPHA_EPSILON
        for index in range(3, len(verified_pixels), 4)
    ):
        raise RuntimeError("additive material image has no verified visible pixels")

    _set_optional_phase_checkpoint(phase_checkpoint, "additive-material-alpha-link")
    alpha_output = _socket_by_name(getattr(image_node, "outputs", None), "Alpha")
    if alpha_output is None:
        raise RuntimeError("proven additive material image has no alpha output")
    alpha_input_identity = _runtime_identity(alpha_input)
    incoming_alpha = [
        link
        for link in list(links)
        if _runtime_identity(getattr(link, "to_socket", None)) == alpha_input_identity
    ]
    if incoming_alpha:
        if len(incoming_alpha) != 1 or (
            not _same_runtime_identity(
                getattr(incoming_alpha[0], "from_node", None), image_node
            )
            or not _same_runtime_identity(
                getattr(incoming_alpha[0], "from_socket", None), alpha_output
            )
        ):
            raise RuntimeError("proven additive material has an ambiguous alpha input")
    else:
        try:
            links.new(alpha_output, alpha_input)
        except (AttributeError, RuntimeError, TypeError) as exc:
            raise RuntimeError(
                "additive material alpha could not be connected"
            ) from exc
    return {
        "converted_materials": 1,
        "duplicated_images": duplicated,
        **pixel_report,
    }


def convert_proven_additive_materials(
    materials: Iterable[Any],
    *,
    phase_checkpoint: _W3DConversionPhaseCheckpoint | None = None,
) -> dict[str, int]:
    _set_optional_phase_checkpoint(phase_checkpoint, "additive-material-discovery")
    report = {
        "converted_materials": 0,
        "duplicated_images": 0,
        "changed_alpha_pixels": 0,
        "transparent_pixels": 0,
        "visible_pixels": 0,
    }
    unique: dict[tuple[str, int], Any] = {}
    for material in materials:
        if material is not None:
            unique[_runtime_identity(material)] = material
    ordered = sorted(
        unique.values(),
        key=lambda item: clean_name(str(getattr(item, "name", ""))),
    )
    for index, material in enumerate(ordered):
        if index:
            _set_optional_phase_checkpoint(
                phase_checkpoint, "additive-material-discovery"
            )
        if not _material_has_proven_additive_blend(material):
            continue
        converted = _convert_proven_additive_material(
            material, phase_checkpoint=phase_checkpoint
        )
        for key, value in converted.items():
            report[key] += value
    return report


def _material_payload(material: Any) -> Any:
    if material is None:
        return None
    payload: dict[str, Any] = {
        "name": str(getattr(material, "name", "")),
        "custom": _custom_fingerprint(material),
    }
    for attribute in (
        "alpha_threshold",
        "blend_method",
        "diffuse_color",
        "diffuse_intensity",
        "metallic",
        "roughness",
        "shadow_method",
        "specular_color",
        "specular_intensity",
        "surface_render_method",
        "use_nodes",
    ):
        if hasattr(material, attribute):
            payload[attribute] = _canonical_fingerprint_value(
                getattr(material, attribute)
            )

    node_tree = getattr(material, "node_tree", None)
    if node_tree is not None:
        nodes = []
        for node in sorted(
            list(getattr(node_tree, "nodes", []) or []),
            key=lambda item: (
                str(getattr(item, "name", "")).casefold(),
                str(getattr(item, "type", "")),
            ),
        ):
            inputs = []
            for socket in list(getattr(node, "inputs", []) or []):
                item = {"name": str(getattr(socket, "name", ""))}
                if hasattr(socket, "default_value"):
                    item["default"] = _canonical_fingerprint_value(socket.default_value)
                inputs.append(item)
            nodes.append(
                {
                    "name": str(getattr(node, "name", "")),
                    "type": str(getattr(node, "type", "")),
                    "label": str(getattr(node, "label", "")),
                    "mute": bool(getattr(node, "mute", False)),
                    "inputs": inputs,
                }
            )
        links = sorted(
            (
                str(getattr(getattr(link, "from_node", None), "name", "")),
                str(getattr(getattr(link, "from_socket", None), "name", "")),
                str(getattr(getattr(link, "to_node", None), "name", "")),
                str(getattr(getattr(link, "to_socket", None), "name", "")),
            )
            for link in (getattr(node_tree, "links", []) or [])
        )
        payload["nodes"] = nodes
        payload["links"] = links
    return payload


def _geometry_payload(item: Any) -> dict[str, Any]:
    data = item.data
    data.calc_loop_triangles()
    vertices = [
        {
            "co": _canonical_fingerprint_value(getattr(vertex, "co", None)),
            "normal": _canonical_fingerprint_value(getattr(vertex, "normal", None)),
        }
        for vertex in (getattr(data, "vertices", []) or [])
    ]
    edges = [
        {
            "vertices": _canonical_fingerprint_value(getattr(edge, "vertices", ())),
            "sharp": bool(getattr(edge, "use_edge_sharp", False)),
        }
        for edge in (getattr(data, "edges", []) or [])
    ]
    polygons = [
        {
            "vertices": _canonical_fingerprint_value(getattr(polygon, "vertices", ())),
            "material": int(getattr(polygon, "material_index", 0)),
            "smooth": bool(getattr(polygon, "use_smooth", False)),
        }
        for polygon in (getattr(data, "polygons", []) or [])
    ]
    triangles = [
        {
            "vertices": _canonical_fingerprint_value(getattr(triangle, "vertices", ())),
            "loops": _canonical_fingerprint_value(getattr(triangle, "loops", ())),
            "material": int(getattr(triangle, "material_index", 0)),
        }
        for triangle in (getattr(data, "loop_triangles", []) or [])
    ]
    uv_layers = []
    for layer in getattr(data, "uv_layers", []) or []:
        uv_layers.append(
            {
                "name": str(getattr(layer, "name", "")),
                "active_render": bool(getattr(layer, "active_render", False)),
                "values": [
                    _canonical_fingerprint_value(getattr(entry, "uv", None))
                    for entry in (getattr(layer, "data", []) or [])
                ],
            }
        )
    return {
        "vertices": vertices,
        "edges": edges,
        "polygons": polygons,
        "triangles": triangles,
        "uv_layers": uv_layers,
    }


def _weight_payload(item: Any) -> dict[str, Any]:
    groups = sorted(
        (
            int(getattr(group, "index", index)),
            str(getattr(group, "name", "")),
            bool(getattr(group, "lock_weight", False)),
        )
        for index, group in enumerate(getattr(item, "vertex_groups", []) or [])
    )
    vertices = []
    for vertex in getattr(item.data, "vertices", []) or []:
        vertices.append(
            sorted(
                (
                    int(getattr(assignment, "group", -1)),
                    _canonical_fingerprint_value(
                        float(getattr(assignment, "weight", 0.0))
                    ),
                )
                for assignment in (getattr(vertex, "groups", []) or [])
            )
        )
    return {"groups": groups, "vertices": vertices}


def _object_data_payload(item: Any) -> dict[str, Any]:
    # Parent/parent_bone are intentionally absent. Some animation-only W3Ds
    # clear attachment parenting while leaving the validated render payload
    # untouched; a separate private proof restores and revalidates that state.
    return {
        "object_name": str(getattr(item, "name", "")),
        "object_type": str(getattr(item, "type", "")),
        "data_name": str(getattr(item.data, "name", "")),
        "w3d_object_type": _w3d_object_type(item),
        "object_custom": _custom_fingerprint(item),
        "data_custom": _custom_fingerprint(item.data),
        "modifiers": [
            {
                "name": str(getattr(modifier, "name", "")),
                "type": str(getattr(modifier, "type", "")),
                "show_render": bool(getattr(modifier, "show_render", True)),
            }
            for modifier in (getattr(item, "modifiers", []) or [])
        ],
    }


def capture_render_geometry_proof(mesh_objects: list[Any]) -> list[dict[str, Any]]:
    """Capture private, deterministic pre-animation proof for render meshes."""

    ordered = sorted(
        mesh_objects,
        key=lambda item: (
            clean_name(str(getattr(item, "name", ""))),
            clean_name(str(getattr(getattr(item, "data", None), "name", ""))),
        ),
    )
    proof: list[dict[str, Any]] = []
    for item in ordered:
        materials = tuple(getattr(item.data, "materials", []) or [])
        proof.append(
            {
                "object_ref": item,
                "object_identity": _runtime_identity(item),
                "data_ref": item.data,
                "data_identity": _runtime_identity(item.data),
                "material_refs": materials,
                "material_identities": tuple(
                    _runtime_identity(value) for value in materials
                ),
                "fingerprints": {
                    "object_data": _digest_fingerprint_payload(
                        _object_data_payload(item)
                    ),
                    "geometry": _digest_fingerprint_payload(_geometry_payload(item)),
                    "materials": _digest_fingerprint_payload(
                        [_material_payload(material) for material in materials]
                    ),
                    "weights": _digest_fingerprint_payload(_weight_payload(item)),
                },
            }
        )
    return proof


def exclude_optional_render_meshes(
    mesh_objects: list[Any],
    excluded_identifiers: list[str],
    required_equipment: Iterable[str],
    rig: Any = None,
) -> list[dict[str, Any]]:
    """Remove an exact, predeclared optional render subobject closure.

    Identifiers refer only to ``clean_name(object.name)``. No source names or
    material payloads leave Blender; provenance contains declared identifiers,
    bounded counts, and digests of the removed geometry/material state.
    """

    requested = normalize_optional_mesh_exclusions(excluded_identifiers)
    if not requested:
        return []
    renderable = [
        item
        for item in mesh_objects
        if getattr(item, "type", "") == "MESH"
        and _w3d_object_type(item) == RENDERABLE_W3D_OBJECT_TYPE
        and not _non_render_reasons(item)
    ]
    by_identifier: dict[str, list[Any]] = {}
    for item in renderable:
        identifier = clean_name(str(getattr(item, "name", "")))
        if identifier:
            by_identifier.setdefault(identifier, []).append(item)

    targets: list[Any] = []
    for identifier in requested:
        matches = by_identifier.get(identifier, [])
        if len(matches) != 1:
            raise RuntimeError(
                f"excluded optional mesh {identifier!r} matched {len(matches)} "
                "initially renderable meshes"
            )
        targets.append(matches[0])

    roles: dict[tuple[str, int], str] = {}
    for item in renderable:
        role, _attachment, _proof_methods = _equipment_classification(item, rig)
        roles[_runtime_identity(item)] = role

    target_identities = {_runtime_identity(item) for item in targets}
    required = set(str(value) for value in required_equipment)
    for identifier, item in zip(requested, targets):
        role = roles[_runtime_identity(item)]
        if role in SUPPORTED_EQUIPMENT_ROLES:
            qualifier = "required" if role in required else "proven"
            raise RuntimeError(
                f"excluded optional mesh {identifier!r} is {qualifier} equipment"
            )
    character_count = sum(role == "character-mesh" for role in roles.values())
    removed_character_count = sum(
        roles[identity] == "character-mesh" for identity in target_identities
    )
    if character_count - removed_character_count < 1:
        raise RuntimeError(
            "excluded optional meshes would remove the last character mesh"
        )

    exclusions: list[dict[str, Any]] = []
    for identifier, item in zip(requested, targets):
        item.data.calc_loop_triangles()
        materials = list(getattr(item.data, "materials", []) or [])
        exclusions.append(
            {
                "identifier": identifier,
                "geometry_sha256": _digest_fingerprint_payload(_geometry_payload(item)),
                "materials_sha256": _digest_fingerprint_payload(
                    [_material_payload(material) for material in materials]
                ),
                "vertices": len(getattr(item.data, "vertices", []) or []),
                "triangles": len(getattr(item.data, "loop_triangles", []) or []),
                "material_slots": len(materials),
            }
        )

    for item in targets:
        bpy.data.objects.remove(item, do_unlink=True)
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    for material in list(bpy.data.materials):
        if material.users == 0:
            bpy.data.materials.remove(material)
    return exclusions


def assert_render_geometry_unchanged(
    proof: list[dict[str, Any]], mesh_objects: list[Any]
) -> None:
    """Fail if animation imports changed any prevalidated render payload."""

    current_by_identity = {_runtime_identity(item): item for item in mesh_objects}
    if len(current_by_identity) != len(proof):
        raise RuntimeError(
            "animation import added, removed, or replaced render geometry"
        )
    for record in proof:
        current = current_by_identity.get(record["object_identity"])
        if (
            current is None
            or _runtime_identity(current.data) != record["data_identity"]
        ):
            raise RuntimeError(
                "animation import added, removed, or replaced render geometry"
            )
        current_materials = tuple(getattr(current.data, "materials", []) or [])
        current_material_identities = tuple(
            _runtime_identity(value) for value in current_materials
        )
        if current_material_identities != record["material_identities"]:
            raise RuntimeError("animation import replaced render material data")
        fingerprints = {
            "object_data": _digest_fingerprint_payload(_object_data_payload(current)),
            "geometry": _digest_fingerprint_payload(_geometry_payload(current)),
            "materials": _digest_fingerprint_payload(
                [_material_payload(material) for material in current_materials]
            ),
            "weights": _digest_fingerprint_payload(_weight_payload(current)),
        }
        changed = sorted(
            name
            for name, fingerprint in fingerprints.items()
            if fingerprint != record["fingerprints"].get(name)
        )
        if changed:
            raise RuntimeError(
                "animation import materially mutated prevalidated render geometry: "
                + ", ".join(changed)
            )


def _copy_private_transform(value: Any) -> Any:
    if value is None:
        return None
    copier = getattr(value, "copy", None)
    if callable(copier):
        return copier()
    return copy_module.deepcopy(value)


def _finite_matrix_elements(value: Any) -> tuple[tuple[int, ...], list[float]] | None:
    """Return matrix shape/elements only when every component is finite."""

    to_list = getattr(value, "to_list", None)
    if callable(to_list):
        value = to_list()

    def collect(item: Any) -> tuple[tuple[int, ...], list[float]] | None:
        if isinstance(item, bool):
            return None
        if isinstance(item, (int, float)):
            number = float(item)
            return ((), [number]) if math.isfinite(number) else None
        try:
            children = list(item)
        except TypeError:
            return None
        collected = [collect(child) for child in children]
        if any(child is None for child in collected):
            return None
        shapes = [child[0] for child in collected if child is not None]
        if shapes and any(shape != shapes[0] for shape in shapes):
            return None
        child_shape = shapes[0] if shapes else ()
        elements: list[float] = []
        for child in collected:
            if child is not None:
                elements.extend(child[1])
        return (len(children),) + child_shape, elements

    return collect(value)


def _private_transforms_close(
    actual: Any,
    expected: Any,
    tolerance: float = ATTACHMENT_MATRIX_TOLERANCE,
) -> bool:
    if not math.isfinite(tolerance) or tolerance < 0.0:
        return False
    actual_matrix = _finite_matrix_elements(actual)
    expected_matrix = _finite_matrix_elements(expected)
    if actual_matrix is None or expected_matrix is None:
        return False
    if actual_matrix[0] != expected_matrix[0] or len(actual_matrix[1]) != len(
        expected_matrix[1]
    ):
        return False
    return all(
        abs(actual_value - expected_value) <= tolerance
        for actual_value, expected_value in zip(actual_matrix[1], expected_matrix[1])
    )


def bake_proven_root_rigid_hierarchy(
    asset_kind: str,
    requested: bool,
    rig: Any,
    mesh_objects: list[Any],
    object_collection: Any,
) -> dict[str, Any]:
    """Bake one scanner-proven rigid carrier into rigid scene meshes.

    OpenSAGE deliberately omits the source root pivot. For a model whose every
    render reference is planner-proven to target that pivot, the importer emits
    one empty armature carrier with rigid mesh children. A static multi-pivot
    hierarchy whose render meshes are all rigidly attached (bone-parented or
    carrier-parented, never skinned) is the same carrier shape with more rest
    pivots: the pivots are rigid rest transforms because nothing can animate
    them. This opt-in path removes only those exact carrier shapes while
    proving that world transforms survive.
    """

    if requested is not True:
        raise RuntimeError(
            "empty hierarchical carrier requires an explicit proven root-rigid bake"
        )
    if asset_kind != "hierarchical":
        raise RuntimeError(
            "proven root-rigid bake requires hierarchical W3D asset kind"
        )
    if rig is None or getattr(rig, "type", None) != "ARMATURE":
        raise RuntimeError(
            "proven root-rigid bake requires exactly one armature carrier"
        )
    # Pivots may only be rigid rest transforms when nothing animates them: no
    # actions anywhere in the scene (proven below) and no bone constraints.
    for pose_bone in list(getattr(getattr(rig, "pose", None), "bones", []) or []):
        if list(getattr(pose_bone, "constraints", []) or []):
            raise RuntimeError("proven root-rigid carrier bone has constraints")
    assert_non_animated_scene_has_no_actions(asset_kind)
    if (
        getattr(rig, "parent", None) is not None
        or str(getattr(rig, "parent_type", "OBJECT")) != "OBJECT"
        or str(getattr(rig, "parent_bone", ""))
        or list(getattr(rig, "modifiers", []) or [])
        or list(getattr(rig, "constraints", []) or [])
    ):
        raise RuntimeError("proven root-rigid carrier has unsupported relationships")
    if not mesh_objects:
        raise RuntimeError("proven root-rigid bake has no retained render meshes")

    retained_identities = {_runtime_identity(item) for item in mesh_objects}
    if len(retained_identities) != len(mesh_objects):
        raise RuntimeError("proven root-rigid bake has duplicate render meshes")
    scene_objects = list(object_collection)
    scene_identities = {_runtime_identity(item) for item in scene_objects}
    if not retained_identities.issubset(scene_identities):
        raise RuntimeError("proven root-rigid render mesh is absent from the scene")
    unexpected_children = [
        item
        for item in scene_objects
        if getattr(item, "parent", None) is rig
        and _runtime_identity(item) not in retained_identities
    ]
    if unexpected_children:
        raise RuntimeError("proven root-rigid carrier has non-render children")

    carrier_bone_count = len(list(getattr(getattr(rig, "data", None), "bones", []) or []))
    inert_vertex_groups: list[tuple[Any, list[str]]] = []
    world_transforms: list[tuple[Any, Any]] = []
    for item in mesh_objects:
        if getattr(item, "type", None) != "MESH":
            raise RuntimeError(
                "proven root-rigid render mesh is not rigidly parented to the carrier"
            )
        parent = getattr(item, "parent", None)
        parent_type = str(getattr(item, "parent_type", ""))
        if parent is None:
            if parent_type != "OBJECT":
                raise RuntimeError(
                    "proven root-rigid render mesh is not rigidly parented to the carrier"
                )
        elif parent is not rig or parent_type not in {"ARMATURE", "OBJECT", "BONE"}:
            raise RuntimeError(
                "proven root-rigid render mesh is not rigidly parented to the carrier"
            )
        inert_modifiers = list(getattr(item, "modifiers", []) or [])
        if any(
            str(getattr(modifier, "type", "")) != "ARMATURE"
            for modifier in inert_modifiers
        ):
            raise RuntimeError(
                "proven root-rigid render mesh has ambiguous deformation state"
            )
        groups = list(getattr(item, "vertex_groups", []) or [])
        if groups:
            # Retail also authors this carrier shape as a skin: gudeadar2.w3d
            # weights every vertex to its one pivot, ROOTTRANSFORM, which
            # OpenSAGE deliberately omits, so the carrier imports with zero
            # bones. A vertex group that names only that omitted root pivot on
            # a carrier that really has no bones cannot move a vertex, and the
            # armature modifier that would consume it must belong to this
            # carrier. Any other group name, any additional group, any real
            # bone, or a foreign armature is a genuine deformation the bake
            # must not silently discard.
            if (
                carrier_bone_count != 0
                or {str(getattr(group, "name", "")) for group in groups}
                != {OMITTED_ROOT_PIVOT_NAME}
                or any(
                    getattr(modifier, "object", None) not in (None, rig)
                    for modifier in inert_modifiers
                )
            ):
                raise RuntimeError(
                    "proven root-rigid render mesh has ambiguous deformation state"
                )
            inert_vertex_groups.append((item, [OMITTED_ROOT_PIVOT_NAME]))
        world = _copy_private_transform(getattr(item, "matrix_world", None))
        finite_world = _finite_matrix_elements(world)
        if finite_world is None or finite_world[0] != (4, 4):
            raise RuntimeError(
                "proven root-rigid render mesh world transform is not finite"
            )
        world_transforms.append((item, world))

    for item, world in world_transforms:
        try:
            # Armature modifiers are deformation-inert without vertex weights
            # (proven above) and dangle once the carrier is removed.
            for modifier in list(getattr(item, "modifiers", []) or []):
                item.modifiers.remove(modifier)
            # Proven-inert groups are removed with their modifier so the baked
            # mesh is rigid in fact, not merely in effect.
            for group in list(getattr(item, "vertex_groups", []) or []):
                item.vertex_groups.remove(group)
            item.parent = None
            item.parent_type = "OBJECT"
            item.parent_bone = ""
            item.matrix_world = _copy_private_transform(world)
        except (
            AttributeError,
            ReferenceError,
            RuntimeError,
            TypeError,
            ValueError,
        ) as exc:
            raise RuntimeError(
                "could not bake proven root-rigid render transform"
            ) from exc
        if (
            getattr(item, "parent", None) is not None
            or str(getattr(item, "parent_type", "")) != "OBJECT"
            or str(getattr(item, "parent_bone", ""))
            or not _private_transforms_close(getattr(item, "matrix_world", None), world)
        ):
            raise RuntimeError("proven root-rigid render transform was not preserved")
        if list(getattr(item, "vertex_groups", []) or []):
            raise RuntimeError("proven root-rigid inert vertex group was not removed")

    try:
        object_collection.remove(rig, do_unlink=True)
    except (AttributeError, ReferenceError, RuntimeError, TypeError, ValueError) as exc:
        raise RuntimeError("could not remove proven root-rigid carrier") from exc
    if any(item is rig for item in object_collection):
        raise RuntimeError("proven root-rigid carrier remains in the scene")
    if any(getattr(item, "type", None) == "ARMATURE" for item in object_collection):
        raise RuntimeError("proven root-rigid bake left an unexpected armature")
    for item, world in world_transforms:
        if getattr(item, "parent", None) is not None or not _private_transforms_close(
            getattr(item, "matrix_world", None), world
        ):
            raise RuntimeError(
                "proven root-rigid render transform changed after carrier removal"
            )
    for _item, names in inert_vertex_groups:
        record = {
            "groups": sorted(names),
            "carrier_bone_count": carrier_bone_count,
            "reason": "omitted-root-pivot-skin",
        }
        if record in _ACTIVE_ROOT_RIGID_INERT_VERTEX_GROUPS:
            continue
        _ACTIVE_ROOT_RIGID_INERT_VERTEX_GROUPS.append(record)
        print(
            "OPENBFME_W3D_ROOT_RIGID_INERT_VERTEX_GROUPS_REMOVED "
            + json.dumps(record, sort_keys=True)
        )
    return {
        "requested": True,
        "applied": True,
        "removed_carriers": 1,
        "baked_meshes": len(mesh_objects),
        "world_transforms_preserved": True,
        "deform_ambiguity_absent": True,
    }


def capture_render_attachment_proof(mesh_objects: list[Any]) -> list[dict[str, Any]]:
    """Capture private attachment state that animation-only imports may clear."""

    proof: list[dict[str, Any]] = []
    for item in mesh_objects:
        parent = getattr(item, "parent", None)
        if not hasattr(item, "matrix_parent_inverse") or not hasattr(
            item, "matrix_basis"
        ):
            raise RuntimeError("prevalidated attachment transform is unavailable")
        parent_inverse = _copy_private_transform(item.matrix_parent_inverse)
        local_transform = _copy_private_transform(item.matrix_basis)
        parent_matrix = _finite_matrix_elements(parent_inverse)
        local_matrix = _finite_matrix_elements(local_transform)
        if parent_matrix is None or parent_matrix[0] != (4, 4):
            raise RuntimeError("prevalidated parent-inverse matrix is not finite")
        if local_matrix is None or local_matrix[0] != (4, 4):
            raise RuntimeError("prevalidated local transform is not finite")
        proof.append(
            {
                "object_ref": item,
                "object_identity": _runtime_identity(item),
                "parent_ref": parent,
                "parent_identity": _runtime_identity(parent)
                if parent is not None
                else None,
                "parent_type": str(getattr(item, "parent_type", "")),
                "parent_bone": str(getattr(item, "parent_bone", "")),
                "parent_inverse": parent_inverse,
                "local_transform": local_transform,
            }
        )
    return proof


def strip_animation_sidecar_meshes(
    proof: list[dict[str, Any]], mesh_objects: list[Any]
) -> dict[str, Any]:
    """Remove meshes animation W3Ds inject that were not in the model import.

    Retail death/fly clips sometimes embed corpse or prop meshes (for example
    ``GOBLINCORPSE01`` inside ``mugblnswrd_flyb.w3d``). Those are not part of
    the unit's pre-animation render inventory; leaving them breaks attachment
    restoration and revalidation. Strip them with explicit report evidence.
    """

    proof_ids = {
        record["object_identity"]
        for record in proof
        if isinstance(record, dict) and "object_identity" in record
    }
    removed = 0
    for item in list(mesh_objects):
        if item.type != "MESH":
            continue
        if _runtime_identity(item) in proof_ids:
            continue
        bpy.data.objects.remove(item, do_unlink=True)
        removed += 1
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    return {
        "count": removed,
        "reason": "not-in-pre-animation-render-inventory",
    }


def restore_render_attachments(
    proof: list[dict[str, Any]], mesh_objects: list[Any], scene_objects: list[Any]
) -> None:
    """Restore exact prevalidated parenting without emitting private details."""

    current_by_identity = {_runtime_identity(item): item for item in mesh_objects}
    if len(current_by_identity) != len(proof):
        raise RuntimeError("animation import changed the render attachment inventory")
    available_by_identity = {_runtime_identity(item): item for item in scene_objects}
    for record in proof:
        current = current_by_identity.get(record["object_identity"])
        if current is None:
            raise RuntimeError(
                "animation import changed the render attachment inventory"
            )
        parent_identity = record["parent_identity"]
        parent = None
        if parent_identity is not None:
            parent = available_by_identity.get(parent_identity)
            if parent is None:
                raise RuntimeError(
                    "prevalidated attachment parent is unavailable after animation import"
                )
        try:
            current.parent = parent
            current.parent_type = record["parent_type"]
            current.parent_bone = record["parent_bone"]
            current.matrix_parent_inverse = _copy_private_transform(
                record["parent_inverse"]
            )
            current.matrix_basis = _copy_private_transform(record["local_transform"])
        except (
            AttributeError,
            ReferenceError,
            RuntimeError,
            TypeError,
            ValueError,
        ) as exc:
            raise RuntimeError(
                "could not restore prevalidated render attachment"
            ) from exc

        restored_parent = getattr(current, "parent", None)
        restored_parent_identity = (
            _runtime_identity(restored_parent) if restored_parent is not None else None
        )
        if (
            restored_parent_identity != parent_identity
            or str(getattr(current, "parent_type", "")) != record["parent_type"]
            or str(getattr(current, "parent_bone", "")) != record["parent_bone"]
        ):
            raise RuntimeError(
                "restored attachment relationship does not match pre-animation proof"
            )
        if not _private_transforms_close(
            getattr(current, "matrix_parent_inverse", None),
            record["parent_inverse"],
        ):
            raise RuntimeError(
                "restored parent-inverse matrix does not match pre-animation proof"
            )
        if not _private_transforms_close(
            getattr(current, "matrix_basis", None),
            record["local_transform"],
        ):
            raise RuntimeError(
                "restored local transform does not match pre-animation proof"
            )


def revalidate_restored_inventory(
    mesh_objects: list[Any],
    required_equipment: Iterable[str],
    rig: Any,
    expected_inventory: list[dict[str, Any]],
    expected_equipment: dict[str, dict[str, Any]],
) -> None:
    try:
        inventory, equipment = build_mesh_inventory(
            mesh_objects, required_equipment, rig
        )
    except (RuntimeError, ValueError) as exc:
        raise RuntimeError("restored attachment semantic revalidation failed") from exc
    if inventory != expected_inventory or equipment != expected_equipment:
        raise RuntimeError(
            "restored attachment semantics differ from pre-animation proof"
        )


def find_single_rig() -> bpy.types.Object:
    rigs = [item for item in bpy.data.objects if item.type == "ARMATURE"]
    if len(rigs) != 1:
        raise RuntimeError(
            f"expected one armature after model import, found {len(rigs)}"
        )
    return rigs[0]


def find_model_rig(asset_kind: str) -> Any:
    """Resolve the model rig; animated composite carriers may be rigless.

    A rigless model is legitimate only for animated conversion, where every
    requested clip must key its own auxiliary rig (composite citadel models
    ship static base meshes while their clips target sibling hierarchies).
    Hierarchical and static conversions keep their exact single/zero rig
    contracts.
    """

    if asset_kind == "static":
        return find_static_rig()
    rigs = [item for item in bpy.data.objects if item.type == "ARMATURE"]
    if asset_kind == "animated" and not rigs:
        return None
    if len(rigs) != 1:
        raise RuntimeError(
            f"expected one armature after model import, found {len(rigs)}"
        )
    return rigs[0]


def _scene_armature_objects() -> list[Any]:
    return [
        item
        for item in list(getattr(bpy.data, "objects", []) or [])
        if getattr(item, "type", None) == "ARMATURE"
    ]


def _owned_active_actions(candidate: Any) -> list[Any]:
    owned: list[Any] = []
    for owner in (candidate, getattr(candidate, "data", None)):
        animation_data = getattr(owner, "animation_data", None)
        active = getattr(animation_data, "action", None)
        if active is not None:
            owned.append(active)
    return owned


def find_animation_owner_rig(model_rig: Any, created_actions: list[Any]) -> Any:
    """Return the single rig owning every action one clip import created.

    Cross-hierarchy clips (mounted rigs, composite citadel siblings) key the
    auxiliary armature the pinned importer creates for their own hierarchy;
    same-hierarchy clips key the model rig. The owner must be unique or the
    capture that follows could silently attribute curves to the wrong rig.
    """

    if not created_actions:
        raise RuntimeError("W3D animation did not create an owned keyed action")
    created_identities = {_runtime_identity(action) for action in created_actions}
    candidates: list[tuple[Any, set[Any]]] = []
    rigs = _scene_armature_objects()
    if model_rig is not None and all(item is not model_rig for item in rigs):
        rigs.append(model_rig)
    for candidate in rigs:
        owned_identities = {
            _runtime_identity(action)
            for action in _owned_active_actions(candidate)
        }
        if owned_identities & created_identities:
            candidates.append((candidate, owned_identities))
    if not candidates:
        raise RuntimeError("W3D animation did not create an owned keyed action")
    if len(candidates) != 1:
        raise RuntimeError("W3D animation created actions across ambiguous owner rigs")
    candidate, owned_identities = candidates[0]
    if not created_identities.issubset(owned_identities):
        raise RuntimeError("W3D animation created actions outside its proven owner set")
    return candidate


def find_static_rig() -> Any:
    """Reject skeletal static imports instead of silently baking an arbitrary pose."""

    rigs = [item for item in bpy.data.objects if item.type == "ARMATURE"]
    if rigs:
        raise RuntimeError(
            f"static W3D import must be armature-free, found {len(rigs)} armature(s)"
        )
    return None


def assert_non_animated_scene_has_no_actions(asset_kind: str) -> None:
    if asset_kind == "animated":
        return
    actions = list(getattr(bpy.data, "actions", []) or [])
    active_actions = 0
    for item in list(getattr(bpy.data, "objects", []) or []):
        for owner in (item, getattr(item, "data", None)):
            animation_data = getattr(owner, "animation_data", None)
            if (
                animation_data is not None
                and getattr(animation_data, "action", None) is not None
            ):
                active_actions += 1
    if actions or active_actions:
        raise RuntimeError(f"{asset_kind} W3D import contains animation actions")


def detach_actions(rig: bpy.types.Object) -> None:
    if rig.animation_data is not None:
        rig.animation_data.action = None
    if rig.data.animation_data is not None:
        rig.data.animation_data.action = None


def _action_has_keyed_curves(action: Any) -> bool:
    curves = list(getattr(action, "fcurves", []) or [])
    return (
        bool(curves)
        and sum(len(getattr(curve, "keyframe_points", []) or []) for curve in curves)
        > 0
    )


def _action_curve_shape(action: Any) -> dict[str, int]:
    """Classify keyed W3D channels without assuming every clip has visibility."""

    transform_curves = 0
    visibility_curves = 0
    material_curves = 0
    unsupported_curves = 0
    for curve in list(getattr(action, "fcurves", []) or []):
        path = str(getattr(curve, "data_path", ""))
        if path == "hide_viewport" or (
            path.startswith('bones["') and path.endswith('"].visibility')
        ):
            visibility_curves += 1
        elif path in {
            "location",
            "rotation_axis_angle",
            "rotation_euler",
            "rotation_quaternion",
            "scale",
        } or (
            path.startswith('pose.bones["')
            and path.rsplit(".", 1)[-1]
            in {
                "location",
                "rotation_axis_angle",
                "rotation_euler",
                "rotation_quaternion",
                "scale",
            }
        ):
            transform_curves += 1
        elif path.startswith("materials[") or path.startswith("nodes["):
            material_curves += 1
        else:
            unsupported_curves += 1
    return {
        "transform_curve_count": transform_curves,
        "visibility_curve_count": visibility_curves,
        "material_curve_count": material_curves,
        "unsupported_curve_count": unsupported_curves,
    }


def _owned_action_semantics(
    owned_actions: Iterable[tuple[str, Any]],
) -> tuple[str, list[dict[str, Any]]]:
    """Fingerprint the full keyed pair and retain exact non-glTF visibility keys."""

    digest = hashlib.sha256()
    visibility_channels: list[dict[str, Any]] = []
    rows = []
    for owner, action in owned_actions:
        for curve in list(getattr(action, "fcurves", []) or []):
            rows.append((owner, curve))
    for owner, curve in sorted(
        rows,
        key=lambda item: (
            item[0],
            str(getattr(item[1], "data_path", "")),
            int(item[1].array_index),
        ),
    ):
        path = str(curve.data_path)
        digest.update(owner.encode("ascii"))
        digest.update(path.encode("utf-8"))
        digest.update(str(int(curve.array_index)).encode("ascii"))
        keys = []
        for point in list(getattr(curve, "keyframe_points", []) or []):
            frame = float(point.co[0])
            value = float(point.co[1])
            interpolation = str(getattr(point, "interpolation", ""))
            digest.update(f"{frame:.9g},{value:.9g},{interpolation};".encode("ascii"))
            keys.append(
                {
                    "frame": frame,
                    "value": value,
                    "interpolation": interpolation,
                }
            )
        if _action_curve_shape(type("SingleCurveAction", (), {"fcurves": [curve]})())[
            "visibility_curve_count"
        ]:
            visibility_channels.append(
                {
                    "owner": owner,
                    "data_path": path,
                    "array_index": int(curve.array_index),
                    "keys": keys,
                }
            )
    return digest.hexdigest(), visibility_channels


def capture_w3d_animation_actions(
    rig: Any, actions: Iterable[Any], source_name: str
) -> tuple[list[Any], dict[str, Any]]:
    """Capture the exact owner/curve shape created by one pinned W3D import."""

    object_animation = getattr(rig, "animation_data", None)
    armature = getattr(rig, "data", None)
    data_animation = getattr(armature, "animation_data", None)
    object_action = getattr(object_animation, "action", None)
    data_action = getattr(data_animation, "action", None)
    owned = [action for action in (object_action, data_action) if action is not None]
    if not owned:
        raise RuntimeError("W3D animation did not create an owned keyed action")
    if len(owned) == 2 and _same_runtime_identity(object_action, data_action):
        raise RuntimeError("split W3D animation action pair is not distinct")

    imported = list(actions)
    expected = {_runtime_identity(action) for action in owned}
    actual = {_runtime_identity(action) for action in imported}
    if len(imported) != len(owned) or actual != expected:
        raise RuntimeError("W3D animation created actions outside its proven owner set")
    aggregate = {
        "transform_curve_count": 0,
        "visibility_curve_count": 0,
        "material_curve_count": 0,
        "unsupported_curve_count": 0,
    }
    for action in owned:
        if not _action_has_keyed_curves(action):
            raise RuntimeError("W3D animation action has no keyed curves")
        for key, value in _action_curve_shape(action).items():
            aggregate[key] += value
        action.use_fake_user = True
    if aggregate["unsupported_curve_count"]:
        raise RuntimeError("W3D animation contains unsupported keyed channel paths")
    if aggregate["material_curve_count"]:
        shape = "material"
    elif aggregate["transform_curve_count"] and aggregate["visibility_curve_count"]:
        shape = "transform-and-visibility"
    elif aggregate["transform_curve_count"]:
        shape = "transform-only"
    elif aggregate["visibility_curve_count"]:
        shape = "visibility-only"
    else:
        raise RuntimeError("W3D animation actions have no typed keyed channels")
    owned_semantics = []
    if object_action is not None:
        owned_semantics.append(("object", object_action))
    if data_action is not None:
        owned_semantics.append(("armature", data_action))
    semantic_fingerprint, visibility_channels = _owned_action_semantics(owned_semantics)
    export_object_action = object_action
    action_copy = getattr(object_action, "copy", None)
    if callable(action_copy):
        export_object_action = action_copy()
        export_object_action.name = clean_name(source_name)
        export_object_action.use_fake_user = True
    public = {
        "name": clean_name(source_name),
        "shape": shape,
        "action_count": len(owned),
        "object_action_count": int(object_action is not None),
        "armature_action_count": int(data_action is not None),
        **aggregate,
    }
    return owned, {
        "public": public,
        "semantic_fingerprint": semantic_fingerprint,
        "visibility_channels": visibility_channels,
        "object_action": export_object_action,
    }


def capture_split_w3d_animation_actions(rig: Any, actions: Iterable[Any]) -> list[Any]:
    """Compatibility wrapper retaining strict pair semantics for old callers/tests."""

    captured, shape = capture_w3d_animation_actions(rig, actions, "split")
    if (
        shape["public"]["object_action_count"] != 1
        or shape["public"]["armature_action_count"] != 1
    ):
        raise RuntimeError(
            "split W3D animation did not create an object/armature action pair"
        )
    return captured


def prepare_w3d_animation_nla_tracks(
    rig: Any, action_shapes: list[dict[str, Any]]
) -> int:
    """Bind each logical W3D transform action to one named NLA export track.

    Each clip is tracked on the rig that actually owns its actions: the model
    rig for same-hierarchy clips, the auxiliary armature for cross-hierarchy
    clips. Shapes without an owner record belong to the model rig.
    """

    owner_rigs: list[Any] = []
    seen_identities: set[Any] = set()
    for candidate in [rig, *(shape.get("owner_rig") for shape in action_shapes)]:
        if candidate is None:
            continue
        identity = _runtime_identity(candidate)
        if identity not in seen_identities:
            seen_identities.add(identity)
            owner_rigs.append(candidate)
    created = 0
    for owner in owner_rigs:
        detach_actions(owner)
        animation_data_create = getattr(owner, "animation_data_create", None)
        if callable(animation_data_create):
            animation_data_create()
        animation_data = getattr(owner, "animation_data", None)
        tracks = getattr(animation_data, "nla_tracks", None)
        if tracks is None:
            raise RuntimeError("W3D rig has no NLA track collection")
        while len(tracks):
            tracks.remove(tracks[0])
        owner_identity = _runtime_identity(owner)
        for shape in action_shapes:
            shape_owner = shape.get("owner_rig", rig)
            if shape_owner is None or _runtime_identity(shape_owner) != (
                owner_identity
            ):
                continue
            action = shape.get("object_action")
            if action is None or shape["public"]["transform_curve_count"] < 1:
                continue
            track = tracks.new()
            track.name = shape["public"]["name"]
            frame_range = getattr(action, "frame_range", (0.0, 0.0))
            raw_start = float(frame_range[0])
            start = int(round(raw_start))
            if abs(raw_start - start) > 1.0e-6:
                raise RuntimeError("W3D action has a fractional NLA start frame")
            strip = track.strips.new(shape["public"]["name"], start, action)
            strip.name = shape["public"]["name"]
            created += 1
    expected = sum(
        1 for shape in action_shapes if shape["public"]["transform_curve_count"] > 0
    )
    if created != expected:
        raise RuntimeError("W3D action shape lacks an NLA transform carrier")
    return created


def restore_duplicate_logical_animations(
    output: Path, action_shapes: list[dict[str, Any]]
) -> dict[str, int]:
    """Restore named source clips that Blender dropped only as exact duplicates.

    Blender's Actions exporter may collapse one of several distinct W3D source
    clips when their glTF-supported transform curves are byte-identical. W3D
    still treats those names as distinct state-machine clips. Cloning the
    already-exported identical transform payload preserves that exact source
    distinction without inventing motion.
    """

    payload = output.read_bytes()
    if len(payload) < 20:
        raise RuntimeError("animation GLB is truncated")
    magic, version, declared_length = struct.unpack_from("<4sII", payload, 0)
    if magic != b"glTF" or version != 2 or declared_length != len(payload):
        raise RuntimeError("animation output is not a consistent glTF 2 GLB")
    chunks: list[tuple[int, bytes]] = []
    document: dict[str, Any] | None = None
    cursor = 12
    json_index = -1
    while cursor < len(payload):
        if cursor + 8 > len(payload):
            raise RuntimeError("animation GLB chunk header is truncated")
        chunk_length, chunk_type = struct.unpack_from("<II", payload, cursor)
        cursor += 8
        chunk = payload[cursor : cursor + chunk_length]
        cursor += chunk_length
        if len(chunk) != chunk_length:
            raise RuntimeError("animation GLB chunk is truncated")
        if chunk_type == 0x4E4F534A:
            if document is not None:
                raise RuntimeError("animation GLB has multiple JSON chunks")
            document = json.loads(chunk.rstrip(b"\x00 \t\r\n").decode("utf-8"))
            json_index = len(chunks)
        chunks.append((chunk_type, chunk))
    if cursor != len(payload) or document is None or json_index < 0:
        raise RuntimeError("animation GLB chunk layout is invalid")

    if not action_shapes:
        raise RuntimeError(
            "animation GLB is missing channels without sealed source action proof"
        )
    expected = [
        shape["public"]["name"]
        for shape in action_shapes
        if shape["public"]["transform_curve_count"] > 0
    ]
    animations = document.get("animations")
    if not isinstance(animations, list):
        if expected:
            raise RuntimeError("animation GLB has no required transform animations")
        # Blender cannot emit Westwood visibility curves as glTF animation
        # channels.  Accept an absent animations array only when every sealed
        # importer shape proves that visibility is the entire authored clip.
        # The exact keyed visibility payload is retained below as root extras;
        # no transform clip or motion is synthesized.
        for shape in action_shapes:
            public = shape.get("public")
            channels = shape.get("visibility_channels")
            if (
                not isinstance(public, dict)
                or public.get("shape") != "visibility-only"
                or public.get("transform_curve_count") != 0
                or type(public.get("visibility_curve_count")) is not int
                or public["visibility_curve_count"] < 1
                or public.get("material_curve_count") != 0
                or public.get("unsupported_curve_count") != 0
                or not isinstance(channels, list)
                or len(channels) != public["visibility_curve_count"]
                or any(
                    not isinstance(channel, dict)
                    or not isinstance(channel.get("keys"), list)
                    or not channel["keys"]
                    for channel in channels
                )
            ):
                raise RuntimeError(
                    "animation GLB is missing channels without sealed "
                    "visibility-only source proof"
                )
        animations = []
    by_name = {
        clean_name(str(animation.get("name", ""))): animation
        for animation in animations
        if isinstance(animation, dict)
    }
    shape_by_name = {shape["public"]["name"]: shape for shape in action_shapes}
    duplicated = 0
    for name, shape in shape_by_name.items():
        if name in by_name:
            continue
        fingerprint = shape.get("semantic_fingerprint")
        if not isinstance(fingerprint, str) or not fingerprint:
            continue
        source_name = next(
            (
                candidate_name
                for candidate_name, candidate_shape in shape_by_name.items()
                if candidate_name in by_name
                and candidate_shape.get("semantic_fingerprint") == fingerprint
            ),
            None,
        )
        if source_name is None:
            continue
        clone = copy_module.deepcopy(by_name[source_name])
        clone["name"] = name
        by_name[name] = clone
        duplicated += 1
    if any(name not in by_name for name in expected):
        raise RuntimeError("missing animation is not an exact exported duplicate")
    visibility_channel_count = 0
    visibility_key_count = 0
    for name in expected:
        channels = shape_by_name[name]["visibility_channels"]
        if not channels:
            continue
        animation = by_name[name]
        extras = animation.setdefault("extras", {})
        if not isinstance(extras, dict):
            raise RuntimeError("animation GLB extras are not an object")
        contract = {
            "schema": "openbfme.w3d-visibility-channels",
            "version": 1,
            "channels": channels,
        }
        existing = extras.get("openbfme_w3d_visibility")
        if existing is not None and existing != contract:
            raise RuntimeError("animation GLB visibility extras conflict")
        extras["openbfme_w3d_visibility"] = contract
        visibility_channel_count += len(channels)
        visibility_key_count += sum(len(channel["keys"]) for channel in channels)
    visibility_only = [
        {
            "name": shape["public"]["name"],
            "shape": "visibility-only",
            "channels": shape["visibility_channels"],
        }
        for shape in action_shapes
        if shape["public"]["shape"] == "visibility-only"
    ]
    if visibility_only:
        extras = document.setdefault("extras", {})
        if not isinstance(extras, dict):
            raise RuntimeError("animation GLB root extras are not an object")
        contract = {
            "schema": "openbfme.w3d-visibility-only-animations",
            "version": 1,
            "animations": visibility_only,
        }
        existing = extras.get("openbfme_w3d_visibility_only_animations")
        if existing is not None and existing != contract:
            raise RuntimeError("animation GLB visibility-only extras conflict")
        extras["openbfme_w3d_visibility_only_animations"] = contract
        visibility_channel_count += sum(
            len(item["channels"]) for item in visibility_only
        )
        visibility_key_count += sum(
            len(channel["keys"])
            for item in visibility_only
            for channel in item["channels"]
        )
    if expected:
        document["animations"] = [by_name[name] for name in expected]
    else:
        # Preserve Blender's truthful static-geometry representation.  An
        # empty animations array would not add channels, but its absence is the
        # exact exporter result and makes the no-motion contract unambiguous.
        document.pop("animations", None)
    encoded = json.dumps(
        document, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")
    encoded += b" " * ((-len(encoded)) % 4)
    chunks[json_index] = (0x4E4F534A, encoded)
    body = b"".join(
        struct.pack("<II", len(chunk), chunk_type) + chunk
        for chunk_type, chunk in chunks
    )
    output.write_bytes(struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body)
    return {
        "duplicated_animations": duplicated,
        "visibility_channels": visibility_channel_count,
        "visibility_keys": visibility_key_count,
        "visibility_only_animations": len(visibility_only),
    }


def validate_split_animation_glb(
    output: Path,
    expected_names: Iterable[str],
    *,
    require_skins: bool = True,
    require_skeletal_mesh: bool = True,
) -> dict[str, int]:
    """Require the exact emitted glTF animation set and skeletal geometry.

    ``require_skins`` is the scene-proven contract that skinned meshes had to
    survive the export. Proven rigid animated models carry no skinned meshes;
    their bone- or armature-parented render meshes are skeletal content even
    though the GLB has no skins array.
    """

    expected = [clean_name(name) for name in expected_names]
    if any(not name for name in expected) or len(expected) != len(set(expected)):
        raise ValueError("split-animation GLB expected names are invalid")

    with output.open("rb") as stream:
        header = stream.read(12)
        if len(header) != 12:
            raise RuntimeError("split-animation GLB header is truncated")
        magic, version, declared_length = struct.unpack("<4sII", header)
        if magic != b"glTF" or version != 2:
            raise RuntimeError("split-animation output is not a glTF 2 GLB")
        if declared_length != output.stat().st_size:
            raise RuntimeError("split-animation GLB length is inconsistent")
        document = None
        consumed = 12
        while consumed < declared_length:
            chunk_header = stream.read(8)
            if len(chunk_header) != 8:
                raise RuntimeError("split-animation GLB chunk header is truncated")
            chunk_length, chunk_type = struct.unpack("<II", chunk_header)
            consumed += 8 + chunk_length
            if consumed > declared_length:
                raise RuntimeError("split-animation GLB chunk exceeds file length")
            payload = stream.read(chunk_length)
            if len(payload) != chunk_length:
                raise RuntimeError("split-animation GLB chunk is truncated")
            if chunk_type == 0x4E4F534A:
                if document is not None:
                    raise RuntimeError("split-animation GLB has multiple JSON chunks")
                try:
                    document = json.loads(
                        payload.rstrip(b"\x00 \t\r\n").decode("utf-8")
                    )
                except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                    raise RuntimeError(
                        "split-animation GLB JSON chunk is invalid"
                    ) from exc
        if consumed != declared_length or stream.read(1):
            raise RuntimeError("split-animation GLB has trailing or missing bytes")

    if not isinstance(document, dict):
        raise RuntimeError("split-animation GLB has no JSON document")
    raw_animations = document.get("animations")
    if expected:
        animations = raw_animations
    elif raw_animations is None:
        animations = []
    elif isinstance(raw_animations, list) and not raw_animations:
        animations = raw_animations
    else:
        raise RuntimeError("W3D visibility-only output emitted unexpected animation")
    if not isinstance(animations, list) or len(animations) != len(expected):
        raise RuntimeError(
            "W3D split actions did not export as the requested animation count"
        )
    actual_names = [
        clean_name(str(animation.get("name", "")))
        if isinstance(animation, dict)
        else ""
        for animation in animations
    ]
    if sorted(actual_names) != sorted(expected):
        raise RuntimeError("split-animation GLB animation name changed")
    channel_count = 0
    sampler_count = 0
    visibility_channel_count = 0
    visibility_key_count = 0
    for animation in animations:
        channels = animation.get("channels")
        samplers = animation.get("samplers")
        if not isinstance(channels, list) or not channels:
            raise RuntimeError("split-animation GLB has no animation channels")
        if not isinstance(samplers, list) or not samplers:
            raise RuntimeError("split-animation GLB has no animation samplers")
        for channel in channels:
            if (
                not isinstance(channel, dict)
                or not isinstance(channel.get("sampler"), int)
                or not 0 <= channel["sampler"] < len(samplers)
                or not isinstance(channel.get("target"), dict)
                or not isinstance(channel["target"].get("node"), int)
                or channel["target"].get("path")
                not in {"translation", "rotation", "scale", "weights"}
            ):
                raise RuntimeError("split-animation GLB channel is invalid")
        channel_count += len(channels)
        sampler_count += len(samplers)
        extras = animation.get("extras", {})
        if not isinstance(extras, dict):
            raise RuntimeError("split-animation GLB extras are invalid")
        visibility = extras.get("openbfme_w3d_visibility")
        if visibility is None:
            continue
        if (
            not isinstance(visibility, dict)
            or set(visibility) != {"schema", "version", "channels"}
            or visibility.get("schema") != "openbfme.w3d-visibility-channels"
            or visibility.get("version") != 1
            or not isinstance(visibility.get("channels"), list)
            or not visibility["channels"]
        ):
            raise RuntimeError("split-animation GLB visibility extras are invalid")
        for visibility_channel in visibility["channels"]:
            if (
                not isinstance(visibility_channel, dict)
                or set(visibility_channel)
                != {"owner", "data_path", "array_index", "keys"}
                or visibility_channel.get("owner") not in {"object", "armature"}
                or not isinstance(visibility_channel.get("data_path"), str)
                or type(visibility_channel.get("array_index")) is not int
                or not isinstance(visibility_channel.get("keys"), list)
                or not visibility_channel["keys"]
            ):
                raise RuntimeError("split-animation GLB visibility channel is invalid")
            path = visibility_channel["data_path"]
            if path != "hide_viewport" and not (
                path.startswith('bones["') and path.endswith('"].visibility')
            ):
                raise RuntimeError("split-animation GLB visibility path is invalid")
            for key in visibility_channel["keys"]:
                if (
                    not isinstance(key, dict)
                    or set(key) != {"frame", "value", "interpolation"}
                    or isinstance(key.get("frame"), bool)
                    or not isinstance(key.get("frame"), (int, float))
                    or isinstance(key.get("value"), bool)
                    or not isinstance(key.get("value"), (int, float))
                    or not isinstance(key.get("interpolation"), str)
                ):
                    raise RuntimeError("split-animation GLB visibility key is invalid")
            visibility_channel_count += 1
            visibility_key_count += len(visibility_channel["keys"])

    visibility_only_animation_count = 0
    root_extras = document.get("extras", {})
    if not isinstance(root_extras, dict):
        raise RuntimeError("split-animation GLB root extras are invalid")
    visibility_only = root_extras.get("openbfme_w3d_visibility_only_animations")
    if visibility_only is not None:
        if (
            not isinstance(visibility_only, dict)
            or set(visibility_only) != {"schema", "version", "animations"}
            or visibility_only.get("schema")
            != "openbfme.w3d-visibility-only-animations"
            or visibility_only.get("version") != 1
            or not isinstance(visibility_only.get("animations"), list)
            or not visibility_only["animations"]
        ):
            raise RuntimeError("split-animation visibility-only extras are invalid")
        seen_visibility_names: set[str] = set()
        for item in visibility_only["animations"]:
            if (
                not isinstance(item, dict)
                or set(item) != {"name", "shape", "channels"}
                or not isinstance(item.get("name"), str)
                or clean_name(item["name"]) in seen_visibility_names
                or item.get("shape") != "visibility-only"
                or not isinstance(item.get("channels"), list)
                or not item["channels"]
            ):
                raise RuntimeError("visibility-only animation entry is invalid")
            seen_visibility_names.add(clean_name(item["name"]))
            visibility_only_animation_count += 1
            for channel in item["channels"]:
                if (
                    not isinstance(channel, dict)
                    or set(channel) != {"owner", "data_path", "array_index", "keys"}
                    or channel.get("owner") not in {"object", "armature"}
                    or not isinstance(channel.get("data_path"), str)
                    or type(channel.get("array_index")) is not int
                    or not isinstance(channel.get("keys"), list)
                    or not channel["keys"]
                ):
                    raise RuntimeError("visibility-only animation channel is invalid")
                path = channel["data_path"]
                if path != "hide_viewport" and not (
                    path.startswith('bones["') and path.endswith('"].visibility')
                ):
                    raise RuntimeError("visibility-only animation path is invalid")
                for key in channel["keys"]:
                    if (
                        not isinstance(key, dict)
                        or set(key) != {"frame", "value", "interpolation"}
                        or isinstance(key.get("frame"), bool)
                        or not isinstance(key.get("frame"), (int, float))
                        or isinstance(key.get("value"), bool)
                        or not isinstance(key.get("value"), (int, float))
                        or not isinstance(key.get("interpolation"), str)
                    ):
                        raise RuntimeError("visibility-only animation key is invalid")
                visibility_channel_count += 1
                visibility_key_count += len(channel["keys"])

    skins = document.get("skins")
    nodes = document.get("nodes")
    if require_skins and (not isinstance(skins, list) or not skins):
        raise RuntimeError("split-animation GLB has no skeletal skin")
    if skins is None:
        skins = []
    if not isinstance(skins, list):
        raise RuntimeError("split-animation GLB has an invalid skeletal skin array")
    if not isinstance(nodes, list) or not nodes:
        raise RuntimeError("split-animation GLB has no nodes")
    joint_nodes: set[int] = set()
    for skin in skins:
        if (
            not isinstance(skin, dict)
            or not isinstance(skin.get("joints"), list)
            or not skin["joints"]
            or any(
                not isinstance(index, int) or not 0 <= index < len(nodes)
                for index in skin["joints"]
            )
        ):
            raise RuntimeError("split-animation GLB skin has invalid joints")
        joint_nodes.update(skin["joints"])

    parents: dict[int, int] = {}
    for parent_index, node in enumerate(nodes):
        if not isinstance(node, dict):
            raise RuntimeError("split-animation GLB node is invalid")
        children = node.get("children", [])
        if not isinstance(children, list) or any(
            not isinstance(child, int) or not 0 <= child < len(nodes)
            for child in children
        ):
            raise RuntimeError("split-animation GLB node children are invalid")
        for child in children:
            if child in parents:
                raise RuntimeError("split-animation GLB node has multiple parents")
            parents[child] = parent_index

    # Proven rigid animated models have no skins; their render meshes are
    # still skeletal content when they hang off a bone (under a joint) or off
    # the armature itself. Armature-parented meshes are a fallback so exact
    # counts of skinned or bone-parented meshes never change. With no joints
    # and no exported channels (an empty single-pivot carrier whose clip only
    # keys object visibility), the armature node is the mesh's non-mesh
    # parent itself.
    armature_nodes: set[int] = set()
    if not require_skins:
        for child in joint_nodes:
            parent = parents.get(child)
            if parent is not None and parent not in joint_nodes:
                armature_nodes.add(parent)
        for animation in animations:
            channels = animation.get("channels") if isinstance(animation, dict) else []
            for channel in channels if isinstance(channels, list) else []:
                if not isinstance(channel, dict):
                    continue
                target = channel.get("target")
                if not isinstance(target, dict):
                    continue
                target_node = target.get("node")
                if isinstance(target_node, int):
                    parent = parents.get(target_node)
                    if parent is not None:
                        armature_nodes.add(parent)
        for node_index, node in enumerate(nodes):
            if isinstance(node, dict) and not isinstance(node.get("mesh"), int):
                armature_nodes.add(node_index)
    skeletal_mesh_count = 0
    armature_parented_mesh_count = 0
    for node_index, node in enumerate(nodes):
        if not isinstance(node.get("mesh"), int):
            continue
        skin_index = node.get("skin")
        if isinstance(skin_index, int) and 0 <= skin_index < len(skins):
            skeletal_mesh_count += 1
            continue
        seen: set[int] = set()
        parent = parents.get(node_index)
        bone_parented = False
        armature_parented = False
        while parent is not None and parent not in seen:
            if parent in joint_nodes:
                bone_parented = True
                break
            if parent in armature_nodes:
                armature_parented = True
                break
            seen.add(parent)
            parent = parents.get(parent)
        if bone_parented:
            skeletal_mesh_count += 1
        elif armature_parented:
            armature_parented_mesh_count += 1
    if skeletal_mesh_count == 0:
        skeletal_mesh_count = armature_parented_mesh_count
    if require_skeletal_mesh and skeletal_mesh_count < 1:
        raise RuntimeError(
            "split-animation GLB has no skinned or bone-parented mesh node"
        )
    return {
        "animations": len(animations),
        "channels": channel_count,
        "samplers": sampler_count,
        "skins": len(skins),
        "skeletal_meshes": skeletal_mesh_count,
        "visibility_channels": visibility_channel_count,
        "visibility_keys": visibility_key_count,
        "visibility_only_animations": visibility_only_animation_count,
    }


def validate_embedded_animation_glb(output: Path, expected_name: str) -> dict[str, int]:
    """Compatibility wrapper for the one self-contained animation case."""

    return validate_split_animation_glb(output, [expected_name])


def remove_non_render_geometry() -> dict[str, Any]:
    """Drop W3D collision/volume helpers before the GLB is exported.

    The OpenSAGE importer deliberately exposes collision boxes and other W3D
    helper geometry in Blender.  Those meshes are useful to an authoring tool,
    but glTF has no equivalent semantic and would otherwise render them as
    opaque purple boxes around every unit.
    """

    removed_count = 0
    removed_types: Counter[str] = Counter()
    removed_reasons: Counter[str] = Counter()
    for item in list(bpy.data.objects):
        if item.type != "MESH":
            continue
        reasons = _non_render_reasons(item)
        if not reasons:
            continue
        removed_count += 1
        removed_types[_w3d_object_type(item) or "UNDECLARED"] += 1
        for reason in reasons:
            removed_reasons[reason] += 1
        bpy.data.objects.remove(item, do_unlink=True)

    # Keep the exported inventory and metrics honest; Blender data blocks can
    # outlive their removed object until explicitly collected.
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    for material in list(bpy.data.materials):
        if material.users == 0:
            bpy.data.materials.remove(material)
    return {
        "count": removed_count,
        "object_types": [
            {"type": name, "count": count}
            for name, count in sorted(removed_types.items())
        ],
        "reasons": [
            {"reason": name, "count": count}
            for name, count in sorted(removed_reasons.items())
        ],
    }


_INITIALIZED_W3D_PLUGIN_ROOT: Path | None = None


def _model_import_phase_scope(
    checkpoint: _W3DConversionPhaseCheckpoint,
) -> _NoopModelImportPhaseScope | _PinnedModelImportPhaseScope:
    if _INITIALIZED_W3D_PLUGIN_ROOT is None:
        return _NoopModelImportPhaseScope(checkpoint)
    return _PinnedModelImportPhaseScope(checkpoint)


def _invoke_w3d_import(
    source: Path,
    *,
    import_phase_scope: _NoopModelImportPhaseScope | _PinnedModelImportPhaseScope,
) -> Any:
    return import_phase_scope.invoke(source)


def initialize_w3d_converter(plugin_root: Path) -> None:
    """Register the pinned importer and compatibility shim once per process."""

    global _INITIALIZED_W3D_PLUGIN_ROOT
    plugin_root = plugin_root.expanduser().resolve()
    if not (plugin_root / "io_mesh_w3d" / "__init__.py").is_file():
        raise FileNotFoundError(plugin_root)
    if _INITIALIZED_W3D_PLUGIN_ROOT is not None:
        if plugin_root != _INITIALIZED_W3D_PLUGIN_ROOT:
            raise RuntimeError(
                "W3D converter cannot switch plugin roots in one process"
            )
        return

    # Factory state must be loaded before registering the pinned plugin. Loading
    # it again between batch jobs can unregister operators and makes registration
    # process-order dependent.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    plugin_root_text = str(plugin_root)
    if plugin_root_text not in sys.path:
        sys.path.insert(0, plugin_root_text)
    import io_mesh_w3d  # type: ignore

    io_mesh_w3d.register()
    install_shader_material_compatibility_shim()
    install_retail_hlod_exclusion_shim()
    _INITIALIZED_W3D_PLUGIN_ROOT = plugin_root


def _remove_all_datablocks(collection_name: str) -> None:
    collection = getattr(bpy.data, collection_name, None)
    if collection is None:
        return
    for item in list(collection):
        try:
            collection.remove(item, do_unlink=True)
        except TypeError:
            collection.remove(item)


def reset_w3d_conversion_scene() -> None:
    """Restore an empty factory-equivalent conversion scene without re-registering."""

    if _INITIALIZED_W3D_PLUGIN_ROOT is None:
        raise RuntimeError("W3D converter plugin has not been initialized")
    scene = bpy.context.scene
    scene.camera = None
    scene.world = None
    animation_data_clear = getattr(scene, "animation_data_clear", None)
    if callable(animation_data_clear):
        animation_data_clear()
    for key in list(scene.keys()):
        del scene[key]
    timeline_markers = getattr(scene, "timeline_markers", None)
    if timeline_markers is not None:
        for marker in list(timeline_markers):
            timeline_markers.remove(marker)

    # Objects go first so every dependent block becomes removable. Direct
    # datablock removal also clears hidden and unlinked importer products that
    # an operator-only object deletion would miss after a failed prior job.
    for collection_name in (
        "objects",
        "collections",
        "actions",
        "armatures",
        "meshes",
        "shape_keys",
        "curves",
        "metaballs",
        "lattices",
        "grease_pencils_v3",
        "grease_pencils",
        "materials",
        "node_groups",
        "textures",
        "images",
        "cameras",
        "lights",
        "speakers",
        "sounds",
        "fonts",
        "particle_settings",
        "worlds",
    ):
        _remove_all_datablocks(collection_name)

    # Keep the active factory scene and remove any additional scenes a failed
    # importer may have created. Core timeline/render values are restored so an
    # animation import cannot influence the next static or hierarchical job.
    for candidate in list(bpy.data.scenes):
        if candidate != scene:
            bpy.data.scenes.remove(candidate, do_unlink=True)
    scene.frame_start = 1
    scene.frame_end = 250
    scene.frame_preview_start = 1
    scene.frame_preview_end = 250
    scene.use_preview_range = False
    scene.render.fps = 24
    scene.render.fps_base = 1.0
    scene.frame_set(1)

    contaminated = {
        name: len(getattr(bpy.data, name))
        for name in (
            "objects",
            "collections",
            "actions",
            "armatures",
            "meshes",
            "shape_keys",
            "materials",
            "images",
        )
        if len(getattr(bpy.data, name))
    }
    if contaminated:
        raise RuntimeError("W3D batch scene reset left conversion datablocks")


def _convert_w3d_job_impl(
    *,
    model: Path,
    asset_kind: str,
    animations: list[Path],
    required_equipment: list[str],
    excluded_optional_meshes: list[str],
    proven_root_rigid_bake: bool,
    proven_pivot_only_model: bool = False,
    retail_absent_textures: list[str] | None = None,
    output: Path,
    animation_output_ledger: AnimationImportOutputLedger,
    phase_checkpoint: _W3DConversionPhaseCheckpoint,
) -> dict[str, Any]:
    """Convert one job and return the established adapter report schema."""

    _ACTIVE_RETAIL_ORPHAN_HLOD_EXCLUSIONS.clear()
    _ACTIVE_SHADER_TEXTURE_SENTINEL_DROPS.clear()
    _ACTIVE_ROOT_RIGID_INERT_VERTEX_GROUPS.clear()

    args = argparse.Namespace(
        asset_kind=asset_kind,
        animations=animations,
        required_equipment=required_equipment,
        excluded_optional_meshes=excluded_optional_meshes,
        proven_root_rigid_bake=proven_root_rigid_bake,
        proven_pivot_only_model=proven_pivot_only_model,
        retail_absent_textures=normalize_retail_absent_textures(
            retail_absent_textures or []
        ),
    )
    model = model.expanduser().resolve()
    output = output.expanduser().resolve()
    if not model.is_file():
        raise FileNotFoundError(model)
    resolved_animations = [animation.expanduser().resolve() for animation in animations]
    embedded_model_animation = (
        asset_kind == "animated"
        and len(resolved_animations) == 1
        and resolved_animations[0] == model
    )

    phase_checkpoint.set("scene-reset")
    reset_w3d_conversion_scene()
    phase_checkpoint.set("model-import")
    with _model_import_phase_scope(phase_checkpoint) as import_phase_scope:
        if embedded_model_animation:
            phase_checkpoint.set("embedded-model-import")
            result = animation_output_ledger.capture(
                lambda: _invoke_w3d_import(
                    model,
                    import_phase_scope=import_phase_scope,
                ),
                operation_phase="embedded-model-import",
                phase_checkpoint=phase_checkpoint,
            )
        else:
            result = _invoke_w3d_import(
                model,
                import_phase_scope=import_phase_scope,
            )
    import_phase_scope.raise_if_failed()
    phase_checkpoint.set("model-import-validation")
    if result != {"FINISHED"}:
        raise RuntimeError(f"model import failed: {result}")
    phase_checkpoint.set("scene-validation")
    phase_checkpoint.set("request-validation")
    validate_asset_kind_request(
        args.asset_kind,
        args.animations,
        args.required_equipment,
        proven_root_rigid_bake=args.proven_root_rigid_bake,
        proven_pivot_only_model=args.proven_pivot_only_model,
    )
    phase_checkpoint.set("rig-validation")
    phase_checkpoint.set("rig-resolution")
    rig = find_model_rig(args.asset_kind)
    phase_checkpoint.set("action-validation")
    assert_non_animated_scene_has_no_actions(args.asset_kind)
    phase_checkpoint.set("geometry-validation")
    filtered_geometry = remove_non_render_geometry()
    model_mesh_objects = [item for item in bpy.data.objects if item.type == "MESH"]
    if not model_mesh_objects and not args.proven_pivot_only_model:
        raise RuntimeError("W3D model import created no meshes")
    phase_checkpoint.set("skin-validation")
    # Recorded retail-absent textures are unlinked before material passes see
    # the graph, so their placeholders never leak into additive or opaque
    # material conversion. Only scanner-recorded exclusions are tolerated.
    retail_absent_texture_result = (
        clear_retail_absent_textures(
            args.retail_absent_textures,
            staging_root=model.parent,
        )
        if args.retail_absent_textures
        else {"cleared": [], "unreferenced": []}
    )
    retail_absent_textures_cleared = retail_absent_texture_result["cleared"]
    retail_absent_textures_unreferenced = retail_absent_texture_result[
        "unreferenced"
    ]
    # Preserve the visual contribution of source-proven additive W3D textures
    # before the render payload is fingerprinted. Unproven materials are never
    # modified by this pass.
    phase_checkpoint.set("material-validation")
    convert_proven_additive_materials(
        list(bpy.data.materials), phase_checkpoint=phase_checkpoint
    )
    opaque_material_normalization = normalize_proven_opaque_materials(
        list(bpy.data.materials)
    )
    phase_checkpoint.set("presentation-validation")
    optional_mesh_exclusions = exclude_optional_render_meshes(
        model_mesh_objects,
        args.excluded_optional_meshes,
        args.required_equipment,
        rig,
    )
    model_mesh_objects = [item for item in bpy.data.objects if item.type == "MESH"]
    model_mesh_count = len(model_mesh_objects)
    if model_mesh_count < 1 and not args.proven_pivot_only_model:
        raise RuntimeError("W3D model import retained no meshes")
    root_rigid_bake = {
        "requested": False,
        "applied": False,
        "removed_carriers": 0,
        "baked_meshes": 0,
        "world_transforms_preserved": False,
        "deform_ambiguity_absent": False,
    }
    phase_checkpoint.set("skin-validation")
    if args.proven_root_rigid_bake:
        root_rigid_bake = bake_proven_root_rigid_hierarchy(
            args.asset_kind,
            True,
            rig,
            model_mesh_objects,
            bpy.data.objects,
        )
        rig = None
        model_mesh_objects = [item for item in bpy.data.objects if item.type == "MESH"]
    apply_w3d_hidden_extras(
        model_mesh_objects,
        w3d_hidden_mesh_names(model.read_bytes()),
    )
    phase_checkpoint.set("attachment-validation")
    phase_checkpoint.set("attachment-canonicalization")
    if rig is not None:
        canonicalize_required_rigid_attachments(
            model_mesh_objects, args.required_equipment, rig
        )
    # Prove equipment semantics against the model import itself. Animation-only
    # W3Ds may clear attachment parenting, so later work verifies this safe
    # render payload by private fingerprints instead of reclassifying it.
    mesh_inventory, equipment = build_mesh_inventory(
        model_mesh_objects,
        args.required_equipment,
        rig,
        phase_checkpoint=phase_checkpoint,
    )
    # An empty armature is only degenerate when real bone-deformed content
    # needs pivots. Rigid render meshes on an empty carrier (retail's
    # single-pivot models with object-level visibility clips) are a
    # legitimate retail shape. The check runs after the mesh inventory (it
    # needs the skinned flags), so re-anchor the phase to skin validation
    # instead of leaving the inventory's last checkpoint as the evidence.
    phase_checkpoint.set("skin-validation")
    if (
        rig is not None
        and len(getattr(rig.data, "bones", []) or []) < 1
        and not args.proven_root_rigid_bake
        and any(item["skinned"] for item in mesh_inventory)
    ):
        raise RuntimeError("skeletal W3D import has an empty hierarchy")
    phase_checkpoint.set("render-proof")
    render_geometry_proof = capture_render_geometry_proof(model_mesh_objects)
    render_attachment_proof = (
        capture_render_attachment_proof(model_mesh_objects) if rig is not None else []
    )

    imported_actions: list[bpy.types.Action] = []
    animation_action_shapes: list[dict[str, Any]] = []
    logical_animation_count = 0
    split_action_animation_count = 0
    embedded_export = {
        "animations": 0,
        "channels": 0,
        "samplers": 0,
        "skins": 0,
        "skeletal_meshes": 0,
        "visibility_channels": 0,
        "visibility_keys": 0,
        "visibility_only_animations": 0,
    }
    split_export = dict(embedded_export)
    action_shape_export = dict(embedded_export)
    duplicated_logical_animation_count = 0
    preserved_visibility_channel_count = 0
    preserved_visibility_key_count = 0
    visibility_only_sidecar_animation_count = 0
    discarded_embedded_model_action_count = 0
    phase_checkpoint.set("animation-import")
    if embedded_model_animation:
        if rig is None:
            raise RuntimeError("embedded W3D animation has no armature")
        imported_actions, action_shape = capture_w3d_animation_actions(
            rig, list(bpy.data.actions), resolved_animations[0].stem
        )
        action_shape["owner_rig"] = rig
        animation_action_shapes.append(action_shape)
        logical_animation_count = 1
        split_action_animation_count = int(
            action_shape["public"]["object_action_count"] == 1
            and action_shape["public"]["armature_action_count"] == 1
        )
    elif rig is not None:
        stray_embedded_actions = list(bpy.data.actions)
        if stray_embedded_actions:
            if not resolved_animations:
                raise RuntimeError(
                    "W3D model import contains unexpected embedded animation actions"
                )
            # RotWK 2.01 models embed a redundant one-channel pose clip
            # beside the externally authored state clips (kbangwgn_a.w3d
            # embeds KBANGWGN_ASKL.KBANGWGN_A while retail binds the _ABLD
            # buildup clip). The attached external clips are the authored
            # presentation this job declares; the embedded pose actions are
            # removed here with explicit report evidence — never silently.
            discarded_embedded_model_action_count = len(stray_embedded_actions)
            for stray_action in stray_embedded_actions:
                bpy.data.actions.remove(stray_action)
            if list(bpy.data.actions):
                raise RuntimeError(
                    "embedded W3D pose actions were not fully discarded"
                )
        detach_actions(rig)
    for source in resolved_animations:
        if embedded_model_animation:
            continue
        if not source.is_file():
            raise FileNotFoundError(source)
        before = set(bpy.data.actions)
        with _model_import_phase_scope(phase_checkpoint) as animation_phase_scope:
            result = animation_output_ledger.capture(
                lambda: _invoke_w3d_import(
                    source,
                    import_phase_scope=animation_phase_scope,
                ),
                operation_phase="animation-import",
                phase_checkpoint=phase_checkpoint,
            )
        animation_phase_scope.raise_if_failed()
        if result != {"FINISHED"}:
            raise RuntimeError(f"animation import failed for {source.name}: {result}")
        after = set(bpy.data.actions)
        created = sorted(after - before, key=lambda item: item.name.casefold())
        if not created:
            active_actions = []
            for candidate in _scene_armature_objects():
                for active in _owned_active_actions(candidate):
                    if active not in before:
                        active_actions.append(active)
            created = active_actions
        # Cross-hierarchy clips key the auxiliary armature their own hierarchy
        # creates; same-hierarchy clips key the model rig. Capture and detach
        # on the true owner so the next clip can never reuse and merge into
        # this clip's still-assigned action.
        owner_rig = find_animation_owner_rig(rig, created)
        captured, action_shape = capture_w3d_animation_actions(
            owner_rig, created, source.stem
        )
        action_shape["owner_rig"] = owner_rig
        imported_actions.extend(captured)
        animation_action_shapes.append(action_shape)
        logical_animation_count += 1
        split_action_animation_count += int(
            action_shape["public"]["object_action_count"] == 1
            and action_shape["public"]["armature_action_count"] == 1
        )
        detach_actions(owner_rig)

    phase_checkpoint.set("scene-validation")
    phase_checkpoint.set("post-animation-validation")
    if logical_animation_count != len(args.animations):
        raise RuntimeError("requested and imported animation counts differ")
    phase_checkpoint.set("action-validation")
    assert_non_animated_scene_has_no_actions(args.asset_kind)

    phase_checkpoint.set("animation-sidecar-mesh-strip")
    mesh_objects = [item for item in bpy.data.objects if item.type == "MESH"]
    # Only strip when we have a pre-animation proof (skeletal animated jobs).
    # An empty proof would incorrectly treat every mesh as a sidecar.
    if rig is not None and render_attachment_proof:
        animation_sidecar_meshes = strip_animation_sidecar_meshes(
            render_attachment_proof,
            mesh_objects,
        )
    else:
        animation_sidecar_meshes = {
            "count": 0,
            "reason": "not-in-pre-animation-render-inventory",
        }
    phase_checkpoint.set("attachment-restoration")
    mesh_objects = [item for item in bpy.data.objects if item.type == "MESH"]
    if rig is not None:
        restore_render_attachments(
            render_attachment_proof, mesh_objects, list(bpy.data.objects)
        )
    phase_checkpoint.set("render-revalidation")
    assert_render_geometry_unchanged(render_geometry_proof, mesh_objects)
    if rig is not None:
        revalidate_restored_inventory(
            mesh_objects,
            args.required_equipment,
            rig,
            mesh_inventory,
            equipment,
        )
    attachments_canonicalized_restored_and_revalidated = rig is not None
    mesh_count = model_mesh_count
    vertices = sum(item["vertices"] for item in mesh_inventory)
    triangles = sum(item["triangles"] for item in mesh_inventory)
    skinned_meshes = sum(1 for item in mesh_inventory if item["skinned"])
    phase_checkpoint.set("material-validation")
    phase_checkpoint.set("generated-image-validation")
    generated_images = sorted(
        image.name for image in bpy.data.images if image.source == "GENERATED"
    )
    if generated_images:
        raise RuntimeError(
            f"generated placeholder textures remain: {len(generated_images)} image(s)"
        )
    phase_checkpoint.set("shader-material-validation")
    shader_material_compatibility = collect_shader_material_compatibility(
        list(bpy.data.materials)
    )
    phase_checkpoint.set("animation-export-preparation")
    animation_curve_count = sum(len(action.fcurves) for action in imported_actions)
    animation_key_count = sum(
        len(curve.keyframe_points)
        for action in imported_actions
        for curve in action.fcurves
    )
    nla_track_count = (
        prepare_w3d_animation_nla_tracks(rig, animation_action_shapes)
        if animation_action_shapes
        else 0
    )

    phase_checkpoint.set("export")
    output.parent.mkdir(parents=True, exist_ok=True)
    export_result = bpy.ops.export_scene.gltf(
        filepath=str(output),
        export_format="GLB",
        export_animations=args.asset_kind == "animated",
        export_animation_mode="NLA_TRACKS" if animation_action_shapes else "ACTIONS",
        export_optimize_animation_size=False,
        export_optimize_animation_keep_anim_object=True,
        export_skins=rig is not None,
        export_morph=True,
        export_yup=True,
        export_apply=False,
        export_extras=True,
        export_cameras=False,
        export_lights=False,
    )
    if export_result != {"FINISHED"}:
        raise RuntimeError(f"glTF export failed: {export_result}")
    phase_checkpoint.set("glb-validation")
    if animation_action_shapes:
        restored = restore_duplicate_logical_animations(output, animation_action_shapes)
        duplicated_logical_animation_count = restored["duplicated_animations"]
        preserved_visibility_channel_count = restored["visibility_channels"]
        preserved_visibility_key_count = restored["visibility_keys"]
        visibility_only_sidecar_animation_count = restored["visibility_only_animations"]
        action_shape_export = validate_split_animation_glb(
            output,
            [
                shape["public"]["name"]
                for shape in animation_action_shapes
                if shape["public"]["transform_curve_count"] > 0
            ],
            require_skins=skinned_meshes > 0,
            require_skeletal_mesh=(
                skinned_meshes > 0
                or (
                    rig is not None
                    and any(
                        getattr(item, "parent", None) is rig for item in mesh_objects
                    )
                )
            ),
        )
        if (
            action_shape_export["visibility_channels"]
            != preserved_visibility_channel_count
            or action_shape_export["visibility_keys"] != preserved_visibility_key_count
            or action_shape_export["visibility_only_animations"]
            != visibility_only_sidecar_animation_count
        ):
            raise RuntimeError("W3D visibility extras were not preserved exactly")
    if split_action_animation_count:
        split_export = action_shape_export
    if embedded_model_animation:
        embedded_export = action_shape_export
    phase_checkpoint.set("report-validation")
    return {
        "report_schema": ADAPTER_REPORT_SCHEMA,
        "report_version": ADAPTER_REPORT_VERSION,
        "asset_kind": args.asset_kind,
        "meshes": mesh_count,
        "mesh_inventory": mesh_inventory,
        "required_equipment": sorted(set(args.required_equipment)),
        "equipment": equipment,
        "retail_absent_textures_cleared": retail_absent_textures_cleared,
        "retail_absent_textures_unreferenced": retail_absent_textures_unreferenced,
        "root_rigid_inert_vertex_groups_removed": list(
            _ACTIVE_ROOT_RIGID_INERT_VERTEX_GROUPS
        ),
        "shader_texture_sentinels_dropped": sorted(
            _ACTIVE_SHADER_TEXTURE_SENTINEL_DROPS,
            key=lambda record: (record["material"], record["property"]),
        ),
        "animations": logical_animation_count,
        "animation_curves": animation_curve_count,
        "animation_keys": animation_key_count,
        "animation_action_shapes": [
            shape["public"] for shape in animation_action_shapes
        ],
        "action_shape_animation_count": len(animation_action_shapes),
        "action_shape_action_count": sum(
            shape["public"]["action_count"] for shape in animation_action_shapes
        ),
        "action_shape_nla_track_count": nla_track_count,
        "action_shape_exported_animation_count": action_shape_export["animations"],
        "action_shape_exported_channel_count": action_shape_export["channels"],
        "action_shape_exported_sampler_count": action_shape_export["samplers"],
        "action_shape_exported_skin_count": action_shape_export["skins"],
        "action_shape_exported_skeletal_mesh_count": action_shape_export[
            "skeletal_meshes"
        ],
        "duplicated_logical_animation_count": duplicated_logical_animation_count,
        "preserved_visibility_channel_count": preserved_visibility_channel_count,
        "preserved_visibility_key_count": preserved_visibility_key_count,
        "visibility_only_sidecar_animation_count": visibility_only_sidecar_animation_count,
        "embedded_model_animation": embedded_model_animation,
        "embedded_model_action_count": (
            len(imported_actions) if embedded_model_animation else 0
        ),
        "discarded_embedded_model_action_count": (
            discarded_embedded_model_action_count
        ),
        "embedded_exported_animation_count": embedded_export["animations"],
        "embedded_exported_channel_count": embedded_export["channels"],
        "embedded_exported_sampler_count": embedded_export["samplers"],
        "embedded_exported_skin_count": embedded_export["skins"],
        "embedded_exported_skeletal_mesh_count": embedded_export["skeletal_meshes"],
        "split_action_animation_count": split_action_animation_count,
        "split_action_count": sum(
            shape["public"]["action_count"]
            for shape in animation_action_shapes
            if shape["public"]["object_action_count"] == 1
            and shape["public"]["armature_action_count"] == 1
        ),
        "split_exported_animation_count": split_export["animations"],
        "split_exported_channel_count": split_export["channels"],
        "split_exported_sampler_count": split_export["samplers"],
        "split_exported_skin_count": split_export["skins"],
        "split_exported_skeletal_mesh_count": split_export["skeletal_meshes"],
        "bones": len(rig.data.bones) if rig is not None else 0,
        "skeletons": int(rig is not None),
        "vertices": vertices,
        "triangles": triangles,
        "skinned_meshes": skinned_meshes,
        "materials": len(bpy.data.materials),
        "images": len(bpy.data.images),
        "generated_images": len(generated_images),
        "shader_material_compatibility": shader_material_compatibility,
        "opaque_material_normalization": opaque_material_normalization,
        "root_rigid_bake": root_rigid_bake,
        "filtered_non_render_geometry": filtered_geometry,
        "animation_sidecar_meshes_removed": animation_sidecar_meshes,
        "excluded_optional_meshes": optional_mesh_exclusions,
        "remaining_non_render_geometry": 0,
        "remaining_ambiguous_box_geometry": 0,
        "equipment_attachments_canonicalized_restored_and_revalidated": attachments_canonicalized_restored_and_revalidated,
        "fps": bpy.context.scene.render.fps,
    }


def convert_w3d_job(
    *,
    model: Path,
    asset_kind: str,
    animations: list[Path],
    required_equipment: list[str],
    excluded_optional_meshes: list[str],
    proven_root_rigid_bake: bool,
    output: Path,
    proven_pivot_only_model: bool = False,
    retail_absent_textures: list[str] | None = None,
) -> dict[str, Any]:
    """Convert one job while retaining raw animation-import output on failure."""

    phase_checkpoint = _W3DConversionPhaseCheckpoint()
    animation_output_ledger: AnimationImportOutputLedger | None = None
    failure_evidence: tuple[str, str] | None = None
    try:
        animation_output_ledger = AnimationImportOutputLedger()
        report = _convert_w3d_job_impl(
            model=model,
            asset_kind=asset_kind,
            animations=animations,
            required_equipment=required_equipment,
            excluded_optional_meshes=excluded_optional_meshes,
            proven_root_rigid_bake=proven_root_rigid_bake,
            proven_pivot_only_model=proven_pivot_only_model,
            retail_absent_textures=retail_absent_textures,
            output=output,
            animation_output_ledger=animation_output_ledger,
            phase_checkpoint=phase_checkpoint,
        )
        suppressed = animation_output_ledger.replay_success()
    except BaseException as error:
        failure_evidence = (
            phase_checkpoint.failure_phase,
            _w3d_conversion_failure_kind(error),
        )
        if animation_output_ledger is not None:
            try:
                animation_output_ledger.replay_failure()
            except BaseException:
                pass
    if failure_evidence is not None:
        raise W3DConversionPhaseError(*failure_evidence) from None
    report["suppressed_redundant_keyframe_warning_count"] = suppressed
    return report


def main() -> None:
    args = parse_args()
    # Keep the original single-job error order: a missing model is reported
    # before the pinned plugin tree is initialized.
    if not args.model.expanduser().resolve().is_file():
        raise FileNotFoundError(args.model.expanduser().resolve())
    initialize_w3d_converter(args.plugin_root)
    try:
        report = convert_w3d_job(
            model=args.model,
            asset_kind=args.asset_kind,
            animations=args.animations,
            required_equipment=args.required_equipment,
            excluded_optional_meshes=args.excluded_optional_meshes,
            proven_root_rigid_bake=args.proven_root_rigid_bake,
            proven_pivot_only_model=args.proven_pivot_only_model,
            retail_absent_textures=args.retail_absent_textures,
            output=args.output,
        )
    except W3DConversionPhaseError as error:
        # The sanitized message is fixed, so a host that only sees the
        # traceback cannot tell one failure from another. Emit the bounded
        # evidence the multi-job driver already reports, then re-raise so the
        # process still exits non-zero.
        print(
            "OPENBFME_W3D_FAIL "
            + json.dumps(
                {
                    "failure_phase": error.failure_phase,
                    "failure_kind": error.failure_kind,
                },
                sort_keys=True,
            ),
            file=sys.stderr,
            flush=True,
        )
        raise
    print("OPENBFME_W3D_OK " + json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
