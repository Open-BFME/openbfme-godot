from __future__ import annotations

from dataclasses import FrozenInstanceError
import hashlib
import json
import math
import struct
import unittest
from unittest import mock

from openbfme_importer.w3d_emitter_streams import (
    SUPPORTED_EMITTER_CHILD_CHUNK_IDS,
    UNSUPPORTED_EMITTER_CHILD_CHUNK_IDS,
    W3DEmitterDecodeError,
    W3DEmitterExtraInfoRecord,
    W3DEmitterHeaderRecord,
    W3DEmitterInfoRecord,
    W3DEmitterInfoV2Record,
    W3DEmitterKeyframeSetRecord,
    W3DEmitterPropertiesRecord,
    W3DEmitterUnsupportedError,
    W3DEmitterUserDataRecord,
    decode_emitter_child,
    decode_emitter_container,
    decode_emitter_file,
)


def _fixed(value: bytes, size: int) -> bytes:
    if len(value) >= size:
        raise ValueError("fixture string must leave room for a terminator")
    return value + b"\0" + b"\0" * (size - len(value) - 1)


def _chunk(
    chunk_id: int,
    payload: bytes,
    *,
    children: bool = False,
    declared_size: int | None = None,
) -> bytes:
    size = len(payload) if declared_size is None else declared_size
    raw_size = size | (0x80000000 if children else 0)
    return struct.pack("<II", chunk_id, raw_size) + payload


def _randomizer(
    class_id: int = 0,
    values: tuple[float, float, float] = (1.0, 2.0, 3.0),
    reserved: tuple[int, int, int, int] = (0, 0, 0, 0),
) -> bytes:
    return struct.pack("<I3f4I", class_id, *values, *reserved)


def _header_payload(*, version: int = 0x00020000) -> bytes:
    return struct.pack("<I", version) + _fixed(b"private-emitter", 16)


def _user_payload(
    *,
    user_type: int = 0,
    value: bytes = b"private-user",
    padding: bytes = b"\0\0\0\0",
) -> bytes:
    encoded = value + b"\0" if value else b""
    return struct.pack("<II", user_type, len(encoded)) + padding + encoded


def _info_payload() -> bytes:
    scalar_values = (1.0, 2.0, 3.0, 4.0, 5.0, 0.1, 0.2, 0.3, -9.8, 0.5)
    vector_values = (1.0, 2.0, 3.0, -1.0, -2.0, -3.0)
    colors = (1, 2, 3, 4, 5, 6, 7, 8)
    return _fixed(b"private-texture.tga", 260) + struct.pack(
        "<10f6f8B", *scalar_values, *vector_values, *colors
    )


def _info_v2_payload(
    *,
    creation: bytes | None = None,
    velocity: bytes | None = None,
    shader: bytes | None = None,
    render_mode: int = 1,
    frame_mode: int = 1,
    reserved: tuple[int, int, int, int, int, int] = (0, 0, 0, 0, 0, 0),
) -> bytes:
    creation = _randomizer() if creation is None else creation
    velocity = _randomizer(1, (2.0, 0.0, 0.0)) if velocity is None else velocity
    shader = (
        bytes((3, 1, 0, 5, 0, 1, 0, 2, 1, 0, 0, 0, 1, 0, 0, 0))
        if shader is None
        else shader
    )
    return (
        struct.pack("<I", 1)
        + creation
        + velocity
        + struct.pack("<2f", 0.25, 0.5)
        + shader
        + struct.pack("<2I6I", render_mode, frame_mode, *reserved)
    )


def _properties_payload(
    *,
    color_keys: tuple[tuple[float, tuple[int, int, int, int]], ...] = (
        (0.0, (10, 20, 30, 40)),
        (2.0, (50, 60, 70, 80)),
    ),
    opacity_keys: tuple[tuple[float, float], ...] = ((0.0, 1.0), (2.0, 0.0)),
    size_keys: tuple[tuple[float, float], ...] = ((0.0, 1.0), (2.0, 4.0)),
    reserved: tuple[int, int, int, int] = (0, 0, 0, 0),
) -> bytes:
    result = struct.pack(
        "<3I4B2f4I",
        len(color_keys),
        len(opacity_keys),
        len(size_keys),
        1,
        2,
        3,
        4,
        0.1,
        0.2,
        *reserved,
    )
    result += b"".join(
        struct.pack("<f4B", time, *color) for time, color in color_keys
    )
    result += b"".join(struct.pack("<2f", *item) for item in opacity_keys)
    result += b"".join(struct.pack("<2f", *item) for item in size_keys)
    return result


def _line_payload(
    *,
    flags: int = 0x01000001,
    values: tuple[float, float, float, float, float] = (1.0, 2.0, 3.0, -1.0, 1.0),
    reserved: tuple[int, int, int, int, int, int, int, int, int] = (
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    ),
) -> bytes:
    return struct.pack("<2I5f9I", flags, 2, *values, *reserved)


def _rotation_payload(
    *,
    keys: tuple[tuple[float, float], ...] = ((0.0, 0.0), (2.0, 1.0)),
    reserved: int = 0xDEADBEEF,
) -> bytes:
    return struct.pack("<I2fI", len(keys) - 1, 0.1, 0.25, reserved) + b"".join(
        struct.pack("<2f", *item) for item in keys
    )


def _frame_payload(
    *,
    keys: tuple[tuple[float, float], ...] = ((0.0, 0.0), (2.0, 3.0)),
    reserved: tuple[int, int] = (0x12345678, 0x90ABCDEF),
) -> bytes:
    return struct.pack("<If2I", len(keys) - 1, 0.0, *reserved) + b"".join(
        struct.pack("<2f", *item) for item in keys
    )


def _blur_payload(
    *,
    keys: tuple[tuple[float, float], ...] = ((0.0, 0.0),),
    reserved: int = 0x01020304,
) -> bytes:
    return struct.pack("<IfI", len(keys) - 1, 0.0, reserved) + b"".join(
        struct.pack("<2f", *item) for item in keys
    )


def _extra_info_payload(
    *,
    future_start_time_seconds: float = 1.25,
    padding: tuple[int, int, int, int, int, int, int, int, int] = (
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    ),
) -> bytes:
    return struct.pack("<f9I", future_start_time_seconds, *padding)


def _payloads() -> dict[int, bytes]:
    return {
        0x501: _header_payload(),
        0x502: _user_payload(),
        0x503: _info_payload(),
        0x504: _info_v2_payload(),
        0x505: _properties_payload(),
        0x509: _line_payload(),
        0x50A: _rotation_payload(),
        0x50B: _frame_payload(),
        0x50C: _blur_payload(),
        0x50D: _extra_info_payload(),
    }


def _container_payload(
    payloads: dict[int, bytes] | None = None,
    *,
    order: tuple[int, ...] = (0x501, 0x502, 0x503, 0x504, 0x505, 0x509, 0x50A, 0x50B, 0x50C, 0x50D),
) -> bytes:
    values = _payloads() if payloads is None else payloads
    return b"".join(_chunk(chunk_id, values[chunk_id]) for chunk_id in order)


def _emitter_file(payload: bytes | None = None, *, children: bool = True) -> bytes:
    return _chunk(
        0x500,
        _container_payload() if payload is None else payload,
        children=children,
    )


class W3DEmitterPositiveTests(unittest.TestCase):
    def test_supported_and_terminal_sets_are_exact(self) -> None:
        self.assertEqual(
            SUPPORTED_EMITTER_CHILD_CHUNK_IDS,
            {
                0x501,
                0x502,
                0x503,
                0x504,
                0x505,
                0x509,
                0x50A,
                0x50B,
                0x50C,
                0x50D,
            },
        )
        self.assertEqual(UNSUPPORTED_EMITTER_CHILD_CHUNK_IDS, set())

    def test_decodes_complete_file_without_revealing_authored_strings(self) -> None:
        source = _emitter_file()
        decoded = decode_emitter_file(source)

        self.assertEqual(decoded.source_byte_length, len(source))
        self.assertEqual(decoded.source_sha256, hashlib.sha256(source).hexdigest())
        self.assertEqual(decoded.top_level_chunk_count, 1)
        self.assertEqual(len(decoded.emitters), 1)
        emitter = decoded.emitters[0]
        self.assertEqual(len(emitter.children), 10)
        self.assertEqual(emitter.decoded_child_count, 10)
        self.assertEqual(emitter.unsupported_terminal_count, 0)
        self.assertTrue(emitter.conversion_ready)

        header = emitter.children[0].record()
        user = emitter.children[1].record()
        info = emitter.children[2].record()
        info_v2 = emitter.children[3].record()
        properties = emitter.children[4].record()
        extra_info = emitter.children[9].record()
        self.assertIsInstance(header, W3DEmitterHeaderRecord)
        self.assertEqual(header.version, 0x00020000)  # type: ignore[union-attr]
        self.assertIsInstance(user, W3DEmitterUserDataRecord)
        self.assertEqual(user.declared_string_byte_length, 13)  # type: ignore[union-attr]
        self.assertIsInstance(info, W3DEmitterInfoRecord)
        self.assertEqual(info.velocity, (1.0, 2.0, 3.0))  # type: ignore[union-attr]
        self.assertIsInstance(info_v2, W3DEmitterInfoV2Record)
        self.assertEqual(info_v2.frame_mode, 1)  # type: ignore[union-attr]
        self.assertIsInstance(properties, W3DEmitterPropertiesRecord)
        self.assertEqual(properties.color_keyframe_count, 2)  # type: ignore[union-attr]
        self.assertIsInstance(extra_info, W3DEmitterExtraInfoRecord)
        self.assertEqual(extra_info.future_start_time_seconds, 1.25)  # type: ignore[union-attr]
        self.assertEqual(extra_info.padding, (0,) * 9)  # type: ignore[union-attr]

        neutral = json.dumps(decoded.neutral(), sort_keys=True)
        for secret in ("private-emitter", "private-user", "private-texture"):
            self.assertNotIn(secret, neutral)
        self.assertIn("contentSha256", neutral)

    def test_declared_reserved_words_are_preserved_not_guessed(self) -> None:
        user = decode_emitter_child(
            0x502,
            _user_payload(padding=b"\x01\x02\x03\x04"),
        ).record()
        rotation = decode_emitter_child(0x50A, _rotation_payload()).record()
        frame = decode_emitter_child(0x50B, _frame_payload()).record()
        blur = decode_emitter_child(0x50C, _blur_payload()).record()

        self.assertIsInstance(user, W3DEmitterUserDataRecord)
        self.assertEqual(user.struct_padding_nonzero_byte_count, 4)  # type: ignore[union-attr]
        self.assertIsInstance(rotation, W3DEmitterKeyframeSetRecord)
        self.assertEqual(rotation.reserved, (0xDEADBEEF,))  # type: ignore[union-attr]
        self.assertEqual(frame.reserved, (0x12345678, 0x90ABCDEF))  # type: ignore[union-attr]
        self.assertEqual(blur.reserved, (0x01020304,))  # type: ignore[union-attr]

    def test_extra_info_preserves_exact_source_record_and_hash(self) -> None:
        payload = _extra_info_payload(future_start_time_seconds=2.5)
        decoded = decode_emitter_child(0x50D, payload)
        record = decoded.record()

        self.assertEqual(decoded.status, "decoded")
        self.assertEqual(decoded.record_layout, "<f9I")
        self.assertEqual(decoded.payload_byte_length, 40)
        self.assertEqual(decoded.payload_sha256, hashlib.sha256(payload).hexdigest())
        self.assertEqual(decoded.canonical_record_sha256, decoded.payload_sha256)
        self.assertIsInstance(record, W3DEmitterExtraInfoRecord)
        self.assertEqual(record.future_start_time_seconds, 2.5)  # type: ignore[union-attr]
        self.assertEqual(record.padding, (0,) * 9)  # type: ignore[union-attr]


class W3DEmitterChildValidationTests(unittest.TestCase):
    def test_header_requires_exact_size_current_version_and_terminator(self) -> None:
        with self.assertRaisesRegex(W3DEmitterDecodeError, "exactly 20"):
            decode_emitter_child(0x501, _header_payload()[:-1])
        with self.assertRaisesRegex(W3DEmitterDecodeError, "version"):
            decode_emitter_child(0x501, _header_payload(version=0x00010000))
        with self.assertRaisesRegex(W3DEmitterDecodeError, "NUL"):
            decode_emitter_child(0x501, struct.pack("<I", 0x00020000) + b"x" * 16)

    def test_user_data_validates_enum_declared_size_and_terminator(self) -> None:
        with self.assertRaisesRegex(W3DEmitterDecodeError, "outside"):
            decode_emitter_child(0x502, _user_payload(user_type=1))
        with self.assertRaisesRegex(W3DEmitterDecodeError, "exactly"):
            decode_emitter_child(0x502, _user_payload()[:-1])
        bad = struct.pack("<II4s", 0, 3, b"\0" * 4) + b"abc"
        with self.assertRaisesRegex(W3DEmitterDecodeError, "final NUL"):
            decode_emitter_child(0x502, bad)

    def test_info_rejects_nonfinite_and_attests_signed_ranges(self) -> None:
        with self.assertRaisesRegex(W3DEmitterDecodeError, "exactly 332"):
            decode_emitter_child(0x503, _info_payload()[:-1])
        nonfinite = bytearray(_info_payload())
        struct.pack_into("<f", nonfinite, 260, math.nan)
        with self.assertRaisesRegex(W3DEmitterDecodeError, "non-finite"):
            decode_emitter_child(0x503, bytes(nonfinite))
        negative = bytearray(_info_payload())
        struct.pack_into("<f", negative, 260, -1.0)
        decoded = decode_emitter_child(0x503, bytes(negative))
        start_size = next(item for item in decoded.bounds if item.name == "startSize")
        self.assertEqual((start_size.minimum, start_size.maximum), (-1.0, -1.0))

    def test_info_v2_validates_randomizers_shader_and_enums(self) -> None:
        bad_class = _info_v2_payload(creation=_randomizer(4))
        with self.assertRaisesRegex(W3DEmitterDecodeError, "class 4"):
            decode_emitter_child(0x504, bad_class)
        bad_extent = _info_v2_payload(creation=_randomizer(0, (-1.0, 0.0, 0.0)))
        decoded_extent = decode_emitter_child(0x504, bad_extent)
        extent_bound = next(
            item for item in decoded_extent.bounds if item.name == "randomizerValue"
        )
        self.assertEqual(extent_bound.minimum, -1.0)
        shader = bytearray(_info_v2_payload())
        shader[76] = 8
        with self.assertRaisesRegex(W3DEmitterDecodeError, "field 0"):
            decode_emitter_child(0x504, bytes(shader))
        shader_padding = bytearray(_info_v2_payload())
        shader_padding[91] = 1
        with self.assertRaisesRegex(W3DEmitterDecodeError, "padding"):
            decode_emitter_child(0x504, bytes(shader_padding))
        with self.assertRaisesRegex(W3DEmitterDecodeError, "render mode"):
            decode_emitter_child(0x504, _info_v2_payload(render_mode=5))
        with self.assertRaisesRegex(W3DEmitterDecodeError, "frame mode"):
            decode_emitter_child(0x504, _info_v2_payload(frame_mode=5))

    def test_properties_validate_sizes_counts_times_values_and_finiteness(self) -> None:
        with self.assertRaisesRegex(W3DEmitterDecodeError, "exactly"):
            decode_emitter_child(0x505, _properties_payload()[:-1])
        zero_count = bytearray(_properties_payload())
        struct.pack_into("<I", zero_count, 0, 0)
        with self.assertRaisesRegex(W3DEmitterDecodeError, "key counts"):
            decode_emitter_child(0x505, bytes(zero_count))
        nonzero_start = _properties_payload(
            color_keys=((1.0, (1, 2, 3, 4)),),
            opacity_keys=((0.0, 1.0),),
            size_keys=((0.0, 1.0),),
        )
        with self.assertRaisesRegex(W3DEmitterDecodeError, "start key time"):
            decode_emitter_child(0x505, nonzero_start)
        descending = _properties_payload(
            color_keys=((0.0, (1, 2, 3, 4)), (0.0, (5, 6, 7, 8))),
        )
        with self.assertRaisesRegex(W3DEmitterDecodeError, "strictly increasing"):
            decode_emitter_child(0x505, descending)
        bad_opacity = _properties_payload(opacity_keys=((0.0, 1.5),))
        opacity = decode_emitter_child(0x505, bad_opacity)
        opacity_bound = next(item for item in opacity.bounds if item.name == "opacity")
        self.assertEqual(opacity_bound.maximum, 1.5)
        bad_size = _properties_payload(size_keys=((0.0, -1.0),))
        size = decode_emitter_child(0x505, bad_size)
        size_bound = next(item for item in size.bounds if item.name == "size")
        self.assertEqual(size_bound.minimum, -1.0)

    def test_line_properties_validate_flags_modes_ranges_and_finiteness(self) -> None:
        with self.assertRaisesRegex(W3DEmitterDecodeError, "unknown flag"):
            decode_emitter_child(0x509, _line_payload(flags=0x10))
        with self.assertRaisesRegex(W3DEmitterDecodeError, "mode 3"):
            decode_emitter_child(0x509, _line_payload(flags=0x03000000))
        signed = decode_emitter_child(
            0x509,
            _line_payload(values=(-1.0, 0.0, 0.0, 0.0, 0.0)),
        )
        signed_bound = next(item for item in signed.bounds if item.name == "lineScalar")
        self.assertEqual(signed_bound.minimum, -1.0)
        with self.assertRaisesRegex(W3DEmitterDecodeError, "non-finite"):
            decode_emitter_child(
                0x509,
                _line_payload(values=(0.0, 0.0, 0.0, math.inf, 0.0)),
            )

    def test_scalar_keyframes_bind_count_start_time_order_and_ranges(self) -> None:
        with self.assertRaisesRegex(W3DEmitterDecodeError, "exactly"):
            decode_emitter_child(0x50A, _rotation_payload()[:-8])
        with self.assertRaisesRegex(W3DEmitterDecodeError, "start key time"):
            decode_emitter_child(0x50A, _rotation_payload(keys=((1.0, 0.0),)))
        with self.assertRaisesRegex(W3DEmitterDecodeError, "strictly increasing"):
            decode_emitter_child(
                0x50A,
                _rotation_payload(keys=((0.0, 0.0), (0.0, 1.0))),
            )
        with self.assertRaisesRegex(W3DEmitterDecodeError, "non-finite"):
            decode_emitter_child(0x50A, _rotation_payload(keys=((0.0, math.inf),)))
        frame = decode_emitter_child(0x50B, _frame_payload(keys=((0.0, -1.0),)))
        blur = decode_emitter_child(0x50C, _blur_payload(keys=((0.0, -1.0),)))
        frame_bound = next(item for item in frame.bounds if item.name == "keyValue")
        blur_bound = next(item for item in blur.bounds if item.name == "keyValue")
        self.assertEqual(frame_bound.minimum, -1.0)
        self.assertEqual(blur_bound.minimum, -1.0)

    def test_extra_info_validates_size_time_domain_and_padding(self) -> None:
        with self.assertRaisesRegex(W3DEmitterDecodeError, "exactly 40"):
            decode_emitter_child(0x50D, b"\0" * 39)
        with self.assertRaisesRegex(W3DEmitterDecodeError, "non-finite"):
            decode_emitter_child(
                0x50D,
                _extra_info_payload(future_start_time_seconds=math.nan),
            )
        with self.assertRaisesRegex(W3DEmitterDecodeError, "negative"):
            decode_emitter_child(
                0x50D,
                _extra_info_payload(future_start_time_seconds=-0.25),
            )
        with self.assertRaisesRegex(W3DEmitterDecodeError, "millisecond range"):
            decode_emitter_child(
                0x50D,
                _extra_info_payload(future_start_time_seconds=4_300_000.0),
            )
        with self.assertRaisesRegex(W3DEmitterDecodeError, "format extension"):
            decode_emitter_child(
                0x50D,
                _extra_info_payload(padding=(1, 0, 0, 0, 0, 0, 0, 0, 0)),
            )
        with self.assertRaises(W3DEmitterUnsupportedError):
            decode_emitter_child(0x506, b"")


class W3DEmitterContainmentTests(unittest.TestCase):
    def test_rejects_flagged_truncated_unknown_and_duplicate_children(self) -> None:
        payloads = _payloads()
        flagged = _chunk(0x501, payloads[0x501], children=True) + _container_payload(
            payloads,
            order=(0x502, 0x503, 0x504, 0x505),
        )
        with self.assertRaisesRegex(W3DEmitterDecodeError, "cannot contain"):
            decode_emitter_container(flagged)

        truncated = _chunk(0x501, payloads[0x501], declared_size=100)
        with self.assertRaisesRegex(W3DEmitterDecodeError, "parent boundary"):
            decode_emitter_container(truncated)

        unknown = _container_payload(payloads, order=(0x501, 0x502, 0x503, 0x504, 0x505))
        unknown += _chunk(0x506, b"")
        with self.assertRaises(W3DEmitterUnsupportedError):
            decode_emitter_container(unknown)

        duplicate = _container_payload(payloads, order=(0x501, 0x502, 0x503, 0x504, 0x505))
        duplicate += _chunk(0x505, payloads[0x505])
        with self.assertRaisesRegex(W3DEmitterDecodeError, "duplicated"):
            decode_emitter_container(duplicate)

    def test_requires_mandatory_prefix_and_cross_validates_frame_grid(self) -> None:
        payloads = _payloads()
        reordered = _container_payload(
            payloads,
            order=(0x502, 0x501, 0x503, 0x504, 0x505),
        )
        with self.assertRaisesRegex(W3DEmitterDecodeError, "mandatory children"):
            decode_emitter_container(reordered)

        payloads[0x50B] = _frame_payload(keys=((0.0, 4.0),))
        with self.assertRaisesRegex(W3DEmitterDecodeError, "texture grid"):
            decode_emitter_container(_container_payload(payloads))

    def test_complete_file_validates_root_and_top_level_boundaries(self) -> None:
        with self.assertRaisesRegex(W3DEmitterDecodeError, "subchunk flag"):
            decode_emitter_file(_emitter_file(children=False))
        with self.assertRaisesRegex(W3DEmitterDecodeError, "source boundary"):
            decode_emitter_file(_chunk(0x500, b"", children=True, declared_size=1))
        with self.assertRaisesRegex(W3DEmitterDecodeError, "no emitter"):
            decode_emitter_file(_chunk(0x1F, b""))
        source = _chunk(0x1F, b"") + _emitter_file()
        self.assertEqual(decode_emitter_file(source).top_level_chunk_count, 2)

    def test_contracts_are_deterministic_immutable_and_bounded(self) -> None:
        source = _emitter_file()
        first = decode_emitter_file(source)
        second = decode_emitter_file(source)
        self.assertEqual(first.neutral(), second.neutral())
        with self.assertRaises(FrozenInstanceError):
            first.source_byte_length = 0  # type: ignore[misc]
        with self.assertRaises(TypeError):
            decode_emitter_file(bytearray(source))  # type: ignore[arg-type]
        with self.assertRaisesRegex(W3DEmitterDecodeError, "unsigned"):
            decode_emitter_child(True, b"")  # type: ignore[arg-type]
        with mock.patch(
            "openbfme_importer.w3d_emitter_streams.MAX_EMITTER_SOURCE_BYTES",
            1,
        ):
            with self.assertRaisesRegex(W3DEmitterDecodeError, "byte limit"):
                decode_emitter_file(b"12")


if __name__ == "__main__":
    unittest.main()
