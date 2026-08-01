"""Seal the exact RotWK War of the Ring strategic APT source closure.

This planner mirrors the shell lane's proof shape without joining the
content-pack pipeline: the strategic bundle is staged into a release by
``tools/wotr-data-staging.ps1`` (env/schema lane), not by an ``ImportProfile``
resource, so no converter registration is added here.  What this module does
prove, fail-closed, before a conversion is trusted:

- every closure source exists below the private effective-assets view and its
  size and SHA-256 agree with the effective-assets manifest;
- the effective-assets view was EXTRACTED FROM THE CATALOG IT IS CHECKED
  AGAINST -- same install root, same catalog identity digest;
- the catalog is demonstrably a RotWK catalog, not another edition's served
  under the RotWK name;
- the catalog names the same winner for every source, matched on the FULL
  archive path INCLUDING its install-layer prefix;
- the per-archive census, source count, payload byte total and canonical
  source aggregate all match the sealed constants below.

WHY THE LAYER PREFIX IS LOAD-BEARING (the bug this file used to have).  A
RotWK catalog is layered: it indexes ``layer-0-rotwk/apt/menuexport.big`` AND
``layer-1-bfme2/apt/menuexport.big``, because RotWK genuinely mounts BFME2
underneath itself.  The layer directory is therefore the ONLY thing in an
archive name that says which edition a winner came from.  This planner used to
strip that prefix before comparing catalog candidate to manifest winner, which
made every BFME2 archive answer to its RotWK-layer name at identical size --
so a BFME2 effective-assets view (``cache/effective-assets``, install root
a BFME2-only tree) validated cleanly against ``catalog/rotwk.json`` and the whole
strategic bundle was cooked from BFME2's art.  Seventeen strategic/shell
sheets differ between the editions, including the palantir ring and the green
BFME2 shell plate where RotWK is blue-steel.  The comparison is now exact and
the view is bound to its catalog, so that substitution fails closed.

NOTE that ``layer-1-bfme2`` sources still appear in the sealed census below,
and that is CORRECT: RotWK ships no replacement for some MenuExport/TimeLine
geometry and inherits BFME2's.  What matters is that the RotWK OVERLAY decided
each winner, not that a BFME2-only tree supplied it.

The emitted plan is payload-free JSON -- identities, sizes, hashes and policy
only, never retail bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

from .catalog import InstallCatalog, catalog_provenance_reason
from .retail_strategic_apt_convert import (
    ATLAS_DIRECTORY,
    FONT_DIRECTORY,
    MANIFEST_FILE_NAME,
    MOVIE_CLOSURE,
    OUTPUT_SCHEMA,
    SCENE_ID,
    SCREEN_DIRECTORY,
    strategic_source_virtual_paths,
)
from .sage_apt import canonical_sha256
from .util import write_json_atomic


STRATEGIC_APT_PLAN_SCHEMA = "openbfme.retail-strategic-apt-plan"
STRATEGIC_APT_PLAN_SCHEMA_VERSION = 0

#: The retail edition this lane imports.  Stated, not inferred: the whole
#: point of the closure is that it is RISE OF THE WITCH KING's strategic UI.
EXPECTED_GAME = "rotwk"

#: The RotWK strategic closure, proved once against the RotWK edition's
#: effective-assets view (``editions/rotwk/cache/effective-assets``, the
#: layered RotWK-over-BFME2 overlay ``catalog/rotwk.json`` indexes).  These
#: are bounds, not guesses: a source set that no longer matches fails closed
#: rather than silently cooking a different strategic UI -- and, having been
#: measured against the RotWK overlay, they no longer accept the BFME2 view
#: (1,076 sources / 70,578,627 bytes) that this lane previously cooked.
EXPECTED_SOURCE_COUNT = 1_337
EXPECTED_PAYLOAD_BYTES = 71_541_102
EXPECTED_SOURCE_AGGREGATE_SHA256 = (
    "6ab93cb3ea6db0835c62ae029cac8218c2e204cd98ff8305863954ca61b831c7"
)

#: Winner archives, keyed by the FULL layered archive path.  ``layer-0-rotwk``
#: is the expansion and wins; ``layer-1-bfme2`` rows are the geometry and
#: texture pages RotWK never replaced and therefore genuinely inherits.  The
#: layer prefix is part of the identity on purpose -- see the module docstring.
EXPECTED_ARCHIVES: dict[str, int] = {
    "layer-0-rotwk/apt/gamewindowgadgets.big": 45,
    "layer-0-rotwk/apt/libingameimagesmain.big": 56,
    "layer-0-rotwk/apt/libingameui.big": 13,
    "layer-0-rotwk/apt/livingworldui.big": 14,
    "layer-0-rotwk/apt/menuexport.big": 191,
    "layer-0-rotwk/apt/menuframeandbg.big": 3,
    "layer-0-rotwk/apt/palantirexport.big": 21,
    "layer-0-rotwk/apt/strategicarmyunitswapper.big": 39,
    "layer-0-rotwk/apt/strategicbattleprompt.big": 46,
    "layer-0-rotwk/apt/strategicbattlepromptarmypanel.big": 13,
    "layer-0-rotwk/apt/strategicbattlepromptplayerpage.big": 20,
    "layer-0-rotwk/apt/strategicchecklist.big": 40,
    "layer-0-rotwk/apt/strategicconflictresults.big": 48,
    "layer-0-rotwk/apt/strategicdetailsarmies.big": 13,
    "layer-0-rotwk/apt/strategicdetailsarmyretinue.big": 44,
    "layer-0-rotwk/apt/strategicdetailsbuildqueue.big": 31,
    "layer-0-rotwk/apt/strategicdetailsregion.big": 9,
    "layer-0-rotwk/apt/strategicdetailsstructures.big": 15,
    "layer-0-rotwk/apt/strategicdetailsterritory.big": 10,
    "layer-0-rotwk/apt/strategicdetailstray.big": 23,
    "layer-0-rotwk/apt/strategicdynamicautoresolve.big": 52,
    "layer-0-rotwk/apt/strategicendturnbutton.big": 15,
    "layer-0-rotwk/apt/strategichud.big": 6,
    "layer-0-rotwk/apt/strategicnextturnind.big": 7,
    "layer-0-rotwk/apt/strategicpalantir.big": 21,
    "layer-0-rotwk/apt/strategicplayerstatus.big": 49,
    "layer-0-rotwk/apt/strategicregionaward.big": 45,
    "layer-0-rotwk/apt/strategicstats.big": 22,
    "layer-0-rotwk/apt/strategicveterancy.big": 51,
    "layer-0-rotwk/apt/timeline.big": 68,
    "layer-0-rotwk/data1.big": 3,
    "layer-0-rotwk/ini.big": 16,
    "layer-0-rotwk/textures2.big": 2,
    "layer-1-bfme2/apt/menuexport.big": 91,
    "layer-1-bfme2/apt/strategicarmyunitswapper.big": 18,
    "layer-1-bfme2/apt/strategicbattleprompt.big": 6,
    "layer-1-bfme2/apt/strategicbattlepromptplayerpage.big": 8,
    "layer-1-bfme2/apt/strategicdetailsarmies.big": 6,
    "layer-1-bfme2/apt/strategicdetailsstructures.big": 8,
    "layer-1-bfme2/apt/strategicstats.big": 14,
    "layer-1-bfme2/apt/strategicveterancy.big": 21,
    "layer-1-bfme2/apt/timeline.big": 60,
    "layer-1-bfme2/textures0.big": 5,
    "layer-1-bfme2/textures2.big": 49,
}

_MAX_JSON_BYTES = 64 * 1024 * 1024


def _mapping(value: object, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{context} must be an object")
    return dict(value)


def _load_json(path: Path | str, context: str) -> dict[str, Any]:
    source = Path(path).expanduser().resolve()
    if not source.is_file() or source.stat().st_size > _MAX_JSON_BYTES:
        raise ValueError(f"{context} is missing or too large")
    try:
        return _mapping(json.loads(source.read_text(encoding="utf-8")), context)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid {context}: {exc}") from exc


def _private_root(path: Path | str) -> Path:
    root = Path(path).expanduser().resolve()
    if ".private" not in {part.casefold() for part in root.parts} or not root.is_dir():
        raise ValueError("effective-assets root must be an existing private directory")
    return root


def _prove_edition(
    effective_assets_manifest: Mapping[str, Any],
    catalog: InstallCatalog,
    game: str,
) -> dict[str, Any]:
    """Bind the effective-assets view to a catalog of the requested edition.

    Two independent bindings, both fail-closed, because either one alone has
    a hole this lane already fell through:

    1. PROVENANCE.  The catalog must not be another edition's tree wearing the
       RotWK name (``catalog_provenance_reason`` refuses a catalog that
       carries a foreign patch archive and none of RotWK's own).
    2. IDENTITY.  The manifest must have been EXTRACTED FROM THIS CATALOG --
       same install root and the same catalog identity digest the extractor
       recorded.  This is the check that refuses the BFME2 view: its manifest
       names a BFME2-only install root and catalog identity ``a996810...``,
       neither of which the RotWK catalog can produce.
    """

    reason = catalog_provenance_reason(
        [archive.relative_path for archive in catalog.archives], game
    )
    if reason is not None:
        raise ValueError(f"strategic APT catalog is not a {game} catalog: {reason}")

    install = _mapping(
        effective_assets_manifest.get("install", {}), "effective-assets install record"
    )
    declared_root = str(install.get("root", ""))
    catalog_root = str(catalog.install_root)
    if Path(declared_root or ".").resolve() != Path(catalog_root).resolve():
        raise ValueError(
            "effective-assets view was not extracted from this catalog's install: "
            f"view root {declared_root!r} vs catalog root {catalog_root!r}"
        )
    declared_catalog = _mapping(
        effective_assets_manifest.get("catalog", {}), "effective-assets catalog record"
    )
    identity = catalog.identity_sha256()
    if str(declared_catalog.get("identity_sha256", "")).casefold() != identity:
        raise ValueError(
            "effective-assets view does not carry this catalog's identity digest"
        )
    return {
        "game": game,
        "catalogInstallRoot": catalog_root,
        "catalogIdentitySha256": identity,
        "catalogArchiveCount": len(catalog.archives),
        # The distinct install layers, in the catalog's own winning order:
        # the first entry shadows the rest, so this reads out "RotWK over
        # BFME2" as evidence rather than as an assumption.
        "layerPrecedence": list(
            dict.fromkeys(
                archive.relative_path.split("/", 1)[0]
                for archive in catalog.archives
                if "/" in archive.relative_path
            )
        ),
    }


def build_retail_strategic_apt_plan(
    effective_assets_root: Path | str,
    effective_assets_manifest: Mapping[str, Any],
    catalog: InstallCatalog,
    *,
    game: str = EXPECTED_GAME,
) -> dict[str, Any]:
    """Prove the exact strategic source closure against manifest and catalog."""

    root = _private_root(effective_assets_root)
    edition = _prove_edition(effective_assets_manifest, catalog, game)
    rows_raw = effective_assets_manifest.get("files")
    if not isinstance(rows_raw, list):
        raise ValueError("effective-assets manifest files must be an array")
    by_path: dict[str, dict[str, Any]] = {}
    for raw in rows_raw:
        row = _mapping(raw, "effective-assets manifest row")
        path = row.get("path")
        if not isinstance(path, str) or not path or path.casefold() in by_path:
            raise ValueError("effective-assets manifest contains an invalid path")
        if not isinstance(row.get("size"), int) or not isinstance(
            row.get("sha256"), str
        ):
            raise ValueError(
                "effective-assets manifest contains an invalid source identity"
            )
        by_path[path.casefold()] = row

    virtual_paths = strategic_source_virtual_paths(root)
    if len(virtual_paths) != EXPECTED_SOURCE_COUNT:
        raise ValueError("strategic APT source closure count changed")

    inventory: list[dict[str, Any]] = []
    archives: dict[str, int] = {}
    for virtual_path in virtual_paths:
        row = by_path.get(virtual_path.casefold())
        if row is None:
            raise ValueError(f"strategic source missing from manifest: {virtual_path}")
        source = root / Path(*virtual_path.split("/"))
        if not source.is_file() or source.stat().st_size != row["size"]:
            raise ValueError(f"strategic source missing or size changed: {virtual_path}")
        payload = source.read_bytes()
        digest = hashlib.sha256(payload).hexdigest()
        if digest != row["sha256"]:
            raise ValueError(f"strategic source digest changed: {virtual_path}")
        # EXACT winner agreement, layer prefix included.  The catalog's own
        # precedence resolution (``resolve_exact``) must land on the very
        # archive the extractor recorded -- same full layered path, same byte
        # size.  Nothing is normalised away: the layer directory is the only
        # field that distinguishes RotWK's copy of an archive from BFME2's,
        # and erasing it is exactly how this lane came to cook BFME2 art.
        winner = catalog.resolve_exact(virtual_path)
        manifest_archive = str(row["archive"])
        if (
            winner is None
            or winner.archive.casefold() != manifest_archive.casefold()
            or winner.size != row["size"]
        ):
            raise ValueError(
                "catalog winner disagrees with manifest: "
                f"{virtual_path} (manifest {manifest_archive!r}, catalog "
                f"{None if winner is None else winner.archive!r})"
            )
        key = manifest_archive.casefold()
        archives[key] = archives.get(key, 0) + 1
        inventory.append(
            {
                "virtualPath": virtual_path,
                "byteLength": row["size"],
                "sha256": digest,
            }
        )
    if archives != EXPECTED_ARCHIVES:
        raise ValueError("strategic APT archive closure changed")
    payload_bytes = sum(int(row["byteLength"]) for row in inventory)
    if payload_bytes != EXPECTED_PAYLOAD_BYTES:
        raise ValueError("strategic APT payload byte total changed")
    source_aggregate = canonical_sha256(inventory)
    if source_aggregate != EXPECTED_SOURCE_AGGREGATE_SHA256:
        raise ValueError("strategic APT source aggregate changed")

    plan: dict[str, Any] = {
        "schema": STRATEGIC_APT_PLAN_SCHEMA,
        "schemaVersion": STRATEGIC_APT_PLAN_SCHEMA_VERSION,
        "sceneId": SCENE_ID,
        "sourceEvidence": {
            "aggregateSha256": effective_assets_manifest.get("aggregate_sha256"),
            "edition": edition,
            "sourceAggregateSha256": source_aggregate,
            "sourceCount": len(inventory),
            "sourcePayloadBytes": payload_bytes,
            "archives": {key: archives[key] for key in sorted(archives)},
            "files": inventory,
            "privateReadBoundary": {
                "policy": "exact-manifest-sources-below-private-root",
            },
        },
        "policy": {
            "scope": "rotwk-war-of-the-ring-strategic-thirty-movie-apt-closure",
            "universalFlashRuntime": False,
            "executesActionScript": False,
            "substitutesAllowed": False,
            "genericArtAllowed": False,
            "retailPayloadInPlan": False,
            "stagedBy": "tools/wotr-data-staging.ps1 (env/schema lane)",
        },
        "movies": [{"movie": name, "role": role} for name, role in MOVIE_CLOSURE],
        "compositeOutputs": [
            {
                "id": "rotwk.ui.strategic.manifest",
                "path": MANIFEST_FILE_NAME,
                "schema": OUTPUT_SCHEMA,
                "semantics": "loader-facing bundle manifest with named gaps",
            },
            {
                "id": "rotwk.ui.strategic.screens",
                "path": f"{SCREEN_DIRECTORY}/",
                "semantics": (
                    "per-screen static draw contracts: frame 0, authored "
                    "labels, authored stop frames, exported sprites"
                ),
            },
            {
                "id": "rotwk.ui.strategic.atlases",
                "path": ATLAS_DIRECTORY,
                "semantics": "lossless hash-addressed PNG atlas directory",
            },
            {
                "id": "rotwk.ui.strategic.fonts",
                "path": FONT_DIRECTORY,
                "semantics": (
                    "byte-preserved retail font winners; only Albertus MT has "
                    "a proven APT-name binding"
                ),
            },
        ],
        "summary": {
            "game": game,
            "movieCount": len(MOVIE_CLOSURE),
            "sourceCount": len(inventory),
            "sourcePayloadBytes": payload_bytes,
            "archiveCount": len(archives),
        },
    }
    plan["aggregateSha256"] = canonical_sha256(plan)
    return plan


def write_retail_strategic_apt_plan(
    path: Path | str, plan: Mapping[str, Any]
) -> None:
    document = _mapping(plan, "strategic APT plan")
    declared = document.get("aggregateSha256")
    basis = dict(document)
    basis.pop("aggregateSha256", None)
    if (
        document.get("schema") != STRATEGIC_APT_PLAN_SCHEMA
        or declared != canonical_sha256(basis)
    ):
        raise ValueError("cannot write invalid strategic APT plan")
    write_json_atomic(Path(path), dict(document))


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effective-assets", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--catalog", required=True)
    parser.add_argument("--output", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    plan = build_retail_strategic_apt_plan(
        args.effective_assets,
        _load_json(args.manifest, "effective-assets manifest"),
        InstallCatalog.load(args.catalog),
    )
    write_retail_strategic_apt_plan(args.output, plan)
    print(
        json.dumps(
            {"aggregateSha256": plan["aggregateSha256"], **plan["summary"]},
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())


__all__ = [
    "EXPECTED_ARCHIVES",
    "EXPECTED_GAME",
    "EXPECTED_PAYLOAD_BYTES",
    "EXPECTED_SOURCE_AGGREGATE_SHA256",
    "EXPECTED_SOURCE_COUNT",
    "STRATEGIC_APT_PLAN_SCHEMA",
    "STRATEGIC_APT_PLAN_SCHEMA_VERSION",
    "build_retail_strategic_apt_plan",
    "write_retail_strategic_apt_plan",
]
