from __future__ import annotations

from copy import deepcopy
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.cli import build_parser
from openbfme_importer.faction_census import (
    MUSIC_PATH,
    SOUND_EFFECTS_PATH,
    VOICE_PATH,
    _read_document,
)
from openbfme_importer.module_census import read_catalog_documents
import openbfme_importer.neutral_pack_profile as neutral_profile_subject
from openbfme_importer.neutral_pack_profile import (
    compile_neutral_unit_pack_artifact,
    compose_neutral_pack_profile,
)
from openbfme_importer.neutral_mob_catalog import compile_neutral_mob_catalog
from importer.tests.test_playable_unit_pack_compiler import (
    _closure as _unit_closure,
    _descriptor as _unit_descriptor,
    _rehash_closure,
    _rehash_descriptor,
)
from openbfme_importer.playable_unit_import import (
    _REQUIRED_DOCUMENTS,
    _required_audio_ids,
    _resolved_media,
    _scenario_source_resolution,
    _select_faction_graph,
    _selected_base_profile,
    _source_documents,
    build_scenario_unit_visual_closure_batch,
    compile_scenario_unit_recipe,
    extend_profile_with_unit,
)
from openbfme_importer.profile import ImportProfile
from openbfme_importer.playable_unit_compiler import (
    compile_playable_unit_descriptor,
    prepare_playable_unit_compiler,
)
from openbfme_importer.sage_audio import (
    parse_sage_audio_definitions,
    resolve_audio_sample_paths_partial,
    resolve_sage_audio_closure,
)
from openbfme_importer.util import write_json_atomic


def _base_profile() -> dict[str, object]:
    return {
        "format": 1,
        "id": "men-fords-complete",
        "title": "Men Fords complete",
        "pack": {
            "id": "bfme2-men-vslice",
            "version": "0.1.0",
            "schema": "openbfme.content-pack",
            "schemaVersion": 0,
            "profile_build_complete": True,
            "files": {"objects": "data/objects.json"},
        },
        "resources": [
            {
                "id": "base-data",
                "kind": "data",
                "converter": "copy",
                "patterns": ["data/base.json"],
                "output": "data/base.json",
                "required": True,
                "limit": 1,
                "expected_count": 1,
            }
        ],
        "runtime_data": {"data/objects.json": {"objects": []}},
    }


def _recipe(
    object_id: str = "GondorRangerHorde", suffix: str = "a"
) -> dict[str, object]:
    resource_id = f"unit-{suffix}"
    return {
        "objectId": object_id,
        "category": "infantry",
        "descriptorSha256": suffix * 64,
        "recipeSha256": ("b" if suffix == "a" else "c") * 64,
        "resources": [
            {
                "id": resource_id,
                "kind": "model",
                "converter": "copy",
                "patterns": [f"art/{suffix}.w3d"],
                "output": f"assets/{suffix}.w3d",
                "options": {},
                "required": True,
                "limit": 1,
                "expected_count": 1,
            }
        ],
        "runtimeRegistration": {"production": [{"slot": 1}]},
    }


def test_source_documents_include_attribute_modifiers_for_unit_levels(
    tmp_path: Path,
) -> None:
    object_path = tmp_path / "data" / "ini" / "object" / "unit.ini"
    object_path.parent.mkdir(parents=True)
    object_path.write_bytes(b"Object Unit\nEnd\n")
    for relative in _REQUIRED_DOCUMENTS:
        path = tmp_path.joinpath(*relative.split("/"))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(relative.encode("ascii"))

    documents = _source_documents(tmp_path)

    assert documents["data/ini/attributemodifier.ini"] == (
        b"data/ini/attributemodifier.ini"
    )


def test_new_unit_is_one_strict_additive_profile_delta(tmp_path: Path) -> None:
    base = _base_profile()
    target, delta = extend_profile_with_unit(base, _recipe())
    assert base == _base_profile()
    assert delta == {
        "runtimePath": "data/playable-units/gondorrangerhorde.json",
        "packFileKey": "playableUnit.gondorrangerhorde",
        "resourceIds": ["unit-a"],
        "update": False,
    }
    assert target["resources"][-1]["id"] == "unit-a"
    document = target["runtime_data"][delta["runtimePath"]]
    assert document["resourceIds"] == ["unit-a"]
    assert document["registration"] == {"production": [{"slot": 1}]}
    assert target["pack"]["files"][delta["packFileKey"]] == delta["runtimePath"]

    path = tmp_path / "profile.json"
    write_json_atomic(path, target)
    loaded = ImportProfile.load(path)
    assert loaded.pack_id == "bfme2-men-vslice"
    assert len(loaded.resources) == 2


def test_reimport_replaces_only_owned_resources() -> None:
    first, _ = extend_profile_with_unit(_base_profile(), _recipe())
    second, delta = extend_profile_with_unit(first, _recipe(suffix="d"))
    assert delta["update"] is True
    ids = [row["id"] for row in second["resources"]]
    assert ids == ["base-data", "unit-d"]
    assert second["runtime_data"][delta["runtimePath"]]["resourceIds"] == ["unit-d"]


def test_slug_collision_cannot_replace_another_object() -> None:
    first, _ = extend_profile_with_unit(_base_profile(), _recipe("Foo_Bar"))
    with pytest.raises(ValueError, match="different Object id"):
        extend_profile_with_unit(first, _recipe("Foo-Bar", "d"))


def test_reimport_rejects_malformed_or_shared_ownership() -> None:
    first, delta = extend_profile_with_unit(_base_profile(), _recipe())
    malformed = deepcopy(first)
    malformed["runtime_data"][delta["runtimePath"]]["schemaVersion"] = 1
    with pytest.raises(ValueError, match="runtime schema"):
        extend_profile_with_unit(malformed, _recipe(suffix="d"))

    shared = deepcopy(first)
    shared["runtime_data"]["data/playable-units/other.json"] = {
        "resourceIds": ["unit-a"]
    }
    with pytest.raises(ValueError, match="shared runtime ownership"):
        extend_profile_with_unit(shared, _recipe(suffix="d"))

    spelling = deepcopy(first)
    spelling["runtime_data"][delta["runtimePath"]]["resourceIds"] = ["UNIT-A"]
    with pytest.raises(ValueError, match="ownership spelling"):
        extend_profile_with_unit(spelling, _recipe(suffix="d"))


def test_selected_base_must_match_state_bundle_and_profile(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    state = tmp_path / "state"
    content = tmp_path / "content"
    profile = state / "profiles" / "base.json"
    write_json_atomic(profile, _base_profile())
    import hashlib

    profile_sha = hashlib.sha256(profile.read_bytes()).hexdigest()
    state_pack = state / "packs" / "bfme2-men-vslice"
    selected = content / "bfme2-men-vslice" / ("a" * 64)
    for root in (state_pack, selected):
        write_json_atomic(root / "pack.json", {"id": "bfme2-men-vslice"})
        write_json_atomic(
            root / "provenance" / "manifest.json",
            {"profile": "men-fords-complete", "profile_sha256": profile_sha},
        )
    write_json_atomic(
        content / "selection.json",
        {
            "schema": "openbfme.pack-selection",
            "schemaVersion": 0,
            "activePack": f"bfme2-men-vslice/{'a' * 64}",
        },
    )
    monkeypatch.setattr(
        "openbfme_importer.playable_unit_import.audit_pack",
        lambda _path: {"valid": True},
    )
    monkeypatch.setattr(
        "openbfme_importer.playable_unit_import.bundle_digest",
        lambda _path: "a" * 64,
    )
    assert (
        _selected_base_profile(
            state, profile, content, state_pack, bootstrap_selection=False
        )
        == profile
    )

    write_json_atomic(selected / "pack.json", {"id": "wrong-pack"})
    with pytest.raises(RuntimeError, match="pack ids differ"):
        _selected_base_profile(
            state, profile, content, state_pack, bootstrap_selection=False
        )


def test_bootstrap_requires_clean_selection_and_returns_profile_without_build(
    tmp_path: Path,
) -> None:
    state = tmp_path / "state"
    content = tmp_path / "content"
    canonical = state / "profiles" / "base.json"
    write_json_atomic(canonical, _base_profile())
    pack = state / "packs" / "bfme2-men-vslice"
    assert (
        _selected_base_profile(
            state, canonical, content, pack, bootstrap_selection=True
        )
        == canonical
    )
    write_json_atomic(
        content / "selection.json",
        {
            "schema": "openbfme.pack-selection",
            "schemaVersion": 0,
            "activePack": f"some-pack/{'a' * 64}",
        },
    )
    with pytest.raises(RuntimeError, match="state base pack"):
        _selected_base_profile(
            state, canonical, content, pack, bootstrap_selection=True
        )


def test_unit_resource_collision_outside_owned_document_fails() -> None:
    base = _base_profile()
    base["resources"].append(deepcopy(_recipe()["resources"][0]))
    with pytest.raises(ValueError, match="resource id collision"):
        extend_profile_with_unit(base, _recipe())


def test_media_resolution_keeps_atlas_crop_and_resolves_audio_graph() -> None:
    descriptor = {
        "presentation": {
            "ui": {
                "portraitImageIds": ["UPRanger"],
                "commands": [
                    {
                        "fields": {"ButtonImage": ["BIRanger"]},
                        "audioRoutes": [{"id": "RangerSelectMS"}],
                    }
                ],
            },
            "audioRoutes": {
                "container": {"VoiceSelect": [{"id": "RangerSelectMS"}]},
                "primaryMember": {"VoiceAttack": [{"id": "RangerAttack"}]},
            },
        }
    }
    graph = {
        "resolvedLeaves": {
            "mappedImages": [
                {
                    "id": identifier,
                    "texture": "Atlas.tga",
                    "textureWidth": 256,
                    "textureHeight": 256,
                    "coords": {
                        "left": index * 16,
                        "top": 0,
                        "right": index * 16 + 16,
                        "bottom": 16,
                    },
                    "compiledTextureVirtualPath": "art/compiledtextures/atlas.dds",
                }
                for index, identifier in enumerate(("UPRanger", "BIRanger"))
            ],
            "audio": {
                "events": [
                    {"id": "RangerSelect", "sounds": [{"id": "SelectA"}]},
                    {"id": "RangerAttack", "sounds": [{"id": "AttackA"}]},
                ],
                "multisounds": [
                    {"id": "RangerSelectMS", "subsounds": [{"id": "RangerSelect"}]}
                ],
                "samplePaths": [
                    {"id": "SelectA", "virtualPath": "data/audio/selecta.wav"},
                    {"id": "AttackA", "virtualPath": "data/audio/attacka.wav"},
                ],
            },
        }
    }
    images, audio = _resolved_media(graph, descriptor)
    assert images["UPRanger"]["coords"]["right"] == 16
    assert images["BIRanger"]["coords"]["left"] == 16
    assert audio == {
        "RangerAttack": ["data/audio/attacka.wav"],
        "RangerSelectMS": ["data/audio/selecta.wav"],
    }


def test_unresolved_required_media_fails_closed() -> None:
    descriptor = {
        "presentation": {
            "ui": {"portraitImageIds": ["Missing"], "commands": []},
            "audioRoutes": {"container": {}, "primaryMember": {}},
        }
    }
    graph = {"resolvedLeaves": {"mappedImages": [], "audio": {}}}
    with pytest.raises(ValueError, match="mapped image is unresolved"):
        _resolved_media(graph, descriptor)


def test_media_delta_resolves_from_catalog_for_late_admitted_unit() -> None:
    descriptor = {
        "presentation": {
            "ui": {
                "portraitImageIds": [],
                "commands": [
                    {
                        "fields": {
                            "ButtonImage": ["BCShipwright_EvilTroopship"]
                        },
                        "audioRoutes": [],
                    }
                ],
            },
            "audioRoutes": {"container": {}, "primaryMember": {}},
        }
    }
    graph = {"resolvedLeaves": {"mappedImages": [], "audio": {}}}
    source = b"""
MappedImage BCShipwright_EvilTroopship
  Texture = BuildingRadialButtons_061.tga
  TextureWidth = 256
  TextureHeight = 256
  Coords = Left:128 Top:192 Right:192 Bottom:256
End
"""
    catalog = SimpleNamespace(
        entries=[
            SimpleNamespace(
                name="art/compiledtextures/bu/BuildingRadialButtons_061.dds"
            )
        ]
    )
    with mock.patch(
        "openbfme_importer.playable_unit_import._mapped_image_documents",
        return_value=[SimpleNamespace(source=source)],
    ):
        images, audio = _resolved_media(graph, descriptor, catalog=catalog)

    assert images["BCShipwright_EvilTroopship"] == {
        "id": "BCShipwright_EvilTroopship",
        "texture": "BuildingRadialButtons_061.tga",
        "textureWidth": 256,
        "textureHeight": 256,
        "coords": {"left": 128, "top": 192, "right": 192, "bottom": 256},
        "compiledTextureVirtualPath": (
            "art/compiledtextures/bu/BuildingRadialButtons_061.dds"
        ),
    }
    assert audio == {}


def test_authored_producer_admission_expands_only_reachability_filter() -> None:
    graph = {
        "definitions": {"objects": [{"id": "Existing", "edges": []}]},
        "target": {"playerTemplate": "FactionElves"},
    }

    def compile_observer(
        _object_id: str,
        _documents: object,
        *,
        faction_graph: object,
        game: str,
        scenario_admission_role: str | None = None,
    ) -> dict[str, object]:
        assert scenario_admission_role is None
        assert game == "rotwk"
        ids = [row["id"] for row in faction_graph["definitions"]["objects"]]
        assert ids == ["Existing", "GoodPort", "ShipWright"]
        return {"objectId": "ElvenTransportShip"}

    with (
        mock.patch(
            "openbfme_importer.playable_unit_import.census_playable_faction",
            return_value=graph,
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.compile_playable_unit_descriptor",
            side_effect=compile_observer,
        ),
    ):
        selected, descriptor = _select_faction_graph(
            mock.Mock(),
            {},
            "ElvenTransportShip",
            "Elves",
            game="rotwk",
            admitted_producer_ids=("GoodPort", "ShipWright"),
        )

    assert selected is graph
    assert descriptor == {"objectId": "ElvenTransportShip"}


def test_source_null_select_portrait_is_skipped() -> None:
    """Retail authors UPGondor_Banner without a MappedImage body."""
    descriptor = {
        "presentation": {
            "ui": {"portraitImageIds": ["UPGondor_Banner"], "commands": []},
            "audioRoutes": {"container": {}, "primaryMember": {}},
        }
    }
    graph = {
        "resolvedLeaves": {"mappedImages": [], "audio": {}},
        "dependencies": {"sourceNullMappedImages": ["UPGondor_Banner"]},
    }
    images, audio = _resolved_media(graph, descriptor)
    assert images == {}
    assert audio == {}


def test_compiled_texture_rebinds_from_effective_root(tmp_path) -> None:
    """Census may omit a DDS path that the sealed effective tree still holds."""
    texture_dir = tmp_path / "art" / "compiledtextures" / "hi"
    texture_dir.mkdir(parents=True)
    (texture_dir / "higaladriel.dds").write_bytes(b"DDS-fake")
    descriptor = {
        "presentation": {
            "ui": {"portraitImageIds": ["HIGaladriel"], "commands": []},
            "audioRoutes": {"container": {}, "primaryMember": {}},
        }
    }
    graph = {
        "resolvedLeaves": {
            "mappedImages": [
                {
                    "id": "HIGaladriel",
                    "texture": "HIGaladriel.tga",
                    "compiledTextureResolution": "missing",
                    "coords": {"left": 0, "top": 0, "right": 63, "bottom": 63},
                }
            ],
            "audio": {},
        }
    }
    images, _ = _resolved_media(graph, descriptor, effective_root=tmp_path)
    path = images["HIGaladriel"]["compiledTextureVirtualPath"]
    assert path.casefold() == "art/compiledtextures/hi/higaladriel.dds"
    assert path.endswith("HIGaladriel.dds") or path.endswith("higaladriel.dds")


def test_unresolved_authored_audio_route_fails_closed() -> None:
    descriptor = {
        "presentation": {
            "ui": {"portraitImageIds": [], "commands": []},
            "audioRoutes": {
                "container": {"VoiceSelect": [{"id": "MissingVoiceEvent"}]},
                "primaryMember": {},
            },
        }
    }
    graph = {
        "resolvedLeaves": {
            "mappedImages": [],
            "audio": {"events": [], "multisounds": [], "samplePaths": []},
        }
    }
    with pytest.raises(ValueError, match="audio .* unresolved"):
        _resolved_media(graph, descriptor)


def test_authored_silent_audio_event_remains_an_empty_binding() -> None:
    descriptor = {
        "presentation": {
            "ui": {
                "portraitImageIds": [],
                "commands": [
                    {
                        "fields": {},
                        "audioRoutes": [{"id": "SilentPurchaseEvent"}],
                    }
                ],
            },
            "audioRoutes": {"container": {}, "primaryMember": {}},
        }
    }
    graph = {
        "resolvedLeaves": {
            "mappedImages": [],
            "audio": {
                "events": [{"id": "SilentPurchaseEvent", "sounds": []}],
                "multisounds": [],
                "samplePaths": [],
            },
        }
    }
    _, audio = _resolved_media(graph, descriptor)
    assert audio == {"SilentPurchaseEvent": []}


def test_cli_exposes_one_command_import_surface() -> None:
    args = build_parser().parse_args(
        [
            "import-unit",
            "--install",
            "D:/Games/BFME2",
            "--object",
            "GondorRangerHorde",
            "--faction",
            "Men",
            "--plan-only",
            "--bootstrap-selection",
        ]
    )
    assert args.command == "import-unit"
    assert args.object == "GondorRangerHorde"
    assert args.faction == "Men"
    assert args.plan_only is True
    assert args.bootstrap_selection is True


def test_batch_entrypoint_has_no_unit_specific_registration() -> None:
    text = (Path(__file__).resolve().parents[2] / "import_unit.bat").read_text(
        encoding="utf-8"
    )
    assert "import-unit" in text
    assert "run_retail_slice.bat" in text
    assert "--bootstrap-selection" in text
    assert "ranger" not in text.casefold()
    assert "trebuchet" not in text.casefold()


class _StubStringCatalog:
    """Minimal InstallCatalog stand-in exposing only the lotr.str read path."""

    def __init__(self, source: bytes) -> None:
        self._source = source

    def resolve_exact(self, virtual_path: str) -> object | None:
        assert virtual_path == "data/lotr.str"
        return "data/lotr.str"

    def as_entry(self, entry: object) -> object:
        return entry

    def open_archive_for(self, entry: object) -> "_StubStringCatalog":
        return self

    def read_entry(self, entry: object, *, max_bytes: int | None = None) -> bytes:
        return self._source


_STRING_SOURCE = (
    b'CONTROLBAR:PresentAndFilled\r\n"Filled"\r\nEND\r\n\r\n'
    b'CONTROLBAR:PresentButEmpty\r\n""\r\nEND\r\n'
)


def _string_descriptor(*identifiers: str) -> dict[str, object]:
    return {
        "presentation": {
            "ui": {
                "portraitImageIds": [],
                "commands": [
                    {"fields": {"TextLabel": list(identifiers)}, "audioRoutes": []}
                ],
            },
            "audioRoutes": {"container": {}, "primaryMember": {}},
        }
    }


def test_present_but_empty_retail_string_is_not_a_retail_null_exemption() -> None:
    # A row that EXISTS with an empty value is a data hole in our read of the
    # table, not retail declining to localize the id. Sanctioning it silently
    # turned a broken parse into a permanent exemption.
    from openbfme_importer.playable_unit_import import _resolved_strings

    catalog = _StubStringCatalog(_STRING_SOURCE)
    graph: dict[str, object] = {
        "layeredDocumentAuthority": "layered-effective-assets"
    }
    with pytest.raises(ValueError, match="present but empty.*CONTROLBAR:PresentButEmpty"):
        _resolved_strings(
            catalog, _string_descriptor("CONTROLBAR:PresentButEmpty"), graph=graph
        )


def test_absent_string_without_layered_tier_authority_fails_closed() -> None:
    # Recorded retail-null evidence is only meaningful against the tier it was
    # recorded on. Without layered authority we are not reading that tier, so
    # the evidence may not sanction the hole.
    from openbfme_importer.playable_unit_import import _resolved_strings

    catalog = _StubStringCatalog(_STRING_SOURCE)
    graph: dict[str, object] = {
        "layeredSourceNullTextIds": ["CONTROLBAR:NotInTableAtAll"]
    }
    with pytest.raises(ValueError, match="CONTROLBAR:NotInTableAtAll"):
        _resolved_strings(
            catalog, _string_descriptor("CONTROLBAR:NotInTableAtAll"), graph=graph
        )


def test_layered_authority_records_genuinely_absent_string() -> None:
    """Positive control: true absence under layered authority stays exempt."""
    from openbfme_importer.playable_unit_import import _resolved_strings

    catalog = _StubStringCatalog(_STRING_SOURCE)
    graph: dict[str, object] = {
        "layeredDocumentAuthority": "layered-effective-assets"
    }
    resolved = _resolved_strings(
        catalog,
        _string_descriptor("CONTROLBAR:PresentAndFilled", "CONTROLBAR:NotInTableAtAll"),
        graph=graph,
    )
    assert resolved == {"CONTROLBAR:PresentAndFilled": "Filled"}
    assert graph["layeredSourceNullTextIds"] == ["CONTROLBAR:NotInTableAtAll"]


def _scenario_descriptor() -> dict[str, object]:
    return {
        "objectId": "NeutralWolf",
        "production": [],
        "scenarioAdmission": {
            "kind": "authored-non-buildable",
            "role": "creature",
            "surfaces": ["map-placement", "script-spawn"],
        },
        "composition": {
            "containerObjectId": "NeutralWolf",
            "members": [{"objectId": "NeutralWolfMember"}],
        },
    }


def test_scenario_recipe_recompiles_catalog_descriptor_without_faction_graph(
    tmp_path: Path,
) -> None:
    descriptor = _scenario_descriptor()
    closure = {
        "schema": "openbfme.retail-visual-closure",
        "schemaVersion": 1,
        "summary": {"ready": True},
        "aggregateSha256": "old",
    }
    recipe = {"objectId": "NeutralWolf", "production": []}
    with (
        mock.patch(
            "openbfme_importer.playable_unit_import._source_documents",
            return_value={"neutral.ini": b"Object NeutralWolf\nEnd\n"},
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.validate_playable_unit_descriptor"
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.compile_playable_unit_descriptor",
            side_effect=[deepcopy(descriptor), deepcopy(descriptor)],
        ) as compile_descriptor,
        mock.patch(
            "openbfme_importer.playable_unit_import._scenario_source_resolution",
            return_value=(
                {"WolfPortrait": {}},
                {"WolfVoice": ["data/audio/wolf.wav"]},
                {"CONTROLBAR:Wolf": "Wolf"},
                {"schema": "openbfme.scenario-unit-source-resolution"},
            ),
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.build_retail_visual_closure",
            return_value=closure,
        ) as visual_closure,
        mock.patch(
            "openbfme_importer.playable_unit_import.compile_playable_unit_pack_recipe",
            return_value=recipe,
        ) as pack_recipe,
    ):
        result_descriptor, result_closure, result_recipe = (
            compile_scenario_unit_recipe(
                object(), tmp_path, descriptor, game="rotwk"
            )
        )

    assert result_descriptor["objectId"] == "NeutralWolf"
    assert result_descriptor["production"] == []
    assert result_recipe is recipe
    assert result_closure["scenarioSourceResolution"]["schema"] == (
        "openbfme.scenario-unit-source-resolution"
    )
    assert result_closure["aggregateSha256"] != "old"
    assert compile_descriptor.call_count == 2
    for call in compile_descriptor.call_args_list:
        assert call.kwargs["game"] == "rotwk"
        assert call.kwargs["faction_graph"] is None
        assert call.kwargs["scenario_admission"] == {
            "role": "creature",
            "surfaces": ["map-placement", "script-spawn"],
        }
    visual_closure.assert_called_once_with(
        tmp_path.resolve(),
        ["NeutralWolf", "NeutralWolfMember"],
        catalog=mock.ANY,
    )
    pack_recipe.assert_called_once_with(result_descriptor, result_closure)


def test_scenario_recipe_requires_explicit_admission_for_target(
    tmp_path: Path,
) -> None:
    with (
        mock.patch(
            "openbfme_importer.playable_unit_import._source_documents",
            return_value={},
        ),
        pytest.raises(ValueError, match="requires explicit admission"),
    ):
        compile_scenario_unit_recipe(
            object(), tmp_path, "NeutralWolf", game="bfme2"
        )


def test_scenario_recipe_requires_explicit_edition(tmp_path: Path) -> None:
    with pytest.raises(TypeError, match="game"):
        compile_scenario_unit_recipe(object(), tmp_path, _scenario_descriptor())

    with pytest.raises(ValueError, match="unsupported scenario unit game"):
        compile_scenario_unit_recipe(
            object(), tmp_path, _scenario_descriptor(), game="unknown"
        )


def test_scenario_recipe_prebuilt_closure_is_byte_equivalent(
    tmp_path: Path,
) -> None:
    descriptor = _scenario_descriptor()
    raw_closure = {
        "schema": "openbfme.retail-visual-closure",
        "schemaVersion": 1,
        "targets": [
            {"requestedName": "NeutralWolf", "status": "resolved"},
            {"requestedName": "NeutralWolfMember", "status": "resolved"},
        ],
        "summary": {"ready": True},
        "aggregateSha256": "old",
    }
    catalog = SimpleNamespace(identity_sha256=lambda: "a" * 64)
    source_documents = {"neutral.ini": b"Object NeutralWolf\nEnd\n"}
    prepared = SimpleNamespace(documents=source_documents)

    def compile_descriptor(*_args, **_kwargs):
        return deepcopy(descriptor)

    def compile_recipe(value, closure):
        return {"objectId": value["objectId"], "closure": deepcopy(closure)}

    with (
        mock.patch(
            "openbfme_importer.playable_unit_import._source_documents",
            return_value=source_documents,
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.validate_playable_unit_descriptor"
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.compile_playable_unit_descriptor",
            side_effect=compile_descriptor,
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import._scenario_source_resolution",
            return_value=({}, {}, {}, {"source": "same"}),
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.build_retail_visual_closure",
            return_value=deepcopy(raw_closure),
        ) as build_closure,
        mock.patch(
            "openbfme_importer.playable_unit_import.compile_playable_unit_pack_recipe",
            side_effect=compile_recipe,
        ),
    ):
        direct = compile_scenario_unit_recipe(
            catalog, tmp_path, descriptor, game="bfme2"
        )
        batch = build_scenario_unit_visual_closure_batch(
            catalog, tmp_path, [descriptor], max_workers=1
        )
        prebuilt = compile_scenario_unit_recipe(
            catalog,
            tmp_path,
            descriptor,
            game="bfme2",
            prebuilt_visual_closure=batch["NeutralWolf"],
        )
        prepared_result = compile_scenario_unit_recipe(
            catalog,
            tmp_path,
            descriptor,
            game="bfme2",
            prebuilt_visual_closure=batch["NeutralWolf"],
            prepared=prepared,
            source_documents=source_documents,
        )

    assert prepared_result == prebuilt == direct
    assert {
        "descriptor": prebuilt[0],
        "visualClosure": prebuilt[1],
        "visualRecipe": prebuilt[2],
    } == {
        "descriptor": direct[0],
        "visualClosure": direct[1],
        "visualRecipe": direct[2],
    }
    assert build_closure.call_count == 2
    assert build_closure.call_args_list[1].kwargs["catalog_identity_sha256"] == (
        "a" * 64
    )


def test_scenario_recipe_rejects_prepared_inputs_for_other_documents(
    tmp_path: Path,
) -> None:
    prepared = SimpleNamespace(documents={"one.ini": b"one"})
    with pytest.raises(ValueError, match="different document mapping"):
        compile_scenario_unit_recipe(
            object(),
            tmp_path,
            _scenario_descriptor(),
            game="bfme2",
            prepared=prepared,
            source_documents={"one.ini": b"one"},
        )


def test_scenario_recipe_rejects_prebuilt_closure_for_other_targets(
    tmp_path: Path,
) -> None:
    descriptor = _scenario_descriptor()
    wrong = {
        "targets": [{"requestedName": "OtherWolf", "status": "resolved"}],
        "aggregateSha256": "not-used",
    }
    with (
        mock.patch(
            "openbfme_importer.playable_unit_import._source_documents",
            return_value={"neutral.ini": b"Object NeutralWolf\nEnd\n"},
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.validate_playable_unit_descriptor"
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.compile_playable_unit_descriptor",
            side_effect=[deepcopy(descriptor), deepcopy(descriptor)],
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import._scenario_source_resolution",
            return_value=({}, {}, {}, {"source": "same"}),
        ),
        pytest.raises(ValueError, match="targets do not match descriptor"),
    ):
        compile_scenario_unit_recipe(
            object(),
            tmp_path,
            descriptor,
            game="bfme2",
            prebuilt_visual_closure=wrong,
        )


def test_scenario_visual_batch_reuses_identical_target_sets(
    tmp_path: Path,
) -> None:
    first = _scenario_descriptor()
    second = deepcopy(first)
    second["objectId"] = "NeutralWolfAlias"
    closure = {
        "targets": [
            {"requestedName": "NeutralWolf", "status": "resolved"},
            {"requestedName": "NeutralWolfMember", "status": "resolved"},
        ],
        "aggregateSha256": "a" * 64,
    }
    catalog = SimpleNamespace(identity_sha256=lambda: "b" * 64)
    with (
        mock.patch(
            "openbfme_importer.playable_unit_import.validate_playable_unit_descriptor"
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.build_retail_visual_closure",
            return_value=closure,
        ) as build_closure,
    ):
        result = build_scenario_unit_visual_closure_batch(
            catalog, tmp_path, [first, second], max_workers=2
        )

    assert sorted(result) == ["NeutralWolf", "NeutralWolfAlias"]
    assert result["NeutralWolf"] == result["NeutralWolfAlias"] == closure
    assert result["NeutralWolf"] is not result["NeutralWolfAlias"]
    build_closure.assert_called_once_with(
        tmp_path.resolve(),
        ("NeutralWolf", "NeutralWolfMember"),
        catalog=catalog,
        catalog_identity_sha256="b" * 64,
    )


def test_prepared_scenario_recipe_and_artifact_are_byte_exact(
    tmp_path: Path,
) -> None:
    descriptor = _unit_descriptor("InfantryHorde")
    descriptor["production"] = []
    descriptor["presentation"]["ui"]["commands"] = []
    descriptor["scenarioAdmission"] = {
        "kind": "authored-non-buildable",
        "role": "scenario-only",
        "surfaces": ["map-placement", "script-spawn", "object-creation-list"],
        "buildCommandExposed": False,
        "evidence": "no-authored-unit-build-route",
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 1,
        "declarationKind": "Object",
    }
    _rehash_descriptor(descriptor)
    raw_closure = _unit_closure(descriptor)
    raw_closure["targets"] = [
        {**row, "requestedName": row["name"]}
        for row in raw_closure["targets"]
    ]
    _rehash_closure(raw_closure)
    documents = {"fixture.ini": b"fixture"}
    prepared = SimpleNamespace(documents=documents)
    catalog = object()
    with (
        mock.patch(
            "openbfme_importer.playable_unit_import._source_documents",
            return_value=documents,
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.validate_playable_unit_descriptor"
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.compile_playable_unit_descriptor",
            side_effect=lambda *_args, **_kwargs: deepcopy(descriptor),
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import._scenario_source_resolution",
            return_value=({}, {}, {}, {"source": "same"}),
        ),
        mock.patch(
            "openbfme_importer.playable_unit_import.build_retail_visual_closure",
            return_value=deepcopy(raw_closure),
        ),
    ):
        standalone = compile_scenario_unit_recipe(
            catalog, tmp_path, descriptor, game="bfme2"
        )
        prepared_result = compile_scenario_unit_recipe(
            catalog,
            tmp_path,
            descriptor,
            game="bfme2",
            prepared=prepared,
            source_documents=documents,
            prebuilt_visual_closure=raw_closure,
        )

    assert prepared_result == standalone
    assert compile_neutral_unit_pack_artifact(
        *prepared_result, game="bfme2", catalog_descriptor=descriptor
    ) == compile_neutral_unit_pack_artifact(
        *standalone, game="bfme2", catalog_descriptor=descriptor
    )


def test_effective_bfme2_neutral_recipe_blockers_are_closed(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    repo = Path(__file__).resolve().parents[2]
    catalog_path = repo / ".private" / "retail-work" / "catalog" / "bfme2.json"
    effective_root = (
        repo / ".private" / "retail-work" / "cache" / "effective-assets"
    )
    if not catalog_path.is_file() or not effective_root.is_dir():
        pytest.skip("operator BFME2 retail recipe inputs are not available")
    catalog = InstallCatalog.load(catalog_path)
    documents = _source_documents(effective_root)
    prepared = prepare_playable_unit_compiler(documents)
    neutral = compile_neutral_mob_catalog(
        documents, game="bfme2", prepared=prepared
    )
    rows = {row["objectId"]: row for row in neutral["neutralMobs"]}

    compiled = {
        object_id: compile_scenario_unit_recipe(
            catalog,
            effective_root,
            rows[object_id]["descriptor"],
            game="bfme2",
            prepared=prepared,
        )
        for object_id in (
            "RohanPeasant",
            "RohanPeasant1",
            "RohanPeasantHorde",
            "MordorShelob",
            "HobbitCivilianHorde",
        )
    }
    recipes = {object_id: result[2] for object_id, result in compiled.items()}
    artifacts = {
        object_id: compile_neutral_unit_pack_artifact(
            *result,
            game="bfme2",
            catalog_descriptor=rows[object_id]["descriptor"],
        )
        for object_id, result in compiled.items()
    }
    assert set(artifacts) == set(compiled)
    assert artifacts["RohanPeasantHorde"]["objectId"] == "RohanPeasantHorde"
    for object_id, artifact in artifacts.items():
        assert artifact["catalogDescriptorSha256"] == rows[object_id][
            "descriptor"
        ]["descriptorSha256"]
        assert artifact["integratedDescriptorSha256"] == compiled[object_id][0][
            "descriptorSha256"
        ]
        assert artifact["catalogDescriptor"]["scenarioAdmission"] == artifact[
            "descriptor"
        ]["scenarioAdmission"]
        assert artifact["catalogDescriptor"]["production"] == artifact[
            "descriptor"
        ]["production"] == []

    real_row = deepcopy(rows["RohanPeasant"])
    mini_catalog = {
        "game": "bfme2",
        "neutralMobs": [real_row],
        "catalogSha256": "d" * 64,
    }
    monkeypatch.setitem(neutral_profile_subject.EXPECTED_COUNTS, "bfme2", 1)
    monkeypatch.setattr(
        neutral_profile_subject, "validate_neutral_mob_catalog", lambda value: None
    )
    monkeypatch.setattr(
        neutral_profile_subject,
        "validate_neutral_dependency_pack_artifact",
        lambda value: None,
    )
    from importer.tests.test_neutral_pack_profile import _dependency_artifact

    real_profile = compose_neutral_pack_profile(
        mini_catalog,
        [artifacts["RohanPeasant"]],
        dependency_artifact=_dependency_artifact(mini_catalog),
        version="retail-regression",
    )
    real_receipt = real_profile["runtime_data"][
        "data/neutral/pack-profile-receipt.json"
    ]["rows"][0]
    assert real_receipt["catalogDescriptorSha256"] == artifacts[
        "RohanPeasant"
    ]["catalogDescriptorSha256"]
    assert real_receipt["integratedDescriptorSha256"] == artifacts[
        "RohanPeasant"
    ]["integratedDescriptorSha256"]

    for object_id in ("RohanPeasant", "RohanPeasant1", "RohanPeasantHorde"):
        assert "UPRohan_Peasant" in recipes[object_id]["runtimeRegistration"][
            "imageBindings"
        ]
    assert "HPShelobNew" in recipes["MordorShelob"]["runtimeRegistration"][
        "imageBindings"
    ]
    presentations = recipes["HobbitCivilianHorde"]["runtimeRegistration"][
        "visual"
    ]["memberPresentations"]
    assert [
        (row["objectId"], row["count"], set(row["coreAnimations"]))
        for row in presentations
    ] == [
        ("HobbitCivilian", 3, {"idle", "move", "death"}),
        ("HobbitCivilianFemale", 2, {"idle", "move", "death"}),
    ]


def test_effective_bfme2_neutral_audio_routes_resolve_exactly() -> None:
    repo = Path(__file__).resolve().parents[2]
    catalog_path = repo / ".private" / "retail-work" / "catalog" / "bfme2.json"
    if not catalog_path.is_file():
        pytest.skip("operator BFME2 retail catalog is not available")
    catalog = InstallCatalog.load(catalog_path)
    documents = dict(read_catalog_documents(catalog))
    prepared = prepare_playable_unit_compiler(documents)
    definitions = parse_sage_audio_definitions(
        b"\n".join(
            _read_document(catalog, path).source
            for path in (SOUND_EFFECTS_PATH, VOICE_PATH, MUSIC_PATH)
        )
    )
    entry_paths = [str(entry.name) for entry in catalog.entries]
    silent = {"Cow", "Crow", "Dove_white_in_game", "Goat", "Sheep", "Wolf"}
    targets = [
        *sorted(silent, key=str.casefold),
        "CaveTroll_Slaved",
        "BarrowWight",
        "BarrowWight_Slaved",
        "BarrowWight_Summoned",
        "FireDrake_Slaved",
    ]
    roots_by_target: dict[str, list[str]] = {}
    for object_id in targets:
        descriptor = compile_playable_unit_descriptor(
            object_id,
            documents,
            prepared=prepared,
            scenario_admission={
                "role": "creature",
                "surfaces": ["map-placement"],
            },
        )
        roots = sorted(_required_audio_ids(descriptor), key=str.casefold)
        roots_by_target[object_id] = roots
        assert not {
            value.casefold() for value in roots
        } & {"nosound", "eva", "sound", "+sound"}
        if not roots:
            continue
        closure = resolve_sage_audio_closure(definitions, roots)
        _, missing, ambiguous = resolve_audio_sample_paths_partial(
            closure.sample_ids, entry_paths
        )
        assert not missing
        assert not ambiguous

    assert all(roots_by_target[object_id] == [] for object_id in silent)
    assert "BarrowWightVoxCreated" in roots_by_target["BarrowWight"]
    assert "BarrowWightVoxCreated" in roots_by_target["BarrowWight_Slaved"]
    assert "BarrowWightVoxCreated" in roots_by_target["BarrowWight_Summoned"]
    assert "FireDrakeVoxCreated" in roots_by_target["FireDrake_Slaved"]


def test_scenario_source_resolution_rejects_missing_and_ambiguous_images(
    tmp_path: Path,
) -> None:
    descriptor = {
        "presentation": {
            "ui": {"portraitImageIds": ["WolfPortrait"], "commands": []},
            "audioRoutes": {"container": {}, "primaryMember": {}},
        }
    }
    catalog = SimpleNamespace(entries=[])
    missing = SimpleNamespace(
        source=b"MappedImage Other\n Texture = Other.tga\n TextureWidth = 1\n TextureHeight = 1\n Coords = Left:0 Top:0 Right:1 Bottom:1\nEnd\n",
        virtual_path="data/ini/mappedimages/test.ini",
        sha256="a" * 64,
        size=128,
    )
    with mock.patch(
        "openbfme_importer.playable_unit_import._mapped_image_documents",
        return_value=[missing],
    ), pytest.raises(ValueError, match="mapped image is unresolved"):
        _scenario_source_resolution(catalog, descriptor, effective_root=tmp_path)

    duplicate_source = b"""
MappedImage WolfPortrait
 Texture = Wolf.tga
 TextureWidth = 1
 TextureHeight = 1
 Coords = Left:0 Top:0 Right:1 Bottom:1
End
MappedImage WolfPortrait
 Texture = Wolf2.tga
 TextureWidth = 1
 TextureHeight = 1
 Coords = Left:0 Top:0 Right:1 Bottom:1
End
"""
    ambiguous = SimpleNamespace(
        source=duplicate_source,
        virtual_path="data/ini/mappedimages/test.ini",
        sha256="b" * 64,
        size=len(duplicate_source),
    )
    with mock.patch(
        "openbfme_importer.playable_unit_import._mapped_image_documents",
        return_value=[ambiguous],
    ), pytest.raises(ValueError, match="mapped image is ambiguous"):
        _scenario_source_resolution(catalog, descriptor, effective_root=tmp_path)


@pytest.mark.parametrize(
    ("image_id", "relative_path"),
    [
        ("UPRohan_Peasant", "art/compiledtextures/up/uprohan_peasant.dds"),
        ("HPShelobNew", "art/compiledtextures/hp/hpshelobnew.dds"),
    ],
)
def test_scenario_source_resolution_accepts_exact_standalone_full_texture(
    tmp_path: Path, image_id: str, relative_path: str
) -> None:
    descriptor = {
        "presentation": {
            "ui": {"portraitImageIds": [image_id], "commands": []},
            "audioRoutes": {"container": {}, "primaryMember": {}},
        }
    }
    physical = tmp_path.joinpath(*relative_path.split("/"))
    physical.parent.mkdir(parents=True)
    dds = bytearray(128)
    dds[:4] = b"DDS "
    dds[4:8] = (124).to_bytes(4, "little")
    dds[12:16] = (64).to_bytes(4, "little")
    dds[16:20] = (128).to_bytes(4, "little")
    physical.write_bytes(dds)
    catalog = SimpleNamespace(entries=[])
    missing = SimpleNamespace(
        source=b"MappedImage Other\n Texture = Other.tga\n TextureWidth = 1\n TextureHeight = 1\n Coords = Left:0 Top:0 Right:1 Bottom:1\nEnd\n",
        virtual_path="data/ini/mappedimages/test.ini",
        sha256="a" * 64,
        size=128,
    )

    with mock.patch(
        "openbfme_importer.playable_unit_import._mapped_image_documents",
        return_value=[missing],
    ), mock.patch(
        "openbfme_importer.playable_unit_import._resolved_strings", return_value={}
    ):
        images, audio, strings, receipt = _scenario_source_resolution(
            catalog, descriptor, effective_root=tmp_path
        )

    assert audio == {}
    assert strings == {}
    assert images[image_id]["compiledTextureVirtualPath"] == relative_path
    assert images[image_id]["coords"] == {
        "left": 0,
        "top": 0,
        "right": 128,
        "bottom": 64,
    }
    assert receipt["scenarioMediaDelta"] == [
        {
            "id": image_id,
            "resolution": "exact-standalone-full-texture",
            "compiledTextureVirtualPath": relative_path,
            "sha256": __import__("hashlib").sha256(dds).hexdigest(),
            "byteLength": 128,
            "width": 128,
            "height": 64,
        }
    ]


def test_scenario_source_resolution_accepts_exact_new_suffix_atlas_alias(
    tmp_path: Path,
) -> None:
    descriptor = {
        "presentation": {
            "ui": {"portraitImageIds": ["HPShelobNew"], "commands": []},
            "audioRoutes": {"container": {}, "primaryMember": {}},
        }
    }
    source = b"""
MappedImage HPShelob
 Texture = HeroUI_052.tga
 TextureWidth = 256
 TextureHeight = 256
 Coords = Left:0 Top:0 Right:192 Bottom:192
End
"""
    document = SimpleNamespace(
        source=source,
        virtual_path="data/ini/mappedimages/heroui.ini",
        sha256=__import__("hashlib").sha256(source).hexdigest(),
        size=len(source),
    )
    catalog = SimpleNamespace(
        entries=[SimpleNamespace(name="art/compiledtextures/he/heroui_052.dds")]
    )
    with mock.patch(
        "openbfme_importer.playable_unit_import._mapped_image_documents",
        return_value=[document],
    ), mock.patch(
        "openbfme_importer.playable_unit_import._resolved_strings", return_value={}
    ):
        images, _audio, _strings, receipt = _scenario_source_resolution(
            catalog, descriptor, effective_root=tmp_path
        )

    assert images["HPShelobNew"]["coords"] == {
        "left": 0,
        "top": 0,
        "right": 192,
        "bottom": 192,
    }
    assert receipt["scenarioMediaDelta"] == [
        {
            "id": "HPShelobNew",
            "resolution": "exact-new-suffix-mapped-image-alias",
            "sourceMappedImageId": "HPShelob",
            "compiledTextureVirtualPath": "art/compiledtextures/he/heroui_052.dds",
            "textureWidth": 256,
            "textureHeight": 256,
            "coords": {"left": 0, "top": 0, "right": 192, "bottom": 192},
        }
    ]


def test_exact_standalone_image_outranks_new_suffix_atlas_alias(
    tmp_path: Path,
) -> None:
    descriptor = {
        "presentation": {
            "ui": {"portraitImageIds": ["HPShelobNew"], "commands": []},
            "audioRoutes": {"container": {}, "primaryMember": {}},
        }
    }
    source = b"""
MappedImage HPShelob
 Texture = HeroUI_052.tga
 TextureWidth = 256
 TextureHeight = 256
 Coords = Left:0 Top:0 Right:192 Bottom:192
End
"""
    document = SimpleNamespace(
        source=source,
        virtual_path="data/ini/mappedimages/heroui.ini",
        sha256=__import__("hashlib").sha256(source).hexdigest(),
        size=len(source),
    )
    physical = tmp_path / "art" / "compiledtextures" / "hp" / "hpshelobnew.dds"
    physical.parent.mkdir(parents=True)
    dds = bytearray(128)
    dds[:4] = b"DDS "
    dds[4:8] = (124).to_bytes(4, "little")
    dds[12:16] = (96).to_bytes(4, "little")
    dds[16:20] = (128).to_bytes(4, "little")
    physical.write_bytes(dds)
    catalog = SimpleNamespace(
        entries=[SimpleNamespace(name="art/compiledtextures/he/heroui_052.dds")]
    )
    with mock.patch(
        "openbfme_importer.playable_unit_import._mapped_image_documents",
        return_value=[document],
    ), mock.patch(
        "openbfme_importer.playable_unit_import._resolved_strings", return_value={}
    ):
        images, _audio, _strings, receipt = _scenario_source_resolution(
            catalog, descriptor, effective_root=tmp_path
        )

    assert images["HPShelobNew"]["compiledTextureVirtualPath"] == (
        "art/compiledtextures/hp/hpshelobnew.dds"
    )
    assert receipt["scenarioMediaDelta"][0]["resolution"] == (
        "exact-standalone-full-texture"
    )
