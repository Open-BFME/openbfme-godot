"""Interface-art reference judge: every retail UI image reference must ship.

Retail BFME2/RotWK authors its icons and portraits as *crops* of a small set of
texture atlases.  A ``CommandButton``'s ``ButtonImage`` and an ``Object``'s
``SelectPortrait`` name a ``MappedImage`` id; the ``MappedImage`` block names the
atlas texture plus the exact pixel rectangle to cut out of it.

This module walks that chain end to end against the retail oracle
(``workspace/retail-work/editions/rotwk/cache/effective-assets`` — rulebook S1)
and asserts that every referenced id has a shipped PNG in the mounted content
packs.  It is the judge for parity-graph node ``ui.interface-art``.

Run it standalone::

    python -m openbfme_importer.interface_art --help

Every gap is reported *by name* with a reason.  Nothing is sampled, capped or
silently skipped (rulebook P7), and the pack directories actually consulted are
printed so a stale-mount can never masquerade as a pass (rulebook P8).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path

from .mapped_image import MappedImageRecord, _parse_mapped_images
from .sage_ini import parse_flat_named_blocks, parse_object_definitions


class InterfaceArtError(ValueError):
    """The interface-art census cannot be completed as requested."""


#: Rulebook S1: this is the only retail oracle.
DEFAULT_ORACLE_ROOT = Path("workspace/retail-work/editions/rotwk/cache/effective-assets")
DEFAULT_PACKS_ROOT = Path("workspace/content-packs")
DEFAULT_SELECTION = DEFAULT_PACKS_ROOT / "selection.json"

#: Documented pack path scheme emitted by the interface-art lane.
PACK_INDEX_RELATIVE = "data/interface-art/index.json"
PACK_INDEX_SCHEMA = "openbfme.interface-art-index"
PACK_IMAGE_ROOT = "assets/ui/interface-art"

#: Retail authors these object fields as MappedImage ids.
OBJECT_IMAGE_FIELDS: tuple[str, ...] = (
    "SelectPortrait",
    "UnitPortrait",
    "ButtonImage",
    "UpgradeCameo1",
    "UpgradeCameo2",
    "UpgradeCameo3",
    "UpgradeCameo4",
    "UpgradeCameo5",
)
#: Retail authors these CommandButton fields as MappedImage ids.
COMMAND_BUTTON_IMAGE_FIELDS: tuple[str, ...] = ("ButtonImage",)

MAPPED_IMAGE_DIR = "data/ini/mappedimages"
COMPILED_TEXTURE_DIR = "art/compiledtextures"

_NULL_IMAGE_VALUES = frozenset({"", "none", "nothing", "noimage"})
_HASH_SUFFIX = re.compile(r"-([0-9a-f]{8})\.png$")
_MAX_INI_BYTES = 16 * 1024 * 1024


# --------------------------------------------------------------------------- #
# naming
# --------------------------------------------------------------------------- #


def _slug(value: str) -> str:
    result = re.sub(r"[^a-z0-9]+", "-", value.casefold()).strip("-")
    if not result:
        raise InterfaceArtError(f"image id has no safe slug: {value!r}")
    return result


def image_key(image_id: str) -> str:
    """Case-insensitive lookup key for a MappedImage id."""

    return image_id.casefold()


def image_digest(image_id: str) -> str:
    """The 8-hex-digit id fingerprint used in shipped PNG filenames."""

    return hashlib.sha256(image_key(image_id).encode()).hexdigest()[:8]


def pack_image_filename(image_id: str) -> str:
    """Shipped PNG basename for an image id.

    Matches the convention already published by the unit/structure/spellbook
    pack compilers, e.g. ``BGArcheryRange`` -> ``bgarcheryrange-e6a50364.png``.
    """

    return f"{_slug(image_id)}-{image_digest(image_id)}.png"


def atlas_digest(virtual_path: str) -> str:
    """Stable 12-hex directory fingerprint for one source atlas."""

    return hashlib.sha256(virtual_path.casefold().encode()).hexdigest()[:12]


# --------------------------------------------------------------------------- #
# reference collection
# --------------------------------------------------------------------------- #


@dataclass(frozen=True, slots=True)
class ImageReference:
    """One authored reference from a retail definition to a MappedImage id."""

    kind: str
    owner: str
    field: str
    image_id: str
    source: str


# MappedImages consumed directly by the SAGE engine rather than named from an
# Object or CommandButton field. They still have authored atlas coordinates and
# belong in the same deterministic crop/index lane when retail defines them.
_HANDCREATED = "data/ini/mappedimages/handcreated/handcreatedmappedimages.ini"

#: The palantir resource bar's currency icon.  ``Palantir.apt`` authors a child
#: named ``ResourceIcon`` (character 127, local [-4, 2] inside the ``ResourceBar``
#: sprite at stage [42.45, 719.4]) whose ~Enabled shape is an untextured 18 x 20
#: quad: the engine binds the art.  The art is ``ResourceBarIcons.tga``, cut by
#: ``Resource_Icon`` and one ``ResourceBar_<faction>`` crop per side.  No Object
#: and no CommandButton names any of them, so the object-driven scope dropped
#: the whole family and the runtime had nothing to draw.
ENGINE_UI_IMAGE_REFERENCES: tuple[ImageReference, ...] = (
    ImageReference("EngineUI", "Radar", "ViewBoxEdge", "RadarViewBoxEdge", _HANDCREATED),
    ImageReference("EngineUI", "ResourceBar", "ResourceIcon", "Resource_Icon", _HANDCREATED),
    ImageReference("EngineUI", "ResourceBar", "ResourceIcon", "ResourceBar_Gondor", _HANDCREATED),
    ImageReference("EngineUI", "ResourceBar", "ResourceIcon", "ResourceBar_Rohan", _HANDCREATED),
    ImageReference("EngineUI", "ResourceBar", "ResourceIcon", "ResourceBar_Mordor", _HANDCREATED),
    ImageReference("EngineUI", "ResourceBar", "ResourceIcon", "ResourceBar_Isengard", _HANDCREATED),
    ImageReference("EngineUI", "ResourceBar", "ResourceIcon", "ResourceBar_Fellowship", _HANDCREATED),
)


def _read(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) > _MAX_INI_BYTES:
        raise InterfaceArtError(f"INI source exceeds the byte limit: {path}")
    return data


def _ini_files(root: Path) -> tuple[Path, ...]:
    if not root.is_dir():
        return ()
    return tuple(
        sorted(
            (path for path in root.rglob("*") if path.is_file() and path.suffix.casefold() in {".ini", ".inc"}),
            key=lambda path: path.as_posix().casefold(),
        )
    )


def _relative(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def _image_values(
    block_values: Iterable[str], known: Mapping[str, object] | None = None
) -> tuple[str, ...]:
    """Normalize one image field into the ids it authors.

    Retail puts one image *per toggle state* on a single line, e.g.
    ``data/ini/commandbutton.ini`` ``CommandButton Command_ToggleStance`` has
    ``ButtonImage = UCCommon_HoldGroundStance UCCommon_BattleStance
    UCCommon_AggresiveStance``.  A handful of MappedImage ids genuinely contain
    a space, though (``MappedImage Ruler-Right End`` in
    ``data/ini/mappedimages/handcreated/handcreatedmappedimages.ini``), so a
    whole-line match against the known ids always wins over splitting.
    """

    result: list[str] = []
    for raw in block_values:
        value = raw.strip().strip('"').strip()
        if value.casefold() in _NULL_IMAGE_VALUES:
            continue
        if known is not None and image_key(value) not in known and " " in value:
            tokens = [token for token in value.split() if token]
            if tokens and all(image_key(token) in known for token in tokens):
                result.extend(tokens)
                continue
        result.append(value)
    return tuple(result)


def _command_button_files(oracle_root: Path) -> tuple[Path, ...]:
    candidates = (
        oracle_root / "data/ini/commandbutton.ini",
        oracle_root / "data/ini/default/commandbutton.ini",
        oracle_root / "data/ini/createaherospecialpowers.ini",
    )
    return tuple(path for path in candidates if path.is_file())


def _command_set_files(oracle_root: Path) -> tuple[Path, ...]:
    candidates = (
        oracle_root / "data/ini/commandset.ini",
        oracle_root / "data/ini/default/commandset.ini",
    )
    return tuple(path for path in candidates if path.is_file())


def collect_command_buttons(
    oracle_root: Path, known_image_ids: Mapping[str, object] | None = None
) -> tuple[dict[str, tuple[ImageReference, ...]], tuple[str, ...]]:
    """Map every CommandButton id to the image references it authors."""

    buttons: dict[str, tuple[ImageReference, ...]] = {}
    duplicates: list[str] = []
    for path in _command_button_files(oracle_root):
        source = _relative(oracle_root, path)
        for block in parse_flat_named_blocks(_read(path), "CommandButton"):
            references = tuple(
                ImageReference("CommandButton", block.name, name, value, source)
                for name in COMMAND_BUTTON_IMAGE_FIELDS
                for value in _image_values(block.values(name), known_image_ids)
            )
            key = block.name.casefold()
            if key in buttons and buttons[key] != references:
                duplicates.append(block.name)
            buttons[key] = references
    return buttons, tuple(sorted(set(duplicates), key=str.casefold))


def collect_command_sets(oracle_root: Path) -> dict[str, tuple[str, ...]]:
    """Map every CommandSet id to the ordered CommandButton ids it lists."""

    sets: dict[str, tuple[str, ...]] = {}
    for path in _command_set_files(oracle_root):
        for block in parse_flat_named_blocks(_read(path), "CommandSet"):
            entries = tuple(
                value.strip()
                for _, value in block.assignments
                if value.strip() and value.strip().casefold() != "none"
            )
            sets[block.name.casefold()] = entries
    return sets


def _object_ini_roots(oracle_root: Path) -> tuple[Path, ...]:
    return tuple(
        path
        for path in (
            oracle_root / "data/ini/object",
            oracle_root / "data/ini/createaherosystem.ini",
        )
        if path.exists()
    )


#: The Create-a-Hero system fields that name a MappedImage: the seven class
#: archetype icons on ``CreateAHeroClass``, and the portrait + roster button on
#: each ``SubClass``.
CREATE_A_HERO_IMAGE_FIELDS: tuple[str, ...] = ("IconImage", "ButtonImage")
#: Where those blocks live.  ``createaherosystem.ini`` is a shell of ``#include``
#: directives; the classes themselves are in the sibling ``.inc`` files.
CREATE_A_HERO_SYSTEM_GLOB = "createaherosystem*.in[ci]"
#: The per-mesh portrait/roster button each ``ModelConditionState`` of the
#: ``CreateAHero`` Object names.  The compiled CaH system table publishes these
#: as ``portraitImageId`` / ``buttonImageId``, so a pack that ships the table
#: without them leaves the hero roster and the creation screen without faces.
#: They live under ``data/ini/object/`` but the object sweep never sees them:
#: the file is an ``.inc`` body fragment (not a standalone block document) and
#: the two field spellings are CaH-only, in neither ``OBJECT_IMAGE_FIELDS`` nor
#: ``COMMAND_BUTTON_IMAGE_FIELDS``.
CREATE_A_HERO_MODEL_IMAGE_FIELDS: tuple[str, ...] = (
    "PortraitImageName",
    "ButtonImageName",
)
CREATE_A_HERO_MODEL_FILES: tuple[str, ...] = (
    "data/ini/object/createahero/createaheromodels.inc",
)
CREATE_A_HERO_AWARD_FILE = "data/ini/awardsystem.ini"
CREATE_A_HERO_MEDAL_IMAGE_FILE = (
    "data/ini/mappedimages/aptimages/createaheroimages.ini"
)


def collect_create_a_hero_images(
    oracle_root: Path, known_image_ids: Mapping[str, object] | None = None
) -> tuple[ImageReference, ...]:
    """Every MappedImage the Create-a-Hero class system names.

    WHY THIS NEEDS ITS OWN COLLECTOR.  These icons are referenced from nowhere
    else in the corpus: they hang off ``CreateAHeroClass`` and ``SubClass``
    blocks in ``data/ini/createaherosystem*.inc``, which is neither an Object
    document (so the object sweep never reads it) nor a CommandButton one.  The
    result was that all seven class archetypes and all sixteen subclass
    portraits resolved to MappedImages that existed in the oracle and were never
    shipped, leaving the Create-a-Hero screen with no icons at all.

    Read as a line scan rather than a block parse: the surface is two flat
    assignment spellings inside nested blocks whose grammar nothing else here
    models.  ``known_image_ids`` disambiguates the multi-image line form exactly
    as it does for CommandButtons; it is NOT a filter, so an id retail names but
    never defines still becomes a reference and is reported downstream as a
    ``no-mapped-image`` gap rather than vanishing (``CPUrukHai`` is the real
    instance).
    """

    oracle_root = Path(oracle_root)
    ini_root = oracle_root / "data/ini"
    if not ini_root.is_dir():
        return ()
    scans: list[tuple[Path, frozenset[str]]] = [
        (path, frozenset(name.casefold() for name in CREATE_A_HERO_IMAGE_FIELDS))
        for path in sorted(ini_root.glob(CREATE_A_HERO_SYSTEM_GLOB))
    ]
    model_fields = frozenset(
        name.casefold() for name in CREATE_A_HERO_MODEL_IMAGE_FIELDS
    )
    for relative in CREATE_A_HERO_MODEL_FILES:
        candidate = oracle_root / relative
        if candidate.is_file():
            scans.append((candidate, model_fields))
    award_file = oracle_root / CREATE_A_HERO_AWARD_FILE
    if award_file.is_file():
        scans.append((award_file, frozenset({"imagename"})))

    references: list[ImageReference] = []
    for path, fields in scans:
        source = _relative(oracle_root, path)
        for line in _read(path).decode("cp1252", errors="replace").splitlines():
            stripped = line.split(";", 1)[0].split("//", 1)[0].strip()
            if "=" not in stripped:
                continue
            field, _, value = stripped.partition("=")
            field = field.strip()
            if field.casefold() not in fields:
                continue
            for image_id in _image_values((value,), known_image_ids):
                references.append(
                    ImageReference(
                        "CreateAHeroSystem", path.stem, field, image_id, source
                    )
                )
    # The dedicated atlas declares 28 medals, including four currently
    # commented-out/unused award ids. Ship the complete retail medal set so a
    # later award enablement cannot produce a text-only row.
    medal_file = oracle_root / CREATE_A_HERO_MEDAL_IMAGE_FILE
    seen = {row.image_id.casefold() for row in references}
    if medal_file.is_file():
        source = _relative(oracle_root, medal_file)
        for line in _read(medal_file).decode("cp1252", errors="replace").splitlines():
            match = re.match(r"^\s*MappedImage\s+(.+?)\s*$", line, re.IGNORECASE)
            if match is None:
                continue
            image_id = match.group(1).strip()
            if not image_id.casefold().startswith("cahaward_") or image_id.casefold() in seen:
                continue
            seen.add(image_id.casefold())
            references.append(
                ImageReference("CreateAHeroAwards", "AwardMedals", "MappedImage", image_id, source)
            )
    return tuple(references)


def collect_image_references(
    oracle_root: Path,
    *,
    object_path_tokens: Sequence[str] | None = None,
    known_image_ids: Mapping[str, object] | None = None,
) -> tuple[ImageReference, ...]:
    """Collect every authored UI image reference in the retail oracle.

    ``object_path_tokens`` scopes the census to objects whose defining INI path
    contains one of the tokens (e.g. ``("structures/men",)``).  Scoped runs also
    follow each in-scope object's ``CommandSet`` so the buttons that object can
    actually show are included.  Unscoped runs cover every CommandButton in the
    game, including buttons no CommandSet references.
    """

    oracle_root = Path(oracle_root)
    if known_image_ids is None:
        known_image_ids = load_mapped_images(oracle_root)[0]
    buttons, duplicate_buttons = collect_command_buttons(oracle_root, known_image_ids)
    if duplicate_buttons:
        raise InterfaceArtError(
            "CommandButton ids define conflicting images: "
            + ", ".join(duplicate_buttons[:8])
        )
    command_sets = collect_command_sets(oracle_root)

    tokens = tuple(token.casefold() for token in (object_path_tokens or ()))
    references: list[ImageReference] = []
    in_scope_buttons: set[str] = set()

    object_root = oracle_root / "data/ini/object"
    for path in _ini_files(object_root):
        source = _relative(oracle_root, path)
        if tokens and not any(token in source.casefold() for token in tokens):
            continue
        try:
            blocks = parse_object_definitions(_read(path))
        except ValueError:
            # A retail .inc fragment is not a standalone block document; its
            # contents are seen through the .ini that includes it.
            continue
        for block in blocks:
            for name in OBJECT_IMAGE_FIELDS:
                for value in _image_values(block.values(name), known_image_ids):
                    references.append(
                        ImageReference("Object", block.name, name, value, source)
                    )
            for value in block.values("CommandSet"):
                for button in command_sets.get(value.strip().casefold(), ()):
                    in_scope_buttons.add(button.casefold())

    if tokens:
        selected = sorted(in_scope_buttons)
    else:
        selected = sorted(buttons)
    for key in selected:
        references.extend(buttons.get(key, ()))

    # The Create-a-Hero class icons, which no Object and no CommandButton names.
    # Included in EVERY scope rather than only the host faction's: the class
    # system is cross-faction (a Captain of Gondor is usable by Men, Elves and
    # Dwarves) and a scoped pack that dropped them would leave the screen blank
    # for exactly the factions that can field the hero.
    references.extend(collect_create_a_hero_images(oracle_root, known_image_ids))

    # SAGE draws the camera/view box directly; no Object field names this
    # MappedImage. Include it whenever the retail corpus defines it so the crop
    # cannot remain absent merely because the engine-owned reference is implicit.
    references.extend(
        reference
        for reference in ENGINE_UI_IMAGE_REFERENCES
        if image_key(reference.image_id) in known_image_ids
    )

    references.sort(
        key=lambda reference: (
            reference.kind,
            reference.owner.casefold(),
            reference.field,
            reference.image_id.casefold(),
        )
    )
    return tuple(references)


# --------------------------------------------------------------------------- #
# mapped-image + atlas resolution
# --------------------------------------------------------------------------- #


def load_mapped_images(
    oracle_root: Path,
) -> tuple[dict[str, MappedImageRecord], tuple[str, ...]]:
    """Index every MappedImage block in the oracle; report ambiguous ids."""

    oracle_root = Path(oracle_root)
    index: dict[str, MappedImageRecord] = {}
    ambiguous: set[str] = set()
    for path in _ini_files(oracle_root / MAPPED_IMAGE_DIR):
        for record in _parse_mapped_images(_read(path), reject_duplicate_ids=False):
            key = image_key(record.id)
            existing = index.get(key)
            if existing is not None and existing != record:
                # THE TIE-BREAK, DECIDED BY WHAT EA ACTUALLY SHIPPED.
                #
                # Retail defines a handful of ids twice, and the duplicates are
                # not equivalent: the Create-a-Hero subclass portraits
                # (CPShieldMaiden, CPDwarfSage, CPDwarfTaskmaster, CPHermit,
                # CPOrcRaider) appear once in mappedimages/aptimages/
                # cahportraits.ini pointing at a dedicated `<id>.tga`, and once
                # in heroui.ini as a crop of the HeroUI_011 atlas.  NONE of the
                # dedicated textures exist in the shipped game; the atlas does.
                # So the cahportraits.ini rows are dead references to art EA cut,
                # and preferring them -- which the file's name invites -- would
                # blank five of the sixteen portraits.
                #
                # Resolvability decides it: a record whose texture is a real
                # compiled atlas beats one whose texture is absent.  When both
                # or neither resolve there is no evidence to choose on, so the
                # id stays ambiguous and is reported rather than guessed.
                existing_ok = compiled_atlas_path(oracle_root, existing.texture) is not None
                candidate_ok = compiled_atlas_path(oracle_root, record.texture) is not None
                if existing_ok and not candidate_ok:
                    continue
                if candidate_ok and not existing_ok:
                    index[key] = record
                    continue
                ambiguous.add(record.id)
            index[key] = record
    return index, tuple(sorted(ambiguous, key=str.casefold))


def compiled_atlas_path(oracle_root: Path, texture: str) -> Path | None:
    """Locate the compiled atlas leaf for a MappedImage ``Texture`` value.

    Mirrors ``mapped_image.resolve_mapped_image_texture_paths_partial``: an
    exact authored-extension leaf wins, otherwise the exact ``.dds`` convention.
    No basename search and no extension guessing.
    """

    basename = texture.replace("\\", "/").rsplit("/", 1)[-1]
    stem = basename.rsplit(".", 1)[0]
    if len(stem) < 2:
        raise InterfaceArtError(f"MappedImage texture stem is too short: {texture!r}")
    directory = Path(oracle_root) / COMPILED_TEXTURE_DIR / stem[:2].casefold()
    if not directory.is_dir():
        return None
    lowered = {entry.name.casefold(): entry for entry in directory.iterdir() if entry.is_file()}
    for candidate in (basename.casefold(), f"{stem.casefold()}.dds"):
        found = lowered.get(candidate)
        if found is not None:
            return found
    return None


# --------------------------------------------------------------------------- #
# pack scanning
# --------------------------------------------------------------------------- #


def resolve_selection_pack_dirs(
    selection_path: Path, packs_root: Path
) -> tuple[Path, ...]:
    """Resolve the mounted pack directories named by ``selection.json``.

    Rulebook P8: the caller prints these, so a stale mount is always visible.
    """

    selection_path = Path(selection_path)
    packs_root = Path(packs_root)
    payload = json.loads(selection_path.read_text(encoding="utf-8"))
    if not isinstance(payload, Mapping):
        raise InterfaceArtError(f"selection document is invalid: {selection_path}")
    names: list[str] = []
    active = payload.get("activePack")
    if isinstance(active, str) and active:
        names.append(active)
    supplemental = payload.get("supplementalPacks")
    if isinstance(supplemental, list):
        names.extend(name for name in supplemental if isinstance(name, str) and name)
    directories: list[Path] = []
    missing: list[str] = []
    for name in names:
        candidate = packs_root.joinpath(*name.split("/"))
        if candidate.is_dir():
            directories.append(candidate)
        else:
            missing.append(name)
    if missing:
        raise InterfaceArtError(
            "selection.json names pack directories that do not exist: "
            + ", ".join(missing)
        )
    return tuple(directories)


def scan_shipped_images(pack_dirs: Iterable[Path]) -> dict[str, str]:
    """Map image key -> pack-relative PNG path across the given packs.

    Two mechanisms, in this order:

    1. ``data/interface-art/index.json`` — the explicit id->path map emitted by
       the interface-art lane.  Authoritative and exact.
    2. the ``<slug>-<hash8>.png`` filename convention already used by the
       unit/structure/spellbook pack compilers, so packs cooked before the lane
       existed still resolve without being re-cooked.
    """

    shipped: dict[str, str] = {}
    for pack in pack_dirs:
        pack = Path(pack)
        for png in sorted(pack.rglob("*.png"), key=lambda path: path.as_posix().casefold()):
            match = _HASH_SUFFIX.search(png.name.casefold())
            if match is None:
                continue
            shipped.setdefault(
                f"#{match.group(1)}", png.relative_to(pack).as_posix()
            )
        manifest = pack / PACK_INDEX_RELATIVE
        if not manifest.is_file():
            continue
        payload = json.loads(manifest.read_text(encoding="utf-8"))
        if not isinstance(payload, Mapping) or payload.get("schema") != PACK_INDEX_SCHEMA:
            raise InterfaceArtError(f"interface-art index is invalid: {manifest}")
        images = payload.get("images")
        if not isinstance(images, Mapping):
            raise InterfaceArtError(f"interface-art index has no images: {manifest}")
        for image_id, relative in images.items():
            if not isinstance(image_id, str) or not isinstance(relative, str):
                raise InterfaceArtError(f"interface-art index row is invalid: {manifest}")
            key = image_key(image_id)
            if (pack / relative).is_file():
                shipped[key] = relative
            else:
                shipped.setdefault(f"!{key}", relative)
    return shipped


def lookup_shipped_image(shipped: Mapping[str, str], image_id: str) -> tuple[str, str]:
    """Return ``(path, reason)``; ``reason`` is empty when the image ships."""

    key = image_key(image_id)
    if key in shipped:
        return shipped[key], ""
    if f"!{key}" in shipped:
        return "", "index-entry-missing-file"
    fingerprint = f"#{image_digest(image_id)}"
    if fingerprint in shipped:
        return shipped[fingerprint], ""
    return "", "not-shipped"


# --------------------------------------------------------------------------- #
# the judge
# --------------------------------------------------------------------------- #


@dataclass(frozen=True)
class InterfaceArtReport:
    """Named, denominator-carrying result of one interface-art census."""

    oracle_root: str
    pack_dirs: tuple[str, ...]
    scope: str
    reference_count: int
    referenced_id_count: int
    shipped_ids: tuple[str, ...]
    unresolved_ids: tuple[str, ...]
    waived_ids: tuple[str, ...]
    reasons: Mapping[str, str]
    owners: Mapping[str, tuple[str, ...]]
    ambiguous_mapped_images: tuple[str, ...] = ()

    @property
    def unresolved_count(self) -> int:
        return len(self.unresolved_ids)

    @property
    def shipped_count(self) -> int:
        return len(self.shipped_ids)

    @property
    def exit_code(self) -> int:
        return 0 if not self.unresolved_ids else 1

    def reason_counts(self) -> dict[str, int]:
        counts: dict[str, int] = {}
        for image_id in self.unresolved_ids:
            reason = self.reasons.get(image_id, "unknown")
            counts[reason] = counts.get(reason, 0) + 1
        return dict(sorted(counts.items()))

    def summary_line(self) -> str:
        return (
            f"unresolved={self.unresolved_count} "
            f"referenced={self.referenced_id_count} "
            f"shipped={self.shipped_count} "
            f"waived={len(self.waived_ids)} "
            f"scope={self.scope}"
        )

    def to_json(self) -> dict[str, object]:
        return {
            "oracleRoot": self.oracle_root,
            "packDirs": list(self.pack_dirs),
            "scope": self.scope,
            "referenceCount": self.reference_count,
            "referencedIdCount": self.referenced_id_count,
            "shippedCount": self.shipped_count,
            "unresolvedCount": self.unresolved_count,
            "reasonCounts": self.reason_counts(),
            "unresolved": [
                {
                    "id": image_id,
                    "reason": self.reasons.get(image_id, "unknown"),
                    "owners": list(self.owners.get(image_key(image_id), ())),
                }
                for image_id in self.unresolved_ids
            ],
            "waived": list(self.waived_ids),
            "ambiguousMappedImages": list(self.ambiguous_mapped_images),
        }


#: Named faction scopes.  Each value is a set of path tokens matched against the
#: retail object INI path, so the scope is proven by the retail tree layout
#: rather than by a hand-maintained object list.
FACTION_OBJECT_PATH_TOKENS: Mapping[str, tuple[str, ...]] = {
    "men": (
        "goodfaction/units/gondor",
        "goodfaction/units/men",
        "goodfaction/units/rohan",
        "goodfaction/structures/men",
        "goodfaction/hordes/men",
        "goodfaction/hordes/rohan",
    ),
    "elves": (
        "goodfaction/units/elven",
        "goodfaction/structures/elven",
        "goodfaction/hordes/elven",
    ),
    "dwarves": (
        "goodfaction/units/dwarven",
        "goodfaction/structures/dwarven",
        "goodfaction/hordes/dwarven",
    ),
    "isengard": (
        "evilfaction/units/isengard",
        "evilfaction/structures/isengard",
        "evilfaction/hordes/isengard",
    ),
    "mordor": (
        "evilfaction/units/mordor",
        "evilfaction/structures/mordor",
        "evilfaction/hordes/mordor",
    ),
    "wild": (
        "evilfaction/units/wild",
        "evilfaction/structures/wild",
        "evilfaction/hordes/wild",
    ),
    "angmar": (
        "evilfaction/units/angmar",
        "evilfaction/structures/angmar",
        "evilfaction/hordes/angmar",
    ),
}


def scope_tokens(scope: str) -> tuple[str, ...] | None:
    key = scope.casefold().strip()
    if key in {"", "all"}:
        return None
    tokens = FACTION_OBJECT_PATH_TOKENS.get(key)
    if tokens is None:
        raise InterfaceArtError(
            f"unknown scope {scope!r}; known: all, "
            + ", ".join(sorted(FACTION_OBJECT_PATH_TOKENS))
        )
    return tokens


def judge_interface_art(
    *,
    oracle_root: Path = DEFAULT_ORACLE_ROOT,
    selection_path: Path | None = DEFAULT_SELECTION,
    packs_root: Path = DEFAULT_PACKS_ROOT,
    extra_pack_dirs: Sequence[Path] = (),
    scope: str = "all",
    source_null_allowlist: Sequence[str] = (),
) -> InterfaceArtReport:
    """Assert every referenced retail UI image resolves to a shipped PNG."""

    oracle_root = Path(oracle_root)
    if not oracle_root.is_dir():
        raise InterfaceArtError(f"retail oracle root does not exist: {oracle_root}")

    tokens = scope_tokens(scope)
    mapped, ambiguous = load_mapped_images(oracle_root)
    references = collect_image_references(
        oracle_root, object_path_tokens=tokens, known_image_ids=mapped
    )
    if not references:
        raise InterfaceArtError(
            f"no UI image references found under {oracle_root} (scope={scope})"
        )

    pack_dirs: list[Path] = []
    if selection_path is not None:
        pack_dirs.extend(resolve_selection_pack_dirs(selection_path, packs_root))
    pack_dirs.extend(Path(directory) for directory in extra_pack_dirs)
    shipped_index = scan_shipped_images(pack_dirs)

    owners: dict[str, list[str]] = {}
    for reference in references:
        owners.setdefault(image_key(reference.image_id), []).append(
            f"{reference.kind}:{reference.owner}.{reference.field}"
        )

    canonical: dict[str, str] = {}
    for reference in references:
        canonical.setdefault(image_key(reference.image_id), reference.image_id)

    # Ambiguity only matters for ids this scope actually references; a duplicate
    # definition of an unreferenced id is retail noise, not a parity gap.
    ambiguous_keys = {image_key(value) for value in ambiguous} & set(canonical)
    ambiguous = tuple(
        sorted(
            (canonical[key] for key in ambiguous_keys),
            key=lambda value: (value.casefold(), value),
        )
    )

    atlas_cache: dict[str, bool] = {}
    shipped_ids: list[str] = []
    reasons: dict[str, str] = {}
    for key in sorted(canonical):
        image_id = canonical[key]
        if key in ambiguous_keys:
            # Two retail definitions disagree on the crop; shipping either one
            # would be a coin flip, so this fails closed and by name.
            reasons[image_id] = "ambiguous-mapped-image"
            continue
        path, reason = lookup_shipped_image(shipped_index, image_id)
        if not reason:
            shipped_ids.append(image_id)
            continue
        record = mapped.get(key)
        if record is None:
            reasons[image_id] = "no-mapped-image"
            continue
        texture = record.texture
        present = atlas_cache.get(texture.casefold())
        if present is None:
            present = compiled_atlas_path(oracle_root, texture) is not None
            atlas_cache[texture.casefold()] = present
        reasons[image_id] = reason if present else "retail-atlas-absent"

    allowlist = {image_key(value) for value in source_null_allowlist}
    invalid = sorted(
        canonical[key]
        for key in allowlist
        if key in canonical
        and reasons.get(canonical[key]) not in {"retail-atlas-absent", "no-mapped-image"}
    )
    if invalid:
        raise InterfaceArtError(
            "source-null allowlist covers images that are not retail-source gaps: "
            + ", ".join(invalid)
        )
    unknown = sorted(value for value in allowlist if value not in canonical)
    if unknown:
        raise InterfaceArtError(
            "source-null allowlist names ids outside this scope: " + ", ".join(unknown)
        )

    waived = tuple(
        sorted(
            (canonical[key] for key in allowlist),
            key=lambda value: (value.casefold(), value),
        )
    )
    unresolved = tuple(
        sorted(
            (
                image_id
                for image_id in reasons
                if image_key(image_id) not in allowlist
            ),
            key=lambda value: (value.casefold(), value),
        )
    )

    return InterfaceArtReport(
        oracle_root=oracle_root.as_posix(),
        pack_dirs=tuple(directory.as_posix() for directory in pack_dirs),
        scope=scope,
        reference_count=len(references),
        referenced_id_count=len(canonical),
        shipped_ids=tuple(shipped_ids),
        unresolved_ids=unresolved,
        waived_ids=waived,
        reasons=dict(reasons),
        owners={key: tuple(sorted(set(value))) for key, value in owners.items()},
        ambiguous_mapped_images=ambiguous,
    )


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #


def _parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.interface_art",
        description=(
            "Judge: every retail CommandButton/CommandSet/SelectPortrait image "
            "reference must resolve to a shipped PNG in the mounted packs."
        ),
    )
    parser.add_argument("--oracle-root", type=Path, default=DEFAULT_ORACLE_ROOT)
    parser.add_argument("--selection", type=Path, default=DEFAULT_SELECTION)
    parser.add_argument("--packs-root", type=Path, default=DEFAULT_PACKS_ROOT)
    parser.add_argument(
        "--extra-pack",
        type=Path,
        action="append",
        default=[],
        help="additional pack directory to treat as mounted (repeatable)",
    )
    parser.add_argument(
        "--no-selection",
        action="store_true",
        help="ignore selection.json and judge only --extra-pack directories",
    )
    parser.add_argument("--scope", default="all")
    parser.add_argument(
        "--source-null-allowlist",
        type=Path,
        help="file of NAMED image ids waived as retail-source gaps (rulebook P6)",
    )
    parser.add_argument("--json", type=Path, help="write the full report as JSON")
    parser.add_argument(
        "--max-listed",
        type=int,
        default=40,
        help="how many unresolved names to print (the JSON report always has all)",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = _parse_args(argv)
    allowlist: tuple[str, ...] = ()
    if args.source_null_allowlist is not None:
        allowlist = tuple(
            line.split("#", 1)[0].strip()
            for line in args.source_null_allowlist.read_text(encoding="utf-8").splitlines()
            if line.split("#", 1)[0].strip()
        )
    try:
        report = judge_interface_art(
            oracle_root=args.oracle_root,
            selection_path=None if args.no_selection else args.selection,
            packs_root=args.packs_root,
            extra_pack_dirs=tuple(args.extra_pack),
            scope=args.scope,
            source_null_allowlist=allowlist,
        )
    except InterfaceArtError as exc:
        print(f"interface-art judge ERROR: {exc}", file=sys.stderr)
        return 2

    # Rulebook P8: name the packs this verdict was computed against.
    print(f"oracle: {report.oracle_root}")
    for directory in report.pack_dirs:
        print(f"pack:   {directory}")
    if not report.pack_dirs:
        print("pack:   <none mounted>")
    print(
        f"references={report.reference_count} "
        f"referenced_ids={report.referenced_id_count} "
        f"shipped={report.shipped_count}"
    )
    if report.ambiguous_mapped_images:
        print(
            "ambiguous MappedImage ids: "
            + ", ".join(report.ambiguous_mapped_images[:20])
        )
    for reason, count in report.reason_counts().items():
        print(f"  {reason}: {count}")
    for image_id in report.unresolved_ids[: max(0, args.max_listed)]:
        owner = ", ".join(report.owners.get(image_key(image_id), ())[:3])
        print(f"  MISSING {image_id} [{report.reasons[image_id]}] <- {owner}")
    if report.unresolved_count > max(0, args.max_listed):
        print(
            f"  ... {report.unresolved_count - max(0, args.max_listed)} more "
            "(full list in --json report; nothing is truncated there)"
        )
    if report.waived_ids:
        print(f"waived (named retail-source gaps): {', '.join(report.waived_ids)}")
    print(report.summary_line())
    if args.json is not None:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps(report.to_json(), indent=2, sort_keys=True), encoding="utf-8"
        )
        print(f"json: {args.json}")
    return report.exit_code


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    raise SystemExit(main())
