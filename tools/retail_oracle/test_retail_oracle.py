"""Tests for the retail oracle: real retail functions called on real hardware.

    python -m pytest tools/retail_oracle -rs

SKIPS (loudly, with the reason in the `-rs` summary) when retail_oracle.exe is not built or the
retail binaries are absent. Install folders come from ROTWK_INSTALL / BFME2_INSTALL, else the
documented defaults (F:\\RotWK, F:\\BFME2). No retail bytes are stored in the repo.

Where an expected value comes from is stated at each test: the PLAN/spec text, the engine's own
implementation on another lane (reproduced in refs.py with its citation), or a recorded
observation of the retail binary (these pin the harness and the finding, not a port).
"""
from __future__ import annotations

import os
import random
import struct
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import helpers  # noqa: E402
import refs  # noqa: E402
from lua_oracle import RetailLua  # noqa: E402
from oracle import Oracle, OracleError, find_host, install_dir  # noqa: E402

RW_DIR = install_dir("ROTWK_INSTALL", r"F:\RotWK")
B2_DIR = install_dir("BFME2_INSTALL", r"F:\BFME2")

if find_host() is None:
    pytest.skip("SKIPPED LOUDLY: tools/retail_oracle/build/retail_oracle.exe is not built (run tools/retail_oracle/build.bat)", allow_module_level=True)

helpers.require_fresh("retail_oracle")  # a helper older than its sources fails here with the rebuild command

needs_rw = pytest.mark.skipif(RW_DIR is None, reason="SKIPPED LOUDLY: RotWK game.dat not found (set ROTWK_INSTALL)")
needs_b2 = pytest.mark.skipif(B2_DIR is None, reason="SKIPPED LOUDLY: BFME2 game.dat not found (set BFME2_INSTALL)")


@pytest.fixture(scope="module")
def rw():
    with Oracle("rw", RW_DIR / "game.dat") as o:
        yield o


@pytest.fixture(scope="module")
def b2():
    with Oracle("b2", B2_DIR / "game.dat") as o:
        yield o


def random_names(rng: random.Random, n: int):
    alphabet = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_." + bytes(range(0x80, 0x100))
    for _ in range(n):
        yield bytes(rng.choice(alphabet) for _ in range(rng.randrange(0, 25)))


# ---------------------------------------------------------------------------------------------
# Host and protocol
# ---------------------------------------------------------------------------------------------
@needs_rw
@needs_b2
def test_images_map_at_their_preferred_base(rw, b2):
    # Both images link at 0x400000 (PE headers, read at runtime); one image per helper process.
    assert rw.fields["base"] == "00400000" and b2.fields["base"] == "00400000"
    assert int(rw.fields["size"], 16) == 0xAD3000 and int(b2.fields["size"], 16) == 0xADA000
    assert int(rw.fields["stub_imports"]) > 0 and int(rw.fields["real_imports"]) > 0


@needs_rw
def test_second_image_in_one_process_is_refused_not_overwritten(rw):
    with pytest.raises(OracleError, match="overlaps already-loaded image"):
        rw.send(f"load other {RW_DIR / 'game.dat'}")


@needs_rw
def test_memory_commands_and_errors(rw):
    a = rw.alloc(64)
    rw.poke(a, bytes(range(16)))
    assert rw.peek(a, 16) == bytes(range(16))
    rw.send(f"pokes {a:x} hello world")
    assert rw.peek(a, 12) == b"hello world\0"
    with pytest.raises(OracleError, match="read fault"):
        rw.peek(0x10, 4)
    with pytest.raises(OracleError, match="unknown command"):
        rw.send("frobnicate")
    with pytest.raises(OracleError, match="unknown calling convention"):
        rw.send("call rw 42b6c1 pascal 0")


def helper_count():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq retail_oracle.exe", "/NH"], capture_output=True, text=True).stdout
    return out.lower().count("retail_oracle.exe")


def test_failed_construction_cleans_up_its_helper_processes(monkeypatch, tmp_path):
    import time

    started = []
    real_popen = subprocess.Popen

    def recording_popen(*a, **k):
        p = real_popen(*a, **k)
        started.append(p)
        return p

    monkeypatch.setattr(subprocess, "Popen", recording_popen)
    before = helper_count()
    for _ in range(3):
        with pytest.raises(OracleError, match="cannot read"):
            Oracle("rw", tmp_path / "missing" / "game.dat")
    started = [p for p in started if "retail_oracle" in str(p.args[0])]
    assert len(started) == 3
    assert all(p.poll() is not None for p in started)  # supervisors are gone
    for _ in range(40):  # workers die with their supervisor's job object
        if helper_count() <= before:
            break
        time.sleep(0.25)
    assert helper_count() <= before


@needs_rw
def test_calls_after_the_watchdog_kills_the_helper_raise_oracle_error():
    # S: a retail call that never returns is killed by the watchdog; the client must stay in the
    # OracleError contract for every later request instead of leaking OSError from the dead pipe.
    with Oracle("rw", RW_DIR / "game.dat", timeout=1.0) as o:
        spin = o.code(bytes([0xEB, 0xFE]))  # jmp $
        with pytest.raises(OracleError, match="timed out after 1.0s"):
            o.call(spin, "cdecl", [])
        for request in (lambda: o.peek(0x400000, 4), lambda: o.alloc(16), lambda: o.call(spin, "cdecl", [])):
            with pytest.raises(OracleError, match="helper is gone"):
                request()


class _LaggingPopen:
    """A Popen whose exit status stays invisible to poll()/returncode until wait() is called.

    Simulates the race behind the old flake: the helper's stdout reaches EOF (the watchdog killed it) while
    the process object still reports "running" because termination has not completed yet.
    """

    def __init__(self, real):
        self._real = real
        self._reaped = False

    def __getattr__(self, name):  # stdin/stdout/stderr/args/kill/...
        return getattr(self._real, name)

    @property
    def returncode(self):
        return self._real.returncode if self._reaped else None

    def poll(self):
        return self._real.poll() if self._reaped else None

    def wait(self, timeout=None):
        rc = self._real.wait(timeout)
        self._reaped = True
        return rc


@needs_rw
def test_watchdog_kill_is_a_timeout_even_when_the_exit_status_lags_behind_eof():
    # Deterministic form of the race in the test above: EOF arrives, a single non-blocking poll() still says
    # "running". The client must (a) know from the watchdog that it caused the death and (b) reap before
    # classifying; before the fix this raised "err helper exited" instead of the timeout.
    with Oracle("rw", RW_DIR / "game.dat", timeout=1.0) as o:
        o._p = _LaggingPopen(o._p)
        spin = o.code(bytes([0xEB, 0xFE]))  # jmp $
        with pytest.raises(OracleError, match="timed out after 1.0s") as e:
            o.call(spin, "cdecl", [])
        assert "helper exited" not in str(e.value)
        assert o._p._reaped  # the supervisor was reaped before the verdict
        with pytest.raises(OracleError, match="helper is gone: timed out"):
            o.peek(0x400000, 4)


@needs_rw
def test_provenance_caveats_are_reported(rw):
    # S-001 (docs/STOPS.md): the RotWK image carries the community-patch sections; the oracle
    # reports that on every RW-derived value instead of hiding it.
    assert len(rw.caveats) == 1 and rw.caveats[0].startswith("S-001")
    for name in ("stxt774", "stxt371", ".mackt", ".danetta"):
        assert name in rw.caveats[0]


@needs_b2
def test_clean_bfme2_image_has_no_caveat(b2):
    assert b2.caveats == []


@needs_rw
def test_game_fpu_state_matches_retail_setfpmode(rw):
    # Retail setFPMode (RW 0x440809; PLAN rule 3 / rule 2): precision 24-bit, round to nearest.
    # The host's default control word must equal what retail's own routine leaves behind.
    r = rw.call(0x440809, "cdecl", [])
    assert r.cw == 0x007F
    assert rw.call(0x42B6C1, "cdecl", [rw.cstr(b"x")]).cw == 0x007F


@needs_rw
def test_fault_is_reported_and_helper_survives(rw):
    with pytest.raises(OracleError) as e:
        rw.call(0x42B6C1, "cdecl", [0])  # strlen-style loop on NULL
    assert e.value.fields["code"] == "c0000005" and e.value.fields["access"] == "read" and e.value.fields["addr"] == "00000000"
    assert e.value.fields["eip"] == "0042b6d2"  # mov cl, [edx] (RW 0x42B6D2), read from the disassembly
    assert rw.call(0x42B6C1, "cdecl", [rw.cstr(b"abc")]).eax == 0xBC6  # ((97*5)+98)*5+99


@needs_rw
def test_unbound_import_fails_loudly_naming_the_function(rw):
    data = (RW_DIR / "game.dat").read_bytes()
    slot = iat_slot(data, "advapi32.dll", "GetUserNameA")
    shim = rw.code(bytes([0xFF, 0x15]) + struct.pack("<I", slot) + bytes([0xC3]))  # call [IAT]; ret
    with pytest.raises(OracleError, match=r"stub import called: rw:advapi32\.dll!GetUserNameA"):
        rw.call(shim, "stdcall", [0, 0])


def iat_slot(data: bytes, dll: str, func: str) -> int:
    """VA of the IAT slot for dll!func, read from the PE import directory."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = pe + 24
    base = struct.unpack_from("<I", data, opt + 28)[0]
    first = opt + struct.unpack_from("<H", data, pe + 20)[0]
    secs = [struct.unpack_from("<8sIIII", data, first + i * 40) for i in range(nsec)]
    irva = struct.unpack_from("<I", data, opt + 96 + 8)[0]

    def off(rva):
        for _n, vs, va, rs, rp in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + rp
        raise ValueError(hex(rva))

    p = off(irva)
    while True:
        oft, _ts, _fc, nm, ft = struct.unpack_from("<5I", data, p)
        if not nm:
            raise KeyError(f"{dll}!{func}")
        name = data[off(nm):data.index(b"\0", off(nm))].decode().lower()
        if name == dll.lower():
            t = off(oft or ft)
            i = 0
            while True:
                v = struct.unpack_from("<I", data, t + 4 * i)[0]
                if not v:
                    break
                if not v & 0x80000000:
                    o = off(v)
                    if data[o + 2:data.index(b"\0", o + 2)].decode() == func:
                        return base + ft + 4 * i
                i += 1
        p += 20


# ---------------------------------------------------------------------------------------------
# Macro hash: RW 0x42B6C1 (core), RW 0x42BC44 (lower-casing wrapper); BFME2 0x42BA61 (core)
# Expected: the REAL compiled engine INIMacroTable::hash (engine/src/Common/INI/INIMacro.cpp, through
# build/hash_driver.exe) and spec section 9.1. refs.macro_hash is only a cross-check of the driver.
# ---------------------------------------------------------------------------------------------
def engine_hashes(names):
    """(INIMacroTable::hash, AptPropertyMap::hash16) of the compiled engine for each name, from build/hash_driver.exe."""
    driver = Path(__file__).resolve().parent / "build" / "hash_driver.exe"
    if not driver.is_file():
        pytest.skip("SKIPPED LOUDLY: build/hash_driver.exe is not built (run tools/retail_oracle/build.bat)")
    helpers.require_fresh("hash_driver")
    out = subprocess.run([str(driver)], input=b"\n".join(names) + b"\n", capture_output=True, check=True).stdout.decode().splitlines()
    assert len(out) == len(names)
    return [tuple(int(x, 16) for x in line.split()) for line in out]


def engine_macro_hashes(names):
    return [h[0] for h in engine_hashes(names)]


def engine_apt_hashes(names):
    return [h[1] for h in engine_hashes(names)]


def lowercase(name):
    return name.translate(bytes(i + 32 if 65 <= i <= 90 else i for i in range(256)))


def test_engine_hash_driver_agrees_with_the_python_cross_check():
    names = [n.replace(b"\0", b"_") for n in random_names(random.Random(1), 2000)]
    assert engine_macro_hashes(names) == [refs.macro_hash(n) for n in names]
    # the Python transcription of the APT hash is only a cross-check of the compiled engine hash now
    assert engine_apt_hashes(names) == [refs.apt_hash(n) for n in names]


@needs_rw
@needs_b2
def test_macro_hash_core_is_byte_identical_in_rotwk_and_bfme2(rw, b2):
    # Shows the (possibly patched) RW function equals the clean BFME2 1.06 one: no RW-only caveat for it.
    assert rw.peek(0x42B6C1, 0x18) == b2.peek(0x42BA61, 0x18)


@needs_rw
def test_macro_hash_core_matches_engine_on_10000_random_names(rw):
    names = [n.replace(b"\0", b"_") for n in random_names(random.Random(20260930), 10000)]
    want = engine_macro_hashes(names)
    bad = []
    for name, w in zip(names, want):
        got = rw.call(0x42B6C1, "cdecl", [rw.tmp_cstr(lowercase(name))]).eax
        if got != w:
            bad.append((name, got, w))
    assert not bad, bad[:5]


@needs_b2
def test_macro_hash_core_bfme2_matches_engine_on_10000_random_names(b2):
    names = [n.replace(b"\0", b"_") for n in random_names(random.Random(20260931), 10000)]
    want = engine_macro_hashes(names)
    bad = []
    for name, w in zip(names, want):
        got = b2.call(0x42BA61, "cdecl", [b2.tmp_cstr(lowercase(name))]).eax
        if got != w:
            bad.append((name, got, w))
    assert not bad, bad[:5]


def prime_asciistring_runtime(rw: Oracle) -> None:
    """Make RW's AsciiString usable without running the game's startup:
    allocator hooks (RW 0xDC5E44 alloc, 0xDC5E3C free) -> host malloc/free, and the string lock
    singleton (0xDC6268) constructed by its own constructor with the 'initialised' flag set so the
    lazy initialiser at 0x4355D0 (which needs the game's CRT atexit) is skipped."""
    rw.poke32(0xDC5E44, rw.hostfn("alloc"))
    rw.poke32(0xDC5E3C, rw.hostfn("free"))
    rw.call(0x440B21, "thiscall", [0xDC6268, 1])
    rw.poke32(0xDC628C, 1)


@needs_rw
def test_macro_hash_wrapper_lowercases_like_retail_on_2000_random_names(rw):
    # RW 0x42BC44 takes an AsciiString& (stdcall, ret 4): it copies it, lower-cases through the CRT
    # tolower (0x4363B0), hashes with 0x42B6C1 and releases the copy (spec section 9.1: "the name is lower-cased").
    prime_asciistring_runtime(rw)
    names = [n.replace(b"\0", b"_") for n in random_names(random.Random(7), 2000)]
    want = engine_macro_hashes(names)
    bad = []
    data, obj = rw.alloc(64), rw.alloc(4)
    rw.poke32(obj, data)
    for name, w in zip(names, want):
        rw.poke(data, struct.pack("<IHH", 1, len(name), len(name) + 1) + name + b"\0")  # refcount, length, capacity, chars
        r = rw.call(0x42BC44, "stdcall", [obj])
        assert r.callee_pop == 4
        if r.eax != w:
            bad.append((name, r.eax, w))
        assert rw.peek32(data) == 1  # caller's buffer keeps refcount 1: the copy was released
    assert not bad, bad[:5]


# ---------------------------------------------------------------------------------------------
# Functions that exist in both engines. BFME2 1.06 is the donor binary, RotWK the target (PLAN
# rule 1): each is run on both. The RW addresses were found with counterpart.py (whole-function
# match: same instructions, only addresses differ; test_counterparts_are_located_by_instruction_match).
#   APT property hash       BFME2 0xAD3800 / wrapper 0xAD3D10      RW 0xAE79A0 / 0xAE7EB0
#   nlerp                   BFME2 0xB17550 (rsqrt 0x44233A)         RW 0xB2B720 (rsqrt 0x441C56)
# ---------------------------------------------------------------------------------------------
@pytest.fixture(scope="module")
def images():
    pytest.importorskip("capstone")
    if RW_DIR is None or B2_DIR is None:
        pytest.skip("SKIPPED LOUDLY: both game.dat files are needed (ROTWK_INSTALL, BFME2_INSTALL)")
    return (B2_DIR / "game.dat").read_bytes(), (RW_DIR / "game.dat").read_bytes()


# (BFME2 VA, RW VA, status). "equivalent": the code and every external callee matched recursively.
# Equals2 is "candidate": its own bytes are identical, but nine external callees (the CRT string
# conversion helpers, getFloat/ToNumber and friends reached through them) do not validate, so
# "RW's Equals2 behaves like BFME2's" is shown only by running both (the Equals2 tests below).
COUNTERPARTS = [
    (0xB17550, 0xB2B720, "equivalent"),  # nlerp
    (0x44233A, 0x441C56, "equivalent"),  # fast reciprocal square root
    (0xAD3800, 0xAE79A0, "equivalent"),  # APT hash
    (0xAD3D10, 0xAE7EB0, "equivalent"),  # APT hash wrapper
    (0x42BA61, 0x42B6C1, "equivalent"),  # macro hash core
    (0xB031E0, 0xB17370, "candidate"),  # Equals2
]
EQUALS2_UNVALIDATED = {
    (0xA29940, 0xA3D69E), (0xA29970, 0xA3D6D4), (0xA29982, 0xA3D6EC), (0xACD070, 0xAE11E0), (0xADD460, 0xAF1630),
    (0xADD6C0, 0xAF1890), (0xAF4290, 0xB084A0), (0xAF4460, 0xB08670), (0xAFC370, 0xB10560),
}


def internal_rel32_jumps(a, va):
    """(offset of rel32 field, target) for every near jump/jcc of the function at `va` that stays inside it."""
    import counterpart

    md, cs = counterpart._md()
    length = counterpart.extent(a, va)
    out = []
    for i in md.disasm(counterpart.read_va(a, va, length), va):
        if i.group(cs.CS_GRP_JUMP) and i.imm_size == 4 and i.operands[0].type == cs.x86.X86_OP_IMM and va <= i.operands[0].imm < va + length:
            out.append((i.address - va + i.imm_offset, i.operands[0].imm))
    return out


def patched(b, va, offset, data):
    from disasm import va_to_offset

    m = bytearray(b)
    o = va_to_offset(b, va) + offset
    m[o:o + len(data)] = data
    return bytes(m)


def test_counterparts_are_located_and_classified(images):
    import counterpart

    a, b = images
    for va, rw_va, status in COUNTERPARTS:
        found = counterpart.find(a, b, va)
        assert [m.va for m in found] == [rw_va], (hex(va), [hex(m.va) for m in found])
        assert found[0].status == status, (hex(va), found[0].status, found[0].problems[:3])
        if status == "candidate":
            assert {(f, t) for f, t, ok in found[0].externals if not ok} == EQUALS2_UNVALIDATED
        else:
            assert all(ok for _f, _t, ok in found[0].externals) and not found[0].problems


def test_counterpart_spans_cover_every_function_through_its_return(images):
    import counterpart

    a, _ = images
    md, _cs = counterpart._md()
    for va, _rw, _status in COUNTERPARTS:
        length = counterpart.extent(a, va)
        last = list(md.disasm(counterpart.read_va(a, va, length), va))[-1]
        assert last.mnemonic == "ret" and last.address + last.size == va + length, hex(va)
    assert counterpart.extent(a, 0x44233A) == 0x52  # `ret 4` is 3 bytes: a 0x50 span ends mid-instruction
    assert counterpart.extent(a, 0xAD3D10) == 0x98


def test_counterpart_rejects_a_span_that_ends_mid_instruction(images):
    import counterpart

    a, _ = images
    with pytest.raises(ValueError, match="incomplete decode"):
        counterpart.pattern(a, 0x44233A, 0x50)
    with pytest.raises(ValueError, match="incomplete decode"):
        counterpart.pattern(a, 0xAD3D10, 0x80)


def test_counterpart_mutation_replacing_the_tail_with_int3_fails(images):
    import counterpart

    a, b = images
    for va, rw_va, _status in COUNTERPARTS:
        length = counterpart.extent(a, va)
        assert counterpart.find(a, patched(b, rw_va, length - 1, b"\xCC"), va) == [], hex(va)  # the final ret byte
        assert counterpart.find(a, patched(b, rw_va, length - 3, b"\xCC\xCC\xCC"), va) == [], hex(va)


def test_counterpart_mutation_redirecting_an_internal_branch_fails(images):
    import counterpart

    a, b = images
    # Equals2: its near (rel32) jumps and jccs that stay inside the function
    jumps = internal_rel32_jumps(a, 0xB031E0)
    assert len(jumps) >= 3
    for off, _target in jumps[:3]:
        bent = patched(b, 0xB17370, off, struct.pack("<i", 0x40))
        assert counterpart.find(a, bent, 0xB031E0) == [], hex(off)
    # nlerp: `jbe 0xB17609` at BFME2 0xB175B7 is a short (rel8) jump over the negative-dot block
    assert a[counterpart.va_to_offset(a, 0xB175B7)] == 0x76
    bent = patched(b, 0xB2B720, 0xB175B7 - 0xB17550 + 1, bytes([0x10]))
    assert counterpart.find(a, bent, 0xB17550) == []


def test_counterpart_mutation_redirecting_an_external_call_is_only_a_candidate(images):
    import counterpart

    a, b = images
    pat = counterpart.pattern(a, 0xB17550, counterpart.extent(a, 0xB17550))
    (off, _target), = pat.externals  # nlerp has exactly one external: the rsqrt call
    macro_hash = 0x42B6C1
    site = 0xB2B720 + off
    bent = patched(b, 0xB2B720, off, struct.pack("<i", macro_hash - (site + 4)))
    found = counterpart.find(a, bent, 0xB17550)
    assert [m.va for m in found] == [0xB2B720]  # the bytes of nlerp itself still match
    assert found[0].status == "candidate" and found[0].problems  # but its callee is not the rsqrt counterpart
    assert (0x44233A, macro_hash, False) in found[0].externals


HASH = {"b2": (0xAD3800, 0xAD3D10), "rw": (0xAE79A0, 0xAE7EB0)}
NLERP = {"b2": 0xB17550, "rw": 0xB2B720}
IMAGES = ["b2", "rw"]


def image(request, tag):
    if tag == "rw" and RW_DIR is None:
        pytest.skip("SKIPPED LOUDLY: RotWK game.dat not found (set ROTWK_INSTALL)")
    if tag == "b2" and B2_DIR is None:
        pytest.skip("SKIPPED LOUDLY: BFME2 game.dat not found (set BFME2_INSTALL)")
    return request.getfixturevalue(tag)


@pytest.mark.parametrize("tag", IMAGES)
def test_apt_hash_core_matches_engine_on_10000_random_names(request, tag):
    # Expected: the REAL compiled engine AptPropertyMap::hash16 (engine/src/Libraries/Source/Apt/AptObject.cpp, FNV-1a
    # over lower-cased signed bytes, 16 bits, 0 -> 0x4567) through build/hash_driver.exe; refs.apt_hash is only the
    # cross-check of the driver (test_engine_hash_driver_agrees_with_the_python_cross_check).
    o = image(request, tag)
    rng = random.Random(11)
    names = [n.replace(b"\0", b"_") for n in random_names(rng, 10000)]
    want = engine_apt_hashes(names)
    bad = []
    for name, w in zip(names, want):
        got = o.call(HASH[tag][0], "cdecl", [o.tmp_cstr(name)]).eax & 0xFFFF
        if got != w:
            bad.append((name, got, w))
    assert not bad, bad[:5]


@pytest.mark.parametrize("tag", IMAGES)
def test_apt_hash_wrapper_caches_in_the_string_header(request, tag):
    # thiscall, ecx -> object whose first dword is the data pointer; +6 caches the hash, chars at +8.
    o = image(request, tag)
    names = (b"_root", b"GotoAndPlay", b"onEnterFrame", b"x")
    want = dict(zip(names, engine_apt_hashes(list(names))))  # the compiled engine's hash16
    for name in names:
        data = o.alloc(32)
        o.poke(data, struct.pack("<IHH", 1, len(name), 0) + name + b"\0")  # +6 = cached hash, empty
        obj = o.alloc(4)
        o.poke32(obj, data)
        r = o.call(HASH[tag][1], "thiscall", [obj])
        assert r.eax & 0xFFFF == want[name]
        assert struct.unpack("<H", o.peek(data + 6, 2))[0] == want[name]
        # with the cache already filled the function recomputes, checks it equals the cache, and returns it
        assert o.call(HASH[tag][1], "thiscall", [obj]).eax & 0xFFFF == want[name]


# ---------------------------------------------------------------------------------------------
# nlerp: cdecl(out*, a*, b*, t)   Expected: refs.nlerp_bfme2 (read from the disassembly: SSE
# blend, then the x87 fast reciprocal square root at PC24).
# ---------------------------------------------------------------------------------------------
def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def call_nlerp(o, tag, a, b, t):
    if not hasattr(o, "nl_buf"):
        o.nl_buf = o.alloc(48)
    A, B, out = o.nl_buf, o.nl_buf + 16, o.nl_buf + 32
    o.poke(A, struct.pack("<8f", *a, *b))
    r = o.call(NLERP[tag], "cdecl", [out, A, B, refs.f32_bits(t)])
    assert r.callee_pop == 0 and r.fpdepth == 0
    return list(struct.unpack("<4I", o.peek(out, 16)))


@pytest.mark.parametrize("tag", IMAGES)
def test_nlerp_matches_hand_model_bit_for_bit_on_2000_random_pairs(request, tag):
    o = image(request, tag)
    rng = random.Random(5)
    bad = []
    for _ in range(2000):
        a = [f32(rng.uniform(-1, 1)) for _ in range(4)]
        b = [f32(rng.uniform(-1, 1)) for _ in range(4)]
        t = f32(rng.random())
        got, want = call_nlerp(o, tag, a, b, t), refs.nlerp_bfme2(a, b, t)
        if got != want:
            bad.append((a, b, t, [hex(x) for x in got], [hex(x) for x in want]))
    assert not bad, bad[:3]


@pytest.mark.parametrize("tag", IMAGES)
def test_nlerp_branches_and_zero_length(request, tag):
    # dot < 0 flips the blend; a zero blend skips the normalisation (ucomiss ... jnp, B17692-B1769F).
    o = image(request, tag)
    zero = [0.0] * 4
    assert call_nlerp(o, tag, zero, zero, 0.5) == [0, 0, 0, 0]
    a, b = [1.0, 0.0, 0.0, 0.0], [-0.6, 0.8, 0.0, 0.0]  # dot < 0
    assert call_nlerp(o, tag, a, b, 0.25) == refs.nlerp_bfme2(a, b, 0.25)
    # t = 0 returns a (already unit length): normalisation leaves it within one ulp
    got0 = [struct.unpack("<f", struct.pack("<I", x))[0] for x in call_nlerp(o, tag, a, a, 0.0)]
    assert abs(got0[0] - 1.0) < 1e-6 and got0[1:] == [0.0, 0.0, 0.0]


# ---------------------------------------------------------------------------------------------
# APT Equals2: BFME2 0xB031E0, RW 0xB17370, on fake integer/float/boolean/undefined values.
# Expected: lane/apt-1 engine/tests/test_apt_value.cpp "Equals2 (BFME2 handler 0x00B031E0)" cases,
# which that lane traced by hand. eq(top, under) means "top on the stack top, under below it".
# ---------------------------------------------------------------------------------------------
NAN = float("nan")


@pytest.mark.parametrize("tag", IMAGES)
def test_apt_equals2_matches_the_hand_traced_cases(request, tag):
    from apt_oracle import AptEquals2

    o = image(request, tag)
    a = AptEquals2(o, tag)

    def eq(top, under, ver=7):
        return a.equals2(under, top, ver)

    # mixed integer/float: |a-b| < 0.001 (0x00B03690); float/float exact (fucompp); int/int exact
    assert eq(a.int_(1), a.float_(1.0005)) is True
    assert eq(a.float_(1.0005), a.int_(1)) is True
    assert eq(a.int_(1), a.float_(1.002)) is False
    assert eq(a.float_(1.0005), a.float_(1.0)) is False
    assert eq(a.float_(1.5), a.float_(1.5)) is True
    assert eq(a.int_(5), a.int_(5)) is True and eq(a.int_(5), a.int_(6)) is False
    assert eq(a.int_(3), a.float_(3.0)) is True
    assert eq(a.float_(NAN), a.float_(NAN)) is False  # unordered is not equal
    # the integer is rounded to float on top (0x00B0361E) and exact underneath (fisub 0x00B03664)
    assert a.equals2(a.int_(16777217), a.float_(16777216.0)) is False  # under = int, top = float
    assert a.equals2(a.float_(16777216.0), a.int_(16777217)) is True
    assert a.equals2(a.int_(16777216), a.float_(16777216.0)) is True
    assert a.equals2(a.float_(16777216.0), a.int_(16777216)) is True
    # a boolean on top compares toInteger; underneath it falls to identity
    assert eq(a.bool_(True), a.int_(1)) is True
    assert eq(a.int_(1), a.bool_(True)) is False
    assert eq(a.bool_(False), a.int_(0)) is True
    assert eq(a.bool_(True), a.bool_(True)) is True
    # undefined: SWF 7 decides by count, SWF 6 through the type dispatch (same answers here)
    u = a.undefined
    for ver in (6, 7):
        assert eq(u, u, ver) is True
        assert eq(u, a.int_(0), ver) is False
        assert eq(a.int_(0), u, ver) is False


@needs_b2
def test_stop_s042_equals2_oracle_has_no_string_or_object_operands(b2):
    # docs/STOPS.md S-042: apt-1's string and object Equals2 cases stay hand-traced; the oracle says so.
    from apt_oracle import AptEquals2

    a = AptEquals2(b2, "b2")
    with pytest.raises(NotImplementedError, match="S-042"):
        a.string_("3")
    with pytest.raises(NotImplementedError, match="S-042"):
        a.object_()


@pytest.mark.parametrize("tag", IMAGES)
def test_apt_equals2_int_float_epsilon_on_random_pairs(request, tag):
    # Model from the disassembly: both integers -> exact; both floats -> exact; int + float -> the
    # integer is rounded to float32 when it is on top, kept exact when underneath, and the test is
    # fabs(a - b) < 0.001f (the constant at BFME2 0xBC28F8).
    from apt_oracle import AptEquals2

    o = image(request, tag)
    a = AptEquals2(o, tag)
    rng = random.Random(21)
    bad = []
    for _ in range(300):
        i = rng.choice([rng.randrange(-5, 6), rng.randrange(-(1 << 24) - 8, (1 << 24) + 8)])
        f = f32(i + rng.choice([0.0, 0.0004, -0.0004, 0.0011, 0.002, 0.5]))
        for under_int in (True, False):
            under, top = (a.int_(i), a.float_(f)) if under_int else (a.float_(f), a.int_(i))
            got = a.equals2(under, top)
            ii = i if under_int else f32(i)  # integer on top goes through fild + fstp dword
            diff = refs.r24(abs(refs.Fraction(f) - refs.Fraction(ii)))  # the subtraction rounds at PC24
            want = diff < refs.Fraction(f32(0.001))
            if got != want:
                bad.append((i, f, under_int, got, want))
    assert not bad, bad[:5]


# ---------------------------------------------------------------------------------------------
# Duration parsers (RW 0x73A429 / 0x73A403): the core multiply, using the retail bytes themselves.
# The whole parsers need an INI object (token buffer); the harness runs the exact instruction
# sequences inside a minimal frame (README, "shims").
# Expected: PLAN rule 2 (ceil(ms * 0.005f) at PC24; 52,428,805 ms -> 262145) and refs.duration_*.
# ---------------------------------------------------------------------------------------------
def duration_shims(rw):
    body = rw.peek(0x73A440, 0x1B)  # test eax,eax ... fild ... fmul [0.005f] ... fstp qword [esp]
    frame = bytes([0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x45, 0x08])  # push ebp; mov ebp,esp; push ecx; mov eax,[ebp+8]
    product = rw.code(frame + body + bytes([0x58, 0x5A, 0xC9, 0xC3]))  # pop eax; pop edx; leave; ret -> edx:eax = double
    unsigned = rw.code(frame + rw.peek(0x73A440, 0x21) + rw.peek(0x73A461, 2) + bytes([0xB8]) + struct.pack("<I", 0xA3CFA4) + bytes([0xFF, 0xD0, 0xC9, 0xC3]))
    real = rw.code(bytes([0xD9, 0x44, 0x24, 0x10]) + rw.peek(0x73A418, 0x11))  # fld dword [esp+0x10]; retail 0x73A418-0x73A428
    return product, unsigned, real


@needs_rw
def test_duration_constant_and_known_values(rw):
    assert rw.peek32(0xD9F610) == 0x3BA3D70A  # the float32 0.005f the parsers multiply by
    assert rw.peek32(0xBD8698) == 0x4F800000  # 2^32 added for inputs >= 2^31
    _, unsigned, _ = duration_shims(rw)
    assert rw.call(unsigned, "cdecl", [52428805]).eax == 262145  # PLAN rule 2
    assert rw.call(unsigned, "cdecl", [500]).eax == 3  # spec 9.1: 500 ms -> 3 frames
    assert rw.call(unsigned, "cdecl", [0]).eax == 0


@needs_rw
def test_duration_unsigned_matches_model_on_random_inputs(rw):
    product, unsigned, _ = duration_shims(rw)
    rng = random.Random(3)
    cases = [1, 2, 3, 100, 200, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFF] + [rng.getrandbits(32) for _ in range(1000)] + [rng.randrange(100000) for _ in range(1000)]
    bad = []
    for ms in cases:
        r = rw.call(product, "cdecl", [ms])
        got = (r.edx << 32) | r.eax
        want = refs.duration_product(ms, 0x3BA3D70A)
        frames = rw.call(unsigned, "cdecl", [ms]).eax
        if got != want or frames != refs.duration_frames(ms, 0x3BA3D70A):
            bad.append((ms, hex(got), hex(want), frames))
    assert not bad, bad[:5]


@needs_rw
def test_duration_real_core_matches_model_on_random_inputs(rw):
    _, _, real = duration_shims(rw)
    rng = random.Random(4)
    store = rw.alloc(8)
    bad = []
    for _ in range(1500):
        x = f32(rng.choice([rng.uniform(0, 5000), rng.randrange(0, 100000), 500.0, rng.uniform(0, 1)]))
        rw.call(real, "cdecl", [0, 0, store, refs.f32_bits(x)])
        got = rw.peek32(store)
        want = refs.to_f32(refs.Fraction(x) * refs.Fraction(refs.bits_f32(0x3BA3D70A)))
        if got != want:
            bad.append((x, hex(got), hex(want)))
    assert not bad, bad[:5]
    rw.call(real, "cdecl", [0, 0, store, refs.f32_bits(500.0)])
    assert struct.unpack("<f", struct.pack("<I", rw.peek32(store)))[0] == 2.5  # spec 9.1: no ceil, 500 ms is 2.5


@needs_rw
def test_duration_matches_engine_numericstate_on_10007_inputs(rw):
    # engine/src/Common/System/NumericState.cpp (merged from INI-1) through numeric_driver.exe, built by
    # build.bat. durationProduct is the double the retail sequence stores; ceilScaled adds the ceil.
    driver = Path(__file__).resolve().parent / "build" / "numeric_driver.exe"
    if not driver.is_file():
        pytest.skip("SKIPPED LOUDLY: build/numeric_driver.exe is not built (run tools/retail_oracle/build.bat)")
    helpers.require_fresh("numeric_driver")
    product, unsigned, _ = duration_shims(rw)
    rng = random.Random(77)
    cases = [0, 1, 500, 52428805, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF] + [rng.getrandbits(32) for _ in range(5000)] + [rng.randrange(100000) for _ in range(5000)]
    assert len(cases) == 10007
    text = "".join(f"{c:x} 3ba3d70a\n" for c in cases)
    out = subprocess.run([str(driver)], input=text, capture_output=True, text=True, check=True).stdout.splitlines()
    assert len(out) == len(cases)
    bad = []
    for ms, line in zip(cases, out):
        bits, frames = line.split()
        r = rw.call(product, "cdecl", [ms])
        if int(bits, 16) != (r.edx << 32 | r.eax) or int(frames) != rw.call(unsigned, "cdecl", [ms]).eax:
            bad.append((ms, bits, frames))
    assert not bad, bad[:5]


def x87_oracle_exe():
    here = Path(__file__).resolve().parent.parent / "x87_oracle" / "build" / "x87_oracle.exe"
    for cand in (os.environ.get("X87_ORACLE"), here):
        if cand and Path(cand).is_file():
            return str(cand)
    return None


@needs_rw
def test_duration_product_agrees_with_the_single_instruction_x87_oracle(rw):
    exe = x87_oracle_exe()
    if exe is None:
        pytest.skip("SKIPPED LOUDLY: tools/x87_oracle is not built (run tools/retail_oracle/build.bat); set X87_ORACLE to its exe to run this comparison")
    if not os.environ.get("X87_ORACLE"):  # an explicit override is the caller's own binary: its sources are unknown
        helpers.require_fresh("x87_oracle")
    product, _, _ = duration_shims(rw)
    rng = random.Random(9)
    cases = [0, 500, 52428805, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF] + [rng.getrandbits(32) for _ in range(300)]
    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        for ms in cases:
            p.stdin.write(f"dur {ms:x} 3ba3d70a\n")
            p.stdin.flush()
            theirs = int(p.stdout.readline(), 16)
            r = rw.call(product, "cdecl", [ms])
            assert (r.edx << 32) | r.eax == theirs, ms
    finally:
        p.stdin.close()
        p.wait()


# ---------------------------------------------------------------------------------------------
# Lua: EA's Lua 4.0.1 inside game.dat (spec lua-scripting.md section 9; gap G8)
# Plain-Lua expectations come from the Lua 4.0 manual; EA boolean results are recorded observations
# of retail (they pin the harness and are the oracle values the Lua lane lacked).
# ---------------------------------------------------------------------------------------------
@pytest.fixture(scope="module")
def lua(rw):
    return RetailLua(rw)


@needs_rw
def test_lua_round_trip_stock_semantics(lua):
    assert lua.run("return _VERSION") == (0, [("string", "Lua 4.0.1")])  # spec step 2
    assert lua.run("return 1+2") == (0, [("number", 3.0)])
    assert lua.run("return 10/4") == (0, [("number", 2.5)])
    assert lua.run("return 'a'..'b'") == (0, [("string", "ab")])
    assert lua.run("a = 3 return a == 3 and 'eq' or 'ne'") == (0, [("string", "eq")])
    assert lua.run("return strlen('abc')") == (0, [("number", 3.0)])
    assert lua.run("return foo(")[0] == 3  # LUA_ERRSYNTAX
    assert lua.run("error('boom')")[0] == 1  # LUA_ERRRUN, stack left empty by the protected call


@needs_rw
def test_lua_library_openers_register_the_recorded_function_counts(rw):
    # Recorded from retail: the five openers called by 0x734478 add 37/19/11/23/5 function-valued
    # globals. The spec (lua-scripting.md 2.4, "33/11/11/23/5") disagrees for the first two: the
    # first opener is stock 4.0.1's base library (37 functions) and the second its io library (19).
    L = rw.call(0xB62250, "cdecl", [0x100]).eax
    counts, prev = [], 0
    for opener in (0xB60B90, 0xB5F820, 0xB5E300, 0xB5CCA0, 0xB5C5D0):
        rw.call(opener, "cdecl", [L])
        rw.call(0xB5B190, "cdecl", [L, 0])
        rw.call(0xB614B0, "cdecl", [L, rw.cstr(b"n=0 foreach(globals(), function(k,v) if type(v)=='function' then n=n+1 end end)")])
        rw.call(0xB5B910, "cdecl", [L, rw.cstr(b"n")])  # lua_getglobal
        n = int(rw.call(0xB5B570, "cdecl", [L, -1]).st0_f64)
        rw.call(0xB5B190, "cdecl", [L, 0])
        counts.append(n - prev)
        prev = n
    assert counts == [37, 19, 11, 23, 5]


@needs_rw
def test_lua_ea_boolean_semantics_recorded_from_retail(lua):
    # Stock 4.0.1 has no boolean type; EA's tag 6 appears as comparison and `not` results, and
    # `nil and x` / `1 and nil` follow the engine's patched jumps. (Spec 7.G8: these had no external value.)
    expected = {
        "return 3 < 4": [("boolean", True)],
        "return 1 == 1": [("boolean", True)],
        "return 1 ~= 2": [("boolean", True)],
        "return nil == false": [("boolean", False)],
        "return nil == nil": [("boolean", True)],
        "return not nil": [("boolean", False)],
        "return not 1": [("boolean", False)],
        "return not not nil": [("boolean", False)],
        "return true": [("boolean", True)],
        "return false": [("boolean", False)],
        "return nil and 1": [("boolean", False)],
        "return 1 and nil": [("nil", None)],
        "return 0 and 1": [("number", 1.0)],
        "return false or 5": [("number", 5.0)],
        "return true and 7": [("number", 7.0)],
        "return tostring(1<2)": [("string", "true")],
    }
    got = {src: lua.run(src)[1] for src in expected}
    assert got == expected


@needs_rw
def test_stop_s041_type_of_a_boolean_reads_past_the_typename_table(rw, lua):
    # docs/STOPS.md S-041. lua_typename (RW 0xB5B320) indexes the 6-entry table at 0xD0B298 with the
    # type tag; EA's boolean tag is 6, so `type(true)` reads the next 4 bytes of .rdata ("tabl") as a
    # char*. Retail faults here; there is no value to port, so the port must report it explicitly.
    with pytest.raises(OracleError) as e:
        lua.run("return type(true)")
    assert e.value.fields["code"] == "c0000005" and e.value.fields["access"] == "read" and e.value.fields["addr"] == "6c626174"
    table = struct.unpack("<8I", rw.peek(0xD0B298, 32))
    assert table[6] == 0x6C626174  # the 7th "entry" is the bytes of the string "table"
    assert RetailLua(rw).run("return 1") == (0, [("number", 1.0)])  # the helper stays usable after the fault
