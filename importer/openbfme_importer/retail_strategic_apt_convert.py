"""Convert RotWK's War of the Ring strategic-screen APT closure into a bundle.

WHY THIS LANE EXISTS.  Every strategic HUD element the WOTR screens have been
hand-drawing placeholders for -- the gold filigree frames, the palantir oval,
the phase chevrons, the END TURN button, the TERRITORY/ARMIES/STRUCTURES tray,
the parchment build-slot cards -- ships in retail's own APT archives and has
simply never been imported.  This module rides the proven HUD/shell machinery
(:mod:`retail_hud_apt_convert` decoding, geometry/atlas binding, the exact
cumulative display-list reconstruction, the lenient shell-lane ActionScript
registration) and only supplies the strategic closure, the per-screen output
shape and the bundle identity.

WHAT IT EMITS.  One ``openbfme.strategic-ui`` bundle:

- ``strategic-ui.json`` -- the loader-facing manifest: the screen index, the
  atlas inventory, the supporting-data inventory and the NAMED GAPS;
- ``screens/<movie>.json`` -- per screen: the exact root timeline (cumulative
  display list per authored frame), the authored frame labels, and flattened
  triangle draws for frame 0 plus every LABELED frame;
- ``assets/ui/strategic/atlases/*.png`` -- the retail atlas textures, decoded
  losslessly from their TGA payloads;
- ``data/ini/livingworld*.ini`` and ``art/compiledtextures/lm/*`` -- the
  supporting living-world data, copied byte-for-byte with a hash inventory.

WHAT IT REFUSES TO DO.  It executes no ActionScript, guesses no playback, and
substitutes no art.  Screens whose visible states are behind script-driven
playback keep their draws grouped under the authored frame LABELS -- authored
identities, not invented selections -- and everything the APT data does not
actually carry is a named gap in the manifest, never a synthesized stand-in.
Retail payloads never leave ``workspace``: the CLI refuses an output directory
outside a ``workspace`` tree.
"""

from __future__ import annotations

import argparse
from io import BytesIO
import json
from pathlib import Path
from typing import Any, Mapping

from .retail_hud_apt_convert import (
    HudAptConvertError,
    MAX_TIMELINE_FRAMES,
    _Movie,
    _Transform,
    _canonical_bytes,
    _movie_from_plan,
    _reconstruct_timeline,
    _sha,
    _source_mapping,
    _timeline_labels,
    register_expected_flagged_null_clip_actions,
)
from .retail_shell_apt_convert import _ShellFlattener
from .sage_apt import (
    canonical_sha256,
    parse_apt_constants,
    parse_apt_dat,
    parse_apt_geometry,
    parse_apt_movie,
    parse_tga_identity,
)


OUTPUT_SCHEMA = "openbfme.strategic-ui"
OUTPUT_SCHEMA_VERSION = 0
SCREEN_SCHEMA = "openbfme.strategic-ui-screen"
SCENE_ID = "rotwk.ui.strategic"
MANIFEST_FILE_NAME = "strategic-ui.json"
SCREEN_DIRECTORY = "screens"
ATLAS_DIRECTORY = "assets/ui/strategic/atlases"

#: RotWK authors ``LivingWorldUI.apt`` at 799,258 bytes; every other strategic
#: movie fits the shared 512 KiB bound.  The relaxation is stated here, once,
#: instead of silently widening the shared parser default.
MAX_STRATEGIC_APT_BYTES = 1024 * 1024

#: RotWK's ``MenuExport.apt`` exports 4,574 symbols where BFME2's exports
#: 1,334 -- the expansion rewrote the shared shell library and roughly
#: tripled its symbol table.  MEASURED across the whole 30-movie RotWK
#: closure: MenuExport is the only movie above the shared 4,096 default, and
#: nothing else comes close.  Stated once here rather than widening the
#: shared parser default for every other lane.
MAX_STRATEGIC_APT_EXPORTS = 8192

#: The complete War of the Ring strategic screen set.  Each row is an
#: independent root movie retail shows at its own moments; none composites
#: into another at authoring time, so each becomes its own screen contract.
SCREEN_MOVIES: tuple[str, ...] = (
    "LivingWorldUI",
    "StrategicHUD",
    "StrategicPalantir",
    "StrategicDetailsTray",
    "StrategicDetailsTerritory",
    "StrategicDetailsArmies",
    "StrategicDetailsStructures",
    "StrategicDetailsBuildQueue",
    "StrategicDetailsRegion",
    "StrategicDetailsArmyRetinue",
    "StrategicEndTurnButton",
    "StrategicNextTurnInd",
    "StrategicPlayerStatus",
    "StrategicBattlePrompt",
    "StrategicBattlePromptArmyPanel",
    "StrategicBattlePromptPlayerPage",
    "StrategicConflictResults",
    "StrategicChecklist",
    "StrategicVeterancy",
    "StrategicStats",
    "StrategicDynamicAutoResolve",
    "StrategicArmyUnitSwapper",
    "StrategicRegionAward",
    "TimeLine",
)

#: Libraries the screens import symbols from.  ``libInGameUI`` and
#: ``libInGameImagesMain`` serve the in-game HUD too; ``TimeLine`` alone pulls
#: the shell libraries (``MenuExport``/``MenuFrameAndBg``/``GameWindowGadgets``).
LIBRARY_MOVIES: tuple[str, ...] = (
    "libInGameUI",
    "libInGameImagesMain",
    "PalantirExport",
    "GameWindowGadgets",
    "MenuExport",
    "MenuFrameAndBg",
)

MOVIE_CLOSURE: tuple[tuple[str, str], ...] = tuple(
    [(name, "screen") for name in SCREEN_MOVIES]
    + [(name, "library") for name in LIBRARY_MOVIES]
)

#: Supporting living-world data copied byte-for-byte beside the screens.  The
#: fixed rows are the exact files the WOTR streams read; the two globs pick up
#: retail's own per-faction icon documents and strategic map texture set.
SUPPORT_FIXED_PATHS: tuple[str, ...] = (
    "data/ini/livingworldbuildingicons.ini",
    "data/ini/livingworldbuildploticons.ini",
    "data/ini/livingworldbuildings.ini",
    "data/ini/livingworldlogic.ini",
    "data/ini/livingworldplayers.ini",
    "data/ini/livingworldregioneffects.ini",
    # The authored font substitution/settings blocks travel with the fonts so
    # the SachaWynter question below stays answerable from the bundle alone.
    "data/ini/fontsettings.ini",
    "data/ini/fontsubstitution.ini",
)

FONT_DIRECTORY = "assets/ui/strategic/fonts"

#: The three retail font winners, copied byte-for-byte.  ``expectedFamily`` is
#: the family name EMBEDDED in the file's own sfnt name table; the converter
#: re-reads it and fails closed on drift.  Only ``Albertus MT`` matches an APT
#: font name exactly (and is additionally the HUD oracle's proven winner);
#: the APT name ``SachaWynter`` matches NO shipped family exactly --
#: ``sachwt__.ttf`` embeds ``SachaWynterTight`` and the authored
#: ``FontSubstitution "SachaWynter"`` rows point at ``Omnia LT Std`` for sizes
#: 8 and 40 only -- so that binding is a NAMED GAP, not a guess.
FONT_SOURCES: tuple[dict[str, Any], ...] = (
    {
        "virtualPath": "albertusmt.otf",
        "expectedFamily": "Albertus MT",
        # RotWK's own Albertus MT, not BFME2's.  The expansion ships a LARGER
        # file (33,860 bytes vs BFME2's 24,712) under the same family name --
        # extended coverage, same typeface.  This lane is the RotWK closure,
        # so it takes RotWK's copy; the HUD lane's sealed BFME2 winner
        # (6a1990e1...) is a different edition's file and stays its own.
        "sha256": (
            "6ac8d7d145b31482de5916274d77e70a047b44bafd43477fbb0b0a0a59f3a062"
        ),
        "aptFontName": "Albertus MT",
        "bindingProven": True,
        "bindingEvidence": (
            "exact embedded-family match on RotWK's own albertusmt.otf, plus"
            " the sealed men-hud-font-albertus-mt winner for the same family"
            " (docs/archive/RETAIL_HUD_RUNTIME.md)"
        ),
    },
    {
        "virtualPath": "sachwt__.ttf",
        "expectedFamily": "SachaWynterTight",
        "sha256": (
            "b03ef65ebe01b73099b04d91a3e27cfcc9ed1ed6b0a18f8d7208258d7bbd15a8"
        ),
        "aptFontName": None,
        "bindingProven": False,
        "bindingEvidence": (
            "candidate for APT font 'SachaWynter'; embedded family is"
            " 'SachaWynterTight', which is NOT an exact match, so the binding"
            " stays unproven and fail-closed"
        ),
    },
    {
        "virtualPath": "omnialtstd.ttf",
        "expectedFamily": "Omnia LT Std",
        "sha256": (
            "4e5dcd628a7cc18f3ff7085cc8eaffb0168f00e9a9d0ea20a887a7fc324a6d16"
        ),
        "aptFontName": None,
        "bindingProven": False,
        "bindingEvidence": (
            "authored FontSubstitution 'SachaWynter' target at sizes 8 and 40"
            " only; other sizes' retail behavior is unproven"
        ),
    },
)
MAX_FONT_BYTES = 4 * 1024 * 1024
SUPPORT_GLOB_DIRECTORIES: tuple[tuple[str, str], ...] = (
    ("data/ini/livingworldicons", "*.ini"),
    ("art/compiledtextures/lm", "*.*"),
)
MAX_SUPPORT_FILES = 256
MAX_SUPPORT_FILE_BYTES = 16 * 1024 * 1024

MAX_STRATEGIC_SOURCE_BYTES = 192 * 1024 * 1024
MAX_STRATEGIC_SOURCE_COUNT = 4096

#: The eight measured PlaceObject records in RotWK ``TimeLine.apt`` whose
#: clip-action flag is set with a null pointer -- the same authored quirk the
#: HUD closure proved once in ``InGameHeroSelect.apt``.  MEASURED, not
#: assumed: a rescan of the whole 30-movie RotWK closure finds exactly these
#: eight, all flags ``0xA6``, at a 64-byte stride.  (The offsets moved when
#: this lane was corrected from the BFME2 asset layer to RotWK's -- BFME2's
#: TimeLine.apt carries the same eight records at 120,344..120,792.  Same
#: authored quirk, different file, so the identities are re-measured rather
#: than reused.)  Any other flagged-null record still fails closed inside the
#: shared parser.
EXPECTED_FLAGGED_NULL_CLIP_ACTIONS: tuple[tuple[str, int, int], ...] = tuple(
    ("timeline.apt", offset, 0xA6)
    for offset in (
        72_228,
        72_292,
        72_356,
        72_420,
        72_484,
        72_548,
        72_612,
        72_676,
    )
)
register_expected_flagged_null_clip_actions(EXPECTED_FLAGGED_NULL_CLIP_ACTIONS)


class StrategicAptConvertError(HudAptConvertError):
    """Raised when a strategic APT source violates the bounded contract."""


def _embedded_font_family(data: bytes, label: str) -> str:
    """Read the family name (nameID 1) out of one sfnt name table, bounded.

    This is identity verification for byte-preserved retail fonts, not a font
    engine: exactly one Windows-Unicode (or legacy Mac Roman) family record is
    required, and every offset is checked before it is read.
    """

    import struct

    if len(data) < 12 or len(data) > MAX_FONT_BYTES:
        raise StrategicAptConvertError(f"{label} violates font size bounds")
    tag = data[:4]
    if tag not in (b"\x00\x01\x00\x00", b"OTTO", b"true"):
        raise StrategicAptConvertError(f"{label} is not an sfnt font")
    (table_count,) = struct.unpack_from(">H", data, 4)
    if not 1 <= table_count <= 64:
        raise StrategicAptConvertError(f"{label} sfnt table count is invalid")
    name_offset = name_length = None
    for index in range(table_count):
        record = 12 + index * 16
        if record + 16 > len(data):
            raise StrategicAptConvertError(f"{label} sfnt directory is truncated")
        table_tag, _, offset, length = struct.unpack_from(">4sIII", data, record)
        if table_tag == b"name":
            name_offset, name_length = offset, length
            break
    if name_offset is None or name_offset + name_length > len(data):
        raise StrategicAptConvertError(f"{label} lacks a bounded name table")
    _, count, string_offset = struct.unpack_from(">HHH", data, name_offset)
    if not 1 <= count <= 512:
        raise StrategicAptConvertError(f"{label} name record count is invalid")
    candidates: dict[tuple[int, int], str] = {}
    for index in range(count):
        record = name_offset + 6 + index * 12
        if record + 12 > name_offset + name_length:
            raise StrategicAptConvertError(f"{label} name records are truncated")
        platform, encoding, language, name_id, length, offset = struct.unpack_from(
            ">6H", data, record
        )
        if name_id != 1:
            continue
        start = name_offset + string_offset + offset
        if start + length > name_offset + name_length or length > 256:
            raise StrategicAptConvertError(f"{label} family string is out of bounds")
        raw = data[start : start + length]
        if platform == 3 and encoding == 1 and language == 0x0409:
            candidates[(3, 1)] = raw.decode("utf-16-be", "strict")
        elif platform == 1 and encoding == 0 and language == 0:
            candidates[(1, 0)] = raw.decode("latin-1", "strict")
    family = candidates.get((3, 1)) or candidates.get((1, 0))
    if not family:
        raise StrategicAptConvertError(f"{label} embeds no readable family name")
    return family


class _StrategicFlattener(_ShellFlattener):
    """The shell lane's lenient flattener plus the flagged-null record path.

    ``TimeLine.apt`` authors eight PlaceObject records whose clip-action flag
    is set with a null pointer (the allowlisted quirk above).  The shared
    flattener would refuse the empty event list; here each such record is
    preserved as an explicit ``source-flagged-null-clip-action-pointer``
    blocker -- the same name the HUD lane uses for its one HeroSelect record
    -- instead of acquiring an invented handler or aborting the screen.

    It also binds external fonts BY EXACT AUTHORED NAME against the measured
    retail font winners (``strategic_font_bindings``): ``Albertus MT`` binds;
    ``SachaWynter`` matches no embedded family exactly and keeps its blocker.
    """

    #: ``authored APT font name -> manifest font row``, set by the converter
    #: from :data:`FONT_SOURCES` after the winners' bytes are verified.
    strategic_font_bindings: dict[str, dict[str, Any]] = {}

    def _font_contract(
        self, movie: _Movie, font_character_id: int
    ) -> tuple[dict[str, Any] | None, tuple[str, int] | None]:
        resolved = self._resolve(movie, font_character_id)
        if resolved is None:
            return None, None
        font_movie, resolved_id = resolved
        character = font_movie.characters[resolved_id]
        if character["kind"] != "font":
            self.block(
                "text-font-character-kind-changed",
                movie.name,
                fontCharacterId=font_character_id,
                resolvedMovie=font_movie.name,
                resolvedCharacterId=resolved_id,
                resolvedKind=str(character["kind"]),
            )
            return None, None
        key = (font_movie.name.casefold(), resolved_id)
        definition = self.font_definitions.get(key)
        if definition is None:
            definition = {
                "fontId": f"{font_movie.name.casefold()}:{resolved_id}",
                "movie": font_movie.name,
                "characterId": resolved_id,
                "sourceOffset": int(character["sourceOffset"]),
                "definitionByteLength": int(character["definitionByteLength"]),
                "definitionSha256": str(character["definitionSha256"]),
                "name": str(character["name"]),
                "glyphCount": int(character["glyphCount"]),
                "glyphCharacterIds": list(character["glyphCharacterIds"]),
                "fontPayloadContained": bool(character["glyphCount"]),
            }
            self.font_definitions[key] = definition
            if not definition["fontPayloadContained"]:
                binding = self.strategic_font_bindings.get(str(definition["name"]))
                if binding is not None:
                    definition["externalFont"] = {
                        "fontName": str(definition["name"]),
                        "cookedFont": str(binding["cookedFont"]),
                        "sourceVirtualPath": str(binding["sourceVirtualPath"]),
                        "sourceSha256": str(binding["sha256"]),
                        "byteLength": int(binding["byteLength"]),
                        "embeddedFamily": str(binding["embeddedFamily"]),
                        "bindingEvidence": str(binding["bindingEvidence"]),
                        "runtimeLoading": "sha256-verified-fontfile",
                        "fallbackAllowed": False,
                    }
                else:
                    self.block(
                        "text-font-payload-not-bundled",
                        font_movie.name,
                        fontId=definition["fontId"],
                        fontCharacterId=resolved_id,
                        fontName=definition["name"],
                        glyphCount=0,
                        sourceOffset=definition["sourceOffset"],
                        sourceClosure="rotwk-strategic-30-movie-bundle",
                        fallbackAllowed=False,
                    )
        return definition, key

    def _register_clip_actions(
        self,
        movie: _Movie,
        row: Mapping[str, Any],
        path: str,
        depth: int,
    ) -> None:
        if str(row.get("clipActionsPointerState", "")) == "source-flagged-null":
            self.block(
                "source-flagged-null-clip-action-pointer",
                movie.name,
                sourceOffset=int(row["sourceOffset"]),
                flags=int(row["flags"]),
                recordSha256=str(row.get("clipActionsRecordSha256", "")),
                path=f"{path}/{depth}",
                parityReady=False,
            )
            return
        super()._register_clip_actions(movie, row, path, depth)


def _normalise(virtual_path: str) -> str:
    return str(virtual_path).replace("\\", "/").strip("/")


def _movie_of(virtual_path: str) -> str | None:
    """Return the closure movie a source virtual path belongs to."""

    relative = _normalise(virtual_path)
    lowered = relative.casefold()
    for name, _role in MOVIE_CLOSURE:
        folded = name.casefold()
        if lowered in {f"{folded}.apt", f"{folded}.const", f"{folded}.dat"}:
            return name
        if lowered.startswith(f"{folded}_geometry/") and lowered.endswith(".ru"):
            return name
        if lowered.startswith(f"art/textures/apt_{folded}_") and lowered.endswith(
            ".tga"
        ):
            return name
    return None


def _is_support_path(virtual_path: str) -> bool:
    lowered = _normalise(virtual_path).casefold()
    if lowered in {path.casefold() for path in SUPPORT_FIXED_PATHS}:
        return True
    return any(
        lowered.startswith(f"{directory.casefold()}/")
        for directory, _pattern in SUPPORT_GLOB_DIRECTORIES
    )


def _font_rows(payloads: Mapping[str, bytes]) -> list[dict[str, Any]]:
    """Verify and describe the three retail font winners, fail-closed."""

    rows: list[dict[str, Any]] = []
    for source in FONT_SOURCES:
        virtual = str(source["virtualPath"])
        payload = payloads.get(virtual.casefold())
        if payload is None:
            raise StrategicAptConvertError(f"strategic closure lacks {virtual}")
        digest = _sha(payload)
        if digest != str(source["sha256"]):
            raise StrategicAptConvertError(f"font winner digest changed: {virtual}")
        family = _embedded_font_family(payload, virtual)
        if family != str(source["expectedFamily"]):
            raise StrategicAptConvertError(
                f"{virtual} embeds family {family!r}, expected "
                f"{source['expectedFamily']!r}"
            )
        stem = Path(virtual).stem.casefold().replace("_", "-").strip("-")
        rows.append(
            {
                "sourceVirtualPath": virtual,
                "cookedFont": f"{FONT_DIRECTORY}/{stem}-{digest[:12]}{Path(virtual).suffix.casefold()}",
                "byteLength": len(payload),
                "sha256": digest,
                "embeddedFamily": family,
                "aptFontName": source["aptFontName"],
                "bindingProven": bool(source["bindingProven"]),
                "bindingEvidence": str(source["bindingEvidence"]),
            }
        )
    return rows


def _required_strategic_contract(
    payloads: Mapping[str, bytes], names: Mapping[str, str]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]]:
    """Group the bounded strategic sources into per-movie plan rows."""

    if not payloads:
        raise StrategicAptConvertError("strategic APT bundle has no sources")
    if len(payloads) > MAX_STRATEGIC_SOURCE_COUNT:
        raise StrategicAptConvertError(
            "strategic APT bundle source count exceeds bounds"
        )
    total = sum(len(value) for value in payloads.values())
    if total > MAX_STRATEGIC_SOURCE_BYTES:
        raise StrategicAptConvertError("strategic APT bundle payload exceeds bounds")

    grouped: dict[str, dict[str, Any]] = {
        name: {"apt": None, "const": None, "dat": None, "geometry": [], "atlases": []}
        for name, _role in MOVIE_CLOSURE
    }
    font_paths = {str(row["virtualPath"]).casefold() for row in FONT_SOURCES}
    support_rows: list[dict[str, Any]] = []
    for key, payload in payloads.items():
        relative = _normalise(names[key])
        if relative.casefold() in font_paths:
            continue  # verified separately by _font_rows
        if _is_support_path(relative):
            if len(payload) > MAX_SUPPORT_FILE_BYTES:
                raise StrategicAptConvertError(
                    f"supporting file exceeds bounds: {relative}"
                )
            support_rows.append(
                {
                    "virtualPath": relative,
                    "byteLength": len(payload),
                    "sha256": _sha(payload),
                }
            )
            continue
        owner = _movie_of(relative)
        if owner is None:
            raise StrategicAptConvertError(
                f"strategic APT bundle contains an out-of-closure source: {relative}"
            )
        lowered = relative.casefold()
        bucket = grouped[owner]
        if lowered.endswith(".apt"):
            bucket["apt"] = (relative, payload)
        elif lowered.endswith(".const"):
            bucket["const"] = (relative, payload)
        elif lowered.endswith(".dat"):
            bucket["dat"] = (relative, payload)
        elif lowered.endswith(".ru"):
            bucket["geometry"].append((relative, payload))
        elif lowered.endswith(".tga"):
            bucket["atlases"].append((relative, payload))
        else:  # pragma: no cover - _movie_of already rejects other suffixes
            raise StrategicAptConvertError(f"unexpected strategic source: {relative}")
    if len(support_rows) > MAX_SUPPORT_FILES:
        raise StrategicAptConvertError("supporting file count exceeds bounds")
    support_rows.sort(key=lambda row: str(row["virtualPath"]).casefold())

    raw_movies: list[dict[str, Any]] = []
    atlas_rows: list[dict[str, Any]] = []
    for name, role in MOVIE_CLOSURE:
        bucket = grouped[name]
        for required in ("apt", "const", "dat"):
            if bucket[required] is None:
                raise StrategicAptConvertError(
                    f"strategic closure lacks {name}.{required}"
                )
        const_path, const_bytes = bucket["const"]
        apt_path, apt_bytes = bucket["apt"]
        dat_path, dat_bytes = bucket["dat"]
        constants = parse_apt_constants(const_bytes, const_path)
        movie = parse_apt_movie(
            apt_bytes,
            constants,
            apt_path,
            max_bytes=MAX_STRATEGIC_APT_BYTES,
            max_exports=MAX_STRATEGIC_APT_EXPORTS,
        )
        image_map = parse_apt_dat(dat_bytes, dat_path)
        geometry = [
            parse_apt_geometry(payload, path)
            for path, payload in sorted(
                bucket["geometry"], key=lambda row: str(row[0]).casefold()
            )
        ]
        movie_atlases: list[dict[str, Any]] = []
        for path, payload in sorted(
            bucket["atlases"], key=lambda row: str(row[0]).casefold()
        ):
            parsed = parse_tga_identity(payload, path)
            digest = str(parsed["sha256"])
            stem = Path(path).stem.casefold().replace("_", "-")
            parsed["resourceId"] = f"strategic-atlas-{stem[:48]}-{digest[:8]}"
            parsed["cookedPng"] = f"{ATLAS_DIRECTORY}/{stem}-{digest[:12]}.png"
            movie_atlases.append(parsed)
            atlas_rows.append(parsed)
        raw_movies.append(
            {
                "movie": name,
                "role": role,
                "apt": movie,
                "constants": constants,
                "imageMap": image_map,
                "geometry": geometry,
                "atlases": movie_atlases,
            }
        )
    return raw_movies, atlas_rows, support_rows


def _synthetic_place_rows(
    display_list: list[Mapping[str, Any]],
) -> list[dict[str, Any]]:
    """Turn one exact cumulative display list back into place-object rows.

    ``_reconstruct_timeline`` already proved these entries exactly; this only
    re-encodes them in the shape ``_Flattener._frame`` consumes so a LABELED
    frame can be flattened from its true cumulative state instead of from its
    own frame items alone.  Nothing is defaulted that the display entry does
    not carry.
    """

    rows: list[dict[str, Any]] = []
    for entry in display_list:
        flags = 0x02 | 0x04 | 0x08 | 0x10
        name = str(entry.get("name", ""))
        if name:
            flags |= 0x20
        clip_depth = int(entry.get("clipDepth", 0))
        if clip_depth:
            flags |= 0x40
        offsets = list(entry.get("sourceOffsets", []))
        row: dict[str, Any] = {
            "kind": "place-object",
            "sourceOffset": int(offsets[-1]) if offsets else -1,
            "flags": flags,
            "depth": int(entry["depth"]),
            "characterId": int(entry["characterId"]),
            "matrix": [float(value) for value in entry["matrix"]],
            "translation": [float(value) for value in entry["translation"]],
            "tint": [float(value) for value in entry["tint"]],
            "additive": [float(value) for value in entry["additive"]],
            "ratio": float(entry.get("ratio", 0.0)),
            "clipDepth": clip_depth,
        }
        if name:
            row["name"] = name
        rows.append(row)
    return rows


def _resolved_reference(movie: _Movie, character_id: int) -> dict[str, Any]:
    """Name what a placed character id is, locally or through an import."""

    reference: dict[str, Any] = {"characterId": character_id}
    if 0 <= character_id < len(movie.characters):
        kind = str(movie.characters[character_id]["kind"])
        reference["kind"] = kind
        if kind == "null" and character_id in movie.imports:
            source_movie, symbol = movie.imports[character_id]
            reference["import"] = {"movie": source_movie, "symbol": symbol}
    else:
        reference["kind"] = "out-of-range"
    return reference


def _named_instances(movie: _Movie, root_frames: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Collect every authored instance NAME, root and sprite scope alike.

    These are the element ids other streams address retail's layout by --
    ``EndTurnButton``, ``PhaseText``, ``TerritoryTab`` and their kin.  Only
    authored PlaceObject names are reported; nothing is inferred from
    resemblance.
    """

    rows: list[dict[str, Any]] = []
    seen: set[tuple[str, int, str]] = set()
    for frame in root_frames:
        for entry in frame["displayList"]:
            name = str(entry.get("name", ""))
            if not name:
                continue
            key = ("root", int(entry["depth"]), name)
            if key in seen:
                continue
            seen.add(key)
            rows.append(
                {
                    "scope": "root",
                    "firstFrame": int(frame["frameIndex"]),
                    "depth": int(entry["depth"]),
                    "name": name,
                    "character": _resolved_reference(
                        movie, int(entry["characterId"])
                    ),
                    "translation": [float(v) for v in entry["translation"]],
                }
            )
    for character in movie.characters:
        frames = character.get("frames")
        if not isinstance(frames, list):
            continue
        sprite_id = int(character["characterId"])
        for frame_index, items in enumerate(frames):
            for row in items:
                if row.get("kind") != "place-object" or "name" not in row:
                    continue
                name = str(row["name"])
                scope = f"sprite:{sprite_id}"
                key = (scope, int(row["depth"]), name)
                if key in seen:
                    continue
                seen.add(key)
                rows.append(
                    {
                        "scope": scope,
                        "firstFrame": frame_index,
                        "depth": int(row["depth"]),
                        "name": name,
                        "character": _resolved_reference(
                            movie, int(row["characterId"])
                        ),
                        "translation": [
                            float(v) for v in row.get("translation", [0.0, 0.0])
                        ],
                    }
                )
    rows.sort(
        key=lambda row: (
            str(row["scope"]),
            int(row["firstFrame"]),
            int(row["depth"]),
            str(row["name"]).casefold(),
        )
    )
    return rows


def _compact_program(program: Mapping[str, Any]) -> dict[str, Any]:
    """Inventory one ActionScript program without shipping its decode.

    The strategic bundle is an ART import; scripts are inventoried so their
    existence and identity are provable, and left unexecuted behind the
    ``action-scripts-not-executed`` named gap.
    """

    return {
        "scriptId": str(program.get("scriptId", program.get("programId", ""))),
        "movie": str(program["movie"]),
        "sourceOffset": int(program["sourceOffset"]),
        "byteLength": int(program["byteLength"]),
        "sha256": str(program["sha256"]),
        "supported": bool(program["supported"]),
    }


def _register_frame_actions(
    flattener: _StrategicFlattener, movie: _Movie, frames: list[list[dict[str, Any]]]
) -> None:
    """Register every frame ActionScript so unsupported programs become
    named blockers instead of silently vanishing from the inventory."""

    for rows in frames:
        for row in rows:
            if row.get("kind") in ("action-script", "init-action-script"):
                flattener._register_action_program(movie, row)


def _stop_frames(
    flattener: _StrategicFlattener,
    movie: _Movie,
    frames: list[list[dict[str, Any]]],
) -> set[int]:
    """Frames whose authored script provably executes ``stop``.

    These are the RESTING states the Flash author parked the playhead on --
    the only frame-selection evidence the APT bytes themselves carry.  Only
    programs the bounded decoder fully proved (``supported``) count; an
    undecodable program never nominates a frame.
    """

    result: set[int] = set()
    for frame_index, rows in enumerate(frames):
        for row in rows:
            if row.get("kind") not in ("action-script", "init-action-script"):
                continue
            script_id = f"{movie.name.casefold()}:{int(row['sourceOffset'])}"
            program = flattener.action_programs.get(script_id)
            if (
                program is not None
                and bool(program.get("supported"))
                and any(
                    effect.get("kind") == "stop"
                    for effect in program.get("effects", [])
                )
            ):
                result.add(frame_index)
    return result


def _flatten_frame_set(
    flattener: _StrategicFlattener,
    movie: _Movie,
    frames: list[list[dict[str, Any]]],
    timeline_frames: list[dict[str, Any]],
    labels: dict[str, int],
    stops: set[int],
    path_prefix: str,
    root_stack_id: int,
) -> list[dict[str, Any]]:
    """Flatten frame 0, every authored label and every authored stop frame."""

    labels_by_frame: dict[int, list[str]] = {}
    for label, frame_index in labels.items():
        labels_by_frame.setdefault(frame_index, []).append(label)
    for frame_labels in labels_by_frame.values():
        frame_labels.sort(key=str.casefold)

    flattened: list[dict[str, Any]] = []
    for frame_index in sorted({0, *labels.values(), *stops}):
        before = len(flattener.draws)
        path = f"{path_prefix}:frame:{frame_index}"
        if frame_index == 0:
            # Frame 0 has no predecessor state, so the authored items ARE the
            # cumulative state; flattening them directly also registers the
            # authored clip actions the synthetic rows deliberately drop.
            rows = frames[0]
        else:
            rows = _synthetic_place_rows(
                timeline_frames[frame_index]["displayList"]
            )
        flattener._frame(
            flattener.movies[movie.name.casefold()],
            rows,
            _Transform(),
            ((movie.name, root_stack_id),),
            path,
            (),
        )
        draws = flattener.draws[before:]
        flattened.append(
            {
                "frameIndex": frame_index,
                "labels": labels_by_frame.get(frame_index, []),
                "authoredStop": frame_index in stops,
                "drawCount": len(draws),
                "draws": draws,
            }
        )
    return flattened


def _export_contracts(
    flattener: _StrategicFlattener,
    movie: _Movie,
    export_rows: list[Mapping[str, Any]],
) -> list[dict[str, Any]]:
    """Flatten every exported sprite as standalone element art.

    Retail attaches these symbols dynamically (``LivingWorldUI`` exports the
    RegionPopup card and the two conquest notices this way), so their art
    never appears on the root's own frames.  Each export is flattened exactly
    like a screen root: frame 0, authored labels, authored stop frames.
    """

    grouped: dict[str, list[int]] = {}
    order: list[tuple[str, int]] = []
    for row in export_rows:
        symbol = str(row["symbol"])
        character_id = int(row["characterId"])
        if symbol not in grouped:
            order.append((symbol, character_id))
        grouped.setdefault(symbol, []).append(character_id)

    contracts: list[dict[str, Any]] = []
    for symbol, character_id in order:
        candidates = sorted(set(grouped[symbol]))
        if len(candidates) != 1:
            flattener.block(
                "ambiguous-export-symbol",
                movie.name,
                symbol=symbol,
                candidateCharacterIds=candidates,
            )
            continue
        if not 0 <= character_id < len(movie.characters):
            flattener.block(
                "export-character-out-of-range",
                movie.name,
                symbol=symbol,
                characterId=character_id,
            )
            continue
        character = movie.characters[character_id]
        kind = str(character["kind"])
        row: dict[str, Any] = {
            "symbol": symbol,
            "characterId": character_id,
            "kind": kind,
        }
        if kind == "sprite":
            frames = character.get("frames", [])
            labels = _timeline_labels(frames)
            timeline, failures = _reconstruct_timeline(
                movie.name, character_id, frames
            )
            if failures:
                flattener.block(
                    "export-timeline-frames-not-converted",
                    movie.name,
                    symbol=symbol,
                    failures=failures,
                )
            _register_frame_actions(flattener, movie, frames)
            stops = _stop_frames(flattener, movie, frames)
            row.update(
                {
                    "labels": {
                        label: labels[label]
                        for label in sorted(labels, key=str.casefold)
                    },
                    "frameCount": len(frames),
                    "flattenedFrames": _flatten_frame_set(
                        flattener,
                        movie,
                        frames,
                        timeline["frames"],
                        labels,
                        stops,
                        f"export:{movie.name}:{symbol}",
                        character_id,
                    ),
                    "timeline": timeline,
                }
            )
        elif kind == "shape":
            before = len(flattener.draws)
            flattener._geometry(
                movie,
                int(character["geometryId"]),
                _Transform(),
                f"export:{movie.name}:{symbol}",
            )
            draws = flattener.draws[before:]
            row["flattenedFrames"] = [
                {
                    "frameIndex": 0,
                    "labels": [],
                    "authoredStop": False,
                    "drawCount": len(draws),
                    "draws": draws,
                }
            ]
        else:
            flattener.block(
                "export-kind-not-flattened",
                movie.name,
                symbol=symbol,
                characterId=character_id,
                characterKind=kind,
            )
        contracts.append(row)
    return contracts


def _compact_timeline(timeline: dict[str, Any]) -> dict[str, Any]:
    """Compress cumulative ``sourceOffsets`` lists to first/last/count.

    A 3,269-frame tween (the LivingWorldUI swirls) accumulates one source
    offset per frame per entry, which serializes quadratically -- 43 MB for
    one sprite.  The identity a consumer needs is WHICH authored records
    produced the state (first placement, latest mutation) and HOW MANY; the
    interior offsets are recoverable from the retail bytes this bundle's
    aggregate pins, so summarizing them loses no provenance the contract
    relies on.
    """

    for frame in timeline["frames"]:
        for entry in frame["displayList"]:
            offsets = entry.pop("sourceOffsets", [])
            entry["sourceOffsetFirst"] = int(offsets[0]) if offsets else -1
            entry["sourceOffsetLast"] = int(offsets[-1]) if offsets else -1
            entry["sourceOffsetCount"] = len(offsets)
    return timeline


def _screen_contract(
    movie: _Movie,
    movies: dict[str, _Movie],
    export_rows: list[Mapping[str, Any]],
    font_bindings: Mapping[str, dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Flatten one strategic screen at frame 0, labels and stop frames."""

    labels = _timeline_labels(movie.frames)
    root_character_id = next(
        (
            int(row["characterId"])
            for row in movie.characters
            if str(row["kind"]) == "movie"
        ),
        -1,
    )
    root_timeline, failures = _reconstruct_timeline(
        movie.name, root_character_id, movie.frames
    )
    root_timeline["timelineId"] = f"{movie.name.casefold()}:root"

    flattener = _StrategicFlattener(movies, {})
    flattener.strategic_font_bindings = dict(font_bindings or {})
    if failures:
        flattener.block(
            "root-timeline-frames-not-converted",
            movie.name,
            failures=failures,
        )

    _register_frame_actions(flattener, movie, movie.frames)
    stops = _stop_frames(flattener, movie, movie.frames)
    flattened = _flatten_frame_set(
        flattener,
        movie,
        movie.frames,
        root_timeline["frames"],
        labels,
        stops,
        f"screen:{movie.name}",
        root_character_id,
    )
    exports = _export_contracts(flattener, movie, export_rows)

    named_instances = _named_instances(movie, root_timeline["frames"])
    # Compaction happens only after everything that consumes the exact
    # per-frame offset lists (synthetic rows, named instances) has run.
    root_timeline = _compact_timeline(root_timeline)
    for row in exports:
        if "timeline" in row:
            _compact_timeline(row["timeline"])
    timelines = [
        _compact_timeline(flattener.timelines[key])
        for key in sorted(flattener.timelines)
    ]
    timeline_frame_count = sum(
        int(timeline["frameCount"]) for timeline in timelines
    ) + int(root_timeline["frameCount"])
    if timeline_frame_count > MAX_TIMELINE_FRAMES:
        raise StrategicAptConvertError(
            f"{movie.name} timeline frame count exceeds bounds"
        )

    blockers = sorted(
        flattener.blockers,
        key=lambda item: json.dumps(item, sort_keys=True, separators=(",", ":")),
    )
    programs = [
        _compact_program(flattener.action_programs[key])
        for key in sorted(flattener.action_programs)
    ]
    clip_programs = [
        _compact_program(flattener.clip_action_programs[key])
        for key in sorted(flattener.clip_action_programs)
    ]
    export_frames = [
        frame for row in exports for frame in row.get("flattenedFrames", [])
    ]
    draw_count = sum(
        int(frame["drawCount"]) for frame in (*flattened, *export_frames)
    )
    textured = sum(
        1
        for frame in (*flattened, *export_frames)
        for draw in frame["draws"]
        if draw["kind"] == "textured-triangle"
    )
    contract: dict[str, Any] = {
        "schema": SCREEN_SCHEMA,
        "schemaVersion": OUTPUT_SCHEMA_VERSION,
        "movie": movie.name,
        "authoredResolution": [
            int(movie.root["width"]),
            int(movie.root["height"]),
        ],
        "millisecondsPerFrame": int(movie.root["millisecondsPerFrame"]),
        "frameCount": int(movie.root["frameCount"]),
        "labels": {label: labels[label] for label in sorted(labels, key=str.casefold)},
        "authoredStopFrames": sorted(stops),
        "flattenedFrames": flattened,
        "exports": exports,
        "rootTimeline": root_timeline,
        "timelines": timelines,
        "timelineInstances": sorted(
            flattener.timeline_instances,
            key=lambda item: json.dumps(item, sort_keys=True, separators=(",", ":")),
        ),
        "namedInstances": named_instances,
        "fonts": [
            flattener.font_definitions[key]
            for key in sorted(flattener.font_definitions)
        ],
        "texts": [
            flattener.text_definitions[key]
            for key in sorted(flattener.text_definitions)
        ],
        "textInstances": sorted(
            flattener.text_instances,
            key=lambda item: json.dumps(item, sort_keys=True, separators=(",", ":")),
        ),
        "buttons": [
            flattener.button_definitions[key]
            for key in sorted(flattener.button_definitions)
        ],
        "buttonInstances": sorted(
            flattener.button_instances,
            key=lambda item: json.dumps(item, sort_keys=True, separators=(",", ":")),
        ),
        "actionScriptInventory": programs,
        "clipActionProgramInventory": clip_programs,
        "unsupportedSemantics": blockers,
        "summary": {
            "drawCount": draw_count,
            "texturedTriangleCount": textured,
            "solidTriangleCount": draw_count - textured,
            "flattenedFrameCount": len(flattened) + len(export_frames),
            "labelCount": len(labels),
            "authoredStopFrameCount": len(stops),
            "exportCount": len(exports),
            "timelineCount": len(timelines),
            "timelineFrameCount": timeline_frame_count,
            # Filled below once the contract dict exists to be measured.
            "namedInstanceCount": 0,
            "actionScriptCount": len(programs),
            "clipActionProgramCount": len(clip_programs),
            "blockerCount": len(blockers),
            "rootDisplayListComplete": bool(root_timeline["displayListComplete"]),
        },
    }
    contract["summary"]["namedInstanceCount"] = len(contract["namedInstances"])
    contract["aggregateSha256"] = canonical_sha256(
        {key: value for key, value in contract.items() if key != "aggregateSha256"}
    )
    return contract


#: What the strategic APT data genuinely does NOT carry.  Recorded here, once,
#: as manifest data so every consumer sees the same honest boundary.
NAMED_GAPS: tuple[dict[str, str], ...] = (
    {
        "gap": "nine-slice-metrics-not-authored",
        "detail": (
            "APT authors flat triangle lists with atlas UVs; the format has no "
            "nine-slice/border metadata anywhere in these movies. A consumer "
            "that stretches a frame must derive its own slicing and say so."
        ),
    },
    {
        "gap": "timeline-playback-not-bound",
        "detail": (
            "Draws are exact static flattenings of frame 0, each authored "
            "frame LABEL and each authored script STOP frame. No clock, no "
            "tween playback, no script-driven frame selection is executed."
        ),
    },
    {
        "gap": "sachawynter-font-binding-unproven",
        "detail": (
            "The strategic texts name font 'SachaWynter'. No shipped font "
            "embeds that family exactly: sachwt__.ttf embeds "
            "'SachaWynterTight', and the authored FontSubstitution block maps "
            "sizes 8 and 40 to 'Omnia LT Std' only. Both candidate files ship "
            "in this bundle byte-for-byte, but the name binding stays "
            "fail-closed until the retail registration path is proven."
        ),
    },
    {
        "gap": "action-scripts-not-executed",
        "detail": (
            "Every frame/clip ActionScript is inventoried by offset and hash "
            "and NOT executed. Visibility toggles, text assignment and layout "
            "the scripts perform at retail runtime are absent from the draws."
        ),
    },
    {
        "gap": "dynamic-content-slots-are-empty",
        "detail": (
            "StrategicHUD exports StrategicCommandButton and the screens place "
            "empty LoadMovieFrame/RenderImage/MovieClipFrame hosts the retail "
            "engine fills at runtime (command buttons, portraits, render "
            "feeds). Those slots are recorded as named instances with no art."
        ),
    },
    {
        "gap": "strategic-text-values-are-live",
        "detail": (
            "Text characters carry authored placeholders and APT: variable "
            "names; the live values (turn number, player names, army stats) "
            "come from the engine and must be fed by the consumer."
        ),
    },
)


def make_strategic_manifest(
    screens: list[dict[str, Any]],
    atlas_rows: list[dict[str, Any]],
    support_rows: list[dict[str, Any]],
    font_rows: list[dict[str, Any]],
    source: Mapping[str, Any],
) -> dict[str, Any]:
    """Build the loader-facing bundle manifest."""

    atlas_paths = sorted(
        {str(row["cookedPng"]) for row in atlas_rows}, key=str.casefold
    )
    # PER-TEXTURE PROVENANCE, not just an aggregate.  Seventeen strategic and
    # shell sheets differ between BFME2 and RotWK (the palantir ring, the
    # shell plate that is green in BFME2 and blue-steel in RotWK, the stats
    # and tray sheets).  An aggregate digest proves the SET is the one that
    # was sealed but says nothing a reader can check per sheet, so every
    # cooked atlas also states the retail source path and digest it came
    # from.  Anyone can now diff one PNG's row against either edition's
    # effective-assets manifest and see which edition supplied it.
    atlas_sources = sorted(
        (
            {
                "sourceVirtualPath": str(row["virtualPath"]),
                "sha256": str(row["sha256"]),
                "byteLength": int(row["byteLength"]),
                "width": int(row["width"]),
                "height": int(row["height"]),
                "cookedPng": str(row["cookedPng"]),
            }
            for row in {
                str(item["cookedPng"]): item for item in atlas_rows
            }.values()
        ),
        key=lambda row: str(row["cookedPng"]).casefold(),
    )
    screen_rows = []
    for screen in screens:
        summary = dict(screen["summary"])
        screen_rows.append(
            {
                "movie": str(screen["movie"]),
                "file": f"{SCREEN_DIRECTORY}/{str(screen['movie']).casefold()}.json",
                "aggregateSha256": str(screen["aggregateSha256"]),
                "labels": dict(screen["labels"]),
                "summary": summary,
            }
        )
    manifest: dict[str, Any] = {
        "schema": OUTPUT_SCHEMA,
        "schemaVersion": OUTPUT_SCHEMA_VERSION,
        "sceneId": SCENE_ID,
        "renderPolicy": {
            "actionScriptExecuted": False,
            "timelinePlaybackBound": False,
            "syntheticFallbackAllowed": False,
            "staticLabeledFramesOnly": True,
        },
        "source": dict(source),
        "movies": [{"movie": name, "role": role} for name, role in MOVIE_CLOSURE],
        "screens": screen_rows,
        "atlases": atlas_paths,
        "atlasSources": atlas_sources,
        "fonts": [dict(row) for row in font_rows],
        "supportingData": support_rows,
        "namedGaps": [dict(row) for row in NAMED_GAPS],
        "summary": {
            "screenCount": len(screen_rows),
            "atlasCount": len(atlas_paths),
            "fontCount": len(font_rows),
            "supportingFileCount": len(support_rows),
            "drawCount": sum(
                int(row["summary"]["drawCount"]) for row in screen_rows
            ),
            "texturedTriangleCount": sum(
                int(row["summary"]["texturedTriangleCount"]) for row in screen_rows
            ),
            "namedInstanceCount": sum(
                int(row["summary"]["namedInstanceCount"]) for row in screen_rows
            ),
            "labelCount": sum(
                int(row["summary"]["labelCount"]) for row in screen_rows
            ),
            "blockerCount": sum(
                int(row["summary"]["blockerCount"]) for row in screen_rows
            ),
            "namedGapCount": len(NAMED_GAPS),
        },
    }
    manifest["aggregateSha256"] = canonical_sha256(
        {key: value for key, value in manifest.items() if key != "aggregateSha256"}
    )
    return manifest


def _effective_assets_provenance(root: Path) -> dict[str, Any]:
    """Read WHERE these bytes were extracted from, or say so when unknown.

    The extractor writes ``.openbfme/manifest.json`` beside every effective-
    assets view, and that document names the install root the view was built
    from and the identity digest of the catalog that decided its winners.
    Copying those two identities into the bundle is a MEASUREMENT, not a
    claim: it lets a reader (and the Godot loader) see which retail install --
    which EDITION -- supplied the art, instead of trusting the directory name
    someone happened to type on the command line.
    """

    document = root / ".openbfme" / "manifest.json"
    if not document.is_file() or document.stat().st_size > 512 * 1024 * 1024:
        return {"known": False, "reason": "no effective-assets manifest beside the view"}
    try:
        parsed = json.loads(document.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise StrategicAptConvertError(
            f"unreadable effective-assets manifest: {exc}"
        ) from exc
    if not isinstance(parsed, dict):
        raise StrategicAptConvertError("effective-assets manifest root is invalid")
    install = parsed.get("install")
    catalog = parsed.get("catalog")
    if not isinstance(install, dict) or not isinstance(catalog, dict):
        raise StrategicAptConvertError(
            "effective-assets manifest carries no install/catalog identity"
        )
    return {
        "known": True,
        "installRoot": str(install.get("root", "")),
        "installIdentitySha256": str(install.get("identity_sha256", "")),
        "catalogIdentitySha256": str(catalog.get("identity_sha256", "")),
        "viewAggregateSha256": str(parsed.get("aggregate_sha256", "")),
    }


def convert_strategic_apt_bundle(
    sources: Mapping[str, Path | str | bytes | bytearray],
    output_directory: Path | str,
    *,
    expected_source_aggregate_sha256: str | None = None,
    effective_assets_provenance: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Convert the strategic closure plus supporting data into one bundle."""

    payloads, names = _source_mapping(sources)
    raw_movies, atlas_rows, support_rows = _required_strategic_contract(
        payloads, names
    )
    font_rows = _font_rows(payloads)
    inventory = [
        {
            "virtualPath": names[key],
            "byteLength": len(payloads[key]),
            "sha256": _sha(payloads[key]),
        }
        for key in sorted(
            payloads, key=lambda item: (names[item].casefold(), names[item])
        )
    ]
    source_aggregate = canonical_sha256(inventory)
    if (
        expected_source_aggregate_sha256 is not None
        and source_aggregate != expected_source_aggregate_sha256
    ):
        raise StrategicAptConvertError("strategic APT source aggregate changed")

    movies_list = [_movie_from_plan(raw, source_bytes=payloads) for raw in raw_movies]
    movies = {movie.name.casefold(): movie for movie in movies_list}
    if len(movies) != len(movies_list):
        raise StrategicAptConvertError("strategic APT movie identities collide")
    missing = [name for name, _role in MOVIE_CLOSURE if name.casefold() not in movies]
    if missing:
        raise StrategicAptConvertError(
            f"strategic APT closure is incomplete: {missing}"
        )

    # The exact authored-name binding table the flattener consults; only rows
    # whose APT-name binding is PROVEN participate.
    font_bindings = {
        str(row["aptFontName"]): row
        for row in font_rows
        if row["aptFontName"] is not None and bool(row["bindingProven"])
    }
    exports_by_movie = {
        str(raw["movie"]).casefold(): list(raw["apt"]["exports"])
        for raw in raw_movies
    }
    screens = [
        _screen_contract(
            movies[name.casefold()],
            movies,
            exports_by_movie.get(name.casefold(), []),
            font_bindings,
        )
        for name in SCREEN_MOVIES
    ]
    manifest = make_strategic_manifest(
        screens,
        atlas_rows,
        support_rows,
        font_rows,
        {
            "sourceAggregateSha256": source_aggregate,
            "sourceCount": len(inventory),
            "sourcePayloadBytes": sum(row["byteLength"] for row in inventory),
            "effectiveAssets": dict(
                effective_assets_provenance
                if effective_assets_provenance is not None
                else {"known": False, "reason": "converted from an in-memory source set"}
            ),
        },
    )

    output = Path(output_directory)
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise StrategicAptConvertError(
            "strategic APT output directory must be empty"
        )
    output.mkdir(parents=True, exist_ok=True)
    try:
        import PIL
        from PIL import Image
    except ImportError as exc:  # pragma: no cover - environment guard
        raise StrategicAptConvertError(
            "Pillow is required for strategic atlas conversion"
        ) from exc
    if PIL.__version__ != "12.2.0":
        raise StrategicAptConvertError(
            "Pillow 12.2.0 is required for deterministic strategic atlases; "
            f"found {PIL.__version__}"
        )
    for atlas in atlas_rows:
        key = _normalise(str(atlas["virtualPath"])).casefold()
        target = output / Path(*str(atlas["cookedPng"]).split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(BytesIO(payloads[key])) as opened:
            opened.convert("RGBA").save(
                target, format="PNG", compress_level=9, optimize=False
            )
    for row in support_rows:
        relative = str(row["virtualPath"])
        target = output / Path(*relative.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payloads[relative.casefold()])
    for row in font_rows:
        target = output / Path(*str(row["cookedFont"]).split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payloads[str(row["sourceVirtualPath"]).casefold()])
    for screen in screens:
        target = output / SCREEN_DIRECTORY / f"{str(screen['movie']).casefold()}.json"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(_canonical_bytes(screen))
    (output / MANIFEST_FILE_NAME).write_bytes(_canonical_bytes(manifest))
    return manifest


def strategic_source_virtual_paths(asset_root: Path | str) -> list[str]:
    """Return the bounded strategic source closure below effective-assets."""

    root = Path(asset_root).expanduser().resolve()
    paths: list[str] = []
    for name, _role in MOVIE_CLOSURE:
        for suffix in ("apt", "const", "dat"):
            candidate = root / f"{name}.{suffix}"
            if not candidate.is_file():
                raise StrategicAptConvertError(
                    f"strategic closure lacks {name}.{suffix}"
                )
            paths.append(f"{name}.{suffix}")
        geometry = root / f"{name}_geometry"
        if geometry.is_dir():
            paths.extend(
                f"{name}_geometry/{item.name}"
                for item in sorted(
                    geometry.glob("*.ru"), key=lambda p: p.name.casefold()
                )
            )
        textures = root / "art" / "Textures"
        if textures.is_dir():
            paths.extend(
                f"art/Textures/{item.name}"
                for item in sorted(
                    textures.glob(f"apt_{name}_*.tga"),
                    key=lambda p: p.name.casefold(),
                )
            )
    for relative in (
        *SUPPORT_FIXED_PATHS,
        *(str(row["virtualPath"]) for row in FONT_SOURCES),
    ):
        candidate = root / Path(*relative.split("/"))
        if not candidate.is_file():
            raise StrategicAptConvertError(
                f"strategic support closure lacks {relative}"
            )
        paths.append(relative)
    for directory, pattern in SUPPORT_GLOB_DIRECTORIES:
        base = root / Path(*directory.split("/"))
        if not base.is_dir():
            raise StrategicAptConvertError(
                f"strategic support closure lacks {directory}/"
            )
        matches = sorted(base.glob(pattern), key=lambda p: p.name.casefold())
        if not matches:
            raise StrategicAptConvertError(
                f"strategic support closure is empty: {directory}/"
            )
        paths.extend(f"{directory}/{item.name}" for item in matches if item.is_file())
    return sorted(paths, key=str.casefold)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effective-assets", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--expected-source-aggregate", default=None)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    root = Path(args.effective_assets).expanduser().resolve()
    output = Path(args.output).expanduser().resolve()
    # Retail payloads NEVER leave workspace: the cooked bundle carries retail
    # art verbatim, so the destination must sit inside a workspace tree.
    if "workspace" not in {part.casefold() for part in output.parts}:
        raise StrategicAptConvertError(
            "strategic bundle output must stay below a workspace directory"
        )
    sources = {
        relative: root.joinpath(*relative.split("/"))
        for relative in strategic_source_virtual_paths(root)
    }
    manifest = convert_strategic_apt_bundle(
        sources,
        output,
        expected_source_aggregate_sha256=args.expected_source_aggregate,
        effective_assets_provenance=_effective_assets_provenance(root),
    )
    print(
        json.dumps(
            {
                "aggregateSha256": manifest["aggregateSha256"],
                "summary": manifest["summary"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())


__all__ = [
    "ATLAS_DIRECTORY",
    "EXPECTED_FLAGGED_NULL_CLIP_ACTIONS",
    "FONT_DIRECTORY",
    "FONT_SOURCES",
    "LIBRARY_MOVIES",
    "MANIFEST_FILE_NAME",
    "MAX_STRATEGIC_APT_BYTES",
    "MAX_STRATEGIC_APT_EXPORTS",
    "MOVIE_CLOSURE",
    "NAMED_GAPS",
    "OUTPUT_SCHEMA",
    "OUTPUT_SCHEMA_VERSION",
    "SCENE_ID",
    "SCREEN_DIRECTORY",
    "SCREEN_MOVIES",
    "SCREEN_SCHEMA",
    "StrategicAptConvertError",
    "convert_strategic_apt_bundle",
    "make_strategic_manifest",
    "strategic_source_virtual_paths",
]
