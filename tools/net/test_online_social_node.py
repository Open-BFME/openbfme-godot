"""Run the real native Social client against an isolated Node lobby."""

from __future__ import annotations

import os
import re
import shutil
import signal
import subprocess
import tempfile
import threading
import time
from collections import deque
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
SERVER_RELATIVE = Path("services/online/lobby/src/server.mjs")
SCENE = "res://tests/online_social_node.tscn"
READY = re.compile(r"openbfme lobby: listening on 127\.0\.0\.1:(\d+)")


def _required_program(name: str) -> str:
    configured = os.environ.get(name.upper(), "")
    path = configured if configured else name.lower()
    resolved = shutil.which(path)
    if resolved is None:
        pytest.skip(f"SKIP: required {name.lower()} executable is unavailable")
    return resolved


def _required_install(name: str) -> Path:
    value = os.environ.get(name, "")
    if not value:
        pytest.skip(f"SKIP: {name} is not set")
    path = Path(value)
    if not path.is_dir():
        pytest.skip(f"SKIP: {name} directory is unavailable")
    if not (path / "game.dat").is_file():
        pytest.skip(f"SKIP: {name} is not a retail installation")
    return path


def _child_env(**values: str) -> dict[str, str]:
    allowed = {}
    for key in ("PATH", "LANG", "LC_ALL", "LD_LIBRARY_PATH"):
        if os.environ.get(key):
            allowed[key] = os.environ[key]
    allowed.update(values)
    return allowed


def _server_root() -> Path:
    configured = os.environ.get("OPENBFME_SOCIAL_NODE_ROOT")
    root = Path(configured).expanduser().resolve() if configured else ROOT
    server = root / SERVER_RELATIVE
    if not server.is_file():
        pytest.fail("social-node lobby server is unavailable at the configured root")
    return root


def _terminate(process: subprocess.Popen[str] | None) -> None:
    if process is None or process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=5)


def _start_lobby(node: str, data: Path, server_root: Path) -> tuple[subprocess.Popen[str], int]:
    key = os.urandom(32).hex()
    session_key = os.urandom(32).hex()
    env = _child_env(
        HOME=str(data / "home"),
        LOBBY_BIND="127.0.0.1:0",
        LOBBY_DATA=str(data / "lobby"),
        TICKET_KEYS=f"1:{key}",
        SESSION_KEY=session_key,
        RELAYS="",
        REQUIRE_INSTALL="1",
    )
    process = subprocess.Popen(
        [node, str(server_root / SERVER_RELATIVE)],
        cwd=server_root,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
    )
    ready: list[int] = []

    def read_startup() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            match = READY.search(line)
            if match:
                ready.append(int(match.group(1)))
                return

    reader = threading.Thread(target=read_startup, name="social-node-server", daemon=True)
    reader.start()
    deadline = time.monotonic() + 15
    while not ready and process.poll() is None and time.monotonic() < deadline:
        reader.join(0.05)
    if not ready:
        code = process.poll()
        _terminate(process)
        raise RuntimeError(f"isolated lobby did not announce a port (exit={code})")
    return process, ready[0]


def _godot_args(godot: str, lobby: str, options: Path) -> list[str]:
    return [
        godot,
        "--headless",
        "--path",
        str(ROOT / "godot"),
        SCENE,
        "--",
        "--online-walk=social-node",
        f"--online-lobby={lobby}",
        "--no-intro",
        "--no-update-check",
        "--max-fps=60",
        f"--options-file={options}",
    ]


def _safe_diagnostics(lines: deque[str]) -> str:
    diagnostics = []
    for line in lines:
        text = line.strip()
        if not any(
            marker in text
            for marker in ("SCRIPT ERROR:", "Parse Error:", "ERROR: Failed to load script", "SOCIAL-NODE FAIL")
        ):
            continue
        text = re.sub(r"(?i)bearer\s+\S+", "Bearer <redacted>", text)
        text = re.sub(r"(?i)(token[=: ]+)\S+", r"\1<redacted>", text)
        text = re.sub(r"(?i)\b[0-9a-f]{32,}\b", "<redacted>", text)
        diagnostics.append(text[:320])
    return " | ".join(diagnostics[-12:]) or "no sanitized diagnostic lines captured"


def test_real_native_social_node_walk() -> None:
    """Exercise the real client, real transport, and real Node routes."""

    node = _required_program("NODE")
    godot = _required_program("GODOT")
    server_root = _server_root()
    bfme2 = _required_install("BFME2_INSTALL")
    rotwk = _required_install("ROTWK_INSTALL")
    extension = ROOT / "godot" / "bin" / "openbfme.linux.template_debug.x86_64.so"
    if not extension.is_file():
        pytest.skip("SKIP: the debug OpenBFME Godot extension is unavailable; build engine first")

    with tempfile.TemporaryDirectory(prefix="openbfme-social-node-") as directory:
        isolated = Path(directory)
        for name in ("home", "data", "config", "cache"):
            (isolated / name).mkdir()
        options = isolated / "options.ini"
        options.touch()
        lobby_process: subprocess.Popen[str] | None = None
        game_process: subprocess.Popen[str] | None = None
        output_lines: deque[str] = deque(maxlen=2000)
        parse_error = threading.Event()
        output_reader: threading.Thread | None = None
        try:
            lobby_process, port = _start_lobby(node, isolated, server_root)
            env = _child_env(
                HOME=str(isolated / "home"),
                XDG_DATA_HOME=str(isolated / "data"),
                XDG_CONFIG_HOME=str(isolated / "config"),
                XDG_CACHE_HOME=str(isolated / "cache"),
                BFME2_INSTALL=str(bfme2),
                ROTWK_INSTALL=str(rotwk),
            )
            game_process = subprocess.Popen(
                _godot_args(godot, f"http://127.0.0.1:{port}", options),
                cwd=ROOT,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                start_new_session=True,
            )
            assert game_process.stdout is not None

            def read_output() -> None:
                assert game_process is not None and game_process.stdout is not None
                for line in game_process.stdout:
                    output_lines.append(line)
                    if any(
                        marker in line
                        for marker in ("SCRIPT ERROR:", "Parse Error:", "ERROR: Failed to load script")
                    ):
                        parse_error.set()

            output_reader = threading.Thread(target=read_output, name="social-node-godot-output", daemon=True)
            output_reader.start()
            deadline = time.monotonic() + 180
            while game_process.poll() is None and time.monotonic() < deadline and not parse_error.is_set():
                parse_error.wait(0.05)
            if parse_error.is_set():
                _terminate(game_process)
                output_reader.join(timeout=5)
                pytest.fail(f"real social-node walk could not load its script: {_safe_diagnostics(output_lines)}")
            if game_process.poll() is None:
                _terminate(game_process)
                output_reader.join(timeout=5)
                pytest.fail(f"real social-node walk exceeded 180 seconds: {_safe_diagnostics(output_lines)}")
            game_process.wait()
            output_reader.join(timeout=5)
            output = "".join(output_lines)
            if game_process.returncode != 0:
                pytest.fail(f"real social-node walk failed: {_safe_diagnostics(output_lines)}")
            assert "SOCIAL-NODE PASS phases=7" in output
        finally:
            _terminate(game_process)
            _terminate(lobby_process)
