import http.server
import json
import os
import shutil
import subprocess
import threading
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")
GAME = "2222222222222222"
PROFILE = "0123456789abcdef0123456789abcdef"


def run_godot(url, mode, simulation_mode="normal"):
    env = dict(os.environ)
    result = subprocess.run(
        [GODOT, "--headless", "--path", str(REPO / "godot"), "--script",
         "res://tests/online_invite_test.gd", "--", f"--lobby={url}", f"--mode={mode}",
         f"--simulation-mode={simulation_mode}"],
        check=False, capture_output=True, text=True, timeout=120, env=env)
    output = result.stdout + result.stderr
    assert result.returncode == 0, output[-3000:]
    return output


@pytest.mark.skipif(not shutil.which(GODOT), reason="Godot binary unavailable")
@pytest.mark.parametrize("simulation_mode", ["normal", "uber"])
def test_invite_accept_uses_local_profile_and_same_session_before_ordinary_join(simulation_mode):
    seen = []

    class Lobby(http.server.BaseHTTPRequestHandler):
        def answer(self, status, value):
            body = json.dumps(value).encode()
            self.send_response(status)
            self.send_header("content-type", "application/json")
            self.send_header("content-length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            raw = self.rfile.read(int(self.headers.get("content-length", 0)))
            seen.append((self.command, self.path, self.headers.get("authorization"), raw))
            if self.path == "/v1/invites/1111111111111111/accept":
                self.answer(200, {"v": 1, "id": GAME, "slot": 0.0, "game": {"id": GAME}})
            elif self.path == f"/v1/games/{GAME}/join":
                self.answer(200, {"v": 1, "slot": 0})
            elif self.path == f"/v1/games/{GAME}/ready":
                self.answer(200, {"v": 1, "ok": True})
            else:
                self.answer(404, {"v": 1, "error": "unavailable", "message": "not found"})

        def do_GET(self):
            seen.append((self.command, self.path, self.headers.get("authorization"), b""))
            self.answer(200, {"v": 1, "start": {"game": GAME, "slot": 0, "relay": "127.0.0.1:7350"}})

        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Lobby)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        run_godot(f"http://127.0.0.1:{server.server_address[1]}", "success", simulation_mode)
    finally:
        server.shutdown()
    paths = [entry[1] for entry in seen]
    assert paths[:3] == ["/v1/invites/1111111111111111/accept", f"/v1/games/{GAME}/join",
                         f"/v1/games/{GAME}/ready"], seen
    assert all(entry[2] == "Bearer fake-session-token" for entry in seen), seen
    body = json.loads(seen[0][3])
    identity = {"profile": PROFILE, "mode": simulation_mode, "platform": "linux"}
    assert body == {"v": 1, **identity}
    joined = json.loads(seen[1][3])
    # lane JOINVER-1: the ordinary join tells this build too (online_lobby.gd own_build); the invite's accept does not (an older lobby refuses it)
    build = joined.pop("build", None)
    assert joined == identity
    assert isinstance(build, dict) and set(build) <= {"version", "date", "official", "platform"} and build.get("platform") == "linux", build
    assert not any(path == "/v1/auth/guest" for path in paths)


@pytest.mark.skipif(not shutil.which(GODOT), reason="Godot binary unavailable")
@pytest.mark.parametrize("mode", ["local"])
def test_invalid_invite_launches_are_rejected_before_http(mode):
    requests = []

    class Lobby(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            requests.append(self.path)
            self.send_response(500)
            self.end_headers()

        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Lobby)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        run_godot(f"http://127.0.0.1:{server.server_address[1]}", mode)
    finally:
        server.shutdown()
    assert requests == []


@pytest.mark.skipif(not shutil.which(GODOT), reason="Godot binary unavailable")
@pytest.mark.parametrize(
    "case,status,value",
    [
        ("profile", 409, {"v": 1, "error": "profile_mismatch", "message": "wrong local profile"}),
        ("full", 409, {"v": 1, "error": "full", "message": "room is full"}),
        ("expired", 404, {"v": 1, "error": "unavailable", "message": "invite expired"}),
        ("malformed", 200, {"v": 1, "id": "invalid", "slot": 0.0, "game": {"id": GAME}}),
        ("redirect", 302, None),
    ],
)
def test_refused_or_malformed_invite_accept_never_joins(case, status, value):
    paths = []

    class Lobby(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            self.rfile.read(int(self.headers.get("content-length", 0)))
            paths.append(self.path)
            body = json.dumps(value).encode() if value is not None else b""
            self.send_response(status)
            if case == "redirect":
                self.send_header("location", f"http://127.0.0.1:{self.server.server_address[1]}/elsewhere")
            self.send_header("content-length", str(len(body)))
            self.end_headers()
            if body:
                self.wfile.write(body)

        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Lobby)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        run_godot(f"http://127.0.0.1:{server.server_address[1]}", case)
    finally:
        server.shutdown()
    assert paths == ["/v1/invites/1111111111111111/accept"], paths
