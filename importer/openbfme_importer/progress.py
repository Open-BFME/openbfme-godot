"""Lightweight stage progress events for CLI and desktop GUI consumers.

Design:
- Overall ETA is driven by a known stage plan (weights), not by overwriting a
  single global unit counter mid-run.
- Within a stage, optional unit counts give finer ETA for that stage only.
- Progress is fail-open: never abort conversion.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import json
import os
import sys
import threading
import time
from pathlib import Path
from typing import Any, Callable, Mapping


_LOCK = threading.Lock()
_SINK: Path | None = None
_STARTED = 0.0
_STAGE = ""
_STAGE_STARTED = 0.0
_DONE_STAGES: list[str] = []
_STAGE_PLAN: list[str] = []
_NEXT_STAGES: list[str] = []
# Stage-local work units (reset when entering a stage that sets total_units).
_STAGE_TOTAL_UNITS = 0
_STAGE_DONE_UNITS = 0
# Optional overall unit budget if caller wants a single counter for the whole run.
_RUN_TOTAL_UNITS = 0
_RUN_DONE_UNITS = 0
_LISTENERS: list[Callable[[dict[str, Any]], None]] = []


@dataclass(slots=True)
class ProgressSnapshot:
    stage: str
    detail: str
    elapsed_s: float
    stage_elapsed_s: float
    done_units: int
    total_units: int
    done_stages: list[str] = field(default_factory=list)
    next_stages: list[str] = field(default_factory=list)
    eta_s: float | None = None
    fraction: float | None = None

    def as_dict(self) -> dict[str, Any]:
        return {
            "stage": self.stage,
            "detail": self.detail,
            "elapsed_s": round(self.elapsed_s, 2),
            "stage_elapsed_s": round(self.stage_elapsed_s, 2),
            "done_units": self.done_units,
            "total_units": self.total_units,
            "done_stages": list(self.done_stages),
            "next_stages": list(self.next_stages),
            "eta_s": None if self.eta_s is None else round(self.eta_s, 1),
            "fraction": self.fraction,
            "stage_done_units": self.done_units,
            "stage_total_units": self.total_units,
        }


def configure_progress(
    *,
    sink: Path | str | None = None,
    stages: list[str] | None = None,
    total_units: int = 0,
) -> None:
    """Reset and configure progress for one import run."""

    global _SINK, _STARTED, _STAGE, _STAGE_STARTED, _DONE_STAGES, _NEXT_STAGES
    global _STAGE_PLAN, _STAGE_TOTAL_UNITS, _STAGE_DONE_UNITS
    global _RUN_TOTAL_UNITS, _RUN_DONE_UNITS
    with _LOCK:
        _SINK = Path(sink) if sink else _env_sink()
        if _SINK is not None:
            _SINK.parent.mkdir(parents=True, exist_ok=True)
            _SINK.write_text("", encoding="utf-8")
        _STARTED = time.monotonic()
        _STAGE = "starting"
        _STAGE_STARTED = _STARTED
        _DONE_STAGES = []
        _STAGE_PLAN = list(stages or [])
        _NEXT_STAGES = list(_STAGE_PLAN)
        _STAGE_TOTAL_UNITS = 0
        _STAGE_DONE_UNITS = 0
        _RUN_TOTAL_UNITS = max(0, int(total_units))
        _RUN_DONE_UNITS = 0
    emit("starting", "import run configured", unit_delta=0)


def _env_sink() -> Path | None:
    raw = os.environ.get("OPENBFME_PROGRESS_FILE", "").strip()
    return Path(raw) if raw else None


def add_listener(callback: Callable[[dict[str, Any]], None]) -> None:
    with _LOCK:
        _LISTENERS.append(callback)


def clear_listeners() -> None:
    with _LOCK:
        _LISTENERS.clear()


def set_stage_plan(stages: list[str]) -> None:
    global _STAGE_PLAN, _NEXT_STAGES
    with _LOCK:
        _STAGE_PLAN = list(stages)
        _NEXT_STAGES = [item for item in stages if item not in _DONE_STAGES]


def set_total_units(total: int) -> None:
    """Set stage-local total units (does not clobber overall stage-plan ETA)."""

    global _STAGE_TOTAL_UNITS, _STAGE_DONE_UNITS
    with _LOCK:
        _STAGE_TOTAL_UNITS = max(0, int(total))
        _STAGE_DONE_UNITS = 0


def _compute_eta(
    *,
    elapsed: float,
    stage_elapsed: float,
) -> tuple[float | None, float | None]:
    """Return (eta_s, fraction) using stage plan + optional stage units."""

    fraction: float | None = None
    eta_s: float | None = None

    # Prefer known stage plan for overall progress (stable across stages).
    plan = _STAGE_PLAN
    if plan:
        current = _STAGE
        partial = 0.0
        if current == "complete":
            completed = len(plan)
        elif current in plan:
            completed = plan.index(current)
            if _STAGE_TOTAL_UNITS > 0:
                partial = min(1.0, _STAGE_DONE_UNITS / _STAGE_TOTAL_UNITS)
        else:
            # Unknown/extra stage (e.g. "starting"): count finished plan stages only.
            completed = sum(1 for name in plan if name in _DONE_STAGES)
        fraction = min(1.0, (completed + partial) / max(1, len(plan)))
        if 0.0 < fraction < 1.0 and elapsed > 0:
            eta_s = (elapsed / fraction) - elapsed
        elif fraction >= 1.0:
            eta_s = 0.0
        return eta_s, fraction

    # Fallback: run-level units if provided.
    if _RUN_TOTAL_UNITS > 0:
        fraction = min(1.0, _RUN_DONE_UNITS / _RUN_TOTAL_UNITS)
        if _RUN_DONE_UNITS > 0 and fraction < 1.0 and elapsed > 0:
            eta_s = (elapsed / _RUN_DONE_UNITS) * (_RUN_TOTAL_UNITS - _RUN_DONE_UNITS)
        return eta_s, fraction

    # Last resort: stage-local units only.
    if _STAGE_TOTAL_UNITS > 0:
        fraction = min(1.0, _STAGE_DONE_UNITS / _STAGE_TOTAL_UNITS)
        if _STAGE_DONE_UNITS > 0 and fraction < 1.0 and stage_elapsed > 0:
            eta_s = (stage_elapsed / _STAGE_DONE_UNITS) * (
                _STAGE_TOTAL_UNITS - _STAGE_DONE_UNITS
            )
        return eta_s, fraction

    return None, None


def emit(
    stage: str = "",
    detail: str = "",
    *,
    unit_delta: int = 0,
    total_units: int | None = None,
    extra: Mapping[str, Any] | None = None,
) -> None:
    """Publish one progress event.

    Pass ``stage=""`` to update detail/units without switching stages (safe for
    concurrent workers). ``total_units`` sets the *current stage* work budget.
    """

    global _STAGE, _STAGE_STARTED, _STAGE_DONE_UNITS, _STAGE_TOTAL_UNITS
    global _STARTED, _NEXT_STAGES, _RUN_DONE_UNITS
    now = time.monotonic()
    with _LOCK:
        if _STARTED <= 0:
            _STARTED = now
            _STAGE_STARTED = now
        if stage and stage != _STAGE:
            if _STAGE and _STAGE not in {"", "starting"} and _STAGE not in _DONE_STAGES:
                _DONE_STAGES.append(_STAGE)
            _STAGE = stage
            _STAGE_STARTED = now
            _STAGE_DONE_UNITS = 0
            _STAGE_TOTAL_UNITS = 0
            if stage in _NEXT_STAGES:
                _NEXT_STAGES = [item for item in _NEXT_STAGES if item != stage]
        if total_units is not None:
            _STAGE_TOTAL_UNITS = max(0, int(total_units))
            # Setting a stage budget does not imply prior units completed.
            if unit_delta == 0:
                _STAGE_DONE_UNITS = 0
        if unit_delta:
            _STAGE_DONE_UNITS = max(0, _STAGE_DONE_UNITS + int(unit_delta))
            if _RUN_TOTAL_UNITS > 0:
                _RUN_DONE_UNITS = max(0, _RUN_DONE_UNITS + int(unit_delta))
        elapsed = now - _STARTED
        stage_elapsed = now - _STAGE_STARTED
        eta_s, fraction = _compute_eta(elapsed=elapsed, stage_elapsed=stage_elapsed)
        next_stages = list(_NEXT_STAGES)
        event = {
            "ts": time.time(),
            "stage": _STAGE,
            "detail": detail,
            "elapsed_s": round(elapsed, 2),
            "stage_elapsed_s": round(stage_elapsed, 2),
            "done_units": _STAGE_DONE_UNITS,
            "total_units": _STAGE_TOTAL_UNITS,
            "run_done_units": _RUN_DONE_UNITS,
            "run_total_units": _RUN_TOTAL_UNITS,
            "done_stages": list(_DONE_STAGES),
            "next_stages": next_stages,
            "stage_plan": list(_STAGE_PLAN),
            "eta_s": None if eta_s is None else round(max(0.0, eta_s), 1),
            "fraction": None if fraction is None else round(fraction, 4),
        }
        if extra:
            event["extra"] = dict(extra)
        sink = _SINK
        listeners = list(_LISTENERS)

    if sink is not None:
        try:
            with sink.open("a", encoding="utf-8") as handle:
                handle.write(json.dumps(event, sort_keys=True) + "\n")
        except OSError:
            pass

    eta_text = (
        f"eta {int(event['eta_s'])}s" if event["eta_s"] is not None else "eta ?"
    )
    frac_text = ""
    if event["fraction"] is not None:
        frac_text = f" | {int(float(event['fraction']) * 100)}%"
    units_text = ""
    if _STAGE_TOTAL_UNITS > 0:
        units_text = f" | units {_STAGE_DONE_UNITS}/{_STAGE_TOTAL_UNITS}"
    skipped_text = ""
    if extra and extra.get("skipped"):
        skipped_text = " | skipped"
    line = (
        f"[progress] {event['stage']}: {detail or '…'} | "
        f"elapsed {int(elapsed)}s | {eta_text}{frac_text}{units_text}{skipped_text}"
    )
    if next_stages:
        line += f" | next: {next_stages[0]}"
    try:
        print(line, file=sys.stderr, flush=True)
    except OSError:
        pass

    for listener in listeners:
        try:
            listener(event)
        except Exception:
            pass


def snapshot() -> ProgressSnapshot:
    now = time.monotonic()
    with _LOCK:
        started = _STARTED if _STARTED > 0 else now
        stage_started = _STAGE_STARTED if _STAGE_STARTED > 0 else now
        elapsed = now - started
        stage_elapsed = now - stage_started
        eta_s, fraction = _compute_eta(elapsed=elapsed, stage_elapsed=stage_elapsed)
        return ProgressSnapshot(
            stage=_STAGE or "idle",
            detail="",
            elapsed_s=elapsed,
            stage_elapsed_s=stage_elapsed,
            done_units=_STAGE_DONE_UNITS,
            total_units=_STAGE_TOTAL_UNITS,
            done_stages=list(_DONE_STAGES),
            next_stages=list(_NEXT_STAGES),
            eta_s=eta_s,
            fraction=fraction,
        )


def complete(detail: str = "done") -> None:
    emit("complete", detail)
