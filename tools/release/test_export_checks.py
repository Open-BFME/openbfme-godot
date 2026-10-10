"""Lane RELTEST-1: test_export.sh's start and smoke verdicts on long output.

The v0.3.0-preview.2 autorelease failed with "RESULT smoke: FAIL exit 0" on a passing smoke test: under `set -o pipefail`,
`echo "$out" | grep -q MARK` fails when grep exits at the match while echo still writes the rest (SIGPIPE, 141). These tests run
test_export.sh on a stand-in package whose game and editor print a marker followed by many lines (the old check missed it every
time), and check that no pipefail script under tools/ greps a pipe quietly again.
"""
from __future__ import annotations

import os
import re
import subprocess
import tarfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
VERSION = "v9.9.9-preview.1"

# the stand-in for both the packaged game and the Godot editor binary test_export.sh copies over it
FAKE = r"""#!/bin/bash
noise() { seq 1 "$1" | sed 's/^/GAME some line /'; }
case " $* " in
  *" --version "*) echo "4.7.2.stable.official.fake"; exit 0 ;;
  *" --script "*) noise 50000; [ "${FAKE_SMOKE_MARKER:-1}" = 1 ] && echo "SMOKE PASS (stand-in)"; noise 20000; exit 0 ;;
  *" --auto "*) echo "OpenBFME __VERSION__ (2026-10-10)"; noise 50000; echo "GAME ran 5 frames"; noise 20000; exit 0 ;;
esac
exit 3
"""


def package(tmp: Path) -> tuple[Path, Path]:
    top = tmp / "src" / f"openbfme-{VERSION}-linux-x64"
    top.mkdir(parents=True)
    (top / "VERSION").write_text(f"OpenBFME {VERSION}\n")
    exe = top / "OpenBFME.x86_64"
    exe.write_text(FAKE.replace("__VERSION__", VERSION))
    exe.chmod(0o755)
    godot = tmp / "godot-editor"
    godot.write_text(FAKE.replace("__VERSION__", VERSION))
    godot.chmod(0o755)
    archive = tmp / f"openbfme-{VERSION}-linux-x64.tar.gz"
    with tarfile.open(archive, "w:gz") as t:
        t.add(top, arcname=top.name)
    return archive, godot


def run_export(tmp: Path, **env: str) -> subprocess.CompletedProcess:
    archive, godot = package(tmp)
    home = tmp / "home"
    home.mkdir(exist_ok=True)
    e = {**os.environ, "ROTWK_INSTALL": str(tmp / "no-rotwk"), "BFME2_INSTALL": str(tmp / "no-bfme2"), "HOME": str(home),
         "TMPDIR": str(tmp), "TEST_EXPORT_LOGS": str(tmp / "logs"), **env}
    return subprocess.run(["bash", str(HERE / "test_export.sh"), str(archive), "--godot", str(godot)], env=e, capture_output=True,
                          text=True, timeout=600)


def test_markers_are_found_before_long_output(tmp_path):
    """the markers with 20000 lines after them: the old `echo | grep -q` under pipefail missed them every time"""
    for i in range(2):
        r = run_export(tmp_path / str(i))
        assert "RESULT start: PASS (GAME ran 5 frames)" in r.stdout, r.stdout[-3000:]
        assert "RESULT smoke: PASS (70001 lines of output)" in r.stdout, r.stdout[-3000:]
        # the full output is kept for the operator
        logs = tmp_path / str(i) / "logs"
        assert "SMOKE PASS (stand-in)" in (logs / "smoke.log").read_text()
        assert "GAME ran 5 frames" in (logs / "start.log").read_text()


def test_a_missing_marker_fails_and_shows_the_output(tmp_path):
    r = run_export(tmp_path, FAKE_SMOKE_MARKER="0")
    assert "RESULT smoke: FAIL exit 0" in r.stdout and "--- the last 30 lines of smoke.log (70000 lines)" in r.stdout, r.stdout[-3000:]
    assert "GAME some line 20000" in r.stdout
    assert "SMOKE PASS" not in (tmp_path / "logs" / "smoke.log").read_text()
    assert r.returncode != 0


PIPED_QUIET_GREP = re.compile(r"\b(echo|printf|cat)\b[^|#]*\|\s*grep\s+(-[a-zA-Z]*q[a-zA-Z]*\b|--quiet)")


def test_no_pipefail_script_greps_a_pipe_quietly():
    """grep -q at the end of a pipe of command output fails spuriously under pipefail: grep a file or a here-string instead"""
    found = []
    for sh in sorted(REPO.glob("tools/**/*.sh")):
        text = sh.read_text(errors="replace")
        if "pipefail" not in text:
            continue
        for n, line in enumerate(text.splitlines(), 1):
            if PIPED_QUIET_GREP.search(line) and not line.lstrip().startswith("#"):
                found.append(f"{sh.relative_to(REPO)}:{n}: {line.strip()}")
    assert not found, "\n".join(found)
