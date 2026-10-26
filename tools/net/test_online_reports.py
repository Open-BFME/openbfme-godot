"""Asset-free player-report controls, privacy and idempotent HTTP retries."""
import http.server
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading

import pytest


def test_report_controls_retry_the_same_authenticated_payload(tmp_path):
    godot = os.environ.get("GODOT", "godot")
    if not shutil.which(godot):
        pytest.skip("SKIP LOUDLY: Godot is needed for the report controls test")
    requests = []

    class Lobby(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            requests.append((self.path, self.headers.get("authorization"),
                             self.headers.get("idempotency-key"),
                             json.loads(self.rfile.read(int(self.headers["content-length"])))))
            if len(requests) == 1:
                code, answer = 503, {"v": 1, "error": "unavailable"}
            else:
                code, answer = 201, {"v": 1, "report_id": "R-" + "a" * 24, "received_at": 1700000000}
            data = json.dumps(answer).encode()
            self.send_response(code)
            self.send_header("content-type", "application/json")
            self.send_header("content-length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Lobby)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    repo = Path(__file__).resolve().parents[2]
    env = dict(os.environ, HOME=str(tmp_path), XDG_DATA_HOME=str(tmp_path / "data"),
               XDG_CONFIG_HOME=str(tmp_path / "config"))
    env.pop("ROTWK_INSTALL", None)
    env.pop("BFME2_INSTALL", None)
    try:
        result = subprocess.run([godot, "--headless", "--path", str(repo / "godot"), "--script",
                                 "res://tests/online_reports_test.gd", "--",
                                 f"--url=http://127.0.0.1:{server.server_port}"], env=env,
                                capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        assert result.returncode == 0, output
        assert "REPORT UI PASS" in output, output
        assert len(requests) == 2
        assert requests[0] == requests[1]
        path, token, key, payload = requests[0]
        assert path == "/v1/reports"
        assert token == "Bearer fake-lobby-session"
        assert re.fullmatch(r"[A-Za-z0-9_-]{8,64}", key)
        assert payload == {"v": 1, "game_id": "0123456789abcdef",
                           "target": "guest:2222222222222222", "reason": "other: Reason about this game."}
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
