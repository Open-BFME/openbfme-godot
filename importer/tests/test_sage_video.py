"""Video lane contract: bounded discovery, pinned-ffmpeg conversion, fail-closed report."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from tests.retail_inputs import rotwk_install

from openbfme_importer.sage_video import (
    SageVideoError,
    VideoSource,
    convert_videos,
    discover_videos,
)
from openbfme_importer.tools import discover_executable

ROOT = Path(__file__).resolve().parents[2]
ROTWK_INSTALL = rotwk_install()


def _fake_install(tmp_path: Path) -> Path:
    movies = tmp_path / "data" / "movies"
    movies.mkdir(parents=True)
    (movies / "intro_b.VP6").write_bytes(b"vp6-b")
    (movies / "intro_a.vp6").write_bytes(b"vp6-a")
    (movies / "credits.bik").write_bytes(b"bik-c")
    (movies / "readme.txt").write_bytes(b"not a movie")
    return tmp_path


def test_discovery_is_sorted_typed_and_hashed(tmp_path: Path) -> None:
    sources = discover_videos(_fake_install(tmp_path))
    assert [source.relative_path for source in sources] == [
        "data/movies/credits.bik",
        "data/movies/intro_a.vp6",
        "data/movies/intro_b.VP6",
    ]
    assert all(len(source.sha256) == 64 for source in sources)
    assert all(source.size > 0 for source in sources)


def test_discovery_ignores_missing_movies_directory(tmp_path: Path) -> None:
    assert discover_videos(tmp_path) == ()


def test_discovery_fails_closed_on_bounds(tmp_path: Path) -> None:
    install = _fake_install(tmp_path)
    with pytest.raises(SageVideoError, match="limit is 2"):
        discover_videos(install, max_files=2)
    with pytest.raises(SageVideoError, match="bytes"):
        discover_videos(install, max_total_bytes=3)


def test_output_name_collisions_fail_closed(tmp_path: Path) -> None:
    movies = tmp_path / "data" / "movies"
    (movies / "x").mkdir(parents=True)
    (movies / "x_y.vp6").write_bytes(b"flat")
    (movies / "x" / "y.vp6").write_bytes(b"nested")
    ffmpeg = discover_executable("ffmpeg", "OPENBFME_FFMPEG")
    if ffmpeg is None:
        pytest.skip("pinned ffmpeg is not present")
    with pytest.raises(SageVideoError, match="collision"):
        convert_videos(tmp_path, tmp_path / "out", ffmpeg)


def test_conversion_records_and_raises_on_garbage_input(tmp_path: Path) -> None:
    ffmpeg = discover_executable("ffmpeg", "OPENBFME_FFMPEG")
    if ffmpeg is None:
        pytest.skip("pinned ffmpeg is not present")
    install = _fake_install(tmp_path)
    output_root = tmp_path / "out"
    with pytest.raises(SageVideoError, match="failed for 1 of 1"):
        convert_videos(install, output_root, ffmpeg, only="intro_a", limit=1)
    report = json.loads((output_root / "videos-report.json").read_text(encoding="utf-8"))
    assert report["failed"] == 1
    (row,) = report["rows"]
    assert row["status"] == "failed"
    assert row["source"] == "data/movies/intro_a.vp6"
    assert not (output_root / "intro_a.ogv").exists()


def test_conversion_of_one_retail_movie_produces_valid_ogg() -> None:
    ffmpeg = discover_executable("ffmpeg", "OPENBFME_FFMPEG")
    if ffmpeg is None:
        pytest.skip("pinned ffmpeg is not present")
    if not (ROTWK_INSTALL / "data" / "movies").is_dir():
        pytest.skip("RotWK retail install is not present")
    sources = discover_videos(ROTWK_INSTALL)
    assert sources, "retail install unexpectedly has no movies"
    smallest = min(sources, key=lambda source: source.size)
    output_root = (
        ROOT / "workspace" / "retail-work" / "scratch" / "video-lane-test"
    )
    report = convert_videos(
        ROTWK_INSTALL,
        output_root,
        ffmpeg,
        only=smallest.relative_path,
        limit=1,
    )
    assert report["converted"] == 1 and report["failed"] == 0
    (row,) = report["rows"]
    output_path = output_root / row["output"]
    assert output_path.stat().st_size > 0
    with output_path.open("rb") as handle:
        assert handle.read(4) == b"OggS"
    assert row["outputSha256"] != row["sourceSha256"]
