# Lane ONLINEMENU-1: godot/scripts/http_timed.gd, the time limit the game's lobby requests share (online_http.gd, online_lobby.gd,
# online_stats.gd). Godot's own HTTPRequest.timeout counts frames that block the main thread (a load), so a loaded gate host timed the menu's
# player count out at its first request (srv1 om1node3-srv1, "no answer: 13 / 0"). godot/tests/http_timed_test.gd blocks frames while the
# answer comes; this fake server answers late, after a second, or never.
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


def test_a_blocking_load_does_not_use_up_a_request_time_limit(tmp_path):
    if not shutil.which(GODOT):
        pytest.skip("SKIP LOUDLY: no Godot binary (GODOT) to run the request time limit")
    release = threading.Event()

    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/never":
                release.wait(60)
                return
            time.sleep(2 if self.path == "/late" else 1)
            body = b"{}"
            self.send_response(200)
            self.send_header("content-type", "application/json")
            self.send_header("content-length", str(len(body)))
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, *a):
            pass

    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Server)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    try:
        env = dict(os.environ, HOME=str(tmp_path), XDG_DATA_HOME=str(tmp_path / "data"), XDG_CONFIG_HOME=str(tmp_path / "config"))
        r = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--script", "res://tests/http_timed_test.gd", "--",
                            f"--server=http://127.0.0.1:{httpd.server_address[1]}"], capture_output=True, text=True, timeout=240, env=env)
    finally:
        release.set()
        httpd.shutdown()
    out = r.stdout + r.stderr
    assert "TIMED FAIL" not in out, out[-4000:]
    assert out.count("TIMED ok") == 8 and r.returncode == 0, out[-4000:]


def test_the_launcher_copy_is_the_same_bytes():
    # the crash client is the same bytes in the game and the launcher (tools/release/test_crash_upload.py) and preloads "../http_timed.gd"
    assert (REPO / "godot/scripts/http_timed.gd").read_bytes() == (REPO / "launcher/scripts/http_timed.gd").read_bytes(), \
        "copy godot/scripts/http_timed.gd to launcher/scripts/"
