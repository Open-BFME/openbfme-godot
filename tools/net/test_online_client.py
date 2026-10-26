# Lane ONLINE-2: the test client's ticket is the 32 bytes the relay checks (the same pin as engine/tests/test_online_relay.cpp and the lobby's test), its
# fixture writes one START per human slot, and the lobby's own tests run when Node.js is installed (skipped loudly otherwise).
import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import online_client  # noqa: E402

KEY = bytes(range(32))


@pytest.mark.parametrize("args,expected", [([], 120), (["--spectator-delay", "0"], 0), (["--spectator-delay", "600"], 600)])
def test_host_spectator_delay_default_and_override(monkeypatch, args, expected):
    seen = []
    monkeypatch.setattr(online_client, "cmd_host", lambda a: seen.append(a.spectator_delay) or 0)
    monkeypatch.setattr(sys, "argv", ["online_client.py", "host", "--lobby", "http://127.0.0.1:8787", "--name", "Host",
                                    "--profile", "0" * 32, "--out", "unused.json", "--map", "maps/x/x.map", "--slot", "player,0,0,0,0"] + args)
    assert online_client.main() == 0
    assert seen == [expected]


def test_ticket_pin():
    assert online_client.ticket(KEY, 1, "0123456789abcdef", 1, 0b0111, 2000000000) == "AQHvzauJZ0UjAQEHAJQ1dxkKDPX40Zd_QbpSaTVc2UI"


def test_rejoin_ticket_pin():
    # lane ONLINE-3: the version 2 rejoin ticket, the same pin as the lobby's test and engine/tests/test_online3_rejoin.cpp
    assert online_client.rejoin_ticket(KEY, 1, "0123456789abcdef", 1, 0b0111, 2000000000, 3) == "AgHvzauJZ0UjAQEHAJQ1dwMAb4T6YFP3_RWwFnRvvO1_ow"


def test_spectator_ticket_pin():
    # lane ONLINE-3: the version 3 spectator ticket, the same pin as the lobby's test and engine/tests/test_online3_spectate.cpp
    assert online_client.spectator_ticket(KEY, 1, "0123456789abcdef", 2, 0b0111, 2000000000, 60) == "AwHvzauJZ0UjAQIHAJQ1dzwADqM2vSmJqqkoEsrChDi_PA"


def test_fixture_writes_a_start_per_human_slot(tmp_path):
    r = subprocess.run([sys.executable, str(HERE / "online_client.py"), "fixture", "--key-hex", KEY.hex(), "--game", "00000000000000aa", "--relay",
                        "192.0.2.10:7350", "--profile", "0" * 32, "--map", "maps/x/x.map", "--slot", "player,0,0,0,0", "--slot", "hard,1,1,1,1",
                        "--slot", "player,2,2,2,0", "--out-dir", str(tmp_path), "--rejoin", "2:1", "--spectator", "0:60"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    s0 = json.loads((tmp_path / "start-0.json").read_text())
    s2 = json.loads((tmp_path / "start-2.json").read_text())
    assert not (tmp_path / "start-1.json").exists()
    assert s0["setup"] == s2["setup"] and s0["slot"] == 0 and s2["slot"] == 2
    t = online_client.base64.urlsafe_b64decode(s2["ticket"] + "=")
    assert len(t) == 32 and t[10] == 2 and t[11] == 0b101
    assert s0["setup"]["slots"][1] == {"state": "hard", "faction": 1, "color": 1, "startPos": 1, "team": 1, "name": "Computer"}
    r2 = json.loads((tmp_path / "start-2-rejoin1.json").read_text())  # lane ONLINE-3
    t2 = online_client.base64.urlsafe_b64decode(r2["ticket"] + "==")
    assert r2["rejoin"] == 1 and len(t2) == 34 and t2[0] == 2 and t2[16] == 1 and r2["setup"] == s2["setup"]
    sp = json.loads((tmp_path / "spectate-0.json").read_text())
    t3 = online_client.base64.urlsafe_b64decode(sp["ticket"] + "==")
    assert sp["kind"] == "openbfme-spectate" and "slot" not in sp and sp["spectator"] == 0 and sp["delay"] == 60
    assert len(t3) == 34 and t3[0] == 3 and t3[10] == 0 and t3[16:18] == bytes([60, 0]) and sp["setup"] == s2["setup"]


def test_lobby_service_node_tests():
    node = shutil.which("node")
    if not node:
        pytest.skip("SKIP LOUDLY: Node.js is not installed here, so services/online/lobby's tests (node --test) did not run")
    r = subprocess.run([node, "--test", str(HERE.parents[1] / "services/online/lobby/test/lobby.test.mjs")], capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-2000:]


def test_smoke_against_a_lobby_without_a_relay(tmp_path):
    # CFDEPLOY-1: the smoke subcommand (the deployment check of the deploy notes) passes against a lobby with RELAYS empty: START refused
    node = shutil.which("node")
    if not node:
        pytest.skip("SKIP LOUDLY: Node.js is not installed here, so the lobby smoke check did not run")
    import os
    import socket
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    env = {**os.environ, "TICKET_KEYS": "1:" + "11" * 32, "SESSION_KEY": "aa" * 32, "UPLOAD_KEY": "bb" * 32, "RELAYS": "",
           "PUBLIC_URL": f"http://127.0.0.1:{port}", "LOBBY_DATA": str(tmp_path), "LOBBY_BIND": f"127.0.0.1:{port}"}
    server = subprocess.Popen([node, str(HERE.parents[1] / "services/online/lobby/src/server.mjs")], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        # Node may print warnings first (e.g. SQLite's ExperimentalWarning): wait for the lobby's own "listening" line
        seen = []
        for _ in range(20):
            line = server.stdout.readline()
            if not line:
                break
            seen.append(line)
            if "listening" in line:
                break
        assert seen and "listening" in seen[-1], "".join(seen)
        smoke = [sys.executable, str(HERE / "online_client.py"), "smoke", "--lobby", f"http://127.0.0.1:{port}", "--profile", "0" * 32, "--map", "maps/x/x.map"]
        r = subprocess.run(smoke + ["--expect-no-relay"], capture_output=True, text=True, timeout=60)
        assert r.returncode == 0, r.stdout + r.stderr
        assert "START refused: HTTP 503 no_relay" in r.stdout and "smoke: 11 of 11 checks passed" in r.stdout
        # without --expect-no-relay the same lobby fails the START check: the smoke catches a lobby that cannot start games
        r = subprocess.run(smoke, capture_output=True, text=True, timeout=60)
        assert r.returncode == 1 and "FAIL START: HTTP 503" in r.stdout, r.stdout + r.stderr
    finally:
        server.kill()
        server.wait()


def test_fixture_auth_writes_each_tickets_mac_key_and_the_tag_required_mode(tmp_path):
    """ONLINE16-FIX: --auth gives every START its ticket's session_key (AUTH-1); --mode 4 signs TICKET_MODE_AUTH into a version-7 ticket"""
    import hashlib
    import hmac
    r = subprocess.run([sys.executable, str(HERE / "online_client.py"), "fixture", "--key-hex", KEY.hex(), "--game", "00000000000000aa", "--relay",
                        "192.0.2.10:7350", "--profile", "0" * 32, "--map", "maps/x/x.map", "--slot", "player,0,0,0,0", "--slot", "player,1,1,1,1",
                        "--out-dir", str(tmp_path), "--rejoin", "1:1", "--spectator", "0:0", "--auth", "--mode", "4"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    for name in ("start-0.json", "start-1.json", "start-1-rejoin1.json", "spectate-0.json"):
        s = json.loads((tmp_path / name).read_text())
        raw = online_client.base64.urlsafe_b64decode(s["ticket"] + "=" * (-len(s["ticket"]) % 4))
        want = online_client.base64.urlsafe_b64encode(hmac.new(KEY, b"OBFME-SESSION-1" + raw, hashlib.sha256).digest()[:16]).rstrip(b"=").decode()
        assert s["session_key"] == want, name
    t = online_client.base64.urlsafe_b64decode(json.loads((tmp_path / "start-1.json").read_text())["ticket"] + "=" * 0)
    assert t[0] == 7 and t[13] == 4 and len(t) == 36
    # without --auth no START carries one (an old lobby's START)
    r = subprocess.run([sys.executable, str(HERE / "online_client.py"), "fixture", "--key-hex", KEY.hex(), "--game", "00000000000000aa", "--relay",
                        "192.0.2.10:7350", "--profile", "0" * 32, "--map", "maps/x/x.map", "--slot", "player,0,0,0,0", "--slot", "player,1,1,1,1",
                        "--out-dir", str(tmp_path / "plain")], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert "session_key" not in json.loads((tmp_path / "plain" / "start-0.json").read_text())


def test_session_key_pin():
    """ONLINE16-FIX: the START's session_key is lane/anticheat1's OBFME-SESSION-1 derivation (pinned there and in test_online16_fix.cpp)"""
    t = online_client.ticket(bytes(range(32)), 1, "0123456789abcdef", 0, 7, 2000000000)
    assert online_client.session_key(bytes(range(32)), t) == "cHLqM4pf8wcINff_naIhJg"
