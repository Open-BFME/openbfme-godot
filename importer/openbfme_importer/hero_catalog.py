"""Compile every object in the coverage ledger's requested hero family."""

from __future__ import annotations

import hashlib
import json
import re
from collections import defaultdict
from collections.abc import Mapping, Sequence
from typing import Any

from .module_contracts import ModuleContractError, compile_all_module_contracts
from .playable_unit_compiler import (
    PlayableUnitCompilerError,
    _ancestry,
    _object_semantic,
    compile_playable_unit_descriptor,
    playable_object_kind_of,
    prepare_playable_unit_compiler,
    validate_playable_unit_descriptor,
)
from .retail_ini_coverage import is_hero_family_object
from .sage_cst import SageCstError, parse_sage_document


SCHEMA = "openbfme.hero-catalog"
SCHEMA_VERSION = 1
TEMPLATE_SCHEMA = "openbfme.authored-hero-template"
_NO_BUILD_ROUTE = "is not targeted by an authored UNIT_BUILD command"


class HeroCatalogError(ValueError):
    pass


def _digest(value: object) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def _values(target: Any, key: str) -> list[str]:
    return [
        assignment.value
        for assignment in target.assignments
        if assignment.key.casefold() == key.casefold()
    ]


def _tokens(target: Any, key: str) -> list[str]:
    return [
        token
        for value in _values(target, key)
        for token in re.findall(r"[A-Za-z0-9_+.-]+", value)
    ]


def _side(target: Any) -> str | None:
    tokens = _tokens(target, "Side")
    return tokens[-1] if tokens else None


def _role(target: Any) -> str:
    identity = target.name.casefold()
    if any(term in identity for term in ("summon", "oathbreaker", "balrog")):
        return "summoned"
    if target.kind.casefold() == "childobject":
        return "variant"
    return "hero"


def _template(
    target: Any, lineage: Sequence[Any], kind_of: Sequence[str], role: str
) -> dict[str, object]:
    semantics_by_path: dict[str, list[dict[str, object]]] = defaultdict(list)
    for item in lineage:
        semantics_by_path[item.source_virtual_path].append(_object_semantic(item))
    try:
        contracts = compile_all_module_contracts(lineage, target.name)
    except ModuleContractError as exc:
        raise HeroCatalogError(
            f"hero {target.name} has an invalid authored module contract: {exc}"
        ) from exc
    descriptor: dict[str, object] = {
        "schema": TEMPLATE_SCHEMA,
        "schemaVersion": 1,
        "objectId": target.name,
        "declarationKind": target.kind,
        "parentObjectId": target.parent,
        "role": role,
        "category": "hero",
        "effectiveKindOf": list(kind_of),
        "moduleContracts": contracts,
        "sourceDocuments": [
            {"virtualPath": path, "semanticSha256": _digest(rows)}
            for path, rows in sorted(
                semantics_by_path.items(), key=lambda item: item[0].casefold()
            )
        ],
    }
    descriptor["descriptorSha256"] = _digest(descriptor)
    return descriptor


def compile_hero_catalog(
    documents: Mapping[str, bytes], *, game: str = "bfme2"
) -> dict[str, object]:
    if game not in {"bfme2", "rotwk"}:
        raise HeroCatalogError(f"unsupported game {game!r}")
    prepared = prepare_playable_unit_compiler(documents)
    objects = dict(prepared.objects)
    # Retail also declares a few hero-family Objects in crate.ini and
    # formationassistant.ini. The playable-unit compiler intentionally indexes
    # data/ini/object only, but the exhaustive coverage denominator does not.
    for path, source in sorted(documents.items(), key=lambda item: item[0].casefold()):
        folded_path = path.replace("\\", "/").casefold()
        if folded_path.startswith("data/ini/object/") or not folded_path.endswith((".ini", ".inc")):
            continue
        try:
            parsed = parse_sage_document(source, path).objects
        except SageCstError:
            if re.search(rb"(?im)^\s*(?:Object|ChildObject)\s+\S*(?:hero|levelup)", source):
                raise HeroCatalogError(
                    f"hero-family object document cannot be parsed: {path}"
                )
            continue
        for target in parsed:
            objects[target.name.casefold()] = target
    targets = []
    for target in objects.values():
        authored_kind_of = _tokens(target, "KindOf")
        if is_hero_family_object(
            object_id=target.name,
            source_ini=target.source_virtual_path,
            side=_side(target),
            kind_of=authored_kind_of,
        ):
            targets.append(target)
    targets.sort(key=lambda item: (item.name.casefold(), item.name))
    rows: list[dict[str, object]] = []
    for target in targets:
        try:
            lineage = _ancestry(objects, target)
            if target.name.casefold() in prepared.objects:
                kind_of = playable_object_kind_of(prepared, target.name)
            else:
                kind_of = tuple(
                    sorted(
                        {
                            token.upper().lstrip("+")
                            for item in lineage
                            for token in _tokens(item, "KindOf")
                            if not token.startswith("-")
                        }
                    )
                )
        except PlayableUnitCompilerError as exc:
            raise HeroCatalogError(f"hero {target.name} has invalid inheritance: {exc}") from exc
        role = _role(target)
        reason: str | None = None
        try:
            if target.name.casefold() not in prepared.objects:
                raise PlayableUnitCompilerError(_NO_BUILD_ROUTE)
            descriptor = compile_playable_unit_descriptor(
                target.name, documents, prepared=prepared, game=game
            )
        except PlayableUnitCompilerError as exc:
            if _NO_BUILD_ROUTE not in str(exc):
                raise HeroCatalogError(
                    f"hero {target.name} failed descriptor compilation: {exc}"
                ) from exc
            descriptor = _template(target, lineage, kind_of, role)
            status = "deferred"
            reason = (
                "authored hero has no UNIT_BUILD route; summon, ring, campaign, "
                "and scenario admission remain runtime-owned"
            )
        else:
            validate_playable_unit_descriptor(descriptor)
            if descriptor.get("objectId") != target.name:
                descriptor = _template(target, lineage, kind_of, role)
                status = "deferred"
                reason = "authored producer resolves to a different object identity"
            else:
                status = "descriptor-ready"
        row: dict[str, object] = {
            "objectId": target.name,
            "side": _side(target),
            "role": role,
            "runtimeStatus": status,
            "descriptor": descriptor,
        }
        if reason:
            row["deferredReason"] = reason
        rows.append(row)
    summary = {
        "heroCount": len(rows),
        "descriptorReadyCount": sum(row["runtimeStatus"] == "descriptor-ready" for row in rows),
        "runtimeDeferredCount": sum(row["runtimeStatus"] == "deferred" for row in rows),
        "summonedCount": sum(row["role"] == "summoned" for row in rows),
        "variantCount": sum(row["role"] == "variant" for row in rows),
    }
    result: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "game": game,
        "heroes": rows,
        "summary": summary,
    }
    result["catalogSha256"] = _digest(result)
    validate_hero_catalog(result)
    return result


def validate_hero_catalog(value: Mapping[str, object]) -> None:
    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise HeroCatalogError("hero catalog schema is invalid")
    if value.get("game") not in {"bfme2", "rotwk"}:
        raise HeroCatalogError("hero catalog game is invalid")
    rows, summary = value.get("heroes"), value.get("summary")
    if not isinstance(rows, list) or not isinstance(summary, Mapping):
        raise HeroCatalogError("hero catalog rows or summary are invalid")
    ids: set[str] = set()
    for row in rows:
        if not isinstance(row, Mapping) or not isinstance(row.get("objectId"), str):
            raise HeroCatalogError("hero catalog row is invalid")
        object_id = str(row["objectId"])
        if object_id.casefold() in ids:
            raise HeroCatalogError("hero identities are duplicated")
        ids.add(object_id.casefold())
        descriptor = row.get("descriptor")
        if row.get("runtimeStatus") not in {"descriptor-ready", "deferred"} or not isinstance(descriptor, Mapping):
            raise HeroCatalogError(f"hero {object_id} status is invalid")
        if descriptor.get("objectId") != object_id:
            raise HeroCatalogError(f"hero {object_id} descriptor identity is invalid")
    expected = {
        "heroCount": len(rows),
        "descriptorReadyCount": sum(row.get("runtimeStatus") == "descriptor-ready" for row in rows),
        "runtimeDeferredCount": sum(row.get("runtimeStatus") == "deferred" for row in rows),
        "summonedCount": sum(row.get("role") == "summoned" for row in rows),
        "variantCount": sum(row.get("role") == "variant" for row in rows),
    }
    if dict(summary) != expected:
        raise HeroCatalogError("hero summary disagrees with rows")
    unsigned = dict(value)
    digest = unsigned.pop("catalogSha256", None)
    if digest != _digest(unsigned):
        raise HeroCatalogError("hero catalog digest is invalid")


__all__ = ["HeroCatalogError", "compile_hero_catalog", "validate_hero_catalog"]
