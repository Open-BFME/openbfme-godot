"""Inventory the active BFME2 ControlBar.wnd callback boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from typing import Any, Mapping

from .retail_hud_wnd_activation_oracle import (
    ASSETS,
    EXPECTED_CALLBACKS,
    GAME_DAT_SHA256,
)
from .sage_apt import parse_wnd_layout


SCHEMA = "openbfme.private-hud-wnd-callback-oracle"
TEXT_DELTA = 0x400A00
RDATA_DELTA = 0x400C00
DATA_DELTA = 0x401400

# name: (descriptor VA, handler VA, entry-prefix bytes, entry-prefix SHA-256)
HANDLERS: dict[str, tuple[int, int, int, str]] = {
    "BeaconWindowInput": (
        0xDBCB8C,
        0x90DBAA,
        32,
        "7760aed1998fd95e082fb8afe04dd6487f8fb943f2bb316672eae9b80301ff24",
    ),
    "ControlBarInput": (
        0xDBCB74,
        0x4D43D0,
        1,
        "4e07408562bedb8b60ce05c1decfe3ad16b72230967de01f640b7e4729b49fce",
    ),
    "ControlBarObserverSystem": (
        0xDBCA84,
        0x90E6A6,
        32,
        "2f2dd8b11a628170f600640ffe63e76457e63e780d14f9a2b3207c688156cdd8",
    ),
    "ControlBarSystem": (
        0xDBCA78,
        0x802455,
        32,
        "685ecc946540077682e394086ea6b687574a40f1e6f9f7f202c348f6e22ac8b0",
    ),
    "GameWinBlockInput": (
        0xDBCACC,
        0x71435A,
        32,
        "caea6a494fbe9ea652306cacb94f5a3193637bb2a33b5e5c923c57d41882e276",
    ),
    "LeftHUDInput": (
        0xDBCB80,
        0x8020CE,
        32,
        "108e437482f02abeaca55181a6a51a8769523d1fa418caa7522a82231c8b7b21",
    ),
    "PassSelectedButtonsToParentSystem": (
        0xDBC9C4,
        0x6C0978,
        32,
        "59e7ba25f1f508d45959eba240a922460b37d7dc57884ae89a42cab52b797a04",
    ),
    "W3DCommandBarBackgroundDraw": (
        0xDB3E60,
        0x49FA82,
        32,
        "ede8ae46ba140ed5c84db0a029be904c687e1be76334cb7038a4c6c7972713b0",
    ),
    "W3DCommandBarForegroundDraw": (
        0xDB3E9C,
        0x49FB63,
        32,
        "4d1d05f62755bcb2cdee2edec89d2089b7f124caa3bbb9f8456e03558ad61b1b",
    ),
    "W3DCommandBarGenExpDraw": (
        0xDB3E78,
        0x49ED8D,
        32,
        "21341ea4ad17c3455dd83129979151129011814abf6ca8cc0fab8fc7babe8b6c",
    ),
    "W3DCommandBarGridDraw": (
        0xDB3E90,
        0x49DE26,
        32,
        "b48caac8aa6684b7af5377ed6de779cbb9400a63b1f7bcdc612d4583d91527e7",
    ),
    "W3DCommandBarTopDraw": (
        0xDB3E6C,
        0x49DF9A,
        32,
        "2efb8ad8556626ea9545b7d06af3ed1b831669ab99c8b57487ee3d908ca0b24c",
    ),
    "W3DGadgetPushButtonImageDraw": (
        0xDB3D28,
        0x4A6019,
        32,
        "d931eca1499eaaa470b4693a03e3fb5ef81d0a05b41e493cce2a6cbc7e1a0421",
    ),
    "W3DLeftHUDDraw": (
        0xDB3E30,
        0x49DCEE,
        32,
        "49d360857036b0ef827afee9c9c4b26080256ef82e1621a232affac89f76e7b9",
    ),
    "W3DNoDraw": (
        0xDB3EA8,
        0x4B3FD0,
        1,
        "ae3f4619b0413d70d3004b9131c3752153074e45725be13b9a148978895e359e",
    ),
    "W3DPowerDraw": (
        0xDB3E54,
        0x49E365,
        32,
        "b1edce6158558e37a2a7564f49e1d4cbe8e83e08a8d23c6c3285658b81df633d",
    ),
    "W3DRightHUDDraw": (
        0xDB3E48,
        0x49DDE6,
        32,
        "c67f6d6d3f4f449226201204256dfa3485ad12471363c061674c0277529e8f09",
    ),
}

BUILT_INS = {
    "GameWinDefaultInput",
    "GameWinDefaultSystem",
    "GameWinDefaultTooltip",
    "W3DGameWinDefaultDraw",
}

META: dict[str, dict[str, Any]] = {
    "BeaconWindowInput": {
        "reachability": "event-dormant-beacon-editor",
        "messages": ["0x0015 when data1=1"],
        "effect": "dispatch UI command 1004, then invoke input-service vslot 0x110",
        "interface": "handle_beacon_input(control, message, data1, data2) -> WndHandled",
    },
    "ControlBarInput": {
        "reachability": "baseline-bound-no-op",
        "messages": ["all"],
        "effect": "exact handler xor-eax/ret returns unhandled with no side effect",
        "interface": "handle_control_bar_input(control, message, data1, data2) -> WndHandled",
    },
    "ControlBarObserverSystem": {
        "reachability": "outside-men-player-vs-men-player-slice",
        "messages": ["0x0001", "0x4005..0x4009"],
        "effect": "select observer player/stat panels and update observer-owned control-bar state",
        "interface": "handle_observer_system(control, message, data1, data2, observer_state) -> WndHandled",
    },
    "ControlBarSystem": {
        "reachability": "baseline-slice-required",
        "messages": [
            "0x0001",
            "0x4005..0x400b",
            "additional branches retained in exact handler",
        ],
        "effect": "central selected-button, production, command, menu, and control-bar state dispatcher",
        "interface": "handle_control_bar_system(control, message, data1, data2, selection) -> WndHandled",
    },
    "GameWinBlockInput": {
        "reachability": "baseline-slice-required",
        "messages": ["0x0006", "0x0015", "0x0018", "all other consumed"],
        "effect": "message 0x0006 clears three input/camera service states; 0x0015/0x0018 remain unconsumed",
        "interface": "block_control_bar_input(control, message, data1, data2) -> WndHandled",
    },
    "GameWinDefaultInput": {
        "reachability": "baseline-slice-required",
        "messages": ["built-in message set unresolved"],
        "effect": "shared WND-library default input behavior",
        "interface": "handle_default_input(control, message, data1, data2) -> WndHandled",
    },
    "GameWinDefaultSystem": {
        "reachability": "baseline-slice-required",
        "messages": ["built-in message set unresolved"],
        "effect": "shared WND-library default system behavior",
        "interface": "handle_default_system(control, message, data1, data2) -> WndHandled",
    },
    "GameWinDefaultTooltip": {
        "reachability": "baseline-slice-required-on-hover",
        "messages": ["built-in tooltip request unresolved"],
        "effect": "resolve the control tooltip through the WND-library default",
        "interface": "resolve_default_tooltip(control, pointer) -> TooltipResult",
    },
    "LeftHUDInput": {
        "reachability": "baseline-slice-required",
        "messages": [
            "0x0000",
            "0x0005",
            "0x0006",
            "0x0008",
            "0x000d",
            "additional radar branches retained in exact handler",
        ],
        "effect": "radar-to-world input, camera movement, and gameplay-order dispatch",
        "interface": "handle_radar_input(control, message, data1, data2, radar_state) -> WndHandled",
    },
    "PassSelectedButtonsToParentSystem": {
        "reachability": "baseline-slice-required",
        "messages": ["0x4006..0x4009", "0x400b", "0x4031"],
        "effect": "forward only selected-button messages to the exact parent system callback",
        "interface": "forward_selected_button(control, message, data1, data2) -> WndHandled",
    },
    "W3DCommandBarBackgroundDraw": {
        "reachability": "baseline-frame-reachable",
        "messages": ["draw-frame"],
        "effect": "load/cache and draw authored command-bar background state",
        "interface": "draw_command_bar_background(control, draw_state) -> void",
    },
    "W3DCommandBarForegroundDraw": {
        "reachability": "baseline-frame-reachable",
        "messages": ["draw-frame"],
        "effect": "load/cache and draw authored command-bar foreground state",
        "interface": "draw_command_bar_foreground(control, draw_state) -> void",
    },
    "W3DCommandBarGenExpDraw": {
        "reachability": "baseline-frame-reachable",
        "messages": ["draw-frame"],
        "effect": "draw the control-bar experience/progress surface from live player state",
        "interface": "draw_command_progress(control, draw_state, player_state) -> void",
    },
    "W3DCommandBarGridDraw": {
        "reachability": "baseline-frame-reachable",
        "messages": ["draw-frame"],
        "effect": "draw selected-unit command grid using exact window and instance draw data",
        "interface": "draw_command_grid(control, draw_state, selection) -> void",
    },
    "W3DCommandBarTopDraw": {
        "reachability": "baseline-frame-reachable",
        "messages": ["draw-frame"],
        "effect": "draw final on-top command-bar overlay when the exact named surface is available",
        "interface": "draw_command_bar_top(control, draw_state) -> void",
    },
    "W3DGadgetPushButtonImageDraw": {
        "reachability": "baseline-frame-reachable",
        "messages": ["draw-frame"],
        "effect": "draw the exact push-button image state or delegate to the button fallback",
        "interface": "draw_push_button_image(control, draw_state) -> void",
    },
    "W3DGameWinDefaultDraw": {
        "reachability": "baseline-frame-reachable",
        "messages": ["draw-frame"],
        "effect": "shared WND-library default draw behavior",
        "interface": "draw_default_window(control, draw_state) -> void",
    },
    "W3DLeftHUDDraw": {
        "reachability": "baseline-frame-reachable",
        "messages": ["draw-frame"],
        "effect": "draw legacy WND radar/HUD surface from live radar and camera services",
        "interface": "draw_left_hud(control, draw_state, radar_state) -> void",
    },
    "W3DNoDraw": {
        "reachability": "baseline-frame-reachable-no-op",
        "messages": ["draw-frame"],
        "effect": "exact handler ret performs no draw and no side effect",
        "interface": "draw_nothing(control, draw_state) -> void",
    },
    "W3DPowerDraw": {
        "reachability": "baseline-frame-reachable",
        "messages": ["draw-frame"],
        "effect": "draw the exact player-power/progress state",
        "interface": "draw_power_state(control, draw_state, player_state) -> void",
    },
    "W3DRightHUDDraw": {
        "reachability": "baseline-frame-reachable",
        "messages": ["draw-frame"],
        "effect": "draw right-HUD and production queue surfaces",
        "interface": "draw_right_hud(control, draw_state, production_state) -> void",
    },
}


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _private_root(path: Path | str) -> Path:
    root = Path(path).resolve()
    if "workspace" not in {part.casefold() for part in root.parts} or not root.is_dir():
        raise ValueError("effective-assets must be an existing private directory")
    return root


def _asset(
    root: Path, manifest: Mapping[str, Any], virtual_path: str
) -> tuple[bytes, dict[str, Any]]:
    rows = [
        dict(row)
        for row in manifest.get("files", [])
        if str(row.get("path", "")).casefold() == virtual_path.casefold()
    ]
    if len(rows) != 1:
        raise ValueError(f"effective source winner changed: {virtual_path}")
    row = rows[0]
    expected = ASSETS[virtual_path]
    payload = (root / virtual_path).read_bytes()
    if (
        row.get("archive") != expected["archive"]
        or len(payload) != expected["size"]
        or _sha(payload) != expected["sha256"]
    ):
        raise ValueError(f"retail asset identity changed: {virtual_path}")
    return payload, row


def _callback_controls(wnd: Mapping[str, Any]) -> dict[str, list[dict[str, Any]]]:
    result: dict[str, list[dict[str, Any]]] = {}
    for window in wnd["windows"]:
        for kind, callback in window["callbacks"].items():
            if callback is not None:
                result.setdefault(callback, []).append(
                    {
                        "kind": kind,
                        "windowIndex": window["index"],
                        "controlId": window["name"],
                        "controlType": window["windowType"],
                        "status": window["status"],
                        "style": window["style"],
                    }
                )
    return result


def _handler_evidence(data: bytes, name: str) -> dict[str, Any]:
    if name in BUILT_INS:
        if name.encode() in data or name.encode("utf-16le") in data:
            raise ValueError(
                f"built-in callback unexpectedly acquired a retail descriptor: {name}"
            )
        return {
            "status": "runtime-built-in-no-game-dat-descriptor",
            "handlerVa": None,
            "reason": "name is authored in ControlBar.wnd but absent as ASCII/UTF-16 and descriptor from game.dat and the install executables",
            "trace": "break after WND load returns at 0x0069c25f; inspect the exact callback slot on the listed control, then trigger its listed message/draw and record the indirect target and four stack words",
        }
    descriptor_va, handler_va, prefix_bytes, prefix_sha = HANDLERS[name]
    string_offset = data.find(name.encode() + b"\0")
    if string_offset < 0:
        raise ValueError(f"callback registry string missing: {name}")
    string_va = string_offset + RDATA_DELTA
    descriptor = data[descriptor_va - DATA_DELTA : descriptor_va - DATA_DELTA + 12]
    if struct.unpack("<III", descriptor) != (string_va, handler_va, 0):
        raise ValueError(f"callback descriptor changed: {name}")
    prefix = data[handler_va - TEXT_DELTA : handler_va - TEXT_DELTA + prefix_bytes]
    if _sha(prefix) != prefix_sha:
        raise ValueError(f"callback handler entry changed: {name}")
    kind = (
        "draw(window, instanceData) -> void"
        if "Draw" in name
        else "message(window, message, data1, data2) -> handled"
    )
    return {
        "status": "exact-retail-handler",
        "descriptorVa": f"0x{descriptor_va:08x}",
        "descriptorSha256": _sha(descriptor),
        "handlerVa": f"0x{handler_va:08x}",
        "entryPrefixBytes": prefix_bytes,
        "entryPrefixSha256": prefix_sha,
        "inferredCdeclShape": kind,
        "trace": f"break on 0x{handler_va:08x}; capture the listed message plus entry stack words and return value",
    }


def build_contract(
    effective_assets: Path | str, manifest: Mapping[str, Any], game_dat: Path | str
) -> dict[str, Any]:
    root = _private_root(effective_assets)
    wnd_payload, wnd_row = _asset(root, manifest, "window/controlbar.wnd")
    resizer_payload, resizer_row = _asset(
        root, manifest, "data/ini/controlbarresizer.ini"
    )
    wnd = parse_wnd_layout(wnd_payload, "window/controlbar.wnd")
    controls = _callback_controls(wnd)
    if tuple(wnd["callbacks"]) != EXPECTED_CALLBACKS or set(controls) != set(
        EXPECTED_CALLBACKS
    ):
        raise ValueError("ControlBar.wnd callback identity set changed")
    data = Path(game_dat).read_bytes()
    if _sha(data) != GAME_DAT_SHA256:
        raise ValueError("BFME2 game.dat identity changed")
    resizer_refs = set(
        re.findall(
            r"ControlBar\.wnd:[A-Za-z0-9_%]+", resizer_payload.decode("latin-1"), re.I
        )
    )
    rows = []
    for name in EXPECTED_CALLBACKS:
        kinds = sorted({row["kind"] for row in controls[name]})
        rows.append(
            {
                "name": name,
                "bindingKinds": kinds,
                "controls": controls[name],
                "resizerReferencedControls": sorted(
                    {
                        row["controlId"]
                        for row in controls[name]
                        if row["controlId"] in resizer_refs
                    },
                    key=str.casefold,
                ),
                **META[name],
                "retailHandler": _handler_evidence(data, name),
                "genericDispatchAllowed": False,
            }
        )
    baseline = [
        row["name"] for row in rows if row["reachability"].startswith("baseline")
    ]
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "source": {
            "gameDatSha256": GAME_DAT_SHA256,
            "wnd": {
                "path": wnd_row["path"],
                "archive": wnd_row["archive"],
                "byteLength": len(wnd_payload),
                "sha256": _sha(wnd_payload),
            },
            "resizer": {
                "path": resizer_row["path"],
                "archive": resizer_row["archive"],
                "byteLength": len(resizer_payload),
                "sha256": _sha(resizer_payload),
            },
            "activationAuthority": "retail_hud_wnd_activation_oracle: active-companion-not-candidate-dead",
        },
        "summary": {
            "callbackIdentityCount": 21,
            "exactRetailHandlerCount": len(HANDLERS),
            "runtimeBuiltInHandlerCount": len(BUILT_INS),
            "baselineSliceRequiredCount": len(baseline),
            "eventDormantCount": 1,
            "outsideDeclaredPlayerSliceCount": 1,
            "genericDispatchAllowed": False,
        },
        "baselineSliceRequired": baseline,
        "eventDormant": ["BeaconWindowInput"],
        "outsideDeclaredPlayerSlice": ["ControlBarObserverSystem"],
        "callbacks": rows,
        "smallestDynamicTrace": {
            "activationTraceRequired": False,
            "unresolvedCallbacks": sorted(BUILT_INS, key=str.casefold),
            "loadReturnBreakpointVa": "0x0069c25f",
            "procedure": [
                "start one BFME2 1.06 Men-v-Men skirmish and break immediately after ControlBar.wnd load",
                "for each unresolved row inspect only its exact listed control callback slot",
                "trigger one listed default input/system/tooltip/draw event and record the indirect target, four entry stack words, return EAX, and touched service addresses",
                "hash the discovered handler entry bytes before promoting any typed adapter",
            ],
        },
        "recommendation": "implement the 19 baseline typed adapters first; retain beacon input as event-gated and observer system outside the declared player slice; never route names through a generic callback dictionary",
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
