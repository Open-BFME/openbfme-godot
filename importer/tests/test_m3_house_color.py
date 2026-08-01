from __future__ import annotations

from pathlib import Path

from openbfme_importer.m3_pack_expansion import build_house_color_from_assets, build_house_color_manifest, house_color_mask_resources, parse_house_colors


def test_house_color_table_and_per_model_metadata_preserve_masks_and_mesh_markers() -> None:
    source = b"""
HouseColor
 BaseTexture = GUManAtArms.tga
 HouseTexture = HC_GUManAtArms04.tga
End
HouseColor
 BaseTexture = GURanger.tga
 HouseTexture = HC_GURanger.tga
End
"""
    paths = [
        "art/compiledtextures/gu/gumanatarms.dds",
        "art/compiledtextures/hc/hc_gumanatarms04.dds",
        "art/compiledtextures/gu/guranger.dds",
        "art/compiledtextures/hc/hc_guranger.dds",
    ]
    table = parse_house_colors(source, paths)
    metadata = {
        "GondorFighter": [{"virtualPath": "art/w3d/gu/gumaarms_skn.w3d", "meshNames": ["MANATARMS", "HOUSECOLOR_SHIELD"], "textureNames": ["GUManAtArms.tga"]}],
        "GondorRanger": [{"virtualPath": "art/w3d/gu/guranger_skn.w3d", "meshNames": ["RANGER"], "textureNames": ["GURanger.tga"]}],
    }
    manifest = build_house_color_manifest(metadata, table)
    assert manifest["summary"] == {"modelCount": 2, "presentCount": 2, "maskTextureCount": 2}
    fighter = manifest["models"][0]
    assert fighter["houseColorMeshes"] == ["HOUSECOLOR_SHIELD"]
    assert fighter["textureBindings"][0]["maskTextures"][0].endswith("hc_gumanatarms04.dds")
    assert all(row["present"] for row in manifest["models"])
    assert len(manifest["maskTextures"]) == 2
    resources = house_color_mask_resources(manifest)
    assert len(resources) == 2
    assert all(row["converter"] == "texture" and row["output"].endswith(".png") for row in resources)


def test_unrelated_missing_house_color_pairs_do_not_escape_bounded_closure() -> None:
    source = b"HouseColor\n BaseTexture = Other.tga\n HouseTexture = HC_Other.tga\nEnd\n"
    assert parse_house_colors(source, ["art/compiledtextures/used.dds"]) == {}


def test_house_color_coverage_is_not_complete_when_a_target_has_no_binding(monkeypatch, tmp_path: Path) -> None:
    closure = {
        "scannedW3d": [{"virtualPath": "art/w3d/model.w3d", "headerIds": {"modelIds": []}}],
        "exactLeaves": [
            {"targetObject": target, "kind": "model", "physicalVirtualPaths": ["art/w3d/model.w3d"]}
            for target in (
                "MenFortress", "GondorFarm", "GondorBarracks", "GondorArcherRange",
                "GondorStable", "GondorWorkshop", "GondorBattleTower", "GondorWell",
                "GondorStatue", "GondorForge", "GondorMarketPlace", "GondorCastleWallHub",
                "GondorTrebuchet", "GondorCavalry", "GondorTowerShieldGuard", "GondorRanger",
                "GondorFighter", "GondorArcher",
            )
        ],
    }
    source_path = tmp_path / "art" / "w3d" / "model.w3d"
    source_path.parent.mkdir(parents=True)
    source_path.write_bytes(b"fixture")
    house_path = tmp_path / "data" / "ini" / "housecolor.ini"
    house_path.parent.mkdir(parents=True)
    house_path.write_bytes(b"")

    class Scan:
        model_references = ()
        mesh_headers = ()
        texture_references = ()
        shader_material_properties = ()
        warnings = ()

    monkeypatch.setattr("openbfme_importer.m3_pack_expansion.scan_w3d_metadata", lambda *_: Scan())
    result = build_house_color_from_assets(closure, tmp_path, [])
    assert result["summary"]["modelCount"] == 18
    assert result["summary"]["presentCount"] == 0
    assert result["complete"] is False
