"""Bug-report-grade diagnostic run logging, shared across Open-BFME components.

The problem this exists for is recorded in AGENTS.md rule 5: this project's
characteristic failure is *confidently wrong output from a silent fallback* — a
stale pack, a PATH ffmpeg instead of the pinned one, another edition's catalog.
A run that produced wrong numbers looked exactly like one that produced right
ones. So the point of this module is not "more logs"; it is to make every
identity and every fallback-prone choice a durable, machine-readable fact that a
bug report can carry.

SHARED LOG CONTRACT (the Godot sim and the WPF launcher write the same tree):

    <root>/<UTC yyyyMMddTHHmmssZ>-<component>-<pid>/
        run.log        human-readable, one timestamped line per event
        run.jsonl      one JSON object per event: ts, level, component, event, fields
        env.json       machine facts (OS, versions, interpreter, env vars, cwd, git)
        identity.json  content identity read from the workspace selection, never guessed
        error.txt      exception + traceback + recent log lines (only on failure)

    root      = %LOCALAPPDATA%/OpenBFME/logs, overridden by OPENBFME_LOG_DIR
    component = importer | converter | sim | launcher
    opt-in    = --diagnostics or OPENBFME_DIAGNOSTICS=1 (default = today's behavior)
    retention = keep the newest 20 run dirs; a dir holding error.txt survives
                until it is beyond 100 runs old
    redaction = mandatory; no signing keys, tokens, or verbatim home paths

Two hard rules for anyone editing this file:

1. **Logging must never break a conversion.** Every disk touch is fail-open. If
   the log root cannot be created we say so loudly on stderr, disable ourselves,
   and let the real work proceed.
2. **Content digests are evidence, not secrets.** Redaction must never mangle a
   sha256 — that is precisely the field a bug report needs.
"""

from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass, field
import datetime as _datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import sys
import threading
import time
import traceback
from typing import Any, Iterator, Mapping, Sequence


__all__ = [
    "COMPONENTS",
    "DIAGNOSTICS_ENV",
    "LOG_ROOT_ENV",
    "MAX_RUNS",
    "MAX_RUNS_WITH_ERROR",
    "RUN_DIR_PATTERN",
    "DiagnosticsRun",
    "Phase",
    "active_run",
    "component_for_command",
    "default_log_root",
    "diagnostics_enabled",
    "redact",
    "set_active_run",
    "start_run",
]


LOG_ROOT_ENV = "OPENBFME_LOG_DIR"
DIAGNOSTICS_ENV = "OPENBFME_DIAGNOSTICS"

#: The only component names the shared tree accepts. A run directory whose
#: component is not in this tuple is not ours and is never pruned.
COMPONENTS: tuple[str, ...] = ("importer", "converter", "sim", "launcher")

#: Retention: newest N run directories survive unconditionally.
MAX_RUNS = 20
#: A run directory containing error.txt survives until it is this far down.
MAX_RUNS_WITH_ERROR = 100

#: The trailing `-<n>` is the collision suffix start_run adds when one pid opens
#: two runs inside the same second. It is part of the pattern on purpose: a name
#: retention cannot recognise is a name retention can never prune.
RUN_DIR_PATTERN = (
    r"(?P<stamp>\d{8}T\d{6}Z)-(?P<component>"
    + "|".join(COMPONENTS)
    + r")-(?P<pid>\d+)(?:-(?P<sequence>\d+))?"
)
_RUN_DIR_RE = re.compile(RUN_DIR_PATTERN)

LEVELS = ("debug", "info", "warning", "error", "critical")

#: How many recent human log lines error.txt carries alongside the traceback.
ERROR_TAIL_LINES = 60

REDACTED = "<REDACTED>"


# ---------------------------------------------------------------------------
# Redaction
# ---------------------------------------------------------------------------

#: Environment variable names whose *value* is never written, whatever it is.
_SECRET_NAME_RE = re.compile(
    r"(TOKEN|SECRET|PASSWORD|PASSWD|PASSPHRASE|CREDENTIAL|SIGNING|PRIVATE_KEY"
    r"|APIKEY|API_KEY|ACCESS_KEY|SESSION|COOKIE|AUTH)",
    re.IGNORECASE,
)

#: Value shapes that are secrets wherever they appear. Deliberately narrow:
#: a bare 64-character hex string is a content digest, which we must preserve.
_SECRET_VALUE_RES: tuple[re.Pattern[str], ...] = (
    re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----"),
    re.compile(r"\bghp_[A-Za-z0-9]{16,}\b"),
    re.compile(r"\bgithub_pat_[A-Za-z0-9_]{16,}\b"),
    re.compile(r"\bgh[oprsu]_[A-Za-z0-9]{16,}\b"),
    re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{10,}\b"),
    re.compile(r"\bsk-[A-Za-z0-9_\-]{16,}\b"),
    re.compile(r"(?i)\bbearer\s+[A-Za-z0-9._\-]{16,}"),
    re.compile(
        r"(?i)\b(api[_-]?key|access[_-]?key|secret|token|password|passwd"
        r"|passphrase|signing[_-]?key)\b\s*[=:]\s*[\"']?[^\s\"',;]{6,}"
    ),
)


def _machine_path_masks() -> list[tuple[re.Pattern[str], str]]:
    """Longest-first path → placeholder rules for this machine.

    Ordered by descending literal length so that nested roots (TEMP lives under
    LOCALAPPDATA, LOCALAPPDATA under USERPROFILE) mask at their most specific
    form instead of leaving a home-relative tail behind.
    """

    candidates: list[tuple[str, str]] = []

    def add(raw: str | None, placeholder: str) -> None:
        if not raw:
            return
        text = str(raw).strip().rstrip("\\/")
        if len(text) < 4:
            return
        candidates.append((text, placeholder))

    try:
        add(str(_repo_root()), "<REPO>")
    except Exception:
        pass
    add(os.environ.get("OPENBFME_IMPORT_ROOT"), "<STATE_ROOT>")
    add(os.environ.get("OPENBFME_CONTENT_ROOT"), "<CONTENT_ROOT>")
    add(os.environ.get("BFME2_INSTALL"), "<RETAIL_INSTALL>")
    add(os.environ.get("TEMP"), "%TEMP%")
    add(os.environ.get("TMP"), "%TEMP%")
    add(os.environ.get("LOCALAPPDATA"), "%LOCALAPPDATA%")
    add(os.environ.get("APPDATA"), "%APPDATA%")
    add(os.environ.get("USERPROFILE"), "%USERPROFILE%")
    try:
        add(str(Path.home()), "%USERPROFILE%")
    except Exception:
        pass
    add(os.environ.get("HOME"), "%USERPROFILE%")

    rules: list[tuple[re.Pattern[str], str]] = []
    seen: set[str] = set()
    for text, placeholder in sorted(candidates, key=lambda item: -len(item[0])):
        key = text.casefold()
        if key in seen:
            continue
        seen.add(key)
        # Treat / and \ as interchangeable; Windows paths arrive both ways.
        pattern = "[\\\\/]+".join(re.escape(part) for part in re.split(r"[\\/]+", text))
        rules.append((re.compile(pattern, re.IGNORECASE), placeholder))
    return rules


def _account_names() -> list[str]:
    names: list[str] = []
    for value in (
        os.environ.get("USERNAME"),
        os.environ.get("USER"),
        os.environ.get("LOGNAME"),
    ):
        if value and len(value.strip()) >= 3:
            names.append(value.strip())
    try:
        home_name = Path.home().name
    except Exception:
        home_name = ""
    if len(home_name) >= 3:
        names.append(home_name)
    unique: list[str] = []
    seen: set[str] = set()
    for name in names:
        key = name.casefold()
        if key not in seen:
            seen.add(key)
            unique.append(name)
    return unique


_MASK_LOCK = threading.Lock()
_MASK_CACHE: tuple[Any, ...] | None = None


def _masks() -> tuple[list[tuple[re.Pattern[str], str]], list[re.Pattern[str]]]:
    """Cached machine masks, invalidated when the relevant env changes."""

    global _MASK_CACHE
    signature = tuple(
        os.environ.get(name, "")
        for name in (
            "OPENBFME_IMPORT_ROOT",
            "OPENBFME_CONTENT_ROOT",
            "BFME2_INSTALL",
            "TEMP",
            "TMP",
            "LOCALAPPDATA",
            "APPDATA",
            "USERPROFILE",
            "HOME",
            "USERNAME",
            "USER",
            "LOGNAME",
        )
    )
    with _MASK_LOCK:
        if _MASK_CACHE is not None and _MASK_CACHE[0] == signature:
            return _MASK_CACHE[1], _MASK_CACHE[2]
        paths = _machine_path_masks()
        accounts = [
            re.compile(rf"(?<![A-Za-z0-9]){re.escape(name)}(?![A-Za-z0-9])", re.IGNORECASE)
            for name in _account_names()
        ]
        _MASK_CACHE = (signature, paths, accounts)
        return paths, accounts


#: Catch-alls applied after the machine-specific masks. These exist because a
#: path can reach the log from a value this process never set — `doctor` reports
#: the install root recorded inside a catalog, for instance — and both shapes
#: below are patterns tools/export-scan.ps1 fails a build over.
_STATIC_PATH_RES: tuple[tuple[re.Pattern[str], str], ...] = (
    # Any drive-rooted retail install, not just the one this run configured.
    (
        re.compile(r"(?i)\b[A-Za-z]:[\\/]+(?:BFME2|ROTWK)\b(?:[\\/][^\s\"',;]*)?"),
        "<RETAIL_INSTALL>",
    ),
    # Any home directory, including another account's.
    (
        re.compile(r"(?i)\b[A-Za-z]:[\\/]+Users[\\/]+[A-Za-z0-9._-]+"),
        "%USERPROFILE%",
    ),
    (re.compile(r"(?i)/(?:home|Users)/[A-Za-z0-9._-]+"), "%USERPROFILE%"),
    # UNC shares name a machine and an operator's share; neither is ours to log.
    (re.compile(r"\\\\[^\\\s\"']+\\[^\\\s\"']+"), "<UNC-PATH>"),
)


def redact_text(value: str) -> str:
    """Mask secrets and machine-identifying paths in one string.

    Content digests are intentionally left intact: a sha256 is the evidence a
    bug report exists to carry, not a secret.
    """

    text = value
    for pattern in _SECRET_VALUE_RES:
        text = pattern.sub(REDACTED, text)
    path_masks, account_masks = _masks()
    for pattern, placeholder in path_masks:
        text = pattern.sub(placeholder, text)
    for pattern, placeholder in _STATIC_PATH_RES:
        text = pattern.sub(placeholder, text)
    for pattern in account_masks:
        text = pattern.sub("<USER>", text)
    return text


def redact(value: Any) -> Any:
    """Recursively redact a JSON-ish value. Pure: the input is never mutated."""

    if isinstance(value, str):
        return redact_text(value)
    if isinstance(value, Path):
        return redact_text(str(value))
    if isinstance(value, Mapping):
        return {str(key): redact(item) for key, item in value.items()}
    if isinstance(value, (list, tuple, set, frozenset)):
        return [redact(item) for item in value]
    if isinstance(value, (int, float, bool)) or value is None:
        return value
    return redact_text(str(value))


# ---------------------------------------------------------------------------
# Enablement and roots
# ---------------------------------------------------------------------------

_TRUE = {"1", "true", "yes", "on", "y", "t"}


def diagnostics_enabled() -> bool:
    """Whether the environment asks for the full firehose."""

    return os.environ.get(DIAGNOSTICS_ENV, "").strip().casefold() in _TRUE


def default_log_root() -> Path:
    """`OPENBFME_LOG_DIR`, else `%LOCALAPPDATA%/OpenBFME/logs`.

    Non-Windows and stripped environments fall back to `~/.local/share`, which
    keeps the sibling components' contract satisfiable off Windows too.
    """

    override = os.environ.get(LOG_ROOT_ENV, "").strip()
    if override:
        return Path(override).expanduser().resolve()
    local = os.environ.get("LOCALAPPDATA", "").strip()
    if local:
        return (Path(local) / "OpenBFME" / "logs").resolve()
    return (Path.home() / ".local" / "share" / "OpenBFME" / "logs").resolve()


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


#: Commands that actually convert retail bytes. Everything else is inspection.
_CONVERTER_COMMANDS = frozenset(
    {
        "build",
        "extract",
        "extract-all-assets",
        "import-faction",
        "import-unit",
        "publish-faction-to-slice",
        "publish-music-pack",
        "convert-videos",
        "convert",
        "convert-assets",
    }
)


def component_for_command(command: str | None) -> str:
    """Map a CLI subcommand onto a shared-contract component name."""

    if command and command.strip().casefold() in _CONVERTER_COMMANDS:
        return "converter"
    return "importer"


def _utc_now() -> _datetime.datetime:
    return _datetime.datetime.now(_datetime.timezone.utc)


def _stamp(moment: _datetime.datetime) -> str:
    return moment.strftime("%Y%m%dT%H%M%SZ")


def _iso(moment: _datetime.datetime) -> str:
    return moment.strftime("%Y-%m-%dT%H:%M:%S.") + f"{moment.microsecond // 1000:03d}Z"


def _warn(message: str) -> None:
    """One loud line on stderr. Never raises — stderr can be closed."""

    try:
        print(f"[diagnostics] {message}", file=sys.stderr, flush=True)
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Identity (the anti-silent-fallback record)
# ---------------------------------------------------------------------------


def _sha256_file(path: Path) -> str | None:
    try:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError:
        return None


def _split_pack_reference(reference: str, content_root: Path) -> dict[str, Any]:
    """Split `<pack-id>/<digest>` and say whether those bytes are really here.

    `present` is the field that catches a stale selection pointing at a pack
    that was never published to this root — the shape of at least three past
    "the runtime kept loading a pack whose address was a lie" incidents.
    """

    text = str(reference).strip().replace("\\", "/")
    pack_id, _, digest = text.rpartition("/")
    entry: dict[str, Any] = {
        "reference": text,
        "id": pack_id or None,
        "digest": digest or None,
        "present": False,
    }
    if not pack_id or not digest:
        entry["malformed"] = True
        return entry
    try:
        entry["present"] = (content_root / pack_id / digest).is_dir()
    except OSError:
        entry["present"] = False
    return entry


def collect_identity(content_root: Path | None = None) -> dict[str, Any]:
    """Read the workspace selection. Report what was read; never guess.

    An unresolved identity is a first-class answer: `resolved: false` plus a
    reason beats a plausible-looking pack id that nothing actually loaded.
    """

    payload: dict[str, Any] = {
        "schema": "openbfme.diagnostics.identity",
        "schemaVersion": 1,
        "resolved": False,
        "content_root": None,
        "selection_path": None,
        "selection_sha256": None,
        "active_pack": None,
        "supplemental_packs": [],
    }
    try:
        if content_root is None:
            from .paths import default_godot_content_root

            root = default_godot_content_root()
        else:
            root = Path(content_root).expanduser().resolve()
    except Exception as exc:  # pragma: no cover - environment dependent
        payload["reason"] = f"content root unresolvable: {exc}"
        return payload

    payload["content_root"] = str(root)
    selection = root / "selection.json"
    payload["selection_path"] = str(selection)
    if not selection.is_file():
        payload["reason"] = "no selection.json under the content root"
        return payload

    payload["selection_sha256"] = _sha256_file(selection)
    try:
        document = json.loads(selection.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        payload["reason"] = f"selection.json is unreadable: {exc}"
        return payload
    if not isinstance(document, dict):
        payload["reason"] = "selection.json is not an object"
        return payload

    payload["selection_schema"] = document.get("schema")
    payload["selection_schema_version"] = document.get("schemaVersion")
    active = document.get("activePack")
    if not isinstance(active, str) or not active.strip():
        payload["reason"] = "selection.json names no activePack"
        return payload
    payload["active_pack"] = _split_pack_reference(active, root)
    supplemental = document.get("supplementalPacks") or []
    if isinstance(supplemental, list):
        payload["supplemental_packs"] = [
            _split_pack_reference(str(item), root) for item in supplemental
        ]
    payload["resolved"] = True
    missing = [
        entry["reference"]
        for entry in [payload["active_pack"], *payload["supplemental_packs"]]
        if not entry.get("present")
    ]
    payload["missing_pack_directories"] = missing
    return payload


def collect_environment(argv: Sequence[str] | None = None) -> dict[str, Any]:
    """Machine facts a bug report needs, with secret-named values masked."""

    interesting_prefixes = ("OPENBFME_",)
    interesting_names = {
        "BFME2_INSTALL",
        "GODOT",
        "GODOT4",
        "PYTHONPATH",
        "PYTHONHOME",
        "PROCESSOR_ARCHITECTURE",
        "NUMBER_OF_PROCESSORS",
        "CI",
        "GITHUB_ACTIONS",
    }
    environment: dict[str, str] = {}
    for name, value in sorted(os.environ.items()):
        if not (name.startswith(interesting_prefixes) or name in interesting_names):
            continue
        environment[name] = REDACTED if _SECRET_NAME_RE.search(name) else value

    payload: dict[str, Any] = {
        "schema": "openbfme.diagnostics.env",
        "schemaVersion": 1,
        "captured_utc": _iso(_utc_now()),
        "os": {
            "platform": platform.platform(),
            "system": platform.system(),
            "release": platform.release(),
            "version": platform.version(),
            "machine": platform.machine(),
        },
        "interpreter": {
            "executable": sys.executable,
            "version": sys.version,
            "implementation": platform.python_implementation(),
            "prefix": sys.prefix,
            "is_pinned_env": "python-3.12-env" in (sys.executable or "").replace("\\", "/"),
        },
        "process": {
            "pid": os.getpid(),
            "cwd": os.getcwd(),
            "argv": list(argv) if argv is not None else list(sys.argv),
        },
        "environment": environment,
    }

    try:
        from .version import __version__ as importer_version

        payload["importer_version"] = importer_version
    except Exception:  # pragma: no cover
        payload["importer_version"] = None

    # Library versions that have silently produced wrong output before.
    libraries: dict[str, Any] = {}
    try:
        import PIL  # type: ignore

        libraries["Pillow"] = {
            "version": getattr(PIL, "__version__", None),
            "path": getattr(PIL, "__file__", None),
        }
    except Exception:
        libraries["Pillow"] = None
    payload["libraries"] = libraries

    try:
        from .tools import git_revision_at_exact_root

        payload["git_commit"] = git_revision_at_exact_root(_repo_root())
    except Exception:
        payload["git_commit"] = None
    payload["repo_root"] = str(_repo_root())
    payload["git_executable"] = shutil.which("git")
    return payload


# ---------------------------------------------------------------------------
# The run
# ---------------------------------------------------------------------------


@dataclass
class Phase:
    """A phase handle; `count()` accumulates the numbers phase.end reports."""

    run: "DiagnosticsRun"
    name: str
    started: float = field(default_factory=time.monotonic)
    counts: dict[str, Any] = field(default_factory=dict)

    def count(self, **values: Any) -> None:
        for key, value in values.items():
            current = self.counts.get(key)
            if isinstance(current, (int, float)) and isinstance(value, (int, float)):
                self.counts[key] = current + value
            else:
                self.counts[key] = value

    def note(self, event: str, **fields: Any) -> None:
        self.run.event(event, phase=self.name, **fields)


class DiagnosticsRun:
    """One run directory. Disabled runs are inert and touch no disk."""

    def __init__(
        self,
        component: str,
        *,
        directory: Path | None,
        enabled: bool,
    ) -> None:
        self.component = component
        self._directory = directory
        self._enabled = bool(enabled and directory is not None)
        self._lock = threading.Lock()
        self._seq = 0
        self._started = time.monotonic()
        self._tail: list[str] = []
        self._closed = False
        self._failed = False
        self._write_warned = False

    # -- properties ---------------------------------------------------------

    @property
    def enabled(self) -> bool:
        return self._enabled

    @property
    def directory(self) -> Path | None:
        return self._directory if self._enabled else None

    # -- low-level writers (fail-open) --------------------------------------

    def _append(self, name: str, text: str) -> None:
        assert self._directory is not None
        with (self._directory / name).open("a", encoding="utf-8", newline="\n") as handle:
            handle.write(text)

    def _write(self, name: str, payload: Any) -> None:
        if not self._enabled or self._directory is None:
            return
        try:
            rendered = json.dumps(redact(payload), indent=2, sort_keys=True) + "\n"
            (self._directory / name).write_text(rendered, encoding="utf-8", newline="\n")
        except (OSError, TypeError, ValueError) as exc:
            self._warn_once(f"could not write {name}: {exc}")

    def _warn_once(self, message: str) -> None:
        if not self._write_warned:
            self._write_warned = True
            _warn(f"WARN {message} (diagnostics degraded; work continues)")

    # -- events -------------------------------------------------------------

    def event(self, event: str, *, level: str = "info", **fields: Any) -> None:
        """Record one event. Never raises, whatever the disk does."""

        if not self._enabled:
            return
        if level not in LEVELS:
            level = "info"
        moment = _utc_now()
        with self._lock:
            self._seq += 1
            seq = self._seq
        body = dict(fields)
        body["seq"] = seq
        body["elapsed_s"] = round(time.monotonic() - self._started, 3)
        record = {
            "ts": _iso(moment),
            "level": level,
            "component": self.component,
            "event": str(event),
            "fields": redact(body),
        }
        rendered_fields = " ".join(
            f"{key}={_render_scalar(value)}"
            for key, value in sorted(record["fields"].items())
            if key not in {"seq", "elapsed_s"}
        )
        line = (
            f"{record['ts']} {record['level'].upper():<8}{self.component} "
            f"{record['event']}"
        )
        if rendered_fields:
            line += f" | {rendered_fields}"
        with self._lock:
            self._tail.append(line)
            if len(self._tail) > ERROR_TAIL_LINES * 4:
                del self._tail[: len(self._tail) - ERROR_TAIL_LINES * 4]
        try:
            self._append("run.jsonl", json.dumps(record, sort_keys=True) + "\n")
            self._append("run.log", line + "\n")
        except Exception as exc:
            self._warn_once(f"could not append to the run log: {exc}")

    def decision(
        self,
        kind: str,
        *,
        chosen: Any,
        reason: str,
        candidates: Sequence[Any] | None = None,
        **fields: Any,
    ) -> None:
        """Record a fallback-prone choice: which tool/pack/root/catalog won.

        AGENTS.md rule 5. If a code path can pick between a pinned artifact and
        whatever happens to be lying around, the pick belongs here.
        """

        self.event(
            "decision",
            kind=kind,
            chosen=chosen,
            reason=reason,
            candidates=list(candidates) if candidates is not None else None,
            **fields,
        )

    def refusal(self, what: str, *, reason: str, **fields: Any) -> None:
        self.event("refusal", level="warning", what=what, reason=reason, **fields)

    def begin_phase(self, name: str, **fields: Any) -> Phase:
        """Open a phase without a `with` block.

        Long orchestration functions (pipeline.build is 470 lines) cannot be
        re-indented into nested context managers without churn that would bury
        the actual change, so the explicit begin/end pair is the supported form
        there. It emits exactly the same events as :meth:`phase`.
        """

        handle = Phase(self, name)
        # `phase.begin`, not `phase.start`: the Godot sim and the WPF launcher
        # emit that name, and a shared log tree whose event vocabulary differs
        # per component is not actually shared.
        self.event("phase.begin", level="debug", phase=name, **fields)
        return handle

    def end_phase(
        self,
        handle: Phase,
        *,
        outcome: str = "ok",
        exc: BaseException | None = None,
        **counts: Any,
    ) -> None:
        handle.count(**counts)
        fields: dict[str, Any] = {
            "phase": handle.name,
            "outcome": outcome,
            "duration_s": round(time.monotonic() - handle.started, 3),
            "counts": dict(handle.counts),
        }
        if exc is not None:
            fields["exception_type"] = type(exc).__name__
            fields["exception"] = str(exc)
        self.event(
            "phase.end", level="error" if outcome != "ok" else "info", **fields
        )

    @contextmanager
    def phase(self, name: str, **fields: Any) -> Iterator[Phase]:
        """Bracket a pipeline phase; records start, duration, counts, outcome."""

        handle = self.begin_phase(name, **fields)
        try:
            yield handle
        except BaseException as exc:
            self.end_phase(handle, outcome="failed", exc=exc)
            if isinstance(exc, Exception):
                self.failure(exc, context={"phase": name})
            raise
        else:
            self.end_phase(handle, outcome="ok")

    def failure(self, exc: BaseException, *, context: Mapping[str, Any] | None = None) -> None:
        """Write error.txt (traceback + recent log lines) and flag the run."""

        if not self._enabled or self._directory is None:
            return
        self._failed = True
        self.event(
            "failure",
            level="error",
            exception_type=type(exc).__name__,
            exception=str(exc),
            **(dict(context) if context else {}),
        )
        try:
            trace = "".join(
                traceback.format_exception(type(exc), exc, exc.__traceback__)
            )
        except Exception:  # pragma: no cover
            trace = f"{type(exc).__name__}: {exc}\n"
        with self._lock:
            tail = list(self._tail[-ERROR_TAIL_LINES:])
        sections = [
            f"component: {self.component}",
            f"utc: {_iso(_utc_now())}",
            f"context: {json.dumps(redact(dict(context or {})), sort_keys=True)}",
            "",
            "--- traceback ---",
            redact_text(trace).rstrip(),
            "",
            f"--- last {len(tail)} log lines ---",
            *tail,
            "",
        ]
        try:
            self._append("error.txt", "\n".join(sections))
        except Exception as exc2:
            self._warn_once(f"could not write error.txt: {exc2}")

    # -- identity re-statement ---------------------------------------------

    def record_identity(self, content_root: Path | None = None) -> dict[str, Any]:
        """Re-read identity once the real content root is known, and log it."""

        # Guarded, not merely no-op-logged: collect_identity walks the content
        # root and sha256s selection.json. A disabled run must cost nothing, or
        # "diagnostics off = today's behavior" stops being true.
        if not self._enabled:
            return {}
        payload = collect_identity(content_root)
        self._write("identity.json", payload)
        self.event(
            "identity",
            level="info" if payload.get("resolved") else "warning",
            resolved=bool(payload.get("resolved")),
            content_root=payload.get("content_root"),
            selection_sha256=payload.get("selection_sha256"),
            active_pack=(payload.get("active_pack") or {}).get("reference"),
            missing_pack_directories=payload.get("missing_pack_directories"),
            reason=payload.get("reason"),
        )
        return payload

    # -- lifecycle ----------------------------------------------------------

    def close(self, *, outcome: str | None = None, **fields: Any) -> None:
        if not self._enabled or self._closed:
            self._closed = True
            return
        self._closed = True
        exit_code = fields.get("exit_code")
        failed = self._failed or (isinstance(exit_code, int) and exit_code != 0)
        resolved = outcome or ("failed" if failed else "ok")
        self.event(
            "run.end",
            level="error" if resolved == "failed" else "info",
            outcome=resolved,
            duration_s=round(time.monotonic() - self._started, 3),
            **fields,
        )
        if _ACTIVE is self:
            set_active_run(None)

    def __enter__(self) -> "DiagnosticsRun":
        return self

    def __exit__(self, exc_type, exc, tb) -> bool:  # type: ignore[no-untyped-def]
        if exc is not None and isinstance(exc, Exception):
            self.failure(exc)
        self.close()
        return False


def _render_scalar(value: Any) -> str:
    if isinstance(value, str):
        return value if " " not in value else json.dumps(value)
    if value is None or isinstance(value, (int, float, bool)):
        return json.dumps(value)
    try:
        return json.dumps(value, sort_keys=True)
    except (TypeError, ValueError):
        return str(value)


class _DisabledRun(DiagnosticsRun):
    """The no-op run handed out when diagnostics are off or unusable."""

    def __init__(self, component: str = "importer") -> None:
        super().__init__(component, directory=None, enabled=False)


_DISABLED = _DisabledRun()
_ACTIVE: DiagnosticsRun | None = None


def active_run() -> DiagnosticsRun:
    """The current run, or an inert one. Callers never need a null check."""

    return _ACTIVE if _ACTIVE is not None else _DISABLED


def set_active_run(run: DiagnosticsRun | None) -> None:
    global _ACTIVE
    _ACTIVE = run


# ---------------------------------------------------------------------------
# Retention
# ---------------------------------------------------------------------------


def prune_runs(root: Path, *, keep: int = MAX_RUNS, keep_errors: int = MAX_RUNS_WITH_ERROR) -> list[str]:
    """Delete stale run dirs, sparing failures until the hard ceiling.

    Only directories matching the shared naming contract are considered; a
    sibling's stray folder is never touched. Names begin with a fixed-width UTC
    stamp, so lexicographic order is chronological order.
    """

    removed: list[str] = []
    try:
        entries = [item for item in root.iterdir() if item.is_dir()]
    except OSError:
        return removed
    runs = sorted(
        (item for item in entries if _RUN_DIR_RE.fullmatch(item.name)),
        key=lambda item: item.name,
        reverse=True,
    )
    for index, item in enumerate(runs):
        if index < keep:
            continue
        try:
            has_error = (item / "error.txt").exists()
        except OSError:
            has_error = False
        if has_error and index < keep_errors:
            continue
        try:
            shutil.rmtree(item)
            removed.append(item.name)
        except OSError:
            continue
    return removed


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def start_run(
    component: str,
    *,
    enabled: bool | None = None,
    root: Path | None = None,
    argv: Sequence[str] | None = None,
    content_root: Path | None = None,
    make_active: bool = True,
    **fields: Any,
) -> DiagnosticsRun:
    """Create a run directory and seed run.log/run.jsonl/env.json/identity.json.

    `enabled=None` consults `OPENBFME_DIAGNOSTICS`. Off is the default so a
    normal run behaves exactly as it does today. Any failure to prepare the log
    tree is announced on stderr and degrades to a no-op run: diagnostics must
    never be the reason a conversion dies.
    """

    if component not in COMPONENTS:
        raise ValueError(
            f"unknown diagnostics component {component!r}; expected one of "
            + ", ".join(COMPONENTS)
        )
    if enabled is None:
        enabled = diagnostics_enabled()
    if not enabled:
        return _DisabledRun(component)

    try:
        log_root = (root or default_log_root()).expanduser()
        log_root.mkdir(parents=True, exist_ok=True)
        log_root = log_root.resolve()
    except (OSError, ValueError) as exc:
        _warn(
            f"WARN could not create the log root ({redact_text(str(root or ''))}): "
            f"{redact_text(str(exc))} — diagnostics disabled, work continues"
        )
        return _DisabledRun(component)

    pruned: list[str] = []
    try:
        # Prune to one below the ceiling: this run is about to occupy the last
        # slot, so the steady state after start is exactly MAX_RUNS directories.
        pruned = prune_runs(
            log_root,
            keep=max(0, MAX_RUNS - 1),
            keep_errors=max(0, MAX_RUNS_WITH_ERROR - 1),
        )
    except Exception as exc:  # pragma: no cover - defensive
        _warn(f"WARN retention pass failed: {redact_text(str(exc))}")

    moment = _utc_now()
    base = f"{_stamp(moment)}-{component}-{os.getpid()}"
    directory = log_root / base
    try:
        # A second run of the same pid inside one second is possible in tests
        # and in fast scripted sequences; never clobber an existing run.
        suffix = 0
        while directory.exists():
            suffix += 1
            directory = log_root / f"{base}-{suffix}"
        directory.mkdir(parents=True)
    except OSError as exc:
        _warn(
            f"WARN could not create the run directory: {redact_text(str(exc))} "
            "— diagnostics disabled, work continues"
        )
        return _DisabledRun(component)

    run = DiagnosticsRun(component, directory=directory, enabled=True)
    if make_active:
        set_active_run(run)

    run.event(
        "run.begin",
        run_directory=str(directory),
        component=component,
        pid=os.getpid(),
        started_utc=_iso(moment),
        argv=list(argv) if argv is not None else list(sys.argv),
        pruned_runs=pruned,
        **fields,
    )
    run._write("env.json", collect_environment(argv))
    run.record_identity(content_root)
    _warn(f"diagnostics run directory: {redact_text(str(directory))}")
    return run
