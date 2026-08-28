"""Convert the bounded BFME2 Palantir APT closure into a Godot draw contract.

This is deliberately not a general ActionScript VM.  It converts declarative
display-list records and the finite, source-proven timeline-control bytecode
used by the bounded Men HUD closure.  Clip events are inventoried exactly and
only deterministic initialize-to-timeline programs and the hash-pinned
resource-flash event intent may execute. Other host calls, external state,
event dispatch, and every opcode outside that exact subset remain blockers.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import re
import struct
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from typing import Any, Iterable, Mapping

from .sage_apt import (
    canonical_sha256,
    parse_apt_constants,
    parse_apt_dat,
    parse_apt_geometry,
    parse_apt_movie,
    parse_tga_identity,
    parse_wnd_layout,
)
from .retail_hud_frame_selection import (
    HudFrameSelectionError,
    _assert_palantir_bytecode,
)


PLAN_SCHEMA = "openbfme.retail-hud-apt-plan"
OUTPUT_SCHEMA = "openbfme.retail-hud-apt-runtime"
SCENE_ID = "bfme2.ui.palantir"
MOVIE_ORDER = ("InGameSideCommandBar", "Palantir")
MOVIE_CLOSURE = (
    "InGameHelpBox",
    "InGameHeroSelect",
    "InGamePlanningMode",
    "InGameSideCommandBar",
    "InGameSpellBook",
    "libInGameImagesMain",
    "libInGameUI",
    "Palantir",
    "PalantirExport",
)
EXTERNAL_MOVIE_LOADS = (
    (0, "InGameSpellBook.swf", "InGameSpellBook", "SpellBookUI"),
    (1, "InGameSideCommandBar.swf", "InGameSideCommandBar", "SideCommandBar"),
    (2, "InGameHelpBox.swf", "InGameHelpBox", "helpBox"),
    (3, "InGameHeroSelect.swf", "InGameHeroSelect", "HeroSelectUI"),
    (4, "InGamePlanningMode.swf", "InGamePlanningMode", "planningModeUI"),
)
PRODUCTION_EXTERNAL_FONT_BINDINGS = (
    {
        "fontId": "palantir:63",
        "fontName": "Albertus MT",
        "resourceId": "men-hud-font-albertus-mt",
        "sourceVirtualPath": "albertusmt.otf",
        "cookedFont": "assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf",
        "sourceSha256": (
            "6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0"
        ),
    },
)
_EXPECTED_FLAGGED_NULL_CLIP_ACTIONS = {
    ("ingameheroselect.apt", 166_756, 0xB6),
}


def register_expected_flagged_null_clip_actions(
    identities: "Iterable[tuple[str, int, int]]",
) -> None:
    """Admit additional exact source-flagged-null PlaceObject records.

    Retail authors a handful of PlaceObject records whose clip-action flag is
    set while the pointer is zero; every one must be admitted BY EXACT
    IDENTITY (casefolded virtual path, record offset, flags byte) or parsing
    fails closed.  The HUD closure needed exactly one; other lanes (the RotWK
    strategic closure measures eight, all in ``TimeLine.apt``) register their
    own measured identities here instead of widening the parser for everyone.
    Identities are keyed by virtual path, so a registration can never change
    what another closure's movies are allowed to contain.
    """

    for identity in identities:
        path, offset, flags = identity
        if (
            not str(path)
            or str(path) != str(path).casefold()
            or not isinstance(offset, int)
            or offset < 0
            or not isinstance(flags, int)
            or not 0 <= flags <= 0xFF
        ):
            raise HudAptConvertError(
                f"invalid flagged-null clip-action identity: {identity!r}"
            )
        _EXPECTED_FLAGGED_NULL_CLIP_ACTIONS.add((str(path), offset, flags))
MAX_RECURSION = 64
MAX_DRAWS = 100_000
MAX_TIMELINES = 4_096
MAX_TIMELINE_FRAMES = 100_000
MAX_ACTION_INSTRUCTIONS = 100_000
MAX_CLIP_ACTIONS = 100_000
PRODUCTION_ATLAS_COUNT = 24
PRODUCTION_DRAW_COUNT = 28
# The four externally loaded movies are bound to exact Palantir child slots.
# Their unresolved completion/visibility/teardown observations are retained as
# one lifecycle-capture blocker. The source-flagged HeroSelect null clip-action
# pointer remains separate. The exact copied Albertus font and three
# byte-identity-bound live strings are executable; their seven opaque font/GPU
# questions remain one rendered-capture blocker.
# The typed Men/Fords selection adapter now owns the exact side-command FadeIn
# state machine. The broad fade-runtime blocker is therefore removed; the
# nondefault Palantir selector and generic timeline-playback blockers remain.
PRODUCTION_BLOCKER_COUNT = 19
PRODUCTION_SOURCE_COUNT = 261
# 24 atlases + contract + font, plus the 157 authored per-image piece crops
# (atlasPieces — the sheet split by UV footprint, 2026-08-26).
PRODUCTION_OUTPUT_COUNT = 183

PRODUCTION_MEN_FORDS_RETAIL_INI_SHA256 = {
    "data/ini/commandset.ini": "3d57ff841b93428ce2118d4bff1871684003bb9eacd8d48865f03ce23e4c5300",
    "data/ini/commandbutton.ini": "bd1af6bedd22acd39bd7571011ac153bd6c5e93543e5f487866e81652c9899c0",
    "data/ini/object/goodfaction/hordes/men/menhordes.ini": (
        "5f73cdc4627d9a745fdfcf79be2d3d8379e3e9180595e98b0df62363645516b9"
    ),
    "data/ini/object/goodfaction/structures/men/fortress.ini": (
        "6d8030714f46bc147fe55adb9a3f101aabe5a773e6b2fa50c479783b7bdb18a0"
    ),
    "data/ini/object/goodfaction/structures/men/farm.ini": (
        "b3a243f0eb887d4127f9596e90a7b2a41e4f482cafe5ec2e538a4fd99d1c941c"
    ),
    "data/ini/object/goodfaction/structures/men/barracks.ini": (
        "e91c4d73a80f51e77c9b6fdce063899fc2195a6abd94eff72f9d6f94531158a7"
    ),
    "data/ini/object/goodfaction/structures/men/archerrange.ini": (
        "fc0caf596dfd74dcff21bbf645b24f92027d33e91c3185c577f78dcacddd99d6"
    ),
    "data/ini/object/goodfaction/structures/men/stable.ini": (
        "d9a04b56739d02fb51545a1bcaac9e8b615bfa0650ef8926cbb1f9c7665ff506"
    ),
}
_MEN_FORDS_ROSTER_COMMAND_SETS = (
    ("battalion", "unit_type", "bfme2.object.gondor-fighter-horde", "GondorFighterHorde", "GondorFighterHordeCommandSet", "data/ini/object/goodfaction/hordes/men/menhordes.ini"),
    ("battalion", "unit_type", "bfme2.object.gondor-tower-guard", "GondorTowerShieldGuardHorde", "GondorTowerShieldGuardCommandSet", "data/ini/object/goodfaction/hordes/men/menhordes.ini"),
    ("battalion", "unit_type", "bfme2.object.gondor-archer", "GondorArcherHorde", "GondorArcherHordeCommandSet", "data/ini/object/goodfaction/hordes/men/menhordes.ini"),
    ("battalion", "unit_type", "bfme2.object.gondor-knight", "GondorKnightHorde", "GondorKnightHordeCommandSet", "data/ini/object/goodfaction/hordes/men/menhordes.ini"),
    ("structure", "structure_kind", "fortress", "MenFortressCitadel", "MenFortressCommandSet", "data/ini/object/goodfaction/structures/men/fortress.ini"),
    ("structure", "structure_kind", "farm", "GondorFarm", "SellableCommandSet", "data/ini/object/goodfaction/structures/men/farm.ini"),
    ("structure", "structure_kind", "barracks", "GondorBarracks", "GondorBarracksCommandSet", "data/ini/object/goodfaction/structures/men/barracks.ini"),
    ("structure", "structure_kind", "archery_range", "GondorArcherRange", "GondorArcheryCommandSet", "data/ini/object/goodfaction/structures/men/archerrange.ini"),
    ("structure", "structure_kind", "stable", "GondorStable", "GondorStablesCommandSet", "data/ini/object/goodfaction/structures/men/stable.ini"),
)

_WND_SOURCE_SHA256 = "a509730457224a111af8022df6d0ef373fcaa5d91a102bc15bccf5fc1a54ced6"
_WND_ORACLE_AGGREGATES = {
    "callback": "ad97b6c02ed6a46eec745adda4434264b84dcc969b7c46115f6a8a6458d33662",
    "message": "238e9de43c8ebae4a22de1f7b04c4ced3933dbe3328c83ffa44d805b5336274c",
    "draw": "748ad63a218497f9ff9565b1b8078a165c90fd75dc7d39335d46a6edd4f3c484",
}
_WND_IMPLEMENTED_CALLBACKS = (
    "ControlBarInput",
    "ControlBarSystem",
    "GameWinBlockInput",
    "LeftHUDInput",
    "PassSelectedButtonsToParentSystem",
    "W3DCommandBarBackgroundDraw",
    "W3DCommandBarForegroundDraw",
    "W3DCommandBarGenExpDraw",
    "W3DCommandBarGridDraw",
    "W3DCommandBarTopDraw",
    "W3DGadgetPushButtonImageDraw",
    "W3DLeftHUDDraw",
    "W3DNoDraw",
    "W3DPowerDraw",
    "W3DRightHUDDraw",
)
_WND_REQUIRED_MESSAGE_CALLBACKS = (
    "ControlBarInput",
    "GameWinBlockInput",
    "PassSelectedButtonsToParentSystem",
    "LeftHUDInput",
    "ControlBarSystem",
)
_WND_UNRESOLVED_BUILTINS = (
    "GameWinDefaultInput",
    "GameWinDefaultSystem",
    "GameWinDefaultTooltip",
    "W3DGameWinDefaultDraw",
)
_WND_DRAW_SERVICE_GATES = (
    "background-blend-and-parameter-aliases",
    "foreground-image-indirection",
    "experience-progress-and-blend-aliases",
    "grid-cell-material-parameters",
    "top-tail-draw-parameters",
    "radar-object-and-blend-clipping-structures",
    "power-counters-and-image-blend-state-aliases",
)
_WND_MESSAGE_ALIAS_GATES = (
    "retail-service-class-aliases-for-three-globals",
    "object-field-0x14-type-aliases",
    "command-id-0x42f-or-0x430-selected-object-branch",
    "messages-0x11-0x12-0x18-selection-camera-aliases",
    "eight-cached-handle-semantic-aliases",
    "message-0x4031-matched-service-aliases",
    "message-0x400b-rejection-before-selected-button-fallback",
)

_EXTERNAL_ATTACHMENT_SPECS: tuple[dict[str, Any], ...] = (
    {
        "loadOrder": 0,
        "swf": "InGameSpellBook.swf",
        "movie": "InGameSpellBook",
        "target": "SpellBookUI",
        "targetPath": "Palantir.root.frame0/SpellBookUI",
        "loadInstructionOffset": 364906,
        "sourceOffset": 95944,
        "recordSha256": "2fdc0ace8677b1243f09fac472fd3a1df1f9750160769ce6dfa17154b4b176cf",
        "depth": 3,
        "matrix": [0.9998626708984375, 0.0, 0.0, 0.9999847412109375],
        "translation": [0.0, 0.0],
        "frameCount": 18,
        "labels": {"_hide": 0, "_show": 9},
        "initialStopFrame": 8,
        "programOffset": 22016,
        "programSha256": "bc9e19fd0ad7500926b31dba505f878012335ec804ec35707cfa733ecb5b943d",
        "defaultState": "hidden-dormant",
        "normalMenVsMen": "dormant-until-spell-book-host-show",
        "loadedCallback": "OnAptInGameSpellBookLoaded",
        "unloadedCallback": "OnAptInGameSpellBookUnloaded",
        "callbackArgument": "GetFullName(this)",
        "godotInterface": "RetailSpellBookSlot",
    },
    {
        "loadOrder": 2,
        "swf": "InGameHelpBox.swf",
        "movie": "InGameHelpBox",
        "target": "helpBox",
        "targetPath": "Palantir.root.frame0/helpBox",
        "loadInstructionOffset": 364938,
        "sourceOffset": 97480,
        "recordSha256": "db2e7a3042c175a1a9a7ab233f472d482bffd19daf8addf05ea83d36f0090028",
        "depth": 176,
        "matrix": [1.0, 0.0, 0.0, 1.0],
        "translation": [585.0, 607.0],
        "frameCount": 1,
        "labels": {},
        "initialStopFrame": 0,
        "programOffset": 3636,
        "programSha256": "71664be06717e6e52ee407d44bda48934a427601d7419d03f26ca75be7eda502",
        "defaultState": "hidden-dormant",
        "normalMenVsMen": "dormant-until-help-show",
        "loadedCallback": "AptPalantir::OnHelpBoxLoaded",
        "unloadedCallback": "AptPalantir::OnHelpBoxUnloaded",
        "callbackArgument": "clip.toString()",
        "godotInterface": "RetailHelpBoxSlot",
    },
    {
        "loadOrder": 3,
        "swf": "InGameHeroSelect.swf",
        "movie": "InGameHeroSelect",
        "target": "HeroSelectUI",
        "targetPath": "Palantir.root.frame0/HeroSelectUI",
        "loadInstructionOffset": 364954,
        "sourceOffset": 97416,
        "recordSha256": "e1a1230fb4f99386fa721c3c9fc59ac093c68e7c21c6318689acbb7c3c182a05",
        "depth": 174,
        "matrix": [1.0, 0.0, 0.0, 1.0],
        "translation": [375.0, 700.0],
        "frameCount": 29,
        "labels": {"_fadein": 9, "_hide": 0, "_show": 19},
        "initialStopFrame": 8,
        "programOffset": 167740,
        "programSha256": "2c43ab2db3b3f9158706bc28154ed4362c9e6fd237bcc218f9e1100621190b1f",
        "defaultState": "hidden-until-captured-show-result",
        "normalMenVsMen": "unresolved-show-hero-select-interface",
        "loadedCallback": "AptPalantir::OnHeroSelectLoaded",
        "unloadedCallback": "AptPalantir::OnHeroSelectUnloaded",
        "callbackArgument": "clip.toString()",
        "godotInterface": "RetailHeroSelectSlot",
    },
    {
        "loadOrder": 4,
        "swf": "InGamePlanningMode.swf",
        "movie": "InGamePlanningMode",
        "target": "planningModeUI",
        "targetPath": "Palantir.root.frame0/planningModeUI",
        "loadInstructionOffset": 364970,
        "sourceOffset": 97608,
        "recordSha256": "cf671b9ebd00322cfec4a4f996f946f8f8942320fc79f30d4a9f8061257c28c3",
        "depth": 180,
        "matrix": [1.0, 0.0, 0.0, 1.0],
        "translation": [512.0, 30.0],
        "frameCount": 27,
        "labels": {"_close": 19, "_init": 0, "_open": 9},
        "initialStopFrame": 8,
        "programOffset": 28244,
        "programSha256": "6a660a1b59561308ac86226bcbf83f347e87b82e894aeb383b60be2e79d74e97",
        "defaultState": "closed-dormant",
        "normalMenVsMen": "dormant-until-planning-open",
        "loadedCallback": "AptPalantir::OnPlanningModeUILoaded",
        "unloadedCallback": "AptPalantir::OnPlanningModeUIUnloaded",
        "callbackArgument": "clip.toString()",
        "godotInterface": "RetailPlanningModeSlot",
    },
)

_EXTERNAL_ATTACHMENT_GATES = (
    {
        "id": "apt-load-completion-order",
        "trace": "record the four source loaded callbacks during InitialSetup",
    },
    {
        "id": "hero-select-initial-visibility",
        "trace": "record HeroSelectUI frame and visibility around ShowHeroSelectInterface",
    },
    {
        "id": "palantir-target-removal-order",
        "trace": "record four unload callbacks and child removals during teardown",
    },
    {
        "id": "help-box-alt-anchor-runtime-value",
        "trace": "record HelpBox clip and alternate-anchor coordinates on load",
    },
)

# The opcode assignment below is the published SWF AVM1 one; SAGE extends it in
# the 0xAE-0xB9 range but never reassigns the base block, which the table itself
# demonstrates (0x30 random, 0x17 pop, 0x62 bitwise-xor all sit where the SWF
# spec puts them).  The five entries added for the retail SCREEN lane (queue
# Q117) - 0x13, 0x18, 0x37, 0x60, 0x64 - are all ZERO-OPERAND stack ops, so
# admitting them cannot shift a stream's decode alignment: a mis-assignment
# would derail into the existing bounded-end refusal rather than decode
# silently.  Naming an opcode makes it DECODABLE, not executable; anything
# outside `_ACTION_TIMELINE_OPS` still surfaces as an
# `action-script-unsupported-opcodes` blocker.
_ACTION_NAMES = {
    0x00: "end",
    0x04: "next-frame",
    0x06: "play",
    0x07: "stop",
    0x0A: "add",
    0x0B: "subtract",
    0x0C: "multiply",
    0x0D: "divide",
    0x0E: "equals",
    0x0F: "less-than",
    0x10: "and",
    0x11: "or",
    0x12: "not",
    0x13: "string-equals",
    0x17: "pop",
    0x18: "to-integer",
    0x1C: "get-variable",
    0x1D: "set-variable",
    0x20: "set-target2",
    0x21: "string-concat",
    0x22: "get-property",
    0x23: "set-property",
    0x26: "trace",
    0x30: "random",
    0x37: "mb-ascii-to-char",
    0x3A: "delete",
    0x3B: "delete2",
    0x3C: "define-local",
    0x3D: "call-function",
    0x3E: "return",
    0x3F: "modulo",
    0x40: "new-object",
    0x41: "var",
    0x42: "init-array",
    0x43: "init-object",
    0x44: "typeof",
    0x47: "add2",
    0x48: "less-than2",
    0x49: "equals2",
    0x4A: "to-number",
    0x4B: "to-string",
    0x4C: "push-duplicate",
    0x4E: "get-member",
    0x4F: "set-member",
    0x50: "increment",
    0x51: "decrement",
    0x52: "call-method",
    0x54: "instanceof",
    0x55: "enumerate2",
    0x56: "push-this",
    0x59: "push-zero",
    0x5A: "push-one",
    0x5B: "call-function-pop",
    0x5D: "call-method-pop",
    0x5E: "call-method-ea",
    0x60: "bitwise-and",
    0x62: "bitwise-xor",
    0x64: "bitwise-right-shift",
    0x66: "strict-equal",
    0x67: "greater",
    0x69: "extends",
    0x70: "push-this-variable",
    0x71: "push-global-variable",
    0x72: "zero-variable",
    0x73: "push-true",
    0x74: "push-false",
    0x75: "push-null",
    0x76: "push-undefined",
    0x81: "goto-frame",
    0x83: "get-url",
    0x87: "set-register",
    0x88: "constant-pool",
    0x8C: "goto-label",
    0x8E: "define-function2",
    0x96: "push-data",
    0x99: "branch-always",
    0x9A: "get-url2",
    0x9B: "define-function",
    0x9D: "branch-if-true",
    0x9F: "goto-frame2",
    0xA1: "push-string",
    0xA2: "push-constant-byte",
    0xA3: "push-constant-word",
    0xA4: "get-string-variable",
    0xA5: "get-string-member",
    0xA6: "set-string-variable",
    0xA7: "set-string-member",
    0xAE: "push-value-of-variable",
    0xAF: "get-named-member",
    0xB0: "call-named-function-pop",
    0xB1: "call-named-function",
    0xB2: "call-named-method-pop",
    0xB3: "call-named-method",
    0xB4: "push-float",
    0xB5: "push-byte",
    0xB6: "push-short",
    0xB7: "push-long",
    0xB8: "branch-if-false",
    0xB9: "push-register",
}
_ACTION_ALIGNED = {
    0x81,
    0x83,
    0x87,
    0x88,
    0x8C,
    0x8E,
    0x96,
    0x99,
    0x9B,
    0x9D,
    0x9F,
    0xA1,
    0xA4,
    0xA5,
    0xA6,
    0xA7,
    0xB8,
}
_ACTION_BYTE_OPERAND = {0xA2, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB5, 0xB9}
_ACTION_WORD_OPERAND = {0xA3, 0xB6}
_ACTION_I32_OPERAND = {0x81, 0x87, 0x99, 0x9D, 0x9F, 0xB7, 0xB8}
_ACTION_STRING_OPERAND = {0x8C, 0xA1, 0xA4, 0xA5, 0xA6, 0xA7}
_ACTION_HOST_OPS = {
    "call-function-pop",
    "call-method",
    "call-method-pop",
    "call-named-function-pop",
    "call-named-function",
    "call-named-method-pop",
    "call-named-method",
    "get-url",
    "get-url2",
}
_ACTION_EXTERNAL_OPS = {
    "get-variable",
    "set-variable",
    "get-property",
    "set-property",
    "get-member",
    "set-member",
    "push-this",
    "push-this-variable",
    "push-global-variable",
    "zero-variable",
    "get-string-variable",
    "get-string-member",
    "set-string-variable",
    "set-string-member",
    "push-value-of-variable",
    "get-named-member",
}
_ACTION_CONTROL_OPS = {
    "branch-always",
    "branch-if-true",
    "branch-if-false",
    "define-function",
    "define-function2",
}
_ACTION_TIMELINE_OPS = {
    "end",
    "play",
    "stop",
    "goto-frame",
    "goto-frame2",
    "goto-label",
    "push-string",
    "push-byte",
    "push-short",
    "push-long",
    "push-zero",
    "push-one",
    "pop",
}
_CLIP_EVENT_NAMES = {
    0x800000: "key-up",
    0x400000: "key-down",
    0x200000: "mouse-up",
    0x100000: "mouse-down",
    0x080000: "mouse-move",
    0x040000: "unload",
    0x020000: "enter-frame",
    0x010000: "load",
    0x008000: "drag-over",
    0x004000: "roll-out",
    0x002000: "roll-over",
    0x001000: "release-outside",
    0x000800: "release",
    0x000400: "press",
    0x000200: "drag-out",
    0x000100: "data",
    0x000004: "construct",
    0x000002: "key-press",
    0x000001: "initialize",
}
_CLIP_EVENT_MASK = sum(_CLIP_EVENT_NAMES)
_CLIP_INPUT_EVENTS = {
    "key-up",
    "key-down",
    "mouse-up",
    "mouse-down",
    "mouse-move",
    "drag-over",
    "roll-out",
    "roll-over",
    "release-outside",
    "release",
    "press",
    "drag-out",
    "key-press",
}
_BUTTON_RECORD_STATES = {
    0x01: "up",
    0x02: "over",
    0x04: "down",
    0x08: "hit",
}
_BUTTON_ACTION_TRANSITIONS = {
    0x01: "idle-to-over-up",
    0x02: "over-up-to-idle",
    0x04: "over-up-to-over-down",
    0x08: "over-down-to-over-up",
    0x10: "over-down-to-out-down",
    0x20: "out-down-to-over-down",
    0x40: "idle-to-over-down",
    0x80: "over-down-to-idle",
}

# These are the only non-timeline clip-initialize programs executable by this
# converter.  All five identities are tied to the BFME2 1.06 retail instruction
# bytes and to one exact typed effect.  This is deliberately not a general
# ActionScript evaluator or a pattern-based permission grant.
_TYPED_INITIALIZE_PROGRAMS: dict[tuple[str, int], dict[str, Any]] = {
    ("InGameSideCommandBar", 13680): {
        "instructionOffset": 13944,
        "byteLength": 59,
        "sha256": "782d8458e3a04ea8fc4a0563665053035b92d6bfd14e978f6e4b6d1f72873fbc",
        "maximumStackDepth": 4,
        "effect": {
            "kind": "define-local-method",
            "receiver": "this",
            "methodName": "SetFlashEffectState",
            "parameters": ["state"],
            "body": {
                "kind": "call-indexed-ancestor-timeline-method",
                "receiverAncestorHops": 2,
                "collection": "flashEffects",
                "index": {"ancestorHops": 1, "property": "_name"},
                "methodName": "gotoAndPlay",
                "arguments": [{"kind": "parameter", "name": "state"}],
            },
            "sourceEvidence": {
                "programId": "ingamesidecommandbar:clip-event:13680",
                "instructionOffset": 13944,
                "instructionEndOffset": 14003,
                "byteLength": 59,
                "sha256": "782d8458e3a04ea8fc4a0563665053035b92d6bfd14e978f6e4b6d1f72873fbc",
            },
        },
    },
    ("libInGameUI", 56252): {
        "instructionOffset": 57324,
        "byteLength": 13,
        "sha256": "0e6307bff26d6ffca7483353a04501e18d7acc5ee2dc60a50e5a75160ae81bb6",
        "maximumStackDepth": 3,
        "effect": {
            "kind": "set-clip-property",
            "target": "",
            "propertyIndex": 7,
            "propertyName": "_visible",
            "value": False,
            "sourceEvidence": {
                "programId": "libingameui:clip-event:56252",
                "instructionOffset": 57324,
                "instructionEndOffset": 57337,
                "byteLength": 13,
                "sha256": "0e6307bff26d6ffca7483353a04501e18d7acc5ee2dc60a50e5a75160ae81bb6",
            },
        },
    },
    ("Palantir", 375628): {
        "instructionOffset": 377292,
        "byteLength": 17,
        "sha256": "b603bb6578f35f5f590765c18791b9dc1ab749058bdac674f0a85381c8b900f1",
        "maximumStackDepth": 2,
        "effect": {
            "kind": "bind-live-text",
            "targetMember": "stringName",
            "aptVariable": "$PalantirResources",
            "runtimeInputs": ["resources"],
            "formatter": "percent-d-space-when-negative",
            "sourceEvidence": {
                "programId": "palantir:clip-event:375628",
                "instructionOffset": 377292,
                "instructionEndOffset": 377309,
                "byteLength": 17,
                "sha256": "b603bb6578f35f5f590765c18791b9dc1ab749058bdac674f0a85381c8b900f1",
            },
        },
    },
    ("Palantir", 375640): {
        "instructionOffset": 377312,
        "byteLength": 17,
        "sha256": "3ea5b0333ab5527877ac56d1107ec97caed63bf3a56d1bb89bc2192d6290926f",
        "maximumStackDepth": 2,
        "effect": {
            "kind": "bind-live-text",
            "targetMember": "stringName",
            "aptVariable": "$PalantirResourceMultiplier",
            "runtimeInputs": ["resourceMultiplier"],
            "formatter": "x-percent-g-space-when-exactly-one",
            "sourceEvidence": {
                "programId": "palantir:clip-event:375640",
                "instructionOffset": 377312,
                "instructionEndOffset": 377329,
                "byteLength": 17,
                "sha256": "3ea5b0333ab5527877ac56d1107ec97caed63bf3a56d1bb89bc2192d6290926f",
            },
        },
    },
    ("Palantir", 375652): {
        "instructionOffset": 377332,
        "byteLength": 17,
        "sha256": "ff3de0b7f0a657cb41fb9c3de4e4fc42e68c9753d32a4b42515b5247ff8f357c",
        "maximumStackDepth": 2,
        "effect": {
            "kind": "bind-live-text",
            "targetMember": "stringName",
            "aptVariable": "$PalantirCommandPoints",
            "runtimeInputs": ["commandPointsCurrent", "commandPointsCap"],
            "formatter": "percent-d-slash-percent-d-current-or-space",
            "sourceEvidence": {
                "programId": "palantir:clip-event:375652",
                "instructionOffset": 377332,
                "instructionEndOffset": 377349,
                "byteLength": 17,
                "sha256": "ff3de0b7f0a657cb41fb9c3de4e4fc42e68c9753d32a4b42515b5247ff8f357c",
            },
        },
    },
}

# These three BFME2 1.06 Palantir programs are the complete bounded MinLOD
# allowlist. Each entry pins the exact retail bytes and emits one typed branch
# contract. No other ActionScript external-state/control-flow program inherits
# permission from these shapes.
_TYPED_MINLOD_PROGRAMS: dict[tuple[str, int], dict[str, Any]] = {
    ("Palantir", 152912): {
        "instructionOffset": 366952,
        "byteLength": 46,
        "sha256": "069d12e949c2bcd03d523f73f6d26d5606ffd9486e920eae18b1e26b22b037d4",
        "maximumStackDepth": 2,
        "instructionNames": [
            "push-global-variable",
            "get-string-member",
            "push-true",
            "equals2",
            "push-duplicate",
            "not",
            "branch-if-true",
            "pop",
            "push-string",
            "push-byte",
            "get-property",
            "push-string",
            "equals2",
            "not",
            "branch-if-true",
            "stop",
            "end",
        ],
        "effect": {
            "kind": "conditional-min-lod",
            "condition": {
                "kind": "required-boolean-input",
                "name": "MinLOD",
                "equals": True,
            },
            "whenTrue": [
                {
                    "kind": "stop-timeline-if-property-equals",
                    "target": "this",
                    "propertyIndex": 13,
                    "propertyName": "_name",
                    "equals": "GlobeSwirlRender",
                }
            ],
            "whenFalse": [],
            "sourceEvidence": {
                "programId": "palantir:152912",
                "instructionOffset": 366952,
                "instructionEndOffset": 366998,
                "byteLength": 46,
                "sha256": "069d12e949c2bcd03d523f73f6d26d5606ffd9486e920eae18b1e26b22b037d4",
            },
        },
    },
    ("Palantir", 333872): {
        "instructionOffset": 370784,
        "byteLength": 37,
        "sha256": "93db87938ba572d0652d77922f052fd66c6cf85e09394c708ddcd1beed97b5ba",
        "maximumStackDepth": 3,
        "instructionNames": [
            "constant-pool",
            "push-global-variable",
            "get-named-member",
            "not",
            "branch-if-true",
            "push-value-of-variable",
            "push-constant-byte",
            "push-zero",
            "set-member",
            "push-value-of-variable",
            "push-constant-byte",
            "push-zero",
            "set-member",
            "end",
        ],
        "constantValues": ["_global", "MinLOD", "effect1", "_visible", "effect4"],
        "targets": ["effect1", "effect4"],
        "effect": {
            "kind": "conditional-min-lod",
            "condition": {
                "kind": "required-boolean-input",
                "name": "MinLOD",
                "equals": True,
            },
            "whenTrue": [
                {
                    "kind": "set-named-clip-property",
                    "target": "effect1",
                    "propertyName": "_visible",
                    "value": False,
                },
                {
                    "kind": "set-named-clip-property",
                    "target": "effect4",
                    "propertyName": "_visible",
                    "value": False,
                },
            ],
            "whenFalse": [],
            "sourceEvidence": {
                "programId": "palantir:333872",
                "instructionOffset": 370784,
                "instructionEndOffset": 370821,
                "byteLength": 37,
                "sha256": "93db87938ba572d0652d77922f052fd66c6cf85e09394c708ddcd1beed97b5ba",
            },
        },
    },
    ("Palantir", 334840): {
        "instructionOffset": 370840,
        "byteLength": 37,
        "sha256": "0206dc32f71abc3c28ec488db2aaad3d0b6ba17da58f15a69ce6bac0b86951db",
        "maximumStackDepth": 3,
        "instructionNames": [
            "constant-pool",
            "push-global-variable",
            "get-named-member",
            "not",
            "branch-if-true",
            "push-value-of-variable",
            "push-constant-byte",
            "push-zero",
            "set-member",
            "push-value-of-variable",
            "push-constant-byte",
            "push-zero",
            "set-member",
            "end",
        ],
        "constantValues": ["_global", "MinLOD", "effect2", "_visible", "effect3"],
        "targets": ["effect2", "effect3"],
        "effect": {
            "kind": "conditional-min-lod",
            "condition": {
                "kind": "required-boolean-input",
                "name": "MinLOD",
                "equals": True,
            },
            "whenTrue": [
                {
                    "kind": "set-named-clip-property",
                    "target": "effect2",
                    "propertyName": "_visible",
                    "value": False,
                },
                {
                    "kind": "set-named-clip-property",
                    "target": "effect3",
                    "propertyName": "_visible",
                    "value": False,
                },
            ],
            "whenFalse": [],
            "sourceEvidence": {
                "programId": "palantir:334840",
                "instructionOffset": 370840,
                "instructionEndOffset": 370877,
                "byteLength": 37,
                "sha256": "0206dc32f71abc3c28ec488db2aaad3d0b6ba17da58f15a69ce6bac0b86951db",
            },
        },
    },
}

# The resource-flash oracle proves this one 26-byte frame-entry program.  It is
# kept separate from the general timeline subset because the second authored
# effect crosses the APT/native boundary.  The converter emits only the exact
# event intent; native counter dispatch and downstream mixer policy remain
# explicit capture blockers.
_TYPED_RESOURCE_FLASH_PROGRAM: dict[str, Any] = {
    "scriptId": "palantir:332504",
    "movie": "Palantir",
    "sourceOffset": 332_504,
    "instructionOffset": 370_752,
    "byteLength": 26,
    "sha256": "0b966556e6fc10d1eaa5c129999f31e185b634425298b7bdaf21b6dd26aeb999",
    "maximumStackDepth": 4,
    "effects": [
        {"kind": "play-current-timeline"},
        {
            "kind": "emit-retail-audio-event-intent",
            "receiver": "_root",
            "method": "PlaySound",
            "arguments": ["Gui_PalantirResourceBarFlash"],
            "discardReturn": True,
            "precondition": "Palantir root Initialized is truthy",
            "dispatch": "FSCommand:PlaySound",
            "sourceEvidence": {
                "programId": "palantir:332504",
                "instructionOffset": 370_752,
                "instructionEndOffset": 370_778,
                "byteLength": 26,
                "sha256": (
                    "0b966556e6fc10d1eaa5c129999f31e185b634425298b7bdaf21b6dd26aeb999"
                ),
            },
        },
    ],
}

# The private retail oracle seals these three side-command scripts and the six
# helper bodies they invoke.  This is a topology adapter, not an ActionScript
# VM: any byte, operand, function body, or authored button layout change falls
# back to the existing unsupported-opcode blocker.
_TYPED_SIDE_COMMAND_PROGRAMS: dict[tuple[str, int], dict[str, Any]] = {
    ("InGameSideCommandBar", 6272): {
        "scriptId": "ingamesidecommandbar:6272",
        "instructionOffset": 11952,
        "byteLength": 10,
        "sha256": "ee3f7f3c582961473ffbbebe851f0086820fd9fa57c62f3573a021d2c5917557",
        "maximumStackDepth": 2,
        "instructionNames": [
            "push-zero",
            "push-string",
            "call-function-pop",
            "end",
        ],
        "effects": [
            {
                "kind": "side-command-update-neighbor-frame-states",
                "order": ["next", "prior"],
            }
        ],
    },
    ("InGameSideCommandBar", 6368): {
        "scriptId": "ingamesidecommandbar:6368",
        "instructionOffset": 11992,
        "byteLength": 18,
        "sha256": "268aab1f60a086e5bf869d83da0aabdfe2f383d688bf98ce6a6a59fab274f040",
        "maximumStackDepth": 2,
        "instructionNames": [
            "push-zero",
            "push-string",
            "call-function-pop",
            "push-zero",
            "push-string",
            "call-function-pop",
            "end",
        ],
        "effects": [
            {"kind": "side-command-update-frame-state"},
            {
                "kind": "side-command-update-neighbor-frame-states",
                "order": ["next", "prior"],
            },
        ],
    },
    ("InGameSideCommandBar", 7296): {
        "scriptId": "ingamesidecommandbar:7296",
        "instructionOffset": 12148,
        "byteLength": 73,
        "sha256": "56466dca85c04dd52fd50a5cb02ea625cdad4b776c7ef14a6854f6412caca675",
        "maximumStackDepth": 6,
        "instructionNames": [
            "constant-pool",
            "push-global-variable",
            "get-named-member",
            "not",
            "not",
            "branch-if-true",
            "push-constant-byte",
            "var",
            "push-constant-byte",
            "zero-variable",
            "push-value-of-variable",
            "push-byte",
            "less-than2",
            "not",
            "branch-if-true",
            "push-constant-byte",
            "push-one",
            "push-this-variable",
            "push-constant-byte",
            "push-value-of-variable",
            "push-one",
            "add2",
            "to-string",
            "add2",
            "get-member",
            "call-named-method-pop",
            "push-constant-byte",
            "push-value-of-variable",
            "increment",
            "set-variable",
            "branch-always",
            "end",
        ],
        "constantValues": [
            "_global",
            "InGame",
            "i",
            "_show",
            "this",
            "Button",
            "gotoAndPlay",
        ],
        "effects": [
            {
                "kind": "side-command-show-buttons-if-in-game",
                "condition": {
                    "kind": "required-boolean-input",
                    "name": "InGame",
                    "equals": True,
                },
                "targets": [f"Button{index}" for index in range(1, 16)],
                "missingTargetEffect": "ordered-no-op",
            }
        ],
    },
}

# Two Palantir CommandButtons programs are sealed by the private oracle.  They
# only install declarations/local methods; neither program invokes a callback
# body while it is running.  Keep the callback bodies as typed metadata rather
# than widening the bounded evaluator into an ActionScript VM.
_PALANTIR_COMMAND_LIFECYCLE_FUNCTIONS: tuple[dict[str, Any], ...] = (
    {
        "name": "OnMovieClipFrameLoaded",
        "definitionOffset": 367636,
        "bodyOffset": 367668,
        "bodyByteLength": 51,
        "bodySha256": "2e04d77ff99a163925615cc9e6b2c7d83dbf945b428d3c9baea695a95c1e12fd",
        "host": "PalantirCommandUI::OnButtonFrameLoaded",
        "argument": "index=clip._name&name=String(clip)",
    },
    {
        "name": "OnMovieClipFrameUnloaded",
        "definitionOffset": 367719,
        "bodyOffset": 367748,
        "bodyByteLength": 36,
        "bodySha256": "4ab0920334b617d403a746a23b7634ca1c5511974f70fd6020e3e32ac7934214",
        "host": "PalantirCommandUI::OnButtonFrameUnloaded",
        "argument": "index=clip._name",
    },
    {
        "name": "OnCommandButtonSubMenuLoaded",
        "definitionOffset": 367784,
        "bodyOffset": 367816,
        "bodyByteLength": 59,
        "bodySha256": "bd603f86f55a96977d0c8d6f001952441af5fca3c4f96d1c59e84ab46eb713bc",
        "host": "PalantirCommandUI::OnSubMenuLoaded",
        "argument": "index=clip._name.substr(7)&name=String(clip)",
    },
    {
        "name": "OnCommandButtonSubMenuUnloaded",
        "definitionOffset": 367875,
        "bodyOffset": 367904,
        "bodyByteLength": 43,
        "bodySha256": "c7032d22743076388774d66857f2d788d3facea1433ff415ff287b999a0087f2",
        "host": "PalantirCommandUI::OnSubMenuUnloaded",
        "argument": "index=clip._name.substr(7)",
    },
    {
        "name": "OnCommandButtonToggleFlashLoaded",
        "definitionOffset": 367947,
        "bodyOffset": 367976,
        "bodyByteLength": 59,
        "bodySha256": "86c91c219bada277fcccbe6a103b33ce9c17b870d05c0b806728e0051664a02e",
        "host": "PalantirCommandUI::OnToggleFlashLoaded",
        "argument": "index=clip._name.substr(11)&name=String(clip)",
    },
    {
        "name": "OnCommandButtonToggleFlashUnloaded",
        "definitionOffset": 368035,
        "bodyOffset": 368064,
        "bodyByteLength": 43,
        "bodySha256": "251de13f80d51b7fcb72b137bd7817901c86b66c37c3dd9d51d24cd525ac3241",
        "host": "PalantirCommandUI::OnToggleFlashUnloaded",
        "argument": "index=clip._name.substr(11)",
    },
)

_PALANTIR_COMMAND_BUTTON_METHODS: tuple[dict[str, Any], ...] = (
    {
        "name": "SetAutoAbilityOverlayState",
        "definitionOffset": 368160,
        "bodyOffset": 368192,
        "bodyByteLength": 45,
        "bodySha256": "abf82bf818bb3423a889db7f20fb3b9483d5e9e7fda65710988bc50f7343a482",
        "parameter": "state",
        "target": "this._parent._parent.AutoAbilityOverlays[this._name]",
        "dispatch": "target.gotoAndPlay(state)",
    },
    {
        "name": "SetFlashEffectState",
        "definitionOffset": 368242,
        "bodyOffset": 368272,
        "bodyByteLength": 45,
        "bodySha256": "348936c664694b5d48c022b09f90552b509cc052dd8a00390a411d980ef46196",
        "parameter": "state",
        "target": "this._parent.FlashEffects[this._name]",
        "dispatch": "target.gotoAndPlay(state)",
    },
    {
        "name": "SetGlassState",
        "definitionOffset": 368322,
        "bodyOffset": 368352,
        "bodyByteLength": 46,
        "bodySha256": "c3546a83b7f1c52d876e993edea2d3f6e9c8054621f8ea3b4e94d821ba84ddb7",
        "parameter": "state",
        "target": "this._parent['glass' + this._name]",
        "dispatch": "target.gotoAndPlay(state)",
    },
)

_TYPED_PALANTIR_COMMAND_PROGRAMS: dict[tuple[str, int], dict[str, Any]] = {
    ("Palantir", 169224): {
        "scriptId": "palantir:169224",
        "instructionOffset": 367624,
        "byteLength": 484,
        "sha256": "3e6f347f6c6574a2d40e85f8f564c1f9af1c13513d0f1671298a1484d629fbfc",
        "maximumStackDepth": 6,
        "instructionNames": ["constant-pool", *("define-function2",) * 6, "end"],
        "constantValues": [
            "OnMovieClipFrameLoaded( ",
            " )",
            "FSCommand:PalantirCommandUI::OnButtonFrameLoaded",
            "index=",
            "_name",
            "&name=",
            "OnMovieClipFrameUnloaded( ",
            "FSCommand:PalantirCommandUI::OnButtonFrameUnloaded",
            "OnCommandButtonSubMenuLoaded( ",
            "FSCommand:PalantirCommandUI::OnSubMenuLoaded",
            "substr",
            "OnCommandButtonSubMenuUnloaded( ",
            "FSCommand:PalantirCommandUI::OnSubMenuUnloaded",
            "OnCommandButtonToggleFlashLoaded( ",
            "FSCommand:PalantirCommandUI::OnToggleFlashLoaded",
            "OnCommandButtonToggleFlashUnloaded( ",
            "FSCommand:PalantirCommandUI::OnToggleFlashUnloaded",
        ],
        "effects": [
            {
                "kind": "palantir-command-register-lifecycle-functions",
                "invocationDuringRegistration": False,
                "functions": [dict(row) for row in _PALANTIR_COMMAND_LIFECYCLE_FUNCTIONS],
            }
        ],
    },
    ("Palantir", 169256): {
        "scriptId": "palantir:169256",
        "instructionOffset": 368120,
        "byteLength": 293,
        "sha256": "c29cecb1997de0b9de26b4c5ec01761c81d45bc21cadf45dfd1e268ac2cefe3b",
        "maximumStackDepth": 6,
        "instructionNames": [
            "constant-pool", "push-constant-byte", "push-zero", "define-local",
            "push-value-of-variable", "push-byte", "less-than2", "not",
            "branch-if-true", "push-constant-byte", "push-this-variable",
            "push-value-of-variable", "to-string", "get-member", "define-local",
            "push-value-of-variable", "push-constant-byte", "define-function2",
            "set-member", "push-value-of-variable", "push-constant-byte",
            "define-function2", "set-member", "push-value-of-variable",
            "push-constant-byte", "define-function2", "set-member",
            "push-constant-byte", "push-value-of-variable", "increment",
            "set-variable", "branch-always", "end",
        ],
        "constantValues": [
            "i", "buttonFrame", "this", "SetAutoAbilityOverlayState", "_parent",
            "AutoAbilityOverlays", "_name", "gotoAndPlay", "SetFlashEffectState",
            "FlashEffects", "SetGlassState", "glass",
        ],
        "effects": [
            {
                "kind": "palantir-command-register-button-methods",
                "buttonOrder": [str(index) for index in range(6)],
                "invocationDuringRegistration": False,
                "methods": [dict(row) for row in _PALANTIR_COMMAND_BUTTON_METHODS],
            }
        ],
    },
}

_SIDE_COMMAND_HELPERS: tuple[dict[str, Any], ...] = (
    {
        "name": "GetNextButton",
        "definitionOffset": 10968,
        "bodyOffset": 10996,
        "bodyByteLength": 85,
        "bodySha256": "a3df6709576a4a211c773964f23a50e74cb2148c23fa5f3e8f1224f5b8b57a13",
    },
    {
        "name": "IsNextButtonFrameVisible",
        "definitionOffset": 11081,
        "bodyOffset": 11108,
        "bodyByteLength": 101,
        "bodySha256": "daa6c68215c032f6ed0819f914138faa968815a25111b996d42e5725b4cf49b5",
    },
    {
        "name": "GetPriorButton",
        "definitionOffset": 11209,
        "bodyOffset": 11236,
        "bodyByteLength": 85,
        "bodySha256": "20a3b9b362d10448c370a7d0f4d466eb64fb422df1759c42b4c81f7af2bce314",
    },
    {
        "name": "IsPriorButtonFrameVisible",
        "definitionOffset": 11321,
        "bodyOffset": 11348,
        "bodyByteLength": 101,
        "bodySha256": "2d80ff5e0dcf689e0af206ce6ffcf3a029f41a419924a6d68710a9e45b717378",
    },
    {
        "name": "UpdateFrameState",
        "definitionOffset": 11449,
        "bodyOffset": 11476,
        "bodyByteLength": 185,
        "bodySha256": "4e3ebf348802940609232fefcd3c8a9693d482524af5bdfc23d00ef55806af53",
    },
    {
        "name": "UpdateNeighborFrameStates",
        "definitionOffset": 11661,
        "bodyOffset": 11688,
        "bodyByteLength": 137,
        "bodySha256": "2ffad24c431995b73b00765b2ad1f8dce45deeed25387e048ea53ffd4baf4d24",
    },
)


class HudAptConvertError(ValueError):
    """Raised when retail input cannot satisfy the bounded runtime contract."""


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical_bytes(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


class _Reader:
    def __init__(self, data: bytes, label: str) -> None:
        self.data = data
        self.label = label

    def require(self, offset: int, size: int, context: str) -> None:
        if offset < 0 or size < 0 or offset > len(self.data) - size:
            raise HudAptConvertError(f"{self.label} {context} range is out of bounds")

    def u32(self, offset: int, context: str) -> int:
        self.require(offset, 4, context)
        return struct.unpack_from("<I", self.data, offset)[0]

    def i32(self, offset: int, context: str) -> int:
        self.require(offset, 4, context)
        return struct.unpack_from("<i", self.data, offset)[0]

    def u16(self, offset: int, context: str) -> int:
        self.require(offset, 2, context)
        return struct.unpack_from("<H", self.data, offset)[0]

    def f32(self, offset: int, context: str) -> float:
        self.require(offset, 4, context)
        value = struct.unpack_from("<f", self.data, offset)[0]
        if not math.isfinite(value):
            raise HudAptConvertError(f"{self.label} {context} is non-finite")
        return value

    def string(self, offset: int, context: str) -> str:
        if not 0 <= offset < len(self.data):
            raise HudAptConvertError(f"{self.label} {context} string is out of bounds")
        end = self.data.find(b"\0", offset, min(len(self.data), offset + 4097))
        if end < 0:
            raise HudAptConvertError(f"{self.label} {context} string is unterminated")
        try:
            return self.data[offset:end].decode("cp1252")
        except UnicodeDecodeError as exc:
            raise HudAptConvertError(
                f"{self.label} {context} string is invalid"
            ) from exc


def _action_constant(entries: list[dict[str, Any]], index: int) -> dict[str, Any]:
    if not 0 <= index < len(entries):
        raise HudAptConvertError(
            f"ActionScript constant index {index} is out of bounds"
        )
    row = entries[index]
    if not isinstance(row, Mapping) or "type" not in row:
        raise HudAptConvertError("ActionScript constant table changed")
    return {"constantIndex": index, "type": int(row["type"]), "value": row.get("value")}


def _decode_action_sequence(
    movie: _Movie,
    start: int,
    end: int | None = None,
) -> tuple[list[dict[str, Any]], int]:
    """Decode one APT instruction stream with exact nested function bounds."""

    reader = movie.reader
    if (
        start < 0
        or start >= len(reader.data)
        or (end is not None and not start <= end <= len(reader.data))
    ):
        raise HudAptConvertError("ActionScript instruction range is out of bounds")
    instructions: list[dict[str, Any]] = []
    position = start
    while end is None or position < end:
        if len(instructions) >= MAX_ACTION_INSTRUCTIONS:
            raise HudAptConvertError("ActionScript instruction count exceeds bounds")
        reader.require(position, 1, "ActionScript opcode")
        source_offset = position
        opcode = reader.data[position]
        position += 1
        name = _ACTION_NAMES.get(opcode)
        if name is None:
            raise HudAptConvertError(
                f"ActionScript opcode 0x{opcode:02x} at {source_offset} is unknown"
            )
        if opcode in _ACTION_ALIGNED:
            aligned = (position + 3) & ~3
            reader.require(position, aligned - position, "ActionScript alignment")
            if any(reader.data[position:aligned]):
                raise HudAptConvertError(
                    f"ActionScript alignment bytes at {source_offset} changed"
                )
            position = aligned
        row: dict[str, Any] = {
            "offset": source_offset,
            "opcode": opcode,
            "name": name,
        }
        if opcode in _ACTION_BYTE_OPERAND:
            reader.require(position, 1, f"ActionScript {name} operand")
            row["operand"] = reader.data[position]
            position += 1
        elif opcode in _ACTION_WORD_OPERAND:
            row["operand"] = reader.u16(position, f"ActionScript {name} operand")
            position += 2
        elif opcode in _ACTION_I32_OPERAND:
            row["operand"] = reader.i32(position, f"ActionScript {name} operand")
            position += 4
        elif opcode == 0xB4:
            row["operand"] = reader.f32(position, "ActionScript push-float operand")
            position += 4
        elif opcode in _ACTION_STRING_OPERAND:
            pointer = reader.u32(position, f"ActionScript {name} pointer")
            row["operand"] = reader.string(pointer, f"ActionScript {name} string")
            row["operandPointer"] = pointer
            position += 4
        elif opcode == 0x83:
            url_pointer = reader.u32(position, "ActionScript URL pointer")
            target_pointer = reader.u32(position + 4, "ActionScript URL target pointer")
            row["url"] = reader.string(url_pointer, "ActionScript URL")
            row["target"] = reader.string(target_pointer, "ActionScript URL target")
            position += 8
        elif opcode in (0x88, 0x96):
            count = reader.u32(position, f"ActionScript {name} count")
            pointer = reader.u32(position + 4, f"ActionScript {name} table")
            if count > 4096:
                raise HudAptConvertError(f"ActionScript {name} count exceeds bounds")
            reader.require(pointer, count * 4, f"ActionScript {name} values")
            indexes = [
                reader.u32(pointer + index * 4, f"ActionScript {name} value")
                for index in range(count)
            ]
            row["constants"] = [
                _action_constant(movie.constants["entries"], index) for index in indexes
            ]
            row["tablePointer"] = pointer
            position += 8
        elif opcode in (0x8E, 0x9B):
            name_pointer = reader.u32(position, "ActionScript function name")
            parameter_count = reader.u32(position + 4, "ActionScript parameter count")
            if parameter_count > 256:
                raise HudAptConvertError("ActionScript parameter count exceeds bounds")
            if opcode == 0x8E:
                reader.require(position + 8, 20, "ActionScript DefineFunction2 header")
                register_count = reader.data[position + 8]
                flags = int.from_bytes(
                    reader.data[position + 9 : position + 12], "little"
                )
                parameter_table = reader.u32(
                    position + 12, "ActionScript parameter table"
                )
                body_size = reader.i32(position + 16, "ActionScript function body size")
                trailer = reader.data[position + 20 : position + 28]
                if trailer != bytes.fromhex("3254769878563412"):
                    raise HudAptConvertError("ActionScript function trailer changed")
                reader.require(
                    parameter_table, parameter_count * 8, "ActionScript parameters"
                )
                parameters = [
                    {
                        "register": reader.i32(
                            parameter_table + index * 8,
                            "ActionScript parameter register",
                        ),
                        "name": reader.string(
                            reader.u32(
                                parameter_table + index * 8 + 4,
                                "ActionScript parameter name",
                            ),
                            "ActionScript parameter name",
                        ),
                    }
                    for index in range(parameter_count)
                ]
                position += 28
                row.update({"registerCount": register_count, "flags": flags})
            else:
                reader.require(position + 8, 16, "ActionScript DefineFunction header")
                parameter_table = reader.u32(
                    position + 8, "ActionScript parameter table"
                )
                body_size = reader.i32(position + 12, "ActionScript function body size")
                trailer = reader.data[position + 16 : position + 24]
                if trailer != bytes.fromhex("3254769878563412"):
                    raise HudAptConvertError("ActionScript function trailer changed")
                reader.require(
                    parameter_table, parameter_count * 4, "ActionScript parameters"
                )
                parameters = [
                    {
                        "register": -1,
                        "name": reader.string(
                            reader.u32(
                                parameter_table + index * 4,
                                "ActionScript parameter name",
                            ),
                            "ActionScript parameter name",
                        ),
                    }
                    for index in range(parameter_count)
                ]
                position += 24
            if body_size < 0 or position > len(reader.data) - body_size:
                raise HudAptConvertError("ActionScript function body is out of bounds")
            body, body_end = _decode_action_sequence(
                movie, position, position + body_size
            )
            if body_end != position + body_size:
                raise HudAptConvertError("ActionScript function body size changed")
            row.update(
                {
                    "functionName": reader.string(
                        name_pointer, "ActionScript function name"
                    ),
                    "parameters": parameters,
                    "bodyByteLength": body_size,
                    "body": body,
                }
            )
            position = body_end
        row["nextOffset"] = position
        instructions.append(row)
        if opcode == 0x00 and end is None:
            break
    if end is not None and position != end:
        raise HudAptConvertError("ActionScript bounded body ended at the wrong offset")
    if not instructions or (end is None and instructions[-1]["name"] != "end"):
        raise HudAptConvertError("ActionScript stream lacks a bounded end instruction")
    boundaries = {int(item["offset"]) for item in instructions}
    boundaries.add(position)
    for item in instructions:
        if item["name"] not in {"branch-always", "branch-if-true", "branch-if-false"}:
            continue
        target = int(item["nextOffset"]) + int(item["operand"])
        if target not in boundaries:
            raise HudAptConvertError(
                f"ActionScript branch at {item['offset']} targets non-instruction {target}"
            )
        item["targetOffset"] = target
    return instructions, position


def _flatten_action_instructions(
    instructions: Iterable[Mapping[str, Any]],
) -> list[Mapping[str, Any]]:
    rows: list[Mapping[str, Any]] = []
    for instruction in instructions:
        rows.append(instruction)
        body = instruction.get("body", [])
        if isinstance(body, list):
            rows.extend(_flatten_action_instructions(body))
    return rows


# Version tag for the additive per-program raw-byte contract. Consumers that
# do not know the field ignore it; the Godot raw-byte VM lane requires exactly
# this version and falls back to row synthesis / legacy effects otherwise.
VM_BYTECODE_VERSION = 1
MAX_VM_BYTE_SPACE = 4 * 1024 * 1024


def _vm_segment_string(reader: _Reader, pointer: int, context: str) -> tuple[int, int]:
    """Exact [start,end) byte range of one NUL-terminated operand string."""

    if not 0 <= pointer < len(reader.data):
        raise HudAptConvertError(f"{reader.label} {context} string is out of bounds")
    end = reader.data.find(b"\0", pointer, min(len(reader.data), pointer + 4097))
    if end < 0:
        raise HudAptConvertError(f"{reader.label} {context} string is unterminated")
    return pointer, end + 1


def _collect_vm_segment_ranges(
    movie: _Movie,
    instructions: list[dict[str, Any]],
    start: int,
    end: int,
) -> list[tuple[int, int]]:
    """Every byte range the VM addresses while executing [start,end).

    APT operand pointers (strings, constant-index tables, function parameter
    tables) are absolute within the movie byte space and usually live outside
    the instruction range, so the raw-byte contract must carry them too.
    """

    reader = movie.reader
    ranges: list[tuple[int, int]] = [(start, end)]

    def add_string(pointer: int, context: str) -> None:
        ranges.append(_vm_segment_string(reader, pointer, context))

    for row in _flatten_action_instructions(instructions):
        opcode = int(row["opcode"])
        aligned = (int(row["offset"]) + 1 + 3) & ~3
        if "operandPointer" in row:
            add_string(int(row["operandPointer"]), "VM segment operand")
        if "tablePointer" in row:
            table = int(row["tablePointer"])
            count = len(row.get("constants", []))
            if count:
                ranges.append((table, table + count * 4))
        if opcode == 0x83:
            add_string(reader.u32(aligned, "VM segment URL"), "VM segment URL")
            add_string(
                reader.u32(aligned + 4, "VM segment URL target"),
                "VM segment URL target",
            )
        if opcode in (0x8E, 0x9B):
            add_string(
                reader.u32(aligned, "VM segment function name"),
                "VM segment function name",
            )
            parameters = row.get("parameters", [])
            count = len(parameters)
            if count:
                if opcode == 0x8E:
                    table = reader.u32(aligned + 12, "VM segment parameter table")
                    stride, name_offset = 8, 4
                else:
                    table = reader.u32(aligned + 8, "VM segment parameter table")
                    stride, name_offset = 4, 0
                ranges.append((table, table + count * stride))
                for index in range(count):
                    add_string(
                        reader.u32(
                            table + index * stride + name_offset,
                            "VM segment parameter name",
                        ),
                        "VM segment parameter name",
                    )
    merged: list[tuple[int, int]] = []
    for range_start, range_end in sorted(ranges):
        if merged and range_start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], range_end))
        else:
            merged.append((range_start, range_end))
    return merged


def _vm_bytecode_contract(
    movie: _Movie,
    instructions: list[dict[str, Any]],
    start: int,
    end: int,
) -> dict[str, Any]:
    """Additive raw-byte execution contract for one supported program."""

    merged = _collect_vm_segment_ranges(movie, instructions, start, end)
    byte_space_size = merged[-1][1]
    if byte_space_size > MAX_VM_BYTE_SPACE:
        raise HudAptConvertError("VM byte space exceeds bounds")
    segments = [
        {
            "offset": range_start,
            "byteLength": range_end - range_start,
            "sha256": _sha(movie.reader.data[range_start:range_end]),
            "bytesBase64": base64.b64encode(
                movie.reader.data[range_start:range_end]
            ).decode("ascii"),
        }
        for range_start, range_end in merged
    ]
    return {
        "version": VM_BYTECODE_VERSION,
        "entryOffset": start,
        "byteLength": end - start,
        "sha256": _sha(movie.reader.data[start:end]),
        "byteSpaceSize": byte_space_size,
        "constantsSha256": str(movie.constants["sha256"]),
        "segments": segments,
    }


def _evaluate_timeline_action_subset(
    instructions: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], int, int, list[dict[str, Any]]]:
    unsupported = [
        {
            "offset": int(row["offset"]),
            "opcode": int(row["opcode"]),
            "name": str(row["name"]),
        }
        for row in _flatten_action_instructions(instructions)
        if str(row["name"]) not in _ACTION_TIMELINE_OPS
    ]
    if unsupported:
        return [], 0, 0, unsupported
    stack: list[dict[str, Any]] = []
    maximum = 0
    effects: list[dict[str, Any]] = []
    for row in instructions:
        name = str(row["name"])
        if name == "end":
            continue
        if name in {"push-string", "push-byte", "push-short", "push-long"}:
            value = row["operand"]
            stack.append(
                {
                    "type": "string" if name == "push-string" else "integer",
                    "value": value,
                }
            )
        elif name in {"push-zero", "push-one"}:
            stack.append({"type": "integer", "value": int(name == "push-one")})
        elif name == "pop":
            if not stack:
                raise HudAptConvertError(
                    f"ActionScript stack underflow at {row['offset']}"
                )
            stack.pop()
        elif name in {"play", "stop"}:
            effects.append({"kind": name})
        elif name in {"goto-frame", "goto-label"}:
            effects.append(
                {
                    "kind": "goto",
                    "targetType": "frame" if name == "goto-frame" else "label",
                    "target": row["operand"],
                }
            )
        elif name == "goto-frame2":
            if not stack:
                raise HudAptConvertError(
                    f"ActionScript stack underflow at {row['offset']}"
                )
            target = stack.pop()
            effects.append(
                {
                    "kind": "goto",
                    "targetType": target["type"],
                    "target": target["value"],
                }
            )
            effects.append({"kind": "play" if int(row["operand"]) & 1 else "stop"})
        maximum = max(maximum, len(stack))
    if stack:
        unsupported.append(
            {
                "offset": int(instructions[-1]["offset"]),
                "opcode": 0,
                "name": "unbalanced-stack",
            }
        )
        return [], maximum, len(stack), unsupported
    return effects, maximum, 0, []


def _typed_initialize_effect(
    movie: _Movie,
    source_offset: int,
    instruction_offset: int,
    payload: bytes,
    instructions: list[dict[str, Any]],
) -> tuple[dict[str, Any], int] | None:
    """Recognize the two byte-exact, source-proven HUD initialize effects."""

    expected = _TYPED_INITIALIZE_PROGRAMS.get((movie.name, source_offset))
    if expected is None:
        return None
    if (
        instruction_offset != int(expected["instructionOffset"])
        or len(payload) != int(expected["byteLength"])
        or _sha(payload) != str(expected["sha256"])
    ):
        return None
    names = [str(row.get("name", "")) for row in instructions]
    if movie.name == "InGameSideCommandBar":
        if names != [
            "constant-pool",
            "push-this-variable",
            "push-constant-byte",
            "define-function",
            "set-member",
            "end",
        ]:
            return None
        constants = instructions[0].get("constants", [])
        if (
            [item.get("value") for item in constants]
            != [
                "this",
                "SetFlashEffectState",
                "state",
                "_parent",
                "flashEffects",
                "_name",
                "gotoAndPlay",
            ]
            or int(instructions[2].get("operand", -1)) != 1
            or str(instructions[3].get("functionName", "invalid")) != ""
            or instructions[3].get("parameters") != [{"register": -1, "name": "state"}]
            or [str(row.get("name", "")) for row in instructions[3].get("body", [])]
            != [
                "push-value-of-variable",
                "push-one",
                "push-this-variable",
                "get-named-member",
                "get-named-member",
                "get-named-member",
                "push-value-of-variable",
                "get-named-member",
                "get-member",
                "call-named-method-pop",
            ]
        ):
            return None
        body = instructions[3]["body"]
        if [row.get("operand") for row in body if "operand" in row] != [
            2,
            3,
            3,
            4,
            3,
            5,
            6,
        ]:
            return None
    elif movie.name == "libInGameUI":
        if (
            names != ["push-string", "push-byte", "push-false", "set-property", "end"]
            or instructions[0].get("operand") != ""
            or int(instructions[1].get("operand", -1)) != 7
        ):
            return None
    elif movie.name == "Palantir":
        effect = expected["effect"]
        if (
            names
            != [
                "push-this-variable",
                "push-string",
                "set-string-member",
                "end",
            ]
            or instructions[1].get("operand") != effect["targetMember"]
            or instructions[2].get("operand") != effect["aptVariable"]
        ):
            return None
    else:  # pragma: no cover - the allowlist above makes this unreachable.
        return None
    # Round-trip through canonical JSON so no mutable constant object is shared
    # between conversions.
    effect = json.loads(json.dumps(expected["effect"], sort_keys=True))
    return effect, int(expected["maximumStackDepth"])


def _typed_minlod_effect(
    movie: _Movie,
    source_offset: int,
    instruction_offset: int,
    payload: bytes,
    instructions: list[dict[str, Any]],
) -> tuple[dict[str, Any], int] | None:
    """Recognize only the three byte-exact retail Palantir MinLOD branches."""

    expected = _TYPED_MINLOD_PROGRAMS.get((movie.name, source_offset))
    if expected is None:
        return None
    if (
        instruction_offset != int(expected["instructionOffset"])
        or len(payload) != int(expected["byteLength"])
        or _sha(payload) != str(expected["sha256"])
        or [str(row.get("name", "")) for row in instructions]
        != expected["instructionNames"]
    ):
        return None
    if source_offset == 152912:
        if (
            instructions[1].get("operand") != "MinLOD"
            or int(instructions[6].get("targetOffset", -1)) != 366989
            or instructions[8].get("operand") != ""
            or int(instructions[9].get("operand", -1)) != 13
            or instructions[11].get("operand") != "GlobeSwirlRender"
            or int(instructions[14].get("targetOffset", -1)) != 366997
        ):
            return None
    else:
        constants = instructions[0].get("constants", [])
        if (
            [row.get("value") for row in constants] != expected["constantValues"]
            or int(instructions[2].get("operand", -1)) != 1
            or int(instructions[4].get("targetOffset", -1)) != instruction_offset + 36
            or [int(instructions[index].get("operand", -1)) for index in (5, 6, 9, 10)]
            != [2, 3, 4, 3]
        ):
            return None
    effect = json.loads(json.dumps(expected["effect"], sort_keys=True))
    return effect, int(expected["maximumStackDepth"])


def _typed_resource_flash_effect(
    movie: _Movie,
    source_offset: int,
    instruction_offset: int,
    payload: bytes,
    instructions: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], int] | None:
    """Recognize only the oracle-proven Palantir resource-flash entry."""

    expected = _TYPED_RESOURCE_FLASH_PROGRAM
    if movie.name != expected["movie"] or source_offset != expected["sourceOffset"]:
        return None
    if (
        instruction_offset != expected["instructionOffset"]
        or len(payload) != expected["byteLength"]
        or _sha(payload) != expected["sha256"]
        or [str(row.get("name", "")) for row in instructions]
        != [
            "play",
            "push-string",
            "push-one",
            "get-string-variable",
            "push-string",
            "call-method-pop",
            "end",
        ]
        or instructions[1].get("operand") != "Gui_PalantirResourceBarFlash"
        or instructions[3].get("operand") != "_root"
        or instructions[4].get("operand") != "PlaySound"
    ):
        return None
    effects = json.loads(json.dumps(expected["effects"], sort_keys=True))
    return effects, int(expected["maximumStackDepth"])


def _typed_side_command_effects(
    movie: _Movie,
    source_offset: int,
    instruction_offset: int,
    payload: bytes,
    instructions: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], int] | None:
    """Recognize only the three oracle-sealed side-command topology calls."""

    expected = _TYPED_SIDE_COMMAND_PROGRAMS.get((movie.name, source_offset))
    if expected is None:
        return None
    if (
        instruction_offset != int(expected["instructionOffset"])
        or len(payload) != int(expected["byteLength"])
        or _sha(payload) != str(expected["sha256"])
        or [str(row.get("name", "")) for row in instructions]
        != expected["instructionNames"]
    ):
        return None
    if source_offset == 6272:
        if instructions[1].get("operand") != "UpdateNeighborFrameStates":
            return None
    elif source_offset == 6368:
        if [instructions[index].get("operand") for index in (1, 4)] != [
            "UpdateFrameState",
            "UpdateNeighborFrameStates",
        ]:
            return None
    elif (
        [row.get("value") for row in instructions[0].get("constants", [])]
        != expected["constantValues"]
        or int(instructions[2].get("operand", -1)) != 1
        or int(instructions[5].get("targetOffset", -1)) != 12220
        or int(instructions[11].get("operand", -1)) != 15
        or int(instructions[14].get("targetOffset", -1)) != 12220
        or [int(instructions[index].get("operand", -1)) for index in (15, 18, 19, 25)]
        != [3, 5, 2, 6]
        or int(instructions[30].get("targetOffset", -1)) != 12178
    ):
        return None
    effects = json.loads(json.dumps(expected["effects"], sort_keys=True))
    return effects, int(expected["maximumStackDepth"])


def _typed_palantir_command_effects(
    movie: _Movie,
    source_offset: int,
    instruction_offset: int,
    payload: bytes,
    instructions: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], int] | None:
    """Recognize only the two oracle-safe CommandButtons registrations."""

    expected = _TYPED_PALANTIR_COMMAND_PROGRAMS.get((movie.name, source_offset))
    if expected is None:
        return None
    if (
        instruction_offset != int(expected["instructionOffset"])
        or len(payload) != int(expected["byteLength"])
        or _sha(payload) != str(expected["sha256"])
        or [str(row.get("name", "")) for row in instructions]
        != expected["instructionNames"]
        or [row.get("value") for row in instructions[0].get("constants", [])]
        != expected["constantValues"]
    ):
        return None
    function_rows = [
        row for row in instructions if row.get("name") == "define-function2"
    ]
    specifications = (
        _PALANTIR_COMMAND_LIFECYCLE_FUNCTIONS
        if source_offset == 169224
        else _PALANTIR_COMMAND_BUTTON_METHODS
    )
    if len(function_rows) != len(specifications):
        return None
    for row, specification in zip(function_rows, specifications, strict=True):
        body_offset = int(specification["bodyOffset"])
        body_length = int(specification["bodyByteLength"])
        expected_name = (
            str(specification["name"]) if source_offset == 169224 else ""
        )
        if (
            int(row.get("offset", -1)) != int(specification["definitionOffset"])
            or str(row.get("functionName", "")) != expected_name
            or int(row.get("registerCount", -1)) != (2 if source_offset == 169224 else 3)
            or row.get("parameters")
            != [
                {
                    "name": "clip" if source_offset == 169224 else "state",
                    "register": 1 if source_offset == 169224 else 2,
                }
            ]
            or int(row.get("bodyByteLength", -1)) != body_length
            or not row.get("body")
            or int(row["body"][0].get("offset", -1)) != body_offset
            or _sha(movie.data[body_offset : body_offset + body_length])
            != str(specification["bodySha256"])
        ):
            return None
    if source_offset == 169256 and (
        int(instructions[1].get("operand", -1)) != 0
        or int(instructions[5].get("operand", -1)) != 6
        or int(instructions[8].get("targetOffset", -1)) != 368412
        or [int(instructions[index].get("operand", -1)) for index in (16, 20, 24)]
        != [3, 8, 10]
        or int(instructions[27].get("operand", -1)) != 0
        or int(instructions[31].get("targetOffset", -1)) != 368136
    ):
        return None
    effects = json.loads(json.dumps(expected["effects"], sort_keys=True))
    return effects, int(expected["maximumStackDepth"])


def _decode_action_program(movie: _Movie, row: Mapping[str, Any]) -> dict[str, Any]:
    source_offset = int(row["sourceOffset"])
    instruction_offset = int(row["instructionsOffset"])
    instructions, end = _decode_action_sequence(movie, instruction_offset)
    effects, maximum, terminal, unsupported = _evaluate_timeline_action_subset(
        instructions
    )
    payload = movie.data[instruction_offset:end]
    typed_initialize = None
    if str(row["kind"]) == "clip-action-event":
        typed_initialize = _typed_initialize_effect(
            movie, source_offset, instruction_offset, payload, instructions
        )
    if typed_initialize is not None:
        effect, maximum = typed_initialize
        effects = [effect]
        terminal = 0
        unsupported = []
    typed_minlod = None
    if str(row["kind"]) == "action-script":
        typed_minlod = _typed_minlod_effect(
            movie, source_offset, instruction_offset, payload, instructions
        )
    if typed_minlod is not None:
        effect, maximum = typed_minlod
        effects = [effect]
        terminal = 0
        unsupported = []
    typed_resource_flash = None
    if str(row["kind"]) == "action-script":
        typed_resource_flash = _typed_resource_flash_effect(
            movie, source_offset, instruction_offset, payload, instructions
        )
    if typed_resource_flash is not None:
        effects, maximum = typed_resource_flash
        terminal = 0
        unsupported = []
    typed_side_command = None
    if str(row["kind"]) == "action-script":
        typed_side_command = _typed_side_command_effects(
            movie, source_offset, instruction_offset, payload, instructions
        )
    if typed_side_command is not None:
        effects, maximum = typed_side_command
        terminal = 0
        unsupported = []
    typed_palantir_command = None
    if str(row["kind"]) == "action-script":
        typed_palantir_command = _typed_palantir_command_effects(
            movie, source_offset, instruction_offset, payload, instructions
        )
    if typed_palantir_command is not None:
        effects, maximum = typed_palantir_command
        terminal = 0
        unsupported = []
    program = {
        "scriptId": f"{movie.name.casefold()}:{source_offset}",
        "movie": movie.name,
        "actionKind": str(row["kind"]),
        "sourceOffset": source_offset,
        "instructionOffset": instruction_offset,
        "byteLength": len(payload),
        "sha256": _sha(payload),
        "instructions": instructions,
        "supported": not unsupported,
        "effects": effects,
        "maximumStackDepth": maximum,
        "terminalStackDepth": terminal,
        "unsupportedInstructions": unsupported,
    }
    if program["supported"]:
        # Additive raw-byte contract: exact retail bytes, byte-space offsets,
        # and the movie CONST identity for the real-VM execution lane.
        program["vmBytecode"] = _vm_bytecode_contract(
            movie, instructions, instruction_offset, end
        )
    return program


def _action_program_capabilities(program: Mapping[str, Any]) -> list[str]:
    names = {
        str(item["name"])
        for item in program.get("unsupportedInstructions", [])
        if isinstance(item, Mapping)
    }
    capabilities: list[str] = []
    if any(name in _ACTION_HOST_OPS for name in names):
        capabilities.append("host-callback")
    if any(name in _ACTION_EXTERNAL_OPS for name in names):
        capabilities.append("external-state")
    if any(name in _ACTION_CONTROL_OPS for name in names):
        capabilities.append("control-flow-or-function")
    if any(
        name not in _ACTION_HOST_OPS | _ACTION_EXTERNAL_OPS | _ACTION_CONTROL_OPS
        for name in names
    ):
        capabilities.append("opcode-subset")
    return capabilities


def _is_typed_initialize_program(program: Mapping[str, Any]) -> bool:
    effects = program.get("effects", [])
    return (
        bool(program.get("supported"))
        and isinstance(effects, list)
        and len(effects) == 1
        and isinstance(effects[0], Mapping)
        and str(effects[0].get("kind", ""))
        in {"define-local-method", "set-clip-property", "bind-live-text"}
    )


@dataclass(frozen=True)
class _Transform:
    matrix: tuple[float, float, float, float] = (1.0, 0.0, 0.0, 1.0)
    translation: tuple[float, float] = (0.0, 0.0)
    tint: tuple[float, float, float, float] = (1.0, 1.0, 1.0, 1.0)
    additive: tuple[float, float, float, float] = (0.0, 0.0, 0.0, 0.0)

    def combine(self, child: _Transform) -> _Transform:
        a, b, c, d = self.matrix
        e, f, g, h = child.matrix
        matrix = (a * e + b * g, a * f + b * h, c * e + d * g, c * f + d * h)
        tint = tuple(self.tint[i] * child.tint[i] for i in range(4))
        additive = tuple(
            self.additive[i] * child.tint[i] + child.additive[i] for i in range(4)
        )
        return _Transform(
            matrix=matrix,
            translation=(
                self.translation[0] + child.translation[0],
                self.translation[1] + child.translation[1],
            ),
            tint=tint,  # type: ignore[arg-type]
            additive=additive,  # type: ignore[arg-type]
        )

    def point(self, point: tuple[float, float]) -> list[float]:
        a, b, c, d = self.matrix
        x, y = point
        return [
            x * a + y * c + self.translation[0],
            x * b + y * d + self.translation[1],
        ]

    def color(self, source: tuple[float, float, float, float]) -> list[float]:
        return [
            min(1.0, max(0.0, source[i] * self.tint[i] + self.additive[i]))
            for i in range(4)
        ]


@dataclass
class _Movie:
    name: str
    data: bytes
    reader: _Reader
    root: Mapping[str, Any]
    constants: dict[str, Any]
    characters: list[dict[str, Any]]
    frames: list[list[dict[str, Any]]]
    imports: dict[int, tuple[str, str]]
    exports: dict[str, list[int]]
    geometry: dict[int, list[dict[str, Any]]]
    image_map: dict[int, int]
    atlases: dict[int, dict[str, Any]]


def _rgba(data: bytes, offset: int) -> tuple[float, float, float, float]:
    if offset < 0 or offset > len(data) - 4:
        raise HudAptConvertError("RGBA range is out of bounds")
    return tuple(value / 255.0 for value in data[offset : offset + 4])  # type: ignore[return-value]


def _parse_clip_actions(reader: _Reader, offset: int) -> dict[str, Any]:
    """Decode one APT clip-event list without inventing dispatch semantics."""

    reader.require(offset, 8, "clip-action list")
    count = reader.i32(offset, "clip-action event count")
    table_offset = reader.u32(offset + 4, "clip-action event table")
    if not 0 < count <= MAX_CLIP_ACTIONS:
        raise HudAptConvertError("clip-action event count is empty or exceeds bounds")
    reader.require(table_offset, count * 12, "clip-action event table")
    events: list[dict[str, Any]] = []
    for index in range(count):
        event_offset = table_offset + index * 12
        mask = int.from_bytes(reader.data[event_offset : event_offset + 3], "little")
        if mask == 0 or mask & ~_CLIP_EVENT_MASK:
            raise HudAptConvertError(
                f"clip-action event mask 0x{mask:06x} at {event_offset} is invalid"
            )
        key_code = reader.data[event_offset + 3]
        if key_code and not mask & 0x000002:
            raise HudAptConvertError(
                f"clip-action key code at {event_offset} lacks key-press event"
            )
        record = reader.data[event_offset : event_offset + 12]
        events.append(
            {
                "eventIndex": index,
                "eventOffset": event_offset,
                "eventEndOffset": event_offset + 12,
                "eventMask": mask,
                "eventNames": [
                    name for flag, name in _CLIP_EVENT_NAMES.items() if mask & flag
                ],
                "keyCode": key_code,
                "nextEventOffset": reader.u32(
                    event_offset + 4, "clip-action next-event offset"
                ),
                "instructionsOffset": reader.u32(
                    event_offset + 8, "clip-action instructions"
                ),
                "recordSha256": _sha(record),
            }
        )
    header = reader.data[offset : offset + 8]
    return {
        "clipActionsOffset": offset,
        "headerEndOffset": offset + 8,
        "eventCount": count,
        "eventTableOffset": table_offset,
        "headerSha256": _sha(header),
        "events": events,
    }


def _parse_place_object(reader: _Reader, offset: int) -> dict[str, Any]:
    """Decode the fixed PlaceObject record used by BFME2 APT v6/v7."""

    reader.require(offset, 60, "place-object")
    if reader.u32(offset, "place-object kind") != 3:
        raise HudAptConvertError("place-object kind changed")
    flags = reader.u32(offset + 4, "place-object flags")
    if flags & ~0xFF:
        raise HudAptConvertError("place-object reserved flag bytes changed")
    row: dict[str, Any] = {
        "kind": "place-object",
        "sourceOffset": offset,
        "flags": flags,
        "depth": reader.i32(offset + 8, "place-object depth"),
        "characterId": reader.i32(offset + 12, "place-object character"),
        "matrix": [
            reader.f32(offset + 16 + index * 4, "place-object matrix")
            for index in range(4)
        ],
        "translation": [
            reader.f32(offset + 32 + index * 4, "place-object translation")
            for index in range(2)
        ],
        "tint": list(_rgba(reader.data, offset + 40)),
        "additive": list(_rgba(reader.data, offset + 44)),
        "ratio": reader.f32(offset + 48, "place-object ratio"),
        "clipDepth": reader.i32(offset + 56, "place-object clip depth"),
    }
    name_pointer = reader.u32(offset + 52, "place-object name")
    if flags & 0x20:
        row["name"] = reader.string(name_pointer, "place-object name")
    if flags & 0x80:
        clip_actions_offset = reader.u32(offset + 60, "place-object clip actions")
        if clip_actions_offset == 0:
            identity = (reader.label.casefold(), offset, flags)
            if identity not in _EXPECTED_FLAGGED_NULL_CLIP_ACTIONS:
                raise HudAptConvertError("place-object clip actions pointer is null")
            row["clipActionsOffset"] = 0
            row["clipActionsPointerState"] = "source-flagged-null"
            row["clipActionsRecordSha256"] = _sha(reader.data[offset : offset + 64])
            return row
        row["clipActionsOffset"] = clip_actions_offset
        row["clipActions"] = _parse_clip_actions(reader, clip_actions_offset)
    return row


def _bounded_bool(reader: _Reader, offset: int, context: str) -> bool:
    value = reader.u32(offset, context)
    if value not in (0, 1):
        raise HudAptConvertError(f"{reader.label} {context} is not a boolean")
    return bool(value)


def _parse_font_character(reader: _Reader, offset: int) -> dict[str, Any]:
    reader.require(offset, 20, "font character")
    if reader.u32(offset, "font kind") != 3:
        raise HudAptConvertError("font character kind changed")
    name_pointer = reader.u32(offset + 8, "font name")
    glyph_count = reader.u32(offset + 12, "font glyph count")
    glyph_table = reader.u32(offset + 16, "font glyph table")
    if glyph_count > 65_536:
        raise HudAptConvertError("font glyph count exceeds bounds")
    if glyph_count:
        if glyph_table == 0:
            raise HudAptConvertError("font glyph table is null")
        reader.require(glyph_table, glyph_count * 4, "font glyph table")
    return {
        "sourceOffset": offset,
        "definitionByteLength": 20,
        "definitionSha256": _sha(reader.data[offset : offset + 20]),
        "name": reader.string(name_pointer, "font name"),
        "glyphCount": glyph_count,
        "glyphCharacterIds": [
            reader.u32(glyph_table + index * 4, "font glyph character")
            for index in range(glyph_count)
        ],
    }


def _parse_text_character(reader: _Reader, offset: int) -> dict[str, Any]:
    reader.require(offset, 60, "text character")
    if reader.u32(offset, "text kind") != 2:
        raise HudAptConvertError("text character kind changed")
    bounds = [reader.f32(offset + 8 + index * 4, "text bounds") for index in range(4)]
    if bounds[2] < bounds[0] or bounds[3] < bounds[1]:
        raise HudAptConvertError("text bounds are inverted")
    alignment = reader.u32(offset + 28, "text alignment")
    if alignment not in (0, 1, 2):
        raise HudAptConvertError("text alignment code is unsupported")
    font_height = reader.f32(offset + 36, "text font height")
    if not 0.0 < font_height <= 4096.0:
        raise HudAptConvertError("text font height is out of bounds")
    return {
        "sourceOffset": offset,
        "definitionByteLength": 60,
        "definitionSha256": _sha(reader.data[offset : offset + 60]),
        "bounds": bounds,
        "fontCharacterId": reader.u32(offset + 24, "text font character"),
        "alignmentCode": alignment,
        "color": list(_rgba(reader.data, offset + 32)),
        "fontHeight": font_height,
        "readOnly": _bounded_bool(reader, offset + 40, "text read-only"),
        "multiline": _bounded_bool(reader, offset + 44, "text multiline"),
        "wordWrap": _bounded_bool(reader, offset + 48, "text word-wrap"),
        "placeholder": reader.string(
            reader.u32(offset + 52, "text content pointer"), "text content"
        ),
        "variableName": reader.string(
            reader.u32(offset + 56, "text variable pointer"), "text variable"
        ),
    }


def _parse_button_character(reader: _Reader, offset: int) -> dict[str, Any]:
    reader.require(offset, 60, "button character")
    if reader.u32(offset, "button kind") != 4:
        raise HudAptConvertError("button character kind changed")
    bounds = [
        reader.f32(offset + 12 + index * 4, "button bounds") for index in range(4)
    ]
    if bounds[2] < bounds[0] or bounds[3] < bounds[1]:
        raise HudAptConvertError("button bounds are inverted")
    triangle_count = reader.u32(offset + 28, "button triangle count")
    vertex_count = reader.u32(offset + 32, "button vertex count")
    vertex_table = reader.u32(offset + 36, "button vertex table")
    triangle_table = reader.u32(offset + 40, "button triangle table")
    record_count = reader.u32(offset + 44, "button record count")
    record_table = reader.u32(offset + 48, "button record table")
    action_count = reader.u32(offset + 52, "button action count")
    action_table = reader.u32(offset + 56, "button action table")
    if vertex_count > 65_536 or triangle_count > 65_536:
        raise HudAptConvertError("button hit geometry exceeds bounds")
    if record_count > 16_384 or action_count > 16_384:
        raise HudAptConvertError("button state/action count exceeds bounds")
    for count, table, stride, context in (
        (vertex_count, vertex_table, 8, "button vertices"),
        (triangle_count, triangle_table, 6, "button triangles"),
        (record_count, record_table, 68, "button records"),
        (action_count, action_table, 8, "button actions"),
    ):
        if count and table == 0:
            raise HudAptConvertError(f"{context} table is null")
        if count:
            reader.require(table, count * stride, context)
    vertices = [
        [
            reader.f32(vertex_table + index * 8, "button vertex x"),
            reader.f32(vertex_table + index * 8 + 4, "button vertex y"),
        ]
        for index in range(vertex_count)
    ]
    triangles = [
        [
            reader.u16(triangle_table + index * 6, "button triangle index"),
            reader.u16(triangle_table + index * 6 + 2, "button triangle index"),
            reader.u16(triangle_table + index * 6 + 4, "button triangle index"),
        ]
        for index in range(triangle_count)
    ]
    if any(index >= vertex_count for triangle in triangles for index in triangle):
        raise HudAptConvertError("button triangle references a missing vertex")
    records: list[dict[str, Any]] = []
    for index in range(record_count):
        position = record_table + index * 68
        flags = reader.data[position]
        reserved = int.from_bytes(reader.data[position + 1 : position + 4], "little")
        if flags == 0 or flags & ~0x0F or reserved != 0:
            raise HudAptConvertError("button record flags or reserved bytes changed")
        records.append(
            {
                "recordIndex": index,
                "sourceOffset": position,
                "stateMask": flags,
                "states": [
                    name for flag, name in _BUTTON_RECORD_STATES.items() if flags & flag
                ],
                "characterId": reader.u32(position + 4, "button record character"),
                "depth": reader.i32(position + 8, "button record depth"),
                "matrix": [
                    reader.f32(position + 12 + item * 4, "button record matrix")
                    for item in range(4)
                ],
                "translation": [
                    reader.f32(position + 28 + item * 4, "button record translation")
                    for item in range(2)
                ],
                "color": [
                    reader.f32(position + 36 + item * 4, "button record color")
                    for item in range(4)
                ],
                "unknown": [
                    reader.f32(position + 52 + item * 4, "button record unknown")
                    for item in range(4)
                ],
            }
        )
    actions: list[dict[str, Any]] = []
    for index in range(action_count):
        position = action_table + index * 8
        flags = reader.data[position]
        reserved = reader.data[position + 3]
        key_code = reader.u16(position + 1, "button action key code")
        # A zero transition mask is legal when the record is a key-press-only
        # handler (the retail BFME2 shell authors one such action on
        # ``MainMenu`` character 103).  A record with neither a transition nor
        # a key code carries no trigger at all and still fails closed.
        if (flags == 0 and key_code == 0) or reserved != 0:
            raise HudAptConvertError("button action flags or reserved byte changed")
        actions.append(
            {
                "actionIndex": index,
                "sourceOffset": position,
                "transitionMask": flags,
                "transitions": [
                    name
                    for flag, name in _BUTTON_ACTION_TRANSITIONS.items()
                    if flags & flag
                ],
                "keyCode": reader.u16(position + 1, "button action key code"),
                "instructionsOffset": reader.u32(
                    position + 4, "button action instructions"
                ),
            }
        )
    return {
        "sourceOffset": offset,
        "definitionByteLength": 60,
        "definitionSha256": _sha(reader.data[offset : offset + 60]),
        "isMenu": _bounded_bool(reader, offset + 8, "button menu"),
        "bounds": bounds,
        "vertices": vertices,
        "triangles": triangles,
        "records": records,
        "actions": actions,
    }


def _parse_frame_items(
    reader: _Reader, frame_table: int, count: int
) -> list[list[dict[str, Any]]]:
    reader.require(frame_table, count * 8, "frame table")
    frames: list[list[dict[str, Any]]] = []
    for frame_index in range(count):
        header = frame_table + frame_index * 8
        item_count = reader.i32(header, "frame item count")
        item_table = reader.u32(header + 4, "frame item table")
        if not 0 <= item_count <= 16_384:
            raise HudAptConvertError("frame item count exceeds bounds")
        reader.require(item_table, item_count * 4, "frame item pointers")
        rows: list[dict[str, Any]] = []
        for item_index in range(item_count):
            pointer = reader.u32(item_table + item_index * 4, "frame item pointer")
            reader.require(pointer, 4, "frame item")
            kind = reader.u32(pointer, "frame item kind")
            if kind == 3:
                rows.append(_parse_place_object(reader, pointer))
            elif kind == 4:
                rows.append(
                    {
                        "kind": "remove-object",
                        "sourceOffset": pointer,
                        "depth": reader.i32(pointer + 4, "remove depth"),
                    }
                )
            elif kind == 5:
                rows.append(
                    {
                        "kind": "background-color",
                        "sourceOffset": pointer,
                        "color": list(_rgba(reader.data, pointer + 4)),
                    }
                )
            elif kind == 2:
                rows.append(
                    {
                        "kind": "frame-label",
                        "sourceOffset": pointer,
                        "name": reader.string(
                            reader.u32(pointer + 4, "frame label name"),
                            "frame label name",
                        ),
                        "flags": reader.u32(pointer + 8, "frame label flags"),
                        "frameId": reader.u32(pointer + 12, "frame label id"),
                    }
                )
            elif kind in (1, 8):
                rows.append(
                    {
                        "kind": "action-script" if kind == 1 else "init-action-script",
                        "sourceOffset": pointer,
                        "instructionsOffset": reader.u32(
                            pointer + 4, "action-script instructions"
                        ),
                    }
                )
            else:
                rows.append({"kind": f"unknown-{kind}", "sourceOffset": pointer})
        frames.append(rows)
    return frames


def _parse_geometry_text(text: str, label: str) -> list[dict[str, Any]]:
    groups: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    for line_number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if line == "c":
            current = None
            continue
        if line.startswith("s "):
            values = [part.strip() for part in line[1:].split(":")]
            style = values[0]
            expected = {"s": 5, "l": 6, "tc": 12}.get(style)
            if expected is None or len(values) != expected:
                raise HudAptConvertError(f"{label}:{line_number} unsupported RU style")
            try:
                numbers = [float(value) for value in values[1:]]
            except ValueError as exc:
                raise HudAptConvertError(
                    f"{label}:{line_number} invalid RU style"
                ) from exc
            if not all(math.isfinite(value) for value in numbers):
                raise HudAptConvertError(f"{label}:{line_number} non-finite RU style")
            current = {"style": style, "values": numbers, "primitives": []}
            groups.append(current)
            continue
        if current is None:
            raise HudAptConvertError(f"{label}:{line_number} primitive without style")
        prefix = line[0]
        expected = 6 if prefix == "t" else 4 if prefix == "l" else 0
        values = [part.strip() for part in line[1:].split(":")]
        if expected == 0 or len(values) != expected:
            raise HudAptConvertError(f"{label}:{line_number} unsupported RU primitive")
        try:
            numbers = [float(value) for value in values]
        except ValueError as exc:
            raise HudAptConvertError(
                f"{label}:{line_number} invalid RU primitive"
            ) from exc
        if not all(math.isfinite(value) for value in numbers):
            raise HudAptConvertError(f"{label}:{line_number} non-finite RU primitive")
        current["primitives"].append(
            [numbers[index : index + 2] for index in range(0, expected, 2)]
        )
    return groups


def _verified_source(asset_root: Path, virtual_path: str, sha256: str) -> bytes:
    root = asset_root.resolve()
    source = (root / Path(*virtual_path.replace("\\", "/").split("/"))).resolve()
    try:
        source.relative_to(root)
    except ValueError as exc:
        raise HudAptConvertError(f"source escaped asset root: {virtual_path}") from exc
    if not source.is_file() or source.is_symlink():
        raise HudAptConvertError(f"source is missing or linked: {virtual_path}")
    data = source.read_bytes()
    if _sha(data) != sha256:
        raise HudAptConvertError(f"source SHA256 changed: {virtual_path}")
    return data


def _verified_mapping_source(
    sources: Mapping[str, bytes], virtual_path: str, sha256: str
) -> bytes:
    data = sources.get(virtual_path.casefold())
    if data is None:
        raise HudAptConvertError(f"bundle source is missing: {virtual_path}")
    if _sha(data) != sha256:
        raise HudAptConvertError(f"bundle source SHA256 changed: {virtual_path}")
    return data


def _movie_from_plan(
    raw: Mapping[str, Any],
    asset_root: Path | None = None,
    source_bytes: Mapping[str, bytes] | None = None,
) -> _Movie:
    name = str(raw.get("movie"))
    apt_summary = raw.get("apt")
    if not isinstance(apt_summary, Mapping):
        raise HudAptConvertError(f"{name} lacks an APT contract")
    virtual_path = str(apt_summary.get("virtualPath"))
    if source_bytes is None:
        if asset_root is None:
            raise HudAptConvertError("movie decoder has no source reader")

        def read_source(path: str, digest: str) -> bytes:
            return _verified_source(asset_root, path, digest)

    else:

        def read_source(path: str, digest: str) -> bytes:
            return _verified_mapping_source(source_bytes, path, digest)

    data = read_source(virtual_path, str(apt_summary.get("sha256")))
    constants_summary = raw.get("constants")
    if not isinstance(constants_summary, Mapping):
        raise HudAptConvertError(f"{name} lacks a CONST contract")
    constants_path = str(constants_summary.get("virtualPath"))
    constants_data = read_source(constants_path, str(constants_summary.get("sha256")))
    constants = parse_apt_constants(constants_data, constants_path)
    reader = _Reader(data, virtual_path)
    root = apt_summary.get("root")
    characters_summary = apt_summary.get("characters")
    if not isinstance(root, Mapping) or not isinstance(characters_summary, list):
        raise HudAptConvertError(f"{name} APT contract shape changed")
    entry = int(root["entryOffset"])
    movie = entry + 8
    frame_count = reader.i32(movie, "movie frame count")
    frame_table = reader.u32(movie + 4, "movie frame table")
    frames = _parse_frame_items(reader, frame_table, frame_count)
    character_count = reader.i32(movie + 12, "movie character count")
    character_table = reader.u32(movie + 16, "movie character table")
    if character_count != len(characters_summary):
        raise HudAptConvertError(f"{name} character count changed")
    reader.require(character_table, character_count * 4, "character table")
    characters: list[dict[str, Any]] = []
    for character_id, summary in enumerate(characters_summary):
        pointer = reader.u32(character_table + character_id * 4, "character pointer")
        kind = str(summary.get("kind")) if isinstance(summary, Mapping) else "invalid"
        row: dict[str, Any] = {
            "characterId": character_id,
            "kind": kind,
            "pointer": pointer,
        }
        if pointer and kind == "shape":
            reader.require(pointer, 36, "shape")
            row["geometryId"] = reader.u32(pointer + 24, "shape geometry")
        elif pointer and kind == "text":
            row.update(_parse_text_character(reader, pointer))
        elif pointer and kind == "font":
            row.update(_parse_font_character(reader, pointer))
        elif pointer and kind == "button":
            row.update(_parse_button_character(reader, pointer))
        elif pointer and kind == "sprite":
            count = reader.i32(pointer + 8, "sprite frame count")
            table = reader.u32(pointer + 12, "sprite frame table")
            row["frames"] = _parse_frame_items(reader, table, count)
        characters.append(row)

    imports = {
        int(item["characterId"]): (str(item["movie"]), str(item["symbol"]))
        for item in apt_summary.get("imports", [])
        if isinstance(item, Mapping)
    }
    exports: dict[str, list[int]] = {}
    for item in apt_summary.get("exports", []):
        if isinstance(item, Mapping):
            symbol = str(item["symbol"]).casefold()
            character_id = int(item["characterId"])
            exports.setdefault(symbol, []).append(character_id)

    geometry: dict[int, list[dict[str, Any]]] = {}
    for item in raw.get("geometry", []):
        if not isinstance(item, Mapping):
            raise HudAptConvertError(f"{name} geometry contract changed")
        path = str(item["virtualPath"])
        match = re.search(r"/(\d+)\.ru$", path.replace("\\", "/"))
        if match is None:
            raise HudAptConvertError(f"{name} geometry path changed: {path}")
        source = read_source(path, str(item["sha256"]))
        try:
            text = source.decode("ascii")
        except UnicodeDecodeError as exc:
            raise HudAptConvertError(f"{path} is not ASCII") from exc
        geometry[int(match.group(1))] = _parse_geometry_text(text, path)

    image_map = {
        int(item["imageId"]): int(item["textureId"])
        for item in raw.get("imageMap", {}).get("mappings", [])
        if isinstance(item, Mapping)
    }
    atlases: dict[int, dict[str, Any]] = {}
    for atlas in raw.get("atlases", []):
        if not isinstance(atlas, Mapping):
            raise HudAptConvertError(f"{name} atlas contract changed")
        match = re.search(r"_(\d+)\.tga$", str(atlas["virtualPath"]), re.IGNORECASE)
        if match is None:
            raise HudAptConvertError(f"{name} atlas name changed")
        atlases[int(match.group(1))] = dict(atlas)
    return _Movie(
        name,
        data,
        reader,
        root,
        constants,
        characters,
        frames,
        imports,
        exports,
        geometry,
        image_map,
        atlases,
    )


def _timeline_labels(frames: list[list[dict[str, Any]]]) -> dict[str, int]:
    result: dict[str, int] = {}
    for frame_index, rows in enumerate(frames):
        for row in rows:
            if row.get("kind") != "frame-label":
                continue
            name = str(row.get("name", ""))
            if not name or name in result or int(row.get("frameId", -1)) != frame_index:
                raise HudAptConvertError(
                    f"invalid or duplicate timeline label: {name!r}"
                )
            result[name] = frame_index
    return result


def _bounded_initial_selection(
    movies: Mapping[str, _Movie],
) -> tuple[dict[str, Any], dict[tuple[str, int], int], list[dict[str, Any]]]:
    """Prove and select only the retail-authored InitialSetup HUD state."""

    palantir = movies.get("palantir")
    export = movies.get("palantirexport")
    side = movies.get("ingamesidecommandbar")
    if palantir is None or export is None or side is None:
        raise HudAptConvertError("bounded HUD selection movies are missing")
    try:
        bytecode = _assert_palantir_bytecode(
            palantir.data, palantir.constants["entries"]
        )
    except (HudFrameSelectionError, KeyError, TypeError) as exc:
        raise HudAptConvertError(
            f"bounded Palantir InitialSetup proof failed: {exc}"
        ) from exc
    initial = bytecode.get("initialSetup")
    if (
        not isinstance(initial, Mapping)
        or initial.get("selectedState") != "_good"
        or initial.get("target") != "PalantirFrame"
        or initial.get("method") != "gotoAndPlay"
    ):
        raise HudAptConvertError("bounded Palantir InitialSetup contract changed")

    root_placements = [
        row
        for row in palantir.frames[0]
        if row.get("kind") == "place-object" and row.get("name") == "PalantirFrame"
    ]
    if (
        len(root_placements) != 1
        or int(root_placements[0].get("characterId", -1)) != 105
    ):
        raise HudAptConvertError("PalantirFrame root placement changed")
    frame_character = palantir.characters[105]
    frame_rows = frame_character.get("frames")
    if not isinstance(frame_rows, list) or len(frame_rows) != 49:
        raise HudAptConvertError("PalantirFrame timeline changed")
    labels = _timeline_labels(frame_rows)
    expected_labels = {
        "_hide": 0,
        "_goodSingle": 9,
        "_good": 19,
        "_evilSingle": 29,
        "_evil": 39,
    }
    if labels != expected_labels:
        raise HudAptConvertError("PalantirFrame state labels changed")
    selected_rows = [row for row in frame_rows[19] if row.get("kind") == "place-object"]
    if len(selected_rows) != 1 or int(selected_rows[0]["characterId"]) != 102:
        raise HudAptConvertError("Palantir Good Double placement changed")
    if palantir.imports.get(102) != (
        "PalantirExport",
        "PalantirFrame_GoodDouble",
    ):
        raise HudAptConvertError("Palantir Good Double import changed")
    export_ids = sorted(set(export.exports.get("palantirframe_gooddouble", [])))
    if export_ids != [19]:
        raise HudAptConvertError("Palantir Good Double export changed")

    side_labels = _timeline_labels(side.frames)
    if side_labels != {"_hide": 1, "_fadeIn": 11, "_fadeOut": 31}:
        raise HudAptConvertError("side-command-bar labels changed")
    side_places = [row for row in side.frames[0] if row.get("kind") == "place-object"]
    if (
        len(side_places) != 1
        or side_places[0].get("name") != "ButtonSet"
        or int(side_places[0].get("characterId", -1)) != 21
        or side_places[0].get("translation") != [1048.300048828125, 361.29998779296875]
    ):
        raise HudAptConvertError("side-command-bar initial placement changed")
    side_stop = [row for row in side.frames[10] if row.get("kind") == "action-script"]
    if (
        len(side_stop) != 1
        or side.reader.u32(int(side_stop[0]["sourceOffset"]) + 4, "stop action") != 9380
        or side.data[9380:9382] != b"\x07\x00"
    ):
        raise HudAptConvertError("side-command-bar hidden Stop changed")

    contract = {
        "policy": "bounded-retail-initial-setup-plus-men-fords-side-fade",
        "actionScriptVmUsed": False,
        "unknownStatePolicy": "fail-closed",
        "palantir": {
            "initialSetupState": "_good",
            "selectedVariant": "good-double",
            "rootCharacterId": 105,
            "selectedFrameIndex": 19,
            "localImportCharacterId": 102,
            "importMovie": "PalantirExport",
            "importSymbol": "PalantirFrame_GoodDouble",
            "exportCharacterId": 19,
            "initialSetupBody": dict(initial["body"]),
        },
        "inGameSideCommandBar": {
            "initialState": "hidden-offscreen",
            "initialFrameIndex": 0,
            "hiddenLabelFrameIndex": 1,
            "settledFrameIndex": 10,
            "fadeInLabelFrameIndex": 11,
            "buttonSetTranslation": [1048.300048828125, 361.29998779296875],
            "fadeInApplied": False,
            "selectionDrivenFadeInBound": True,
            "fadeRuntimeContract": "sideCommandFadeRuntime",
        },
    }
    blockers = [
        {
            "code": "palantir-nondefault-frame-selection-not-bound",
            "movie": "Palantir",
            "appliedState": "_good",
            "unboundStates": ["_evil", "_evilSingle", "_goodSingle"],
        },
    ]
    return contract, {("palantir", 105): 19}, blockers


def _retail_ini_block(text: str, kind: str, name: str) -> str:
    match = re.search(
        rf"(?ims)^\s*{re.escape(kind)}\s+{re.escape(name)}\s*$"
        rf"(.*?)^\s*End\s*$",
        text,
    )
    if match is None:
        raise HudAptConvertError(f"retail INI block is missing: {kind} {name}")
    return match.group(1)


def _retail_object_body(text: str, name: str) -> str:
    declaration = re.search(
        rf"(?im)^\s*(?:Object|ChildObject)\s+{re.escape(name)}(?:\s|$)", text
    )
    if declaration is None:
        raise HudAptConvertError(f"retail roster object is missing: {name}")
    next_declaration = re.search(
        r"(?im)^\s*(?:Object|ChildObject)\s+[^\s;]+", text[declaration.end() :]
    )
    end = (
        declaration.end() + next_declaration.start()
        if next_declaration is not None
        else len(text)
    )
    return text[declaration.end() : end]


def _men_fords_roster_contract(
    retail_ini_sources: Mapping[str, Path | str | bytes | bytearray],
) -> tuple[list[dict[str, Any]], dict[str, str]]:
    normalized_sources: dict[str, Path | str | bytes | bytearray] = {}
    for raw_virtual, raw_source in retail_ini_sources.items():
        virtual = str(raw_virtual).replace("\\", "/").strip("/").casefold()
        if virtual in normalized_sources:
            raise HudAptConvertError("Men/Fords retail INI identities collide")
        normalized_sources[virtual] = raw_source
    if set(normalized_sources) != set(PRODUCTION_MEN_FORDS_RETAIL_INI_SHA256):
        raise HudAptConvertError("Men/Fords retail INI closure changed")
    sources: dict[str, bytes] = {}
    for relative, expected_sha in PRODUCTION_MEN_FORDS_RETAIL_INI_SHA256.items():
        raw_source = normalized_sources[relative]
        if isinstance(raw_source, (bytes, bytearray)):
            payload = bytes(raw_source)
        else:
            path = Path(raw_source)
            if not path.is_file() or path.is_symlink():
                raise HudAptConvertError(f"Men/Fords retail INI is missing: {relative}")
            payload = path.read_bytes()
        if not payload:
            raise HudAptConvertError(f"Men/Fords retail INI is missing: {relative}")
        if _sha(payload) != expected_sha:
            raise HudAptConvertError(f"Men/Fords retail INI changed: {relative}")
        sources[relative] = payload
    command_sets = sources["data/ini/commandset.ini"].decode(
        "utf-8", errors="replace"
    )
    command_buttons = sources["data/ini/commandbutton.ini"].decode(
        "utf-8", errors="replace"
    )
    result: list[dict[str, Any]] = []
    for kind, field, selector, object_name, command_set, object_source in (
        _MEN_FORDS_ROSTER_COMMAND_SETS
    ):
        object_text = sources[object_source].decode("utf-8", errors="replace")
        object_body = _retail_object_body(object_text, object_name)
        object_sets = re.findall(
            r"(?im)^\s*CommandSet\s*=\s*([^\s;]+)", object_body
        )
        if command_set not in object_sets:
            raise HudAptConvertError(
                f"retail roster object {object_name} lost {command_set}"
            )
        set_body = _retail_ini_block(command_sets, "CommandSet", command_set)
        command_rows = [
            (int(match.group(1)), match.group(2))
            for raw_line in set_body.splitlines()
            if (
                match := re.match(
                    r"(\d+)\s*=\s*([^\s]+)", raw_line.split(";", 1)[0].strip()
                )
            )
        ]
        if not command_rows:
            raise HudAptConvertError(f"retail command set is empty: {command_set}")
        eligible: list[str] = []
        multi_select: list[str] = []
        for _slot, command_name in command_rows:
            button_body = _retail_ini_block(
                command_buttons, "CommandButton", command_name
            )
            fields = {
                key.strip().casefold(): value.strip()
                for raw_line in button_body.splitlines()
                if "=" in (line := raw_line.split(";", 1)[0])
                for key, value in [line.split("=", 1)]
            }
            if fields.get("inpalantir", "").casefold() != "yes":
                continue
            eligible.append(command_name)
            if "OK_FOR_MULTI_SELECT" in fields.get("options", "").upper().split():
                multi_select.append(command_name)
        if not eligible:
            raise HudAptConvertError(
                f"retail command set has no InPalantir row: {command_set}"
            )
        result.append(
            {
                "selectionKind": kind,
                "selectorField": field,
                "selectorValue": selector,
                "retailObject": object_name,
                "commandSet": command_set,
                "objectSource": object_source,
                "commandRowCount": len(command_rows),
                "eligibleCommandCount": len(eligible),
                "inPalantirYesCommands": eligible,
                "multiSelectCommands": multi_select,
            }
        )
    battalions = [row for row in result if row["selectionKind"] == "battalion"]
    common_multi = set(battalions[0]["multiSelectCommands"])
    for row in battalions[1:]:
        common_multi.intersection_update(row["multiSelectCommands"])
    expected_common = {
        "Command_ToggleStance",
        "Command_AttackMove",
        "Command_Stop",
    }
    if not expected_common.issubset(common_multi):
        raise HudAptConvertError("Men battalion multi-selection commands changed")
    return result, dict(PRODUCTION_MEN_FORDS_RETAIL_INI_SHA256)


def _side_command_fade_runtime_contract(
    movie: _Movie,
    retail_ini_sources: Mapping[str, Path | str | bytes | bytearray],
) -> dict[str, Any]:
    if (
        _sha(movie.data)
        != "84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef"
        or int(movie.root.get("frameCount", -1)) != 42
        or int(movie.root.get("millisecondsPerFrame", -1)) != 33
        or _timeline_labels(movie.frames)
        != {"_hide": 1, "_fadeIn": 11, "_fadeOut": 31}
    ):
        raise HudAptConvertError("side-command FadeIn source identity changed")
    byte_ranges = (
        (8836, 9009, "e360a3640690bda116ca9437e11bb4ece5f5afbe5f2f46f463facc5540a8939a"),
        (9404, 10086, "47b0231d9b4f7952f3dba37fd2ba6f3f07914edb3a546ba63a0f873e51ef1a9c"),
        (10088, 10090, "0a6361b3a802f55cd5ae06101c88a1e216320fe11cc0cfe1d791eed08a1200fd"),
    )
    for start, end, expected_sha in byte_ranges:
        if _sha(movie.data[start:end]) != expected_sha:
            raise HudAptConvertError("side-command FadeIn byte evidence changed")
    roster, ini_hashes = _men_fords_roster_contract(retail_ini_sources)
    return {
        "schema": "openbfme.retail-hud-men-fords-side-fade",
        "schemaVersion": 0,
        "movie": "InGameSideCommandBar",
        "source": {
            "virtualPath": "InGameSideCommandBar.apt",
            "byteLength": len(movie.data),
            "sha256": _sha(movie.data),
            "retailIniSha256": ini_hashes,
        },
        "typedInput": {
            "type": "MenFordsSelectionCommandContext",
            "selectedIdsField": "selected_ids",
            "selectedStructureIdField": "selected_structure_id",
            "entitiesField": "entities",
            "structuresField": "structures",
            "winnerField": "winner",
            "localTeamField": "local_team",
            "localTeam": 0,
            "inProgressWinner": -1,
            "selectionKindsMutuallyExclusive": True,
            "selectedIdsSortedUnique": True,
        },
        "eligibility": {
            "roster": roster,
            "singleSelectionPredicate": (
                "local team and health > 0 and winner == -1 and "
                "eligibleCommandCount > 0"
            ),
            "multiBattalionCommands": [
                "Command_ToggleStance",
                "Command_AttackMove",
                "Command_Stop",
            ],
            "multiBattalionEligibleCommandCount": 3,
            "noSelectionEligible": False,
            "enemyDeadOrPostMatchEligible": False,
        },
        "timeline": {
            "frameCount": 42,
            "millisecondsPerFrame": 33,
            "labelsZeroBased": {"_hide": 1, "_fadeIn": 11, "_fadeOut": 31},
            "initialFrameZeroBased": 0,
            "fadeInStartOneBased": 12,
            "fadeInEndOneBased": 22,
            "fadeOutStartOneBased": 32,
            "fadeOutEndOneBased": 42,
            "targetRule": "outside [32,42) -> 12; inside [32,42) -> 12 + 42 - currentframe",
            "targetExamples": {"31": 12, "32": 22, "37": 17, "41": 13, "42": 12},
            "fadeInBodyRange": [8836, 9009],
            "fadeInBodySha256": byte_ranges[0][2],
            "completionFrameOneBased": 22,
            "completionProgramRange": [9404, 10086],
            "completionProgramSha256": byte_ranges[1][2],
            "completionCallback": "OnAptInGameSideCommandBarFadeInComplete",
            "completionStateTransition": [2, 3],
            "settledStopFrameOneBased": 31,
            "settledStopProgramRange": [10088, 10090],
            "settledStopProgramSha256": byte_ranges[2][2],
        },
        "nativeStateMachine": {
            "loadedState": 1,
            "fadingInState": 2,
            "settledVisibleState": 3,
            "dispatchOnlyOutsideStates": [2, 3],
            "eligibleCommandCountPredicate": "greater-than-zero",
            "fadeOutBound": False,
        },
        "remainingTraceGates": [
            {
                "id": "side-command-native-row-alias-trace",
                "blocksTypedGodotImplementation": False,
                "blocksExactNativeAliasParityClaim": True,
            }
        ],
        "genericActionScriptVmUsed": False,
        "genericTimelinePlaybackRequired": False,
    }


def _default_display_entry(depth: int) -> dict[str, Any]:
    return {
        "depth": depth,
        "characterId": -1,
        "matrix": [1.0, 0.0, 0.0, 1.0],
        "translation": [0.0, 0.0],
        "tint": [1.0, 1.0, 1.0, 1.0],
        "additive": [0.0, 0.0, 0.0, 0.0],
        "ratio": 0.0,
        "name": "",
        "clipDepth": 0,
        "sourceOffsets": [],
    }


def _apply_place_to_display(
    display: dict[int, dict[str, Any]], row: Mapping[str, Any]
) -> str | None:
    """Apply the declarative PlaceObject2 fields without executing callbacks.

    APT uses the SWF PlaceObject2 flag layout. A move preserves fields whose
    presence bits are absent; a placement without Move starts from authored
    identity defaults. The returned error is evidence that a display list
    cannot be reconstructed exactly, never an invitation to guess.
    """

    flags = int(row["flags"])
    depth = int(row["depth"])
    move = bool(flags & 0x01)
    has_character = bool(flags & 0x02)
    previous = display.get(depth)
    if move:
        if previous is None:
            return "move-without-existing-depth"
        entry = dict(previous)
        entry["sourceOffsets"] = list(previous["sourceOffsets"])
    else:
        if not has_character:
            return "placement-without-character"
        entry = _default_display_entry(depth)
    if has_character:
        entry["characterId"] = int(row["characterId"])
    if int(entry["characterId"]) < 0:
        return "display-entry-has-no-character"
    if flags & 0x04:
        entry["matrix"] = [float(value) for value in row["matrix"]]
        entry["translation"] = [float(value) for value in row["translation"]]
    if flags & 0x08:
        entry["tint"] = [float(value) for value in row["tint"]]
        entry["additive"] = [float(value) for value in row["additive"]]
    if flags & 0x10:
        entry["ratio"] = float(row["ratio"])
    if flags & 0x20:
        entry["name"] = str(row.get("name", ""))
    if flags & 0x40:
        entry["clipDepth"] = int(row["clipDepth"])
    if flags & 0x80:
        entry["clipActionsOffset"] = int(row["clipActionsOffset"])
    entry["sourceOffsets"].append(int(row["sourceOffset"]))
    display[depth] = entry
    return None


def _reconstruct_timeline(
    movie: str, character_id: int, frames: list[list[dict[str, Any]]]
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Return exact cumulative display lists and any reconstruction failures."""

    display: dict[int, dict[str, Any]] = {}
    background: list[float] | None = None
    output_frames: list[dict[str, Any]] = []
    failures: list[dict[str, Any]] = []
    for frame_index, rows in enumerate(frames):
        labels: list[dict[str, Any]] = []
        actions: list[dict[str, Any]] = []
        operations: list[dict[str, Any]] = []
        for row in rows:
            kind = str(row["kind"])
            source_offset = int(row["sourceOffset"])
            if kind == "place-object":
                operations.append(
                    {
                        "kind": kind,
                        "sourceOffset": source_offset,
                        "flags": int(row["flags"]),
                        "depth": int(row["depth"]),
                    }
                )
                reason = _apply_place_to_display(display, row)
                if reason is not None:
                    failures.append(
                        {
                            "frameIndex": frame_index,
                            "sourceOffset": source_offset,
                            "reason": reason,
                        }
                    )
            elif kind == "remove-object":
                depth = int(row["depth"])
                operations.append(
                    {
                        "kind": kind,
                        "sourceOffset": source_offset,
                        "depth": depth,
                    }
                )
                display.pop(depth, None)
            elif kind == "background-color":
                background = [float(value) for value in row["color"]]
                operations.append(
                    {
                        "kind": kind,
                        "sourceOffset": source_offset,
                        "color": list(background),
                    }
                )
            elif kind == "frame-label":
                labels.append(
                    {
                        "name": str(row["name"]),
                        "frameId": int(row["frameId"]),
                        "flags": int(row["flags"]),
                        "sourceOffset": source_offset,
                    }
                )
            elif kind in ("action-script", "init-action-script"):
                actions.append(
                    {
                        "kind": kind,
                        "sourceOffset": source_offset,
                        "instructionsOffset": int(row["instructionsOffset"]),
                    }
                )
            else:
                failures.append(
                    {
                        "frameIndex": frame_index,
                        "sourceOffset": source_offset,
                        "reason": f"unsupported-frame-item:{kind}",
                    }
                )
        frame: dict[str, Any] = {
            "frameIndex": frame_index,
            "displayList": [dict(display[depth]) for depth in sorted(display)],
            "operations": operations,
        }
        if background is not None:
            frame["backgroundColor"] = list(background)
        if labels:
            frame["labels"] = labels
        if actions:
            frame["actionScripts"] = actions
        output_frames.append(frame)
    contract = {
        "timelineId": f"{movie.casefold()}:{character_id}",
        "movie": movie,
        "characterId": character_id,
        "frameCount": len(frames),
        "displayListComplete": not failures,
        "frames": output_frames,
    }
    return contract, failures


def _geometry_primitives(
    movie: _Movie,
    geometry_id: int,
    transform: _Transform,
    draw_count_start: int = 0,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Emit the triangles of one authored geometry under one transform.

    Returns ``(draw_rows, receipts)``.  Draw rows carry the sealed
    solid/textured triangle schema minus ``displayOrder``/``movie``/``path``,
    which every caller assigns for its own contract section.  Receipts carry
    the exact blocker code plus evidence, minus ``movie``/``path``.
    """

    groups = movie.geometry.get(geometry_id)
    if groups is None:
        return [], [{"code": "geometry-missing", "geometryId": geometry_id}]
    rows: list[dict[str, Any]] = []
    receipts: list[dict[str, Any]] = []
    for group_index, group in enumerate(groups):
        style = str(group["style"])
        values = [float(value) for value in group["values"]]
        primitives = group["primitives"]
        if style == "l":
            receipts.append(
                {
                    "code": "line-geometry-not-converted",
                    "geometryId": geometry_id,
                    "styleIndex": group_index,
                }
            )
            continue
        color = tuple(value / 255.0 for value in values[:4])
        if style == "s":
            for primitive in primitives:
                if draw_count_start + len(rows) >= MAX_DRAWS:
                    raise HudAptConvertError("HUD draw count exceeds bounds")
                rows.append(
                    {
                        "kind": "solid-triangle",
                        "geometryId": geometry_id,
                        "points": [
                            transform.point((float(point[0]), float(point[1])))
                            for point in primitive
                        ],
                        "color": transform.color(color),
                    }
                )
            continue
        image_id = int(values[4])
        texture_id = movie.image_map.get(image_id)
        atlas = movie.atlases.get(texture_id) if texture_id is not None else None
        if atlas is None:
            receipts.append(
                {
                    "code": "texture-assignment-unresolved",
                    "geometryId": geometry_id,
                    "imageId": image_id,
                    "textureId": texture_id,
                }
            )
            continue
        width = int(atlas["width"])
        height = int(atlas["height"])
        uv_matrix = values[5:9]
        uv_translation = values[9:11]
        for primitive in primitives:
            if draw_count_start + len(rows) >= MAX_DRAWS:
                raise HudAptConvertError("HUD draw count exceeds bounds")
            uvs = []
            for point in primitive:
                x, y = float(point[0]), float(point[1])
                u = x * uv_matrix[0] + y * uv_matrix[2] + uv_translation[0]
                v = x * uv_matrix[1] + y * uv_matrix[3] + uv_translation[1]
                uvs.append([u / width, v / height])
            rows.append(
                {
                    "kind": "textured-triangle",
                    "geometryId": geometry_id,
                    "imageId": image_id,
                    "points": [
                        transform.point((float(point[0]), float(point[1])))
                        for point in primitive
                    ],
                    "uvs": uvs,
                    "color": transform.color((1.0, 1.0, 1.0, 1.0)),
                    "atlas": str(atlas["cookedPng"]),
                    "atlasSha256": str(atlas["sha256"]),
                }
            )
    return rows, receipts


# ---------------------------------------------------------------------------
# Stage pieces: named authored HUD assemblies with their retail art.
#
# The static scene subset above flattens only the bounded InitialSetup frame,
# which leaves every named dock piece whose sprite is authored hidden at frame
# 0 (radar backing, spectral highlights, socket rings, resource cartouche,
# rope/ornament strips) without pixels a runtime can composite.  This pass
# emits, for every NAMED PlaceObject row on a root layer's frame 0, every
# DISTINCT authored content state of that piece (state identity = the
# depth/characterId/name display-list signature, so tween motion does not
# multiply states) flattened to stage-space triangles against the shipped
# atlases.  Placement, scale and depth all come from the movie's own records.
# Pieces or children without authored image art stay NAMED receipts, never
# invented pixels.
# ---------------------------------------------------------------------------

_STAGE_PIECE_STATE_SIGNATURE = "depth-characterId-name-display-list"
_STAGE_PIECE_NESTED_POLICY = "nested-first-populated-frame"


def _place_row_transform(row: Mapping[str, Any]) -> _Transform:
    flags = int(row["flags"])
    matrix = (
        tuple(float(value) for value in row["matrix"])
        if flags & 0x04
        else (1.0, 0.0, 0.0, 1.0)
    )
    translation = (
        tuple(float(value) for value in row["translation"])
        if flags & 0x04
        else (0.0, 0.0)
    )
    tint = (
        tuple(float(value) for value in row["tint"])
        if flags & 0x08
        else (1.0, 1.0, 1.0, 1.0)
    )
    additive = (
        tuple(float(value) for value in row["additive"])
        if flags & 0x08
        else (0.0, 0.0, 0.0, 0.0)
    )
    return _Transform(matrix, translation, tint, additive)  # type: ignore[arg-type]


def _display_row_transform(row: Mapping[str, Any]) -> _Transform:
    return _Transform(
        tuple(float(value) for value in row["matrix"]),  # type: ignore[arg-type]
        tuple(float(value) for value in row["translation"]),  # type: ignore[arg-type]
        tuple(float(value) for value in row["tint"]),  # type: ignore[arg-type]
        tuple(float(value) for value in row["additive"]),  # type: ignore[arg-type]
    )


def _compose_authored(parent: _Transform, child: _Transform) -> _Transform:
    """Exact affine composition (child placed under parent).

    ``_Transform.combine`` is the sealed static-scene composition; every one
    of its production uses runs under identity parents so its unrotated
    translation never mattered.  Stage pieces place children under SCALED
    parents (the dish globe anchors scale 0.64/0.69, the region ring 1.31), so
    this pass composes with the exact SWF semantics: the child translation is
    transformed by the parent matrix.
    """

    a, b, c, d = parent.matrix
    e, f, g, h = child.matrix
    matrix = (
        e * a + f * c,
        e * b + f * d,
        g * a + h * c,
        g * b + h * d,
    )
    translation = tuple(parent.point(child.translation))
    tint = tuple(parent.tint[i] * child.tint[i] for i in range(4))
    additive = tuple(
        parent.additive[i] * child.tint[i] + child.additive[i] for i in range(4)
    )
    return _Transform(matrix, translation, tint, additive)  # type: ignore[arg-type]


def _resolve_stage_character(
    movies: Mapping[str, _Movie], movie: _Movie, character_id: int
) -> tuple[_Movie, int] | dict[str, Any]:
    """Resolve one placed character or return the exact refusal receipt."""

    if not 0 <= character_id < len(movie.characters):
        return {
            "code": "character-id-out-of-range",
            "movie": movie.name,
            "characterId": character_id,
        }
    character = movie.characters[character_id]
    if character["kind"] != "null":
        return movie, character_id
    edge = movie.imports.get(character_id)
    if edge is None:
        return {
            "code": "unresolved-null-character",
            "movie": movie.name,
            "characterId": character_id,
        }
    target = movies.get(edge[0].casefold())
    if target is None:
        return {
            "code": "unresolved-import-movie",
            "movie": movie.name,
            "characterId": character_id,
            "targetMovie": edge[0],
            "symbol": edge[1],
        }
    exported = sorted(set(target.exports.get(edge[1].casefold(), [])))
    if len(exported) != 1:
        return {
            "code": (
                "unresolved-import-symbol"
                if not exported
                else "ambiguous-import-symbol"
            ),
            "movie": movie.name,
            "characterId": character_id,
            "targetMovie": edge[0],
            "symbol": edge[1],
        }
    return target, exported[0]


class _StagePieceCollector:
    """Flatten named root placements into authored per-state stage draws."""

    def __init__(self, movies: Mapping[str, _Movie]) -> None:
        self.movies = movies
        self.timeline_cache: dict[tuple[str, int], dict[str, Any]] = {}
        self.draw_total = 0

    def _timeline(
        self, movie: _Movie, character_id: int, frames: list[list[dict[str, Any]]]
    ) -> dict[str, Any]:
        key = (movie.name.casefold(), character_id)
        cached = self.timeline_cache.get(key)
        if cached is None:
            timeline, failures = _reconstruct_timeline(
                movie.name, character_id, frames
            )
            cached = {"timeline": timeline, "failures": failures}
            self.timeline_cache[key] = cached
        return cached

    @staticmethod
    def _new_state(frame_index: int, labels: list[str]) -> dict[str, Any]:
        return {
            "firstFrameIndex": frame_index,
            "labels": labels,
            "draws": [],
            "nestedTimelineSelections": [],
            "receipts": [],
        }

    def _collect(
        self,
        movie: _Movie,
        character_id: int,
        transform: _Transform,
        stack: tuple[tuple[str, int], ...],
        path: str,
        state: dict[str, Any],
    ) -> None:
        resolved = _resolve_stage_character(self.movies, movie, character_id)
        if isinstance(resolved, dict):
            state["receipts"].append({**resolved, "path": path})
            return
        target, resolved_id = resolved
        key = (target.name, resolved_id)
        if key in stack or len(stack) >= MAX_RECURSION:
            state["receipts"].append(
                {
                    "code": "recursive-character",
                    "movie": target.name,
                    "characterId": resolved_id,
                    "path": path,
                }
            )
            return
        character = target.characters[resolved_id]
        kind = str(character["kind"])
        if kind == "shape":
            self._collect_shape(target, character, transform, path, state)
            return
        if kind in ("sprite", "movie"):
            self._collect_sprite(
                target, resolved_id, character, kind, transform, stack + (key,),
                path, state,
            )
            return
        if kind == "button":
            self._collect_button(
                target, resolved_id, character, transform, stack + (key,),
                path, state,
            )
            return
        state["receipts"].append(
            {
                "code": "character-kind-not-piece-art",
                "movie": target.name,
                "characterId": resolved_id,
                "characterKind": kind,
                "path": path,
            }
        )

    def _collect_button(
        self,
        target: _Movie,
        resolved_id: int,
        character: Mapping[str, Any],
        transform: _Transform,
        stack: tuple[tuple[str, int], ...],
        path: str,
        state: dict[str, Any],
    ) -> None:
        """Flatten a button character's UP-state records as static piece art.

        Retail parks static ornament art inside button characters —
        ``PalantirBack`` (the rope/frame behind the radar and dish) is one —
        and the idle HUD shows the authored up state.  Records compose exactly
        like display rows: record matrix/translation under the parent
        transform, record color as the tint, drawn in authored depth order.
        Hit-only records carry the invisible hit polygon, never art, and fall
        out via the state mask.  A button with no up-state record is a named
        receipt, not silence.
        """
        up_records = [
            record
            for record in character["records"]
            if "up" in record["states"]
        ]
        if not up_records:
            state["receipts"].append(
                {
                    "code": "button-has-no-up-state-art",
                    "movie": target.name,
                    "characterId": resolved_id,
                    "path": path,
                }
            )
            return
        for record in sorted(up_records, key=lambda item: int(item["depth"])):
            child = _compose_authored(
                transform,
                _Transform(
                    tuple(float(value) for value in record["matrix"]),
                    tuple(float(value) for value in record["translation"]),
                    tuple(float(value) for value in record["color"]),
                    (0.0, 0.0, 0.0, 0.0),
                ),
            )
            child_path = f"{path}/up:{int(record['recordIndex'])}"
            self._collect(
                target, int(record["characterId"]), child, stack, child_path,
                state,
            )

    def _collect_shape(
        self,
        target: _Movie,
        character: Mapping[str, Any],
        transform: _Transform,
        path: str,
        state: dict[str, Any],
    ) -> None:
        rows, receipts = _geometry_primitives(
            target, int(character["geometryId"]), transform, self.draw_total
        )
        for receipt in receipts:
            state["receipts"].append(
                {**receipt, "movie": target.name, "path": path}
            )
        for row in rows:
            row["movie"] = target.name
            row["path"] = path
            row["pieceOrder"] = len(state["draws"])
            state["draws"].append(row)
            self.draw_total += 1

    def _collect_sprite(
        self,
        target: _Movie,
        resolved_id: int,
        character: Mapping[str, Any],
        kind: str,
        transform: _Transform,
        stack: tuple[tuple[str, int], ...],
        path: str,
        state: dict[str, Any],
    ) -> None:
        frames = target.frames if kind == "movie" else character.get("frames", [])
        if not frames:
            state["receipts"].append(
                {
                    "code": "sprite-has-no-populated-frame",
                    "movie": target.name,
                    "characterId": resolved_id,
                    "path": path,
                }
            )
            return
        entry = self._timeline(target, resolved_id, frames)
        frame_rows = entry["timeline"]["frames"]
        chosen = next(
            (frame for frame in frame_rows if frame["displayList"]), None
        )
        if chosen is None:
            state["receipts"].append(
                {
                    "code": "sprite-has-no-populated-frame",
                    "movie": target.name,
                    "characterId": resolved_id,
                    "path": path,
                }
            )
            return
        if len(frame_rows) > 1:
            state["nestedTimelineSelections"].append(
                {
                    "movie": target.name,
                    "characterId": resolved_id,
                    "chosenFrameIndex": int(chosen["frameIndex"]),
                    "frameCount": len(frame_rows),
                    "policy": _STAGE_PIECE_NESTED_POLICY,
                }
            )
        self._flatten_display(
            target, chosen["displayList"], transform, stack, path, state
        )

    def _flatten_display(
        self,
        movie: _Movie,
        display_list: list[dict[str, Any]],
        transform: _Transform,
        stack: tuple[tuple[str, int], ...],
        path: str,
        state: dict[str, Any],
    ) -> None:
        for row in sorted(display_list, key=lambda item: int(item["depth"])):
            child = _compose_authored(transform, _display_row_transform(row))
            name = str(row.get("name", "") or "")
            child_path = f"{path}/{int(row['depth'])}" + (
                f":{name}" if name else ""
            )
            self._collect(
                movie, int(row["characterId"]), child, stack, child_path, state
            )

    def piece(
        self, movie: _Movie, layer_index: int, row: Mapping[str, Any]
    ) -> dict[str, Any]:
        piece_name = str(row["name"])
        base_path = f"piece:{layer_index}:{movie.name}/{piece_name}"
        placement = _place_row_transform(row)
        piece: dict[str, Any] = {
            "movie": movie.name,
            "layer": layer_index,
            "name": piece_name,
            "rootDepth": int(row["depth"]),
            "characterId": int(row["characterId"]),
            "placement": {
                "matrix": list(placement.matrix),
                "translation": list(placement.translation),
                "tint": list(placement.tint),
                "additive": list(placement.additive),
            },
            "stateSignature": _STAGE_PIECE_STATE_SIGNATURE,
            "nestedPolicy": _STAGE_PIECE_NESTED_POLICY,
            "labels": {},
            "states": [],
            "receipts": [],
        }
        resolved = _resolve_stage_character(
            self.movies, movie, int(row["characterId"])
        )
        if isinstance(resolved, dict):
            piece["receipts"].append({**resolved, "path": base_path})
            piece["artless"] = True
            return piece
        target, resolved_id = resolved
        piece["resolvedMovie"] = target.name
        piece["resolvedCharacterId"] = resolved_id
        character = target.characters[resolved_id]
        kind = str(character["kind"])
        frames: list[list[dict[str, Any]]] | None
        if kind == "sprite":
            frames = character.get("frames", [])
        elif kind == "movie":
            frames = target.frames
        else:
            frames = None
        if not frames:
            state = self._new_state(0, [])
            self._collect(
                movie, int(row["characterId"]), placement, (), base_path, state
            )
            piece["states"].append(state)
        else:
            self._piece_sprite_states(
                piece, target, resolved_id, frames, placement, base_path
            )
        piece["artless"] = not any(
            state["draws"] for state in piece["states"]
        )
        return piece

    def _piece_sprite_states(
        self,
        piece: dict[str, Any],
        target: _Movie,
        resolved_id: int,
        frames: list[list[dict[str, Any]]],
        placement: _Transform,
        base_path: str,
    ) -> None:
        entry = self._timeline(target, resolved_id, frames)
        for failure in entry["failures"]:
            piece["receipts"].append(
                {"code": "timeline-reconstruction-failure", **failure}
            )
        labels: dict[str, int] = {}
        for frame in entry["timeline"]["frames"]:
            for label in frame.get("labels", []):
                labels[str(label["name"])] = int(frame["frameIndex"])
        piece["labels"] = labels
        seen_signatures: set[tuple[tuple[int, int, str], ...]] = set()
        for frame in entry["timeline"]["frames"]:
            signature = tuple(
                sorted(
                    (
                        int(item["depth"]),
                        int(item["characterId"]),
                        str(item.get("name", "") or ""),
                    )
                    for item in frame["displayList"]
                )
            )
            if signature in seen_signatures:
                continue
            seen_signatures.add(signature)
            frame_index = int(frame["frameIndex"])
            state = self._new_state(
                frame_index,
                sorted(
                    name for name, index in labels.items() if index == frame_index
                ),
            )
            self._flatten_display(
                target,
                frame["displayList"],
                placement,
                ((target.name, resolved_id),),
                base_path,
                state,
            )
            piece["states"].append(state)


def _stage_pieces_contract(
    movies: Mapping[str, _Movie],
) -> list[dict[str, Any]]:
    """Emit every named root-layer piece with its authored per-state art."""

    collector = _StagePieceCollector(movies)
    pieces: list[dict[str, Any]] = []
    for layer_index, name in enumerate(MOVIE_ORDER):
        movie = movies.get(name.casefold())
        if movie is None or not movie.frames:
            continue
        for row in movie.frames[0]:
            if row.get("kind") != "place-object" or not row.get("name"):
                continue
            pieces.append(collector.piece(movie, layer_index, row))
    return pieces


def _stage_piece_usage_names(
    stage_pieces: Iterable[Mapping[str, Any]],
) -> dict[tuple[str, int], set[str]]:
    """Image names read from the movie's own placement hierarchy.

    Every flattened stage draw carries the image it samples and the path of
    NAMED instances above it (``PalantirBack``, ``glass3``, ``toggleFlash`` -
    all authored strings). An image a named part draws is named after that
    part: root piece name, joined with the deepest named child when one
    exists. Purely numeric path segments (depths, button record indices) are
    not names and are skipped.
    """

    usage: dict[tuple[str, int], set[str]] = {}
    for piece in stage_pieces:
        for state in piece.get("states", []):
            for row in state.get("draws", []):
                image_id = row.get("imageId")
                if image_id is None:
                    continue
                segments = [str(piece.get("name", ""))]
                for token in str(row.get("path", "")).split("/")[2:]:
                    if ":" not in token:
                        continue
                    child = token.split(":", 1)[1]
                    if child and not child.isdigit():
                        segments.append(child)
                segments = [segment for segment in segments if segment]
                if not segments:
                    continue
                name = (
                    segments[0]
                    if len(segments) == 1 or segments[-1] == segments[0]
                    else f"{segments[0]}-{segments[-1]}"
                )
                usage.setdefault(
                    (str(row.get("movie", "")).casefold(), int(image_id)), set()
                ).add(name)
    return usage


def _placement_usage_names(
    movies: Mapping[str, _Movie],
) -> dict[tuple[str, int], set[str]]:
    """Image names read from NAMED PlaceObject rows anywhere in the family.

    The stage-piece pass only walks the root movies' layer-0 placements; the
    hero-select and planning-mode movies keep their names on inner display
    rows instead. Every named placement names the images of the character it
    places — a shape directly, or a sprite's placed shapes one level deep.
    All strings are the movie's own; nothing is invented.
    """

    usage: dict[tuple[str, int], set[str]] = {}

    def _shape_images(movie: _Movie, character: Mapping[str, Any]) -> list[int]:
        images: list[int] = []
        for group in movie.geometry.get(int(character.get("geometryId", -1)), []):
            if str(group["style"]) == "tc":
                images.append(int(float(group["values"][4])))
        return images

    def _name_images(
        movie: _Movie,
        character_id: int,
        symbol: str,
        visited: set[int] | None = None,
    ) -> None:
        # Recursive: a named placement names EVERY image the placed subtree
        # draws (all frames, all button states), so deep unnamed sprite
        # nesting cannot orphan its art. The visited set breaks cycles.
        if visited is None:
            visited = set()
        if character_id in visited or not 0 <= character_id < len(movie.characters):
            return
        visited.add(character_id)
        character = movie.characters[character_id]
        kind = str(character.get("kind", ""))
        if kind == "shape":
            for image_id in _shape_images(movie, character):
                usage.setdefault(
                    (movie.name.casefold(), image_id), set()
                ).add(symbol)
            return
        if kind == "button":
            for record in character.get("records", []):
                _name_images(
                    movie, int(record.get("characterId", -1)), symbol, visited
                )
            return
        if kind == "sprite":
            for frame in character.get("frames", []):
                for row in frame:
                    if row.get("kind") != "place-object":
                        continue
                    child_name = str(row.get("name", "") or "")
                    child_symbol = symbol
                    if child_name and not child_name.isdigit():
                        # A named child refines the name: Hero1 -> Hero1-portrait.
                        child_symbol = f"{symbol}-{child_name}"
                    _name_images(
                        movie,
                        int(row.get("characterId", -1)),
                        child_symbol,
                        visited,
                    )

    for movie in movies.values():
        frame_sources: list[list[list[dict[str, Any]]]] = [movie.frames]
        for character in movie.characters:
            if str(character.get("kind", "")) == "sprite":
                frame_sources.append(character.get("frames", []))
        for frames in frame_sources:
            for frame in frames:
                for row in frame:
                    if row.get("kind") != "place-object":
                        continue
                    name = str(row.get("name", "") or "")
                    if not name or name.isdigit():
                        continue
                    _name_images(movie, int(row.get("characterId", -1)), name)
    return usage


def _atlas_piece_manifest(
    movies: Mapping[str, _Movie],
    usage_names: Mapping[tuple[str, int], set[str]] | None = None,
) -> list[dict[str, Any]]:
    """One row per authored IMAGE of the palantir movie family.

    The shipped atlases are packed sprite sheets; nothing about their layout
    is guessed here.  Every textured geometry group maps its shape into ATLAS
    PIXELS through the authored UV affine, so the union of those pixel bounds
    per image id is that image's authored footprint on its sheet — the exact
    split the owner asked for ("each icon or asset gets its own thing"),
    read from the movie's own tables rather than classified visually.

    Names come from the movies' export symbols: an exported shape names its
    geometry's images; an exported sprite names the images of the shapes its
    first populated frame places (one level deep — deeper nesting keeps the
    movie/imageId identity, which is still exact).  The cropped PNG path is
    rect-derived so the contract stays deterministic before pixels exist;
    ``convert_hud_apt_bundle`` writes the crop and stamps its sha256.
    """

    rows: list[dict[str, Any]] = []
    for movie_key in sorted(movies):
        movie = movies[movie_key]
        # authored pixel bounds per image id, plus the geometry ids that use it
        bounds: dict[int, list[float]] = {}
        atlas_by_image: dict[int, Mapping[str, Any]] = {}
        geometry_images: dict[int, set[int]] = {}
        for geometry_id, groups in movie.geometry.items():
            for group in groups:
                if str(group["style"]) != "tc":
                    continue
                values = [float(value) for value in group["values"]]
                image_id = int(values[4])
                texture_id = movie.image_map.get(image_id)
                atlas = (
                    movie.atlases.get(texture_id)
                    if texture_id is not None
                    else None
                )
                if atlas is None:
                    continue  # already a texture-assignment-unresolved receipt
                uv_matrix = values[5:9]
                uv_translation = values[9:11]
                box = bounds.setdefault(
                    image_id, [float("inf"), float("inf"), float("-inf"), float("-inf")]
                )
                atlas_by_image[image_id] = atlas
                geometry_images.setdefault(int(geometry_id), set()).add(image_id)
                for primitive in group["primitives"]:
                    for point in primitive:
                        x, y = float(point[0]), float(point[1])
                        u = x * uv_matrix[0] + y * uv_matrix[2] + uv_translation[0]
                        v = x * uv_matrix[1] + y * uv_matrix[3] + uv_translation[1]
                        box[0] = min(box[0], u)
                        box[1] = min(box[1], v)
                        box[2] = max(box[2], u)
                        box[3] = max(box[3], v)
        # export symbols -> image ids, shallowly through shapes and sprites
        names: dict[int, set[str]] = {}

        def _name_shape(character: Mapping[str, Any], symbol: str) -> None:
            for named_image in geometry_images.get(
                int(character.get("geometryId", -1)), ()
            ):
                names.setdefault(named_image, set()).add(symbol)

        for symbol, character_ids in movie.exports.items():
            for character_id in character_ids:
                if not 0 <= int(character_id) < len(movie.characters):
                    continue
                character = movie.characters[int(character_id)]
                kind = str(character.get("kind", ""))
                if kind == "shape":
                    _name_shape(character, str(symbol))
                elif kind == "sprite":
                    for frame in character.get("frames", []):
                        placed = [
                            row
                            for row in frame
                            if row.get("kind") == "place-object"
                        ]
                        if not placed:
                            continue
                        for row in placed:
                            child_id = int(row.get("characterId", -1))
                            if 0 <= child_id < len(movie.characters):
                                child = movie.characters[child_id]
                                if str(child.get("kind", "")) == "shape":
                                    _name_shape(child, str(symbol))
                        break
        slug = re.sub(r"[^a-z0-9]+", "", movie.name.casefold())
        for image_id in sorted(bounds):
            box = bounds[image_id]
            atlas = atlas_by_image[image_id]
            width = int(atlas["width"])
            height = int(atlas["height"])
            x0 = max(0, math.floor(box[0]))
            y0 = max(0, math.floor(box[1]))
            x1 = min(width, math.ceil(box[2]))
            y1 = min(height, math.ceil(box[3]))
            if x1 <= x0 or y1 <= y0:
                continue
            # One texel of margin on every side. The authored UV footprints
            # sample half a texel inside the art (standard bilinear inset),
            # so a bare floor/ceil crop cut the soft outer edge of pieces
            # (owner 2026-08-26: the ability-button frame and the orb strip
            # lost their edges). The sheets pack with a one-texel gutter, so
            # one texel of margin cannot pull in a neighbour sprite.
            x0 = max(0, x0 - 1)
            y0 = max(0, y0 - 1)
            x1 = min(width, x1 + 1)
            y1 = min(height, y1 + 1)
            export_names = sorted(names.get(image_id, ()))
            hierarchy_names = sorted(
                (usage_names or {}).get((movie_key, image_id), ())
            )
            # The file name carries the best authored name: an export symbol
            # first (retail's own asset name), else the shortest placement-
            # hierarchy name. Only a truly anonymous image keeps the rect id.
            derived = ""
            if export_names:
                derived = min(export_names, key=lambda value: (len(value), value))
            elif hierarchy_names:
                # Most specific placement name first (more named segments),
                # then shortest, then alphabetical - all deterministic.
                derived = min(
                    hierarchy_names,
                    key=lambda value: (-value.count("-"), len(value), value),
                )
            derived_slug = re.sub(r"[^a-z0-9]+", "-", derived.casefold()).strip("-")
            cropped = (
                f"assets/ui/palantir/pieces/apt-{slug}-{derived_slug}-i{image_id}.png"
                if derived_slug
                else (
                    "assets/ui/palantir/pieces/"
                    f"apt-{slug}-i{image_id}-{x0}x{y0}-{x1 - x0}x{y1 - y0}.png"
                )
            )
            rows.append(
                {
                    "movie": movie.name,
                    "imageId": image_id,
                    "atlas": str(atlas["cookedPng"]),
                    "atlasSha256": str(atlas["sha256"]),
                    "rect": [x0, y0, x1 - x0, y1 - y0],
                    "names": sorted({*export_names, *hierarchy_names}),
                    "derivedName": derived,
                    "croppedPng": cropped,
                }
            )
    return rows


class _Flattener:
    def __init__(
        self,
        movies: dict[str, _Movie],
        selected_frames: Mapping[tuple[str, int], int],
        external_fonts: Iterable[Mapping[str, Any]] = (),
    ) -> None:
        self.movies = movies
        self.selected_frames = dict(selected_frames)
        self.external_fonts = {str(row["fontId"]): dict(row) for row in external_fonts}
        self.draws: list[dict[str, Any]] = []
        self.blockers: list[dict[str, Any]] = []
        self.timelines: dict[tuple[str, int], dict[str, Any]] = {}
        self.timeline_instances: list[dict[str, Any]] = []
        self.action_programs: dict[str, dict[str, Any]] = {}
        self.clip_action_programs: dict[str, dict[str, Any]] = {}
        self.clip_actions: list[dict[str, Any]] = []
        self.font_definitions: dict[tuple[str, int], dict[str, Any]] = {}
        self.text_definitions: dict[tuple[str, int], dict[str, Any]] = {}
        self.text_instances: list[dict[str, Any]] = []
        self.button_definitions: dict[tuple[str, int], dict[str, Any]] = {}
        self.button_instances: list[dict[str, Any]] = []
        self.display_order = 0

    def _next_display_order(self) -> int:
        value = self.display_order
        self.display_order += 1
        return value

    def block(self, code: str, movie: str, **evidence: object) -> None:
        row = {"code": code, "movie": movie, **evidence}
        if row not in self.blockers:
            self.blockers.append(row)

    def _register_action_program(self, movie: _Movie, row: Mapping[str, Any]) -> str:
        script_id = f"{movie.name.casefold()}:{int(row['sourceOffset'])}"
        existing = self.action_programs.get(script_id)
        if existing is None:
            program = _decode_action_program(movie, row)
            self.action_programs[script_id] = program
            if not bool(program["supported"]):
                self.block(
                    "action-script-unsupported-opcodes",
                    movie.name,
                    scriptId=script_id,
                    sourceOffset=int(row["sourceOffset"]),
                    instructionOffset=int(row["instructionsOffset"]),
                    sha256=str(program["sha256"]),
                    unboundCapabilities=_action_program_capabilities(program),
                    unsupportedInstructions=program["unsupportedInstructions"],
                )
        elif int(existing["instructionOffset"]) != int(
            row["instructionsOffset"]
        ) or str(existing["actionKind"]) != str(row["kind"]):
            raise HudAptConvertError(f"ActionScript identity changed: {script_id}")
        return script_id

    def _register_clip_action_program(
        self, movie: _Movie, event: Mapping[str, Any]
    ) -> str:
        event_offset = int(event["eventOffset"])
        program_id = f"{movie.name.casefold()}:clip-event:{event_offset}"
        existing = self.clip_action_programs.get(program_id)
        if existing is None:
            program = _decode_action_program(
                movie,
                {
                    "kind": "clip-action-event",
                    "sourceOffset": event_offset,
                    "instructionsOffset": int(event["instructionsOffset"]),
                },
            )
            program["programId"] = program_id
            program["instructionEndOffset"] = int(program["instructionOffset"]) + int(
                program["byteLength"]
            )
            program.pop("scriptId")
            self.clip_action_programs[program_id] = program
        elif int(existing["instructionOffset"]) != int(event["instructionsOffset"]):
            raise HudAptConvertError(
                f"clip-action program identity changed: {program_id}"
            )
        return program_id

    def _register_clip_actions(
        self,
        movie: _Movie,
        row: Mapping[str, Any],
        path: str,
        depth: int,
    ) -> None:
        value = row.get("clipActions", {})
        if not isinstance(value, Mapping):
            raise HudAptConvertError("clip-action record is missing")
        target_path = f"{path}/{depth}"
        clip_action_id = (
            f"{movie.name.casefold()}:{int(row['sourceOffset'])}:{target_path}"
        )
        resolved = self._resolve(movie, int(row["characterId"]))
        target_movie = ""
        target_character_id = -1
        target_kind = ""
        target_clip_id = ""
        target_timeline_id = ""
        if resolved is not None:
            target, target_character_id = resolved
            target_movie = target.name
            character = target.characters[target_character_id]
            target_kind = str(character["kind"])
            frames = (
                target.frames if target_kind == "movie" else character.get("frames", [])
            )
            if target_kind in ("movie", "sprite"):
                target_clip_id = f"{target.name.casefold()}:{target_character_id}"
            if target_kind in ("movie", "sprite") and len(frames) > 1:
                target_timeline_id = target_clip_id
        binding_events: list[dict[str, Any]] = []
        blocked_events: list[dict[str, Any]] = []
        for event in value.get("events", []):
            if not isinstance(event, Mapping):
                raise HudAptConvertError("clip-action event is invalid")
            program_id = self._register_clip_action_program(movie, event)
            program = self.clip_action_programs[program_id]
            event_names = [str(name) for name in event["eventNames"]]
            capabilities = _action_program_capabilities(program)
            if any(name in _CLIP_INPUT_EVENTS for name in event_names):
                capabilities.append("input-dispatch")
            if "unload" in event_names:
                capabilities.append("lifecycle-dispatch")
            elif event_names != ["initialize"]:
                capabilities.append("event-dispatch")
            typed_initialize = _is_typed_initialize_program(program)
            if (
                event_names == ["initialize"]
                and not target_timeline_id
                and not typed_initialize
            ):
                capabilities.append("unresolved-target-semantics")
            if int(event["nextEventOffset"]) != 0:
                capabilities.append("unsupported-event-order")
            capabilities = list(dict.fromkeys(capabilities))
            executable = (
                event_names == ["initialize"]
                and int(event["keyCode"]) == 0
                and int(event["nextEventOffset"]) == 0
                and (bool(target_timeline_id) or typed_initialize)
                and bool(program["supported"])
                and not capabilities
            )
            event_row = {
                **dict(event),
                "programId": program_id,
                "dispatchOrder": (
                    "after-target-create-and-name-before-display-list-insert"
                    if event_names == ["initialize"]
                    else "runtime-event-dispatch-not-bound"
                ),
                "executable": executable,
            }
            binding_events.append(event_row)
            if not executable:
                blocked_events.append(
                    {
                        "eventIndex": int(event["eventIndex"]),
                        "eventOffset": int(event["eventOffset"]),
                        "eventEndOffset": int(event["eventEndOffset"]),
                        "eventMask": int(event["eventMask"]),
                        "eventNames": event_names,
                        "keyCode": int(event["keyCode"]),
                        "nextEventOffset": int(event["nextEventOffset"]),
                        "programId": program_id,
                        "instructionOffset": int(program["instructionOffset"]),
                        "instructionEndOffset": int(program["instructionEndOffset"]),
                        "byteLength": int(program["byteLength"]),
                        "sha256": str(program["sha256"]),
                        "unboundCapabilities": capabilities,
                        "unsupportedInstructions": program["unsupportedInstructions"],
                    }
                )
        if not binding_events:
            raise HudAptConvertError("clip-action record has no events")
        binding = {
            "clipActionId": clip_action_id,
            "movie": movie.name,
            "sourceOffset": int(row["sourceOffset"]),
            "clipActionsOffset": int(value["clipActionsOffset"]),
            "headerEndOffset": int(value["headerEndOffset"]),
            "eventTableOffset": int(value["eventTableOffset"]),
            "eventCount": int(value["eventCount"]),
            "headerSha256": str(value["headerSha256"]),
            "targetPath": target_path,
            "targetSourceCharacterId": int(row["characterId"]),
            "targetMovie": target_movie,
            "targetCharacterId": target_character_id,
            "targetKind": target_kind,
            "targetClipId": target_clip_id,
            "targetTimelineId": target_timeline_id,
            "events": binding_events,
        }
        self.clip_actions.append(binding)
        if blocked_events:
            capabilities = {
                capability
                for event in blocked_events
                for capability in event["unboundCapabilities"]
            }
            if "lifecycle-dispatch" in capabilities:
                code = "clip-action-lifecycle-dispatch-not-bound"
            elif "input-dispatch" in capabilities:
                code = "clip-action-input-dispatch-not-bound"
            elif "host-callback" in capabilities:
                code = "clip-action-host-callback-not-bound"
            elif "external-state" in capabilities:
                code = "clip-action-external-state-not-bound"
            else:
                code = "clip-action-target-or-event-semantics-not-bound"
            binding["blockerCode"] = code
            self.block(
                code,
                movie.name,
                clipActionId=clip_action_id,
                sourceOffset=int(row["sourceOffset"]),
                clipActionsOffset=int(value["clipActionsOffset"]),
                targetPath=target_path,
                targetMovie=target_movie,
                targetCharacterId=target_character_id,
                targetClipId=target_clip_id,
                targetTimelineId=target_timeline_id,
                events=blocked_events,
            )

    def _resolve(self, movie: _Movie, character_id: int) -> tuple[_Movie, int] | None:
        if not 0 <= character_id < len(movie.characters):
            self.block(
                "character-id-out-of-range", movie.name, characterId=character_id
            )
            return None
        character = movie.characters[character_id]
        if character["kind"] != "null":
            return movie, character_id
        edge = movie.imports.get(character_id)
        if edge is None:
            self.block(
                "unresolved-null-character", movie.name, characterId=character_id
            )
            return None
        target = self.movies.get(edge[0].casefold())
        if target is None:
            self.block(
                "unresolved-import-movie",
                movie.name,
                characterId=character_id,
                targetMovie=edge[0],
                symbol=edge[1],
            )
            return None
        exported = target.exports.get(edge[1].casefold())
        if not exported:
            self.block(
                "unresolved-import-symbol",
                movie.name,
                characterId=character_id,
                targetMovie=edge[0],
                symbol=edge[1],
            )
            return None
        choices = sorted(set(exported))
        if len(choices) != 1:
            self.block(
                "ambiguous-import-symbol",
                movie.name,
                characterId=character_id,
                targetMovie=edge[0],
                symbol=edge[1],
                candidateCharacterIds=choices,
            )
            return None
        return target, choices[0]

    def _place_transform(self, row: Mapping[str, Any]) -> _Transform:
        flags = int(row["flags"])
        matrix = (
            tuple(float(value) for value in row["matrix"])
            if flags & 0x04
            else (1.0, 0.0, 0.0, 1.0)
        )
        translation = (
            tuple(float(value) for value in row["translation"])
            if flags & 0x04
            else (0.0, 0.0)
        )
        tint = (
            tuple(float(value) for value in row["tint"])
            if flags & 0x08
            else (1.0, 1.0, 1.0, 1.0)
        )
        additive = (
            tuple(float(value) for value in row["additive"])
            if flags & 0x08
            else (0.0, 0.0, 0.0, 0.0)
        )
        return _Transform(matrix, translation, tint, additive)  # type: ignore[arg-type]

    @staticmethod
    def _transform_contract(transform: _Transform) -> dict[str, list[float]]:
        return {
            "matrix": list(transform.matrix),
            "translation": list(transform.translation),
            "tint": list(transform.tint),
            "additive": list(transform.additive),
        }

    def _font_contract(
        self, movie: _Movie, font_character_id: int
    ) -> tuple[dict[str, Any] | None, tuple[str, int] | None]:
        resolved = self._resolve(movie, font_character_id)
        if resolved is None:
            return None, None
        font_movie, resolved_id = resolved
        character = font_movie.characters[resolved_id]
        if character["kind"] != "font":
            self.block(
                "text-font-character-kind-changed",
                movie.name,
                fontCharacterId=font_character_id,
                resolvedMovie=font_movie.name,
                resolvedCharacterId=resolved_id,
                resolvedKind=str(character["kind"]),
            )
            return None, None
        key = (font_movie.name.casefold(), resolved_id)
        definition = self.font_definitions.get(key)
        if definition is None:
            definition = {
                "fontId": f"{font_movie.name.casefold()}:{resolved_id}",
                "movie": font_movie.name,
                "characterId": resolved_id,
                "sourceOffset": int(character["sourceOffset"]),
                "definitionByteLength": int(character["definitionByteLength"]),
                "definitionSha256": str(character["definitionSha256"]),
                "name": str(character["name"]),
                "glyphCount": int(character["glyphCount"]),
                "glyphCharacterIds": list(character["glyphCharacterIds"]),
                "fontPayloadContained": bool(character["glyphCount"]),
            }
            self.font_definitions[key] = definition
            if not definition["fontPayloadContained"]:
                external = self.external_fonts.get(str(definition["fontId"]))
                if (
                    external is not None
                    and external.get("fontName") == definition["name"]
                ):
                    definition["externalFont"] = {
                        **dict(external),
                        "byteLength": 24_712,
                        "family": "Albertus MT",
                        "subfamily": "Regular",
                        "postScriptName": "AlbertusMT",
                        "outlineFormat": "CFF",
                        "unitsPerEm": 1000,
                        "glyphCount": 298,
                        "embeddedBitmapStrikeCount": 0,
                        "runtimeLoading": "sha256-verified-fontfile",
                        "fallbackAllowed": False,
                    }
                else:
                    self.block(
                        "text-font-payload-not-bundled",
                        font_movie.name,
                        fontId=definition["fontId"],
                        fontCharacterId=resolved_id,
                        fontName=definition["name"],
                        glyphCount=0,
                        sourceOffset=definition["sourceOffset"],
                        sourceClosure="sealed-261-source-hud-bundle",
                        fallbackAllowed=False,
                    )
        return definition, key

    def _text_runtime_source(
        self,
        movie: _Movie,
        character: Mapping[str, Any],
        ancestry: tuple[tuple[_Movie, Mapping[str, Any], str], ...],
        path: str,
    ) -> dict[str, Any]:
        variable_name = str(character["variableName"])
        for ancestor_movie, row, target_path in reversed(ancestry):
            clip = row.get("clipActions")
            if not isinstance(clip, Mapping):
                continue
            events = clip.get("events", [])
            if not isinstance(events, list) or len(events) != 1:
                continue
            event = events[0]
            if not isinstance(event, Mapping) or event.get("eventNames") != [
                "initialize"
            ]:
                continue
            program_id = (
                f"{ancestor_movie.name.casefold()}:clip-event:"
                f"{int(event['eventOffset'])}"
            )
            program = self.clip_action_programs.get(program_id)
            if program is None or not bool(program.get("supported", False)):
                continue
            instructions = program.get("instructions", [])
            if not isinstance(instructions, list) or len(instructions) != 4:
                continue
            names = [str(instruction.get("name", "")) for instruction in instructions]
            if names != [
                "push-this-variable",
                "push-string",
                "set-string-member",
                "end",
            ]:
                continue
            if str(instructions[1].get("operand", "")) != variable_name:
                continue
            value = str(instructions[2].get("operand", ""))
            effects = program.get("effects", [])
            if (
                not isinstance(effects, list)
                or len(effects) != 1
                or not isinstance(effects[0], Mapping)
                or effects[0].get("kind") != "bind-live-text"
                or effects[0].get("targetMember") != variable_name
                or effects[0].get("aptVariable") != value
            ):
                continue
            effect = effects[0]
            clip_action_id = (
                f"{ancestor_movie.name.casefold()}:{int(row['sourceOffset'])}:"
                f"{target_path}"
            )
            source = {
                "kind": "initialize-string-member",
                "variableName": variable_name,
                "initialValue": value,
                "sourceMovie": ancestor_movie.name,
                "sourceInstanceName": str(row.get("name", "")),
                "sourcePath": target_path,
                "clipActionId": clip_action_id,
                "programId": program_id,
                "programSha256": str(program["sha256"]),
                "runtimeInputs": list(effect["runtimeInputs"]),
                "formatter": str(effect["formatter"]),
                "localizationOrLiveValueBound": True,
                "fallbackAllowed": False,
            }
            return source
        self.block(
            "text-dynamic-variable-source-unresolved",
            movie.name,
            characterId=int(character["characterId"]),
            path=path,
            variableName=variable_name,
            fallbackAllowed=False,
        )
        return {
            "kind": "unresolved",
            "variableName": variable_name,
            "localizationOrLiveValueBound": False,
        }

    def _text_character(
        self,
        movie: _Movie,
        character_id: int,
        character: Mapping[str, Any],
        transform: _Transform,
        ancestry: tuple[tuple[_Movie, Mapping[str, Any], str], ...],
        path: str,
    ) -> None:
        key = (movie.name.casefold(), character_id)
        font, _font_key = self._font_contract(movie, int(character["fontCharacterId"]))
        if key not in self.text_definitions:
            self.text_definitions[key] = {
                "textId": f"{movie.name.casefold()}:{character_id}",
                "movie": movie.name,
                "characterId": character_id,
                "sourceOffset": int(character["sourceOffset"]),
                "definitionByteLength": int(character["definitionByteLength"]),
                "definitionSha256": str(character["definitionSha256"]),
                "bounds": list(character["bounds"]),
                "fontId": "" if font is None else str(font["fontId"]),
                "alignmentCode": int(character["alignmentCode"]),
                "color": list(character["color"]),
                "fontHeight": float(character["fontHeight"]),
                "readOnly": bool(character["readOnly"]),
                "multiline": bool(character["multiline"]),
                "wordWrap": bool(character["wordWrap"]),
                "placeholder": str(character["placeholder"]),
                "variableName": str(character["variableName"]),
                "contentPolicy": (
                    "dynamic-variable"
                    if str(character["variableName"])
                    else "static-placeholder"
                ),
            }
        bounds = [float(value) for value in character["bounds"]]
        corners = [
            transform.point((bounds[0], bounds[1])),
            transform.point((bounds[2], bounds[1])),
            transform.point((bounds[2], bounds[3])),
            transform.point((bounds[0], bounds[3])),
        ]
        self.text_instances.append(
            {
                "textId": f"{movie.name.casefold()}:{character_id}",
                "path": path,
                "displayOrder": self._next_display_order(),
                **self._transform_contract(transform),
                "transformedBounds": corners,
                "transformedColor": transform.color(tuple(character["color"])),
                "runtimeSource": self._text_runtime_source(
                    movie, character, ancestry, path
                ),
            }
        )

    def _button_character(
        self,
        movie: _Movie,
        character_id: int,
        character: Mapping[str, Any],
        transform: _Transform,
        path: str,
    ) -> None:
        key = (movie.name.casefold(), character_id)
        definition = self.button_definitions.get(key)
        if definition is None:
            records: list[dict[str, Any]] = []
            for raw_record in character["records"]:
                record = dict(raw_record)
                resolved = self._resolve(movie, int(record["characterId"]))
                if resolved is None:
                    record["resolvedCharacter"] = None
                else:
                    target_movie, target_id = resolved
                    target = target_movie.characters[target_id]
                    record["resolvedCharacter"] = {
                        "movie": target_movie.name,
                        "characterId": target_id,
                        "kind": str(target["kind"]),
                        "geometryId": target.get("geometryId"),
                    }
                records.append(record)
            definition = {
                "buttonId": f"{movie.name.casefold()}:{character_id}",
                "movie": movie.name,
                "characterId": character_id,
                "sourceOffset": int(character["sourceOffset"]),
                "definitionByteLength": int(character["definitionByteLength"]),
                "definitionSha256": str(character["definitionSha256"]),
                "isMenu": bool(character["isMenu"]),
                "bounds": list(character["bounds"]),
                "vertices": [list(vertex) for vertex in character["vertices"]],
                "triangles": [list(triangle) for triangle in character["triangles"]],
                "records": records,
                "actions": [dict(action) for action in character["actions"]],
                "visualStatePolicy": "source-records-only",
                "eventPolicy": "source-actions-only",
            }
            self.button_definitions[key] = definition
            if character["actions"]:
                self.block(
                    "button-action-programs-not-bound",
                    movie.name,
                    characterId=character_id,
                    actionCount=len(character["actions"]),
                )
        hit_records = [
            record for record in character["records"] if "hit" in record["states"]
        ]
        if len(hit_records) != 1:
            self.block(
                "button-hit-state-not-unique",
                movie.name,
                characterId=character_id,
                path=path,
                hitRecordCount=len(hit_records),
            )
            hit_transform = transform
        else:
            record = hit_records[0]
            hit_transform = transform.combine(
                _Transform(
                    tuple(float(value) for value in record["matrix"]),
                    tuple(float(value) for value in record["translation"]),
                    (1.0, 1.0, 1.0, 1.0),
                    (0.0, 0.0, 0.0, 0.0),
                )
            )
        vertices = [
            hit_transform.point((float(vertex[0]), float(vertex[1])))
            for vertex in character["vertices"]
        ]
        self.button_instances.append(
            {
                "buttonId": f"{movie.name.casefold()}:{character_id}",
                "path": path,
                **self._transform_contract(transform),
                "hitTransform": self._transform_contract(hit_transform),
                "hitVertices": vertices,
                "hitTriangles": [list(triangle) for triangle in character["triangles"]],
                "eventBindings": [],
            }
        )

    def _frame(
        self,
        movie: _Movie,
        rows: Iterable[Mapping[str, Any]],
        transform: _Transform,
        stack: tuple[tuple[str, int], ...],
        path: str,
        ancestry: tuple[tuple[_Movie, Mapping[str, Any], str], ...],
    ) -> None:
        display: dict[int, Mapping[str, Any]] = {}
        for row in rows:
            kind = str(row["kind"])
            if kind == "place-object":
                flags = int(row["flags"])
                depth = int(row["depth"])
                if flags & 0x80:
                    self._register_clip_actions(movie, row, path, depth)
                if flags & 0x40:
                    self.block(
                        "clip-mask-not-converted",
                        movie.name,
                        sourceOffset=int(row["sourceOffset"]),
                        path=path,
                    )
                if flags & 0x02:
                    display[depth] = row
                elif flags & 0x01:
                    self.block(
                        "move-without-local-placement",
                        movie.name,
                        sourceOffset=int(row["sourceOffset"]),
                        depth=depth,
                        path=path,
                    )
            elif kind == "remove-object":
                display.pop(int(row["depth"]), None)
            elif kind in ("action-script", "init-action-script"):
                self._register_action_program(movie, row)
            elif kind == "frame-label" or kind == "background-color":
                continue
            else:
                self.block(
                    "unsupported-frame-item",
                    movie.name,
                    sourceOffset=int(row["sourceOffset"]),
                    itemKind=kind,
                    path=path,
                )
        for depth, row in sorted(display.items()):
            child = transform.combine(self._place_transform(row))
            child_path = f"{path}/{depth}"
            self._character(
                movie,
                int(row["characterId"]),
                child,
                stack,
                child_path,
                ancestry + ((movie, row, child_path),),
            )

    def _geometry(
        self, movie: _Movie, geometry_id: int, transform: _Transform, path: str
    ) -> None:
        rows, receipts = _geometry_primitives(
            movie, geometry_id, transform, len(self.draws)
        )
        for receipt in receipts:
            code = str(receipt["code"])
            detail = {
                key: value for key, value in receipt.items() if key != "code"
            }
            self.block(code, movie.name, **detail, path=path)
        for row in rows:
            row["displayOrder"] = self._next_display_order()
            row["movie"] = movie.name
            row["path"] = path
            self.draws.append(row)

    def _character(
        self,
        source_movie: _Movie,
        character_id: int,
        transform: _Transform,
        stack: tuple[tuple[str, int], ...],
        path: str,
        ancestry: tuple[tuple[_Movie, Mapping[str, Any], str], ...],
    ) -> None:
        resolved = self._resolve(source_movie, character_id)
        if resolved is None:
            return
        movie, resolved_id = resolved
        key = (movie.name, resolved_id)
        if key in stack or len(stack) >= MAX_RECURSION:
            self.block(
                "recursive-character", movie.name, characterId=resolved_id, path=path
            )
            return
        character = movie.characters[resolved_id]
        kind = str(character["kind"])
        nested = stack + (key,)
        if kind == "shape":
            self._geometry(movie, int(character["geometryId"]), transform, path)
        elif kind in ("sprite", "movie"):
            frames = movie.frames if kind == "movie" else character.get("frames", [])
            if not frames:
                self.block(
                    "playable-has-no-frame",
                    movie.name,
                    characterId=resolved_id,
                    path=path,
                )
                return
            selected_frame = self.selected_frames.get(
                (movie.name.casefold(), resolved_id), 0
            )
            if not 0 <= selected_frame < len(frames):
                raise HudAptConvertError("bounded selected frame is out of range")
            if len(frames) > 1:
                timeline_key = (movie.name.casefold(), resolved_id)
                timeline = self.timelines.get(timeline_key)
                if timeline is None:
                    if len(self.timelines) >= MAX_TIMELINES:
                        raise HudAptConvertError("HUD timeline count exceeds bounds")
                    timeline, failures = _reconstruct_timeline(
                        movie.name, resolved_id, frames
                    )
                    for frame in timeline["frames"]:
                        for action in frame.get("actionScripts", []):
                            action["scriptId"] = self._register_action_program(
                                movie, action
                            )
                    self.timelines[timeline_key] = timeline
                    if failures:
                        self.block(
                            "additional-timeline-frames-not-converted",
                            movie.name,
                            characterId=resolved_id,
                            frameCount=len(frames),
                            failures=failures,
                        )
                self.timeline_instances.append(
                    {
                        "timelineId": str(timeline["timelineId"]),
                        "path": path,
                        "matrix": list(transform.matrix),
                        "translation": list(transform.translation),
                        "tint": list(transform.tint),
                        "additive": list(transform.additive),
                    }
                )
            self._frame(
                movie,
                frames[selected_frame],
                transform,
                nested,
                path,
                ancestry,
            )
        elif kind == "text":
            self._text_character(
                movie,
                resolved_id,
                character,
                transform,
                ancestry,
                path,
            )
        elif kind == "button":
            self._button_character(
                movie,
                resolved_id,
                character,
                transform,
                path,
            )
        else:
            self.block(
                "character-kind-not-converted",
                movie.name,
                characterId=resolved_id,
                characterKind=kind,
                path=path,
            )

    def flatten(self) -> None:
        for layer_index, name in enumerate(MOVIE_ORDER):
            movie = self.movies.get(name.casefold())
            if movie is None:
                self.block("required-root-movie-missing", name)
                continue
            before = len(self.draws)
            self._frame(
                movie,
                movie.frames[0],
                _Transform(),
                ((movie.name, 0),),
                f"layer:{layer_index}:{name}",
                (),
            )
            if len(self.draws) == before:
                self.block("required-root-layer-has-no-draws", movie.name)

    def flatten_screen(self, roots: Iterable[tuple[str, int]]) -> None:
        """Flatten arbitrary root movies at an AUTHORED frame, additively.

        `flatten` above is the Palantir instrument: fixed MOVIE_ORDER, always
        frame 0.  It stays byte-for-byte so every Palantir pin keeps its
        meaning.  The retail SCREENS need something else, because they are
        script-driven state machines - `ScoreScreen` places nothing on frame 0
        and reaches its authored open state at its own `_open` label.  So this
        sibling replays the root timeline from frame 0 up to and including the
        target frame, which is exactly what feeding `_frame` the concatenated
        rows does: place-with-character sets a depth, remove-object clears it,
        and a bare move over a depth placed in an EARLIER frame still raises
        `move-without-local-placement` rather than being silently merged.  That
        blocker is the honest edge of this reconstruction, not a papered gap.
        """

        for layer_index, (name, target_frame) in enumerate(roots):
            movie = self.movies.get(name.casefold())
            if movie is None:
                self.block("required-root-movie-missing", name)
                continue
            if not 0 <= target_frame < len(movie.frames):
                self.block(
                    "root-target-frame-out-of-range",
                    movie.name,
                    targetFrame=int(target_frame),
                    frameCount=len(movie.frames),
                )
                continue
            rows = [
                row for frame in movie.frames[: target_frame + 1] for row in frame
            ]
            before = len(self.draws)
            self._frame(
                movie,
                rows,
                _Transform(),
                ((movie.name, target_frame),),
                f"layer:{layer_index}:{name}",
                (),
            )
            if len(self.draws) == before:
                self.block("required-root-layer-has-no-draws", movie.name)


def _validated_external_font_bindings(
    value: Iterable[Mapping[str, Any]],
) -> list[dict[str, Any]]:
    rows = [dict(row) for row in value]
    expected = [dict(row) for row in PRODUCTION_EXTERNAL_FONT_BINDINGS]
    if rows not in ([], expected):
        raise HudAptConvertError("HUD external font binding contract changed")
    return rows


def _flagged_null_clip_action_records(
    movies: Iterable[_Movie],
) -> list[dict[str, Any]]:
    records: dict[tuple[str, int], dict[str, Any]] = {}
    for movie in movies:
        frame_sets = [movie.frames]
        frame_sets.extend(
            character["frames"]
            for character in movie.characters
            if isinstance(character.get("frames"), list)
        )
        for frames in frame_sets:
            for frame in frames:
                for row in frame:
                    if row.get("clipActionsPointerState") != "source-flagged-null":
                        continue
                    key = (movie.name.casefold(), int(row["sourceOffset"]))
                    records[key] = {
                        "movie": movie.name,
                        "sourceVirtualPath": movie.reader.label,
                        "sourceOffset": int(row["sourceOffset"]),
                        "flags": int(row["flags"]),
                        "clipActionsOffset": 0,
                        "recordSha256": str(row["clipActionsRecordSha256"]),
                    }
    return [records[key] for key in sorted(records)]


def _root_label_map(movie: _Movie) -> dict[str, int]:
    return {
        str(row["name"]): frame_index
        for frame_index, frame in enumerate(movie.frames)
        for row in frame
        if row.get("kind") == "frame-label"
    }


def _validate_external_default_state(movie: _Movie, stop_frame: int) -> None:
    stop_sha = "0a6361b3a802f55cd5ae06101c88a1e216320fe11cc0cfe1d791eed08a1200fd"
    if movie.name == "InGameHelpBox":
        placements = [
            row for row in movie.frames[0] if row.get("kind") == "place-object"
        ]
        if len(placements) != 1 or placements[0].get("name") != "box":
            raise HudAptConvertError("HelpBox root attachment entry changed")
        character = movie.characters[int(placements[0]["characterId"])]
        if character.get("kind") != "sprite":
            raise HudAptConvertError("HelpBox box attachment changed")
        labels = {
            str(row["name"]): frame_index
            for frame_index, frame in enumerate(character["frames"])
            for row in frame
            if row.get("kind") == "frame-label"
        }
        frame = character["frames"][0]
        if labels.get("_hide") != 0:
            raise HudAptConvertError("HelpBox hidden entry label changed")
    else:
        if any(
            row.get("kind") == "place-object"
            for frame in movie.frames[: stop_frame + 1]
            for row in frame
        ):
            raise HudAptConvertError(
                f"{movie.name} default dormant entry acquired display content"
            )
        frame = movie.frames[stop_frame]
    actions = [row for row in frame if row.get("kind") == "action-script"]
    if len(actions) != 1:
        raise HudAptConvertError(f"{movie.name} default stop changed")
    offset = int(actions[0]["instructionsOffset"])
    instructions, end = _decode_action_sequence(movie, offset)
    if [str(row["name"]) for row in instructions] != ["stop", "end"] or _sha(
        movie.data[offset:end]
    ) != stop_sha:
        raise HudAptConvertError(f"{movie.name} default stop bytecode changed")


def _external_movie_attachment_contract(
    movies: Mapping[str, _Movie],
) -> list[dict[str, Any]]:
    palantir = movies.get("palantir")
    if palantir is None:
        raise HudAptConvertError("Palantir attachment owner is missing")
    empty = palantir.characters[41]
    if empty.get("kind") != "sprite" or empty.get("frames") != [[]]:
        raise HudAptConvertError("Palantir attachment placeholder changed")
    placements = {
        str(row.get("name")): row
        for row in palantir.frames[0]
        if row.get("kind") == "place-object" and row.get("name")
    }
    result: list[dict[str, Any]] = []
    for spec in _EXTERNAL_ATTACHMENT_SPECS:
        movie = movies.get(str(spec["movie"]).casefold())
        if movie is None:
            raise HudAptConvertError(
                f"external attachment source is missing: {spec['movie']}"
            )
        placement = placements.get(str(spec["target"]))
        if placement is None:
            raise HudAptConvertError(
                f"Palantir attachment target is missing: {spec['target']}"
            )
        source_offset = int(placement["sourceOffset"])
        placement_identity = (
            source_offset,
            int(placement["flags"]),
            int(placement["depth"]),
            int(placement["characterId"]),
            [float(value) for value in placement["matrix"]],
            [float(value) for value in placement["translation"]],
            [float(value) for value in placement["tint"]],
            [float(value) for value in placement["additive"]],
            _sha(palantir.data[source_offset : source_offset + 60]),
        )
        expected_identity = (
            int(spec["sourceOffset"]),
            0x26,
            int(spec["depth"]),
            41,
            list(spec["matrix"]),
            list(spec["translation"]),
            [1.0, 1.0, 1.0, 1.0],
            [0.0, 0.0, 0.0, 0.0],
            str(spec["recordSha256"]),
        )
        if placement_identity != expected_identity:
            raise HudAptConvertError(
                f"Palantir attachment target changed: {spec['target']}"
            )
        if len(movie.frames) != int(spec["frameCount"]):
            raise HudAptConvertError(
                f"external attachment frame count changed: {movie.name}"
            )
        if _root_label_map(movie) != spec["labels"]:
            raise HudAptConvertError(
                f"external attachment root labels changed: {movie.name}"
            )
        program_offset = int(spec["programOffset"])
        _instructions, program_end = _decode_action_sequence(movie, program_offset)
        if _sha(movie.data[program_offset:program_end]) != spec["programSha256"]:
            raise HudAptConvertError(
                f"external attachment source program changed: {movie.name}"
            )
        _validate_external_default_state(movie, int(spec["initialStopFrame"]))
        result.append(
            {
                "loadOrder": int(spec["loadOrder"]),
                "loadInstructionOffset": int(spec["loadInstructionOffset"]),
                "swf": str(spec["swf"]),
                "movie": movie.name,
                "target": str(spec["target"]),
                "targetPath": str(spec["targetPath"]),
                "attachmentKind": "replace-authored-empty-child-clip",
                "godotInterface": str(spec["godotInterface"]),
                "placeholder": {
                    "sourceOffset": source_offset,
                    "recordSha256": str(spec["recordSha256"]),
                    "characterId": 41,
                    "depth": int(spec["depth"]),
                    "matrix": list(spec["matrix"]),
                    "translation": list(spec["translation"]),
                    "tint": [1.0, 1.0, 1.0, 1.0],
                    "additive": [0.0, 0.0, 0.0, 0.0],
                },
                "sourceRoot": {
                    "characterKind": "movie",
                    "entryFrame": 0,
                    "frameCount": int(spec["frameCount"]),
                    "labels": dict(spec["labels"]),
                    "initialStopFrame": int(spec["initialStopFrame"]),
                    "programOffset": program_offset,
                    "programSha256": str(spec["programSha256"]),
                },
                "defaultState": str(spec["defaultState"]),
                "normalMenVsMen": str(spec["normalMenVsMen"]),
                "lifecycle": {
                    "loadedCallback": str(spec["loadedCallback"]),
                    "unloadedCallback": str(spec["unloadedCallback"]),
                    "argument": str(spec["callbackArgument"]),
                    "dispatchBound": False,
                },
                "genericVmRequired": False,
                "independentRootAllowed": False,
            }
        )
    return result


def _external_movie_load_contract(
    movies: Mapping[str, _Movie],
    attachments: Iterable[Mapping[str, Any]],
) -> list[dict[str, Any]]:
    by_movie = {str(row["movie"]): row for row in attachments}
    result: list[dict[str, Any]] = []
    for load_order, swf, movie_name, target in EXTERNAL_MOVIE_LOADS:
        movie = movies.get(movie_name.casefold())
        if movie is None:
            raise HudAptConvertError(
                f"unconditional external movie source is missing: {movie_name}"
            )
        already_root_layer = movie_name in MOVIE_ORDER
        attachment = by_movie.get(movie_name)
        if not already_root_layer and attachment is None:
            raise HudAptConvertError(
                f"external movie attachment is missing: {movie_name}"
            )
        result.append(
            {
                "loadOrder": load_order,
                "swf": swf,
                "movie": movie_name,
                "target": target,
                "sourceLoadReachable": True,
                "sourceClosurePresent": True,
                "sourceFileCount": 3 + len(movie.geometry) + len(movie.atlases),
                "rootFrameCount": len(movie.frames),
                "characterCount": len(movie.characters),
                "importCount": len(movie.imports),
                "atlasCount": len(movie.atlases),
                "runtimeAttachment": (
                    "already-bound-root-layer"
                    if already_root_layer
                    else "exact-palantir-child-slot-bound"
                ),
                "targetPath": (
                    f"Palantir.root.frame0/{target}"
                    if attachment is None
                    else str(attachment["targetPath"])
                ),
                "defaultState": (
                    "hidden-offscreen"
                    if attachment is None
                    else str(attachment["defaultState"])
                ),
                "runtimeReachableDrawCount": 0,
                "runtimeReachableTimelineCount": 0,
                "runtimeReachableActionScriptCount": 0,
                "runtimeReachableClipActionCount": 0,
            }
        )
    return result


def _resource_flash_contract(
    movies: Mapping[str, _Movie],
    timelines: Iterable[Mapping[str, Any]],
    timeline_instances: Iterable[Mapping[str, Any]],
    action_programs: Mapping[str, Mapping[str, Any]],
) -> dict[str, Any] | None:
    entry = action_programs.get("palantir:332504")
    if entry is None:
        return None
    expected = _TYPED_RESOURCE_FLASH_PROGRAM
    if (
        not entry.get("supported")
        or entry.get("movie") != "Palantir"
        or entry.get("sourceOffset") != expected["sourceOffset"]
        or entry.get("instructionOffset") != expected["instructionOffset"]
        or entry.get("byteLength") != expected["byteLength"]
        or entry.get("sha256") != expected["sha256"]
        or entry.get("effects") != expected["effects"]
    ):
        raise HudAptConvertError("Palantir resource-flash entry contract changed")

    owner = action_programs.get("palantir:95856")
    if (
        owner is None
        or owner.get("instructionOffset") != 359_864
        or owner.get("byteLength") != 4_052
        or owner.get("sha256")
        != "7d3fe42f7872a5c1c9c34446b8707512d1ac80abf9a1459a1ad3586e035e07d9"
    ):
        raise HudAptConvertError("PlayCommandPointEffect owner changed")
    pools = [
        row
        for row in owner.get("instructions", [])
        if row.get("name") == "constant-pool"
    ]
    functions = [
        row
        for row in owner.get("instructions", [])
        if row.get("functionName") == "PlayCommandPointEffect"
    ]
    if len(pools) != 1 or len(functions) != 1:
        raise HudAptConvertError("PlayCommandPointEffect identity changed")
    pool = [row.get("value") for row in pools[0].get("constants", [])]
    function = functions[0]
    body = function.get("body", [])
    if (
        len(pool) <= 39
        or (pool[1], pool[38], pool[39]) != ("gotoAndPlay", "_go", "CommandPointsFlash")
        or function.get("parameters") != []
        or function.get("offset") != 361_120
        or function.get("bodyByteLength") != 16
        or [row.get("name") for row in body]
        != [
            "push-constant-byte",
            "push-one",
            "push-data",
            "get-named-member",
            "call-named-method-pop",
        ]
        or [body[index].get("operand") for index in (0, 3, 4)] != [38, 39, 1]
    ):
        raise HudAptConvertError("PlayCommandPointEffect semantics changed")

    palantir = movies.get("palantir")
    if palantir is None:
        raise HudAptConvertError("Palantir resource-flash source is missing")
    if (
        _sha(palantir.data[361_152:361_168])
        != "a5b9a91b9ad21d12bced1a7d9f94c803d2abbb5fe542646356fdc90663f47788"
        or _sha(palantir.data[97_352:97_412])
        != "6673eea4c330f20d073788d1f1bc36f50ba4b456a73a7ff1e40477da6b93c527"
    ):
        raise HudAptConvertError("Palantir resource-flash byte evidence changed")

    rows = [row for row in timelines if row.get("timelineId") == "palantir:309"]
    instances = [
        row for row in timeline_instances if row.get("timelineId") == "palantir:309"
    ]
    if len(rows) != 1 or len(instances) != 1:
        raise HudAptConvertError("CommandPointsFlash runtime identity changed")
    timeline = rows[0]
    frames = timeline.get("frames", [])
    if (
        _sha(_canonical_bytes(timeline))
        != "f2254f867b5f59070284fd2f028d5f4e4d787f09af9f59220491559053b069d6"
        or timeline.get("characterId") != 309
        or timeline.get("frameCount") != 58
        or len(frames) != 58
        or frames[0].get("labels", [])[0].get("name") != "_stop"
        or frames[8].get("labels", [])[0].get("name") != "_go"
        or frames[8].get("actionScripts", [])[0].get("scriptId") != "palantir:332504"
        or frames[57].get("actionScripts", [])[0].get("scriptId") != "palantir:358480"
    ):
        raise HudAptConvertError("CommandPointsFlash timeline changed")
    instance = instances[0]
    if (
        instance.get("path") != "layer:1:Palantir/148"
        or instance.get("matrix") != [1.0, 0.0, 0.0, 1.0]
        or instance.get("translation") != [203.5, 770.2999877929688]
    ):
        raise HudAptConvertError("CommandPointsFlash placement changed")

    return {
        "typedInput": {
            "receiver": "Palantir root",
            "method": "PlayCommandPointEffect",
            "arguments": [],
            "bodyOffset": 361_152,
            "bodyByteLength": 16,
            "bodySha256": (
                "a5b9a91b9ad21d12bced1a7d9f94c803d2abbb5fe542646356fdc90663f47788"
            ),
            "effect": {
                "target": "CommandPointsFlash",
                "method": "gotoAndPlay",
                "arguments": ["_go"],
            },
        },
        "visual": {
            "instanceName": "CommandPointsFlash",
            "timelineId": "palantir:309",
            "characterId": 309,
            "placementPath": "layer:1:Palantir/148",
            "placementSha256": (
                "6673eea4c330f20d073788d1f1bc36f50ba4b456a73a7ff1e40477da6b93c527"
            ),
            "frameCount": 58,
            "millisecondsPerFrame": 33,
            "timelineSha256": (
                "f2254f867b5f59070284fd2f028d5f4e4d787f09af9f59220491559053b069d6"
            ),
            "stoppedFrame": {"index": 0, "label": "_stop", "script": "palantir:332480"},
            "entryFrame": {"index": 8, "label": "_go", "script": "palantir:332504"},
            "returnFrame": {"index": 57, "script": "palantir:358480"},
            "entryToReturnIntervals": 49,
            "authoredFrameIntervalSpanMilliseconds": 1_617,
            "retriggerPolicy": "rewind-one-placed-instance-to-entry-frame",
        },
        "entryAction": {
            "scriptId": "palantir:332504",
            "instructionRange": [370_752, 370_778],
            "byteLength": 26,
            "sha256": expected["sha256"],
            "effectsInAuthoredOrder": json.loads(
                json.dumps(expected["effects"], sort_keys=True)
            ),
        },
        "audioEventIntent": {
            "eventId": "Gui_PalantirResourceBarFlash",
            "dispatch": "FSCommand:PlaySound",
            "nativeHandlerSha256": (
                "d7552e58b40a463b9f39d1cb6a3fa92dd0a6d8c0014fbc5234380865c447c6da"
            ),
            "leafSha256": (
                "f2d3aff531ecfd3616069d53551823f92aee92f009382d3bf39d4ec8e2eca350"
            ),
            "leafDurationSeconds": 2.130408163265306,
            "requestMode": 2,
            "existingVoiceSuppressionInHandler": False,
        },
        "runtimePolicy": {
            "nativeCounterAutoTriggerBound": False,
            "mixerOverlapPolicyBound": False,
            "genericDispatchAllowed": False,
            "fallbackAllowed": False,
        },
    }


def _side_command_topology_contract(
    movies: Mapping[str, _Movie], action_programs: Mapping[str, Mapping[str, Any]]
) -> dict[str, Any]:
    """Bind the exact retail button topology used by the three typed scripts."""

    movie = movies.get("ingamesidecommandbar")
    if movie is None:
        raise HudAptConvertError("side-command topology movie is missing")
    if len(movie.data) != 14_082 or _sha(movie.data) != (
        "84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef"
    ):
        raise HudAptConvertError("side-command topology source changed")
    helper_program = action_programs.get("ingamesidecommandbar:6264")
    if (
        helper_program is None
        or int(helper_program.get("instructionOffset", -1)) != 10_956
        or int(helper_program.get("byteLength", -1)) != 996
        or str(helper_program.get("sha256", ""))
        != "abcf2a697a9852b4b61c07de74f7e4151bed6cd467ebd98bb0eb74e17833fa16"
    ):
        raise HudAptConvertError("side-command helper library changed")
    for helper in _SIDE_COMMAND_HELPERS:
        start = int(helper["bodyOffset"])
        length = int(helper["bodyByteLength"])
        if _sha(movie.data[start : start + length]) != helper["bodySha256"]:
            raise HudAptConvertError(
                f"side-command helper body changed: {helper['name']}"
            )
    button = movie.characters[18]
    button_set = movie.characters[21]
    if (
        button.get("kind") != "sprite"
        or button_set.get("kind") != "sprite"
        or len(button.get("frames", [])) != 21
        or len(button_set.get("frames", [])) != 1
    ):
        raise HudAptConvertError("side-command sprite topology changed")
    button_frames = button["frames"]
    if _timeline_labels(button_frames) != {"_hide": 0, "_show": 10}:
        raise HudAptConvertError("side-command button labels changed")
    hide_actions = [
        (int(row["sourceOffset"]), int(row["instructionsOffset"]))
        for row in button_frames[0]
        if row.get("kind") == "action-script"
    ]
    show_actions = [
        (int(row["sourceOffset"]), int(row["instructionsOffset"]))
        for row in button_frames[10]
        if row.get("kind") == "action-script"
    ]
    show_places = [
        {key: row[key] for key in ("sourceOffset", "depth", "characterId", "name")}
        for row in button_frames[10]
        if row.get("kind") == "place-object"
    ]
    if hide_actions != [(6264, 10956), (6272, 11952)]:
        raise HudAptConvertError("side-command hide action order changed")
    if show_actions != [(6368, 11992)] or show_places != [
        {"sourceOffset": 6392, "depth": 3, "characterId": 16, "name": "Frame"},
        {
            "sourceOffset": 6456,
            "depth": 9,
            "characterId": 17,
            "name": "ButtonGlass",
        },
    ]:
        raise HudAptConvertError("side-command show frame order changed")
    set_rows = button_set["frames"][0]
    set_actions = [
        (int(row["sourceOffset"]), int(row["instructionsOffset"]))
        for row in set_rows
        if row.get("kind") == "action-script"
    ]
    local_buttons = [
        {
            "name": str(row.get("name", "")),
            "depth": int(row.get("depth", -1)),
            "characterId": int(row.get("characterId", -1)),
            "sourceOffset": int(row.get("sourceOffset", -1)),
        }
        for row in set_rows
        if row.get("kind") == "place-object"
        and str(row.get("name", "")).startswith("Button")
    ]
    if set_actions != [(7296, 12148)] or local_buttons != [
        {
            "name": f"Button{index}",
            "depth": index * 10 + 1,
            "characterId": 18,
            "sourceOffset": 7304 + index * 64,
        }
        for index in range(12)
    ]:
        raise HudAptConvertError("side-command ButtonSet topology changed")
    expected_script_ids = {
        str(value["scriptId"]) for value in _TYPED_SIDE_COMMAND_PROGRAMS.values()
    }
    if not all(
        bool(action_programs.get(script_id, {}).get("supported"))
        for script_id in expected_script_ids
    ):
        raise HudAptConvertError("side-command typed programs are not executable")
    show_targets = [f"Button{index}" for index in range(1, 16)]
    return {
        "schema": "openbfme.retail-hud-side-command-topology",
        "schemaVersion": 0,
        "movie": "InGameSideCommandBar",
        "source": {
            "virtualPath": "InGameSideCommandBar.apt",
            "byteLength": len(movie.data),
            "sha256": _sha(movie.data),
        },
        "buttonSet": {
            "characterId": 21,
            "localButtons": local_buttons,
            "showTargets": show_targets,
            "staticallyAbsentShowTargets": show_targets[11:],
            "missingTargetEffect": "ordered-no-op",
        },
        "button": {
            "characterId": 18,
            "labels": {"_hide": 0, "_show": 10},
            "showFramePlacementsBeforeQueuedActions": show_places,
        },
        "helperLibrary": {
            "scriptId": "ingamesidecommandbar:6264",
            "instructionOffset": 10956,
            "byteLength": 996,
            "sha256": str(helper_program["sha256"]),
            "functions": [dict(row) for row in _SIDE_COMMAND_HELPERS],
            "frameVisiblePredicate": (
                "neighbor != undefined && neighbor.Frame != undefined"
            ),
            "truthTable": [
                {
                    "nextFrameVisible": False,
                    "priorFrameVisible": False,
                    "label": "_topbottom",
                },
                {"nextFrameVisible": True, "priorFrameVisible": False, "label": "_top"},
                {
                    "nextFrameVisible": False,
                    "priorFrameVisible": True,
                    "label": "_bottom",
                },
                {
                    "nextFrameVisible": True,
                    "priorFrameVisible": True,
                    "label": "_middle",
                },
            ],
            "neighborUpdateOrder": ["next", "prior"],
        },
        "typedScriptIds": sorted(expected_script_ids),
        "scheduling": {
            "sameFramePlacementsBeforeQueuedActions": True,
            "genericActionScriptVmUsed": False,
        },
        "unresolvedRuntimeTraceCount": 0,
    }


def _palantir_command_topology_contract(
    movies: Mapping[str, _Movie], action_programs: Mapping[str, Mapping[str, Any]]
) -> dict[str, Any]:
    """Bind exact CommandButtons placement topology to the two typed programs."""

    movie = movies.get("palantir")
    if movie is None:
        raise HudAptConvertError("Palantir command topology movie is missing")
    if len(movie.data) != 378_173 or _sha(movie.data) != (
        "c1f500847f0c77d4c6504edf79113b5723300165bebd42b4dafda479516f5140"
    ):
        raise HudAptConvertError("Palantir command topology source changed")
    imports = [
        {
            "localCharacterId": character_id,
            "movie": movie_name,
            "symbol": symbol,
        }
        for character_id, movie_name, symbol in (
            (106, "libInGameUI", "CommandButtonSubMenu"),
            (108, "libInGameUI", "MovieClipFrame"),
            (110, "libInGameUI", "ButtonGlass"),
            (111, "libInGameUI", "CommandButtonToggleFlash"),
        )
    ]
    if any(
        movie.imports.get(int(row["localCharacterId"]))
        != (str(row["movie"]), str(row["symbol"]))
        for row in imports
    ):
        raise HudAptConvertError("Palantir command imports changed")
    expected_root = [
        {
            "sourceOffset": 96392,
            "depth": 17,
            "characterId": 86,
            "name": "CommandUI",
            "translation": [287.6000061035156, 660.0],
        },
        {
            "sourceOffset": 96776,
            "depth": 44,
            "characterId": 114,
            "name": "CommandButtons",
            "translation": [289.54998779296875, 659.8499755859375],
        },
        {
            "sourceOffset": 96840,
            "depth": 90,
            "characterId": 122,
            "name": "AutoAbilityOverlays",
            "translation": [287.6000061035156, 660.0],
        },
    ]
    actual_root = [
        {
            key: row[key]
            for key in ("sourceOffset", "depth", "characterId", "name", "translation")
        }
        for row in movie.frames[0]
        if row.get("kind") == "place-object"
        and row.get("name") in {"CommandUI", "CommandButtons", "AutoAbilityOverlays"}
    ]
    if actual_root != expected_root:
        raise HudAptConvertError("Palantir command root placements changed")
    command_ui = movie.characters[86]
    command_buttons = movie.characters[114]
    flash_effects = movie.characters[113]
    overlays = movie.characters[122]
    if (
        any(
            character.get("kind") != "sprite"
            for character in (command_ui, command_buttons, flash_effects, overlays)
        )
        or len(command_ui.get("frames", [])) != 19
        or len(command_buttons.get("frames", [])) != 19
        or len(flash_effects.get("frames", [])) != 1
        or len(overlays.get("frames", [])) != 1
        or _timeline_labels(command_ui["frames"]) != {"_hide": 0, "_show": 9}
        or _timeline_labels(command_buttons["frames"])
        != {"_hide": 0, "_show": 9}
    ):
        raise HudAptConvertError("Palantir command sprite topology changed")
    hide_actions = [
        (int(row["sourceOffset"]), int(row["instructionsOffset"]))
        for row in command_buttons["frames"][0]
        if row.get("kind") == "action-script"
    ]
    show_rows = command_buttons["frames"][9]
    show_source_order = [
        {"kind": str(row["kind"]), "sourceOffset": int(row["sourceOffset"])}
        for row in show_rows
    ]
    expected_show_source_order = [
        {"kind": "action-script", "sourceOffset": 169256},
        {"kind": "frame-label", "sourceOffset": 169264},
        *[
            {"kind": "place-object", "sourceOffset": source_offset}
            for source_offset in range(169280, 170689, 64)
        ],
    ]
    if hide_actions != [(169224, 367624)] or show_source_order != expected_show_source_order:
        raise HudAptConvertError("Palantir command authored frame order changed")
    expected_placements = [
        (169280, 1, 106, "subMenu0"),
        (169344, 3, 106, "subMenu1"),
        (169408, 5, 106, "subMenu2"),
        (169472, 7, 106, "subMenu3"),
        (169536, 9, 107, ""),
        (169600, 11, 108, "1"),
        (169664, 13, 108, "2"),
        (169728, 15, 108, "3"),
        (169792, 17, 108, "4"),
        (169856, 19, 108, "5"),
        (169920, 21, 108, "0"),
        (169984, 23, 109, ""),
        (170048, 25, 110, "glass0"),
        (170112, 27, 110, "glass5"),
        (170176, 29, 110, "glass4"),
        (170240, 31, 110, "glass3"),
        (170304, 33, 110, "glass2"),
        (170368, 35, 110, "glass1"),
        (170432, 37, 111, "toggleFlash0"),
        (170496, 39, 111, "toggleFlash1"),
        (170560, 41, 111, "toggleFlash2"),
        (170624, 43, 111, "toggleFlash3"),
        (170688, 45, 113, "FlashEffects"),
    ]
    actual_placements = [
        (
            int(row["sourceOffset"]),
            int(row["depth"]),
            int(row["characterId"]),
            str(row.get("name", "")),
        )
        for row in show_rows
        if row.get("kind") == "place-object"
    ]
    if actual_placements != expected_placements:
        raise HudAptConvertError("Palantir command show placements changed")

    def child_placements(character: Mapping[str, Any]) -> list[dict[str, Any]]:
        return [
            {
                "sourceOffset": int(row["sourceOffset"]),
                "depth": int(row["depth"]),
                "characterId": int(row["characterId"]),
                "name": str(row.get("name", "")),
            }
            for row in character["frames"][0]
            if row.get("kind") == "place-object"
        ]

    flash_children = child_placements(flash_effects)
    overlay_children = child_placements(overlays)
    if flash_children != [
        {
            "sourceOffset": 168840 + index * 64,
            "depth": index * 2 + 1,
            "characterId": 112,
            "name": str(index),
        }
        for index in range(6)
    ] or overlay_children != [
        {
            "sourceOffset": 233848 + index * 64,
            "depth": index * 2 + 1,
            "characterId": 121,
            "name": str((index + 1) % 6),
        }
        for index in range(6)
    ]:
        raise HudAptConvertError("Palantir command target collections changed")
    typed_ids = sorted(
        str(value["scriptId"]) for value in _TYPED_PALANTIR_COMMAND_PROGRAMS.values()
    )
    if not all(
        bool(action_programs.get(script_id, {}).get("supported"))
        for script_id in typed_ids
    ):
        raise HudAptConvertError("Palantir command typed programs are not executable")
    skill_upgrade = action_programs.get("palantir:167296", {})
    if (
        bool(skill_upgrade.get("supported"))
        or int(skill_upgrade.get("instructionOffset", -1)) != 367200
        or int(skill_upgrade.get("byteLength", -1)) != 68
        or str(skill_upgrade.get("sha256", ""))
        != "2ddccd66a9c1b67fde9f41dfd3cd471bc0061724827ee1bfa426d8dac2447567"
    ):
        raise HudAptConvertError("Palantir skill-upgrade trace gate changed")
    return {
        "schema": "openbfme.retail-hud-palantir-command-topology",
        "schemaVersion": 0,
        "movie": "Palantir",
        "source": {
            "virtualPath": "Palantir.apt",
            "byteLength": len(movie.data),
            "sha256": _sha(movie.data),
        },
        "root": expected_root,
        "imports": imports,
        "commandButtons": {
            "characterId": 114,
            "labels": {"_hide": 0, "_show": 9},
            "declarationAction": "palantir:169224",
            "showAction": "palantir:169256",
            "showSourceOrder": expected_show_source_order,
            "showPlacements": [
                {
                    "sourceOffset": source_offset,
                    "depth": depth,
                    "characterId": character_id,
                    "name": name,
                }
                for source_offset, depth, character_id, name in expected_placements
            ],
            "numericButtonFrames": ["1", "2", "3", "4", "5", "0"],
            "glassTargets": [f"glass{index}" for index in range(6)],
            "toggleFlashTargets": [f"toggleFlash{index}" for index in range(4)],
        },
        "collections": {
            "flashEffects": {
                "characterId": 113,
                "placementName": "FlashEffects",
                "children": flash_children,
            },
            "autoAbilityOverlays": {
                "characterId": 122,
                "placementName": "AutoAbilityOverlays",
                "children": overlay_children,
            },
        },
        "lifecycleFunctions": [
            dict(row) for row in _PALANTIR_COMMAND_LIFECYCLE_FUNCTIONS
        ],
        "buttonMethods": [dict(row) for row in _PALANTIR_COMMAND_BUTTON_METHODS],
        "typedScriptIds": typed_ids,
        "scheduling": {
            "rawActionRecordEffect": "deferred",
            "rawPlacementRecordEffect": "immediate",
            "sameFramePlacementsBeforeQueuedActions": True,
            "genericActionScriptVmUsed": False,
        },
        "remainingTraceGates": [
            {
                "id": "skill-upgrade-root-method-effects",
                "programId": "palantir:167296",
                "scenario": "enter CommandUI _show once while InGame is true",
            },
            {
                "id": "command-child-lifecycle-host-result",
                "programId": "palantir:169224",
                "scenario": "one CommandButtons show-hide cycle",
            },
        ],
        "unresolvedRuntimeTraceCount": 2,
    }


def _make_contract(
    movies_list: list[_Movie],
    source: Mapping[str, Any],
    retail_ini_sources: Mapping[str, Path | str | bytes | bytearray],
    extra_blockers: Iterable[Mapping[str, Any]] = (),
    external_fonts: Iterable[Mapping[str, Any]] = (),
    wnd_companion: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    movies = {movie.name.casefold(): movie for movie in movies_list}
    if len(movies) != len(movies_list):
        raise HudAptConvertError("HUD APT movie identities collide")
    font_bindings = _validated_external_font_bindings(external_fonts)
    external_movie_attachments = _external_movie_attachment_contract(movies)
    external_movie_loads = _external_movie_load_contract(
        movies, external_movie_attachments
    )
    flagged_null_records = _flagged_null_clip_action_records(movies_list)
    frame_selection, selected_frames, selection_blockers = _bounded_initial_selection(
        movies
    )
    flattener = _Flattener(movies, selected_frames, font_bindings)
    flattener.flatten()
    flattener.block(
        "text-rendered-parity-capture-not-passed",
        "Palantir",
        fontId="palantir:63",
        textCharacterIds=[130, 132, 134],
        gateCount=7,
        gates=[
            "font-size-device-mapping",
            "baseline-and-glyph-origin",
            "antialiasing-and-cff-hinting",
            "final-color-and-alpha-blend",
            "ancestor-clipping",
            "final-composite-order",
            "runtime-font-winner",
        ],
        resolution=[1024, 768],
        retailVsGodotCaptureRequired=True,
        fallbackAllowed=False,
        parityReady=False,
    )
    for row in external_movie_loads:
        if row["runtimeAttachment"] not in {
            "already-bound-root-layer",
            "exact-palantir-child-slot-bound",
        }:
            continue
        movie_name = str(row["movie"])
        row["runtimeReachableDrawCount"] = sum(
            draw.get("movie") == movie_name for draw in flattener.draws
        )
        row["runtimeReachableTimelineCount"] = sum(
            timeline.get("movie") == movie_name
            for timeline in flattener.timelines.values()
        )
        row["runtimeReachableActionScriptCount"] = sum(
            action.get("movie") == movie_name
            for action in flattener.action_programs.values()
        )
        row["runtimeReachableClipActionCount"] = sum(
            binding.get("movie") == movie_name for binding in flattener.clip_actions
        )
    timelines = [flattener.timelines[key] for key in sorted(flattener.timelines)]
    timeline_frame_count = sum(int(timeline["frameCount"]) for timeline in timelines)
    if timeline_frame_count > MAX_TIMELINE_FRAMES:
        raise HudAptConvertError("HUD timeline frame count exceeds bounds")
    resource_flash = _resource_flash_contract(
        movies,
        timelines,
        flattener.timeline_instances,
        flattener.action_programs,
    )
    side_command_topology = _side_command_topology_contract(
        movies, flattener.action_programs
    )
    side_command_fade_runtime = _side_command_fade_runtime_contract(
        movies["ingamesidecommandbar"], retail_ini_sources
    )
    palantir_command_topology = _palantir_command_topology_contract(
        movies, flattener.action_programs
    )
    if resource_flash is not None:
        flattener.block(
            "resource-flash-native-trigger-capture-not-passed",
            "Palantir",
            method="PlayCommandPointEffect",
            nativeAptStubRange=["0x007fe9bb", "0x007fe9da"],
            nativeAptStubSha256=(
                "0db675e029ff06307ba4b9185ffed58c6adbf316667c1b3a62232089f4acb55d"
            ),
            callerVas=["0x006d48e3", "0x006d4a02"],
            gate="semantic names of the two stripped native counters",
            autoTriggerBound=False,
            parityReady=False,
        )
        flattener.block(
            "resource-flash-mixer-overlap-capture-not-passed",
            "Palantir",
            eventId="Gui_PalantirResourceBarFlash",
            leafDurationSeconds=2.130408163265306,
            gate="two requests less than one leaf duration apart",
            requiredObservations=[
                "vslot-0x64-request-objects",
                "returned-voice-handles",
                "audible-mixer-result",
            ],
            mixerOverlapPolicyBound=False,
            parityReady=False,
        )
    complete_timelines = [
        timeline for timeline in timelines if timeline["displayListComplete"]
    ]
    if complete_timelines:
        flattener.block(
            "timeline-playback-not-bound",
            "APT closure",
            timelineIds=[timeline["timelineId"] for timeline in complete_timelines],
            selectionPolicy="static-selected-frames-only",
        )
    for blocker in (*selection_blockers, *extra_blockers):
        flattener.block(
            str(blocker["code"]),
            str(blocker.get("movie", "controlbar.wnd")),
            **{
                key: value
                for key, value in blocker.items()
                if key not in {"code", "movie"}
            },
        )
    flattener.block(
        "external-movie-lifecycle-capture-not-passed",
        "Palantir",
        gateCount=len(_EXTERNAL_ATTACHMENT_GATES),
        gates=[dict(row) for row in _EXTERNAL_ATTACHMENT_GATES],
        targets=[str(row["target"]) for row in external_movie_attachments],
        loadOrder=[int(row["loadOrder"]) for row in external_movie_attachments],
        heroInitialVisibilityGuessed=False,
        asyncCompletionOrderGuessed=False,
        unloadOrderGuessed=False,
        parityReady=False,
    )
    for row in flagged_null_records:
        flattener.block(
            "source-flagged-null-clip-action-pointer",
            str(row["movie"]),
            sourceVirtualPath=str(row["sourceVirtualPath"]),
            sourceOffset=int(row["sourceOffset"]),
            flags=int(row["flags"]),
            clipActionsOffset=0,
            recordSha256=str(row["recordSha256"]),
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
    supported_action_count = sum(
        bool(program["supported"]) for program in action_programs
    )
    clip_action_programs = [
        flattener.clip_action_programs[key]
        for key in sorted(flattener.clip_action_programs)
    ]
    clip_actions = sorted(
        flattener.clip_actions,
        key=lambda item: json.dumps(item, sort_keys=True, separators=(",", ":")),
    )
    clip_action_event_count = sum(
        int(binding["eventCount"]) for binding in clip_actions
    )
    executable_clip_action_event_count = sum(
        bool(event["executable"])
        for binding in clip_actions
        for event in binding["events"]
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
    stage_pieces = _stage_pieces_contract(movies)
    stage_piece_states = [
        state for piece in stage_pieces for state in piece["states"]
    ]
    piece_usage_names = _stage_piece_usage_names(stage_pieces)
    for key, symbols in _placement_usage_names(movies).items():
        piece_usage_names.setdefault(key, set()).update(symbols)
    atlas_pieces = _atlas_piece_manifest(movies, piece_usage_names)
    contract: dict[str, Any] = {
        "schema": OUTPUT_SCHEMA,
        "schemaVersion": 0,
        "sceneId": SCENE_ID,
        "authoredResolution": [1024, 768],
        "renderPolicy": {
            "actionScriptExecuted": False,
            "boundedActionScriptSubsetExecuted": True,
            "boundedClipActionSubsetExecuted": True,
            "boundedInitialSetupApplied": True,
            "defaultRuntimeMode": "fail-closed",
            "staticSubsetRequiresExplicitOptIn": True,
            "syntheticFallbackAllowed": False,
            "exactTimelineDisplayLists": True,
            "timelinePlaybackBound": False,
            "exactExternalFontLoadingBound": True,
            "exactLiveTextBindingsBound": True,
            "exactUnifiedDisplayOrder": True,
            "exactExternalMovieChildSlotsBound": True,
            "exactResourceFlashActionBound": resource_flash is not None,
            "exactSideCommandTopologyBound": True,
            "exactMenFordsSideCommandFadeRuntimeBound": True,
            "exactPalantirCommandRegistrationsBound": True,
            "resourceFlashNativeTriggerCapturePassed": False,
            "resourceFlashMixerOverlapCapturePassed": False,
            "externalMovieLifecycleCapturePassed": False,
            "renderedTextParityCapturePassed": False,
            "exactWndCompanionBound": wnd_companion is not None,
            "wndLiveDispatchBound": False,
            "wndRenderServicesBound": False,
        },
        "source": dict(source),
        "wndCompanion": dict(wnd_companion) if wnd_companion is not None else None,
        "runtimeAssetBindings": {"externalFonts": font_bindings},
        "frameSelection": frame_selection,
        "layers": list(MOVIE_ORDER),
        "externalMovieLoads": external_movie_loads,
        "externalMovieAttachments": external_movie_attachments,
        "externalMovieLifecycle": {
            "initialSetupLoadOrder": [
                "InGameSpellBook",
                "InGameSideCommandBar",
                "InGameHelpBox",
                "InGameHeroSelect",
                "InGamePlanningMode",
            ],
            "blockedTargetLoadOrder": [
                str(row["movie"]) for row in external_movie_attachments
            ],
            "nativeRetainedSlots": {
                "HeroSelectUI": "+0xc4",
                "helpBox": "+0xc8",
                "planningModeUI": "+0xcc",
            },
            "nativeResetClearOrder": [
                "HeroSelectUI",
                "helpBox",
                "planningModeUI",
            ],
            "nativeResetSha256": "caa92439a63eac781e297a16ace1e3f48e79abe8750f6b3b1e8a5637d6a61587",
            "spellBookResetPath": "separate-fscommand-relative-order-unresolved",
            "runtimeLoadPolicy": "atomic-authored-issue-order-without-callback-dispatch",
            "runtimeResetPolicy": "atomic-clear-without-synthetic-unload-dispatch",
        },
        "resourceFlash": resource_flash,
        "sideCommandTopology": side_command_topology,
        "sideCommandFadeRuntime": side_command_fade_runtime,
        "palantirCommandTopology": palantir_command_topology,
        "sourceDiagnostics": {
            "flaggedNullClipActionPointers": flagged_null_records,
        },
        "atlases": atlas_paths,
        "atlasPieces": atlas_pieces,
        "draws": flattener.draws,
        "stagePieces": stage_pieces,
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
            "timelineCount": len(timelines),
            "timelineFrameCount": timeline_frame_count,
            "timelineInstanceCount": len(flattener.timeline_instances),
            "actionScriptCount": len(action_programs),
            "supportedActionScriptCount": supported_action_count,
            "unsupportedActionScriptCount": len(action_programs)
            - supported_action_count,
            "typedSideCommandActionScriptCount": len(
                side_command_topology["typedScriptIds"]
            ),
            "typedMenFordsSideCommandFadeRuntimeCount": 1,
            "typedPalantirCommandActionScriptCount": len(
                palantir_command_topology["typedScriptIds"]
            ),
            "vmBytecodeProgramCount": len(vm_bytecode_programs),
            "vmConstantsMovieCount": len(vm_constants),
            "clipActionProgramCount": len(clip_action_programs),
            "supportedClipActionProgramCount": sum(
                bool(program["supported"]) for program in clip_action_programs
            ),
            "clipActionCount": len(clip_actions),
            "clipActionEventCount": clip_action_event_count,
            "executableClipActionEventCount": executable_clip_action_event_count,
            "fontCount": len(font_definitions),
            "embeddedFontGlyphCount": sum(
                int(font["glyphCount"]) for font in font_definitions
            ),
            "textCount": len(text_definitions),
            "textInstanceCount": len(text_instances),
            "displayItemCount": flattener.display_order,
            "buttonCount": len(button_definitions),
            "buttonInstanceCount": len(button_instances),
            "buttonActionCount": sum(
                len(button["actions"]) for button in button_definitions
            ),
            "externalMovieLoadCount": len(external_movie_loads),
            "externalMovieAttachmentBlockerCount": sum(
                row["runtimeAttachment"] != "already-bound-root-layer"
                and row["runtimeAttachment"] != "exact-palantir-child-slot-bound"
                for row in external_movie_loads
            ),
            "externalMovieAttachmentCount": len(external_movie_attachments),
            "externalMovieLifecycleCaptureBlockerCount": sum(
                blocker["code"] == "external-movie-lifecycle-capture-not-passed"
                for blocker in blockers
            ),
            "externalFontBindingCount": len(font_bindings),
            "flaggedNullClipActionPointerCount": len(flagged_null_records),
            "wndCompanionBound": wnd_companion is not None,
            "wndTypedCallbackCount": 15 if wnd_companion is not None else 0,
            "wndRequiredMessageCallbackCount": 5 if wnd_companion is not None else 0,
            "wndRequiredMessageUnimplementedCount": 0,
            "wndUnresolvedBuiltinCount": 4 if wnd_companion is not None else 0,
            "stagePieceCount": len(stage_pieces),
            "stagePieceStateCount": len(stage_piece_states),
            "stagePieceDrawCount": sum(
                len(state["draws"]) for state in stage_piece_states
            ),
            "stagePieceArtlessCount": sum(
                bool(piece["artless"]) for piece in stage_pieces
            ),
            "stagePieceReceiptCount": sum(
                len(piece["receipts"]) for piece in stage_pieces
            )
            + sum(len(state["receipts"]) for state in stage_piece_states),
            "atlasPieceCount": len(atlas_pieces),
            "atlasPieceNamedCount": sum(
                bool(piece["names"]) for piece in atlas_pieces
            ),
            "staticSubsetReady": bool(flattener.draws),
            "parityReady": not blockers and bool(flattener.draws),
        },
    }
    contract["aggregateSha256"] = canonical_sha256(contract)
    return contract


def _source_mapping(
    sources: Mapping[str, Path | str | bytes | bytearray],
) -> tuple[dict[str, bytes], dict[str, str]]:
    payloads: dict[str, bytes] = {}
    names: dict[str, str] = {}
    for raw_name, value in sources.items():
        name = str(raw_name).replace("\\", "/").strip("/")
        key = name.casefold()
        if (
            not name
            or key in payloads
            or any(part in {"", ".", ".."} for part in name.split("/"))
        ):
            raise HudAptConvertError(
                f"invalid or duplicate bundle virtual path: {raw_name!r}"
            )
        if isinstance(value, (bytes, bytearray)):
            data = bytes(value)
        else:
            path = Path(value)
            if not path.is_file() or path.is_symlink():
                raise HudAptConvertError(f"bundle source is missing or linked: {name}")
            data = path.read_bytes()
        payloads[key] = data
        names[key] = name
    return payloads, names


def _external_font_payloads(
    sources: Mapping[str, Path | str | bytes | bytearray],
    bindings: Iterable[Mapping[str, Any]],
    *,
    external_font_sources: Mapping[
        str, Path | str | bytes | bytearray
    ] | None = None,
) -> dict[str, bytes]:
    """Find exact external font payloads beside the sealed 261-source tree.

    The font stays outside the APT source aggregate, whose source count and
    aggregate are already sealed. Paths are derived only by stripping each
    known virtual path from its physical source and appending the exact font
    virtual path; byte-backed callers therefore fail closed.
    """

    result: dict[str, bytes] = {}
    for binding in bindings:
        virtual = str(binding["sourceVirtualPath"]).replace("\\", "/").strip("/")
        payload_candidates: list[bytes] = []
        if external_font_sources is not None:
            for raw_virtual, raw_source in external_font_sources.items():
                normalized = str(raw_virtual).replace("\\", "/").strip("/")
                if normalized.casefold() != virtual.casefold():
                    continue
                if isinstance(raw_source, (bytes, bytearray)):
                    payload_candidates.append(bytes(raw_source))
                    continue
                source_path = Path(raw_source)
                if source_path.is_file() and not source_path.is_symlink():
                    payload_candidates.append(source_path.read_bytes())
        else:
            candidates: set[Path] = set()
            for raw_virtual, raw_source in sources.items():
                if isinstance(raw_source, (bytes, bytearray)):
                    continue
                source_path = Path(raw_source)
                parts = Path(
                    *str(raw_virtual).replace("\\", "/").strip("/").split("/")
                )
                folded_source = [part.casefold() for part in source_path.parts]
                folded_virtual = [part.casefold() for part in parts.parts]
                if len(folded_source) < len(folded_virtual):
                    continue
                if folded_source[-len(folded_virtual) :] != folded_virtual:
                    continue
                root = source_path
                for _part in parts.parts:
                    root = root.parent
                candidate = root / Path(*virtual.split("/"))
                if candidate.is_file() and not candidate.is_symlink():
                    candidates.add(candidate.resolve())
            payload_candidates.extend(path.read_bytes() for path in candidates)
        if len(payload_candidates) != 1:
            raise HudAptConvertError(
                f"exact external font source could not be resolved: {virtual}"
            )
        payload = payload_candidates[0]
        if len(payload) != 24_712 or _sha(payload) != str(binding["sourceSha256"]):
            raise HudAptConvertError("exact external font source identity changed")
        result[str(binding["fontId"])] = payload
    return result


def _required_bundle_contract(
    payloads: Mapping[str, bytes], names: Mapping[str, str]
) -> tuple[list[dict[str, Any]], dict[str, Any], list[dict[str, Any]]]:
    required: set[str] = set()
    movies: list[dict[str, Any]] = []
    atlas_rows: list[dict[str, Any]] = []
    for movie_name in MOVIE_CLOSURE:
        triplet: dict[str, tuple[str, bytes]] = {}
        for suffix in ("apt", "const", "dat"):
            key = f"{movie_name}.{suffix}".casefold()
            if key not in payloads:
                raise HudAptConvertError(
                    f"bundle source is missing: {movie_name}.{suffix}"
                )
            required.add(key)
            triplet[suffix] = (names[key], payloads[key])
        constants = parse_apt_constants(triplet["const"][1], triplet["const"][0])
        apt = parse_apt_movie(triplet["apt"][1], constants, triplet["apt"][0])
        image_map = parse_apt_dat(triplet["dat"][1], triplet["dat"][0])
        geometry_rows: list[dict[str, Any]] = []
        geometry_ids = sorted(
            {
                int(character["geometryId"])
                for character in apt["characters"]
                if character.get("kind") == "shape"
            }
        )
        for geometry_id in geometry_ids:
            key = f"{movie_name}_geometry/{geometry_id}.ru".casefold()
            if key not in payloads:
                raise HudAptConvertError(
                    f"bundle source is missing: {movie_name}_geometry/{geometry_id}.ru"
                )
            required.add(key)
            geometry_rows.append(parse_apt_geometry(payloads[key], names[key]))
        texture_ids = sorted({int(row["textureId"]) for row in image_map["mappings"]})
        movie_atlases: list[dict[str, Any]] = []
        for texture_id in texture_ids:
            key = f"art/textures/apt_{movie_name}_{texture_id}.tga".casefold()
            if key not in payloads:
                raise HudAptConvertError(
                    f"bundle source is missing: art/Textures/apt_{movie_name}_{texture_id}.tga"
                )
            required.add(key)
            atlas = parse_tga_identity(payloads[key], names[key])
            slug = re.sub(r"[^a-z0-9]+", "", movie_name.casefold())
            atlas["cookedPng"] = (
                f"assets/ui/palantir/atlases/apt-{slug}-{texture_id}-"
                f"{atlas['sha256'][:12]}.png"
            )
            movie_atlases.append(atlas)
            atlas_rows.append(atlas)
        movies.append(
            {
                "movie": movie_name,
                "apt": apt,
                "constants": constants,
                "geometry": geometry_rows,
                "imageMap": image_map,
                "atlases": movie_atlases,
            }
        )
    wnd_key = "window/controlbar.wnd"
    if wnd_key not in payloads:
        raise HudAptConvertError("bundle source is missing: window/controlbar.wnd")
    required.add(wnd_key)
    wnd = parse_wnd_layout(payloads[wnd_key], names[wnd_key])
    if set(payloads) != required:
        extras = sorted(set(payloads) - required)
        missing = sorted(required - set(payloads))
        raise HudAptConvertError(
            f"HUD APT bundle closure changed: extras={extras!r} missing={missing!r}"
        )
    return movies, wnd, atlas_rows


def _wnd_companion_contract(wnd: Mapping[str, Any]) -> dict[str, Any]:
    if (
        wnd.get("sha256") != _WND_SOURCE_SHA256
        or int(wnd.get("windowCount", -1)) != 87
    ):
        raise HudAptConvertError("active ControlBar.wnd identity changed")
    bindings: dict[str, list[str]] = {}
    for window in wnd.get("windows", []):
        if not isinstance(window, Mapping):
            raise HudAptConvertError("ControlBar.wnd window inventory changed")
        callbacks = window.get("callbacks", {})
        if not isinstance(callbacks, Mapping):
            raise HudAptConvertError("ControlBar.wnd callback inventory changed")
        for kind, callback in callbacks.items():
            if callback is None:
                continue
            name = str(callback)
            bindings.setdefault(name, []).append(
                f"{kind}|{int(window['index'])}|{window['name']}|{window['windowType']}"
            )
    outside_slice = {
        "BeaconWindowInput": "event-dormant",
        "ControlBarObserverSystem": "outside declared player-v-player slice",
    }
    expected = set(_WND_IMPLEMENTED_CALLBACKS) | set(outside_slice) | set(
        _WND_UNRESOLVED_BUILTINS
    )
    if set(bindings) != expected or len(bindings) != 21:
        raise HudAptConvertError("ControlBar.wnd frozen callback closure changed")
    return {
        "schema": "openbfme.retail-hud-wnd-companion",
        "schemaVersion": 0,
        "source": {
            "virtualPath": "window/controlbar.wnd",
            "sha256": _WND_SOURCE_SHA256,
            "windowCount": 87,
            "callbackCount": 21,
            "activationAuthority": "active-companion-not-candidate-dead",
        },
        "oracleAggregates": dict(_WND_ORACLE_AGGREGATES),
        "callbackBindings": {
            name: rows for name, rows in sorted(bindings.items(), key=lambda row: row[0])
        },
        "runtimeInventory": {
            "implementedCallbacks": list(_WND_IMPLEMENTED_CALLBACKS),
            "implementedCallbackCount": 15,
            "requiredMessageCallbacks": list(_WND_REQUIRED_MESSAGE_CALLBACKS),
            "requiredMessageCallbackCount": 5,
            "requiredMessageUnimplemented": [],
            "outsideSlice": outside_slice,
            "unresolvedBuiltins": list(_WND_UNRESOLVED_BUILTINS),
        },
        "dynamicGates": {
            "drawAndService": list(_WND_DRAW_SERVICE_GATES),
            "messageAliases": list(_WND_MESSAGE_ALIAS_GATES),
        },
        "liveBinding": {
            "callbackDispatchBound": False,
            "renderServicesBound": False,
            "genericDispatchAllowed": False,
            "fallbackVisualsAllowed": False,
        },
    }


def _wnd_companion_blockers() -> tuple[dict[str, Any], ...]:
    return (
        {
            "code": "wnd-unresolved-runtime-builtins-not-bound",
            "movie": "controlbar.wnd",
            "callbacks": list(_WND_UNRESOLVED_BUILTINS),
            "callbackCount": 4,
            "parityReady": False,
        },
        {
            "code": "wnd-dynamic-draw-service-capture-not-passed",
            "movie": "controlbar.wnd",
            "gates": list(_WND_DRAW_SERVICE_GATES),
            "gateCount": 7,
            "parityReady": False,
        },
        {
            "code": "wnd-live-dispatch-render-services-not-bound",
            "movie": "controlbar.wnd",
            "callbackDispatchBound": False,
            "renderServicesBound": False,
            "messageAliasGates": list(_WND_MESSAGE_ALIAS_GATES),
            "messageAliasGateCount": 7,
            "genericDispatchAllowed": False,
            "fallbackVisualsAllowed": False,
            "parityReady": False,
        },
    )


def convert_hud_apt_bundle(
    sources: Mapping[str, Path | str | bytes | bytearray],
    output_directory: Path | str,
    *,
    expected_source_aggregate_sha256: str | None = None,
    external_fonts: Iterable[Mapping[str, Any]] = (),
    external_font_sources: Mapping[
        str, Path | str | bytes | bytearray
    ] | None = None,
    retail_ini_sources: Mapping[
        str, Path | str | bytes | bytearray
    ] | None = None,
) -> dict[str, Any]:
    """Convert an exact 261-source bundle without depending on the large plan.

    Production supplies the expected canonical source aggregate plus the closed
    exact external-font binding. Atlas output paths derive solely from the
    retail movie/texture identity and source hash.
    """

    font_bindings = _validated_external_font_bindings(external_fonts)
    font_payloads = _external_font_payloads(
        sources,
        font_bindings,
        external_font_sources=external_font_sources,
    )
    payloads, names = _source_mapping(sources)
    raw_movies, wnd, atlas_rows = _required_bundle_contract(payloads, names)
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
        raise HudAptConvertError("HUD APT source aggregate changed")
    movies = [_movie_from_plan(raw, source_bytes=payloads) for raw in raw_movies]
    wnd_companion = _wnd_companion_contract(wnd)
    if retail_ini_sources is None:
        side_source = next(
            (
                value
                for virtual_path, value in sources.items()
                if str(virtual_path).replace("\\", "/").strip("/").casefold()
                == "ingamesidecommandbar.apt"
            ),
            None,
        )
        if isinstance(side_source, (bytes, bytearray)) or side_source is None:
            raise HudAptConvertError(
                "HUD APT production conversion requires explicit retail INI sources"
            )
        retail_root = Path(side_source).resolve().parent
        retail_ini_sources = {
            relative: retail_root.joinpath(*relative.split("/"))
            for relative in PRODUCTION_MEN_FORDS_RETAIL_INI_SHA256
        }
    contract = _make_contract(
        movies,
        {
            "sourceAggregateSha256": source_aggregate,
            "sourceCount": len(inventory),
            "sourcePayloadBytes": sum(row["byteLength"] for row in inventory),
            "controlbarWndSha256": wnd["sha256"],
        },
        retail_ini_sources,
        _wnd_companion_blockers(),
        external_fonts=font_bindings,
        wnd_companion=wnd_companion,
    )
    output = Path(output_directory)
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise HudAptConvertError("HUD APT output directory must be empty")
    output.mkdir(parents=True, exist_ok=True)
    try:
        import PIL
        from PIL import Image
    except ImportError as exc:
        raise HudAptConvertError("Pillow is required for HUD atlas conversion") from exc
    if PIL.__version__ != "12.2.0":
        raise HudAptConvertError(
            f"Pillow 12.2.0 is required for deterministic HUD atlases; found {PIL.__version__}"
        )
    outputs: list[Path] = []
    for atlas in atlas_rows:
        key = str(atlas["virtualPath"]).casefold()
        target = output / Path(*str(atlas["cookedPng"]).split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(BytesIO(payloads[key])) as opened:
            opened.convert("RGBA").save(
                target, format="PNG", compress_level=9, optimize=False
            )
        outputs.append(target)
    for piece in contract.get("atlasPieces", []):
        # The authored per-image split: one PNG per image, cropped to the
        # movie's own UV footprint on its sheet (owner 2026-08-26: "each icon
        # or asset gets its own thing"). The crop's content hash is stamped
        # into the contract row it came from.
        atlas_source = output / Path(*str(piece["atlas"]).split("/"))
        target = output / Path(*str(piece["croppedPng"]).split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        x, y, w, h = (int(value) for value in piece["rect"])
        with Image.open(atlas_source) as opened:
            opened.convert("RGBA").crop((x, y, x + w, y + h)).save(
                target, format="PNG", compress_level=9, optimize=False
            )
        piece["sha256"] = _sha(target.read_bytes())
        outputs.append(target)
    for binding in font_bindings:
        target = output / Path(*str(binding["cookedFont"]).split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        payload = font_payloads[str(binding["fontId"])]
        target.write_bytes(payload)
        if _sha(target.read_bytes()) != str(binding["sourceSha256"]):
            raise HudAptConvertError("copied external font identity changed")
        outputs.append(target)
    contract_path = output / "data/ui/palantir/scene-contract.json"
    contract_path.parent.mkdir(parents=True, exist_ok=True)
    contract_path.write_bytes(_canonical_bytes(contract))
    outputs.append(contract_path)
    return contract


def convert_hud_apt(
    plan_path: Path | str, asset_root: Path | str, output_path: Path | str
) -> dict[str, Any]:
    plan_file = Path(plan_path)
    root = Path(asset_root)
    destination = Path(output_path)
    plan = json.loads(plan_file.read_text(encoding="utf-8"))
    if plan.get("schema") != PLAN_SCHEMA or plan.get("schemaVersion") != 0:
        raise HudAptConvertError("HUD APT plan schema changed")
    scene = plan.get("sceneContract")
    if not isinstance(scene, Mapping) or scene.get("sceneId") != SCENE_ID:
        raise HudAptConvertError("HUD APT scene identity changed")
    raw_movies = scene.get("movies")
    if not isinstance(raw_movies, list):
        raise HudAptConvertError("HUD APT movie contract changed")
    movies_list = [
        _movie_from_plan(item, root) for item in raw_movies if isinstance(item, Mapping)
    ]
    runtime_bindings = scene.get("runtimeAssetBindings", {})
    if not isinstance(runtime_bindings, Mapping):
        raise HudAptConvertError("HUD APT runtime asset bindings changed")
    external_fonts = runtime_bindings.get("externalFonts", [])
    if not isinstance(external_fonts, list):
        raise HudAptConvertError("HUD APT external font bindings changed")
    retail_ini_sources = {
        relative: root.joinpath(*relative.split("/"))
        for relative in PRODUCTION_MEN_FORDS_RETAIL_INI_SHA256
    }
    contract = _make_contract(
        movies_list,
        {
            "planAggregateSha256": str(plan["aggregateSha256"]),
            "planFileSha256": _sha(plan_file.read_bytes()),
            "sceneContractSha256": str(scene["aggregateSha256"]),
        },
        retail_ini_sources,
        external_fonts=external_fonts,
    )
    encoded = _canonical_bytes(contract)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and destination.read_bytes() == encoded:
        return contract
    destination.write_bytes(encoded)
    return contract


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--asset-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    contract = convert_hud_apt(args.plan, args.asset_root, args.output)
    print(
        json.dumps(
            {
                "output": str(args.output),
                "aggregateSha256": contract["aggregateSha256"],
                "summary": contract["summary"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
