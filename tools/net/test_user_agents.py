# Lane INFRA-2: every Python client of play.openbfme.com sends its own User-Agent. Cloudflare's browser integrity check answers Python's default
# ("Python-urllib/3.x") with 403, error 1010 (found 2026-10-13: the relay's log uploads and the test client were refused by the live lobby).
import http.server
import json
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
for d in ("tools/net", "services/online/relay", "services/online/deploy"):
    sys.path.insert(0, str(ROOT / d))
import lobby_backup  # noqa: E402
import monitor  # noqa: E402
import online_client  # noqa: E402
import upload_logs  # noqa: E402


def capture():
    seen = []

    class H(http.server.BaseHTTPRequestHandler):
        def answer(self):
            seen.append(self.headers.get("user-agent", ""))
            n = int(self.headers.get("content-length") or 0)
            self.rfile.read(n)
            body = json.dumps({"v": 1, "ok": True, "blobs": [], "next": None, "relays": [], "logStore": "x"}).encode()
            self.send_response(200)
            self.send_header("content-type", "application/json")
            self.send_header("content-length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        do_GET = do_POST = answer

        def log_message(self, *a):
            pass
    srv = http.server.HTTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, f"http://127.0.0.1:{srv.server_address[1]}", seen


def test_no_client_sends_pythons_default_user_agent(tmp_path):
    srv, url, seen = capture()
    try:
        log = tmp_path / "00000000000000aa.obfmerlog"
        log.write_bytes(b"OBFMERLG")
        assert upload_logs.upload(log, url, b"k" * 32) == ""
        online_client.Lobby(url).call("GET", "/v1/health")
        lobby_backup.Admin(url, b"k" * 32).list_all()
        monitor.check_lobby(url, "x", "x")
    finally:
        srv.shutdown()
    assert len(seen) == 4 and all(ua and not ua.startswith("Python-urllib") for ua in seen), seen
