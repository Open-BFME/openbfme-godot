"""Cursor decoder tests over synthetic, project-authored Win32 containers.

No retail bytes appear here: every fixture is built in this file so the suite
runs on a machine with no game installed.  The retail oracle for this decoder is
exercised separately by ``publish-cursor-pack`` against the operator's install.
"""

from __future__ import annotations

import struct

import pytest

from openbfme_importer.cursor_art import (
    AnimatedCursor,
    CursorFormatError,
    CursorFrame,
    cursor_source_filename,
    decode_ani,
    decode_cursor,
    decode_icon_container,
    parse_mouse_cursor_bindings,
)


def _dib(
    width: int,
    height: int,
    pixels: list[tuple[int, int, int]],
    mask: list[int],
) -> bytes:
    """Build a 24bpp icon DIB (XOR plane over a 1bpp AND mask), bottom-up."""

    header = struct.pack(
        "<IiiHHIIiiII",
        40,
        width,
        height * 2,
        1,
        24,
        0,
        0,
        0,
        0,
        0,
        0,
    )
    xor_stride = ((width * 24 + 31) // 32) * 4
    and_stride = ((width + 31) // 32) * 4
    xor = bytearray()
    and_plane = bytearray()
    for y in range(height - 1, -1, -1):
        row = bytearray()
        for x in range(width):
            red, green, blue = pixels[y * width + x]
            row += bytes((blue, green, red))
        row += b"\x00" * (xor_stride - len(row))
        xor += row
        bits = bytearray(and_stride)
        for x in range(width):
            if mask[y * width + x]:
                bits[x >> 3] |= 1 << (7 - (x & 7))
        and_plane += bits
    return header + bytes(xor) + bytes(and_plane)


def _cur(
    width: int,
    height: int,
    pixels: list[tuple[int, int, int]],
    mask: list[int],
    hotspot: tuple[int, int],
) -> bytes:
    image = _dib(width, height, pixels, mask)
    offset = 6 + 16
    directory = struct.pack("<HHH", 0, 2, 1) + struct.pack(
        "<BBBBHHII",
        width,
        height,
        0,
        0,
        hotspot[0],
        hotspot[1],
        len(image),
        offset,
    )
    return directory + image


def _riff_chunk(chunk_id: bytes, body: bytes) -> bytes:
    payload = chunk_id + struct.pack("<I", len(body)) + body
    if len(body) & 1:
        payload += b"\x00"
    return payload


def _ani(
    frames: list[bytes],
    *,
    sequence: list[int] | None,
    rates: list[int] | None,
    display_rate: int = 6,
) -> bytes:
    steps = len(sequence) if sequence is not None else len(frames)
    body = _riff_chunk(
        b"anih",
        struct.pack(
            "<9I", 36, len(frames), steps, 0, 0, 24, 1, display_rate, 0x03 if sequence else 0x01
        ),
    )
    if rates is not None:
        body += _riff_chunk(b"rate", struct.pack("<%dI" % len(rates), *rates))
    if sequence is not None:
        body += _riff_chunk(b"seq ", struct.pack("<%dI" % len(sequence), *sequence))
    frame_list = b"fram" + b"".join(_riff_chunk(b"icon", frame) for frame in frames)
    body += _riff_chunk(b"LIST", frame_list)
    return b"RIFF" + struct.pack("<I", 4 + len(body)) + b"ACON" + body


RED = (255, 0, 0)
BLACK = (0, 0, 0)


def _solid_frame(colour: tuple[int, int, int], *, opaque_corner: bool = True) -> bytes:
    """2x2 cursor: three pixels of ``colour`` and one masked-out pixel."""

    pixels = [colour, colour, colour, BLACK]
    mask = [0, 0, 0, 1 if opaque_corner else 0]
    return _cur(2, 2, pixels, mask, hotspot=(1, 1))


def test_cur_decode_applies_the_and_mask_and_reads_the_hotspot() -> None:
    frames = decode_icon_container(_solid_frame(RED))
    assert len(frames) == 1
    frame = frames[0]
    assert (frame.width, frame.height) == (2, 2)
    assert (frame.hotspot_x, frame.hotspot_y) == (1, 1)
    # Top-down RGBA: three red opaque pixels, the AND-masked one fully clear.
    assert frame.rgba == bytes(
        [255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 0, 0, 0, 0]
    )
    assert frame.opaque_pixel_count == 3


def test_cur_decode_is_top_down_not_bottom_up() -> None:
    # A bottom-up read would put the marker pixel in the wrong row; assert the
    # row order explicitly so a flipped decoder cannot pass.
    pixels = [(1, 2, 3), (4, 5, 6), (7, 8, 9), (10, 11, 12)]
    frame = decode_icon_container(_cur(2, 2, pixels, [0, 0, 0, 0], (0, 0)))[0]
    assert frame.rgba[0:4] == bytes([1, 2, 3, 255])
    assert frame.rgba[12:16] == bytes([10, 11, 12, 255])


def test_ani_decode_reads_frames_sequence_and_rates() -> None:
    data = _ani(
        [_solid_frame(RED), _solid_frame((0, 255, 0)), _solid_frame((0, 0, 255))],
        sequence=[0, 1, 2, 1],
        rates=[7, 7, 7, 7],
    )
    cursor = decode_ani(data)
    assert len(cursor.frames) == 3
    assert cursor.sequence == (0, 1, 2, 1)
    assert cursor.rates_jiffies == (7, 7, 7, 7)
    assert cursor.animated is True
    assert (cursor.hotspot_x, cursor.hotspot_y) == (1, 1)
    assert cursor.step_seconds() == pytest.approx((7 / 60,) * 4)
    assert cursor.duration_seconds() == pytest.approx(28 / 60)


def test_ani_without_seq_or_rate_falls_back_to_the_header_rate() -> None:
    data = _ani([_solid_frame(RED), _solid_frame(BLACK)], sequence=None, rates=None, display_rate=9)
    cursor = decode_ani(data)
    assert cursor.sequence == (0, 1)
    assert cursor.rates_jiffies == (9, 9)


def test_ani_refuses_a_frame_count_that_disagrees_with_the_payload() -> None:
    good = _ani([_solid_frame(RED)], sequence=None, rates=None)
    # Rewrite anih.nFrames to 4 while only one icon chunk is present.
    index = good.index(b"anih")
    broken = bytearray(good)
    struct.pack_into("<I", broken, index + 8 + 4, 4)
    with pytest.raises(CursorFormatError, match="declares 4 frames"):
        decode_ani(bytes(broken))


def test_ani_refuses_raw_dib_frames() -> None:
    good = _ani([_solid_frame(RED)], sequence=None, rates=None)
    index = good.index(b"anih")
    broken = bytearray(good)
    struct.pack_into("<I", broken, index + 8 + 32, 0)
    with pytest.raises(CursorFormatError, match="raw DIB frames"):
        decode_ani(bytes(broken))


def test_decode_cursor_dispatches_on_the_container_magic() -> None:
    static = decode_cursor(_solid_frame(RED))
    assert static.animated is False
    assert static.sequence == (0,)
    animated = decode_cursor(_ani([_solid_frame(RED), _solid_frame(BLACK)], sequence=None, rates=None))
    assert animated.animated is True


def test_animated_cursor_refuses_an_out_of_range_sequence_step() -> None:
    frame = CursorFrame(1, 1, 0, 0, bytes([0, 0, 0, 255]))
    with pytest.raises(CursorFormatError, match="sequence step"):
        AnimatedCursor(frames=(frame,), sequence=(3,), rates_jiffies=(6,))


def test_icon_container_refuses_a_non_iconodir_header() -> None:
    with pytest.raises(CursorFormatError):
        decode_icon_container(b"\x01\x00\x02\x00\x01\x00" + b"\x00" * 32)


@pytest.mark.parametrize(
    ("texture", "expected"),
    [
        ("SCCAttack", "sccattack.ani"),
        ("SCCPointer.cur", "sccpointer.cur"),
        ("beacon.CUR", "beacon.cur"),
        ("Magnify", "magnify.ani"),
    ],
)
def test_cursor_source_filename(texture: str, expected: str) -> None:
    assert cursor_source_filename(texture) == expected


@pytest.mark.parametrize("texture", ["", "../evil.ani", "cursors/deep.ani", "art.tga"])
def test_cursor_source_filename_refuses_unsafe_or_unknown_art(texture: str) -> None:
    with pytest.raises(CursorFormatError):
        cursor_source_filename(texture)


MOUSE_INI = """
Mouse
  DragTolerance = 15
End

MouseCursor Arrow
  Texture             = SCCPointer.cur
  Image               = SCCPointer.cur
  HotSpot             = X:2 Y:2
End

MouseCursor AttackObj
  Texture             = SCCAttack
  Image               = SCCAttack
End

MouseCursor ForceAttackGround
  Texture             = SCCAttack   ; same art, different intent
  Image               = SCCAttack
End
"""


def test_parse_mouse_cursor_bindings() -> None:
    bindings = parse_mouse_cursor_bindings(MOUSE_INI)
    assert set(bindings) == {"arrow", "attackobj", "forceattackground"}
    assert bindings["attackobj"].texture == "SCCAttack"
    assert bindings["attackobj"].source_filename == "sccattack.ani"
    assert bindings["attackobj"].hotspot is None
    assert bindings["arrow"].hotspot == (2, 2)
    assert bindings["arrow"].source_filename == "sccpointer.cur"
    # A trailing INI comment must not become part of the art name.
    assert bindings["forceattackground"].source_filename == "sccattack.ani"


def test_parse_mouse_cursor_bindings_ignores_the_mouse_block() -> None:
    assert parse_mouse_cursor_bindings("Mouse\n  Texture = nope\nEnd\n") == {}
