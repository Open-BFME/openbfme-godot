from __future__ import annotations

from copy import deepcopy
from pathlib import Path

import pytest

from openbfme_importer.cli import build_parser
from openbfme_importer.playable_unit_import import (
    _resolved_media,
    _selected_base_profile,
    extend_profile_with_unit,
)
from openbfme_importer.profile import ImportProfile
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
