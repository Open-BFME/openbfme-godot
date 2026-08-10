"""Pack-compose emissions: the strings lane and the Create-a-Hero table.

Covers the two document lanes ``compose_faction_profile`` adds on top of the
coverage-approved artifact composition:

* ``data/strings.json`` -- every CONTROLBAR: id the composed pack's own
  runtime documents reference, resolved against the retail string table, with
  retail-absent ids recorded as ``sourceNullStringIds`` evidence (the set
  ``game/tests/hud_string_completeness_runner.gd`` derives its unfixable
  population from).
* ``data/cah/system.json`` -- the compiled Create-a-Hero system table,
  published only by the RotWK Men host pack.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path

import pytest

from openbfme_importer.cah_system_compiler import (
    RUNTIME_SCHEMA as CAH_RUNTIME_SCHEMA,
    RUNTIME_SCHEMA_VERSION as CAH_RUNTIME_SCHEMA_VERSION,
)
from openbfme_importer.faction_slice_profile import (
    CAH_SYSTEM_PACK_KEY,
    CAH_SYSTEM_RUNTIME_PATH,
    STRINGS_PACK_KEY,
    STRINGS_RUNTIME_PATH,
    compose_faction_profile,
)
from openbfme_importer.sage_string import parse_string_catalog


def _digest(value: object) -> str:
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


def _cah_digest(value: object) -> str:
    # Matches cah_system_compiler._canonical_bytes exactly.
    return hashlib.sha256(
        json.dumps(
            value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
        ).encode("utf-8")
    ).hexdigest()


def _base() -> dict[str, object]:
    return {
        "format": 1, "id": "base", "title": "base",
        "pack": {"id": "bfme2-men-vslice", "version": "test", "files": {}},
        "resources": [], "runtime_data": {},
    }


def _base_with_selection_contract() -> dict[str, object]:
    from openbfme_importer.retail_fords_completion_profile import (
        MEN_SELECTION_PACK_KEY,
        MEN_SELECTION_RESOURCES,
        MEN_SELECTION_RUNTIME,
        MEN_SELECTION_RUNTIME_PATH,
    )

    base = _base()
    base["resources"] = [deepcopy(row) for row in MEN_SELECTION_RESOURCES]
    base["runtime_data"] = {
        MEN_SELECTION_RUNTIME_PATH: deepcopy(MEN_SELECTION_RUNTIME),
    }
    base["pack"]["files"] = {
        MEN_SELECTION_PACK_KEY: MEN_SELECTION_RUNTIME_PATH,
    }
    return base


def _resource(identifier: str) -> dict[str, object]:
    return {
        "id": identifier, "kind": "model", "converter": "copy",
        "patterns": [f"art/{identifier}.w3d"], "output": f"assets/{identifier}.w3d",
        "options": {}, "required": True, "limit": 1, "expected_count": 1,
    }


def _write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def _faction_coverage(
    root: Path, faction: str, *, registration: dict[str, object] | None = None
) -> None:
    """One converted playable unit whose runtime registration we control."""
    unit_recipe = {
        "objectId": "ElvenArcher", "category": "infantry",
        "descriptorSha256": "a" * 64, "recipeSha256": "b" * 64,
        "resources": [_resource("unit-elvenarcher")],
        "runtimeRegistration": registration if registration is not None else {"production": []},
    }
    rows = [
        {"id": "ElvenArcher", "family": "playable-unit", "status": "converted", "recipeSha256": "b" * 64},
    ]
    coverage = {
        "schema": "openbfme.faction-import-coverage", "schemaVersion": 0,
        "objects": rows,
        "summary": {"convertedCount": 1, "converterGapCount": 0, "conversionComplete": True},
    }
    coverage["aggregateSha256"] = _digest(coverage)
    _write_json(root / f"{faction}-coverage.json", coverage)
    _write_json(root / f"{faction}/objects/elvenarcher/pack-recipe.json", unit_recipe)


# The referenced ids live at different nesting depths on purpose: the lane's
# definition of "reference" is the HUD gate's text scrape, not a schema walk.
_HUD_REGISTRATION: dict[str, object] = {
    "production": [
        {"button": {"textLabel": "CONTROLBAR:TestLabel"}},
    ],
    "hud": {
        "tooltips": ["CONTROLBAR:DeepTooltip"],
        "missing": "CONTROLBAR:MissingFromRetail",
        "empty": "CONTROLBAR:EmptyRetailRow",
    },
}

_RETAIL_STR = b"""
CONTROLBAR:TestLabel
  "Elven Archer"
END
CONTROLBAR:DeepTooltip
  "Fires arrows"
END
CONTROLBAR:EmptyRetailRow
  ""
END
CONTROLBAR:UnreferencedRow
  "Never asked for"
END
"""


def _string_catalog():
    return parse_string_catalog(
        _RETAIL_STR, duplicate_policy="first-wins", strict=False
    )


def _cah_runtime(*, tamper: bool = False) -> dict[str, object]:
    body: dict[str, object] = {
        "schema": CAH_RUNTIME_SCHEMA,
        "schemaVersion": CAH_RUNTIME_SCHEMA_VERSION,
        "descriptorSha256": "d" * 64,
        "registration": {
            "system": {"buildCost": 500},
            "attributeGroups": [{"groupName": "CreateAHero_ArmorAttribute"}],
            "classes": [
                {"id": "ClassHeroOfTheWest", "label": "CONTROLBAR:TestLabel"}
            ],
        },
    }
    body["runtimeSha256"] = _cah_digest(body)
    if tamper:
        body["registration"]["system"]["buildCost"] = 800
    return body


# --- strings lane ----------------------------------------------------------

def test_rotwk_compose_ships_pack_strings_document(tmp_path: Path) -> None:
    # THE measured gap: the RotWK faction slice profile emitted no
    # files.strings at all, so every CONTROLBAR id its documents reference
    # rendered as a raw-key fallback (779 recoverable ids on the mounted
    # selection). The composed pack must now ship the strings document and it
    # must SURVIVE the lean expansion filter.
    _faction_coverage(tmp_path, "angmar", registration=deepcopy(_HUD_REGISTRATION))
    target, receipt = compose_faction_profile(
        _base_with_selection_contract(), tmp_path, ["angmar"],
        game="rotwk", string_catalog=_string_catalog(),
    )
    document = target["runtime_data"][STRINGS_RUNTIME_PATH]
    assert target["pack"]["files"][STRINGS_PACK_KEY] == STRINGS_RUNTIME_PATH
    assert document["schema"] == "openbfme.localized-strings"
    assert document["schemaVersion"] == 0
    assert document["locale"] == "en"
    assert document["complete"] is False
    assert document["strings"]["CONTROLBAR:TestLabel"] == "Elven Archer"
    assert document["strings"]["CONTROLBAR:DeepTooltip"] == "Fires arrows"
    # Selection is referenced-ids-only: unreferenced retail rows are not bulk
    # exported into every pack.
    assert "CONTROLBAR:UnreferencedRow" not in document["strings"]
    assert receipt["strings"]["referencedCount"] == 4
    assert receipt["strings"]["resolvedCount"] == 3
    assert receipt["strings"]["sourceNullCount"] == 1


def test_referenced_id_absent_from_retail_is_recorded_as_source_null_evidence(
    tmp_path: Path,
) -> None:
    # Second half of the job: the id retail's own table has no row for MUST
    # become sourceNullStringIds evidence, or the HUD gate's derived
    # retail-absent set stays empty and its evidence check keeps failing.
    _faction_coverage(tmp_path, "angmar", registration=deepcopy(_HUD_REGISTRATION))
    target, _ = compose_faction_profile(
        _base_with_selection_contract(), tmp_path, ["angmar"],
        game="rotwk", string_catalog=_string_catalog(),
    )
    document = target["runtime_data"][STRINGS_RUNTIME_PATH]
    assert document["sourceNullStringIds"] == ["CONTROLBAR:MissingFromRetail"]
    assert "CONTROLBAR:MissingFromRetail" not in document["strings"]


def test_empty_retail_row_is_shipped_verbatim_not_reclassified(tmp_path: Path) -> None:
    # Retail declining to localize shows up as NO row; a present-but-empty row
    # is retail's own authored text and ships as-is. Folding the two together
    # would launder data holes into sanctioned exemptions.
    _faction_coverage(tmp_path, "angmar", registration=deepcopy(_HUD_REGISTRATION))
    target, _ = compose_faction_profile(
        _base_with_selection_contract(), tmp_path, ["angmar"],
        game="rotwk", string_catalog=_string_catalog(),
    )
    document = target["runtime_data"][STRINGS_RUNTIME_PATH]
    assert document["strings"]["CONTROLBAR:EmptyRetailRow"] == ""
    assert "CONTROLBAR:EmptyRetailRow" not in document.get("sourceNullStringIds", [])


def test_bfme2_compose_merges_existing_census_rows(tmp_path: Path) -> None:
    # The BFME2 base profile already ships a census-selected strings document
    # (faction_profile.py Men leaf precedent). Rows for ids this compose does
    # not reference are carried, never dropped; referenced ids resolve fresh.
    _faction_coverage(tmp_path, "men", registration=deepcopy(_HUD_REGISTRATION))
    base = _base()
    base["runtime_data"][STRINGS_RUNTIME_PATH] = {
        "schema": "openbfme.localized-strings", "schemaVersion": 0,
        "locale": "en", "complete": False,
        "strings": {
            "CONTROLBAR:LegacyOnly": "Census text",
            "CONTROLBAR:TestLabel": "Stale spelling",
        },
    }
    base["pack"]["files"][STRINGS_PACK_KEY] = STRINGS_RUNTIME_PATH
    target, _ = compose_faction_profile(
        base, tmp_path, ["men"], string_catalog=_string_catalog(),
    )
    document = target["runtime_data"][STRINGS_RUNTIME_PATH]
    assert document["strings"]["CONTROLBAR:LegacyOnly"] == "Census text"
    # Referenced ids are re-resolved against the cook's table -- the fresh
    # retail value wins over a stale carried row.
    assert document["strings"]["CONTROLBAR:TestLabel"] == "Elven Archer"


def test_strings_document_is_deterministic(tmp_path: Path) -> None:
    # These documents are sha256-digested downstream: byte-identical output
    # for identical inputs is a contract, not a nicety.
    _faction_coverage(tmp_path, "angmar", registration=deepcopy(_HUD_REGISTRATION))
    first, _ = compose_faction_profile(
        _base_with_selection_contract(), tmp_path, ["angmar"],
        game="rotwk", string_catalog=_string_catalog(),
    )
    second, _ = compose_faction_profile(
        _base_with_selection_contract(), tmp_path, ["angmar"],
        game="rotwk", string_catalog=_string_catalog(),
    )
    encode = lambda value: json.dumps(value, sort_keys=True, ensure_ascii=False)
    assert encode(first["runtime_data"][STRINGS_RUNTIME_PATH]) == encode(
        second["runtime_data"][STRINGS_RUNTIME_PATH]
    )


def test_compose_without_string_catalog_keeps_legacy_shape(tmp_path: Path) -> None:
    # Direct legacy callers (tools/compose-faction-profile.py) still compose;
    # they just do not get the lane. The publish CLI always supplies the
    # catalog, so a real cook can never take this branch silently.
    _faction_coverage(tmp_path, "angmar", registration=deepcopy(_HUD_REGISTRATION))
    target, receipt = compose_faction_profile(
        _base_with_selection_contract(), tmp_path, ["angmar"], game="rotwk",
    )
    assert STRINGS_RUNTIME_PATH not in target["runtime_data"]
    assert STRINGS_PACK_KEY not in target["pack"]["files"]
    assert "strings" not in receipt


# --- Create-a-Hero system table --------------------------------------------

def test_rotwk_men_compose_publishes_cah_system_table(tmp_path: Path) -> None:
    # The compiled table becomes a normal pack document on the Men host cook:
    # data/cah/system.json with a cah.system files registration, surviving the
    # lean expansion filter.
    _faction_coverage(tmp_path, "men", registration=deepcopy(_HUD_REGISTRATION))
    runtime = _cah_runtime()
    target, receipt = compose_faction_profile(
        _base_with_selection_contract(), tmp_path, ["men"],
        game="rotwk", string_catalog=_string_catalog(), cah_runtime=runtime,
    )
    assert target["runtime_data"][CAH_SYSTEM_RUNTIME_PATH] == runtime
    assert target["pack"]["files"][CAH_SYSTEM_PACK_KEY] == CAH_SYSTEM_RUNTIME_PATH
    assert receipt["cahSystem"] == {
        "runtimePath": CAH_SYSTEM_RUNTIME_PATH,
        "packFileKey": CAH_SYSTEM_PACK_KEY,
        "runtimeSha256": runtime["runtimeSha256"],
    }


def test_cah_table_controlbar_ids_are_covered_by_the_strings_lane(
    tmp_path: Path,
) -> None:
    # The table is registered BEFORE the strings scan, so a label id it
    # carries is part of the reference population like any other document.
    _faction_coverage(tmp_path, "men", registration={"production": []})
    target, _ = compose_faction_profile(
        _base_with_selection_contract(), tmp_path, ["men"],
        game="rotwk", string_catalog=_string_catalog(), cah_runtime=_cah_runtime(),
    )
    document = target["runtime_data"][STRINGS_RUNTIME_PATH]
    assert document["strings"]["CONTROLBAR:TestLabel"] == "Elven Archer"


def test_cah_system_is_refused_outside_the_rotwk_men_host(tmp_path: Path) -> None:
    # Single-owner rule: publishing the table from a supplemental faction (or
    # a BFME2 compose) would create two mounted owners resolved by mount
    # order. Same precedent as livingWorld.
    _faction_coverage(tmp_path, "angmar", registration={"production": []})
    with pytest.raises(ValueError, match="RotWK Men host"):
        compose_faction_profile(
            _base_with_selection_contract(), tmp_path, ["angmar"],
            game="rotwk", cah_runtime=_cah_runtime(),
        )
    _faction_coverage(tmp_path, "men", registration={"production": []})
    with pytest.raises(ValueError, match="RotWK Men host"):
        compose_faction_profile(
            _base(), tmp_path, ["men"], cah_runtime=_cah_runtime(),
        )


def test_tampered_cah_runtime_refuses_the_compose(tmp_path: Path) -> None:
    # Fail closed: a table whose digest does not answer must refuse the whole
    # compose rather than publish a pack that claims cah.system and lies.
    _faction_coverage(tmp_path, "men", registration={"production": []})
    with pytest.raises(ValueError, match="cah system runtime document is invalid"):
        compose_faction_profile(
            _base_with_selection_contract(), tmp_path, ["men"],
            game="rotwk", cah_runtime=_cah_runtime(tamper=True),
        )


def test_wrong_schema_cah_runtime_refuses_the_compose(tmp_path: Path) -> None:
    _faction_coverage(tmp_path, "men", registration={"production": []})
    foreign = {"schema": "openbfme.cah-system-descriptor", "schemaVersion": 0}
    with pytest.raises(ValueError, match="cah system runtime document is invalid"):
        compose_faction_profile(
            _base_with_selection_contract(), tmp_path, ["men"],
            game="rotwk", cah_runtime=foreign,
        )


# --- Create-a-Hero meshes ---------------------------------------------------

def _cah_model_resources() -> list[dict[str, object]]:
    from openbfme_importer.cah_model_pack import compile_cah_model_pack

    system = {
        "registration": {
            "classes": [
                {
                    "classIndex": 0,
                    "subClasses": [
                        {
                            "subClassIndex": 0,
                            "models": {
                                "battlefield": {
                                    "model": "CHHW_CG_U_SKN",
                                    "skeleton": "CHHW_CG_U_SKL",
                                }
                            },
                        }
                    ],
                }
            ]
        }
    }
    pack = compile_cah_model_pack(
        system,
        w3d_lookup={
            "chhw_cg_u_skn.w3d": "art/w3d/ch/chhw_cg_u_skn.w3d",
            "chhw_cg_u_skl.w3d": "art/w3d/ch/chhw_cg_u_skl.w3d",
        }.get,
    )
    return [dict(row) for row in pack.resources]


def test_rotwk_men_compose_ships_the_meshes_the_cah_table_names(
    tmp_path: Path,
) -> None:
    # The published gap: data/cah/system.json named 34 meshes and the pack
    # carried none of them, so every hero in the roster drew nothing.
    _faction_coverage(tmp_path, "men", registration={"production": []})
    target, receipt = compose_faction_profile(
        _base_with_selection_contract(), tmp_path, ["men"],
        game="rotwk", string_catalog=_string_catalog(),
        cah_runtime=_cah_runtime(), cah_model_resources=_cah_model_resources(),
    )
    outputs = {
        str(row.get("output"))
        for row in target["resources"]
        if str(row.get("output", "")).startswith("assets/models/cah/")
    }
    assert outputs == {"assets/models/cah/CHHW_CG_U_SKN.glb"}
    assert receipt["cahModels"]["modelOutputs"] == [
        "assets/models/cah/CHHW_CG_U_SKN.glb"
    ]


def test_cah_meshes_without_the_table_refuse_the_compose(tmp_path: Path) -> None:
    _faction_coverage(tmp_path, "men", registration={"production": []})
    with pytest.raises(ValueError, match="require the cah.system table"):
        compose_faction_profile(
            _base_with_selection_contract(), tmp_path, ["men"],
            game="rotwk", cah_model_resources=_cah_model_resources(),
        )


# --- retail interface art ---------------------------------------------------

def _interface_art() -> tuple[list[dict[str, object]], dict[str, object]]:
    from openbfme_importer.interface_art import PACK_INDEX_SCHEMA

    resources = [
        {
            "id": "interface-art-abcdef012345-00",
            "kind": "ui",
            "converter": "texture-atlas-crops",
            "patterns": ["art/compiledtextures/he/heroui_030.dds"],
            "output": "assets/ui/interface-art/abcdef012345",
            "options": {
                "crops": [
                    {
                        "logicalName": "hicahcaptaingondor-1f7c1c6f",
                        "output": "hicahcaptaingondor-1f7c1c6f.png",
                        "crop": [0, 0, 64, 64],
                    }
                ]
            },
            "required": True,
            "limit": 1,
            "expected_count": 1,
        }
    ]
    document = {
        "schema": PACK_INDEX_SCHEMA,
        "schemaVersion": 1,
        "scope": "all",
        "atlases": [],
        "images": {
            "HICAHCaptainGondor": (
                "assets/ui/interface-art/abcdef012345/hicahcaptaingondor-1f7c1c6f.png"
            )
        },
        "gaps": [],
    }
    return resources, document


def test_men_host_compose_publishes_the_interface_art_index(tmp_path: Path) -> None:
    # No mounted pack carried data/interface-art/index.json, so ContentDB's
    # consumer had nothing to load and every retail icon fell back.
    from openbfme_importer.faction_slice_profile import (
        INTERFACE_ART_PACK_KEY,
        INTERFACE_ART_RUNTIME_PATH,
    )

    _faction_coverage(tmp_path, "men", registration={"production": []})
    resources, document = _interface_art()
    target, receipt = compose_faction_profile(
        _base_with_selection_contract(), tmp_path, ["men"],
        game="rotwk", string_catalog=_string_catalog(),
        cah_runtime=_cah_runtime(), interface_art=(resources, document),
    )
    assert target["runtime_data"][INTERFACE_ART_RUNTIME_PATH] == document
    assert target["pack"]["files"][INTERFACE_ART_PACK_KEY] == INTERFACE_ART_RUNTIME_PATH
    # A Create-a-Hero roster button resolves through the shipped index.
    assert "HICAHCaptainGondor" in target["runtime_data"][INTERFACE_ART_RUNTIME_PATH]["images"]
    assert any(
        row["id"] == "interface-art-abcdef012345-00" for row in target["resources"]
    )
    assert receipt["interfaceArt"]["imageCount"] == 1


def test_interface_art_index_is_refused_outside_the_men_host(tmp_path: Path) -> None:
    # Two mounted owners of one index resolve by mount order, not by contract.
    _faction_coverage(tmp_path, "elves", registration={"production": []})
    with pytest.raises(ValueError, match="owned by the Men host pack"):
        compose_faction_profile(
            _base_with_selection_contract(), tmp_path, ["elves"],
            game="rotwk", interface_art=_interface_art(),
        )


def test_interface_art_document_with_a_foreign_schema_refuses(tmp_path: Path) -> None:
    _faction_coverage(tmp_path, "men", registration={"production": []})
    resources, document = _interface_art()
    document["schema"] = "openbfme.something-else"
    with pytest.raises(ValueError, match="interface-art index document is invalid"):
        compose_faction_profile(
            _base_with_selection_contract(), tmp_path, ["men"],
            game="rotwk", interface_art=(resources, document),
        )
