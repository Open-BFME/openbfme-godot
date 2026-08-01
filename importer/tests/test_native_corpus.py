from __future__ import annotations

from contextlib import redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from unittest import mock

from PIL import Image

import openbfme_importer.native_corpus as native_corpus_module
from openbfme_importer.native_corpus import (
    FFMPEG_WAV_ARGUMENT_TEMPLATE,
    NATIVE_CORPUS_MANIFEST,
    NativeCorpusBuildError,
    NativeCorpusDependencyError,
    NativeCorpusError,
    NativeCorpusLimitError,
    NativeCorpusReuseError,
    build_native_corpus,
)


def _aggregate(files: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(str(item["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(item["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _manifest(
    files: dict[str, tuple[bytes, str]],
    *,
    declared_hashes: dict[str, str] | None = None,
) -> dict[str, object]:
    inventory: list[dict[str, object]] = []
    offset = 32
    for precedence, (path, source) in enumerate(
        sorted(files.items(), key=lambda item: item[0].casefold())
    ):
        payload, archive = source
        inventory.append(
            {
                "archive": archive,
                "offset": offset,
                "path": path,
                "precedence": precedence,
                "sha256": (
                    declared_hashes[path]
                    if declared_hashes and path in declared_hashes
                    else hashlib.sha256(payload).hexdigest()
                ),
                "size": len(payload),
            }
        )
        offset += len(payload)
    return {
        "schema": "openbfme.effective-assets-manifest",
        "schema_version": 0,
        "catalog": {
            "archive_count": len({archive for _, archive in files.values()}),
            "entry_count": len(inventory),
            "format": 4,
            "identity_sha256": "a" * 64,
        },
        "install": {
            "identity_sha256": "b" * 64,
            "root": "synthetic-install",
        },
        "totals": {
            "bytes": sum(len(payload) for payload, _ in files.values()),
            "files": len(files),
        },
        "aggregate_sha256": _aggregate(inventory),
        "files": inventory,
    }


def _write_manifest(root: Path, manifest: dict[str, object]) -> None:
    metadata = root / ".openbfme"
    metadata.mkdir(parents=True, exist_ok=True)
    (metadata / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _write_effective_assets(
    root: Path,
    files: dict[str, tuple[bytes, str]],
    *,
    declared_hashes: dict[str, str] | None = None,
) -> dict[str, object]:
    for relative, (payload, _) in files.items():
        target = root.joinpath(*relative.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    manifest = _manifest(files, declared_hashes=declared_hashes)
    _write_manifest(root, manifest)
    return manifest


def _image_payload(format_name: str, color: tuple[int, int, int, int]) -> bytes:
    output = io.BytesIO()
    with Image.new("RGBA", (3, 2), color) as image:
        save_image = image.convert("RGB") if format_name == "JPEG" else image
        save_image.save(output, format=format_name)
        if save_image is not image:
            save_image.close()
    return output.getvalue()


def _wav_payload(*, samples: bytes = b"\x00\x00\x01\x00") -> bytes:
    channels = 1
    sample_rate = 8_000
    bits = 16
    block_align = channels * bits // 8
    byte_rate = sample_rate * block_align
    fmt = struct.pack("<HHIIHH", 1, channels, sample_rate, byte_rate, block_align, bits)
    body = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    body += b"data" + struct.pack("<I", len(samples)) + samples
    return b"RIFF" + struct.pack("<I", len(body) + 4) + b"WAVE" + body


def _compressed_wav_payload() -> bytes:
    """Small synthetic format-tag-2 WAVE routed through the mocked decoder."""

    channels = 1
    sample_rate = 8_000
    block_align = 256
    bits = 4
    fmt = struct.pack(
        "<HHIIHHH",
        2,  # Microsoft ADPCM
        channels,
        sample_rate,
        sample_rate * block_align,
        block_align,
        bits,
        0,
    )
    samples = b"\0" * block_align
    body = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    body += b"data" + struct.pack("<I", len(samples)) + samples
    return b"RIFF" + struct.pack("<I", len(body) + 4) + b"WAVE" + body


def _mp3_payload() -> bytes:
    # MPEG-1 Layer III, 128 kbps, 44.1 kHz, stereo; two complete 417-byte frames.
    header = bytes.fromhex("fffb9000")
    frame = header + b"\0" * (417 - len(header))
    return frame + frame


def _ear_refpack_payload(body: bytes) -> bytes:
    """Encode a bounded literal-only EAR/RefPack fixture."""

    if not body or len(body) >= 1 << 24:
        raise ValueError("synthetic RefPack body must fit the three-byte size form")
    stream = bytearray(b"\x10\xfb")
    stream.extend(len(body).to_bytes(3, "big"))
    position = 0
    while len(body) - position >= 4:
        literal_count = min(112, ((len(body) - position) // 4) * 4)
        stream.append(0xE0 + literal_count // 4 - 1)
        stream.extend(body[position : position + literal_count])
        position += literal_count
    tail = body[position:]
    stream.append(0xFC + len(tail))
    stream.extend(tail)
    return b"EAR\0" + struct.pack("<I", len(body)) + bytes(stream)


def _tree_snapshot(root: Path) -> dict[str, str]:
    return {
        path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(item for item in root.rglob("*") if item.is_file())
    }


class NativeCorpusTests(unittest.TestCase):
    def setUp(self) -> None:
        self._ffmpeg_temp = tempfile.TemporaryDirectory()
        self.addCleanup(self._ffmpeg_temp.cleanup)
        self.ffmpeg = Path(self._ffmpeg_temp.name) / "ffmpeg.exe"
        self.ffmpeg.write_bytes(b"synthetic pinned ffmpeg test executable")
        self.ffmpeg_sha256 = hashlib.sha256(self.ffmpeg.read_bytes()).hexdigest()
        self.ffmpeg_commands: list[list[str]] = []
        self.ffmpeg_fail_all = False

        def fake_run(command: object, **_: object) -> subprocess.CompletedProcess:
            rendered = [str(item) for item in command]  # type: ignore[arg-type]
            self.ffmpeg_commands.append(rendered)
            if rendered[1:] == ["-version"]:
                return subprocess.CompletedProcess(
                    rendered,
                    0,
                    stdout="ffmpeg version 8.1.1-synthetic-test\n",
                    stderr="",
                )
            source = Path(rendered[rendered.index("-i") + 1])
            payload = source.read_bytes()
            if self.ffmpeg_fail_all or payload in {b"not a wave", b"bad wave"}:
                return subprocess.CompletedProcess(
                    rendered,
                    23,
                    stdout=b"",
                    stderr=b"C:\\private\\retail\\secret-source.wav",
                )
            Path(rendered[-1]).write_bytes(
                _wav_payload(samples=b"\x00\x00\x01\x00\x02\x00")
            )
            return subprocess.CompletedProcess(rendered, 0, stdout=b"", stderr=b"")

        hash_patch = mock.patch.object(
            native_corpus_module,
            "FFMPEG_EXE_SHA256",
            self.ffmpeg_sha256,
        )
        discovery_patch = mock.patch.object(
            native_corpus_module,
            "discover_executable",
            return_value=self.ffmpeg,
        )
        run_patch = mock.patch.object(
            native_corpus_module.subprocess,
            "run",
            side_effect=fake_run,
        )
        hash_patch.start()
        discovery_patch.start()
        run_patch.start()
        self.addCleanup(run_patch.stop)
        self.addCleanup(discovery_patch.stop)
        self.addCleanup(hash_patch.stop)

    def test_builds_deduplicated_native_objects_and_repeats_deterministically(
        self,
    ) -> None:
        rgba = (17, 34, 51, 255)
        png = _image_payload("PNG", rgba)
        tga = _image_payload("TGA", rgba)
        wav = _wav_payload()
        mp3 = _mp3_payload()
        files = {
            "Art/CompiledTextures/Alpha.PNG": (png, "textures0.big"),
            "art/compiledtextures/Beta.tga": (tga, "textures1.big"),
            "data/audio/sounds/click.wav": (wav, "audio.big"),
            "data/audio/sounds/copy.WAV": (wav, "audio.big"),
            "data/audio/tracks/theme.mp3": (mp3, "music.big"),
            "art/models/ignored.w3d": (b"not a native lane", "w3d.big"),
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            first_output = base / "packs" / "first"
            second_output = base / "packs" / "second"
            _write_effective_assets(source, files)

            stdout = io.StringIO()
            with redirect_stdout(stdout):
                first = build_native_corpus(source, first_output)
                repeated = build_native_corpus(source, first_output)
                copied = build_native_corpus(source, second_output)
                forced = build_native_corpus(source, first_output, force=True)

            self.assertEqual(stdout.getvalue(), "")
            self.assertTrue(first.complete)
            self.assertFalse(first.reused)
            self.assertTrue(repeated.reused)
            self.assertFalse(copied.reused)
            self.assertFalse(forced.reused)
            self.assertEqual(first.neutral(), repeated.neutral())
            self.assertEqual(first.neutral(), copied.neutral())
            self.assertEqual(first.neutral(), forced.neutral())
            self.assertEqual(
                (first_output / NATIVE_CORPUS_MANIFEST).read_bytes(),
                (second_output / NATIVE_CORPUS_MANIFEST).read_bytes(),
            )

            self.assertEqual(len(first.entries), 5)
            self.assertEqual(len(first.outputs), 3)
            by_source = {item.source_path: item for item in first.entries}
            self.assertEqual(
                by_source["Art/CompiledTextures/Alpha.PNG"].output_path,
                by_source["art/compiledtextures/Beta.tga"].output_path,
            )
            self.assertEqual(
                by_source["data/audio/sounds/click.wav"].output_path,
                by_source["data/audio/sounds/copy.WAV"].output_path,
            )
            self.assertEqual(by_source["data/audio/tracks/theme.mp3"].family, "music")
            self.assertEqual(by_source["data/audio/sounds/click.wav"].family, "sound")
            self.assertEqual(
                by_source["Art/CompiledTextures/Alpha.PNG"].native_family, "png"
            )
            self.assertEqual(
                by_source["Art/CompiledTextures/Alpha.PNG"].evidence["facts"]["mode"],
                "RGBA",
            )
            self.assertTrue(all(item.evidence["valid"] for item in first.outputs))
            mp3_entry = by_source["data/audio/tracks/theme.mp3"]
            self.assertEqual(
                first_output.joinpath(*mp3_entry.output_path.split("/")).read_bytes(),
                mp3,
            )
            self.assertEqual(
                first.conversion,
                {
                    "ffmpeg": {
                        "tool": "ffmpeg",
                        "version": "8.1.1",
                        "executableSha256": self.ffmpeg_sha256,
                        "container": "wav",
                        "audioCodec": "pcm_s16le",
                        "sampleFormat": "s16",
                        "argumentTemplate": list(FFMPEG_WAV_ARGUMENT_TEMPLATE),
                    }
                },
            )
            self.assertTrue(
                all(
                    item.path
                    == f"objects/sha256/{item.sha256[:2]}/{item.sha256}{Path(item.path).suffix}"
                    for item in first.outputs
                )
            )
            self.assertFalse((first_output / ".work").exists())
            json.dumps(first.neutral(), allow_nan=False)

    def test_parallel_textures_are_bounded_and_byte_identical_to_serial(self) -> None:
        files = {
            f"art/texture-{index:02d}.tga": (
                _image_payload(
                    "TGA",
                    (index // 2 * 17, index // 2 * 19, index // 2 * 23, 255),
                ),
                "textures.big",
            )
            for index in range(8)
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            serial_output = base / "serial"
            parallel_output = base / "parallel"
            _write_effective_assets(source, files)

            serial = build_native_corpus(
                source,
                serial_output,
                families=["texture"],
                texture_workers=1,
            )

            active = 0
            peak = 0
            lock = threading.Lock()
            real_render = native_corpus_module._render_texture

            def observed_render(
                source_path: Path, target: Path, image_module: object
            ) -> str | None:
                nonlocal active, peak
                with lock:
                    active += 1
                    peak = max(peak, active)
                try:
                    time.sleep(0.03)
                    return real_render(source_path, target, image_module)
                finally:
                    with lock:
                        active -= 1

            with mock.patch.object(
                native_corpus_module,
                "_render_texture",
                side_effect=observed_render,
            ):
                parallel = build_native_corpus(
                    source,
                    parallel_output,
                    families=["texture"],
                    texture_workers=4,
                )

            self.assertGreaterEqual(peak, 2)
            self.assertLessEqual(peak, 4)
            self.assertEqual(serial.neutral(), parallel.neutral())
            self.assertEqual(serial.request_sha256, parallel.request_sha256)
            self.assertEqual(serial.identity_sha256, parallel.identity_sha256)
            self.assertEqual(
                _tree_snapshot(serial_output), _tree_snapshot(parallel_output)
            )
            self.assertEqual(
                (serial_output / NATIVE_CORPUS_MANIFEST).read_bytes(),
                (parallel_output / NATIVE_CORPUS_MANIFEST).read_bytes(),
            )
            self.assertEqual(len(parallel.entries), 8)
            self.assertEqual(len(parallel.outputs), 4)
            for first, second in zip(parallel.entries[::2], parallel.entries[1::2]):
                self.assertEqual(first.output_path, second.output_path)

            serialized = json.dumps(parallel.neutral(), sort_keys=True)
            for forbidden in (
                str(base),
                "openbfme-texture",
                "texture_workers",
                ".staging-",
                ".work",
                "candidate-",
                "source-",
            ):
                self.assertNotIn(forbidden, serialized)
            self.assertFalse(
                any(
                    thread.name.startswith("openbfme-texture")
                    for thread in threading.enumerate()
                )
            )

    def test_parallel_reuse_backtests_are_bounded_and_identity_neutral(self) -> None:
        files = {
            f"art/texture-{index:02d}.png": (
                _image_payload("PNG", (index * 17, index * 13, index * 11, 255)),
                "textures.big",
            )
            for index in range(8)
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            output = base / "native"
            _write_effective_assets(source, files)
            initial = build_native_corpus(
                source,
                output,
                families=["texture"],
                texture_workers=1,
            )

            active = 0
            peak = 0
            lock = threading.Lock()
            real_validate = native_corpus_module._validate_evidence

            def observed_validate(*args: object, **kwargs: object) -> object:
                nonlocal active, peak
                with lock:
                    active += 1
                    peak = max(peak, active)
                try:
                    time.sleep(0.03)
                    return real_validate(*args, **kwargs)
                finally:
                    with lock:
                        active -= 1

            with mock.patch.object(
                native_corpus_module,
                "_validate_evidence",
                side_effect=observed_validate,
            ):
                reused = build_native_corpus(
                    source,
                    output,
                    families=["texture"],
                    texture_workers=4,
                )

            self.assertTrue(reused.reused)
            self.assertGreaterEqual(peak, 2)
            self.assertLessEqual(peak, 4)
            self.assertEqual(initial.identity_sha256, reused.identity_sha256)
            self.assertEqual(initial.request_sha256, reused.request_sha256)
            self.assertEqual(initial.neutral(), reused.neutral())
            self.assertFalse(
                any(
                    thread.name.startswith("openbfme-native-verify")
                    for thread in threading.enumerate()
                )
            )

    def test_parallel_failure_order_and_cleanup_match_serial(self) -> None:
        files = {
            "art/A.png": (b"broken-a", "textures.big"),
            "art/b.png": (b"broken-b", "textures.big"),
            "art/C.png": (b"broken-c", "textures.big"),
            "art/d.png": (b"broken-d", "textures.big"),
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            _write_effective_assets(source, files)

            failures: list[tuple[dict[str, str], ...]] = []
            for workers in (1, 4, 4):
                output = base / f"failure-{workers}-{len(failures)}"
                with self.assertRaises(NativeCorpusBuildError) as caught:
                    build_native_corpus(
                        source,
                        output,
                        families=["texture"],
                        texture_workers=workers,
                    )
                failures.append(
                    tuple(item.neutral() for item in caught.exception.failures)
                )
                self.assertFalse(output.exists())
                self.assertEqual(
                    list(base.glob(f".{output.name}.staging-*")),
                    [],
                )

            self.assertEqual(failures[0], failures[1])
            self.assertEqual(failures[1], failures[2])
            self.assertEqual(
                [item["sourcePath"] for item in failures[1]],
                ["art/A.png", "art/b.png", "art/C.png", "art/d.png"],
            )

    def test_texture_worker_validation_precedes_filesystem_access(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            missing = base / "missing"
            for invalid in (True, False, 1.5, "4", None):
                with self.assertRaisesRegex(TypeError, "worker count"):
                    build_native_corpus(
                        missing,
                        base / "unused",
                        texture_workers=invalid,  # type: ignore[arg-type]
                    )
            for invalid in (0, -1, 9):
                with self.assertRaisesRegex(ValueError, "1..8"):
                    build_native_corpus(
                        missing,
                        base / "unused",
                        texture_workers=invalid,
                    )
            self.assertEqual(list(base.iterdir()), [])

    def test_parallel_reclassification_is_canonical_and_never_packages_maps(
        self,
    ) -> None:
        direct = b"CkMp" + b"direct-map-body"
        wrapped = _ear_refpack_payload(b"CkMp" + b"wrapped-map-body")
        files = {
            "art/compiledtextures/a-map.dds": (direct, "maps.big"),
            "art/compiledtextures/b-map.tga": (wrapped, "maps.big"),
            "art/compiledtextures/c-real.tga": (
                _image_payload("TGA", (1, 2, 3, 255)),
                "textures.big",
            ),
            "art/compiledtextures/d-real.jpg": (
                _image_payload("JPEG", (4, 5, 6, 255)),
                "textures.big",
            ),
            "art/compiledtextures/e-real.png": (
                _image_payload("PNG", (7, 8, 9, 255)),
                "textures.big",
            ),
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            _write_effective_assets(source, files)
            serial = build_native_corpus(
                source,
                base / "serial",
                families=["texture"],
                texture_workers=1,
            )
            parallel = build_native_corpus(
                source,
                base / "parallel",
                families=["texture"],
                texture_workers=4,
            )

            self.assertEqual(serial.neutral(), parallel.neutral())
            self.assertEqual(len(parallel.entries), 3)
            self.assertEqual(len(parallel.reclassified), 2)
            self.assertEqual(
                [item.source_path for item in parallel.reclassified],
                [
                    "art/compiledtextures/a-map.dds",
                    "art/compiledtextures/b-map.tga",
                ],
            )
            packaged_hashes = {item.sha256 for item in parallel.outputs}
            self.assertNotIn(hashlib.sha256(direct).hexdigest(), packaged_hashes)
            self.assertNotIn(hashlib.sha256(wrapped).hexdigest(), packaged_hashes)

    def test_parallel_fatal_failure_preserves_existing_publish_and_drains_workers(
        self,
    ) -> None:
        files = {
            f"art/texture-{index}.png": (
                _image_payload("PNG", (index, index, index, 255)),
                "textures.big",
            )
            for index in range(6)
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            output = base / "native"
            _write_effective_assets(source, files)
            build_native_corpus(source, output, texture_workers=4)
            before = _tree_snapshot(output)
            real_prepare = native_corpus_module._prepare_texture_candidate

            def fail_one(
                item: object,
                staged_source: Path,
                candidate: Path,
                image_module: object,
            ) -> object:
                if staged_source.name.startswith("source-00000000"):
                    raise RuntimeError("synthetic texture worker failure")
                time.sleep(0.02)
                return real_prepare(item, staged_source, candidate, image_module)

            with mock.patch.object(
                native_corpus_module,
                "_prepare_texture_candidate",
                side_effect=fail_one,
            ):
                with self.assertRaisesRegex(RuntimeError, "worker failure"):
                    build_native_corpus(
                        source,
                        output,
                        force=True,
                        texture_workers=4,
                    )

            self.assertEqual(_tree_snapshot(output), before)
            self.assertEqual(list(base.glob(".native.staging-*")), [])
            self.assertEqual(list(base.glob(".native.backup-*")), [])
            self.assertFalse(
                any(
                    thread.name.startswith("openbfme-texture")
                    for thread in threading.enumerate()
                )
            )

    def test_compressed_wav_uses_exact_flags_and_records_sanitized_evidence(
        self,
    ) -> None:
        compressed = _compressed_wav_payload()
        files = {"Data/Audio/Sounds/SecretRetailVoice.WAV": (compressed, "audio.big")}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            first_output = base / "first"
            second_output = base / "second"
            _write_effective_assets(source, files)

            first = build_native_corpus(
                source,
                first_output,
                families=["sound"],
                ffmpeg_path=self.ffmpeg,
            )
            second = build_native_corpus(
                source,
                second_output,
                families=["sound"],
                ffmpeg_path=self.ffmpeg,
            )

            self.assertEqual(first.neutral(), second.neutral())
            self.assertEqual(first.entries[0].native_family, "wav-pcm")
            self.assertEqual(first.entries[0].evidence["facts"]["encoding"], "pcm-16")
            self.assertEqual(first.entries[0].evidence["facts"]["bitsPerSample"], 16)
            object_path = first_output.joinpath(
                *first.entries[0].output_path.split("/")
            )
            self.assertNotEqual(object_path.read_bytes(), compressed)

            conversions = [
                command for command in self.ffmpeg_commands if "-i" in command
            ]
            self.assertEqual(len(conversions), 2)
            for command in conversions:
                arguments = command[1:]
                arguments[arguments.index("-i") + 1] = "<input>"
                arguments[-1] = "<output>"
                self.assertEqual(tuple(arguments), FFMPEG_WAV_ARGUMENT_TEMPLATE)

            serialized_evidence = json.dumps(first.conversion, sort_keys=True)
            self.assertNotIn(str(base), serialized_evidence)
            self.assertNotIn("SecretRetailVoice", serialized_evidence)
            self.assertNotIn(str(self.ffmpeg), serialized_evidence)
            self.assertEqual(
                first.conversion["ffmpeg"]["audioCodec"],
                "pcm_s16le",
            )

    def test_missing_or_untrusted_ffmpeg_fails_before_transaction(self) -> None:
        files = {"audio/voice.wav": (_compressed_wav_payload(), "audio.big")}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            _write_effective_assets(source, files)

            missing_output = base / "missing-output"
            missing_tool = base / "missing-ffmpeg.exe"
            with self.assertRaises(NativeCorpusDependencyError) as missing:
                build_native_corpus(
                    source,
                    missing_output,
                    ffmpeg_path=missing_tool,
                )
            self.assertFalse(missing_output.exists())
            self.assertEqual(list(base.glob(".missing-output.staging-*")), [])
            self.assertNotIn(str(missing_tool), str(missing.exception))

            untrusted_tool = base / "untrusted-ffmpeg.exe"
            untrusted_tool.write_bytes(b"not the pinned binary")
            untrusted_output = base / "untrusted-output"
            with self.assertRaises(NativeCorpusDependencyError) as untrusted:
                build_native_corpus(
                    source,
                    untrusted_output,
                    ffmpeg_path=untrusted_tool,
                )
            self.assertFalse(untrusted_output.exists())
            self.assertEqual(list(base.glob(".untrusted-output.staging-*")), [])
            self.assertNotIn(str(untrusted_tool), str(untrusted.exception))

    def test_family_selection_and_selected_bounds_fail_before_output(self) -> None:
        files = {
            "art/one.png": (_image_payload("PNG", (1, 2, 3, 255)), "textures0.big"),
            "art/map-disguised.tga": (b"CkMpbounded-map", "maps.big"),
            "audio/one.wav": (_wav_payload(), "audio.big"),
            "audio/two.wav": (_wav_payload(samples=b"\x02\x00"), "audio.big"),
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            _write_effective_assets(source, files)
            sound_output = base / "sound"
            report = build_native_corpus(source, sound_output, families=["sound"])
            self.assertEqual(report.families, ("sound",))
            self.assertEqual({item.family for item in report.entries}, {"sound"})
            self.assertEqual(len(report.entries), 2)

            with self.assertRaisesRegex(NativeCorpusLimitError, "file count"):
                build_native_corpus(source, base / "file-limit", max_files=2)
            self.assertFalse((base / "file-limit").exists())
            with self.assertRaisesRegex(NativeCorpusLimitError, "file count"):
                build_native_corpus(
                    source,
                    base / "texture-file-limit",
                    families=["texture"],
                    max_files=1,
                )
            self.assertFalse((base / "texture-file-limit").exists())
            total = sum(len(payload) for payload, _ in files.values())
            with self.assertRaisesRegex(NativeCorpusLimitError, "byte total"):
                build_native_corpus(
                    source, base / "byte-limit", max_total_bytes=total - 1
                )
            self.assertFalse((base / "byte-limit").exists())

            with self.assertRaises(TypeError):
                build_native_corpus(source, base / "bad", families="texture")
            with self.assertRaisesRegex(ValueError, "must not be empty"):
                build_native_corpus(source, base / "bad", families=[])
            with self.assertRaisesRegex(ValueError, "unsupported"):
                build_native_corpus(source, base / "bad", families=["w3d"])
            with self.assertRaisesRegex(NativeCorpusError, "declares no files"):
                build_native_corpus(source, base / "music", families=["music"])

    def test_exact_failures_are_collected_and_transaction_is_not_published(
        self,
    ) -> None:
        files = {
            "art/unsupported.bmp": (b"synthetic bmp", "textures0.big"),
            "audio/broken.wav": (b"not a wave", "audio.big"),
            "audio/unsupported.ogg": (b"synthetic ogg", "audio.big"),
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            output = base / "native"
            _write_effective_assets(source, files)
            with self.assertRaises(NativeCorpusBuildError) as caught:
                build_native_corpus(source, output)

            failures = {item.source_path: item for item in caught.exception.failures}
            self.assertEqual(set(failures), set(files))
            self.assertEqual(
                failures["art/unsupported.bmp"].code, "unsupported-extension"
            )
            self.assertEqual(
                failures["audio/unsupported.ogg"].code, "unsupported-extension"
            )
            self.assertEqual(
                failures["audio/broken.wav"].code, "audio-transcode-failed"
            )
            self.assertIn("exit code 23", failures["audio/broken.wav"].detail)
            rendered_error = str(caught.exception)
            self.assertIn("3 failures", rendered_error)
            self.assertIn("audio-transcode-failed=1", rendered_error)
            self.assertNotIn("broken.wav", rendered_error)
            self.assertNotIn("secret-source", rendered_error)
            self.assertFalse(output.exists())
            self.assertEqual(list(base.glob(".native.staging-*")), [])
            self.assertEqual(list(base.glob(".native.backup-*")), [])

    def test_existing_output_is_verified_and_force_repairs_tamper(self) -> None:
        files = {"art/a.png": (_image_payload("PNG", (7, 8, 9, 255)), "textures0.big")}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            output = base / "native"
            _write_effective_assets(source, files)
            original = build_native_corpus(source, output, texture_workers=4)
            object_path = output.joinpath(*original.outputs[0].path.split("/"))
            object_path.write_bytes(object_path.read_bytes() + b"tamper")

            with self.assertRaisesRegex(NativeCorpusReuseError, "failed verification"):
                build_native_corpus(source, output, texture_workers=1)
            repaired = build_native_corpus(
                source, output, force=True, texture_workers=4
            )
            self.assertEqual(repaired.neutral(), original.neutral())
            self.assertEqual(
                hashlib.sha256(object_path.read_bytes()).hexdigest(),
                repaired.outputs[0].sha256,
            )

            (output / "extra.bin").write_bytes(b"extra")
            with self.assertRaisesRegex(NativeCorpusReuseError, "undeclared files"):
                build_native_corpus(source, output, texture_workers=1)
            rebuilt = build_native_corpus(source, output, force=True, texture_workers=4)
            self.assertFalse((output / "extra.bin").exists())
            self.assertEqual(rebuilt.neutral(), original.neutral())

    def test_force_failure_preserves_previous_corpus_and_cleans_staging(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            valid_source = base / "valid"
            invalid_source = base / "invalid"
            output = base / "native"
            _write_effective_assets(
                valid_source,
                {"audio/click.wav": (_wav_payload(), "audio.big")},
            )
            _write_effective_assets(
                invalid_source,
                {"audio/click.wav": (b"bad wave", "audio.big")},
            )
            build_native_corpus(valid_source, output)
            before = _tree_snapshot(output)

            with self.assertRaises(NativeCorpusBuildError) as caught:
                build_native_corpus(invalid_source, output, force=True)
            self.assertNotIn("audio/click.wav", str(caught.exception))
            self.assertNotIn(str(invalid_source), str(caught.exception))
            self.assertEqual(
                caught.exception.failures[0].code,
                "audio-transcode-failed",
            )
            self.assertEqual(_tree_snapshot(output), before)
            self.assertEqual(list(base.glob(".native.staging-*")), [])
            self.assertEqual(list(base.glob(".native.backup-*")), [])

    def test_publish_error_rolls_back_previous_corpus(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            first_source = base / "first"
            second_source = base / "second"
            output = base / "native"
            _write_effective_assets(
                first_source,
                {"art/a.png": (_image_payload("PNG", (1, 1, 1, 255)), "textures0.big")},
            )
            _write_effective_assets(
                second_source,
                {"art/a.png": (_image_payload("PNG", (2, 2, 2, 255)), "textures0.big")},
            )
            build_native_corpus(first_source, output, texture_workers=4)
            before = _tree_snapshot(output)
            real_replace = os.replace

            def fail_publish(source: object, destination: object) -> None:
                source_path = Path(source)
                destination_path = Path(destination)
                if (
                    source_path.name.startswith(".native.staging-")
                    and destination_path == output
                ):
                    raise OSError("synthetic publish failure")
                real_replace(source_path, destination_path)

            with mock.patch.object(
                native_corpus_module.os, "replace", side_effect=fail_publish
            ):
                with self.assertRaisesRegex(
                    NativeCorpusError, "prior output was preserved"
                ):
                    build_native_corpus(
                        second_source,
                        output,
                        force=True,
                        texture_workers=4,
                    )

            self.assertEqual(_tree_snapshot(output), before)
            self.assertEqual(list(base.glob(".native.staging-*")), [])
            self.assertEqual(list(base.glob(".native.backup-*")), [])

    def test_source_hash_tree_and_path_containment_are_enforced(self) -> None:
        files = {"art/a.png": (_image_payload("PNG", (4, 5, 6, 255)), "textures0.big")}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            mismatch = base / "mismatch"
            _write_effective_assets(
                mismatch,
                files,
                declared_hashes={"art/a.png": "c" * 64},
            )
            with self.assertRaises(NativeCorpusBuildError) as caught:
                build_native_corpus(mismatch, base / "mismatch-output")
            self.assertEqual(
                caught.exception.failures[0].code, "source-sha256-mismatch"
            )

            extra = base / "extra"
            _write_effective_assets(extra, files)
            (extra / "undeclared.bin").write_bytes(b"extra")
            with self.assertRaisesRegex(NativeCorpusError, "undeclared files"):
                build_native_corpus(extra, base / "extra-output")

            overlap = base / "overlap"
            _write_effective_assets(overlap, files)
            with self.assertRaisesRegex(NativeCorpusError, "must not overlap"):
                build_native_corpus(overlap, overlap / "native")

    def test_ear_wrapped_ckmp_with_tga_suffix_is_reclassified(self) -> None:
        disguised = _ear_refpack_payload(b"CkMp" + b"synthetic-map-body")
        texture = _image_payload("PNG", (21, 22, 23, 255))
        files = {
            "art/compiledtextures/disguised.tga": (disguised, "maps.big"),
            "art/compiledtextures/real.png": (texture, "textures.big"),
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            output = base / "native"
            _write_effective_assets(source, files)

            report = build_native_corpus(source, output, families=["texture"])

            self.assertEqual(report.candidate_file_count, 2)
            self.assertEqual(report.converted_file_count, 1)
            self.assertEqual(report.reclassified_file_count, 1)
            self.assertEqual(len(report.outputs), 1)
            detected = report.reclassified[0]
            self.assertEqual(detected.source_path, "art/compiledtextures/disguised.tga")
            self.assertEqual(detected.source_bytes, len(disguised))
            self.assertEqual(
                detected.source_sha256, hashlib.sha256(disguised).hexdigest()
            )
            self.assertEqual(detected.original_family, "texture")
            self.assertEqual(detected.original_extension, ".tga")
            self.assertEqual(detected.detected_kind, "ear-refpack")
            self.assertRegex(detected.evidence_sha256, r"^[0-9a-f]{64}$")
            self.assertNotIn(
                detected.source_path, {item.source_path for item in report.entries}
            )

    def test_direct_ckmp_with_texture_suffix_is_reclassified(self) -> None:
        direct = b"CkMp" + b"direct-map-body"
        files = {
            "art/compiledtextures/direct.dds": (direct, "textures.big"),
            "art/compiledtextures/real.tga": (
                _image_payload("TGA", (31, 32, 33, 255)),
                "textures.big",
            ),
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            output = base / "native"
            _write_effective_assets(source, files)

            report = build_native_corpus(source, output, families=["texture"])

            self.assertEqual(len(report.entries), 1)
            self.assertEqual(len(report.reclassified), 1)
            self.assertEqual(report.reclassified[0].detected_kind, "uncompressed")
            self.assertEqual(report.reclassified[0].original_extension, ".dds")
            self.assertEqual(
                report.candidate_bytes,
                sum(map(len, (direct, files["art/compiledtextures/real.tga"][0]))),
            )

    def test_ordinary_tga_still_converts_and_backtests(self) -> None:
        texture = _image_payload("TGA", (41, 42, 43, 255))
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            output = base / "native"
            _write_effective_assets(
                source,
                {"art/compiledtextures/ordinary.tga": (texture, "textures.big")},
            )

            report = build_native_corpus(source, output, families=["texture"])

            self.assertEqual(report.candidate_file_count, 1)
            self.assertEqual(report.converted_file_count, 1)
            self.assertEqual(report.reclassified_file_count, 0)
            self.assertEqual(report.entries[0].native_family, "png")
            self.assertTrue(report.entries[0].evidence["valid"])

    def test_non_ckmp_ear_texture_is_not_silently_reclassified(self) -> None:
        not_a_map = _ear_refpack_payload(b"NOPE" + b"not-a-map")
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            output = base / "native"
            _write_effective_assets(
                source,
                {"art/compiledtextures/not-a-map.tga": (not_a_map, "maps.big")},
            )

            with self.assertRaises(NativeCorpusBuildError) as caught:
                build_native_corpus(source, output, families=["texture"])

            self.assertEqual(len(caught.exception.failures), 1)
            self.assertEqual(caught.exception.failures[0].code, "texture-decode-failed")
            self.assertFalse(output.exists())

    def test_reclassification_evidence_changes_request_and_corpus_identity(
        self,
    ) -> None:
        direct = b"CkMp" + b"identity-map"
        files = {"art/identity.png": (direct, "maps.big")}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            _write_effective_assets(source, files)
            first = build_native_corpus(source, base / "first", families=["texture"])

            replacement_evidence = "f" * 64
            with mock.patch.object(
                native_corpus_module,
                "_reclassification_evidence_sha256",
                return_value=replacement_evidence,
            ):
                second = build_native_corpus(
                    source, base / "second", families=["texture"]
                )

            self.assertNotEqual(first.request_sha256, second.request_sha256)
            self.assertNotEqual(first.identity_sha256, second.identity_sha256)
            self.assertEqual(
                first.reclassified[0].source_sha256,
                second.reclassified[0].source_sha256,
            )
            self.assertEqual(
                second.reclassified[0].evidence_sha256, replacement_evidence
            )

    def test_reclassification_manifest_tamper_is_rejected_during_reuse(self) -> None:
        direct = b"CkMp" + b"reuse-map"
        files = {"art/reuse.tga": (direct, "maps.big")}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            output = base / "native"
            _write_effective_assets(source, files)
            original = build_native_corpus(source, output, families=["texture"])

            manifest_path = output / NATIVE_CORPUS_MANIFEST
            document = json.loads(manifest_path.read_text(encoding="utf-8"))
            reclassified = document["reclassified"][0]
            reclassified["evidenceSha256"] = "e" * 64
            selection = document["selection"]
            selection["requestSha256"] = (
                native_corpus_module._request_sha256_from_sources(
                    selection["sourceManifestSha256"],
                    selection["sourceManifestAggregateSha256"],
                    tuple(selection["families"]),
                    selection["conversion"],
                    (
                        {
                            "path": reclassified["sourcePath"],
                            "bytes": reclassified["sourceBytes"],
                            "sha256": reclassified["sourceSha256"],
                            "family": reclassified["originalFamily"],
                            "extension": reclassified["originalExtension"],
                            "disposition": "map-payload",
                            "detectedKind": reclassified["detectedKind"],
                            "evidenceSha256": reclassified["evidenceSha256"],
                        },
                    ),
                )
            )
            basis = {
                key: value for key, value in document.items() if key != "identitySha256"
            }
            document["identitySha256"] = native_corpus_module._canonical_sha256(basis)
            manifest_path.write_bytes(
                native_corpus_module._canonical_json_bytes(document, pretty=True)
            )

            with self.assertRaisesRegex(
                NativeCorpusReuseError, "does not match the requested"
            ):
                build_native_corpus(source, output, families=["texture"])
            repaired = build_native_corpus(
                source, output, families=["texture"], force=True
            )
            self.assertEqual(repaired.neutral(), original.neutral())

    def test_reclassified_only_corpus_has_no_packaged_map_object(self) -> None:
        direct = b"CkMp" + b"map-bytes-must-not-be-packaged"
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            output = base / "native"
            _write_effective_assets(
                source,
                {"art/map-with-jpg-suffix.jpg": (direct, "maps.big")},
            )

            report = build_native_corpus(source, output, families=["texture"])

            self.assertEqual(report.entries, ())
            self.assertEqual(report.outputs, ())
            self.assertEqual(len(report.reclassified), 1)
            self.assertFalse((output / "objects").exists())
            self.assertEqual(
                [path.relative_to(output).as_posix() for path in output.rglob("*")],
                [NATIVE_CORPUS_MANIFEST],
            )
            self.assertEqual(
                report.neutral()["totals"],
                {
                    "candidateFileCount": 1,
                    "candidateBytes": len(direct),
                    "convertedFileCount": 0,
                    "convertedBytes": 0,
                    "reclassifiedFileCount": 1,
                    "reclassifiedBytes": len(direct),
                    "outputFileCount": 0,
                    "outputBytes": 0,
                },
            )

    def test_links_are_rejected_when_supported_by_the_platform(self) -> None:
        files = {"audio/a.wav": (_wav_payload(), "audio.big")}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective"
            _write_effective_assets(source, files)
            declared = source / "audio" / "a.wav"
            target = base / "actual.wav"
            target.write_bytes(declared.read_bytes())
            declared.unlink()
            try:
                os.symlink(target, declared)
            except OSError:
                declared.write_bytes(target.read_bytes())
                original = native_corpus_module._is_link_like
                with mock.patch.object(
                    native_corpus_module,
                    "_is_link_like",
                    side_effect=lambda path: path == declared or original(path),
                ):
                    with self.assertRaisesRegex(NativeCorpusError, "contains a link"):
                        build_native_corpus(source, base / "native")
            else:
                with self.assertRaisesRegex(NativeCorpusError, "contains a link"):
                    build_native_corpus(source, base / "native")


if __name__ == "__main__":
    unittest.main()
