"""More BFME2 1.06 Apt handlers run on fake AptValue objects (the value layout is in apt_oracle.py).

Handlers (VAs in the clean BFME2 1.06 image, preferred base 0x400000), all `cdecl(stack*)` with the operand stack at {+0 count, +8 array of value*},
top = last:
    Equals2 0xB031E0   Add2 0xB02B60   Subtract 0xB00880   Multiply 0xB009E0   Divide 0xB00B40   Modulo 0xB02600
    Less2 0xB02F20     Greater 0xB04710   Increment 0xB03F40   Decrement 0xB04020
and the value methods ToNumber 0xADD460 (thiscall, returns ST0), ToInteger 0xADD360 (thiscall, eax) and the global
Boolean() native 0xAFF850 (cdecl(self, argc); reads the global operand stack at 0xE182E0 / array at 0xE182E8).

The handlers end by building a result value and pushing it. Here the entry points of the four functions involved are
patched in the HELPER'S copy of the image (never the file) to stubs that record what was built:
    AptBoolean::Create 0xAD88C0 (cdecl, 1 arg)   AptInteger::Create 0xAD8520 (cdecl, 1 arg)
    MakeFloat 0xAD8700 (cdecl, a float arg)      push 0xAFE6E0 (thiscall, value, ret 4)
    pop-n-and-push 0xAFE880 (thiscall, n, value, ret 8; Greater uses it)
Every type dispatch, string conversion and x87 operation before that is the retail code.

Strings are type-1 values: +8 holds a pointer to the string data {u16 refcount, u16 length, u16 capacity, u16 hash,
then the NUL-terminated text}, read from 0xADCE50 (value+8 is the EAStringC), 0xA20090 (text = data+8) and 0xAD3750
(length = word at data+2). Objects are not built (docs/STOPS.md S-042).

The VAs above hold only for an image whose code and data layout they were read from. identify_image() decides that from the file
BEFORE anything is patched: it accepts exactly the images in KNOWN_IMAGES (preferred base, SizeOfImage and the SHA-256 of the
.text bytes) and raises UnsupportedImage, with the reason, for anything else.
"""
from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass

from oracle import Oracle

PROFILE = dict(
    version=0xE17724,
    undefined=0xE18078,
    handlers=dict(equals2=0xB031E0, add2=0xB02B60, subtract=0xB00880, multiply=0xB009E0, divide=0xB00B40, modulo=0xB02600,
                  less2=0xB02F20, greater=0xB04710, increment=0xB03F40, decrement=0xB04020),
    create_bool=0xAD88C0, create_int=0xAD8520, make_float=0xAD8700, push=0xAFE6E0, pop_push=0xAFE880,
    to_number=0xADD460, to_integer=0xADD360, boolean=0xAFF850,
    global_stack=0xE182E0,
)

# Pool allocator fault (docs/STOPS.md S-016), clean 1.06 VA: the first load of its never built global.
POOL_FAULT_VA = 0xADB177
CLEAN_BASE = 0x400000


class UnsupportedImage(RuntimeError):
    """The BFME2 game.dat is not an image these fixtures have a verified address profile for. Raised before any patching."""


@dataclass(frozen=True)
class KnownImage:
    name: str
    base: int  # PE ImageBase: the oracle host maps the image there, so every profile VA must lie inside base + size
    size: int  # PE SizeOfImage
    text_sha256: str  # SHA-256 of the first VirtualSize bytes of .text (a digest only; no retail bytes are kept)
    profile: dict


KNOWN_IMAGES = (
    KnownImage("BFME2 1.06 clean (base 0x400000)", CLEAN_BASE, 0xADA000,
               "86ee7235ec01068a26bc3d78ad0f92788f57051efea3b30fabfbf0ee0454ce55", PROFILE),
)


def pe_summary(data: bytes) -> dict:
    """ImageBase, SizeOfImage and the .text digest of a PE32 file; UnsupportedImage when it is not one."""
    try:
        if data[:2] != b"MZ":
            raise ValueError("no MZ header")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("no PE signature")
        nsec, opt_size = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
        opt = pe + 24
        if struct.unpack_from("<H", data, opt)[0] != 0x10B:
            raise ValueError("not a 32-bit PE")
        base, size = struct.unpack_from("<I", data, opt + 28)[0], struct.unpack_from("<I", data, opt + 56)[0]
        for i in range(nsec):
            name, vsize, _va, rsize, rptr = struct.unpack_from("<8sIIII", data, opt + opt_size + 40 * i)
            if name.rstrip(b"\0") == b".text":
                raw = data[rptr:rptr + min(vsize, rsize)]
                if len(raw) != min(vsize, rsize):
                    raise ValueError(".text lies beyond the end of the file")
                return dict(base=base, size=size, text_sha256=hashlib.sha256(raw).hexdigest())
        raise ValueError("no .text section")
    except (ValueError, struct.error) as e:
        raise UnsupportedImage(f"not a readable PE32 image: {e}") from e


def identify_image(data: bytes) -> KnownImage:
    """The verified image that `data` (the BFME2 game.dat bytes) is, else UnsupportedImage saying why. Call before patching."""
    got = pe_summary(data)
    for known in KNOWN_IMAGES:
        if (known.base, known.size, known.text_sha256) == (got["base"], got["size"], got["text_sha256"]):
            return known
    known_list = "; ".join(f"{k.name}: size {k.size:#x}, .text sha256 {k.text_sha256[:16]}" for k in KNOWN_IMAGES)
    raise UnsupportedImage(
        f"BFME2 game.dat has no verified Apt address profile (base {got['base']:#x}, size {got['size']:#x}, .text sha256 "
        f"{got['text_sha256'][:16]}). These fixtures patch fixed BFME2 1.06 VAs, which would hit unmapped or unrelated memory in "
        f"another image. Known: {known_list}. Point BFME2_INSTALL at a folder holding the clean BFME2 1.06 game.dat.")
T_STRING, T_BOOL, T_FLOAT, T_INT, T_UNDEF = 1, 5, 6, 7, 0x13


class AptHandlers:
    def __init__(self, o: Oracle, image: KnownImage):
        """`image` is identify_image() of the file `o` loaded; the host's own view of that file is cross-checked first."""
        if int(o.fields["base"], 16) != image.base or int(o.fields["size"], 16) != image.size:
            raise UnsupportedImage(f"the oracle host mapped base {o.fields['base']} size {o.fields['size']}, not {image.name} "
                                   f"(base {image.base:#x}, size {image.size:#x}): a different file than the one identified")
        self.o = o
        self.image = image
        self.p = image.profile
        self.pool_fault = "eip=%08x" % (POOL_FAULT_VA + image.base - CLEAN_BASE)
        self.rec = o.alloc(32)
        self.dummy = o.alloc(64)
        self.vt = o.alloc(64)
        o.poke32(self.vt + 4, o.code(b"\xC3"))  # release: ret
        self._patch(self.p["create_bool"], self._create_stub(1))
        self._patch(self.p["create_int"], self._create_stub(2))
        self._patch(self.p["make_float"], self._create_stub(3))
        self._patch(self.p["push"], o.code(b"\x8B\x44\x24\x04" + b"\xA3" + struct.pack("<I", self.rec + 8) + b"\xC2\x04\x00"))
        # pop-n-and-push: n at [esp+4] (recorded at rec+12), the value at [esp+8]
        self._patch(self.p["pop_push"], o.code(b"\x8B\x44\x24\x08" + b"\xA3" + struct.pack("<I", self.rec + 8) + b"\x8B\x44\x24\x04" + b"\xA3" +
                                               struct.pack("<I", self.rec + 12) + b"\xC2\x08\x00"))
        self.stack = o.alloc(16)
        self.arr = o.alloc(64)
        o.poke32(self.stack + 8, self.arr)
        self.undefined = self._obj(T_UNDEF, b"", defined=False)
        o.poke32(self.p["undefined"], self.undefined)
        # operand slots, reused for every case (each helper allocation is a page-granular VirtualAlloc that is never freed)
        self.slots = [(o.alloc(16), o.alloc(self.MAX_TEXT + 16)) for _ in range(3)]

    MAX_TEXT = 4096

    def _create_stub(self, kind: int) -> int:
        # mov eax,[esp+4]; mov [rec+4],eax; mov dword [rec],kind; mov eax,dummy; ret
        return self.o.code(b"\x8B\x44\x24\x04" + b"\xA3" + struct.pack("<I", self.rec + 4) + b"\xC7\x05" + struct.pack("<II", self.rec, kind) + b"\xB8" +
                           struct.pack("<I", self.dummy) + b"\xC3")

    def _patch(self, va: int, stub: int) -> None:
        self.o.poke(va, b"\xE9" + struct.pack("<I", (stub - (va + 5)) & 0xFFFFFFFF))

    # -- values --------------------------------------------------------------------------------
    def _obj(self, typ: int, payload: bytes, defined: bool = True) -> int:
        a = self.o.alloc(16)
        self.o.poke(a, struct.pack("<II", self.vt, (typ << 25) | (0x10 if defined else 0)) + payload)
        return a

    def int_(self, v: int) -> int:
        return self._obj(T_INT, struct.pack("<i", v))

    def float_bits(self, bits: int) -> int:
        return self._obj(T_FLOAT, struct.pack("<I", bits))

    def bool_(self, v: bool) -> int:
        return self._obj(T_BOOL, bytes([1 if v else 0]))

    def string(self, text: bytes) -> int:
        data = self.o.alloc(8 + len(text) + 1)
        self.o.poke(data, struct.pack("<HHHH", 1, len(text), len(text), 0) + text + b"\0")
        return self._obj(T_STRING, struct.pack("<I", data))

    def build(self, token: str, slot: int = 0) -> int:
        """A value from the driver's operand token (apt_driver.cpp): I:<dec> F:<hexbits> B:<0|1> U S:<hex>.
        The value lives in operand slot `slot` (0..2), overwritten by the next build into the same slot."""
        if token == "U":
            return self.undefined
        obj, data = self.slots[slot]
        kind, body = token[0], token[2:]
        if kind == "I":
            payload, typ = struct.pack("<i", int(body)), T_INT
        elif kind == "F":
            payload, typ = struct.pack("<I", int(body, 16)), T_FLOAT
        elif kind == "B":
            payload, typ = bytes([1 if body == "1" else 0]), T_BOOL
        elif kind == "S":
            text = bytes.fromhex(body)
            assert len(text) <= self.MAX_TEXT
            self.o.poke(data, struct.pack("<HHHH", 1, len(text), len(text), 0) + text + b"\0")
            payload, typ = struct.pack("<I", data), T_STRING
        else:
            raise ValueError(token)
        self.o.poke(obj, struct.pack("<II", self.vt, (typ << 25) | 0x10) + payload)
        return obj

    # -- results -------------------------------------------------------------------------------
    def _decode(self, pushed: int) -> str:
        kind = self.o.peek32(self.rec)
        payload = self.o.peek32(self.rec + 4)
        if pushed == self.undefined:
            assert kind == 0, "a value was built and the undefined singleton pushed"
            return "U"
        assert pushed == self.dummy, f"pushed {pushed:#x}: neither the undefined singleton nor the built value"
        if kind == 1:
            return "B:1" if payload & 0xFF else "B:0"
        if kind == 2:
            return "I:%d" % struct.unpack("<i", struct.pack("<I", payload))[0]
        if kind == 3:
            return "F:%08x" % payload
        raise AssertionError("the handler pushed a value that was never built")

    def run(self, op: str, swf: int, *operands: int) -> str:
        """Run a binary/unary handler; operands are value addresses, `under` first. Returns a driver-format token."""
        o = self.o
        o.poke32(self.p["version"], swf)
        for i, v in enumerate(operands):
            o.poke32(self.arr + 4 * i, v)
        o.poke32(self.stack, len(operands))
        o.poke(self.rec, b"\0" * 16)
        o.call(self.p["handlers"][op], "cdecl", [self.stack])
        return self._decode(o.peek32(self.rec + 8))

    def to_number(self, value: int) -> str:
        """ToNumber's ST0 as a driver-format 'D:<float64 bits>' (exact: every operand is a double)."""
        r = self.o.call(self.p["to_number"], "thiscall", [value])
        assert r.fpdepth == 1 and r.st0_bytes is not None
        d = r.st0_f64
        return "D:%016x" % struct.unpack("<Q", struct.pack("<d", d))[0]

    def to_integer(self, value: int) -> str:
        r = self.o.call(self.p["to_integer"], "thiscall", [value])
        return "I:%d" % struct.unpack("<i", struct.pack("<I", r.eax))[0]

    def boolean(self, swf: int, *args: int) -> str:
        o = self.o
        o.poke32(self.p["version"], swf)
        base = self.p["global_stack"]
        o.poke32(base + 8, self.arr)
        for i, v in enumerate(args):
            o.poke32(self.arr + 4 * i, v)
        o.poke32(base, len(args))
        o.poke(self.rec, b"\0" * 16)
        r = o.call(self.p["boolean"], "cdecl", [0, len(args)])
        return self._decode(r.eax)
