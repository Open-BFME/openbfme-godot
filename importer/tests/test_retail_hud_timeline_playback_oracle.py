from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest

from openbfme_importer.retail_hud_timeline_playback_oracle import build_contract


ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "workspace" / "scratch" / "hud-261-source-conversion"
PLAN = BASE / "plan-a.json"
SCENE = BASE / "bundle-a" / "data" / "ui" / "palantir" / "scene-contract.json"
ASSETS = ROOT / "workspace" / "retail-work" / "cache" / "effective-assets"

pytestmark = pytest.mark.skipif(
    not PLAN.is_file() or not SCENE.is_file(),
    reason="private 261-source HUD inputs are absent",
)


def _inputs() -> tuple[dict, dict]:
    return (
        json.loads(PLAN.read_text(encoding="utf-8")),
        json.loads(SCENE.read_text(encoding="utf-8")),
    )


def _build(plan: dict | None = None, scene: dict | None = None) -> dict:
    source_plan, source_scene = _inputs()
    return build_contract(
        plan if plan is not None else source_plan,
        scene if scene is not None else source_scene,
        ASSETS,
    )


def test_exact_timeline_playback_census_is_deterministic() -> None:
    first = _build()
    assert first == _build()
    assert first["summary"] == {
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
    }


def test_exact_initial_control_and_goto_targets_are_retained() -> None:
    by_id = {row["timelineId"]: row for row in _build()["timelines"]}
    assert by_id["palantir:309"]["initialControl"] == (
        "explicit-stop-on-frame-zero"
    )
    assert all(
        row["initialControl"]
        == "no-explicit-control-opcode-engine-default-unresolved"
        for key, row in by_id.items()
        if key != "palantir:309"
    )
    gotos = {
        (row["timelineId"], row["frameIndex"]): (
            row["effects"][0]["target"],
            row["resolvedGotoFrame"],
        )
        for timeline in by_id.values()
        for row in timeline["actionFrames"]
        if [effect["kind"] for effect in row["effects"]] == ["goto", "play"]
    }
    assert gotos == {
        ("palantir:257", 34): ("_show", 0),
        ("palantir:293", 35): ("_up", 10),
        ("palantir:293", 51): ("_up", 10),
        ("palantir:309", 57): ("_stop", 0),
    }


def test_source_order_is_exact_but_runtime_phase_is_not_invented() -> None:
    contract = _build()
    mixed = contract["mixedActionMutationFrames"]
    assert len(mixed) == 11
    frame_57 = next(
        row
        for row in mixed
        if row["timelineId"] == "palantir:309" and row["frameIndex"] == 57
    )
    assert frame_57["mutationsBeforeFirstAction"] == 1
    assert frame_57["mutationsAfterLastAction"] == 9
    assert frame_57["sourceItems"][0]["kind"] == "remove-object"
    assert frame_57["sourceItems"][1]["scriptId"] == "palantir:358480"
    assert frame_57["runtimePhaseOrder"] == "unresolved-retail-scheduler-gate"
    assert len(contract["unresolvedSchedulerGates"]) == 5


def test_changed_timeline_frame_fails_closed() -> None:
    plan, scene = _inputs()
    changed = copy.deepcopy(scene)
    changed["timelines"][0]["frames"][0]["frameIndex"] = 7
    with pytest.raises(ValueError, match="frame index changed"):
        _build(plan, changed)
