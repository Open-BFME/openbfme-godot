"""Pins how the Apt handler fixtures decide which BFME2 image they run against (apt_handlers.identify_image).

    python -m pytest tools/retail_oracle/test_apt_image_identity.py -rs

The fixtures patch fixed virtual addresses of BFME2 1.06. On an image that does not map those addresses (the dev machine's
game.dat there is based at 0x10000000, SizeOfImage 0xACC000) the patch used to fault with `write fault in ad88c0` before any
comparison ran. Now the image is identified from the file first, and anything unknown is an UnsupportedImage that names the
base, size and .text digest.

The tests use synthetic PE files (a header and a .text section of made-up bytes): no retail bytes, no helper executable.
"""
from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import apt_handlers  # noqa: E402
from apt_handlers import CLEAN_BASE, KNOWN_IMAGES, POOL_FAULT_VA, AptHandlers, KnownImage, UnsupportedImage, identify_image, pe_summary  # noqa: E402


def synthetic_pe(base: int, size: int, text: bytes, text_name: bytes = b".text") -> bytes:
    """A minimal PE32 file: DOS header, NT headers, one section holding `text` at file offset 0x400. Not executable; only the
    fields identify_image reads are meaningful."""
    pe = 0x80
    head = bytearray(0x400)
    head[0:2] = b"MZ"
    struct.pack_into("<I", head, 0x3C, pe)
    head[pe:pe + 4] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", head, pe + 4, 0x14C, 1, 0, 0, 0, 0xE0, 0x102)
    opt = pe + 24
    struct.pack_into("<H", head, opt, 0x10B)
    struct.pack_into("<I", head, opt + 28, base)
    struct.pack_into("<I", head, opt + 56, size)
    struct.pack_into("<8sIIII", head, opt + 0xE0, text_name, len(text), 0x1000, len(text), 0x400)
    return bytes(head) + text


def test_a_known_images_digest_identifies_it():
    text = b"synthetic code for the test"
    known = KnownImage("synthetic", 0x10000000, 0x1000, hashlib.sha256(text).hexdigest(), {"marker": 1})
    data = synthetic_pe(0x10000000, 0x1000, text)
    assert pe_summary(data) == dict(base=0x10000000, size=0x1000, text_sha256=known.text_sha256)
    old = apt_handlers.KNOWN_IMAGES
    apt_handlers.KNOWN_IMAGES = old + (known,)
    try:
        assert identify_image(data) is known
    finally:
        apt_handlers.KNOWN_IMAGES = old


def test_an_unknown_image_is_refused_with_its_identity_in_the_message():
    # the original failure: an image based at 0x10000000 that none of the fixed VAs fit
    data = synthetic_pe(0x10000000, 0xACC000, b"not the 1.06 code")
    with pytest.raises(UnsupportedImage) as e:
        identify_image(data)
    msg = str(e.value)
    assert "base 0x10000000" in msg and "size 0xacc000" in msg
    assert hashlib.sha256(b"not the 1.06 code").hexdigest()[:16] in msg
    for known in KNOWN_IMAGES:  # says what it would have accepted
        assert known.text_sha256[:16] in msg


@pytest.mark.parametrize("what", ["base", "size", "text"])
def test_every_part_of_the_identity_is_checked(what):
    # a known image with exactly one part changed is not that image: the base alone, the size alone or the code alone is not enough
    text = b"the one verified code"
    known = KnownImage("synthetic", 0x400000, 0x2000, hashlib.sha256(text).hexdigest(), {})
    args = dict(base=0x400000, size=0x2000, text=text)
    args[what] = {"base": 0x10000000, "size": 0x3000, "text": b"the one verified cod3"}[what]
    old = apt_handlers.KNOWN_IMAGES
    apt_handlers.KNOWN_IMAGES = (known,)
    try:
        with pytest.raises(UnsupportedImage):
            identify_image(synthetic_pe(args["base"], args["size"], args["text"]))
        identify_image(synthetic_pe(0x400000, 0x2000, text))  # and the unchanged one is accepted
    finally:
        apt_handlers.KNOWN_IMAGES = old


@pytest.mark.parametrize("data,why", [
    (b"", "no MZ header"),
    (b"MZ" + b"\0" * 0x100, "no PE signature"),
    (synthetic_pe(0x400000, 0x1000, b"x", text_name=b".rdata"), "no .text section"),
    (synthetic_pe(0x400000, 0x1000, b"abcdef")[:0x402], ".text lies beyond the end of the file"),
])
def test_a_file_that_is_not_a_readable_pe_is_refused_with_the_reason(data, why):
    with pytest.raises(UnsupportedImage, match=why):
        identify_image(data)


class FakeOracle:
    def __init__(self, base, size):
        self.fields = {"base": "%08x" % base, "size": "%x" % size}


def test_the_hosts_own_view_of_the_loaded_file_must_match_the_identified_one():
    image = KNOWN_IMAGES[0]
    with pytest.raises(UnsupportedImage, match="a different file than the one identified"):
        AptHandlers(FakeOracle(0x10000000, image.size), image)
    with pytest.raises(UnsupportedImage, match="a different file than the one identified"):
        AptHandlers(FakeOracle(image.base, 0xACC000), image)


@pytest.mark.parametrize("image", KNOWN_IMAGES, ids=lambda i: i.name)
def test_every_profile_address_lies_inside_its_image(image):
    # The original failure, found without running the host: 0xAD88C0 is not inside an image based at 0x10000000 of size 0xACC000.
    p = image.profile
    vas = [p["version"], p["undefined"], p["global_stack"], p["global_stack"] + 8, POOL_FAULT_VA + image.base - CLEAN_BASE,
           *p["handlers"].values(), *(p[k] for k in ("create_bool", "create_int", "make_float", "push", "pop_push", "to_number", "to_integer", "boolean"))]
    assert all(image.base <= va < image.base + image.size for va in vas), [hex(v) for v in vas if not image.base <= v < image.base + image.size]
