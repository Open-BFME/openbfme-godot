"""Fail-closed gates that stand between a cooked pack and a publication.

Why this is its own module rather than private helpers on ``cli``: three
commands publish content into the Godot content root - ``build``,
``import-unit`` and ``publish-faction-to-slice`` - and for a while only the
last of them was gated. A gate that one of three doors ignores is not a gate.
Keeping the rules here lets every door call the same code, and lets
``playable_unit_import`` reach them without importing ``cli``.

Three things are checked, each with its own override flag so an operator who
means it can still ship, loudly:

``coverage``
    The converted-coverage report must record a complete conversion. This is
    the gate that bundle ``8585890b`` needed: its report said
    ``conversionComplete: false`` with 8 converter gaps, and the publish path
    - which only ever consulted the *asset manifest* incomplete list - shipped
    20 of 24 playable units twice while printing ``conversion_failures: 0``.

``binding``
    The coverage report must describe *the content being cooked now*. A report
    can be perfectly clean and still be a lie about the current tree: the six
    faction packs re-cooked in round 11 all carried clean reports, but were
    compiled by a compiler that predated the ``prerequisiteAnyOf`` fix, so 20
    tier-2/tier-3 units shipped as permanently dead buttons. Nothing in a
    completeness check can see that. The binding check can: it pins the report
    to the catalog identity and the compiler identity token that produced it.

``regression``
    A cook must not ship a playable-unit roster that is *missing names* the
    already-published bundle of the same pack id has. Name level, not count
    level - a swap at constant count is exactly as bad as a drop, and the
    count check could not see it.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Iterable, Sequence

from .faction_object_cache import compiler_identity_token


# Exit code for a publish refused by a completeness gate. Distinct from 3
# (pack audit invalid) and 6 (a *conversion* command reporting its own
# incompleteness) so orchestration can tell "you asked me to ship a known-short
# build" apart from "the thing you built is broken".
PUBLISH_GATE_EXIT = 7


class PublishGateError(RuntimeError):
    """A publication refused by a gate, raised where returning 7 is not possible.

    ``import-unit`` publishes from inside ``playable_unit_import``, far below
    the ``main()`` frame that owns the exit code. It raises this; ``cli``
    catches it and returns :data:`PUBLISH_GATE_EXIT`.
    """

    def __init__(self, blockers: Sequence[str], override_flag: str) -> None:
        self.blockers = list(blockers)
        self.override_flag = override_flag
        super().__init__("; ".join(self.blockers))


# ---------------------------------------------------------------------------
# coverage completeness
# ---------------------------------------------------------------------------


def _load_coverage(path: Path) -> tuple[dict | None, str | None]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return (None, f"coverage report is missing or unreadable at {path} ({exc})")
    if not isinstance(document, dict):
        return (None, f"coverage report at {path} is not a JSON object")
    return (document, None)


def coverage_publish_blockers(
    coverage_root: Path | str, factions: Iterable[str]
) -> list[str]:
    """Reasons the converted faction coverage forbids a publication."""

    root = Path(coverage_root)
    blockers: list[str] = []
    for faction in factions:
        path = root / f"{faction}-coverage.json"
        document, error = _load_coverage(path)
        if error is not None:
            blockers.append(f"{faction}: {error}")
            continue
        assert document is not None
        summary = document.get("summary")
        if not isinstance(summary, dict):
            blockers.append(
                f"{faction}: coverage report at {path} has no summary block, so "
                "its completeness cannot be established"
            )
            continue
        gaps = summary.get("converterGapCount")
        complete = summary.get("conversionComplete")
        if not isinstance(gaps, int) or isinstance(gaps, bool):
            blockers.append(
                f"{faction}: coverage report at {path} has no integer "
                f"converterGapCount (found {gaps!r})"
            )
            continue
        if gaps > 0 or complete is not True:
            converted = summary.get("convertedCount", "?")
            objects = summary.get("objectCount", "?")
            blockers.append(
                f"{faction}: conversion is incomplete - converterGapCount={gaps}, "
                f"conversionComplete={complete!r}, converted {converted}/{objects} "
                f"objects (report: {path}). Re-run: openbfme-import "
                f"import-faction --faction {faction} --convert"
            )
    return blockers


# ---------------------------------------------------------------------------
# coverage binding: is this report about the tree we are cooking?
# ---------------------------------------------------------------------------


def coverage_binding_blockers(
    coverage_root: Path | str,
    factions: Iterable[str],
    *,
    catalog_identity: str,
) -> list[str]:
    """Reasons the coverage report does not describe the content being cooked.

    A clean report is not the same as a *current* report. Both identities
    checked here are recorded by ``convert_faction_import`` at the moment it
    writes the report:

    * ``inputs.catalogIdentitySha256`` - which install tree was read.
    * ``inputs.compilerIdentityToken`` - which compiler bytes produced the
      descriptors. This is the one that catches the round-11 defect, where six
      clean reports described output from a compiler two fixes behind.

    A report with *no* compiler token predates this gate and is therefore of
    unknown provenance. That is a refusal, not a pass.
    """

    root = Path(coverage_root)
    expected_token = compiler_identity_token()
    blockers: list[str] = []
    for faction in factions:
        path = root / f"{faction}-coverage.json"
        document, error = _load_coverage(path)
        if error is not None:
            # Completeness already reports unreadable reports; do not double up.
            continue
        assert document is not None
        inputs = document.get("inputs")
        if not isinstance(inputs, dict):
            blockers.append(
                f"{faction}: coverage report at {path} records no inputs block, "
                "so it cannot be bound to the content being cooked"
            )
            continue
        recorded_catalog = inputs.get("catalogIdentitySha256")
        if recorded_catalog != catalog_identity:
            blockers.append(
                f"{faction}: coverage report at {path} was generated against "
                f"catalog identity {recorded_catalog!r}, but this cook resolves "
                f"against {catalog_identity!r}. The report describes a different "
                "install tree."
            )
            continue
        recorded_token = inputs.get("compilerIdentityToken")
        if not isinstance(recorded_token, str) or not recorded_token:
            blockers.append(
                f"{faction}: coverage report at {path} records no "
                "compilerIdentityToken, so the compiler that produced its "
                "descriptors is unknown. Re-run: openbfme-import "
                f"import-faction --faction {faction} --convert"
            )
            continue
        if recorded_token != expected_token:
            blockers.append(
                f"{faction}: coverage report at {path} was produced by compiler "
                f"identity {recorded_token[:12]}..., but the compiler on disk is "
                f"{expected_token[:12]}.... Publishing it would ship descriptors "
                "from a compiler that is no longer the one in the tree - the "
                "exact failure that shipped 20 unbuildable units across six "
                "faction packs. Re-run: openbfme-import import-faction "
                f"--faction {faction} --convert"
            )
    return blockers


# ---------------------------------------------------------------------------
# playable-unit roster regression
# ---------------------------------------------------------------------------


def pack_playable_unit_ids(pack_root: Path | str) -> set[str]:
    """Document ids of the playable units a built or published pack carries.

    The id is read from the document when it declares one and falls back to the
    file stem, because that is what the runtime resolves by and what the
    incumbent comparison has to be stable against.
    """

    directory = Path(pack_root) / "data" / "playable-units"
    if not directory.is_dir():
        return set()
    ids: set[str] = set()
    for child in sorted(directory.iterdir()):
        if not child.is_file() or child.suffix.casefold() != ".json":
            continue
        identifier = child.stem
        try:
            document = json.loads(child.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            document = None
        if isinstance(document, dict):
            declared = document.get("id")
            if isinstance(declared, str) and declared:
                identifier = declared
        ids.add(identifier)
    return ids


def pack_playable_unit_count(pack_root: Path | str) -> int:
    """Number of playable-unit documents a built or published pack carries."""

    directory = Path(pack_root) / "data" / "playable-units"
    if not directory.is_dir():
        return 0
    return sum(
        1
        for child in directory.iterdir()
        if child.is_file() and child.suffix.casefold() == ".json"
    )


def _pack_catalog_identity(pack_root: Path | str) -> str | None:
    """Catalog identity the bundle claims to have been compiled against.

    Pack ids are product labels, not sufficient source identity.  Historical
    developer cooks accidentally published expansion-layer objects under the
    BFME2 Men id.  Treating those as incumbents makes the regression gate demand
    that a clean BFME2 cook retain objects its retail tree does not contain.
    Missing identities remain comparable for legacy packs; only a proven
    mismatch is excluded.
    """

    path = Path(pack_root) / "pack.json"
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if not isinstance(document, dict):
        return None
    identity = document.get("sourceCatalogIdentitySha256")
    return identity if isinstance(identity, str) and identity else None


def incumbent_playable_unit_ids(
    content_root: Path | str,
    pack_id: str,
    *,
    catalog_identity: str | None = None,
) -> tuple[set[str] | None, str | None]:
    """Richest already-published bundle for *pack_id*: (unit ids, digest).

    "Richest", not "newest": the guarantee we want is that a publication never
    silently regresses below the best build the operator already has on disk,
    whichever order they were cooked in.
    """

    root = Path(content_root) / pack_id
    if not root.is_dir():
        return (None, None)
    best_ids: set[str] | None = None
    best_bundle: str | None = None
    for child in sorted(root.iterdir()):
        # `.building` is publish_to_godot's staging name; a crashed copy must
        # never become the baseline other cooks are measured against.
        if not child.is_dir() or child.name.endswith(".building"):
            continue
        incumbent_identity = _pack_catalog_identity(child)
        if (
            catalog_identity is not None
            and incumbent_identity is not None
            and incumbent_identity != catalog_identity
        ):
            continue
        ids = pack_playable_unit_ids(child)
        if best_ids is None or len(ids) > len(best_ids):
            best_ids, best_bundle = ids, child.name
    return (best_ids, best_bundle)


def incumbent_playable_unit_count(
    content_root: Path | str, pack_id: str
) -> tuple[int | None, str | None]:
    """Count-level view of :func:`incumbent_playable_unit_ids`."""

    ids, bundle = incumbent_playable_unit_ids(content_root, pack_id)
    return (None if ids is None else len(ids), bundle)


def playable_unit_regression_blocker(
    pack_root: Path | str, content_root: Path | str, pack_id: str
) -> str | None:
    """Refuse to drop playable-unit *names* the incumbent bundle already ships.

    Name level on purpose. The count-level predecessor of this function passed
    a cook that replaced one unit with another, which is a regression the
    operator would only discover in game.
    """

    fresh_identity = _pack_catalog_identity(pack_root)
    incumbent, bundle = incumbent_playable_unit_ids(
        content_root, pack_id, catalog_identity=fresh_identity
    )
    if incumbent is None:
        return None
    fresh = pack_playable_unit_ids(pack_root)
    missing = sorted(incumbent - fresh)
    if not missing:
        return None
    shown = ", ".join(missing[:8])
    if len(missing) > 8:
        shown += f", ... (+{len(missing) - 8} more)"
    added = sorted(fresh - incumbent)
    added_note = ""
    if added:
        added_note = (
            f" It does add {len(added)} id(s) the incumbent lacks "
            f"({', '.join(added[:5])}{'...' if len(added) > 5 else ''}), so this "
            "is a swap, not only a drop."
        )
    return (
        f"{pack_id}: this cook carries {len(fresh)} playable units and the "
        f"already-published bundle {bundle} carries {len(incumbent)}, but "
        f"{len(missing)} id(s) present in the incumbent are missing here: "
        f"{shown}.{added_note} Publishing would silently regress the slice. "
        "Fix the conversion, or acknowledge the reduction with "
        "--allow-fewer-playable-units."
    )


def enforce_playable_unit_gate(
    pack_root: Path | str,
    content_root: Path | str,
    pack_id: str,
    *,
    allow_fewer: bool,
) -> str | None:
    """Raise :class:`PublishGateError` unless the roster gate is satisfied.

    Returns the blocker text when an override let it through, so callers can
    record it, and ``None`` when there was nothing to report.
    """

    blocker = playable_unit_regression_blocker(pack_root, content_root, pack_id)
    if blocker is None:
        return None
    if not allow_fewer:
        raise PublishGateError([blocker], "--allow-fewer-playable-units")
    return blocker


__all__ = [
    "PUBLISH_GATE_EXIT",
    "PublishGateError",
    "compiler_identity_token",
    "coverage_binding_blockers",
    "coverage_publish_blockers",
    "enforce_playable_unit_gate",
    "incumbent_playable_unit_count",
    "incumbent_playable_unit_ids",
    "pack_playable_unit_count",
    "pack_playable_unit_ids",
    "playable_unit_regression_blocker",
]
