"""Build a payload-free BFME2 Palantir text raster/layout oracle.

The contract is identity-bound to the BFME2 1.06 Palantir APT triplet, its
winning Albertus MT OTF, and the retail executable.  It deliberately separates
layout semantics proven by static retail code from raster/blend behavior that
still needs a rendered retail oracle.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
from typing import Any, Mapping

from .sage_apt import parse_apt_constants, parse_apt_dat, parse_apt_movie


SCHEMA = "openbfme.private-hud-text-raster-oracle"
SCHEMA_VERSION = 0

SOURCE_IDENTITIES: dict[str, dict[str, Any]] = {
    "Palantir.apt": {
        "archive": "apt/palantir.big",
        "precedence": 51,
        "entryOffset": 3204,
        "byteLength": 378_173,
        "sha256": "c1f500847f0c77d4c6504edf79113b5723300165bebd42b4dafda479516f5140",
    },
    "Palantir.const": {
        "archive": "apt/palantir.big",
        "precedence": 51,
        "entryOffset": 381_380,
        "byteLength": 10_260,
        "sha256": "f07e24e3b70e286d491652cc827aef904a2ccabf54107d4f1bfc3030beee8fd9",
    },
    "Palantir.dat": {
        "archive": "apt/palantir.big",
        "precedence": 51,
        "entryOffset": 391_640,
        "byteLength": 586,
        "sha256": "d8e8964711e4061b0643dd0dd3de1876b7326cee6d60e11214793b5d483f3ae4",
    },
    "albertusmt.otf": {
        "archive": "_patch103.big",
        "precedence": 0,
        "entryOffset": 2510,
        "byteLength": 24_712,
        "sha256": "6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0",
    },
    "game.dat": {
        "byteLength": 10_969_600,
        "sha256": "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640",
    },
}

TEXT_EXPECTATIONS: dict[int, dict[str, Any]] = {
    130: {
        "sourceOffset": 5056,
        "definitionSha256": "74e0ded9d73c9427b062861b50eb02de878a07f9b390564a10115c418fdd9ff6",
        "bounds": [-2.0, -2.0, 50.20000076293945, 21.149999618530273],
        "alignmentCode": 0,
        "alignment": "right",
        "placeholder": "999999",
        "runtimeVariable": "$PalantirResources",
        "instancePath": "layer:1:Palantir/102/5/3",
        "instanceDepths": [5, 3],
        "matrix": [1.0019073486328125, 0.0, 0.0, 1.0017348445253447],
        "translation": [56.70000058412552, 722.2000243663788],
    },
    132: {
        "sourceOffset": 5136,
        "definitionSha256": "983ecd3faceaaaba22b6957063146357943eae4794b1fe0ab9bb9494de2febaf",
        "bounds": [-2.0, -2.0, 25.5, 21.149999618530273],
        "alignmentCode": 2,
        "alignment": "left",
        "placeholder": "x99",
        "runtimeVariable": "$PalantirResourceMultiplier",
        "instancePath": "layer:1:Palantir/102/9/3",
        "instanceDepths": [9, 3],
        "matrix": [1.0018157958984375, 0.0, 0.0, 1.0039100646972656],
        "translation": [111.60000228881836, 722.000024408102],
    },
    134: {
        "sourceOffset": 5216,
        "definitionSha256": "0d5655b96e05c53c93c0374ad74cadcd9924179503b5f826fadfa34b094b4dea",
        "bounds": [-2.0, -2.0, 58.95000076293945, 21.149999618530273],
        "alignmentCode": 1,
        "alignment": "center",
        "placeholder": "999/999",
        "runtimeVariable": "$PalantirCommandPoints",
        "instancePath": "layer:1:Palantir/102/13/3",
        "instanceDepths": [13, 3],
        "matrix": [1.0007171630859375, 0.0, 0.0, 1.0017348445253447],
        "translation": [141.60000228881836, 722.2000243663788],
    },
}

FONT_EXPECTATION = {
    "characterId": 63,
    "sourceOffset": 3736,
    "definitionByteLength": 20,
    "definitionSha256": "b49b2cdcdee84450c6870316757b553b5d2a1630823ab7bf934e315c870a115d",
    "name": "Albertus MT",
    "glyphCount": 0,
    "externalFont": "albertusmt.otf",
    "externalFamily": "Albertus MT",
    "externalSubfamily": "Regular",
    "postScriptName": "AlbertusMT",
    "unitsPerEm": 1000,
    "hheaAscent": 750,
    "hheaDescent": -250,
    "hheaLineGap": 196,
    "winAscent": 946,
    "winDescent": 250,
    "glyphCountInOtf": 298,
    "outlineFormat": "CFF",
    "embeddedBitmapStrikes": 0,
}

GAME_CODE_RANGES: tuple[dict[str, Any], ...] = (
    {
        "id": "apt-external-text-layout",
        "startVa": 0x00AE1260,
        "endVa": 0x00AE1532,
        "sha256": "7fe24c251afa315e50ace1acb0aa93f42600379c82b197dcf6f4346280587c93",
        "proves": "rectangle layout, three-way horizontal alignment, 0.5 center factor, host string allocation",
    },
    {
        "id": "apt-dynamic-text-draw-dispatch",
        "startVa": 0x00AE18A2,
        "endVa": 0x00AE18EE,
        "sha256": "26efd3866819c0b68adfccb3e4e688a0d5fcd4702f12991131f8a51305b27f32",
        "proves": "TextInst calls gAptFuncs.pfnDrawString; no embedded glyph traversal on this branch",
    },
    {
        "id": "host-external-string-draw",
        "startVa": 0x004A9570,
        "endVa": 0x004A97A9,
        "sha256": "88c5a87809067576f37b0ec5391638be95a4493e18dc809223a075aae1d2ad80",
        "proves": "transformed rectangle, alignment offsets, vertical centering branch, color transform, integer draw origin",
    },
    {
        "id": "host-external-string-create",
        "startVa": 0x004AA966,
        "endVa": 0x004AACA9,
        "sha256": "28686cca7802610bac59a6a180b00936cecaaae7980cadd03c5683a28f1a3aed",
        "proves": "external font name and fontHeight reach the font service; multiline and word-wrap configure layout",
    },
    {
        "id": "host-abgr-color-transform",
        "startVa": 0x004A90B0,
        "endVa": 0x004A9192,
        "sha256": "4714822691806323c6188c758994c54b74c6334489d077cd496e9f6a6c3330ea",
        "proves": "all four packed ABGR channels use multiplicative and additive transform components",
    },
    {
        "id": "embedded-glyph-advance-not-palantir-path",
        "startVa": 0x00AE19D7,
        "endVa": 0x00AE1B31,
        "sha256": "8a8b03103edab42bfa3f9399d51e1397b9c2e9997a0a01f042d00a5082f37c48",
        "proves": "0.05 twip conversion belongs to embedded glyph advance; Palantir glyphCount=0 excludes this path",
    },
)

OPENSAGE_FILES: tuple[dict[str, Any], ...] = (
    {
        "path": "src/OpenSage.Game/Data/Apt/Characters/Text.cs",
        "sha256": "f2cc22dcbec8f6e79f2b49ef8200dd10c241623c82c43759eaacb3190585a659",
        "lines": [8, 42],
        "use": "APT record layout observation",
    },
    {
        "path": "src/OpenSage.Game/Data/Apt/Characters/Font.cs",
        "sha256": "80602bd3d3effd5110c1c3560991a7696167d1ac11199503474f66f7b1cfc234",
        "lines": [8, 43],
        "use": "font record layout observation",
    },
    {
        "path": "src/OpenSage.Game/Gui/Apt/AptRenderingContext.cs",
        "sha256": "3fb0d34f3c729fde7f46a25435f81b5059a584db3240fa6304facb7f17de984b",
        "lines": [58, 146],
        "use": "non-authoritative implementation comparison; Arial and forced-center behavior rejected",
    },
)


class HudTextRasterOracleError(ValueError):
    """Raised when an identity or exact retail fact changes."""


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical_sha(value: Mapping[str, Any]) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return _sha(payload)


def _verify_source(label: str, data: bytes) -> dict[str, Any]:
    expected = SOURCE_IDENTITIES[label]
    if len(data) != expected["byteLength"] or _sha(data) != expected["sha256"]:
        raise HudTextRasterOracleError(f"{label} identity changed")
    return {"virtualPath": label, **expected}


def _c_string(data: bytes, offset: int, label: str) -> str:
    if not 0 < offset < len(data):
        raise HudTextRasterOracleError(f"{label} string pointer is invalid")
    end = data.find(b"\0", offset)
    if end < 0:
        raise HudTextRasterOracleError(f"{label} string is unterminated")
    return data[offset:end].decode("utf-8", "strict")


def _character_offsets(movie: Mapping[str, Any]) -> dict[int, int]:
    result: dict[int, int] = {}
    for row in movie["characters"]:
        if row.get("kind") != "null":
            result[int(row["characterId"])] = int(row["sourceOffset"])
    return result


def _parse_font(apt: bytes, offset: int) -> dict[str, Any]:
    if struct.unpack_from("<II", apt, offset) != (3, 0x09876543):
        raise HudTextRasterOracleError("Palantir font character header changed")
    name_pointer, glyph_count, glyph_pointer = struct.unpack_from("<III", apt, offset + 8)
    if glyph_count != 0 or glyph_pointer > len(apt):
        raise HudTextRasterOracleError("Palantir external font closure changed")
    raw = apt[offset : offset + 20]
    return {
        "characterId": 63,
        "sourceOffset": offset,
        "definitionByteLength": len(raw),
        "definitionSha256": _sha(raw),
        "name": _c_string(apt, name_pointer, "font name"),
        "glyphCount": glyph_count,
    }


def _parse_text(apt: bytes, character_id: int, offset: int) -> dict[str, Any]:
    if struct.unpack_from("<II", apt, offset) != (2, 0x09876543):
        raise HudTextRasterOracleError(f"Palantir text {character_id} header changed")
    bounds = list(struct.unpack_from("<4f", apt, offset + 8))
    font_id, alignment = struct.unpack_from("<II", apt, offset + 24)
    rgba = list(apt[offset + 32 : offset + 36])
    font_height = struct.unpack_from("<f", apt, offset + 36)[0]
    read_only, multiline, word_wrap = struct.unpack_from("<III", apt, offset + 40)
    content_pointer, variable_pointer = struct.unpack_from("<II", apt, offset + 52)
    raw = apt[offset : offset + 60]
    return {
        "characterId": character_id,
        "sourceOffset": offset,
        "definitionByteLength": len(raw),
        "definitionSha256": _sha(raw),
        "bounds": bounds,
        "fontCharacterId": font_id,
        "alignmentCode": alignment,
        "colorRgba8": rgba,
        "packedAbgr32": f"0x{int.from_bytes(bytes(rgba), 'little'):08x}",
        "fontHeight": font_height,
        "readOnly": bool(read_only),
        "multiline": bool(multiline),
        "wordWrap": bool(word_wrap),
        "placeholder": _c_string(apt, content_pointer, f"text {character_id} content"),
        "variableName": _c_string(apt, variable_pointer, f"text {character_id} variable"),
    }


def _pe_sections(payload: bytes) -> list[dict[str, int]]:
    if payload[:2] != b"MZ":
        raise HudTextRasterOracleError("game.dat is not a PE image")
    pe_offset = struct.unpack_from("<I", payload, 0x3C)[0]
    if payload[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise HudTextRasterOracleError("game.dat PE header changed")
    count = struct.unpack_from("<H", payload, pe_offset + 6)[0]
    optional_size = struct.unpack_from("<H", payload, pe_offset + 20)[0]
    table = pe_offset + 24 + optional_size
    sections: list[dict[str, int]] = []
    for index in range(count):
        start = table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<4I", payload, start + 8
        )
        sections.append(
            {
                "virtualSize": virtual_size,
                "virtualAddress": virtual_address,
                "rawSize": raw_size,
                "rawOffset": raw_offset,
            }
        )
    return sections


def _va_bytes(payload: bytes, sections: list[dict[str, int]], start: int, size: int) -> bytes:
    rva = start - 0x00400000
    for section in sections:
        section_start = section["virtualAddress"]
        extent = max(section["virtualSize"], section["rawSize"])
        if section_start <= rva and rva + size <= section_start + extent:
            offset = section["rawOffset"] + rva - section_start
            return payload[offset : offset + size]
    raise HudTextRasterOracleError(f"game.dat VA range 0x{start:08x} is not file-backed")


def _verify_code_ranges(game_dat: bytes) -> list[dict[str, Any]]:
    sections = _pe_sections(game_dat)
    result: list[dict[str, Any]] = []
    for expected in GAME_CODE_RANGES:
        start, end = int(expected["startVa"]), int(expected["endVa"])
        body = _va_bytes(game_dat, sections, start, end - start)
        if _sha(body) != expected["sha256"]:
            raise HudTextRasterOracleError(f"game.dat {expected['id']} code changed")
        result.append(
            {
                **expected,
                "startVa": f"0x{start:08x}",
                "endVa": f"0x{end:08x}",
                "byteLength": len(body),
            }
        )
    return result


def _opensage_observation(root: Path | None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "authority": "observation-only-not-parity-authority",
        "commit": "588ac477367a0022adf29f20a084e8873014e6ce",
        "files": list(OPENSAGE_FILES),
        "rejectedFallbacks": ["Arial", "forced-center-alignment"],
        "identityVerified": False,
    }
    if root is None or not root.is_dir():
        return result
    for row in OPENSAGE_FILES:
        path = root / str(row["path"])
        if not path.is_file() or _sha(path.read_bytes()) != row["sha256"]:
            raise HudTextRasterOracleError(f"OpenSAGE observation changed: {row['path']}")
    result["identityVerified"] = True
    return result


def build_contract(
    apt: bytes,
    const: bytes,
    dat: bytes,
    otf: bytes,
    game_dat: bytes,
    *,
    opensage_root: Path | None = None,
) -> dict[str, Any]:
    """Validate private inputs and return a deterministic payload-free contract."""

    sources = [
        _verify_source("Palantir.apt", apt),
        _verify_source("Palantir.const", const),
        _verify_source("Palantir.dat", dat),
        _verify_source("albertusmt.otf", otf),
        _verify_source("game.dat", game_dat),
    ]
    constants = parse_apt_constants(const, "Palantir.const")
    movie = parse_apt_movie(apt, constants, "Palantir.apt")
    atlas_map = parse_apt_dat(dat, "Palantir.dat")
    if movie["root"]["width"] != 1024 or movie["root"]["height"] != 768:
        raise HudTextRasterOracleError("Palantir authored resolution changed")
    offsets = _character_offsets(movie)

    font = _parse_font(apt, offsets[63])
    for key in ("sourceOffset", "definitionSha256", "name", "glyphCount"):
        if font[key] != FONT_EXPECTATION[key]:
            raise HudTextRasterOracleError(f"Palantir font {key} changed")
    font.update({key: value for key, value in FONT_EXPECTATION.items() if key not in font})

    texts: list[dict[str, Any]] = []
    for character_id, expected in sorted(TEXT_EXPECTATIONS.items()):
        row = _parse_text(apt, character_id, offsets[character_id])
        checks = {
            "sourceOffset": expected["sourceOffset"],
            "definitionSha256": expected["definitionSha256"],
            "bounds": expected["bounds"],
            "alignmentCode": expected["alignmentCode"],
            "placeholder": expected["placeholder"],
            "fontCharacterId": 63,
            "colorRgba8": [0, 204, 255, 255],
            "fontHeight": 14.0,
            "readOnly": True,
            "multiline": False,
            "wordWrap": False,
            "variableName": "stringName",
        }
        for key, value in checks.items():
            if row[key] != value:
                raise HudTextRasterOracleError(f"Palantir text {character_id} {key} changed")
        row.update(
            {
                "alignment": expected["alignment"],
                "runtimeVariable": expected["runtimeVariable"],
                "instancePath": expected["instancePath"],
                "instanceDepths": expected["instanceDepths"],
                "matrix": expected["matrix"],
                "translation": expected["translation"],
            }
        )
        texts.append(row)

    proven = [
        {
            "area": "coordinate-units",
            "finding": "external-font bounds, placements, and fontHeight remain 32-bit authored-pixel floats; the 0.05 twip factor is confined to the excluded embedded-glyph branch",
            "status": "retail-static-proven",
        },
        {
            "area": "bounds-and-origin",
            "finding": "bounds are an alignment and vertical-centering rectangle; the host transforms its endpoints and truncates the final x/y draw origin toward zero",
            "status": "retail-static-proven",
        },
        {
            "area": "horizontal-alignment",
            "finding": "APT 0=right, 1=center, 2=left; center uses exactly 0.5 of remaining width",
            "status": "retail-static-proven",
        },
        {
            "area": "vertical-layout",
            "finding": "these non-multiline, non-wrapped strings use top + (boxHeight - measuredTextHeight) * 0.5 before integer truncation",
            "status": "retail-static-proven",
        },
        {
            "area": "font-selection",
            "finding": "APT character 63 has no embedded glyphs and requests Albertus MT at height 14; the winning OTF family is exactly Albertus MT Regular",
            "status": "retail-source-and-static-path-proven",
        },
        {
            "area": "color-input",
            "finding": "source is opaque RGBA(0,204,255,255), packed 0xffffcc00 ABGR; retail transforms all four channels multiplicatively and additively",
            "status": "retail-static-proven",
        },
        {
            "area": "text-box-clipping",
            "finding": "the external DrawString call receives an integer origin, not the text rectangle as a scissor; bounds perform layout, not leaf text clipping",
            "status": "retail-static-proven",
        },
        {
            "area": "draw-order",
            "finding": "wrappers occupy Palantir depths 5, 9, and 13 and each text leaf is depth 3; preserve APT display-list depth order",
            "status": "retail-source-proven",
        },
    ]
    unresolved = [
        {
            "area": "font-size-device-mapping",
            "gate": "capture retail glyph pixel extents at authored 1024x768 and at one scaled viewport; static code proves the height-14 request but not the font backend's point/pixel convention",
        },
        {
            "area": "baseline-and-glyph-origin",
            "gate": "capture baseline-relative glyph bounds; retail passes a vertically centered integer origin into an opaque font renderer vtable",
        },
        {
            "area": "antialiasing-and-cff-hinting",
            "gate": "pixel-diff retail digit, x, slash, and space glyphs; do not assume Godot FreeType hinting or antialias defaults",
        },
        {
            "area": "final-color-and-alpha-blend",
            "gate": "capture text over transparent and textured HUD pixels; static code proves channel transforms but not final render-state blend/gamma",
        },
        {
            "area": "ancestor-clipping",
            "gate": "render-capture the reachable Palantir frame to prove no ancestor mask intersects the three text leaves",
        },
        {
            "area": "final-composite-order",
            "gate": "render-capture the complete reachable frame; source depth order is exact but GPU composite parity remains unmeasured",
        },
        {
            "area": "runtime-font-winner",
            "gate": "instrument or capture retail font registration once; source precedence and family identity select the exact candidate, but static evidence does not observe the live font handle",
        },
    ]
    contract: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "sources": sources,
        "apt": {
            "authoredResolution": [1024, 768],
            "atlasMappingCount": len(atlas_map["mappings"]),
            "font": font,
            "texts": texts,
        },
        "otf": {
            **{key: value for key, value in FONT_EXPECTATION.items() if key not in {"characterId", "sourceOffset", "definitionByteLength", "definitionSha256", "name", "glyphCount"}},
            "requiredCmap": {
                "space": {"glyph": "space", "advance": 333},
                "zero": {"glyph": "zero", "advance": 594},
                "one": {"glyph": "one", "advance": 260},
                "two": {"glyph": "two", "advance": 531},
                "three": {"glyph": "three", "advance": 542},
                "four": {"glyph": "four", "advance": 573},
                "five": {"glyph": "five", "advance": 510},
                "six": {"glyph": "six", "advance": 500},
                "seven": {"glyph": "seven", "advance": 510},
                "eight": {"glyph": "eight", "advance": 500},
                "nine": {"glyph": "nine", "advance": 500},
                "x": {"glyph": "x", "advance": 469},
                "slash": {"glyph": "slash", "advance": 552},
            },
            "advanceUnits": "font units at unitsPerEm=1000",
        },
        "gameDatCode": _verify_code_ranges(game_dat),
        "semantics": {"proven": proven, "unresolvedRenderedGates": unresolved},
        "opensage": _opensage_observation(opensage_root),
        "policy": {
            "arialFallbackAllowed": False,
            "placeholderFallbackAllowed": False,
            "syntheticGlyphFallbackAllowed": False,
            "parityReady": False,
        },
        "summary": {
            "textCharacterCount": len(texts),
            "externalFontCount": 1,
            "embeddedGlyphCount": 0,
            "provenSemanticCount": len(proven),
            "unresolvedRenderedGateCount": len(unresolved),
        },
    }
    contract["aggregateSha256"] = _canonical_sha(contract)
    return contract


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apt", type=Path, required=True)
    parser.add_argument("--const", type=Path, required=True)
    parser.add_argument("--dat", type=Path, required=True)
    parser.add_argument("--otf", type=Path, required=True)
    parser.add_argument("--game-dat", type=Path, required=True)
    parser.add_argument("--opensage-root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    contract = build_contract(
        args.apt.read_bytes(),
        args.const.read_bytes(),
        args.dat.read_bytes(),
        args.otf.read_bytes(),
        args.game_dat.read_bytes(),
        opensage_root=args.opensage_root,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(contract, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
