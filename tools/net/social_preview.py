#!/usr/bin/env python3
"""Local SOCIAL-1 visual fixture, NOT a lobby implementation. Synthetic data only.

Run with --port, then Godot --online-lobby=http://127.0.0.1:PORT
--online-walk=social --online-shots=OUTPUT. Never bind a public interface.
"""
import argparse
import copy
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

ME = {"pid": "1111111111111111", "nick": "Messenger tester", "code": "0123456789"}
FRIEND = {"pid": "2222222222222222", "nick": "Rohan Scout", "code": "123456789A"}
PEER = {"pid": "3333333333333333", "nick": "Gondor Guard", "code": "23456789AB"}
REQUESTER = {"pid": "4444444444444444", "nick": "Rivendell", "code": "3456789ABC"}
GAME = "5555555555555555"
EPOCH = "aaaaaaaaaaaaaaaa"
TOKEN = "local-social-fixture-not-a-real-credential"


class Fixture:
    def __init__(self):
        self.changed = threading.Condition()
        self.revision = 1
        self.privacy = {"invites": "friends_or_room", "challenges": "friends_or_room",
                        "requests": "anyone", "presence": "friends"}
        self.hidden = False
        self.friends = [FRIEND]
        self.requests = [{"id": "6666666666666666", "from": REQUESTER, "to": ME, "at": 1700000000}]
        self.invites = [{"id": "7777777777777777", "from": FRIEND, "to": ME, "game": GAME,
                         "kind": "challenge", "mode": "normal", "note": "A friendly 1v1?", "expires": 2000000000}]
        self.blocks = []
        self.messages = [{"id": 1, "from": FRIEND["pid"], "to": ME["pid"], "at": 1700000000,
                          "client_id": "a" * 32, "text": "Ready for a game when you are."},
                         {"id": 2, "from": ME["pid"], "to": FRIEND["pid"], "at": 1700000001,
                          "client_id": "b" * 32, "text": "Yes — normal mode, please."}]
        self.read_id = 0

    @staticmethod
    def stats():
        return {"v": 1, "players": 3, "games_open": 1, "games_running": 0, "as_of": int(time.time())}

    @staticmethod
    def presence(player):
        return {"pid": player["pid"], "state": "away", "game": None, "joinable": False,
                "since": 1700000000, "map": None, "mode": None}

    def me(self):
        return {"v": 1, **ME, "privacy": self.privacy, "hidden": self.hidden}

    def response(self, method, path, query, body):
        with self.changed:
            if method == "GET":
                if path == "/v1/stats":
                    return 200, self.stats()
                if path == "/v1/motd":
                    return 200, {"v": 1, "motd": "SOCIAL-1 local fake lobby — synthetic players, no deployment."}
                if path in ("/v1/games", "/v1/mine"):
                    return 200, {"v": 1, "games": [], "account": {"name": ME["nick"], "kind": "guest", "pid": ME["pid"]}}
                if path == "/v1/me":
                    return 200, self.me()
                if path == "/v1/friends":
                    return 200, {"v": 1, "friends": [{**p, "since": 1700000000, "presence": self.presence(p)} for p in self.friends]}
                if path == "/v1/inbox":
                    seq = f"{EPOCH}:{self.revision}"
                    if query.get("since", [None])[0] == seq:
                        self.changed.wait(timeout=min(25, int(query.get("wait", [0])[0])))
                    return 200, {"v": 1, "seq": f"{EPOCH}:{self.revision}", "reset": "since" not in query,
                                 "presence": [self.presence(p) for p in self.friends], "requests": self.requests,
                                 "invites": self.invites, "notices": [], "chat": [{"pid": FRIEND["pid"],
                                 "last_id": len(self.messages), "read_id": self.read_id}], "stats": self.stats(), "wait": 25}
                if path == "/v1/recent":
                    return 200, {"v": 1, "games": [{"game": GAME, "at": 1700000000,
                                                   "players": [{**PEER, "target": "guest:fixture-peer"},
                                                               {**FRIEND, "target": "guest:fixture-friend"}]}]}
                if path == "/v1/blocks":
                    return 200, {"v": 1, "blocks": self.blocks, "next": None}
                if path == "/v1/chat":
                    return 200, {"v": 1, "threads": [{**FRIEND, "last_id": len(self.messages), "read_id": self.read_id}], "next": None}
                if path == f"/v1/chat/{FRIEND['pid']}/messages":
                    after = int(query.get("after", [0])[0])
                    return 200, {"v": 1, "messages": [m for m in self.messages if m["id"] > after],
                                 "next": None, "last_id": len(self.messages), "read_id": self.read_id}
            if method == "POST" and path == "/v1/auth/guest":
                return 200, {"v": 1, "token": TOKEN, "account": {"id": "guest:fixture-me", "name": ME["nick"], "kind": "guest", "pid": ME["pid"]}}
            if method in ("POST", "PUT", "DELETE"):
                result = {"v": 1, "ok": True}
                if path == "/v1/me/privacy" and method == "PUT":
                    self.privacy = body["privacy"]
                    result = self.me()
                elif path == "/v1/me/status":
                    self.hidden = body.get("hidden", self.hidden)
                elif path == "/v1/reports":
                    return 201, {"v": 1, "report_id": "R-" + "a" * 24, "received_at": int(time.time())}
                elif path == f"/v1/chat/{FRIEND['pid']}/read":
                    self.read_id = min(body["through"], len(self.messages))
                    result = {"v": 1, "read_id": self.read_id}
                elif path == f"/v1/chat/{FRIEND['pid']}/messages":
                    existing = next((m for m in self.messages if m["client_id"] == body["client_id"]), None)
                    if existing is not None and existing["text"] != body["text"]:
                        return 409, {"v": 1, "error": "idempotency_conflict", "message": "The retry changed."}
                    message = existing or {"id": len(self.messages) + 1, "from": ME["pid"], "to": FRIEND["pid"],
                                           "at": int(time.time()), "client_id": body["client_id"], "text": body["text"]}
                    if existing is None:
                        self.messages.append(message)
                    result = {"v": 1, "message": message}
                elif path == "/v1/friends/requests/6666666666666666/accept":
                    self.requests.clear()
                    self.friends.append(REQUESTER)
                elif path == "/v1/friends/requests/6666666666666666/decline":
                    self.requests.clear()
                elif path == "/v1/invites/7777777777777777/decline":
                    self.invites.clear()
                else:
                    return 404, {"v": 1, "error": "unavailable", "message": "This operation is outside the local visual fixture."}
                self.revision += 1
                self.changed.notify_all()
                return 200, result
            return 404, {"v": 1, "error": "unavailable", "message": "No such local fixture route."}


class Handler(BaseHTTPRequestHandler):
    def handle_request(self):
        url = urlsplit(self.path)
        if url.path not in ("/v1/auth/guest", "/v1/stats", "/v1/motd") and self.headers.get("Authorization") != "Bearer " + TOKEN:
            status, result = 401, {"v": 1, "error": "signed_out", "message": "Sign in to this local fixture."}
        else:
            length = int(self.headers.get("Content-Length", 0))
            if length > 16384:
                self.send_error(413)
                return
            body = json.loads(self.rfile.read(length)) if length else {}
            with self.server.fixture.changed:
                status, result = self.server.fixture.response(self.command, url.path, parse_qs(url.query), body)
                result = copy.deepcopy(result)
        raw = json.dumps(result).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        try:
            self.wfile.write(raw)
        except (BrokenPipeError, ConnectionResetError):
            pass

    do_GET = do_POST = do_PUT = do_DELETE = handle_request

    def log_message(self, *_args):
        pass  # Never log request bodies or auth headers.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=18781)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.fixture = Fixture()
    print(f"Local SOCIAL fixture on 127.0.0.1:{server.server_port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
