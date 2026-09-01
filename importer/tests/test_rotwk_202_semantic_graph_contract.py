from __future__ import annotations

from dataclasses import FrozenInstanceError
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest import mock

from openbfme_importer import semantic_graph_contract as contract


# These fixtures are transcribed from the frozen owner design.  They are not
# assembled from production registries.
EXPECTED_TABLES = tuple(
    "source_records documents definitions assignments object_occurrences "
    "module_declarations module_kinds assets opaque_tokens script_calls "
    "nested_sites directive_sites unknown_sites maps map_objects map_libraries "
    "apt_movies apt_symbols wnd_windows parser_dispositions evidence_referents "
    "mapcache_selection reference_occurrences edges selector_dispositions "
    "root_results root_memberships root_traversals root_residual_links "
    "domain_dispositions map_dispositions residuals counts".split()
)

EXPECTED_ARTIFACT_REGISTRY_ORDER = (
    "INPUT_ARTIFACT_DIGESTS", "BOUND_INPUT_IDS", "SOURCE_SCHEMA_INPUT_SHAPE",
    "NODE_TABLES", "ADMIN_TABLES", "ALL_TABLES", "DOMAIN_KEYS", "ROOT_IDS",
    "RESIDUAL_KINDS", "PARSER_FAMILIES", "COVERAGE_DISPOSITIONS",
    "OCCURRENCE_KINDS", "CANDIDATE_FAMILIES", "ASSET_SUFFIXES",
    "SKIRMISH_KINDS", "WOTR_KINDS", "CAH_KINDS", "CAMPAIGN_KINDS",
    "SHELL_KINDS", "IDENTITY_REGISTRY", "EDGE_DECLARATIONS", "EDGE_SPECS",
    "EDGE_KINDS", "OCCURRENCE_SOURCES", "CANDIDATE_TABLE_BY_FAMILY",
    "OCCURRENCE_ALLOWED_STATES", "OCCURRENCE_OWNER_LIFT",
    "OCCURRENCE_RESIDUALS", "OCCURRENCE_EDGE_DIRECTIONS",
    "DISCRIMINATED_FKS", "RESOLUTION_CARDINALITY", "EVIDENCE_VARIANTS",
    "TRAVERSAL_COMMON", "TRAVERSAL_EFFECTIVE", "ROOT_SPECS",
    "SELECTOR_STATE_RULES", "DOMAIN_ROOTS", "MAP_MODE_RULES",
    "PARSER_DISPATCH", "BOUND_SPECS", "BOUNDS", "ROW_LIMIT_PRECEDENCE",
    "LIVE_INVARIANTS", "TABLE_SPECS",
)

EXPECTED_ARTIFACT_COUNTS = {
    "registryCount": 44,
    "tableCount": 33,
    "nodeTableCount": 20,
    "adminTableCount": 13,
    "edgeKindCount": 50,
    "occurrenceSourceCount": 11,
    "rootCount": 8,
    "residualKindCount": 37,
    "boundCount": 15,
}

EXPECTED_ARTIFACT_IMPLEMENTATION_PATHS = (
    "importer/openbfme_importer/semantic_graph_contract.py",
    "importer/tests/test_rotwk_202_semantic_graph_contract.py",
    "tools/check-rotwk-202-semantic-contract.py",
)

EXPECTED_ARTIFACT_DIGESTS = {
    "canonicalAtomVectorSha256": "d0a281275c9f25331320fc7fed10046cc8f07a5be443c4df897d65f96831387a",
    "fieldContractSha256": "c52402837d60d07817ea7a2da6c194fc5e351ba8dd5ed808ad5fbeb8eb3ce0d2",
    "registryPayloadSha256": "2c6b185f7a32002ff705d2ce1859fc704a834ab8f5dfd760be7cbef1671d505b",
}

EXPECTED_ARTIFACT_KEYS = (
    "baselineId", "contentSha256", "counts", "expected",
    "implementationClosure", "normativeDesignSha256", "registries",
    "registryOrder", "schema", "schemaVersion", "semanticSchema",
    "semanticSchemaVersion",
)

EXPECTED_FIELD_ROWS = """
source_records|id,kind,evidenceIds,residualIds,rawCatalogIndex,recordIndex,key,archive,member,offset,size,precedence,payloadSha256,disposition,chainIndex,shadowSemantics
documents|id,kind,evidenceIds,residualIds,sourceRecordId,path,archive,bytes,precedence,sha256,coverageDisposition,lexicalDocumentId
definitions|id,kind,evidenceIds,residualIds,documentId,lexicalId,definitionKind,name,sourceIni,line,occurrenceIndex,categories
assignments|id,kind,evidenceIds,residualIds,documentId,lexicalId,ownerTable,ownerId,sourceIni,rootKind,rootName,field,line,occurrenceIndex,valueSha256,valueTokenCount,moduleKind,categories,classification
object_occurrences|id,kind,evidenceIds,residualIds,documentId,lexicalId,definitionId,objectKind,objectId,parentToken,sourceIni,side,kindOf,commandSets,buildCostExpressions,buildTimeExpressions,moduleCounts,resolvedSides,resolvedKindOf,categories,acceptedObjectOrdinal,bindingState,candidateDefinitionIds
module_declarations|id,kind,evidenceIds,residualIds,assignmentId,ownerTable,ownerId,moduleKindId,moduleKindSpelling,line,occurrenceIndex
module_kinds|id,kind,evidenceIds,residualIds,casefoldName,declarationIds
assets|id,kind,evidenceIds,residualIds,sourceRecordId,path,stem,suffix,payloadSha256,assetClass
opaque_tokens|id,kind,evidenceIds,residualIds,assignmentId,occurrenceOrdinal,start,end,token,state
script_calls|id,kind,evidenceIds,residualIds,documentId,ownerTable,ownerId,lexicalId,line,occurrenceOrdinal,callName,argumentsSha256,sourceIni,rootKind,rootName,categories
nested_sites|id,kind,evidenceIds,residualIds,documentId,ownerTable,ownerId,lexicalId,line,occurrenceIndex,depth,siteKind,sourceIni,rootKind,rootName,selectorSha256,categories
directive_sites|id,kind,evidenceIds,residualIds,documentId,lexicalId,line,occurrenceIndex,directive,payloadSha256,sourceIni
unknown_sites|id,kind,evidenceIds,residualIds,documentId,ownerTable,ownerId,lexicalId,line,occurrenceIndex,rawSha256,rawLength,sourceIni,rootKind
maps|id,kind,evidenceIds,residualIds,sourceRecordId,path,payloadSha256,parserDispositionId,mapCacheSelectionId,mapCacheState,isMultiplayer,isScenarioMp,mapCacheEvidenceIds,parseState
map_objects|id,kind,evidenceIds,residualIds,mapId,objectOrdinal,typeToken,factsSha256
map_libraries|id,kind,evidenceIds,residualIds,mapId,referenceOrdinal,pathToken,factsSha256
apt_movies|id,kind,evidenceIds,residualIds,sourceRecordId,path,directory,stem,payloadSha256,admissionState,constAssetId,datAssetId,geometryAssetIds,parserDispositionId,parseState
apt_symbols|id,kind,evidenceIds,residualIds,aptMovieId,symbolOrdinal,symbolName,symbolKind,factsSha256
wnd_windows|id,kind,evidenceIds,residualIds,sourceRecordId,windowOrdinal,parentWindowId,name,callbacks,factsSha256,parserDispositionId
parser_dispositions|id,kind,evidenceIds,residualIds,sourceRecordId,parserFamily,state,reasonCode,reasonSha256
evidence_referents|id,referentKind,referentTable,referentId,inputId,sourceRecordId,documentId,line,byteStart,byteEnd,factKind,factOrdinal,digestSha256
mapcache_selection|id,virtualPath,state,candidateSourceIds,selectedSourceId,sourceParserDispositionId,factsSha256,reasonSha256,evidenceIds,residualIds
reference_occurrences|id,occurrenceKind,sourceFamily,sourceField,sourceOrdinal,siteTable,siteId,ownerTable,ownerId,occurrenceOrdinal,token,lookupToken,candidateFamily,candidateTable,resolution,candidateIds,targetId,edgeId,evidenceIds,residualIds
edges|id,edgeKind,sourceTable,sourceId,targetTable,targetId,occurrenceId,directOrdinal,evidenceIds
selector_dispositions|id,rootId,candidateTable,candidateId,state,ruleId,promotionEdgeId,evidenceIds
root_results|id,rootId,state,seedCount,promotedSeedCount,traversedEdgeCount,reachedNodeCount,residualCount,seedSetSha256,promotedSeedSetSha256,traversedEdgeSetSha256,reachedNodeSetSha256,residualSetSha256,iterations,evidenceIds
root_memberships|id,rootId,membershipKind,memberTable,memberId,firstIteration,evidenceIds
root_traversals|id,rootId,edgeId,iteration,evidenceIds
root_residual_links|id,rootId,residualId,evidenceIds
domain_dispositions|id,nodeTable,nodeId,states,evidenceIds,residualIds
map_dispositions|id,mapId,modeState,candidateModes,domainStates,evidenceIds,residualIds
residuals|id,residualKind,subjectTable,subjectId,ordinal,reasonCode,reasonSha256,evidenceIds
counts|id,tableCounts,edgeKindCounts,occurrenceKindCounts,residualKindCounts,parserFamilyCounts,coverageDispositionCounts,evidenceIds
""".strip().splitlines()

EXPECTED_FIELDS = {
    table: tuple(fields.split(","))
    for table, fields in (line.split("|", 1) for line in EXPECTED_FIELD_ROWS)
}

EXPECTED_IDENTITY_ROWS = """
source_records|source-record|SRC-|rawCatalogIndex:int,recordIndex:int,archive:path,member:path,offset:int,size:int,payloadSha256:sha256
documents|document|DOC-|sourceRecordId:id,coverageDisposition:enum
definitions|definition|DEF-|documentId:id,line:int,occurrenceIndex:int,definitionKind:ci,name:ci-null
assignments|assignment|ASN-|documentId:id,line:int,occurrenceIndex:int,field:ci
object_occurrences|object-occurrence|OBJ-|documentId:id,acceptedObjectOrdinal:int,objectKind:enum,objectId:ci
module_declarations|module-declaration|MOD-|assignmentId:id,occurrenceIndex:int,moduleKindSpelling:ci
module_kinds|module-kind|MKD-|casefoldName:ci
assets|asset|AST-|sourceRecordId:id
opaque_tokens|opaque-token|TOK-|assignmentId:id,occurrenceOrdinal:int,start:int,end:int,token:cs
script_calls|script-call|SCR-|documentId:id,line:int,occurrenceOrdinal:int,callName:cs
nested_sites|nested-site|NST-|documentId:id,line:int,occurrenceIndex:int,depth:int,siteKind:cs
directive_sites|directive-site|DIR-|documentId:id,line:int,occurrenceIndex:int,directive:cs
unknown_sites|unknown-site|UNK-|documentId:id,line:int,occurrenceIndex:int,rawSha256:sha256
maps|map|MAP-|sourceRecordId:id
map_objects|map-object|MOB-|mapId:id,objectOrdinal:int
map_libraries|map-library|MLB-|mapId:id,referenceOrdinal:int
apt_movies|apt-movie|APM-|sourceRecordId:id
apt_symbols|apt-symbol|APS-|aptMovieId:id,symbolOrdinal:int
wnd_windows|wnd-window|WND-|sourceRecordId:id,windowOrdinal:int
parser_dispositions|parser-disposition|PAR-|sourceRecordId:id
evidence_referents|evidence-referent|EVID-|referentKind:enum,referentTable:enum-null,referentId:id-null,inputId:enum-null,sourceRecordId:id-null,documentId:id-null,line:int-null,byteStart:int-null,byteEnd:int-null,factKind:cs-null,factOrdinal:int-null,digestSha256:sha256
mapcache_selection|mapcache-selection|MCS-|maps/mapcache.ini:literal-path
reference_occurrences|reference-occurrence|OCC-|sourceFamily:enum,sourceField:enum,sourceOrdinal:int,occurrenceKind:enum,siteId:id,occurrenceOrdinal:int,token:cs
edges|edge|EDG-|edgeKind:enum,sourceId:id,targetId:id,occurrenceId:id-null,directOrdinal:int-null
selector_dispositions|selector-disposition|SEL-|rootId:enum,candidateId:id
root_results|root-result|ROT-|rootId:enum
root_memberships|root-membership|RMB-|rootId:enum,membershipKind:enum,memberId:id
root_traversals|root-traversal|RTR-|rootId:enum,edgeId:id
root_residual_links|root-residual-link|RRL-|rootId:enum,residualId:id
domain_dispositions|domain-disposition|DOM-|nodeId:id
map_dispositions|map-disposition|MDP-|mapId:id
residuals|residual|RES-|residualKind:enum,subjectId:id,ordinal:int
counts|counts|CNT-|graph:literal-cs
""".strip().splitlines()


def _parse_identity_rows() -> tuple[tuple[str, str, str, tuple[tuple[str, str], ...]], ...]:
    rows = []
    for line in EXPECTED_IDENTITY_ROWS:
        table, type_tag, prefix, parts = line.split("|", 3)
        rows.append((table, type_tag, prefix, tuple(tuple(part.split(":")) for part in parts.split(","))))
    return tuple(rows)


EXPECTED_EDGE_ROWS = """
source-document|source_records|documents|structural|1/T
source-asset|source_records|assets|structural|1/T
source-map|source_records|maps|structural|1/T
source-apt-movie|source_records|apt_movies|structural|1/T
source-wnd-window|source_records|wnd_windows|structural|1/T top-level
source-parser-disposition|source_records|parser_dispositions|structural|1/T and 1/S
document-definition|documents|definitions|structural|1/T
document-assignment|documents|assignments|structural|1/T
document-object-occurrence|documents|object_occurrences|structural|1/T
document-script-call|documents|script_calls|structural|1/T
document-nested-site|documents|nested_sites|structural|1/T
document-directive-site|documents|directive_sites|structural|1/T
document-unknown-site|documents|unknown_sites|structural|1/T
definition-assignment|definitions|assignments|structural|1/T iff definition-owned
object-binding|object_occurrences|definitions|structural|1 iff resolved
module-owner-definition|definitions|module_declarations|structural|1/T iff definition-owned
module-owner-document|documents|module_declarations|structural|1/T iff document-owned
module-taxonomy|module_declarations|module_kinds|occurrence|1/S and 1 occurrence edge
assignment-opaque-token|assignments|opaque_tokens|structural|1/T
map-object-member|maps|map_objects|structural|1/T
map-library-member|maps|map_libraries|structural|1/T
apt-symbol-member|apt_movies|apt_symbols|structural|1/T
wnd-child|wnd_windows|wnd_windows|structural|1/T non-root, same-source, acyclic
precedence-chain|source_records|source_records|structural|1/T shadow, increasing chainIndex
definition-reference-from-definition|definitions|definitions|occurrence|one iff resolved
definition-reference-from-document|documents|definitions|occurrence|one iff resolved
asset-reference-from-definition|definitions|assets|occurrence|one iff resolved
asset-reference-from-document|documents|assets|occurrence|one iff resolved
object-parent|definitions|definitions|occurrence|one iff resolved
map-object-type|map_objects|definitions|occurrence|one iff resolved
map-library-reference|map_libraries|maps|occurrence|one iff resolved
apt-import|apt_movies|apt_movies|occurrence|one iff resolved
apt-const-companion|apt_movies|assets|structural|targets(S)={constAssetId}|empty
apt-dat-companion|apt_movies|assets|structural|targets(S)={datAssetId}|empty
apt-geometry-companion|apt_movies|assets|structural|targets(S)=geometryAssetIds; 1/T
apt-const-parser-disposition|apt_movies|parser_dispositions|structural|targets(S)=const parser|empty
apt-dat-parser-disposition|apt_movies|parser_dispositions|structural|targets(S)=dat parser|empty
apt-geometry-parser-disposition|apt_movies|parser_dispositions|structural|targets(S)=geometry parsers
parser-owns-map|parser_dispositions|maps|structural|1/T
parser-owns-map-object|parser_dispositions|map_objects|structural|1/T
parser-owns-map-library|parser_dispositions|map_libraries|structural|1/T
parser-owns-apt-movie|parser_dispositions|apt_movies|structural|1/T
parser-owns-apt-symbol|parser_dispositions|apt_symbols|structural|1/T
parser-owns-wnd-window|parser_dispositions|wnd_windows|structural|1/T
map-parser-disposition|maps|parser_dispositions|structural|1/S same-source
map-object-parser-disposition|map_objects|parser_dispositions|structural|1/S via map
map-library-parser-disposition|map_libraries|parser_dispositions|structural|1/S via map
apt-movie-parser-disposition|apt_movies|parser_dispositions|structural|1/S same-source
apt-symbol-parser-disposition|apt_symbols|parser_dispositions|structural|1/S via movie
wnd-parser-disposition|wnd_windows|parser_dispositions|structural|1/S same-source
""".strip().splitlines()

EXPECTED_EDGES = tuple(tuple(line.split("|", 4)) for line in EXPECTED_EDGE_ROWS)

EXPECTED_OCCURRENCES = (
    ("lexical-asset-reference", "assetReferences.retailResolution", "ini-definition", "assignments", "ini-definition-reference", "definition-any-kind"),
    ("lexical-asset-reference", "assetReferences.retailResolution", "retail-asset|ambiguous-retail-asset", "assignments", "retail-asset-path", "effective-path"),
    ("lexical-asset-reference", "assetReferences.retailResolution", "retail-asset-stem|ambiguous-retail-stem", "assignments", "retail-asset-stem", "effective-stem"),
    ("lexical-asset-reference", "assetReferences.retailResolution", "unresolved-candidate", "assignments", "unresolved-retail-candidate", "untyped-retail-candidate"),
    ("lexical-object-parent", "objects.parent", "non-null parentToken", "object_occurrences", "object-parent", "definition-object-kind"),
    ("lexical-module-kind", "assignments.moduleKind", "non-null moduleKind", "module_declarations", "module-kind-declaration", "module-kind-taxonomy"),
    ("lexical-script-call", "scriptCalls.command", "every accepted script call", "script_calls", "script-call", "unsupported-script"),
    ("map-object-fact", "mapObjects.typeToken", "every emitted map-object fact", "map_objects", "map-object-type", "definition-object-kind"),
    ("map-library-fact", "mapLibraries.pathToken", "every emitted library fact", "map_libraries", "map-library-reference", "map-path"),
    ("apt-import-fact", "aptMovies.import", "every emitted APT import fact", "apt_movies", "apt-import", "apt-same-directory-stem"),
    ("wnd-callback-fact", "wndWindows.callbacks", "every callback in parser order", "wnd_windows", "wnd-callback", "unsupported-wnd-callback"),
)

EXPECTED_CANDIDATE_TABLES = (
    ("definition-any-kind", "definitions"), ("definition-object-kind", "definitions"),
    ("effective-path", "assets"), ("effective-stem", "assets"),
    ("untyped-retail-candidate", "assets"), ("map-path", "maps"),
    ("apt-same-directory-stem", "apt_movies"), ("module-kind-taxonomy", "module_kinds"),
    ("unsupported-script", None), ("unsupported-wnd-callback", None),
)

EXPECTED_OCCURRENCE_ALLOWED_STATES = (
    ("ini-definition-reference", ("resolved", "unresolved", "ambiguous")),
    ("retail-asset-path", ("resolved", "unresolved", "ambiguous")),
    ("retail-asset-stem", ("resolved", "unresolved", "ambiguous")),
    ("unresolved-retail-candidate", ("unresolved",)),
    ("object-parent", ("resolved", "unresolved", "ambiguous")),
    ("map-object-type", ("resolved", "unresolved", "ambiguous")),
    ("map-library-reference", ("resolved", "unresolved", "ambiguous")),
    ("apt-import", ("resolved", "unresolved", "ambiguous")),
    ("module-kind-declaration", ("resolved",)),
    ("script-call", ("unsupported",)), ("wnd-callback", ("unsupported",)),
)

EXPECTED_OCCURRENCE_OWNER_LIFT = (
    ("ini-definition-reference", "assignment owner definition/document"),
    ("retail-asset-path", "assignment owner definition/document"),
    ("retail-asset-stem", "assignment owner definition/document"),
    ("unresolved-retail-candidate", "assignment owner definition/document"),
    ("object-parent", "resolved child definition, else object occurrence"),
    ("map-object-type", "map object"), ("map-library-reference", "map library"),
    ("apt-import", "importing movie"), ("module-kind-declaration", "module declaration"),
    ("script-call", "script call"), ("wnd-callback", "WND window"),
)

EXPECTED_OCCURRENCE_RESIDUALS = (
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

EXPECTED_OCCURRENCE_DIRECTIONS = (
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

EXPECTED_RESOLUTION_CARDINALITY = (
    ("resolved", 1, 1, 1, 0), ("unresolved", 0, 0, 0, 1),
    ("ambiguous", 2, None, 0, 1), ("unsupported", 0, 0, 0, 1),
)

EXPECTED_DISCRIMINATED_FKS = (
    ("assignments", "ownerTable", ("ownerId",)),
    ("module_declarations", "ownerTable", ("ownerId",)),
    ("script_calls", "ownerTable", ("ownerId",)),
    ("nested_sites", "ownerTable", ("ownerId",)),
    ("unknown_sites", "ownerTable", ("ownerId",)),
    ("evidence_referents", "referentTable", ("referentId",)),
    ("reference_occurrences", "siteTable", ("siteId",)),
    ("reference_occurrences", "ownerTable", ("ownerId",)),
    ("reference_occurrences", "candidateTable", ("candidateIds", "targetId")),
    ("edges", "sourceTable", ("sourceId",)), ("edges", "targetTable", ("targetId",)),
    ("selector_dispositions", "candidateTable", ("candidateId",)),
    ("root_memberships", "memberTable", ("memberId",)),
    ("domain_dispositions", "nodeTable", ("nodeId",)),
    ("residuals", "subjectTable", ("subjectId",)),
)

EXPECTED_EVIDENCE_VARIANTS = (
    ("output-row", ("referentTable", "referentId"), ("referentTable", "referentId")),
    ("input-artifact", ("inputId",), ("inputId",)),
    ("source-span", ("sourceRecordId", "line", "byteStart", "byteEnd"), ("sourceRecordId", "documentId", "line", "byteStart", "byteEnd")),
    ("parser-fact", ("sourceRecordId", "factKind", "factOrdinal"), ("sourceRecordId", "factKind", "factOrdinal")),
)

EXPECTED_ROOT_SPECS = (
    ("effective-retail-winners", ("source_records",), ("all-winning-source",), "TRAVERSAL_EFFECTIVE"),
    ("retail-reference-closure", ("definitions",), ("all-definition",), "TRAVERSAL_COMMON"),
    ("all-map-payloads", ("maps",), ("all-map",), "TRAVERSAL_COMMON"),
    ("skirmish-roots", ("definitions", "maps"), ("literal-skirmish-kind", "mapcache-multiplayer"), "TRAVERSAL_COMMON"),
    ("campaign-roots", ("definitions", "maps"), ("literal-campaign-kind",), "TRAVERSAL_COMMON"),
    ("war-of-the-ring-roots", ("definitions", "maps"), ("literal-wotr-kind",), "TRAVERSAL_COMMON"),
    ("create-a-hero-roots", ("definitions", "maps"), ("literal-cah-kind",), "TRAVERSAL_COMMON"),
    ("shell-roots", ("definitions", "maps", "apt_movies", "wnd_windows", "source_records"), ("literal-shell-kind", "all-shell-apt", "all-shell-wnd", "all-shell-str"), "TRAVERSAL_COMMON"),
)

EXPECTED_SELECTOR_RULES = (
    ("not-seed", ("not-selected",), "promotionEdgeId=null"),
    ("seed", ("all-winning-source", "all-definition", "all-map", "literal-skirmish-kind", "literal-campaign-kind", "literal-wotr-kind", "literal-cah-kind", "literal-shell-kind", "mapcache-multiplayer", "all-shell-apt", "all-shell-wnd", "all-shell-str"), "promotionEdgeId=null"),
    ("promoted-seed", ("incoming-owner-promotion", "reached-map-promotion"), "promotionEdgeId=lowest-ASCII-causal-edge"),
)

EXPECTED_MAP_MODES = (
    ("skirmish-multiplayer", "mapcache resolved and isMultiplayer=true and isScenarioMp=false"),
    ("scenario-multiplayer", "mapcache resolved and isMultiplayer=true and isScenarioMp=true"),
    ("library", "resolved LibraryMapLists occurrence targets map"),
    ("campaign-scenario", "campaign-roots reached-node membership"),
    ("war-of-the-ring-battle", "war-of-the-ring-roots reached-node membership"),
)

EXPECTED_PARSER_DISPATCH = (
    (1, "effective winner and exact accepted lexical tuple", "accepted-lexical-ini"),
    (2, "winning .ini/.inc absent from lexical input", "nonlexical-effective-ini"),
    (3, "additive shadow .ini/.inc absent from lexical input", "unscanned-additive-shadow-ini"),
    (4, "any other shadow", "unscanned-shadow"), (5, "winning .map", "sage-map"),
    (6, "winning .apt", "apt-movie"),
    (7, "winning sole same-directory/casefolded-stem .const", "apt-constants"),
    (8, "winning .dat in exact admitted apt/const/dat triplet", "apt-dat"),
    (9, "winning safe <directory>/<stem>_geometry/<decimal-id>.ru", "apt-geometry"),
    (10, "winning .wnd", "wnd-layout"), (11, "every other winning record", "identity-only"),
)

EXPECTED_SOURCE_SCHEMA_SHAPE = (
    ("inputId", "source-schema"), ("algorithm", "python-ast-local-import-closure-v1"),
    ("rowFields", ("path", "sha256")), ("pathOrder", "ASCII"),
    ("rowEncoding", "utf8-lf:path|sha256\\n"),
    ("derivationOwner", "P0-CORPUS-SCHEMA-001 facade"),
)

EXPECTED_BOUNDS = (
    ("maxJsonDepth", 16), ("maxContainerItems", 1_000_000),
    ("maxStringUtf8Bytes", 1_048_576), ("maxJsonlLineBytes", 1_048_576),
    ("maxRowsPerShard", 100_000), ("maxBytesPerShard", 67_108_864),
    ("maxShardsPerTable", 4_096), ("maxRowsPerNodeTable", 1_000_000),
    ("maxNodeRowsTotal", 5_000_000), ("maxReferenceOccurrences", 5_000_000),
    ("maxEdges", 10_000_000), ("maxResiduals", 5_000_000),
    ("maxRowsPerAdminTable", 10_000_000), ("maxRowsTotal", 20_000_000),
    ("maxOutputBytes", 8_589_934_592),
)

EXPECTED_BOUND_SPECS = (
    ("maxJsonDepth", 16, "E_JSON_DEPTH_LIMIT"),
    ("maxContainerItems", 1_000_000, "E_CONTAINER_ITEMS_LIMIT"),
    ("maxStringUtf8Bytes", 1_048_576, "E_STRING_BYTES_LIMIT"),
    ("maxJsonlLineBytes", 1_048_576, "E_LINE_BYTES_LIMIT"),
    ("maxRowsPerShard", 100_000, "E_SHARD_ROWS_LIMIT"),
    ("maxBytesPerShard", 67_108_864, "E_SHARD_BYTES_LIMIT"),
    ("maxShardsPerTable", 4_096, "E_SHARD_COUNT_LIMIT"),
    ("maxRowsPerNodeTable", 1_000_000, "E_NODE_TABLE_ROWS_LIMIT"),
    ("maxNodeRowsTotal", 5_000_000, "E_NODE_ROWS_TOTAL_LIMIT"),
    ("maxReferenceOccurrences", 5_000_000, "E_OCCURRENCE_ROWS_LIMIT"),
    ("maxEdges", 10_000_000, "E_EDGE_ROWS_LIMIT"),
    ("maxResiduals", 5_000_000, "E_RESIDUAL_ROWS_LIMIT"),
    ("maxRowsPerAdminTable", 10_000_000, "E_ADMIN_TABLE_ROWS_LIMIT"),
    ("maxRowsTotal", 20_000_000, "E_TOTAL_ROWS_LIMIT"),
    ("maxOutputBytes", 8_589_934_592, "E_OUTPUT_BYTES_LIMIT"),
)

EXPECTED_ROW_LIMIT_PRECEDENCE = (
    ("reference_occurrences", "maxReferenceOccurrences", "E_OCCURRENCE_ROWS_LIMIT"),
    ("edges", "maxEdges", "E_EDGE_ROWS_LIMIT"),
    ("residuals", "maxResiduals", "E_RESIDUAL_ROWS_LIMIT"),
    ("NODE_TABLES", "maxRowsPerNodeTable", "E_NODE_TABLE_ROWS_LIMIT"),
    ("ADMIN_TABLES", "maxRowsPerAdminTable", "E_ADMIN_TABLE_ROWS_LIMIT"),
)

EXPECTED_PREFIXES = {
    table: prefix for table, _, prefix, _ in _parse_identity_rows()
}


def _value_signature(value: contract.ValueSpec) -> str:
    suffix = "?" if value.nullable else ""
    if value.kind in ("enum", "literal"):
        return f"{value.kind}{{{','.join(value.values)}}}{suffix}"
    if value.kind in ("list", "set"):
        return f"{value.kind}<{_value_signature(value.item)}>{suffix}"
    if value.kind == "record":
        body = ",".join(f"{name}:{_value_signature(child)}" for name, child in value.fields)
        return f"record{{{body}}}{suffix}"
    if value.kind == "map":
        return f"map{{{','.join(value.values)}}}<{_value_signature(value.item)}>{suffix}"
    return value.kind + suffix


def _field_contract_bytes() -> bytes:
    rows = []
    for table in contract.TABLE_SPECS:
        fields = []
        for field in table.fields:
            fk = "->" + ",".join(field.fk_tables) if field.fk_tables else ""
            fields.append(f"{field.name}:{_value_signature(field.value)}{fk}")
        rows.append(f"{table.name}|{';'.join(fields)}")
    return ("\n".join(rows) + "\n").encode("utf-8")


def _id(prefix: str) -> str:
    return prefix + "0" * 64


def _valid_value(value: contract.ValueSpec):
    if value.nullable:
        return None
    if value.kind == "s":
        return "x"
    if value.kind == "path":
        return "x"
    if value.kind == "sha256":
        return "a" * 64
    if value.kind == "id":
        return _id("SRC-")
    if value.kind == "eid":
        return _id("EVID-")
    if value.kind in ("u16", "u32", "u64", "i64"):
        return 0
    if value.kind == "b":
        return False
    if value.kind in ("enum", "literal"):
        return value.values[0]
    if value.kind in ("list", "set"):
        return []
    if value.kind == "record":
        return {name: _valid_value(child) for name, child in value.fields}
    if value.kind == "map":
        return {key: _valid_value(value.item) for key in value.values}
    raise AssertionError(value.kind)


def _valid_shape_row(table: contract.TableSpec) -> dict:
    row = {field.name: _valid_value(field.value) for field in table.fields}
    row["id"] = _id(table.prefix)
    for field in table.fields:
        if not field.fk_tables or row[field.name] is None or isinstance(row[field.name], list):
            continue
        row[field.name] = _id(EXPECTED_PREFIXES[field.fk_tables[0]])
    for declared_table, discriminator, id_fields in EXPECTED_DISCRIMINATED_FKS:
        if declared_table != table.name or row[discriminator] is None:
            continue
        prefix = EXPECTED_PREFIXES[row[discriminator]]
        for field in id_fields:
            if row[field] is None:
                continue
            row[field] = [_id(prefix)] if isinstance(row[field], list) else _id(prefix)
    return row


def _fixture_json_bytes(value, *, trailing_lf: bool = False) -> bytes:
    raw = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return raw + (b"\n" if trailing_lf else b"")


def _assert_no_ambient_path_or_payload(value) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if key in ("payload", "payloadBytes", "retailBytes", "absolutePath"):
                raise AssertionError(f"private or ambient artifact key: {key}")
            _assert_no_ambient_path_or_payload(child)
    elif isinstance(value, list):
        for child in value:
            _assert_no_ambient_path_or_payload(child)
    elif isinstance(value, str):
        if re.search(r"(?:^|[^A-Za-z])[A-Za-z]:[\\/]", value) or value.startswith(("/", "\\\\")):
            raise AssertionError(f"ambient absolute path in artifact: {value}")


def _fixture_source_bytes(path: Path) -> bytes:
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf") or b"\x00" in raw:
        raise AssertionError(f"non-portable implementation source: {path.name}")
    return raw.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n").encode("utf-8")


def validate_contract_artifact(document: dict, root: Path | None = None) -> None:
    root = Path(__file__).resolve().parents[2] if root is None else Path(root)
    if tuple(sorted(document)) != EXPECTED_ARTIFACT_KEYS:
        raise AssertionError("artifact top-level fields differ from literal fixture")
    expected_scalars = {
        "schema": "openbfme.rotwk-202-semantic-contract-core",
        "schemaVersion": 1,
        "semanticSchema": "openbfme.rotwk-202-semantic-schema-manifest",
        "semanticSchemaVersion": 4,
        "baselineId": "rotwk-202-v9.7.7-en",
        "normativeDesignSha256": "d5f9d8fc12a631156b4c3b73737f23faae8e40c71b7577a800e06f6c2e39090c",
    }
    for field, expected in expected_scalars.items():
        if document.get(field) != expected:
            raise AssertionError(f"artifact {field} differs from literal fixture")
    if tuple(document.get("registryOrder", ())) != EXPECTED_ARTIFACT_REGISTRY_ORDER:
        raise AssertionError("artifact registry order differs from literal fixture")
    registries = document.get("registries")
    if not isinstance(registries, list) or tuple(row.get("name") for row in registries) != EXPECTED_ARTIFACT_REGISTRY_ORDER:
        raise AssertionError("artifact registry rows differ from literal fixture")
    registry_digest = hashlib.sha256(_fixture_json_bytes(registries)).hexdigest()
    if registry_digest != EXPECTED_ARTIFACT_DIGESTS["registryPayloadSha256"]:
        raise AssertionError("artifact registry payload differs from frozen contract")
    if document.get("counts") != EXPECTED_ARTIFACT_COUNTS:
        raise AssertionError("artifact counts differ from literal fixture")
    if document.get("expected") != EXPECTED_ARTIFACT_DIGESTS:
        raise AssertionError("artifact expected digests differ from literal fixture")

    closure = document.get("implementationClosure")
    if not isinstance(closure, dict) or tuple(sorted(closure)) != (
        "closureSha256", "files", "hashProfile", "pathOrder"
    ):
        raise AssertionError("artifact implementation closure fields differ")
    if closure.get("hashProfile") != "utf8-lf" or tuple(closure.get("pathOrder", ())) != EXPECTED_ARTIFACT_IMPLEMENTATION_PATHS:
        raise AssertionError("artifact implementation closure policy differs")
    files = closure.get("files")
    if not isinstance(files, list) or tuple(row.get("path") for row in files) != EXPECTED_ARTIFACT_IMPLEMENTATION_PATHS:
        raise AssertionError("artifact implementation closure paths differ")
    actual_closure_rows = []
    for row in files:
        if tuple(sorted(row)) != ("bytes", "path", "sha256"):
            raise AssertionError("artifact implementation file row fields differ")
        if type(row["bytes"]) is not int or row["bytes"] <= 0:
            raise AssertionError("artifact implementation byte count is invalid")
        if not re.fullmatch(r"[0-9a-f]{64}", row["sha256"]):
            raise AssertionError("artifact implementation digest is invalid")
        source = _fixture_source_bytes(root / row["path"])
        actual_closure_rows.append({
            "path": row["path"], "bytes": len(source),
            "sha256": hashlib.sha256(source).hexdigest(),
        })
    if files != actual_closure_rows:
        raise AssertionError("artifact implementation closure is stale")
    closure_bytes = "".join(
        f"{row['path']}|{row['sha256']}\n" for row in files
    ).encode("utf-8")
    if closure.get("closureSha256") != hashlib.sha256(closure_bytes).hexdigest():
        raise AssertionError("artifact implementation closure digest mismatch")

    content = dict(document)
    claimed_content = content.pop("contentSha256", None)
    if claimed_content != hashlib.sha256(_fixture_json_bytes(content)).hexdigest():
        raise AssertionError("artifact content digest mismatch")
    _assert_no_ambient_path_or_payload(document)


def _checker_module():
    name = "_openbfme_semantic_contract_checker"
    module = sys.modules.get(name)
    if module is not None:
        return module
    root = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location(
        name, root / "tools" / "check-rotwk-202-semantic-contract.py"
    )
    if spec is None or spec.loader is None:
        raise AssertionError("cannot load semantic contract checker")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def _refresh_content_digest(document: dict) -> dict:
    result = copy.deepcopy(document)
    result.pop("contentSha256", None)
    result["contentSha256"] = hashlib.sha256(_fixture_json_bytes(result)).hexdigest()
    return result


class RegistryTests(unittest.TestCase):
    def test_table_fields_types_enums_and_fks_match_frozen_fixture(self):
        self.assertEqual(tuple(spec.name for spec in contract.TABLE_SPECS), EXPECTED_TABLES)
        self.assertEqual(
            {spec.name: tuple(field.name for field in spec.fields) for spec in contract.TABLE_SPECS},
            EXPECTED_FIELDS,
        )
        self.assertEqual(
            hashlib.sha256(_field_contract_bytes()).hexdigest(),
            "c52402837d60d07817ea7a2da6c194fc5e351ba8dd5ed808ad5fbeb8eb3ce0d2",
        )

    def test_identity_registry_is_literal_and_complete(self):
        self.assertEqual(contract.IDENTITY_REGISTRY, _parse_identity_rows())

    def test_edge_direction_and_cardinality_registry(self):
        observed = tuple(
            (edge.kind, edge.source_table, edge.target_table, edge.mode, edge.cardinality)
            for edge in contract.EDGE_SPECS
        )
        self.assertEqual(observed, EXPECTED_EDGES)

    def test_occurrence_root_domain_parser_and_bound_registries(self):
        observed_occurrences = tuple(
            (row.source_family, row.source_field, row.source_predicate, row.site_table, row.occurrence_kind, row.candidate_family)
            for row in contract.OCCURRENCE_SOURCES
        )
        self.assertEqual(observed_occurrences, EXPECTED_OCCURRENCES)
        self.assertEqual(contract.CANDIDATE_TABLE_BY_FAMILY, EXPECTED_CANDIDATE_TABLES)
        self.assertEqual(contract.OCCURRENCE_ALLOWED_STATES, EXPECTED_OCCURRENCE_ALLOWED_STATES)
        self.assertEqual(contract.OCCURRENCE_OWNER_LIFT, EXPECTED_OCCURRENCE_OWNER_LIFT)
        self.assertEqual(contract.OCCURRENCE_RESIDUALS, EXPECTED_OCCURRENCE_RESIDUALS)
        self.assertEqual(contract.OCCURRENCE_EDGE_DIRECTIONS, EXPECTED_OCCURRENCE_DIRECTIONS)
        self.assertEqual(contract.RESOLUTION_CARDINALITY, EXPECTED_RESOLUTION_CARDINALITY)
        self.assertEqual(contract.DISCRIMINATED_FKS, EXPECTED_DISCRIMINATED_FKS)
        self.assertEqual(
            tuple((row.name, row.required_non_null, row.permitted_non_null) for row in contract.EVIDENCE_VARIANTS),
            EXPECTED_EVIDENCE_VARIANTS,
        )
        self.assertEqual(
            tuple((root.root_id, root.candidate_tables, root.initial_rules, root.traversal) for root in contract.ROOT_SPECS),
            EXPECTED_ROOT_SPECS,
        )
        self.assertEqual(contract.DOMAIN_ROOTS, (
            ("retail-skirmish", "skirmish-roots"),
            ("retail-campaigns", "campaign-roots"),
            ("retail-war-of-the-ring", "war-of-the-ring-roots"),
            ("retail-create-a-hero", "create-a-hero-roots"),
            ("retail-shell", "shell-roots"),
        ))
        self.assertEqual(contract.CAMPAIGN_KINDS, ("LinearCampaign",))
        self.assertEqual(contract.SELECTOR_STATE_RULES, EXPECTED_SELECTOR_RULES)
        self.assertEqual(contract.MAP_MODE_RULES, EXPECTED_MAP_MODES)
        self.assertEqual(contract.PARSER_DISPATCH, EXPECTED_PARSER_DISPATCH)
        self.assertEqual(contract.BOUNDS, EXPECTED_BOUNDS)
        self.assertEqual(
            tuple((row.name, row.maximum, row.refusal_code) for row in contract.BOUND_SPECS),
            EXPECTED_BOUND_SPECS,
        )
        self.assertEqual(contract.ROW_LIMIT_PRECEDENCE, EXPECTED_ROW_LIMIT_PRECEDENCE)
        self.assertEqual(contract.BOUND_INPUT_IDS[-1], "source-schema")
        self.assertEqual(contract.SOURCE_SCHEMA_INPUT_SHAPE, EXPECTED_SOURCE_SCHEMA_SHAPE)

    def test_contract_records_are_deeply_immutable(self):
        with self.assertRaises(FrozenInstanceError):
            contract.TABLE_SPECS[0].name = "changed"
        with self.assertRaises(TypeError):
            contract.TABLE_SPECS[0].fields[0] = contract.TABLE_SPECS[0].fields[1]


class ShapeHostileTests(unittest.TestCase):
    def test_every_table_every_missing_and_wrong_field(self):
        for table in contract.TABLE_SPECS:
            good = _valid_shape_row(table)
            contract.validate_row_shape(table.name, good)
            with self.subTest(table=table.name, hostile="extra"):
                bad = dict(good, extraField=0)
                with self.assertRaises(contract.ContractError):
                    contract.validate_row_shape(table.name, bad)
            for field in table.fields:
                with self.subTest(table=table.name, field=field.name, hostile="missing"):
                    bad = dict(good)
                    del bad[field.name]
                    with self.assertRaises(contract.ContractError):
                        contract.validate_row_shape(table.name, bad)
                with self.subTest(table=table.name, field=field.name, hostile="wrong-type"):
                    bad = dict(good)
                    bad[field.name] = object()
                    with self.assertRaises(contract.ContractError):
                        contract.validate_row_shape(table.name, bad)

    def test_every_declared_fk_rejects_wrong_prefix(self):
        for table in contract.TABLE_SPECS:
            good = _valid_shape_row(table)
            for field in table.fields:
                if not field.fk_tables:
                    continue
                wrong_table = next(name for name in EXPECTED_TABLES if name not in field.fk_tables)
                bad = dict(good)
                wrong_id = _id(EXPECTED_PREFIXES[wrong_table])
                bad[field.name] = [wrong_id] if field.value.kind in ("list", "set") else wrong_id
                with self.subTest(table=table.name, field=field.name):
                    with self.assertRaises(contract.ContractError):
                        contract.validate_row_shape(table.name, bad)

    def test_every_discriminator_rejects_cross_family_id(self):
        discriminator_targets = {
            ("assignments", "ownerTable"): "definitions",
            ("module_declarations", "ownerTable"): "definitions",
            ("script_calls", "ownerTable"): "definitions",
            ("nested_sites", "ownerTable"): "definitions",
            ("unknown_sites", "ownerTable"): "definitions",
            ("evidence_referents", "referentTable"): "documents",
            ("reference_occurrences", "siteTable"): "assignments",
            ("reference_occurrences", "ownerTable"): "documents",
            ("reference_occurrences", "candidateTable"): "definitions",
            ("edges", "sourceTable"): "definitions",
            ("edges", "targetTable"): "assets",
            ("selector_dispositions", "candidateTable"): "definitions",
            ("root_memberships", "memberTable"): "definitions",
            ("domain_dispositions", "nodeTable"): "definitions",
            ("residuals", "subjectTable"): "documents",
        }
        discriminator_wrong_targets = {
            ("assignments", "ownerTable"): "documents",
            ("module_declarations", "ownerTable"): "documents",
            ("script_calls", "ownerTable"): "documents",
            ("nested_sites", "ownerTable"): "documents",
            ("unknown_sites", "ownerTable"): "documents",
            ("evidence_referents", "referentTable"): "source_records",
            ("reference_occurrences", "siteTable"): "object_occurrences",
            ("reference_occurrences", "ownerTable"): "definitions",
            ("reference_occurrences", "candidateTable"): "assets",
            ("edges", "sourceTable"): "assets",
            ("edges", "targetTable"): "definitions",
            ("selector_dispositions", "candidateTable"): "assets",
            ("root_memberships", "memberTable"): "assets",
            ("domain_dispositions", "nodeTable"): "assets",
            ("residuals", "subjectTable"): "source_records",
        }
        for table, discriminator, id_fields in EXPECTED_DISCRIMINATED_FKS:
            row = _valid_shape_row(contract.table_spec(table))
            target = discriminator_targets[(table, discriminator)]
            row[discriminator] = target
            wrong_table = discriminator_wrong_targets[(table, discriminator)]
            for field in id_fields:
                if field == "candidateIds":
                    row[field] = [_id(EXPECTED_PREFIXES[wrong_table])]
                else:
                    row[field] = _id(EXPECTED_PREFIXES[wrong_table])
            with self.subTest(table=table, discriminator=discriminator):
                with self.assertRaises(contract.ContractError):
                    contract.validate_row_shape(table, row)


class IdentityAndAtomTests(unittest.TestCase):
    def test_portable_atom_vector(self):
        atoms = (
            contract.canonical_atom("null"), contract.canonical_atom("cs", "\u00c9owyn"),
            contract.canonical_atom("ci", "Stra\u00dfe"), contract.canonical_atom("path", "Data\\INI"),
            contract.canonical_atom("int", -7), contract.canonical_atom("bool", True),
            contract.canonical_atom("id", _id("SRC-")), contract.canonical_atom("sha256", "a" * 64),
            contract.canonical_atom("enum", "winner", allowed_values=("winner", "shadow")),
        )
        encoded = contract.canonical_preimage("vector", atoms)
        self.assertEqual(hashlib.sha256(encoded).hexdigest(), "d0a281275c9f25331320fc7fed10046cc8f07a5be443c4df897d65f96831387a")

    def test_table_id_vector_casefold_and_duplicate_preimage(self):
        lower = {"casefoldName": "Stra\u00dfe"}
        upper = {"casefoldName": "STRASSE"}
        expected = "MKD-0c0edbf8f25836dc5fcb92f69d0f8c64faef78365bfd233cf3a8e8dee4a8be31"
        self.assertEqual(contract.expected_id("module_kinds", lower), expected)
        self.assertEqual(contract.expected_id("module_kinds", upper), expected)
        lower["id"] = expected
        upper["id"] = expected
        with self.assertRaisesRegex(contract.ContractError, "duplicate preimage"):
            contract.validate_unique_identities("module_kinds", (lower, upper))
        wrong = {"id": "MKD-" + "1" * 64, "casefoldName": "Stra\u00dfe"}
        with self.assertRaisesRegex(contract.ContractError, "ID does not match"):
            contract.validate_identity("module_kinds", wrong)

    def test_missing_nullable_identity_part_is_not_explicit_null(self):
        base = {"documentId": _id("DOC-"), "line": 1, "occurrenceIndex": 0, "definitionKind": "Object"}
        with self.assertRaisesRegex(contract.ContractError, "missing identity part name"):
            contract.identity_preimage("definitions", base)
        base["name"] = None
        self.assertTrue(contract.identity_preimage("definitions", base))

    def test_all_canonical_atom_refusals(self):
        hostile_calls = (
            ("null", "x", {}), ("cs", None, {}), ("unknown", "x", {}),
            ("cs", "bad\x00", {}), ("ci", "e\u0301", {}),
            ("path", "../x", {}), ("path", "C:/x", {}), ("path", "x:ads", {}),
            ("int", True, {}), ("int", -1, {"integer_kind": "u16"}),
            ("int", 1, {"integer_kind": "u8"}),
            ("bool", 1, {}), ("id", "SRC-ABC", {}), ("sha256", "A" * 64, {}),
            ("enum", "other", {"allowed_values": ("winner", "shadow")}),
            ("list", "not-a-sequence", {}), ("list", [["null", "x"]], {}),
            ("list", [["bogus", "x"]], {}), ("list", [["int", "01"]], {}),
            ("list", [["bool", "True"]], {}), ("list", [["ci", "Foo"]], {}),
            ("list", [["path", "a\\b"]], {}),
            ("list", [["enum", ""]], {}), ("list", [["enum", "m\u00f3de"]], {}),
            ("list", [["id", "SRC-" + "A" * 64]], {}),
            ("list", [["sha256", "A" * 64]], {}),
            ("set", [["cs", "b"], ["cs", "a"]], {}),
            ("set", [["cs", "a"], ["cs", "a"]], {}),
        )
        for tag, value, kwargs in hostile_calls:
            with self.subTest(tag=tag, value=value):
                with self.assertRaises(contract.ContractError):
                    contract.canonical_atom(tag, value, **kwargs)
        self.assertEqual(
            contract.canonical_atom("list", [["int", "1"], ["bool", "true"], ["ci", "foo"], ["path", "a/b"]]),
            ["list", ["int", "1"], ["bool", "true"], ["ci", "foo"], ["path", "a/b"]],
        )
        self.assertEqual(contract.canonical_atom("ci-null", "Stra\u00dfe"), ["ci", "strasse"])


class EvidenceOccurrenceAndBoundTests(unittest.TestCase):
    def _evidence_base(self, kind: str) -> dict:
        row = {name: None for name in (
            "referentTable", "referentId", "inputId", "sourceRecordId", "documentId",
            "line", "byteStart", "byteEnd", "factKind", "factOrdinal",
        )}
        row["referentKind"] = kind
        return row

    def test_every_evidence_variant_and_digest_mismatch(self):
        variants = {
            "output-row": {"referentTable": "documents", "referentId": _id("DOC-")},
            "input-artifact": {"inputId": "catalog"},
            "source-span": {"sourceRecordId": _id("SRC-"), "line": 1, "byteStart": 0, "byteEnd": 1},
            "parser-fact": {"sourceRecordId": _id("SRC-"), "factKind": "map", "factOrdinal": 0},
        }
        for kind, values in variants.items():
            row = self._evidence_base(kind)
            row.update(values)
            contract.validate_evidence_variant(row)
            bad = dict(row)
            forbidden = next(name for name in ("referentTable", "inputId", "sourceRecordId", "factKind") if name not in values)
            bad[forbidden] = "x"
            with self.assertRaises(contract.ContractError):
                contract.validate_evidence_variant(bad)
        digest_row = {"digestSha256": hashlib.sha256(b"right").hexdigest()}
        contract.validate_evidence_digest(digest_row, b"right")
        with self.assertRaisesRegex(contract.ContractError, "digest mismatch"):
            contract.validate_evidence_digest(digest_row, b"wrong")

    def _occurrence(self, fixture, state: str) -> tuple[dict, str]:
        source_family, source_field, predicates, site_table, occurrence_kind, candidate_family = fixture
        predicate = predicates.split("|")[0]
        candidate_table = dict(EXPECTED_CANDIDATE_TABLES)[candidate_family]
        candidate_prefix = EXPECTED_PREFIXES[candidate_table] if candidate_table else None
        candidates = []
        target = None
        edge = None
        residuals = []
        if state == "resolved":
            candidates = [_id(candidate_prefix)]
            target = candidates[0]
            edge = _id("EDG-")
        elif state == "ambiguous":
            candidates = [candidate_prefix + "0" * 63 + "0", candidate_prefix + "0" * 63 + "1"]
            residuals = [_id("RES-")]
        else:
            residuals = [_id("RES-")]
        owner_table = {
            "assignments": "documents", "object_occurrences": "definitions",
            "module_declarations": "module_declarations", "script_calls": "script_calls",
            "map_objects": "map_objects", "map_libraries": "map_libraries",
            "apt_movies": "apt_movies", "wnd_windows": "wnd_windows",
        }[site_table]
        return ({
            "id": _id("OCC-"), "occurrenceKind": occurrence_kind,
            "sourceFamily": source_family, "sourceField": source_field,
            "siteTable": site_table, "ownerTable": owner_table,
            "ownerId": _id(EXPECTED_PREFIXES[owner_table]),
            "candidateFamily": candidate_family, "candidateTable": candidate_table,
            "resolution": state, "candidateIds": candidates, "targetId": target,
            "edgeId": edge, "residualIds": residuals,
        }, predicate)

    def test_every_occurrence_state_and_exact_residual(self):
        allowed = dict(EXPECTED_OCCURRENCE_ALLOWED_STATES)
        for fixture in EXPECTED_OCCURRENCES:
            for state in allowed[fixture[4]]:
                row, predicate = self._occurrence(fixture, state)
                contract.validate_occurrence_cardinality(row, source_predicate=predicate)
                expected_kind = contract.expected_occurrence_residual_kind(row)
                literal_mapping = next(item for item in EXPECTED_OCCURRENCE_RESIDUALS if item[0] == row["occurrenceKind"])
                literal_kind = None if state == "resolved" else literal_mapping[{"unresolved": 1, "ambiguous": 2, "unsupported": 3}[state]]
                self.assertEqual(expected_kind, literal_kind)
                residual_rows = []
                if expected_kind:
                    residual_rows = [{
                        "id": row["residualIds"][0], "residualKind": expected_kind,
                        "reasonCode": expected_kind, "subjectTable": "reference_occurrences",
                        "subjectId": row["id"], "ordinal": 0,
                    }]
                contract.validate_occurrence_residuals(row, residual_rows)
                if residual_rows:
                    bad = [dict(residual_rows[0], residualKind="opaque-semantic-token")]
                    with self.assertRaises(contract.ContractError):
                        contract.validate_occurrence_residuals(row, bad)

    def test_occurrence_registry_and_edge_hostiles(self):
        row, predicate = self._occurrence(EXPECTED_OCCURRENCES[6], "unsupported")
        row["candidateFamily"] = "effective-path"
        row["candidateTable"] = "assets"
        with self.assertRaisesRegex(contract.ContractError, "source-registry"):
            contract.validate_occurrence_cardinality(row, source_predicate=predicate)

        row, predicate = self._occurrence(EXPECTED_OCCURRENCES[0], "resolved")
        edge = {
            "id": row["edgeId"], "edgeKind": "definition-reference-from-document",
            "sourceTable": "documents", "sourceId": row["ownerId"],
            "targetTable": "definitions", "targetId": row["targetId"],
            "occurrenceId": row["id"], "directOrdinal": None,
        }
        contract.validate_occurrence_edge(row, edge)
        with self.assertRaises(contract.ContractError):
            contract.validate_occurrence_edge(row, dict(edge, id=_id("EDG-")[:-1] + "1"))
        with self.assertRaises(contract.ContractError):
            contract.validate_occurrence_edge(row, dict(edge, sourceTable="definitions"))

    def test_every_occurrence_edge_direction(self):
        for occurrence_kind, source_table, target_table, edge_kind in EXPECTED_OCCURRENCE_DIRECTIONS:
            fixture = next(item for item in EXPECTED_OCCURRENCES if item[4] == occurrence_kind)
            row, predicate = self._occurrence(fixture, "resolved")
            row["ownerTable"] = source_table
            row["ownerId"] = _id(EXPECTED_PREFIXES[source_table])
            contract.validate_occurrence_cardinality(row, source_predicate=predicate)
            edge = {
                "id": row["edgeId"], "edgeKind": edge_kind,
                "sourceTable": source_table, "sourceId": row["ownerId"],
                "targetTable": target_table, "targetId": row["targetId"],
                "occurrenceId": row["id"], "directOrdinal": None,
            }
            with self.subTest(kind=occurrence_kind, source=source_table):
                contract.validate_occurrence_edge(row, edge)
                with self.assertRaises(contract.ContractError):
                    contract.validate_occurrence_edge(row, dict(edge, edgeKind="source-document"))

    def test_named_row_limits_precede_generic_admin(self):
        self.assertIsNone(contract.first_table_row_limit_violation("edges", 10_000_000))
        self.assertEqual(contract.first_table_row_limit_violation("edges", 10_000_001), "E_EDGE_ROWS_LIMIT")
        self.assertEqual(contract.first_table_row_limit_violation("reference_occurrences", 5_000_001), "E_OCCURRENCE_ROWS_LIMIT")
        self.assertEqual(contract.first_table_row_limit_violation("residuals", 5_000_001), "E_RESIDUAL_ROWS_LIMIT")
        self.assertEqual(contract.first_table_row_limit_violation("counts", 10_000_001), "E_ADMIN_TABLE_ROWS_LIMIT")
        self.assertEqual(contract.first_table_row_limit_violation("source_records", 1_000_001), "E_NODE_TABLE_ROWS_LIMIT")


class ArtifactContractTests(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    def _document(self):
        gate = _checker_module()
        return gate.parse_artifact(gate.artifact_bytes(self.ROOT))

    def _assert_literal_rejected(self, document):
        with self.assertRaises(AssertionError):
            validate_contract_artifact(_refresh_content_digest(document))

    def test_two_fresh_isolated_serializations_match_literal_contract(self):
        gate = _checker_module()
        first = gate.isolated_artifact_bytes(self.ROOT)
        second = gate.isolated_artifact_bytes(self.ROOT)
        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        self.assertNotIn(b"\r", first)
        self.assertFalse(first.startswith(b"\xef\xbb\xbf"))
        document = gate.parse_artifact(first)
        validate_contract_artifact(document)
        self.assertEqual(gate.validate_artifact(first, self.ROOT), document)

    def test_literal_validator_refuses_schema_registry_and_digest_drift(self):
        base = self._document()
        scalar_hostiles = {
            "schema": "wrong-schema",
            "schemaVersion": 2,
            "semanticSchema": "wrong-semantic-schema",
            "semanticSchemaVersion": 5,
            "baselineId": "wrong-baseline",
            "normativeDesignSha256": "0" * 64,
        }
        for field, value in scalar_hostiles.items():
            with self.subTest(field=field):
                hostile = copy.deepcopy(base)
                hostile[field] = value
                self._assert_literal_rejected(hostile)

        hostile = copy.deepcopy(base)
        hostile["registryOrder"] = list(reversed(hostile["registryOrder"]))
        self._assert_literal_rejected(hostile)
        hostile = copy.deepcopy(base)
        hostile["registries"][0]["value"] = []
        self._assert_literal_rejected(hostile)
        for field in EXPECTED_ARTIFACT_COUNTS:
            with self.subTest(count=field):
                hostile = copy.deepcopy(base)
                hostile["counts"][field] += 1
                self._assert_literal_rejected(hostile)
        for field in EXPECTED_ARTIFACT_DIGESTS:
            with self.subTest(digest=field):
                hostile = copy.deepcopy(base)
                hostile["expected"][field] = "0" * 64
                self._assert_literal_rejected(hostile)

    def test_literal_validator_refuses_closure_and_content_drift(self):
        base = self._document()
        closure_hostiles = (
            ("hashProfile", "raw-bytes"),
            ("pathOrder", list(reversed(EXPECTED_ARTIFACT_IMPLEMENTATION_PATHS))),
            ("closureSha256", "0" * 64),
        )
        for field, value in closure_hostiles:
            with self.subTest(closure=field):
                hostile = copy.deepcopy(base)
                hostile["implementationClosure"][field] = value
                self._assert_literal_rejected(hostile)
        for field, value in (
            ("path", "wrong.py"), ("sha256", "0" * 64), ("bytes", 0),
        ):
            with self.subTest(file_field=field):
                hostile = copy.deepcopy(base)
                hostile["implementationClosure"]["files"][0][field] = value
                self._assert_literal_rejected(hostile)

        hostile = copy.deepcopy(base)
        hostile["contentSha256"] = "0" * 64
        with self.assertRaises(AssertionError):
            validate_contract_artifact(hostile)

    def test_parser_refuses_noncanonical_and_duplicate_json(self):
        gate = _checker_module()
        canonical = gate.artifact_bytes(self.ROOT)
        hostiles = (
            b"\xef\xbb\xbf" + canonical,
            canonical.replace(b"\n", b"\r\n"),
            canonical[:-1],
            canonical + b"\n",
            b'{"x":1,"x":2}\n',
            b'{"x":1.0}\n',
            b'{"x": 1}\n',
        )
        for hostile in hostiles:
            with self.subTest(raw=hostile[:16]):
                with self.assertRaises(gate.ArtifactError):
                    gate.parse_artifact(hostile)

    def test_publish_is_atomic_idempotent_and_refuses_stale_output(self):
        gate = _checker_module()
        raw = b"canonical\n"
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "nested" / "artifact.json"
            gate._publish_bytes(output, raw)
            self.assertEqual(output.read_bytes(), raw)
            gate._publish_bytes(output, raw)
            output.write_bytes(b"stale\n")
            with self.assertRaisesRegex(gate.ArtifactError, "stale"):
                gate._publish_bytes(output, raw)

    def test_output_path_refuses_outside_leaf_and_parent_reparse(self):
        gate = _checker_module()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "repo"
            output = root / gate.ARTIFACT_RELATIVE
            output.parent.mkdir(parents=True)
            output.write_bytes(b"existing\n")
            gate._assert_output_path(root, output)
            with self.assertRaisesRegex(gate.ArtifactError, "outside"):
                gate._assert_output_path(root, root.parent / "artifact.json")
            for reparse in (output, output.parent):
                with self.subTest(reparse=reparse.name):
                    with mock.patch.object(
                        gate, "_has_reparse_point",
                        side_effect=lambda path, target=reparse: path == target,
                    ):
                        with self.assertRaisesRegex(gate.ArtifactError, "reparse"):
                            gate._assert_output_path(root, output)


if __name__ == "__main__":
    unittest.main()
