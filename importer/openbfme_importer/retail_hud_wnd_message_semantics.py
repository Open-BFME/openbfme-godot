"""Hash-pinned BFME2 semantics for slice-required WND message callbacks."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

from .retail_hud_wnd_activation_oracle import ASSETS, GAME_DAT_SHA256
from .retail_hud_wnd_callback_oracle import HANDLERS
from .sage_apt import parse_wnd_layout


SCHEMA = "openbfme.private-hud-wnd-message-semantics"
TEXT_DELTA = 0x400A00

RANGES = {
    "ControlBarInput": (
        0x4D43D0,
        0x4D43D3,
        "4bc724f3b1d0caf4fe369c18cba3102e6c4ea057f63fe1587e3973134a7f755e",
    ),
    "PassSelectedButtonsToParentSystem": (
        0x6C0978,
        0x6C09DF,
        "84d4c0731f143e6fe8f24c9b905598a4b1488be0887724c53b198a4bab08b4ec",
    ),
    "GameWinBlockInput": (
        0x71435A,
        0x7143B9,
        "40ad155d51e0f767340537a6eb7115a8d318b97fdc8890fe40d0f7c0d6c9f35c",
    ),
    "LeftHUDInput": (
        0x8020CE,
        0x802455,
        "c6fdeb00fffbabe8948133fd836baa9a5d3e47a51609c8abfbc693685b8403a4",
    ),
    "ControlBarSystem": (
        0x802455,
        0x802960,
        "d07f981cdf59ebc3867bece45df556c18f19875c1f96c5198ed41eb0e0121595",
    ),
}

SEMANTICS: dict[str, dict[str, Any]] = {
    "ControlBarInput": {
        "interface": "handle_control_bar_input(control, message, data1, data2) -> WndHandled",
        "returnContract": "always 0 (unhandled)",
        "states": [{"when": "all inputs", "effects": [], "return": 0}],
        "dependencies": [],
        "unresolved": [],
    },
    "GameWinBlockInput": {
        "interface": "block_control_bar_input(control, message, data1, data2) -> WndHandled",
        "returnContract": "0 only for messages 0x0015 and 0x0018; 1 otherwise",
        "states": [
            {"when": "message in {0x0015,0x0018}", "effects": [], "return": 0},
            {
                "when": "message == 0x0006",
                "effects": [
                    "call 0x00852354(global 0x00e03220,0)",
                    "call 0x0082fa01(global 0x00e03220)",
                    "call global 0x00dfea3c vslot 0x1a4 with 0",
                    "call global 0x00dfedf0 vslots 0xac and 0x6c with 0",
                ],
                "return": 1,
            },
            {"when": "all other messages", "effects": [], "return": 1},
        ],
        "dependencies": [
            "0x00e03220 input service",
            "0x00dfea3c control-bar service",
            "0x00dfedf0 game UI service",
        ],
        "unresolved": [
            "retail service class names behind the three globals are stripped; retain addresses/vslots rather than aliases"
        ],
    },
    "PassSelectedButtonsToParentSystem": {
        "interface": "forward_selected_button(control, message, data1, data2) -> WndHandled",
        "returnContract": "0 for null control, non-allowlisted message, or absent parent; otherwise exact parent callback return",
        "states": [
            {"when": "control == null", "effects": [], "return": 0},
            {
                "when": "message not in {0x4006,0x4007,0x4008,0x4009,0x400b,0x4031}",
                "effects": [],
                "return": 0,
            },
            {
                "when": "allowlisted message and parent from 0x007140a4 exists",
                "effects": [
                    "dispatch global 0x00dfef1c vslot 0xe8(parent,message,data1,data2)"
                ],
                "return": "forwarded EAX",
            },
            {
                "when": "allowlisted message but parent absent",
                "effects": [],
                "return": 0,
            },
        ],
        "dependencies": [
            "0x007140a4 exact parent resolver",
            "0x00dfef1c WND manager vslot 0xe8",
        ],
        "unresolved": [],
    },
    "LeftHUDInput": {
        "interface": "handle_radar_input(control, message, data1, data2, radar_state) -> WndHandled",
        "returnContract": "returns 1 for mode-gated input and recognized radar branches; returns 0 only through exact reject branch 0x0080212f",
        "states": [
            {
                "when": "both global 0x00dff070 mode bytes +0x10/+0x11 are false and predicate 0x006aa08e is false",
                "effects": [],
                "return": 1,
            },
            {
                "when": "message in {0x0006,0x0008,0x0009,0x000a,0x000e}",
                "effects": [],
                "return": 1,
            },
            {
                "when": "message in {0x0005,0x000d}",
                "effects": [
                    "derive two control rectangles via 0x00713bc6/0x00713b3c",
                    "project pointer with 0x006d81ec then 0x006d7744",
                    "route selected-object or terrain radar action through exact service calls",
                ],
                "return": 1,
            },
            {
                "when": "message in {0x0000,0x0011,0x0012,0x0018}",
                "effects": [
                    "query selected object and radar projection",
                    "conditionally update camera/radar state through global 0x00dfdca0 vslot 0x4c or selection services",
                ],
                "return": 1,
            },
            {"when": "all other messages", "effects": [], "return": 0},
        ],
        "dependencies": [
            "0x00dff070 mode state",
            "0x00dfedf0 selection/camera service",
            "0x00dfe758 player state",
            "0x00dfe77c command service",
            "0x00dfdca0 radar/camera service",
            "0x006d81ec screen-to-radar projection",
            "0x006d7744 radar-to-world projection",
        ],
        "unresolved": [
            "object field +0x14 values {0x0a,0x18,0x20,0x26} are exact but stripped type aliases",
            "branches choosing command IDs 0x42f/0x430 require one trace of selected object and returned order packet",
            "messages 0x0011/0x0012/0x0018 share aliased selection/camera branches and are not given semantic names",
        ],
    },
    "ControlBarSystem": {
        "interface": "handle_control_bar_system(control, message, data1, data2, cached_controls) -> WndHandled",
        "returnContract": "returns 1 after recognized dispatch; returns 0 for the exact gate/unrecognized paths ending at 0x0080276a",
        "states": [
            {
                "when": "global 0x00dfe16c exists and field +0x1a104 >= 0",
                "effects": [],
                "return": 0,
            },
            {
                "when": "message == 0x0001",
                "effects": [
                    "resolve and cache exact control handle at global 0x00e02f00"
                ],
                "return": 1,
            },
            {
                "when": "message in {0x4006,0x4007}",
                "effects": ["dispatch 0x0071abd4 with message and data2"],
                "return": 1,
            },
            {
                "when": "message in {0x4008,0x4009,0x400b}",
                "effects": [
                    "lazily resolve eight exact cached control handles at 0x00e02f00..0x00e02f20",
                    "route selected control to its exact menu/production/command service branch",
                ],
                "return": 1,
            },
            {
                "when": "message == 0x4031",
                "effects": [
                    "resolve data2 through 0x009c4ae9",
                    "compare against cached control handles",
                    "dispatch exact matched service branch",
                ],
                "return": "1 on recognized/matched branch; 0 on reject branch",
            },
            {"when": "other message", "effects": [], "return": 0},
        ],
        "dependencies": [
            "0x00dfe16c game-state gate",
            "0x00df36a4 name/control resolver",
            "0x00dfedf0 UI service",
            "0x00e01cfc control-bar owner",
            "cached handles 0x00e02f00..0x00e02f24",
            "0x0071abcf/0x0071abd4 selected-button dispatchers",
            "0x009c4ae9 data2-to-control resolver",
        ],
        "unresolved": [
            "the eight cached handle names are code-addressed strings but semantic aliases are not promoted here",
            "matched 0x4031 branches call distinct stripped services; each needs one handler breakpoint trace with exact data2",
            "message 0x400b has both cached-control rejection and selected-button fallback branches; preserve ordering exactly",
        ],
    },
}


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def build_contract(
    effective_assets: Path | str, manifest: Mapping[str, Any], game_dat: Path | str
) -> dict[str, Any]:
    root = Path(effective_assets).resolve()
    if "workspace" not in {part.casefold() for part in root.parts}:
        raise ValueError("effective-assets must be private")
    wnd_path = root / "window/controlbar.wnd"
    wnd_payload = wnd_path.read_bytes()
    if _sha(wnd_payload) != ASSETS["window/controlbar.wnd"]["sha256"]:
        raise ValueError("ControlBar.wnd identity changed")
    rows = [
        row
        for row in manifest.get("files", [])
        if str(row.get("path", "")).casefold() == "window/controlbar.wnd"
    ]
    if len(rows) != 1 or rows[0].get("archive") != "window.big":
        raise ValueError("ControlBar.wnd winner changed")
    wnd = parse_wnd_layout(wnd_payload, "window/controlbar.wnd")
    bindings: dict[str, list[dict[str, Any]]] = {name: [] for name in RANGES}
    for window in wnd["windows"]:
        for kind, callback in window["callbacks"].items():
            if callback in bindings:
                bindings[callback].append(
                    {
                        "kind": kind,
                        "windowIndex": window["index"],
                        "controlId": window["name"],
                        "controlType": window["windowType"],
                        "status": window["status"],
                    }
                )
    data = Path(game_dat).read_bytes()
    if _sha(data) != GAME_DAT_SHA256:
        raise ValueError("BFME2 game.dat identity changed")
    handlers = []
    for name, (start, end, digest) in RANGES.items():
        payload = data[start - TEXT_DELTA : end - TEXT_DELTA]
        if _sha(payload) != digest or HANDLERS[name][1] != start:
            raise ValueError(f"handler range changed: {name}")
        handlers.append(
            {
                "name": name,
                "entryVa": f"0x{start:08x}",
                "endVa": f"0x{end:08x}",
                "byteLength": len(payload),
                "sha256": digest,
                "controls": bindings[name],
                **SEMANTICS[name],
                "genericDispatchAllowed": False,
            }
        )
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "source": {"gameDatSha256": GAME_DAT_SHA256, "wndSha256": _sha(wnd_payload)},
        "summary": {
            "prioritizedHandlerCount": 5,
            "fullyExactHandlerCount": 3,
            "boundedOpaqueBranchHandlerCount": 2,
            "implemented": False,
            "genericDispatchAllowed": False,
        },
        "handlers": handlers,
        "retainedNotImplemented": {
            "BeaconWindowInput": "event-dormant",
            "ControlBarObserverSystem": "outside declared player-v-player slice",
        },
        "tracePlan": [
            "break only on 0x008020ce and 0x00802455",
            "record message/data1/data2 plus listed global pointer values",
            "for each unresolved branch record called target, return EAX, and exact written addresses",
            "promote a semantic alias only after two identical Men-v-Men observations",
        ],
    }
    result["aggregateSha256"] = _sha(_canonical(result))
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--effective-assets", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--game-dat", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    result = build_contract(
        args.effective_assets,
        json.loads(args.manifest.read_text(encoding="utf-8")),
        args.game_dat,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(_canonical(result))
    print(
        json.dumps(
            {"aggregateSha256": result["aggregateSha256"], "output": str(args.output)},
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
