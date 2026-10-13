# Lane ONLINE-4 (Sol r1): the game's own lobby code (scripts/game.gd, online_lobby.gd, online_http.gd) logs no credential and reads no hostile answer
# beyond its bound. A local server plays the hostile lobby: under /echo it refuses every request with a message that echoes a relay ticket and a session
# token, under /huge it answers every request with 1 MiB. godot/tests/online_hostile_test.gd checks each path; here the whole output must not carry the
# ticket or the token.
import http.server
import os
import shutil
import subprocess
import threading
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")
TICKET = "AQEjRWeJq83vASNFZ4mrze8AAQIDBAUGBwgJCgsMDQ4"  # a relay ticket's shape: 32 bytes of base64url
TOKEN = "eyJpZCI6Imd1ZXN0OjEyMzQ1Njc4IiwibmFtZSI6IlJvaGFuIiwia2luZCI6Imd1ZXN0In0.qL5x0v9a4R2mTzQe7cJb1nWs8YkHd3FgUo6PiXaZtVw"


def test_the_lobby_code_logs_no_credential_and_bounds_its_answers(tmp_path):
    if not shutil.which(GODOT):
        pytest.skip("SKIP LOUDLY: no Godot binary (GODOT) to run the lobby code")

    class Lobby(http.server.BaseHTTPRequestHandler):
        def answer(self):
            if self.command == "POST":
                self.rfile.read(int(self.headers.get("content-length", 0)))
            if self.path.startswith("/echo/"):
                status, body = 400, ('{"v":1,"error":"bad_field","message":"ticket %s token %s"}' % (TICKET, TOKEN)).encode()
            else:
                status, body = 200, b'{"v":1,"motd":"' + b"x" * (1024 * 1024) + b'"}'
            self.send_response(status)
            self.send_header("content-type", "application/json")
            self.send_header("content-length", str(len(body)))
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass  # the client stopped reading at its bound

        do_GET = answer
        do_POST = answer

        def log_message(self, *a):
            pass

    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Lobby)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    try:
        env = dict(os.environ, HOME=str(tmp_path), XDG_DATA_HOME=str(tmp_path / "data"), XDG_CONFIG_HOME=str(tmp_path / "config"))
        r = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--script", "res://tests/online_hostile_test.gd", "--",
                            f"--lobby=http://127.0.0.1:{httpd.server_address[1]}", f"--ticket={TICKET}", f"--token={TOKEN}"],
                           capture_output=True, text=True, timeout=180, env=env)
        out = r.stdout + r.stderr
        # the test itself was given both: they reach the output only as its own arguments' echo (none: it prints no argument)
        assert TICKET not in out and TOKEN not in out, out[-3000:]
        assert out.count("HOSTILE ok") == 4, out[-3000:]
        assert r.returncode == 0, out[-3000:]
    finally:
        httpd.shutdown()
