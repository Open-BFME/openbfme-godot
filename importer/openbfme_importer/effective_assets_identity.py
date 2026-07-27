"""Make an effective-assets tree say which install it is, and refuse silently wrong ones.

``extract-all-assets`` writes one sealed tree per workspace root, and
``workspace_root()`` gives BFME2 and RotWK different roots, so the trees
themselves are correct.  What the layout does *not* give a consumer is any way
to know which tree it just opened: every tree is a directory literally named
``effective-assets``, and the paths inside it are identical across editions
(``libraries/ai_spell_execution/ai_spell_execution.map`` exists in all of
them, with different bytes).  A lane that hard-codes one root and labels its
output with the other edition's name produces a confidently wrong measurement
and no error - the silent-fallback class this project keeps paying for.

The manifest already carries the evidence (``install.root``, the identity
hashes, and the winning archive of every file).  This module turns that
evidence into an answer a consumer can act on, and into a refusal when the
answer contradicts what the consumer asked for.

Nothing here regenerates or mutates a cache; it is read-only.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
from typing import Any

from .catalog import InstallCatalog, catalog_provenance_reason
from .game import RETAIL_GAME_IDS, retail_game

EFFECTIVE_ASSET_MANIFEST_SCHEMA = "openbfme.effective-assets-manifest"
EFFECTIVE_ASSET_METADATA_DIRECTORY = ".openbfme"
EFFECTIVE_ASSET_MANIFEST_RELATIVE = (
    f"{EFFECTIVE_ASSET_METADATA_DIRECTORY}/manifest.json"
)
MAX_MANIFEST_BYTES = 64 * 1024 * 1024

VERIFY_CHOICES = ("manifest", "sizes", "hashes")

# Least to most specific.  RotWK layers over BFME2, so a RotWK tree legitimately
# contains BFME2 patch archives and BFME2 evidence alone never identifies a tree.
# The most specific *evidenced* edition is the one whose bytes actually win, and
# that is the only edition this tree may be measured as.
EDITION_SPECIFICITY = ("bfme2", "rotwk")


class EffectiveAssetsIdentityError(ValueError):
    """Raised when an effective-assets tree cannot be trusted as asked for.

    Carries a structured ``diagnostic`` so a CLI or a lane can print the exact
    mismatch instead of a bare traceback.
    """

    def __init__(self, diagnostic: Mapping[str, Any]):
        self.diagnostic = dict(diagnostic)
        super().__init__(str(self.diagnostic.get("message", "effective-assets identity error")))


def _fail(**diagnostic: Any) -> None:
    raise EffectiveAssetsIdentityError(diagnostic)


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def _read_manifest(root: Path) -> tuple[Path, dict[str, Any], str]:
    manifest_path = root.joinpath(
        *PurePosixPath(EFFECTIVE_ASSET_MANIFEST_RELATIVE).parts
    )
    if not root.is_dir():
        _fail(
            error="effective-assets-root-missing",
            root=str(root),
            message=f"effective-assets root is missing or not a directory: {root}",
        )
    if not manifest_path.is_file():
        _fail(
            error="effective-assets-manifest-missing",
            root=str(root),
            manifest=str(manifest_path),
            message=(
                f"{root} carries no {EFFECTIVE_ASSET_MANIFEST_RELATIVE}, so nothing "
                "identifies which install its bytes came from. Refusing to guess."
            ),
        )
    if manifest_path.stat().st_size > MAX_MANIFEST_BYTES:
        _fail(
            error="effective-assets-manifest-oversized",
            manifest=str(manifest_path),
            message=f"effective-assets manifest exceeds the safety limit: {manifest_path}",
        )
    raw = manifest_path.read_bytes()
    try:
        manifest = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise EffectiveAssetsIdentityError(
            {
                "error": "effective-assets-manifest-unreadable",
                "manifest": str(manifest_path),
                "reason": f"{type(exc).__name__}: {exc}",
                "message": f"effective-assets manifest is unreadable: {manifest_path}",
            }
        ) from exc
    if not isinstance(manifest, dict):
        _fail(
            error="effective-assets-manifest-unreadable",
            manifest=str(manifest_path),
            message=f"effective-assets manifest root is not an object: {manifest_path}",
        )
    if manifest.get("schema") != EFFECTIVE_ASSET_MANIFEST_SCHEMA:
        _fail(
            error="effective-assets-manifest-schema",
            manifest=str(manifest_path),
            schema=manifest.get("schema"),
            message=(
                f"{manifest_path} is not an {EFFECTIVE_ASSET_MANIFEST_SCHEMA} document "
                f"(schema={manifest.get('schema')!r})"
            ),
        )
    files = manifest.get("files")
    install = manifest.get("install")
    catalog = manifest.get("catalog")
    if (
        not isinstance(files, list)
        or not isinstance(install, Mapping)
        or not isinstance(catalog, Mapping)
    ):
        _fail(
            error="effective-assets-manifest-shape",
            manifest=str(manifest_path),
            message=f"effective-assets manifest is missing required sections: {manifest_path}",
        )
    return manifest_path, manifest, hashlib.sha256(raw).hexdigest()


def _manifest_archives(manifest: Mapping[str, Any]) -> tuple[str, ...]:
    archives: set[str] = set()
    for item in manifest["files"]:
        if not isinstance(item, Mapping):
            _fail(
                error="effective-assets-manifest-shape",
                message="effective-assets manifest file entry is not an object",
            )
        archive = item.get("archive")
        if not isinstance(archive, str) or not archive:
            _fail(
                error="effective-assets-manifest-shape",
                message="effective-assets manifest file entry has no archive",
            )
        archives.add(archive.replace("\\", "/"))
    return tuple(sorted(archives, key=lambda value: (value.casefold(), value)))


def effective_assets_identity(root: Path | str) -> dict[str, Any]:
    """Describe which install a sealed effective-assets tree actually holds.

    Read-only.  ``games`` reports, per supported retail edition, whether this
    tree's archives *contradict* that edition (the same affirmative-contradiction
    rule the catalog provenance guard uses).  ``edition`` is set only when
    exactly one edition survives, so an ambiguous tree stays ``None`` rather
    than being labelled by guesswork.
    """

    resolved = Path(root).expanduser().resolve()
    manifest_path, manifest, manifest_sha256 = _read_manifest(resolved)
    archives = _manifest_archives(manifest)
    patch_archives = tuple(
        item for item in archives if "patch" in PurePosixPath(item).name.casefold()
    )

    games: dict[str, Any] = {}
    admitted: list[str] = []
    for game_id in RETAIL_GAME_IDS:
        reason = catalog_provenance_reason(archives, game_id)
        games[game_id] = {
            "contradicted": reason is not None,
            "reason": reason,
            "own_patch_archives": sorted(
                item
                for item in archives
                if PurePosixPath(item).name.casefold()
                in {name.casefold() for name in retail_game(game_id).patch_archives}
            ),
        }
        if reason is None:
            admitted.append(game_id)
    if set(EDITION_SPECIFICITY) != set(RETAIL_GAME_IDS):
        raise RuntimeError(
            "EDITION_SPECIFICITY does not cover every retail game; extend it "
            "deliberately rather than letting a new edition be unrankable"
        )
    evidenced = [
        game_id for game_id in admitted if games[game_id]["own_patch_archives"]
    ]
    # The winning edition is the most specific one evidenced, not merely a
    # non-contradicted one: a RotWK tree contains BFME2 archives but serves
    # RotWK bytes, and measuring it as BFME2 is exactly the wrong-tree bug.
    ranked = sorted(evidenced, key=EDITION_SPECIFICITY.index)

    install = manifest["install"]
    catalog = manifest["catalog"]
    totals = manifest.get("totals") or {}
    return {
        "root": str(resolved),
        "manifest": str(manifest_path),
        "manifest_sha256": manifest_sha256,
        "install_root": install.get("root"),
        "install_identity_sha256": install.get("identity_sha256"),
        "catalog_identity_sha256": catalog.get("identity_sha256"),
        "catalog_archive_count": catalog.get("archive_count"),
        "catalog_entry_count": catalog.get("entry_count"),
        "aggregate_sha256": manifest.get("aggregate_sha256"),
        "file_count": len(manifest["files"]),
        "total_bytes": totals.get("bytes"),
        "winning_archive_count": len(archives),
        "patch_archives": list(patch_archives),
        "games": games,
        "admitted_games": admitted,
        "evidenced_games": ranked,
        "edition": ranked[-1] if ranked else None,
    }


def _load_catalog(catalog: InstallCatalog | Path | str) -> tuple[InstallCatalog, str | None]:
    if isinstance(catalog, InstallCatalog):
        return catalog, None
    path = Path(catalog).expanduser().resolve()
    if not path.is_file():
        _fail(
            error="catalog-missing",
            catalog=str(path),
            message=f"catalog is missing: {path}",
        )
    try:
        return InstallCatalog.load(path), str(path)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise EffectiveAssetsIdentityError(
            {
                "error": "catalog-unreadable",
                "catalog": str(path),
                "reason": f"{type(exc).__name__}: {exc}",
                "message": f"catalog is unreadable or malformed: {path}",
            }
        ) from exc


def _verify_tree_bytes(
    root: Path, manifest: Mapping[str, Any], *, verify: str
) -> dict[str, Any]:
    checked = missing = size_mismatch = hash_mismatch = 0
    offenders: list[dict[str, str]] = []

    def note(path: str, kind: str) -> None:
        if len(offenders) < 20:
            offenders.append({"path": path, "problem": kind})

    for item in manifest["files"]:
        relative = str(item.get("path", "")).replace("\\", "/")
        target = root.joinpath(*PurePosixPath(relative).parts)
        checked += 1
        if not target.is_file():
            missing += 1
            note(relative, "missing")
            continue
        if target.stat().st_size != item.get("size"):
            size_mismatch += 1
            note(relative, "size")
            continue
        if verify == "hashes" and _sha256_file(target) != item.get("sha256"):
            hash_mismatch += 1
            note(relative, "sha256")

    expected = {str(item.get("path", "")).replace("\\", "/").casefold() for item in manifest["files"]}
    expected.add(EFFECTIVE_ASSET_MANIFEST_RELATIVE.casefold())
    unexpected: list[str] = []
    for dirpath, _dirs, names in os.walk(root):
        for name in names:
            relative = (
                Path(dirpath, name).relative_to(root).as_posix()
            )
            if relative.casefold() not in expected and len(unexpected) < 20:
                unexpected.append(relative)
    return {
        "mode": verify,
        "files_checked": checked,
        "missing": missing,
        "size_mismatch": size_mismatch,
        "hash_mismatch": hash_mismatch if verify == "hashes" else None,
        "unexpected_sample": unexpected,
        "offenders": offenders,
        "clean": not (missing or size_mismatch or hash_mismatch or unexpected),
    }


def verify_effective_assets(
    root: Path | str,
    *,
    game: str | None = None,
    catalog: InstallCatalog | Path | str | None = None,
    verify: str = "manifest",
    consumer: str | None = None,
) -> dict[str, Any]:
    """Assert a tree is the install the caller believes it is, or raise.

    ``game`` refuses a wrong-*tree* read: the tree's winning archives must not
    affirmatively contradict that edition, and if the edition is evidenced at
    all it must be the requested one.

    ``catalog`` refuses a *stale* read: the tree's recorded catalog identity
    must equal the live catalog's, so a cache extracted before the install
    changed can never masquerade as current.

    ``verify`` escalates from cheap to exhaustive: ``manifest`` trusts the
    sealed manifest, ``sizes`` stats every file, ``hashes`` re-digests every
    byte.
    """

    if verify not in VERIFY_CHOICES:
        raise ValueError(f"unsupported verify mode: {verify!r}")
    resolved = Path(root).expanduser().resolve()
    identity = effective_assets_identity(resolved)
    if consumer is not None:
        identity["consumer"] = consumer

    if game is not None:
        definition = retail_game(game)
        entry = identity["games"].get(definition.id, {})
        # One refusal, not two. An affirmative contradiction always coincides
        # with another edition winning the ranking, so a separate branch for it
        # would be unreachable-by-construction cover; its `reason` is folded in
        # here instead, where it still reaches the diagnostic.
        edition = identity["edition"]
        if edition is not None and edition != definition.id:
            _fail(
                error="effective-assets-game-mismatch",
                requested_game=definition.id,
                tree_edition=edition,
                root=identity["root"],
                install_root=identity["install_root"],
                evidenced_games=identity["evidenced_games"],
                patch_archives=identity["patch_archives"],
                reason=entry.get("reason"),
                message=(
                    f"{resolved} serves {retail_game(edition).display_name} bytes "
                    f"(evidenced {identity['evidenced_games']}), not {definition.id}; "
                    f"its install root is {identity['install_root']!r}. Measuring it "
                    f"and labelling the result {definition.id} would be a wrong-tree "
                    "measurement."
                ),
            )
        identity["requested_game"] = definition.id

    if catalog is not None:
        loaded, catalog_path = _load_catalog(catalog)
        identity["catalog"] = catalog_path
        live_identity = loaded.identity_sha256()
        if live_identity != identity["catalog_identity_sha256"]:
            _fail(
                error="effective-assets-stale",
                root=identity["root"],
                catalog=catalog_path,
                cache_catalog_identity_sha256=identity["catalog_identity_sha256"],
                live_catalog_identity_sha256=live_identity,
                cache_install_root=identity["install_root"],
                live_install_root=str(loaded.install_root),
                message=(
                    f"{resolved} was extracted from a different catalog than "
                    f"{catalog_path or 'the supplied catalog'}: cache identity "
                    f"{identity['catalog_identity_sha256']} vs live {live_identity}. "
                    "These bytes are stale or belong to another install; "
                    "re-run extract-all-assets --force before measuring them."
                ),
            )
        stale = loaded.stale_reasons()
        if stale:
            _fail(
                error="catalog-stale",
                root=identity["root"],
                catalog=catalog_path,
                reasons=list(stale),
                message=(
                    f"the catalog behind {resolved} is itself stale: "
                    + "; ".join(stale)
                ),
            )

    if verify != "manifest":
        _, manifest, _ = _read_manifest(resolved)
        result = _verify_tree_bytes(resolved, manifest, verify=verify)
        identity["verification"] = result
        if not result["clean"]:
            _fail(
                error="effective-assets-bytes-mismatch",
                root=identity["root"],
                verification=result,
                message=(
                    f"{resolved} does not match its own sealed manifest "
                    f"({result['missing']} missing, {result['size_mismatch']} wrong size, "
                    f"{result['hash_mismatch'] or 0} wrong hash, "
                    f"{len(result['unexpected_sample'])} unexpected). "
                    "Re-run extract-all-assets --force."
                ),
            )
    else:
        identity["verification"] = {"mode": "manifest"}

    identity["trusted"] = True
    return identity


def describe_effective_assets_trees(
    roots: Iterable[Path | str],
) -> list[dict[str, Any]]:
    """Identify several trees at once, recording failures rather than raising."""

    described: list[dict[str, Any]] = []
    for root in roots:
        try:
            described.append(effective_assets_identity(root))
        except EffectiveAssetsIdentityError as exc:
            described.append({"root": str(root), "error": exc.diagnostic})
    return described
