"""Verify BFME2's exact Palantir live-text formatting routines.

The retail executable is observation evidence only.  This module records no
retail payload and emits only hashes, addresses, format strings, and typed
formatting rules needed by the Godot HUD bridge.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from typing import Any


SCHEMA = "openbfme.retail-hud-live-text-oracle"
SCHEMA_VERSION = 0
EXPECTED_GAME_DAT_SHA256 = (
    "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640"
)
EXPECTED_GAME_DAT_BYTES = 10_969_600

_ROUTINES = (
    {
        "id": "palantir-resources",
        "variable": "APT:PalantirResources",
        "startVa": 0x006D46AD,
        "endVa": 0x006D4748,
        "sha256": "5f6ce8ff47a0270bf6b3b2547e7e285e39c0447ae3834470d2ec282cda6c37df",
        "format": "%d",
        "fallback": "single-space-when-negative",
    },
    {
        "id": "palantir-command-points",
        "variable": "APT:PalantirCommandPoints",
        "startVa": 0x007FEECB,
        "endVa": 0x007FEF80,
        "sha256": "809daf295462787fb38184d73a434437a6c987aeb3051926fc6d96846b2453cb",
        "format": "%d/%d",
        "fallback": "current-only-when-cap-negative-space-when-current-negative",
    },
    {
        "id": "palantir-resource-multiplier",
        "variable": "APT:PalantirResourceMultiplier",
        "startVa": 0x007FEF80,
        "endVa": 0x007FF02E,
        "sha256": "109403f651e8fbb1ab16188d4d416afecded55a6e53079ae43f2244d1892fdf1",
        "format": "x%g",
        "fallback": "single-space-when-exactly-one",
    },
)


class HudLiveTextOracleError(ValueError):
    """Raised when the retail executable does not match the proven oracle."""


def format_resources(value: int) -> str:
    """Apply the exact retail resource-count branch."""

    return str(value) if value >= 0 else " "


def format_command_points(current: int, cap: int) -> str:
    """Apply the exact retail command-point branch."""

    if current < 0:
        return " "
    return f"{current}/{cap}" if cap >= 0 else str(current)


def format_resource_multiplier(value: float) -> str:
    """Apply the finite, gameplay-reachable retail ``x%g`` branch."""

    if not math.isfinite(value):
        raise HudLiveTextOracleError("resource multiplier must be finite")
    if value == 1.0:
        return " "
    return "x" + format(value, "g")


def _pe_sections(payload: bytes) -> list[dict[str, int | str]]:
    if len(payload) < 0x100 or payload[:2] != b"MZ":
        raise HudLiveTextOracleError("game.dat is not a PE image")
    pe_offset = struct.unpack_from("<I", payload, 0x3C)[0]
    if pe_offset + 24 > len(payload) or payload[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise HudLiveTextOracleError("game.dat PE header is invalid")
    section_count = struct.unpack_from("<H", payload, pe_offset + 6)[0]
    optional_bytes = struct.unpack_from("<H", payload, pe_offset + 20)[0]
    table = pe_offset + 24 + optional_bytes
    sections: list[dict[str, int | str]] = []
    for index in range(section_count):
        offset = table + index * 40
        if offset + 40 > len(payload):
            raise HudLiveTextOracleError("game.dat section table is truncated")
        name = payload[offset : offset + 8].split(b"\0", 1)[0].decode("ascii", "strict")
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<4I", payload, offset + 8
        )
        sections.append(
            {
                "name": name,
                "virtualSize": virtual_size,
                "virtualAddress": virtual_address,
                "rawSize": raw_size,
                "rawOffset": raw_offset,
            }
        )
    return sections


def _va_bytes(payload: bytes, sections: list[dict[str, int | str]], va: int, size: int) -> bytes:
    image_base = 0x00400000
    rva = va - image_base
    for section in sections:
        start = int(section["virtualAddress"])
        extent = max(int(section["virtualSize"]), int(section["rawSize"]))
        if start <= rva and rva + size <= start + extent:
            offset = int(section["rawOffset"]) + rva - start
            value = payload[offset : offset + size]
            if len(value) != size:
                break
            return value
    raise HudLiveTextOracleError(f"virtual address range is not file-backed: 0x{va:08x}")


def inspect_game_dat(path: Path | str) -> dict[str, Any]:
    source = Path(path).expanduser().resolve()
    payload = source.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    if len(payload) != EXPECTED_GAME_DAT_BYTES or digest != EXPECTED_GAME_DAT_SHA256:
        raise HudLiveTextOracleError("game.dat identity is not the BFME2 1.06 oracle")
    sections = _pe_sections(payload)
    routines: list[dict[str, Any]] = []
    for expected in _ROUTINES:
        start = int(expected["startVa"])
        end = int(expected["endVa"])
        body = _va_bytes(payload, sections, start, end - start)
        body_sha256 = hashlib.sha256(body).hexdigest()
        if body_sha256 != expected["sha256"]:
            raise HudLiveTextOracleError(f"{expected['id']} code identity changed")
        routines.append(
            {
                **expected,
                "byteLength": len(body),
                "startVa": f"0x{start:08x}",
                "endVa": f"0x{end:08x}",
            }
        )
    contract: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "gameDat": {"byteLength": len(payload), "sha256": digest},
        "routines": routines,
        "godotBindings": {
            "$PalantirResources": "resources",
            "$PalantirResourceMultiplier": "resourceMultiplier",
            "$PalantirCommandPoints": "commandPointsCurrent/commandPointsCap",
        },
        "nonfiniteMultiplierPolicy": "fail-closed-not-gameplay-reachable",
    }
    canonical = json.dumps(contract, sort_keys=True, separators=(",", ":")).encode()
    contract["aggregateSha256"] = hashlib.sha256(canonical).hexdigest()
    return contract


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game_dat", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    contract = inspect_game_dat(args.game_dat)
    rendered = json.dumps(contract, indent=2, sort_keys=True) + "\n"
    if args.output is None:
        print(rendered, end="")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
