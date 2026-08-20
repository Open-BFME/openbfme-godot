"""Structured conversion progress ledger for RotWK systems tooling.

Records every conversion unit (map, binding type, faction object, resource)
with status, timing, and failure detail. Emits a final summary with percentages.

Fail-open for I/O errors on the log sink so conversion is never aborted by
telemetry alone — but fatal conversion failures still raise from callers.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass, field
import json
import sys
import threading
import time
from pathlib import Path
from typing import Any, Mapping

from .util import write_json_atomic

LEDGER_SCHEMA = "openbfme.conversion-ledger"
LEDGER_SCHEMA_VERSION = 1


@dataclass
class ConversionLedger:
    """Thread-safe conversion accounting for one systems run."""

    run_id: str
    game: str
    install_root: str
    sink_path: Path | None = None
    started_s: float = field(default_factory=time.time)
    _lock: threading.Lock = field(default_factory=threading.Lock, repr=False)
    _events: list[dict[str, Any]] = field(default_factory=list, repr=False)
    _status_counts: Counter[str] = field(default_factory=Counter, repr=False)
    _kind_counts: Counter[str] = field(default_factory=Counter, repr=False)
    _failures: list[dict[str, Any]] = field(default_factory=list, repr=False)
    _sink_errors: list[str] = field(default_factory=list, repr=False)
    _sink_stream: Any = field(default=None, repr=False)
    strict_sink: bool = False

    def _write_event(self, event: dict[str, Any]) -> None:
        """Append one JSONL event, keeping the sink handle open between calls.

        This used to ``mkdir`` + ``open(mode="a")`` + ``close`` per event. At
        ~385 events per seven-faction convert that is ~1 150 filesystem calls on
        a Windows box with a scanner in the path, and it measured as most of the
        batch's ~38 s parent tail. The handle is opened lazily on the first
        event and flushed after every one, so durability is unchanged: a crash
        still leaves a complete JSONL up to the last recorded event.

        Caller holds ``self._lock``.
        """

        if self.sink_path is None:
            return
        try:
            if self._sink_stream is None:
                self.sink_path.parent.mkdir(parents=True, exist_ok=True)
                self._sink_stream = self.sink_path.open("a", encoding="utf-8")
            self._sink_stream.write(json.dumps(event, sort_keys=True) + "\n")
            self._sink_stream.flush()
        except OSError as exc:
            msg = f"could not append event: {exc}"
            self._sink_errors.append(msg)
            print(f"LEDGER ERROR {msg}", file=sys.stderr)
            self._sink_stream = None
            if self.strict_sink:
                raise

    def close_sink(self) -> None:
        with self._lock:
            stream, self._sink_stream = self._sink_stream, None
        if stream is not None:
            try:
                stream.close()
            except OSError:
                pass

    def record(
        self,
        *,
        kind: str,
        unit_id: str,
        status: str,
        detail: str = "",
        error: str | None = None,
        traceback_tail: str | None = None,
        metrics: Mapping[str, Any] | None = None,
        log_to_stderr: bool = True,
    ) -> None:
        """Append one conversion unit result.

        ``status`` should be one of: converted, planned, skipped, gap,
        failed, rejected, deferred, excluded, connected, disconnected.
        """

        event = {
            "ts": time.time(),
            "elapsed_s": round(time.time() - self.started_s, 3),
            "kind": kind,
            "unitId": unit_id,
            "status": status,
            "detail": detail,
        }
        if error:
            event["error"] = error[:2000]
        if traceback_tail:
            event["tracebackTail"] = traceback_tail[-4000:]
        if metrics:
            event["metrics"] = dict(metrics)
        with self._lock:
            self._events.append(event)
            self._status_counts[status] += 1
            self._kind_counts[kind] += 1
            if status in {"failed", "rejected", "cook-error", "cook-rejected"}:
                self._failures.append(event)
            self._write_event(event)
        if log_to_stderr:
            line = f"CONVERT [{status}] {kind} {unit_id}"
            if detail:
                line += f" — {detail}"
            if error:
                line += f" ERROR={error[:300]}"
            print(line, file=sys.stderr)

    def summary(self) -> dict[str, Any]:
        with self._lock:
            total_events = sum(self._status_counts.values())
            # Eligible unit statuses for % (exclude informational aggregate rollups
            # like map-level "planned" that are metrics-only companions).
            _ELIGIBLE = frozenset(
                {
                    "converted",
                    "cooked",
                    "cooked-and-connected",
                    "bound",
                    "connected",
                    "failed",
                    "rejected",
                    "cook-error",
                    "cook-rejected",
                    "gap",
                    "unbound",
                    "disconnected",
                    "cooked-but-starts-disconnected",
                    "deferred",
                    "excluded",
                    "skipped",
                }
            )
            eligible = sum(
                self._status_counts[s] for s in _ELIGIBLE if s in self._status_counts
            )
            converted_like = sum(
                self._status_counts[s]
                for s in (
                    "converted",
                    "cooked",
                    "cooked-and-connected",
                    "bound",
                    "connected",
                )
                if s in self._status_counts
            )
            failed_like = sum(
                self._status_counts[s]
                for s in (
                    "failed",
                    "rejected",
                    "cook-error",
                    "cook-rejected",
                    "gap",
                )
                if s in self._status_counts
            )
            gap_like = sum(
                self._status_counts[s]
                for s in (
                    "gap",
                    "unbound",
                    "disconnected",
                    "cooked-but-starts-disconnected",
                    "deferred",
                    "excluded",
                    "skipped",
                )
                if s in self._status_counts
            )
            denom = eligible if eligible > 0 else total_events
            pct = lambda n: (0.0 if denom == 0 else round(100.0 * n / denom, 2))
            return {
                "schema": LEDGER_SCHEMA,
                "schemaVersion": LEDGER_SCHEMA_VERSION,
                "runId": self.run_id,
                "game": self.game,
                "installRoot": self.install_root,
                "elapsed_s": round(time.time() - self.started_s, 2),
                "totalUnits": total_events,
                "eligibleUnits": eligible,
                "statusCounts": dict(sorted(self._status_counts.items())),
                "kindCounts": dict(sorted(self._kind_counts.items())),
                "percentConvertedLike": pct(converted_like),
                "percentFailedLike": pct(failed_like),
                "percentGapLike": pct(gap_like),
                "convertedLikeCount": converted_like,
                "failedLikeCount": failed_like,
                "gapLikeCount": gap_like,
                "failureCount": len(self._failures),
                "failures": list(self._failures[-50:]),
                "eventCount": len(self._events),
                "sinkErrorCount": len(self._sink_errors),
                "sinkErrors": list(self._sink_errors[-20:]),
            }

    def write_summary(self, path: Path) -> dict[str, Any]:
        # The run is over: release the JSONL handle before the summary lands so
        # nothing downstream sees a half-flushed sink.
        self.close_sink()
        summary = self.summary()
        path.parent.mkdir(parents=True, exist_ok=True)
        write_json_atomic(path, summary)
        print(
            "CONVERT SUMMARY "
            f"total={summary['totalUnits']} "
            f"converted%={summary['percentConvertedLike']} "
            f"failed%={summary['percentFailedLike']} "
            f"gap%={summary['percentGapLike']} "
            f"failures={summary['failureCount']} "
            f"report={path}",
            file=sys.stderr,
        )
        return summary


__all__ = ["ConversionLedger", "LEDGER_SCHEMA", "LEDGER_SCHEMA_VERSION"]
