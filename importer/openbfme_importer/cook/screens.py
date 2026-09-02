"""Convert the complete effective APT/WND corpus into native screen-v1 documents."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from typing import Any, Mapping, Sequence

from ..native_content import (
    _relative_path,
    _write_selection_atomic,
    prepare_effective_ini_tree,
)
from .. import retail_hud_apt_convert as hud
from ..retail_screen_apt_convert import (
    ScreenAptConvertError,
    build_screen_scene,
    screen_bundle_virtual_paths,
)
from ..retail_screen_apt_plan import build_screen_apt_plan
from ..sage_apt import AptParseError, parse_wnd_layout
from ..util import write_json_atomic


SCHEMA = "openbfme.screen.v1"
INDEX_SCHEMA = "openbfme.native-screens-index"
SWEEP_SCHEMA = "openbfme.native-screen-sweep"


@dataclass(frozen=True)
class ScreenSweepResult:
    active: str
    index_path: Path
    report_path: Path
    attempted: int
    converted: int
    failed: int
    written: int


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical_bytes(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def _write_if_changed(path: Path, payload: bytes) -> bool:
    if path.is_file() and path.read_bytes() == payload:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    return True


def _slug(source_id: str) -> str:
    stem = re.sub(r"[^a-z0-9]+", "-", source_id.casefold()).strip("-")
    return f"{stem[:80]}-{hashlib.sha256(source_id.encode()).hexdigest()[:10]}"


def _failure_class(error: BaseException) -> str:
    text = str(error).casefold()
    if ".const" in text and ("missing" in text or "unsupported const" in text):
        return "unparseable-const"
    if ".dat" in text and "missing" in text:
        return "missing-texture-reference"
    if "geometry" in text and ("missing" in text or "unresolved" in text):
        return "missing-geometry"
    if "texture" in text or "atlas" in text:
        return "missing-texture-reference"
    if "opcode" in text:
        return "unsupported-opcode"
    if "wnd" in text:
        return "unparseable-wnd"
    return type(error).__name__


def _active_native_selection(content_root: Path) -> tuple[str, dict[str, object]]:
    selection_path = content_root / "native" / "selection.json"
    if not selection_path.is_file():
        raise FileNotFoundError(
            "native selection is missing; build the native bundle before screens"
        )
    selection = json.loads(selection_path.read_text(encoding="utf-8"))
    if not isinstance(selection, dict) or not isinstance(selection.get("active"), str):
        raise ValueError("native selection is invalid")
    active = str(selection["active"])
    if not re.fullmatch(r"[0-9a-f]{64}", active):
        raise ValueError("native selection active identity is invalid")
    return active, selection


def _source_files(root: Path, movie: str) -> list[dict[str, object]]:
    rows = []
    for relative in screen_bundle_virtual_paths(root, movie):
        path = root / Path(*relative.split("/"))
        data = path.read_bytes()
        rows.append(
            {"path": relative.replace("\\", "/").casefold(), "bytes": len(data), "sha256": _sha(data)}
        )
    return rows


def _structural_apt_scene(root: Path, movie_name: str) -> dict[str, object]:
    """Preserve libraries/script-only screens that intentionally draw nothing."""

    plan = build_screen_apt_plan(root, movie_name)
    hud.register_expected_flagged_null_clip_actions(
        (row["virtualPath"], row["recordOffset"], row["flags"])
        for row in plan["flaggedNullClipActions"]
    )
    movie = hud._movie_from_plan(plan, asset_root=root)
    timelines: list[dict[str, object]] = []
    programs: dict[str, dict[str, object]] = {}
    candidates: list[tuple[int, list[list[dict[str, Any]]]]] = [(0, movie.frames)]
    candidates.extend(
        (character_id, character.get("frames", []))
        for character_id, character in enumerate(movie.characters)
        if str(character.get("kind")) == "sprite" and character.get("frames")
    )
    for character_id, frames in candidates:
        timeline, _failures = hud._reconstruct_timeline(movie.name, character_id, frames)
        for frame in timeline["frames"]:
            for action in frame.get("actionScripts", []):
                program = hud._decode_action_program(movie, action)
                script_id = str(program["scriptId"])
                start = int(program["instructionOffset"])
                end = start + int(program["byteLength"])
                if "vmBytecode" not in program:
                    program["vmBytecode"] = hud._vm_bytecode_contract(
                        movie, list(program["instructions"]), start, end
                    )
                programs[script_id] = program
                action["scriptId"] = script_id
        timelines.append(timeline)
    constants = {
        movie.name.casefold(): {
            "movie": movie.name,
            "sha256": str(movie.constants["sha256"]),
            "entries": [
                {"type": int(entry["type"]), "value": entry.get("value")}
                for entry in movie.constants["entries"]
            ],
        }
    }
    stage = plan["apt"]["root"]
    return {
        "movie": movie.name,
        "closure": [movie.name],
        "unplannableImports": [],
        "frameSelection": {
            "frame": 0,
            "label": None,
            "rule": "structural-script-or-library-frame-one",
            "priority": [],
            "availableLabels": {},
        },
        "stage": {
            "width": int(stage["width"]),
            "height": int(stage["height"]),
            "frameCount": int(stage["frameCount"]),
            "millisecondsPerFrame": int(stage["millisecondsPerFrame"]),
        },
        "sources": [],
        "sourceAggregateSha256": str(plan["apt"]["sha256"]),
        "atlases": [dict(row) for row in plan["atlases"]],
        "draws": [],
        "textInstances": [],
        "buttonInstances": [],
        "timelineInstances": [],
        "timelines": timelines,
        "actionScripts": [programs[key] for key in sorted(programs)],
        "clipActionPrograms": [],
        "vmConstants": constants,
        "clipActions": [],
        "blockers": [],
        "blockerCounts": {},
        "totals": {"draws": 0, "blockers": 0, "actionScripts": len(programs)},
    }


def _cook_apt(
    root: Path,
    source: Path,
    source_id: str,
    output_root: Path,
    content_root: Path,
) -> tuple[dict[str, object], int]:
    movie = source.stem
    try:
        scene = build_screen_scene(root, movie)
    except ScreenAptConvertError as error:
        if "reconstructed nothing" not in str(error):
            raise
        scene = _structural_apt_scene(root, movie)
    source_files = _source_files(root, movie)
    slug = _slug(source_id)
    written = 0
    try:
        import PIL
        from PIL import Image
    except ImportError as error:  # pragma: no cover - pinned environment guard
        raise ScreenAptConvertError("Pillow is required for screen textures") from error
    if PIL.__version__ != "12.2.0":
        raise ScreenAptConvertError(
            f"Pillow 12.2.0 is required for deterministic screen textures; found {PIL.__version__}"
        )
    atlases: list[dict[str, object]] = []
    for atlas_value in scene.get("atlases", []):
        atlas = dict(atlas_value)
        virtual = str(atlas["virtualPath"])
        payload = (root / Path(*virtual.split("/"))).read_bytes()
        if _sha(payload) != str(atlas["sha256"]):
            raise ScreenAptConvertError(f"{virtual} changed during screen cook")
        target = output_root / "textures" / slug / (Path(virtual).stem.casefold() + ".png")
        buffer = BytesIO()
        with Image.open(BytesIO(payload)) as opened:
            opened.convert("RGBA").save(
                buffer, format="PNG", compress_level=9, optimize=False
            )
        png = buffer.getvalue()
        written += int(_write_if_changed(target, png))
        atlas["cookedPng"] = _relative_path(target, content_root)
        atlas["cookedPngSha256"] = _sha(png)
        atlases.append(atlas)
    document: dict[str, object] = {
        **scene,
        "schema": SCHEMA,
        "schemaVersion": 1,
        "kind": "apt",
        "id": source_id,
        "source": {
            "path": source_id,
            "sha256": _sha(source.read_bytes()),
            "files": source_files,
        },
        "atlases": atlases,
    }
    return document, written


def _cook_wnd(source: Path, source_id: str) -> dict[str, object]:
    payload = source.read_bytes()
    layout = parse_wnd_layout(payload, source_id)
    return {
        "schema": SCHEMA,
        "schemaVersion": 1,
        "kind": "wnd",
        "id": source_id,
        "source": {"path": source_id, "sha256": _sha(payload), "files": []},
        "stage": {"width": 800, "height": 600, "frameCount": 1, "millisecondsPerFrame": 0},
        "window": layout,
        "actionScripts": [],
        "clipActionPrograms": [],
        "vmConstants": {},
        "atlases": [],
        "draws": [],
        "blockers": [],
        "blockerCounts": {},
        "totals": {"draws": 0, "blockers": 0},
    }


def _receipt_path(index_root: Path, source_id: str) -> Path:
    return index_root / "receipts" / f"{_slug(source_id)}.load.json"


def convert_screen_corpus(
    *, install: Path, state_root: Path, content_root: Path
) -> ScreenSweepResult:
    """Convert all 104 effective winners and write a deterministic native index."""

    _ini_root, _layered, _built = prepare_effective_ini_tree(install, state_root)
    # prepare_effective_ini_tree verified this edition-specific tree.  Never use
    # the sibling layered-effective-assets cache: it is not an effective winner
    # view and is explicitly outside the screens contract.
    effective_root = state_root / "editions" / "rotwk" / "cache" / "effective-assets"
    if not effective_root.is_dir():
        raise FileNotFoundError(f"effective asset tree is missing: {effective_root}")
    active, selection = _active_native_selection(content_root)
    screen_root = content_root / "native" / active / "screens"
    document_root = screen_root / "documents"
    sources = [
        *(path for path in effective_root.glob("*.apt") if path.is_file()),
        *(path for path in effective_root.rglob("*.wnd") if path.is_file()),
    ]
    sources.sort(key=lambda path: path.relative_to(effective_root).as_posix().casefold())
    rows: list[dict[str, object]] = []
    written = 0
    for source in sources:
        source_id = source.relative_to(effective_root).as_posix().casefold()
        slug = _slug(source_id)
        row: dict[str, object] = {
            "id": source_id,
            "name": source.stem,
            "kind": source.suffix.casefold().removeprefix("."),
        }
        try:
            if source.suffix.casefold() == ".apt":
                document, texture_writes = _cook_apt(
                    effective_root, source, source_id, screen_root, content_root
                )
                written += texture_writes
            else:
                document = _cook_wnd(source, source_id)
            document_path = document_root / f"{slug}.screen-v1.json"
            payload = _canonical_bytes(document)
            written += int(_write_if_changed(document_path, payload))
            row.update(
                {
                    "status": "ok",
                    "document": _relative_path(document_path, content_root),
                    "documentSha256": _sha(payload),
                    "receipt": _relative_path(
                        _receipt_path(screen_root, source_id), content_root
                    ),
                    "blockerCounts": document.get("blockerCounts", {}),
                }
            )
        except (AptParseError, OSError, ScreenAptConvertError, ValueError) as error:
            row.update(
                {
                    "status": "failed",
                    "failure": {
                        "class": _failure_class(error),
                        "message": str(error),
                    },
                }
            )
        rows.append(row)
    index = {
        "schema": INDEX_SCHEMA,
        "schemaVersion": 1,
        "active": active,
        "attempted": len(rows),
        "ok": sum(row["status"] == "ok" for row in rows),
        "failed": sum(row["status"] == "failed" for row in rows),
        "screens": rows,
    }
    index_path = screen_root / "index.json"
    written += int(_write_if_changed(index_path, _canonical_bytes(index)))
    selection["screens"] = _relative_path(index_path, content_root)
    _write_selection_atomic(content_root / "native" / "selection.json", selection)
    report_path = state_root / "reports" / "rotwk-screen-sweep.json"
    report = {**index, "schema": SWEEP_SCHEMA, "index": _relative_path(index_path, content_root)}
    write_json_atomic(report_path, report)
    return ScreenSweepResult(
        active=active,
        index_path=index_path,
        report_path=report_path,
        attempted=len(rows),
        converted=int(index["ok"]),
        failed=int(index["failed"]),
        written=written,
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--state-root", required=True, type=Path)
    parser.add_argument("--content-root", required=True, type=Path)
    parser.add_argument("--sweep", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        result = convert_screen_corpus(
            install=args.install.expanduser().resolve(),
            state_root=args.state_root.expanduser().resolve(),
            content_root=args.content_root.expanduser().resolve(),
        )
    except (OSError, RuntimeError, TypeError, ValueError) as error:
        print(f"SCREEN_SWEEP_FAIL {error}", file=sys.stderr)
        return 1
    print(
        f"SCREEN_SWEEP_RESULT attempted={result.attempted} converted={result.converted} "
        f"failed={result.failed} written={result.written} index={result.index_path}"
    )
    return 0 if result.failed == 0 else 3


if __name__ == "__main__":
    raise SystemExit(main())
