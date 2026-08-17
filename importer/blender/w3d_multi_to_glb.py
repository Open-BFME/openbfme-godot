"""Run multiple W3D→GLB jobs in one Blender process.

Host stages inputs and writes a JSON job list. This driver initializes the
plugin once, converts each job via convert_w3d_job, and emits one marker per
job so the pipeline can keep its existing validation path.

Each job runs with the process output descriptors redirected into per-job
files, and the captured text rides the success marker as ``output_log``. The
pipeline's warning-text guards (``not supported`` / ``texture not found``)
must evaluate the same real content the single-job process log carries —
a GLB with unresolved textures must fail its job and never reach the cache.
The converter's animation-import ledger re-dup2s the process descriptors and
replays its compacted capture into them, so the per-job files receive the
replayed import output exactly as the single-job log would.
"""

from __future__ import annotations

import argparse
import contextlib
import json
import os
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterator


# Multi-job markers must stay single-line JSON, so the per-job capture is
# bounded. Four attested retail jobs legitimately emit about 1.62 MiB of
# Blender mesh-validation output; 2 MiB admits those complete logs while
# retaining a narrow fail-closed ceiling. Never truncate: a guard line can
# occur at the end of the output.
MAX_JOB_OUTPUT_CAPTURE_BYTES = 2 * 1024 * 1024


def _failure_evidence(module: Any, error: BaseException) -> tuple[str | None, str | None]:
    """Preserve the converter's sanitized phase classification when present.

    The converter only ever raises ``W3DConversionPhaseError`` with bounded
    frozenset values; anything else is reported as type/message alone so no
    private payload crosses the process boundary.
    """

    phase_error_type = getattr(module, "W3DConversionPhaseError", None)
    if phase_error_type is None or type(error) is not phase_error_type:
        return None, None
    phase = getattr(error, "failure_phase", None)
    kind = getattr(error, "failure_kind", None)
    phases = getattr(module, "_W3D_CONVERSION_FAILURE_PHASES", frozenset())
    kinds = getattr(module, "_W3D_CONVERSION_FAILURE_KINDS", frozenset())
    if (
        type(phase) is str
        and phase in phases
        and type(kind) is str
        and kind in kinds
    ):
        return phase, kind
    return None, None


def _flush_process_streams() -> None:
    """Flush Blender's Python streams before changing process descriptors."""

    for stream in (sys.stdout, sys.stderr):
        try:
            stream.flush()
        except (AttributeError, OSError, ValueError):
            pass


@contextlib.contextmanager
def _captured_job_output(
    target_fds: tuple[int, ...] = (1, 2),
) -> Iterator[list[Path]]:
    """Redirect the process output descriptors into per-job temp files.

    Mirrors the converter ledger's descriptor capture: anything the job (or
    the ledger's success replay) writes to stdout/stderr lands in the files
    instead of the shared batch output, where per-job attribution would be
    impossible. The caller reads and unlinks the files after the block.
    """

    _flush_process_streams()
    saved_fds = [os.dup(target_fd) for target_fd in target_fds]
    capture_fds: list[int] = []
    capture_paths: list[Path] = []
    redirected = False
    try:
        try:
            for index in range(len(target_fds)):
                capture_fd, raw_path = tempfile.mkstemp(
                    prefix=f"openbfme-w3d-multi-{index}-", suffix=".log"
                )
                capture_fds.append(capture_fd)
                capture_paths.append(Path(raw_path))
            for target_fd, capture_fd in zip(target_fds, capture_fds):
                os.dup2(capture_fd, target_fd)
            redirected = True
        except BaseException:
            for path in capture_paths:
                path.unlink(missing_ok=True)
            raise
        yield capture_paths
    finally:
        if redirected:
            _flush_process_streams()
            for target_fd, saved_fd in zip(target_fds, saved_fds):
                os.dup2(saved_fd, target_fd)
        for file_descriptor in (*saved_fds, *capture_fds):
            try:
                os.close(file_descriptor)
            except OSError:
                pass


def _read_bounded_job_output(capture_paths: list[Path]) -> str:
    """Return the per-job output, laid out like the single-job combined log.

    Fails closed when the capture exceeds the bound: truncating could cut
    exactly the line the pipeline's warning-text guards must see.
    """

    chunks: list[bytes] = []
    byte_counts: list[int] = []
    for path in capture_paths:
        data = path.read_bytes()
        byte_counts.append(len(data))
        chunks.append(data)
    combined = b"\n".join(chunks)
    total = len(combined)
    if total > MAX_JOB_OUTPUT_CAPTURE_BYTES:
        stdout_bytes = byte_counts[0] if byte_counts else 0
        stderr_bytes = byte_counts[1] if len(byte_counts) > 1 else 0
        raise RuntimeError(
            "W3D multi-job conversion output exceeded the bounded per-job capture "
            f"(total_bytes={total}, stdout_bytes={stdout_bytes}, "
            f"stderr_bytes={stderr_bytes}, "
            f"limit_bytes={MAX_JOB_OUTPUT_CAPTURE_BYTES})"
        )
    return combined.decode("utf-8", errors="replace")


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Multi-job W3D to GLB (one process)")
    parser.add_argument("--plugin-root", type=Path, required=True)
    parser.add_argument("--jobs", type=Path, required=True, help="JSON job list")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None, *, converter_module: Any = None) -> int:
    # Blender injects its own argv before "--".
    if argv is None:
        argv = sys.argv[1:]
        if "--" in argv:
            argv = argv[argv.index("--") + 1 :]
    args = _parse_args(argv)
    jobs_path = args.jobs.expanduser().resolve()
    plugin_root = args.plugin_root.expanduser().resolve()
    document = json.loads(jobs_path.read_text(encoding="utf-8"))
    if not isinstance(document, dict) or document.get("schema") != "openbfme.w3d-multi-jobs":
        raise SystemExit("invalid multi-job document schema")
    rows = document.get("jobs")
    if not isinstance(rows, list) or not rows:
        raise SystemExit("multi-job document has no jobs")

    if converter_module is None:
        # Import sibling converter after Blender has set up bpy.
        converter_path = Path(__file__).with_name("w3d_to_glb.py")
        import importlib.util

        spec = importlib.util.spec_from_file_location(
            "_openbfme_w3d_to_glb_multi", converter_path
        )
        if spec is None or spec.loader is None:
            raise SystemExit("could not load w3d_to_glb.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    else:
        module = converter_module
    module.initialize_w3d_converter(plugin_root)

    for row in rows:
        job_id = str(row["job_id"])
        capture_paths: list[Path] = []
        try:
            with _captured_job_output() as capture_paths:
                report = module.convert_w3d_job(
                    model=Path(row["model"]),
                    asset_kind=str(row["asset_kind"]),
                    animations=[Path(item) for item in row.get("animations", [])],
                    required_equipment=list(row.get("required_equipment", [])),
                    excluded_optional_meshes=list(row.get("excluded_optional_meshes", [])),
                    proven_root_rigid_bake=bool(row.get("proven_root_rigid_bake", False)),
                    proven_pivot_only_model=bool(row.get("proven_pivot_only_model", False)),
                    retail_absent_textures=list(row.get("retail_absent_textures", [])),
                    output=Path(row["output"]),
                )
            output_log = _read_bounded_job_output(capture_paths)
            payload: dict[str, Any] = {
                "job_id": job_id,
                "report": report,
                "output_log": output_log,
            }
            print("OPENBFME_W3D_JOB_OK " + json.dumps(payload, sort_keys=True), flush=True)
        except BaseException as exc:
            failure_phase, failure_kind = _failure_evidence(module, exc)
            payload = {
                "job_id": job_id,
                "error_type": type(exc).__name__,
                "error": str(exc)[:500],
            }
            if failure_phase is not None and failure_kind is not None:
                payload["failure_phase"] = failure_phase
                payload["failure_kind"] = failure_kind
            print(
                "OPENBFME_W3D_JOB_FAIL " + json.dumps(payload, sort_keys=True),
                flush=True,
            )
        finally:
            for capture_path in capture_paths:
                capture_path.unlink(missing_ok=True)
    print("OPENBFME_W3D_MULTI_DONE " + json.dumps({"jobs": len(rows)}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
