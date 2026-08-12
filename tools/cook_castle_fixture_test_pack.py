#!/usr/bin/env python3
"""Cook the lane-L2b castle-fixture test pack (Erebor + Carn Dum).

The mounted packs predate lane L2a and ship no fixtures.json, and lane policy
forbids a republish.  Runtime proof for L2b therefore loads LOCALLY COOKED
documents through the production loader.  This tool cooks the two maps from
the pure RotWK oracle with ``convert_sage_map``:

* real ``fixtures.json`` documents (``castle_fixtures.build_map_fixtures``);
* the L2b lifecycle-structure reclassification
  (``castle_fixtures.rebind_castle_fixture_structures``) applied to a
  synthetic renderable binding set — one models row per fixture type, since
  the full visual-closure binder is a multi-minute build the runtime proof
  does not need (the loader never reads GLB payloads);
* 20-byte header-valid GLB stubs for every bound model, so the loader's
  ``_validate_bound_glb`` (magic/version/length) sees exactly the shape a
  real cook produces.

Layout mirrors a real pack: ``<out>/<slug>/map.json`` et al. and
``<out>/assets/models/props/*.glb``, so the map directory's parent is the
pack root handed to ``RetailMapData.load_from_pack``.

Usage (from the worktree root, pinned interpreter):

    python tools/cook_castle_fixture_test_pack.py --output %TEMP%/kimi-L2b-castle-pack

Read-only against the oracle; writes only under --output.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

PRIVATE_ROOT = ROOT / ".private" / "retail-work"
if (
    not (PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets").is_dir()
    and ROOT.parent.name == "worktrees"
):
    PRIVATE_ROOT = ROOT.parents[2] / ".private" / "retail-work"
EFFECTIVE_ASSETS = PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets"
CATALOG_PATH = PRIVATE_ROOT / "catalog" / "rotwk.json"

MAPS = {
    "wor-erebor": "maps/map wor erebor/map wor erebor.map",
    "wor-ang-carn-dum": "maps/map wor ang carn dum/map wor ang carn dum.map",
}


def _corpus_documents() -> dict[str, bytes]:
    ini_root = EFFECTIVE_ASSETS / "data" / "ini"
    documents: dict[str, bytes] = {}
    for path in sorted(ini_root.rglob("*")):
        if path.suffix.casefold() in {".ini", ".inc"} and path.is_file():
            documents[path.relative_to(EFFECTIVE_ASSETS).as_posix()] = path.read_bytes()
    return documents


def _stub_glb(path: Path) -> None:
    """A header-valid GLB stub: magic, version 2, length == file size."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(struct.pack("<III", 0x46546C67, 2, 20) + b"\0" * 8)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--maps",
        nargs="*",
        default=sorted(MAPS),
        choices=sorted(MAPS),
        help="subset of maps to cook (default: both)",
    )
    args = parser.parse_args()

    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.castle_fixtures import (
        build_map_fixtures,
        castle_fixture_seed_disposition,
        rebind_castle_fixture_structures,
    )
    from openbfme_importer.map_profile import CASTLE_SIEGE_MAPS
    from openbfme_importer.sage_map import MAX_SOURCE_BYTES, convert_sage_map
    from openbfme_importer.terrain_materials import (
        convert_terrain_materials,
        resolve_terrain_material_references,
    )

    documents = _corpus_documents()
    catalog = InstallCatalog.load(CATALOG_PATH)
    terrain_ini = EFFECTIVE_ASSETS / "data" / "ini" / "terrain.ini"
    terrain_ini_bytes = terrain_ini.read_bytes()
    args.output.mkdir(parents=True, exist_ok=True)

    summary: dict[str, object] = {}
    for slug in args.maps:
        virtual = MAPS[slug]
        entry = catalog.resolve_exact(virtual)
        if entry is None:
            print(f"FAIL map missing from retail catalog: {virtual}")
            return 1
        archive = catalog.open_archive_for(entry)
        source = archive.read_entry(catalog.as_entry(entry), max_bytes=MAX_SOURCE_BYTES)
        source_path = args.output / f"{slug}.map"
        source_path.write_bytes(source)

        from openbfme_importer.sage_map import parse_sage_map_bytes

        parsed = parse_sage_map_bytes(source)
        fixtures = build_map_fixtures(documents, parsed.objects, game="rotwk")
        type_names = sorted({str(row["typeName"]) for row in fixtures["fixtures"]})
        bindings = {
            "logical": [],
            "models": [
                {
                    "typeName": name,
                    "sourceVirtualModel": f"art/w3d/eb/{name.casefold()}.w3d",
                    "glb": f"assets/models/props/{name.casefold()}.glb",
                    "matchMethod": "exact-type-name",
                }
                for name in type_names
            ],
        }
        bindings, evidence = rebind_castle_fixture_structures(bindings, fixtures)

        # A real map-scope terrain-materials cook (the production loader
        # digest-validates every texture row the map's blend table names).
        symbols = [str(row["name"]) for row in parsed.blend["textures"]]
        material_refs = resolve_terrain_material_references(terrain_ini_bytes, symbols)
        material_sources: list[tuple[str, Path]] = [
            ("data/ini/terrain.ini", terrain_ini)
        ]
        seen_textures: set[str] = set()
        for reference in material_refs:
            texture_key = str(reference.texture).casefold()
            if texture_key in seen_textures:
                continue
            seen_textures.add(texture_key)
            texture_path = EFFECTIVE_ASSETS / "art" / "terrain" / str(reference.texture)
            material_sources.append((f"art/terrain/{reference.texture}", texture_path))
        materials_output = args.output / "assets" / "terrain" / slug
        convert_terrain_materials(material_sources, materials_output, symbols)

        output = args.output / slug
        convert_sage_map(
            source_path,
            output,
            metadata={
                "id": f"rotwk.map.{slug}",
                "displayName": slug,
                "castleSiege": CASTLE_SIEGE_MAPS[virtual]["runtimeContract"],
                "terrainMaterials": f"assets/terrain/{slug}/terrain-materials.json",
            },
            object_bindings=bindings,
            fixtures=fixtures,
        )
        for row in [*bindings["models"], *bindings["structures"]]:
            _stub_glb(args.output / str(row["glb"]))

        deferred: dict[str, int] = {}
        seeded = 0
        for row in fixtures["fixtures"]:
            disposition = castle_fixture_seed_disposition(row)
            if disposition == "seed":
                seeded += 1
            else:
                deferred[disposition] = deferred.get(disposition, 0) + 1
        summary[slug] = {
            "fixtures": int(fixtures["count"]),
            "seeded": seeded,
            "deferred": deferred,
            "lifecycleTypes": len(evidence["movedTypeNames"]),
        }
        print(
            f"COOKED {slug}: fixtures={fixtures['count']} seeded={seeded} "
            f"deferred={deferred} lifecycleTypes={len(evidence['movedTypeNames'])}"
        )
        source_path.unlink()

    print(json.dumps(summary, indent=1, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
