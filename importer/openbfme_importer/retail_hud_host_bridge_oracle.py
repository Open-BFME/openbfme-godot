"""Build the payload-free Men/Fords HUD host-bridge oracle contract.

This is an observation tool, not an ActionScript runtime.  It consumes the
already-decoded retail scene contract plus controlbar.wnd, verifies the exact
bounded blocker set, and emits only identities, hashes, ordering, and an
allowlisted bridge proposal.  Retail payload bytes are never copied out.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable

from .sage_apt import parse_wnd_layout


SCHEMA = "openbfme.private-hud-host-bridge-oracle"

SCRIPT_MAP: dict[str, dict[str, Any]] = {
    "ingamesidecommandbar:3392": {
        "role": "side-command-bar module and load lifecycle",
        "mapping": "allowlisted local functions plus lifecycle notifications",
        "state": ["_global.InGame", "_currentframe"],
        "host": [
            "OnAptInGameSideCommandBarLoaded",
            "OnAptInGameSideCommandBarUnloaded",
        ],
        "unresolved": [],
    },
    "ingamesidecommandbar:6264": {
        "role": "button-frame neighbor state and load lifecycle",
        "mapping": "deterministic clip hierarchy traversal and lifecycle notifications",
        "state": ["clip._parent", "clip._name", "Button.length"],
        "host": [
            "OnAptInGameSideCommandBarButtonFrameLoaded",
            "OnAptInGameSideCommandBarButtonFrameUnloaded",
        ],
        "unresolved": [],
    },
    "ingamesidecommandbar:6272": {
        "role": "invoke UpdateNeighborFrameStates with zero arguments",
        "mapping": "allowlisted local function call",
        "state": [],
        "host": [],
        "unresolved": [],
    },
    "ingamesidecommandbar:6368": {
        "role": "invoke UpdateFrameState then UpdateNeighborFrameStates",
        "mapping": "allowlisted local function calls in authored order",
        "state": [],
        "host": [],
        "unresolved": [],
    },
    "ingamesidecommandbar:7296": {
        "role": "show side buttons 1 through 15",
        "mapping": "when _global.InGame is truthy call Button1..Button15.gotoAndPlay('_show')",
        "state": ["_global.InGame", "this.Button1..this.Button15"],
        "host": [],
        "unresolved": [],
    },
    "libingameui:37332": {
        "role": "dynamic command-button content lifecycle",
        "mapping": "allowlisted attach/remove movie adapter and parent frame notifications",
        "state": [
            "contentClip",
            "placeholder._x",
            "placeholder._y",
            "placeholder._width",
            "placeholder._height",
            "extern",
            "_ContentName",
            "initialized",
        ],
        "host": [],
        "unresolved": [
            "contentType/contentName are dynamic and require a converted-movie allowlist"
        ],
    },
    "palantir:152912": {
        "role": "minimum-LOD globe-swirl stop guard",
        "mapping": "stop when _global.MinLOD is true or Flash property 13 equals GlobeSwirlRender",
        "state": ["_global.MinLOD", "this[property:13]"],
        "host": [],
        "unresolved": [],
    },
    "palantir:167296": {
        "role": "skill-upgrade command-button initialization",
        "mapping": "root.UpdateSkillUpgradeButton(); when _global.InGame set states (1,_up), (2,_disabled), (4,_up), (5,_disabled)",
        "state": ["_global.InGame", "_root.UpdateSkillUpgradeButton"],
        "host": [],
        "unresolved": [],
    },
    "palantir:169224": {
        "role": "command-button child lifecycle notifications",
        "mapping": "six allowlisted PalantirCommandUI lifecycle notifications",
        "state": ["clip.index", "clip._name"],
        "host": [
            "PalantirCommandUI::OnButtonFrameLoaded",
            "PalantirCommandUI::OnButtonFrameUnloaded",
            "PalantirCommandUI::OnSubMenuLoaded",
            "PalantirCommandUI::OnSubMenuUnloaded",
            "PalantirCommandUI::OnToggleFlashLoaded",
            "PalantirCommandUI::OnToggleFlashUnloaded",
        ],
        "unresolved": [],
    },
    "palantir:169256": {
        "role": "per-button overlay setters",
        "mapping": "bind SetAutoAbilityOverlayState, SetFlashEffectState, and SetGlassState to exact named child clips",
        "state": [
            "_parent.AutoAbilityOverlays[_name]",
            "_parent.FlashEffects[_name]",
            "_parent.glass",
        ],
        "host": [],
        "unresolved": [],
    },
    "palantir:332504": {
        "role": "resource-bar flash audio",
        "mapping": "play timeline and call _root.PlaySound('Gui_PalantirResourceBarFlash')",
        "state": ["_root.PlaySound"],
        "host": ["PlaySound"],
        "unresolved": [],
    },
    "palantir:333872": {
        "role": "minimum-LOD effect visibility",
        "mapping": "when _global.MinLOD is truthy set effect1._visible and effect4._visible to false",
        "state": ["_global.MinLOD", "effect1._visible", "effect4._visible"],
        "host": [],
        "unresolved": [],
    },
    "palantir:334840": {
        "role": "minimum-LOD effect visibility",
        "mapping": "when _global.MinLOD is truthy set effect2._visible and effect3._visible to false",
        "state": ["_global.MinLOD", "effect2._visible", "effect3._visible"],
        "host": [],
        "unresolved": [],
    },
    "palantir:95848": {
        "role": "globe-UI collection and hero-select state",
        "mapping": "allowlisted named clip state transitions driven by deterministic HUD selection state",
        "state": [
            "GlobeUIs",
            "CommandUI",
            "_global.InGame",
            "HeroSelectUI.Hero",
        ],
        "host": [],
        "unresolved": ["extern globe render ownership requires the renderer adapter"],
    },
    "palantir:95856": {
        "role": "Palantir public HUD API",
        "mapping": "allowlisted facade over clip state, radar pings, resources, buttons, progress, and audio",
        "state": [
            "CommandUI",
            "RankUI",
            "CostModifierUpgradeUI",
            "PalantirButtons.Buttons",
            "PlayerMagic.ProgressBar",
            "Radar.RadarPings",
            "ResourceBar.ResourceIcon",
            "PlayerFactionIcon",
            "ObserverStuff",
        ],
        "host": ["PlaySound"],
        "unresolved": [
            "radar ping projection requires authoritative world-to-radar coordinates"
        ],
    },
    "palantir:95864": {
        "role": "host gateway and external HUD movie bootstrap",
        "mapping": "closed FSCommand gateway plus exact movie-to-target loader",
        "state": ["extern", "_global.InGame", "PalantirMinLOD", "MinLOD"],
        "host": [
            "AptPalantir::OnHelpBoxLoaded",
            "AptPalantir::OnHelpBoxUnloaded",
            "AptPalantir::OnHeroSelectLoaded",
            "AptPalantir::OnHeroSelectUnloaded",
            "AptPalantir::OnPlanningModeUILoaded",
            "AptPalantir::OnPlanningModeUIUnloaded",
            "AptPalantir::OnPlanningModeButtonLoaded",
            "AptPalantir::OnPlanningModeButtonUnloaded",
            "AptPalantir::OnInitialized",
            "PlaySound",
        ],
        "unresolved": [
            "InGameSpellBook.swf -> SpellBookUI",
            "InGameSideCommandBar.swf -> SideCommandBar",
            "InGameHelpBox.swf -> helpBox",
            "InGameHeroSelect.swf -> HeroSelectUI",
            "InGamePlanningMode.swf -> planningModeUI",
        ],
    },
    "palantir:95872": {
        "role": "initial exact child/button setup",
        "mapping": "deterministic initial state assignment and CreateContent calls from authored constants",
        "state": [
            "_global.InGame",
            "PalantirButtons.Buttons",
            "CommandButtons",
            "FlashEffects",
            "AutoAbilityOverlays",
            "RadarPings",
        ],
        "host": [],
        "unresolved": ["dynamic CommandButton content requires the converted-movie allowlist"],
    },
}

HOST_CALLS = [
    {"name": "OnAptInGameSideCommandBarLoaded", "argument": "GetFullName(this)", "effect": "lifecycle notification"},
    {"name": "OnAptInGameSideCommandBarUnloaded", "argument": "GetFullName(this)", "effect": "lifecycle notification"},
    {"name": "OnAptInGameSideCommandBarButtonFrameLoaded", "argument": "index=this._name.substr(namePrefixLength)&name=clip.toString()", "effect": "increment exact side-button readiness"},
    {"name": "OnAptInGameSideCommandBarButtonFrameUnloaded", "argument": "index=this._name.substr(namePrefixLength)", "effect": "lifecycle notification; retail decrement semantics unresolved"},
    {"name": "PalantirCommandUI::OnButtonFrameLoaded", "argument": "index=clip._name&name=clip.toString()", "effect": "lifecycle notification"},
    {"name": "PalantirCommandUI::OnButtonFrameUnloaded", "argument": "index=clip._name", "effect": "lifecycle notification"},
    {"name": "PalantirCommandUI::OnSubMenuLoaded", "argument": "clip._name.substr(7)&name=clip.toString()", "effect": "lifecycle notification"},
    {"name": "PalantirCommandUI::OnSubMenuUnloaded", "argument": "clip._name.substr(7)", "effect": "lifecycle notification"},
    {"name": "PalantirCommandUI::OnToggleFlashLoaded", "argument": "clip._name.substr(11)&name=clip.toString()", "effect": "lifecycle notification"},
    {"name": "PalantirCommandUI::OnToggleFlashUnloaded", "argument": "clip._name.substr(11)", "effect": "lifecycle notification"},
    {"name": "AptPalantir::OnHelpBoxLoaded", "argument": "clip.toString()", "effect": "lifecycle notification"},
    {"name": "AptPalantir::OnHelpBoxUnloaded", "argument": "clip.toString()", "effect": "lifecycle notification"},
    {"name": "AptPalantir::OnHeroSelectLoaded", "argument": "clip.toString()", "effect": "lifecycle notification"},
    {"name": "AptPalantir::OnHeroSelectUnloaded", "argument": "clip.toString()", "effect": "lifecycle notification"},
    {"name": "AptPalantir::OnPlanningModeUILoaded", "argument": "clip.toString()", "effect": "lifecycle notification"},
    {"name": "AptPalantir::OnPlanningModeUIUnloaded", "argument": "clip.toString()", "effect": "lifecycle notification"},
    {"name": "AptPalantir::OnPlanningModeButtonLoaded", "argument": "clip.toString()", "effect": "lifecycle notification"},
    {"name": "AptPalantir::OnPlanningModeButtonUnloaded", "argument": "clip.toString()", "effect": "lifecycle notification"},
    {"name": "AptPalantir::OnInitialized", "argument": "empty string", "effect": "mark Palantir host initialized"},
    {"name": "PlaySound", "argument": "audio event name; proven call is Gui_PalantirResourceBarFlash", "effect": "route to retail audio-event adapter"},
]

RENDER_CALLBACKS = [
    "AptPalantir::ClipRadar",
    "AptPalantir::RenderGlobe",
    "AptPalantir::RenderMovie",
    "AptPalantir::RenderRadar",
    "AptPalantir::RenderRadarViewBox",
]

WND_CALLBACK_MAP: dict[str, dict[str, str]] = {
    "BeaconWindowInput": {"status": "unresolved", "proposal": "typed beacon editor input adapter", "risk": "ui-and-network-side-effect"},
    "ControlBarInput": {"status": "unresolved", "proposal": "typed BFME2 control-bar input adapter", "risk": "gameplay-command-side-effect"},
    "ControlBarObserverSystem": {"status": "unresolved", "proposal": "typed observer selection/statistics adapter", "risk": "observer-state-side-effect"},
    "ControlBarSystem": {"status": "unresolved", "proposal": "typed BFME2 selected-button dispatcher", "risk": "gameplay-and-menu-side-effect"},
    "GameWinBlockInput": {"status": "deterministic-proposal", "proposal": "consume the addressed input without another action", "risk": "input-only"},
    "GameWinDefaultInput": {"status": "unresolved", "proposal": "shared typed default WND input behavior", "risk": "ui-side-effect"},
    "GameWinDefaultSystem": {"status": "unresolved", "proposal": "shared typed default WND system behavior", "risk": "ui-side-effect"},
    "GameWinDefaultTooltip": {"status": "unresolved", "proposal": "localized tooltip resolver from exact control state", "risk": "read-only-ui"},
    "LeftHUDInput": {"status": "unresolved", "proposal": "typed minimap input using authoritative radar-to-world projection", "risk": "gameplay-order-and-camera-side-effect"},
    "PassSelectedButtonsToParentSystem": {"status": "deterministic-proposal", "proposal": "forward only SelectedButton to the exact parent system callback", "risk": "mediated-ui-side-effect"},
    "W3DCommandBarBackgroundDraw": {"status": "unresolved", "proposal": "typed retail command-bar background draw", "risk": "render-only"},
    "W3DCommandBarForegroundDraw": {"status": "unresolved", "proposal": "typed retail command-bar foreground draw", "risk": "render-only"},
    "W3DCommandBarGenExpDraw": {"status": "unresolved", "proposal": "typed experience-progress draw", "risk": "render-only"},
    "W3DCommandBarGridDraw": {"status": "unresolved", "proposal": "typed command-grid draw", "risk": "render-only"},
    "W3DCommandBarTopDraw": {"status": "unresolved", "proposal": "typed top-overlay draw", "risk": "render-only"},
    "W3DGadgetPushButtonImageDraw": {"status": "deterministic-proposal", "proposal": "shared push-button image draw from exact WND image/state", "risk": "render-only"},
    "W3DGameWinDefaultDraw": {"status": "unresolved", "proposal": "shared default WND draw after draw-data conversion", "risk": "render-only"},
    "W3DLeftHUDDraw": {"status": "unresolved", "proposal": "typed radar/minimap draw", "risk": "privileged-world-render"},
    "W3DNoDraw": {"status": "deterministic-proposal", "proposal": "intentional no-op draw", "risk": "none"},
    "W3DPowerDraw": {"status": "unresolved", "proposal": "typed power-state draw", "risk": "render-only"},
    "W3DRightHUDDraw": {"status": "unresolved", "proposal": "typed production/right-HUD draw", "risk": "render-only"},
}


def _canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _walk(instructions: Iterable[dict[str, Any]]) -> Iterable[dict[str, Any]]:
    for instruction in instructions:
        yield instruction
        yield from _walk(instruction.get("body", []))


def _script_evidence(row: dict[str, Any]) -> dict[str, Any]:
    strings: set[str] = set()
    functions: list[dict[str, Any]] = []
    for instruction in _walk(row["instructions"]):
        for constant in instruction.get("constants", []):
            if constant.get("type") == 1:
                strings.add(str(constant["value"]))
        operand = instruction.get("operand")
        if isinstance(operand, str):
            strings.add(operand)
        if instruction["name"] in {"define-function", "define-function2"}:
            functions.append(
                {
                    "name": str(instruction.get("functionName", "")),
                    "parameters": [
                        str(parameter["name"])
                        for parameter in instruction.get("parameters", [])
                    ],
                }
            )
    return {
        "scriptId": row["scriptId"],
        "movie": row["movie"],
        "sourceOffset": row["sourceOffset"],
        "sha256": row["sha256"],
        "exactStrings": sorted(strings, key=lambda value: (value.casefold(), value)),
        "functions": functions,
        **SCRIPT_MAP[row["scriptId"]],
    }


def _clip_effect(program_id: str) -> dict[str, Any]:
    effects = {
        "ingamesidecommandbar:clip-event:13680": {
            "effect": "define this.SetFlashEffectState(state) to call this._parent._parent.flashEffects[this._parent._name].gotoAndPlay(state)",
            "classification": "timeline-local-method-binding",
        },
        "libingameui:clip-event:56252": {
            "effect": "set Flash property 7 (_visible) on the empty-string target to false",
            "classification": "clip-property-write",
        },
        "palantir:clip-event:374220": {
            "effect": "set variable _type to AptPalantir::RenderMovie",
            "classification": "privileged-renderer-binding",
        },
        "palantir:clip-event:375628": {
            "effect": "set this.stringName to $PalantirResources",
            "classification": "localized-string-binding",
        },
        "palantir:clip-event:375640": {
            "effect": "set this.stringName to $PalantirResourceMultiplier",
            "classification": "localized-string-binding",
        },
        "palantir:clip-event:375652": {
            "effect": "set this.stringName to $PalantirCommandPoints",
            "classification": "localized-string-binding",
        },
    }
    return effects[program_id]


def build_contract(
    scene_contract: dict[str, Any],
    wnd_payload: bytes,
    *,
    opensage_root: Path | None = None,
) -> dict[str, Any]:
    unsupported = [row for row in scene_contract["actionScripts"] if not row["supported"]]
    ids = {row["scriptId"] for row in unsupported}
    if ids != set(SCRIPT_MAP) or len(unsupported) != 17:
        raise ValueError("HUD blocked ActionScript identity set changed")
    clip_actions = scene_contract["clipActions"]
    if len(clip_actions) != 28 or sum(len(row["events"]) for row in clip_actions) != 28:
        raise ValueError("HUD clip-action event set changed")
    program_ids = {
        event["programId"] for row in clip_actions for event in row["events"]
    }
    if program_ids != {
        "ingamesidecommandbar:clip-event:13680",
        "libingameui:clip-event:56252",
        "palantir:clip-event:374220",
        "palantir:clip-event:375628",
        "palantir:clip-event:375640",
        "palantir:clip-event:375652",
    }:
        raise ValueError("HUD clip-action program identity set changed")

    wnd = parse_wnd_layout(wnd_payload, "window/controlbar.wnd")
    if wnd["windowCount"] != 87:
        raise ValueError("controlbar.wnd window count changed")
    callback_controls: dict[str, list[dict[str, Any]]] = {}
    for window in wnd["windows"]:
        for kind, callback in window["callbacks"].items():
            if callback is not None:
                callback_controls.setdefault(callback, []).append(
                    {
                        "kind": kind,
                        "windowIndex": window["index"],
                        "controlId": window["name"],
                    }
                )
    if len(callback_controls) != 21:
        raise ValueError("controlbar.wnd callback identity set changed")
    if set(callback_controls) != set(WND_CALLBACK_MAP):
        raise ValueError("controlbar.wnd callback mapping set changed")

    clip_events = []
    for row in clip_actions:
        for event in row["events"]:
            clip_events.append(
                {
                    "blockerCode": row["blockerCode"],
                    "movie": row["movie"],
                    "targetPath": row["targetPath"],
                    "targetClipId": row["targetClipId"],
                    "event": event["eventNames"],
                    "dispatchOrder": event["dispatchOrder"],
                    "programId": event["programId"],
                    "recordSha256": event["recordSha256"],
                    **_clip_effect(event["programId"]),
                }
            )
    clip_events.sort(key=lambda row: (row["movie"].casefold(), row["targetPath"], row["programId"]))

    opensage_evidence = []
    if opensage_root is not None:
        for relative in (
            "src/OpenSage.Mods.Bfme/BfmeDefinition.cs",
            "src/OpenSage.Mods.Bfme2/Bfme2Definition.cs",
            "src/OpenSage.Mods.Bfme/Gui/Global.cs",
            "src/OpenSage.Mods.Bfme/Gui/AptPalantir.cs",
            "src/OpenSage.Mods.Bfme/Gui/AptControlBarSource.cs",
            "src/OpenSage.Mods.Bfme/Gui/PalantirCommandUI.cs",
            "src/OpenSage.Mods.Generals/Gui/ControlBarCallbacks.cs",
            "src/OpenSage.Game/Gui/Wnd/DefaultCallbacks.cs",
        ):
            path = opensage_root / relative
            if path.is_file():
                payload = path.read_bytes()
                opensage_evidence.append(
                    {"path": relative, "byteLength": len(payload), "sha256": _sha(payload)}
                )

    contract: dict[str, Any] = {
        "schema": SCHEMA,
        "source": {
            "sceneAggregateSha256": scene_contract["aggregateSha256"],
            "sceneSourceAggregateSha256": scene_contract["source"]["sourceAggregateSha256"],
            "controlbarWndSha256": wnd["sha256"],
            "opensageObservationOnly": opensage_evidence,
        },
        "summary": {
            "blockedActionScriptCount": 17,
            "clipEventCount": 28,
            "clipProgramCount": 6,
            "initializeEventCount": sum("initialize" in row["event"] for row in clip_events),
            "unloadEventCount": sum("unload" in row["event"] for row in clip_events),
            "wndWindowCount": 87,
            "wndCallbackIdentityCount": 21,
            "genericCallbackDispatchAllowed": False,
            "implementationIncluded": False,
        },
        "actionScripts": [_script_evidence(row) for row in unsupported],
        "clipEvents": clip_events,
        "hostCalls": HOST_CALLS,
        "rendererCallbacks": [
            {
                "name": name,
                "status": "unresolved-privileged-engine-render-callback",
                "genericDispatchAllowed": False,
            }
            for name in RENDER_CALLBACKS
        ],
        "wnd": {
            "activationGate": {
                "retailStaticEvidence": "ControlBarResizer INI names controlbar.wnd controls, so the file is not payload noise",
                "opensageObservation": "BFME2 selects AptControlBarSource; its AddToScene loads Palantir.apt, and no BFME/BFME2 source reference loads controlbar.wnd",
                "decision": "defer all WND callback implementation until a retail runtime trace proves controlbar.wnd is active in BFME2 skirmish",
                "status": "candidate-dead-for-vertical-slice-not-retail-proven",
            },
            "numericCommandIds": "not authored by controlbar.wnd; do not invent",
            "controls": [
                {
                    "windowIndex": window["index"],
                    "parentIndex": window["parentIndex"],
                    "controlId": window["name"],
                    "windowType": window["windowType"],
                    "callbacks": window["callbacks"],
                }
                for window in wnd["windows"]
            ],
            "callbackControls": [
                {
                    "callback": callback,
                    **WND_CALLBACK_MAP[callback],
                    "controls": callback_controls[callback],
                }
                for callback in sorted(callback_controls, key=lambda value: (value.casefold(), value))
            ],
            "status": "callback identities and control IDs exact; BFME2 message semantics unresolved unless separately proven",
        },
        "ordering": {
            "sourceProvenInitialize": [
                "create target clip",
                "assign authored instance name",
                "dispatch initialize clip action",
                "insert target into display list",
            ],
            "authoredFrameActions": "execute in timeline order; emit a loaded notification only where the authored script calls it",
            "unload": "event is exact, but timing relative to display-list removal is unresolved and must remain gated",
        },
        "security": [
            {"surface": "clip state", "classification": "deterministic-local", "policy": "allow exact target/property/method tuples only"},
            {"surface": "FSCommand", "classification": "host-side-effect", "policy": "closed name and argument-schema allowlist; reject dynamic unknown names"},
            {"surface": "PlaySound", "classification": "audio-side-effect", "policy": "resolve only cataloged retail audio-event IDs"},
            {"surface": "external movie", "classification": "resource-load", "policy": "exact converted movie and exact target allowlist; no paths or URLs"},
            {"surface": "renderer callback", "classification": "privileged-render", "policy": "typed native adapter only; never ActionScript reflection"},
            {"surface": "WND system/input", "classification": "gameplay-or-ui-mutation", "policy": "typed control/message adapter only after BFME semantics are proven"},
        ],
        "minimalRuntimeApi": [
            "read_hud_state(exact_key)",
            "set_clip_property(exact_target, exact_property, scalar_value)",
            "call_clip_method(exact_target, exact_method, typed_args)",
            "notify_hud_lifecycle(exact_callback, validated_param)",
            "play_retail_audio_event(cataloged_event_id)",
            "load_converted_hud_movie(exact_movie_id, exact_target)",
            "bind_typed_renderer(exact_callback, exact_target)",
            "dispatch_typed_wnd_message(exact_control_id, exact_callback, typed_message)",
        ],
        "deterministicGodotInputs": [
            "player alignment/faction",
            "selection and command-set state",
            "resource count and resource multiplier",
            "command points and progress values",
            "button enable/highlight/flash state",
            "HUD lifecycle and exact clip hierarchy",
            "configured minimum-LOD boolean",
        ],
        "unresolved": [
            "five AptPalantir renderer callbacks need typed render adapters",
            "five external HUD movies need verified converted closure and targets",
            "world-to-radar projection and radar view box need authoritative camera/map state",
            "21 WND callback identities need BFME2-specific typed semantics; OpenSAGE's observed ControlBarSystem is Generals-specific",
            "numeric WND command IDs are absent from controlbar.wnd",
            "exact unload-versus-display-list-removal timing is not proven by the static contract",
        ],
    }
    contract["actionScripts"].sort(key=lambda row: row["scriptId"])
    contract["aggregateSha256"] = _sha(_canonical(contract))
    return contract


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scene-contract", type=Path, required=True)
    parser.add_argument("--wnd", type=Path, required=True)
    parser.add_argument("--opensage-root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    scene = json.loads(args.scene_contract.read_text(encoding="utf-8"))
    result = build_contract(
        scene,
        args.wnd.read_bytes(),
        opensage_root=args.opensage_root,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(_canonical(result))
    print(json.dumps(result["summary"], sort_keys=True))
    print(result["aggregateSha256"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
