from __future__ import annotations

from dataclasses import FrozenInstanceError
import hashlib
import json
import math
import struct
import unittest
from unittest import mock

from openbfme_importer.w3d_animation_streams import (
    W3D_CHUNK_ANIMATION_BIT_CHANNEL,
    W3D_CHUNK_ANIMATION_CHANNEL,
    W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
    W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL,
    W3D_CHUNK_COMPRESSED_BIT_CHANNEL,
    W3D_COMPRESSED_FLAVOR_ADAPTIVE_DELTA,
    W3D_COMPRESSED_FLAVOR_TIME_CODED,
    W3DAnimationBitKey,
    W3DAnimationKey,
    W3DAnimationStreamDecodeError,
    W3DAnimationStreamUnsupportedError,
    decode_animation_stream,
    decode_compressed_animation_bit_channel,
    decode_compressed_animation_channel,
    decode_motion_animation_channel,
    decode_raw_animation_bit_channel,
    decode_raw_animation_channel,
)


def _range(result, name: str):
    item = next(value for value in result.attestation.ranges if value.name == name)
    return item.minimum, item.maximum


def _adaptive_scalar_payload(
    *,
    key_count: int = 3,
    scale: float = 0.5,
    initial: float = 1.0,
) -> bytes:
    header = struct.pack("<IHBBf", key_count, 1, 1, 0, scale)
    block = struct.pack("<B8b", 16, 0x21, 0, 0, 0, 0, 0, 0, 0)
    return header + struct.pack("<f", initial) + block + b"\x00\x00\x00"


class RawAnimationChannelTests(unittest.TestCase):
    def test_scalar_layout_bounds_immutability_and_payload_free_attestation(
        self,
    ) -> None:
        payload = struct.pack("<6H3f", 2, 4, 1, 0, 1, 0, -1.5, 0.0, 3.25)

        result = decode_raw_animation_channel(
            payload,
            animation_frame_count=8,
            pivot_count=3,
        )

        self.assertEqual(result.channel.stream_kind, "raw-channel")
        self.assertEqual(result.channel.channel_type_name, "x-translation")
        self.assertEqual(
            result.channel.keys,
            (
                W3DAnimationKey(2, -1.5),
                W3DAnimationKey(3, 0.0),
                W3DAnimationKey(4, 3.25),
            ),
        )
        self.assertEqual(_range(result, "frame"), (2, 4))
        self.assertEqual(_range(result, "value"), (-1.5, 3.25))
        self.assertEqual(
            result.attestation.payload_sha256,
            hashlib.sha256(payload).hexdigest(),
        )
        neutral = result.attestation.neutral()
        self.assertEqual(neutral["keyCount"], 3)
        self.assertTrue(neutral["pivotInRange"])
        self.assertEqual(neutral["primaryImportDisposition"], "bound")
        self.assertEqual(neutral["outOfOwnerFrameCount"], 0)
        self.assertEqual(
            neutral["outOfOwnerFrameRange"],
            {"minimum": None, "maximum": None},
        )
        self.assertNotIn("keys", neutral)
        self.assertNotIn("payload", neutral)
        self.assertNotIn("path", neutral)
        self.assertNotIn("name", neutral)
        json.dumps(result.attestation.json_ready(), sort_keys=True)
        with self.assertRaises(FrozenInstanceError):
            result.channel.pivot_index = 2  # type: ignore[misc]

    def test_quaternion_layout_validates_norm_and_attests_padding(self) -> None:
        payload = (
            struct.pack(
                "<6H4f",
                0,
                0,
                4,
                6,
                0,
                0,
                0.0,
                0.0,
                0.0,
                1.0,
            )
            + b"\xaa\xbb\xcc"
        )

        result = decode_raw_animation_channel(
            payload,
            animation_frame_count=1,
            pivot_count=1,
        )

        self.assertEqual(result.channel.vector_width, 4)
        self.assertEqual(result.channel.padding_length, 3)
        self.assertEqual(
            result.channel.padding_sha256,
            hashlib.sha256(b"\xaa\xbb\xcc").hexdigest(),
        )
        self.assertEqual(result.channel.keys[0].value, (0.0, 0.0, 0.0, 1.0))
        self.assertEqual(_range(result, "valueW"), (1.0, 1.0))

        not_normalized = bytearray(payload)
        struct.pack_into("<4f", not_normalized, 12, 0.0, 0.0, 0.0, 2.0)
        with self.assertRaisesRegex(
            W3DAnimationStreamDecodeError,
            "not normalized",
        ):
            decode_raw_animation_channel(
                bytes(not_normalized),
                animation_frame_count=1,
                pivot_count=1,
            )

    def test_raw_channel_rejects_truncation_header_frame_and_float(self) -> None:
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "truncated"):
            decode_raw_animation_channel(
                b"\x00" * 11,
                animation_frame_count=1,
                pivot_count=1,
            )
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "require at least"):
            decode_raw_animation_channel(
                struct.pack("<6H", 0, 1, 1, 0, 0, 0) + struct.pack("<f", 1.0),
                animation_frame_count=2,
                pivot_count=1,
            )
        skipped = decode_raw_animation_channel(
            struct.pack("<6Hf", 0, 0, 1, 0, 2, 0, 1.0),
            animation_frame_count=1,
            pivot_count=2,
        )
        self.assertFalse(skipped.attestation.pivot_in_range)
        self.assertEqual(
            skipped.attestation.primary_import_disposition,
            "skipped-invalid-pivot",
        )
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "frame 2"):
            decode_raw_animation_channel(
                struct.pack("<6Hf", 2, 2, 1, 0, 0, 0, 1.0),
                animation_frame_count=2,
                pivot_count=1,
            )
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "non-finite"):
            decode_raw_animation_channel(
                struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, math.nan),
                animation_frame_count=1,
                pivot_count=1,
            )

    def test_opaque_header_and_124_byte_tail_are_preserved_in_evidence(self) -> None:
        tail = bytes((index * 17 + 3) % 256 for index in range(124))
        payload = struct.pack("<6Hf", 0, 0, 1, 0, 0, 65535, 1.0) + tail

        first = decode_raw_animation_channel(
            payload,
            animation_frame_count=1,
            pivot_count=1,
        )
        repeated = decode_raw_animation_channel(
            payload,
            animation_frame_count=1,
            pivot_count=1,
        )

        self.assertEqual(first, repeated)
        self.assertEqual(first.channel.opaque_header_value, 65535)
        self.assertEqual(first.channel.padding_length, 124)
        self.assertEqual(first.channel.padding_sha256, hashlib.sha256(tail).hexdigest())
        self.assertEqual(first.attestation.opaque_header_value, 65535)
        self.assertEqual(first.attestation.padding_length, 124)
        self.assertEqual(
            first.attestation.padding_sha256, hashlib.sha256(tail).hexdigest()
        )
        neutral = first.attestation.neutral()
        self.assertEqual(neutral["opaqueHeaderValue"], 65535)
        self.assertEqual(neutral["paddingSha256"], hashlib.sha256(tail).hexdigest())
        self.assertNotIn(tail.hex(), json.dumps(neutral, sort_keys=True))

        changed_header = decode_raw_animation_channel(
            struct.pack("<6Hf", 0, 0, 1, 0, 0, 65534, 1.0) + tail,
            animation_frame_count=1,
            pivot_count=1,
        )
        changed_tail = decode_raw_animation_channel(
            payload[:-1] + bytes((payload[-1] ^ 0xFF,)),
            animation_frame_count=1,
            pivot_count=1,
        )
        self.assertNotEqual(
            first.attestation.canonical_record_sha256,
            changed_header.attestation.canonical_record_sha256,
        )
        self.assertNotEqual(
            first.attestation.canonical_record_sha256,
            changed_tail.attestation.canonical_record_sha256,
        )

    def test_raw_channel_retains_global_hard_byte_bound(self) -> None:
        payload = struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0)
        with mock.patch(
            "openbfme_importer.w3d_animation_streams.MAX_ANIMATION_STREAM_BYTES",
            len(payload) - 1,
        ):
            with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "byte limit"):
                decode_raw_animation_channel(
                    payload,
                    animation_frame_count=1,
                    pivot_count=1,
                )

    def test_unknown_type_and_wrong_vector_width_fail_closed(self) -> None:
        with self.assertRaisesRegex(
            W3DAnimationStreamUnsupportedError,
            "type 3",
        ):
            decode_raw_animation_channel(
                struct.pack("<6Hf", 0, 0, 1, 3, 0, 0, 1.0),
                animation_frame_count=1,
                pivot_count=1,
            )
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "vector width 4"):
            decode_raw_animation_channel(
                struct.pack("<6Hf", 0, 0, 1, 6, 0, 0, 1.0),
                animation_frame_count=1,
                pivot_count=1,
            )

    def test_canonical_hash_attests_padding_bytes(self) -> None:
        base = struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0)
        first = decode_raw_animation_channel(
            base + b"\x00\x00",
            animation_frame_count=1,
            pivot_count=1,
        )
        second = decode_raw_animation_channel(
            base + b"\xaa\xbb",
            animation_frame_count=1,
            pivot_count=1,
        )

        self.assertNotEqual(
            first.attestation.payload_sha256,
            second.attestation.payload_sha256,
        )
        self.assertNotEqual(
            first.attestation.canonical_record_sha256,
            second.attestation.canonical_record_sha256,
        )


class RawBitChannelTests(unittest.TestCase):
    def test_lsb_first_bits_default_and_unused_high_bits_are_attested(self) -> None:
        payload = struct.pack("<4HB", 1, 10, 0, 1, 128) + bytes(
            (0b10100101, 0b00000010)
        )

        result = decode_raw_animation_bit_channel(
            payload,
            animation_frame_count=12,
            pivot_count=2,
        )

        self.assertEqual(result.channel.stream_kind, "raw-bit-channel")
        self.assertAlmostEqual(result.channel.default_value, 128 / 255.0)
        self.assertEqual(result.channel.keys[0], W3DAnimationBitKey(1, True))
        self.assertEqual(result.channel.keys[1], W3DAnimationBitKey(2, False))
        self.assertEqual(result.channel.keys[-1], W3DAnimationBitKey(10, True))
        self.assertEqual(_range(result, "value"), (0, 1))
        self.assertEqual(result.channel.unused_bit_count, 6)
        self.assertEqual(result.channel.unused_bit_mask, 0b11111100)
        self.assertEqual(result.channel.unused_bit_value, 0)
        self.assertEqual(result.attestation.neutral()["unusedBitCount"], 6)

        high_bits = payload[:-1] + bytes((0b10101010,))
        changed = decode_raw_animation_bit_channel(
            high_bits,
            animation_frame_count=12,
            pivot_count=2,
        )
        repeated = decode_raw_animation_bit_channel(
            high_bits,
            animation_frame_count=12,
            pivot_count=2,
        )
        self.assertEqual(changed, repeated)
        self.assertEqual(changed.channel.keys, result.channel.keys)
        self.assertEqual(changed.channel.unused_bit_count, 6)
        self.assertEqual(changed.channel.unused_bit_mask, 0b11111100)
        self.assertEqual(changed.channel.unused_bit_value, 0b10101000)
        self.assertEqual(changed.attestation.unused_bit_value, 0b10101000)
        self.assertEqual(
            changed.attestation.neutral()["unusedBitValue"],
            0b10101000,
        )
        self.assertNotEqual(
            result.attestation.canonical_record_sha256,
            changed.attestation.canonical_record_sha256,
        )

    def test_raw_bit_rejects_exact_length_and_unknown_type(self) -> None:
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "exact"):
            decode_raw_animation_bit_channel(
                struct.pack("<4HB", 0, 0, 0, 0, 255),
                animation_frame_count=1,
                pivot_count=1,
            )
        with self.assertRaisesRegex(
            W3DAnimationStreamUnsupportedError,
            "bit-channel type 15",
        ):
            decode_raw_animation_bit_channel(
                struct.pack("<4HBB", 0, 0, 15, 0, 255, 1),
                animation_frame_count=1,
                pivot_count=1,
            )


class CompressedAnimationChannelTests(unittest.TestCase):
    def test_time_coded_scalar_layout_order_and_interpolation(self) -> None:
        payload = struct.pack("<IHBB", 3, 2, 1, 2) + b"".join(
            (
                struct.pack("<If", 0, -1.0),
                struct.pack("<If", 0x80000003, 2.0),
                struct.pack("<If", 7, 4.5),
            )
        )

        result = decode_compressed_animation_channel(
            payload,
            animation_frame_count=8,
            pivot_count=4,
            flavor=W3D_COMPRESSED_FLAVOR_TIME_CODED,
        )

        self.assertEqual(result.channel.stream_kind, "compressed-time-coded-channel")
        self.assertEqual([key.frame for key in result.channel.keys], [0, 3, 7])
        self.assertEqual(
            [key.interpolated for key in result.channel.keys],
            [False, True, False],
        )
        self.assertEqual(result.attestation.interpolation_key_count, 1)

        unordered = bytearray(payload)
        struct.pack_into("<I", unordered, 8 + 8, 0)
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "increasing"):
            decode_compressed_animation_channel(
                bytes(unordered),
                animation_frame_count=8,
                pivot_count=4,
                flavor=W3D_COMPRESSED_FLAVOR_TIME_CODED,
            )

    def test_time_coded_out_of_owner_frames_are_exactly_attested(self) -> None:
        payload = struct.pack("<IHBB", 3, 0, 1, 0) + b"".join(
            (
                struct.pack("<If", 0, 1.0),
                struct.pack("<If", 5, 2.0),
                struct.pack("<If", 9, 3.0),
            )
        )

        result = decode_compressed_animation_channel(
            payload,
            animation_frame_count=2,
            pivot_count=1,
            flavor=W3D_COMPRESSED_FLAVOR_TIME_CODED,
        )

        self.assertEqual([key.frame for key in result.channel.keys], [0, 5, 9])
        self.assertEqual(result.attestation.out_of_owner_frame_count, 2)
        self.assertEqual(result.attestation.out_of_owner_frame_minimum, 5)
        self.assertEqual(result.attestation.out_of_owner_frame_maximum, 9)
        self.assertEqual(
            result.attestation.neutral()["outOfOwnerFrameRange"],
            {"minimum": 5, "maximum": 9},
        )
        self.assertEqual(
            result.attestation.payload_sha256,
            hashlib.sha256(payload).hexdigest(),
        )

        changed = bytearray(payload)
        struct.pack_into("<I", changed, 8 + 2 * 8, 10)
        changed_result = decode_compressed_animation_channel(
            bytes(changed),
            animation_frame_count=2,
            pivot_count=1,
            flavor=W3D_COMPRESSED_FLAVOR_TIME_CODED,
        )
        self.assertNotEqual(
            result.attestation.payload_sha256,
            changed_result.attestation.payload_sha256,
        )
        self.assertNotEqual(
            result.attestation.canonical_record_sha256,
            changed_result.attestation.canonical_record_sha256,
        )

    def test_time_coded_quaternion_and_exact_length_are_validated(self) -> None:
        payload = struct.pack("<IHBBI4f", 1, 0, 4, 6, 2, 0.0, 0.0, 0.0, 1.0)

        result = decode_compressed_animation_channel(
            payload,
            animation_frame_count=3,
            pivot_count=1,
            flavor=W3D_COMPRESSED_FLAVOR_TIME_CODED,
        )
        self.assertEqual(result.channel.keys[0].frame, 2)
        self.assertEqual(result.channel.vector_width, 4)

        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "exact"):
            decode_compressed_animation_channel(
                payload[:-1],
                animation_frame_count=3,
                pivot_count=1,
                flavor=W3D_COMPRESSED_FLAVOR_TIME_CODED,
            )

    def test_adaptive_delta_four_bit_layout_expands_and_attests_blocks(self) -> None:
        payload = _adaptive_scalar_payload()

        result = decode_compressed_animation_channel(
            payload,
            animation_frame_count=5,
            pivot_count=2,
            flavor=W3D_COMPRESSED_FLAVOR_ADAPTIVE_DELTA,
        )

        self.assertEqual(
            result.channel.stream_kind,
            "compressed-adaptive-delta-channel",
        )
        self.assertEqual(result.channel.delta_bits, 4)
        self.assertEqual(len(result.channel.delta_blocks), 1)
        self.assertEqual(len(result.channel.keys), 3)
        self.assertEqual(result.channel.keys[0].value, 1.0)
        self.assertEqual(result.attestation.record_count, 2)
        self.assertEqual(result.attestation.key_count, 3)
        self.assertEqual(_range(result, "deltaBlockIndex"), (16, 16))
        self.assertEqual(
            result.attestation,
            decode_compressed_animation_channel(
                payload,
                animation_frame_count=5,
                pivot_count=2,
                flavor=W3D_COMPRESSED_FLAVOR_ADAPTIVE_DELTA,
            ).attestation,
        )

    def test_adaptive_rejects_truncation_nonfinite_and_bad_initial_rotation(
        self,
    ) -> None:
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "exact"):
            decode_compressed_animation_channel(
                _adaptive_scalar_payload()[:-1],
                animation_frame_count=5,
                pivot_count=2,
                flavor=W3D_COMPRESSED_FLAVOR_ADAPTIVE_DELTA,
            )
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "non-finite"):
            decode_compressed_animation_channel(
                _adaptive_scalar_payload(scale=math.inf),
                animation_frame_count=5,
                pivot_count=2,
                flavor=W3D_COMPRESSED_FLAVOR_ADAPTIVE_DELTA,
            )

        header = struct.pack("<IHBBf", 1, 0, 4, 6, 0.0)
        initial = struct.pack("<4f", 0.0, 0.0, 0.0, 0.0)
        blocks = b"".join(struct.pack("<B8b", 0, *([0] * 8)) for _ in range(4))
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "not normalized"):
            decode_compressed_animation_channel(
                header + initial + blocks + b"\x00\x00\x00",
                animation_frame_count=1,
                pivot_count=1,
                flavor=W3D_COMPRESSED_FLAVOR_ADAPTIVE_DELTA,
            )


class CompressedBitAndMotionTests(unittest.TestCase):
    def test_invalid_pivots_are_primary_skips_for_282_283_and_284(self) -> None:
        compressed = struct.pack("<IHBBIf", 1, 2, 1, 0, 0, 1.0)
        compressed_bit = struct.pack("<IhBBI", 1, -1, 0, 1, 0)
        motion = struct.pack("<4Bhhh", 0, 0, 1, 0, 1, -1, 0)
        motion += b"\x00\x00" + struct.pack("<f", 1.0)
        cases = (
            (
                W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
                compressed,
                W3D_COMPRESSED_FLAVOR_TIME_CODED,
            ),
            (W3D_CHUNK_COMPRESSED_BIT_CHANNEL, compressed_bit, None),
            (W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL, motion, None),
        )

        for chunk_id, payload, flavor in cases:
            with self.subTest(chunk_id=chunk_id):
                result = decode_animation_stream(
                    chunk_id,
                    payload,
                    animation_frame_count=1,
                    pivot_count=2,
                    compressed_flavor=flavor,
                )
                self.assertFalse(result.channel.pivot_in_range)
                self.assertFalse(result.attestation.pivot_in_range)
                self.assertEqual(
                    result.attestation.primary_import_disposition,
                    "skipped-invalid-pivot",
                )
                self.assertEqual(
                    result.attestation.payload_sha256,
                    hashlib.sha256(payload).hexdigest(),
                )

        pivot_mutation = bytearray(compressed)
        struct.pack_into("<H", pivot_mutation, 4, 3)
        changed = decode_compressed_animation_channel(
            bytes(pivot_mutation),
            animation_frame_count=1,
            pivot_count=2,
            flavor=W3D_COMPRESSED_FLAVOR_TIME_CODED,
        )
        original = decode_compressed_animation_channel(
            compressed,
            animation_frame_count=1,
            pivot_count=2,
            flavor=W3D_COMPRESSED_FLAVOR_TIME_CODED,
        )
        self.assertNotEqual(
            original.attestation.canonical_record_sha256,
            changed.attestation.canonical_record_sha256,
        )

    def test_compressed_bit_channel_uses_high_value_bit_and_allows_default_only(
        self,
    ) -> None:
        payload = struct.pack("<IhBB3I", 3, 1, 0, 12, 0, 0x80000002, 5)
        result = decode_compressed_animation_bit_channel(
            payload,
            animation_frame_count=6,
            pivot_count=2,
        )

        self.assertEqual(
            result.channel.stream_kind, "compressed-time-coded-bit-channel"
        )
        self.assertEqual(result.channel.default_value, 12)
        self.assertEqual(
            result.channel.keys,
            (
                W3DAnimationBitKey(0, False),
                W3DAnimationBitKey(2, True),
                W3DAnimationBitKey(5, False),
            ),
        )

        default_only = decode_compressed_animation_bit_channel(
            struct.pack("<IhBB", 0, 0, 0, 1),
            animation_frame_count=1,
            pivot_count=1,
        )
        self.assertEqual(default_only.channel.keys, ())
        self.assertIsNone(default_only.attestation.first_frame)

    def test_motion_time_coded_layout_has_separate_times_alignment_and_values(
        self,
    ) -> None:
        header = struct.pack("<4Bhh", 0, 0, 1, 1, 3, 1)
        payload = header + struct.pack("<3h", 0, 2, 4) + b"\x00\x00"
        payload += struct.pack("<3f", -2.0, 0.5, 7.0)

        result = decode_motion_animation_channel(
            payload,
            animation_frame_count=5,
            pivot_count=2,
        )

        self.assertEqual(result.channel.stream_kind, "motion-time-coded-channel")
        self.assertEqual(result.channel.padding_length, 2)
        self.assertEqual([key.frame for key in result.channel.keys], [0, 2, 4])
        self.assertTrue(all(key.interpolated for key in result.channel.keys))
        self.assertEqual([key.value for key in result.channel.keys], [-2.0, 0.5, 7.0])

    def test_motion_time_codes_outside_owner_range_remain_attested(self) -> None:
        payload = struct.pack("<4Bhh2h2f", 0, 0, 1, 0, 2, 0, 300, 400, 1.0, 2.0)

        result = decode_motion_animation_channel(
            payload,
            animation_frame_count=2,
            pivot_count=1,
        )

        self.assertEqual([key.frame for key in result.channel.keys], [300, 400])
        self.assertEqual(result.attestation.out_of_owner_frame_count, 2)
        self.assertEqual(result.attestation.out_of_owner_frame_minimum, 300)
        self.assertEqual(result.attestation.out_of_owner_frame_maximum, 400)
        self.assertEqual(
            result.attestation.neutral()["outOfOwnerFrameRange"],
            {"minimum": 300, "maximum": 400},
        )

    def test_motion_time_codes_mask_binary_movement_flag(self) -> None:
        # Retail motion channels set the high bit of a 16-bit time code for
        # step transitions (W3D_TIMECODED_BINARY_MOVEMENT_FLAG).  The ordered
        # frame is the low 15 bits, so a flagged key must not read as a
        # negative or far-future frame.
        header = struct.pack("<4Bhh", 0, 0, 1, 15, 5, 1)
        payload = header + struct.pack("<5H", 0, 19, 0x8000 | 20, 21, 64)
        payload += b"\x00\x00"
        payload += struct.pack("<5f", 0.0, 0.25, 0.5, 0.75, 1.0)

        result = decode_motion_animation_channel(
            payload,
            animation_frame_count=65,
            pivot_count=2,
        )

        self.assertEqual(
            [key.frame for key in result.channel.keys], [0, 19, 20, 21, 64]
        )
        self.assertTrue(all(key.interpolated for key in result.channel.keys))
        self.assertEqual(
            [key.value for key in result.channel.keys], [0.0, 0.25, 0.5, 0.75, 1.0]
        )
        self.assertEqual(result.attestation.out_of_owner_frame_count, 0)
        self.assertEqual(result.channel.last_frame, 64)

    def test_motion_time_codes_reject_unordered_masked_frames(self) -> None:
        header = struct.pack("<4Bhh", 0, 0, 1, 0, 2, 0)
        payload = header + struct.pack("<2H2f", 30, 0x8000 | 29, 1.0, 2.0)

        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "increasing"):
            decode_motion_animation_channel(
                payload,
                animation_frame_count=64,
                pivot_count=1,
            )

    def test_motion_eight_bit_adaptive_layout_is_distinct_and_bounded(self) -> None:
        header = struct.pack("<4Bhhf", 0, 2, 1, 0, 2, 0, 0.25)
        initial = struct.pack("<f", 1.0)
        block = struct.pack("<B16b", 32, *([0] * 16))

        result = decode_motion_animation_channel(
            header + initial + block,
            animation_frame_count=2,
            pivot_count=1,
        )

        self.assertEqual(result.channel.stream_kind, "motion-adaptive-delta-channel")
        self.assertEqual(result.channel.delta_bits, 8)
        self.assertEqual(result.attestation.delta_block_count, 1)
        self.assertEqual(len(result.channel.keys), 2)
        self.assertTrue(all(math.isfinite(key.value) for key in result.channel.keys))

    def test_motion_rejects_reserved_unknown_delta_bad_time_target_and_float(
        self,
    ) -> None:
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "reserved"):
            decode_motion_animation_channel(
                struct.pack("<4Bhhhf", 1, 0, 1, 0, 1, 0, 0, 1.0),
                animation_frame_count=1,
                pivot_count=1,
            )
        with self.assertRaisesRegex(
            W3DAnimationStreamUnsupportedError,
            "delta type 3",
        ):
            decode_motion_animation_channel(
                struct.pack("<4Bhh", 0, 3, 1, 0, 1, 0),
                animation_frame_count=1,
                pivot_count=1,
            )
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "increasing"):
            payload = struct.pack("<4Bhh", 0, 0, 1, 0, 2, 0)
            payload += struct.pack("<2h2f", 1, 0, 1.0, 2.0)
            decode_motion_animation_channel(
                payload,
                animation_frame_count=2,
                pivot_count=1,
            )
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "non-finite"):
            payload = struct.pack("<4Bhhh", 0, 0, 1, 0, 1, 0, 0)
            payload += b"\x00\x00" + struct.pack("<f", math.inf)
            decode_motion_animation_channel(
                payload,
                animation_frame_count=1,
                pivot_count=1,
            )


class AnimationDispatchTests(unittest.TestCase):
    def test_dispatch_preserves_all_five_chunk_kinds(self) -> None:
        raw = struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0)
        raw_bit = struct.pack("<4HBB", 0, 0, 0, 0, 255, 1)
        compressed = struct.pack("<IHBBIf", 1, 0, 1, 0, 0, 1.0)
        compressed_bit = struct.pack("<IhBBI", 1, 0, 0, 1, 0)
        motion = struct.pack("<4Bhhh", 0, 0, 1, 0, 1, 0, 0)
        motion += b"\x00\x00" + struct.pack("<f", 1.0)
        cases = (
            (W3D_CHUNK_ANIMATION_CHANNEL, raw, None, "raw-channel"),
            (W3D_CHUNK_ANIMATION_BIT_CHANNEL, raw_bit, None, "raw-bit-channel"),
            (
                W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
                compressed,
                W3D_COMPRESSED_FLAVOR_TIME_CODED,
                "compressed-time-coded-channel",
            ),
            (
                W3D_CHUNK_COMPRESSED_BIT_CHANNEL,
                compressed_bit,
                None,
                "compressed-time-coded-bit-channel",
            ),
            (
                W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL,
                motion,
                None,
                "motion-time-coded-channel",
            ),
        )
        for chunk_id, payload, flavor, expected_kind in cases:
            with self.subTest(chunk_id=chunk_id):
                result = decode_animation_stream(
                    chunk_id,
                    payload,
                    animation_frame_count=1,
                    pivot_count=1,
                    compressed_flavor=flavor,
                )
                self.assertEqual(result.channel.stream_kind, expected_kind)
                self.assertEqual(result.attestation.chunk_id, chunk_id)

    def test_dispatch_requires_flavor_and_rejects_unknown_chunk_and_flavor(
        self,
    ) -> None:
        payload = struct.pack("<IHBBIf", 1, 0, 1, 0, 0, 1.0)
        with self.assertRaisesRegex(W3DAnimationStreamDecodeError, "requires"):
            decode_animation_stream(
                W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
                payload,
                animation_frame_count=1,
                pivot_count=1,
            )
        with self.assertRaisesRegex(
            W3DAnimationStreamUnsupportedError,
            "flavor 9",
        ):
            decode_animation_stream(
                W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
                payload,
                animation_frame_count=1,
                pivot_count=1,
                compressed_flavor=9,
            )
        with self.assertRaisesRegex(
            W3DAnimationStreamUnsupportedError,
            "0xDEADBEEF",
        ):
            decode_animation_stream(
                0xDEADBEEF,
                b"",
                animation_frame_count=1,
                pivot_count=1,
            )


if __name__ == "__main__":
    unittest.main()
