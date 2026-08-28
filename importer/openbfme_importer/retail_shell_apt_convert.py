"""Convert the bounded BFME2 main-menu shell APT closure into a draw contract.

This lane mirrors :mod:`retail_hud_apt_convert` -- it reuses that module's
proven, movie-agnostic primitives (``_Movie`` decoding, geometry/atlas binding,
the display-list flattener, the ActionScript decoder and the VM bytecode
handoff) and only supplies the shell-specific closure, layer order and output
identity.  Nothing new is invented here: the shell is the same class of retail
APT composite as the in-game palantir, so it rides the same machinery.

Scope boundary
--------------
The shell backdrop is *not* an APT image.  ``Background.apt`` places
``GameWindowGadgets::View3D``, an empty native gadget the retail engine renders
a live 3D shellmap into.  This converter records that fact as an explicit
blocker (``shell-backdrop-requires-native-view3d``) rather than substituting
art; the Godot side keeps its own backdrop under that blocker.
"""

from __future__ import annotations

import argparse
from io import BytesIO
import json
from pathlib import Path
from typing import Any, Iterable, Mapping

from .retail_hud_apt_convert import (
    HudAptConvertError,
    MAX_TIMELINE_FRAMES,
    _Flattener,
    _timeline_labels,
    _Movie,
    _Transform,
    _canonical_bytes,
    _movie_from_plan,
    _sha,
    _source_mapping,
)
from .sage_apt import (
    canonical_sha256,
    parse_apt_dat,
    parse_apt_geometry,
    parse_apt_movie,
    parse_apt_constants,
    parse_tga_identity,
)


OUTPUT_SCHEMA = "openbfme.retail-shell-apt-runtime"
OUTPUT_SCHEMA_VERSION = 0
SCENE_ID = "bfme2.ui.shell.mainmenu"
RUNTIME_OUTPUT_PATH = "data/ui/shell/scene-contract.json"
ATLAS_DIRECTORY = "assets/ui/shell/atlases"

#: Root scene layers, drawn back to front, each with the AUTHORED frame label
#: that names its shown state.  ``Background`` is the retail backdrop stage
#: (native View3D host); ``MainMenu`` is the shell chrome.
#:
#: The label matters and frame 0 is wrong for the backdrop.  ``Background``
#: places NOTHING on frame 0 - its frame 0 label is literally
#: ``_no_BG`` / "Used to hide other background if shown".  Its front-end state
#: is ``_show_FE_BG`` at frame 29, which places `MenuFrameAndBg::BackgroundShow`
#: at depth 5, `MenuFrameAndBg::InGameBackgroundHide` at depth 1 and a
#: full-stage button at depth 3.  Cooking frame 0 therefore shipped a main menu
#: with its authored backdrop art missing.  ``MainMenu``'s own `_show`@1 places
#: nothing frame 0 did not, so naming it changes no pixel - it only makes the
#: state explicit instead of incidental.
MOVIE_LAYERS: tuple[tuple[str, str], ...] = (
    ("Background", "_show_FE_BG"),
    ("MainMenu", "_show"),
)
MOVIE_ORDER = tuple(name for name, _label in MOVIE_LAYERS)

#: The exact six-movie shell closure.  ``role`` mirrors the HUD lane's
#: scene/library split.
MOVIE_CLOSURE: tuple[tuple[str, str], ...] = (
    ("AptLevel0", "scene"),
    ("Background", "scene"),
    ("MainMenu", "scene"),
    ("GameWindowGadgets", "library"),
    ("MenuExport", "library"),
    ("MenuFrameAndBg", "library"),
)

#: Native gadget symbols the shell imports that have no APT payload at all.
#: ``View3D`` is the live 3D shellmap host and ``BinkMovie`` is the video host.
NATIVE_GADGET_SYMBOLS = ("BinkMovie", "View3D")

AUTHORED_RESOLUTION = (1024, 768)

MAX_SHELL_SOURCE_BYTES = 32 * 1024 * 1024
MAX_SHELL_SOURCE_COUNT = 1024


class ShellAptConvertError(HudAptConvertError):
    """Raised when a retail shell APT source violates the bounded contract."""


def _normalise(virtual_path: str) -> str:
    return str(virtual_path).replace("\\", "/").strip("/")


def _movie_of(virtual_path: str) -> str | None:
    """Return the closure movie a source virtual path belongs to."""

    relative = _normalise(virtual_path)
    lowered = relative.casefold()
    for name, _role in MOVIE_CLOSURE:
        folded = name.casefold()
        if lowered in {f"{folded}.apt", f"{folded}.const", f"{folded}.dat"}:
            return name
        if lowered.startswith(f"{folded}_geometry/") and lowered.endswith(".ru"):
            return name
        if lowered.startswith(f"art/textures/apt_{folded}_") and lowered.endswith(
            ".tga"
        ):
            return name
    return None


def _required_shell_contract(
    payloads: Mapping[str, bytes], names: Mapping[str, str]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Group the bounded shell sources into per-movie plan rows."""

    if not payloads:
        raise ShellAptConvertError("shell APT bundle has no sources")
    if len(payloads) > MAX_SHELL_SOURCE_COUNT:
        raise ShellAptConvertError("shell APT bundle source count exceeds bounds")
    total = sum(len(value) for value in payloads.values())
    if total > MAX_SHELL_SOURCE_BYTES:
        raise ShellAptConvertError("shell APT bundle payload exceeds bounds")

    grouped: dict[str, dict[str, Any]] = {
        name: {"apt": None, "const": None, "dat": None, "geometry": [], "atlases": []}
        for name, _role in MOVIE_CLOSURE
    }
    for key, payload in payloads.items():
        relative = _normalise(names[key])
        owner = _movie_of(relative)
        if owner is None:
            raise ShellAptConvertError(
                f"shell APT bundle contains an out-of-closure source: {relative}"
            )
        lowered = relative.casefold()
        bucket = grouped[owner]
        if lowered.endswith(".apt"):
            bucket["apt"] = (relative, payload)
        elif lowered.endswith(".const"):
            bucket["const"] = (relative, payload)
        elif lowered.endswith(".dat"):
            bucket["dat"] = (relative, payload)
        elif lowered.endswith(".ru"):
            bucket["geometry"].append((relative, payload))
        elif lowered.endswith(".tga"):
            bucket["atlases"].append((relative, payload))
        else:  # pragma: no cover - _movie_of already rejects other suffixes
            raise ShellAptConvertError(f"unexpected shell source: {relative}")

    raw_movies: list[dict[str, Any]] = []
    atlas_rows: list[dict[str, Any]] = []
    for name, role in MOVIE_CLOSURE:
        bucket = grouped[name]
        for required in ("apt", "const", "dat"):
            if bucket[required] is None:
                raise ShellAptConvertError(f"shell closure lacks {name}.{required}")
        const_path, const_bytes = bucket["const"]
        apt_path, apt_bytes = bucket["apt"]
        dat_path, dat_bytes = bucket["dat"]
        constants = parse_apt_constants(const_bytes, const_path)
        movie = parse_apt_movie(apt_bytes, constants, apt_path)
        image_map = parse_apt_dat(dat_bytes, dat_path)
        geometry = [
            parse_apt_geometry(payload, path)
            for path, payload in sorted(
                bucket["geometry"], key=lambda row: str(row[0]).casefold()
            )
        ]
        movie_atlases: list[dict[str, Any]] = []
        for path, payload in sorted(
            bucket["atlases"], key=lambda row: str(row[0]).casefold()
        ):
            parsed = parse_tga_identity(payload, path)
            digest = str(parsed["sha256"])
            stem = Path(path).stem.casefold().replace("_", "-")
            parsed["resourceId"] = f"shell-atlas-{stem[:40]}-{digest[:8]}"
            parsed["cookedPng"] = f"{ATLAS_DIRECTORY}/{stem}-{digest[:12]}.png"
            movie_atlases.append(parsed)
            atlas_rows.append(parsed)
        raw_movies.append(
            {
                "movie": name,
                "role": role,
                "apt": movie,
                "constants": constants,
                "imageMap": image_map,
                "geometry": geometry,
                "atlases": movie_atlases,
            }
        )
    return raw_movies, atlas_rows


class _ShellFlattener(_Flattener):
    """Flatten the shell root layers instead of the palantir root layers.

    The shell authors far richer ActionScript than the palantir (nested
    ``DefineFunction2`` bodies driving the flyouts).  The bounded HUD decoder
    is deliberately narrow and raises on streams it cannot prove; the shell
    lane records those programs as opaque, unsupported handoffs plus a blocker
    instead of aborting the whole conversion, so the retail display list still
    reaches the runtime while script execution stays fail-closed.
    """

    def _opaque_program(
        self, movie: _Movie, row: Mapping[str, Any], reason: str
    ) -> dict[str, Any]:
        source_offset = int(row["sourceOffset"])
        instruction_offset = int(row["instructionsOffset"])
        return {
            "scriptId": f"{movie.name.casefold()}:{source_offset}",
            "movie": movie.name,
            "actionKind": str(row["kind"]),
            "sourceOffset": source_offset,
            "instructionOffset": instruction_offset,
            "byteLength": 0,
            "sha256": _sha(b""),
            "instructions": [],
            "supported": False,
            "effects": [],
            "maximumStackDepth": 0,
            "terminalStackDepth": 0,
            "unsupportedInstructions": [
                {"reason": "undecodable-action-stream", "detail": reason}
            ],
        }

    def _register_action_program(
        self, movie: _Movie, row: Mapping[str, Any]
    ) -> str:
        script_id = f"{movie.name.casefold()}:{int(row['sourceOffset'])}"
        if script_id in self.action_programs:
            return super()._register_action_program(movie, row)
        try:
            return super()._register_action_program(movie, row)
        except HudAptConvertError as exc:
            self.action_programs[script_id] = self._opaque_program(
                movie, row, str(exc)
            )
            self.block(
                "action-script-stream-not-decodable",
                movie.name,
                scriptId=script_id,
                sourceOffset=int(row["sourceOffset"]),
                instructionOffset=int(row["instructionsOffset"]),
                reason=str(exc),
                parityReady=False,
            )
            return script_id

    def _register_clip_action_program(
        self, movie: _Movie, event: Mapping[str, Any]
    ) -> str:
        program_id = f"{movie.name.casefold()}:clip-event:{int(event['eventOffset'])}"
        if program_id in self.clip_action_programs:
            return super()._register_clip_action_program(movie, event)
        try:
            return super()._register_clip_action_program(movie, event)
        except HudAptConvertError as exc:
            program = self._opaque_program(
                movie,
                {
                    "kind": "clip-action-event",
                    "sourceOffset": int(event["eventOffset"]),
                    "instructionsOffset": int(event["instructionsOffset"]),
                },
                str(exc),
            )
            program["programId"] = program_id
            program["instructionEndOffset"] = int(program["instructionOffset"])
            program.pop("scriptId")
            self.clip_action_programs[program_id] = program
            self.block(
                "clip-action-stream-not-decodable",
                movie.name,
                programId=program_id,
                eventOffset=int(event["eventOffset"]),
                reason=str(exc),
                parityReady=False,
            )
            return program_id

    def flatten(self) -> None:
        roots: list[tuple[str, int]] = []
        for name, label in MOVIE_LAYERS:
            movie = self.movies.get(name.casefold())
            if movie is None:
                self.block("required-root-movie-missing", name)
                continue
            labels = _timeline_labels(movie.frames)
            if label not in labels:
                raise ShellAptConvertError(
                    f"{name} no longer authors the {label} state"
                )
            roots.append((movie.name, int(labels[label])))
        self.flatten_screen(roots)


def _native_gadget_bindings(movies: Mapping[str, _Movie]) -> list[dict[str, Any]]:
    """Record every import of a payload-free native gadget symbol."""

    rows: list[dict[str, Any]] = []
    for movie in movies.values():
        for character_id, (source_movie, symbol) in sorted(movie.imports.items()):
            if symbol not in NATIVE_GADGET_SYMBOLS:
                continue
            rows.append(
                {
                    "movie": movie.name,
                    "characterId": int(character_id),
                    "sourceMovie": source_movie,
                    "symbol": symbol,
                    "runtimeBinding": "native-host-required-no-apt-payload",
                }
            )
    rows.sort(key=lambda row: (str(row["movie"]).casefold(), int(row["characterId"])))
    return rows


def _import_graph(movies: Mapping[str, _Movie]) -> list[dict[str, Any]]:
    graph: list[dict[str, Any]] = []
    for key in sorted(movies):
        movie = movies[key]
        edges = sorted(
            {
                (source_movie, symbol)
                for source_movie, symbol in movie.imports.values()
            }
        )
        graph.append(
            {
                "movie": movie.name,
                "imports": [
                    {"movie": source_movie, "symbol": symbol}
                    for source_movie, symbol in edges
                ],
            }
        )
    return graph


def make_shell_contract(
    movies_list: list[_Movie],
    source: Mapping[str, Any],
) -> dict[str, Any]:
    """Build the bounded shell runtime contract from decoded movies."""

    movies = {movie.name.casefold(): movie for movie in movies_list}
    if len(movies) != len(movies_list):
        raise ShellAptConvertError("shell APT movie identities collide")
    missing = [name for name, _role in MOVIE_CLOSURE if name.casefold() not in movies]
    if missing:
        raise ShellAptConvertError(f"shell APT closure is incomplete: {missing}")

    flattener = _ShellFlattener(movies, {})
    flattener.flatten()

    native_gadgets = _native_gadget_bindings(movies)
    for row in native_gadgets:
        flattener.block(
            "shell-native-gadget-has-no-apt-payload",
            str(row["movie"]),
            symbol=str(row["symbol"]),
            sourceMovie=str(row["sourceMovie"]),
            characterId=int(row["characterId"]),
            parityReady=False,
        )
    if any(row["symbol"] == "View3D" for row in native_gadgets):
        flattener.block(
            "shell-backdrop-requires-native-view3d",
            "Background",
            gate="retail draws a live 3D shellmap through the View3D gadget",
            requiredObservations=[
                "shellmap-map-selection",
                "shellmap-camera-path",
                "shellmap-scene-render-target",
            ],
            substituteArtAllowed=False,
            parityReady=False,
        )

    timelines = [flattener.timelines[key] for key in sorted(flattener.timelines)]
    timeline_frame_count = sum(int(timeline["frameCount"]) for timeline in timelines)
    if timeline_frame_count > MAX_TIMELINE_FRAMES:
        raise ShellAptConvertError("shell timeline frame count exceeds bounds")
    complete_timelines = [
        timeline for timeline in timelines if timeline["displayListComplete"]
    ]
    if complete_timelines:
        flattener.block(
            "timeline-playback-not-bound",
            "shell APT closure",
            timelineIds=[timeline["timelineId"] for timeline in complete_timelines],
            selectionPolicy="static-frame-zero-only",
        )

    blockers = sorted(
        flattener.blockers,
        key=lambda item: json.dumps(item, sort_keys=True, separators=(",", ":")),
    )
    atlas_paths = sorted(
        {
            str(atlas["cookedPng"])
            for movie in movies_list
            for atlas in movie.atlases.values()
        },
        key=str.casefold,
    )
    action_programs = [
        flattener.action_programs[key] for key in sorted(flattener.action_programs)
    ]
    clip_action_programs = [
        flattener.clip_action_programs[key]
        for key in sorted(flattener.clip_action_programs)
    ]
    clip_actions = sorted(
        flattener.clip_actions,
        key=lambda item: json.dumps(item, sort_keys=True, separators=(",", ":")),
    )
    font_definitions = [
        flattener.font_definitions[key] for key in sorted(flattener.font_definitions)
    ]
    text_definitions = [
        flattener.text_definitions[key] for key in sorted(flattener.text_definitions)
    ]
    text_instances = sorted(
        flattener.text_instances,
        key=lambda item: json.dumps(item, sort_keys=True, separators=(",", ":")),
    )
    button_definitions = [
        flattener.button_definitions[key]
        for key in sorted(flattener.button_definitions)
    ]
    button_instances = sorted(
        flattener.button_instances,
        key=lambda item: json.dumps(item, sort_keys=True, separators=(",", ":")),
    )
    vm_bytecode_programs = [
        program
        for program in (*action_programs, *clip_action_programs)
        if "vmBytecode" in program
    ]
    vm_constants: dict[str, Any] = {}
    for program in vm_bytecode_programs:
        movie_key = str(program["movie"]).casefold()
        if movie_key in vm_constants:
            continue
        movie = movies[movie_key]
        vm_constants[movie_key] = {
            "movie": movie.name,
            "sha256": str(movie.constants["sha256"]),
            "entries": [
                {"type": int(entry["type"]), "value": entry.get("value")}
                for entry in movie.constants["entries"]
            ],
        }
    vm_constants = {key: vm_constants[key] for key in sorted(vm_constants)}

    contract: dict[str, Any] = {
        "schema": OUTPUT_SCHEMA,
        "schemaVersion": OUTPUT_SCHEMA_VERSION,
        "sceneId": SCENE_ID,
        "authoredResolution": list(AUTHORED_RESOLUTION),
        "renderPolicy": {
            "actionScriptExecuted": False,
            "boundedActionScriptSubsetExecuted": False,
            "defaultRuntimeMode": "fail-closed",
            "staticSubsetRequiresExplicitOptIn": True,
            "syntheticFallbackAllowed": False,
            "exactTimelineDisplayLists": True,
            "timelinePlaybackBound": False,
            "nativeBackdropBound": False,
            "nativeGadgetsBound": False,
        },
        "source": dict(source),
        "layers": list(MOVIE_ORDER),
        "movies": [
            {"movie": name, "role": role}
            for name, role in MOVIE_CLOSURE
        ],
        "importGraph": _import_graph(movies),
        "nativeGadgets": native_gadgets,
        "atlases": atlas_paths,
        "draws": flattener.draws,
        "timelines": timelines,
        "timelineInstances": sorted(
            flattener.timeline_instances,
            key=lambda item: json.dumps(item, sort_keys=True, separators=(",", ":")),
        ),
        "actionScripts": action_programs,
        "clipActionPrograms": clip_action_programs,
        "vmConstants": vm_constants,
        "clipActions": clip_actions,
        "fonts": font_definitions,
        "texts": text_definitions,
        "textInstances": text_instances,
        "buttons": button_definitions,
        "buttonInstances": button_instances,
        "unsupportedSemantics": blockers,
        "summary": {
            "atlasCount": len(atlas_paths),
            "blockerCount": len(blockers),
            "drawCount": len(flattener.draws),
            "solidTriangleCount": sum(
                draw["kind"] == "solid-triangle" for draw in flattener.draws
            ),
            "texturedTriangleCount": sum(
                draw["kind"] == "textured-triangle" for draw in flattener.draws
            ),
            "movieCount": len(MOVIE_CLOSURE),
            "layerCount": len(MOVIE_ORDER),
            "timelineCount": len(timelines),
            "timelineFrameCount": timeline_frame_count,
            "timelineInstanceCount": len(flattener.timeline_instances),
            "actionScriptCount": len(action_programs),
            "supportedActionScriptCount": sum(
                bool(program["supported"]) for program in action_programs
            ),
            "clipActionProgramCount": len(clip_action_programs),
            "clipActionCount": len(clip_actions),
            "vmBytecodeProgramCount": len(vm_bytecode_programs),
            "vmConstantsMovieCount": len(vm_constants),
            "fontCount": len(font_definitions),
            "textCount": len(text_definitions),
            "textInstanceCount": len(text_instances),
            "displayItemCount": flattener.display_order,
            "buttonCount": len(button_definitions),
            "buttonInstanceCount": len(button_instances),
            "nativeGadgetCount": len(native_gadgets),
            "staticSubsetReady": bool(flattener.draws),
            "parityReady": not blockers and bool(flattener.draws),
        },
    }
    contract["aggregateSha256"] = canonical_sha256(contract)
    return contract


def convert_shell_apt_bundle(
    sources: Mapping[str, Path | str | bytes | bytearray],
    output_directory: Path | str,
    *,
    expected_source_aggregate_sha256: str | None = None,
) -> dict[str, Any]:
    """Convert the bounded shell source closure into pack outputs.

    Mirrors :func:`retail_hud_apt_convert.convert_hud_apt_bundle`: atlas PNG
    paths derive solely from the retail movie/texture identity and source hash,
    and the runtime contract lands at :data:`RUNTIME_OUTPUT_PATH`.
    """

    payloads, names = _source_mapping(sources)
    raw_movies, atlas_rows = _required_shell_contract(payloads, names)
    inventory = [
        {
            "virtualPath": names[key],
            "byteLength": len(payloads[key]),
            "sha256": _sha(payloads[key]),
        }
        for key in sorted(
            payloads, key=lambda item: (names[item].casefold(), names[item])
        )
    ]
    source_aggregate = canonical_sha256(inventory)
    if (
        expected_source_aggregate_sha256 is not None
        and source_aggregate != expected_source_aggregate_sha256
    ):
        raise ShellAptConvertError("shell APT source aggregate changed")
    movies = [_movie_from_plan(raw, source_bytes=payloads) for raw in raw_movies]
    contract = make_shell_contract(
        movies,
        {
            "sourceAggregateSha256": source_aggregate,
            "sourceCount": len(inventory),
            "sourcePayloadBytes": sum(row["byteLength"] for row in inventory),
        },
    )
    output = Path(output_directory)
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ShellAptConvertError("shell APT output directory must be empty")
    output.mkdir(parents=True, exist_ok=True)
    try:
        import PIL
        from PIL import Image
    except ImportError as exc:  # pragma: no cover - environment guard
        raise ShellAptConvertError(
            "Pillow is required for shell atlas conversion"
        ) from exc
    if PIL.__version__ != "12.2.0":
        raise ShellAptConvertError(
            "Pillow 12.2.0 is required for deterministic shell atlases; "
            f"found {PIL.__version__}"
        )
    for atlas in atlas_rows:
        key = _normalise(str(atlas["virtualPath"])).casefold()
        target = output / Path(*str(atlas["cookedPng"]).split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(BytesIO(payloads[key])) as opened:
            opened.convert("RGBA").save(
                target, format="PNG", compress_level=9, optimize=False
            )
    contract_path = output / Path(*RUNTIME_OUTPUT_PATH.split("/"))
    contract_path.parent.mkdir(parents=True, exist_ok=True)
    contract_path.write_bytes(_canonical_bytes(contract))
    return contract


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effective-assets", required=True)
    parser.add_argument("--output", required=True)
    return parser


def shell_source_virtual_paths(asset_root: Path | str) -> list[str]:
    """Return the bounded shell source closure below an effective-assets root."""

    root = Path(asset_root).expanduser().resolve()
    paths: list[str] = []
    for name, _role in MOVIE_CLOSURE:
        for suffix in ("apt", "const", "dat"):
            candidate = root / f"{name}.{suffix}"
            if not candidate.is_file():
                raise ShellAptConvertError(f"shell closure lacks {name}.{suffix}")
            paths.append(f"{name}.{suffix}")
        geometry = root / f"{name}_geometry"
        if geometry.is_dir():
            paths.extend(
                f"{name}_geometry/{item.name}"
                for item in sorted(geometry.glob("*.ru"), key=lambda p: p.name.casefold())
            )
        textures = root / "art" / "Textures"
        if textures.is_dir():
            paths.extend(
                f"art/Textures/{item.name}"
                for item in sorted(
                    textures.glob(f"apt_{name}_*.tga"), key=lambda p: p.name.casefold()
                )
            )
    return sorted(paths, key=str.casefold)


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    root = Path(args.effective_assets).expanduser().resolve()
    sources = {
        relative: root.joinpath(*relative.split("/"))
        for relative in shell_source_virtual_paths(root)
    }
    contract = convert_shell_apt_bundle(sources, args.output)
    print(
        json.dumps(
            {
                "aggregateSha256": contract["aggregateSha256"],
                "summary": contract["summary"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())


__all__ = [
    "ATLAS_DIRECTORY",
    "MOVIE_CLOSURE",
    "MOVIE_ORDER",
    "OUTPUT_SCHEMA",
    "OUTPUT_SCHEMA_VERSION",
    "RUNTIME_OUTPUT_PATH",
    "SCENE_ID",
    "ShellAptConvertError",
    "convert_shell_apt_bundle",
    "make_shell_contract",
    "shell_source_virtual_paths",
]
