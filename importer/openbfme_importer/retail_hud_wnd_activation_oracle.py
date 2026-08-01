"""Prove whether BFME2 1.06 activates ControlBar.wnd beside Palantir.apt."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from typing import Any, Mapping

from .sage_apt import parse_wnd_layout


SCHEMA = "openbfme.private-hud-wnd-activation-oracle"
GAME_DAT_SHA256 = "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640"
TEXT_VA_FILE_DELTA = 0x400A00
RDATA_VA_FILE_DELTA = 0x400C00

ASSETS = {
    "window/controlbar.wnd": {
        "archive": "window.big",
        "size": 289_881,
        "sha256": "a509730457224a111af8022df6d0ef373fcaa5d91a102bc15bccf5fc1a54ced6",
    },
    "data/ini/controlbarresizer.ini": {
        "archive": "ini.big",
        "size": 9_398,
        "sha256": "6a7c3f69410c570392732bc92578234f3ae74fb6f0156b396d14243aac89441c",
    },
}

CODE_RANGES: tuple[dict[str, Any], ...] = (
    {
        "name": "bfme2-controlbar-manager-constructor",
        "entryVa": 0x48EF3F,
        "endVa": 0x48EF81,
        "sha256": "8f0c33e01ce21b3fa29c1fb14f57851962574f3a31c56cd0914e385c663e5967",
        "proof": "installs BFME2 vtable 0x00bc7a88 after the shared manager constructor",
    },
    {
        "name": "bfme2-init-thunk",
        "entryVa": 0x48F0BB,
        "endVa": 0x48F0C0,
        "sha256": "bc3a41e903971e7321f3f9295750b89f9ef73a84e2e34edce0953e4d4acc8cf3",
        "proof": "BFME2 vtable init slot tail-jumps to shared init 0x0069e2de",
    },
    {
        "name": "controlbar-manager-init",
        "entryVa": 0x69E2DE,
        "endVa": 0x69E656,
        "sha256": "9f7c8211a9bb03c65f7ff96a9f5b117cf8b474eae592c260f6eb3eebf3873263",
        "proof": "same init calls WND loader and BFME2 vslot 0x1dc Apt factory",
    },
    {
        "name": "controlbar-wnd-loader",
        "entryVa": 0x69C23E,
        "endVa": 0x69C269,
        "sha256": "016a86c931355de11d99023c93a53312e3f10750cd1c0d11d968fc143b20a8ee",
        "proof": "pushes exact string VA 0x00bfd060 (ControlBar.wnd) into WND loader",
    },
    {
        "name": "apt-palantir-factory",
        "entryVa": 0x48F32C,
        "endVa": 0x48F371,
        "sha256": "0b879ed0508b3e619b65a5d2edf71a4fa8180b7f119a5f3543e52ad7270d6c4d",
        "proof": "BFME2 vslot 0x1dc allocates and retains the Apt control-bar object",
    },
    {
        "name": "apt-palantir-constructor",
        "entryVa": 0x6D638E,
        "endVa": 0x6D644C,
        "sha256": "4376b46bb1eb90e02006581af337c37cd5dc4eca6d5f5f17925cea2f14966ca5",
        "proof": "constructs the Apt movie from global string object 0x00dff030",
    },
)

EXPECTED_CALLBACKS = (
    "BeaconWindowInput",
    "ControlBarInput",
    "ControlBarObserverSystem",
    "ControlBarSystem",
    "GameWinBlockInput",
    "GameWinDefaultInput",
    "GameWinDefaultSystem",
    "GameWinDefaultTooltip",
    "LeftHUDInput",
    "PassSelectedButtonsToParentSystem",
    "W3DCommandBarBackgroundDraw",
    "W3DCommandBarForegroundDraw",
    "W3DCommandBarGenExpDraw",
    "W3DCommandBarGridDraw",
    "W3DCommandBarTopDraw",
    "W3DGadgetPushButtonImageDraw",
    "W3DGameWinDefaultDraw",
    "W3DLeftHUDDraw",
    "W3DNoDraw",
    "W3DPowerDraw",
    "W3DRightHUDDraw",
)


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _private_root(path: Path | str) -> Path:
    root = Path(path).resolve()
    if ".private" not in {part.casefold() for part in root.parts} or not root.is_dir():
        raise ValueError("effective-assets must be an existing private directory")
    return root


def _source_row(manifest: Mapping[str, Any], virtual_path: str) -> dict[str, Any]:
    rows = [
        row
        for row in manifest.get("files", [])
        if str(row.get("path", "")).casefold() == virtual_path.casefold()
    ]
    if len(rows) != 1:
        raise ValueError(f"effective source winner changed: {virtual_path}")
    return dict(rows[0])


def _read_asset(
    root: Path, manifest: Mapping[str, Any], virtual_path: str
) -> tuple[bytes, dict[str, Any]]:
    expected = ASSETS[virtual_path]
    row = _source_row(manifest, virtual_path)
    payload = (root / virtual_path).read_bytes()
    if (
        row.get("archive") != expected["archive"]
        or row.get("size") != expected["size"]
        or row.get("sha256") != expected["sha256"]
        or len(payload) != expected["size"]
        or _sha(payload) != expected["sha256"]
    ):
        raise ValueError(f"retail asset identity changed: {virtual_path}")
    return payload, row


def _game_dat_evidence(path: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    data = path.read_bytes()
    if _sha(data) != GAME_DAT_SHA256:
        raise ValueError("BFME2 game.dat identity changed")
    code = []
    for row in CODE_RANGES:
        payload = data[
            row["entryVa"] - TEXT_VA_FILE_DELTA : row["endVa"] - TEXT_VA_FILE_DELTA
        ]
        if _sha(payload) != row["sha256"]:
            raise ValueError(f"retail activation code changed: {row['name']}")
        code.append(
            {
                **row,
                "entryVa": f"0x{row['entryVa']:08x}",
                "endVa": f"0x{row['endVa']:08x}",
                "byteLength": len(payload),
            }
        )

    def dword(va: int) -> int:
        return struct.unpack_from("<I", data, va - RDATA_VA_FILE_DELTA)[0]

    if dword(0xBC7A8C) != 0x48F0BB or dword(0xBC7C64) != 0x48F32C:
        raise ValueError("BFME2 control-bar vtable routing changed")
    if (
        data[0xBFD060 - RDATA_VA_FILE_DELTA : 0xBFD060 - RDATA_VA_FILE_DELTA + 15]
        != b"ControlBar.wnd\0"
    ):
        raise ValueError("ControlBar.wnd executable string changed")
    if (
        data[0xC031DC - RDATA_VA_FILE_DELTA : 0xC031DC - RDATA_VA_FILE_DELTA + 13]
        != b"Palantir.apt\0"
    ):
        raise ValueError("Palantir.apt executable string changed")
    decisive = data[0x69E5A5 - TEXT_VA_FILE_DELTA : 0x69E5FB - TEXT_VA_FILE_DELTA]
    if (
        _sha(decisive)
        != "2f54f844726ee9bb43d2de977409908f000059203ea7d6a7dac9f22bfca2c65f"
    ):
        raise ValueError("shared WND-plus-Apt init sequence changed")
    return code, {
        "managerVtableVa": "0x00bc7a88",
        "initSlot": {
            "slotOffset": "0x004",
            "targetVa": "0x0048f0bb",
            "tailTargetVa": "0x0069e2de",
        },
        "wndLoadCallVa": "0x0069e5ad",
        "wndLoaderVa": "0x0069c23e",
        "aptFactorySlot": {"slotOffset": "0x1dc", "targetVa": "0x0048f32c"},
        "aptFactoryDispatchVa": "0x0069e5f5",
        "wndFilenameVa": "0x00bfd060",
        "aptFilenameVa": "0x00c031dc",
        "decisiveSequenceSha256": _sha(decisive),
    }


def _opensage_evidence(root: Path | None) -> list[dict[str, Any]]:
    if root is None:
        return []
    expected = {
        "src/OpenSage.Mods.Bfme2/Bfme2Definition.cs": "3dc7f1a28df8a28c2432fa4a54c885f622c6915962dfe3cd93a1c91200ad72fb",
        "src/OpenSage.Mods.Bfme/Gui/AptControlBarSource.cs": "e9a33eb13a4130118ba35f3bd6dc81a2fe9938f2d7c0e02b7fd7c4c1f27cb585",
    }
    result = []
    for relative, digest in expected.items():
        payload = (root / relative).read_bytes()
        if _sha(payload) != digest:
            raise ValueError(f"OpenSAGE observation changed: {relative}")
        result.append({"path": relative, "byteLength": len(payload), "sha256": digest})
    return result


def build_contract(
    effective_assets: Path | str,
    manifest: Mapping[str, Any],
    game_dat: Path | str,
    *,
    opensage_root: Path | None = None,
) -> dict[str, Any]:
    root = _private_root(effective_assets)
    wnd_payload, wnd_row = _read_asset(root, manifest, "window/controlbar.wnd")
    resizer_payload, resizer_row = _read_asset(
        root, manifest, "data/ini/controlbarresizer.ini"
    )
    wnd = parse_wnd_layout(wnd_payload, "window/controlbar.wnd")
    if wnd["windowCount"] != 87 or tuple(wnd["callbacks"]) != EXPECTED_CALLBACKS:
        raise ValueError("ControlBar.wnd tree or callback identities changed")
    references = sorted(
        set(
            re.findall(
                r"ControlBar\.wnd:[A-Za-z0-9_%]+",
                resizer_payload.decode("latin-1"),
                re.I,
            )
        ),
        key=lambda value: (value.casefold(), value),
    )
    if (
        len(references) != 84
        or _sha(_canonical(references))
        != "d88db2cab8d6e5991e573a6506dda0b6a1c1013f2220e4c27e96aed8b33bc145"
    ):
        raise ValueError("ControlBarResizer exact WND reference closure changed")
    code, route = _game_dat_evidence(Path(game_dat))
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "source": {
            "gameDatSha256": GAME_DAT_SHA256,
            "effectiveManifestAggregateSha256": manifest.get("aggregate_sha256"),
            "opensageObservationOnly": _opensage_evidence(opensage_root),
        },
        "summary": {
            "decision": "active-companion-not-candidate-dead",
            "wndLayoutLoaded": True,
            "aptPalantirConstructed": True,
            "wndCallbackIdentityCount": 21,
            "runtimeTraceRequiredForActivation": False,
            "wndSemanticBlockerRetained": True,
        },
        "staticActivationRoute": route,
        "retailCode": code,
        "wnd": {
            "source": {
                "virtualPath": wnd_row["path"],
                "archive": wnd_row["archive"],
                "byteLength": wnd_row["size"],
                "sha256": wnd_row["sha256"],
            },
            "windowCount": wnd["windowCount"],
            "callbacks": wnd["callbacks"],
            "callbackSemantics": "identities are live-bound by the loaded WND; individual invocation is event-dependent and semantics remain fail-closed",
        },
        "resizer": {
            "source": {
                "virtualPath": resizer_row["path"],
                "archive": resizer_row["archive"],
                "byteLength": resizer_row["size"],
                "sha256": resizer_row["sha256"],
            },
            "exactControlReferenceCount": len(references),
            "referencesSha256": _sha(_canonical(references)),
            "references": references,
        },
        "recommendation": {
            "deleteCandidateDeadAssumption": True,
            "retainWndPayload": True,
            "retainAll21CallbackIdentities": True,
            "nextGate": "implement or trace exact BFME2 callback semantics; do not substitute Generals callbacks or generic dispatch",
            "smallestOptionalSemanticTrace": "break on 0x0069c23e and each selected callback registration/invocation while starting one BFME2 1.06 skirmish; activation itself no longer needs a trace",
        },
    }
    result["aggregateSha256"] = _sha(_canonical(result))
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--effective-assets", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--game-dat", type=Path, required=True)
    parser.add_argument("--opensage-root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    result = build_contract(
        args.effective_assets,
        json.loads(args.manifest.read_text(encoding="utf-8")),
        args.game_dat,
        opensage_root=args.opensage_root,
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
