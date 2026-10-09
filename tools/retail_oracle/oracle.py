"""Python client for retail_oracle.exe (see README.md for the protocol).

    from oracle import Oracle, find_host, install_dir
    with Oracle("rotwk", "F:/RotWK/game.dat") as o:
        r = o.call(0x42B6C1, "cdecl", [o.cstr(b"abc")])
        print(hex(r.eax))

A fault or an unbound-import call in the retail code comes back as OracleError (never a crash).
Nothing here reads or stores retail bytes except through the helper at runtime.
"""
from __future__ import annotations

import os
import struct
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
HOST = HERE / "build" / "retail_oracle.exe"


class OracleError(RuntimeError):
    """The helper answered `err ...`; `.line` holds the full line."""

    def __init__(self, line: str):
        super().__init__(line)
        self.line = line
        self.fields = dict(tok.split("=", 1) for tok in line.split() if "=" in tok)


def find_host() -> Path | None:
    return HOST if HOST.is_file() else None


def install_dir(env: str, fallback: str) -> Path | None:
    """Retail install folder from the environment (ROTWK_INSTALL / BFME2_INSTALL), else a default."""
    for cand in (os.environ.get(env), fallback):
        if cand and (Path(cand) / "game.dat").is_file():
            return Path(cand)
    return None


@dataclass
class CallResult:
    eax: int
    ecx: int
    edx: int
    ebx: int
    esi: int
    edi: int
    ebp: int
    callee_pop: int
    cw: int
    mxcsr: int
    fpdepth: int
    st0_bytes: bytes | None  # 10-byte x87 extended value, or None when ST0 was empty
    xmm0: bytes
    raw: str = field(default="", repr=False)

    @property
    def st0_f64(self) -> float | None:
        """ST0 as a Python float (exact when ST0 holds a float32 or float64 value)."""
        return None if self.st0_bytes is None else _ext_to_float(self.st0_bytes)

    @property
    def st0_f32_bits(self) -> int | None:
        """ST0 stored as float32 (what `fstp dword` would write), as bits; None if empty."""
        f = self.st0_f64
        return None if f is None else struct.unpack("<I", struct.pack("<f", f))[0]

    @property
    def xmm0_f32(self) -> tuple[float, float, float, float]:
        return struct.unpack("<4f", self.xmm0)


def _ext_to_float(b: bytes) -> float:
    mant = int.from_bytes(b[:8], "little")
    se = int.from_bytes(b[8:10], "little")
    sign = -1.0 if se & 0x8000 else 1.0
    exp = se & 0x7FFF
    if exp == 0x7FFF:
        return sign * float("inf") if (mant << 1) & ((1 << 64) - 1) == 0 else float("nan")
    if exp == 0 and mant == 0:
        return sign * 0.0
    import math

    return sign * math.ldexp(mant, (exp or 1) - 16383 - 63)


class Oracle:
    def __init__(self, tag: str, game_dat: str | os.PathLike, binds: tuple[str, ...] = (), timeout: float = 30.0):
        host = find_host()
        if host is None:
            raise FileNotFoundError(f"{HOST} is not built; run tools/retail_oracle/build.bat")
        self._timeout = timeout
        self.tag = tag
        self._p = subprocess.Popen([str(host)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
        self._deadline = None
        self._lock = threading.Lock()  # guards _deadline and _timed_out between send() and the watchdog
        self._timed_out = False  # set by the watchdog BEFORE it kills the helper; never inferred from the exit status
        self._closing = False
        self._dead: str | None = None  # terminal failure; every later request raises OracleError with it
        try:
            threading.Thread(target=self._watchdog, daemon=True).start()
            for dll in binds:
                self.send(f"bind {dll}")
            self.load_info = self.send(f"load {tag} {game_dat}")
            self.fields = dict(tok.split("=", 1) for tok in self.load_info.split() if "=" in tok)
        except BaseException:
            self._terminate()  # no supervisor, worker or pipe outlives a failed construction
            raise

    # -- provenance -------------------------------------------------------------------------
    # Section names added to the RotWK image by the community patch (docs/STOPS.md S-001).
    PATCH_SECTIONS = ("stxt774", "stxt371", ".mackt", ".danetta")

    def section_names(self) -> list[str]:
        return [tok.split("@")[0].strip() for tok in self.send(f"info {self.tag}").split()]

    @property
    def caveats(self) -> list[str]:
        """Provenance caveats that apply to every value taken from this image (reported, never hidden)."""
        found = [s for s in self.section_names() if s in self.PATCH_SECTIONS]
        if found:
            return [f"S-001: community-patched image (sections {', '.join(found)}); not a pristine 2.01 oracle"]
        return []

    # -- plumbing ---------------------------------------------------------------------------
    def _watchdog(self) -> None:
        while not self._closing and self._p.poll() is None:
            time.sleep(0.25)
            with self._lock:
                d = self._deadline
                if d is None or time.monotonic() <= d:
                    continue
                # Record the cause first: the reader can see EOF before the process object reports an exit
                # status, and must still classify this as a timeout (it used to read "helper exited").
                self._timed_out = True
            self._p.kill()  # the supervisor's job object takes the worker down with it
            return

    def send(self, line: str) -> str:
        """Send one request, return the text after `ok `; raise OracleError on `err`."""
        assert "\n" not in line
        if self._dead:
            raise OracleError(f"err helper is gone: {self._dead}")
        # A retail function that never returns would hang the caller: the watchdog kills the helper.
        with self._lock:
            self._deadline = time.monotonic() + self._timeout
        try:
            self._p.stdin.write(line + "\n")
            self._p.stdin.flush()
            ans = self._p.stdout.readline().rstrip("\r\n")
        except (OSError, ValueError) as e:  # the pipe is dead: the helper was killed or exited
            ans = ""
            io_error = repr(e)
        else:
            io_error = None
        finally:
            with self._lock:
                self._deadline = None
        if not ans:
            # EOF. Reap the supervisor (bounded) before classifying: right after EOF a single poll() can still
            # return None while termination completes, which misreported a watchdog kill as a normal exit.
            try:
                self._p.wait(10)
            except subprocess.TimeoutExpired:
                pass  # still alive with a closed pipe: _terminate() kills it below; the exit status stays unknown
            with self._lock:
                timed_out = self._timed_out
            if timed_out:
                self._dead = f"timed out after {self._timeout}s (killed by the watchdog) on: {line[:80]}"
            elif io_error or self._p.returncode != 0:
                self._dead = f"died (exit {self._p.returncode}) on: {line[:80]}"
            else:
                self._dead = "exited"
            dead = self._dead
            self._terminate()
            raise OracleError("err helper exited" if dead == "exited" else f"err helper {dead}")
        if ans.startswith("err"):
            raise OracleError(ans)
        assert ans.startswith("ok"), ans
        return ans[3:]

    def _terminate(self) -> None:
        """Kill the supervisor (its job object takes the worker), reap it and close every pipe."""
        self._closing = True
        if self._p.poll() is None:
            self._p.kill()
        try:
            self._p.wait(10)
        except subprocess.TimeoutExpired:
            pass
        for pipe in (self._p.stdin, self._p.stdout, self._p.stderr):
            try:
                pipe.close()
            except (OSError, ValueError):
                pass

    def close(self) -> None:
        self._closing = True
        if not self._dead:
            try:
                self._p.stdin.write("quit\n")
                self._p.stdin.flush()
            except (OSError, ValueError):
                pass
            try:
                self._p.wait(self._timeout)
            except subprocess.TimeoutExpired:
                pass
        self._terminate()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- memory -----------------------------------------------------------------------------
    def alloc(self, size: int, addr: int | None = None) -> int:
        return int(self.send(f"alloc {size:x}" + (f" {addr:x}" if addr else "")), 16)

    def poke(self, addr: int, data: bytes) -> None:
        for i in range(0, len(data), 4096):
            self.send(f"poke {addr + i:x} {data[i:i + 4096].hex()}")

    def peek(self, addr: int, n: int) -> bytes:
        return bytes.fromhex(self.send(f"peek {addr:x} {n:x}"))

    def peek32(self, addr: int) -> int:
        return struct.unpack("<I", self.peek(addr, 4))[0]

    def poke32(self, addr: int, v: int) -> None:
        self.poke(addr, struct.pack("<I", v & 0xFFFFFFFF))

    def cstr(self, data: bytes) -> int:
        """Allocate a NUL-terminated copy of `data` in the helper and return its address."""
        a = self.alloc(len(data) + 1)
        self.poke(a, data + b"\0")
        return a

    def tmp_cstr(self, data: bytes) -> int:
        """Like cstr but reuses one scratch buffer (valid until the next tmp_cstr): for hot loops."""
        if not hasattr(self, "_scratch"):
            self._scratch = self.alloc(4096)
        self.poke(self._scratch, data + b"\0")
        return self._scratch

    def f32s(self, values) -> int:
        a = self.alloc(4 * len(values))
        self.poke(a, struct.pack(f"<{len(values)}f", *values))
        return a

    def hostfn(self, name: str) -> int:
        """Address of a host cdecl helper: 'alloc' (size, a, b) -> ptr, 'free' (ptr, a)."""
        return int(self.send(f"hostfn {name}"), 16)

    def sym(self, dll: str, name: str) -> int:
        """Address of an export of a DLL the helper has loaded (msvcr71.dll after `load`, kernel32.dll)."""
        return int(self.send(f"sym {dll} {name}"), 16)

    def code(self, machine_code: bytes) -> int:
        """Place a small shim of machine code in executable memory and return its address."""
        a = self.alloc(len(machine_code))
        self.poke(a, machine_code)
        return a

    # -- calls ------------------------------------------------------------------------------
    def call(self, va: int, conv: str = "cdecl", args=(), regs: dict | None = None) -> CallResult:
        toks = [f"call {self.tag} {va:x} {conv}"]
        for a in args:
            if isinstance(a, tuple) and a[0] == "d":  # ("d", 64-bit value)
                toks.append(f"d:{a[1]:x}")
            else:
                toks.append(f"{a & 0xFFFFFFFF:x}")
        for r, v in (regs or {}).items():
            toks.append(f"r:{r}={v & 0xFFFFFFFF:x}")
        text = self.send(" ".join(toks))
        kv = dict(tok.split("=", 1) for tok in text.split())
        st0 = None if kv["st0"] == "-" else bytes.fromhex(kv["st0"])
        return CallResult(
            *(int(kv[k], 16) for k in ("eax", "ecx", "edx", "ebx", "esi", "edi", "ebp", "callee_pop", "cw", "mxcsr")),
            fpdepth=int(kv["fpdepth"]),
            st0_bytes=st0,
            xmm0=bytes.fromhex(kv["xmm0"]),
            raw=text,
        )
