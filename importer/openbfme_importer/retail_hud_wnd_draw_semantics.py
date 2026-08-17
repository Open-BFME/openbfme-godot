"""Hash-pinned draw-command contracts for active BFME2 WND callbacks."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

from .retail_hud_wnd_activation_oracle import ASSETS, GAME_DAT_SHA256
from .retail_hud_wnd_callback_oracle import HANDLERS
from .sage_apt import parse_wnd_layout

SCHEMA = "openbfme.private-hud-wnd-draw-semantics"
TEXT_DELTA = 0x400A00

RANGES = {
    "W3DLeftHUDDraw": (
        0x49DCEE,
        0x49DDE6,
        "678d3ff87adbc874a7c552a53829dc163e0379ed01b4aa796f5b0cf919e528ae",
    ),
    "W3DRightHUDDraw": (
        0x49DDE6,
        0x49DE01,
        "058e05d1153aa40c9ec26a7afdcf8a27ad8831a5f8b4edf138af4c4626384259",
    ),
    "W3DCommandBarGridDraw": (
        0x49DE26,
        0x49DF9A,
        "7b0f33eca0e080b0050f1746d99ed32598b1211651f9a657a84bb1cdc4dea9d2",
    ),
    "W3DCommandBarTopDraw": (
        0x49DF9A,
        0x49DFDD,
        "ab200ff2a46e98dc87c7624664fcf485f11da9de73a266586fefdbbd33791f9b",
    ),
    "W3DPowerDraw": (
        0x49E365,
        0x49E71C,
        "05190717b451101f2da65d001f8bc9e083df23fa3f45cc32e1a439a8e3dec558",
    ),
    "W3DCommandBarGenExpDraw": (
        0x49ED8D,
        0x49F0C8,
        "d05aeb522557f45e5ba31cae97a599bce98326e0f499a1599777eac3c4fffccf",
    ),
    "W3DCommandBarBackgroundDraw": (
        0x49FA82,
        0x49FB63,
        "e72f3f40b8b7772715d33529bb43b5ba70f8093d9ad1649af77c8904017c269f",
    ),
    "W3DCommandBarForegroundDraw": (
        0x49FB63,
        0x49FC44,
        "42a2e8e0d09554941836372d70c1194bb6dfdaa6685b68944ce8f5078dd77354",
    ),
    "W3DGadgetPushButtonImageDraw": (
        0x4A6019,
        0x4A6097,
        "54906e711c013dfbf54e391119e05078fa5545140191d7fe954633cbff312762",
    ),
    "W3DNoDraw": (
        0x4B3FD0,
        0x4B3FD1,
        "ae3f4619b0413d70d3004b9131c3752153074e45725be13b9a148978895e359e",
    ),
}

DRAW: dict[str, dict[str, Any]] = {
    "W3DCommandBarBackgroundDraw": {
        "assets": ["ControlBar.wnd:BackgroundMarker"],
        "inputs": [
            "control rect",
            "instance draw state",
            "control-bar owner field +0x44",
            "cached image 0x00de60ec",
        ],
        "order": [
            "reject missing owner",
            "lazy-resolve BackgroundMarker",
            "resolve marker window",
            "derive rect",
            "0x0071aeae pre-state",
            "0x0071fac5 background draw",
        ],
        "state": "handler-selected background state; exact blend enum is opaque",
        "unresolved": [
            "0x0071aeae and 0x0071fac5 parameter aliases require one draw breakpoint trace"
        ],
    },
    "W3DCommandBarForegroundDraw": {
        "assets": ["ControlBar.wnd:BackgroundMarker"],
        "inputs": [
            "control rect",
            "instance draw state",
            "control-bar owner field +0x44",
            "cached image 0x00de60fc",
        ],
        "order": [
            "reject missing owner",
            "lazy-resolve marker",
            "derive rect",
            "0x0071ae93 pre-state",
            "0x0071fa9a foreground draw",
        ],
        "state": "handler-selected foreground state; exact blend enum is opaque",
        "unresolved": [
            "foreground cache resolves from the marker identity; exact image indirection requires one trace"
        ],
    },
    "W3DCommandBarGenExpDraw": {
        "assets": ["GenExpBarTop1", "GenExpBarBottom1", "GenExpBar1"],
        "inputs": [
            "control rect",
            "instance draw state",
            "player experience/progress",
            "three cached mapped images",
        ],
        "order": [
            "predicate 0x006aa231",
            "lazy-resolve three images",
            "derive rect",
            "clamp progress to 0..100",
            "draw bar and top/bottom caps through image vslot 0x108",
        ],
        "state": "per-image draw state plus clipped progress geometry",
        "unresolved": [
            "progress source aliases and vslot 0x108 blend flags are stripped"
        ],
    },
    "W3DCommandBarGridDraw": {
        "assets": [],
        "inputs": ["control rect", "instance draw state", "selection grid state"],
        "order": [
            "predicate 0x0070f45f",
            "fallback 0x0049dc32 if negative",
            "derive rect",
            "0x007143ff grid state",
            "up to four ordered 0x0044d664 cell draws",
        ],
        "state": "four retail-computed rectangles; blend/material is owned by 0x0044d664",
        "unresolved": [
            "cell semantic names and 0x0044d664 material parameters require one selected-unit trace"
        ],
    },
    "W3DCommandBarTopDraw": {
        "assets": ["ControlBar.wnd:ButtonGeneral"],
        "inputs": ["resolved ButtonGeneral window", "control-bar top state"],
        "order": [
            "resolve ButtonGeneral",
            "skip missing window",
            "predicate 0x00713cd9",
            "tail-dispatch 0x006a7dd0",
        ],
        "state": "top overlay state is entirely owned by 0x006a7dd0",
        "unresolved": ["tail target draw parameters are not exposed by aliases"],
    },
    "W3DGadgetPushButtonImageDraw": {
        "assets": [],
        "inputs": [
            "control",
            "instance data",
            "control image field +0x84",
            "instance flag 0x20",
        ],
        "order": [
            "predicate 0x00727df6 then delegate 0x004a501c",
            "else reject missing image",
            "if flag 0x20 derive rect and call 0x004a56ed",
            "otherwise fallback 0x004a4b7c",
        ],
        "state": "exact normal/image/fallback branch; image and blend come from authored instance data",
        "unresolved": [],
    },
    "W3DLeftHUDDraw": {
        "assets": [],
        "inputs": [
            "control rect",
            "instance data",
            "radar service",
            "mode bytes 0x00dff070+0x10/+0x11",
        ],
        "order": [
            "query global 0x00dfedf0 vslot 0x164",
            "derive rect",
            "if radar object exists call its vslot 0x104",
            "else mode-gated call selected service vslot 0x1c",
        ],
        "state": "world/radar render service owns material and clipping",
        "unresolved": [
            "radar object and vslot blend/clipping structures require one frame trace"
        ],
    },
    "W3DNoDraw": {
        "assets": [],
        "inputs": [],
        "order": [],
        "state": "none",
        "unresolved": [],
        "proof": "single byte 0xc3 RET; no reads, writes, calls, draw commands, or return guarantee",
    },
    "W3DPowerDraw": {
        "assets": ["PowerPointY", "PowerPointG", "PowerBarSlider"],
        "inputs": [
            "control rect",
            "instance data",
            "player power/progress state",
            "three cached mapped images",
        ],
        "order": [
            "lazy-resolve PowerPointY, PowerPointG, PowerBarSlider",
            "query player/power state",
            "derive rect",
            "emit ordered point/slider image draws via vslots 0x108, 0xa8, 0xb0",
        ],
        "state": "retail-computed point count, slider clip, and per-image state",
        "unresolved": [
            "power counters and image vslot blend/state aliases are stripped; capture one nonzero-power frame"
        ],
    },
    "W3DRightHUDDraw": {
        "assets": [],
        "inputs": ["control", "instance data", "predicate 0x0070f45f"],
        "order": [
            "evaluate predicate",
            "if negative delegate exact default helper 0x0049dc32",
            "otherwise emit no draw",
        ],
        "state": "delegated helper owns draw state",
        "unresolved": [],
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
    wnd_payload = (root / "window/controlbar.wnd").read_bytes()
    if _sha(wnd_payload) != ASSETS["window/controlbar.wnd"]["sha256"]:
        raise ValueError("ControlBar.wnd identity changed")
    if (
        len(
            [
                row
                for row in manifest.get("files", [])
                if str(row.get("path", "")).casefold() == "window/controlbar.wnd"
            ]
        )
        != 1
    ):
        raise ValueError("ControlBar.wnd winner changed")
    wnd = parse_wnd_layout(wnd_payload, "window/controlbar.wnd")
    controls: dict[str, list[dict[str, Any]]] = {name: [] for name in RANGES}
    for window in wnd["windows"]:
        if window["callbacks"].get("draw") in controls:
            name = window["callbacks"]["draw"]
            controls[name].append(
                {
                    "windowIndex": window["index"],
                    "controlId": window["name"],
                    "controlType": window["windowType"],
                    "rect": window["screenRect"],
                    "status": window["status"],
                }
            )
    data = Path(game_dat).read_bytes()
    if _sha(data) != GAME_DAT_SHA256:
        raise ValueError("BFME2 game.dat identity changed")
    rows = []
    for name, (start, end, digest) in RANGES.items():
        body = data[start - TEXT_DELTA : end - TEXT_DELTA]
        if _sha(body) != digest or HANDLERS[name][1] != start:
            raise ValueError(f"draw handler changed: {name}")
        rows.append(
            {
                "name": name,
                "entryVa": f"0x{start:08x}",
                "endVa": f"0x{end:08x}",
                "byteLength": len(body),
                "sha256": digest,
                "interface": f"draw_{name.removeprefix('W3D').removesuffix('Draw').lower()}(control, instance_data, typed_state) -> list[DrawCommand]",
                "controls": controls[name],
                **DRAW[name],
                "genericDispatchAllowed": False,
            }
        )
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "source": {"gameDatSha256": GAME_DAT_SHA256, "wndSha256": _sha(wnd_payload)},
        "summary": {
            "drawCallbackCount": 10,
            "exactFullBodyCount": 10,
            "provenNoOpCount": 1,
            "callbacksImplemented": False,
            "inventedVisualsAllowed": False,
            "genericDispatchAllowed": False,
        },
        "callbacks": rows,
        "tracePlan": [
            "break only on the exact unresolved handler entry",
            "capture control and instance-data pointers plus ordered downstream call arguments",
            "record mapped-image identity, destination rect, color/state words, and called vslot",
            "promote only byte-stable typed DrawCommands; never synthesize a fallback image",
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
