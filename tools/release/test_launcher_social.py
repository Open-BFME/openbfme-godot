#!/usr/bin/env python3
"""Focused social-client tests using Godot's HTTPRequest and a loopback HTTP stand-in."""
from __future__ import annotations

import argparse
import json
import re
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

REPO = Path(__file__).resolve().parents[2]
LAUNCHER = REPO / "launcher"
TEST_SCRIPT = "res://tests/social_client_test.gd"
SECRET = "social-test-token-never-print"
RECOVERY_CODE = "0123456789ABCDEF"
SELF_PID = "0123456789abcdef"
FRIEND_PID = "fedcba9876543210"
FRIEND_CODE = "ABCD-EFGH-JK"


def identity_player(pid: str, nick: str) -> dict:
    return {"pid": pid, "nick": nick, "code": FRIEND_CODE}


def presence(pid: str, state: str = "online") -> dict:
    return {"pid": pid, "state": state, "game": None, "joinable": False, "since": 1700000000, "map": None, "mode": None}


def snapshot(revision: int, reset: bool, with_items: bool = False) -> dict:
    requests = []
    notices = []
    if with_items:
        requests = [{"id": "aaaaaaaaaaaaaaaa", "from": identity_player(FRIEND_PID, "Friend"),
                     "to": identity_player(SELF_PID, "Alice"), "at": 1700000000}]
        notices = [{"id": "notice_1", "kind": "expired", "invite": None, "at": 1700000000}]
    return {
        "v": 1,
        "seq": f"0123456789abcdef:{revision}",
        "reset": reset,
        "presence": [presence(FRIEND_PID)],
        "requests": requests,
        "invites": [],
        "notices": notices,
        "chat": [],
        "stats": {"v": 1, "players": 2, "games_open": 0, "games_running": 0, "as_of": 1700000000},
        "wait": 25,
    }


class SocialServer:
    def __init__(self) -> None:
        self.mode = ""
        self.lock = threading.Lock()
        self.inbox_calls = 0
        self.send_calls = 0
        self.auth_calls = 0
        self.health_calls = 0
        self.me_calls = 0
        self.friends_calls = 0
        self.friend_request_calls = 0
        self.inbox_times: list[float] = []
        self.large_payload_sizes: list[int] = []
        self.auth_installs: list[str] = []
        self.send_client_ids: list[str] = []
        self.redirect_capture_count = 0
        self.redirect_capture_authorized = False
        self.protected_authorized: list[bool] = []
        server = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args) -> None:
                return

            def _body(self) -> dict:
                length = min(int(self.headers.get("Content-Length", "0")), 16384)
                raw = self.rfile.read(length)
                try:
                    return json.loads(raw or b"{}")
                except (json.JSONDecodeError, UnicodeDecodeError):
                    return {}

            def _json(self, status: int, value: dict, headers: dict | None = None) -> None:
                body = json.dumps(value, separators=(",", ":")).encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Cache-Control", "no-store")
                for key, val in (headers or {}).items():
                    self.send_header(key, val)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                try:
                    self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError):
                    pass

            def _record_auth(self) -> None:
                with server.lock:
                    server.protected_authorized.append(self.headers.get("Authorization") == f"Bearer {SECRET}")

            def do_GET(self) -> None:
                path = urlsplit(self.path).path
                if path == "/capture":
                    with server.lock:
                        server.redirect_capture_count += 1
                    return self._json(200, {"v": 1, "ok": True})
                if path == "/v1/health":
                    # ONLINEMENU-1 (BATCH-P3): the launcher signs in only where the lobby names the social service; "no_social" is the
                    # deployed lobby before the social routes (no features)
                    with server.lock:
                        server.health_calls += 1
                        mode = server.mode
                    health = {"v": 1, "ok": True, "games": 0, "relays": []}
                    if mode != "no_social":
                        health["features"] = ["social"]
                    return self._json(200, health)
                if path == "/v1/me":
                    self._record_auth()
                    with server.lock:
                        server.me_calls += 1
                        count = server.me_calls
                        mode = server.mode
                        redirect = server.mode == "redirect"
                    if mode == "signin_me_503" and count == 1:
                        return self._json(503, {"v": 1, "error": "social_unavailable"})
                    if redirect:
                        self.send_response(302)
                        self.send_header("Location", server.capture_origin + "/capture")
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                        return
                    return self._json(200, {
                        "v": 1, "pid": SELF_PID, "code": "AAAA-BBBB-CC", "nick": "Alice",
                        "privacy": {"invites": "friends_or_room", "challenges": "friends_or_room",
                                    "requests": "anyone", "presence": "friends"},
                        "hidden": False,
                    })
                if path == "/v1/friends":
                    self._record_auth()
                    with server.lock:
                        server.friends_calls += 1
                        count = server.friends_calls
                        mode = server.mode
                    if mode == "signin_friends_503" and count == 1:
                        return self._json(503, {"v": 1, "error": "social_unavailable"})
                    if mode == "malformed_friends":
                        return self._json(200, {"v": 1, "friends": [{
                            "pid": FRIEND_PID, "nick": 123, "code": FRIEND_CODE, "since": 1,
                            "presence": presence(FRIEND_PID),
                        }]})
                    if mode == "refresh_retry":
                        if count == 2:
                            return self._json(503, {"v": 1, "error": "social_unavailable"})
                        rows = [{"pid": FRIEND_PID, "nick": "Friend", "code": FRIEND_CODE, "since": 1,
                                 "presence": presence(FRIEND_PID)}]
                        if count > 2:
                            rows.append({"pid": "1111111111111111", "nick": "Other", "code": FRIEND_CODE,
                                         "since": 2, "presence": presence("1111111111111111")})
                        return self._json(200, {"v": 1, "friends": rows})
                    if mode == "large_friends":
                        rows = []
                        for index in range(200):
                            pid = f"{index + 100:016x}"
                            rows.append({"pid": pid, "nick": "🧪" * 24, "code": FRIEND_CODE, "since": index,
                                         "presence": {**presence(pid), "map": "🧪" * 64}})
                        payload = {"v": 1, "friends": rows}
                        with server.lock:
                            server.large_payload_sizes.append(len(json.dumps(payload, separators=(",", ":")).encode()))
                        return self._json(200, payload)
                    return self._json(200, {"v": 1, "friends": [{
                        "pid": FRIEND_PID, "nick": "Friend", "code": FRIEND_CODE, "since": 1700000000,
                        "presence": presence(FRIEND_PID),
                    }]})
                if path == "/v1/inbox":
                    self._record_auth()
                    with server.lock:
                        server.inbox_calls += 1
                        count = server.inbox_calls
                        mode = server.mode
                        server.inbox_times.append(time.monotonic())
                    if mode == "normal" and count == 1:
                        return self._json(429, {"v": 1, "error": "rate_limited", "message": "server-only-secret"},
                                          {"Retry-After": "1"})
                    if mode == "late" and count == 2:
                        time.sleep(1.5)
                    if mode == "held_poll" and count == 2:
                        time.sleep(26)
                    if mode == "refresh_retry":
                        result = snapshot(count, count == 1)
                        result["presence"] = [presence(FRIEND_PID), presence("1111111111111111")]
                        if count == 1:
                            result["backoff"] = 10
                        return self._json(200, result)
                    if mode == "large_friends":
                        result = snapshot(count, count == 1)
                        result["presence"] = [
                            {**presence(f"{index + 100:016x}"), "map": "🧪" * 64}
                            for index in range(200)
                        ]
                        result["backoff"] = 10
                        return self._json(200, result)
                    if mode == "malformed_inbox":
                        result = snapshot(count, count == 1)
                        result["presence"][0]["device"] = "synthetic-private-field"
                        result["presence"][0]["game"] = 123
                        return self._json(200, result)
                    if mode == "normal":
                        if count == 2:
                            result = snapshot(1, True, True)
                        elif count == 3:
                            time.sleep(1.3)
                            result = snapshot(2, False, False)
                        elif count == 4:
                            result = snapshot(3, False, False)
                            result["backoff"] = 10
                        else:
                            result = snapshot(count, False, False)
                            result["backoff"] = 10
                        return self._json(200, result)
                    if mode == "serve":
                        result = snapshot(count, count == 1, count == 1)
                        result["backoff"] = 10
                        return self._json(200, result)
                    result = snapshot(count, count == 1, count == 1)
                    if mode not in {"late", "held_poll"}:
                        result["backoff"] = 10
                    return self._json(200, result)
                if path == "/v1/chat/fedcba9876543210/messages":
                    self._record_auth()
                    with server.lock:
                        mode = server.mode
                    if mode == "chat_muted":
                        return self._json(200, {"v": 1, "messages": [], "next": None, "last_id": 0, "read_id": 0})
                    if mode == "malformed_chat":
                        return self._json(200, {"v": 1, "messages": [{
                            "id": 2, "from": FRIEND_PID, "to": SELF_PID, "text": "Message",
                            "at": 1700000000, "client_id": "b" * 32,
                        }], "next": 1, "last_id": 3, "read_id": 0})
                    return self._json(200, {"v": 1, "messages": [{
                        "id": 1, "from": FRIEND_PID, "to": SELF_PID, "text": "Earlier message",
                        "at": 1700000000, "client_id": "a" * 32,
                    }], "next": None, "last_id": 1, "read_id": 0})
                self._json(404, {"v": 1, "error": "not_found", "message": "server-only-secret"})

            def do_POST(self) -> None:
                path = urlsplit(self.path).path
                body = self._body()
                if path == "/v1/auth/guest":
                    with server.lock:
                        server.auth_calls += 1
                        count = server.auth_calls
                        mode = server.mode
                        server.auth_installs.append(str(body.get("install", "")))
                    if mode == "signin_auth_429" and count == 1:
                        return self._json(429, {"v": 1, "error": "rate_limited", "retry_after": 1},
                                          {"Retry-After": "1"})
                    return self._json(200, {"v": 1, "token": SECRET,
                                            "account": {"id": "legacy-roster-id", "name": "Alice", "kind": "guest", "pid": SELF_PID}})
                if path == "/v1/friends/requests":
                    self._record_auth()
                    with server.lock:
                        server.friend_request_calls += 1
                        count = server.friend_request_calls
                        mode = server.mode
                    if mode == "action_cooldown" and count == 1:
                        return self._json(429, {"v": 1, "error": "rate_limited", "retry_after": 2},
                                          {"Retry-After": "65"})
                    return self._json(200, {"v": 1, "request": {
                        "id": "aaaaaaaaaaaaaaaa", "from": identity_player(SELF_PID, "Alice"),
                        "to": identity_player(FRIEND_PID, "Friend"), "at": 1700000000,
                    }})
                if path == "/v1/friends/requests/0123456789abcdef/accept":
                    self._record_auth()
                    return self._json(200, {"v": 1, "ok": True})
                if path == "/v1/friends/requests/aaaaaaaaaaaaaaaa/decline":
                    self._record_auth()
                    return self._json(200, {"v": 1, "ok": True})
                if path == "/v1/invites/fedcba9876543210/decline":
                    self._record_auth()
                    return self._json(200, {"v": 1, "ok": True})
                if path == "/v1/me/recovery":
                    self._record_auth()
                    # ONLINEMENU-1 (PR #8 review, finding 2): the lobby makes a code only for a request naming the session's install
                    if set(body) != {"v", "install"} or not re.fullmatch(r"[0-9a-f]{32}", str(body.get("install", ""))):
                        return self._json(403, {"v": 1, "error": "install_proof", "message": "no install"})
                    return self._json(200, {"v": 1, "code": RECOVERY_CODE})
                if path == "/v1/me/status":
                    self._record_auth()
                    with server.lock:
                        mode = server.mode
                    if mode == "normal":
                        return self._json(403, {"v": 1, "error": "banned", "message": "server-only-secret"})
                    return self._json(200, {"v": 1, "ok": True})
                if path == "/v1/chat/fedcba9876543210/messages":
                    self._record_auth()
                    with server.lock:
                        server.send_calls += 1
                        count = server.send_calls
                        mode = server.mode
                        server.send_client_ids.append(str(body.get("client_id", "")))
                    if mode == "chat_muted" and count == 1:
                        return self._json(403, {"v": 1, "error": "chat_muted"})
                    if mode == "chat_muted" and count == 2:
                        return self._json(200, {"v": 1, "message": {
                            "id": 1, "from": SELF_PID, "to": FRIEND_PID,
                            "text": body.get("text", "") + " mismatched", "at": 1700000001,
                            "client_id": body.get("client_id", ""),
                        }})
                    if count == 1:
                        return self._json(500, {"v": 1, "error": "unavailable", "message": "server-only-secret"})
                    return self._json(200, {"v": 1, "message": {
                        "id": 2 if mode == "normal" else 1, "from": SELF_PID, "to": FRIEND_PID, "text": body.get("text", ""),
                        "at": 1700000001, "client_id": body.get("client_id", ""),
                    }})
                if path == "/v1/chat/fedcba9876543210/read":
                    self._record_auth()
                    return self._json(200, {"v": 1, "read_id": int(body.get("through", 0))})
                self._record_auth()
                self._json(200, {"v": 1, "ok": True})

            def do_DELETE(self) -> None:
                self._record_auth()
                self._json(200, {"v": 1, "ok": True})

        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.origin = f"http://127.0.0.1:{self.httpd.server_address[1]}"
        class CaptureHandler(BaseHTTPRequestHandler):
            def log_message(self, *_args) -> None:
                return

            def do_GET(self) -> None:
                with server.lock:
                    server.redirect_capture_count += 1
                    server.redirect_capture_authorized = self.headers.get("Authorization", "") != ""
                body = b'{"v":1,"ok":true}'
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.capture_httpd = ThreadingHTTPServer(("127.0.0.1", 0), CaptureHandler)
        self.capture_origin = f"http://127.0.0.1:{self.capture_httpd.server_address[1]}"
        self.capture_thread = threading.Thread(target=self.capture_httpd.serve_forever, daemon=True)
        self.capture_thread.start()
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def reset(self, mode: str) -> None:
        with self.lock:
            self.mode = mode
            self.inbox_calls = 0
            self.inbox_times = []
            self.send_calls = 0
            self.auth_calls = 0
            self.me_calls = 0
            self.friends_calls = 0
            self.friend_request_calls = 0
            self.health_calls = 0

    def stop(self) -> None:
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join(timeout=3)
        self.capture_httpd.shutdown()
        self.capture_httpd.server_close()
        self.capture_thread.join(timeout=3)


def run_godot(godot: str, server: SocialServer, case: str, game_dir: Path) -> str:
    env = dict(os.environ)
    env["SOCIAL_TEST_CASE"] = case
    env["SOCIAL_TEST_ORIGIN"] = server.origin
    env["OPENBFME_GAME_USER_DIR"] = str(game_dir)
    try:
        result = subprocess.run(
            [godot, "--headless", "--path", str(LAUNCHER), "--script", TEST_SCRIPT],
            env=env, capture_output=True, text=True, timeout=40, stdin=subprocess.DEVNULL,
        )
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        error_output = error.stderr or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        if isinstance(error_output, bytes):
            error_output = error_output.decode("utf-8", errors="replace")
        output += error_output
        safe_lines = [line for line in output.splitlines()
                      if line.startswith(("SOCIAL", "SCRIPT ERROR", "Parse Error", "ERROR"))]
        with server.lock:
            counts = (f"auth={len(server.auth_installs)} protected={len(server.protected_authorized)} "
                      f"authorized={sum(server.protected_authorized)} inbox={server.inbox_calls} sends={server.send_calls}")
        raise RuntimeError(
            f"Godot social case {case} timed out ({counts}): {' | '.join(safe_lines[-20:])}"
        ) from None
    output = result.stdout + result.stderr
    if result.returncode != 0 or "SCRIPT ERROR" in output or "SOCIAL OK" not in output:
        safe_lines = [line for line in output.splitlines() if line.startswith(("SOCIAL", "SCRIPT ERROR", "Parse Error"))]
        raise RuntimeError(f"Godot social case {case} failed with exit {result.returncode}: {' | '.join(safe_lines[-20:])}")
    if SECRET in output or RECOVERY_CODE in output:
        raise RuntimeError(f"Godot social case {case} printed a credential or recovery code")
    print(f"Godot {case}: passed")
    return output


def run_import(godot: str) -> None:
    result = subprocess.run(
        [godot, "--headless", "--path", str(LAUNCHER), "--script", TEST_SCRIPT, "--check-only"],
        capture_output=True, text=True, timeout=30, stdin=subprocess.DEVNULL,
    )
    output = result.stdout + result.stderr
    if result.returncode != 0 or "SCRIPT ERROR" in output or "Parse Error" in output:
        safe_lines = [line for line in output.splitlines() if "ERROR" in line or "Parse Error" in line]
        raise RuntimeError(f"Godot headless project parse/import failed: {' | '.join(safe_lines[-20:])}")
    print("Godot headless social-script parse/import: passed")


def main() -> int:
    parser = argparse.ArgumentParser()
    default_godot = os.environ.get("GODOT") or shutil.which("godot") or str(
        Path.home() / ".cache/godot-4.7.2/Godot_v4.7.2-stable_linux.x86_64"
    )
    parser.add_argument("--godot", default=default_godot)
    parser.add_argument("--serve", action="store_true", help="serve the local synthetic lobby for UI work")
    args = parser.parse_args()
    if args.serve:
        server = SocialServer()
        server.reset("serve")
        print(f"Synthetic social lobby: {server.origin}", flush=True)
        try:
            while True:
                time.sleep(3600)
        except KeyboardInterrupt:
            pass
        finally:
            server.stop()
        return 0
    godot = shutil.which(args.godot) or args.godot
    try:
        version = subprocess.run([godot, "--version"], capture_output=True, text=True, timeout=15, check=True).stdout.strip()
    except (OSError, subprocess.SubprocessError) as error:
        print(f"Godot executable unavailable: {error}", file=sys.stderr)
        return 2
    if not version.startswith("4.7.2"):
        print(f"Expected Godot 4.7.2 for this focused launcher test; got {version}", file=sys.stderr)
        return 2
    try:
        run_import(godot)
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="launcher-social-") as temp:
        game_dir = Path(temp) / "game-user"
        game_dir.mkdir()
        server = SocialServer()
        try:
            server.reset("normal")
            run_godot(godot, server, "main", game_dir)
            normal_inbox_calls = server.inbox_calls
            normal_inbox_times = list(server.inbox_times)
            chat_client_ids = list(server.send_client_ids)

            server.reset("no_social")
            run_godot(godot, server, "no_social", game_dir)
            if server.auth_calls != 0 or server.me_calls != 0 or server.inbox_calls != 0 or server.health_calls != 1:
                raise RuntimeError("a lobby without the social service was signed in to or polled "
                                   f"(health={server.health_calls} auth={server.auth_calls} me={server.me_calls} inbox={server.inbox_calls})")

            server.reset("late")
            run_godot(godot, server, "late", game_dir)
            server.reset("redirect")
            run_godot(godot, server, "redirect", game_dir)
            server.reset("normal")
            run_godot(godot, server, "lock_retry", game_dir)
            server.reset("normal")
            run_godot(godot, server, "oauth_handoff", game_dir)
            if server.auth_calls != 0:
                raise RuntimeError("OAuth handoff allocated another guest identity")
            for case in ["signin_auth_429", "signin_me_503", "signin_friends_503",
                         "refresh_retry", "held_poll", "chat_muted", "action_cooldown", "malformed_friends",
                         "malformed_inbox", "malformed_chat", "large_friends"]:
                server.reset(case)
                run_godot(godot, server, case, game_dir)
                if case == "refresh_retry" and (len(server.inbox_times) < 2
                        or server.inbox_times[1] - server.inbox_times[0] < 9.5):
                    raise RuntimeError("Friends refresh erased the inbox backoff deadline")
                if case == "large_friends" and (not server.large_payload_sizes
                        or server.large_payload_sizes[-1] <= 65536):
                    raise RuntimeError("The large-friends fixture did not exceed 64 KiB")
                if case == "action_cooldown" and server.friend_request_calls != 1:
                    raise RuntimeError("The client automatically retried a rate-limited mutation")
                if case == "chat_muted" and (len(server.send_client_ids) < 3
                        or len(set(server.send_client_ids[-3:])) != 1):
                    raise RuntimeError("Chat retry changed its client_id before a matching acknowledgement")

            if len(server.auth_installs) < 4 or not server.auth_installs[0] \
                    or any(value != server.auth_installs[0] for value in server.auth_installs):
                raise RuntimeError("Game-owned install identity did not persist across launcher processes")
            if len(chat_client_ids) != 2 or chat_client_ids[0] != chat_client_ids[1]:
                raise RuntimeError("Chat retry did not preserve its client_id")
            if server.redirect_capture_count != 0:
                raise RuntimeError("Godot followed a redirect to the credential-capture endpoint")
            if server.redirect_capture_authorized:
                raise RuntimeError("A bearer credential reached the redirect target")
            if not server.protected_authorized or not all(server.protected_authorized):
                raise RuntimeError("A protected social request did not carry the configured bearer token")
            if normal_inbox_calls != 4 or len(normal_inbox_times) != 4:
                raise RuntimeError("The client did not honor sequential inbox polling/backoff")
            if normal_inbox_times[1] - normal_inbox_times[0] < 0.8:
                raise RuntimeError("The client did not honor Retry-After on HTTP 429")
            print("PASS: persistent identity, lock retry, real HTTPRequest actions, inbox replacement/backoff, recovery secrecy, 403, redirect refusal, chat replay, and sign-out epoch")
            return 0
        finally:
            server.stop()


if __name__ == "__main__":
    raise SystemExit(main())
