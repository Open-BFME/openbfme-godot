# Lane ONLINE-2 r2 (Sol r1): Godot 4.7's HTTPRequest followed a redirect to another origin WITH the bearer token. The lobby client must follow no
# redirect: a lobby that redirects to a second server makes the client fail, and the second server never sees a request (so never the token).
import http.server
import os
import shutil
import subprocess
import threading
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GODOT = os.environ.get("GODOT", "godot")


def serve(handler):
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


@pytest.mark.parametrize("status", [301, 302, 303, 307, 308])
@pytest.mark.parametrize("where", ["join", "poll"])
def test_the_lobby_client_follows_no_redirect_and_keeps_its_token(tmp_path, status, where):
    """`where`: the redirect answers the join (a POST) or, after a join and a ready that succeed, the poll (a GET: what Sol reproduced)"""
    if not shutil.which(GODOT):
        pytest.skip("SKIP LOUDLY: no Godot binary (GODOT) to run the lobby client")
    seen = []

    class Other(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            seen.append((self.command, self.path, self.headers.get("authorization")))
            self.send_response(200)
            self.send_header("content-type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"v":1}')
        do_POST = do_GET

        def log_message(self, *a):
            pass

    other = serve(Other)

    class Lobby(http.server.BaseHTTPRequestHandler):
        def answer(self, body: bytes):
            self.send_response(200)
            self.send_header("content-type", "application/json")
            self.send_header("content-length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def redirect(self):
            self.send_response(status)
            self.send_header("location", f"http://127.0.0.1:{other.server_address[1]}{self.path}")
            self.send_header("content-length", "0")
            self.end_headers()

        def do_POST(self):
            self.rfile.read(int(self.headers.get("content-length", 0)))
            if where == "join" or not (self.path.endswith("/join") or self.path.endswith("/ready")):
                return self.redirect()
            self.answer(b'{"v":1,"slot":1}')

        def do_GET(self):
            self.redirect()

        def log_message(self, *a):
            pass

    lobby = serve(Lobby)
    try:
        env = dict(os.environ, HOME=str(tmp_path), XDG_DATA_HOME=str(tmp_path / "data"), XDG_CONFIG_HOME=str(tmp_path / "config"))
        r = subprocess.run([GODOT, "--headless", "--path", str(REPO / "godot"), "--script", "res://tests/online_lobby_redirect_test.gd", "--",
                            f"--lobby=http://127.0.0.1:{lobby.server_address[1]}"], capture_output=True, text=True, timeout=120, env=env)
        out = r.stdout + r.stderr
        assert seen == [], f"the other origin got {seen}"
        assert "REDIRECT failed: " in out and "redirect" in out.split("REDIRECT failed: ", 1)[1], out[-3000:]
        assert r.returncode == 0, out[-3000:]
    finally:
        lobby.shutdown()
        other.shutdown()
