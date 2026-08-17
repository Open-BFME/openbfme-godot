"""Seal the unresolved BFME2 ControlBar.wnd built-in callback boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any, Mapping

from .retail_hud_wnd_activation_oracle import ASSETS, GAME_DAT_SHA256
from .sage_apt import parse_wnd_layout


SCHEMA = "openbfme.private-hud-wnd-builtin-oracle"
TEXT_DELTA = 0x400A00
RDATA_DELTA = 0x400C00
DATA_DELTA = 0x401400

BUILT_INS: dict[str, dict[str, Any]] = {
    "GameWinDefaultInput": {
        "kind": "input",
        "slotOffset": "0x1e0",
        "setterVa": 0x714147,
        "setterEndVa": 0x71415A,
        "setterSha256": (
            "a663406cd4cf505f62127891e4a76c07228b7edcbd3309a86f503f5a3631e95f"
        ),
        "occurrences": ((188_306, "e1e7cd76fdfb4837d50ca716b6152528393fc31174ae92d8b0040ce5ae60d1ea"),),
        "dynamicGates": (
            "resolved-callback-slot-target",
            "exact-message-set-and-handled-return",
            "touched-window-and-service-state",
        ),
    },
    "GameWinDefaultSystem": {
        "kind": "system",
        "slotOffset": "0x1e4",
        "setterVa": 0x714134,
        "setterEndVa": 0x714147,
        "setterSha256": (
            "14ac3f0d7e77de82053215285cd9ca9e86907da28e9c0696e4f77f81874ca034"
        ),
        "occurrences": (
            (6_565, "faa309f2f368727bf388b6b03cbbf728e08b448bfe903794c278f4d31eda2645"),
            (267_754, "3b6e6a5bb7038b09bd3b98703689f2a25af481de15579c3ced92fc393ae4e881"),
        ),
        "dynamicGates": (
            "resolved-callback-slot-target",
            "exact-message-set-and-handled-return",
            "parent-child-and-service-dispatch",
        ),
    },
    "GameWinDefaultTooltip": {
        "kind": "tooltip",
        "slotOffset": "0x1ec",
        "setterVa": 0x71416D,
        "setterEndVa": 0x71417C,
        "setterSha256": (
            "8a572730c67a269ddb09c4897703b3062797763f72cb7ef5787f996fa015fa57"
        ),
        "occurrences": (
            (188_354, "c44f0b2f7ff4f026197fe4d0fb115ced7a94eb5d4c1b6d174ceda0f0a384f05b"),
            (267_838, "1e5a92b585e0301f6e26228b545150cfc6abf1131e304d579772d93c2c921618"),
        ),
        "dynamicGates": (
            "resolved-callback-slot-target",
            "calling-convention-result-and-ownership",
            "tooltip-text-image-and-localization-services",
        ),
    },
    "W3DGameWinDefaultDraw": {
        "kind": "draw",
        "slotOffset": "0x1e8",
        "setterVa": 0x71415A,
        "setterEndVa": 0x71416D,
        "setterSha256": (
            "34004e07e8f986989f923afe4b0a7ce19016c99ae9de7203ed99929e21db7b30"
        ),
        "occurrences": ((504, "17463ae6a17a88f1236d21e9b7a8411cec9e736db5324ac9fdf13875ed352825"),),
        "dynamicGates": (
            "resolved-callback-slot-target",
            "no-op-versus-default-delegation",
            "ordered-draw-commands-blend-and-clipping",
            "candidate-0x0049dc32-identity",
        ),
    },
}

EXPECTED_BINDINGS = {
    "GameWinDefaultInput": [
        "input|56|ControlBar.wnd:ProductionQueueWindow|USER|ENABLED,NOFOCUS,SEE_THRU|USER"
    ],
    "GameWinDefaultSystem": [
        "system|2|ControlBar.wnd:LeftHUD|USER|ENABLED,NOFOCUS|USER",
        "system|80|ControlBar.wnd:LeftHUD1Input|USER|ENABLED,NOFOCUS|USER",
    ],
    "GameWinDefaultTooltip": [
        "tooltip|56|ControlBar.wnd:ProductionQueueWindow|USER|ENABLED,NOFOCUS,SEE_THRU|USER",
        "tooltip|80|ControlBar.wnd:LeftHUD1Input|USER|ENABLED,NOFOCUS|USER",
    ],
    "W3DGameWinDefaultDraw": [
        "draw|0|ControlBar.wnd:ControlBarParent|USER|BORDER,ENABLED,SEE_THRU|USER"
    ],
}

REGISTRY_TABLES = (
    {
        "name": "draw",
        "startVa": 0xDB3D1C,
        "endVa": 0xDB3EC0,
        "entryCount": 35,
        "sha256": "2e643f5adc104e0d52394cc6da1b0f0517f1bcc7574542c13171876e3c997c8f",
        "rowsSha256": "1586e3089eabf025d3a5c12d24427ffee1cd71496ba04b6bd231c1c6d9257fdc",
    },
    {
        "name": "system",
        "startVa": 0xDBC9C4,
        "endVa": 0xDBCAC0,
        "entryCount": 21,
        "sha256": "dbb70cd4c80855b75ed0b5ec55f3c13018f8358197057def6357cefbcdb366ea",
        "rowsSha256": "74bf99bbfc720ba8599dc3614890400d3ab827fdb4704228bafa15879b269f39",
    },
    {
        "name": "input",
        "startVa": 0xDBCACC,
        "endVa": 0xDBCBA4,
        "entryCount": 18,
        "sha256": "aa21307e606f42be613372496ccf6acbbf518a0cbaf49aa2da5387c77cb8c60e",
        "rowsSha256": "50a851b6478a2835793690db3c3a76b8be7dc9974548798691ec69638408e8d7",
    },
    {
        "name": "tooltip-token",
        "startVa": 0xDBE308,
        "endVa": 0xDBE314,
        "entryCount": 1,
        "sha256": "e6bd9c8f2f13ade15656701c95bbf2ad6c5e3653501fe5fdb96448a774ae4acb",
        "rowsSha256": "4a1a9dd5263047054f941f41f66b1603f6bd65a7f6e8f83abdc24b470c51aed9",
    },
)

CODE_RANGES = (
    {
        "name": "controlbar-wnd-loader",
        "entryVa": 0x69C23E,
        "endVa": 0x69C269,
        "sha256": "016a86c931355de11d99023c93a53312e3f10750cd1c0d11d968fc143b20a8ee",
        "semantics": "indirect WND load through TheWindowManager vslot 0x7c; return site is 0x0069c25f",
    },
    {
        "name": "candidate-default-draw-override",
        "entryVa": 0x49DC05,
        "endVa": 0x49DC22,
        "sha256": "f8045f4611483eb3217ad81f811e5a9ad65db9f28e1aac222b362bf1b0301f71",
        "semantics": "calls the per-window +0x1e8 draw callback when present; this is not identity evidence for the authored built-in",
    },
    {
        "name": "candidate-default-draw-delegate",
        "entryVa": 0x49DC32,
        "endVa": 0x49DC58,
        "sha256": "feac94c0bf8d93e18d1bc7c242642959052ae7112acafff33c221d25f8af4112",
        "semantics": "tries the +0x1e8 override then delegates to the window draw object; candidate only",
    },
)

FULL_DESCRIPTOR_COUNT = 473
FULL_DESCRIPTOR_ROWS_SHA256 = (
    "75a223fadaa4305dd99582332ad8fd119fc265ca57844fb6438110f44917006f"
)
CANDIDATE_DEFAULT_DRAW_CALLERS = (
    0x49DDFB,
    0x49DE40,
    0x49DE81,
    0x4A33A2,
    0x4A3A57,
)


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _private_root(path: Path | str) -> Path:
    root = Path(path).resolve()
    if "workspace" not in {part.casefold() for part in root.parts} or not root.is_dir():
        raise ValueError("effective-assets must be an existing private directory")
    return root


def _read_c_string(data: bytes, va: int) -> str | None:
    if not 0xBBA000 <= va < 0xDA3783:
        return None
    start = va - RDATA_DELTA
    end = data.find(b"\0", start, start + 128)
    if end < 0:
        return None
    payload = data[start:end]
    if not payload or any(byte < 32 or byte >= 127 for byte in payload):
        return None
    return payload.decode("ascii")


def _descriptor_rows(data: bytes, start_va: int, end_va: int) -> list[dict[str, str]]:
    rows = []
    for va in range(start_va, end_va, 12):
        string_va, handler_va, terminator = struct.unpack_from(
            "<III", data, va - DATA_DELTA
        )
        name = _read_c_string(data, string_va)
        if name is None or not 0x401000 <= handler_va < 0xBB9CE2 or terminator != 0:
            raise ValueError(f"callback registry descriptor changed at 0x{va:08x}")
        rows.append(
            {
                "descriptorVa": f"0x{va:08x}",
                "name": name,
                "handlerVa": f"0x{handler_va:08x}",
            }
        )
    return rows


def _all_structural_descriptors(data: bytes) -> list[dict[str, str]]:
    rows = []
    for va in range(0xDA4000, 0xDDE000, 4):
        string_va, handler_va, terminator = struct.unpack_from(
            "<III", data, va - DATA_DELTA
        )
        name = _read_c_string(data, string_va)
        if name is not None and 0x401000 <= handler_va < 0xBB9CE2 and terminator == 0:
            rows.append(
                {
                    "descriptorVa": f"0x{va:08x}",
                    "name": name,
                    "handlerVa": f"0x{handler_va:08x}",
                }
            )
    return rows


def _direct_callers(data: bytes, target_va: int) -> list[int]:
    result = []
    text_start = 0x600
    text_size = 0x7B8CE2
    for offset in range(text_start, text_start + text_size - 5):
        if data[offset] != 0xE8:
            continue
        caller_va = 0x401000 + offset - text_start
        relative = struct.unpack_from("<i", data, offset + 1)[0]
        if caller_va + 5 + relative == target_va:
            result.append(caller_va)
    return result


def _binding_inventory(wnd: Mapping[str, Any]) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {name: [] for name in BUILT_INS}
    for window in wnd.get("windows", []):
        for kind, callback in window.get("callbacks", {}).items():
            if callback not in result:
                continue
            result[callback].append(
                "|".join(
                    (
                        str(kind),
                        str(window["index"]),
                        str(window["name"]),
                        str(window["windowType"]),
                        ",".join(window["status"]),
                        ",".join(window["style"]),
                    )
                )
            )
    if result != EXPECTED_BINDINGS:
        raise ValueError("built-in callback control bindings changed")
    return result


def _asset_payload(
    root: Path, manifest: Mapping[str, Any], virtual_path: str
) -> tuple[bytes, dict[str, Any]]:
    matches = [
        dict(row)
        for row in manifest.get("files", [])
        if str(row.get("path", "")).casefold() == virtual_path.casefold()
    ]
    if len(matches) != 1:
        raise ValueError(f"effective source winner changed: {virtual_path}")
    row = matches[0]
    expected = ASSETS[virtual_path]
    payload = (root / virtual_path).read_bytes()
    if (
        row.get("archive") != expected["archive"]
        or len(payload) != expected["size"]
        or _sha(payload) != expected["sha256"]
    ):
        raise ValueError(f"retail asset identity changed: {virtual_path}")
    return payload, row


def build_contract(
    effective_assets: Path | str, manifest: Mapping[str, Any], game_dat: Path | str
) -> dict[str, Any]:
    root = _private_root(effective_assets)
    wnd_payload, wnd_row = _asset_payload(
        root, manifest, "window/controlbar.wnd"
    )
    wnd = parse_wnd_layout(wnd_payload, "window/controlbar.wnd")
    bindings = _binding_inventory(wnd)
    data = Path(game_dat).read_bytes()
    if _sha(data) != GAME_DAT_SHA256:
        raise ValueError("BFME2 game.dat identity changed")

    code_rows = []
    for spec in CODE_RANGES:
        payload = data[
            int(spec["entryVa"]) - TEXT_DELTA : int(spec["endVa"]) - TEXT_DELTA
        ]
        if _sha(payload) != spec["sha256"]:
            raise ValueError(f"built-in oracle code changed: {spec['name']}")
        code_rows.append(
            {
                **spec,
                "entryVa": f"0x{int(spec['entryVa']):08x}",
                "endVa": f"0x{int(spec['endVa']):08x}",
                "byteLength": len(payload),
            }
        )

    table_rows = []
    registry_names: set[str] = set()
    for spec in REGISTRY_TABLES:
        payload = data[
            int(spec["startVa"]) - DATA_DELTA : int(spec["endVa"]) - DATA_DELTA
        ]
        rows = _descriptor_rows(data, int(spec["startVa"]), int(spec["endVa"]))
        if (
            len(rows) != spec["entryCount"]
            or _sha(payload) != spec["sha256"]
            or _sha(_canonical(rows)) != spec["rowsSha256"]
        ):
            raise ValueError(f"callback registry table changed: {spec['name']}")
        registry_names.update(row["name"] for row in rows)
        table_rows.append(
            {
                **spec,
                "startVa": f"0x{int(spec['startVa']):08x}",
                "endVa": f"0x{int(spec['endVa']):08x}",
                "byteLength": len(payload),
                "first": rows[0],
                "last": rows[-1],
            }
        )

    all_descriptors = _all_structural_descriptors(data)
    if (
        len(all_descriptors) != FULL_DESCRIPTOR_COUNT
        or _sha(_canonical(all_descriptors)) != FULL_DESCRIPTOR_ROWS_SHA256
    ):
        raise ValueError("full callback-like descriptor scan changed")
    authored_names = set(BUILT_INS)
    if authored_names & registry_names or authored_names & {
        row["name"] for row in all_descriptors
    }:
        raise ValueError("built-in unexpectedly acquired a game.dat descriptor")
    for name in BUILT_INS:
        if name.encode() in data or name.encode("utf-16le") in data:
            raise ValueError(f"built-in executable string unexpectedly present: {name}")
    callers = _direct_callers(data, 0x49DC32)
    if callers != list(CANDIDATE_DEFAULT_DRAW_CALLERS):
        raise ValueError("candidate default draw caller closure changed")

    callbacks = []
    for name, spec in BUILT_INS.items():
        occurrences = []
        start = 0
        needle = name.encode()
        while True:
            offset = wnd_payload.find(needle, start)
            if offset < 0:
                break
            context = wnd_payload[
                max(0, offset - 32) : offset + len(needle) + 32
            ]
            occurrences.append((offset, _sha(context)))
            start = offset + 1
        if tuple(occurrences) != spec["occurrences"]:
            raise ValueError(f"built-in WND occurrence identity changed: {name}")
        controls = []
        for key in bindings[name]:
            kind, index, control_id, control_type, status, style = key.split("|")
            controls.append(
                {
                    "kind": kind,
                    "windowIndex": int(index),
                    "controlId": control_id,
                    "controlType": control_type,
                    "status": status.split(","),
                    "style": style.split(","),
                }
            )
        callbacks.append(
            {
                "name": name,
                "kind": spec["kind"],
                "controls": controls,
                "wndOccurrences": [
                    {"sourceOffset": offset, "contextSha256": digest}
                    for offset, digest in occurrences
                ],
                "callbackSlot": {
                    "offset": spec["slotOffset"],
                    "setterVa": f"0x{spec['setterVa']:08x}",
                    "setterEndVa": f"0x{spec['setterEndVa']:08x}",
                    "setterSha256": spec["setterSha256"],
                },
                "staticDecision": {
                    "descriptorFound": False,
                    "handlerVa": None,
                    "noOpProven": False,
                    "delegationProven": False,
                    "implementationSafe": False,
                    "classification": "dynamic-target-unresolved",
                },
                "dynamicGates": list(spec["dynamicGates"]),
                "genericDispatchAllowed": False,
                "runtimeCapture": {
                    "loadReturnBreakpointVa": "0x0069c25f",
                    "slotRead": f"DWORD PTR [control+{spec['slotOffset']}]",
                    "procedure": [
                        "break at 0x0069c25f immediately after the exact ControlBar.wnd load",
                        "resolve only the listed control identity and read its exact callback slot",
                        "if zero, hardware-watch that four-byte slot and reload; otherwise execute-break the observed target",
                        "trigger only this callback kind and record target VA, entry stack words, return EAX where applicable, writes, calls, and ordered draw commands",
                        "hash the resolved full handler range before promoting an adapter",
                    ],
                },
            }
        )

    result: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": 0,
        "source": {
            "gameDatSha256": GAME_DAT_SHA256,
            "wnd": {
                "virtualPath": wnd_row["path"],
                "archive": wnd_row["archive"],
                "byteLength": len(wnd_payload),
                "sha256": _sha(wnd_payload),
                "windowCount": wnd["windowCount"],
            },
        },
        "summary": {
            "builtInCount": 4,
            "implementationSafeCount": 0,
            "unresolvedDynamicTargetCount": 4,
            "noOpProvenCount": 0,
            "delegationProvenCount": 0,
            "genericDispatchAllowed": False,
            "runtimeCaptureRequired": True,
        },
        "loaderAndCandidateCode": code_rows,
        "registryTables": table_rows,
        "fullStructuralDescriptorScan": {
            "descriptorCount": len(all_descriptors),
            "rowsSha256": _sha(_canonical(all_descriptors)),
            "builtInDescriptorCount": 0,
            "asciiOrUtf16BuiltInNameCount": 0,
        },
        "defaultDrawCandidate": {
            "candidateVa": "0x0049dc32",
            "directCallers": [f"0x{va:08x}" for va in callers],
            "identityTieToAuthoredBuiltIn": False,
            "implementationSafe": False,
            "reason": "the exact helper delegates default drawing but no static table or loader edge binds it to W3DGameWinDefaultDraw",
        },
        "callbacks": callbacks,
        "recommendation": {
            "implementationSafeCallbacks": [],
            "retainBlockedCallbacks": list(BUILT_INS),
            "deleteBroadBuiltInAssumption": False,
            "genericDispatcherAllowed": False,
            "nextGate": "capture each exact callback slot target with the callback-specific recipe; do not substitute Generals or OpenSAGE behavior",
        },
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
    output = args.output.resolve()
    if "workspace" not in {part.casefold() for part in output.parts}:
        raise ValueError("built-in oracle output must remain under workspace")
    result = build_contract(
        args.effective_assets,
        json.loads(args.manifest.read_text(encoding="utf-8")),
        args.game_dat,
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(_canonical(result))
    print(
        json.dumps(
            {"aggregateSha256": result["aggregateSha256"], "output": str(output)},
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
