"""Exact-evidence tests for the hero-ability / spellbook FX ingress lane.

Every fixture below is a verbatim reduction of retail 1.06 authoring:
``data/ini/fxlist.ini:7941-7973`` (FX_Telekinesis / FX_TelekinesisAtBone),
``data/ini/fxparticlesystem.ini:30580`` and ``:30668`` (GandalfWaveBlastProxy /
GandalfWaveBlastWave), and the keyed-token forms retail uses for FX and
particle references.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.profile import ImportProfile
from openbfme_importer.retail_ability_fx_ingress import (
    AbilityFxIngressError,
    build_ability_fx_closure,
    build_texture_index,
    fx_recipe_parts,
    harvest_fx_ids,
    parse_keyed_value,
    validate_ability_fx_closure,
)


FX_LIST_INI = b"""
;FXList FX_TelekinesisAtBone
;  Commented-out earlier draft; must never be parsed as a definition.
;End

FXList FX_Telekinesis
  ParticleSystem
    Name = GandalfWaveBlastProxy
     OrientToObject = Yes
  End
  ParticleSystem
    Name = GandalfWaveBlastWave
     OrientToObject = Yes
  End
  CameraShakerVolume
    Radius = 500
  End
End

FXList FX_TelekinesisAtBone
\tFXListAtBonePos
\t\tFX = FX_Telekinesis
\t\tBoneName = STAFF
\tEnd
      Sound
            Name = GandalfWizardBlast
      End
End

FXList FX_LightningKeyed
  ParticleSystem
    Name = LightningStrike FollowBone:Yes
  End
End

FXList FX_SoundOnly
  Sound
    Name = EomerSpearFly
  End
End
"""


FX_PARTICLE_INI = b"""
FXParticleSystem GandalfWaveBlastProxy
  System
    Priority = ALWAYS_RENDER
    ParticleName = EXCloud01.tga
    PerParticleAttachedSystem = GandalfWaveBlastDust
    Lifetime = 35 35
  End
  Color = DefaultColor
    Color1 = R:255 G:255 B:255 0
  End
End

FXParticleSystem GandalfWaveBlastDust
  System
    ParticleName = EXCloud01.tga
    IsGroundAligned = Yes
  End
  Color = DefaultColor
    Color1 = R:134 G:126 B:102 0
  End
End

FXParticleSystem GandalfWaveBlastWave
  System
    ParticleName = EXShockWavVTight.tga
    Lifetime = 20 20
    IsGroundAligned = Yes
  End
  Color = DefaultColor
    Color1 = R:82 G:139 B:235 0
    Color2 = R:0 G:0 B:0 20
  End
  Update = DefaultUpdate
    SizeRate = 5 10
  End
End

FXParticleSystem LightningStrike
  System
    ParticleName = EXLightningBolt04.tga
  End
End

FXParticleSystem GeometryEmitter
  System
    ParticleName = EXLightRing.W3D
  End
End
"""


LEGACY_PARTICLE_INI = b"""
ParticleSystem GandalfWaveBlastWave
  ParticleName = EXShockWavVTight.tga
End
"""


TEXTURE_INDEX = {
    "excloud01": "art/compiledtextures/ex/excloud01.dds",
    "exshockwavvtight": "art/compiledtextures/ex/exshockwavvtight.dds",
    "exlightningbolt04": "art/compiledtextures/ex/exlightningbolt04.dds",
}


def _documents(*, legacy: bool = False) -> dict[str, bytes]:
    documents = {
        "data/ini/fxlist.ini": FX_LIST_INI,
        "data/ini/fxparticlesystem.ini": FX_PARTICLE_INI,
    }
    if legacy:
        documents["data/ini/particlesystem.ini"] = LEGACY_PARTICLE_INI
    return documents


def _closure(
    fx_ids,
    *,
    legacy: bool = False,
    namespace: str = "GondorGandalf",
    particle_ids=(),
):
    return build_ability_fx_closure(
        _documents(legacy=legacy),
        fx_ids,
        namespace=namespace,
        texture_index=TEXTURE_INDEX,
        particle_ids=particle_ids,
    )


class TestDrawModuleParticleSeeds:
    """ParticleSysBone systems: an object's own art, named outside any FXList.

    Retail authors CloudBreakSunbeam and ElvenGrove with ``Model = None`` and a
    single ``ParticleSysBone`` (goodfactionprops.ini / structures/elven/
    grove.ini): the particle system IS the object. Those ids reach no FXList,
    so the closure has to accept them as explicit seeds.
    """

    def test_seeded_system_converts_without_any_fx_list(self) -> None:
        closure = _closure([], particle_ids=["LightningStrike"])
        bindings = closure["runtimeBindings"]
        assert bindings["authoredParticleSystemIds"] == ["LightningStrike"]
        assert bindings["presentableParticleSystemIds"] == ["LightningStrike"]
        assert [
            row["definitionId"] for row in bindings["definitionRegistry"]
        ] == ["LightningStrike"]

    def test_seeds_are_absent_when_none_are_authored(self) -> None:
        # Byte-identical to the pre-seed lane for every owner without a
        # ParticleSysBone.
        bindings = _closure(["FX_Telekinesis"])["runtimeBindings"]
        assert "authoredParticleSystemIds" not in bindings
        assert "presentableParticleSystemIds" not in bindings

    def test_w3d_geometry_emitter_is_recorded_as_a_gap_not_invented(self) -> None:
        # EXLightRing.W3D is mesh geometry, not a render leaf: the emitter has
        # no convertible texture, so it must stay an explicit unresolved row.
        bindings = _closure([], particle_ids=["GeometryEmitter"])["runtimeBindings"]
        assert bindings["authoredParticleSystemIds"] == ["GeometryEmitter"]
        assert bindings["presentableParticleSystemIds"] == []
        assert {row["kind"] for row in bindings["unresolved"]} == {
            "particle-system",
            "particle-texture",
        }


class TestKeyedValues:
    def test_fx_namespace_prefix_is_not_part_of_the_name(self) -> None:
        # data/ini/object/goodfaction/structures/men/marketplace.ini:307
        assert parse_keyed_value("FX:FX_ForgeChimneySmoke BONE:FireSmall01") == (
            "FX_ForgeChimneySmoke",
            {"bone": "FireSmall01"},
        )

    def test_particle_name_keeps_only_its_first_token(self) -> None:
        # data/ini/fxlist.ini:4218
        assert parse_keyed_value("LightningStrike FollowBone:Yes") == (
            "LightningStrike",
            {"followbone": "Yes"},
        )

    def test_bare_identifier_is_unchanged(self) -> None:
        assert parse_keyed_value("FX_GandalfBlast") == ("FX_GandalfBlast", {})


class TestHarvest:
    def test_every_authored_fx_field_family_is_harvested(self) -> None:
        document = {
            "registration": {
                "abilities": [
                    {"effect": {"fireFxId": "FX_TelekinesisAtBone"}},
                    {"effect": {"healFxId": "FX_AragornAthelas", "fxIds": ["FX_A"]}},
                ],
                "experience": {"levels": [{"levelUpFxId": "FX:GandalfLevelUp1FX"}]},
                "leaves": {"attributeModifiers": [{"fxLists": ["FX_B"]}]},
            }
        }
        assert harvest_fx_ids(document) == [
            "FX_A",
            "FX_AragornAthelas",
            "FX_B",
            "FX_TelekinesisAtBone",
            "GandalfLevelUp1FX",
        ]

    def test_authored_null_tokens_are_not_fx_ids(self) -> None:
        assert harvest_fx_ids({"fireFxId": "None", "fxIds": ["", "NONE"]}) == []

    def test_documents_without_fx_fields_harvest_nothing(self) -> None:
        assert harvest_fx_ids({"audioBindings": {"x": ["a.wav"]}}) == []


class TestClosure:
    def test_wizard_blast_chain_closes_through_bone_hop_and_child_emitter(self) -> None:
        closure = _closure(["FX_TelekinesisAtBone"])
        bindings = closure["runtimeBindings"]
        assert bindings["presentableFxListIds"] == [
            "FX_Telekinesis",
            "FX_TelekinesisAtBone",
        ]
        definitions = {row["definitionId"] for row in bindings["definitionRegistry"]}
        # The bone hop reaches FX_Telekinesis; the Proxy's
        # PerParticleAttachedSystem reaches the only visible emitter.
        assert definitions == {
            "GandalfWaveBlastProxy",
            "GandalfWaveBlastWave",
            "GandalfWaveBlastDust",
        }
        assert [row["virtualPath"] for row in bindings["textures"]] == [
            "art/compiledtextures/ex/excloud01.dds",
            "art/compiledtextures/ex/exshockwavvtight.dds",
        ]

    def test_authored_scalars_are_verbatim_source_values(self) -> None:
        closure = _closure(["FX_TelekinesisAtBone"])
        wave = next(
            row
            for row in closure["runtimeBindings"]["definitionRegistry"]
            if row["definitionId"] == "GandalfWaveBlastWave"
        )
        # The render leaf itself rides textureResourceIds, so the scalar map
        # carries only the presentation values the runtime may read.
        assert wave["authoredScalars"] == {
            "lifetime": "20 20",
            "isgroundaligned": "Yes",
            "color1": "R:82 G:139 B:235 0",
            "color2": "R:0 G:0 B:0 20",
            "sizerate": "5 10",
        }

    def test_commented_out_definition_is_not_parsed(self) -> None:
        closure = _closure(["FX_TelekinesisAtBone"])
        root = next(
            row
            for row in closure["runtimeBindings"]["fxLists"]
            if row["fxListId"] == "FX_TelekinesisAtBone"
        )
        assert root["audioEventIds"] == ["GandalfWizardBlast"]

    def test_keyed_particle_reference_resolves_to_its_bare_definition(self) -> None:
        closure = _closure(["FX_LightningKeyed"])
        assert [
            row["definitionId"]
            for row in closure["runtimeBindings"]["definitionRegistry"]
        ] == ["LightningStrike"]

    def test_missing_fx_list_is_recorded_not_invented(self) -> None:
        closure = _closure(["FX_ThereIsNoSuchList"])
        assert closure["resources"] == []
        assert closure["runtimeBindings"]["unresolved"] == [
            {
                "kind": "fx-list",
                "id": "FX_ThereIsNoSuchList",
                "reason": "no authored FXList block in the retail corpus",
            }
        ]

    def test_sound_only_fx_list_resolves_but_never_presents(self) -> None:
        closure = _closure(["FX_SoundOnly"])
        bindings = closure["runtimeBindings"]
        assert bindings["fxLists"][0]["audioEventIds"] == ["EomerSpearFly"]
        assert bindings["presentableFxListIds"] == []
        assert closure["resources"] == []

    def test_w3d_render_leaf_fails_closed_with_both_reasons(self) -> None:
        documents = _documents()
        documents["data/ini/fxlist.ini"] = (
            FX_LIST_INI
            + b"\nFXList FX_Geometry\n  ParticleSystem\n    Name = GeometryEmitter\n  End\nEnd\n"
        )
        closure = build_ability_fx_closure(
            documents,
            ["FX_Geometry"],
            namespace="Probe",
            texture_index=TEXTURE_INDEX,
        )
        assert closure["resources"] == []
        reasons = {row["kind"]: row for row in closure["runtimeBindings"]["unresolved"]}
        assert reasons["particle-texture"]["authoredNames"] == ["EXLightRing.W3D"]
        assert reasons["particle-system"]["authoredFamilies"] == ["FXParticleSystem"]
        assert (
            reasons["particle-system"]["reason"]
            == "authored definition has no convertible render leaf"
        )

    def test_cross_family_duplicate_preserves_both_candidates_unresolved(self) -> None:
        closure = _closure(["FX_TelekinesisAtBone"], legacy=True)
        bindings = closure["runtimeBindings"]
        assert bindings["familyResolution"] == {
            "duplicateIdentifierSystemIds": ["GandalfWaveBlastWave"],
            "crossFamilyPrecedenceProven": False,
        }
        kinds = sorted(
            row["kind"]
            for row in bindings["definitionRegistry"]
            if row["definitionId"] == "GandalfWaveBlastWave"
        )
        assert kinds == ["FXParticleSystem", "ParticleSystem"]

    def test_resources_are_namespaced_by_owner(self) -> None:
        gandalf = _closure(["FX_TelekinesisAtBone"], namespace="GondorGandalf")
        theoden = _closure(["FX_TelekinesisAtBone"], namespace="RohanTheoden")
        gandalf_ids = {row["id"] for row in gandalf["resources"]}
        theoden_ids = {row["id"] for row in theoden["resources"]}
        assert gandalf_ids and not (gandalf_ids & theoden_ids)
        assert all(value.startswith("fx-gondorgandalf-") for value in gandalf_ids)

    def test_missing_fx_list_document_fails_closed(self) -> None:
        with pytest.raises(AbilityFxIngressError):
            build_ability_fx_closure(
                {}, ["FX_Telekinesis"], namespace="Probe", texture_index={}
            )


class TestContract:
    def test_closure_resources_load_as_profile_resources(self, tmp_path: Path) -> None:
        closure = _closure(["FX_TelekinesisAtBone"])
        document = {
            "format": 1,
            "id": "fx-lane-probe",
            "title": "fx lane probe",
            "pack": {"id": "probe", "version": "0"},
            "resources": closure["resources"],
        }
        path = tmp_path / "profile.json"
        path.write_text(json.dumps(document), encoding="utf-8")
        profile = ImportProfile.load(path)
        converters = {rule.converter for rule in profile.resources}
        assert converters == {"texture", "sage-particle-definition"}

    def test_tampered_closure_is_rejected(self) -> None:
        closure = _closure(["FX_TelekinesisAtBone"])
        closure["resources"] = []
        with pytest.raises(AbilityFxIngressError):
            validate_ability_fx_closure(closure)

    def test_recipe_parts_reject_a_foreign_namespace(self) -> None:
        closure = _closure(["FX_TelekinesisAtBone"], namespace="GondorGandalf")
        with pytest.raises(AbilityFxIngressError):
            fx_recipe_parts(closure, "RohanTheoden")

    def test_absent_closure_contributes_nothing(self) -> None:
        assert fx_recipe_parts(None, "GondorGandalf") == ([], None)

    def test_texture_index_drops_ambiguous_stems(self, tmp_path: Path) -> None:
        (tmp_path / ".openbfme").mkdir()
        (tmp_path / ".openbfme" / "manifest.json").write_text(
            json.dumps(
                {
                    "files": [
                        {"path": "art/compiledtextures/ex/exheal.dds"},
                        {"path": "art/textures/exheal.tga"},
                        {"path": "art/compiledtextures/ex/excross02.dds"},
                        {"path": "art/w3d/ex/exlightring.w3d"},
                    ]
                }
            ),
            encoding="utf-8",
        )
        index = build_texture_index(tmp_path)
        # Two render leaves share the "exheal" stem: ambiguous, so it is not
        # offered and the caller records an unresolved row instead of guessing.
        assert index == {"excross02": "art/compiledtextures/ex/excross02.dds"}
