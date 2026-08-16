from __future__ import annotations

import pytest

from openbfme_importer.playable_unit_pack_compiler import (
    PlayableUnitPackCompilerError,
    _animation_nonnegative_integer_property,
    _animation_speed_factor_range,
)
from openbfme_importer.typed_visual_graph import resolve_typed_visual_documents
from openbfme_importer.w3d_index import W3DFileHeaders, build_w3d_index


def _closure(value: str = "0.9 1.1") -> dict[str, object]:
    provenance = {
        "definingObject": "FixtureUnit",
        "inheritanceDistance": 0,
        "virtualPath": "data/ini/object/fixture.ini",
        "line": 9,
        "scopePath": [
            "W3DScriptedModelDraw ModuleTag_Draw",
            "AnimationState MOVING",
            "Animation Run",
        ],
    }
    return {
        "objects": [
            {
                "name": "FixtureUnit",
                "drawModules": [
                    {
                        "states": [
                            {
                                "properties": [
                                    {
                                        "key": "AnimationSpeedFactorRange",
                                        "value": value,
                                        "provenance": provenance,
                                    }
                                ]
                            }
                        ]
                    }
                ],
            }
        ]
    }


def _animation_row() -> dict[str, object]:
    return {
        "targetObject": "FixtureUnit",
        "provenance": {
            "definingObject": "FixtureUnit",
            "inheritanceDistance": 0,
            "virtualPath": "data/ini/object/fixture.ini",
            "line": 8,
            "scopePath": [
                "W3DScriptedModelDraw ModuleTag_Draw",
                "AnimationState MOVING",
                "Animation Run",
            ],
        },
    }


def test_authored_animation_speed_range_uses_exact_animation_scope() -> None:
    assert _animation_speed_factor_range(_closure(), _animation_row()) == (0.9, 1.1)
    other = _animation_row()
    other["provenance"] = {
        **other["provenance"],
        "scopePath": [
            "W3DScriptedModelDraw ModuleTag_Draw",
            "AnimationState MOVING",
            "Animation Walk",
        ],
    }
    assert _animation_speed_factor_range(_closure(), other) is None


@pytest.mark.parametrize("value", ["1.0", "fast slow", "0 1", "1.2 0.8"])
def test_authored_animation_speed_range_fails_closed(value: str) -> None:
    with pytest.raises(PlayableUnitPackCompilerError, match="AnimationSpeedFactorRange"):
        _animation_speed_factor_range(_closure(value), _animation_row())


@pytest.mark.parametrize(
    ("property_name", "raw", "expected"),
    [
        ("AnimationBlendTime", "0", 0),
        ("AnimationBlendTime", "1666", 1666),
        ("AnimationPriority", "0", 0),
        ("AnimationPriority", "500", 500),
    ],
)
def test_integer_animation_property_uses_exact_scope_and_preserves_receipt(
    property_name: str, raw: str, expected: int
) -> None:
    closure = _closure()
    prop = closure["objects"][0]["drawModules"][0]["states"][0]["properties"][0]
    prop["key"] = property_name
    prop["value"] = raw
    parsed = _animation_nonnegative_integer_property(
        closure, _animation_row(), property_name
    )
    assert parsed is not None
    value, receipts = parsed
    assert value == expected
    assert len(receipts) == 1
    assert receipts[0]["provenance"]["line"] == 9


def test_integer_animation_property_does_not_leak_across_defining_object() -> None:
    closure = _closure()
    prop = closure["objects"][0]["drawModules"][0]["states"][0]["properties"][0]
    prop["key"] = "AnimationBlendTime"
    prop["value"] = "15"
    prop["provenance"]["definingObject"] = "OtherAncestor"
    assert (
        _animation_nonnegative_integer_property(
            closure, _animation_row(), "AnimationBlendTime"
        )
        is None
    )


@pytest.mark.parametrize("raw", ["-1", "1.5", "10 ms", "", "2147483648"])
def test_integer_animation_property_fails_closed(raw: str) -> None:
    closure = _closure()
    prop = closure["objects"][0]["drawModules"][0]["states"][0]["properties"][0]
    prop["key"] = "AnimationPriority"
    prop["value"] = raw
    with pytest.raises(PlayableUnitPackCompilerError, match="AnimationPriority"):
        _animation_nonnegative_integer_property(
            closure, _animation_row(), "AnimationPriority"
        )


def test_repeated_animation_scalar_uses_last_authored_value() -> None:
    """SAGE accepts repeated scalar keys in one Animation; the last wins.

    Retail IsengardUrukCrossbow authors AnimationPriority = 1 followed by 4
    inside the same IDLE block.  Treating that legal override as ambiguity
    drops both the member and its horde from the faction pack.
    """
    closure = _closure()
    properties = closure["objects"][0]["drawModules"][0]["states"][0]["properties"]
    properties[0]["key"] = "AnimationPriority"
    properties[0]["value"] = "1"
    conflicting = dict(properties[0])
    conflicting["value"] = "4"
    conflicting["provenance"] = {
        **conflicting["provenance"],
        "line": 11,
    }
    properties.append(conflicting)
    parsed = _animation_nonnegative_integer_property(
        closure, _animation_row(), "AnimationPriority"
    )
    assert parsed is not None
    value, receipts = parsed
    assert value == 4
    assert [row["provenance"]["line"] for row in receipts] == [9, 11]


def test_anonymous_sibling_animation_blocks_keep_exact_property_ownership() -> None:
    """Retail NeutralWarg authors two anonymous idle Animation occurrences."""

    documents = {
        "entry.ini": b"""
Object FixtureUnit
  Draw = W3DHordeModelDraw ModuleTag_Draw
    IdleAnimationState
      Animation
        AnimationName = TEST_SKL.IDLA
        AnimationPriority = 20
      End
      Animation
        AnimationName = TEST_SKL.IDLB
        AnimationPriority = 1
      End
    End
  End
End
"""
    }
    index = build_w3d_index(
        ["art/test_skl.w3d"],
        [
            W3DFileHeaders(
                "art/test_skl.w3d",
                animation_ids=("TEST_SKL.IDLA", "TEST_SKL.IDLB"),
            )
        ],
    )
    graph = resolve_typed_visual_documents(
        "entry.ini", documents, ["FixtureUnit"], index
    )
    closure = graph.neutral()
    references = [
        reference.neutral()
        for reference in graph.references
        if reference.kind == "animation"
    ]
    for reference in references:
        reference["targetObject"] = "FixtureUnit"

    resolved = {
        reference["identifier"]: _animation_nonnegative_integer_property(
            closure, reference, "AnimationPriority"
        )
        for reference in references
    }
    assert {key: value[0] for key, value in resolved.items()} == {
        "TEST_SKL.IDLA": 20,
        "TEST_SKL.IDLB": 1,
    }
    assert (
        references[0]["provenance"]["scopePath"]
        != references[1]["provenance"]["scopePath"]
    )
