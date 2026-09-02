"""Shared helpers for the fleet tools. Standard library only."""
from __future__ import annotations

import json
import os
import re
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
WORKSPACE = REPO / "workspace"
QUEUES_DIR = Path(__file__).resolve().parent / "queues"
REPORTS = WORKSPACE / "retail-work" / "reports"
LOGS = WORKSPACE / "logs"
CENSUS = REPO / "game" / "data" / "retail_module_census.json"
MODULES_DIR = REPO / "engine" / "OpenBfme.Sim" / "Modules"
TREE = "rotwk-retail"


def agent_name() -> str:
    return os.environ.get("FLEET_AGENT") or os.environ.get("USERNAME") or os.environ.get("USER") or "anonymous"


def read_json(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, indent=2, sort_keys=True)
        handle.write("\n")


def git(*args: str, check: bool = True) -> str:
    result = subprocess.run(["git", *args], cwd=REPO, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if check and result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout


def tracked_files() -> list[str]:
    """Tracked plus untracked-but-not-ignored files, so a fresh unit counts before it is committed."""
    out = git("ls-files", "--cached", "--others", "--exclude-standard")
    return sorted({line for line in out.splitlines() if line})


# ---------------------------------------------------------------- derived queues

def _module_file_names() -> set[str]:
    if not MODULES_DIR.is_dir():
        return set()
    return {p.stem for p in MODULES_DIR.glob("*.cs")}


def core_modules_units() -> tuple[list[dict], int]:
    """Module types without a file under engine/.../Modules, ranked by retail object count."""
    if not CENSUS.is_file():
        return [], 0
    members = read_json(CENSUS)["members"]
    done = _module_file_names()
    units = []
    for member in members:
        name = member["name"]
        count = int((member.get("objectCount") or {}).get(TREE, 0))
        if name in done:
            continue
        units.append({
            "id": f"core-modules/{name}",
            "title": f"Implement {name} in engine/OpenBfme.Sim/Modules/{name}.cs",
            "rank": -count,
            "detail": f"{count} retail objects declare it; classification {member.get('classification')}: {member.get('classificationNote', '')}",
            "oracle": f"dotnet test engine/OpenBfme.Engine.sln --nologo --filter FullyQualifiedName~{name}",
        })
    units.sort(key=lambda u: (u["rank"], u["id"]))
    return units, len(members)


def red_units() -> tuple[list[dict], int]:
    """FAIL lines from the latest headless slice runner log."""
    candidates = sorted(LOGS.glob("*retail_slice_runner*.txt"), key=lambda p: p.stat().st_mtime, reverse=True) if LOGS.is_dir() else []
    if not candidates:
        return [], 0
    latest = candidates[0]
    total = 0
    units = []
    seen = set()
    pattern = re.compile(r"^RETAIL_SLICE (PASS|FAIL) (\S+)(.*)$")
    for line in latest.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line.strip())
        if not match:
            continue
        total += 1
        status, name, rest = match.groups()
        if status != "FAIL" or name in seen:
            continue
        seen.add(name)
        units.append({
            "id": f"red/{name}",
            "title": f"Make slice check {name} pass",
            "rank": 0,
            "detail": rest.strip()[:200] + f"  (from {latest.name})",
            "oracle": "run_tests.bat   # then confirm the FAIL line is gone and no new FAIL appeared",
        })
    return units, total


def _census_minus_converted(kind: str) -> tuple[list[dict], int]:
    """Corpus census minus converted digests. Both files are private workspace
    reports; when absent the queue is empty and progress.py says why."""
    census_path = REPORTS / f"rotwk-{kind}-queue.json"
    if not census_path.is_file():
        return [], 0
    doc = read_json(census_path)
    total = int(doc.get("total", 0))
    units = []
    for row in doc.get("open", []):
        units.append({
            "id": f"{kind}/{row['id']}",
            "title": row.get("title") or row["id"],
            "rank": int(row.get("rank", 0)),
            "detail": row.get("detail", ""),
            "oracle": row.get("oracle", f"python tools/openbfme_import.py verify-{kind} --id {row['id']}"),
        })
    units.sort(key=lambda u: (u["rank"], u["id"]))
    return units, total


def maps_units():
    return _census_minus_converted("maps")


def assets_units():
    return _census_minus_converted("assets")


def screens_units():
    return _census_minus_converted("screens")


def bugs_units() -> tuple[list[dict], int]:
    try:
        out = subprocess.run(
            ["gh", "issue", "list", "--label", "playtest", "--state", "open", "--limit", "200", "--json", "number,title"],
            cwd=REPO, capture_output=True, text=True, encoding="utf-8", timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired):
        return [], 0
    if out.returncode != 0:
        return [], 0
    rows = json.loads(out.stdout or "[]")
    units = [{
        "id": f"bugs/{row['number']}",
        "title": row["title"],
        "rank": row["number"],
        "detail": f"gh issue view {row['number']}",
        "oracle": "add a reproduction runner under game/tests, make it green, run_tests.bat",
    } for row in rows]
    return units, len(units)


# ---------------------------------------------------------------- manual queues

def manual_units(name: str) -> tuple[list[dict], int]:
    folder = QUEUES_DIR / name
    if not folder.is_dir():
        return [], 0
    units = []
    total = 0
    for path in sorted(folder.glob("*.json")):
        doc = read_json(path)
        total += 1
        if doc.get("status") == "done":
            continue
        units.append({
            "id": f"{name}/{path.stem}",
            "title": doc.get("title", path.stem),
            "rank": int(doc.get("rank", 0)),
            "detail": doc.get("detail", ""),
            "oracle": doc.get("oracle", ""),
            "path": str(path.relative_to(REPO)),
        })
    units.sort(key=lambda u: (u["rank"], u["id"]))
    return units, total


QUEUE_ORDER = [
    "bugs", "red", "launcher", "core", "core-modules", "cook", "render", "assets", "maps",
    "screens", "missions", "ai", "net", "mods",
]

DERIVED = {
    "core-modules": core_modules_units,
    "red": red_units,
    "maps": maps_units,
    "assets": assets_units,
    "screens": screens_units,
    "bugs": bugs_units,
}


def units_for(name: str) -> tuple[list[dict], int]:
    if name in DERIVED:
        return DERIVED[name]()
    return manual_units(name)
