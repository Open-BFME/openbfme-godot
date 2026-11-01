"""Native Windows regression coverage for the launcher's TCP instance lock."""
from __future__ import annotations

import contextlib
from datetime import datetime, timezone
import hashlib
import json
import os
import queue
import re
import shutil
import socket
import subprocess
import threading
from pathlib import Path
from uuid import uuid4

import pytest

REPO = Path(__file__).resolve().parents[2]
LAUNCHER = REPO / "launcher"
SCRIPT = "res://tests/instance_lock_test.gd"
PARSER_SCRIPT = "res://tests/instance_lock_parser_test.gd"
FIRST_PORT = 17000
PORTS = 15000
JSON_PREFIX = "INSTANCE_LOCK_JSON "
DEFAULT_GODOT = Path.home() / "Godot-4.7.2" / "Godot_v4.7.2-stable_win64_console.exe"
_ACTIVE_PROCESSES = set()


def _normal_path(path: Path | str) -> str:
    return os.path.normcase(os.path.normpath(str(path)))


def _godot_path(path: Path | str) -> str:
    return str(path).replace("\\", "/").rstrip("/")


def _port_for_path(path: Path | str) -> int:
    digest = hashlib.md5(_godot_path(path).encode("utf-8")).hexdigest()
    return FIRST_PORT + int(digest[:8], 16) % PORTS


def _parse_netsh(text: str) -> list[list[int]]:
    separator = re.compile(r"^\s*-+\s+-+\s*$")
    row = re.compile(r"^\s*(\d+)\s+(\d+)(?:\s+\*)?\s*$")
    table = False
    footer = False
    ranges: list[list[int]] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not table:
            table = separator.fullmatch(line) is not None
            continue
        if not line:
            continue
        if not footer and line.startswith("*"):
            footer = True
            continue
        matched = row.fullmatch(line)
        if footer or matched is None:
            raise AssertionError(f"unrecognized netsh row: {line!r}")
        first, last = map(int, matched.groups())
        if first < 1 or last > 65535 or first > last:
            raise AssertionError(f"invalid netsh range: {line!r}")
        ranges.append([first, last])
    if not table:
        raise AssertionError("netsh returned no excluded-port table separator")
    return ranges


def _excluded(port: int, ranges: list[list[int]]) -> bool:
    return any(first <= port <= last for first, last in ranges)


def _fallback_port(first: int, ranges: list[list[int]]) -> int:
    for offset in range(PORTS):
        candidate = FIRST_PORT + (first - FIRST_PORT + offset) % PORTS
        if not _excluded(candidate, ranges):
            return candidate
    raise AssertionError("the observed excluded ranges cover the full launcher lock range")


def _record(stdout: str) -> dict:
    for line in stdout.splitlines():
        if line.startswith(JSON_PREFIX):
            return json.loads(line[len(JSON_PREFIX):])
    raise AssertionError(f"no {JSON_PREFIX.strip()} record in stdout:\n{stdout}")


def _save_process(evidence: Path, name: str, command: list[str], returncode: int,
                  stdout: str, stderr: str) -> None:
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / f"{name}.stdout.txt").write_text(stdout, encoding="utf-8")
    (evidence / f"{name}.stderr.txt").write_text(stderr, encoding="utf-8")
    (evidence / f"{name}.json").write_text(json.dumps({
        "command": command,
        "returncode": returncode,
        "stdout": stdout,
        "stderr": stderr,
    }, indent=2) + "\n", encoding="utf-8")


def _godot_command(executable: Path, project: Path, args: list[str], script: str = SCRIPT) -> list[str]:
    return [str(executable), "--headless", "--path", str(project), "--script", script, "--", *args]


def _run_script(executable: Path, project: Path, args: list[str], env: dict[str, str],
                evidence: Path, name: str, timeout: int = 45,
                script: str = SCRIPT) -> tuple[subprocess.CompletedProcess[str], dict]:
    command = _godot_command(executable, project, args, script)
    result = subprocess.run(command, cwd=REPO, env=env, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=timeout)
    _save_process(evidence, name, command, result.returncode, result.stdout, result.stderr)
    return result, _record(result.stdout)


class GodotProcess:
    def __init__(self, executable: Path, project: Path, args: list[str], env: dict[str, str],
                 evidence: Path, name: str):
        self.command = _godot_command(executable, project, args)
        self.evidence = evidence
        self.name = name
        self.process = subprocess.Popen(
            self.command, cwd=REPO, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", errors="replace", bufsize=1,
        )
        self.stdout: list[str] = []
        self.stderr: list[str] = []
        self.records: queue.Queue[dict] = queue.Queue()
        self._threads = [
            threading.Thread(target=self._read, args=(self.process.stdout, self.stdout, True), daemon=True),
            threading.Thread(target=self._read, args=(self.process.stderr, self.stderr, False), daemon=True),
        ]
        for thread in self._threads:
            thread.start()
        self._saved = False
        _ACTIVE_PROCESSES.add(self)

    def _read(self, stream, lines: list[str], parse_records: bool) -> None:
        assert stream is not None
        for line in stream:
            lines.append(line)
            if parse_records and line.startswith(JSON_PREFIX):
                self.records.put(json.loads(line[len(JSON_PREFIX):]))

    def next_record(self, timeout: float) -> dict:
        try:
            return self.records.get(timeout=timeout)
        except queue.Empty as exc:
            raise AssertionError(f"{self.name} did not report within {timeout}s; {self.output()}") from exc

    def output(self) -> str:
        return "".join(self.stdout) + "\n--- stderr ---\n" + "".join(self.stderr)

    def finish(self, timeout: float = 30) -> int:
        code = self.process.wait(timeout=timeout)
        for thread in self._threads:
            thread.join(timeout=5)
            if thread.is_alive():
                raise AssertionError(f"{self.name} output reader did not finish after process exit")
        self._save(code)
        _ACTIVE_PROCESSES.discard(self)
        return code

    def kill(self) -> int:
        if self.process.poll() is None:
            self.process.kill()
        return self.finish()

    def _cleanup(self) -> None:
        try:
            if self.process.poll() is None:
                self.process.kill()
            try:
                code = self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                code = self.process.wait(timeout=10)
            for thread in self._threads:
                thread.join(timeout=5)
            self._save(code)
            if any(thread.is_alive() for thread in self._threads):
                raise AssertionError(f"{self.name} output reader did not finish after cleanup")
        finally:
            _ACTIVE_PROCESSES.discard(self)

    def _save(self, code: int) -> None:
        if not self._saved:
            _save_process(self.evidence, self.name, self.command, code,
                          "".join(self.stdout), "".join(self.stderr))
            self._saved = True


def _is_open(port: int) -> bool:
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        probe.bind(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        probe.close()


def _find_data_dir(parent: Path, excluded: list[list[int]]) -> tuple[Path, int]:
    parent.mkdir(parents=True, exist_ok=True)
    for index in range(250000):
        data_dir = parent / f"data-{index:06d}"
        port = _port_for_path(data_dir)
        if _excluded(port, excluded) or not _is_open(port):
            continue
        return data_dir.resolve(), port
    raise AssertionError("could not find an unexcluded, unused test lock port")


def _isolated_environment(evidence: Path, appdata: Path, user_data: Path) -> dict[str, str]:
    local = evidence / "local-appdata"
    appdata.mkdir(parents=True, exist_ok=True)
    local.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["APPDATA"] = str(appdata)
    env["LOCALAPPDATA"] = str(local)
    env["OPENBFME_EXPECTED_USER_DATA"] = str(user_data)
    return env


def _netsh_ranges(evidence: Path) -> tuple[list[list[int]], str]:
    system_root = Path(os.environ["SystemRoot"])
    executable = system_root / "System32" / "netsh.exe"
    command = [str(executable), "int", "ipv4", "show", "excludedportrange", "protocol=tcp"]
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
    (evidence / "netsh-excluded-ports.json").write_text(json.dumps({
        "command": command, "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr
    }, indent=2) + "\n", encoding="utf-8")
    if result.returncode != 0:
        raise AssertionError(f"netsh exited {result.returncode}: {result.stdout}\n{result.stderr}")
    text = result.stdout + result.stderr
    return _parse_netsh(text), text


def _profile_name() -> str:
    match = re.search(r'(?m)^config/custom_user_dir_name="([^"]+)"\s*$', (LAUNCHER / "project.godot").read_text())
    if match is None:
        raise AssertionError("launcher project has no custom user directory name")
    return match.group(1)


def _find_excluded_profile(evidence: Path, ranges: list[list[int]], executable: Path) -> tuple[Path, Path, dict[str, str], dict]:
    profile_name = _profile_name()
    profile_root = evidence / "isolated-appdata"
    profile_root.mkdir(parents=True, exist_ok=True)
    for index in range(250000):
        appdata = profile_root / f"profile-{index:06d}"
        user_data = appdata / profile_name
        expected_port = _port_for_path(user_data)
        if not _excluded(expected_port, ranges):
            continue
        env = _isolated_environment(evidence, appdata, user_data)
        result, record = _run_script(executable, LAUNCHER, ["--mode=profile"], env,
                                     evidence / "profile-probes", f"profile-probe-{index:06d}")
        if result.returncode != 0:
            raise AssertionError(f"profile probe exited {result.returncode}:\n{result.stdout}\n{result.stderr}")
        if not record["expected_match"] or _normal_path(record["user_data_dir"]) != _normal_path(user_data):
            raise AssertionError(
                f"Godot did not honor isolated APPDATA; actual={record['user_data_dir']!r}, expected={str(user_data)!r}"
            )
        if int(record["base_port"]) != expected_port:
            raise AssertionError(f"Python/Godot MD5 port mismatch: {expected_port} vs {record['base_port']}")
        return appdata, user_data, env, record
    raise AssertionError("could not find an isolated APPDATA profile hashing into the observed exclusions")


def _baseline_project(evidence: Path, baseline_ref: str) -> Path:
    target = evidence / "baseline-project"
    shutil.copytree(LAUNCHER, target, ignore=shutil.ignore_patterns(".godot"))
    old = subprocess.run(
        ["git", "show", f"{baseline_ref}:launcher/scripts/core/instance_lock.gd"],
        cwd=REPO, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30,
    )
    if old.returncode != 0:
        raise AssertionError(f"could not read baseline {baseline_ref}: {old.stderr}")
    (target / "scripts" / "core" / "instance_lock.gd").write_text(old.stdout, encoding="utf-8")
    return target


def _start_listener(port: int, *, http: bool = False) -> tuple[socket.socket, threading.Thread | None]:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 0)
    listener.bind(("127.0.0.1", port))
    listener.listen(4)
    thread = None
    if http:
        def respond() -> None:
            try:
                connection, _ = listener.accept()
                with connection:
                    connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n")
            except OSError:
                pass
        thread = threading.Thread(target=respond, daemon=True)
        thread.start()
    return listener, thread


@contextlib.contextmanager
def _cleanup_active_processes():
    try:
        yield
    finally:
        failures = []
        for process in tuple(_ACTIVE_PROCESSES):
            try:
                process._cleanup()
            except (OSError, subprocess.TimeoutExpired, AssertionError) as exc:
                failures.append(f"{process.name}: {exc}")
        if failures:
            raise AssertionError("could not clean up native Godot processes: " + "; ".join(failures))


def _run_native_suite(evidence: Path, executable: Path) -> dict:
    with _cleanup_active_processes():
        return _run_native_suite_inner(evidence, executable)


def _run_native_suite_inner(evidence: Path, executable: Path) -> dict:
    evidence.mkdir(parents=True, exist_ok=True)
    baseline_ref = os.environ.get("OPENBFME_INSTANCE_LOCK_BASELINE", "").strip()
    print(f"INSTANCE_LOCK_EVIDENCE {evidence}")
    (evidence / "run-info.json").write_text(json.dumps({
        "godot": str(executable),
        "baseline": baseline_ref or {"status": "skipped", "reason": "OPENBFME_INSTANCE_LOCK_BASELINE not set"},
    }, indent=2) + "\n", encoding="utf-8")
    ranges, netsh_text = _netsh_ranges(evidence)
    if not any(first <= FIRST_PORT + PORTS - 1 and last >= FIRST_PORT for first, last in ranges):
        # FLAKEFIX-2: the lock range left the dynamic range, where Windows' own exclusions usually lie
        pytest.skip(f"no Windows-excluded TCP port in the launcher lock range {FIRST_PORT}..{FIRST_PORT + PORTS - 1} on this machine")
    appdata, user_data, env, profile = _find_excluded_profile(evidence, ranges, executable)
    first = int(profile["base_port"])
    if not _excluded(first, ranges):
        raise AssertionError(f"selected port {first} is not in the fresh netsh table")
    raw = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        raw.bind(("127.0.0.1", first))
    except OSError as exc:
        raw_bind = {"failed": True, "errno": exc.errno, "winerror": getattr(exc, "winerror", None), "error": str(exc)}
    else:
        raw.close()
        raise AssertionError(f"raw bind unexpectedly succeeded on netsh-excluded TCP port {first}")
    finally:
        raw.close()
    (evidence / "profile.json").write_text(json.dumps({
        "appdata": str(appdata), "user_data_dir": str(user_data), "base_port": first,
        "excluded_ranges": ranges, "raw_bind": raw_bind, "godot_version": profile["godot_version"],
        "netsh_output": netsh_text,
    }, indent=2) + "\n", encoding="utf-8")

    parser, parser_record = _run_script(
        executable, LAUNCHER, [], env, evidence, "parser-tests", script=PARSER_SCRIPT,
    )
    assert parser.returncode == 0 and parser_record["checks"] == 8 and not parser_record["failures"], parser.stdout + parser.stderr

    baseline_summary = {
        "status": "skipped",
        "reason": "OPENBFME_INSTANCE_LOCK_BASELINE not set",
    }
    if baseline_ref:
        baseline = _baseline_project(evidence, baseline_ref)
        before, before_record = _run_script(
            executable, baseline, ["--mode=try", f"--version=baseline-{baseline_ref}"], env,
            evidence, "baseline-excluded-port",
        )
        expected_before_error = f"local port {first} is in use by another program (it did not answer as a launcher)"
        assert before.returncode == 0 and not before_record["acquired"], before.stdout + before.stderr
        assert before_record["port"] == first and before_record["error"] == expected_before_error, before.stdout + before.stderr
        assert _normal_path(before_record["user_data_dir"]) == _normal_path(user_data), before_record
        assert before_record["godot_version"] == profile["godot_version"], before_record
        baseline_summary = {
            "status": "verified",
            "ref": baseline_ref,
            "acquired": before_record["acquired"],
            "port": before_record["port"],
            "error": before_record["error"],
            "godot_version": before_record["godot_version"],
        }

    expected_fallback = _fallback_port(first, ranges)
    holder_version = "native-lock-test-v1"
    holder = GodotProcess(executable, LAUNCHER, ["--mode=hold", f"--version={holder_version}"], env,
                          evidence, "after-holder")
    holder_record = holder.next_record(45)
    assert holder_record["acquired"] and holder_record["port"] == expected_fallback, holder.output()
    assert _normal_path(holder_record["user_data_dir"]) == _normal_path(user_data), holder_record
    assert holder_record["godot_version"] == profile["godot_version"], holder_record
    fallback = int(holder_record["port"])
    marker = user_data / "launcher-lock-ports" / str(fallback)
    assert marker.is_dir(), f"successful listener marker was not published: {marker}"

    cli_command = [str(executable), "--headless", "--path", str(LAUNCHER), "--", "--offline", "--list"]
    cli_busy = subprocess.run(cli_command, cwd=REPO, env=env, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=45)
    _save_process(evidence, "launcher-cli-refused-by-holder", cli_command, cli_busy.returncode,
                  cli_busy.stdout, cli_busy.stderr)
    refusal = f"Another OpenBFME Launcher ({holder_version}) is already open."
    assert cli_busy.returncode != 0 and refusal in cli_busy.stdout + cli_busy.stderr, (
        f"launcher CLI did not report the exact holder version: {cli_busy.returncode}\n{cli_busy.stdout}\n{cli_busy.stderr}"
    )
    holder.kill()

    reacquire, reacquired = _run_script(executable, LAUNCHER, ["--mode=try", "--version=after-kill"], env,
                                       evidence, "after-kill-reacquire")
    assert reacquire.returncode == 0 and reacquired["acquired"] and reacquired["port"] == fallback, (
        reacquire.stdout + reacquire.stderr
    )
    cli_idle = subprocess.run(cli_command, cwd=REPO, env=env, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=45)
    _save_process(evidence, "launcher-cli-offline-list", cli_command, cli_idle.returncode,
                  cli_idle.stdout, cli_idle.stderr)
    assert cli_idle.returncode == 0, (
        f"launcher CLI idle smoke exited {cli_idle.returncode}\n{cli_idle.stdout}\n{cli_idle.stderr}"
    )
    fallback_line = f"LAUNCHER local lock port {first} is excluded by Windows; using {fallback}"
    assert fallback_line in cli_idle.stdout + cli_idle.stderr, cli_idle.stdout + cli_idle.stderr

    release, release_record = _run_script(executable, LAUNCHER, ["--mode=release-reacquire"], env,
                                          evidence, "release-reacquire")
    assert release.returncode == 0 and release_record["first_acquired"] and release_record["second_acquired"], (
        release.stdout + release.stderr
    )

    races = []
    for round_number in range(1, 4):
        pair = [
            GodotProcess(executable, LAUNCHER, ["--mode=hold", f"--version=race-{round_number}-a"], env,
                         evidence, f"simultaneous-{round_number}-a"),
            GodotProcess(executable, LAUNCHER, ["--mode=hold", f"--version=race-{round_number}-b"], env,
                         evidence, f"simultaneous-{round_number}-b"),
        ]
        records = []
        try:
            records = [proc.next_record(45) for proc in pair]
            winners = [index for index, record in enumerate(records) if record["acquired"]]
            assert len(winners) == 1, f"round {round_number} did not have exactly one winner: {records}"
            loser = 1 - winners[0]
            pair[loser].finish(20)
            winner_record = records[winners[0]]
            assert winner_record["port"] == expected_fallback, winner_record
            races.append({"round": round_number, "winner": winner_record["version"], "port": winner_record["port"],
                          "loser_error": records[loser]["error"]})
        finally:
            for proc in pair:
                if proc.process.poll() is None:
                    proc.kill()

    for kind in ("silent", "http"):
        data_dir, candidate = _find_data_dir(evidence / "unrelated" / kind, ranges)
        listener, response_thread = _start_listener(candidate, http=kind == "http")
        try:
            args = ["--mode=try", f"--data-dir={data_dir}", f"--version=foreign-{kind}", "--query-ranges="]
            result, record = _run_script(executable, LAUNCHER, args, env, evidence, f"foreign-{kind}-listener")
            assert result.returncode == 0 and not record["acquired"], result.stdout + result.stderr
            assert record["port"] == candidate and str(candidate) in record["error"], record
            expected_text = "it answered something else" if kind == "http" else "it did not answer as a launcher"
            assert expected_text in record["error"], record
        finally:
            listener.close()
            if response_thread is not None:
                response_thread.join(timeout=3)

    query_failure, failed_query = _run_script(
        executable, LAUNCHER, ["--mode=try", "--version=query-failure", "--query-error=injected query failure"],
        env, evidence, "query-failure",
    )
    assert query_failure.returncode == 0 and not failed_query["acquired"], query_failure.stdout + query_failure.stderr
    assert failed_query["error"] == f"cannot bind local port {first}: injected query failure", failed_query

    exhaustion, exhausted = _run_script(
        executable, LAUNCHER,
        ["--mode=try", "--version=exhaustion", f"--query-ranges={FIRST_PORT}-{FIRST_PORT + PORTS - 1}"],
        env, evidence, "excluded-range-exhaustion", timeout=60,
    )
    assert exhaustion.returncode == 0 and not exhausted["acquired"], exhaustion.stdout + exhaustion.stderr
    assert exhausted["error"] == "all launcher lock ports are excluded by Windows", exhausted

    wrap_ranges = f"{first}-{FIRST_PORT + PORTS - 1},{FIRST_PORT}-{FIRST_PORT + 1}"
    wrap, wrapped = _run_script(
        executable, LAUNCHER, ["--mode=try", "--version=wrap-scan", f"--query-ranges={wrap_ranges}"],
        env, evidence, "circular-fallback",
    )
    assert wrap.returncode == 0 and wrapped["acquired"] and wrapped["port"] == FIRST_PORT + 2, (
        wrap.stdout + wrap.stderr
    )

    for marker_kind in ("silent", "closed"):
        data_dir, base = _find_data_dir(evidence / "markers" / marker_kind, ranges)
        assert _is_open(base), f"test marker base port {base} is not free"
        marker_port = next(
            port for port in range(FIRST_PORT, FIRST_PORT + PORTS)
            if port != base and not _excluded(port, ranges) and _is_open(port)
        )
        marker_dir = data_dir / "launcher-lock-ports" / str(marker_port)
        marker_dir.mkdir(parents=True, exist_ok=True)
        if marker_kind == "silent":
            listener, _ = _start_listener(marker_port)
        else:
            listener = None
        try:
            result, record = _run_script(
                executable, LAUNCHER, ["--mode=try", f"--data-dir={data_dir}", f"--version=marker-{marker_kind}"],
                env, evidence, f"marker-{marker_kind}",
            )
            if marker_kind == "silent":
                assert result.returncode == 0 and not record["acquired"], result.stdout + result.stderr
                assert record["port"] == marker_port and "did not answer as a launcher" in record["error"], record
                assert str(base) not in record["error"], record
            else:
                assert result.returncode == 0 and record["acquired"] and record["port"] == base, (
                    result.stdout + result.stderr
                )
        finally:
            if listener is not None:
                listener.close()

    changed_data_dir, changed_base = _find_data_dir(evidence / "changed-exclusions", ranges)
    assert _is_open(changed_base), f"changed-exclusions base port {changed_base} is not free"
    override_port = next(
        candidate for candidate in range(FIRST_PORT, FIRST_PORT + PORTS)
        if candidate != changed_base and not _excluded(candidate, ranges) and _is_open(candidate)
    )
    fallback_holder = GodotProcess(
        executable, LAUNCHER,
        ["--mode=hold", f"--data-dir={changed_data_dir}", f"--first-port={override_port}",
         "--version=fallback-owner", "--query-ranges="],
        env, evidence, "changed-exclusions-holder",
    )
    fallback_record = fallback_holder.next_record(45)
    assert fallback_record["acquired"] and fallback_record["port"] == override_port, fallback_holder.output()
    changed, changed_record = _run_script(
        executable, LAUNCHER,
        ["--mode=try", f"--data-dir={changed_data_dir}", "--version=changed-query", "--query-ranges="],
        env, evidence, "changed-exclusions-refusal",
    )
    assert changed.returncode == 0 and not changed_record["acquired"], changed.stdout + changed.stderr
    assert changed_record["other_launcher"] == "fallback-owner", changed_record
    assert changed_record["port"] == override_port, changed_record
    assert _is_open(changed_base), f"changed-exclusions base port {changed_base} became occupied"
    fallback_holder.kill()

    background = GodotProcess(
        executable, LAUNCHER, ["--mode=background", "--version=native-update"], env,
        evidence, "background-holder",
    )
    background_record = background.next_record(45)
    assert background_record["acquired"], background.output()
    takeover, takeover_record = _run_script(
        executable, LAUNCHER, ["--mode=takeover"], env, evidence, "game-start-takeover", timeout=30,
    )
    assert takeover.returncode == 0 and takeover_record["taken_over"], takeover.stdout + takeover.stderr
    assert takeover_record["observed_launcher"] == "native-update background", takeover_record
    background_exit = background.finish(15)
    assert background_exit == 0 and any(
        json.loads(line[len(JSON_PREFIX):]).get("stop_file_seen")
        for line in background.stdout if line.startswith(JSON_PREFIX) and "background_released" in line
    ), background.output()
    assert not (user_data / "game-start.stop").exists(), "GameStart.take_over left its stop marker behind"

    summary = {
        "godot": str(executable),
        "godot_version": profile["godot_version"],
        "appdata": str(appdata),
        "user_data_dir": str(user_data),
        "evidence": str(evidence),
        "base_port": first,
        "raw_bind": raw_bind,
        "excluded_ranges": ranges,
        "baseline": baseline_summary,
        "patched": {
            "acquired": holder_record["acquired"], "port": fallback, "expected_fallback": expected_fallback,
            "godot_version": holder_record["godot_version"],
        },
        "simultaneous": races,
        "cli_holder_refusal": {"returncode": cli_busy.returncode, "version": holder_version},
        "cli_idle": {"returncode": cli_idle.returncode},
        "changed_exclusions_refusal": {
            "base_port": changed_base,
            "holder_port": override_port,
            "record": changed_record,
        },
        "takeover": takeover_record,
    }
    (evidence / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return summary


@pytest.mark.skipif(os.name != "nt", reason="native excluded-port tests require Windows")
def test_instance_lock_native_windows():
    executable = Path(os.environ.get("OPENBFME_GODOT", str(DEFAULT_GODOT)))
    if not executable.is_file():
        pytest.skip(f"official Godot console executable not found: {executable}")
    evidence_root = Path(os.environ.get(
        "OPENBFME_INSTANCE_LOCK_EVIDENCE",
        str(Path.home() / "openbfme-logs" / "launcher-instance-lock"),
    ))
    evidence_root.mkdir(parents=True, exist_ok=True)
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ") + "-" + uuid4().hex[:8]
    evidence = (evidence_root / f"run-{run_id}").resolve()
    evidence.mkdir()
    _run_native_suite(evidence, executable.resolve())


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-q", "-s"]))
