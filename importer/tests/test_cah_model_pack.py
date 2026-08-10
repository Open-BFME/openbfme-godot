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


# --- garment swap textures --------------------------------------------------

_SWAP_SYSTEM = {
    "registration": {
        "appearanceOptions": [
            {
                "upgradeName": "Upgrade_CAPG_CHBOD01",
                "subObjects": {
                    "show": [],
                    "hide": [],
                    "textureSwaps": [
                        {
                            "fromTexture": "CHHW_SMN_01.tga",
                            "index": 0,
                            "texture": "CHHW_SMN.tga",
                        }
                    ],
                },
            },
            {
                "upgradeName": "Upgrade_CAPG_CHBOD02",
                "subObjects": {
                    "show": [],
                    "hide": [],
                    "textureSwaps": [
                        {
                            "fromTexture": "CHHW_SMN.tga",
                            "index": 0,
                            "texture": "CHHW_SMN_01.tga",
                        }
                    ],
                },
            },
            # A part-driven option contributes no texture.
            {
                "upgradeName": "Upgrade_CaptainOfGondor_CHH02",
                "subObjects": {"show": ["HLMT_01"], "hide": [], "textureSwaps": []},
            },
        ]
    }
}

_SWAP_CATALOG = (
    "art/compiledtextures/ch/chhw_smn.dds",
    "art/compiledtextures/ch/chhw_smn_01.dds",
    "art/compiledtextures/xx/unrelated.dds",
)


def test_swap_targets_publish_under_the_retail_basename() -> None:
    # THE PUBLISHED GAP: every Body group repaints the mesh through
    # UpgradeTexture, and the mesh conversion embeds only the images that mesh
    # already references, so the alternate skins were named by the table and
    # present in no pack.
    from openbfme_importer.cah_model_pack import compile_cah_swap_texture_pack

    pack = compile_cah_swap_texture_pack(
        _SWAP_SYSTEM, texture_catalog_paths=_SWAP_CATALOG
    )
    outputs = sorted(str(row["output"]) for row in pack.resources)
    assert outputs == [
        "assets/textures/cah/CHHW_SMN.png",
        "assets/textures/cah/CHHW_SMN_01.png",
    ]
    assert {row["converter"] for row in pack.resources} == {"texture"}
    assert pack.receipt["textureCount"] == 2


def test_both_ends_of_a_swap_are_published() -> None:
    # A Body group is a cycle: each option swaps every other option's skin back
    # to its own, so a client that cannot restore `fromTexture` cannot let the
    # player change their mind.
    from openbfme_importer.cah_model_pack import cah_swap_texture_identifiers

    assert set(cah_swap_texture_identifiers(_SWAP_SYSTEM)) == {
        "CHHW_SMN.tga",
        "CHHW_SMN_01.tga",
    }


def test_the_authored_tga_bridges_to_the_compiled_dds() -> None:
    from openbfme_importer.cah_model_pack import compile_cah_swap_texture_pack

    pack = compile_cah_swap_texture_pack(
        _SWAP_SYSTEM, texture_catalog_paths=_SWAP_CATALOG
    )
    sources = {str(row["patterns"][0]) for row in pack.resources}
    assert sources == {
        "art/compiledtextures/ch/chhw_smn.dds",
        "art/compiledtextures/ch/chhw_smn_01.dds",
    }


def test_a_swap_target_retail_lacks_refuses_the_compile() -> None:
    from openbfme_importer.cah_model_pack import (
        CahModelPackError,
        compile_cah_swap_texture_pack,
    )

    with pytest.raises(CahModelPackError, match="CHHW_SMN_01.tga"):
        compile_cah_swap_texture_pack(
            _SWAP_SYSTEM,
            texture_catalog_paths=("art/compiledtextures/ch/chhw_smn.dds",),
        )


def test_a_system_with_no_swaps_ships_nothing() -> None:
    from openbfme_importer.cah_model_pack import compile_cah_swap_texture_pack

    pack = compile_cah_swap_texture_pack(
        {"registration": {"appearanceOptions": []}}, texture_catalog_paths=()
    )
    assert pack.resources == ()


# --- creation-screen idle animations ----------------------------------------

def _idle_system(*, cheer: bool = True) -> dict:
    specials = [
        {"role": "selectedCheer", "animation": "chhw_cg_c_slca",
         "sourceAnimation": "CHHW_CG_C_SLCA"},
        {"role": "examineWeapon", "animation": "chhw_cg_c_wpna",
         "sourceAnimation": "CHHW_CG_C_WPNA"},
        {"role": "examineSelf", "animation": "chhw_cg_c_clra",
         "sourceAnimation": "CHHW_CG_C_CLRA"},
    ]
    if not cheer:
        specials = specials[1:]
    return {
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
                                },
                                "creationScreen": {
                                    "model": "CHHW_CG_C_SKN",
                                    "skeleton": "CHHW_CG_C_SKL",
                                    "creationIdles": {
                                        "animationPrefix": "CHHW_CG",
                                        "base": [
                                            {
                                                "animation": "chhw_cg_c_atnb",
                                                "sourceAnimation": "CHHW_CG_C_ATNB",
                                            }
                                        ],
                                        "specials": specials,
                                        "specialChancePercent": 20.0,
                                    },
                                },
                            },
                        }
                    ],
                }
            ]
        }
    }


_IDLE_W3D = {
    "chhw_cg_u_skn.w3d": "art/w3d/ch/chhw_cg_u_skn.w3d",
    "chhw_cg_u_skl.w3d": "art/w3d/ch/chhw_cg_u_skl.w3d",
    "chhw_cg_c_skn.w3d": "art/w3d/ch/chhw_cg_c_skn.w3d",
    "chhw_cg_c_skl.w3d": "art/w3d/ch/chhw_cg_c_skl.w3d",
    "chhw_cg_c_atnb.w3d": "art/w3d/ch/chhw_cg_c_atnb.w3d",
    "chhw_cg_c_slca.w3d": "art/w3d/ch/chhw_cg_c_slca.w3d",
    "chhw_cg_c_wpna.w3d": "art/w3d/ch/chhw_cg_c_wpna.w3d",
    "chhw_cg_c_clra.w3d": "art/w3d/ch/chhw_cg_c_clra.w3d",
}


def _by_output(pack, output: str) -> dict:
    for row in pack.resources:
        if row.get("output") == output:
            return row
    raise AssertionError(f"no resource outputs {output}")


def test_a_creation_screen_mesh_converts_through_the_animated_lane() -> None:
    # THE GAP: the creation-screen hero stood in its bind pose because the
    # hierarchical converter refuses animations outright, so no clip could ship.
    from openbfme_importer.cah_model_pack import compile_cah_model_pack

    pack = compile_cah_model_pack(_idle_system(), w3d_lookup=_IDLE_W3D.get)
    creation = _by_output(pack, "assets/models/cah/CHHW_CG_C_SKN.glb")
    assert creation["converter"] == "w3d-bundle"
    assert creation["options"]["animations"] == [
        "chhw_cg_c_atnb.w3d",
        "chhw_cg_c_slca.w3d",
        "chhw_cg_c_wpna.w3d",
        "chhw_cg_c_clra.w3d",
    ]
    # Every declared clip is staged: options.animations only NAMES files the
    # pattern closure already selected.
    for name in creation["options"]["animations"]:
        assert f"art/w3d/ch/{name}" in creation["patterns"]


def test_a_battlefield_mesh_keeps_the_still_lane() -> None:
    # Keeping the eighteen battlefield meshes hierarchical is what stops this
    # from invalidating their conversion cache.
    from openbfme_importer.cah_model_pack import compile_cah_model_pack

    pack = compile_cah_model_pack(_idle_system(), w3d_lookup=_IDLE_W3D.get)
    battlefield = _by_output(pack, "assets/models/cah/CHHW_CG_U_SKN.glb")
    assert battlefield["converter"] == "w3d-hierarchical"
    assert "animations" not in battlefield["options"]


def test_a_clip_retail_never_shipped_is_a_named_gap_not_a_refusal() -> None:
    # Neither dwarf subclass has a cheer animation. Refusing the mesh over it
    # would cost the roster a hero to save a flourish.
    from openbfme_importer.cah_model_pack import compile_cah_model_pack

    lookup = dict(_IDLE_W3D)
    del lookup["chhw_cg_c_slca.w3d"]
    pack = compile_cah_model_pack(_idle_system(), w3d_lookup=lookup.get)
    creation = _by_output(pack, "assets/models/cah/CHHW_CG_C_SKN.glb")
    assert creation["converter"] == "w3d-bundle"
    assert "chhw_cg_c_slca.w3d" not in creation["options"]["animations"]
    assert pack.receipt["animationGaps"] == [
        {
            "modelId": "CHHW_CG_C_SKN",
            "animationId": "CHHW_CG_C_SLCA",
            "reason": "the retail install carries no such W3D",
        }
    ]


def test_a_mesh_the_install_lacks_is_still_fatal() -> None:
    # The tolerance above is for CLIPS only.
    from openbfme_importer.cah_model_pack import (
        CahModelPackError,
        compile_cah_model_pack,
    )

    lookup = dict(_IDLE_W3D)
    del lookup["chhw_cg_c_skn.w3d"]
    with pytest.raises(CahModelPackError, match="CHHW_CG_C_SKN"):
        compile_cah_model_pack(_idle_system(), w3d_lookup=lookup.get)


def test_the_receipt_counts_animated_meshes() -> None:
    from openbfme_importer.cah_model_pack import compile_cah_model_pack

    pack = compile_cah_model_pack(_idle_system(), w3d_lookup=_IDLE_W3D.get)
    assert pack.receipt["animatedModelCount"] == 1
    assert pack.receipt["modelCount"] == 2
