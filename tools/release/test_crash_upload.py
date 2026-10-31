"""Lane CRASHACT-1: crash reports upload by themselves (godot/scripts/crash/crash_client.gd, its launcher copy, release.gd, launcher_crash.gd).

    python -m pytest -q tools/release/test_crash_upload.py

* the game's and the launcher's crash client are the same bytes; net_guard allows each exactly its one request to the crash service;
* the game's headless checks (godot/tests/crash_upload_test.gd --stage=local): privacy filtering, the queue's limits, the Options toggle, the
  previous run's crash with the toggle on and off, offline;
* end to end against a FAKE crash service on 127.0.0.1 (never the live one): it checks every request as the service does (route, headers,
  HMAC signature, canonical gzip, the report schema, the service's private-data rules) and answers 201 / 429 / 400 by plan;
* the launcher's checks (launcher/tests/crash_upload_test.gd), end to end too.
"""
from __future__ import annotations

import gzip
import hashlib
import hmac
import ipaddress
import json
import re
import subprocess
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import net_guard  # noqa: E402
from launcher_test_support import godot_engine, isolated_environment  # noqa: E402

GODOT = godot_engine(__import__("os").environ.get("GODOT", "godot"))
GAME_CLIENT = REPO / "godot/scripts/crash/crash_client.gd"
LAUNCHER_CLIENT = REPO / "launcher/scripts/net/crash_client.gd"
SCHEMA = json.loads((REPO / "services/crash/report.schema.json").read_text(encoding="utf-8"))
KEY = "t" * 32 + "0123456789abcdef0123456789abcdef"  # synthetic: 64 characters like the service's client key
EXTENSION = any((REPO / "godot/bin").glob("*openbfme*")) if (REPO / "godot/bin").is_dir() else False


def _godot_ok() -> bool:
    try:
        return subprocess.run([GODOT, "--version"], capture_output=True, text=True, timeout=60).stdout.startswith("4.7.2.")
    except (OSError, subprocess.TimeoutExpired):
        return False


godot_needed = pytest.mark.skipif(not _godot_ok(), reason="SKIP crash upload tests: Godot 4.7.2 not found (set GODOT)")

# the crash service's private-data rules (services/crash protocol.py _contains_private_marker), applied to every string of a report
_IPV4 = re.compile(r"(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])")
_IPV6 = re.compile(r"(?<![\w:])(?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}(?![\w:])")
_HOME = re.compile(r"(?i)(?:[A-Za-z]:\\Users\\[^\\\s]+|/home/[^/\s]+|/Users/[^/\s]+)")
_CREDENTIAL = re.compile(r"(?i)\b(?:access[_ -]?key|api[_ -]?key|credential|secret|token|ticket|password|authorization|bearer|"
                         r"user(?:name|[-_ ]name))\b\s*(?:[:=]|\s)")


def private_marker(value: str) -> bool:
    if _HOME.search(value) or _CREDENTIAL.search(value):
        return True
    for pattern, kind in ((_IPV4, ipaddress.IPv4Address), (_IPV6, ipaddress.IPv6Address)):
        for m in pattern.finditer(value):
            try:
                kind(m.group(0))
                return True
            except ValueError:
                continue
    return False


def schema_errors(value, schema, path="$") -> list[str]:
    """the subset of JSON Schema report.schema.json uses (type, const, enum, required, additionalProperties false, bounds, items, anyOf)"""
    if "anyOf" in schema:
        return [] if any(not schema_errors(value, s, path) for s in schema["anyOf"]) else [f"{path}: no anyOf branch matches {value!r}"]
    if "const" in schema and value != schema["const"]:
        return [f"{path}: {value!r} is not {schema['const']!r}"]
    if "enum" in schema and value not in schema["enum"]:
        return [f"{path}: {value!r} not in {schema['enum']}"]
    kind = schema.get("type")
    types = {"object": dict, "array": list, "string": str, "boolean": bool}
    if kind == "integer":
        if not isinstance(value, int) or isinstance(value, bool):
            return [f"{path}: not an integer"]
        if value < schema.get("minimum", value) or value > schema.get("maximum", value):
            return [f"{path}: {value} out of bounds"]
    elif kind and not isinstance(value, types[kind]):
        return [f"{path}: not {kind}"]
    out = []
    if kind == "string" and not schema.get("minLength", 0) <= len(value) <= schema.get("maxLength", len(value)):
        out.append(f"{path}: length {len(value)}")
    if kind == "array":
        if not schema.get("minItems", 0) <= len(value) <= schema.get("maxItems", len(value)):
            out.append(f"{path}: {len(value)} items")
        for i, item in enumerate(value):
            out += schema_errors(item, schema.get("items", {}), f"{path}[{i}]")
    if kind == "object":
        props = schema.get("properties", {})
        out += [f"{path}: missing {k}" for k in schema.get("required", []) if k not in value]
        if schema.get("additionalProperties") is False:
            out += [f"{path}: unknown field {k}" for k in value if k not in props]
        for k, v in value.items():
            if k in props:
                out += schema_errors(v, props[k], f"{path}.{k}")
    return out


def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, list):
        for v in value:
            yield from strings(v)
    elif isinstance(value, dict):
        for v in value.values():
            yield from strings(v)


class FakeCrashService(ThreadingHTTPServer):
    """POST /v1/reports checked as the crash service checks it; answers from `plan` (status codes, in order)"""

    def __init__(self, plan: list[int]):
        super().__init__(("127.0.0.1", 0), _Handler)
        self.plan = list(plan)
        self.requests: list[dict] = []
        self.problems: list[str] = []
        self.lock = threading.Lock()

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self.server_address[1]}/v1/reports"

    def __enter__(self):
        threading.Thread(target=self.serve_forever, daemon=True).start()
        return self

    def __exit__(self, *exc):
        self.shutdown()
        self.server_close()


class _Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _reply(self, status: int, body: dict):
        data = json.dumps(body, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self._reply(405, {"v": 1, "error": "crash.invalid_request"})

    def do_POST(self):
        server: FakeCrashService = self.server  # type: ignore[assignment]
        wire = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        problems = self._check(wire)
        with server.lock:
            server.problems += problems
            status = 400 if problems else (server.plan.pop(0) if server.plan else 500)
        if status == "hang":   # never answers (the client's own timeout ends the request)
            time.sleep(60)
            return
        if isinstance(status, tuple):   # ("delay", seconds, status): answers late
            time.sleep(status[1])
            status = status[2]
        if status == 201:
            self._reply(201, {"v": 1, "report_id": uuid.uuid4().hex[:24]})
        else:
            self._reply(status, {"v": 1, "error": {400: "crash.invalid_report", 429: "crash.rate_limited"}.get(status, "crash.unavailable")})

    def _check(self, wire: bytes) -> list[str]:
        h = {k.lower(): v for k, v in self.headers.items()}
        p = []
        if self.path != "/v1/reports":
            p.append(f"path {self.path}")
        if h.get("content-type", "").lower() != "application/octet-stream" or h.get("content-encoding", "").lower() != "gzip":
            p.append(f"content headers {h.get('content-type')} / {h.get('content-encoding')}")
        if len(wire) > 262_144:
            p.append(f"{len(wire)} bytes on the wire")
        try:
            install, nonce = uuid.UUID(h["x-crash-install-id"]), uuid.UUID(h["x-crash-nonce"])
            assert install.version == 4 and nonce.version == 4 and str(install) == h["x-crash-install-id"] and str(nonce) == h["x-crash-nonce"]
            stamp = h["x-crash-time"]
            assert re.fullmatch(r"0|[1-9][0-9]*", stamp) and abs(time.time() - int(stamp)) <= 300
            canonical = f"crash-v1\nPOST\n/v1/reports\n{stamp}\n{install}\n{nonce}\n{hashlib.sha256(wire).hexdigest()}"
            if not hmac.compare_digest(hmac.new(KEY.encode(), canonical.encode(), hashlib.sha256).hexdigest(), h["x-crash-signature"]):
                p.append("signature")
        except (KeyError, ValueError, AssertionError) as e:
            p.append(f"auth headers: {e!r}")
        if wire[:4] != b"\x1f\x8b\x08\x00" or wire[4:8] != b"\0\0\0\0":
            p.append("gzip header (flags and mtime must be 0)")
        try:
            report = json.loads(gzip.decompress(wire).decode("utf-8"), object_pairs_hook=_no_duplicates)
        except Exception as e:  # noqa: BLE001 - any decode failure is the finding
            return p + [f"body: {e!r}"]
        p += schema_errors(report, SCHEMA)
        if report.get("client", {}).get("version") != report.get("version"):
            p.append("client.version != version")
        p += [f"private-looking text: {s!r}" for s in strings(report) if private_marker(s)]
        p += [f"the install id in the report" for s in strings(report) if h.get("x-crash-install-id", "-") in s]
        self.server.requests.append({"report": report, "headers": h})  # type: ignore[attr-defined]
        return p


def _no_duplicates(pairs):
    keys = [k for k, _ in pairs]
    if len(keys) != len(set(keys)):
        raise ValueError("duplicate key")
    return dict(pairs)


def _env(tmp_path: Path, **extra: str) -> dict:
    """the isolated environment of a run; OPENBFME_CRASH_UPLOAD (0 on every CI host) only when the test sets it"""
    env = isolated_environment(tmp_path, **extra)
    if "OPENBFME_CRASH_UPLOAD" not in extra:
        env.pop("OPENBFME_CRASH_UPLOAD", None)
    return env


def _run(project: str, script: str, env: dict, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run([GODOT, "--headless", "--path", str(REPO / project), "--script", f"res://tests/{script}", "--", *args],
                          env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300)


def _ok(r: subprocess.CompletedProcess, summary: str) -> str:
    out = r.stdout + r.stderr
    assert "SCRIPT ERROR" not in out and "Parse Error" not in out, out[-6000:]
    assert r.returncode == 0 and summary in r.stdout, out[-6000:]
    return out


def test_clients_identical_and_guarded():
    assert GAME_CLIENT.read_bytes() == LAUNCHER_CLIENT.read_bytes(), "copy godot/scripts/crash/crash_client.gd to launcher/scripts/net/"
    for path in net_guard.CRASH_CLIENT_FILES:
        assert net_guard.crash_client_problems((REPO / path).read_text(encoding="utf-8")) == [], path
    assert net_guard.ALLOWED_GAME["godot/scripts/crash/crash_client.gd"] == {"HTTPRequest", "HTTPClient"}
    assert net_guard.ALLOWED_LAUNCHER["launcher/scripts/net/crash_client.gd"] == {"HTTPRequest", "HTTPClient"}
    # the rule bites: a second request, another endpoint, or redirects on are findings
    text = GAME_CLIENT.read_text(encoding="utf-8")
    assert net_guard.crash_client_problems(text + "\nfunc x():\n\t_http.request(\"https://x.example/\")\n")
    assert net_guard.crash_client_problems(text.replace("https://api.openbfme.com/v1/reports", "https://x.example/v1/reports"))
    assert net_guard.crash_client_problems(text.replace("max_redirects = 0", "max_redirects = 8"))
    assert net_guard.main() == 0


def test_fake_service_checks_bite():
    assert private_marker("C:\\Users\\Bob\\x") and private_marker("from 203.0.113.20") and private_marker("token = 5") \
        and not private_marker("15:30:12 Object::kill [redacted-ip]")
    assert schema_errors({"schema_version": 1}, SCHEMA)


@godot_needed
def test_game_local(tmp_path):
    out = _ok(_run("godot", "crash_upload_test.gd", _env(tmp_path), "--stage=local",
                   f"--options-file={tmp_path / 'Options.ini'}"), "CRASHUP OK")
    if EXTENSION:
        assert "CRASHUP redaction through ReleaseInfo.redact" in out and "CRASHUP the Options toggle checks ran" in out, out[-4000:]
    print(out[-2000:])


@godot_needed
def test_game_upload_end_to_end(tmp_path):
    with FakeCrashService([201, 201, 429, 400, 201, ("delay", 4, 201), "hang"]) as service:
        env = _env(tmp_path, OPENBFME_CRASH_TEST_ENDPOINT=service.url, OPENBFME_CRASH_TEST_KEY=KEY)
        out = _ok(_run("godot", "crash_upload_test.gd", env, "--stage=upload"), "CRASHUP OK")
    assert service.problems == [], service.problems
    assert len(service.requests) == 7 and not service.plan, (len(service.requests), service.plan, out[-3000:])
    installs = {r["headers"]["x-crash-install-id"] for r in service.requests}
    assert len(installs) == 1, "one install id, kept"
    report = service.requests[0]["report"]
    assert report["log_tail"][0].startswith("OpenBFME crash report") and report["backtrace"][0].startswith("ERROR:")


@godot_needed
def test_launcher_local_and_upload(tmp_path):
    with FakeCrashService([201]) as service:
        env = _env(tmp_path, OPENBFME_GAME_USER_DIR=str(tmp_path / "game"), OPENBFME_CRASH_TEST_ENDPOINT=service.url,
                                   OPENBFME_CRASH_TEST_KEY=KEY)
        out = _ok(_run("launcher", "crash_upload_test.gd", env, "--stage=upload"), "CRASHUP LAUNCHER OK")
    assert service.problems == [], service.problems
    assert len(service.requests) == 1 and service.requests[0]["report"]["log_tail"][0].startswith("OpenBFME crash report: the launcher"), out[-3000:]


def _plant_crash(data_home: Path) -> None:
    """a run that was killed (a dead process id, a player's run per its marker) left its log and marker in the game's user folder"""
    logs = data_home / "godot/app_userdata/OpenBFME/logs"
    logs.mkdir(parents=True, exist_ok=True)
    dead = subprocess.Popen(["true"])
    dead.wait()
    (logs / "openbfme-20261027-120000.log").write_text("GAME smoke run\nERROR: killed by the CI host\n", encoding="utf-8")
    (logs / "openbfme-20261027-120000.log.running").write_text(json.dumps({"pid": dead.pid, "eligible": True, "version": "v0.4.12"}))


@godot_needed
@pytest.mark.skipif(not EXTENSION, reason="SKIP: Release finds a crashed run through the openbfme extension (ReleaseInfo); build it first")
def test_a_killed_ci_smoke_run_sends_nothing(tmp_path):
    """CRASHACT-1: OPENBFME_CRASH_UPLOAD=0 (every CI host's env.sh): the next start finds the killed run's crash, queues it and sends
    nothing, even with a service it could reach (the fake one here)"""
    with FakeCrashService([201] * 5) as service:
        env = _env(tmp_path, OPENBFME_CRASH_UPLOAD="0", OPENBFME_CRASH_TEST_ENDPOINT=service.url, OPENBFME_CRASH_TEST_KEY=KEY)
        _plant_crash(Path(env["XDG_DATA_HOME"]))
        out = _ok(_run("godot", "crash_upload_test.gd", env, "--stage=ci"), "CRASHUP OK")
    assert service.requests == [] and service.problems == [], (service.requests, out[-3000:])
    assert "crash.upload_off" in out, out[-3000:]


def test_every_ci_host_env_switches_uploads_off():
    """the CI hosts' env.sh sources live outside the repository and are checked there; the repository's own
    package smoke test sets it too"""
    assert "export OPENBFME_CRASH_UPLOAD=0" in (REPO / "tools/release/test_export.sh").read_text(encoding="utf-8")


def test_crash_key_writes_only_a_valid_key_into_a_fresh_stage(tmp_path, monkeypatch):
    import crash_key
    project = tmp_path / "p"
    project.mkdir()
    (project / "project.godot").write_text("config_version=5\n")
    monkeypatch.delenv("CRASH_CLIENT_KEY", raising=False)
    assert "no CRASH_CLIENT_KEY" in crash_key.write(project) and not (project / crash_key.FILE).exists()
    with pytest.raises(SystemExit, match="64 letters"):
        crash_key.write(project, "short")
    with pytest.raises(SystemExit, match="64 letters"):
        crash_key.write(project, "x" * 63 + "!")
    line = crash_key.write(project, KEY)
    assert KEY not in line, "the key is never printed"
    assert f'const KEY := "{KEY}"' in (project / crash_key.FILE).read_text()
    with pytest.raises(SystemExit, match="already exists"):
        crash_key.write(project, KEY)
    with pytest.raises(SystemExit, match="not a Godot project"):
        crash_key.write(tmp_path, KEY)
    ignored = subprocess.run(["git", "-C", str(REPO), "check-ignore", "godot/scripts/crash_client_key.gd", "launcher/scripts/crash_client_key.gd"],
                             capture_output=True, text=True)
    assert ignored.stdout.split() == ["godot/scripts/crash_client_key.gd", "launcher/scripts/crash_client_key.gd"], "the key scripts are git-ignored"
    tracked = subprocess.run(["git", "-C", str(REPO), "ls-files", "--", "godot/scripts/crash_client_key.gd", "launcher/scripts/crash_client_key.gd"],
                             capture_output=True, text=True).stdout
    assert tracked == "", "a key script is committed"


PROBE = '''extends SceneTree
const CrashClient := preload("res://scripts/crash/crash_client.gd")
func _initialize() -> void:
	var t := CrashClient.target()
	print("TARGET ok=%s url=%s keylen=%d error=%s" % [t.ok, t.url, str(t.key).length(), t.error])
	quit()
'''


@godot_needed
@pytest.mark.parametrize("case", ["key", "none", "bad"])
def test_the_client_signs_with_the_package_key_only(tmp_path, case, monkeypatch):
    """a staged project with crash_key.py's script signs for the service; without it nothing is sent (no fallback); a bad one is named"""
    import crash_key
    project = tmp_path / "stage"
    project.mkdir()
    (project / "project.godot").write_text('config_version=5\n\n[application]\n\nconfig/name="crashkey"\n')
    # the client's place in the game (it preloads "../http_timed.gd", lane ONLINEMENU-1)
    (project / "scripts/crash").mkdir(parents=True)
    (project / "scripts/crash/crash_client.gd").write_bytes(GAME_CLIENT.read_bytes())
    (project / "scripts/http_timed.gd").write_bytes((REPO / "godot/scripts/http_timed.gd").read_bytes())
    (project / "probe.gd").write_text(PROBE)
    if case == "key":
        crash_key.write(project, KEY)
    elif case == "bad":
        (project / crash_key.FILE).parent.mkdir(parents=True, exist_ok=True)
        (project / crash_key.FILE).write_text('extends RefCounted\nconst KEY := "short"\n')
    env = _env(tmp_path)
    for name in ("OPENBFME_CRASH_TEST_ENDPOINT", "OPENBFME_CRASH_TEST_KEY"):
        env.pop(name, None)
    r = subprocess.run([GODOT, "--headless", "--path", str(project), "--script", "res://probe.gd"], env=env, capture_output=True,
                       text=True, timeout=120)
    out = r.stdout + r.stderr
    want = {"key": "TARGET ok=true url=https://api.openbfme.com/v1/reports keylen=64 error=",
            "none": "TARGET ok=false url= keylen=0 error=crash.no_client_key",
            "bad": "TARGET ok=false url= keylen=0 error=crash.client_key_invalid"}[case]
    assert want in out, out[-3000:]
    assert KEY not in out, "the key is never printed"


def test_the_workflow_lint_allows_the_crash_key_only_where_pinned():
    """CRASHACT-1: the crash key's exception in tools/release/public_workflows.py takes exactly its lines; every variation is refused"""
    import public_workflows as w
    rel_path, pkg_path = ".github/workflows/release.yml", ".github/workflows/release-package.yml"
    rel = (REPO / rel_path).read_text(encoding="utf-8")
    pkg = (REPO / pkg_path).read_text(encoding="utf-8")
    pr = (REPO / ".github/workflows/pr.yml").read_text(encoding="utf-8")
    assert w.workflow_problems(rel_path, rel) == [] and w.workflow_problems(pkg_path, pkg) == []
    line = "          CRASH_CLIENT_KEY: ${{ secrets.CRASH_CLIENT_KEY }}"
    call = "    secrets:\n      CRASH_CLIENT_KEY: ${{ secrets.CRASH_CLIENT_KEY }}\n"
    linux_call = "      platform: linux\n      commit: ${{ inputs.commit }}\n      version: ${{ inputs.tag }}\n      request: ${{ inputs.request }}\n"
    assert linux_call in rel
    bad_release = {
        "the key passed to the Linux call too": rel.replace(linux_call, linux_call + call),
        "secrets: inherit on the macOS call": rel.replace(call, "    secrets: inherit\n"),
        "another secret on the macOS call": rel.replace(call, call + "      OTHER: ${{ secrets.OTHER }}\n"),
        "another variable name": rel.replace(line, "          CRASH_KEY: ${{ secrets.CRASH_CLIENT_KEY }}"),
        "another secret in the package step": rel.replace(line, "          CRASH_CLIENT_KEY: ${{ secrets.OPENBFME_STABLE_SIGNING_KEY }}"),
        "the key at job level": rel.replace("  package:\n", "  package:\n    env:\n      CRASH_CLIENT_KEY: ${{ secrets.CRASH_CLIENT_KEY }}\n", 1),
        "the key in a second step": rel.replace("      - name: Build the packages (package.sh)\n",
                                                "      - name: Peek\n        env:\n" + line + "\n        run: env\n"
                                                "      - name: Build the packages (package.sh)\n", 1),
        "a second step of the same name": rel.replace("      - name: Build the packages (package.sh)\n",
                                                      "      - name: Build the packages (package.sh)\n        env:\n" + line + "\n        run: env\n"
                                                      "      - name: Build the packages (package.sh)\n", 1),
    }
    for what, text in bad_release.items():
        assert text != rel, what
        assert w.workflow_problems(rel_path, text), f"the lint passed: {what}"
    bad_pkg = {
        "another declared secret": pkg.replace("      CRASH_CLIENT_KEY:\n        required: false\n",
                                               "      CRASH_CLIENT_KEY:\n        required: false\n      OTHER:\n        required: false\n"),
        "the key in the Linux job": pkg.replace("      - name: Build (build_linux.sh)\n",
                                                "      - name: Build (build_linux.sh)\n        env:\n" + line + "\n", 1),
    }
    # Sol r3: an anchored env reused through a YAML alias by another job's step (the secret's text line still appears once)
    step = "      - name: Build the package (package_macos.py)\n        env:\n"
    assert step in pkg
    bad_pkg["an anchored env reused by an alias"] = pkg.replace(step, step.replace("env:", "env: &crashenv"), 1).replace(
        "      - name: Build (build_linux.sh)\n", "      - name: Build (build_linux.sh)\n        env: *crashenv\n", 1)
    for what, text in bad_pkg.items():
        assert text != pkg, what
        assert w.workflow_problems(pkg_path, text), f"the lint passed: {what}"
    # a pull request's workflow gets no exception at all
    leaked = pr.replace("jobs:\n", "jobs:\n  steal:\n    runs-on: ubuntu-24.04\n    steps:\n      - name: Build the packages (package.sh)\n"
                                   "        env:\n" + line + "\n        run: env\n", 1)
    assert leaked != pr and w.workflow_problems(".github/workflows/pr.yml", leaked)


def test_the_key_script_is_allowed_in_both_packs():
    """Sol r3: the generated script must pass the package audit's pack allowlists (game and launcher), or a keyed release fails there"""
    import audit_package
    import crash_key
    for rules in (audit_package.PCK_RULES, audit_package.LAUNCHER_PCK_RULES):
        assert [kind for rx, kind in rules if rx.match(crash_key.FILE)] == ["text"], crash_key.FILE
