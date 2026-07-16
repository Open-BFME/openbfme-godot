"""Build the payload-free HUD timeline-playback evidence contract.

The oracle inventories only the 22 reachable BFME2 HUD timelines.  It records
source item order and exact typed timeline-control bytecode, while keeping the
retail scheduler behaviors that are not encoded in APT data as explicit trace
gates.  It neither implements a clock nor assumes Flash-player defaults.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Mapping

from .retail_hud_apt_convert import _movie_from_plan


SCHEMA = "openbfme.private-hud-timeline-playback-oracle"
PLAN_AGGREGATE_SHA256 = (
    "d8850c6033b8ae3041e044246ab216550b6eabb1d8cce0397006f936066c36c4"
)
SCENE_AGGREGATE_SHA256 = (
    "0b146e729bdb34d598cc82afa530b1182350b14f838e6c91af0d0b14f1ce4b43"
)
SOURCE_AGGREGATE_SHA256 = (
    "f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599"
)

EXPECTED_TIMELINES = {
    "ingamesidecommandbar:18": 21,
    "libingameui:23": 20,
    "palantir:50": 50,
    "palantir:52": 20,
    "palantir:55": 35,
    "palantir:57": 20,
    "palantir:86": 19,
    "palantir:89": 20,
    "palantir:93": 28,
    "palantir:98": 28,
    "palantir:105": 49,
    "palantir:114": 19,
    "palantir:121": 20,
    "palantir:125": 19,
    "palantir:127": 19,
    "palantir:139": 29,
    "palantir:256": 30,
    "palantir:257": 35,
    "palantir:260": 20,
    "palantir:275": 20,
    "palantir:293": 61,
    "palantir:309": 58,
}

EXPECTED_GOTOS = {
    ("palantir:257", 34): ("_show", 0),
    ("palantir:293", 35): ("_up", 10),
    ("palantir:293", 51): ("_up", 10),
    ("palantir:309", 57): ("_stop", 0),
}


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _sha(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _private_root(path: Path | str) -> Path:
    root = Path(path).resolve()
    if ".private" not in {part.casefold() for part in root.parts}:
        raise ValueError("asset root must remain under .private")
    return root


def _source_item(row: Mapping[str, Any], script_id: str | None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "kind": str(row.get("kind", "")),
        "sourceOffset": int(row.get("sourceOffset", -1)),
    }
    if script_id is not None:
        result["scriptId"] = script_id
    for key in ("depth", "characterId", "name", "frameId"):
        if key in row:
            result[key] = row[key]
    return result


def _labels(timeline: Mapping[str, Any]) -> dict[str, int]:
    result: dict[str, int] = {}
    for frame in timeline.get("frames", []):
        frame_index = int(frame.get("frameIndex", -1))
        for label in frame.get("labels", []):
            name = str(label.get("name", ""))
            if not name or name in result or int(label.get("frameId", -1)) != frame_index:
                raise ValueError("timeline label identity changed")
            result[name] = frame_index
    return result


def _validate_source_frame(
    timeline_id: str,
    scene_frame: Mapping[str, Any],
    source_rows: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    actions = {
        int(row.get("sourceOffset", -1)): str(row.get("scriptId", ""))
        for row in scene_frame.get("actionScripts", [])
    }
    source_action_offsets = [
        int(row["sourceOffset"])
        for row in source_rows
        if row["kind"] in {"action-script", "init-action-script"}
    ]
    if sorted(actions) != sorted(source_action_offsets):
        raise ValueError(f"source action order changed: {timeline_id}")
    source_operation_offsets = [
        int(row["sourceOffset"])
        for row in source_rows
        if row["kind"] in {"place-object", "remove-object"}
    ]
    scene_operation_offsets = [
        int(row.get("sourceOffset", -1)) for row in scene_frame.get("operations", [])
    ]
    if source_operation_offsets != scene_operation_offsets:
        raise ValueError(f"source display-list operation order changed: {timeline_id}")
    source_labels = [
        (int(row["sourceOffset"]), str(row.get("name", "")))
        for row in source_rows
        if row["kind"] == "frame-label"
    ]
    scene_labels = [
        (int(row.get("sourceOffset", -1)), str(row.get("name", "")))
        for row in scene_frame.get("labels", [])
    ]
    if source_labels != scene_labels:
        raise ValueError(f"source label order changed: {timeline_id}")
    compact = [
        _source_item(row, actions.get(int(row["sourceOffset"]))) for row in source_rows
    ]
    mixed: list[dict[str, Any]] = []
    action_indexes = [
        index
        for index, row in enumerate(source_rows)
        if row["kind"] in {"action-script", "init-action-script"}
    ]
    mutation_indexes = [
        index
        for index, row in enumerate(source_rows)
        if row["kind"] in {"place-object", "remove-object"}
    ]
    if action_indexes and mutation_indexes:
        first_action = min(action_indexes)
        last_action = max(action_indexes)
        mixed.append(
            {
                "timelineId": timeline_id,
                "frameIndex": int(scene_frame["frameIndex"]),
                "sourceItems": compact,
                "mutationsBeforeFirstAction": sum(
                    index < first_action for index in mutation_indexes
                ),
                "mutationsAfterLastAction": sum(
                    index > last_action for index in mutation_indexes
                ),
                "runtimePhaseOrder": "unresolved-retail-scheduler-gate",
            }
        )
    return compact, mixed


def _action_row(
    timeline_id: str,
    frame_index: int,
    source_order: int,
    binding: Mapping[str, Any],
    script: Mapping[str, Any],
    labels: Mapping[str, int],
) -> dict[str, Any]:
    effects = script.get("effects", [])
    result: dict[str, Any] = {
        "timelineId": timeline_id,
        "frameIndex": frame_index,
        "sourceOrder": source_order,
        "sourceOffset": int(binding.get("sourceOffset", -1)),
        "scriptId": str(binding.get("scriptId", "")),
        "instructionOffset": int(script.get("instructionOffset", -1)),
        "byteLength": int(script.get("byteLength", -1)),
        "sha256": str(script.get("sha256", "")),
        "supported": bool(script.get("supported", False)),
        "effects": effects,
    }
    if not result["supported"]:
        result["gate"] = {
            "unsupportedInstructions": script.get("unsupportedInstructions", []),
            "policy": "do-not-advance-past-this-frame-without-typed-handler",
        }
    for effect in effects:
        if effect.get("kind") != "goto":
            continue
        target = str(effect.get("target", ""))
        if effect.get("targetType") != "label" or target not in labels:
            raise ValueError(f"goto target changed: {timeline_id}:{frame_index}")
        result["resolvedGotoFrame"] = labels[target]
    return result


def build_contract(
    plan: Mapping[str, Any],
    scene: Mapping[str, Any],
    asset_root: Path | str,
) -> dict[str, Any]:
    """Return the exact playback evidence without selecting scheduler policy."""

    if plan.get("aggregateSha256") != PLAN_AGGREGATE_SHA256:
        raise ValueError("261-source HUD plan identity changed")
    if scene.get("aggregateSha256") != SCENE_AGGREGATE_SHA256:
        raise ValueError("261-source HUD scene identity changed")
    source = scene.get("source", {})
    if (
        source.get("sourceAggregateSha256") != SOURCE_AGGREGATE_SHA256
        or source.get("sourceCount") != 261
        or source.get("sourcePayloadBytes") != 10700284
    ):
        raise ValueError("261-source HUD closure changed")
    summary = scene.get("summary", {})
    if (
        summary.get("timelineCount") != 22
        or summary.get("timelineFrameCount") != 640
        or summary.get("timelineInstanceCount") != 51
    ):
        raise ValueError("reachable timeline census changed")

    root = _private_root(asset_root)
    raw_movies = {
        str(row.get("movie", "")).casefold(): row
        for row in plan.get("sceneContract", {}).get("movies", [])
    }
    required_movies = {timeline_id.split(":", 1)[0] for timeline_id in EXPECTED_TIMELINES}
    if not required_movies.issubset(raw_movies):
        raise ValueError("required playback movie source changed")
    movies = {
        name: _movie_from_plan(raw_movies[name], asset_root=root)
        for name in required_movies
    }
    if any(movie.root.get("millisecondsPerFrame") != 33 for movie in movies.values()):
        raise ValueError("HUD authored frame interval changed")

    scene_timelines = {
        str(row.get("timelineId", "")): row for row in scene.get("timelines", [])
    }
    if set(scene_timelines) != set(EXPECTED_TIMELINES):
        raise ValueError("reachable timeline identity set changed")
    instances: dict[str, list[str]] = defaultdict(list)
    for row in scene.get("timelineInstances", []):
        instances[str(row.get("timelineId", ""))].append(str(row.get("path", "")))
    if sum(len(paths) for paths in instances.values()) != 51:
        raise ValueError("reachable timeline instance set changed")
    scripts = {
        str(row.get("scriptId", "")): row for row in scene.get("actionScripts", [])
    }

    timeline_contracts: list[dict[str, Any]] = []
    all_actions: list[dict[str, Any]] = []
    mixed_frames: list[dict[str, Any]] = []
    initial_counts: Counter[str] = Counter()
    for timeline_id in sorted(EXPECTED_TIMELINES):
        timeline = scene_timelines[timeline_id]
        frame_count = EXPECTED_TIMELINES[timeline_id]
        if (
            int(timeline.get("frameCount", -1)) != frame_count
            or len(timeline.get("frames", [])) != frame_count
            or not timeline.get("displayListComplete", False)
        ):
            raise ValueError(f"timeline shape changed: {timeline_id}")
        movie_name, character_text = timeline_id.split(":", 1)
        character_id = int(character_text)
        character = movies[movie_name].characters[character_id]
        source_frames = character.get("frames", [])
        if len(source_frames) != frame_count:
            raise ValueError(f"source timeline frame count changed: {timeline_id}")
        if any(
            int(frame.get("frameIndex", -1)) != frame_index
            for frame_index, frame in enumerate(timeline.get("frames", []))
        ):
            raise ValueError(f"frame index changed: {timeline_id}")
        label_map = _labels(timeline)
        frame_hashes: list[str] = []
        timeline_actions: list[dict[str, Any]] = []
        for frame_index, (scene_frame, source_rows) in enumerate(
            zip(timeline.get("frames", []), source_frames, strict=True)
        ):
            if int(scene_frame.get("frameIndex", -1)) != frame_index:
                raise ValueError(f"frame index changed: {timeline_id}")
            compact, mixed = _validate_source_frame(
                timeline_id, scene_frame, source_rows
            )
            frame_hashes.append(_sha(_canonical(compact)))
            mixed_frames.extend(mixed)
            source_order_by_offset = {
                int(row["sourceOffset"]): index for index, row in enumerate(source_rows)
            }
            for binding in scene_frame.get("actionScripts", []):
                script_id = str(binding.get("scriptId", ""))
                if script_id not in scripts:
                    raise ValueError(f"frame action script missing: {script_id}")
                action = _action_row(
                    timeline_id,
                    frame_index,
                    source_order_by_offset[int(binding["sourceOffset"])],
                    binding,
                    scripts[script_id],
                    label_map,
                )
                timeline_actions.append(action)
                all_actions.append(action)
        frame_zero_effects = [
            effect
            for action in timeline_actions
            if action["frameIndex"] == 0 and action["supported"]
            for effect in action["effects"]
            if effect.get("kind") in {"play", "stop", "goto"}
        ]
        if frame_zero_effects == [{"kind": "stop"}]:
            initial_contract = "explicit-stop-on-frame-zero"
        elif frame_zero_effects:
            raise ValueError(f"unexpected frame-zero control effect: {timeline_id}")
        else:
            initial_contract = "no-explicit-control-opcode-engine-default-unresolved"
        initial_counts[initial_contract] += 1
        timeline_contracts.append(
            {
                "timelineId": timeline_id,
                "movie": str(timeline.get("movie", "")),
                "characterId": character_id,
                "frameCount": frame_count,
                "millisecondsPerFrame": 33,
                "definitionSha256": _sha(_canonical(timeline)),
                "instancePaths": sorted(instances[timeline_id]),
                "instanceCount": len(instances[timeline_id]),
                "labels": [
                    {"name": name, "frameIndex": index}
                    for name, index in sorted(label_map.items(), key=lambda row: row[1])
                ],
                "initialControl": initial_contract,
                "frameSourceOrderSha256": frame_hashes,
                "frameSourceOrderAggregateSha256": _sha(_canonical(frame_hashes)),
                "actionFrames": timeline_actions,
                "implicitEndWrapRequiredByAuthoredSegments": False,
            }
        )

    supported = [row for row in all_actions if row["supported"]]
    blocked = [row for row in all_actions if not row["supported"]]
    effects = Counter(tuple(effect["kind"] for effect in row["effects"]) for row in supported)
    gotos = {
        (row["timelineId"], row["frameIndex"]): (
            str(row["effects"][0]["target"]),
            int(row["resolvedGotoFrame"]),
        )
        for row in supported
        if tuple(effect["kind"] for effect in row["effects"]) == ("goto", "play")
    }
    if (
        len(all_actions) != 66
        or len(supported) != 56
        or len(blocked) != 10
        or effects != Counter({("stop",): 52, ("goto", "play"): 4})
        or gotos != EXPECTED_GOTOS
        or initial_counts
        != Counter(
            {
                "no-explicit-control-opcode-engine-default-unresolved": 21,
                "explicit-stop-on-frame-zero": 1,
            }
        )
        or len(mixed_frames) != 11
    ):
        raise ValueError("timeline playback action census changed")
    if sum(row["mutationsBeforeFirstAction"] > 0 for row in mixed_frames) != 1:
        raise ValueError("mixed frame source ordering changed")

    result: dict[str, Any] = {
        "schema": SCHEMA,
        "source": {
            "planAggregateSha256": PLAN_AGGREGATE_SHA256,
            "sceneAggregateSha256": SCENE_AGGREGATE_SHA256,
            "sourceAggregateSha256": SOURCE_AGGREGATE_SHA256,
            "sourceCount": 261,
            "sourcePayloadBytes": 10700284,
        },
        "summary": {
            "timelineCount": 22,
            "timelineFrameCount": 640,
            "timelineInstanceCount": 51,
            "frameActionCount": 66,
            "supportedFrameActionCount": 56,
            "blockedFrameActionCount": 10,
            "pureStopActionCount": 52,
            "gotoPlayActionCount": 4,
            "mixedActionMutationFrameCount": 11,
            "explicitInitialStopTimelineCount": 1,
            "defaultInitialStateTraceTimelineCount": 21,
            "implicitWrapNeededCount": 0,
            "implementationIncluded": False,
            "schedulerPolicyGuessed": False,
        },
        "timelines": timeline_contracts,
        "mixedActionMutationFrames": mixed_frames,
        "closedSemantics": {
            "instanceIdentity": "51 exact paths; state must be per path even where a timeline definition is shared",
            "authoredFrameInterval": "all three source movies use 33 milliseconds per frame",
            "frameAdvanceData": "next authored frame index is current+1 within each exact frame array",
            "labels": "all goto labels resolve uniquely to exact frame indexes",
            "stop": "sets the addressed timeline playing state false without an authored frame change",
            "play": "sets the addressed timeline playing state true without an authored frame change",
            "gotoPlay": "effects are authored in order: set the exact label target, then set playing true",
            "displayListData": "all 640 cumulative display lists and every source mutation record are exact",
            "implicitWrap": "no authored segment needs last-frame implicit wrap when its exact frame actions execute",
        },
        "unresolvedSchedulerGates": [
            "whether a newly created clip with no frame-zero control opcode begins playing before or after its first render",
            "whether frame actions execute before, after, or in a separate phase from same-frame display-list mutations",
            "whether goto executes the target frame actions immediately, after the current action list, or on the next scheduler step",
            "whether a delayed update catches up multiple 33-ms frames, drops frames, or advances only once",
            "the retail fallback at frameCount if an unsupported action is suppressed; authored production paths never require this wrap",
        ],
        "tracePlan": [
            "freshly create one palantir:52 instance and record frame index, playing state, and first-render order across the first 33-ms boundary",
            "trigger CommandPointsFlash.gotoAndPlay('_go') and record frame-8 action versus its same-frame placement order",
            "trace palantir:309 frame 57, which authors remove before goto/play and nine placements after it, then record when frame-0 stop executes",
            "repeat one transition after a controlled delay exceeding three frame intervals to distinguish catch-up from single-step behavior",
            "repeat the trace twice on BFME2 1.06 before promoting scheduler policy",
        ],
        "safeImplementationSlice": [
            "load one immutable definition per timeline and allocate independent state per exact instance path",
            "resolve only inventoried labels and retain the exact 33-ms source interval as data",
            "apply explicit stop, play, and goto-then-play effects atomically in authored effect order",
            "reuse the already-converted cumulative display list for an explicitly selected frame",
            "do not start an automatic clock, execute target-frame actions recursively, or add generic wrap until the narrow scheduler trace closes those policies",
            "fail closed before each of the ten blocked frame actions unless its separate typed handler is present",
        ],
    }
    result["aggregateSha256"] = _sha(_canonical(result))
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--scene-contract", type=Path, required=True)
    parser.add_argument("--asset-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    contract = build_contract(
        json.loads(args.plan.read_text(encoding="utf-8")),
        json.loads(args.scene_contract.read_text(encoding="utf-8")),
        args.asset_root,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(_canonical(contract))
    print(json.dumps(contract["summary"], sort_keys=True))
    print(contract["aggregateSha256"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
