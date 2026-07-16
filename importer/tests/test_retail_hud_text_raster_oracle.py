from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.retail_hud_text_raster_oracle import (
    HudTextRasterOracleError,
    build_contract,
)


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
APT = ASSETS / "Palantir.apt"
CONST = ASSETS / "Palantir.const"
DAT = ASSETS / "Palantir.dat"
OTF = ASSETS / "albertusmt.otf"
GAME_DAT = Path("F:/BFME2/game.dat")
OPENSAGE = ROOT / ".private" / "scratch" / "opensage-hud-semantics"

pytestmark = pytest.mark.skipif(
    not all(path.is_file() for path in (APT, CONST, DAT, OTF, GAME_DAT)),
    reason="private retail Palantir text oracle inputs are absent",
)


def _payloads() -> tuple[bytes, bytes, bytes, bytes, bytes]:
    return (
        APT.read_bytes(),
        CONST.read_bytes(),
        DAT.read_bytes(),
        OTF.read_bytes(),
        GAME_DAT.read_bytes(),
    )


def _build() -> dict:
    return build_contract(*_payloads(), opensage_root=OPENSAGE)


def test_contract_is_exact_payload_free_and_deterministic() -> None:
    first = _build()
    second = _build()
    assert first == second
    assert first["aggregateSha256"] == (
        "a9e396fd5bc3d8f8e961eaede40373a4572d6ff740d03977f1943175fa29b8ee"
    )
    assert first["summary"] == {
        "textCharacterCount": 3,
        "externalFontCount": 1,
        "embeddedGlyphCount": 0,
        "provenSemanticCount": 8,
        "unresolvedRenderedGateCount": 7,
    }
    assert all("payload" not in source for source in first["sources"])


def test_exact_text_layout_and_external_albertus_binding_are_sealed() -> None:
    contract = _build()
    texts = contract["apt"]["texts"]
    assert [
        (
            row["characterId"],
            row["alignmentCode"],
            row["alignment"],
            row["runtimeVariable"],
        )
        for row in texts
    ] == [
        (130, 0, "right", "$PalantirResources"),
        (132, 2, "left", "$PalantirResourceMultiplier"),
        (134, 1, "center", "$PalantirCommandPoints"),
    ]
    assert all(row["fontHeight"] == 14.0 for row in texts)
    assert all(row["colorRgba8"] == [0, 204, 255, 255] for row in texts)
    assert all(row["packedAbgr32"] == "0xffffcc00" for row in texts)
    assert contract["apt"]["font"]["name"] == "Albertus MT"
    assert contract["apt"]["font"]["glyphCount"] == 0
    assert contract["otf"]["externalFont"] == "albertusmt.otf"
    assert contract["otf"]["postScriptName"] == "AlbertusMT"


def test_retail_code_evidence_and_fail_closed_policy_are_sealed() -> None:
    contract = _build()
    assert [row["id"] for row in contract["gameDatCode"]] == [
        "apt-external-text-layout",
        "apt-dynamic-text-draw-dispatch",
        "host-external-string-draw",
        "host-external-string-create",
        "host-abgr-color-transform",
        "embedded-glyph-advance-not-palantir-path",
    ]
    assert contract["policy"] == {
        "arialFallbackAllowed": False,
        "placeholderFallbackAllowed": False,
        "syntheticGlyphFallbackAllowed": False,
        "parityReady": False,
    }
    assert contract["opensage"]["rejectedFallbacks"] == [
        "Arial",
        "forced-center-alignment",
    ]


def test_wrong_retail_identity_fails_before_emitting_a_contract() -> None:
    apt, const, dat, otf, game_dat = _payloads()
    corrupted = bytearray(apt)
    corrupted[5056] ^= 0x01
    with pytest.raises(HudTextRasterOracleError, match="Palantir.apt identity changed"):
        build_contract(bytes(corrupted), const, dat, otf, game_dat)

