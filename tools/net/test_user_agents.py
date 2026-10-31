# Lane INFRA-2: every Python client of play.openbfme.com sends its own User-Agent. Cloudflare's browser integrity check answers Python's default
# ("Python-urllib/3.x") with 403, error 1010 (found 2026-10-13: the relay's log uploads and the test client were refused by the live lobby).
# The operator's own backup and monitor clients are kept outside this repository, with their own copy of this check.
# Online batch r5: the relay uploader's INPUT sidecar upload against a stub lobby (below).
import hashlib
import hmac
import http.server
import json
import os
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
for d in ("tools/net", "services/online/relay"):
    sys.path.insert(0, str(ROOT / d))
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
    finally:
        srv.shutdown()
    assert len(seen) == 2 and all(ua and not ua.startswith("Python-urllib") for ua in seen), seen


# ---- online batch r5: upload_logs.py sends a version-2 log's INPUT sidecar after its log (ADMINFIX-1's /v1/relay/logs/<id>/input) ----------------

KEY = bytes(range(32))
GAME = "000000000e302ba3"


def stub_lobby(answers):
    """A lobby whose answer to each path is answers[path] (a list consumed in order; the last repeats): (status, error code or None)."""
    seen = []

    class H(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            body = self.rfile.read(int(self.headers.get("content-length") or 0))
            seen.append((self.path, self.headers.get("x-openbfme-signature", ""), body))
            queue = answers[self.path]
            status, code = queue.pop(0) if len(queue) > 1 else queue[0]
            out = json.dumps({"v": 1, "ok": True} if status == 200 else {"v": 1, "error": code, "message": code}).encode()
            self.send_response(status)
            self.send_header("content-type", "application/json")
            self.send_header("content-length", str(len(out)))
            self.end_headers()
            self.wfile.write(out)

        def log_message(self, *a):
            pass
    srv = http.server.HTTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, f"http://127.0.0.1:{srv.server_address[1]}", seen


def relay_logs(tmp_path, sidecar=True):
    log = tmp_path / f"{GAME}.obfmerlog"
    log.write_bytes(b"OBFMERLG\x02\x00\x00\x00 a version-2 log")
    if sidecar:
        (tmp_path / f"{GAME}.obfmerlog.input").write_bytes(b"OBFMERLI\x01\x00\x00\x00 its INPUT records\x00\xff")
    return log


LOG_PATH, SIDE_PATH = f"/v1/relay/logs/{GAME}", f"/v1/relay/logs/{GAME}/input"


def names(d):
    return sorted(p.name for p in d.iterdir() if p.is_file())


def test_upload_sidecar_after_its_log_both_move(tmp_path):
    log = relay_logs(tmp_path)
    side_bytes, log_bytes = (tmp_path / f"{GAME}.obfmerlog.input").read_bytes(), log.read_bytes()
    srv, url, seen = stub_lobby({LOG_PATH: [(200, None)], SIDE_PATH: [(200, None)]})
    try:
        assert upload_logs.upload_all(tmp_path, url, KEY) == (2, 0)
    finally:
        srv.shutdown()
    assert [p for p, _, _ in seen] == [LOG_PATH, SIDE_PATH]  # the sidecar after its log
    # the signature is HMAC-SHA256(upload key, the exact bytes of each file)
    assert seen[0][2] == log_bytes and seen[0][1] == hmac.new(KEY, log_bytes, hashlib.sha256).hexdigest()
    assert seen[1][2] == side_bytes and seen[1][1] == hmac.new(KEY, side_bytes, hashlib.sha256).hexdigest()
    assert names(tmp_path) == [] and names(tmp_path / "uploaded") == [f"{GAME}.obfmerlog", f"{GAME}.obfmerlog.input"]


def test_upload_sidecar_log_missing_keeps_both_then_the_next_run_moves_both(tmp_path):
    relay_logs(tmp_path)
    srv, url, seen = stub_lobby({LOG_PATH: [(200, None)], SIDE_PATH: [(409, "log_missing"), (200, None)]})
    try:
        assert upload_logs.upload_all(tmp_path, url, KEY) == (0, 0)  # not a failure: retried next run
        assert names(tmp_path) == [f"{GAME}.obfmerlog", f"{GAME}.obfmerlog.input"]
        assert json.loads(upload_logs.status_body("1.2.3.4:5", tmp_path, None, 0, 0))["pending"] == 2  # the sidecar counts as pending
        assert upload_logs.upload_all(tmp_path, url, KEY) == (2, 0)  # the log again (the lobby: 200 unchanged), then its sidecar
    finally:
        srv.shutdown()
    assert [p for p, _, _ in seen] == [LOG_PATH, SIDE_PATH, LOG_PATH, SIDE_PATH]
    assert names(tmp_path) == [] and names(tmp_path / "uploaded") == [f"{GAME}.obfmerlog", f"{GAME}.obfmerlog.input"]


def test_upload_sidecar_refused_keeps_both_and_counts_a_failure(tmp_path):
    for status, code in ((500, "internal"), (409, "blob_exists")):
        d = tmp_path / str(status)
        d.mkdir()
        relay_logs(d)
        srv, url, seen = stub_lobby({LOG_PATH: [(200, None)], SIDE_PATH: [(status, code)]})
        try:
            assert upload_logs.upload_all(d, url, KEY) == (0, 1)
        finally:
            srv.shutdown()
        assert names(d) == [f"{GAME}.obfmerlog", f"{GAME}.obfmerlog.input"] and names(d / "uploaded") == []


def test_upload_failed_log_keeps_its_sidecar_unsent(tmp_path):
    relay_logs(tmp_path)
    srv, url, seen = stub_lobby({LOG_PATH: [(500, "internal")], SIDE_PATH: [(200, None)]})
    try:
        assert upload_logs.upload_all(tmp_path, url, KEY) == (0, 2)  # the log and the sidecar waiting for it
    finally:
        srv.shutdown()
    assert [p for p, _, _ in seen] == [LOG_PATH]
    assert names(tmp_path) == [f"{GAME}.obfmerlog", f"{GAME}.obfmerlog.input"]


def test_upload_log_without_sidecar_as_before(tmp_path):
    relay_logs(tmp_path, sidecar=False)
    srv, url, seen = stub_lobby({LOG_PATH: [(200, None)], SIDE_PATH: [(500, "internal")]})
    try:
        assert upload_logs.upload_all(tmp_path, url, KEY) == (1, 0)
    finally:
        srv.shutdown()
    assert [p for p, _, _ in seen] == [LOG_PATH]
    assert names(tmp_path) == [] and names(tmp_path / "uploaded") == [f"{GAME}.obfmerlog"]


# ---- online batch r6 (Sol): a sidecar left behind (a failed log finalization, or a run interrupted between the two moves) ----------------------

def test_upload_sidecar_of_an_uploaded_log_moves_on_the_next_run(tmp_path):
    relay_logs(tmp_path)
    (tmp_path / "uploaded").mkdir()
    os.replace(tmp_path / f"{GAME}.obfmerlog", tmp_path / "uploaded" / f"{GAME}.obfmerlog")  # the log moved, the sidecar did not
    srv, url, seen = stub_lobby({LOG_PATH: [(200, None)], SIDE_PATH: [(500, "internal"), (200, None)]})
    try:
        assert upload_logs.upload_all(tmp_path, url, KEY) == (0, 1)  # refused: it stays
        assert names(tmp_path) == [f"{GAME}.obfmerlog.input"]
        assert upload_logs.upload_all(tmp_path, url, KEY) == (1, 0)
    finally:
        srv.shutdown()
    assert [p for p, _, _ in seen] == [SIDE_PATH, SIDE_PATH]  # the log is not sent again
    assert names(tmp_path) == [] and names(tmp_path / "uploaded") == [f"{GAME}.obfmerlog", f"{GAME}.obfmerlog.input"]


def test_prune_deletes_an_orphaned_sidecar_older_than_keep_days(tmp_path):
    (tmp_path / "uploaded").mkdir()
    old, young = tmp_path / "00000000000000a1.obfmerlog.input", tmp_path / "00000000000000a2.obfmerlog.input"
    waiting = relay_logs(tmp_path)  # a sidecar beside its log is never an orphan
    for p in (old, young):
        p.write_bytes(b"OBFMERLI")
    now = 1_800_000_000
    for p in (old, waiting, waiting.with_name(waiting.name + ".input")):
        os.utime(p, (now - 15 * 86400, now - 15 * 86400))
    os.utime(young, (now - 86400, now - 86400))
    upload_logs.prune(tmp_path, 14.0, now)
    assert names(tmp_path) == ["00000000000000a2.obfmerlog.input", f"{GAME}.obfmerlog", f"{GAME}.obfmerlog.input"]
