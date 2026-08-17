#!/usr/bin/env python3
"""Generate the exhaustive BFME2/RotWK INI-to-Godot coverage ledger.

The output is private because it is derived from the operator's retail files.
Every effective INI/INC winner and every non-comment semantic line receives a
machine-readable receipt.  Evidence is deliberately conservative:

* importer-mentioned: exact authored term exists as a Python string literal;
* descriptor-emitted: exact term exists in a selected pack JSON key/value;
* runtime-mentioned: exact term exists as a GDScript string literal, or a
  module row has a matching attach/step/apply consumer symbol;
* runtime-tested: it is also named by a test source.

None of those statuses means retail behavior parity.  Module rows additionally
carry the project's measured importer census status and descriptor-side
``runtimeStatus`` values so opaque/deferred extraction cannot inflate progress.

Usage (repository root):
  py -3 tools/retail-ini-coverage.py
  py -3 tools/retail-ini-coverage.py --check
  py -3 tools/retail-ini-coverage.py --require-complete
"""

from __future__ import annotations

import argparse
import ast
from collections import Counter, defaultdict
import csv
from dataclasses import dataclass, field
from hashlib import sha256
import io
import json
from pathlib import Path, PurePosixPath
import re
import sys
from typing import Any, Iterable, Iterator, Mapping


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.catalog import CatalogEntry, InstallCatalog  # noqa: E402
from openbfme_importer.module_census import (  # noqa: E402
    effective_ini_entries,
    module_kind_from_assignment,
    scan_module_support,
)
from openbfme_importer.pipeline import bundle_digest  # noqa: E402
from openbfme_importer.retail_ini_coverage import (  # noqa: E402
    classify_feature_categories,
    evidence_status,
    is_hero_family_object,
    is_neutral_mob_object,
)
from openbfme_importer.sage_ini import MAX_INI_BYTES, parse_object_definitions  # noqa: E402
from openbfme_importer.sage_particles import (  # noqa: E402
    _indent_width,
    _lines as strict_ini_lines,
    _strip_comment,
)


DEFAULT_CATALOGS = {
    "bfme2": ROOT / "workspace/retail-work/catalog/bfme2.json",
    "rotwk": ROOT / "workspace/retail-work/catalog/rotwk.json",
}
DEFAULT_SELECTION = ROOT / "workspace/content-packs/selection.json"
DEFAULT_OUT = ROOT / "workspace/retail-work/reports/retail-ini-coverage"
MODULE_CENSUS = ROOT / "game/data/retail_module_census.json"
GAME_LABELS = {"bfme2": "BFME2 1.06", "rotwk": "RotWK 2.01"}
GAMES = ("bfme2", "rotwk")

# Exact, reviewed authored-command -> descriptor-operation transformations.
# Keep this closed and limited to operations the current Godot consumer really
# executes.  Merely parsing a drawable command is not sufficient: module
# visibility, transition animation, sound, permanent visibility, and script
# control flow deliberately remain coverage gaps.
_EXECUTABLE_SCRIPT_OPERATION_ALIASES: dict[str, tuple[str, ...]] = {
    "curdrawablehidesubobject": ("hide-sub-object",),
    "curdrawableshowsubobject": ("show-sub-object",),
    "curdrawableplaysound": ("play-sound", "audio_intents"),
    "curdrawablehidesubobjectpermanently": ("hide-sub-object-permanently",),
    "curdrawableshowsubobjectpermanently": ("show-sub-object-permanently",),
    "curdrawablehidemodule": ("hide-module",),
    "curdrawableshowmodule": ("show-module",),
    "curdrawablesettransitionanimstate": ("set-transition-animation-state", "transition_anim_state"),
    "curdrawableallowtocontinue": ("allow-to-continue",),
}

_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_.-]*$")
_FIELD_IDENT = re.compile(r"^(?:[A-Za-z_][A-Za-z0-9_.%-]*|[0-9]+)$")
_DIRECTIVE = re.compile(r"^#\s*([A-Za-z_][A-Za-z0-9_]*)")
_SCRIPT_CALL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*\(")
_STRING_TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_./\\:+-]*")
# Coverage evidence is intentionally limited to ordinary, single-line GDScript
# string literals.  Allowing ``[^\"\\]`` to cross a newline made an unmatched
# quote in a comment consume arbitrary later source, hiding real executable
# literals (AllowNeutralInside was the observed retail-field false negative).
# Triple-quoted strings are documentation rather than exact executable-field
# evidence here, so failing closed on them is deliberate.
_GDSCRIPT_STRING = re.compile(
    r'(?:"((?:\\.|[^"\\\r\n])*)"|\'((?:\\.|[^\'\\\r\n])*)\')'
)
_ASSET_FIELD = re.compile(
    r"(?:model|animation|texture|image|icon|portrait|sound|voice|music|audio|"
    r"video|movie|fx|particle|shader|cursor|font|apt|w3d|ocl)",
    re.IGNORECASE,
)
_ASSET_EXTENSIONS = {
    ".apt", ".const", ".dds", ".ealayer3", ".jpg", ".mp3", ".png",
    ".scb", ".tga", ".v3", ".vp6", ".wav", ".w3d",
}
_TOKEN_STOP = {
    "yes", "no", "true", "false", "none", "all", "default", "loop",
    "once", "primary", "secondary", "tertiary", "idle", "user_1",
    "user_2", "user_3", "user_4", "user_5", "user_6", "user_7", "user_8",
}


@dataclass(frozen=True, slots=True)
class CoverageLine:
    number: int
    indent: int
    text: str


def coverage_lines(source: bytes) -> tuple[CoverageLine, ...]:
    """Lex retail INI without dropping a document on a malformed quote.

    The shipping particle lexer intentionally rejects RotWK ``credits.ini``'s
    unterminated quote.  A coverage census must retain that line, so this
    wrapper uses the strict lexer normally and falls back to raw CP1252 lines,
    preserving the malformed line verbatim instead of guessing a correction.
    """

    if source.startswith(b"\xef\xbb\xbf"):
        source = source[3:]
    try:
        return tuple(CoverageLine(line.number, line.indent, line.text) for line in strict_ini_lines(source))
    except ValueError as strict_error:
        if "unterminated quoted value" not in str(strict_error):
            raise
    result: list[CoverageLine] = []
    for number, raw in enumerate(source.decode("cp1252").splitlines(), start=1):
        try:
            uncommented = _strip_comment(raw, number)
        except ValueError:
            uncommented = raw.rstrip()
        leading = uncommented[: len(uncommented) - len(uncommented.lstrip(" \t"))]
        text = uncommented[len(leading):].strip()
        if text:
            result.append(CoverageLine(number, _indent_width(leading), text))
    return tuple(result)


def json_text(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2) + "\n"


def jsonl_text(rows: Iterable[Mapping[str, Any]]) -> str:
    return "".join(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n" for row in rows)


def csv_text(headers: list[str], rows: Iterable[Iterable[Any]]) -> str:
    stream = io.StringIO(newline="")
    writer = csv.writer(stream, lineterminator="\n")
    writer.writerow(headers)
    writer.writerows(rows)
    return stream.getvalue()


def file_sha256(path: Path) -> str:
    digest = sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def _split_assignment(text: str) -> tuple[str, str] | None:
    if "=" not in text:
        return None
    key, value = text.split("=", 1)
    key = key.strip()
    if not key or not _FIELD_IDENT.fullmatch(key):
        return None
    return key, value.strip()


def _unquote_gd(value: str) -> str:
    """Decode only GDScript escapes without Python's permissive codec.

    ``unicode_escape`` both warns on ordinary retail-looking backslash-q text and
    can reinterpret non-ASCII UTF-8 bytes.  Coverage needs stable exact terms,
    so unknown escapes are retained verbatim and the small GDScript escape set
    is handled explicitly.
    """

    escapes = {"n": "\n", "r": "\r", "t": "\t", '"': '"', "'": "'", "\\": "\\"}
    result: list[str] = []
    index = 0
    while index < len(value):
        if value[index] != "\\" or index + 1 >= len(value):
            result.append(value[index])
            index += 1
            continue
        escaped = value[index + 1]
        replacement = escapes.get(escaped)
        if replacement is None:
            result.extend(("\\", escaped))
        else:
            result.append(replacement)
        index += 2
    return "".join(result)


def gdscript_strings(paths: Iterable[Path]) -> dict[str, list[str]]:
    index: dict[str, list[str]] = {}
    for path in sorted(paths, key=lambda item: item.as_posix().casefold()):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            text = path.read_text(encoding="utf-8", errors="replace")
        relative = path.relative_to(ROOT).as_posix()
        for match in _GDSCRIPT_STRING.finditer(text):
            value = _unquote_gd(match.group(1) if match.group(1) is not None else match.group(2))
            folded = value.casefold()
            samples = index.setdefault(folded, [])
            if relative not in samples and len(samples) < 8:
                samples.append(relative)
    return index


def gdscript_function_symbols(paths: Iterable[Path]) -> dict[str, list[str]]:
    """Index executable GDScript function names by compact identifier.

    Module consumers intentionally use snake_case function names while retail
    module kinds are CamelCase (``PhysicsBehavior`` versus
    ``_step_physics_bodies``).  Quoted-string-only evidence therefore hid real
    consumers.  Restrict this index to function declarations; comments and
    arbitrary identifier mentions cannot promote coverage.
    """

    index: dict[str, list[str]] = {}
    declaration = re.compile(r"(?m)^\s*func\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")
    for path in sorted(paths, key=lambda item: item.as_posix().casefold()):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            text = path.read_text(encoding="utf-8", errors="replace")
        relative = path.relative_to(ROOT).as_posix()
        for match in declaration.finditer(text):
            compact = re.sub(r"[^a-z0-9]", "", match.group(1).casefold())
            samples = index.setdefault(compact, [])
            if relative not in samples and len(samples) < 8:
                samples.append(relative)
    return index


def module_runtime_symbol_files(
    module_name: str, symbols: Mapping[str, list[str]]
) -> list[str]:
    """Return executable consumer evidence for one retail module kind.

    A suffix-free module stem must occur in a function that also declares a
    consumer action.  The test ledger separately requires an exact authored
    module-name occurrence, so a helper alone can never become runtime-tested.
    """

    compact = re.sub(r"[^a-z0-9]", "", module_name.casefold())
    stems = {compact}
    for suffix in ("clientbehavior", "behavior", "update"):
        if compact.endswith(suffix) and len(compact) > len(suffix):
            stems.add(compact[: -len(suffix)])
    actions = ("attach", "step", "apply", "trigger", "spawn", "execute", "process")
    matches: list[str] = []
    for symbol, files in symbols.items():
        if not any(action in symbol for action in actions):
            continue
        if not any(len(stem) >= 7 and stem in symbol for stem in stems):
            continue
        for source in files:
            if source not in matches and len(matches) < 8:
                matches.append(source)
    return matches


class _StringCollector(ast.NodeVisitor):
    def __init__(self) -> None:
        self.values: set[str] = set()

    def _visit_body_without_docstring(self, body: list[ast.stmt]) -> None:
        start = 1 if body and isinstance(body[0], ast.Expr) and isinstance(body[0].value, ast.Constant) and isinstance(body[0].value.value, str) else 0
        for node in body[start:]:
            self.visit(node)

    def visit_Module(self, node: ast.Module) -> None:  # noqa: N802
        self._visit_body_without_docstring(node.body)

    def visit_ClassDef(self, node: ast.ClassDef) -> None:  # noqa: N802
        for deco in node.decorator_list:
            self.visit(deco)
        self._visit_body_without_docstring(node.body)

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:  # noqa: N802
        for deco in node.decorator_list:
            self.visit(deco)
        self._visit_body_without_docstring(node.body)

    visit_AsyncFunctionDef = visit_FunctionDef

    def visit_Constant(self, node: ast.Constant) -> None:  # noqa: N802
        if isinstance(node.value, str):
            self.values.add(node.value)


def python_strings(paths: Iterable[Path]) -> dict[str, list[str]]:
    index: dict[str, list[str]] = {}
    for path in sorted(paths, key=lambda item: item.as_posix().casefold()):
        tree = ast.parse(path.read_text(encoding="utf-8-sig"), filename=str(path))
        collector = _StringCollector()
        collector.visit(tree)
        relative = path.relative_to(ROOT).as_posix()
        for value in collector.values:
            folded = value.casefold()
            samples = index.setdefault(folded, [])
            if relative not in samples and len(samples) < 8:
                samples.append(relative)
    return index


@dataclass(slots=True)
class SelectedIndex:
    pack_ids: list[str] = field(default_factory=list)
    missing_packs: list[str] = field(default_factory=list)
    keys: dict[str, list[str]] = field(default_factory=dict)
    values: dict[str, list[str]] = field(default_factory=dict)
    object_ids: dict[str, list[str]] = field(default_factory=dict)
    module_statuses: dict[str, Counter[str]] = field(default_factory=lambda: defaultdict(Counter))
    file_stems: dict[str, list[str]] = field(default_factory=dict)
    source_sites: dict[tuple[str, int], list[str]] = field(default_factory=dict)
    json_files: int = 0
    pack_addresses: list[dict[str, Any]] = field(default_factory=list)

    def _add(self, index: dict[str, list[str]], value: str, source: str) -> None:
        folded = value.casefold()
        samples = index.setdefault(folded, [])
        if source not in samples and len(samples) < 8:
            samples.append(source)

    def walk(self, value: Any, source: str) -> None:
        if isinstance(value, dict):
            source_ini = value.get("sourceIni")
            source_line = value.get("line")
            if isinstance(source_ini, str) and isinstance(source_line, int):
                source_key = (
                    source_ini.replace("\\", "/").casefold(), source_line
                )
                source_samples = self.source_sites.setdefault(source_key, [])
                if source not in source_samples and len(source_samples) < 8:
                    source_samples.append(source)
            provenance = value.get("provenance")
            if isinstance(provenance, Mapping):
                virtual_path = provenance.get("virtualPath")
                provenance_line = provenance.get("line")
                if isinstance(virtual_path, str) and isinstance(provenance_line, int):
                    provenance_key = (
                        virtual_path.replace("\\", "/").casefold(),
                        provenance_line,
                    )
                    provenance_samples = self.source_sites.setdefault(
                        provenance_key, []
                    )
                    if source not in provenance_samples and len(provenance_samples) < 8:
                        provenance_samples.append(source)
            module = value.get("module")
            kind = value.get("kind")
            runtime_status = value.get("runtimeStatus")
            module_name = module if isinstance(module, str) else None
            if module_name and isinstance(runtime_status, str):
                self.module_statuses[module_name.casefold()][runtime_status.casefold()] += 1
            for key, child in value.items():
                self._add(self.keys, str(key), source)
                if key == "objectId" and isinstance(child, str):
                    self._add(self.object_ids, child, source)
                if key in {"module", "kind"} and isinstance(child, str):
                    self.module_statuses[child.casefold()]["descriptor-reference"] += 1
                self.walk(child, source)
        elif isinstance(value, list):
            for child in value:
                self.walk(child, source)
        elif isinstance(value, str) and len(value) <= 1024:
            self._add(self.values, value, source)


def _pack_applies_to_game(pack_id: str, game: str | None) -> bool:
    if game is None or game == "rotwk":
        # RotWK is a layered installation and the live selection intentionally
        # mounts BFME2 supplements beneath RotWK packs.
        return True
    if game == "bfme2":
        return pack_id.casefold().startswith("bfme2-")
    raise ValueError(f"unknown game selection scope: {game}")


def build_selected_index(
    selection_path: Path, *, game: str | None = None
) -> SelectedIndex:
    value = json.loads(selection_path.read_text(encoding="utf-8"))
    ids = [
        pack_id
        for pack_id in [value["activePack"], *value.get("supplementalPacks", [])]
        if _pack_applies_to_game(str(pack_id), game)
    ]
    result = SelectedIndex(pack_ids=ids)
    pack_root = selection_path.parent
    for pack_id in ids:
        directory = pack_root / Path(pack_id)
        if not directory.is_dir():
            result.missing_packs.append(pack_id)
            continue
        for path in sorted(directory.rglob("*"), key=lambda item: item.as_posix().casefold()):
            if not path.is_file():
                continue
            relative = f"{pack_id}/{path.relative_to(directory).as_posix()}"
            result._add(result.file_stems, path.stem, relative)
            if path.suffix.casefold() != ".json":
                continue
            result.json_files += 1
            try:
                payload = json.loads(path.read_text(encoding="utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise ValueError(f"selected pack JSON is unreadable: {relative}: {exc}") from exc
            result.walk(payload, relative)
        _pack_name, separator, declared = pack_id.rpartition("/")
        actual = bundle_digest(directory)
        result.pack_addresses.append({
            "packId": pack_id,
            "declared": declared if separator else None,
            "actual": actual,
            "honest": bool(separator and declared == actual),
        })
    return result


@dataclass(slots=True)
class Aggregate:
    spellings: Counter[str] = field(default_factory=Counter)
    sites: int = 0
    documents: set[str] = field(default_factory=set)
    samples: list[dict[str, Any]] = field(default_factory=list)
    category_sites: Counter[str] = field(default_factory=Counter)

    def add(
        self,
        spelling: str,
        path: str,
        line: int,
        *,
        categories: Iterable[str] = (),
        limit: int = 8,
    ) -> None:
        self.spellings[spelling] += 1
        self.sites += 1
        self.documents.add(path.casefold())
        self.category_sites.update(set(categories))
        if len(self.samples) < limit:
            self.samples.append({"path": path, "line": line})

    @property
    def display(self) -> str:
        return min(self.spellings, key=lambda item: (-self.spellings[item], item.casefold(), item))


@dataclass(slots=True)
class GameScan:
    game: str
    documents: list[dict[str, Any]] = field(default_factory=list)
    definitions: list[dict[str, Any]] = field(default_factory=list)
    assignment_sites: list[dict[str, Any]] = field(default_factory=list)
    nested_sites: list[dict[str, Any]] = field(default_factory=list)
    script_call_sites: list[dict[str, Any]] = field(default_factory=list)
    directive_sites: list[dict[str, Any]] = field(default_factory=list)
    object_rows: list[dict[str, Any]] = field(default_factory=list)
    asset_references: list[dict[str, Any]] = field(default_factory=list)
    unknown_lines: list[dict[str, Any]] = field(default_factory=list)
    top_kinds: dict[str, Aggregate] = field(default_factory=dict)
    fields: dict[tuple[str, str], Aggregate] = field(default_factory=dict)
    nested: dict[tuple[str, str], Aggregate] = field(default_factory=dict)
    script_calls: dict[tuple[str, str], Aggregate] = field(default_factory=dict)
    directives: Counter[str] = field(default_factory=Counter)
    line_kinds: Counter[str] = field(default_factory=Counter)
    module_sites: Counter[str] = field(default_factory=Counter)
    meaningful_lines: int = 0


def _aggregate(mapping: dict[Any, Aggregate], key: Any) -> Aggregate:
    row = mapping.get(key)
    if row is None:
        row = mapping[key] = Aggregate()
    return row


def _site_categories(
    feature_kind: str,
    feature_name: str,
    source_ini: str,
    root_kind: str,
    root_name: str | None,
    object_categories: Mapping[tuple[str, str], frozenset[str]],
) -> tuple[str, ...]:
    """Classify one authored site without lexical neutral-mob false positives.

    ``Neutral`` and ``Civilian`` occur in diplomacy, living-world colours,
    weapon allegiance switches, and audio names.  Those are not neutral mobs.
    Conversely, ordinary fields such as ``MaxHealth`` inside a Warg or lair
    are part of the neutral-mob surface even though the field name contains no
    family keyword.  Bind that category to the exact authored Object family;
    retain the existing lexical policy for every other risk category.
    """

    categories = set(
        classify_feature_categories(feature_kind, feature_name, source_ini)
    )
    object_root = root_name is not None and root_kind.casefold() in {
        "object",
        "childobject",
        "objectreskin",
    }
    categories.discard("neutral-mobs")
    if object_root:
        # Object fields such as SpecialPowerTemplate and VoiceEnterUnitShip do
        # not make every owning unit a hero, spellbook, or ship. Bind these
        # object-family lanes to the resolved Object identity instead.
        categories.difference_update({"hero-abilities", "ships", "spellbooks"})
        exact = object_categories.get(
            (source_ini.casefold(), root_name.casefold()), frozenset()
        )
        categories.update(
            set(exact)
            & {"neutral-mobs", "hero-abilities", "ships", "spellbooks"}
        )
    return tuple(sorted(categories))


def _feature_evidence(
    term: str,
    importer: Mapping[str, list[str]],
    selected: SelectedIndex,
    runtime: Mapping[str, list[str]],
    tests: Mapping[str, list[str]],
    *,
    descriptor_key: bool = False,
    aliases: Iterable[str] = (),
) -> dict[str, Any]:
    folded = term.casefold()
    candidates = tuple(dict.fromkeys((folded, *(value.casefold() for value in aliases))))

    def files_for(index: Mapping[str, list[str]]) -> list[str]:
        result: list[str] = []
        for candidate in candidates:
            for path in index.get(candidate, []):
                if path not in result:
                    result.append(path)
        return result[:8]

    importer_files = files_for(importer)
    selected_files = files_for(selected.keys if descriptor_key else selected.values)
    runtime_files = files_for(runtime)
    test_files = files_for(tests)
    return {
        "status": evidence_status(bool(importer_files), bool(selected_files), bool(runtime_files), bool(test_files)),
        "importerMentioned": bool(importer_files),
        "descriptorEmitted": bool(selected_files),
        "runtimeMentioned": bool(runtime_files),
        "runtimeTested": bool(test_files),
        "evidence": {
            "importer": importer_files,
            "descriptors": selected_files,
            "runtime": runtime_files,
            "tests": test_files,
        },
        "warning": "static evidence only; not a retail behavior-parity claim",
    }


def _site_descriptor_evidence(
    sites: Iterable[Mapping[str, Any]], selected: SelectedIndex
) -> dict[str, Any]:
    """Return exact selected-descriptor provenance coverage for authored sites."""

    materialized = list(sites)
    mapped = 0
    files: list[str] = []
    category_counts: dict[str, Counter[str]] = defaultdict(Counter)
    for site in materialized:
        path = str(site.get("sourceIni", "")).replace("\\", "/").casefold()
        line = site.get("line")
        if not path or isinstance(line, bool) or not isinstance(line, int):
            continue
        matches = selected.source_sites.get((path, line), [])
        if matches:
            mapped += 1
        for category in site.get("categories", []):
            category_counts[str(category)]["total"] += 1
            category_counts[str(category)]["mapped" if matches else "unmapped"] += 1
        for source in matches:
            if source not in files and len(files) < 8:
                files.append(source)
    total = len(materialized)
    return {
        "descriptorSiteCounts": {
            "mapped": mapped,
            "unmapped": total - mapped,
            "total": total,
        },
        "descriptorSitesComplete": bool(total and mapped == total),
        "descriptorSiteEvidence": files,
        "descriptorCategorySiteCounts": {
            category: {
                "mapped": counts["mapped"],
                "unmapped": counts["unmapped"],
                "total": counts["total"],
            }
            for category, counts in sorted(category_counts.items())
        },
    }


def _annotate_site_descriptor_evidence(
    sites: Iterable[dict[str, Any]], selected: SelectedIndex
) -> None:
    """Put the exact mapping decision on every emitted authored-site receipt."""

    for site in sites:
        path = str(site.get("sourceIni", "")).replace("\\", "/").casefold()
        line = site.get("line")
        matches = (
            selected.source_sites.get((path, line), [])
            if path and isinstance(line, int) and not isinstance(line, bool)
            else []
        )
        site["descriptorSiteMapped"] = bool(matches)
        site["descriptorSiteEvidence"] = list(matches[:8])


def _annotate_exact_site_statuses(
    sites: Iterable[dict[str, Any]], evidence: Mapping[str, Any]
) -> None:
    """Apply the evidence ladder independently to each authored source site."""

    for site in sites:
        site["siteCoverageStatus"] = evidence_status(
            bool(evidence.get("importerMentioned")),
            bool(site.get("descriptorSiteMapped")),
            bool(evidence.get("runtimeMentioned")),
            bool(evidence.get("runtimeTested")),
        )


def _with_site_descriptor_evidence(
    evidence: dict[str, Any], site_evidence: Mapping[str, Any]
) -> dict[str, Any]:
    """Replace broad key/value matching with exact per-site provenance proof."""

    result = dict(evidence)
    exact = bool(site_evidence.get("descriptorSitesComplete", False))
    result["descriptorEmitted"] = exact
    result["descriptorSiteCounts"] = dict(
        site_evidence.get("descriptorSiteCounts", {})
    )
    result["descriptorSitesComplete"] = exact
    result["descriptorCategorySiteCounts"] = dict(
        site_evidence.get("descriptorCategorySiteCounts", {})
    )
    nested = dict(result.get("evidence", {}))
    nested["descriptors"] = list(site_evidence.get("descriptorSiteEvidence", []))
    result["evidence"] = nested
    result["status"] = evidence_status(
        bool(result.get("importerMentioned")),
        exact,
        bool(result.get("runtimeMentioned")),
        bool(result.get("runtimeTested")),
    )
    return result


def _category_gap_sites(row: Mapping[str, Any], category: str) -> int:
    sites = int(row.get("categorySites", {}).get(category, 0))
    if sites <= 0:
        return 0
    # Importer/runtime evidence is signature-wide. If either link is absent,
    # every category site remains below runtime-mentioned (the category table's
    # documented threshold). When both links exist, count only exact source
    # sites absent from selected descriptors instead of penalizing mapped
    # siblings in the same signature. The stricter --require-complete gate still
    # requires runtime-tested at the signature level.
    if not (
        row.get("importerMentioned")
        and row.get("runtimeMentioned")
    ):
        return sites
    category_receipts = row.get("descriptorCategorySiteCounts", {}).get(
        category, {}
    )
    return int(category_receipts.get("unmapped", sites))


def _asset_type(field_name: str) -> str:
    folded = field_name.casefold()
    for label, terms in (
        ("model", ("model", "w3d")),
        ("animation", ("animation",)),
        ("texture-image", ("texture", "image", "icon", "portrait")),
        ("audio", ("sound", "voice", "music", "audio")),
        ("video", ("video", "movie")),
        ("effect-reference", ("fx", "particle", "ocl")),
        ("ui", ("cursor", "font", "apt")),
    ):
        if any(term in folded for term in terms):
            return label
    return "asset-candidate"


def _catalog_indexes(catalog: InstallCatalog) -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    exact: dict[str, list[str]] = defaultdict(list)
    stems: dict[str, list[str]] = defaultdict(list)
    winners: dict[str, CatalogEntry] = {}
    for entry in sorted(catalog.entries, key=lambda item: (item.precedence, item.archive.casefold(), item.name.casefold())):
        winners.setdefault(entry.key, entry)
    for entry in winners.values():
        name = entry.name.replace("\\", "/")
        exact[name.casefold()].append(name)
        path = PurePosixPath(name)
        if path.suffix.casefold() in _ASSET_EXTENSIONS:
            stems[path.stem.casefold()].append(name)
    return exact, stems


def _asset_resolution(token: str, exact: Mapping[str, list[str]], stems: Mapping[str, list[str]], definitions: set[str]) -> tuple[str, list[str]]:
    normalized = token.strip("\"'(),[]{}")
    folded = normalized.replace("\\", "/").casefold()
    if folded in definitions:
        return "ini-definition", []
    matches = exact.get(folded, [])
    if matches:
        return ("retail-asset" if len(matches) == 1 else "ambiguous-retail-asset"), matches[:8]
    stem = PurePosixPath(folded).stem
    matches = stems.get(stem, [])
    if matches:
        return ("retail-asset-stem" if len(matches) == 1 else "ambiguous-retail-stem"), matches[:8]
    return "unresolved-candidate", []


def _is_proven_retail_asset(row: Mapping[str, Any]) -> bool:
    """Separate physical BIG members from broad syntactic candidates.

    The lexical inventory intentionally sees compound values such as
    ``AnimationSound = Sound:Foo Animation:Bar Frames:1``.  Tokens like
    ``Frames:1`` must remain visible in JSONL, but they are not missing files.
    Only catalog-resolved physical members contribute to the selected-pack
    asset completeness denominator; INI definitions and unresolved syntactic
    candidates remain covered by their field/module receipts.
    """

    return str(row.get("retailResolution", "")).startswith(
        ("retail-asset", "ambiguous-retail-")
    )


def _asset_reference_statuses(rows: Iterable[Mapping[str, Any]]) -> dict[str, int]:
    materialized = list(rows)
    selected = sum(bool(row.get("selectedPackMapped")) for row in materialized)
    proven_missing = sum(
        _is_proven_retail_asset(row) and not bool(row.get("selectedPackMapped"))
        for row in materialized
    )
    return {
        "selected-pack-mapped": selected,
        "proven-retail-asset-not-selected-pack-mapped": proven_missing,
        "ini-definition-not-selected-pack-mapped": sum(
            row.get("retailResolution") == "ini-definition"
            and not bool(row.get("selectedPackMapped"))
            for row in materialized
        ),
        "unresolved-candidate-not-selected-pack-mapped": sum(
            row.get("retailResolution") == "unresolved-candidate"
            and not bool(row.get("selectedPackMapped"))
            for row in materialized
        ),
        "all-not-selected-pack-mapped": sum(
            not bool(row.get("selectedPackMapped")) for row in materialized
        ),
    }


def _object_rows(path: str, source: bytes) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for obj in parse_object_definitions(source):
        if obj.name.startswith("="):
            continue
        values: dict[str, list[str]] = defaultdict(list)
        modules: Counter[str] = Counter()
        for key, value in obj.assignments:
            values[key.casefold()].append(value)
            module = module_kind_from_assignment(key, value)
            if module:
                modules[module] += 1
        def tokens(key: str) -> list[str]:
            return [token for value in values.get(key, []) for token in value.split()]
        side = tokens("side")[-1] if tokens("side") else None
        kind_of = tokens("kindof")
        context = " ".join([path, side or "", *kind_of])
        categories = set(classify_feature_categories("object", obj.name, context))
        # The lexical category intentionally catches every Neutral/Civilian
        # feature name. Object classification is narrower: the requested lane
        # is neutral *mobs* (including their lairs), not thousands of passive
        # civilian props.
        categories.discard("neutral-mobs")
        upper_kind = {token.upper().lstrip("+-") for token in kind_of}
        if is_neutral_mob_object(
            object_id=obj.name,
            parent_id=obj.parent,
            source_ini=path,
            side=side,
            kind_of=kind_of,
        ):
            categories.add("neutral-mobs")
        if is_hero_family_object(
            object_id=obj.name,
            source_ini=path,
            side=side,
            kind_of=kind_of,
        ):
            categories.add("hero-abilities")
        if {"SHIP", "BOAT"} & {token.upper() for token in kind_of}:
            categories.add("ships")
        # Side=Neutral/Civilian alone is not a mob classification: trees,
        # bridges, ambient props, buildings, and map markers use those sides.
        # The neutral_source + mob/lair identity test above is the closed
        # criterion. Side=Creeps still qualifies because it is an explicit
        # gameplay ownership bucket, not a presentation side.
        rows.append({
            "objectId": obj.name,
            "objectKind": obj.kind,
            "parent": obj.parent,
            "sourceIni": path,
            "side": side,
            "kindOf": kind_of,
            "commandSets": tokens("commandset"),
            "buildCostExpressions": values.get("buildcost", []),
            "buildTimeExpressions": values.get("buildtime", []),
            "modules": dict(sorted(modules.items(), key=lambda item: item[0].casefold())),
            "categories": sorted(categories),
        })
    return rows


def _resolve_object_family_categories(rows: list[dict[str, Any]]) -> None:
    """Apply inherited Object identity before assigning requested families.

    ``ChildObject`` and ``ObjectReskin`` commonly omit ``Side`` and ``KindOf``.
    Classifying those rows from their local assignments alone silently removes
    every ordinary field on the child from hero, ship, and neutral-mob
    denominators.  Resolve the small family-relevant inheritance surface here;
    the ledger still preserves the directly authored values separately.

    A plain child ``KindOf`` replaces its parent's list, while +/- tokens patch
    the inherited set.  ``Side`` follows the normal child override rule.
    Duplicate parent definitions are conservatively unioned because the
    coverage ledger must not hide a possible family member.
    """

    by_name: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        by_name[str(row["objectId"]).casefold()].append(row)

    cache: dict[int, tuple[set[str], set[str]]] = {}

    def resolve(
        row: dict[str, Any], active: frozenset[int] = frozenset()
    ) -> tuple[set[str], set[str]]:
        identity = id(row)
        cached = cache.get(identity)
        if cached is not None:
            return set(cached[0]), set(cached[1])

        direct_sides = {str(row["side"])} if row.get("side") else set()
        direct_kind = [str(value) for value in row.get("kindOf", [])]
        parent_sides: set[str] = set()
        parent_kind: set[str] = set()
        parent_name = str(row.get("parent") or "").casefold()
        if parent_name and identity not in active:
            next_active = active | {identity}
            for parent in by_name.get(parent_name, []):
                parent_identity = id(parent)
                if parent_identity in next_active:
                    continue
                inherited_sides, inherited_kind = resolve(parent, next_active)
                parent_sides.update(inherited_sides)
                parent_kind.update(inherited_kind)

        sides = direct_sides or parent_sides
        if not direct_kind:
            kinds = parent_kind
        elif any(value.startswith(("+", "-")) for value in direct_kind):
            kinds = set(parent_kind)
            for value in direct_kind:
                if value.startswith("-"):
                    kinds.discard(value[1:].upper())
                else:
                    kinds.add(value.lstrip("+").upper())
        else:
            kinds = {value.upper() for value in direct_kind}

        cache[identity] = (set(sides), set(kinds))
        return sides, kinds

    for row in rows:
        sides, kinds = resolve(row)
        row["resolvedSides"] = sorted(sides, key=str.casefold)
        row["resolvedKindOf"] = sorted(kinds, key=str.casefold)
        categories = set(str(value) for value in row.get("categories", []))
        effective_sides: tuple[str | None, ...] = (
            tuple(row["resolvedSides"]) if row["resolvedSides"] else (None,)
        )
        if any(
            is_neutral_mob_object(
                object_id=str(row["objectId"]),
                parent_id=str(row.get("parent") or "") or None,
                source_ini=str(row["sourceIni"]),
                side=side,
                kind_of=row["resolvedKindOf"],
            )
            for side in effective_sides
        ):
            categories.add("neutral-mobs")
        if any(
            is_hero_family_object(
                object_id=str(row["objectId"]),
                source_ini=str(row["sourceIni"]),
                side=side,
                kind_of=row["resolvedKindOf"],
            )
            for side in effective_sides
        ):
            categories.add("hero-abilities")
        if {"SHIP", "BOAT"} & kinds:
            categories.add("ships")
        row["categories"] = sorted(categories)


def scan_game(catalog: InstallCatalog, game: str) -> GameScan:
    result = GameScan(game)
    exact_assets, stem_assets = _catalog_indexes(catalog)
    entries = effective_ini_entries(catalog.entries)
    payloads: list[tuple[CatalogEntry, bytes]] = []
    all_definition_names: set[str] = set()

    for entry in entries:
        archive = catalog.open_archive_for(entry)
        source = archive.read_entry(catalog.as_entry(entry), max_bytes=MAX_INI_BYTES)
        payloads.append((entry, source))
        result.documents.append({
            "path": entry.name,
            "archive": entry.archive,
            "precedence": entry.precedence,
            "bytes": len(source),
            "sha256": sha256(source).hexdigest(),
        })
        for line in coverage_lines(source):
            text = line.text.strip()
            if not text or text.startswith("#") or "=" in text or text.casefold() == "end" or line.indent != 0:
                continue
            parts = text.split()
            if parts and _IDENT.fullmatch(parts[0]) and len(parts) > 1:
                all_definition_names.add(parts[1].casefold())

    object_categories: dict[tuple[str, str], frozenset[str]] = {}
    for entry, source in payloads:
        rows = _object_rows(entry.name, source)
        result.object_rows.extend(rows)
    _resolve_object_family_categories(result.object_rows)
    for row in result.object_rows:
        object_categories[
            (str(row["sourceIni"]).casefold(), str(row["objectId"]).casefold())
        ] = frozenset(str(value) for value in row.get("categories", []))

    for entry, source in payloads:
        path = entry.name
        root_kind = "<global>"
        root_name: str | None = None
        root_indent = -1
        for line in coverage_lines(source):
            text = line.text.strip()
            if not text:
                continue
            result.meaningful_lines += 1
            directive = _DIRECTIVE.match(text)
            if directive:
                kind = directive.group(1)
                result.directives[kind.casefold()] += 1
                result.directive_sites.append({
                    "game": game,
                    "sourceIni": path,
                    "line": line.number,
                    "directive": kind,
                })
                result.line_kinds["directive"] += 1
                continue
            if text.casefold() in {"end", "endscript"}:
                terminal_kind = "end-script" if text.casefold() == "endscript" else "end"
                result.line_kinds[terminal_kind] += 1
                if line.indent <= root_indent:
                    root_kind, root_name, root_indent = "<global>", None, -1
                continue
            assignment = _split_assignment(text)
            if assignment is not None:
                field_name, value = assignment
                categories = _site_categories(
                    "field",
                    f"{root_kind}.{field_name}",
                    path,
                    root_kind,
                    root_name,
                    object_categories,
                )
                _aggregate(
                    result.fields, (root_kind.casefold(), field_name.casefold())
                ).add(field_name, path, line.number, categories=categories)
                result.line_kinds["assignment"] += 1
                module = module_kind_from_assignment(field_name, value)
                if module:
                    result.module_sites[module.casefold()] += 1
                value_digest = sha256(value.encode("utf-8")).hexdigest()
                result.assignment_sites.append({
                    "game": game,
                    "sourceIni": path,
                    "line": line.number,
                    "rootKind": root_kind,
                    "rootName": root_name,
                    "field": field_name,
                    "valueSha256": value_digest,
                    "valueTokenCount": len(value.split()),
                    "moduleKind": module,
                    "categories": list(categories),
                })
                if _ASSET_FIELD.search(field_name):
                    seen_tokens: set[str] = set()
                    for token in _STRING_TOKEN.findall(value):
                        folded = token.casefold()
                        if folded in _TOKEN_STOP or folded in seen_tokens:
                            continue
                        seen_tokens.add(folded)
                        resolution, candidates = _asset_resolution(token, exact_assets, stem_assets, all_definition_names)
                        result.asset_references.append({
                            "game": game,
                            "sourceIni": path,
                            "line": line.number,
                            "rootKind": root_kind,
                            "rootName": root_name,
                            "field": field_name,
                            "token": token,
                            "referenceType": _asset_type(field_name),
                            "retailResolution": resolution,
                            "retailCandidates": candidates,
                        })
                continue
            script_call = _SCRIPT_CALL.match(text)
            if script_call is not None:
                command = script_call.group(1)
                categories = _site_categories(
                    "script-call",
                    command,
                    path,
                    root_kind,
                    root_name,
                    object_categories,
                )
                _aggregate(
                    result.script_calls, (root_kind.casefold(), command.casefold())
                ).add(command, path, line.number, categories=categories)
                result.script_call_sites.append({
                    "game": game,
                    "sourceIni": path,
                    "line": line.number,
                    "rootKind": root_kind,
                    "rootName": root_name,
                    "command": command,
                    "argumentsSha256": sha256(text[text.find("("):].encode("utf-8")).hexdigest(),
                    "categories": list(categories),
                })
                result.line_kinds["script-call"] += 1
                continue
            parts = text.split()
            token = parts[0] if parts else ""
            if _IDENT.fullmatch(token):
                if line.indent == 0:
                    root_kind = token
                    root_name = parts[1] if len(parts) > 1 else None
                    root_indent = line.indent
                    categories = _site_categories(
                        "definition-kind",
                        token,
                        path,
                        root_kind,
                        root_name,
                        object_categories,
                    )
                    _aggregate(result.top_kinds, token.casefold()).add(
                        token, path, line.number, categories=categories
                    )
                    result.definitions.append({
                        "game": game,
                        "kind": token,
                        "name": root_name,
                        "sourceIni": path,
                        "line": line.number,
                        "categories": list(categories),
                    })
                    result.line_kinds["definition"] += 1
                else:
                    categories = _site_categories(
                        "nested-block",
                        f"{root_kind}.{token}",
                        path,
                        root_kind,
                        root_name,
                        object_categories,
                    )
                    _aggregate(
                        result.nested, (root_kind.casefold(), token.casefold())
                    ).add(token, path, line.number, categories=categories)
                    result.nested_sites.append({
                        "game": game,
                        "sourceIni": path,
                        "line": line.number,
                        "rootKind": root_kind,
                        "rootName": root_name,
                        "block": token,
                        "selectorSha256": sha256(" ".join(parts[1:]).encode("utf-8")).hexdigest(),
                        "categories": list(categories),
                    })
                    result.line_kinds["nested-block"] += 1
                continue
            result.line_kinds["unknown"] += 1
            result.unknown_lines.append({
                "game": game,
                "sourceIni": path,
                "line": line.number,
                "textSha256": sha256(text.encode("utf-8")).hexdigest(),
                "textLength": len(text),
                "rootKind": root_kind,
            })
    if sum(result.line_kinds.values()) != result.meaningful_lines:
        raise AssertionError(f"{game}: semantic-line accounting mismatch")
    receipt_count = sum(
        len(rows)
        for rows in (
            result.definitions,
            result.assignment_sites,
            result.nested_sites,
            result.script_call_sites,
            result.directive_sites,
            result.unknown_lines,
        )
    )
    terminal_count = sum(
        result.line_kinds.get(kind, 0) for kind in ("end", "end-script")
    )
    if receipt_count + terminal_count != result.meaningful_lines:
        raise AssertionError(f"{game}: non-terminal receipt accounting mismatch")
    return result


def _enrich_scan(
    scan: GameScan,
    importer: Mapping[str, list[str]],
    selected: SelectedIndex,
    runtime: Mapping[str, list[str]],
    tests: Mapping[str, list[str]],
) -> dict[str, Any]:
    for sites in (
        scan.definitions,
        scan.assignment_sites,
        scan.nested_sites,
        scan.script_call_sites,
        scan.directive_sites,
    ):
        _annotate_site_descriptor_evidence(sites, selected)

    definition_sites: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for site in scan.definitions:
        definition_sites[str(site["kind"]).casefold()].append(site)
    assignment_sites: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for site in scan.assignment_sites:
        assignment_sites[
            (str(site["rootKind"]).casefold(), str(site["field"]).casefold())
        ].append(site)
    nested_site_rows: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for site in scan.nested_sites:
        nested_site_rows[
            (str(site["rootKind"]).casefold(), str(site["block"]).casefold())
        ].append(site)
    script_site_rows: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for site in scan.script_call_sites:
        script_site_rows[
            (str(site["rootKind"]).casefold(), str(site["command"]).casefold())
        ].append(site)
    directive_site_rows: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for site in scan.directive_sites:
        directive_site_rows[str(site["directive"]).casefold()].append(site)

    top_rows = []
    for folded, usage in sorted(scan.top_kinds.items()):
        term = usage.display
        evidence = _with_site_descriptor_evidence(
            _feature_evidence(term, importer, selected, runtime, tests),
            _site_descriptor_evidence(definition_sites[folded], selected),
        )
        top_rows.append({
            "signature": f"definition-kind:{term}",
            "kind": term,
            "sites": usage.sites,
            "documents": len(usage.documents),
            "samples": usage.samples,
            "categories": sorted(usage.category_sites),
            "categorySites": dict(sorted(usage.category_sites.items())),
            **evidence,
        })
    field_rows = []
    for (root, folded), usage in sorted(scan.fields.items()):
        term = usage.display
        signature = f"{root}.{term}"
        evidence = _with_site_descriptor_evidence(
            _feature_evidence(
                term,
                importer,
                selected,
                runtime,
                tests,
                descriptor_key=True,
            ),
            _site_descriptor_evidence(assignment_sites[(root, folded)], selected),
        )
        field_rows.append({
            "signature": f"field:{signature}",
            "rootKind": root,
            "field": term,
            "sites": usage.sites,
            "documents": len(usage.documents),
            "samples": usage.samples,
            "categories": sorted(usage.category_sites),
            "categorySites": dict(sorted(usage.category_sites.items())),
            **evidence,
        })
    nested_rows = []
    for (root, folded), usage in sorted(scan.nested.items()):
        term = usage.display
        evidence = _with_site_descriptor_evidence(
            _feature_evidence(
                term,
                importer,
                selected,
                runtime,
                tests,
                descriptor_key=True,
            ),
            _site_descriptor_evidence(nested_site_rows[(root, folded)], selected),
        )
        nested_rows.append({
            "signature": f"nested:{root}.{term}",
            "rootKind": root,
            "block": term,
            "sites": usage.sites,
            "documents": len(usage.documents),
            "samples": usage.samples,
            "categories": sorted(usage.category_sites),
            "categorySites": dict(sorted(usage.category_sites.items())),
            **evidence,
        })

    script_rows = []
    for (root, folded), usage in sorted(scan.script_calls.items()):
        term = usage.display
        aliases = _EXECUTABLE_SCRIPT_OPERATION_ALIASES.get(term.casefold(), ())
        evidence = _with_site_descriptor_evidence(
            _feature_evidence(
                term, importer, selected, runtime, tests, aliases=aliases
            ),
            _site_descriptor_evidence(script_site_rows[(root, folded)], selected),
        )
        script_rows.append({
            "signature": f"script-call:{root}.{term}",
            "rootKind": root,
            "command": term,
            "sites": usage.sites,
            "documents": len(usage.documents),
            "samples": usage.samples,
            "categories": sorted(usage.category_sites),
            "categorySites": dict(sorted(usage.category_sites.items())),
            **({"descriptorOperationAliases": list(aliases)} if aliases else {}),
            **evidence,
        })

    directive_rows = []
    for folded, sites in sorted(scan.directives.items()):
        samples = [
            {"path": row["sourceIni"], "line": row["line"]}
            for row in scan.directive_sites
            if row["directive"].casefold() == folded
        ][:8]
        evidence = _with_site_descriptor_evidence(
            _feature_evidence(folded, importer, selected, runtime, tests),
            _site_descriptor_evidence(directive_site_rows[folded], selected),
        )
        directive_rows.append({
            "signature": f"directive:{folded}",
            "directive": folded,
            "sites": sites,
            "documents": len({row["sourceIni"].casefold() for row in scan.directive_sites if row["directive"].casefold() == folded}),
            "samples": samples,
            "categories": list(classify_feature_categories("directive", folded, "")),
            "categorySites": {
                category: sites
                for category in classify_feature_categories("directive", folded, "")
            },
            **evidence,
        })

    for row in scan.definitions:
        term = row.get("name") or row["kind"]
        row.update(
            _with_site_descriptor_evidence(
                _feature_evidence(term, importer, selected, runtime, tests),
                _site_descriptor_evidence([row], selected),
            )
        )
        _annotate_exact_site_statuses([row], row)
        if "categories" not in row:
            row["categories"] = list(
                classify_feature_categories(
                    "definition", f"{row['kind']}.{term}", row["sourceIni"]
                )
            )
    field_status = {
        (row["rootKind"].casefold(), row["field"].casefold()): (row["signature"], row["status"])
        for row in field_rows
    }
    field_evidence = {row["signature"]: row for row in field_rows}
    for row in scan.assignment_sites:
        signature, status = field_status[(row["rootKind"].casefold(), row["field"].casefold())]
        row["coverageSignature"] = signature
        row["coverageStatus"] = status
        _annotate_exact_site_statuses([row], field_evidence[signature])
    nested_status = {
        (row["rootKind"].casefold(), row["block"].casefold()): (row["signature"], row["status"])
        for row in nested_rows
    }
    nested_evidence = {row["signature"]: row for row in nested_rows}
    for row in scan.nested_sites:
        signature, status = nested_status[(row["rootKind"].casefold(), row["block"].casefold())]
        row["coverageSignature"] = signature
        row["coverageStatus"] = status
        _annotate_exact_site_statuses([row], nested_evidence[signature])
    script_status = {
        (row["rootKind"].casefold(), row["command"].casefold()): (row["signature"], row["status"])
        for row in script_rows
    }
    script_evidence = {row["signature"]: row for row in script_rows}
    for row in scan.script_call_sites:
        signature, status = script_status[(row["rootKind"].casefold(), row["command"].casefold())]
        row["coverageSignature"] = signature
        row["coverageStatus"] = status
        _annotate_exact_site_statuses([row], script_evidence[signature])
    directive_status = {row["directive"].casefold(): (row["signature"], row["status"]) for row in directive_rows}
    directive_evidence = {row["signature"]: row for row in directive_rows}
    for row in scan.directive_sites:
        signature, status = directive_status[row["directive"].casefold()]
        row["coverageSignature"] = signature
        row["coverageStatus"] = status
        _annotate_exact_site_statuses([row], directive_evidence[signature])
    for row in scan.object_rows:
        term = row["objectId"]
        selected_files = selected.object_ids.get(term.casefold(), [])
        evidence = _feature_evidence(term, importer, selected, runtime, tests)
        if selected_files:
            evidence["descriptorEmitted"] = True
            evidence["evidence"]["descriptors"] = selected_files
            evidence["status"] = evidence_status(
                evidence["importerMentioned"], True,
                evidence["runtimeMentioned"], evidence["runtimeTested"],
            )
        row.update(evidence)
    for row in scan.asset_references:
        term = row["token"]
        selected_files = selected.values.get(term.casefold(), []) or selected.file_stems.get(PurePosixPath(term).stem.casefold(), [])
        row["selectedPackMapped"] = bool(selected_files)
        row["selectedEvidence"] = selected_files

    return {
        "topLevelKinds": top_rows,
        "fields": field_rows,
        "nestedBlocks": nested_rows,
        "scriptCalls": script_rows,
        "directives": directive_rows,
    }


def _module_rows(
    scan_by_game: Mapping[str, GameScan],
    selected: SelectedIndex,
    runtime: Mapping[str, list[str]],
    runtime_symbols: Mapping[str, list[str]],
    tests: Mapping[str, list[str]],
) -> list[dict[str, Any]]:
    census = json.loads(MODULE_CENSUS.read_text(encoding="utf-8"))
    live_support = scan_module_support({member["name"].casefold() for member in census["members"]})
    rows = []
    for member in census["members"]:
        folded = member["name"].casefold()
        live_importer_status = live_support.status(folded)
        live_consumed_by = list(live_support.consumed.get(folded, ()))
        live_refused_by = list(live_support.refused.get(folded, ()))
        census_drift = (
            live_importer_status != member["status"]
            or live_consumed_by != member.get("consumedBy", [])
            or live_refused_by != member.get("refusedBy", [])
        )
        descriptor_statuses = dict(selected.module_statuses.get(folded, {}))
        runtime_files = list(runtime.get(folded, []))
        for source in module_runtime_symbol_files(member["name"], runtime_symbols):
            if source not in runtime_files and len(runtime_files) < 8:
                runtime_files.append(source)
        test_files = tests.get(folded, [])
        if live_importer_status in {"refused", "unhandled"}:
            status = live_importer_status
        elif descriptor_statuses.get("deferred", 0) or descriptor_statuses.get("opaque", 0):
            status = "deferred-descriptor"
        elif descriptor_statuses and test_files and runtime_files:
            status = "runtime-tested"
        elif descriptor_statuses and runtime_files:
            status = "runtime-mentioned"
        elif descriptor_statuses:
            status = "descriptor-emitted"
        else:
            status = "importer-only"
        rows.append({
            "name": member["name"],
            "classification": member["classification"],
            "classificationNote": member["classificationNote"],
            "importerStatus": live_importer_status,
            "committedImporterStatus": member["status"],
            "committedCensusDrift": census_drift,
            "coverageStatus": status,
            "consumedBy": live_consumed_by,
            "refusedBy": live_refused_by,
            "declarationSites": member["declarationSites"],
            "liveScanSites": {game: scan_by_game[game].module_sites.get(folded, 0) for game in GAMES},
            "descriptorStatuses": descriptor_statuses,
            "runtimeEvidence": runtime_files,
            "testEvidence": test_files,
            "families": member.get("families", []),
            "warning": "consumed/emitted/mentioned/tested is not semantic parity",
            "categories": list(classify_feature_categories("module", member["name"], member["classificationNote"])),
        })
    return rows


def _object_union(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[str, dict[str, Any]] = {}
    for row in rows:
        key = row["objectId"].casefold()
        current = grouped.get(key)
        if current is None:
            current = grouped[key] = {
                "objectId": row["objectId"],
                "definitions": 0,
                "sourceIni": [],
                "objectKinds": set(),
                "parents": set(),
                "sides": set(),
                "kindOf": set(),
                "resolvedSides": set(),
                "resolvedKindOf": set(),
                "commandSets": set(),
                "modules": Counter(),
                "categories": set(),
                "descriptorEmitted": False,
                "descriptorEvidence": [],
            }
        current["definitions"] += 1
        if row["sourceIni"] not in current["sourceIni"]:
            current["sourceIni"].append(row["sourceIni"])
        current["objectKinds"].add(row["objectKind"])
        if row["parent"]:
            current["parents"].add(row["parent"])
        if row["side"]:
            current["sides"].add(row["side"])
        current["kindOf"].update(row["kindOf"])
        current["resolvedSides"].update(row.get("resolvedSides", []))
        current["resolvedKindOf"].update(row.get("resolvedKindOf", []))
        current["commandSets"].update(row["commandSets"])
        current["modules"].update(row["modules"])
        current["categories"].update(row["categories"])
        if bool(row.get("descriptorEmitted", False)):
            current["descriptorEmitted"] = True
            for sample in row.get("evidence", {}).get("descriptors", []):
                if sample not in current["descriptorEvidence"] and len(current["descriptorEvidence"]) < 8:
                    current["descriptorEvidence"].append(sample)
    result = []
    for current in grouped.values():
        for key in (
            "objectKinds",
            "parents",
            "sides",
            "kindOf",
            "resolvedSides",
            "resolvedKindOf",
            "commandSets",
            "categories",
        ):
            current[key] = sorted(current[key], key=str.casefold)
        current["sourceIni"].sort(key=str.casefold)
        current["modules"] = dict(sorted(current["modules"].items(), key=lambda item: item[0].casefold()))
        current["coverageStatus"] = "descriptor-emitted" if current["descriptorEmitted"] else "object-not-emitted"
        result.append(current)
    return sorted(result, key=lambda row: row["objectId"].casefold())


def _status_counts(rows: Iterable[Mapping[str, Any]], key: str = "status") -> dict[str, int]:
    return dict(sorted(Counter(str(row.get(key, "unknown")) for row in rows).items()))


def _completion_summary(
    feature_rows: Mapping[str, list[Mapping[str, Any]]],
    site_rows: Mapping[str, list[Mapping[str, Any]]],
    modules: list[Mapping[str, Any]],
    objects: Mapping[str, list[Mapping[str, Any]]],
    missing_assets: Mapping[str, int],
    unknown_lines: Mapping[str, int],
    *,
    dishonest_pack_addresses: int,
) -> dict[str, Any]:
    """Expose non-conflated static completion denominators."""

    all_features = [row for game in GAMES for row in feature_rows[game]]
    all_sites = [row for game in GAMES for row in site_rows[game]]
    all_objects = [row for game in GAMES for row in objects[game]]
    result = {
        "featureSignatures": {
            "total": len(all_features),
            "incomplete": sum(
                str(row.get("status", "")) != "runtime-tested"
                for row in all_features
            ),
        },
        "authoredSites": {
            "total": len(all_sites),
            "incomplete": sum(
                str(row.get("siteCoverageStatus", "")) != "runtime-tested"
                for row in all_sites
            ),
        },
        "modules": {
            "total": len(modules),
            "incomplete": sum(
                str(row.get("coverageStatus", "")) != "runtime-tested"
                for row in modules
            ),
        },
        "objects": {
            "total": len(all_objects),
            "notEmitted": sum(
                not bool(row.get("descriptorEmitted")) for row in all_objects
            ),
        },
        "provenRetailAssetsNotSelected": sum(int(missing_assets[g]) for g in GAMES),
        "unknownSemanticLines": sum(int(unknown_lines[g]) for g in GAMES),
        "dishonestPackAddresses": int(dishonest_pack_addresses),
    }
    result["staticCoverageComplete"] = not any(
        (
            int(result["featureSignatures"]["incomplete"]),
            int(result["authoredSites"]["incomplete"]),
            int(result["modules"]["incomplete"]),
            int(result["objects"]["notEmitted"]),
            int(result["provenRetailAssetsNotSelected"]),
            int(result["unknownSemanticLines"]),
            int(result["dishonestPackAddresses"]),
        )
    )
    return result


def _object_slice_summary(rows: Iterable[Mapping[str, Any]]) -> dict[str, dict[str, int]]:
    materialized = list(rows)
    predicates = {
        "heroFamily": lambda row: "hero-abilities" in row["categories"],
        "neutralMobFamily": lambda row: "neutral-mobs" in row["categories"],
        "shipNameOrPathFamily": lambda row: "ships" in row["categories"],
        "authoredKindOfShip": lambda row: "SHIP" in {token.upper().lstrip("+-") for token in row["kindOf"]},
        "effectiveKindOfShip": lambda row: "SHIP" in {
            token.upper().lstrip("+-") for token in row.get("resolvedKindOf", [])
        },
    }
    result: dict[str, dict[str, int]] = {}
    for label, predicate in predicates.items():
        selected = [row for row in materialized if predicate(row)]
        result[label] = {
            "objects": len(selected),
            "descriptorEmitted": sum(bool(row["descriptorEmitted"]) for row in selected),
            "objectNotEmitted": sum(not row["descriptorEmitted"] for row in selected),
        }
    return result


def md_table(headers: list[str], rows: Iterable[Iterable[Any]]) -> str:
    output = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    for row in rows:
        output.append("| " + " | ".join(str(value).replace("|", "\\|").replace("\n", " ") for value in row) + " |")
    return "\n".join(output)


def _completion_markdown(completion: Mapping[str, Any]) -> str:
    """Render the exact, non-conflated completion denominators for humans."""

    rows = [
        (
            "Feature signatures",
            completion["featureSignatures"]["total"],
            completion["featureSignatures"]["incomplete"],
        ),
        (
            "Authored semantic sites",
            completion["authoredSites"]["total"],
            completion["authoredSites"]["incomplete"],
        ),
        (
            "Object module kinds",
            completion["modules"]["total"],
            completion["modules"]["incomplete"],
        ),
        (
            "Effective objects",
            completion["objects"]["total"],
            completion["objects"]["notEmitted"],
        ),
        (
            "Catalog-proven retail assets",
            "-",
            completion["provenRetailAssetsNotSelected"],
        ),
        (
            "Unknown semantic lines",
            "-",
            completion["unknownSemanticLines"],
        ),
        (
            "Selected pack addresses",
            "-",
            completion["dishonestPackAddresses"],
        ),
    ]
    return "\n".join(
        [
            "## Exact completion accounting",
            "",
            "These denominators are deliberately separate: a mapped signature does not hide",
            "unmapped authored occurrences, and an emitted object does not prove its behavior.",
            "",
            md_table(["Surface", "Total", "Incomplete"], rows),
            "",
            "Static coverage complete: "
            + ("**yes**" if completion["staticCoverageComplete"] else "**no**"),
            "",
        ]
    )


def _summary(
    scans: Mapping[str, GameScan],
    matrices: Mapping[str, dict[str, Any]],
    module_rows: list[dict[str, Any]],
    objects: Mapping[str, list[dict[str, Any]]],
    selected: SelectedIndex,
    catalog_paths: Mapping[str, Path],
    completion: Mapping[str, Any],
) -> str:
    drifted_packs = [row for row in selected.pack_addresses if not row["honest"]]
    lines = [
        "# Retail INI coverage ledger",
        "",
        "Generated deterministically by `tools/retail-ini-coverage.py` from the effective",
        "BIG-archive winners. This report inventories static mapping evidence; no status in",
        "this report is a claim of 1:1 gameplay parity.",
        "",
        "## Corpus identity",
        "",
        md_table(
            ["Game", "Catalog SHA-256", "Documents", "Bytes", "Semantic lines", "Unknown lines"],
            [[
                GAME_LABELS[game], file_sha256(catalog_paths[game]), len(scans[game].documents),
                sum(row["bytes"] for row in scans[game].documents), scans[game].meaningful_lines,
                len(scans[game].unknown_lines),
            ] for game in GAMES],
        ),
        "",
        f"Selected pack identity: `{selected.pack_ids[0]}` plus {len(selected.pack_ids) - 1} supplemental packs; "
        f"{selected.json_files:,} JSON documents indexed; {len(selected.missing_packs)} missing packs.",
        "",
        f"Selected pack address integrity: {len(selected.pack_addresses) - len(drifted_packs)}/{len(selected.pack_addresses)} honest; "
        f"{len(drifted_packs)} drifted. "
        + ("Drift: " + ", ".join(f"`{row['packId']}` actual `{row['actual']}`" for row in drifted_packs) + "." if drifted_packs else ""),
        "",
        "## Coverage levels",
        "",
        "`unmapped` < `importer-mentioned` < `descriptor-emitted` < `runtime-mentioned` < `runtime-tested`.",
        "These are evidence levels only. In particular, exact string mentions and passing unit runners",
        "do not prove retail timing, targeting, state transitions, save/load, lockstep, or visual/audio parity.",
        "",
        _completion_markdown(completion),
        "## Surface totals",
        "",
        md_table(
            ["Game", "Definitions", "Top-level kinds", "Assignment sites", "Field signatures", "Nested signatures", "Script calls", "Unique objects", "Asset/effect refs"],
            [[
                GAME_LABELS[game], len(scans[game].definitions), len(matrices[game]["topLevelKinds"]),
                len(scans[game].assignment_sites), len(matrices[game]["fields"]), len(matrices[game]["nestedBlocks"]), len(matrices[game]["scriptCalls"]),
                len(objects[game]), len(scans[game].asset_references),
            ] for game in GAMES],
        ),
        "",
        "## Static evidence distribution",
        "",
    ]
    for game in GAMES:
        lines += [f"### {GAME_LABELS[game]}", ""]
        for label, key in (("Top-level kinds", "topLevelKinds"), ("Field signatures", "fields"), ("Nested blocks", "nestedBlocks"), ("Script calls", "scriptCalls"), ("Directives", "directives")):
            counts = _status_counts(matrices[game][key])
            lines.append(f"- {label}: " + ", ".join(f"{name}={count:,}" for name, count in counts.items()))
        object_counts = Counter("descriptor-emitted" if row["descriptorEmitted"] else "object-not-emitted" for row in objects[game])
        lines.append("- Objects: " + ", ".join(f"{name}={count:,}" for name, count in sorted(object_counts.items())))
        lines.append("")
    lines += [
        "## Requested object slices",
        "",
        md_table(
            [
                "Game",
                "Hero family",
                "Hero missing",
                "Neutral-mob family",
                "Neutral-mob missing",
                "Ship name/path family",
                "Ship family missing",
                "Authored KindOf SHIP",
                "Authored SHIP missing",
                "Effective KindOf SHIP",
                "Effective SHIP missing",
            ],
            [[
                GAME_LABELS[game],
                _object_slice_summary(objects[game])["heroFamily"]["objects"],
                _object_slice_summary(objects[game])["heroFamily"]["objectNotEmitted"],
                _object_slice_summary(objects[game])["neutralMobFamily"]["objects"],
                _object_slice_summary(objects[game])["neutralMobFamily"]["objectNotEmitted"],
                _object_slice_summary(objects[game])["shipNameOrPathFamily"]["objects"],
                _object_slice_summary(objects[game])["shipNameOrPathFamily"]["objectNotEmitted"],
                _object_slice_summary(objects[game])["authoredKindOfShip"]["objects"],
                _object_slice_summary(objects[game])["authoredKindOfShip"]["objectNotEmitted"],
                _object_slice_summary(objects[game])["effectiveKindOfShip"]["objects"],
                _object_slice_summary(objects[game])["effectiveKindOfShip"]["objectNotEmitted"],
            ] for game in GAMES],
        ),
        "",
    ]
    module_counts = _status_counts(module_rows, "coverageStatus")
    census_drift = [row for row in module_rows if row["committedCensusDrift"]]
    lines += [
        "## Object module behavior surface",
        "",
        f"The committed module denominator contains {len(module_rows)} kinds. Coverage states: "
        + ", ".join(f"{name}={count:,}" for name, count in module_counts.items()) + ".",
        "",
        f"Committed module-census evidence drift: {len(census_drift)} rows (" +
        (", ".join(row["name"] for row in census_drift) if census_drift else "none") +
        "). The ledger uses the live importer-source scan for status/consumer evidence.",
        "",
        "Any `deferred-descriptor`, `importer-only`, `refused`, or `unhandled` Class-C row is a",
        "simulation gap even if authored fields were preserved. See `GAPS.md` and `coverage.json`.",
        "",
        "## Machine-readable receipts",
        "",
        "- `coverage.json`: all top-level, field, nested-block, module, category, and corpus totals.",
        "- `definitions-*.jsonl`: every top-level definition site and exact static evidence.",
        "- `assignment-sites-*.jsonl`: every assignment site; values are represented by SHA-256, not copied.",
        "- `nested-sites-*.jsonl`, `script-call-sites-*.jsonl`, and `directive-sites-*.jsonl`: every remaining non-terminal semantic site.",
        "- `objects-*.jsonl`: every effective object identity with side/KindOf/module/model-emission evidence.",
        "- `asset-references-*.jsonl`: every syntactic asset/effect candidate and retail/selected-pack resolution.",
        "  Only catalog-proven physical BIG members contribute to the missing-asset completion denominator;",
        "  INI-definition IDs and unresolved compound-value tokens remain visible and are judged by their field/module receipts.",
        "- `unmapped-features.csv`: complete sortable field/nested/top-level gap ledger.",
        "- `GAPS.md`: usage-weighted backlog; `CRITICAL-GAPS.md`: requested high-risk slices.",
        "",
    ]
    return "\n".join(lines)


def _gap_markdown(matrices: Mapping[str, dict[str, Any]], module_rows: list[dict[str, Any]], objects: Mapping[str, list[dict[str, Any]]]) -> str:
    lines = [
        "# Retail INI mapping gaps",
        "",
        "Usage-weighted static gap ledger. Full rows are in `coverage.json` and `unmapped-features.csv`.",
        "A row below is not promoted merely because another feature with the same broad subsystem works.",
        "",
    ]
    for game in GAMES:
        lines += [f"## {GAME_LABELS[game]}: highest-frequency field/nested gaps", ""]
        rows = [*matrices[game]["fields"], *matrices[game]["nestedBlocks"], *matrices[game]["scriptCalls"], *matrices[game]["directives"]]
        gaps = [row for row in rows if row["status"] not in {"runtime-mentioned", "runtime-tested"}]
        gaps.sort(key=lambda row: (-row["sites"], row["signature"].casefold()))
        lines += [md_table(
            ["Rank", "Signature", "Sites", "Documents", "Evidence status", "Categories"],
            [[index, row["signature"], row["sites"], row["documents"], row["status"], ", ".join(row["categories"])] for index, row in enumerate(gaps[:250], 1)],
        ), ""]
        missing_objects = [row for row in objects[game] if not row["descriptorEmitted"]]
        critical = [row for row in missing_objects if set(row["categories"]) & {"hero-abilities", "neutral-mobs", "ships"}]
        critical.sort(key=lambda row: ("ships" not in row["categories"], "hero-abilities" not in row["categories"], row["objectId"].casefold()))
        lines += [f"### Critical objects not emitted ({len(critical):,} of {len(missing_objects):,} non-emitted objects)", "", md_table(
            ["Object", "Side", "KindOf", "Categories", "Source"],
            [[row["objectId"], ", ".join(row["sides"]), " ".join(row["kindOf"]), ", ".join(row["categories"]), row["sourceIni"][0]] for row in critical[:300]],
        ), ""]
    module_gaps = [row for row in module_rows if row["coverageStatus"] not in {"runtime-mentioned", "runtime-tested"}]
    module_gaps.sort(key=lambda row: (-max(row["declarationSites"].values()), row["name"].casefold()))
    lines += ["## Object module gaps", "", md_table(
        ["Rank", "Module", "Class", "BFME2 sites", "RotWK sites", "Coverage", "Importer"],
        [[index, row["name"], row["classification"], row["declarationSites"]["bfme2-retail"], row["declarationSites"]["rotwk-retail"], row["coverageStatus"], row["importerStatus"]] for index, row in enumerate(module_gaps, 1)],
    ), ""]
    return "\n".join(lines)


def _critical_markdown(matrices: Mapping[str, dict[str, Any]], objects: Mapping[str, list[dict[str, Any]]], scans: Mapping[str, GameScan]) -> str:
    requested = ("timers", "scripts", "assets", "hero-abilities", "neutral-mobs", "ships", "combat-effects", "spellbooks")
    lines = [
        "# Critical requested INI slices",
        "",
        "This is the adversarial review checklist requested by the operator. Counts come from the full",
        "ledger, and rows remain gaps unless direct static runtime/test evidence exists. The complete",
        "per-site detail is in the JSONL files.",
        "",
    ]
    for category in requested:
        lines += [f"## {category}", ""]
        category_rows = []
        for game in GAMES:
            rows = [*matrices[game]["topLevelKinds"], *matrices[game]["fields"], *matrices[game]["nestedBlocks"], *matrices[game]["scriptCalls"], *matrices[game]["directives"]]
            selected_rows = [row for row in rows if category in row["categories"]]
            site_total = sum(
                int(row.get("categorySites", {}).get(category, 0))
                for row in selected_rows
            )
            gap_total = sum(
                _category_gap_sites(row, category)
                for row in selected_rows
            )
            obj_rows = [row for row in objects[game] if category in row["categories"]]
            obj_missing = sum(1 for row in obj_rows if not row["descriptorEmitted"])
            asset_rows = [row for row in scans[game].asset_references if category == "assets"]
            asset_missing = sum(
                1 for row in asset_rows
                if _is_proven_retail_asset(row) and not row.get("selectedPackMapped")
            )
            category_rows.append([GAME_LABELS[game], len(selected_rows), site_total, gap_total, len(obj_rows), obj_missing, len(asset_rows), asset_missing])
        lines += [md_table(
            ["Game", "Feature signatures", "Sites", "Sites below runtime-mentioned", "Objects", "Objects not emitted", "Asset/effect refs", "Proven retail assets not in selected packs"],
            category_rows,
        ), ""]
        if category == "ships":
            lines += [
                "### Direct-authored and effective inherited `KindOf = SHIP` objects",
                "",
                md_table(
                    ["Game", "Surface", "Objects", "Descriptor emitted", "Not emitted"],
                    [
                        [
                            GAME_LABELS[game],
                            label,
                            _object_slice_summary(objects[game])[key]["objects"],
                            _object_slice_summary(objects[game])[key]["descriptorEmitted"],
                            _object_slice_summary(objects[game])[key]["objectNotEmitted"],
                        ]
                        for game in GAMES
                        for label, key in (
                            ("direct authored KindOf", "authoredKindOfShip"),
                            ("effective inherited KindOf", "effectiveKindOfShip"),
                        )
                    ],
                ),
                "",
            ]
        top = []
        for game in GAMES:
            rows = [*matrices[game]["topLevelKinds"], *matrices[game]["fields"], *matrices[game]["nestedBlocks"], *matrices[game]["scriptCalls"], *matrices[game]["directives"]]
            top.extend((game, row) for row in rows if category in row["categories"] and row["status"] not in {"runtime-mentioned", "runtime-tested"})
        top.sort(
            key=lambda pair: (
                -int(pair[1].get("categorySites", {}).get(category, 0)),
                pair[0],
                pair[1]["signature"].casefold(),
            )
        )
        lines += [md_table(
            ["Game", "Signature", "Sites", "Status"],
            [
                [
                    game,
                    row["signature"],
                    int(row.get("categorySites", {}).get(category, 0)),
                    row["status"],
                ]
                for game, row in top[:80]
            ],
        ), ""]
    lines += [
        "## Adversarial second-pass warnings",
        "",
        "- Exact-field matching undercounts normalized mappings (`BuildTime` may become `buildTimeMs`); such rows stay gaps until an explicit mapping contract supplies evidence.",
        "- Runtime string matching overcounts semantics when code logs or stores a field without applying it; no mention is called parity.",
        "- Selected faction packs are descriptor subsets, so neutral/cinematic/system objects and ships can be absent even when their source IDs were parsed elsewhere.",
        "- Asset candidates include INI-defined FX/OCL/audio-event identifiers as well as physical BIG members; `retailResolution` distinguishes those cases.",
        "- Object family classification resolves inherited `Side`/`KindOf` (including +/- KindOf patches); direct and resolved values remain separate in site receipts.",
        "- Module-census `consumed` is importer recognition only. Deferred opaque contracts remain simulation gaps.",
        "- Passing unit runners do not establish exact retail timers, command availability, damage ordering, RNG, lockstep hashes, save/load, or presentation timing.",
        "",
    ]
    return "\n".join(lines)


def build_outputs(catalog_paths: Mapping[str, Path], selection_path: Path) -> dict[str, str]:
    for path in [*catalog_paths.values(), selection_path, MODULE_CENSUS]:
        if not path.is_file():
            raise FileNotFoundError(path)
    importer = python_strings((ROOT / "importer/openbfme_importer").glob("*.py"))
    runtime_paths = tuple((ROOT / "game/src").rglob("*.gd"))
    test_paths = tuple((ROOT / "game/tests").rglob("*.gd"))
    runtime = gdscript_strings(runtime_paths)
    tests = gdscript_strings(test_paths)
    importer_tests = python_strings((ROOT / "importer/tests").glob("test_*.py"))
    for key, files in importer_tests.items():
        samples = tests.setdefault(key, [])
        for file in files:
            if file not in samples and len(samples) < 8:
                samples.append(file)
    selected = build_selected_index(selection_path)
    selected_by_game = {
        "bfme2": build_selected_index(selection_path, game="bfme2"),
        # The unscoped index is exactly the live layered RotWK mount. Reuse it
        # rather than walking thousands of selected JSON files twice.
        "rotwk": selected,
    }
    if selected.missing_packs:
        raise FileNotFoundError("selected packs missing: " + ", ".join(selected.missing_packs))
    for game, game_selection in selected_by_game.items():
        if game_selection.missing_packs:
            raise FileNotFoundError(
                f"{game} selected packs missing: "
                + ", ".join(game_selection.missing_packs)
            )

    catalogs = {game: InstallCatalog.load(catalog_paths[game]) for game in GAMES}
    scans = {game: scan_game(catalogs[game], game) for game in GAMES}
    matrices = {
        game: _enrich_scan(
            scans[game], importer, selected_by_game[game], runtime, tests
        )
        for game in GAMES
    }
    runtime_symbols = gdscript_function_symbols(runtime_paths)
    modules = _module_rows(scans, selected, runtime, runtime_symbols, tests)
    objects = {game: _object_union(scans[game].object_rows) for game in GAMES}

    category_summary: dict[str, dict[str, Any]] = {}
    for game in GAMES:
        for row in [*matrices[game]["topLevelKinds"], *matrices[game]["fields"], *matrices[game]["nestedBlocks"], *matrices[game]["scriptCalls"], *matrices[game]["directives"]]:
            for category in row["categories"]:
                item = category_summary.setdefault(category, {g: {"signatures": 0, "sites": 0, "gapSites": 0} for g in GAMES})[game]
                item["signatures"] += 1
                category_sites = int(row.get("categorySites", {}).get(category, 0))
                item["sites"] += category_sites
                item["gapSites"] += _category_gap_sites(row, category)

    feature_rows_by_game = {
        game: [
            *matrices[game]["topLevelKinds"],
            *matrices[game]["fields"],
            *matrices[game]["nestedBlocks"],
            *matrices[game]["scriptCalls"],
            *matrices[game]["directives"],
        ]
        for game in GAMES
    }
    site_rows_by_game = {
        game: [
            *scans[game].definitions,
            *scans[game].assignment_sites,
            *scans[game].nested_sites,
            *scans[game].script_call_sites,
            *scans[game].directive_sites,
        ]
        for game in GAMES
    }
    completion = _completion_summary(
        feature_rows_by_game,
        site_rows_by_game,
        modules,
        objects,
        {
            game: _asset_reference_statuses(scans[game].asset_references).get(
                "proven-retail-asset-not-selected-pack-mapped", 0
            )
            for game in GAMES
        },
        {game: scans[game].line_kinds.get("unknown", 0) for game in GAMES},
        dishonest_pack_addresses=sum(
            not row["honest"] for row in selected.pack_addresses
        ),
    )

    coverage = {
        "schema": "openbfme.retail-ini-coverage",
        "schemaVersion": 0,
        "policy": {
            "levels": ["unmapped", "importer-mentioned", "descriptor-emitted", "runtime-mentioned", "runtime-tested"],
            "parityClaimed": False,
            "note": "all levels are conservative static evidence; semantic retail parity requires behavior/oracle gates",
        },
        "catalogs": {game: {"path": str(catalog_paths[game]), "sha256": file_sha256(catalog_paths[game]), "installRoot": str(catalogs[game].install_root)} for game in GAMES},
        "selection": {
            "path": str(selection_path),
            "sha256": file_sha256(selection_path),
            "packIds": selected.pack_ids,
            "gamePackIds": {
                game: selected_by_game[game].pack_ids for game in GAMES
            },
            "gameJsonFiles": {
                game: selected_by_game[game].json_files for game in GAMES
            },
            "jsonFiles": selected.json_files,
            "packAddresses": selected.pack_addresses,
            "addressHonest": all(row["honest"] for row in selected.pack_addresses),
        },
        "games": {
            game: {
                "documents": len(scans[game].documents),
                "bytes": sum(row["bytes"] for row in scans[game].documents),
                "meaningfulLines": scans[game].meaningful_lines,
                "lineAccounting": dict(sorted(scans[game].line_kinds.items())),
                "nonTerminalReceipts": (
                    len(scans[game].definitions)
                    + len(scans[game].assignment_sites)
                    + len(scans[game].nested_sites)
                    + len(scans[game].script_call_sites)
                    + len(scans[game].directive_sites)
                    + len(scans[game].unknown_lines)
                ),
                "definitions": len(scans[game].definitions),
                "definitionSiteStatuses": _status_counts(scans[game].definitions),
                "assignmentSites": len(scans[game].assignment_sites),
                "uniqueObjects": len(objects[game]),
                "objectStatuses": dict(sorted(Counter(row["coverageStatus"] for row in objects[game]).items())),
                "objectSlices": _object_slice_summary(objects[game]),
                "assetReferences": len(scans[game].asset_references),
                "assetReferenceStatuses": _asset_reference_statuses(scans[game].asset_references),
                **matrices[game],
            } for game in GAMES
        },
        "modules": modules,
        "categories": dict(sorted(category_summary.items())),
        "completion": completion,
    }

    unmapped_csv_rows = []
    for game in GAMES:
        for feature_type in ("topLevelKinds", "fields", "nestedBlocks", "scriptCalls", "directives"):
            for row in matrices[game][feature_type]:
                if row["status"] in {"runtime-mentioned", "runtime-tested"}:
                    continue
                descriptor_counts = row.get("descriptorSiteCounts", {})
                unmapped_csv_rows.append([
                    game, feature_type, row["signature"], row["sites"], row["documents"], row["status"],
                    descriptor_counts.get("mapped", ""),
                    descriptor_counts.get("unmapped", ""),
                    bool(row.get("importerMentioned")),
                    bool(row.get("runtimeMentioned")),
                    bool(row.get("runtimeTested")),
                    ";".join(row["categories"]), row["samples"][0]["path"] if row["samples"] else "",
                    row["samples"][0]["line"] if row["samples"] else "",
                ])
    unmapped_csv_rows.sort(key=lambda row: (row[0], -int(row[3]), str(row[2]).casefold()))

    outputs = {
        "SUMMARY.md": _summary(
            scans, matrices, modules, objects, selected, catalog_paths, completion
        ),
        "GAPS.md": _gap_markdown(matrices, modules, objects),
        "CRITICAL-GAPS.md": _critical_markdown(matrices, objects, scans),
        "coverage.json": json_text(coverage),
        "unmapped-features.csv": csv_text(
            [
                "game", "featureType", "signature", "sites", "documents", "status",
                "descriptorMappedSites", "descriptorUnmappedSites",
                "importerMentioned", "runtimeMentioned", "runtimeTested",
                "categories", "samplePath", "sampleLine",
            ],
            unmapped_csv_rows,
        ),
    }
    for game in GAMES:
        outputs[f"documents-{game}.jsonl"] = jsonl_text(scans[game].documents)
        outputs[f"definitions-{game}.jsonl"] = jsonl_text(scans[game].definitions)
        outputs[f"assignment-sites-{game}.jsonl"] = jsonl_text(scans[game].assignment_sites)
        outputs[f"nested-sites-{game}.jsonl"] = jsonl_text(scans[game].nested_sites)
        outputs[f"script-call-sites-{game}.jsonl"] = jsonl_text(scans[game].script_call_sites)
        outputs[f"directive-sites-{game}.jsonl"] = jsonl_text(scans[game].directive_sites)
        outputs[f"objects-{game}.jsonl"] = jsonl_text(objects[game])
        outputs[f"asset-references-{game}.jsonl"] = jsonl_text(scans[game].asset_references)
        outputs[f"unknown-lines-{game}.jsonl"] = jsonl_text(scans[game].unknown_lines)
    return outputs


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--bfme2-catalog", type=Path, default=DEFAULT_CATALOGS["bfme2"])
    parser.add_argument("--rotwk-catalog", type=Path, default=DEFAULT_CATALOGS["rotwk"])
    parser.add_argument("--selection", type=Path, default=DEFAULT_SELECTION)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--check", action="store_true", help="fail if regenerated outputs differ from disk")
    parser.add_argument("--require-complete", action="store_true", help="fail while any feature is below runtime-tested or any object is not emitted")
    args = parser.parse_args(argv)
    catalog_paths = {"bfme2": args.bfme2_catalog.resolve(), "rotwk": args.rotwk_catalog.resolve()}
    outputs = build_outputs(catalog_paths, args.selection.resolve())
    out = args.out.resolve()
    if args.check:
        drift = []
        for name, content in outputs.items():
            path = out / name
            if not path.is_file():
                drift.append(f"{name}: missing")
            elif path.read_text(encoding="utf-8") != content:
                drift.append(f"{name}: drift")
        extras = sorted(path.name for path in out.iterdir() if path.is_file() and path.name not in outputs) if out.is_dir() else []
        drift.extend(f"{name}: unexpected" for name in extras)
        if drift:
            print("RETAIL_INI_COVERAGE_CHECK_FAILED")
            for item in drift:
                print(f"  {item}")
            return 1
        print(f"RETAIL_INI_COVERAGE_CHECK_OK files={len(outputs)}")
    else:
        out.mkdir(parents=True, exist_ok=True)
        for name, content in outputs.items():
            (out / name).write_text(content, encoding="utf-8", newline="\n")
        print(f"RETAIL_INI_COVERAGE_WRITTEN path={out} files={len(outputs)}")

    coverage = json.loads(outputs["coverage.json"])
    incomplete = 0
    for game in GAMES:
        for key in ("topLevelKinds", "fields", "nestedBlocks", "scriptCalls", "directives"):
            incomplete += sum(row["status"] != "runtime-tested" for row in coverage["games"][game][key])
        incomplete += coverage["games"][game]["objectStatuses"].get("object-not-emitted", 0)
        incomplete += coverage["games"][game]["assetReferenceStatuses"].get(
            "proven-retail-asset-not-selected-pack-mapped", 0
        )
        incomplete += coverage["games"][game]["lineAccounting"].get("unknown", 0)
    incomplete += sum(row["coverageStatus"] != "runtime-tested" for row in coverage["modules"])
    incomplete += sum(not row["honest"] for row in coverage["selection"]["packAddresses"])
    completion = coverage["completion"]
    print(
        "RETAIL_INI_COVERAGE_EXACT "
        f"incomplete_sites={completion['authoredSites']['incomplete']} "
        f"total_sites={completion['authoredSites']['total']} "
        f"incomplete_signatures={completion['featureSignatures']['incomplete']} "
        f"static_complete={str(completion['staticCoverageComplete']).lower()}"
    )
    print(f"RETAIL_INI_COVERAGE_RESULT incomplete_items={incomplete} parity_claimed=false")
    if args.require_complete and incomplete:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
