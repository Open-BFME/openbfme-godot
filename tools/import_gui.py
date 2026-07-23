#!/usr/bin/env python3
"""OpenBFME Importer — advanced 5-stage desktop UI (power-user).

Honest stage rail (PENDING / ACTIVE / DONE / SKIPPED / FAILED), unit progress,
process-tree stop, log filters, success CTAs. Tkinter only.
"""

from __future__ import annotations

import os
from pathlib import Path
import queue
import re
import subprocess
import shutil
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
# Ensure importer package is importable when launched via pythonw.
_IMPORTER = REPO_ROOT / "importer"
if str(_IMPORTER) not in sys.path:
    sys.path.insert(0, str(_IMPORTER))

try:
    from openbfme_importer.paths import default_retail_install as _default_retail_install
except ImportError:  # pragma: no cover - degraded UI without package

    def _default_retail_install() -> Path:
        return Path(os.environ.get("BFME2_INSTALL", "")) or Path.cwd()


# Auto-detected from this machine; override with BFME2_INSTALL.
DEFAULT_INSTALL = _default_retail_install()
DEFAULT_STATE = Path(
    os.environ.get(
        "OPENBFME_IMPORT_ROOT",
        str(REPO_ROOT / ".private" / "retail-work"),
    )
)
DEFAULT_CONTENT = Path(
    os.environ.get(
        "OPENBFME_CONTENT_ROOT",
        str(REPO_ROOT / ".private" / "content-packs"),
    )
)
def _default_godot() -> Path:
    """Machine-neutral Godot guess; override with OPENBFME_GODOT."""

    configured = os.environ.get("OPENBFME_GODOT", "").strip()
    if configured:
        return Path(configured)
    on_path = shutil.which("godot")
    if on_path:
        return Path(on_path)
    return Path.home() / "Downloads" / "godot47" / "Godot_v4.7-stable_win64.exe"


GODOT_DEFAULT = _default_godot()

try:
    from openbfme_importer.dependency_check import (
        blocking_message,
        check_dependencies,
        format_dependency_report,
    )
except ImportError:  # pragma: no cover - degraded UI without package
    check_dependencies = None  # type: ignore[assignment]
    format_dependency_report = None  # type: ignore[assignment]
    blocking_message = None  # type: ignore[assignment]
FACTIONS = ("men", "elves", "dwarves", "isengard", "mordor", "wild")
MODES = (
    ("Plan faction (gaps only)", "faction-plan"),
    ("Convert faction objects", "faction-convert"),
    ("Publish faction → slice", "publish-faction"),
    ("Build Men pack → Godot", "men-build"),
)

# Work stages only (Complete is terminal state, not a work step).
STAGE_PLANS: dict[str, tuple[dict[str, str], ...]] = {
    "faction-plan": (
        {
            "id": "catalog",
            "title": "1 · Catalog",
            "blurb": "Index install & policy",
            "tip": "Loads/builds the BFME2 archive catalog and checks size/mtime "
            "(and payload samples). Deep full-MD5 only if OPENBFME_CATALOG_DEEP=1.",
        },
        {
            "id": "assets",
            "title": "2 · Assets",
            "blurb": "Effective tree ready",
            "tip": "Ensures the effective-assets tree exists. SKIPPED when the "
            "manifest is already present (warm run).",
        },
        {
            "id": "census",
            "title": "3 · Census",
            "blurb": "Faction dependency graph",
            "tip": "Walks command-reachable objects for the selected faction from "
            "PlayerTemplate / command sets (no payload conversion yet).",
        },
        {
            "id": "plan",
            "title": "4 · Plan",
            "blurb": "Descriptor / gap report",
            "tip": "Builds the faction import plan JSON: which objects are "
            "descriptor-ready vs converter-gap. Does not cook assets.",
        },
    ),
    "faction-convert": (
        {
            "id": "catalog",
            "title": "1 · Catalog",
            "blurb": "Index install & policy",
            "tip": "Loads/builds the BFME2 archive catalog and verifies install "
            "archives against pinned policy.",
        },
        {
            "id": "assets",
            "title": "2 · Assets",
            "blurb": "Extract / verify tree",
            "tip": "Materializes the winning virtual asset tree under private "
            "cache. SKIPPED if already extracted.",
        },
        {
            "id": "census",
            "title": "3 · Census",
            "blurb": "Faction dependency graph",
            "tip": "Resolves playable faction objects, command sets, audio/image "
            "leaves, and structure composites from retail INI.",
        },
        {
            "id": "convert",
            "title": "4 · Convert",
            "blurb": "Objects · closure · recipes",
            "tip": "Compiles each supported unit/structure: descriptors, visual "
            "closure, pack recipes. Watch UNITS N/M — this is usually the longest stage.",
        },
    ),
    "publish-faction": (
        {
            "id": "catalog",
            "title": "1 · Catalog",
            "blurb": "Index install & policy",
            "tip": "Loads the install catalog used to resolve pack resources.",
        },
        {
            "id": "compose",
            "title": "2 · Compose",
            "blurb": "Coverage → pack profile",
            "tip": "Folds converted playable unit/structure/spellbook artifacts "
            "into a faction-slice profile the cook can assemble.",
        },
        {
            "id": "extract",
            "title": "3 · Extract",
            "blurb": "Sources · tool attest",
            "tip": "Attests pinned tools and extracts only the sources the "
            "composed profile requires.",
        },
        {
            "id": "cook",
            "title": "4 · Cook",
            "blurb": "W3D · audio · textures",
            "tip": "Converts models, audio, and textures. Cached when inputs "
            "are unchanged.",
        },
        {
            "id": "pack",
            "title": "5 · Pack",
            "blurb": "Assemble · audit · select",
            "tip": "Builds the Godot pack, audits it, and updates selection.json "
            "so the vertical slice loads the faction automatically.",
        },
    ),
    "men-build": (
        {
            "id": "catalog",
            "title": "1 · Catalog",
            "blurb": "Index install & policy",
            "tip": "Loads the install catalog for the Men pack profile selection.",
        },
        {
            "id": "extract",
            "title": "2 · Extract",
            "blurb": "Sources · tool attest",
            "tip": "Attests pinned tools (Blender/ffmpeg) and extracts only the "
            "source files selected by the pack profile.",
        },
        {
            "id": "cook",
            "title": "3 · Cook",
            "blurb": "W3D · audio · textures",
            "tip": "Converts models (Blender multi-job), audio (ffmpeg), and "
            "textures (Pillow). Uses content caches when inputs are unchanged.",
        },
        {
            "id": "pack",
            "title": "4 · Pack",
            "blurb": "Assemble · audit · publish",
            "tip": "Builds the Godot pack, audits provenance hashes, and "
            "optionally publishes selection.json for the retail slice.",
        },
    ),
}

MODE_TIPS = {
    "faction-plan": "Fast: discover converter gaps for one faction without cooking "
    "W3D/audio/textures. Output: private reports/faction-import/<faction>-plan.json",
    "faction-convert": "Compile playable unit/structure descriptors and recipes for "
    "one faction. Longest stage is Convert (per-object visual closure).",
    "publish-faction": "Compose converted faction coverage into a pack profile, "
    "cook models/audio/textures, audit, and update selection.json so the Godot "
    "vertical slice loads the faction automatically (no manual hardwire).",
    "men-build": "Legacy: cook the Men host pack (men-fords-v0) only. Prefer "
    "'Publish faction → slice' after convert so playable units/structures reach Godot.",
}

FACTION_TIP = (
    "BFME2 playable side used for plan/convert/publish. Hidden for legacy Men "
    "host-pack build only."
)
INSTALL_TIP = (
    "Path to your BFME2 1.06 install (folder with lotrbfme2.exe / *.big). "
    "Never written by the importer — read-only source."
)

STAGE_ALIASES: dict[str, dict[str, str]] = {
    "faction-plan": {
        "starting": "catalog",
        "catalog": "catalog",
        "extract-assets": "assets",
        "census": "census",
        "faction-plan": "plan",
        "complete": "plan",
    },
    "faction-convert": {
        "starting": "catalog",
        "catalog": "catalog",
        "extract-assets": "assets",
        "census": "census",
        "faction-plan": "convert",
        "faction-convert": "convert",
        "complete": "convert",
    },
    "publish-faction": {
        "starting": "catalog",
        "catalog": "catalog",
        "compose": "compose",
        "attest": "extract",
        "extract": "extract",
        "convert": "cook",
        "convert-assets": "cook",
        "blender-w3d": "cook",
        "media": "cook",
        "assemble": "pack",
        "publish": "pack",
        "complete": "pack",
    },
    "men-build": {
        "starting": "catalog",
        "catalog": "catalog",
        "attest": "extract",
        "extract": "extract",
        "convert": "cook",
        "convert-assets": "cook",
        "blender-w3d": "cook",
        "media": "cook",
        "assemble": "pack",
        "publish": "pack",
        "complete": "pack",
    },
}

PROGRESS_LINE = re.compile(
    r"^\[progress\]\s+(?P<stage>[^:]+):\s+(?P<detail>.*?)\s+\|\s+"
    r"elapsed\s+(?P<elapsed>\d+)s\s+\|\s+eta\s+(?P<eta>[^|]+?)"
    r"(?:\s+\|\s+(?P<pct>\d+)%)?"
    r"(?:\s+\|\s+units\s+(?P<units_done>\d+)/(?P<units_total>\d+))?"
    r"(?:\s+\|\s+skipped)?"
    r"(?:\s+\|\s+next:\s+(?P<next>.+))?$"
)
SKIPPED_LINE = re.compile(r"\|\s+skipped(?:\s+\||$)")
REPORT_IN_DETAIL = re.compile(r"report=([^\s]+)")

C = {
    "bg": "#0a0c10",
    "panel": "#11151c",
    "panel2": "#181e28",
    "border": "#252c38",
    "text": "#f2f4f7",
    "muted": "#7d8a9e",
    "dim": "#4a5568",
    "accent": "#3b82f6",
    "accent_soft": "#1e3a5f",
    "ok": "#22c55e",
    "ok_soft": "#14532d",
    "warn": "#eab308",
    "warn_soft": "#422006",
    "err": "#ef4444",
    "err_soft": "#450a0a",
    "log_bg": "#07090d",
    "input": "#0e1218",
    "track": "#1a2030",
    "white": "#ffffff",
}


def _python() -> Path:
    pinned = DEFAULT_STATE / "tools" / "python-3.12-env" / "Scripts" / "python.exe"
    if pinned.is_file():
        return pinned
    return Path(sys.executable)


def _importer_cmd() -> list[str]:
    return [
        str(_python()),
        str(REPO_ROOT / "tools" / "openbfme_import.py"),
        "--state-root",
        str(DEFAULT_STATE),
    ]


def _game_arguments(install: Path) -> list[str]:
    """Route a chosen install to its edition, mirroring onboard.classify_install.

    RotWK is the content baseline and the importer's default, so this only
    speaks up for the case the default would get wrong: a flat BFME2 tree,
    identified by its own executable and the absence of the RotWK one. A
    layered RotWK root carries no executable at its top level, so "undetected"
    correctly falls through to the rotwk default. The importer still performs
    the authoritative fail-closed identity check either way.
    """

    try:
        if (install / "lotrbfme2.exe").is_file() and not (
            install / "lotrbfme2ep1.exe"
        ).is_file():
            return ["--game", "bfme2"]
    except OSError:
        pass
    return []


def _fmt_duration(seconds: float | int) -> str:
    total = max(0, int(seconds))
    if total < 60:
        return f"{total}s"
    minutes, sec = divmod(total, 60)
    if minutes < 60:
        return f"{minutes}m {sec:02d}s"
    hours, minutes = divmod(minutes, 60)
    return f"{hours}h {minutes:02d}m"


def _parse_eta(raw: str) -> str:
    if raw in {"?", "—", "", "…"}:
        return "—"
    digits = re.sub(r"[^0-9]", "", raw)
    return _fmt_duration(int(digits)) if digits else raw


def _kill_tree(pid: int) -> None:
    if sys.platform == "win32":
        subprocess.run(
            ["taskkill", "/PID", str(pid), "/T", "/F"],
            capture_output=True,
            check=False,
        )
        return
    try:
        os.killpg(os.getpgid(pid), 15)
    except (ProcessLookupError, PermissionError, AttributeError):
        try:
            os.kill(pid, 15)
        except (ProcessLookupError, PermissionError):
            pass


class ToolTip:
    """Dark delayed tooltip for power-user hover help."""

    def __init__(self, widget: tk.Misc, text: str, *, delay_ms: int = 450) -> None:
        self.widget = widget
        self.text = text
        self.delay_ms = delay_ms
        self._after: str | None = None
        self._tip: tk.Toplevel | None = None
        widget.bind("<Enter>", self._schedule, add="+")
        widget.bind("<Leave>", self._hide, add="+")
        widget.bind("<ButtonPress>", self._hide, add="+")

    def set_text(self, text: str) -> None:
        self.text = text

    def _schedule(self, _event: object | None = None) -> None:
        self._cancel()
        if not self.text.strip():
            return
        self._after = self.widget.after(self.delay_ms, self._show)

    def _cancel(self) -> None:
        if self._after is not None:
            try:
                self.widget.after_cancel(self._after)
            except tk.TclError:
                pass
            self._after = None

    def _hide(self, _event: object | None = None) -> None:
        self._cancel()
        if self._tip is not None:
            try:
                self._tip.destroy()
            except tk.TclError:
                pass
            self._tip = None

    def _show(self) -> None:
        self._after = None
        if self._tip is not None or not self.text.strip():
            return
        try:
            if not self.widget.winfo_exists():
                return
        except tk.TclError:
            return
        x = self.widget.winfo_rootx() + 16
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 6
        tip = tk.Toplevel(self.widget)
        tip.wm_overrideredirect(True)
        tip.wm_attributes("-topmost", True)
        tip.configure(bg=C["border"])
        frame = tk.Frame(tip, bg=C["panel2"], padx=1, pady=1)
        frame.pack(fill="both", expand=True)
        label = tk.Label(
            frame,
            text=self.text,
            bg=C["panel2"],
            fg=C["text"],
            font=("Segoe UI", 9),
            justify="left",
            wraplength=320,
            padx=10,
            pady=8,
        )
        label.pack()
        tip.update_idletasks()
        # Keep on-screen if near bottom edge.
        try:
            screen_h = tip.winfo_screenheight()
            tip_h = tip.winfo_height()
            if y + tip_h > screen_h - 8:
                y = self.widget.winfo_rooty() - tip_h - 6
        except tk.TclError:
            pass
        tip.wm_geometry(f"+{x}+{y}")
        self._tip = tip


def tip(widget: tk.Misc, text: str) -> ToolTip:
    """Attach a hover description to one widget (and return the tooltip)."""

    return ToolTip(widget, text)


def tip_tree(widget: tk.Misc, text: str) -> None:
    """Attach the same tip to a widget and all current children."""

    tip(widget, text)
    for child in widget.winfo_children():
        tip_tree(child, text)


def _wheel_delta(event: Any) -> int:
    delta = int(getattr(event, "delta", 0) or 0)
    if delta == 0 and getattr(event, "num", None) in (4, 5):
        delta = 120 if event.num == 4 else -120
    # Windows often reports ±120; some devices report smaller steps.
    if delta == 0:
        return 0
    if abs(delta) < 120:
        return -1 if delta > 0 else 1
    return int(-1 * (delta / 120))


def _bind_mousewheel_tree(root: tk.Misc, target: tk.Misc) -> None:
    """Bind wheel on *root* and all current descendants to scroll *target*."""

    def _on_wheel(event: Any) -> str:
        steps = _wheel_delta(event)
        if steps:
            try:
                target.yview_scroll(steps, "units")
            except tk.TclError:
                pass
        return "break"

    def _walk(widget: tk.Misc) -> None:
        for seq in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
            widget.bind(seq, _on_wheel, add="+")
        for child in widget.winfo_children():
            _walk(child)

    _walk(root)


class ScrollFrame(tk.Frame):
    """Vertical scroll container (Canvas + inner frame + always-usable wheel)."""

    def __init__(self, parent: tk.Misc, *, bg: str, height: int | None = None) -> None:
        super().__init__(parent, bg=bg, highlightthickness=0)
        self.canvas = tk.Canvas(
            self,
            bg=bg,
            highlightthickness=0,
            bd=0,
            height=height if height is not None else 120,
        )
        self.scrollbar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.inner = tk.Frame(self.canvas, bg=bg)
        self._window = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.canvas.configure(yscrollcommand=self._on_scrollregion_set)

        self.canvas.pack(side="left", fill="both", expand=True)
        self.scrollbar.pack(side="right", fill="y")

        self.inner.bind("<Configure>", self._on_inner_configure)
        self.canvas.bind("<Configure>", self._on_canvas_configure)
        # Direct binds (not bind_all) so log + pipeline can both scroll.
        for seq in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
            self.canvas.bind(seq, self._on_wheel)
            self.inner.bind(seq, self._on_wheel)
            self.bind(seq, self._on_wheel)

    def _on_wheel(self, event: Any) -> str:
        steps = _wheel_delta(event)
        if steps:
            self.canvas.yview_scroll(steps, "units")
        return "break"

    def _on_scrollregion_set(self, first: str, last: str) -> None:
        self.scrollbar.set(first, last)
        # Hide useless scrollbar only when content fits; keep wheel working either way.
        try:
            if float(first) <= 0.0 and float(last) >= 1.0:
                self.scrollbar.pack_forget()
            elif not self.scrollbar.winfo_ismapped():
                self.scrollbar.pack(side="right", fill="y")
        except (TypeError, ValueError):
            pass

    def _on_inner_configure(self, _event: object | None = None) -> None:
        bbox = self.canvas.bbox("all")
        if bbox is not None:
            self.canvas.configure(scrollregion=bbox)

    def _on_canvas_configure(self, event: Any) -> None:
        self.canvas.itemconfigure(self._window, width=max(1, int(event.width)))

    def rebind_children(self) -> None:
        """Call after adding rows so wheel works over labels/buttons."""

        _bind_mousewheel_tree(self.inner, self.canvas)
        self._on_inner_configure()


class StageRow:
    def __init__(
        self,
        parent: tk.Misc,
        index: int,
        title: str,
        blurb: str,
        *,
        tip_text: str = "",
    ) -> None:
        self.index = index
        self.title = title
        self.blurb = blurb
        self.tip_text = tip_text
        self.state = "pending"
        self.detail = ""
        self.duration_s: float | None = None

        self.frame = tk.Frame(parent, bg=C["panel"], highlightthickness=0)
        self.frame.pack(fill="x", pady=3)

        self.num = tk.Label(
            self.frame,
            text=str(index),
            width=3,
            bg=C["track"],
            fg=C["muted"],
            font=("Segoe UI Semibold", 11),
            pady=8,
        )
        self.num.pack(side="left", padx=(0, 12))

        mid = tk.Frame(self.frame, bg=C["panel"])
        mid.pack(side="left", fill="x", expand=True)
        self.title_lbl = tk.Label(
            mid, text=title, bg=C["panel"], fg=C["text"], font=("Segoe UI Semibold", 11), anchor="w"
        )
        self.title_lbl.pack(fill="x")
        self.blurb_lbl = tk.Label(
            mid, text=blurb, bg=C["panel"], fg=C["muted"], font=("Segoe UI", 9), anchor="w"
        )
        self.blurb_lbl.pack(fill="x")
        mono = ("Cascadia Mono", 8) if sys.platform == "win32" else ("Consolas", 8)
        self.detail_lbl = tk.Label(
            mid, text="", bg=C["panel"], fg=C["dim"], font=mono, anchor="w"
        )
        self.detail_lbl.pack(fill="x")

        self.time_lbl = tk.Label(
            self.frame, text="—", bg=C["panel"], fg=C["dim"], font=("Segoe UI", 9), width=8, anchor="e"
        )
        self.time_lbl.pack(side="right", padx=(8, 0))

        self.status_lbl = tk.Label(
            self.frame,
            text="PENDING",
            bg=C["track"],
            fg=C["muted"],
            font=("Segoe UI Semibold", 8),
            padx=8,
            pady=3,
        )
        self.status_lbl.pack(side="right", padx=(8, 0))

        full_tip = tip_text or blurb
        for w in (
            self.frame,
            self.num,
            mid,
            self.title_lbl,
            self.blurb_lbl,
            self.detail_lbl,
            self.time_lbl,
            self.status_lbl,
        ):
            tip(w, full_tip)

    def set_state(
        self,
        state: str,
        *,
        detail: str = "",
        duration_s: float | None = None,
    ) -> None:
        self.state = state
        if detail:
            self.detail = detail
            shown = detail if len(detail) < 80 else detail[:77] + "…"
            self.detail_lbl.configure(text=shown)
        if duration_s is not None:
            self.duration_s = duration_s
            self.time_lbl.configure(text=_fmt_duration(duration_s), fg=C["muted"])

        styles = {
            "pending": (C["track"], C["muted"], "PENDING", C["text"], C["muted"]),
            "active": (C["accent_soft"], C["accent"], "ACTIVE", C["accent"], C["text"]),
            "done": (C["ok_soft"], C["ok"], "DONE", C["ok"], C["muted"]),
            "skipped": (C["warn_soft"], C["warn"], "SKIPPED", C["warn"], C["muted"]),
            "failed": (C["err_soft"], C["err"], "FAILED", C["err"], C["err"]),
        }
        bg, fg, label, title_fg, blurb_fg = styles.get(state, styles["pending"])
        self.num.configure(bg=bg, fg=fg)
        self.status_lbl.configure(bg=bg, fg=fg, text=label)
        self.title_lbl.configure(fg=title_fg)
        self.blurb_lbl.configure(fg=blurb_fg)
        if state == "active" and self.duration_s is None:
            self.time_lbl.configure(text="…", fg=C["accent"])
        if state == "pending":
            self.time_lbl.configure(text="—", fg=C["dim"])
            if not detail:
                self.detail_lbl.configure(text="")
        if state == "skipped":
            self.time_lbl.configure(text="—", fg=C["warn"])


class ImportGui(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("OpenBFME Importer · Advanced")
        self.geometry("1020x740")
        self.minsize(900, 640)
        self.configure(bg=C["bg"])

        self._proc: subprocess.Popen[str] | None = None
        self._reader: threading.Thread | None = None
        self._q: queue.Queue[tuple[str, object]] = queue.Queue()
        self._run_started = 0.0
        self._mode_key = "faction-convert"
        self._stage_started: dict[str, float] = {}
        self._stage_rows: dict[str, StageRow] = {}
        self._active_stage_id = ""
        self._seen_stages: set[str] = set()
        self._last_report: Path | None = None
        self._log_filter = tk.StringVar(value="all")
        self._log_buffer: list[tuple[str, str]] = []

        self.install_var = tk.StringVar(value=str(DEFAULT_INSTALL))
        self.faction_var = tk.StringVar(value="men")
        self.mode_var = tk.StringVar(value="Convert faction objects")
        # Default OFF: shipping-grade full hash audit. Opt in for faster cooks.
        self.dev_var = tk.BooleanVar(value=False)
        self.deps_var = tk.StringVar(value="Dependencies · checking…")
        self._deps_report: dict[str, Any] | None = None
        self.hero_stage = tk.StringVar(value="Ready")
        self.hero_detail = tk.StringVar(
            value="Select install and mode — Start when ready."
        )
        self.elapsed_var = tk.StringVar(value="0s")
        self.eta_var = tk.StringVar(value="—")
        self.next_var = tk.StringVar(value="—")
        self.pct_var = tk.StringVar(value="0%")
        self.units_var = tk.StringVar(value="—")
        self.backend_var = tk.StringVar(value="backend · idle")
        self.progress_var = tk.DoubleVar(value=0.0)
        self._mode_labels = {label: key for label, key in MODES}

        self._setup_style()
        self._build()
        self._rebuild_stage_rail()
        self._sync_mode_controls()
        self.mode_var.trace_add("write", lambda *_: self._on_mode_change())
        self.mode_var.trace_add("write", lambda *_: self.after(50, self._run_dependency_check))
        self.install_var.trace_add(
            "write", lambda *_: self.after(400, self._run_dependency_check)
        )
        self.bind("<Return>", lambda _e: self._start())
        self.bind("<Escape>", lambda _e: self._stop())
        self.after(80, self._drain_queue)
        # Background dependency preflight shortly after first paint.
        self.after(200, self._run_dependency_check)

    def _setup_style(self) -> None:
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure(".", background=C["bg"], foreground=C["text"], font=("Segoe UI", 10))
        style.configure("TFrame", background=C["bg"])
        style.configure("TLabel", background=C["bg"], foreground=C["text"])
        # Prefer tk.Button for actions — ttk + dark clam often paints blank labels
        # on Windows (fg/bg theme map ignored).
        style.configure(
            "Horizontal.TProgressbar",
            troughcolor=C["track"],
            background=C["accent"],
            bordercolor=C["border"],
            lightcolor=C["accent"],
            darkcolor=C["accent"],
            thickness=6,
        )
        style.configure(
            "TCombobox",
            fieldbackground=C["input"],
            background=C["panel2"],
            foreground=C["text"],
            arrowcolor=C["text"],
        )
        style.map(
            "TCombobox",
            fieldbackground=[("readonly", C["input"])],
            foreground=[("readonly", C["text"])],
            selectbackground=[("readonly", C["panel2"])],
            selectforeground=[("readonly", C["text"])],
        )
        style.configure("TEntry", fieldbackground=C["input"], foreground=C["text"], insertcolor=C["text"])
        style.configure(
            "TRadiobutton",
            background=C["panel"],
            foreground=C["muted"],
            font=("Segoe UI", 8),
        )
        style.map("TRadiobutton", background=[("active", C["panel"])], foreground=[("selected", C["text"])])

    def _panel(self, parent: tk.Misc, **pack: Any) -> tk.Frame:
        shell = tk.Frame(parent, bg=C["border"], bd=0)
        shell.pack(**pack)
        inner = tk.Frame(shell, bg=C["panel"], bd=0)
        inner.pack(fill="both", expand=True, padx=1, pady=1)
        return inner

    def _btn(
        self,
        parent: tk.Misc,
        text: str,
        command: Any,
        *,
        kind: str = "default",
        hover: str = "",
        **pack: Any,
    ) -> tk.Button:
        """tk.Button with explicit colors (visible on dark Windows themes)."""

        styles = {
            "default": {
                "bg": C["panel2"],
                "fg": C["text"],
                "activebackground": C["border"],
                "activeforeground": C["white"],
                "disabledforeground": C["dim"],
                "font": ("Segoe UI", 10),
                "padx": 12,
                "pady": 6,
            },
            "accent": {
                "bg": C["accent"],
                "fg": C["white"],
                "activebackground": "#2563eb",
                "activeforeground": C["white"],
                "disabledforeground": "#93c5fd",
                "font": ("Segoe UI Semibold", 10),
                "padx": 18,
                "pady": 8,
            },
            "danger": {
                "bg": "#5b2430",
                "fg": C["white"],
                "activebackground": "#7f1d1d",
                "activeforeground": C["white"],
                "disabledforeground": "#fca5a5",
                "font": ("Segoe UI", 10),
                "padx": 12,
                "pady": 6,
            },
            "success": {
                "bg": C["ok_soft"],
                "fg": C["ok"],
                "activebackground": "#166534",
                "activeforeground": C["white"],
                "disabledforeground": C["dim"],
                "font": ("Segoe UI Semibold", 10),
                "padx": 12,
                "pady": 6,
            },
        }
        s = styles.get(kind, styles["default"])
        btn = tk.Button(
            parent,
            text=text,
            command=command,
            relief="flat",
            bd=0,
            highlightthickness=0,
            cursor="hand2",
            **s,
        )
        if hover:
            tip(btn, hover)
        if pack:
            btn.pack(**pack)
        return btn

    def _build(self) -> None:
        root = tk.Frame(self, bg=C["bg"])
        root.pack(fill="both", expand=True, padx=20, pady=16)

        # Footer first so Start/Stop stay visible; body fills remainder.
        foot = tk.Frame(root, bg=C["bg"])
        foot.pack(side="bottom", fill="x", pady=(14, 0))
        self.start_btn = self._btn(
            foot,
            "Start",
            self._start,
            kind="accent",
            hover="Run the selected mode against the install path (Enter). "
            "Preflights for a BFME2-looking folder first.",
            side="left",
        )
        self.stop_btn = self._btn(
            foot,
            "Stop",
            self._stop,
            kind="danger",
            hover="Kill the importer process tree, including Blender/ffmpeg children (Esc).",
            side="left",
            padx=(10, 0),
        )
        self.stop_btn.configure(state="disabled")
        self._btn(
            foot,
            "Reports folder",
            self._open_progress,
            kind="default",
            hover="Open private reports/ (faction plans, coverage JSON, progress logs).",
            side="left",
            padx=(10, 0),
        )
        self._btn(
            foot,
            "Quit",
            self.destroy,
            kind="default",
            hover="Close the importer UI. Does not stop a running job — press Stop first.",
            side="right",
        )
        short_state = DEFAULT_STATE.name
        try:
            short_state = f"…/{DEFAULT_STATE.parent.name}/{DEFAULT_STATE.name}"
        except Exception:
            pass
        state_lbl = tk.Label(foot, text=short_state, bg=C["bg"], fg=C["dim"], font=("Segoe UI", 8))
        state_lbl.pack(side="right", padx=(0, 16))
        tip(
            state_lbl,
            f"Private importer state root (caches, packs, reports):\n{DEFAULT_STATE}",
        )

        head = tk.Frame(root, bg=C["bg"])
        head.pack(side="top", fill="x", pady=(0, 14))
        lbl_imp = tk.Label(
            head, text="IMPORTER", bg=C["bg"], fg=C["dim"], font=("Segoe UI Semibold", 9)
        )
        lbl_imp.pack(anchor="w")
        tip(lbl_imp, "Retail-safe BFME2 → Godot content pipeline (no retail bytes leave private state).")
        lbl_title = tk.Label(
            head, text="OpenBFME", bg=C["bg"], fg=C["text"], font=("Segoe UI Semibold", 22)
        )
        lbl_title.pack(anchor="w")
        tip(lbl_title, "OpenBFME advanced importer — plan factions, convert objects, or build the Men pack.")
        lbl_sub = tk.Label(
            head,
            text="Advanced · 4 work stages · honest progress · Enter start · Esc stop · hover for help",
            bg=C["bg"],
            fg=C["muted"],
            font=("Segoe UI", 10),
        )
        lbl_sub.pack(anchor="w", pady=(2, 0))
        tip(lbl_sub, "Hover any control or pipeline stage for a full description.")

        body = tk.Frame(root, bg=C["bg"])
        body.pack(fill="both", expand=True)

        left = tk.Frame(body, bg=C["bg"], width=360)
        left.pack(side="left", fill="both", padx=(0, 14))
        left.pack_propagate(False)
        right = tk.Frame(body, bg=C["bg"])
        right.pack(side="left", fill="both", expand=True)

        # Config
        cfg = self._panel(left, fill="x", pady=(0, 12))
        cpad = tk.Frame(cfg, bg=C["panel"])
        cpad.pack(fill="x", padx=14, pady=14)
        cfg_hdr = tk.Label(
            cpad, text="CONFIGURATION", bg=C["panel"], fg=C["dim"], font=("Segoe UI Semibold", 8)
        )
        cfg_hdr.pack(anchor="w")
        tip(cfg_hdr, "Inputs for this run. Retail install is read-only; outputs stay under private state.")

        inst_lbl = tk.Label(
            cpad, text="Install path", bg=C["panel"], fg=C["muted"], font=("Segoe UI", 9)
        )
        inst_lbl.pack(anchor="w", pady=(10, 2))
        tip(inst_lbl, INSTALL_TIP)
        path_row = tk.Frame(cpad, bg=C["panel"])
        path_row.pack(fill="x")
        install_entry = ttk.Entry(path_row, textvariable=self.install_var)
        install_entry.pack(side="left", fill="x", expand=True, padx=(0, 8))
        tip(install_entry, INSTALL_TIP)
        self._btn(
            path_row,
            "Browse",
            self._browse,
            kind="default",
            hover="Choose the BFME2 install folder (contains lotrbfme2.exe or *.big).",
            side="right",
        )

        self.mode_frame = tk.Frame(cpad, bg=C["panel"])
        self.mode_frame.pack(fill="x", pady=(12, 0))
        mode_lbl = tk.Label(
            self.mode_frame, text="Mode", bg=C["panel"], fg=C["muted"], font=("Segoe UI", 9)
        )
        mode_lbl.pack(anchor="w")
        tip(
            mode_lbl,
            "Plan = gaps only · Convert = compile faction objects · "
            "Men pack = cook Godot pack for the vertical slice.",
        )
        self.mode_combo = ttk.Combobox(
            self.mode_frame,
            textvariable=self.mode_var,
            values=[label for label, _ in MODES],
            state="readonly",
            width=34,
        )
        self.mode_combo.pack(anchor="w", pady=(2, 0))
        tip(self.mode_combo, MODE_TIPS.get("faction-convert", ""))
        self.mode_var.trace_add("write", lambda *_: self._update_mode_tip())

        self.faction_frame = tk.Frame(cpad, bg=C["panel"])
        self.faction_frame.pack(fill="x", pady=(12, 0))
        fac_lbl = tk.Label(
            self.faction_frame, text="Faction", bg=C["panel"], fg=C["muted"], font=("Segoe UI", 9)
        )
        fac_lbl.pack(anchor="w")
        tip(fac_lbl, FACTION_TIP)
        fac_combo = ttk.Combobox(
            self.faction_frame,
            textvariable=self.faction_var,
            values=list(FACTIONS),
            state="readonly",
            width=14,
        )
        fac_combo.pack(anchor="w", pady=(2, 0))
        tip(fac_combo, FACTION_TIP)

        self.dev_frame = tk.Frame(cpad, bg=C["panel"])
        self.dev_frame.pack(fill="x", pady=(10, 0))
        dev_cb = ttk.Checkbutton(
            self.dev_frame,
            text="Developer mode (faster cook)",
            variable=self.dev_var,
        )
        dev_cb.pack(anchor="w")
        tip(
            dev_cb,
            "Passes --dev: PNG level 6, soft Blender re-attest, light pack audit "
            "(size-only), object DDC + shared media/W3D cache. Leave on for "
            "iteration; uncheck for shipping-grade verification.",
        )

        deps_frame = tk.Frame(cpad, bg=C["panel"])
        deps_frame.pack(fill="x", pady=(12, 0))
        deps_hdr = tk.Label(
            deps_frame,
            text="DEPENDENCIES",
            bg=C["panel"],
            fg=C["dim"],
            font=("Segoe UI Semibold", 8),
        )
        deps_hdr.pack(anchor="w")
        tip(
            deps_hdr,
            "BFME2 1.06 install, pinned Python/Pillow, Blender 4.2, FFmpeg, "
            "OpenSAGE W3D plugin, Godot (for launch). Checked before Start.",
        )
        deps_row = tk.Frame(deps_frame, bg=C["panel"])
        deps_row.pack(fill="x", pady=(4, 0))
        self.deps_lbl = tk.Label(
            deps_row,
            textvariable=self.deps_var,
            bg=C["panel"],
            fg=C["muted"],
            font=("Segoe UI", 9),
            justify="left",
            anchor="w",
            wraplength=300,
        )
        self.deps_lbl.pack(side="left", fill="x", expand=True)
        self._btn(
            deps_row,
            "Check",
            self._run_dependency_check,
            kind="default",
            hover="Re-scan install + tools (fast). Use CLI doctor --deep for full tree hashes.",
            side="right",
        )

        # Stages (scrollable — was clipping with no way to scroll down)
        stages_panel = self._panel(left, fill="both", expand=True)
        sp = tk.Frame(stages_panel, bg=C["panel"])
        sp.pack(fill="both", expand=True, padx=14, pady=14)
        top = tk.Frame(sp, bg=C["panel"])
        top.pack(fill="x")
        pipe_lbl = tk.Label(
            top, text="PIPELINE", bg=C["panel"], fg=C["dim"], font=("Segoe UI Semibold", 8)
        )
        pipe_lbl.pack(side="left")
        tip(
            pipe_lbl,
            "Four work stages for the selected mode. Hover a stage for what it does. "
            "Status: PENDING · ACTIVE · DONE · SKIPPED · FAILED. Scroll if needed.",
        )
        self.stage_count_lbl = tk.Label(
            top, text="4 STAGES", bg=C["panel"], fg=C["muted"], font=("Segoe UI", 8)
        )
        self.stage_count_lbl.pack(side="right")
        tip(self.stage_count_lbl, "Number of work stages in this mode (completion is separate).")
        self._stages_scroll = ScrollFrame(sp, bg=C["panel"])
        self._stages_scroll.pack(fill="both", expand=True, pady=(10, 0))
        tip(
            self._stages_scroll.canvas,
            "Scroll the pipeline list with the mouse wheel or scrollbar.",
        )
        self.stages_host = self._stages_scroll.inner

        # Hero
        hero = self._panel(right, fill="x", pady=(0, 12))
        hp = tk.Frame(hero, bg=C["panel"])
        hp.pack(fill="x", padx=16, pady=16)
        hr = tk.Frame(hp, bg=C["panel"])
        hr.pack(fill="x")
        hl = tk.Frame(hr, bg=C["panel"])
        hl.pack(side="left", fill="x", expand=True)
        live_lbl = tk.Label(
            hl, text="LIVE", bg=C["panel"], fg=C["dim"], font=("Segoe UI Semibold", 8)
        )
        live_lbl.pack(anchor="w")
        tip(live_lbl, "Current human-facing stage and the latest importer detail line.")
        hero_stage_lbl = tk.Label(
            hl,
            textvariable=self.hero_stage,
            bg=C["panel"],
            fg=C["text"],
            font=("Segoe UI Semibold", 20),
        )
        hero_stage_lbl.pack(anchor="w", pady=(2, 0))
        tip(hero_stage_lbl, "Active pipeline stage title (mapped from backend stage names).")
        hero_detail_lbl = tk.Label(
            hl,
            textvariable=self.hero_detail,
            bg=C["panel"],
            fg=C["muted"],
            font=("Segoe UI", 10),
            wraplength=440,
            justify="left",
        )
        hero_detail_lbl.pack(anchor="w", pady=(4, 0))
        tip(hero_detail_lbl, "Latest progress detail from the importer (object id, skip reason, etc.).")
        pr = tk.Frame(hr, bg=C["panel"])
        pr.pack(side="right")
        pct_lbl = tk.Label(
            pr, textvariable=self.pct_var, bg=C["panel"], fg=C["text"], font=("Segoe UI Semibold", 36)
        )
        pct_lbl.pack(anchor="e")
        tip(
            pct_lbl,
            "Honest overall percent: stage index + within-stage units "
            "(e.g. objects converted). No fake +35% creep.",
        )
        self.status_pill = tk.Label(
            pr, text="IDLE", bg=C["track"], fg=C["muted"], font=("Segoe UI Semibold", 9), padx=12, pady=4
        )
        self.status_pill.pack(anchor="e", pady=(4, 0))
        tip(self.status_pill, "Run state: IDLE · RUNNING · SUCCESS · FAILED.")

        bar = ttk.Progressbar(hp, variable=self.progress_var, maximum=100.0, mode="determinate")
        bar.pack(fill="x", pady=(14, 12))
        tip(bar, "Overall run progress driven by pipeline stage + unit counts.")

        metrics = tk.Frame(hp, bg=C["panel"])
        metrics.pack(fill="x")
        metric_tips = {
            "ELAPSED": "Wall time since Start for this run.",
            "ETA": "Estimated time remaining from stage plan + unit throughput.",
            "UNITS": "Within-stage work units (e.g. objects done/total in Convert).",
            "NEXT": "Next pipeline stage after the current one.",
        }
        for title, var in (
            ("ELAPSED", self.elapsed_var),
            ("ETA", self.eta_var),
            ("UNITS", self.units_var),
            ("NEXT", self.next_var),
        ):
            cell = tk.Frame(metrics, bg=C["panel2"], padx=12, pady=8)
            cell.pack(side="left", padx=(0, 8))
            t = tk.Label(cell, text=title, bg=C["panel2"], fg=C["dim"], font=("Segoe UI", 8))
            t.pack(anchor="w")
            v = tk.Label(
                cell, textvariable=var, bg=C["panel2"], fg=C["text"], font=("Segoe UI Semibold", 12)
            )
            v.pack(anchor="w")
            tip_tree(cell, metric_tips[title])

        mono = ("Cascadia Mono", 8) if sys.platform == "win32" else ("Consolas", 8)
        backend_lbl = tk.Label(
            hp, textvariable=self.backend_var, bg=C["panel"], fg=C["dim"], font=mono
        )
        backend_lbl.pack(anchor="w", pady=(10, 0))
        tip(
            backend_lbl,
            "Raw importer stage name (catalog, census, blender-w3d, …) for debugging.",
        )

        # Success actions (shown when idle after success)
        self.success_bar = tk.Frame(hp, bg=C["panel"])
        self.open_report_btn = self._btn(
            self.success_bar,
            "Open report",
            self._open_report,
            kind="success",
            hover="Open the plan/coverage JSON or pack folder produced by this run.",
            side="left",
            padx=(0, 8),
        )
        self.copy_report_btn = self._btn(
            self.success_bar,
            "Copy path",
            self._copy_report,
            kind="default",
            hover="Copy the artifact path to the clipboard.",
            side="left",
            padx=(0, 8),
        )
        self.launch_godot_btn = self._btn(
            self.success_bar,
            "Launch Godot",
            self._launch_godot,
            kind="default",
            hover="Launch the retail vertical slice (Men pack). Needs run_retail_slice.bat or OPENBFME_GODOT.",
            side="left",
        )

        # Log
        log_panel = self._panel(right, fill="both", expand=True)
        lp = tk.Frame(log_panel, bg=C["panel"])
        lp.pack(fill="both", expand=True, padx=12, pady=12)
        lh = tk.Frame(lp, bg=C["panel"])
        lh.pack(fill="x", pady=(0, 6))
        tele_lbl = tk.Label(
            lh, text="TELEMETRY", bg=C["panel"], fg=C["dim"], font=("Segoe UI Semibold", 8)
        )
        tele_lbl.pack(side="left")
        tip(tele_lbl, "Live importer output. Use filters to focus on progress or errors.")
        filt = tk.Frame(lh, bg=C["panel"])
        filt.pack(side="right")
        filter_tips = {
            "all": "Show all telemetry lines.",
            "progress": "Show stage progress + command + success lines only.",
            "errors": "Show error/traceback lines only.",
        }
        for value, label in (("all", "All"), ("progress", "Progress"), ("errors", "Errors")):
            rb = ttk.Radiobutton(
                filt,
                text=label,
                value=value,
                variable=self._log_filter,
                command=self._refresh_log_view,
            )
            rb.pack(side="left", padx=4)
            tip(rb, filter_tips[value])
        self._btn(
            filt,
            "Clear",
            self._clear_log,
            kind="default",
            hover="Clear the telemetry buffer for this session.",
            side="left",
            padx=(8, 0),
        )

        log_shell = tk.Frame(lp, bg=C["border"])
        log_shell.pack(fill="both", expand=True)
        self.log = tk.Text(
            log_shell,
            height=12,
            wrap="word",
            state="disabled",
            bg=C["log_bg"],
            fg=C["text"],
            insertbackground=C["text"],
            selectbackground=C["panel2"],
            relief="flat",
            font=("Cascadia Mono", 9) if sys.platform == "win32" else ("Consolas", 9),
            padx=10,
            pady=8,
            borderwidth=0,
            highlightthickness=0,
        )
        scroll = ttk.Scrollbar(log_shell, command=self.log.yview)
        self.log.configure(yscrollcommand=scroll.set)
        self.log.pack(side="left", fill="both", expand=True, padx=1, pady=1)
        scroll.pack(side="right", fill="y")
        tip(
            self.log,
            "Importer stdout/stderr stream. Hover filters to change what is shown. "
            "Scroll with the mouse wheel.",
        )
        tip(scroll, "Scroll the telemetry log.")

        def _log_wheel(event: Any) -> str:
            steps = _wheel_delta(event)
            if steps:
                self.log.yview_scroll(steps, "units")
            return "break"

        for seq in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
            self.log.bind(seq, _log_wheel)
            log_shell.bind(seq, _log_wheel)
        self.log.tag_configure("cmd", foreground=C["accent"])
        self.log.tag_configure("progress", foreground=C["ok"])
        self.log.tag_configure("error", foreground=C["err"])
        self.log.tag_configure("meta", foreground=C["muted"])
        self.log.tag_configure("ok", foreground=C["ok"])
        self.log.tag_configure("warn", foreground=C["warn"])
        self.log.tag_configure("err", foreground=C["err"])

    def _on_mode_change(self) -> None:
        if self._proc is not None:
            return
        self._mode_key = self._mode_labels.get(self.mode_var.get(), "faction-convert")
        self._rebuild_stage_rail()
        self._sync_mode_controls()

    def _sync_mode_controls(self) -> None:
        if self._mode_key == "men-build":
            self.faction_frame.pack_forget()
        else:
            self.faction_frame.pack(fill="x", pady=(12, 0))

    def _update_mode_tip(self) -> None:
        key = self._mode_labels.get(self.mode_var.get(), "faction-convert")
        text = MODE_TIPS.get(key, "")
        if hasattr(self, "mode_combo") and text:
            tip(self.mode_combo, text)

    def _rebuild_stage_rail(self) -> None:
        for child in self.stages_host.winfo_children():
            child.destroy()
        self._stage_rows.clear()
        plan = STAGE_PLANS.get(self._mode_key, STAGE_PLANS["faction-convert"])
        self.stage_count_lbl.configure(text=f"{len(plan)} STAGES")
        for i, spec in enumerate(plan, start=1):
            row = StageRow(
                self.stages_host,
                i,
                spec["title"],
                spec["blurb"],
                tip_text=spec.get("tip", spec["blurb"]),
            )
            self._stage_rows[spec["id"]] = row
        # Refresh scroll region + bind wheel on every stage label.
        self.stages_host.update_idletasks()
        if hasattr(self, "_stages_scroll"):
            self._stages_scroll.rebind_children()
            self._stages_scroll.canvas.yview_moveto(0)
        self._update_mode_tip()

    def _map_stage(self, backend: str) -> str:
        aliases = STAGE_ALIASES.get(self._mode_key, {})
        return aliases.get(backend, "catalog")

    def _plan_ids(self) -> list[str]:
        return [s["id"] for s in STAGE_PLANS.get(self._mode_key, ())]

    def _honest_pct(self, stage_id: str, units_done: int = 0, units_total: int = 0) -> float:
        ids = self._plan_ids()
        if not ids or stage_id not in ids:
            return 0.0
        idx = ids.index(stage_id)
        n = len(ids)
        base = idx / n
        if units_total > 0:
            partial = min(1.0, units_done / units_total) / n
            return min(100.0, (base + partial) * 100.0)
        return min(100.0, (base + 0.05) * 100.0)

    def _activate_stage(
        self,
        stage_id: str,
        detail: str = "",
        *,
        skipped: bool = False,
    ) -> None:
        now = time.monotonic()
        ids = self._plan_ids()
        if stage_id not in ids:
            return
        self._seen_stages.add(stage_id)
        idx = ids.index(stage_id)

        # Close previous active.
        if self._active_stage_id and self._active_stage_id in self._stage_rows:
            prev = self._stage_rows[self._active_stage_id]
            if prev.state == "active" and self._active_stage_id != stage_id:
                started = self._stage_started.get(self._active_stage_id, now)
                prev.set_state("done", duration_s=now - started)

        # Prior pending stages that were never seen → SKIPPED (not fake DONE).
        for sid in ids[:idx]:
            row = self._stage_rows[sid]
            if sid not in self._seen_stages and row.state == "pending":
                row.set_state("skipped", detail="not required for this run")
            elif row.state == "active":
                started = self._stage_started.get(sid, now)
                row.set_state("done", duration_s=now - started)

        row = self._stage_rows[stage_id]
        if skipped:
            row.set_state("skipped", detail=detail or "skipped")
            self._active_stage_id = stage_id
            return
        if row.state != "active":
            self._stage_started[stage_id] = now
        row.set_state("active", detail=detail)
        self._active_stage_id = stage_id

    def _finish_stages(self, ok: bool) -> None:
        now = time.monotonic()
        ids = self._plan_ids()
        if ok:
            for sid in ids:
                row = self._stage_rows[sid]
                if row.state == "active":
                    started = self._stage_started.get(sid, now)
                    row.set_state("done", duration_s=now - started)
                elif row.state == "pending":
                    row.set_state("skipped", detail="not required for this run")
        else:
            active = self._active_stage_id or (ids[0] if ids else "")
            if active in self._stage_rows:
                started = self._stage_started.get(active, now)
                self._stage_rows[active].set_state(
                    "failed",
                    detail="failed — see telemetry",
                    duration_s=now - started,
                )

    def _set_status_pill(self, kind: str, text: str) -> None:
        colors = {
            "idle": (C["track"], C["muted"]),
            "running": (C["accent_soft"], C["accent"]),
            "ok": (C["ok_soft"], C["ok"]),
            "failed": (C["err_soft"], C["err"]),
        }
        bg, fg = colors.get(kind, colors["idle"])
        self.status_pill.configure(text=text, bg=bg, fg=fg)

    def _set_progress(self, pct: float) -> None:
        pct = max(0.0, min(100.0, pct))
        self.progress_var.set(pct)
        self.pct_var.set(f"{int(pct)}%")

    def _show_success_bar(self, show: bool) -> None:
        if show and self._last_report is not None:
            self.success_bar.pack(fill="x", pady=(12, 0))
            is_pack = self._mode_key in {"men-build", "publish-faction"}
            self.launch_godot_btn.configure(state="normal" if is_pack else "disabled")
        else:
            self.success_bar.pack_forget()

    def _browse(self) -> None:
        path = filedialog.askdirectory(initialdir=self.install_var.get() or str(Path.home()))
        if path:
            self.install_var.set(path)

    def _clear_log(self) -> None:
        self._log_buffer.clear()
        self._refresh_log_view()

    def _append_log(self, line: str, tag: str = "") -> None:
        self._log_buffer.append((line.rstrip(), tag))
        if len(self._log_buffer) > 4000:
            self._log_buffer = self._log_buffer[-3000:]
        self._refresh_log_view(scroll=True)

    def _refresh_log_view(self, scroll: bool = False) -> None:
        filt = self._log_filter.get()
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        for line, tag in self._log_buffer:
            if filt == "progress" and tag not in {"progress", "ok", "cmd"}:
                continue
            if filt == "errors" and tag != "error":
                continue
            if tag:
                self.log.insert("end", line + "\n", tag)
            else:
                self.log.insert("end", line + "\n")
        if scroll:
            self.log.see("end")
        self.log.configure(state="disabled")

    def _mode_for_deps(self) -> str:
        key = self._mode_labels.get(self.mode_var.get(), "faction-convert")
        # publish-faction and men-build both cook packs (need Blender/ffmpeg).
        if key in {"men-build", "publish-faction"}:
            return "men-build"
        return key

    def _run_dependency_check(self) -> None:
        """Fast dependency scan; updates badge + log (non-blocking UI)."""

        if check_dependencies is None:
            self.deps_var.set("Dependencies · importer package missing")
            self.deps_lbl.configure(fg=C["err"])
            return

        install = Path(self.install_var.get()).expanduser()
        mode = self._mode_for_deps()
        self.deps_var.set("Dependencies · checking…")
        self.deps_lbl.configure(fg=C["muted"])

        def _worker() -> None:
            try:
                report = check_dependencies(
                    install,
                    DEFAULT_STATE,
                    mode=mode,
                    deep=False,
                    godot_path=GODOT_DEFAULT if GODOT_DEFAULT.is_file() else None,
                )
                self._q.put(("deps", report))
            except Exception as exc:  # noqa: BLE001 — surface in UI
                self._q.put(("deps-error", str(exc)))

        threading.Thread(target=_worker, daemon=True).start()

    def _apply_deps_report(self, report: dict[str, Any]) -> None:
        self._deps_report = report
        summary = str(report.get("summary", "checked"))
        self.deps_var.set(summary)
        if report.get("ready"):
            self.deps_lbl.configure(fg=C["ok"])
        elif report.get("error_count", 0):
            self.deps_lbl.configure(fg=C["err"])
        else:
            self.deps_lbl.configure(fg=C["warn"])
        if format_dependency_report is not None:
            self._append_log("— dependency check —", "meta")
            for line in format_dependency_report(report).splitlines():
                tag = "ok" if line.startswith("[OK") else (
                    "err" if line.startswith("[ERR") else (
                        "warn" if line.startswith("[WRN") else "meta"
                    )
                )
                self._append_log(line, tag)

    def _preflight(self, install: Path) -> str | None:
        if check_dependencies is None:
            if not install.is_dir():
                return f"Install folder not found:\n{install}"
            markers = (
                "lotrbfme2.exe",
                "game.dat",
                "INI.big",
                "ini.big",
                "_patch106.big",
            )
            if not any((install / name).exists() for name in markers):
                return (
                    f"Folder does not look like a BFME2 install:\n{install}\n\n"
                    "Expected lotrbfme2.exe, game.dat, or *.big archives."
                )
            return None

        mode = self._mode_for_deps()
        report = check_dependencies(
            install,
            DEFAULT_STATE,
            mode=mode,
            deep=False,
            godot_path=GODOT_DEFAULT if GODOT_DEFAULT.is_file() else None,
        )
        self._apply_deps_report(report)
        if blocking_message is not None:
            return blocking_message(report)
        return None if report.get("ready") else "Dependencies not ready."

    def _start(self) -> None:
        if self._proc is not None:
            return
        install = Path(self.install_var.get()).expanduser()
        err = self._preflight(install)
        if err:
            messagebox.showerror("Dependencies", err)
            return
        faction = self.faction_var.get().strip().casefold()
        self._mode_key = self._mode_labels.get(self.mode_var.get(), "faction-convert")
        if self._mode_key != "men-build" and faction not in FACTIONS:
            messagebox.showerror("Faction", f"Unknown faction: {faction}")
            return

        self._rebuild_stage_rail()
        self._stage_started.clear()
        self._active_stage_id = ""
        self._seen_stages.clear()
        self._last_report = None
        self._show_success_bar(False)

        mode = self._mode_key
        cmd = _importer_cmd() + _game_arguments(install)
        if mode == "faction-plan":
            cmd += [
                "import-faction",
                "--install",
                str(install),
                "--faction",
                faction,
                "--plan-only",
            ]
        elif mode == "faction-convert":
            cmd += [
                "import-faction",
                "--install",
                str(install),
                "--faction",
                faction,
                "--convert",
            ]
        elif mode == "publish-faction":
            cmd += [
                "publish-faction-to-slice",
                "--install",
                str(install),
                "--faction",
                faction,
                "--godot-content-root",
                str(DEFAULT_CONTENT),
                # The GUI publish button means "make this faction live", so it
                # opts into selection (publish alone no longer rewrites it).
                "--select",
            ]
        else:
            cmd += [
                "build",
                "--install",
                str(install),
                "--profile",
                "men-fords-v0",
            ]
        if self.dev_var.get() and mode in {
            "faction-convert",
            "men-build",
            "publish-faction",
        }:
            cmd.append("--dev")

        progress_file = DEFAULT_STATE / "reports" / "progress" / "gui-run.jsonl"
        env = os.environ.copy()
        env["OPENBFME_PROGRESS_FILE"] = str(progress_file)
        env["OPENBFME_IMPORT_ROOT"] = str(DEFAULT_STATE)
        env["PYTHONPATH"] = str(REPO_ROOT / "importer") + os.pathsep + env.get(
            "PYTHONPATH", ""
        )

        self._run_started = time.monotonic()
        self._set_progress(0.0)
        self._set_status_pill("running", "RUNNING")
        self.hero_stage.set("Starting")
        self.hero_detail.set("Launching importer…")
        self.eta_var.set("…")
        self.next_var.set(self._stage_rows[self._plan_ids()[0]].title if self._plan_ids() else "—")
        self.units_var.set("—")
        self.backend_var.set("backend · starting")
        self._activate_stage(self._plan_ids()[0], "bootstrapping")
        self._append_log(f"$ {' '.join(cmd)}", "cmd")

        try:
            creationflags = 0
            if sys.platform == "win32":
                creationflags = subprocess.CREATE_NEW_PROCESS_GROUP  # type: ignore[attr-defined]
            self._proc = subprocess.Popen(
                cmd,
                cwd=str(REPO_ROOT),
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
                creationflags=creationflags,
            )
        except OSError as exc:
            messagebox.showerror("Launch failed", str(exc))
            self._proc = None
            self._set_status_pill("failed", "FAILED")
            self._finish_stages(False)
            return

        self.start_btn.configure(state="disabled")
        self.stop_btn.configure(state="normal")
        self._reader = threading.Thread(target=self._read_output, args=(self._proc,), daemon=True)
        self._reader.start()

    def _read_output(self, proc: subprocess.Popen[str]) -> None:
        assert proc.stdout is not None
        for line in proc.stdout:
            self._q.put(("line", line))
            stripped = line.strip()
            match = PROGRESS_LINE.match(stripped)
            if match:
                data = match.groupdict()
                data["skipped"] = bool(SKIPPED_LINE.search(stripped))
                self._q.put(("progress", data))
            report_match = REPORT_IN_DETAIL.search(stripped)
            if report_match:
                self._q.put(("report", report_match.group(1)))
        code = proc.wait()
        self._q.put(("done", code))

    def _stop(self) -> None:
        proc = self._proc
        if proc is None or proc.pid is None:
            return
        self._append_log("… stop requested (killing process tree)", "meta")
        _kill_tree(proc.pid)
        try:
            proc.terminate()
        except OSError:
            pass

    def _open_progress(self) -> None:
        path = DEFAULT_STATE / "reports"
        path.mkdir(parents=True, exist_ok=True)
        try:
            os.startfile(path)  # type: ignore[attr-defined]
        except OSError as exc:
            messagebox.showinfo("Reports", f"{path}\n{exc}")

    def _open_report(self) -> None:
        if self._last_report is None:
            return
        path = self._last_report
        if path.is_file():
            try:
                os.startfile(path)  # type: ignore[attr-defined]
            except OSError:
                os.startfile(path.parent)  # type: ignore[attr-defined]
        elif path.is_dir():
            os.startfile(path)  # type: ignore[attr-defined]
        else:
            messagebox.showinfo("Report", f"Not found:\n{path}")

    def _copy_report(self) -> None:
        if self._last_report is None:
            return
        self.clipboard_clear()
        self.clipboard_append(str(self._last_report))
        self._append_log(f"copied {self._last_report}", "meta")

    def _launch_godot(self) -> None:
        bat = REPO_ROOT / "run_retail_slice.bat"
        if bat.is_file():
            try:
                os.startfile(bat)  # type: ignore[attr-defined]
                return
            except OSError:
                pass
        godot = GODOT_DEFAULT
        if godot.is_file():
            subprocess.Popen(
                [str(godot), "--path", str(REPO_ROOT / "game"), "res://scenes/retail_vertical_slice.tscn"],
                cwd=str(REPO_ROOT),
            )
            return
        messagebox.showinfo(
            "Godot",
            "Could not find run_retail_slice.bat or Godot.\nSet OPENBFME_GODOT.",
        )

    def _next_label(self, stage_id: str) -> str:
        ids = self._plan_ids()
        if stage_id not in ids:
            return "—"
        idx = ids.index(stage_id)
        if idx + 1 >= len(ids):
            return "Done"
        return self._stage_rows[ids[idx + 1]].title

    def _drain_queue(self) -> None:
        try:
            while True:
                kind, payload = self._q.get_nowait()
                if kind == "deps":
                    if isinstance(payload, dict):
                        self._apply_deps_report(payload)
                elif kind == "deps-error":
                    self.deps_var.set(f"Dependencies · check failed: {payload}")
                    self.deps_lbl.configure(fg=C["err"])
                    self._append_log(f"dependency check error: {payload}", "error")
                elif kind == "line":
                    text = str(payload).rstrip()
                    # Structured progress events update the rail; skip raw duplicates.
                    if text.startswith("[progress]"):
                        continue
                    if "ERROR" in text or "Error" in text or "Traceback" in text:
                        self._append_log(text, "error")
                    elif text.startswith("$"):
                        self._append_log(text, "cmd")
                    else:
                        self._append_log(text)
                elif kind == "report":
                    try:
                        self._last_report = Path(str(payload))
                    except Exception:
                        pass
                elif kind == "progress":
                    data = payload  # type: ignore[assignment]
                    assert isinstance(data, dict)
                    backend = str(data.get("stage", "")).strip()
                    detail = str(data.get("detail", "")).strip()
                    elapsed = str(data.get("elapsed", "0"))
                    eta_raw = str(data.get("eta", "?")).strip()
                    skipped = bool(data.get("skipped"))
                    units_done = data.get("units_done")
                    units_total = data.get("units_total")
                    pct = data.get("pct")

                    # Prefer report= from detail.
                    rm = REPORT_IN_DETAIL.search(detail)
                    if rm:
                        try:
                            self._last_report = Path(rm.group(1))
                        except Exception:
                            pass

                    stage_id = self._map_stage(backend)
                    self._activate_stage(stage_id, detail, skipped=skipped)

                    plan = STAGE_PLANS.get(self._mode_key, ())
                    title = next(
                        (s["title"] for s in plan if s["id"] == stage_id),
                        backend.replace("-", " ").title(),
                    )
                    self.hero_stage.set(title)
                    self.hero_detail.set(detail or "Working…")
                    self.backend_var.set(f"backend · {backend or '—'}")
                    try:
                        self.elapsed_var.set(_fmt_duration(int(elapsed)))
                    except ValueError:
                        self.elapsed_var.set(f"{elapsed}s")
                    self.eta_var.set(_parse_eta(eta_raw))
                    self.next_var.set(self._next_label(stage_id))

                    ud = int(units_done) if units_done not in (None, "") else 0
                    ut = int(units_total) if units_total not in (None, "") else 0
                    if ut > 0:
                        self.units_var.set(f"{ud}/{ut}")
                        self._set_progress(self._honest_pct(stage_id, ud, ut))
                    else:
                        self.units_var.set("—")
                        # Prefer backend stage-plan % when no units.
                        if pct not in (None, ""):
                            try:
                                self._set_progress(float(pct))
                            except ValueError:
                                self._set_progress(self._honest_pct(stage_id))
                        else:
                            self._set_progress(self._honest_pct(stage_id))

                    # Compact telemetry: stage transitions + throttled unit ticks.
                    unit_bit = f" · {ud}/{ut}" if ut > 0 else ""
                    skip_bit = " · skipped" if skipped else ""
                    log_this = True
                    if ut > 0 and 0 < ud < ut:
                        step = max(1, ut // 15)
                        log_this = ud == 1 or ud % step == 0
                    if log_this or skipped or backend in {"complete", "census", "catalog"}:
                        self._append_log(
                            f"→ {title}{unit_bit}{skip_bit}: {detail or '…'}",
                            "progress",
                        )
                    if backend == "complete":
                        self._set_progress(100.0)

                elif kind == "done":
                    code = int(payload)  # type: ignore[arg-type]
                    elapsed = int(time.monotonic() - self._run_started)
                    self.elapsed_var.set(_fmt_duration(elapsed))
                    if code == 0:
                        self._append_log(f"✓ complete · {_fmt_duration(elapsed)}", "ok")
                        self.hero_stage.set("Complete")
                        detail = "Pipeline finished."
                        if self._last_report is not None:
                            detail = f"Artifact: {self._last_report}"
                        self.hero_detail.set(detail)
                        self._set_progress(100.0)
                        self.eta_var.set("0s")
                        self.next_var.set("—")
                        self._set_status_pill("ok", "SUCCESS")
                        self._finish_stages(True)
                        self._show_success_bar(True)
                    else:
                        self._append_log(
                            f"✗ failed · exit {code} · {_fmt_duration(elapsed)}", "error"
                        )
                        self.hero_stage.set("Failed")
                        self.hero_detail.set(
                            f"Exit code {code}. Switch telemetry to Errors."
                        )
                        self._set_status_pill("failed", "FAILED")
                        self._finish_stages(False)
                        self._show_success_bar(False)
                    self.backend_var.set(f"backend · exit {code}")
                    self.start_btn.configure(state="normal")
                    self.stop_btn.configure(state="disabled")
                    self._proc = None
        except queue.Empty:
            pass

        if self._proc is not None and self._run_started:
            self.elapsed_var.set(_fmt_duration(time.monotonic() - self._run_started))
            if self._active_stage_id in self._stage_rows:
                started = self._stage_started.get(self._active_stage_id)
                row = self._stage_rows[self._active_stage_id]
                if started and row.state == "active":
                    row.time_lbl.configure(
                        text=_fmt_duration(time.monotonic() - started), fg=C["accent"]
                    )
        self.after(80, self._drain_queue)


def main() -> int:
    app = ImportGui()
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
