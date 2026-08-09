"""Offline coverage for the RotWK multimap binder handoff."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path

import pytest

from openbfme_importer import cli
from openbfme_importer.catalog import InstallCatalog

from importer.tests.test_big import make_big
from importer.tests.test_map_census import _encoded_path
from importer.tests.test_sage_map import _synthetic_map


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "rotwk_multimap_skirmish.py"


def _encoded_display_key(key: str) -> str:
    """Escape one string-table key exactly as retail's mapcache does.

    Retail writes the ``displayName`` field as UTF-16LE bytes with every
    non-literal byte escaped ``_XX``; escaping every byte is a valid instance
    of that encoding and round-trips through the census decoder.
    """

    return "".join(f"_{byte:02X}" for byte in key.encode("utf-16-le"))


def _mapcache_record(
    path: str, *, players: int = 1, display_key: str = ""
) -> str:
    lines = [f"MapCache {_encoded_path(path)}"]
    if display_key:
        lines.append(f"  displayName = {_encoded_display_key(display_key)}")
    lines.extend(
        [
            "  isMultiplayer = Yes",
            "  isOfficial = Yes",
            "  isScenarioMP = No",
            f"  numPlayers = {players}",
            "End",
        ]
    )
    return "\n".join(lines)


def _string_table(names: dict[str, str]) -> bytes:
    blocks = [f'{key}\n "{value}"\nEND' for key, value in names.items()]
    return ("\n\n".join(blocks) + "\n").encode("cp1252")


def _native_emission_catalog(root: Path) -> InstallCatalog:
    """An install whose registry authors names the directories get wrong."""

    source, _ = _synthetic_map()
    directories = {
        # Directory derivation would say "Fall Back 4p"; retail authors
        # "Stonewain Valley". This is the exact defect class the emission lane
        # must never reintroduce.
        "map mp fall back 4p": "Map:MAPMPFallBack4p",
        # Two DIFFERENT maps that both strip to "harlindon".
        "map mp harlindon": "Map:MAPMPHarlindon",
        "map wor harlindon": "Map:MAPWORHarlindon",
        # Ships no displayName key at all: the derived fallback must be
        # recorded in planning evidence, never silent.
        "map mp bravo ridge": "",
    }
    entries: dict[str, bytes] = {
        "data/lotr.str": _string_table(
            {
                "Map:MAPMPFallBack4p": "Stonewain Valley",
                "Map:MAPMPHarlindon": "Harlindon",
                "Map:MAPWORHarlindon": "Harlindon",
            }
        ),
    }
    records = []
    for directory, display_key in directories.items():
        base = f"maps/{directory}/{directory}"
        entries[f"{base}.map"] = source
        entries[f"{base}_art.tga"] = b"art"
        entries[f"{base}_pic.tga"] = b"preview"
        records.append(
            _mapcache_record(f"{base}.map", display_key=display_key)
        )
    entries["maps/mapcache.ini"] = ("\n".join(records) + "\n").encode("cp1252")
    make_big(root / "maps.big", entries)
    return InstallCatalog.build(root)


def _load_tool():
    spec = importlib.util.spec_from_file_location("rotwk_multimap_skirmish", TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_planned_model_binding_is_nonempty_and_survives_pack_cook(tmp_path: Path) -> None:
    mod = _load_tool()
    profile = {
        "resources": [
            {
                "converter": "sage-map",
                "output": "maps/mock-map",
                "options": {
                    "objectBindings": {
                        "logical": [],
                        "models": [
                            {
                                "typeName": "RetailTree",
                                "sourceVirtualModel": "art/w3d/retailtree.w3d",
                                "glb": "assets/models/props/retailtree.glb",
                                "matchMethod": "exact-type-name",
                            }
                        ],
                    }
                },
            }
        ]
    }
    inventory = mod.profile_binding_inventory(profile)
    assert inventory["modelCount"] == 1

    binding_path = tmp_path / "maps" / "mock-map" / "object-bindings.json"
    binding_path.parent.mkdir(parents=True)
    binding_path.write_text(
        json.dumps(
            {
                "records": [
                    {"typeName": "RetailTree", "status": "bound"},
                    {"typeName": "CaptureFlag", "status": "unresolved"},
                ]
            }
        ),
        encoding="utf-8",
    )
    proof = mod.verify_pack_binding_inventory(tmp_path, inventory)
    assert proof["ok"] is True
    assert proof["checkedMapCount"] == 1


def test_pack_proof_fails_when_planned_binding_is_dropped(tmp_path: Path) -> None:
    mod = _load_tool()
    planned = {
        "maps": {
            "maps/mock-map": {
                "boundTypeNames": ["RetailTree"],
            }
        }
    }
    binding_path = tmp_path / "maps" / "mock-map" / "object-bindings.json"
    binding_path.parent.mkdir(parents=True)
    binding_path.write_text(
        json.dumps({"records": [{"typeName": "RetailTree", "status": "unresolved"}]}),
        encoding="utf-8",
    )
    proof = mod.verify_pack_binding_inventory(tmp_path, planned)
    assert proof["ok"] is False
    assert proof["missingSample"] == ["maps/mock-map:RetailTree"]


def test_cli_exposes_map_limit_and_publish_without_select() -> None:
    parser = cli.build_parser()
    map_args = parser.parse_args(
        [
            "generate-map-profile",
            "--install",
            "retail",
            "--game",
            "rotwk",
            "--map-set",
            "skirmish",
            "--map-limit",
            "10",
        ]
    )
    assert map_args.map_limit == 10
    build_args = parser.parse_args(
        ["build", "--install", "retail", "--no-select"]
    )
    assert build_args.no_select is True


def test_registry_catalog_natively_emits_names_categories_and_art(
    tmp_path: Path,
) -> None:
    """A fresh registry-catalog cook needs no post-hoc --repair-catalog.

    Pins the three fields the shipped goal-official-72 pack got wrong at once:
    category (skirmish vs wotr-battle), authored displayName (never the
    title-cased directory), and bound preview/art resources.
    """

    mod = _load_tool()
    catalog = _native_emission_catalog(tmp_path)
    profile = mod.build_registry_skirmish_catalog(catalog, game="bfme2")

    rows = {
        str(row["id"]): row
        for row in profile["runtime_data"]["data/maps.json"]["maps"]
    }
    # Skirmish slugs use the canonical runtime grammar (the ONLY id the
    # runtime installs scripts under: retail_vertical_slice.gd:5027-5037);
    # non-skirmish rows keep their retail kind token, which is what keeps the
    # two Harlindons collision-free.
    assert sorted(rows) == [
        "bfme2.map.bravo-ridge",
        "bfme2.map.fall-back-4p",
        "bfme2.map.harlindon",
        "bfme2.map.wor-harlindon",
    ]

    # Authored names win; the directory derivation is a recorded last resort.
    assert rows["bfme2.map.fall-back-4p"]["displayName"] == "Stonewain Valley"
    assert rows["bfme2.map.harlindon"]["displayName"] == "Harlindon"
    assert rows["bfme2.map.wor-harlindon"]["displayName"] == "Harlindon"
    assert rows["bfme2.map.bravo-ridge"]["displayName"] == "Bravo Ridge"

    # Real category classification: wor rows are never skirmish.
    assert rows["bfme2.map.fall-back-4p"]["category"] == "skirmish"
    assert rows["bfme2.map.wor-harlindon"]["category"] == "wotr-battle"
    evidence = profile["planning_evidence"]
    assert evidence["categoryCounts"] == {"skirmish": 3, "wotr-battle": 1}

    # Every row binds its art and preview, and the conversions are declared.
    for row in rows.values():
        slug = str(row["id"]).rsplit(".", 1)[-1]
        assert row["art"] == f"assets/ui/maps/{slug}-art.png"
        assert row["preview"] == f"assets/ui/maps/{slug}-preview.png"
    resource_ids = {str(item["id"]) for item in profile["resources"]}
    assert "map-fall-back-4p-art" in resource_ids
    assert "map-wor-harlindon-preview" in resource_ids
    assert evidence["mapArtCounts"] == {"withPreview": 4, "withArt": 4}

    # Name provenance is evidence, not trust.
    assert evidence["displayNameSource"]["derivedFromDirectoryCount"] == 1
    assert evidence["displayNameSource"]["derivedFromDirectorySlugs"] == [
        "bravo-ridge"
    ]
    assert evidence["displayNameSource"]["authoredCount"] == 3

    # The pack's entry map must be a skirmish row even though the catalog also
    # carries WOTR battle maps.
    entry_map = profile["pack"]["files"]["entryMap"]
    entry_rows = [row for row in rows.values() if row["map"] == entry_map]
    assert entry_rows and entry_rows[0]["category"] == "skirmish"


def test_repair_leaves_ambiguous_kind_stripped_art_unbound(tmp_path: Path) -> None:
    """``map mp harlindon`` and ``map wor harlindon`` both strip to "harlindon".

    A bare ``harlindon-art.png`` in the pack must bind to NEITHER: both rows
    are reported under ambiguousArtLeftUnbound instead of one map silently
    showing the other's art.
    """

    mod = _load_tool()
    install = tmp_path / "install"
    install.mkdir()
    payloads = {
        "maps/map mp harlindon/map mp harlindon.map": b"payload-mp-harlindon",
        "maps/map wor harlindon/map wor harlindon.map": b"payload-wor-harlindon",
    }
    entries: dict[str, bytes] = dict(payloads)
    entries["data/lotr.str"] = _string_table(
        {"Map:MAPMPHarlindon": "Harlindon", "Map:MAPWORHarlindon": "Harlindon"}
    )
    entries["maps/mapcache.ini"] = (
        "\n".join(
            _mapcache_record(path, display_key=key)
            for path, key in (
                (
                    "maps/map mp harlindon/map mp harlindon.map",
                    "Map:MAPMPHarlindon",
                ),
                (
                    "maps/map wor harlindon/map wor harlindon.map",
                    "Map:MAPWORHarlindon",
                ),
            )
        )
        + "\n"
    ).encode("cp1252")
    make_big(install / "maps.big", entries)
    catalog = InstallCatalog.build(install)

    pack = tmp_path / "pack"
    for slug, path in (
        ("mp-harlindon", "maps/map mp harlindon/map mp harlindon.map"),
        ("wor-harlindon", "maps/map wor harlindon/map wor harlindon.map"),
    ):
        cooked_dir = pack / "maps" / slug
        cooked_dir.mkdir(parents=True)
        (cooked_dir / "map.json").write_text(
            json.dumps(
                {
                    "id": f"rotwk.map.{slug}",
                    "displayName": "stale",
                    "category": "skirmish",
                    "source": {
                        "sha256": hashlib.sha256(payloads[path]).hexdigest()
                    },
                }
            ),
            encoding="utf-8",
        )
    art_dir = pack / "assets" / "ui" / "maps"
    art_dir.mkdir(parents=True)
    (art_dir / "harlindon-art.png").write_bytes(b"png")
    (pack / "data").mkdir()
    (pack / "data" / "maps.json").write_text(
        json.dumps(
            {
                "schema": "openbfme.map-catalog",
                "schemaVersion": 0,
                "maps": [
                    {
                        "id": "rotwk.map.mp-harlindon",
                        "displayName": "stale",
                        "category": "skirmish",
                        "map": "maps/mp-harlindon/map.json",
                    },
                    {
                        "id": "rotwk.map.wor-harlindon",
                        "displayName": "stale",
                        "category": "skirmish",
                        "map": "maps/wor-harlindon/map.json",
                    },
                ],
            }
        ),
        encoding="utf-8",
    )

    report = mod.repair_pack_catalog(catalog, pack, apply=True)

    assert report["unmatched"] == []
    assert report["categoryCounts"] == {"skirmish": 1, "wotr-battle": 1}
    assert report["ambiguousArtLeftUnbound"] == [
        "rotwk.map.mp-harlindon:harlindon",
        "rotwk.map.wor-harlindon:harlindon",
    ]
    repaired = json.loads(
        (pack / "data" / "maps.json").read_text(encoding="utf-8")
    )
    for row in repaired["maps"]:
        assert row["displayName"] == "Harlindon"
        assert not row.get("art")
        assert not row.get("preview")


def test_map_slug_uses_canonical_runtime_slug_for_skirmish_sources() -> None:
    """Skirmish slugs match the runtime's canonical map-id grammar.

    The runtime installs a map's script composite ONLY under
    ``<game>.map.<canonical runtime slug>`` (``map mp `` prefix stripped);
    a kept ``mp`` token authors a row whose scripts are refused forever -
    the goal-official-72 harlindon defect. Non-skirmish families keep their
    kind tokens so ``map wor harlindon`` stays distinct beside it.
    """

    mod = _load_tool()
    assert (
        mod._map_slug("maps/map mp adorn river/map mp adorn river.map")
        == "adorn-river"
    )
    assert (
        mod._map_slug("maps/map wor ang barrow downs/map wor ang barrow downs.map")
        == "wor-ang-barrow-downs"
    )
    assert mod._map_slug("maps/map mp harlindon/map mp harlindon.map") == "harlindon"
    assert (
        mod._map_slug("maps/map wor harlindon/map wor harlindon.map")
        == "wor-harlindon"
    )
    # A non-canonical skirmish payload (directory/file identity mismatch)
    # cannot install scripts anyway; it keeps the disambiguated fallback.
    assert mod._map_slug("maps/map mp odd/renamed.map") == "mp-odd"


def test_load_catalog_refuses_to_overwrite_on_install_root_mismatch(
    tmp_path: Path,
) -> None:
    """A cached catalog built from another install is an error, not a rebuild.

    Silently rebuilding once destroyed the layered RotWK catalog with a
    BFME2-rooted one - no prompt, no backup.  The mismatch must refuse and
    name both roots, leaving the cached bytes untouched.
    """

    mod = _load_tool()
    owning_install = tmp_path / "owning-install"
    owning_install.mkdir()
    make_big(owning_install / "maps.big", {"maps/mapcache.ini": b"x"})
    other_install = tmp_path / "other-install"
    other_install.mkdir()
    state_root = tmp_path / "state"
    catalog_path = state_root / "catalog" / "rotwk.json"
    catalog_path.parent.mkdir(parents=True)
    InstallCatalog.build(owning_install).save(catalog_path)
    before = catalog_path.read_bytes()

    with pytest.raises(SystemExit) as failure:
        mod._load_catalog(state_root, "rotwk", other_install)

    message = str(failure.value)
    assert "catalog-install-mismatch" in message
    assert str(owning_install.resolve()) in message
    assert str(other_install.resolve()) in message
    assert catalog_path.read_bytes() == before


def test_default_assets_ignore_legacy_layered_overlay(tmp_path: Path) -> None:
    mod = _load_tool()
    stale = tmp_path / "editions" / "rotwk" / "cache" / "layered-effective-assets"
    canonical = tmp_path / "editions" / "rotwk" / "cache" / "effective-assets"
    stale.mkdir(parents=True)
    canonical.mkdir()

    assert mod._default_effective_assets(tmp_path, "rotwk") == canonical
