"""Closed, storage-free contract for the RotWK 2.02 semantic graph.

This module is intentionally boring.  It contains immutable declarations and
small, pure validators that later storage and conformance lanes can share.  It
does not read files, open SQLite, traverse a graph, discover APT companions, or
project retail input.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import re
import unicodedata
from typing import Any, Iterable, Mapping, Sequence


SCHEMA_NAME = "openbfme.rotwk-202-semantic-schema-manifest"
SCHEMA_VERSION = 4
BASELINE_ID = "rotwk-202-v9.7.7-en"
NORMATIVE_DESIGN_SHA256 = (
    "d5f9d8fc12a631156b4c3b73737f23faae8e40c71b7577a800e06f6c2e39090c"
)

INPUT_ARTIFACT_DIGESTS = (
    ("product-policy", "ff292f92857a4ebcac17ada3649563d7d810e1718683212bc4486c9b67af7b58"),
    ("catalog", "e1485aa8af794e0d154d2f5ccb65fa24af937c4ac9731f27d62e0eef9753c748"),
    ("archive-policy", "aaf30a92eacc76a8b11c0534235e569653f06fecb47400e28260981b5a04cf31"),
    ("effective-tree", "52fe8f2eb81371804e0b95f205561c0e3e98b58be86d3ee04f74505a93c1b6e6"),
    ("lexical-feature-graph", "cdb25cf7043b8d3e0d8649c54c365c1bddf6ad15966b35405ea8836aff22d7fa"),
)

BOUND_INPUT_IDS = (
    "baseline-contract", "product-policy", "catalog", "archive-policy",
    "effective-tree", "lexical-feature-graph", "source-schema",
)

SOURCE_SCHEMA_INPUT_SHAPE = (
    ("inputId", "source-schema"),
    ("algorithm", "python-ast-local-import-closure-v1"),
    ("rowFields", ("path", "sha256")),
    ("pathOrder", "ASCII"),
    ("rowEncoding", "utf8-lf:path|sha256\\n"),
    ("derivationOwner", "P0-CORPUS-SCHEMA-001 facade"),
)


class ContractError(ValueError):
    """Raised when a value contradicts the closed contract."""


@dataclass(frozen=True, slots=True)
class ValueSpec:
    kind: str
    nullable: bool = False
    values: tuple[str, ...] = ()
    item: "ValueSpec | None" = None
    fields: tuple[tuple[str, "ValueSpec"], ...] = ()


@dataclass(frozen=True, slots=True)
class FieldSpec:
    name: str
    value: ValueSpec
    fk_tables: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class IdentityPart:
    source: str
    atom: str
    literal: Any = None


@dataclass(frozen=True, slots=True)
class TableSpec:
    name: str
    row_kind: str | None
    type_tag: str
    prefix: str
    fields: tuple[FieldSpec, ...]
    identity: tuple[IdentityPart, ...]
    edge_endpoint: bool


@dataclass(frozen=True, slots=True)
class EvidenceVariantSpec:
    name: str
    required_non_null: tuple[str, ...]
    permitted_non_null: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class OccurrenceSourceSpec:
    source_family: str
    source_field: str
    source_predicate: str
    site_table: str
    source_ordinal: str
    occurrence_kind: str
    candidate_family: str


@dataclass(frozen=True, slots=True)
class EdgeSpec:
    kind: str
    source_table: str
    target_table: str
    mode: str
    cardinality: str


@dataclass(frozen=True, slots=True)
class RootSpec:
    root_id: str
    candidate_tables: tuple[str, ...]
    initial_rules: tuple[str, ...]
    traversal: str


@dataclass(frozen=True, slots=True)
class BoundSpec:
    name: str
    maximum: int
    refusal_code: str


def scalar(kind: str, *, nullable: bool = False) -> ValueSpec:
    return ValueSpec(kind=kind, nullable=nullable)


def enum(*values: str, nullable: bool = False) -> ValueSpec:
    return ValueSpec(kind="enum", nullable=nullable, values=tuple(values))


def literal(value: str) -> ValueSpec:
    return ValueSpec(kind="literal", values=(value,))


def sequence(kind: str, item: ValueSpec) -> ValueSpec:
    return ValueSpec(kind=kind, item=item)


def record(**fields: ValueSpec) -> ValueSpec:
    return ValueSpec(kind="record", fields=tuple(fields.items()))


def closed_map(keys: Sequence[str], item: ValueSpec) -> ValueSpec:
    return ValueSpec(kind="map", values=tuple(keys), item=item)


S = scalar("s")
PATH = scalar("path")
SHA256 = scalar("sha256")
ID = scalar("id")
EID = scalar("eid")
U16 = scalar("u16")
U32 = scalar("u32")
U64 = scalar("u64")
I64 = scalar("i64")
BOOL = scalar("b")


NODE_TABLES = (
    "source_records", "documents", "definitions", "assignments",
    "object_occurrences", "module_declarations", "module_kinds", "assets",
    "opaque_tokens", "script_calls", "nested_sites", "directive_sites",
    "unknown_sites", "maps", "map_objects", "map_libraries", "apt_movies",
    "apt_symbols", "wnd_windows", "parser_dispositions",
)

ADMIN_TABLES = (
    "evidence_referents", "mapcache_selection", "reference_occurrences",
    "edges", "selector_dispositions", "root_results", "root_memberships",
    "root_traversals", "root_residual_links", "domain_dispositions",
    "map_dispositions", "residuals", "counts",
)

ALL_TABLES = NODE_TABLES + ADMIN_TABLES

DOMAIN_KEYS = (
    "retail-skirmish", "retail-campaigns", "retail-war-of-the-ring",
    "retail-create-a-hero", "retail-shell",
)

ROOT_IDS = (
    "effective-retail-winners", "retail-reference-closure", "all-map-payloads",
    "skirmish-roots", "campaign-roots", "war-of-the-ring-roots",
    "create-a-hero-roots", "shell-roots",
)

RESIDUAL_KINDS = (
    "opaque-semantic-token", "unresolved-definition-reference",
    "ambiguous-definition-reference", "unresolved-asset-reference",
    "ambiguous-asset-reference", "unresolved-object-binding",
    "ambiguous-object-binding", "unresolved-object-parent",
    "ambiguous-object-parent", "unresolved-map-object-type",
    "ambiguous-map-object-type", "unresolved-map-library",
    "ambiguous-map-library", "unresolved-apt-import", "ambiguous-apt-import",
    "unsupported-script-semantics", "unsupported-callback-semantics",
    "orphan-definition", "nonlexical-effective-ini",
    "unscanned-additive-shadow", "unscanned-shadow",
    "semantic-parser-unavailable", "map-parse-error",
    "map-unsupported-semantics", "mapcache-missing", "mapcache-duplicate",
    "mapcache-ambiguous", "mapcache-parse-error", "apt-missing-companion",
    "apt-duplicate-companion", "apt-parse-error", "apt-unsupported-semantics",
    "wnd-parse-error", "wnd-unsupported-semantics",
    "unclassified-domain-membership", "ambiguous-map-mode",
    "unclassified-map-mode",
)

PARSER_FAMILIES = (
    "accepted-lexical-ini", "nonlexical-effective-ini",
    "unscanned-additive-shadow-ini", "unscanned-shadow", "sage-map",
    "apt-movie", "apt-constants", "apt-dat", "apt-geometry", "wnd-layout",
    "identity-only",
)

COVERAGE_DISPOSITIONS = (
    "accepted-lexical", "nonlexical-effective", "unscanned-additive-shadow",
)

OCCURRENCE_KINDS = (
    "ini-definition-reference", "retail-asset-path", "retail-asset-stem",
    "unresolved-retail-candidate", "object-parent", "map-object-type",
    "map-library-reference", "apt-import", "module-kind-declaration",
    "script-call", "wnd-callback",
)

CANDIDATE_FAMILIES = (
    "definition-any-kind", "definition-object-kind", "effective-path",
    "effective-stem", "untyped-retail-candidate", "map-path",
    "apt-same-directory-stem", "module-kind-taxonomy", "unsupported-script",
    "unsupported-wnd-callback",
)

ASSET_SUFFIXES = (
    ("w3d", (".w3d",)),
    ("image", (".dds", ".tga", ".png", ".jpg", ".jpeg")),
    ("audio", (".wav", ".mp3")),
    ("apt-resource", (".apt", ".const", ".dat", ".ru")),
    ("map-resource", (".map",)),
    ("localization", (".str", ".csf")),
)

SKIRMISH_KINDS = tuple(
    "AIBase AIData AIDozerAssignment ChildObject FactionVictoryData GameData "
    "MultiplayerSettings Object ObjectReskin PlayerAIType PlayerTemplate Science "
    "SkirmishAIData SpecialPower Upgrade VictorySystemData".split()
)

WOTR_KINDS = tuple(
    "ArmyDefinition ArmySummaryDescription AutoResolveArmor AutoResolveBody "
    "AutoResolveCombatChain AutoResolveHandicapLevel AutoResolveLeadership "
    "AutoResolveReinforcementSchedule AutoResolveWeapon BannerType "
    "ConcurrentRegionBonus LivingWorldAITemplate LivingWorldAnimObject "
    "LivingWorldArmyIcon LivingWorldAutoResolveResourceBonus "
    "LivingWorldAutoResolveSciencePurchasePointBonus LivingWorldBuilding "
    "LivingWorldBuildingIcon LivingWorldBuildPlotIcon LivingWorldCampaign "
    "LivingWorldMapInfo LivingWorldObject LivingWorldPlayerArmy "
    "LivingWorldPlayerTemplate LivingWorldRegionCampaign LivingWorldRegionEffects "
    "LivingWorldSound LivingWorldVictoryType Region RegionCampain SpawnArmy StrategicHUD".split()
)

CAH_KINDS = ("CreateAHeroBling", "CreateAHeroClass", "CreateAHeroSystem")
CAMPAIGN_KINDS = ("LinearCampaign",)

SHELL_KINDS = tuple(
    "AptButtonTooltipMap ButtonSet CommandButton CommandMap CommandSet "
    "ControlBarResizer ControlBarScheme DebugCommandMap FontDefaultSettings "
    "FontSubstitution InGameNotificationBox InGameUI LoadSubsystem MappedImage "
    "Mouse MouseCursor OnlineChatColors OptionGroup ShellMenuScheme StrategicHUD "
    "Video WebpageURL WindowTransition".split()
)


def F(name: str, value: ValueSpec, *fk_tables: str) -> FieldSpec:
    return FieldSpec(name, value, tuple(fk_tables))


NODE_BASE_FIELDS = (
    F("id", ID), F("kind", S), F("evidenceIds", sequence("set", EID), "evidence_referents"),
    F("residualIds", sequence("set", ID), "residuals"),
)


NODE_FIELDS = (
    ("source_records", (
        F("rawCatalogIndex", U32), F("recordIndex", U32), F("key", PATH),
        F("archive", PATH), F("member", PATH), F("offset", U64), F("size", U64),
        F("precedence", U16), F("payloadSha256", SHA256),
        F("disposition", enum("winner", "shadow")), F("chainIndex", U16),
        F("shadowSemantics", enum("winner", "override", "additive", "identity-only")),
    )),
    ("documents", (
        F("sourceRecordId", ID, "source_records"), F("path", PATH), F("archive", PATH),
        F("bytes", U64), F("precedence", U16), F("sha256", SHA256),
        F("coverageDisposition", enum(*COVERAGE_DISPOSITIONS)),
        F("lexicalDocumentId", scalar("s", nullable=True)),
    )),
    ("definitions", (
        F("documentId", ID, "documents"), F("lexicalId", S), F("definitionKind", S),
        F("name", scalar("s", nullable=True)), F("sourceIni", PATH), F("line", U32),
        F("occurrenceIndex", U32), F("categories", sequence("set", S)),
    )),
    ("assignments", (
        F("documentId", ID, "documents"), F("lexicalId", S),
        F("ownerTable", enum("documents", "definitions")),
        F("ownerId", ID, "documents", "definitions"), F("sourceIni", PATH),
        F("rootKind", S), F("rootName", scalar("s", nullable=True)), F("field", S),
        F("line", U32), F("occurrenceIndex", U32), F("valueSha256", SHA256),
        F("valueTokenCount", U32), F("moduleKind", scalar("s", nullable=True)),
        F("categories", sequence("set", S)),
        F("classification", enum("scalar", "reference", "collection-reference", "opaque-unresolved")),
    )),
    ("object_occurrences", (
        F("documentId", ID, "documents"), F("lexicalId", S),
        F("definitionId", scalar("id", nullable=True), "definitions"),
        F("objectKind", enum("Object", "ChildObject", "ObjectReskin")), F("objectId", S),
        F("parentToken", scalar("s", nullable=True)), F("sourceIni", PATH),
        F("side", scalar("s", nullable=True)), F("kindOf", sequence("list", S)),
        F("commandSets", sequence("list", S)), F("buildCostExpressions", sequence("list", S)),
        F("buildTimeExpressions", sequence("list", S)),
        F("moduleCounts", sequence("list", record(moduleKind=S, count=U32))),
        F("resolvedSides", sequence("set", S)), F("resolvedKindOf", sequence("set", S)),
        F("categories", sequence("set", S)), F("acceptedObjectOrdinal", U32),
        F("bindingState", enum("resolved", "unresolved", "ambiguous")),
        F("candidateDefinitionIds", sequence("set", ID), "definitions"),
    )),
    ("module_declarations", (
        F("assignmentId", ID, "assignments"), F("ownerTable", enum("documents", "definitions")),
        F("ownerId", ID, "documents", "definitions"), F("moduleKindId", ID, "module_kinds"),
        F("moduleKindSpelling", S), F("line", U32), F("occurrenceIndex", U32),
    )),
    ("module_kinds", (
        F("casefoldName", S), F("declarationIds", sequence("set", ID), "module_declarations"),
    )),
    ("assets", (
        F("sourceRecordId", ID, "source_records"), F("path", PATH), F("stem", S),
        F("suffix", S), F("payloadSha256", SHA256),
        F("assetClass", enum("w3d", "image", "audio", "apt-resource", "map-resource", "localization", "other-binary")),
    )),
    ("opaque_tokens", (
        F("assignmentId", ID, "assignments"), F("occurrenceOrdinal", U32), F("start", U32),
        F("end", U32), F("token", S), F("state", enum("stop-token", "unclassified")),
    )),
    ("script_calls", (
        F("documentId", ID, "documents"), F("ownerTable", enum("documents", "definitions")),
        F("ownerId", ID, "documents", "definitions"), F("lexicalId", S), F("line", U32),
        F("occurrenceOrdinal", U32), F("callName", S), F("argumentsSha256", SHA256),
        F("sourceIni", PATH), F("rootKind", S), F("rootName", scalar("s", nullable=True)),
        F("categories", sequence("set", S)),
    )),
    ("nested_sites", (
        F("documentId", ID, "documents"), F("ownerTable", enum("documents", "definitions")),
        F("ownerId", ID, "documents", "definitions"), F("lexicalId", S), F("line", U32),
        F("occurrenceIndex", U32), F("depth", U16), F("siteKind", S), F("sourceIni", PATH),
        F("rootKind", S), F("rootName", scalar("s", nullable=True)), F("selectorSha256", SHA256),
        F("categories", sequence("set", S)),
    )),
    ("directive_sites", (
        F("documentId", ID, "documents"), F("lexicalId", S), F("line", U32),
        F("occurrenceIndex", U32), F("directive", S), F("payloadSha256", SHA256), F("sourceIni", PATH),
    )),
    ("unknown_sites", (
        F("documentId", ID, "documents"), F("ownerTable", enum("documents", "definitions")),
        F("ownerId", ID, "documents", "definitions"), F("lexicalId", S), F("line", U32),
        F("occurrenceIndex", U32), F("rawSha256", SHA256), F("rawLength", U32),
        F("sourceIni", PATH), F("rootKind", S),
    )),
    ("maps", (
        F("sourceRecordId", ID, "source_records"), F("path", PATH), F("payloadSha256", SHA256),
        F("parserDispositionId", ID, "parser_dispositions"), F("mapCacheSelectionId", ID, "mapcache_selection"),
        F("mapCacheState", enum("resolved", "unlisted", "record-ambiguous", "selection-unavailable")),
        F("isMultiplayer", scalar("b", nullable=True)), F("isScenarioMp", scalar("b", nullable=True)),
        F("mapCacheEvidenceIds", sequence("set", EID), "evidence_referents"),
        F("parseState", enum("parsed", "error", "unsupported")),
    )),
    ("map_objects", (
        F("mapId", ID, "maps"), F("objectOrdinal", U32), F("typeToken", S), F("factsSha256", SHA256),
    )),
    ("map_libraries", (
        F("mapId", ID, "maps"), F("referenceOrdinal", U32), F("pathToken", PATH), F("factsSha256", SHA256),
    )),
    ("apt_movies", (
        F("sourceRecordId", ID, "source_records"), F("path", PATH), F("directory", PATH), F("stem", S),
        F("payloadSha256", SHA256), F("admissionState", enum("fully-admitted", "missing-const", "duplicate-const")),
        F("constAssetId", scalar("id", nullable=True), "assets"), F("datAssetId", scalar("id", nullable=True), "assets"),
        F("geometryAssetIds", sequence("set", ID), "assets"), F("parserDispositionId", ID, "parser_dispositions"),
        F("parseState", enum("parsed", "not-admitted", "error", "unsupported")),
    )),
    ("apt_symbols", (
        F("aptMovieId", ID, "apt_movies"), F("symbolOrdinal", U32), F("symbolName", scalar("s", nullable=True)),
        F("symbolKind", S), F("factsSha256", SHA256),
    )),
    ("wnd_windows", (
        F("sourceRecordId", ID, "source_records"), F("windowOrdinal", U32),
        F("parentWindowId", scalar("id", nullable=True), "wnd_windows"), F("name", scalar("s", nullable=True)),
        F("callbacks", sequence("list", S)), F("factsSha256", SHA256),
        F("parserDispositionId", ID, "parser_dispositions"),
    )),
    ("parser_dispositions", (
        F("sourceRecordId", ID, "source_records"), F("parserFamily", enum(*PARSER_FAMILIES)),
        F("state", enum("projected", "parsed", "not-admitted", "identity-only", "error", "unsupported")),
        F("reasonCode", ValueSpec("enum", nullable=True, values=RESIDUAL_KINDS)),
        F("reasonSha256", scalar("sha256", nullable=True)),
    )),
)


EDGE_DECLARATIONS = (
    ("source-document", "source_records", "documents", "structural", "1/T"),
    ("source-asset", "source_records", "assets", "structural", "1/T"),
    ("source-map", "source_records", "maps", "structural", "1/T"),
    ("source-apt-movie", "source_records", "apt_movies", "structural", "1/T"),
    ("source-wnd-window", "source_records", "wnd_windows", "structural", "1/T top-level"),
    ("source-parser-disposition", "source_records", "parser_dispositions", "structural", "1/T and 1/S"),
    ("document-definition", "documents", "definitions", "structural", "1/T"),
    ("document-assignment", "documents", "assignments", "structural", "1/T"),
    ("document-object-occurrence", "documents", "object_occurrences", "structural", "1/T"),
    ("document-script-call", "documents", "script_calls", "structural", "1/T"),
    ("document-nested-site", "documents", "nested_sites", "structural", "1/T"),
    ("document-directive-site", "documents", "directive_sites", "structural", "1/T"),
    ("document-unknown-site", "documents", "unknown_sites", "structural", "1/T"),
    ("definition-assignment", "definitions", "assignments", "structural", "1/T iff definition-owned"),
    ("object-binding", "object_occurrences", "definitions", "structural", "1 iff resolved"),
    ("module-owner-definition", "definitions", "module_declarations", "structural", "1/T iff definition-owned"),
    ("module-owner-document", "documents", "module_declarations", "structural", "1/T iff document-owned"),
    ("module-taxonomy", "module_declarations", "module_kinds", "occurrence", "1/S and 1 occurrence edge"),
    ("assignment-opaque-token", "assignments", "opaque_tokens", "structural", "1/T"),
    ("map-object-member", "maps", "map_objects", "structural", "1/T"),
    ("map-library-member", "maps", "map_libraries", "structural", "1/T"),
    ("apt-symbol-member", "apt_movies", "apt_symbols", "structural", "1/T"),
    ("wnd-child", "wnd_windows", "wnd_windows", "structural", "1/T non-root, same-source, acyclic"),
    ("precedence-chain", "source_records", "source_records", "structural", "1/T shadow, increasing chainIndex"),
    ("definition-reference-from-definition", "definitions", "definitions", "occurrence", "one iff resolved"),
    ("definition-reference-from-document", "documents", "definitions", "occurrence", "one iff resolved"),
    ("asset-reference-from-definition", "definitions", "assets", "occurrence", "one iff resolved"),
    ("asset-reference-from-document", "documents", "assets", "occurrence", "one iff resolved"),
    ("object-parent", "definitions", "definitions", "occurrence", "one iff resolved"),
    ("map-object-type", "map_objects", "definitions", "occurrence", "one iff resolved"),
    ("map-library-reference", "map_libraries", "maps", "occurrence", "one iff resolved"),
    ("apt-import", "apt_movies", "apt_movies", "occurrence", "one iff resolved"),
    ("apt-const-companion", "apt_movies", "assets", "structural", "targets(S)={constAssetId}|empty"),
    ("apt-dat-companion", "apt_movies", "assets", "structural", "targets(S)={datAssetId}|empty"),
    ("apt-geometry-companion", "apt_movies", "assets", "structural", "targets(S)=geometryAssetIds; 1/T"),
    ("apt-const-parser-disposition", "apt_movies", "parser_dispositions", "structural", "targets(S)=const parser|empty"),
    ("apt-dat-parser-disposition", "apt_movies", "parser_dispositions", "structural", "targets(S)=dat parser|empty"),
    ("apt-geometry-parser-disposition", "apt_movies", "parser_dispositions", "structural", "targets(S)=geometry parsers"),
    ("parser-owns-map", "parser_dispositions", "maps", "structural", "1/T"),
    ("parser-owns-map-object", "parser_dispositions", "map_objects", "structural", "1/T"),
    ("parser-owns-map-library", "parser_dispositions", "map_libraries", "structural", "1/T"),
    ("parser-owns-apt-movie", "parser_dispositions", "apt_movies", "structural", "1/T"),
    ("parser-owns-apt-symbol", "parser_dispositions", "apt_symbols", "structural", "1/T"),
    ("parser-owns-wnd-window", "parser_dispositions", "wnd_windows", "structural", "1/T"),
    ("map-parser-disposition", "maps", "parser_dispositions", "structural", "1/S same-source"),
    ("map-object-parser-disposition", "map_objects", "parser_dispositions", "structural", "1/S via map"),
    ("map-library-parser-disposition", "map_libraries", "parser_dispositions", "structural", "1/S via map"),
    ("apt-movie-parser-disposition", "apt_movies", "parser_dispositions", "structural", "1/S same-source"),
    ("apt-symbol-parser-disposition", "apt_symbols", "parser_dispositions", "structural", "1/S via movie"),
    ("wnd-parser-disposition", "wnd_windows", "parser_dispositions", "structural", "1/S same-source"),
)

EDGE_SPECS = tuple(EdgeSpec(*row) for row in EDGE_DECLARATIONS)
EDGE_KINDS = tuple(spec.kind for spec in EDGE_SPECS)


DOMAIN_STATE_MAP = closed_map(DOMAIN_KEYS, enum("member", "unclassified"))

ADMIN_FIELDS = (
    ("evidence_referents", (
        F("id", ID), F("referentKind", enum("output-row", "input-artifact", "source-span", "parser-fact")),
        F("referentTable", ValueSpec("enum", nullable=True, values=tuple(t for t in ALL_TABLES if t != "evidence_referents"))),
        F("referentId", scalar("id", nullable=True), *tuple(t for t in ALL_TABLES if t != "evidence_referents")),
        F("inputId", enum("baseline-contract", "product-policy", "catalog", "archive-policy", "effective-tree", "lexical-feature-graph", "source-schema", nullable=True)),
        F("sourceRecordId", scalar("id", nullable=True), "source_records"), F("documentId", scalar("id", nullable=True), "documents"),
        F("line", scalar("u32", nullable=True)), F("byteStart", scalar("u64", nullable=True)), F("byteEnd", scalar("u64", nullable=True)),
        F("factKind", scalar("s", nullable=True)), F("factOrdinal", scalar("u32", nullable=True)), F("digestSha256", SHA256),
    )),
    ("mapcache_selection", (
        F("id", ID), F("virtualPath", literal("maps/mapcache.ini")), F("state", enum("parsed", "missing", "duplicate", "error")),
        F("candidateSourceIds", sequence("set", ID), "source_records"), F("selectedSourceId", scalar("id", nullable=True), "source_records"),
        F("sourceParserDispositionId", scalar("id", nullable=True), "parser_dispositions"), F("factsSha256", scalar("sha256", nullable=True)),
        F("reasonSha256", scalar("sha256", nullable=True)), F("evidenceIds", sequence("set", EID), "evidence_referents"),
        F("residualIds", sequence("set", ID), "residuals"),
    )),
    ("reference_occurrences", (
        F("id", ID), F("occurrenceKind", enum(*OCCURRENCE_KINDS)),
        F("sourceFamily", enum("lexical-asset-reference", "lexical-object-parent", "lexical-module-kind", "lexical-script-call", "map-object-fact", "map-library-fact", "apt-import-fact", "wnd-callback-fact")),
        F("sourceField", enum("assetReferences.retailResolution", "objects.parent", "assignments.moduleKind", "scriptCalls.command", "mapObjects.typeToken", "mapLibraries.pathToken", "aptMovies.import", "wndWindows.callbacks")),
        F("sourceOrdinal", U32), F("siteTable", enum("assignments", "object_occurrences", "map_objects", "map_libraries", "apt_movies", "module_declarations", "script_calls", "wnd_windows")),
        F("siteId", ID, "assignments", "object_occurrences", "map_objects", "map_libraries", "apt_movies", "module_declarations", "script_calls", "wnd_windows"),
        F("ownerTable", enum("documents", "definitions", "object_occurrences", "map_objects", "map_libraries", "apt_movies", "module_declarations", "script_calls", "wnd_windows")),
        F("ownerId", ID, "documents", "definitions", "object_occurrences", "map_objects", "map_libraries", "apt_movies", "module_declarations", "script_calls", "wnd_windows"),
        F("occurrenceOrdinal", U32), F("token", S), F("lookupToken", S), F("candidateFamily", enum(*CANDIDATE_FAMILIES)),
        F("candidateTable", enum("definitions", "assets", "maps", "apt_movies", "module_kinds", nullable=True)),
        F("resolution", enum("resolved", "unresolved", "ambiguous", "unsupported")), F("candidateIds", sequence("set", ID), "definitions", "assets", "maps", "apt_movies", "module_kinds"),
        F("targetId", scalar("id", nullable=True), "definitions", "assets", "maps", "apt_movies", "module_kinds"), F("edgeId", scalar("id", nullable=True), "edges"),
        F("evidenceIds", sequence("set", EID), "evidence_referents"), F("residualIds", sequence("set", ID), "residuals"),
    )),
    ("edges", (
        F("id", ID), F("edgeKind", enum(*EDGE_KINDS)), F("sourceTable", enum(*NODE_TABLES)), F("sourceId", ID, *NODE_TABLES),
        F("targetTable", enum(*NODE_TABLES)), F("targetId", ID, *NODE_TABLES), F("occurrenceId", scalar("id", nullable=True), "reference_occurrences"),
        F("directOrdinal", scalar("u32", nullable=True)), F("evidenceIds", sequence("set", EID), "evidence_referents"),
    )),
    ("selector_dispositions", (
        F("id", ID), F("rootId", enum(*ROOT_IDS)), F("candidateTable", enum(*NODE_TABLES)), F("candidateId", ID, *NODE_TABLES),
        F("state", enum("seed", "promoted-seed", "not-seed")),
        F("ruleId", enum("all-winning-source", "all-definition", "all-map", "literal-skirmish-kind", "literal-campaign-kind", "literal-wotr-kind", "literal-cah-kind", "literal-shell-kind", "mapcache-multiplayer", "all-shell-apt", "all-shell-wnd", "all-shell-str", "incoming-owner-promotion", "reached-map-promotion", "not-selected")),
        F("promotionEdgeId", scalar("id", nullable=True), "edges"), F("evidenceIds", sequence("set", EID), "evidence_referents"),
    )),
    ("root_results", (
        F("id", ID), F("rootId", enum(*ROOT_IDS)), F("state", enum("executed", "executed-with-residuals")),
        F("seedCount", U64), F("promotedSeedCount", U64), F("traversedEdgeCount", U64), F("reachedNodeCount", U64), F("residualCount", U64),
        F("seedSetSha256", SHA256), F("promotedSeedSetSha256", SHA256), F("traversedEdgeSetSha256", SHA256), F("reachedNodeSetSha256", SHA256),
        F("residualSetSha256", SHA256), F("iterations", U32), F("evidenceIds", sequence("set", EID), "evidence_referents"),
    )),
    ("root_memberships", (
        F("id", ID), F("rootId", enum(*ROOT_IDS)), F("membershipKind", enum("seed", "promoted-seed", "reached-node")),
        F("memberTable", enum(*NODE_TABLES)), F("memberId", ID, *NODE_TABLES), F("firstIteration", U32), F("evidenceIds", sequence("set", EID), "evidence_referents"),
    )),
    ("root_traversals", (
        F("id", ID), F("rootId", enum(*ROOT_IDS)), F("edgeId", ID, "edges"), F("iteration", U32), F("evidenceIds", sequence("set", EID), "evidence_referents"),
    )),
    ("root_residual_links", (
        F("id", ID), F("rootId", enum(*ROOT_IDS)), F("residualId", ID, "residuals"), F("evidenceIds", sequence("set", EID), "evidence_referents"),
    )),
    ("domain_dispositions", (
        F("id", ID), F("nodeTable", enum(*NODE_TABLES)), F("nodeId", ID, *NODE_TABLES), F("states", DOMAIN_STATE_MAP),
        F("evidenceIds", sequence("set", EID), "evidence_referents"), F("residualIds", sequence("set", ID), "residuals"),
    )),
    ("map_dispositions", (
        F("id", ID), F("mapId", ID, "maps"), F("modeState", enum("classified", "conflicted", "unclassified")),
        F("candidateModes", sequence("set", enum("skirmish-multiplayer", "scenario-multiplayer", "library", "campaign-scenario", "war-of-the-ring-battle"))),
        F("domainStates", DOMAIN_STATE_MAP), F("evidenceIds", sequence("set", EID), "evidence_referents"), F("residualIds", sequence("set", ID), "residuals"),
    )),
    ("residuals", (
        F("id", ID), F("residualKind", enum(*RESIDUAL_KINDS)),
        F("subjectTable", enum(*(NODE_TABLES + ("mapcache_selection", "reference_occurrences", "domain_dispositions", "map_dispositions")))),
        F("subjectId", ID, *(NODE_TABLES + ("mapcache_selection", "reference_occurrences", "domain_dispositions", "map_dispositions"))),
        F("ordinal", U32), F("reasonCode", enum(*RESIDUAL_KINDS)), F("reasonSha256", SHA256), F("evidenceIds", sequence("set", EID), "evidence_referents"),
    )),
    ("counts", (
        F("id", ID), F("tableCounts", closed_map(ALL_TABLES, U64)), F("edgeKindCounts", closed_map(EDGE_KINDS, U64)),
        F("occurrenceKindCounts", closed_map(OCCURRENCE_KINDS, U64)), F("residualKindCounts", closed_map(RESIDUAL_KINDS, U64)),
        F("parserFamilyCounts", closed_map(PARSER_FAMILIES, U64)), F("coverageDispositionCounts", closed_map(COVERAGE_DISPOSITIONS, U64)),
        F("evidenceIds", sequence("set", EID), "evidence_referents"),
    )),
)


IDENTITY_REGISTRY = (
    ("source_records", "source-record", "SRC-", (("rawCatalogIndex", "int"), ("recordIndex", "int"), ("archive", "path"), ("member", "path"), ("offset", "int"), ("size", "int"), ("payloadSha256", "sha256"))),
    ("documents", "document", "DOC-", (("sourceRecordId", "id"), ("coverageDisposition", "enum"))),
    ("definitions", "definition", "DEF-", (("documentId", "id"), ("line", "int"), ("occurrenceIndex", "int"), ("definitionKind", "ci"), ("name", "ci-null"))),
    ("assignments", "assignment", "ASN-", (("documentId", "id"), ("line", "int"), ("occurrenceIndex", "int"), ("field", "ci"))),
    ("object_occurrences", "object-occurrence", "OBJ-", (("documentId", "id"), ("acceptedObjectOrdinal", "int"), ("objectKind", "enum"), ("objectId", "ci"))),
    ("module_declarations", "module-declaration", "MOD-", (("assignmentId", "id"), ("occurrenceIndex", "int"), ("moduleKindSpelling", "ci"))),
    ("module_kinds", "module-kind", "MKD-", (("casefoldName", "ci"),)),
    ("assets", "asset", "AST-", (("sourceRecordId", "id"),)),
    ("opaque_tokens", "opaque-token", "TOK-", (("assignmentId", "id"), ("occurrenceOrdinal", "int"), ("start", "int"), ("end", "int"), ("token", "cs"))),
    ("script_calls", "script-call", "SCR-", (("documentId", "id"), ("line", "int"), ("occurrenceOrdinal", "int"), ("callName", "cs"))),
    ("nested_sites", "nested-site", "NST-", (("documentId", "id"), ("line", "int"), ("occurrenceIndex", "int"), ("depth", "int"), ("siteKind", "cs"))),
    ("directive_sites", "directive-site", "DIR-", (("documentId", "id"), ("line", "int"), ("occurrenceIndex", "int"), ("directive", "cs"))),
    ("unknown_sites", "unknown-site", "UNK-", (("documentId", "id"), ("line", "int"), ("occurrenceIndex", "int"), ("rawSha256", "sha256"))),
    ("maps", "map", "MAP-", (("sourceRecordId", "id"),)),
    ("map_objects", "map-object", "MOB-", (("mapId", "id"), ("objectOrdinal", "int"))),
    ("map_libraries", "map-library", "MLB-", (("mapId", "id"), ("referenceOrdinal", "int"))),
    ("apt_movies", "apt-movie", "APM-", (("sourceRecordId", "id"),)),
    ("apt_symbols", "apt-symbol", "APS-", (("aptMovieId", "id"), ("symbolOrdinal", "int"))),
    ("wnd_windows", "wnd-window", "WND-", (("sourceRecordId", "id"), ("windowOrdinal", "int"))),
    ("parser_dispositions", "parser-disposition", "PAR-", (("sourceRecordId", "id"),)),
    ("evidence_referents", "evidence-referent", "EVID-", (("referentKind", "enum"), ("referentTable", "enum-null"), ("referentId", "id-null"), ("inputId", "enum-null"), ("sourceRecordId", "id-null"), ("documentId", "id-null"), ("line", "int-null"), ("byteStart", "int-null"), ("byteEnd", "int-null"), ("factKind", "cs-null"), ("factOrdinal", "int-null"), ("digestSha256", "sha256"))),
    ("mapcache_selection", "mapcache-selection", "MCS-", (("maps/mapcache.ini", "literal-path"),)),
    ("reference_occurrences", "reference-occurrence", "OCC-", (("sourceFamily", "enum"), ("sourceField", "enum"), ("sourceOrdinal", "int"), ("occurrenceKind", "enum"), ("siteId", "id"), ("occurrenceOrdinal", "int"), ("token", "cs"))),
    ("edges", "edge", "EDG-", (("edgeKind", "enum"), ("sourceId", "id"), ("targetId", "id"), ("occurrenceId", "id-null"), ("directOrdinal", "int-null"))),
    ("selector_dispositions", "selector-disposition", "SEL-", (("rootId", "enum"), ("candidateId", "id"))),
    ("root_results", "root-result", "ROT-", (("rootId", "enum"),)),
    ("root_memberships", "root-membership", "RMB-", (("rootId", "enum"), ("membershipKind", "enum"), ("memberId", "id"))),
    ("root_traversals", "root-traversal", "RTR-", (("rootId", "enum"), ("edgeId", "id"))),
    ("root_residual_links", "root-residual-link", "RRL-", (("rootId", "enum"), ("residualId", "id"))),
    ("domain_dispositions", "domain-disposition", "DOM-", (("nodeId", "id"),)),
    ("map_dispositions", "map-disposition", "MDP-", (("mapId", "id"),)),
    ("residuals", "residual", "RES-", (("residualKind", "enum"), ("subjectId", "id"), ("ordinal", "int"))),
    ("counts", "counts", "CNT-", (("graph", "literal-cs"),)),
)


OCCURRENCE_SOURCES = (
    OccurrenceSourceSpec("lexical-asset-reference", "assetReferences.retailResolution", "ini-definition", "assignments", "accepted assetReferences index", "ini-definition-reference", "definition-any-kind"),
    OccurrenceSourceSpec("lexical-asset-reference", "assetReferences.retailResolution", "retail-asset|ambiguous-retail-asset", "assignments", "accepted assetReferences index", "retail-asset-path", "effective-path"),
    OccurrenceSourceSpec("lexical-asset-reference", "assetReferences.retailResolution", "retail-asset-stem|ambiguous-retail-stem", "assignments", "accepted assetReferences index", "retail-asset-stem", "effective-stem"),
    OccurrenceSourceSpec("lexical-asset-reference", "assetReferences.retailResolution", "unresolved-candidate", "assignments", "accepted assetReferences index", "unresolved-retail-candidate", "untyped-retail-candidate"),
    OccurrenceSourceSpec("lexical-object-parent", "objects.parent", "non-null parentToken", "object_occurrences", "acceptedObjectOrdinal", "object-parent", "definition-object-kind"),
    OccurrenceSourceSpec("lexical-module-kind", "assignments.moduleKind", "non-null moduleKind", "module_declarations", "accepted assignments index", "module-kind-declaration", "module-kind-taxonomy"),
    OccurrenceSourceSpec("lexical-script-call", "scriptCalls.command", "every accepted script call", "script_calls", "accepted scriptCalls index", "script-call", "unsupported-script"),
    OccurrenceSourceSpec("map-object-fact", "mapObjects.typeToken", "every emitted map-object fact", "map_objects", "parser fact ordinal", "map-object-type", "definition-object-kind"),
    OccurrenceSourceSpec("map-library-fact", "mapLibraries.pathToken", "every emitted library fact", "map_libraries", "parser fact ordinal", "map-library-reference", "map-path"),
    OccurrenceSourceSpec("apt-import-fact", "aptMovies.import", "every emitted APT import fact", "apt_movies", "parser fact ordinal", "apt-import", "apt-same-directory-stem"),
    OccurrenceSourceSpec("wnd-callback-fact", "wndWindows.callbacks", "every callback in parser order", "wnd_windows", "parser fact ordinal", "wnd-callback", "unsupported-wnd-callback"),
)

CANDIDATE_TABLE_BY_FAMILY = (
    ("definition-any-kind", "definitions"),
    ("definition-object-kind", "definitions"),
    ("effective-path", "assets"),
    ("effective-stem", "assets"),
    ("untyped-retail-candidate", "assets"),
    ("map-path", "maps"),
    ("apt-same-directory-stem", "apt_movies"),
    ("module-kind-taxonomy", "module_kinds"),
    ("unsupported-script", None),
    ("unsupported-wnd-callback", None),
)

OCCURRENCE_ALLOWED_STATES = (
    ("ini-definition-reference", ("resolved", "unresolved", "ambiguous")),
    ("retail-asset-path", ("resolved", "unresolved", "ambiguous")),
    ("retail-asset-stem", ("resolved", "unresolved", "ambiguous")),
    ("unresolved-retail-candidate", ("unresolved",)),
    ("object-parent", ("resolved", "unresolved", "ambiguous")),
    ("map-object-type", ("resolved", "unresolved", "ambiguous")),
    ("map-library-reference", ("resolved", "unresolved", "ambiguous")),
    ("apt-import", ("resolved", "unresolved", "ambiguous")),
    ("module-kind-declaration", ("resolved",)),
    ("script-call", ("unsupported",)),
    ("wnd-callback", ("unsupported",)),
)

OCCURRENCE_OWNER_LIFT = (
    ("ini-definition-reference", "assignment owner definition/document"),
    ("retail-asset-path", "assignment owner definition/document"),
    ("retail-asset-stem", "assignment owner definition/document"),
    ("unresolved-retail-candidate", "assignment owner definition/document"),
    ("object-parent", "resolved child definition, else object occurrence"),
    ("map-object-type", "map object"),
    ("map-library-reference", "map library"),
    ("apt-import", "importing movie"),
    ("module-kind-declaration", "module declaration"),
    ("script-call", "script call"),
    ("wnd-callback", "WND window"),
)

OCCURRENCE_RESIDUALS = (
    ("ini-definition-reference", "unresolved-definition-reference", "ambiguous-definition-reference", None),
    ("retail-asset-path", "unresolved-asset-reference", "ambiguous-asset-reference", None),
    ("retail-asset-stem", "unresolved-asset-reference", "ambiguous-asset-reference", None),
    ("unresolved-retail-candidate", "unresolved-asset-reference", None, None),
    ("object-parent", "unresolved-object-parent", "ambiguous-object-parent", None),
    ("map-object-type", "unresolved-map-object-type", "ambiguous-map-object-type", None),
    ("map-library-reference", "unresolved-map-library", "ambiguous-map-library", None),
    ("apt-import", "unresolved-apt-import", "ambiguous-apt-import", None),
    ("module-kind-declaration", None, None, None),
    ("script-call", None, None, "unsupported-script-semantics"),
    ("wnd-callback", None, None, "unsupported-callback-semantics"),
)

OCCURRENCE_EDGE_DIRECTIONS = (
    ("ini-definition-reference", "definitions", "definitions", "definition-reference-from-definition"),
    ("ini-definition-reference", "documents", "definitions", "definition-reference-from-document"),
    ("retail-asset-path", "definitions", "assets", "asset-reference-from-definition"),
    ("retail-asset-path", "documents", "assets", "asset-reference-from-document"),
    ("retail-asset-stem", "definitions", "assets", "asset-reference-from-definition"),
    ("retail-asset-stem", "documents", "assets", "asset-reference-from-document"),
    ("object-parent", "definitions", "definitions", "object-parent"),
    ("map-object-type", "map_objects", "definitions", "map-object-type"),
    ("map-library-reference", "map_libraries", "maps", "map-library-reference"),
    ("apt-import", "apt_movies", "apt_movies", "apt-import"),
    ("module-kind-declaration", "module_declarations", "module_kinds", "module-taxonomy"),
)

DISCRIMINATED_FKS = (
    ("assignments", "ownerTable", ("ownerId",)),
    ("module_declarations", "ownerTable", ("ownerId",)),
    ("script_calls", "ownerTable", ("ownerId",)),
    ("nested_sites", "ownerTable", ("ownerId",)),
    ("unknown_sites", "ownerTable", ("ownerId",)),
    ("evidence_referents", "referentTable", ("referentId",)),
    ("reference_occurrences", "siteTable", ("siteId",)),
    ("reference_occurrences", "ownerTable", ("ownerId",)),
    ("reference_occurrences", "candidateTable", ("candidateIds", "targetId")),
    ("edges", "sourceTable", ("sourceId",)),
    ("edges", "targetTable", ("targetId",)),
    ("selector_dispositions", "candidateTable", ("candidateId",)),
    ("root_memberships", "memberTable", ("memberId",)),
    ("domain_dispositions", "nodeTable", ("nodeId",)),
    ("residuals", "subjectTable", ("subjectId",)),
)

RESOLUTION_CARDINALITY = (
    ("resolved", 1, 1, 1, 0),
    ("unresolved", 0, 0, 0, 1),
    ("ambiguous", 2, None, 0, 1),
    ("unsupported", 0, 0, 0, 1),
)

EVIDENCE_VARIANTS = (
    EvidenceVariantSpec("output-row", ("referentTable", "referentId"), ("referentTable", "referentId")),
    EvidenceVariantSpec("input-artifact", ("inputId",), ("inputId",)),
    EvidenceVariantSpec("source-span", ("sourceRecordId", "line", "byteStart", "byteEnd"), ("sourceRecordId", "documentId", "line", "byteStart", "byteEnd")),
    EvidenceVariantSpec("parser-fact", ("sourceRecordId", "factKind", "factOrdinal"), ("sourceRecordId", "factKind", "factOrdinal")),
)

TRAVERSAL_COMMON = tuple(kind for kind in EDGE_KINDS if kind != "precedence-chain")
TRAVERSAL_EFFECTIVE = tuple(kind for kind in EDGE_KINDS)

ROOT_SPECS = (
    RootSpec("effective-retail-winners", ("source_records",), ("all-winning-source",), "TRAVERSAL_EFFECTIVE"),
    RootSpec("retail-reference-closure", ("definitions",), ("all-definition",), "TRAVERSAL_COMMON"),
    RootSpec("all-map-payloads", ("maps",), ("all-map",), "TRAVERSAL_COMMON"),
    RootSpec("skirmish-roots", ("definitions", "maps"), ("literal-skirmish-kind", "mapcache-multiplayer"), "TRAVERSAL_COMMON"),
    RootSpec("campaign-roots", ("definitions", "maps"), ("literal-campaign-kind",), "TRAVERSAL_COMMON"),
    RootSpec("war-of-the-ring-roots", ("definitions", "maps"), ("literal-wotr-kind",), "TRAVERSAL_COMMON"),
    RootSpec("create-a-hero-roots", ("definitions", "maps"), ("literal-cah-kind",), "TRAVERSAL_COMMON"),
    RootSpec("shell-roots", ("definitions", "maps", "apt_movies", "wnd_windows", "source_records"), ("literal-shell-kind", "all-shell-apt", "all-shell-wnd", "all-shell-str"), "TRAVERSAL_COMMON"),
)

SELECTOR_STATE_RULES = (
    ("not-seed", ("not-selected",), "promotionEdgeId=null"),
    ("seed", ("all-winning-source", "all-definition", "all-map", "literal-skirmish-kind", "literal-campaign-kind", "literal-wotr-kind", "literal-cah-kind", "literal-shell-kind", "mapcache-multiplayer", "all-shell-apt", "all-shell-wnd", "all-shell-str"), "promotionEdgeId=null"),
    ("promoted-seed", ("incoming-owner-promotion", "reached-map-promotion"), "promotionEdgeId=lowest-ASCII-causal-edge"),
)

DOMAIN_ROOTS = (
    ("retail-skirmish", "skirmish-roots"),
    ("retail-campaigns", "campaign-roots"),
    ("retail-war-of-the-ring", "war-of-the-ring-roots"),
    ("retail-create-a-hero", "create-a-hero-roots"),
    ("retail-shell", "shell-roots"),
)

MAP_MODE_RULES = (
    ("skirmish-multiplayer", "mapcache resolved and isMultiplayer=true and isScenarioMp=false"),
    ("scenario-multiplayer", "mapcache resolved and isMultiplayer=true and isScenarioMp=true"),
    ("library", "resolved LibraryMapLists occurrence targets map"),
    ("campaign-scenario", "campaign-roots reached-node membership"),
    ("war-of-the-ring-battle", "war-of-the-ring-roots reached-node membership"),
)

PARSER_DISPATCH = (
    (1, "effective winner and exact accepted lexical tuple", "accepted-lexical-ini"),
    (2, "winning .ini/.inc absent from lexical input", "nonlexical-effective-ini"),
    (3, "additive shadow .ini/.inc absent from lexical input", "unscanned-additive-shadow-ini"),
    (4, "any other shadow", "unscanned-shadow"),
    (5, "winning .map", "sage-map"),
    (6, "winning .apt", "apt-movie"),
    (7, "winning sole same-directory/casefolded-stem .const", "apt-constants"),
    (8, "winning .dat in exact admitted apt/const/dat triplet", "apt-dat"),
    (9, "winning safe <directory>/<stem>_geometry/<decimal-id>.ru", "apt-geometry"),
    (10, "winning .wnd", "wnd-layout"),
    (11, "every other winning record", "identity-only"),
)

BOUND_SPECS = (
    BoundSpec("maxJsonDepth", 16, "E_JSON_DEPTH_LIMIT"),
    BoundSpec("maxContainerItems", 1_000_000, "E_CONTAINER_ITEMS_LIMIT"),
    BoundSpec("maxStringUtf8Bytes", 1_048_576, "E_STRING_BYTES_LIMIT"),
    BoundSpec("maxJsonlLineBytes", 1_048_576, "E_LINE_BYTES_LIMIT"),
    BoundSpec("maxRowsPerShard", 100_000, "E_SHARD_ROWS_LIMIT"),
    BoundSpec("maxBytesPerShard", 67_108_864, "E_SHARD_BYTES_LIMIT"),
    BoundSpec("maxShardsPerTable", 4_096, "E_SHARD_COUNT_LIMIT"),
    BoundSpec("maxRowsPerNodeTable", 1_000_000, "E_NODE_TABLE_ROWS_LIMIT"),
    BoundSpec("maxNodeRowsTotal", 5_000_000, "E_NODE_ROWS_TOTAL_LIMIT"),
    BoundSpec("maxReferenceOccurrences", 5_000_000, "E_OCCURRENCE_ROWS_LIMIT"),
    BoundSpec("maxEdges", 10_000_000, "E_EDGE_ROWS_LIMIT"),
    BoundSpec("maxResiduals", 5_000_000, "E_RESIDUAL_ROWS_LIMIT"),
    BoundSpec("maxRowsPerAdminTable", 10_000_000, "E_ADMIN_TABLE_ROWS_LIMIT"),
    BoundSpec("maxRowsTotal", 20_000_000, "E_TOTAL_ROWS_LIMIT"),
    BoundSpec("maxOutputBytes", 8_589_934_592, "E_OUTPUT_BYTES_LIMIT"),
)

BOUNDS = tuple((spec.name, spec.maximum) for spec in BOUND_SPECS)

# The single authoritative per-table check order.  The ledger addendum puts
# named administrative tables before the generic administrative-table bound.
ROW_LIMIT_PRECEDENCE = (
    ("reference_occurrences", "maxReferenceOccurrences", "E_OCCURRENCE_ROWS_LIMIT"),
    ("edges", "maxEdges", "E_EDGE_ROWS_LIMIT"),
    ("residuals", "maxResiduals", "E_RESIDUAL_ROWS_LIMIT"),
    ("NODE_TABLES", "maxRowsPerNodeTable", "E_NODE_TABLE_ROWS_LIMIT"),
    ("ADMIN_TABLES", "maxRowsPerAdminTable", "E_ADMIN_TABLE_ROWS_LIMIT"),
)

LIVE_INVARIANTS = (
    ("source_records", 53_433), ("winner source_records", 48_566),
    ("object_occurrences", 5_988), ("accepted-lexical documents", 850),
    ("nonlexical-effective documents", 332), ("effective winner documents", 1_182),
    ("definitions", 36_940), ("assignments", 597_886),
    ("unique objectId casefolds", 5_494), ("asset-reference occurrences", 188_543),
    ("module_declarations", 24_230), ("script_calls", 4_384),
    ("opaque-unresolved assignments", 290_942),
    ("unresolved retail asset references", 76_656),
    ("ambiguous retail asset references", 2_203),
)


def _identity_parts(parts: Sequence[tuple[str, str]]) -> tuple[IdentityPart, ...]:
    result: list[IdentityPart] = []
    for source, atom in parts:
        if atom.startswith("literal-"):
            result.append(IdentityPart("$literal", atom.removeprefix("literal-"), source))
        else:
            result.append(IdentityPart(source, atom))
    return tuple(result)


_node_field_map = dict(NODE_FIELDS)
_admin_field_map = dict(ADMIN_FIELDS)
TABLE_SPECS = tuple(
    TableSpec(
        name=name,
        row_kind=type_tag if name in NODE_TABLES else None,
        type_tag=type_tag,
        prefix=prefix,
        fields=(
            (NODE_BASE_FIELDS[0], F("kind", literal(type_tag)), *NODE_BASE_FIELDS[2:], *_node_field_map[name])
            if name in NODE_TABLES else _admin_field_map[name]
        ),
        identity=_identity_parts(parts),
        edge_endpoint=name in NODE_TABLES,
    )
    for name, type_tag, prefix, parts in IDENTITY_REGISTRY
)


def table_spec(name: str) -> TableSpec:
    for spec in TABLE_SPECS:
        if spec.name == name:
            return spec
    raise ContractError(f"unknown table: {name}")


def canonical_path(value: str) -> str:
    value = _require_string(value)
    converted = value.replace("\\", "/")
    parts = converted.split("/")
    if not converted or converted.startswith("/") or ":" in converted or any(part in ("", ".", "..") for part in parts):
        raise ContractError("path is not a safe canonical relative path")
    normalized = "/".join(unicodedata.normalize("NFC", part) for part in parts)
    if normalized != converted:
        raise ContractError("path is not NFC-normalized")
    return normalized


def _require_string(value: Any) -> str:
    if not isinstance(value, str) or "\x00" in value or unicodedata.normalize("NFC", value) != value:
        raise ContractError("expected NFC string without NUL")
    return value


_ID_RE = re.compile(r"^[A-Z]+-[0-9a-f]{64}$")
_SHA_RE = re.compile(r"^[0-9a-f]{64}$")


def canonical_atom(
    tag: str,
    value: Any = None,
    *,
    allowed_values: Sequence[str] = (),
    integer_kind: str | None = None,
) -> list[Any]:
    nullable = tag.endswith("-null")
    base = tag.removesuffix("-null")
    if value is None:
        if nullable or base == "null":
            return ["null"]
        raise ContractError(f"{tag} does not admit null")
    if base in ("cs", "ci", "path", "enum"):
        text = canonical_path(value) if base == "path" else _require_string(value)
        if base == "enum" and allowed_values and text not in allowed_values:
            raise ContractError("enum atom is outside its closed registry")
        if base == "ci":
            text = text.casefold()
        return [base, text]
    if base == "int":
        if isinstance(value, bool) or not isinstance(value, int):
            raise ContractError("integer atom rejects booleans and non-integers")
        if integer_kind is not None:
            ranges = {
                "u16": (0, 65_535), "u32": (0, 4_294_967_295),
                "u64": (0, 18_446_744_073_709_551_615),
                "i64": (-9_223_372_036_854_775_808, 9_223_372_036_854_775_807),
            }
            if integer_kind not in ranges or not ranges[integer_kind][0] <= value <= ranges[integer_kind][1]:
                raise ContractError("integer atom is outside its declared type")
        return ["int", str(value)]
    if base == "bool":
        if type(value) is not bool:
            raise ContractError("boolean atom requires JSON boolean")
        return ["bool", "true" if value else "false"]
    if base == "id":
        if not isinstance(value, str) or not _ID_RE.fullmatch(value):
            raise ContractError("invalid ID atom")
        return ["id", value]
    if base == "sha256":
        if not isinstance(value, str) or not _SHA_RE.fullmatch(value):
            raise ContractError("invalid SHA-256 atom")
        return ["sha256", value]
    if base in ("list", "set"):
        if not isinstance(value, (list, tuple)):
            raise ContractError(f"{base} atom requires a sequence of tagged atoms")
        atoms = list(value)
        for atom in atoms:
            _validate_tagged_atom(atom)
        if base == "set":
            rendered = [json.dumps(atom, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False) for atom in atoms]
            if rendered != sorted(set(rendered)):
                raise ContractError("set atom must be unique and ASCII-sorted")
        return [base, *atoms]
    if base == "null":
        raise ContractError("null atom rejects non-null values")
    raise ContractError(f"unknown atom tag: {tag}")


def _validate_tagged_atom(atom: Any) -> None:
    if not isinstance(atom, list) or not atom or not isinstance(atom[0], str):
        raise ContractError("container atom contains an untagged value")
    tag = atom[0]
    if tag == "null":
        if len(atom) != 1:
            raise ContractError("malformed null atom")
        return
    if tag in ("cs", "ci", "path", "int", "bool", "id", "sha256", "enum"):
        if len(atom) != 2:
            raise ContractError(f"malformed {tag} atom")
        payload = atom[1]
        if tag in ("cs", "enum"):
            _require_string(payload)
            if tag == "enum" and (not payload.isascii() or not payload):
                raise ContractError("encoded enum atom must be a non-empty ASCII token")
        elif tag == "ci":
            text = _require_string(payload)
            if text != text.casefold():
                raise ContractError("encoded CI atom is not casefolded")
        elif tag == "path":
            if canonical_path(payload) != payload:
                raise ContractError("encoded path atom is not canonical")
        elif tag == "int":
            if not isinstance(payload, str) or not re.fullmatch(r"0|-?[1-9][0-9]*", payload):
                raise ContractError("encoded integer atom is not canonical base-10")
        elif tag == "bool":
            if payload not in ("true", "false"):
                raise ContractError("encoded boolean atom is not canonical")
        elif tag == "id":
            if not isinstance(payload, str) or not _ID_RE.fullmatch(payload):
                raise ContractError("encoded ID atom is invalid")
        elif tag == "sha256":
            if not isinstance(payload, str) or not _SHA_RE.fullmatch(payload):
                raise ContractError("encoded SHA-256 atom is invalid")
        return
    if tag in ("list", "set"):
        canonical_atom(tag, atom[1:])
        return
    raise ContractError(f"unknown nested atom tag: {tag}")


def canonical_preimage(type_tag: str, atoms: Sequence[list[Any]]) -> bytes:
    _require_string(type_tag)
    value = [SCHEMA_VERSION, type_tag, *atoms]
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


def identity_preimage(table: str, row: Mapping[str, Any]) -> bytes:
    spec = table_spec(table)
    atoms: list[list[Any]] = []
    for part in spec.identity:
        if part.source == "$literal":
            value = part.literal
        else:
            if part.source not in row:
                raise ContractError(f"{table}: missing identity part {part.source}")
            value = row[part.source]
        field = next((item for item in spec.fields if item.name == part.source), None)
        allowed_values = field.value.values if field and field.value.kind == "enum" else ()
        integer_kind = field.value.kind if field and field.value.kind in ("u16", "u32", "u64", "i64") else None
        atoms.append(canonical_atom(part.atom, value, allowed_values=allowed_values, integer_kind=integer_kind))
    return canonical_preimage(spec.type_tag, atoms)


def expected_id(table: str, row: Mapping[str, Any]) -> str:
    spec = table_spec(table)
    return spec.prefix + hashlib.sha256(identity_preimage(table, row)).hexdigest()


def validate_identity(table: str, row: Mapping[str, Any]) -> None:
    if row.get("id") != expected_id(table, row):
        raise ContractError(f"{table}: ID does not match canonical preimage")


def validate_unique_identities(table: str, rows: Iterable[Mapping[str, Any]]) -> None:
    seen_preimages: set[bytes] = set()
    seen_ids: set[str] = set()
    for row in rows:
        validate_identity(table, row)
        preimage = identity_preimage(table, row)
        identity = row["id"]
        if preimage in seen_preimages:
            raise ContractError(f"{table}: duplicate preimage")
        if identity in seen_ids:
            raise ContractError(f"{table}: duplicate ID")
        seen_preimages.add(preimage)
        seen_ids.add(identity)


def _validate_value(spec: ValueSpec, value: Any, field: str) -> None:
    if value is None:
        if spec.nullable:
            return
        raise ContractError(f"{field}: null is forbidden")
    if spec.kind in ("s", "path", "sha256", "id", "eid"):
        if spec.kind == "s":
            _require_string(value)
        elif spec.kind == "path":
            canonical_path(value)
        elif spec.kind == "sha256" and (not isinstance(value, str) or not _SHA_RE.fullmatch(value)):
            raise ContractError(f"{field}: invalid sha256")
        elif spec.kind in ("id", "eid"):
            if not isinstance(value, str) or not _ID_RE.fullmatch(value):
                raise ContractError(f"{field}: invalid id")
            if spec.kind == "eid" and not value.startswith("EVID-"):
                raise ContractError(f"{field}: expected evidence ID")
        return
    if spec.kind in ("u16", "u32", "u64", "i64"):
        bounds = {"u16": (0, 65_535), "u32": (0, 4_294_967_295), "u64": (0, 18_446_744_073_709_551_615), "i64": (-9_223_372_036_854_775_808, 9_223_372_036_854_775_807)}
        if isinstance(value, bool) or not isinstance(value, int) or not bounds[spec.kind][0] <= value <= bounds[spec.kind][1]:
            raise ContractError(f"{field}: invalid {spec.kind}")
        return
    if spec.kind == "b":
        if type(value) is not bool:
            raise ContractError(f"{field}: expected boolean")
        return
    if spec.kind in ("enum", "literal"):
        if value not in spec.values:
            raise ContractError(f"{field}: value outside closed registry")
        return
    if spec.kind in ("list", "set"):
        if not isinstance(value, list):
            raise ContractError(f"{field}: expected JSON array")
        assert spec.item is not None
        for index, item in enumerate(value):
            _validate_value(spec.item, item, f"{field}/{index}")
        if spec.kind == "set":
            rendered = [json.dumps(item, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False) for item in value]
            if rendered != sorted(set(rendered)):
                raise ContractError(f"{field}: set must be unique and ASCII-sorted")
        return
    if spec.kind in ("record", "map"):
        if not isinstance(value, dict):
            raise ContractError(f"{field}: expected JSON object")
        declared = dict(spec.fields) if spec.kind == "record" else {key: spec.item for key in spec.values}
        if set(value) != set(declared):
            raise ContractError(f"{field}: object keys do not match closed contract")
        for key, child in declared.items():
            assert child is not None
            _validate_value(child, value[key], f"{field}/{key}")
        return
    raise ContractError(f"{field}: unknown type {spec.kind}")


def validate_row_shape(table: str, row: Mapping[str, Any]) -> None:
    spec = table_spec(table)
    if not isinstance(row, Mapping):
        raise ContractError(f"{table}: row must be an object")
    expected_fields = tuple(field.name for field in spec.fields)
    if set(row) != set(expected_fields):
        missing = tuple(name for name in expected_fields if name not in row)
        extra = tuple(name for name in row if name not in expected_fields)
        raise ContractError(f"{table}: fields mismatch missing={missing} extra={extra}")
    for field in spec.fields:
        _validate_value(field.value, row[field.name], f"/{field.name}")
        if field.fk_tables:
            values = row[field.name]
            if values is None:
                values = ()
            elif isinstance(values, list):
                values = tuple(values)
            else:
                values = (values,)
            permitted_prefixes = tuple(table_spec(target).prefix for target in field.fk_tables)
            for identity in values:
                if not isinstance(identity, str) or not identity.startswith(permitted_prefixes):
                    raise ContractError(f"{table}: {field.name} has wrong FK family")
    if spec.row_kind is not None and row["kind"] != spec.row_kind:
        raise ContractError(f"{table}: wrong literal kind")
    if not str(row["id"]).startswith(spec.prefix):
        raise ContractError(f"{table}: wrong ID prefix")
    for declared_table, discriminator, id_fields in DISCRIMINATED_FKS:
        if declared_table != table:
            continue
        target_table = row[discriminator]
        if target_table is None:
            if any(row[field] not in (None, []) for field in id_fields):
                raise ContractError(f"{table}: null {discriminator} requires null/empty target IDs")
            continue
        target_prefix = table_spec(target_table).prefix
        for field in id_fields:
            values = row[field]
            if values is None:
                continue
            if not isinstance(values, list):
                values = [values]
            if any(not value.startswith(target_prefix) for value in values):
                raise ContractError(f"{table}: {field} disagrees with {discriminator}")


def validate_evidence_variant(row: Mapping[str, Any]) -> None:
    name = row.get("referentKind")
    variant = next((item for item in EVIDENCE_VARIANTS if item.name == name), None)
    if variant is None:
        raise ContractError("unknown evidence variant")
    variant_fields = (
        "referentTable", "referentId", "inputId", "sourceRecordId", "documentId",
        "line", "byteStart", "byteEnd", "factKind", "factOrdinal",
    )
    for field in variant.required_non_null:
        if row.get(field) is None:
            raise ContractError(f"evidence variant requires {field}")
    for field in variant_fields:
        if field not in variant.permitted_non_null and row.get(field) is not None:
            raise ContractError(f"evidence variant forbids {field}")
    if name == "source-span" and row["byteStart"] >= row["byteEnd"]:
        raise ContractError("source span must be non-empty and half-open")


def validate_evidence_digest(row: Mapping[str, Any], exact_bytes: bytes) -> None:
    observed = hashlib.sha256(exact_bytes).hexdigest()
    if row.get("digestSha256") != observed:
        raise ContractError("evidence digest mismatch")


def validate_occurrence_cardinality(
    row: Mapping[str, Any], *, source_predicate: str
) -> None:
    occurrence_kind = row.get("occurrenceKind")
    source_matches = tuple(
        spec for spec in OCCURRENCE_SOURCES
        if spec.source_family == row.get("sourceFamily")
        and spec.source_field == row.get("sourceField")
        and spec.site_table == row.get("siteTable")
        and spec.occurrence_kind == occurrence_kind
        and spec.candidate_family == row.get("candidateFamily")
        and source_predicate in spec.source_predicate.split("|")
    )
    if len(source_matches) != 1:
        raise ContractError("occurrence does not match exactly one source-registry row")
    state = row.get("resolution")
    allowed_states = dict(OCCURRENCE_ALLOWED_STATES).get(occurrence_kind, ())
    if state not in allowed_states:
        raise ContractError("occurrence family/state pair is forbidden")
    candidate_tables = dict(CANDIDATE_TABLE_BY_FAMILY)
    if row.get("candidateFamily") not in candidate_tables:
        raise ContractError("unknown occurrence candidate family")
    expected_candidate_table = candidate_tables[row.get("candidateFamily")]
    if row.get("candidateTable") != expected_candidate_table:
        raise ContractError("occurrence candidate table disagrees with candidate family")
    candidates = row.get("candidateIds")
    if not isinstance(candidates, list):
        raise ContractError("candidateIds must be a list")
    target_count = int(row.get("targetId") is not None)
    edge_count = int(row.get("edgeId") is not None)
    residual_count = len(row.get("residualIds", ()))
    if state == "resolved":
        valid = (
            len(candidates) == target_count == edge_count == 1
            and residual_count == 0 and row.get("targetId") in candidates
        )
    elif state == "unresolved":
        valid = len(candidates) == target_count == edge_count == 0 and residual_count == 1
    elif state == "ambiguous":
        valid = len(candidates) >= 2 and target_count == edge_count == 0 and residual_count == 1
    elif state == "unsupported":
        valid = len(candidates) == target_count == edge_count == 0 and residual_count == 1
    else:
        valid = False
    if not valid:
        raise ContractError("occurrence resolution/cardinality mismatch")


def expected_occurrence_residual_kind(row: Mapping[str, Any]) -> str | None:
    mapping = next((item for item in OCCURRENCE_RESIDUALS if item[0] == row.get("occurrenceKind")), None)
    if mapping is None:
        raise ContractError("unknown occurrence residual family")
    state_index = {"unresolved": 1, "ambiguous": 2, "unsupported": 3}
    if row.get("resolution") == "resolved":
        return None
    if row.get("resolution") not in state_index:
        raise ContractError("unknown occurrence resolution state")
    result = mapping[state_index[row["resolution"]]]
    if result is None:
        raise ContractError("occurrence state has no permitted residual")
    return result


def validate_occurrence_residuals(
    row: Mapping[str, Any], residual_rows: Sequence[Mapping[str, Any]]
) -> None:
    expected_kind = expected_occurrence_residual_kind(row)
    expected_ids = row.get("residualIds")
    if expected_kind is None:
        if expected_ids != [] or residual_rows:
            raise ContractError("resolved occurrence must have no residual")
        return
    if len(residual_rows) != 1 or expected_ids != [residual_rows[0].get("id")]:
        raise ContractError("occurrence must backlink exactly one residual")
    residual = residual_rows[0]
    if (
        residual.get("residualKind") != expected_kind
        or residual.get("reasonCode") != expected_kind
        or residual.get("subjectTable") != "reference_occurrences"
        or residual.get("subjectId") != row.get("id")
        or residual.get("ordinal") != 0
    ):
        raise ContractError("occurrence residual kind/subject/cardinality mismatch")


def validate_occurrence_edge(row: Mapping[str, Any], edge: Mapping[str, Any] | None) -> None:
    if row.get("resolution") != "resolved":
        if edge is not None:
            raise ContractError("non-resolved occurrence cannot have an edge")
        return
    if edge is None:
        raise ContractError("resolved occurrence requires its edge")
    direction = next(
        (
            item for item in OCCURRENCE_EDGE_DIRECTIONS
            if item[0] == row.get("occurrenceKind") and item[1] == row.get("ownerTable")
        ),
        None,
    )
    if direction is None:
        raise ContractError("resolved occurrence has no permitted edge direction")
    expected = {
        "id": row.get("edgeId"),
        "edgeKind": direction[3],
        "sourceTable": direction[1],
        "sourceId": row.get("ownerId"),
        "targetTable": direction[2],
        "targetId": row.get("targetId"),
        "occurrenceId": row.get("id"),
        "directOrdinal": None,
    }
    if any(edge.get(key) != value for key, value in expected.items()):
        raise ContractError("occurrence edge kind/direction does not match registry")


def first_table_row_limit_violation(table: str, observed: int) -> str | None:
    if isinstance(observed, bool) or not isinstance(observed, int) or observed < 0:
        raise ContractError("row count must be a non-negative integer")
    limits = dict(BOUNDS)
    for target, bound_name, code in ROW_LIMIT_PRECEDENCE:
        applies = target == table or target == "NODE_TABLES" and table in NODE_TABLES or target == "ADMIN_TABLES" and table in ADMIN_TABLES
        if applies and observed > limits[bound_name]:
            return code
    return None


def validate_contract() -> None:
    if tuple(spec.name for spec in TABLE_SPECS) != ALL_TABLES:
        raise ContractError("table registry or order drift")
    if tuple(spec.kind for spec in EDGE_SPECS) != EDGE_KINDS or len(set(EDGE_KINDS)) != len(EDGE_KINDS):
        raise ContractError("edge registry drift")
    if tuple(spec.root_id for spec in ROOT_SPECS) != ROOT_IDS:
        raise ContractError("root registry drift")
    if tuple(priority for priority, _, _ in PARSER_DISPATCH) != tuple(range(1, 12)):
        raise ContractError("parser dispatch priority drift")
    if tuple(kind for kind, _ in OCCURRENCE_ALLOWED_STATES) != OCCURRENCE_KINDS:
        raise ContractError("occurrence state registry drift")
    if tuple(family for family, _ in CANDIDATE_TABLE_BY_FAMILY) != CANDIDATE_FAMILIES:
        raise ContractError("candidate-family table registry drift")
    for spec in TABLE_SPECS:
        names = tuple(field.name for field in spec.fields)
        if len(names) != len(set(names)) or not spec.identity:
            raise ContractError(f"{spec.name}: duplicate field or empty identity")
        if spec.edge_endpoint != (spec.name in NODE_TABLES):
            raise ContractError(f"{spec.name}: endpoint classification drift")
    for edge in EDGE_SPECS:
        if edge.source_table not in NODE_TABLES or edge.target_table not in NODE_TABLES:
            raise ContractError(f"{edge.kind}: edge endpoints must be node tables")
    if tuple(key for key, _ in DOMAIN_ROOTS) != DOMAIN_KEYS:
        raise ContractError("domain registry drift")
    # The equal-limit edge case is the explicit ledger override.
    if tuple(spec.name for spec in BOUND_SPECS[7:14]) != (
        "maxRowsPerNodeTable", "maxNodeRowsTotal", "maxReferenceOccurrences",
        "maxEdges", "maxResiduals", "maxRowsPerAdminTable", "maxRowsTotal",
    ):
        raise ContractError("resolved aggregate bound order drift")
    edge_limit = dict(BOUNDS)["maxEdges"]
    if first_table_row_limit_violation("edges", edge_limit + 1) != "E_EDGE_ROWS_LIMIT":
        raise ContractError("named edge bound must precede generic admin bound")
    if first_table_row_limit_violation("counts", dict(BOUNDS)["maxRowsPerAdminTable"] + 1) != "E_ADMIN_TABLE_ROWS_LIMIT":
        raise ContractError("generic administrative bound is unreachable")


validate_contract()


__all__ = (
    "ADMIN_TABLES", "ALL_TABLES", "ASSET_SUFFIXES", "BASELINE_ID", "BOUND_INPUT_IDS", "BOUNDS",
    "BOUND_SPECS", "CAH_KINDS", "CAMPAIGN_KINDS", "CANDIDATE_FAMILIES",
    "CANDIDATE_TABLE_BY_FAMILY", "ContractError",
    "COVERAGE_DISPOSITIONS", "DOMAIN_KEYS", "DOMAIN_ROOTS", "EDGE_KINDS",
    "DISCRIMINATED_FKS", "EDGE_SPECS", "EVIDENCE_VARIANTS", "IDENTITY_REGISTRY",
    "INPUT_ARTIFACT_DIGESTS", "LIVE_INVARIANTS", "MAP_MODE_RULES",
    "NODE_TABLES", "NORMATIVE_DESIGN_SHA256", "OCCURRENCE_KINDS",
    "OCCURRENCE_ALLOWED_STATES", "OCCURRENCE_EDGE_DIRECTIONS",
    "OCCURRENCE_OWNER_LIFT", "OCCURRENCE_RESIDUALS", "OCCURRENCE_SOURCES",
    "PARSER_DISPATCH", "PARSER_FAMILIES", "RESIDUAL_KINDS",
    "RESOLUTION_CARDINALITY", "ROOT_IDS", "ROOT_SPECS", "ROW_LIMIT_PRECEDENCE",
    "SCHEMA_NAME", "SCHEMA_VERSION", "SELECTOR_STATE_RULES", "SHELL_KINDS",
    "SKIRMISH_KINDS", "SOURCE_SCHEMA_INPUT_SHAPE",
    "TABLE_SPECS", "TRAVERSAL_COMMON", "TRAVERSAL_EFFECTIVE", "WOTR_KINDS",
    "canonical_atom", "canonical_path", "canonical_preimage", "expected_id",
    "first_table_row_limit_violation", "identity_preimage", "table_spec",
    "validate_contract", "validate_evidence_digest", "validate_evidence_variant",
    "expected_occurrence_residual_kind", "validate_identity",
    "validate_occurrence_cardinality", "validate_occurrence_edge",
    "validate_occurrence_residuals", "validate_row_shape",
    "validate_unique_identities",
)
