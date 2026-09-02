"""Cook every effective SAGE Object-family definition into bundle-v1 rows.

This pass is deliberately independent of faction profiles and admission rules.
It consumes caller-owned files only, delegates syntax and include safety to the
existing bounded SAGE CST, preserves authored order in arrays, and emits
canonical JSON for deterministic builds.
"""

from __future__ import annotations

import argparse
from collections import Counter, OrderedDict
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from typing import Any

from ..module_contracts import OPAQUE_DEFERRED_MODULE_KINDS, TYPED_MODULE_KINDS
from ..playable_unit_compiler import _numeric_defines
from ..sage_cst import (
    SageAssignment,
    SageBlock,
    SageCstError,
    SageObject,
    resolve_sage_documents,
)


BUNDLE_SCHEMA = "openbfme.bundle.v1"
REPORT_SCHEMA = "openbfme.cook.objects.report.v1"
OBJECT_DENOMINATOR = 5_494
MODULE_KIND_DENOMINATOR = 236
_ENTRY_PATH = "__openbfme_cook_entry__.ini"
_OBJECT_HEADER_BYTES = re.compile(
    rb"(?mi)^[ \t]*(?:Object|ChildObject|ObjectReskin)[ \t]+(?P<name>\S+)"
)
_DEFINE_LINE = re.compile(
    rb"(?m)^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+([^\r\n]*?)[ \t]*\r?$"
)
_INTEGER = re.compile(r"[+-]?[0-9]+\Z")
_NUMBER = re.compile(
    r"[+-]?(?:(?:[0-9]+(?:\.[0-9]*)?)|(?:\.[0-9]+))(?:[Ee][+-]?[0-9]+)?\Z"
)
_LOCATION = re.compile(r"(?P<path>[^\s:]+(?:/[^\s:]+)*):(?P<line>[0-9]+)")
_OBJECT_LINE = re.compile(
    r"^[ \t]*(?:Object|ChildObject|ObjectReskin)[ \t]+(?P<name>\S+)",
    re.IGNORECASE,
)
_CANONICAL_CARRIERS = {
    "behavior": "Behavior",
    "body": "Body",
    "draw": "Draw",
    "clientupdate": "ClientUpdate",
    "clientbehavior": "ClientBehavior",
    "flasher": "Flasher",
}
_KNOWN_MODULES = frozenset(
    name.casefold() for name in (TYPED_MODULE_KINDS | OPAQUE_DEFERRED_MODULE_KINDS)
)


@dataclass(frozen=True, slots=True)
class CookResult:
    bundle: dict[str, Any]
    report: dict[str, Any]


def _canonical_json_bytes(value: object) -> bytes:
    return (
        json.dumps(
            value,
            indent=2,
            sort_keys=True,
            ensure_ascii=False,
            allow_nan=False,
        )
        + "\n"
    ).encode("utf-8")


def _content_sha256(source: bytes) -> str:
    return hashlib.sha256(source).hexdigest()


def _source_identity(documents: Sequence[tuple[str, bytes]]) -> dict[str, object]:
    rows = [
        {"path": path, "sha256": _content_sha256(source)}
        for path, source in documents
    ]
    encoded = "".join(f"{row['path']}|{row['sha256']}\n" for row in rows)
    return {
        "effective_tree_sha256": hashlib.sha256(encoded.encode("utf-8")).hexdigest(),
        "paths": rows,
    }


def _strip_define_comment(raw: str) -> str:
    quoted = False
    index = 0
    while index < len(raw):
        char = raw[index]
        if char == '"':
            quoted = not quoted
        elif not quoted and char == ";":
            return raw[:index].strip()
        elif not quoted and raw[index : index + 2] == "//":
            return raw[:index].strip()
        index += 1
    return raw.strip()


def _literal_value(raw: str) -> str | int | float | bool:
    value = raw.strip()
    if _INTEGER.fullmatch(value):
        return int(value)
    if _NUMBER.fullmatch(value):
        return float(value)
    if value.casefold() == "yes":
        return True
    if value.casefold() == "no":
        return False
    if len(value) >= 2 and value[0] == value[-1] == '"':
        return value[1:-1]
    return value


def _resolved_defines(
    documents: Sequence[tuple[str, bytes]],
) -> tuple[dict[str, str | int | float | bool], list[dict[str, str]]]:
    document_map = dict(documents)
    diagnostics: list[dict[str, str]] = []
    numeric = _numeric_defines(document_map)

    spelling: OrderedDict[str, str] = OrderedDict()
    bodies: dict[str, str] = {}
    for _path, source in documents:
        for match in _DEFINE_LINE.finditer(source):
            name = match.group(1).decode("ascii")
            body = _strip_define_comment(match.group(2).decode("cp1252"))
            key = name.casefold()
            if key not in spelling:
                spelling[key] = name
            bodies[key] = body

    resolving: set[str] = set()
    resolved: dict[str, str | int | float | bool] = {}

    def resolve(key: str) -> str | int | float | bool:
        cached = resolved.get(key)
        if cached is not None:
            return cached
        if key in resolving:
            return bodies[key]
        resolving.add(key)
        try:
            if key in numeric:
                value: str | int | float | bool = numeric[key]
            else:
                body = bodies[key]
                reference = body.casefold()
                value = resolve(reference) if reference in bodies else _literal_value(body)
            resolved[key] = value
            return value
        finally:
            resolving.remove(key)

    output = {spelling[key]: resolve(key) for key in spelling}
    return dict(sorted(output.items(), key=lambda item: (item[0].casefold(), item[0]))), diagnostics


def _value(raw: str, defines: Mapping[str, object], *, verbatim: bool = False) -> object:
    authored = raw.strip()
    if verbatim:
        return authored
    resolved = defines.get(authored.casefold())
    return resolved if resolved is not None else _literal_value(authored)


def _folded_defines(defines: Mapping[str, object]) -> dict[str, object]:
    return {name.casefold(): value for name, value in defines.items()}


def _fields(
    assignments: Iterable[SageAssignment],
    defines: Mapping[str, object],
    *,
    verbatim: bool = False,
) -> dict[str, object]:
    spelling: OrderedDict[str, str] = OrderedDict()
    grouped: dict[str, list[object]] = {}
    for assignment in assignments:
        key = assignment.key.casefold()
        spelling.setdefault(key, assignment.key)
        grouped.setdefault(key, []).append(
            _value(assignment.value, defines, verbatim=verbatim)
        )
    return {
        spelling[key]: values[0] if len(values) == 1 else values
        for key, values in grouped.items()
    }


def _block_row(
    block: SageBlock,
    defines: Mapping[str, object],
    *,
    verbatim: bool = False,
) -> dict[str, object]:
    return {
        "type": block.header_key or block.kind,
        "tag": block.instance_tag or " ".join(block.header_tokens),
        "fields": _fields(block.assignments, defines, verbatim=verbatim),
        "blocks": [
            _block_row(child, defines, verbatim=verbatim) for child in block.blocks
        ],
    }


def _walk_blocks(blocks: Iterable[SageBlock]) -> Iterable[SageBlock]:
    for block in blocks:
        yield block
        yield from _walk_blocks(block.blocks)


def _module_row(block: SageBlock, defines: Mapping[str, object]) -> dict[str, object]:
    carrier = _CANONICAL_CARRIERS.get((block.header_key or "").casefold(), "other")
    gap = block.kind.casefold() not in _KNOWN_MODULES
    return {
        "carrier": carrier,
        "type": block.kind,
        "tag": block.instance_tag or "",
        "fields": _fields(block.assignments, defines, verbatim=gap),
        "blocks": [
            _block_row(child, defines, verbatim=gap) for child in block.blocks
        ],
        "gap": gap,
    }


def _last_assignment(obj: SageObject, key: str) -> SageAssignment | None:
    folded = key.casefold()
    return next(
        (item for item in reversed(obj.assignments) if item.key.casefold() == folded),
        None,
    )


def _summary_value(
    obj: SageObject,
    key: str,
    defines: Mapping[str, object],
) -> object | None:
    assignment = _last_assignment(obj, key)
    return None if assignment is None else _value(assignment.value, defines)


def _numeric_summary(
    obj: SageObject,
    key: str,
    defines: Mapping[str, object],
    diagnostics: list[dict[str, str]],
) -> int | float | None:
    value = _summary_value(obj, key, defines)
    if value is None:
        return None
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return value
    diagnostics.append(
        {
            "template": obj.name,
            "message": f"{obj.source_virtual_path}:{obj.line}: {key} is not numeric: {value!r}",
        }
    )
    return None


def _kind_name(kind: str) -> str:
    return {
        "object": "object",
        "childobject": "child",
        "objectreskin": "reskin",
    }[kind.casefold()]


def _template_row(
    obj: SageObject,
    defines: Mapping[str, object],
    diagnostics: list[dict[str, str]],
) -> dict[str, object]:
    module_blocks = [
        block
        for block in _walk_blocks(obj.blocks)
        if (block.header_key or "").casefold() in _CANONICAL_CARRIERS
    ]
    modules = [_module_row(block, defines) for block in module_blocks]
    for block, module in zip(module_blocks, modules, strict=True):
        if module["gap"]:
            diagnostics.append(
                {
                    "template": obj.name,
                    "message": (
                        f"{block.source_virtual_path}:{block.line}: unknown module type "
                        f"{module['type']} retained with gap=true"
                    ),
                }
            )

    row: dict[str, object] = {
        "name": obj.name,
        "kind": _kind_name(obj.kind),
        "parent": obj.parent,
        "kindof": [],
        "geometry": {},
        "fields": _fields(obj.assignments, defines),
        "blocks": [
            _block_row(block, defines)
            for block in obj.blocks
            if (block.header_key or "").casefold() not in _CANONICAL_CARRIERS
        ],
        "modules": modules,
    }
    side = _summary_value(obj, "Side", defines)
    if side is not None:
        row["side"] = str(side)
    kindof = _summary_value(obj, "KindOf", defines)
    if kindof is not None:
        row["kindof"] = str(kindof).split()

    geometry: dict[str, object] = {}
    shape = _summary_value(obj, "Geometry", defines)
    if shape is not None:
        geometry["shape"] = str(shape)
    for source_key, target_key in (
        ("GeometryMajorRadius", "major_radius"),
        ("GeometryMinorRadius", "minor_radius"),
        ("GeometryHeight", "height"),
    ):
        value = _numeric_summary(obj, source_key, defines, diagnostics)
        if value is not None:
            geometry[target_key] = value
    row["geometry"] = geometry

    for source_key, target_key in (
        ("BuildCost", "build_cost"),
        ("BuildTime", "build_time"),
        ("CommandPoints", "command_points"),
    ):
        value = _numeric_summary(obj, source_key, defines, diagnostics)
        if value is not None:
            row[target_key] = value

    health: int | float | None = None
    for block in _walk_blocks(obj.blocks):
        if (block.header_key or "").casefold() != "body":
            continue
        assignment = next(
            (
                item
                for item in reversed(block.assignments)
                if item.key.casefold() == "maxhealth"
            ),
            None,
        )
        if assignment is None:
            continue
        candidate = _value(assignment.value, defines)
        if isinstance(candidate, (int, float)) and not isinstance(candidate, bool):
            health = candidate
        else:
            diagnostics.append(
                {
                    "template": obj.name,
                    "message": (
                        f"{assignment.source_virtual_path}:{assignment.line}: "
                        f"Body MaxHealth is not numeric: {candidate!r}"
                    ),
                }
            )
    if health is not None:
        row["health"] = health
    return row


def _failure_record(
    message: str, documents: Mapping[str, bytes], fallback_path: str
) -> dict[str, str]:
    match = _LOCATION.search(message.replace("\\", "/"))
    path = match.group("path") if match else fallback_path
    line = int(match.group("line")) if match else 0
    template = ""
    source = documents.get(path)
    if source is not None and line:
        for raw in source.decode("cp1252", errors="replace").splitlines()[:line]:
            header = _OBJECT_LINE.match(raw)
            if header:
                template = header.group("name")
    return {"file": path, "template": template, "message": message}


def _root_documents(documents: Sequence[tuple[str, bytes]]) -> list[str]:
    return [
        path
        for path, source in documents
        if _OBJECT_HEADER_BYTES.search(source)
    ]


def _recover_document(
    path: str, sources: Mapping[str, bytes]
) -> tuple[list[SageObject], list[dict[str, str]]]:
    source = sources[path]
    headers = list(_OBJECT_HEADER_BYTES.finditer(source))
    objects: list[SageObject] = []
    failures: list[dict[str, str]] = []
    for index, header in enumerate(headers):
        start = header.start()
        stop = headers[index + 1].start() if index + 1 < len(headers) else len(source)
        line = source[:start].count(b"\n")
        fragment = (b"\n" * line) + source[start:stop]
        fragment_sources = dict(sources)
        fragment_sources[path] = fragment
        name = header.group("name").decode("cp1252", errors="replace")
        try:
            resolved = resolve_sage_documents(path, fragment_sources)
            candidates = [
                obj
                for obj in resolved.objects
                if obj.source_virtual_path.casefold() == path.casefold()
                and obj.name.casefold() == name.casefold()
            ]
            if len(candidates) != 1:
                raise ValueError(
                    f"recovery expected one {name} definition in {path}, found "
                    f"{len(candidates)}"
                )
            objects.append(candidates[0])
        except (SageCstError, ValueError) as exc:
            failures.append({"file": path, "template": name, "message": str(exc)})
    return objects, failures


def _resolve_objects(
    documents: Sequence[tuple[str, bytes]],
) -> tuple[list[SageObject], list[dict[str, str]]]:
    roots = _root_documents(documents)
    sources = dict(documents)
    entry_lines = [f'#include "{path}"' for path in roots]
    sources[_ENTRY_PATH] = ("\n".join(entry_lines) + "\n").encode("ascii")
    try:
        return list(resolve_sage_documents(_ENTRY_PATH, sources).objects), []
    except SageCstError as exc:
        failures: list[dict[str, str]] = []
        recovered: list[SageObject] = []
        seen_failures: set[tuple[str, str, str]] = set()
        for path in roots:
            try:
                recovered.extend(resolve_sage_documents(path, sources).objects)
            except SageCstError as item_exc:
                path_objects, path_failures = _recover_document(path, sources)
                recovered.extend(path_objects)
                if not path_failures:
                    continue
                for record in path_failures:
                    key = (record["file"], record["template"], record["message"])
                    if key not in seen_failures:
                        failures.append(record)
                        seen_failures.add(key)
        if not failures:
            failures.append(_failure_record(str(exc), sources, _ENTRY_PATH))
        return recovered, failures


def _report(
    templates: Sequence[Mapping[str, object]],
    parse_failures: Sequence[Mapping[str, str]],
) -> dict[str, object]:
    kinds = Counter(str(row["kind"]) for row in templates)
    module_types: dict[str, str] = {}
    gap_spelling: dict[str, str] = {}
    gaps: Counter[str] = Counter()
    for template in templates:
        for module in template["modules"]:  # type: ignore[index]
            module_type = str(module["type"])
            folded = module_type.casefold()
            module_types.setdefault(folded, module_type)
            if module["gap"]:
                gap_spelling.setdefault(folded, module_type)
                gaps[folded] += 1
    gap_rows = {
        gap_spelling[key]: count
        for key, count in sorted(
            gaps.items(), key=lambda item: (gap_spelling[item[0]].casefold(), gap_spelling[item[0]])
        )
    }
    return {
        "schema": REPORT_SCHEMA,
        "template_count": len(templates),
        "templates_by_kind": {
            kind: kinds.get(kind, 0) for kind in ("child", "object", "reskin")
        },
        "object_identifier_denominator": OBJECT_DENOMINATOR,
        "object_identifier_delta": len(templates) - OBJECT_DENOMINATOR,
        "distinct_module_types": len(module_types),
        "module_kind_denominator": MODULE_KIND_DENOMINATOR,
        "module_kind_delta": len(module_types) - MODULE_KIND_DENOMINATOR,
        "gap_module_rows": sum(gaps.values()),
        "gap_module_rows_by_type": gap_rows,
        "parse_failures": list(parse_failures),
    }


def cook_documents(documents: Iterable[tuple[str, bytes]]) -> CookResult:
    normalized: list[tuple[str, bytes]] = []
    seen: set[str] = set()
    for raw_path, source in documents:
        path = raw_path.replace("\\", "/").lstrip("/")
        if not path or path.casefold() in seen:
            raise ValueError(f"duplicate or empty INI virtual path: {raw_path!r}")
        if not isinstance(source, bytes):
            raise TypeError(f"INI source {path!r} must be bytes")
        seen.add(path.casefold())
        normalized.append((path, source))
    normalized.sort(key=lambda item: (item[0].casefold(), item[0]))
    if not normalized:
        raise ValueError("no INI files were supplied")

    defines, diagnostics = _resolved_defines(normalized)
    folded_defines = _folded_defines(defines)
    objects, parse_failures = _resolve_objects(normalized)
    for failure in parse_failures:
        diagnostics.append(
            {
                "template": failure["template"],
                "message": f"{failure['file']}: {failure['message']}",
            }
        )

    effective: OrderedDict[str, SageObject] = OrderedDict()
    for obj in objects:
        key = obj.name.casefold()
        if key in effective:
            del effective[key]
        effective[key] = obj
    templates = [
        _template_row(obj, folded_defines, diagnostics) for obj in effective.values()
    ]
    bundle = {
        "schema": BUNDLE_SCHEMA,
        "source": _source_identity(normalized),
        "templates": templates,
        "defines": defines,
        "diagnostics": diagnostics,
    }
    return CookResult(bundle=bundle, report=_report(templates, parse_failures))


def _path_documents(paths: Sequence[Path], root: Path) -> list[tuple[str, bytes]]:
    documents: list[tuple[str, bytes]] = []
    for path in paths:
        resolved = path.resolve()
        if not resolved.is_file():
            raise ValueError(f"INI input is not a file: {path}")
        relative = resolved.relative_to(root).as_posix()
        # Line endings are not INI content. Git checks fixtures out with CRLF
        # on Windows and LF elsewhere; normalising here keeps the source
        # digests and the golden bundle identical across checkouts.
        documents.append((f"data/ini/{relative}", _normalize_newlines(resolved.read_bytes())))
    return documents


def _normalize_newlines(source: bytes) -> bytes:
    return source.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def cook_ini_root(ini_root: Path | str) -> CookResult:
    root = Path(ini_root).resolve()
    if not root.is_dir():
        raise ValueError(f"INI root is not a directory: {root}")
    paths = sorted(
        (
            path
            for path in root.rglob("*")
            if path.is_file() and path.suffix.casefold() in {".ini", ".inc"}
        ),
        key=lambda path: (path.relative_to(root).as_posix().casefold(), path.as_posix()),
    )
    return cook_documents(_path_documents(paths, root))


def cook_files(files: Sequence[Path | str]) -> CookResult:
    paths = [Path(path).resolve() for path in files]
    if not paths:
        raise ValueError("--files requires at least one INI file")
    common = Path(os.path.commonpath([str(path.parent) for path in paths])).resolve()
    return cook_documents(_path_documents(paths, common))


def write_bundle(bundle: Mapping[str, object], path: Path | str) -> None:
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(_canonical_json_bytes(bundle))


def _write_report(report: Mapping[str, object], path: Path | str) -> None:
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(_canonical_json_bytes(report))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--ini-root", type=Path)
    inputs.add_argument("--files", type=Path, nargs="+")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(argv)
    try:
        result = (
            cook_ini_root(args.ini_root)
            if args.ini_root is not None
            else cook_files(args.files)
        )
        write_bundle(result.bundle, args.out)
        if args.report is not None:
            _write_report(result.report, args.report)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"object cook failed: {exc}\n")
    if result.report["parse_failures"]:
        print(
            f"object cook retained a partial bundle with "
            f"{len(result.report['parse_failures'])} named parse failure(s)",
            file=sys.stderr,
        )
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
