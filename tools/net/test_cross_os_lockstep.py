"""Lane WIN-1 r2 (Sol r1): tools/net/cross_os_lockstep.sh must fail unless every count is right.

The script runs a Linux openbfme_peer and a Windows one under wine. Here both are a stub peer (a Python script writing the reports, hash files and
replay reports a real peer writes) and `wine` is a stub that runs its argument, so the gate logic is tested without the game: a faithful run passes;
a run that did no CRC check, wrote no hashes, stopped early, compared too few replay hashes or reported another engine id fails.
"""
import os
import stat
import subprocess
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "cross_os_lockstep.sh")

STUB_PEER = r'''#!/usr/bin/env python3
import os, sys
args = sys.argv[1:]
def opt(name, default=None):
    return args[args.index(name) + 1] if name in args else default
mode = os.environ.get("STUB_MODE", "good")
frames = int(opt("--frames", "0"))
report = opt("--report")
if "--replay" in args:
    frames = int(open(opt("--replay")).read())
    compared = frames - 1 if mode != "short-replay" else frames // 2
    open(report, "w").write("frames %d of %d\nhashes_compared %d\nmismatches 0\nfinal_hash 0x12345678 recorded 0x12345678\n" % (frames if mode != "short-replay" else frames, frames, compared))
    sys.exit(0)
crc = 0 if mode == "no-crc" else (frames - 3) // 100
engine = "b" * 64 if (mode == "other-engine" and "--join" in args) else "a" * 64
done = frames if mode != "early-stop" else frames - 100
open(report, "w").write("frames %d\ndesyncs 0\ncrc_checks_passed %d\nfinal_hash 0x12345678\nengine_id %s (provenance x)\n" % (done, crc, engine))
hashes = opt("--hashes")
if mode != "no-hashes":
    open(hashes, "w").write("".join("%d 0x%08X 0x0\n" % (f, f * 2654435761 % 2 ** 32) for f in range(1, frames)))
else:
    open(hashes, "w").write("")
if "--record" in args:
    open(opt("--record"), "w").write(str(frames))
'''

STUB_WINE = "#!/bin/sh\nexec \"$@\"\n"


def write_exec(path, text):
    with open(path, "w") as f:
        f.write(text)
    os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC)


def run(tmp_path, mode):
    bindir = tmp_path / "bin"
    bindir.mkdir(exist_ok=True)
    write_exec(str(bindir / "peer"), STUB_PEER.replace("/usr/bin/env python3", sys.executable))
    write_exec(str(bindir / "wine"), STUB_WINE)
    env = dict(os.environ, PATH=str(bindir) + os.pathsep + os.environ["PATH"], STUB_MODE=mode)
    out = tmp_path / ("out-" + mode)
    r = subprocess.run(["bash", SCRIPT, str(bindir / "peer"), str(bindir / "peer"), str(out), "400", "7", "27500"],
                       env=env, capture_output=True, text=True, timeout=120)
    return r.returncode, r.stdout + r.stderr


def test_a_faithful_run_passes(tmp_path):
    rc, out = run(tmp_path, "good")
    assert rc == 0, out
    assert "CROSS-OS LOCKSTEP PASS" in out


@pytest.mark.parametrize("mode", ["no-crc", "no-hashes", "early-stop", "short-replay", "other-engine"])
def test_a_stub_or_incomplete_run_fails(tmp_path, mode):
    rc, out = run(tmp_path, mode)
    assert rc != 0, out
    assert "CROSS-OS LOCKSTEP FAIL" in out
