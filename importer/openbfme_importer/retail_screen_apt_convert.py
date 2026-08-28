"""Cook ANY retail screen movie into a scene contract Godot can draw.

`retail_hud_apt_convert.convert_hud_apt_bundle` is the Men-HUD emitter: it
requires an exact 261-source bundle, a `controlbar.wnd` companion, the
men-fords retail INI closure and the Palantir external-font binding, and it
writes `data/ui/palantir/scene-contract.json`.  None of that is wrong for THAT
scene; all of it is why a screen cannot be appended to it (queue Q117).

This module is the screen equivalent, and it is thin because the pieces already
exist: `retail_screen_apt_plan` plans a movie and its transitive import
closure, and `_Flattener.flatten_screen` reconstructs the display list at an
AUTHORED frame.  What is added here is only assembly and emit.

WHICH FRAME.  A screen is a script-driven state machine, so "the screen" is not
frame 0 - it is the state the movie's own author NAMED.  The selection rule is
fixed, declared in `OPEN_LABEL_PRIORITY`, and recorded in the contract next to
every label the movie carries, so the choice is auditable rather than implicit.
A movie with no label at all is cooked at frame 0 and says so.

Nothing here fills a gap.  Every unreconstructable thing the flatten found is
carried into `blockers` verbatim, grouped by code and counted, and the emit
refuses outright rather than writing a partial scene.
"""

from __future__ import annotations

import hashlib
import json
import tempfile
from io import BytesIO
from pathlib import Path
from typing import Any, Iterable, Mapping

from . import retail_hud_apt_convert as hud
from .retail_screen_apt_plan import (
    ScreenAptPlanError,
    build_screen_closure_plans,
    screen_movie_names,
    screen_source_virtual_paths,
)

#: The authored state a screen is shown in, most specific first.  `_open` and
#: `_show` are the two names retail actually uses for "the screen is up";
#: `_init` and `_fadeIn` are the fallbacks used by movies that never author a
#: separate open state.  This ORDER is the whole selection policy.
OPEN_LABEL_PRIORITY = ("_open", "_show", "_init", "_fadeIn")

#: Pillow is pinned for byte-deterministic PNG output, exactly as the HUD
#: emitter pins it - a different encoder version silently changes the bytes.
REQUIRED_PILLOW_VERSION = "12.2.0"


class ScreenAptConvertError(ValueError):
    """A screen cannot be reconstructed into a complete, honest scene."""


def _digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical_bytes(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def select_open_frame(labels: Mapping[str, int]) -> dict[str, Any]:
    """Pick the authored open state, and say exactly how it was picked."""

    for label in OPEN_LABEL_PRIORITY:
        if label in labels:
            return {
                "frame": int(labels[label]),
                "label": label,
                "rule": "authored-open-label",
                "priority": list(OPEN_LABEL_PRIORITY),
                "availableLabels": dict(sorted(labels.items())),
            }
    return {
        "frame": 0,
        "label": None,
        "rule": "no-authored-label-frame-zero",
        "priority": list(OPEN_LABEL_PRIORITY),
        "availableLabels": dict(sorted(labels.items())),
    }


def build_screen_scene(
    effective_assets_root: Path | str,
    movie: str,
    *,
    frame: int | None = None,
) -> dict[str, Any]:
    """Reconstruct one screen's authored open state into a scene contract."""

    root = Path(effective_assets_root)
    try:
        closure = build_screen_closure_plans(root, movie)
    except ScreenAptPlanError as error:
        raise ScreenAptConvertError(f"{movie}: {error}") from error

    for plan in closure["plans"].values():
        hud.register_expected_flagged_null_clip_actions(
            (row["virtualPath"], row["recordOffset"], row["flags"])
            for row in plan["flaggedNullClipActions"]
        )
    movies: dict[str, Any] = {}
    for plan in closure["plans"].values():
        try:
            loaded = hud._movie_from_plan(plan, asset_root=root)
        except hud.HudAptConvertError as error:
            raise ScreenAptConvertError(f"{plan['movie']}: {error}") from error
        movies[loaded.name.casefold()] = loaded

    root_movie = movies[movie.casefold()]
    labels = hud._timeline_labels(root_movie.frames)
    selection = select_open_frame(labels)
    if frame is not None:
        if not 0 <= frame < len(root_movie.frames):
            raise ScreenAptConvertError(f"{movie}: frame {frame} is out of range")
        selection = {**selection, "frame": int(frame), "rule": "caller-supplied-frame"}

    flattener = hud._Flattener(movies, {}, ())
    try:
        flattener.flatten_screen([(root_movie.name, int(selection["frame"]))])
    except hud.HudAptConvertError as error:
        raise ScreenAptConvertError(f"{movie}: {error}") from error
    if not flattener.draws and not flattener.text_instances:
        state = selection["label"] or "frame 0"
        raise ScreenAptConvertError(f"{movie}: reconstructed nothing at {state}")

    root_key = next(
        key for key in closure["plans"] if key.casefold() == movie.casefold()
    )
    stage = closure["plans"][root_key]["apt"]["root"]
    sources = sorted(
        (
            {
                "virtualPath": str(plan["apt"]["virtualPath"]),
                "byteLength": int(plan["apt"]["byteLength"]),
                "sha256": str(plan["apt"]["sha256"]),
            }
            for plan in closure["plans"].values()
        ),
        key=lambda row: row["virtualPath"].casefold(),
    )
    atlases = sorted(
        (dict(atlas) for plan in closure["plans"].values() for atlas in plan["atlases"]),
        key=lambda row: str(row["virtualPath"]).casefold(),
    )
    blocker_counts: dict[str, int] = {}
    for row in flattener.blockers:
        code = str(row["code"])
        blocker_counts[code] = blocker_counts.get(code, 0) + 1

    return {
        "schema": "openbfme.retail-screen-scene",
        "schemaVersion": 0,
        "movie": root_movie.name,
        "closure": sorted(closure["plans"], key=str.casefold),
        "unplannableImports": closure["unplannable"],
        "frameSelection": selection,
        "stage": {
            "width": int(stage["width"]),
            "height": int(stage["height"]),
            "frameCount": int(stage["frameCount"]),
            "millisecondsPerFrame": int(stage["millisecondsPerFrame"]),
        },
        "sources": sources,
        "sourceAggregateSha256": _digest(_canonical_bytes(sources)),
        "atlases": atlases,
        "draws": flattener.draws,
        "textInstances": flattener.text_instances,
        "buttonInstances": flattener.button_instances,
        "timelineInstances": flattener.timeline_instances,
        "timelines": [flattener.timelines[key] for key in sorted(flattener.timelines)],
        "clipActions": flattener.clip_actions,
        "blockers": flattener.blockers,
        "blockerCounts": dict(sorted(blocker_counts.items())),
        "totals": {
            "draws": len(flattener.draws),
            "textInstances": len(flattener.text_instances),
            "buttonInstances": len(flattener.button_instances),
            "timelines": len(flattener.timelines),
            "blockers": len(flattener.blockers),
        },
    }


def convert_screen_apt(
    effective_assets_root: Path | str,
    movie: str,
    output_directory: Path | str,
    *,
    frame: int | None = None,
) -> dict[str, Any]:
    """Write one screen's scene contract and its atlas PNGs, or refuse."""

    contract = build_screen_scene(effective_assets_root, movie, frame=frame)
    root = Path(effective_assets_root)
    output = Path(output_directory)
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ScreenAptConvertError("screen output directory must be empty")
    output.mkdir(parents=True, exist_ok=True)

    try:
        import PIL
        from PIL import Image
    except ImportError as error:  # pragma: no cover - environment guard
        raise ScreenAptConvertError(
            "Pillow is required for screen atlas conversion"
        ) from error
    if PIL.__version__ != REQUIRED_PILLOW_VERSION:
        raise ScreenAptConvertError(
            f"Pillow {REQUIRED_PILLOW_VERSION} is required for deterministic "
            f"screen atlases; found {PIL.__version__}"
        )

    for atlas in contract["atlases"]:
        source = root / Path(*str(atlas["virtualPath"]).split("/"))
        payload = source.read_bytes()
        if _digest(payload) != str(atlas["sha256"]):
            raise ScreenAptConvertError(
                f"{atlas['virtualPath']} changed between plan and cook"
            )
        target = output / Path(*str(atlas["cookedPng"]).split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(BytesIO(payload)) as opened:
            opened.convert("RGBA").save(
                target, format="PNG", compress_level=9, optimize=False
            )
        atlas["cookedPngSha256"] = _digest(target.read_bytes())

    contract_path = (
        output / "data/ui/screens" / movie.casefold() / "scene-contract.json"
    )
    contract_path.parent.mkdir(parents=True, exist_ok=True)
    contract_path.write_bytes(_canonical_bytes(contract))
    return contract


def convert_screen_apt_tree(
    effective_assets_root: Path | str,
    movies: Iterable[str],
    output_directory: Path | str,
) -> dict[str, Any]:
    """Cook several screens into one output tree, naming every refusal."""

    output = Path(output_directory)
    cooked: dict[str, dict[str, Any]] = {}
    refused: list[dict[str, str]] = []
    for movie in movies:
        try:
            cooked[movie] = convert_screen_apt(
                effective_assets_root, movie, output / movie.casefold()
            )
        except (ScreenAptConvertError, hud.HudAptConvertError) as error:
            refused.append({"movie": movie, "reason": str(error)})
    return {
        "schema": "openbfme.retail-screen-scene-tree",
        "schemaVersion": 0,
        "cooked": cooked,
        "refused": sorted(refused, key=lambda row: row["movie"].casefold()),
        "totals": {
            "cooked": len(cooked),
            "refused": len(refused),
            "draws": sum(int(row["totals"]["draws"]) for row in cooked.values()),
        },
    }


def screen_bundle_virtual_paths(
    effective_assets_root: Path | str, movie: str
) -> tuple[str, ...]:
    """Every virtual path a screen AND its import closure consume.

    The pipeline resolves converter inputs by virtual path out of extracted
    archives, so a screen lane has to be able to state its whole source set up
    front.  A screen is not a scene without its imports, so the bundle is the
    union across the closure - MainMenu needs MenuExport and GameWindowGadgets
    too - deduplicated and ordered.
    """

    root = Path(effective_assets_root)
    closure = build_screen_closure_plans(root, movie)
    paths: list[str] = []
    for name in sorted(closure["plans"], key=str.casefold):
        for path in screen_source_virtual_paths(root, name):
            if path not in paths:
                paths.append(path)
    return tuple(paths)


def convert_screen_apt_bundle(
    sources: Mapping[str, Path | str | bytes | bytearray],
    movie: str,
    output_directory: Path | str,
    *,
    frame: int | None = None,
) -> dict[str, Any]:
    """Cook a screen from an explicit source mapping, not from a tree.

    This is the shape the pipeline speaks: it hands a converter the extracted
    archive entries keyed by virtual path.  Rather than teach the plan builder
    a second way to read bytes - which would double the surface that has to
    stay honest - the mapping is staged into a temporary tree in EXACTLY the
    layout the retail tree uses, and the same code path runs against it.  The
    digests in the resulting plan therefore attest the archive bytes, because
    those are the only bytes that were ever read.
    """

    with tempfile.TemporaryDirectory(prefix="openbfme-screen-apt-") as staged:
        root = Path(staged)
        for virtual_path, value in sources.items():
            relative = str(virtual_path).replace("\\", "/").strip("/")
            if not relative or relative.startswith("../") or "/../" in relative:
                raise ScreenAptConvertError(
                    f"screen source path is unsafe: {virtual_path}"
                )
            target = root.joinpath(*relative.split("/"))
            target.parent.mkdir(parents=True, exist_ok=True)
            if isinstance(value, (bytes, bytearray)):
                target.write_bytes(bytes(value))
            else:
                target.write_bytes(Path(value).read_bytes())
        return convert_screen_apt(root, movie, output_directory, frame=frame)
