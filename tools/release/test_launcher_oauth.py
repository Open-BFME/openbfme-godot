#!/usr/bin/env python3
"""Run the real Node-lobby/SQLite launcher OAuth regression.

The runner starts the test-only lobby fixture, then runs the real Godot
OAuthAccount and SocialClient against it. Provider HTTP is mocked only inside
the fixture's ``lobby.env.oauthFetch`` seam. ``--serve`` leaves the fixture
running and prints loopback origins for a later UI session.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import selectors
import shutil
import signal
import subprocess
import tempfile
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
FIXTURE = REPO / "services/online/lobby/test/launcher-oauth-fixture.mjs"
SCRIPT = "res://tests/oauth_integration_test.gd"
SENSITIVE_MARKERS = (
    "launcher-fixture-",
    "900000000000001",
    "SESSION_KEY",
    "client_id=",
    "client_secret=",
    "code_verifier=",
    "access_token=",
    "authorization=",
    "code=",
    "state=",
)
ENGINE_ERROR = re.compile(r"^\s*(?:SCRIPT ERROR|ERROR):", re.IGNORECASE)


def node_command() -> str:
    configured = os.environ.get("NODE")
    if configured:
        resolved = shutil.which(configured)
        if resolved:
            return resolved
        candidate = Path(configured)
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
        raise RuntimeError("NODE is set but the Node.js executable is unavailable")
    resolved = shutil.which("node")
    if not resolved:
        raise RuntimeError("Node.js was not found; set NODE or add node to PATH")
    return resolved


def contains_sensitive(value: str) -> bool:
    lower = value.lower()
    return any(marker.lower() in lower for marker in SENSITIVE_MARKERS)


def safe_output(value: str) -> str:
    """Keep failure diagnostics useful without echoing callback secrets."""
    lines: list[str] = []
    for line in value.splitlines():
        lower = line.lower()
        if any(marker.lower() in lower for marker in SENSITIVE_MARKERS):
            lines.append("[sensitive output suppressed]")
        elif "code=" in lower or "state=" in lower or "token" in lower:
            lines.append("[oauth output suppressed]")
        else:
            lines.append(line[:240])
    return "\n".join(lines[-60:])


def wait_ready(process: subprocess.Popen[str], timeout: float = 15.0) -> dict[str, str]:
    selector = selectors.DefaultSelector()
    assert process.stdout is not None
    selector.register(process.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError("fixture exited before readiness")
            events = selector.select(max(0.05, deadline - time.monotonic()))
            for key, _ in events:
                line = key.fileobj.readline()
                if not line:
                    continue
                try:
                    value = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if value.get("v") == 1 and value.get("lobby_origin") and value.get("control_origin"):
                    return {
                        "lobby": str(value["lobby_origin"]),
                        "control": str(value["control_origin"]),
                    }
    finally:
        selector.close()
    raise RuntimeError("fixture readiness timed out")


def stop_process(process: subprocess.Popen[str]) -> str:
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
    try:
        _, stderr = process.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        _, stderr = process.communicate(timeout=5)
    return stderr or ""


def start_fixture(data_dir: Path) -> tuple[subprocess.Popen[str], dict[str, str]]:
    command = [node_command(), str(FIXTURE), "--data-dir", str(data_dir)]
    process = subprocess.Popen(
        command,
        cwd=REPO,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    try:
        return process, wait_ready(process)
    except Exception as error:
        stderr = stop_process(process)
        diagnostic = safe_output(stderr)
        details = f": {diagnostic}" if diagnostic else ""
        raise RuntimeError(f"fixture startup failed ({type(error).__name__}){details}") from error


def run_godot(godot: Path, origins: dict[str, str], install_root: Path, timeout: float) -> tuple[int, str]:
    command = [
        str(godot),
        "--headless",
        "--path",
        str(REPO / "launcher"),
        "--script",
        SCRIPT,
        "--",
        f"--lobby-origin={origins['lobby']}",
        f"--control-origin={origins['control']}",
        f"--install-root={install_root}",
    ]
    process = subprocess.run(
        command,
        cwd=REPO,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return process.returncode, process.stdout + "\n" + process.stderr


def serve() -> int:
    data_dir = REPO / "workspace" / "launcher-oauth-serve-data"
    data_dir.mkdir(parents=True, exist_ok=True)
    process, origins = start_fixture(data_dir)
    print(f"launcher OAuth fixture ready: lobby={origins['lobby']} control={origins['control']}", flush=True)
    print("Control API: GET /health, GET /stats, GET /authorize?url=<encoded URL>, POST /callback, POST /fault/next-exchange", flush=True)
    try:
        while process.poll() is None:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        stderr = stop_process(process)
    if stderr:
        diagnostic = safe_output(stderr)
        if diagnostic:
            print(diagnostic)
        return 1
    return int(process.returncode or 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", type=Path, help="Godot 4.7.2 executable")
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--serve", action="store_true", help="leave the real lobby fixture running for UI testing")
    args = parser.parse_args()
    if args.serve:
        try:
            return serve()
        except Exception as error:
            print(f"FAIL: launcher OAuth fixture unavailable ({type(error).__name__})")
            return 2
    if args.godot is None:
        parser.error("--godot is required unless --serve is used")
    if not args.godot.is_file() or not os.access(args.godot, os.X_OK):
        print("FAIL: the supplied Godot executable is unavailable")
        return 2

    temporary = tempfile.TemporaryDirectory(prefix="launcher-oauth-")
    fixture = None
    output = ""
    code = None
    failure = None
    summary = None
    fixture_exited_early = False
    fixture_stderr = ""
    try:
        fixture, origins = start_fixture(Path(temporary.name) / "lobby-data")
        code, output = run_godot(args.godot, origins, Path(temporary.name) / "installs", args.timeout)
        fixture_exited_early = fixture.poll() is not None
        summary = next(
            (line.strip() for line in output.splitlines() if line.strip().startswith("OAUTH INTEGRATION PASS")),
            None,
        )
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", "replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", "replace")
        output = stdout + "\n" + stderr
        failure = "Godot integration timed out"
    except Exception as error:
        failure = f"launcher OAuth fixture unavailable ({type(error).__name__})"
        output = str(error)
    finally:
        if fixture is not None:
            fixture_stderr = stop_process(fixture)
        temporary.cleanup()

    if contains_sensitive(output) or contains_sensitive(fixture_stderr):
        failure = "captured output contained a sensitive OAuth value"
    elif fixture_exited_early:
        failure = "fixture exited before the launcher integration completed"
    elif fixture_stderr.strip():
        failure = "fixture wrote to stderr"
    elif any(ENGINE_ERROR.match(line) for line in output.splitlines()):
        failure = "Godot emitted SCRIPT ERROR or ERROR output"
    elif code != 0 or summary is None:
        failure = f"Godot integration exited with status {code}"

    if failure:
        print(f"FAIL: {failure}")
        diagnostics = safe_output(output + "\n" + fixture_stderr)
        if diagnostics:
            print(diagnostics)
        return 1
    print(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
