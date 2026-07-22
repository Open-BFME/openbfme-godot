"""Unified dependency preflight for importer CLI + GUI.

Checks BFME2 install shape/patch, pinned Python/Pillow tools, Blender/FFmpeg/
OpenSAGE plugin, and optional Godot for pack launch/publish.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import sys
from typing import Any, Mapping, Sequence

from .bootstrap import (
    BLENDER_EXE_SHA256,
    BLENDER_VERSION,
    DEFUSEDXML_VERSION,
    FFMPEG_EXE_SHA256,
    FFMPEG_VERSION,
    FONTTOOLS_VERSION,
    PLUGIN_COMMIT,
    PYTHON_VERSION,
    tool_status,
)
from .catalog import doctor_install
from .big import sha256_file
from .paths import default_godot_content_root, default_state_root


CheckItem = dict[str, Any]

# Modes that require W3D cook tools (Blender + plugin + ffmpeg).
_W3D_MODES = frozenset({"men-build", "build", "import-unit"})
# Modes that only need install + Python/Pillow for plan/convert descriptors.
_CONVERT_MODES = frozenset(
    {"faction-plan", "faction-convert", "import-faction", "plan", "convert"}
)


def _item(
    *,
    id: str,
    label: str,
    ok: bool,
    required: bool,
    detail: str,
    fix: str = "",
    expected: str = "",
    found: str = "",
) -> CheckItem:
    return {
        "id": id,
        "label": label,
        "ok": bool(ok),
        "required": bool(required),
        "severity": "error" if required and not ok else ("ok" if ok else "warn"),
        "detail": detail,
        "fix": fix,
        "expected": expected,
        "found": found,
    }


def _discover_godot() -> tuple[Path | None, str]:
    """Locate Godot executable; returns (path|None, how_found)."""

    env = os.environ.get("OPENBFME_GODOT", "").strip()
    if env:
        path = Path(env).expanduser()
        if path.is_file():
            return path.resolve(), "OPENBFME_GODOT"
    # Common private defaults used by the GUI / slice runner.
    candidates = [
        Path(r"C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64.exe"),
        Path.home() / "Downloads" / "godot47" / "Godot_v4.7-stable_win64.exe",
    ]
    which = shutil.which("godot")
    if which:
        candidates.insert(0, Path(which))
    for path in candidates:
        if path.is_file():
            return path.resolve(), "discovered"
    return None, "missing"


def _fast_tool_checks(state_root: Path) -> list[CheckItem]:
    """Presence + pin-hash for executables; version strings for Python deps.

    Avoids full Blender tree re-hash (seconds) so the GUI stays responsive.
    """

    tools_root = Path(state_root).expanduser().resolve() / "tools"
    items: list[CheckItem] = []

    py_ver = sys.version.split()[0]
    py_ok = py_ver == PYTHON_VERSION
    items.append(
        _item(
            id="python",
            label=f"Python {PYTHON_VERSION}",
            ok=py_ok,
            required=True,
            expected=PYTHON_VERSION,
            found=py_ver,
            detail=f"interpreter {sys.executable}",
            fix="run tools/bootstrap-importer-python.ps1 into the private state tools env",
        )
    )

    def _pkg(name: str, expected: str, import_name: str | None = None) -> CheckItem:
        mod_name = import_name or name
        try:
            mod = __import__(mod_name)
            found = str(getattr(mod, "__version__", "?"))
            ok = found == expected
        except ImportError:
            found = "missing"
            ok = False
        return _item(
            id=name,
            label=f"{name} {expected}",
            ok=ok,
            required=True,
            expected=expected,
            found=found,
            detail="pip package in importer env",
            fix="recreate tools/python-3.12-env via bootstrap-importer-python.ps1",
        )

    items.append(_pkg("Pillow", "12.2.0", "PIL"))
    items.append(_pkg("fontTools", FONTTOOLS_VERSION))
    items.append(_pkg("defusedxml", DEFUSEDXML_VERSION))

    blender = tools_root / "blender-4.2.0-windows-x64" / "blender.exe"
    if blender.is_file():
        try:
            digest = sha256_file(blender).casefold()
            blender_ok = digest == BLENDER_EXE_SHA256.casefold()
            found = f"present sha={digest[:12]}…"
        except OSError as exc:
            blender_ok = False
            found = f"unreadable: {exc}"
    else:
        blender_ok = False
        found = "missing"
    items.append(
        _item(
            id="blender",
            label=f"Blender {BLENDER_VERSION} (pinned)",
            ok=blender_ok,
            required=True,
            expected=BLENDER_EXE_SHA256[:16] + "…",
            found=found,
            detail=str(blender),
            fix="run: openbfme-import bootstrap-tools",
        )
    )

    plugin = tools_root / "OpenSAGE.BlenderPlugin" / "io_mesh_w3d" / "__init__.py"
    plugin_ok = plugin.is_file()
    items.append(
        _item(
            id="w3d_plugin",
            label="OpenSAGE W3D plugin",
            ok=plugin_ok,
            required=True,
            expected=PLUGIN_COMMIT[:12],
            found="present" if plugin_ok else "missing",
            detail=str(plugin.parent.parent),
            fix="run: openbfme-import bootstrap-tools",
        )
    )

    ffmpeg = tools_root / "ffmpeg-8.1.1" / "bin" / "ffmpeg.exe"
    if ffmpeg.is_file():
        try:
            digest = sha256_file(ffmpeg).casefold()
            ffmpeg_ok = digest == FFMPEG_EXE_SHA256.casefold()
            found = f"present sha={digest[:12]}…"
        except OSError as exc:
            ffmpeg_ok = False
            found = f"unreadable: {exc}"
    else:
        ffmpeg_ok = False
        found = "missing"
    items.append(
        _item(
            id="ffmpeg",
            label=f"FFmpeg {FFMPEG_VERSION} (pinned)",
            ok=ffmpeg_ok,
            required=True,
            expected=FFMPEG_EXE_SHA256[:16] + "…",
            found=found,
            detail=str(ffmpeg),
            fix="bootstrap-tools --ffmpeg <path-to-ffmpeg-8.1.1.exe>",
        )
    )

    git = shutil.which("git")
    items.append(
        _item(
            id="git",
            label="git (for plugin attest)",
            ok=bool(git),
            required=True,
            found=git or "missing",
            detail="needed to verify OpenSAGE plugin commit cleanliness",
            fix="install Git for Windows and ensure it is on PATH",
        )
    )
    return items


def _deep_tool_items(state_root: Path) -> list[CheckItem]:
    status = tool_status(state_root, skip_w3d_attestation=False)
    checks = status.get("checks", {})
    labels = {
        "blender": f"Blender {BLENDER_VERSION} exe hash",
        "blender_tree": "Blender portable tree hash",
        "opensage_w3d_plugin": "OpenSAGE plugin commit + clean tree",
        "ffmpeg": f"FFmpeg {FFMPEG_VERSION} hash",
        "ffprobe": "FFprobe hash",
        "python": f"Python {PYTHON_VERSION}",
        "python_runtime": "Python runtime tree pin",
        "pillow": "Pillow 12.2.0",
        "pillow_tree": "Pillow package tree pin",
        "fonttools": f"fontTools {FONTTOOLS_VERSION}",
        "fonttools_tree": "fontTools tree pin",
        "defusedxml": f"defusedxml {DEFUSEDXML_VERSION}",
        "defusedxml_tree": "defusedxml tree pin",
    }
    items: list[CheckItem] = []
    for key, label in labels.items():
        ok = bool(checks.get(key))
        items.append(
            _item(
                id=f"deep:{key}",
                label=label,
                ok=ok,
                required=True,
                detail="deep tool_status attestation",
                fix="bootstrap-tools / recreate python env" if not ok else "",
            )
        )
    return items


def check_dependencies(
    install: Path | str | None = None,
    state_root: Path | str | None = None,
    *,
    mode: str = "faction-convert",
    deep: bool = False,
    godot_path: Path | str | None = None,
) -> dict[str, Any]:
    """Return a structured dependency report.

    *mode* selects which checks are **required** for Start:
    - faction-plan / faction-convert: install + Python/Pillow (W3D tools warn)
    - men-build / build: install + full cook toolchain; Godot warn for launch
    """

    install_path = (
        Path(install).expanduser()
        if install is not None
        else Path(os.environ.get("BFME2_INSTALL", r"F:\BFME2"))
    )
    state = (
        Path(state_root).expanduser().resolve()
        if state_root is not None
        else default_state_root()
    )
    mode_key = (mode or "faction-convert").strip().casefold()
    needs_w3d = mode_key in _W3D_MODES or mode_key == "men-build"
    # Map GUI labels
    if mode_key in {"men-build", "build"}:
        needs_w3d = True

    items: list[CheckItem] = []

    # --- Install ---
    try:
        doctor = doctor_install(install_path, deep=False, game="bfme2")
    except (OSError, ValueError, TypeError) as exc:
        doctor = {
            "ready": False,
            "install_root": str(install_path),
            "declared_patch": "unknown",
            "missing_required": [str(exc)],
            "executable_present": False,
        }
    install_ok = bool(doctor.get("ready"))
    patch = str(doctor.get("declared_patch", "unknown"))
    patch_ok = patch.startswith("1.06") or patch == "1.06"
    # Prefer 1.06; warn (not hard fail) on older patches so doctor still runs.
    items.append(
        _item(
            id="install",
            label="BFME2 install present",
            ok=install_ok,
            required=True,
            expected="lotrbfme2.exe + core .big archives",
            found=str(doctor.get("install_root", install_path)),
            detail=(
                f"patch={patch}; missing={doctor.get('missing_required') or []}"
            ),
            fix="set Install path to a complete BFME2 1.06 folder",
        )
    )
    items.append(
        _item(
            id="patch",
            label="BFME2 patch 1.06",
            ok=patch_ok if install_ok else False,
            required=True,
            expected="1.06",
            found=patch,
            detail="importer targets BFME2 1.06 retail",
            fix="apply official 1.06 patch (or confirmed patch archives)",
        )
    )
    if doctor.get("executable_attestation", {}).get("modified_marker_detected"):
        items.append(
            _item(
                id="modded_exe",
                label="Unmodded executable preference",
                ok=False,
                required=False,
                detail="DEV!ANCE / modified company marker on game.dat",
                fix="prefer an unmodded install for deterministic catalogs",
            )
        )

    # --- Tools ---
    if deep:
        tool_items = _deep_tool_items(state)
    else:
        tool_items = _fast_tool_checks(state)

    for item in tool_items:
        # Soften W3D tool requirements for plan/convert-only modes.
        if not needs_w3d and item["id"] in {
            "blender",
            "w3d_plugin",
            "ffmpeg",
            "git",
            "deep:blender",
            "deep:blender_tree",
            "deep:opensage_w3d_plugin",
            "deep:ffmpeg",
            "deep:ffprobe",
        }:
            item = dict(item)
            item["required"] = False
            if not item["ok"]:
                item["severity"] = "warn"
        items.append(item)

    # --- Godot (launch / publish target) ---
    if godot_path is not None and str(godot_path).strip():
        gpath = Path(godot_path).expanduser()
        g_ok = gpath.is_file()
        g_found = str(gpath) if g_ok else "missing"
        g_how = "configured"
    else:
        discovered, g_how = _discover_godot()
        g_ok = discovered is not None
        g_found = str(discovered) if discovered else "missing"
    godot_required = needs_w3d  # men-build wants Godot for launch CTA; not hard for cook
    items.append(
        _item(
            id="godot",
            label="Godot 4.x (slice launch)",
            ok=g_ok,
            required=False,  # pack cook can finish without launching
            expected="Godot 4.x stable win64",
            found=f"{g_found} ({g_how})",
            detail=f"content root default: {default_godot_content_root()}",
            fix="set OPENBFME_GODOT to Godot_v4.x-stable_win64.exe",
        )
    )

    # --- State root writable ---
    try:
        state.mkdir(parents=True, exist_ok=True)
        probe = state / ".openbfme-write-probe"
        probe.write_text("ok", encoding="utf-8")
        probe.unlink(missing_ok=True)
        state_ok = True
        state_detail = str(state)
    except OSError as exc:
        state_ok = False
        state_detail = str(exc)
    items.append(
        _item(
            id="state_root",
            label="Private state root writable",
            ok=state_ok,
            required=True,
            found=state_detail,
            detail="caches, packs, reports",
            fix="set OPENBFME_IMPORT_ROOT to a writable external/private path",
        )
    )

    errors = [i for i in items if i["required"] and not i["ok"]]
    warnings = [i for i in items if (not i["required"]) and not i["ok"]]
    return {
        "schema": "openbfme.dependency-check",
        "schemaVersion": 1,
        "mode": mode_key,
        "deep": bool(deep),
        "install": doctor,
        "state_root": str(state),
        "ready": len(errors) == 0,
        "error_count": len(errors),
        "warning_count": len(warnings),
        "items": items,
        "errors": errors,
        "warnings": warnings,
        "summary": (
            f"{'READY' if not errors else 'BLOCKED'} · "
            f"{sum(1 for i in items if i['ok'])}/{len(items)} checks ok · "
            f"{len(errors)} errors · {len(warnings)} warnings"
        ),
    }


def format_dependency_report(report: Mapping[str, object]) -> str:
    """Human-readable multi-line report for CLI/GUI log."""

    lines = [str(report.get("summary", "dependency check")), ""]
    items = report.get("items")
    if not isinstance(items, list):
        return "\n".join(lines)
    for raw in items:
        if not isinstance(raw, Mapping):
            continue
        mark = "OK " if raw.get("ok") else ("ERR" if raw.get("required") else "WRN")
        label = raw.get("label", raw.get("id", "?"))
        detail = raw.get("detail", "")
        found = raw.get("found", "")
        line = f"[{mark}] {label}"
        if found:
            line += f" — {found}"
        if detail and str(detail) not in line:
            line += f" ({detail})"
        lines.append(line)
        fix = raw.get("fix")
        if fix and not raw.get("ok"):
            lines.append(f"       fix: {fix}")
    return "\n".join(lines)


def blocking_message(report: Mapping[str, object]) -> str | None:
    """Short dialog text if Start should be blocked; else None."""

    if report.get("ready"):
        return None
    errors = report.get("errors")
    if not isinstance(errors, list) or not errors:
        return "Dependencies not ready."
    lines = ["Cannot start — fix these first:\n"]
    for raw in errors[:8]:
        if not isinstance(raw, Mapping):
            continue
        lines.append(f"• {raw.get('label', raw.get('id'))}: {raw.get('found') or raw.get('detail')}")
        if raw.get("fix"):
            lines.append(f"  → {raw['fix']}")
    return "\n".join(lines)


__all__ = [
    "blocking_message",
    "check_dependencies",
    "format_dependency_report",
]
