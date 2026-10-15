# Lane ONLINECOUNT-1: the game's "N players online" (godot/scripts/online_stats.gd) against a fake lobby. godot/tests/online_stats_test.gd drives the
# script through every kind of answer; here the fake lobby records what it was asked: the main menu's request carries no token, the online menu's carries
# the session token, a turned-off count asks nothing, and a redirect is not followed.
import http.server
import os
import shutil
import subprocess
import threading
import time
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")
GOOD = b'{"v":1,"players":3,"games_open":1,"games_running":2,"as_of":1700000000}'


def test_the_player_count_answers_requests_and_off_switch(tmp_path):
    if not shutil.which(GODOT):
        pytest.skip("SKIP LOUDLY: no Godot binary (GODOT) to run the game's player count")
    seen: list[tuple[str, str]] = []  # (path, authorization)
    lock = threading.Lock()
    switched = {"n": 0}

    class Lobby(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            with lock:
                seen.append((self.path, self.headers.get("authorization", "")))
            status, body, extra = 200, GOOD, {}
            if self.path.startswith("/e404/"):
                status, body = 404, b'{"v":1,"error":"not_found"}'
            elif self.path.startswith("/bad/"):
                body = b"<html>not json</html>"
            elif self.path.startswith("/float/"):
                body = b'{"v":1,"players":1.5,"games_open":0,"games_running":0}'
            elif self.path.startswith("/neg/"):
                body = b'{"v":1,"players":-3,"games_open":0,"games_running":0}'
            elif self.path.startswith("/huge/"):
                body = b'{"v":1,"players":3,"games_open":1,"games_running":2,"pad":"' + b"x" * (1024 * 1024) + b'"}'
            elif self.path.startswith("/redirect/"):
                status, body, extra = 302, b"", {"Location": self.path.replace("/redirect/", "/redirected/")}
            elif self.path.startswith("/slow/"):
                time.sleep(3)
            elif self.path.startswith("/switch/"):
                switched["n"] += 1
                if switched["n"] > 1:
                    status, body = 503, b"{}"
            self.send_response(status)
            self.send_header("content-type", "application/json")
            self.send_header("content-length", str(len(body)))
            for k, v in extra.items():
                self.send_header(k, v)
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, *a):
            pass

    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Lobby)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    try:
        env = dict(os.environ, HOME=str(tmp_path), XDG_DATA_HOME=str(tmp_path / "data"), XDG_CONFIG_HOME=str(tmp_path / "config"))
        r = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--script", "res://tests/online_stats_test.gd", "--",
                            f"--lobby=http://127.0.0.1:{httpd.server_address[1]}"], capture_output=True, text=True, timeout=240, env=env)
    finally:
        httpd.shutdown()
    out = r.stdout + r.stderr
    assert "STATS FAIL" not in out, out[-4000:]
    assert out.count("STATS ok") >= 30 and r.returncode == 0, out[-4000:]
    ok = [a for p, a in seen if p == "/ok/v1/stats"]
    assert "" in ok and "Bearer SESSION-TOKEN-1" in ok, seen          # the main menu's request has no token, the online menu's has the session's
    assert not any("MENU-MUST-NOT-SEND" in a for _, a in seen), seen  # a token offered in the main menu is never sent
    assert not [p for p, _ in seen if p.startswith("/off/")], seen     # a turned-off count asks nothing
    assert not [p for p, _ in seen if p.startswith("/redirected/")], seen
    assert [p for p, _ in seen].count("/redirect/v1/stats") == 1, seen
    assert all(p.endswith("/v1/stats") for p, _ in seen), seen        # nothing else is ever asked
