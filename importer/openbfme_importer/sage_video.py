"""Loose-file VP6/Bink movie discovery and pinned-FFmpeg conversion.

Retail SAGE cinematics are NOT packed into .big archives: they live as loose
files under ``<install>/data/movies`` (measured 2026-07-22: RotWK 2.01 ships
76 ``.vp6`` files / ~1.6 GB, BFME2 1.06 ships 56 / ~1.0 GB, zero ``.bik``).
This lane converts them to Ogg Theora/Vorbis, which Godot plays natively.

Fail-closed rules match the rest of the importer: discovery is bounded, every
source and output is content-hashed into the report, a conversion that yields
anything but a well-formed Ogg stream is a recorded failure, and any failure
makes the lane raise after accounting for every file.
"""

from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from .big import sha256_file

VIDEO_REPORT_SCHEMA = "openbfme.video-conversion-report"
VIDEO_REPORT_SCHEMA_VERSION = 0

MOVIES_SUBDIRECTORY = PurePosixPath("data/movies")
VIDEO_SUFFIXES = (".vp6", ".bik")
MAX_VIDEO_FILES = 512
MAX_TOTAL_VIDEO_BYTES = 8 * 1024 * 1024 * 1024
OGG_MAGIC = b"OggS"
_CONVERT_TIMEOUT_SECONDS = 15 * 60


class SageVideoError(ValueError):
    """Raised when video discovery or conversion violates the lane contract."""


@dataclass(frozen=True, slots=True)
class VideoSource:
    """One discovered loose movie file, identified by content."""

    relative_path: str
    size: int
    sha256: str


def discover_videos(
    install_root: Path,
    *,
    max_files: int = MAX_VIDEO_FILES,
    max_total_bytes: int = MAX_TOTAL_VIDEO_BYTES,
) -> tuple[VideoSource, ...]:
    """Enumerate loose movie files under ``data/movies``, deterministically.

    Returns sources sorted by casefolded relative path. Unknown extensions are
    ignored (they are not silently converted); bounds violations raise.
    """

    movies_root = install_root.joinpath(*MOVIES_SUBDIRECTORY.parts)
    if not movies_root.is_dir():
        return ()
    candidates: list[Path] = [
        path
        for path in movies_root.rglob("*")
        if path.is_file() and path.suffix.casefold() in VIDEO_SUFFIXES
    ]
    if len(candidates) > max_files:
        raise SageVideoError(
            f"video discovery found {len(candidates)} files; limit is {max_files}"
        )
    total_bytes = sum(path.stat().st_size for path in candidates)
    if total_bytes > max_total_bytes:
        raise SageVideoError(
            f"video discovery found {total_bytes} bytes; limit is {max_total_bytes}"
        )
    sources = [
        VideoSource(
            relative_path=path.relative_to(install_root).as_posix(),
            size=path.stat().st_size,
            sha256=sha256_file(path),
        )
        for path in candidates
    ]
    sources.sort(key=lambda source: (source.relative_path.casefold(), source.relative_path))
    return tuple(sources)


def convert_videos(
    install_root: Path,
    output_root: Path,
    ffmpeg: Path,
    *,
    only: str | None = None,
    limit: int | None = None,
    sources: tuple[VideoSource, ...] | None = None,
) -> dict:
    """Convert discovered movies to Ogg Theora/Vorbis under ``output_root``.

    Every attempted file gets a report row. Any failed row makes this function
    raise ``SageVideoError`` AFTER the full accounting pass, with the report
    (including failures) already written to ``output_root / videos-report.json``.
    """

    if not ffmpeg.is_file():
        raise SageVideoError(f"ffmpeg executable is missing: {ffmpeg}")
    selected = list(sources if sources is not None else discover_videos(install_root))
    if only is not None:
        needle = only.casefold()
        selected = [source for source in selected if needle in source.relative_path.casefold()]
    if limit is not None:
        if limit < 1:
            raise SageVideoError("limit must be a positive integer")
        selected = selected[:limit]

    # Output names flatten the relative path so nested movies with equal stems
    # can never silently overwrite each other; ambiguity is still checked.
    output_names: dict[str, str] = {}
    for source in selected:
        relative = PurePosixPath(source.relative_path)
        flattened = "_".join(part.casefold() for part in relative.parts[2:]) or relative.stem.casefold()
        name = str(PurePosixPath(flattened).with_suffix(".ogv"))
        previous = output_names.get(name)
        if previous is not None:
            raise SageVideoError(
                f"output name collision: {previous} and {source.relative_path} both map to {name}"
            )
        output_names[name] = source.relative_path

    output_root.mkdir(parents=True, exist_ok=True)
    name_by_source = {value: key for key, value in output_names.items()}
    rows: list[dict] = []
    failures = 0
    for source in selected:
        source_path = install_root / Path(*PurePosixPath(source.relative_path).parts)
        output_path = output_root / name_by_source[source.relative_path]
        row: dict = {
            "source": source.relative_path,
            "sourceSha256": source.sha256,
            "sourceBytes": source.size,
            "output": output_path.name,
        }
        exit_code: int | None
        stderr_tail = ""
        try:
            completed = subprocess.run(
                [
                    str(ffmpeg),
                    "-hide_banner",
                    "-nostdin",
                    "-y",
                    "-i",
                    str(source_path),
                    "-c:v",
                    "libtheora",
                    "-q:v",
                    "7",
                    "-c:a",
                    "libvorbis",
                    "-q:a",
                    "4",
                    str(output_path),
                ],
                capture_output=True,
                timeout=_CONVERT_TIMEOUT_SECONDS,
                check=False,
            )
            exit_code = completed.returncode
            stderr_tail = completed.stderr.decode("utf-8", errors="replace")[-500:]
        except subprocess.TimeoutExpired as exc:
            # A hung encode is a recorded failure, never an unaccounted crash.
            exit_code = None
            stderr_tail = f"timeout after {exc.timeout} seconds"
        ogg_ok = False
        if exit_code == 0 and output_path.is_file() and output_path.stat().st_size > 0:
            with output_path.open("rb") as handle:
                ogg_ok = handle.read(4) == OGG_MAGIC
        if exit_code == 0 and ogg_ok:
            row["status"] = "converted"
            row["outputSha256"] = sha256_file(output_path)
            row["outputBytes"] = output_path.stat().st_size
        else:
            failures += 1
            row["status"] = "failed"
            row["ffmpegExit"] = exit_code
            row["stderrTail"] = stderr_tail
            if output_path.is_file():
                output_path.unlink()
        rows.append(row)

    report = {
        "schema": VIDEO_REPORT_SCHEMA,
        "schemaVersion": VIDEO_REPORT_SCHEMA_VERSION,
        "install": str(install_root),
        "discovered": len(selected),
        "converted": len(rows) - failures,
        "failed": failures,
        "rows": rows,
    }
    report_path = output_root / "videos-report.json"
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    if failures:
        raise SageVideoError(
            f"video conversion failed for {failures} of {len(rows)} files; see {report_path}"
        )
    return report
