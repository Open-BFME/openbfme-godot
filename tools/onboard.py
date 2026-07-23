#!/usr/bin/env python3
"""Guided onboarding wizard for a fresh OpenBFME contributor/player machine.

The wizard walks four fail-closed steps:

1. Prerequisites — importer Python env, Godot 4.7 executable (prompted once and
   persisted to a local untracked config), git, and the pinned Blender/FFmpeg
   conversion tools (verified through the importer doctor).
2. Retail source — locate a lawfully owned BFME2 1.06 install (and optionally
   RotWK 2.01) and validate its identity through the existing importer
   ``doctor`` command. Identity validation is read-only and fail-closed: an
   unrecognized or incomplete install stops the wizard with the doctor's own
   actionable report.
3. Content — verify the already-published private packs, or run the two-step
   Men conversion (``import-faction --convert`` then
   ``publish-faction-to-slice``) when no selected pack exists yet.
4. Verification — run the key headless gates (retail_slice_runner with its
   pinned deterministic battle signature, plus menu_skirmish_runner) and print
   a pass/fail summary with launch instructions.

Everything retail-derived stays under the ignored ``.private`` workspace; the
wizard never copies or distributes retail bytes. Non-interactive mode for CI:

    python tools/onboard.py --install F:\\BFME2 --godot C:\\path\\Godot.exe --yes

All state the wizard persists lives in ``.private/onboard.config.json``
(untracked; override with ``--config``).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Mapping, Sequence

REPO_ROOT = Path(__file__).resolve().parents[1]
IMPORTER_CLI = REPO_ROOT / "tools" / "openbfme_import.py"
GAME_ROOT = REPO_ROOT / "game"
DEFAULT_CONFIG_PATH = REPO_ROOT / ".private" / "onboard.config.json"
DEFAULT_STATE_ROOT = REPO_ROOT / ".private" / "retail-work"
DEFAULT_CONTENT_ROOT = REPO_ROOT / ".private" / "content-packs"
BOOTSTRAP_SCRIPT = REPO_ROOT / "tools" / "bootstrap-importer-python.ps1"

CONFIG_SCHEMA = "openbfme.onboard-config"
CONFIG_VERSION = 1

# Host pack the vertical slice asserts on; the Men publish selects it.
MEN_PACK_ID = "bfme2-men-vslice"

# import-faction --convert exit 6 = "conversion incomplete" (known converter
# gaps such as MenGarrisonTowerExpansion). publish-faction-to-slice composes
# only converted coverage, so the publish step remains sound; surface the gap
# loudly but do not hard-stop the wizard on it.
IMPORT_FACTION_KNOWN_GAP_EXIT = 6

GATES: tuple[dict[str, str], ...] = (
    {
        "name": "retail_slice_runner",
        "script": "res://tests/retail_slice_runner.gd",
        "result_pattern": r"(?m)^RETAIL_SLICE_RESULT passed=(\d+) failed=(\d+)\s*$",
    },
    {
        "name": "menu_skirmish_runner",
        "script": "res://tests/menu_skirmish_runner.gd",
        "result_pattern": r"(?m)^MENU_SKIRMISH_RESULT passed=(\d+) failed=(\d+)\s*$",
    },
)
SIGNATURE_PATTERN = r"(?m)^RETAIL_SLICE_SIGNATURE ([0-9A-F]+)\s*$"

_GATE_TIMEOUT_SECONDS = 900
_IMPORTER_TIMEOUT_SECONDS = 3600


class OnboardError(RuntimeError):
    """Fatal wizard failure with an actionable message."""


# ---------------------------------------------------------------------------
# Pure logic (unit-tested; no subprocesses, no prompts)
# ---------------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="onboard",
        description=(
            "Guided setup: prerequisites, retail install validation, Men pack "
            "conversion/verification, and headless gate verification."
        ),
    )
    parser.add_argument(
        "--install",
        type=Path,
        default=None,
        help="BFME2 1.06 installation root (prompted when omitted)",
    )
    parser.add_argument(
        "--rotwk",
        type=Path,
        default=None,
        help="optional RotWK 2.01 installation root to validate as well",
    )
    parser.add_argument(
        "--godot",
        type=Path,
        default=None,
        help="Godot 4.7 executable (prompted once and persisted to the config)",
    )
    parser.add_argument(
        "--yes",
        action="store_true",
        help="non-interactive mode: never prompt; fail closed on missing input",
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=DEFAULT_CONFIG_PATH,
        help="local untracked wizard config (default: .private/onboard.config.json)",
    )
    parser.add_argument(
        "--state-root",
        type=Path,
        default=None,
        help="private importer state root (default: .private/retail-work)",
    )
    parser.add_argument(
        "--content-root",
        type=Path,
        default=None,
        help="private Godot content-packs root (default: .private/content-packs)",
    )
    parser.add_argument(
        "--skip-gates",
        action="store_true",
        help="skip the headless verification gates (setup-only run)",
    )
    parser.add_argument(
        "--force-convert",
        action="store_true",
        help="run the Men convert+publish even when a selected pack already exists",
    )
    return parser


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    return build_parser().parse_args(argv)


def load_config(path: Path) -> dict[str, Any]:
    """Read the wizard config; missing file is an empty config, corrupt is fatal."""

    if not path.is_file():
        return {}
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise OnboardError(
            f"onboard config at {path} is unreadable or not valid JSON ({exc}); "
            "fix or delete the file and rerun"
        ) from exc
    if not isinstance(document, dict):
        raise OnboardError(
            f"onboard config at {path} must be a JSON object; "
            "fix or delete the file and rerun"
        )
    return document


def save_config(path: Path, config: Mapping[str, Any]) -> None:
    document = dict(config)
    document["schema"] = CONFIG_SCHEMA
    document["schemaVersion"] = CONFIG_VERSION
    path.parent.mkdir(parents=True, exist_ok=True)
    serialized = json.dumps(document, indent=2, sort_keys=True) + "\n"
    staging = path.with_suffix(path.suffix + ".tmp")
    staging.write_text(serialized, encoding="utf-8")
    staging.replace(path)


def classify_install(root: Path) -> str | None:
    """Cheap local dispatch: which retail game does this directory look like?

    This only routes the request to the right importer ``doctor --game``
    invocation; the doctor performs the authoritative fail-closed identity
    validation (executable, archives, patch level).
    """

    if not root.is_dir():
        return None
    if (root / "lotrbfme2ep1.exe").is_file():
        return "rotwk"
    if (root / "lotrbfme2.exe").is_file():
        return "bfme2"
    return None


def importer_python(state_root: Path) -> Path:
    return state_root / "tools" / "python-3.12-env" / "Scripts" / "python.exe"


@dataclass
class PrereqResult:
    name: str
    ok: bool
    detail: str
    fix: str = ""
    required: bool = True


def evaluate_prerequisites(
    *,
    python_env: Path,
    godot_exe: Path | None,
    git_path: str | None,
    importer_cli: Path = IMPORTER_CLI,
    game_root: Path = GAME_ROOT,
) -> list[PrereqResult]:
    """Pure prerequisite evaluation over explicit paths (injectable for tests)."""

    results: list[PrereqResult] = []
    results.append(
        PrereqResult(
            name="repository layout",
            ok=importer_cli.is_file() and game_root.is_dir(),
            detail=f"importer CLI {importer_cli}; game project {game_root}",
            fix="run the wizard from a full open-bfme checkout (tools/ + game/)",
        )
    )
    results.append(
        PrereqResult(
            name="importer Python env",
            ok=python_env.is_file(),
            detail=str(python_env),
            fix=(
                "bootstrap it: powershell -ExecutionPolicy Bypass -File "
                "tools/bootstrap-importer-python.ps1 (the wizard offers this)"
            ),
        )
    )
    godot_ok = godot_exe is not None and godot_exe.is_file()
    results.append(
        PrereqResult(
            name="Godot 4.7 executable",
            ok=godot_ok,
            detail=str(godot_exe) if godot_exe else "not configured",
            fix=(
                "download Godot 4.7 stable (win64), keep it OUTSIDE the repo, "
                "and pass --godot or answer the prompt once (persisted to config)"
            ),
        )
    )
    results.append(
        PrereqResult(
            name="git",
            ok=bool(git_path),
            detail=git_path or "not found on PATH",
            fix="install Git for Windows and reopen the terminal",
        )
    )
    return results


def prerequisites_ready(results: Sequence[PrereqResult]) -> bool:
    return all(result.ok for result in results if result.required)


def resolve_godot_console(godot_exe: Path) -> Path:
    """Prefer the *_console.exe sibling for headless runs on Windows.

    The GUI-subsystem Godot exe does not attach stdout, so gate output would be
    lost. When a console sibling exists next to the configured exe, use it.
    """

    name = godot_exe.name
    if name.lower().endswith("_console.exe"):
        return godot_exe
    if name.lower().endswith(".exe"):
        sibling = godot_exe.with_name(name[:-4] + "_console.exe")
        if sibling.is_file():
            return sibling
    return godot_exe


@dataclass
class PackPlan:
    action: str  # "verify" or "convert"
    reason: str
    active_pack: str = ""
    pack_root: Path | None = None


def plan_pack_step(
    selection_document: Mapping[str, Any] | None,
    content_root: Path,
    *,
    force_convert: bool = False,
) -> PackPlan:
    """Decide between verifying existing packs and running convert+publish.

    Pure over a parsed selection.json document plus the content root path, so
    tests can drive it with temp directories.
    """

    if force_convert:
        return PackPlan(action="convert", reason="--force-convert requested")
    if selection_document is None:
        return PackPlan(
            action="convert",
            reason=f"no selection.json under {content_root}",
        )
    active = selection_document.get("activePack")
    if not isinstance(active, str) or "/" not in active:
        return PackPlan(
            action="convert",
            reason="selection.json has no activePack entry of the form <id>/<sha256>",
        )
    pack_id, _, bundle = active.partition("/")
    if pack_id != MEN_PACK_ID:
        return PackPlan(
            action="convert",
            reason=f"active pack is {pack_id!r}, expected the {MEN_PACK_ID} host pack",
            active_pack=active,
        )
    pack_root = content_root / pack_id / bundle
    if not (pack_root / "pack.json").is_file():
        return PackPlan(
            action="convert",
            reason=f"selected bundle missing on disk: {pack_root}",
            active_pack=active,
        )
    return PackPlan(
        action="verify",
        reason="selected Men pack bundle present on disk",
        active_pack=active,
        pack_root=pack_root,
    )


@dataclass
class GateOutcome:
    name: str
    ran: bool
    passed: int = 0
    failed: int = 0
    signature: str = ""
    detail: str = ""

    @property
    def ok(self) -> bool:
        return self.ran and self.failed == 0 and self.passed > 0


def parse_gate_output(name: str, output: str, result_pattern: str) -> GateOutcome:
    """Extract the pass/fail counts (and slice signature) from runner output."""

    match = re.search(result_pattern, output)
    if not match:
        return GateOutcome(
            name=name,
            ran=False,
            detail="runner produced no recognizable result line (crash or wrong pack?)",
        )
    outcome = GateOutcome(
        name=name,
        ran=True,
        passed=int(match.group(1)),
        failed=int(match.group(2)),
    )
    signature = re.search(SIGNATURE_PATTERN, output)
    if signature:
        outcome.signature = signature.group(1)
    return outcome


def summarize_gates(outcomes: Sequence[GateOutcome]) -> tuple[bool, list[str]]:
    lines: list[str] = []
    all_ok = bool(outcomes)
    for outcome in outcomes:
        if not outcome.ran:
            all_ok = False
            lines.append(f"  FAIL {outcome.name}: {outcome.detail}")
            continue
        status = "PASS" if outcome.ok else "FAIL"
        if not outcome.ok:
            all_ok = False
        extra = f" signature={outcome.signature}" if outcome.signature else ""
        lines.append(
            f"  {status} {outcome.name}: {outcome.passed} passed, "
            f"{outcome.failed} failed{extra}"
        )
    return all_ok, lines


# ---------------------------------------------------------------------------
# Interaction + subprocess plumbing
# ---------------------------------------------------------------------------


def _prompt(question: str, *, non_interactive: bool, flag_hint: str) -> str:
    if non_interactive:
        raise OnboardError(
            f"missing required input in non-interactive mode: {question} "
            f"(pass {flag_hint})"
        )
    try:
        answer = input(f"{question}: ").strip().strip('"')
    except EOFError as exc:
        raise OnboardError(
            f"no terminal available to prompt for: {question} (pass {flag_hint})"
        ) from exc
    if not answer:
        raise OnboardError(f"no value provided for: {question} (pass {flag_hint})")
    return answer


def _confirm(question: str, *, non_interactive: bool) -> bool:
    if non_interactive:
        return True
    try:
        answer = input(f"{question} [y/N]: ").strip().casefold()
    except EOFError:
        return False
    return answer in {"y", "yes"}


def run_command(
    command: Sequence[str],
    *,
    env: Mapping[str, str] | None = None,
    timeout: int,
) -> tuple[int, str]:
    """Run a subprocess, streaming nothing; returns (exit_code, combined output)."""

    try:
        completed = subprocess.run(
            list(command),
            cwd=str(REPO_ROOT),
            env=dict(env) if env is not None else None,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
    except FileNotFoundError as exc:
        raise OnboardError(f"cannot execute {command[0]}: {exc}") from exc
    except subprocess.TimeoutExpired as exc:
        raise OnboardError(
            f"{command[0]} timed out after {timeout} seconds"
        ) from exc
    return completed.returncode, (completed.stdout or "") + (completed.stderr or "")


def run_importer_json(
    python_exe: Path,
    arguments: Sequence[str],
    *,
    state_root: Path,
    runner: Callable[..., tuple[int, str]] = run_command,
) -> tuple[int, dict[str, Any]]:
    """Invoke the importer CLI read-only with --json and parse its report."""

    env = dict(os.environ)
    env["OPENBFME_IMPORT_ROOT"] = str(state_root)
    env["PYTHONPATH"] = str(REPO_ROOT / "importer")
    exit_code, output = runner(
        [str(python_exe), str(IMPORTER_CLI), "--json", *arguments],
        env=env,
        timeout=_IMPORTER_TIMEOUT_SECONDS,
    )
    # The CLI prints exactly one JSON document on success paths and a JSON
    # error object on stderr for handled failures; find the outermost object.
    document: dict[str, Any] = {}
    start = output.find("{")
    if start >= 0:
        try:
            document = json.loads(output[start:])
        except ValueError:
            document = {}
    if not document:
        document = {"raw_output": output.strip()[-2000:]}
    return exit_code, document


# ---------------------------------------------------------------------------
# Wizard steps
# ---------------------------------------------------------------------------


@dataclass
class WizardContext:
    args: argparse.Namespace
    config: dict[str, Any] = field(default_factory=dict)
    config_dirty: bool = False
    state_root: Path = DEFAULT_STATE_ROOT
    content_root: Path = DEFAULT_CONTENT_ROOT
    godot_exe: Path | None = None
    install_root: Path | None = None
    rotwk_root: Path | None = None
    python_exe: Path | None = None


def _config_path_value(config: Mapping[str, Any], key: str) -> Path | None:
    value = config.get(key)
    if isinstance(value, str) and value.strip():
        return Path(value.strip())
    return None


def step_prerequisites(ctx: WizardContext) -> None:
    print("== Step 1/4: prerequisites ==")
    args = ctx.args
    ctx.state_root = (args.state_root or DEFAULT_STATE_ROOT).resolve()
    ctx.content_root = (args.content_root or DEFAULT_CONTENT_ROOT).resolve()

    godot = args.godot or _config_path_value(ctx.config, "godotExe")
    if godot is None or not Path(godot).is_file():
        if godot is not None:
            print(f"  configured Godot executable missing: {godot}")
        answer = _prompt(
            "Path to the Godot 4.7 executable (kept outside the repo)",
            non_interactive=args.yes,
            flag_hint="--godot PATH",
        )
        godot = Path(answer)
    godot = Path(godot).expanduser()
    if not godot.is_file():
        raise OnboardError(
            f"Godot executable not found: {godot}. Download Godot 4.7 stable "
            "(win64), keep it outside the repository, and rerun with --godot."
        )
    ctx.godot_exe = godot.resolve()
    if ctx.config.get("godotExe") != str(ctx.godot_exe):
        ctx.config["godotExe"] = str(ctx.godot_exe)
        ctx.config_dirty = True

    python_env = importer_python(ctx.state_root)
    results = evaluate_prerequisites(
        python_env=python_env,
        godot_exe=ctx.godot_exe,
        git_path=shutil.which("git"),
    )
    for result in results:
        mark = "OK " if result.ok else "ERR"
        print(f"  [{mark}] {result.name} — {result.detail}")
        if not result.ok and result.fix:
            print(f"        fix: {result.fix}")

    env_result = next(r for r in results if r.name == "importer Python env")
    if not env_result.ok:
        if not BOOTSTRAP_SCRIPT.is_file():
            raise OnboardError(f"bootstrap script missing: {BOOTSTRAP_SCRIPT}")
        if not _confirm(
            "Importer Python env is missing. Bootstrap it now (downloads pinned "
            "Python 3.12 + packages into .private)?",
            non_interactive=ctx.args.yes,
        ):
            raise OnboardError(
                "importer Python env is required; rerun after bootstrapping it"
            )
        print("  bootstrapping importer Python env (this downloads pinned tools)...")
        exit_code, output = run_command(
            [
                "powershell.exe",
                "-NoLogo",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(BOOTSTRAP_SCRIPT),
                "-StateRoot",
                str(ctx.state_root),
            ],
            timeout=_IMPORTER_TIMEOUT_SECONDS,
        )
        if exit_code != 0 or not python_env.is_file():
            raise OnboardError(
                "importer Python bootstrap failed; last output:\n"
                + output.strip()[-2000:]
            )
        results = evaluate_prerequisites(
            python_env=python_env,
            godot_exe=ctx.godot_exe,
            git_path=shutil.which("git"),
        )

    if not prerequisites_ready(results):
        raise OnboardError(
            "prerequisites are not satisfied; fix the ERR items above and rerun"
        )
    ctx.python_exe = python_env
    print("  prerequisites OK")


def _validate_install(
    ctx: WizardContext, root: Path, *, expected_game: str, label: str
) -> None:
    kind = classify_install(root)
    if kind is None:
        raise OnboardError(
            f"{label} at {root} does not look like a retail install "
            "(no lotrbfme2.exe / lotrbfme2ep1.exe). Point the wizard at the "
            "installation root, not a copy of loose files."
        )
    if kind != expected_game:
        raise OnboardError(
            f"{label} at {root} looks like a "
            f"{'RotWK' if kind == 'rotwk' else 'BFME2'} install; expected "
            f"{'RotWK 2.01' if expected_game == 'rotwk' else 'BFME2 1.06'}. "
            "Pass the RotWK directory via --rotwk and the BFME2 directory via --install."
        )
    assert ctx.python_exe is not None
    print(f"  validating {label} identity via importer doctor (read-only)...")
    exit_code, report = run_importer_json(
        ctx.python_exe,
        ["doctor", "--install", str(root), "--game", expected_game],
        state_root=ctx.state_root,
    )
    ready = bool(report.get("ready"))
    if exit_code != 0 or not ready:
        errors = report.get("errors")
        details: list[str] = []
        if isinstance(errors, list):
            for item in errors:
                if isinstance(item, Mapping):
                    details.append(
                        f"  - {item.get('label', item.get('id'))}: "
                        f"{item.get('found') or item.get('detail')}"
                        + (f" (fix: {item['fix']})" if item.get("fix") else "")
                    )
        if not details and report.get("raw_output"):
            details.append(str(report["raw_output"]))
        raise OnboardError(
            f"{label} at {root} failed fail-closed identity validation "
            f"(doctor exit {exit_code}):\n" + "\n".join(details)
        )
    summary = report.get("summary", "ready")
    print(f"  {label} accepted: {summary}")


def step_retail_install(ctx: WizardContext) -> None:
    print("== Step 2/4: retail source ==")
    args = ctx.args
    install = args.install or _config_path_value(ctx.config, "bfme2Install")
    if install is None:
        answer = _prompt(
            "Path to your BFME2 1.06 installation (e.g. F:\\BFME2)",
            non_interactive=args.yes,
            flag_hint="--install PATH",
        )
        install = Path(answer)
    install = Path(install).expanduser().resolve()
    _validate_install(ctx, install, expected_game="bfme2", label="BFME2 install")
    ctx.install_root = install
    if ctx.config.get("bfme2Install") != str(install):
        ctx.config["bfme2Install"] = str(install)
        ctx.config_dirty = True

    rotwk = args.rotwk or _config_path_value(ctx.config, "rotwkInstall")
    if rotwk is not None:
        rotwk = Path(rotwk).expanduser().resolve()
        _validate_install(ctx, rotwk, expected_game="rotwk", label="RotWK install")
        ctx.rotwk_root = rotwk
        if ctx.config.get("rotwkInstall") != str(rotwk):
            ctx.config["rotwkInstall"] = str(rotwk)
            ctx.config_dirty = True


def _read_selection(content_root: Path) -> Mapping[str, Any] | None:
    selection_path = content_root / "selection.json"
    if not selection_path.is_file():
        return None
    try:
        document = json.loads(selection_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise OnboardError(
            f"selection.json at {selection_path} is unreadable ({exc}); "
            "restore it or rerun the Men publish to regenerate it"
        ) from exc
    if not isinstance(document, dict):
        raise OnboardError(f"selection.json at {selection_path} is not a JSON object")
    return document


def step_content(ctx: WizardContext) -> None:
    print("== Step 3/4: content packs ==")
    assert ctx.python_exe is not None and ctx.install_root is not None
    plan = plan_pack_step(
        _read_selection(ctx.content_root),
        ctx.content_root,
        force_convert=ctx.args.force_convert,
    )
    if plan.action == "verify":
        print(f"  existing packs verified: {plan.active_pack}")
        print(f"  bundle root: {plan.pack_root}")
        return

    print(f"  conversion needed: {plan.reason}")
    if not _confirm(
        "Run the Men faction convert+publish now? The first conversion takes "
        "a long time (roughly 30-60 minutes cold)",
        non_interactive=ctx.args.yes,
    ):
        raise OnboardError("Men conversion declined; rerun when ready")

    print("  [1/2] import-faction --faction men --convert (descriptors + recipes)...")
    exit_code, report = run_importer_json(
        ctx.python_exe,
        [
            "import-faction",
            "--install",
            str(ctx.install_root),
            "--faction",
            "men",
            "--convert",
        ],
        state_root=ctx.state_root,
    )
    if exit_code == IMPORT_FACTION_KNOWN_GAP_EXIT:
        print(
            "  WARNING: conversion reported known converter gaps "
            f"(gap count: {report.get('converter_gap_count', '?')}); "
            "continuing — publish composes converted coverage only"
        )
    elif exit_code != 0:
        raise OnboardError(
            f"import-faction failed (exit {exit_code}): "
            f"{report.get('error') or report.get('raw_output') or report}"
        )

    print("  [2/2] publish-faction-to-slice --faction men (cook + select pack)...")
    exit_code, report = run_importer_json(
        ctx.python_exe,
        [
            "publish-faction-to-slice",
            "--install",
            str(ctx.install_root),
            "--faction",
            "men",
            "--godot-content-root",
            str(ctx.content_root),
        ],
        state_root=ctx.state_root,
    )
    if exit_code != 0 or not report.get("valid", False):
        raise OnboardError(
            f"publish-faction-to-slice failed (exit {exit_code}): "
            f"{report.get('error') or report.get('raw_output') or report}"
        )

    verify = plan_pack_step(_read_selection(ctx.content_root), ctx.content_root)
    if verify.action != "verify":
        raise OnboardError(
            f"publish finished but the selection did not verify: {verify.reason}"
        )
    print(f"  published and selected: {verify.active_pack}")


def step_gates(ctx: WizardContext) -> list[GateOutcome]:
    print("== Step 4/4: headless verification gates ==")
    assert ctx.godot_exe is not None
    if ctx.args.skip_gates:
        print("  skipped (--skip-gates)")
        return []
    godot = resolve_godot_console(ctx.godot_exe)
    env = dict(os.environ)
    env["OPENBFME_CONTENT"] = str(ctx.content_root)
    # Mirror run_retail_slice.bat: the reviewed ranger-overlay approval hash
    # lets ContentDB mount the optional Men ranger overlay pack when present.
    env.setdefault(
        "OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256",
        "3e6399441fdfec38009ba2465e9249d57acb961934907c5839f5744be48df116",
    )
    outcomes: list[GateOutcome] = []
    for gate in GATES:
        print(f"  running {gate['name']} (headless, may take a few minutes)...")
        exit_code, output = run_command(
            [
                str(godot),
                "--headless",
                "--path",
                str(GAME_ROOT),
                "--script",
                gate["script"],
            ],
            env=env,
            timeout=_GATE_TIMEOUT_SECONDS,
        )
        outcome = parse_gate_output(gate["name"], output, gate["result_pattern"])
        if outcome.ran and outcome.failed == 0 and exit_code != 0:
            outcome.detail = f"runner exit code {exit_code} despite 0 failed checks"
            outcome.failed = max(outcome.failed, 1)
        outcomes.append(outcome)
    return outcomes


def print_summary(ctx: WizardContext, outcomes: Sequence[GateOutcome]) -> bool:
    print()
    print("== Onboarding summary ==")
    print(f"  BFME2 install : {ctx.install_root}")
    if ctx.rotwk_root:
        print(f"  RotWK install : {ctx.rotwk_root}")
    print(f"  Godot         : {ctx.godot_exe}")
    print(f"  Content root  : {ctx.content_root}")
    print(f"  Wizard config : {ctx.args.config}")
    if ctx.args.skip_gates:
        print("  Gates         : skipped")
        ok = True
    else:
        ok, lines = summarize_gates(outcomes)
        print("  Gates:")
        for line in lines:
            print("  " + line)
    print()
    if ok:
        print("RESULT: PASS — your machine is set up.")
        print()
        print("Next steps:")
        print("  - Launch the game:      run_game.bat (uses OPENBFME_GODOT if set)")
        print("  - Launch the slice:     run_retail_slice.bat")
        print("  - Headless smoke test:  run_retail_slice.bat --test")
        print("  - In-game settings persist under Godot user:// (user_settings)")
        print("  - Pack selection lives in .private/content-packs/selection.json")
        print("  - Docs: docs/ONBOARDING.md and docs/GETTING_STARTED.md")
    else:
        print("RESULT: FAIL — one or more verification gates did not pass.")
        print("  Re-run with --skip-gates to finish setup only, or check")
        print("  STATUS.md for currently known failures before filing an issue.")
    return ok


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    ctx = WizardContext(args=args)
    try:
        ctx.config = load_config(Path(args.config))
        step_prerequisites(ctx)
        step_retail_install(ctx)
        if ctx.config_dirty:
            save_config(Path(args.config), ctx.config)
            print(f"  saved wizard config: {args.config}")
        step_content(ctx)
        outcomes = step_gates(ctx)
        return 0 if print_summary(ctx, outcomes) else 1
    except OnboardError as exc:
        print(f"\nONBOARD FAIL: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nONBOARD ABORTED by user", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
