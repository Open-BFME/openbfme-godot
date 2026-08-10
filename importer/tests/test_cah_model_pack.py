"""Fast tests for the Create-a-Hero mesh lane.

The compiled CaH system table names the meshes a hero roster shows.  Nothing
converted them, so every published pack shipped a hero table whose art
resolved to nothing.  These tests pin the two properties that failure needed:
the mesh list is DERIVED from the compiled descriptor (never a hardcoded
roster that silently rots), and a mesh the retail install cannot answer is
refused by name rather than dropped.
"""

from __future__ import annotations

import json
from pathlib import Path
from types import SimpleNamespace

import pytest

from openbfme_importer.cah_model_pack import (
    CAH_MODEL_PACK_ROOT,
    CahModelPackError,
    cah_model_bindings,
    cah_texture_resolver,
    cah_w3d_ids,
    compile_cah_model_pack,
)
from openbfme_importer.profile import ImportProfile


def _system(extra_subclass: dict[str, object] | None = None) -> dict[str, object]:
    """A compiled-runtime-shaped table exercising all four binding sites."""

    sub_classes: list[dict[str, object]] = [
        {
            "subClassIndex": 0,
            "nameStringId": "CreateAHero:CaptainOfGondor",
            "models": {
                "battlefield": {
                    "model": "CHHW_CG_U_SKN",
                    "skeleton": "CHHW_CG_U_SKL",
                    "mounted": {
                        "model": "CHHW_MW_M_SKN",
                        "skeleton": "CHHW_MW_M_SKL",
                    },
                },
                "creationScreen": {
                    "model": "CHHW_CG_C_SKN",
                    "skeleton": "CHHW_CG_C_SKL",
                    "conditionalStates": [
                        {"model": "CHAR_EL_C_SKN", "skeleton": "CHAR_AR_C_SKL"}
                    ],
                },
            },
        }
    ]
    if extra_subclass is not None:
        sub_classes.append(extra_subclass)
    return {
        "schema": "openbfme.cah-system-runtime",
        "schemaVersion": 0,
        "registration": {
            "classes": [
                {
                    "classIndex": 0,
                    "nameStringId": "CreateAHero:HeroesOfTheWest",
                    "subClasses": sub_classes,
                }
            ]
        },
    }


def _lookup(known: dict[str, str]):
    return known.get


def _paths(*identifiers: str) -> dict[str, str]:
    return {
        f"{identifier.casefold()}.w3d": f"art/w3d/ch/{identifier.casefold()}.w3d"
        for identifier in identifiers
    }


_ALL = (
    "CHHW_CG_U_SKN",
    "CHHW_CG_U_SKL",
    "CHHW_MW_M_SKN",
    "CHHW_MW_M_SKL",
    "CHHW_CG_C_SKN",
    "CHHW_CG_C_SKL",
    "CHAR_EL_C_SKN",
    "CHAR_AR_C_SKL",
)


def test_mesh_list_is_derived_from_the_descriptor_not_a_hardcoded_roster() -> None:
    bindings = cah_model_bindings(_system())
    assert [row.model_id for row in bindings] == [
        "CHAR_EL_C_SKN",
        "CHHW_CG_C_SKN",
        "CHHW_CG_U_SKN",
        "CHHW_MW_M_SKN",
    ]
    # Conditional restatements and the mounted presentation are real art, not
    # decoration: dropping either leaves a subclass unrenderable.
    assert {row.model_id: row.skeleton_id for row in bindings} == {
        "CHAR_EL_C_SKN": "CHAR_AR_C_SKL",
        "CHHW_CG_C_SKN": "CHHW_CG_C_SKL",
        "CHHW_CG_U_SKN": "CHHW_CG_U_SKL",
        "CHHW_MW_M_SKN": "CHHW_MW_M_SKL",
    }
    assert set(cah_w3d_ids(_system())) == set(_ALL)


def test_a_new_subclass_in_the_descriptor_enters_the_pack_automatically() -> None:
    extra = {
        "subClassIndex": 1,
        "nameStringId": "CreateAHero:Wanderer",
        "models": {
            "battlefield": {"model": "CHWZ_YW_U_SKN", "skeleton": "CHWZ_YW_U_SKL"}
        },
    }
    ids = cah_w3d_ids(_system(extra))
    assert "CHWZ_YW_U_SKN" in ids and "CHWZ_YW_U_SKL" in ids


def test_resources_convert_each_mesh_to_its_contracted_pack_path() -> None:
    pack = compile_cah_model_pack(_system(), w3d_lookup=_lookup(_paths(*_ALL)))
    models = {
        str(row["output"]): row for row in pack.resources if row["kind"] == "model"
    }
    assert set(models) == {
        f"{CAH_MODEL_PACK_ROOT}/{identifier}.glb"
        for identifier in (
            "CHAR_EL_C_SKN",
            "CHHW_CG_C_SKN",
            "CHHW_CG_U_SKN",
            "CHHW_MW_M_SKN",
        )
    }
    # Uppercase exactly as the descriptor spells it: the runtime contract.
    row = models[f"{CAH_MODEL_PACK_ROOT}/CHHW_CG_C_SKN.glb"]
    assert row["converter"] == "w3d-hierarchical"
    assert row["options"]["model"] == "chhw_cg_c_skn.w3d"
    # The hierarchy is STAGED with the skin, not emitted as its own GLB.
    assert row["patterns"] == [
        "art/w3d/ch/chhw_cg_c_skl.w3d",
        "art/w3d/ch/chhw_cg_c_skn.w3d",
    ]
    assert row["expected_count"] == 2 and row["required"] is True
    assert not any(
        str(item["output"]).endswith("_SKL.glb")
        for item in pack.resources
        if item.get("output")
    )


def test_textures_ride_as_a_staged_companion_resource() -> None:
    pack = compile_cah_model_pack(
        _system(),
        w3d_lookup=_lookup(_paths(*_ALL)),
        textures_for=lambda staged: ("art/compiledtextures/ch/chhw_cg.dds",),
    )
    model = next(
        row
        for row in pack.resources
        if row.get("output") == f"{CAH_MODEL_PACK_ROOT}/CHHW_CG_C_SKN.glb"
    )
    texture_ids = model["options"]["inputResourceIds"]
    assert texture_ids == ["cah-model-chhw_cg_c_skn-textures"]
    companion = next(row for row in pack.resources if row["id"] == texture_ids[0])
    assert companion["converter"] == "hash-only"
    assert companion["patterns"] == ["art/compiledtextures/ch/chhw_cg.dds"]


def test_missing_retail_w3d_is_refused_by_name(tmp_path: Path) -> None:
    # The failure this guards: a pack that quietly ships 32 of 34 heroes.
    known = _paths(*_ALL)
    known.pop("chhw_mw_m_skn.w3d")
    known.pop("char_ar_c_skl.w3d")
    with pytest.raises(CahModelPackError) as error:
        compile_cah_model_pack(_system(), w3d_lookup=_lookup(known))
    message = str(error.value)
    assert "CHHW_MW_M_SKN" in message and "CHAR_AR_C_SKL" in message
    assert "2 Create-a-Hero W3D file(s)" in message


def test_mesh_without_a_skeleton_refuses_rather_than_staging_a_loose_skin() -> None:
    system = _system()
    battlefield = system["registration"]["classes"][0]["subClasses"][0]["models"][
        "battlefield"
    ]
    del battlefield["skeleton"]
    with pytest.raises(CahModelPackError, match="names no skeleton"):
        cah_model_bindings(system)


def test_one_mesh_bound_to_two_hierarchies_refuses() -> None:
    extra = {
        "subClassIndex": 1,
        "nameStringId": "CreateAHero:Impostor",
        "models": {
            "battlefield": {"model": "CHHW_CG_U_SKN", "skeleton": "CHWZ_YW_U_SKL"}
        },
    }
    with pytest.raises(CahModelPackError, match="binds two hierarchies"):
        cah_model_bindings(_system(extra))


def test_emitted_resources_are_valid_to_the_import_profile_schema(
    tmp_path: Path,
) -> None:
    # Cheap proof that the rows the compose lane appends can actually be
    # loaded by the pipeline (slug ids, canonical outputs, converter options).
    pack = compile_cah_model_pack(
        _system(),
        w3d_lookup=_lookup(_paths(*_ALL)),
        textures_for=lambda staged: ("art/compiledtextures/ch/chhw_cg.dds",),
    )
    profile_path = tmp_path / "profile.json"
    profile_path.write_text(
        json.dumps(
            {
                "format": 1,
                "id": "cah-model-test",
                "title": "cah-model-test",
                "pack": {"id": "test-vslice", "version": "test", "files": {}},
                "resources": list(pack.resources),
                "runtime_data": {},
            }
        ),
        encoding="utf-8",
    )
    loaded = ImportProfile.load(profile_path)
    outputs = {rule.output for rule in loaded.resources if rule.output}
    assert f"{CAH_MODEL_PACK_ROOT}/CHHW_CG_C_SKN.glb" in outputs


# --- texture closure -------------------------------------------------------


def _metadata(*identifiers: str) -> SimpleNamespace:
    return SimpleNamespace(
        texture_references=[SimpleNamespace(identifier=item) for item in identifiers],
        shader_material_properties=[],
    )


def test_texture_resolver_bridges_authored_tga_to_the_compiled_dds() -> None:
    resolver = cah_texture_resolver(
        read_w3d=lambda path: b"",
        texture_catalog_paths=("art/compiledtextures/ch/chhw_cg.dds",),
        scan=lambda data, path: _metadata("CHHW_CG.TGA"),
    )
    assert resolver(("art/w3d/ch/chhw_cg_c_skn.w3d",)) == (
        "art/compiledtextures/ch/chhw_cg.dds",
    )


def test_texture_resolver_handles_rotwk_space_bearing_texture_names() -> None:
    # RotWK 2.01 really ships "CHSS_UK_OF3D_HLMT_08 .TGA": its extensionless
    # stem is not a safe relative path, so the explicit compiled basename is
    # requested instead.
    resolver = cah_texture_resolver(
        read_w3d=lambda path: b"",
        texture_catalog_paths=("art/compiledtextures/ch/chss_uk_of3d_hlmt_08 .dds",),
        scan=lambda data, path: _metadata("CHSS_UK_OF3D_HLMT_08 .TGA"),
    )
    assert resolver(("art/w3d/ch/chss_uk_u_skn.w3d",)) == (
        "art/compiledtextures/ch/chss_uk_of3d_hlmt_08 .dds",
    )


def test_unresolved_texture_refuses_rather_than_shipping_an_untextured_hero() -> None:
    resolver = cah_texture_resolver(
        read_w3d=lambda path: b"",
        texture_catalog_paths=("art/compiledtextures/ch/other.dds",),
        scan=lambda data, path: _metadata("CHHW_CG.TGA"),
    )
    with pytest.raises(CahModelPackError, match="unresolved texture"):
        resolver(("art/w3d/ch/chhw_cg_c_skn.w3d",))
