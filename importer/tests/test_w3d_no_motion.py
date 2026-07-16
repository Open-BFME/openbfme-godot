from __future__ import annotations

from dataclasses import FrozenInstanceError, replace
import hashlib
import json
import math
import struct
import unittest
from unittest.mock import patch

from openbfme_importer.w3d_metadata import scan_w3d_metadata
from openbfme_importer.w3d_no_motion import (
    W3DNoMotionError,
    W3DNoMotionExpectation,
    strip_proven_header_only_animations,
)


CONTAINER = 0x80000000
MESH = 0x00000000
MESH_HEADER = 0x0000001F
HIERARCHY = 0x00000100
HIERARCHY_HEADER = 0x00000101
PIVOTS = 0x00000102
ANIMATION = 0x00000200
ANIMATION_HEADER = 0x00000201
ANIMATION_CHANNEL = 0x00000202
ANIMATION_BIT_CHANNEL = 0x00000203
COMPRESSED_ANIMATION = 0x00000280
COMPRESSED_ANIMATION_HEADER = 0x00000281
COMPRESSED_ANIMATION_CHANNEL = 0x00000282
COMPRESSED_BIT_CHANNEL = 0x00000283
COMPRESSED_MOTION_CHANNEL = 0x00000284
MORPH_ANIMATION = 0x000002C0
HLOD = 0x00000700
HLOD_HEADER = 0x00000701
HLOD_LOD_ARRAY = 0x00000702
HLOD_LOD_ARRAY_HEADER = 0x00000703
HLOD_SUB_OBJECT = 0x00000704


def _fixed(value: str, size: int = 16) -> bytes:
    encoded = value.encode("ascii")
    if len(encoded) > size:
        raise ValueError(value)
    return encoded + b"\x00" * (size - len(encoded))


def _chunk(kind: int, payload: bytes, *, container: bool = False) -> bytes:
    size = len(payload) | (CONTAINER if container else 0)
    return struct.pack("<II", kind, size) + payload


def _hierarchy(identifier: str = "TEST_SKL") -> bytes:
    header = struct.pack(
        "<I16sI3f",
        0x00040001,
        _fixed(identifier),
        1,
        0.0,
        0.0,
        0.0,
    )
    pivot = struct.pack(
        "<16si10f",
        _fixed("ROOTTRANSFORM"),
        -1,
        *([0.0] * 9),
        1.0,
    )
    return _chunk(
        HIERARCHY,
        _chunk(HIERARCHY_HEADER, header) + _chunk(PIVOTS, pivot),
        container=True,
    )


def _mesh(identifier: str = "TEST_MODEL") -> bytes:
    header = struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0,
        _fixed("MESH"),
        _fixed(identifier),
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        3,
        1,
        *([0.0] * 10),
    )
    return _chunk(
        MESH,
        _chunk(MESH_HEADER, header)
        + _chunk(0x0000000C, b"retained mesh user text\x00"),
        container=True,
    )


def _hlod(
    model_identifier: str = "TEST_MODEL",
    hierarchy_identifier: str = "TEST_SKL",
) -> bytes:
    header = struct.pack(
        "<II16s16s",
        0x00010000,
        1,
        _fixed(model_identifier),
        _fixed(hierarchy_identifier),
    )
    lod = _chunk(HLOD_LOD_ARRAY_HEADER, struct.pack("<If", 1, 1.0)) + _chunk(
        HLOD_SUB_OBJECT,
        struct.pack("<I32s", 0, _fixed(f"{model_identifier}.MESH", 32)),
    )
    return _chunk(
        HLOD,
        _chunk(HLOD_HEADER, header) + _chunk(HLOD_LOD_ARRAY, lod, container=True),
        container=True,
    )


def _animation_header(
    *,
    identifier: str = "TEST_IDLE",
    hierarchy_identifier: str = "TEST_SKL",
    frame_count: int = 101,
    frame_rate: int = 30,
    compressed: bool = False,
    flavor: int = 1,
    raw_identifier: bytes | None = None,
) -> bytes:
    raw_id = _fixed(identifier) if raw_identifier is None else raw_identifier
    if compressed:
        payload = struct.pack(
            "<I16s16sIHH",
            0x00040001,
            raw_id,
            _fixed(hierarchy_identifier),
            frame_count,
            frame_rate,
            flavor,
        )
        return _chunk(COMPRESSED_ANIMATION_HEADER, payload)
    payload = struct.pack(
        "<I16s16sII",
        0x00040001,
        raw_id,
        _fixed(hierarchy_identifier),
        frame_count,
        frame_rate,
    )
    return _chunk(ANIMATION_HEADER, payload)


def _animation(
    *,
    identifier: str = "TEST_IDLE",
    hierarchy_identifier: str = "TEST_SKL",
    frame_count: int = 101,
    frame_rate: int = 30,
    compressed: bool = False,
    flavor: int = 1,
    extra: bytes = b"",
    container: bool = True,
    raw_identifier: bytes | None = None,
) -> bytes:
    header = _animation_header(
        identifier=identifier,
        hierarchy_identifier=hierarchy_identifier,
        frame_count=frame_count,
        frame_rate=frame_rate,
        compressed=compressed,
        flavor=flavor,
        raw_identifier=raw_identifier,
    )
    return _chunk(
        COMPRESSED_ANIMATION if compressed else ANIMATION,
        header + extra,
        container=container,
    )


def _source(
    *animations: bytes,
    hierarchy_identifier: str = "TEST_SKL",
    model_identifier: str = "TEST_MODEL",
) -> bytes:
    if not animations:
        animations = (_animation(hierarchy_identifier=hierarchy_identifier),)
    return b"".join(
        (
            _chunk(0x12345678, b"retained-prefix"),
            _hierarchy(hierarchy_identifier),
            *animations,
            _mesh(model_identifier),
            _chunk(0x76543210, b"retained-suffix"),
            _hlod(model_identifier, hierarchy_identifier),
        )
    )


def _expectation(
    *,
    identifier: str = "TEST_IDLE",
    hierarchy_identifier: str = "TEST_SKL",
    frame_count: int | float = 101,
    frame_rate: int | float = 30,
    compressed: bool = False,
    model_identifier: str | None = "TEST_MODEL",
    flavor: int | float | None = None,
) -> W3DNoMotionExpectation:
    return W3DNoMotionExpectation(
        identifier=identifier,
        hierarchy_identifier=hierarchy_identifier,
        frame_count=frame_count,
        frame_rate=frame_rate,
        compressed=compressed,
        model_identifier=model_identifier,
        flavor=(1 if compressed and flavor is None else flavor),
    )


def _top_chunks(source: bytes) -> list[tuple[int, bytes]]:
    result = []
    cursor = 0
    while cursor < len(source):
        kind, raw_size = struct.unpack_from("<II", source, cursor)
        end = cursor + 8 + (raw_size & 0x7FFFFFFF)
        if end > len(source):
            raise AssertionError("invalid fixture")
        result.append((kind, source[cursor:end]))
        cursor = end
    if cursor != len(source):
        raise AssertionError("invalid fixture")
    return result


class W3DNoMotionSuccessTests(unittest.TestCase):
    def test_raw_header_only_container_is_removed_by_top_level_concatenation(
        self,
    ) -> None:
        source = _source()
        before = _top_chunks(source)

        result = strip_proven_header_only_animations(
            source,
            virtual_path="art/w3d/test_model.w3d",
            expectations=(_expectation(),),
        )
        output = result.output_bytes()
        proof = result.proof
        after = _top_chunks(output)

        retained = [item for item in before if item[0] != ANIMATION]
        self.assertEqual(after, retained)
        removed = next(payload for kind, payload in before if kind == ANIMATION)
        self.assertEqual(proof.removed_container_count, 1)
        self.assertEqual(proof.removed_byte_count, len(removed))
        self.assertEqual(len(source) - len(output), len(removed))
        self.assertEqual(proof.input_sha256, hashlib.sha256(source).hexdigest())
        self.assertEqual(proof.output_sha256, hashlib.sha256(output).hexdigest())
        self.assertEqual(proof.model_header_count, 1)
        self.assertEqual(proof.model_reference_count, 1)
        self.assertEqual(proof.hierarchy_header_count, 1)
        self.assertEqual(proof.hierarchy_pivot_count, 1)
        self.assertEqual(proof.mesh_header_count, 1)
        self.assertEqual(proof.headers[0].identifier, "TEST_IDLE")
        self.assertEqual(proof.headers[0].hierarchy_identifier, "TEST_SKL")
        self.assertEqual(proof.headers[0].model_identifier, "TEST_MODEL")
        self.assertEqual(proof.headers[0].frame_count, 101)
        self.assertEqual(proof.headers[0].frame_rate, 30)
        self.assertFalse(proof.headers[0].compressed)
        self.assertIsNone(proof.headers[0].flavor)
        self.assertEqual(
            scan_w3d_metadata(output, "art/w3d/test_model.w3d").animation_headers,
            (),
        )

    def test_compressed_header_only_container_and_zero_flavor_are_supported(
        self,
    ) -> None:
        source = _source(_animation(compressed=True, flavor=0))
        result = strip_proven_header_only_animations(
            source,
            virtual_path="art/w3d/test_model.w3d",
            expectations=(_expectation(compressed=True, flavor=0),),
        )

        self.assertTrue(result.proof.headers[0].compressed)
        self.assertEqual(result.proof.headers[0].flavor, 0)
        self.assertNotIn(
            COMPRESSED_ANIMATION,
            [kind for kind, _ in _top_chunks(result.output_bytes())],
        )

    def test_multiple_actions_match_by_exact_id_not_expectation_order(self) -> None:
        source = _source(
            _animation(identifier="TEST_IDLE", frame_count=10),
            _animation(identifier="TEST_MOVE", frame_count=20),
        )
        expectations = (
            _expectation(identifier="TEST_MOVE", frame_count=20),
            _expectation(identifier="TEST_IDLE", frame_count=10),
        )
        first = strip_proven_header_only_animations(
            source,
            virtual_path="art/w3d/test_model.w3d",
            expectations=expectations,
        )
        second = strip_proven_header_only_animations(
            source,
            virtual_path="art/w3d/test_model.w3d",
            expectations=tuple(reversed(expectations)),
        )

        self.assertEqual(first.output_bytes(), second.output_bytes())
        self.assertEqual(first.proof.neutral(), second.proof.neutral())
        self.assertEqual(
            [header.identifier for header in first.proof.headers],
            ["TEST_IDLE", "TEST_MOVE"],
        )

    def test_proof_is_deterministic_immutable_canonical_and_payload_free(self) -> None:
        source = _source()
        first = strip_proven_header_only_animations(
            source,
            virtual_path="art/w3d/private-name.w3d",
            expectations=(_expectation(),),
        )
        second = strip_proven_header_only_animations(
            source,
            virtual_path="art/w3d/another-name.w3d",
            expectations=(_expectation(),),
        )

        self.assertEqual(first.proof.neutral(), second.proof.neutral())
        neutral = first.proof.neutral()
        encoded = json.dumps(neutral, sort_keys=True)
        self.assertNotIn("private-name", encoded)
        self.assertNotIn("retained-prefix", encoded)
        self.assertNotIn("outputBytes", encoded)
        digest = neutral.pop("proofSha256")
        canonical = json.dumps(
            neutral,
            ensure_ascii=True,
            separators=(",", ":"),
            sort_keys=True,
        )
        self.assertEqual(digest, hashlib.sha256(canonical.encode()).hexdigest())
        with self.assertRaises(FrozenInstanceError):
            first.proof.removed_byte_count = 0  # type: ignore[misc]


class W3DNoMotionFailureTests(unittest.TestCase):
    def _assert_fails(
        self,
        source: bytes,
        expectations: tuple[W3DNoMotionExpectation, ...] | None = None,
        message: str | None = None,
    ) -> None:
        context = (
            self.assertRaisesRegex(W3DNoMotionError, message)
            if message is not None
            else self.assertRaises(W3DNoMotionError)
        )
        with context:
            strip_proven_header_only_animations(
                source,
                virtual_path="art/w3d/test_model.w3d",
                expectations=(_expectation(),)
                if expectations is None
                else expectations,
            )

    def test_all_motion_and_unknown_animation_children_fail_closed(self) -> None:
        cases = (
            ("raw-channel", ANIMATION_CHANNEL),
            ("raw-bit-channel", ANIMATION_BIT_CHANNEL),
            ("compressed-channel", COMPRESSED_ANIMATION_CHANNEL),
            ("compressed-bit-channel", COMPRESSED_BIT_CHANNEL),
            ("compressed-motion-channel", COMPRESSED_MOTION_CHANNEL),
            ("unknown-child", 0x0000027F),
        )
        for label, kind in cases:
            with self.subTest(label=label):
                self._assert_fails(
                    _source(_animation(extra=_chunk(kind, b"motion"))),
                    message="exactly one header",
                )

    def test_malformed_nesting_and_animation_ownership_fail_closed(self) -> None:
        nested_header = _chunk(
            ANIMATION,
            _chunk(
                0x11111111,
                _animation_header(),
                container=True,
            ),
            container=True,
        )
        nested_animation = _chunk(
            MESH,
            _animation(),
            container=True,
        )
        top_level_header = _animation_header()
        malformed_owner = _chunk(HIERARCHY, b"\x01", container=True)
        cases = (
            ("nested-header", _source(nested_header)),
            ("nested-animation", malformed_owner + nested_animation),
            ("top-level-header", top_level_header + _source()),
            ("truncated-owner", malformed_owner + _source()),
        )
        for label, source in cases:
            with self.subTest(label=label):
                self._assert_fails(source)

    def test_unflagged_wrong_sized_and_dirty_fixed_headers_fail_closed(self) -> None:
        dirty = b"TEST\x00DIRTY" + b"\x00" * 6
        wrong_size_header = _chunk(
            ANIMATION,
            _animation_header() + b"\x00",
            container=True,
        )
        cases = (
            ("unflagged", _source(_animation(container=False))),
            ("wrong-size", _source(wrong_size_header)),
            ("dirty-fixed", _source(_animation(raw_identifier=dirty))),
        )
        for label, source in cases:
            with self.subTest(label=label):
                self._assert_fails(source)

    def test_extra_missing_duplicate_and_morph_containers_fail_closed(self) -> None:
        two = _source(
            _animation(identifier="TEST_IDLE"),
            _animation(identifier="TEST_MOVE"),
        )
        duplicate = _source(_animation(), _animation())
        morph = _chunk(MORPH_ANIMATION, b"morph") + _source()
        self._assert_fails(two, message="count does not match")
        self._assert_fails(duplicate, expectations=(_expectation(), _expectation()))
        self._assert_fails(morph, message="not owned")
        self._assert_fails(_source(), expectations=(), message="at least one")

    def test_exact_identifier_header_timing_compression_and_flavor_are_sealed(
        self,
    ) -> None:
        cases = (
            ("case-sensitive-ID", _expectation(identifier="test_idle")),
            (
                "hierarchy",
                _expectation(hierarchy_identifier="OTHER_SKL"),
            ),
            ("frame-count", _expectation(frame_count=100)),
            ("frame-rate", _expectation(frame_rate=60)),
            ("compression", _expectation(compressed=True)),
        )
        for label, expectation in cases:
            with self.subTest(label=label):
                self._assert_fails(_source(), expectations=(expectation,))

        compressed = _source(_animation(compressed=True, flavor=2))
        self._assert_fails(
            compressed,
            expectations=(_expectation(compressed=True, flavor=1),),
        )

    def test_source_and_expected_timing_must_be_positive_finite_integers(self) -> None:
        self._assert_fails(_source(_animation(frame_count=0)), message="positive")
        self._assert_fails(_source(_animation(frame_rate=0)), message="positive")
        for label, expectation in (
            ("zero-frames", _expectation(frame_count=0)),
            ("zero-rate", _expectation(frame_rate=0)),
            ("nan-frames", _expectation(frame_count=math.nan)),
            ("infinite-rate", _expectation(frame_rate=math.inf)),
            ("fractional-rate", _expectation(frame_rate=29.5)),
        ):
            with self.subTest(label=label):
                self._assert_fails(_source(), expectations=(expectation,))

    def test_embedded_hierarchy_and_model_bindings_must_match_exactly(self) -> None:
        wrong_hierarchy = _source(
            _animation(hierarchy_identifier="ANIM_SKL"),
            hierarchy_identifier="EMBEDDED_SKL",
        )
        self._assert_fails(
            wrong_hierarchy,
            expectations=(_expectation(hierarchy_identifier="ANIM_SKL"),),
            message="hierarchy headers",
        )
        self._assert_fails(
            _source(),
            expectations=(_expectation(model_identifier="OTHER_MODEL"),),
            message="model headers",
        )
        self._assert_fails(
            _source(),
            expectations=(_expectation(model_identifier=None),),
            message="model headers",
        )

    def test_ambiguous_expectations_and_invalid_types_fail_before_output(self) -> None:
        casefold_duplicate = (
            _expectation(identifier="TEST_IDLE"),
            _expectation(identifier="test_idle"),
        )
        self._assert_fails(_source(), expectations=casefold_duplicate)
        with self.assertRaises(TypeError):
            strip_proven_header_only_animations(  # type: ignore[arg-type]
                bytearray(_source()),
                virtual_path="art/w3d/test_model.w3d",
                expectations=(_expectation(),),
            )
        with self.assertRaises(TypeError):
            strip_proven_header_only_animations(
                _source(),
                virtual_path="art/w3d/test_model.w3d",
                expectations=(object(),),  # type: ignore[arg-type]
            )
        with self.assertRaises(TypeError):
            strip_proven_header_only_animations(
                _source(),
                virtual_path="art/w3d/test_model.w3d",
                expectations=(replace(_expectation(), compressed=1),),  # type: ignore[arg-type]
            )

    def test_source_and_chunk_bounds_are_hard(self) -> None:
        with patch(
            "openbfme_importer.w3d_no_motion.MAX_W3D_BYTES",
            len(_source()) - 1,
        ):
            self._assert_fails(_source(), message="byte no-motion limit")
        oversized_chunk = struct.pack("<II", 0x12345678, 100) + b"small"
        self._assert_fails(oversized_chunk, message="owner boundary")


if __name__ == "__main__":
    unittest.main()
