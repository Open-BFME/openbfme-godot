"""Deterministic decoders for retail SAGE mouse-cursor art.

Retail BFME2/RotWK bind their mouse cursors in ``data/ini/mouse.ini`` and ship
the art beside it as *loose* Win32 cursor files under ``data/cursors``.  They
are the only retail art family that is never packed into a BIG: Windows loads a
cursor from the filesystem, so EA had to leave them loose.  That is why this
module reads Win32 ``.cur``/``.ico``/``.ani`` containers instead of going
through the BIG catalog readers used everywhere else.

Nothing here touches the filesystem or emits PNG: it turns bytes into plain
RGBA frames plus the retail animation timing, so the pack lane and the tests
can share one decoder and the tests can use synthetic, project-authored
fixtures rather than retail bytes.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
import struct
from typing import Iterator


# Resource-exhaustion bounds, not semantic limits.  A retail cursor is a 32x32
# 24bpp animation of at most a handful of frames; these ceilings only stop a
# malformed or hostile file from allocating without bound.
MAX_CURSOR_BYTES = 8 * 1024 * 1024
MAX_FRAMES = 64
MAX_SEQUENCE_STEPS = 256
MAX_DIMENSION = 256
JIFFIES_PER_SECOND = 60.0

_ICON_HEADER = struct.Struct("<HHH")
_ICON_ENTRY = struct.Struct("<BBBBHHII")
_ANIH = struct.Struct("<9I")

# ``anih.bfAttributes`` bit 0: the frames are complete icon/cursor containers
# rather than raw DIBs.  Every retail SAGE cursor sets it; a file that does not
# is refused rather than guessed at.
ANI_FLAG_ICON = 0x01

_MOUSE_CURSOR_BLOCK = re.compile(
    r"^[ \t]*MouseCursor[ \t]+(?P<name>[A-Za-z0-9_]+)[ \t]*\r?\n"
    r"(?P<body>.*?)"
    r"^[ \t]*End[ \t]*\r?$",
    re.DOTALL | re.MULTILINE | re.IGNORECASE,
)
_FIELD = re.compile(
    r"^[ \t]*(?P<key>[A-Za-z0-9_]+)[ \t]*=[ \t]*(?P<value>[^;\r\n]*)",
    re.MULTILINE,
)
_HOTSPOT = re.compile(r"X[ \t]*:[ \t]*(-?\d+)[ \t]+Y[ \t]*:[ \t]*(-?\d+)", re.IGNORECASE)


class CursorFormatError(ValueError):
    """The input is not a structurally valid Win32 cursor container."""


@dataclass(frozen=True, slots=True)
class CursorFrame:
    """One decoded cursor image: top-down, row-major, straight (unmultiplied) RGBA."""

    width: int
    height: int
    hotspot_x: int
    hotspot_y: int
    rgba: bytes

    def __post_init__(self) -> None:
        if len(self.rgba) != self.width * self.height * 4:
            raise CursorFormatError("decoded cursor frame has an inconsistent pixel count")

    @property
    def opaque_pixel_count(self) -> int:
        return sum(1 for index in range(3, len(self.rgba), 4) if self.rgba[index] != 0)


@dataclass(frozen=True, slots=True)
class AnimatedCursor:
    """Frames plus the retail playback order and per-step display rate."""

    frames: tuple[CursorFrame, ...]
    sequence: tuple[int, ...]
    rates_jiffies: tuple[int, ...]

    def __post_init__(self) -> None:
        if not self.frames:
            raise CursorFormatError("cursor has no frames")
        if len(self.sequence) != len(self.rates_jiffies):
            raise CursorFormatError("cursor sequence and rate tables disagree in length")
        for index in self.sequence:
            if not 0 <= index < len(self.frames):
                raise CursorFormatError(f"cursor sequence step {index} has no frame")

    @property
    def hotspot_x(self) -> int:
        return self.frames[0].hotspot_x

    @property
    def hotspot_y(self) -> int:
        return self.frames[0].hotspot_y

    @property
    def width(self) -> int:
        return self.frames[0].width

    @property
    def height(self) -> int:
        return self.frames[0].height

    @property
    def animated(self) -> bool:
        return len(self.sequence) > 1

    def step_seconds(self) -> tuple[float, ...]:
        return tuple(rate / JIFFIES_PER_SECOND for rate in self.rates_jiffies)

    def duration_seconds(self) -> float:
        return sum(self.step_seconds())


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise CursorFormatError(message)


def _riff_chunks(data: bytes, start: int, end: int) -> Iterator[tuple[bytes, int, int]]:
    """Yield ``(chunk_id, body_start, body_end)`` for a RIFF chunk run."""

    offset = start
    while offset + 8 <= end:
        chunk_id = data[offset : offset + 4]
        (size,) = struct.unpack_from("<I", data, offset + 4)
        body_start = offset + 8
        body_end = body_start + size
        _require(body_end <= end, f"RIFF chunk {chunk_id!r} runs past its parent")
        yield chunk_id, body_start, body_end
        # RIFF chunks are word-aligned; an odd size carries one pad byte.
        offset = body_end + (size & 1)


def _decode_dib_frame(
    data: bytes,
    start: int,
    end: int,
    hotspot_x: int,
    hotspot_y: int,
) -> CursorFrame:
    _require(end - start >= 40, "cursor image is too small to hold a BITMAPINFOHEADER")
    header_size, dib_width, dib_height, planes, bpp, compression = struct.unpack_from(
        "<IiiHHI", data, start
    )
    _require(header_size >= 40, "cursor DIB header is truncated")
    _require(compression == 0, f"cursor DIB uses unsupported compression {compression}")
    _require(planes == 1, f"cursor DIB declares {planes} colour planes")
    _require(
        0 < dib_width <= MAX_DIMENSION,
        f"cursor DIB width {dib_width} is out of range",
    )
    # An icon/cursor DIB stacks the XOR image over the AND mask, so the stored
    # height is twice the real one.
    _require(
        dib_height > 0 and dib_height % 2 == 0 and dib_height // 2 <= MAX_DIMENSION,
        f"cursor DIB height {dib_height} is not a valid doubled icon height",
    )
    _require(bpp in (1, 4, 8, 24, 32), f"cursor DIB uses unsupported {bpp}bpp")

    height = dib_height // 2
    (palette_entries,) = struct.unpack_from("<I", data, start + 32)
    if bpp <= 8 and palette_entries == 0:
        palette_entries = 1 << bpp
    palette_bytes = palette_entries * 4 if bpp <= 8 else 0
    pixel_start = start + header_size + palette_bytes
    palette = data[start + header_size : pixel_start]

    xor_stride = ((dib_width * bpp + 31) // 32) * 4
    and_stride = ((dib_width + 31) // 32) * 4
    xor_size = xor_stride * height
    and_size = and_stride * height
    _require(
        pixel_start + xor_size <= end,
        "cursor DIB colour plane is truncated",
    )
    has_and_mask = pixel_start + xor_size + and_size <= end
    xor_plane = data[pixel_start : pixel_start + xor_size]
    and_plane = (
        data[pixel_start + xor_size : pixel_start + xor_size + and_size]
        if has_and_mask
        else b""
    )

    # A 32bpp cursor normally carries a real alpha channel and its AND mask is
    # a legacy stub; only fall back to the mask when every alpha byte is zero,
    # which would otherwise decode the whole cursor as invisible.
    use_alpha_channel = bpp == 32 and any(
        xor_plane[index] for index in range(3, len(xor_plane), 4)
    )

    out = bytearray(dib_width * height * 4)
    for y in range(height):
        # DIB rows are bottom-up.
        source_y = height - 1 - y
        xor_row = source_y * xor_stride
        and_row = source_y * and_stride
        for x in range(dib_width):
            if bpp == 32:
                offset = xor_row + x * 4
                blue, green, red, alpha = xor_plane[offset : offset + 4]
                if not use_alpha_channel:
                    alpha = 255
            elif bpp == 24:
                offset = xor_row + x * 3
                blue, green, red = xor_plane[offset : offset + 3]
                alpha = 255
            else:
                if bpp == 8:
                    index = xor_plane[xor_row + x]
                elif bpp == 4:
                    packed = xor_plane[xor_row + (x >> 1)]
                    index = (packed >> 4) if x % 2 == 0 else (packed & 0x0F)
                else:
                    packed = xor_plane[xor_row + (x >> 3)]
                    index = (packed >> (7 - (x & 7))) & 1
                base = index * 4
                _require(base + 3 <= len(palette), "cursor DIB palette index is out of range")
                blue, green, red = palette[base], palette[base + 1], palette[base + 2]
                alpha = 255
            if and_plane and not use_alpha_channel:
                masked = (and_plane[and_row + (x >> 3)] >> (7 - (x & 7))) & 1
                if masked:
                    # AND=1 means "leave the screen alone": transparent.  The
                    # XOR colour under it is retail's inversion trick and is not
                    # representable in straight RGBA, so it is dropped to zero
                    # instead of leaking a stray colour into the halo.
                    red = green = blue = 0
                    alpha = 0
            target = (y * dib_width + x) * 4
            out[target] = red
            out[target + 1] = green
            out[target + 2] = blue
            out[target + 3] = alpha

    return CursorFrame(
        width=dib_width,
        height=height,
        hotspot_x=hotspot_x,
        hotspot_y=hotspot_y,
        rgba=bytes(out),
    )


def decode_icon_container(data: bytes) -> tuple[CursorFrame, ...]:
    """Decode a Win32 ``.cur``/``.ico`` container into its images."""

    _require(len(data) <= MAX_CURSOR_BYTES, "cursor container exceeds the read bound")
    _require(len(data) >= _ICON_HEADER.size, "cursor container is truncated")
    reserved, kind, count = _ICON_HEADER.unpack_from(data, 0)
    _require(reserved == 0, "cursor container header is not a Win32 ICONDIR")
    _require(kind in (1, 2), f"cursor container declares unknown type {kind}")
    _require(0 < count <= MAX_FRAMES, f"cursor container declares {count} images")

    frames: list[CursorFrame] = []
    for index in range(count):
        entry_offset = _ICON_HEADER.size + index * _ICON_ENTRY.size
        _require(
            entry_offset + _ICON_ENTRY.size <= len(data),
            "cursor container directory is truncated",
        )
        (
            _width,
            _height,
            _colour_count,
            entry_reserved,
            planes_or_hotspot_x,
            bpp_or_hotspot_y,
            size,
            offset,
        ) = _ICON_ENTRY.unpack_from(data, entry_offset)
        _require(entry_reserved == 0, "cursor container directory entry is malformed")
        _require(
            0 < size and offset + size <= len(data),
            "cursor container image runs past the file",
        )
        # In a CUR the planes/bit-count fields carry the hotspot; an ICO has none.
        hotspot_x = planes_or_hotspot_x if kind == 2 else 0
        hotspot_y = bpp_or_hotspot_y if kind == 2 else 0
        _require(
            data[offset : offset + 8] != b"\x89PNG\r\n\x1a\n",
            "PNG-compressed cursor images are not supported",
        )
        frames.append(
            _decode_dib_frame(data, offset, offset + size, hotspot_x, hotspot_y)
        )
    return tuple(frames)


def decode_ani(data: bytes) -> AnimatedCursor:
    """Decode a RIFF ``ACON`` animated cursor into frames plus retail timing."""

    _require(len(data) <= MAX_CURSOR_BYTES, "animated cursor exceeds the read bound")
    _require(len(data) >= 12, "animated cursor is truncated")
    _require(data[0:4] == b"RIFF", "animated cursor is not a RIFF file")
    (riff_size,) = struct.unpack_from("<I", data, 4)
    _require(data[8:12] == b"ACON", "animated cursor is not a RIFF ACON file")
    end = min(len(data), 8 + riff_size)

    frame_blobs: list[bytes] = []
    header: tuple[int, ...] | None = None
    sequence: tuple[int, ...] | None = None
    rates: tuple[int, ...] | None = None

    for chunk_id, body_start, body_end in _riff_chunks(data, 12, end):
        body = data[body_start:body_end]
        if chunk_id == b"anih":
            _require(len(body) >= _ANIH.size, "animated cursor anih chunk is truncated")
            header = _ANIH.unpack_from(body, 0)
        elif chunk_id == b"seq ":
            _require(len(body) % 4 == 0, "animated cursor seq chunk is malformed")
            sequence = struct.unpack_from("<%dI" % (len(body) // 4), body, 0)
        elif chunk_id == b"rate":
            _require(len(body) % 4 == 0, "animated cursor rate chunk is malformed")
            rates = struct.unpack_from("<%dI" % (len(body) // 4), body, 0)
        elif chunk_id == b"LIST" and body[0:4] == b"fram":
            for inner_id, inner_start, inner_end in _riff_chunks(
                data, body_start + 4, body_end
            ):
                if inner_id == b"icon":
                    _require(
                        len(frame_blobs) < MAX_FRAMES,
                        "animated cursor declares too many frames",
                    )
                    frame_blobs.append(data[inner_start:inner_end])

    _require(header is not None, "animated cursor has no anih chunk")
    assert header is not None
    (
        _cb_size,
        frame_count,
        step_count,
        _width,
        _height,
        _bit_count,
        _planes,
        display_rate,
        flags,
    ) = header
    _require(
        bool(flags & ANI_FLAG_ICON),
        "animated cursor stores raw DIB frames, which are not supported",
    )
    _require(frame_blobs, "animated cursor has no frames")
    _require(
        frame_count == len(frame_blobs),
        f"animated cursor declares {frame_count} frames but carries {len(frame_blobs)}",
    )
    _require(
        0 < step_count <= MAX_SEQUENCE_STEPS,
        f"animated cursor declares {step_count} sequence steps",
    )

    frames: list[CursorFrame] = []
    for blob in frame_blobs:
        images = decode_icon_container(blob)
        # Retail frames carry exactly one image; take the first deterministically.
        frames.append(images[0])
    first = frames[0]
    for frame in frames[1:]:
        _require(
            frame.width == first.width and frame.height == first.height,
            "animated cursor frames disagree on size",
        )

    if sequence is None:
        _require(
            step_count == len(frames),
            "animated cursor has no seq chunk and its step count is not the frame count",
        )
        sequence = tuple(range(len(frames)))
    else:
        _require(
            len(sequence) == step_count,
            "animated cursor seq chunk length disagrees with its step count",
        )

    if rates is None:
        _require(display_rate > 0, "animated cursor declares a zero display rate")
        rates = tuple([display_rate] * step_count)
    else:
        _require(
            len(rates) == step_count,
            "animated cursor rate chunk length disagrees with its step count",
        )
        _require(all(rate > 0 for rate in rates), "animated cursor declares a zero rate")

    return AnimatedCursor(
        frames=tuple(frames), sequence=tuple(sequence), rates_jiffies=tuple(rates)
    )


def decode_cursor(data: bytes, *, display_rate_jiffies: int = 6) -> AnimatedCursor:
    """Decode either a ``.ani`` animation or a single-image ``.cur``/``.ico``."""

    if data[0:4] == b"RIFF":
        return decode_ani(data)
    frames = decode_icon_container(data)
    return AnimatedCursor(
        frames=(frames[0],), sequence=(0,), rates_jiffies=(display_rate_jiffies,)
    )


def cursor_source_filename(texture: str) -> str:
    """Return the loose ``data/cursors`` filename a mouse.ini Texture names.

    ``Texture = SCCAttack`` means ``sccattack.ani``; retail only ever writes the
    extension when the art is a static ``.cur``.
    """

    value = texture.strip()
    _require(bool(value), "mouse.ini cursor texture is empty")
    # Refuse a path rather than silently stripping it: mouse.ini only ever
    # names a bare leaf under data/cursors, so anything with a separator (or a
    # traversal segment) is a file this lane must not guess at.
    _require(
        re.fullmatch(r"[A-Za-z0-9_.\-]+", value) is not None and value not in {".", ".."},
        f"mouse.ini cursor texture is not a plain filename: {texture!r}",
    )
    lowered = value.casefold()
    if "." not in lowered:
        return f"{lowered}.ani"
    _require(
        lowered.rsplit(".", 1)[1] in {"ani", "cur", "ico"},
        f"mouse.ini cursor texture has an unsupported extension: {texture!r}",
    )
    return lowered


@dataclass(frozen=True, slots=True)
class MouseCursorBinding:
    """One ``MouseCursor`` block: the retail intent name and the art it names."""

    name: str
    texture: str
    source_filename: str
    hotspot: tuple[int, int] | None


def parse_mouse_cursor_bindings(text: str) -> dict[str, MouseCursorBinding]:
    """Parse the ``MouseCursor`` blocks of retail ``data/ini/mouse.ini``.

    Keyed by the casefolded block name (``attackobj``, ``forceattackobj``, ...),
    which is the name the engine looks a cursor up by.
    """

    bindings: dict[str, MouseCursorBinding] = {}
    for match in _MOUSE_CURSOR_BLOCK.finditer(text):
        name = match.group("name")
        body = match.group("body")
        texture: str | None = None
        hotspot: tuple[int, int] | None = None
        for field in _FIELD.finditer(body):
            key = field.group("key").casefold()
            value = field.group("value").strip()
            if key == "texture" and texture is None:
                texture = value
            elif key == "hotspot":
                spot = _HOTSPOT.search(value)
                if spot is not None:
                    hotspot = (int(spot.group(1)), int(spot.group(2)))
        if not texture:
            continue
        bindings[name.casefold()] = MouseCursorBinding(
            name=name,
            texture=texture,
            source_filename=cursor_source_filename(texture),
            hotspot=hotspot,
        )
    return bindings


__all__ = [
    "AnimatedCursor",
    "CursorFormatError",
    "CursorFrame",
    "MouseCursorBinding",
    "cursor_source_filename",
    "decode_ani",
    "decode_cursor",
    "decode_icon_container",
    "parse_mouse_cursor_bindings",
]
