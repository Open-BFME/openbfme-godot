# Lane INFRA-2: services/online/deploy/monitor.py, the online service's 10-minute check on bfme-pve. Each failure mode it is meant to catch, against a
# local UDP stand-in for the relay, stand-in health answers and backup folders: the line names it and the run exits 1.
import datetime
import io
import json
import socket
import sys
import threading
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "services/online/deploy"))
import monitor  # noqa: E402

RELAY = "203.0.113.10:7350"


def health(doc):
    return lambda url: io.BytesIO(json.dumps(doc).encode())


GOOD = {"v": 1, "ok": True, "games": 2, "relays": [{"id": "us-east", "address": RELAY}], "logStore": "durable-object"}


def test_lobby_checks():
    assert monitor.check_lobby("https://x", RELAY, "durable-object", opener=health(GOOD))[0]
    ok, why = monitor.check_lobby("https://x", RELAY, "durable-object", opener=health({**GOOD, "relays": [], "relay": "no relay configured"}))
    assert not ok and "not listed" in why and "no relay configured" in why
    ok, why = monitor.check_lobby("https://x", RELAY, "durable-object", opener=health({**GOOD, "relays": [{"address": "198.51.100.1:7350"}]}))
    assert not ok and "198.51.100.1:7350" in why
    assert "log store" in monitor.check_lobby("https://x", RELAY, "durable-object", opener=health({**GOOD, "logStore": "none"}))[1]
    assert "health not ok" in monitor.check_lobby("https://x", RELAY, "durable-object", opener=health({**GOOD, "ok": False}))[1]

    def down(url):
        raise OSError("connection refused")
    ok, why = monitor.check_lobby("https://x", RELAY, "durable-object", opener=down)
    assert not ok and "unreachable" in why


def fake_relay(answer):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    got = []

    def serve():
        s.settimeout(5)
        try:
            while True:
                d, addr = s.recvfrom(64)
                got.append(d)
                if answer is not None:
                    s.sendto(answer(d), addr)
        except OSError:
            pass
    threading.Thread(target=serve, daemon=True).start()
    return s, f"127.0.0.1:{s.getsockname()[1]}", got


def test_relay_probe_not_bound_refused_and_silence():
    s, addr, got = fake_relay(lambda d: b"BR\x03\x05")
    ok, why = monitor.probe_relay(addr)
    s.close()
    assert ok and "NOT_BOUND" in why and got[0] == b"BR\x03\x03\x00"  # a DATA envelope, slot 0, no ticket: nothing to bind
    s, addr, _ = fake_relay(lambda d: b"BR\x02\x06\x01")
    ok, why = monitor.probe_relay(addr)
    s.close()
    assert not ok and "REFUSED reason 1 (envelope version 2; this monitor speaks 3)" in why
    s, addr, got = fake_relay(None)
    ok, why = monitor.probe_relay(addr, tries=2, wait=0.2)
    s.close()
    assert not ok and "no answer to 2 probes" in why and len(got) == 2


def snapshot(d, name, taken):
    (d / name).mkdir(parents=True)
    (d / name / "manifest.json").write_text(json.dumps({"taken": taken.isoformat(), "blobs": [{"key": "logs/x"}]}))


def test_backup_age(tmp_path):
    now = datetime.datetime(2026, 10, 13, 12, tzinfo=datetime.timezone.utc)
    assert "no snapshot" in monitor.check_backup(tmp_path, now=now)[1]
    snapshot(tmp_path, "2026-10-11", now - datetime.timedelta(hours=37))
    ok, why = monitor.check_backup(tmp_path, now=now)
    assert not ok and "2026-10-11 is 37 h old" in why
    snapshot(tmp_path, "2026-10-13", now - datetime.timedelta(hours=7))
    ok, why = monitor.check_backup(tmp_path, now=now)
    assert ok and why.startswith("2026-10-13 1 blobs (7.0 h old, 2 kept)")
    (tmp_path / "2026-10-14").mkdir()
    assert "no readable manifest" in monitor.check_backup(tmp_path, now=now)[1]


def test_one_line_per_run_and_the_exit_code(tmp_path, monkeypatch):
    monkeypatch.setattr(monitor, "check_lobby", lambda *a: (True, "ok"))
    monkeypatch.setattr(monitor, "probe_relay", lambda *a: (False, "x no answer"))
    monkeypatch.setattr(monitor, "admin_get", lambda *a: {"now": 1000, "relays": [{**STATUS, "relay": "3.13.126.208:7350"}]})
    snapshot(tmp_path / "bk", "2099-01-01", datetime.datetime.now(datetime.timezone.utc))
    log = tmp_path / "m" / "status.log"
    key = tmp_path / "admin.key"
    key.write_text("cc" * 32)
    common = ["--admin-key-file", str(key), "--backups", str(tmp_path / "bk"), "--log", str(log)]
    assert monitor.main(common) == 1
    monkeypatch.setattr(monitor, "probe_relay", lambda *a: (True, "x NOT_BOUND"))
    assert monitor.main(common) == 0
    lines = log.read_text().splitlines()
    assert len(lines) == 2 and " FAIL lobby ok | relay FAIL x no answer | uploads ok " in lines[0] and " OK lobby ok | relay x NOT_BOUND | uploads ok " in lines[1]
    # INFRA-2 r2 (Sol r1): a check that crashes is a FAIL in the line, never a missing line
    monkeypatch.setattr(monitor, "check_lobby", lambda *a: 1 / 0)
    assert monitor.main(common) == 1
    assert " FAIL lobby FAIL check crashed: ZeroDivisionError" in log.read_text().splitlines()[-1]


STATUS = {"relay": RELAY, "at": 990, "receivedAt": 990, "pending": 0, "partial": 1, "oldestPendingS": 0, "uploaded": 2, "failed": 0, "logErrors": 0}


def test_relay_upload_status_checks():
    ok, why = monitor.check_uploads({"now": 1000, "relays": [STATUS]}, RELAY)
    assert ok and why == "ok pending 0 running 1 (reported 10 s ago)"
    cases = [
        ({"relays": []}, "never reported"),
        ({"relays": [{**STATUS, "receivedAt": 1000 - 660}]}, "uploader silent 11 min"),
        ({"relays": [{**STATUS, "failed": 1}]}, "1 upload(s) failed"),
        ({"relays": [{**STATUS, "pending": 3, "oldestPendingS": 1200}]}, "3 log(s) waiting, the oldest 20 min"),
        ({"relays": [{**STATUS, "logErrors": 2}]}, "could not write 2 game log(s)"),
        ({"relays": [{**STATUS, "logErrors": None}]}, "status file is unreadable"),
    ]
    for doc, want in cases:
        ok, why = monitor.check_uploads({"now": 1000, **doc}, RELAY)
        assert not ok and want in why, why


def test_malformed_manifests_are_a_fail_not_a_crash(tmp_path):
    now = datetime.datetime(2026, 10, 13, 12, tzinfo=datetime.timezone.utc)
    for i, text in enumerate(["[1, 2]", '{"taken": "2026-10-13T10:00:00", "blobs": []}', '{"taken": "2026-10-13T10:00:00+00:00", "blobs": 3}',
                              '{"taken": 5, "blobs": []}', "\x00\xff"]):
        d = tmp_path / str(i) / "2026-10-13"
        d.mkdir(parents=True)
        (d / "manifest.json").write_text(text)
        ok, why = monitor.check_backup(tmp_path / str(i), now=now)
        assert not ok and "no readable manifest" in why, (text, why)


def test_stale_says_when_the_last_ok_line_is_old(tmp_path):
    log = tmp_path / "status.log"
    now = datetime.datetime(2026, 10, 13, 12, 0, tzinfo=datetime.timezone.utc)
    assert monitor.stale(log, now=now)[1].startswith("MONITOR STALE: cannot read")
    log.write_text("2026-10-13T11:40:00Z FAIL lobby FAIL x\n")
    assert monitor.stale(log, now=now) == (False, "MONITOR STALE: no OK line in the status log (1 lines); last: 2026-10-13T11:40:00Z FAIL lobby FAIL x")
    log.write_text("2026-10-13T11:10:00Z OK lobby ok\n2026-10-13T11:20:00Z FAIL relay FAIL y\n")
    ok, why = monitor.stale(log, now=now)
    assert not ok and "(2026-10-13T11:10:00Z) is 50 min old" in why and "FAIL relay FAIL y" in why  # every run failing since counts too
    log.write_text("2026-10-13T11:50:00Z OK lobby ok\n")
    assert monitor.stale(log, now=now) == (True, "MONITOR OK: last OK line 2026-10-13T11:50:00Z (10 min old)")


def test_deck_watch_writes_one_line_for_ok_stale_and_unreachable(tmp_path):
    import os
    import subprocess
    script = HERE.parents[1] / "services/online/deploy/deck_watch.sh"
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    log = tmp_path / "watch.log"
    for answer, code, want, rc in (("MONITOR OK: last OK line x (3 min old)\n|| 2026 OK lobby ok", 0, "MONITOR OK: last OK line x (3 min old) || 2026 OK lobby ok", 0),
                                   ("MONITOR STALE: the last OK line (x) is 47 min old\n|| y", 0, "MONITOR STALE: the last OK line (x) is 47 min old || y", 1),
                                   ("ssh: connect to host bfme-pve port 22: No route to host", 255, "MONITOR UNREACHABLE: ssh bfme-pve failed (exit 255): ssh: connect", 1)):
        (tmp_path / "answer.txt").write_text(answer + "\n")
        (bin_dir / "ssh").write_text(f"#!/bin/bash\ncat {tmp_path / 'answer.txt'}\nexit {code}\n")
        (bin_dir / "ssh").chmod(0o755)
        r = subprocess.run(["bash", str(script), "run"], env={**os.environ, "PATH": f"{bin_dir}:{os.environ['PATH']}", "WATCH_LOG": str(log)},
                           capture_output=True, text=True)
        assert r.returncode == rc and want in log.read_text().splitlines()[-1], (r.stdout, r.stderr, log.read_text())
    assert len(log.read_text().splitlines()) == 3


def test_the_relay_uploader_status_reaches_the_monitor_through_a_real_lobby(tmp_path):
    # upload_logs.py (with --relay-id) -> POST /v1/relay/status on a self-hosted lobby -> monitor.admin_get + check_uploads, as on pve
    import os
    import shutil
    import socket
    import subprocess
    node = shutil.which("node")
    if not node:
        pytest.skip("SKIP LOUDLY: Node.js is not installed here, so the relay status round trip did not run")
    root = HERE.parents[1]
    with socket.socket() as so:
        so.bind(("127.0.0.1", 0))
        port = so.getsockname()[1]
    url = f"http://127.0.0.1:{port}"
    env = {**os.environ, "TICKET_KEYS": "1:" + "11" * 32, "SESSION_KEY": "aa" * 32, "UPLOAD_KEY": "bb" * 32, "ADMIN_KEY": "cc" * 32, "RELAYS": "",
           "PUBLIC_URL": url, "LOBBY_DATA": str(tmp_path / "lobby"), "LOBBY_BIND": f"127.0.0.1:{port}"}
    server = subprocess.Popen([node, str(root / "services/online/lobby/src/server.mjs")], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        assert "listening" in server.stdout.readline()
        logs = tmp_path / "relay-logs"
        logs.mkdir()
        (tmp_path / "upload.key").write_text("bb" * 32)
        status = tmp_path / "relay-status.json"
        uploader = [sys.executable, str(root / "services/online/relay/upload_logs.py"), "--log-dir", str(logs), "--lobby", url,
                    "--upload-key-file", str(tmp_path / "upload.key"), "--relay-id", "203.0.113.10:7350", "--status-file", str(status)]
        key = bytes.fromhex("cc" * 32)
        # the relay is down (no status file): the status says so and the monitor fails
        assert subprocess.run(uploader, capture_output=True, text=True).returncode == 0
        ok, why = monitor.check_uploads(monitor.admin_get(url, key, "/v1/admin/relay-status"), "203.0.113.10:7350")
        assert not ok and "status file is unreadable" in why
        # a log the lobby refuses (no such game) stays, fails the run, and the monitor names it
        status.write_text(json.dumps({"logErrors": 0}))
        (logs / "00000000000000aa.obfmerlog").write_bytes(b"OBFMERLG" + bytes(17))
        assert subprocess.run(uploader, capture_output=True, text=True).returncode == 1
        ok, why = monitor.check_uploads(monitor.admin_get(url, key, "/v1/admin/relay-status"), "203.0.113.10:7350")
        assert not ok and "1 upload(s) failed" in why
        (logs / "00000000000000aa.obfmerlog").unlink()
        assert subprocess.run(uploader, capture_output=True, text=True).returncode == 0
        assert monitor.check_uploads(monitor.admin_get(url, key, "/v1/admin/relay-status"), "203.0.113.10:7350")[0]
        # the status survives a lobby restart (stored, not only in memory) and is not part of the backup's blob listing
        server.kill()
        server.wait()
        server = subprocess.Popen([node, str(root / "services/online/lobby/src/server.mjs")], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        assert "listening" in server.stdout.readline()
        doc = monitor.admin_get(url, key, "/v1/admin/relay-status")
        assert [r["relay"] for r in doc["relays"]] == ["203.0.113.10:7350"]
        sys.path.insert(0, str(root / "services/online/deploy"))
        import lobby_backup
        assert lobby_backup.Admin(url, key).list_all() == []
        # unsigned status reads and wrongly signed / stale status posts are refused
        import urllib.error
        import urllib.request
        with pytest.raises(urllib.error.HTTPError) as e:
            urllib.request.urlopen(urllib.request.Request(url + "/v1/admin/relay-status", headers={"user-agent": "t"}), timeout=10)
        assert e.value.code == 401
        sys.path.insert(0, str(root / "services/online/relay"))
        import upload_logs
        assert upload_logs.post(url + "/v1/relay/status", upload_logs.status_body("203.0.113.10:7350", logs, None, 0, 0), b"x" * 32).startswith("HTTP 401")
        old = upload_logs.status_body("203.0.113.10:7350", logs, None, 0, 0, now=1_000_000)
        assert "status_clock" in upload_logs.post(url + "/v1/relay/status", old, bytes.fromhex("bb" * 32))
    finally:
        server.kill()
        server.wait()

