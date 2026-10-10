"""Lane LAUNCH-1: a local stand-in for GitHub Releases (the API's releases list with ETag / 304 and a 403 rate limit; the downloads
with Range resumes, a cut connection and redirects), for tools/release/test_launcher.py and launcher_windows_test.py."""
from __future__ import annotations

import hashlib
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TEST_REPO = "Test/repo"
COMMIT = "0123456789abcdef0123456789abcdef01234567"


class ReleaseServer:
    def __init__(self) -> None:
        self.releases: list[dict] = []
        self.files: dict[str, bytes] = {}
        self.rate_limited = False
        self.truncate: dict[str, int] = {}     # path -> bytes to send once before cutting the connection
        self.redirect: dict[str, str] = {}     # path -> Location
        self.ignore_range = False
        self.status: dict[str, int] = {}       # path -> an HTTP error status to answer (lane AIO-1)
        self.requests: list[tuple[str, dict]] = []
        self.stopped = False
        srv = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *a):  # quiet
                pass

            def do_GET(self):
                srv.requests.append((self.path, {k.lower(): v for k, v in self.headers.items()}))
                if self.path.startswith("/repos/"):
                    return self._api()
                if self.path in srv.redirect:
                    self.send_response(302)
                    self.send_header("Location", srv.redirect[self.path])
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                if self.path in srv.status:
                    self.send_error(srv.status[self.path])
                    return
                data = srv.files.get(self.path)
                if data is None:
                    self.send_error(404)
                    return
                start = 0
                rng = self.headers.get("Range", "")
                if rng.startswith("bytes=") and rng.endswith("-") and not srv.ignore_range:
                    start = int(rng[6:-1])
                    self.send_response(206)
                    self.send_header("Content-Range", f"bytes {start}-{len(data) - 1}/{len(data)}")
                else:
                    self.send_response(200)
                body = data[start:]
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                cut = srv.truncate.pop(self.path, None)
                if cut is not None:
                    self.wfile.write(body[:cut])
                    self.wfile.flush()
                    self.close_connection = True
                    return
                self.wfile.write(body)

            def _api(self):
                if srv.rate_limited:
                    body = b'{"message": "API rate limit exceeded"}'
                    self.send_response(403)
                    self.send_header("X-RateLimit-Remaining", "0")
                    self.send_header("X-RateLimit-Reset", str(int(time.time()) + 1800))
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                body = json.dumps(srv.releases).encode()
                etag = '"%s"' % hashlib.sha256(body).hexdigest()[:20]
                if self.headers.get("If-None-Match") == etag:
                    self.send_response(304)
                    self.send_header("ETag", etag)
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header("ETag", etag)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.origin = f"http://127.0.0.1:{self.httpd.server_address[1]}"
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def stop(self) -> None:
        if self.stopped:
            return
        self.stopped = True
        self.httpd.shutdown()
        self.httpd.server_close()

    def publish(self, tag: str, files: dict[str, bytes], prerelease: bool | None = None, notes: str = "") -> None:
        assets = []
        for name, data in files.items():
            path = f"/dl/{tag}/{name}"
            self.files[path] = data
            assets.append({"name": name, "size": len(data), "browser_download_url": self.origin + path})
        pre = ("-" in tag) if prerelease is None else prerelease
        self.releases = [r for r in self.releases if r["tag_name"] != tag]
        self.releases.insert(0, {"tag_name": tag, "draft": False, "prerelease": pre, "body": notes or f"Notes of {tag}",
                                 "published_at": "2026-10-09T00:00:00Z", "assets": assets})


def tiny_package(name: str, files: dict[str, bytes]) -> bytes:
    """a package archive (make_archive.py's format: one top folder named like the asset) holding `files`, for test releases whose
    manifest lists every file of every package (format 2)"""
    import tempfile
    from pathlib import Path

    import make_archive
    with tempfile.TemporaryDirectory() as t:
        top = Path(t) / name.removesuffix(".tar.gz").removesuffix(".zip")
        top.mkdir()
        for rel, data in files.items():
            (top / rel).parent.mkdir(parents=True, exist_ok=True)
            (top / rel).write_bytes(data)
            if rel.endswith((".x86_64", ".exe")):
                (top / rel).chmod(0o755)
        out = Path(t) / name
        (make_archive.zip_file if name.endswith(".zip") else make_archive.tar_gz)(top, out, 1700000000)
        return out.read_bytes()
