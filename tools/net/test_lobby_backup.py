# Lane INFRA-2: the lobby's data backup (services/online/deploy/lobby_backup.py + lobby.mjs's signed admin API) against real self-hosted lobbies
# (server.mjs under Node; skipped loudly without Node). The restore test: lobby A's blobs are pulled into a snapshot, pushed into an empty lobby B, and a
# player of the game then downloads the same replay log from B with a session token, as after a lost lobby.
import base64
import datetime
import hashlib
import hmac
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "services/online/deploy"))
import lobby_backup  # noqa: E402

SESSION = "aa" * 32
ADMIN = "cc" * 32
GAMES = ["00000000000000a1", "00000000000000b2"]
PLAYER = "guest:0123456789abcdef"


def relay_log(game, payload):
    # services/online/relay/RelayCore.h "Log format": "OBFMERLG", u32 version 1, u64 game id, u8 slot mask, u32 start
    return b"OBFMERLG" + struct.pack("<IQBI", 1, int(game, 16), 0b11, 0) + payload


def seed(data):
    for sub in ("logs", "starts", "players", "games"):
        (data / sub).mkdir(parents=True, exist_ok=True)
    for i, g in enumerate(GAMES):
        (data / "logs" / f"{g}.obfmerlog").write_bytes(relay_log(g, bytes([i]) * (1000 + i)))
        (data / "starts" / f"{g}.json").write_text(json.dumps({"v": 1, "kind": "openbfme-replay-start", "game": g, "profile": "0" * 32}))
        (data / "players" / f"{g}.json").write_text(json.dumps([PLAYER, "guest:fedcba9876543210"]))


@pytest.fixture
def lobbies(tmp_path):
    node = shutil.which("node")
    if not node:
        pytest.skip("SKIP LOUDLY: Node.js is not installed here, so the lobby backup / restore test did not run")
    procs = []

    def start(name, admin_key=ADMIN):
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]
        data = tmp_path / name
        data.mkdir()
        env = {**os.environ, "TICKET_KEYS": "1:" + "11" * 32, "SESSION_KEY": SESSION, "UPLOAD_KEY": "bb" * 32, "RELAYS": "",
               "PUBLIC_URL": f"http://127.0.0.1:{port}", "LOBBY_DATA": str(data), "LOBBY_BIND": f"127.0.0.1:{port}"}
        if admin_key:
            env["ADMIN_KEY"] = admin_key
        p = subprocess.Popen([node, str(ROOT / "services/online/lobby/src/server.mjs")], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        procs.append(p)
        assert "listening" in p.stdout.readline()
        return f"http://127.0.0.1:{port}", data

    yield start
    for p in procs:
        p.kill()
        p.wait()


def token(account_id):
    body = json.dumps({"id": account_id, "name": "P", "kind": "guest", "exp": int(time.time()) + 3600}).encode()
    mac = hmac.new(bytes.fromhex(SESSION), body, hashlib.sha256).digest()
    b64 = lambda b: base64.urlsafe_b64encode(b).rstrip(b"=").decode()  # noqa: E731
    return f"{b64(body)}.{b64(mac)}"


def get(url, tok=None):
    req = urllib.request.Request(url, headers={"authorization": f"Bearer {tok}"} if tok else {})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def test_pull_push_restores_the_replays_into_an_empty_lobby(lobbies, tmp_path):
    url_a, data_a = lobbies("a")
    seed(data_a)
    url_b, data_b = lobbies("b")
    key = bytes.fromhex(ADMIN)
    dest = tmp_path / "backups"
    lines = []
    entries = lobby_backup.pull(lobby_backup.Admin(url_a, key), dest, 14, log=lines.append)
    assert [e["key"] for e in entries] == [f"logs/{g}.obfmerlog" for g in GAMES] + [f"players/{g}.json" for g in GAMES] + [f"starts/{g}.json" for g in GAMES]
    snap = lobby_backup.snapshots(dest)[0]
    assert lobby_backup.verify(snap) == 6 and "6 blobs (6 new" in lines[0]
    for e in entries:  # the snapshot holds exactly the lobby's bytes
        assert (snap / e["key"]).read_bytes() == (data_a / e["key"]).read_bytes()
    # before the restore, B has no replay; after it, a player of the game downloads the same log and START record from B
    with pytest.raises(urllib.error.HTTPError) as e:
        get(f"{url_b}/v1/replays/{GAMES[1]}/log", token(PLAYER))
    assert e.value.code == 404
    assert lobby_backup.push(lobby_backup.Admin(url_b, key), snap, log=lines.append) == (6, 0)
    for g in GAMES:
        assert get(f"{url_b}/v1/replays/{g}/log", token(PLAYER)) == (data_a / "logs" / f"{g}.obfmerlog").read_bytes()
        assert json.loads(get(f"{url_b}/v1/replays/{g}/start", token(PLAYER)))["game"] == g
    # and only the game's players read it on B too
    with pytest.raises(urllib.error.HTTPError) as e:
        get(f"{url_b}/v1/replays/{GAMES[0]}/log", token("guest:1111111111111111"))
    assert e.value.code == 403
    # a second push changes nothing (unchanged), so a restore can be re-run into a lobby that already has some of the blobs
    assert lobby_backup.push(lobby_backup.Admin(url_b, key), snap, log=lines.append) == (0, 6)


def test_incremental_pull_hard_links_old_blobs_and_keeps_14_days(lobbies, tmp_path):
    url, data = lobbies("a")
    seed(data)
    admin = lobby_backup.Admin(url, bytes.fromhex(ADMIN))
    dest = tmp_path / "backups"
    day0 = datetime.date(2026, 10, 1)
    lobby_backup.pull(admin, dest, 14, today=day0, log=lambda _: None)
    g = "00000000000000c3"
    (data / "logs" / f"{g}.obfmerlog").write_bytes(relay_log(g, b"new game"))
    lines = []
    lobby_backup.pull(admin, dest, 14, today=day0 + datetime.timedelta(days=1), log=lines.append)
    old, new = lobby_backup.snapshots(dest)
    assert "7 blobs (1 new" in lines[0]
    k = f"logs/{GAMES[0]}.obfmerlog"
    assert os.stat(old / k).st_ino == os.stat(new / k).st_ino  # the same file, not a second copy
    assert not (old / f"logs/{g}.obfmerlog").exists() and (new / f"logs/{g}.obfmerlog").read_bytes() == relay_log(g, b"new game")
    # day 16: the snapshots of day 0 and day 1 are past the 14 days and go; the newest snapshot stays complete
    lobby_backup.pull(admin, dest, 14, today=day0 + datetime.timedelta(days=16), log=lines.append)
    assert [s.name for s in lobby_backup.snapshots(dest)] == ["2026-10-17"] and "pruned 2026-10-01, 2026-10-02" in lines[-1]
    assert lobby_backup.verify(lobby_backup.snapshots(dest)[0]) == 7


def test_keep_days_14_keeps_exactly_14_dates(lobbies, tmp_path):
    # INFRA-2 r2 (Sol r1): a daily pull for 20 days with --keep-days 14 leaves the 14 newest dates, today's included
    url, data = lobbies("a")
    seed(data)
    admin = lobby_backup.Admin(url, bytes.fromhex(ADMIN))
    dest = tmp_path / "backups"
    day0 = datetime.date(2026, 10, 1)
    for d in range(20):
        lobby_backup.pull(admin, dest, 14, today=day0 + datetime.timedelta(days=d), log=lambda _: None)
    names = [s.name for s in lobby_backup.snapshots(dest)]
    assert len(names) == 14 and names[0] == "2026-10-07" and names[-1] == "2026-10-20"


def test_a_damaged_snapshot_is_refused_before_anything_is_pushed(lobbies, tmp_path):
    url_a, data_a = lobbies("a")
    seed(data_a)
    url_b, data_b = lobbies("b")
    key = bytes.fromhex(ADMIN)
    lobby_backup.pull(lobby_backup.Admin(url_a, key), tmp_path / "bk", 14, log=lambda _: None)
    snap = lobby_backup.snapshots(tmp_path / "bk")[0]
    p = snap / "logs" / f"{GAMES[1]}.obfmerlog"
    os.unlink(p)  # break the hard link, then change one byte
    p.write_bytes(relay_log(GAMES[1], b"\x01" * 1000 + b"\x02"))
    with pytest.raises(lobby_backup.BackupError, match="does not match its manifest"):
        lobby_backup.push(lobby_backup.Admin(url_b, key), snap)
    assert not any((data_b / "logs").iterdir()) and not any((data_b / "starts").iterdir())


def test_the_admin_api_refuses_wrong_keys_stale_clocks_overwrites_and_lobbies_without_a_key(lobbies, tmp_path):
    url, data = lobbies("a")
    seed(data)
    good = lobby_backup.Admin(url, bytes.fromhex(ADMIN))
    with pytest.raises(lobby_backup.BackupError, match="HTTP 401 admin_signature"):
        lobby_backup.Admin(url, bytes.fromhex("dd" * 32)).list_all()
    with pytest.raises(lobby_backup.BackupError, match="HTTP 401 admin_clock"):
        lobby_backup.Admin(url, bytes.fromhex(ADMIN), now=lambda: time.time() - 600).list_all()
    # the upload key (held by every relay) is not an admin key
    with pytest.raises(lobby_backup.BackupError, match="HTTP 401 admin_signature"):
        lobby_backup.Admin(url, bytes.fromhex("bb" * 32)).call("GET", f"/v1/admin/blobs/logs/{GAMES[0]}.obfmerlog")
    # a stored blob is never replaced, a log is stored only under its own game, and only the three kinds of key are accepted
    with pytest.raises(lobby_backup.BackupError, match="HTTP 409 blob_exists"):
        good.call("POST", f"/v1/admin/blobs/logs/{GAMES[0]}.obfmerlog", body=relay_log(GAMES[0], b"other bytes"))
    with pytest.raises(lobby_backup.BackupError, match="HTTP 409 log_game_mismatch"):
        good.call("POST", "/v1/admin/blobs/logs/00000000000000d4.obfmerlog", body=relay_log(GAMES[0], b"x"))
    with pytest.raises(lobby_backup.BackupError, match="HTTP 400 bad_field"):
        good.call("GET", "/v1/admin/blobs/games/00000000000000a1.json")
    # a signature is bound to its path and body: the one for one key does not fetch another
    ts = str(int(time.time()))
    mac = hmac.new(bytes.fromhex(ADMIN), f"GET /v1/admin/blobs/logs/{GAMES[0]}.obfmerlog?\n{ts}\n".encode(), hashlib.sha256).hexdigest()
    req = urllib.request.Request(f"{url}/v1/admin/blobs/logs/{GAMES[1]}.obfmerlog", headers={"x-openbfme-admin": f"{ts}.{mac}"})
    with pytest.raises(urllib.error.HTTPError) as e:
        urllib.request.urlopen(req, timeout=30)
    assert e.value.code == 401
    # a lobby deployed without ADMIN_KEY refuses every admin call (501), it never falls back to another key
    url2, data2 = lobbies("nokey", admin_key=None)
    with pytest.raises(lobby_backup.BackupError, match="HTTP 501 not_configured"):
        lobby_backup.Admin(url2, bytes.fromhex(ADMIN)).list_all()


def test_cli_pull_and_verify(lobbies, tmp_path):
    url, data = lobbies("a")
    seed(data)
    kf = tmp_path / "admin.key"
    kf.write_text(ADMIN + "\n")
    script = str(ROOT / "services/online/deploy/lobby_backup.py")
    r = subprocess.run([sys.executable, script, "pull", "--lobby", url, "--admin-key-file", str(kf), "--dest", str(tmp_path / "bk")], capture_output=True, text=True)
    assert r.returncode == 0 and "6 blobs (6 new" in r.stdout and ADMIN not in r.stdout + r.stderr, r.stdout + r.stderr
    assert (os.stat(tmp_path / "bk").st_mode & 0o777) == 0o700
    snap = lobby_backup.snapshots(tmp_path / "bk")[0]
    r = subprocess.run([sys.executable, script, "verify", "--snapshot", str(snap)], capture_output=True, text=True)
    assert r.returncode == 0 and "6 blobs match" in r.stdout
    (snap / "starts" / f"{GAMES[0]}.json").unlink()
    r = subprocess.run([sys.executable, script, "verify", "--snapshot", str(snap)], capture_output=True, text=True)
    assert r.returncode == 1 and "is missing or does not match" in r.stderr
