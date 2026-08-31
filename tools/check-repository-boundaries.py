"""Fail-closed validation for OpenBFME repository ownership boundaries."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any


SCHEMA = "openbfme.repository-boundaries"
SCHEMA_VERSION = 2
SCOPE_ID = "openbfme.rotwk-202-v9.7.7.product-scope"
BASELINE_ID = "rotwk-202-v9.7.7-en"
AUTHORITY_RULE = (
    "This is an enforced ownership map derived from canonical state, not a "
    "competing product or completion authority."
)
TEXT_SUFFIXES = {
    ".bat", ".cfg", ".cs", ".gd", ".godot", ".json", ".md", ".ps1",
    ".py", ".toml", ".tscn", ".txt", ".yaml", ".yml",
}


class DuplicateKeyError(ValueError):
    pass


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKeyError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as handle:
        value = json.load(handle, object_pairs_hook=_unique_object)
    if not isinstance(value, dict):
        raise ValueError(f"{path.as_posix()} must contain a JSON object")
    return value


def _git(root: Path, *args: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(root), *args],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout).strip()
        raise RuntimeError(f"git {' '.join(args)} failed: {detail}")
    return completed.stdout


def _tracked_files(root: Path) -> list[str]:
    return sorted(line for line in _git(root, "ls-files", "-z").split("\0") if line)


def _portable(path: str, *, allow_directory: bool = True) -> bool:
    if not path or "\\" in path:
        return False
    probe = path[:-1] if allow_directory and path.endswith("/") else path
    pure = PurePosixPath(probe)
    return bool(probe) and not pure.is_absolute() and ".." not in pure.parts


def _duplicates(values: list[str]) -> list[str]:
    seen: set[str] = set()
    duplicates: set[str] = set()
    for value in values:
        if value in seen:
            duplicates.add(value)
        seen.add(value)
    return sorted(duplicates)


def _root_matches(path: str, root: str) -> bool:
    return path.startswith(root) if root.endswith("/") else path == root


def _rule_matches(path: str, rule: dict[str, Any]) -> bool:
    roots = rule.get("roots", [])
    excluded = rule.get("exclude", [])
    return any(_root_matches(path, root) for root in roots) and not any(
        _root_matches(path, root) for root in excluded
    )


def _assigned_collisions(path: str, items: list[dict[str, Any]], owner: str) -> list[str]:
    collisions: list[str] = []
    for item in items:
        ownership = item.get("ownership", {})
        if ownership.get("state") != "assigned" or item.get("id") == owner:
            continue
        owned_paths = [str(value).replace("\\", "/") for value in ownership.get("ownedPaths", [])]
        if any(_root_matches(path, value) for value in owned_paths):
            collisions.append(str(item.get("id")))
    return sorted(collisions)


def _line_count(path: Path) -> int:
    with path.open("r", encoding="utf-8", errors="replace", newline=None) as handle:
        return sum(1 for _ in handle)


def _project_settings(text: str) -> tuple[dict[str, str], dict[str, str]]:
    settings: dict[str, str] = {}
    autoloads: dict[str, str] = {}
    section = ""
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        match = re.fullmatch(r'([^=]+)="([^"]*)"', line)
        if not match:
            continue
        key, value = match.group(1).strip(), match.group(2)
        if section == "autoload":
            autoloads[key] = value
        else:
            settings[key] = value
    return settings, autoloads


def _quoted_literals(text: str, line_comments: tuple[str, ...]) -> set[str]:
    """Extract simple quoted literals while ignoring line comments."""

    values: set[str] = set()
    for raw in text.splitlines():
        line = raw
        stripped = line.lstrip()
        if stripped.casefold().startswith("rem ") or stripped.startswith("::"):
            continue
        index = 0
        while index < len(line):
            if any(line.startswith(marker, index) for marker in line_comments):
                break
            if line[index] not in {'"', "'"}:
                index += 1
                continue
            quote = line[index]
            index += 1
            chars: list[str] = []
            while index < len(line):
                char = line[index]
                if char == "\\" and index + 1 < len(line):
                    chars.extend((char, line[index + 1]))
                    index += 2
                    continue
                if char == quote:
                    break
                chars.append(char)
                index += 1
            if index < len(line) and line[index] == quote:
                values.add("".join(chars))
            index += 1
    return values


def _python_literals(text: str) -> set[str]:
    tree = ast.parse(text)
    path_types = {
        "Path", "PosixPath", "PurePath", "PurePosixPath", "PureWindowsPath",
        "WindowsPath",
    }
    constructor_names = set(path_types)
    pathlib_modules = {"pathlib"}
    os_modules = {"os"}
    path_modules = {"ntpath", "posixpath"}
    join_names: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                local = alias.asname or alias.name
                if alias.name == "pathlib":
                    pathlib_modules.add(local)
                elif alias.name == "os":
                    os_modules.add(local)
                elif alias.name in {"os.path", "ntpath", "posixpath"}:
                    path_modules.add(local)
        elif isinstance(node, ast.ImportFrom):
            if node.module == "pathlib":
                for alias in node.names:
                    if alias.name in path_types:
                        constructor_names.add(alias.asname or alias.name)
            elif node.module in {"os.path", "ntpath", "posixpath"}:
                for alias in node.names:
                    if alias.name == "join":
                        join_names.add(alias.asname or alias.name)
            elif node.module == "os":
                for alias in node.names:
                    if alias.name == "path":
                        path_modules.add(alias.asname or alias.name)
    docstrings: set[int] = set()
    for node in ast.walk(tree):
        body = getattr(node, "body", None)
        if isinstance(body, list) and body:
            first = body[0]
            if (
                isinstance(first, ast.Expr)
                and isinstance(first.value, ast.Constant)
                and isinstance(first.value.value, str)
            ):
                docstrings.add(id(first.value))
    values = {
        node.value
        for node in ast.walk(tree)
        if isinstance(node, ast.Constant)
        and isinstance(node.value, str)
        and id(node) not in docstrings
    }
    def static_string(node: ast.AST) -> str | None:
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            return node.value
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            left = static_string(node.left)
            right = static_string(node.right)
            return left + right if left is not None and right is not None else None
        return None

    def dotted_name(node: ast.AST) -> str | None:
        if isinstance(node, ast.Name):
            return node.id
        if isinstance(node, ast.Attribute):
            prefix = dotted_name(node.value)
            return f"{prefix}.{node.attr}" if prefix is not None else None
        return None

    def is_path_constructor(node: ast.AST) -> bool:
        if isinstance(node, ast.Name):
            return node.id in constructor_names
        if isinstance(node, ast.Attribute) and node.attr in path_types:
            return dotted_name(node.value) in pathlib_modules
        return False

    def is_path_join(node: ast.AST) -> bool:
        if isinstance(node, ast.Name):
            return node.id in join_names
        dotted = dotted_name(node)
        if dotted is None:
            return False
        if any(dotted == f"{module}.path.join" for module in os_modules):
            return True
        return any(dotted == f"{module}.join" for module in path_modules)

    def join_parts(parts: list[str]) -> str:
        result = parts[0].replace("\\", "/")
        for raw in parts[1:]:
            part = raw.replace("\\", "/").lstrip("/")
            if result.endswith("://"):
                result += part
            else:
                result = result.rstrip("/") + "/" + part
        return result

    def path_parts(node: ast.AST) -> list[str]:
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Div):
            return path_parts(node.left) + path_parts(node.right)
        if isinstance(node, ast.Call) and node.args:
            function = node.func
            arguments = [static_string(argument) for argument in node.args]
            if (
                (is_path_constructor(function) or is_path_join(function))
                and all(value is not None for value in arguments)
            ):
                return [str(value) for value in arguments]
            if (
                isinstance(function, ast.Attribute)
                and function.attr == "joinpath"
                and all(value is not None for value in arguments)
            ):
                return path_parts(function.value) + [str(value) for value in arguments]
        value = static_string(node)
        if value is not None:
            return [value]
        return []

    for node in ast.walk(tree):
        is_path_call = isinstance(node, ast.Call) and (
            is_path_constructor(node.func)
            or is_path_join(node.func)
            or isinstance(node.func, ast.Attribute) and node.func.attr == "joinpath"
        )
        if (isinstance(node, ast.BinOp) and isinstance(node.op, ast.Div)) or is_path_call:
            parts = path_parts(node)
            if len(parts) >= 2:
                values.add(join_parts(parts))
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            value = static_string(node)
            if value is not None:
                values.add(value)
    return values


def _source_literals(path: str, text: str) -> set[str]:
    suffix = Path(path).suffix.casefold()
    if suffix == ".py":
        return _python_literals(text)
    if suffix == ".ps1":
        text = re.sub(r"<#.*?#>", "", text, flags=re.DOTALL)
        return _quoted_literals(text, ("#",))
    if suffix == ".cs":
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
        return _quoted_literals(text, ("//",))
    if suffix in {".godot", ".tscn", ".cfg"}:
        return _quoted_literals(text, (";",))
    return _quoted_literals(text, ("#",))


def _batch_launches_godot_project(text: str) -> bool:
    return any(
        re.fullmatch(
            r'\s*"%OPENBFME_GODOT%"\s+--path\s+"%ROOT%game"\s*', raw,
            flags=re.IGNORECASE,
        )
        is not None
        for raw in text.splitlines()
    )


def _batch_assigns(text: str, variable: str) -> bool:
    assignment = re.compile(
        rf'\s*@?\s*(?:if\s+(?:not\s+)?defined\s+[^\s]+\s+)?'
        rf'set\s+"{re.escape(variable)}=[^"]*"\s*',
        flags=re.IGNORECASE,
    )
    return any(
        assignment.fullmatch(raw) is not None
        for raw in text.splitlines()
    )


def _operational_literal_index(
    root: Path,
    tracked: list[str],
    classifications: dict[str, dict[str, Any]],
    excluded: set[str],
) -> tuple[dict[str, set[str]], list[str], list[str]]:
    result: dict[str, set[str]] = {}
    unreadable: list[str] = []
    unparsable: list[str] = []
    executable_classes = {
        "automation-config", "developer-entrypoint", "importer-source", "release-config",
        "shipping-code", "test-code", "tool-source",
    }
    for relative in tracked:
        if relative in excluded or classifications.get(relative, {}).get("class") not in executable_classes:
            continue
        if Path(relative).suffix.casefold() not in TEXT_SUFFIXES:
            continue
        try:
            text = (root / relative).read_text(encoding="utf-8-sig", errors="replace")
        except OSError:
            unreadable.append(relative)
            continue
        try:
            result[relative] = {
                value.replace("\\", "/") for value in _source_literals(relative, text)
            }
        except SyntaxError:
            unparsable.append(relative)
    return result, unreadable, unparsable


def _operational_references(index: dict[str, set[str]], tokens: list[str]) -> set[str]:
    normalized_tokens = {token.replace("\\", "/") for token in tokens}
    return {
        path
        for path, literals in index.items()
        if literals & normalized_tokens
    }


def _pointer_path(value: str) -> str:
    return value.partition("#")[0].replace("\\", "/")


def validate(root: Path, manifest_path: Path) -> tuple[list[str], dict[str, Any]]:
    errors: list[str] = []
    manifest = _load_json(manifest_path)
    tracked = _tracked_files(root)
    tracked_set = set(tracked)

    if manifest.get("schema") != SCHEMA or manifest.get("schemaVersion") != SCHEMA_VERSION:
        errors.append("unsupported repository-boundaries schema identity")
    authority = manifest.get("authority", {})
    required_authorities = {
        "AGENTS.md", "contracts/rotwk-202-v9.7.7-product-scope.json",
        "contracts/rotwk-202-v9.7.7-baseline.json", "DIRECTION.md", "PLAN.md",
        "orchestration/work-items.json",
    }
    if set(authority.get("derivedFrom", [])) != required_authorities:
        errors.append("boundary map does not derive from the complete canonical authority set")
    if authority.get("rule") != AUTHORITY_RULE:
        errors.append("boundary map canonical-authority disclaimer drifted")

    target = manifest.get("target", {})
    if target.get("productScopeContract") != SCOPE_ID:
        errors.append("target does not name the exact v9.7.7 product-scope contract")
    if target.get("retailBaselineId") != BASELINE_ID:
        errors.append("target does not name the exact v9.7.7 retail baseline")
    if target.get("engine") != "Godot 4.7":
        errors.append("target engine must be Godot 4.7")
    try:
        scope = _load_json(root / "contracts/rotwk-202-v9.7.7-product-scope.json")
        baseline = _load_json(root / "contracts/rotwk-202-v9.7.7-baseline.json")
        if scope.get("contract_id") != SCOPE_ID:
            errors.append("product-scope identity disagrees with boundary target")
        if baseline.get("baselineId") != BASELINE_ID:
            errors.append("retail-baseline identity disagrees with boundary target")
    except (OSError, ValueError, DuplicateKeyError) as exc:
        errors.append(f"cannot validate target contracts: {exc}")

    definitions = manifest.get("classDefinitions", [])
    if not isinstance(definitions, list):
        definitions = []
        errors.append("classDefinitions must be an array")
    class_ids = [row.get("id") for row in definitions if isinstance(row, dict)]
    duplicate_classes = _duplicates([value for value in class_ids if isinstance(value, str)])
    if duplicate_classes:
        errors.append(f"duplicate class definitions: {duplicate_classes}")
    classes: dict[str, dict[str, Any]] = {}
    for row in definitions:
        if not isinstance(row, dict) or not isinstance(row.get("id"), str):
            errors.append("every class definition requires a string id")
            continue
        if row.get("tracked") is not True or not isinstance(row.get("currentEvidenceEligible"), bool):
            errors.append(f"class {row['id']} must pin tracked=true and evidence eligibility")
        classes[row["id"]] = row

    rules = manifest.get("pathRules", [])
    if not isinstance(rules, list):
        rules = []
        errors.append("pathRules must be an array")
    rule_ids = [row.get("id") for row in rules if isinstance(row, dict)]
    duplicate_rules = _duplicates([value for value in rule_ids if isinstance(value, str)])
    if duplicate_rules:
        errors.append(f"duplicate path-rule ids: {duplicate_rules}")
    used_classes: set[str] = set()
    for rule in rules:
        if not isinstance(rule, dict) or not isinstance(rule.get("id"), str):
            errors.append("every path rule requires a string id")
            continue
        class_id = rule.get("class")
        if class_id not in classes:
            errors.append(f"path rule {rule['id']} uses undefined class {class_id!r}")
        else:
            used_classes.add(class_id)
        roots = rule.get("roots", [])
        exclusions = rule.get("exclude", [])
        if not isinstance(roots, list) or not roots:
            errors.append(f"path rule {rule['id']} has no roots")
            continue
        for value in [*roots, *exclusions]:
            if not isinstance(value, str) or not _portable(value):
                errors.append(f"path rule {rule['id']} contains non-portable root {value!r}")
        for exclusion in exclusions:
            if isinstance(exclusion, str) and not any(
                _root_matches(exclusion.rstrip("/"), root) or _root_matches(exclusion, root)
                for root in roots if isinstance(root, str)
            ):
                errors.append(f"path rule {rule['id']} excludes content outside its roots: {exclusion}")

    unused_classes = sorted(set(classes) - used_classes)
    if unused_classes:
        errors.append(f"unused class definitions: {unused_classes}")
    classifications: dict[str, dict[str, Any]] = {}
    for relative in tracked:
        matches = [rule for rule in rules if isinstance(rule, dict) and _rule_matches(relative, rule)]
        if not matches:
            errors.append(f"tracked path has no class: {relative}")
        elif len(matches) > 1:
            errors.append(
                f"tracked path has overlapping classes: {relative} -> {[row.get('id') for row in matches]}"
            )
        else:
            classifications[relative] = matches[0]
    for rule in rules:
        if isinstance(rule, dict) and not any(
            _rule_matches(relative, rule) for relative in tracked
        ):
            errors.append(f"path rule matches no tracked content: {rule.get('id')}")

    try:
        ledger = _load_json(root / "orchestration/work-items.json")
    except (OSError, ValueError, DuplicateKeyError) as exc:
        ledger = {}
        errors.append(f"cannot validate work-item ledger: {exc}")
    items = [row for row in ledger.get("workItems", []) if isinstance(row, dict)]
    item_by_id = {row.get("id"): row for row in items if isinstance(row.get("id"), str)}
    evidence_rows = [row for row in ledger.get("evidenceSources", []) if isinstance(row, dict)]
    evidence_ids = [row.get("id") for row in evidence_rows if isinstance(row.get("id"), str)]
    duplicate_evidence_ids = _duplicates(evidence_ids)
    if duplicate_evidence_ids:
        errors.append(f"duplicate evidence-source ids: {duplicate_evidence_ids}")
    evidence_by_id = {
        row.get("id"): row for row in evidence_rows if isinstance(row.get("id"), str)
    }

    evidence_policy = manifest.get("evidencePolicy", {})
    current_types = set(evidence_policy.get("currentTypes", []))
    historical_types = set(evidence_policy.get("historicalDiagnosticTypes", []))
    private_prefix = evidence_policy.get("privatePrefix")
    if not current_types or not historical_types or private_prefix != "workspace/":
        errors.append("evidence policy must pin current types, historical types, and workspace prefix")
    unknown_types = sorted(
        {str(row.get("type")) for row in evidence_rows} - current_types - historical_types
    )
    if unknown_types:
        errors.append(f"evidence sources use unclassified types: {unknown_types}")
    directly_rooted: dict[str, bool] = {}
    for evidence in evidence_rows:
        evidence_id = evidence.get("id")
        evidence_type = evidence.get("type")
        rooted = False
        direct_path = evidence.get("path")
        if isinstance(direct_path, str):
            normalized = _pointer_path(direct_path)
            rule = classifications.get(normalized)
            if rule is None:
                errors.append(f"evidence {evidence_id} path is not a classified tracked file: {direct_path}")
            elif evidence_type in current_types and not classes.get(rule.get("class"), {}).get(
                "currentEvidenceEligible", False
            ):
                errors.append(f"current evidence {evidence_id} points at ineligible class: {direct_path}")
            elif evidence_type in current_types:
                rooted = True
        for input_id in evidence.get("inputEvidence", []):
            if input_id not in evidence_by_id:
                errors.append(f"evidence {evidence_id} has unknown input evidence {input_id}")
        for pointer in evidence.get("sourcePointers", []):
            normalized = _pointer_path(str(pointer))
            if normalized.startswith(private_prefix):
                if evidence_type in current_types:
                    errors.append(
                        f"current evidence {evidence_id} cannot root itself in private workspace: {pointer}"
                    )
                continue
            rule = classifications.get(normalized)
            if rule is None:
                errors.append(f"evidence {evidence_id} has unresolved source pointer {pointer}")
            elif evidence_type in current_types and not classes.get(rule.get("class"), {}).get(
                "currentEvidenceEligible", False
            ):
                errors.append(f"current evidence {evidence_id} points at ineligible source {pointer}")
            elif evidence_type in current_types:
                rooted = True
        if isinstance(evidence_id, str):
            directly_rooted[evidence_id] = rooted

    def evidence_is_rooted(evidence_id: str, visiting: set[str]) -> bool:
        row = evidence_by_id.get(evidence_id, {})
        if row.get("type") not in current_types:
            return False
        if directly_rooted.get(evidence_id, False):
            return True
        if evidence_id in visiting:
            return False
        return any(
            isinstance(input_id, str)
            and evidence_is_rooted(input_id, visiting | {evidence_id})
            for input_id in row.get("inputEvidence", [])
        )

    for evidence in evidence_rows:
        evidence_id = evidence.get("id")
        if (
            isinstance(evidence_id, str)
            and evidence.get("type") in current_types
            and not evidence_is_rooted(evidence_id, set())
        ):
            errors.append(f"current evidence {evidence_id} has no classified source root")

    generated = manifest.get("generatedArtifacts", [])
    if not isinstance(generated, list):
        generated = []
        errors.append("generatedArtifacts must be an array")
    generated_paths = [row.get("path") for row in generated if isinstance(row, dict)]
    duplicate_generated = _duplicates([value for value in generated_paths if isinstance(value, str)])
    if duplicate_generated:
        errors.append(f"duplicate generated-artifact rows: {duplicate_generated}")
    generated_set = {value for value in generated_paths if isinstance(value, str)}
    expected_generated = {
        path for path, rule in classifications.items()
        if rule.get("class") in {"generated-build-metadata", "historical-derived-evidence"}
    }
    if generated_set != expected_generated:
        errors.append(
            "generated-artifact inventory mismatch: missing="
            f"{sorted(expected_generated - generated_set)} extra={sorted(generated_set - expected_generated)}"
        )
    policy_reference_exclusions = {
        manifest_path.resolve().relative_to(root).as_posix(),
        "docs/CODE_OWNERSHIP.md",
    }
    literal_index, unreadable_executable_files, unparsable_python_files = _operational_literal_index(
        root, tracked, classifications, policy_reference_exclusions | generated_set
    )
    errors.extend(
        f"cannot read tracked executable text: {path}"
        for path in unreadable_executable_files
    )
    errors.extend(
        f"cannot parse tracked Python executable text: {path}"
        for path in unparsable_python_files
    )
    consumer_count = 0
    for row in generated:
        if not isinstance(row, dict):
            errors.append("generated-artifact entries must be objects")
            continue
        generated_keys = {
            "path", "ownerWorkItem", "replacement", "referenceTokens",
            "operationalConsumers",
        }
        if set(row) != generated_keys:
            errors.append(
                f"generated-artifact entry keys differ for {row.get('path')}: "
                f"missing={sorted(generated_keys - set(row))} "
                f"extra={sorted(set(row) - generated_keys)}"
            )
        path = row.get("path")
        owner = row.get("ownerWorkItem")
        if not isinstance(owner, str) or owner not in item_by_id:
            errors.append(f"generated artifact {path} has invalid owner {owner}")
        else:
            candidates = {
                str(value).replace("\\", "/")
                for value in item_by_id[owner].get("ownership", {}).get("candidatePaths", [])
            }
            if path not in candidates:
                errors.append(f"generated artifact {path} lacks an exact candidate path in owner {owner}")
            collisions = _assigned_collisions(str(path), items, owner)
            if collisions:
                errors.append(f"generated artifact {path} collides with assigned lanes: {collisions}")
        if not row.get("replacement"):
            errors.append(f"generated artifact {path} has no replacement disposition")
        tokens = row.get("referenceTokens", [])
        if (
            not isinstance(tokens, list)
            or not tokens
            or any(
                not isinstance(token, str)
                or "/" not in token.replace("\\", "/")
                for token in tokens
            )
        ):
            errors.append(f"generated artifact {path} has invalid reference tokens")
            tokens = []
        expected_tokens = {
            str(path), "res://" + str(path).removeprefix("game/")
        }
        if set(tokens) != expected_tokens:
            errors.append(
                f"generated artifact {path} reference tokens drifted: "
                f"actual={sorted(tokens)} expected={sorted(expected_tokens)}"
            )
        consumers = row.get("operationalConsumers", [])
        consumer_paths = [entry.get("path") for entry in consumers if isinstance(entry, dict)]
        if _duplicates([value for value in consumer_paths if isinstance(value, str)]):
            errors.append(f"generated artifact {path} has duplicate consumers")
        declared = {value for value in consumer_paths if isinstance(value, str)}
        actual = _operational_references(literal_index, tokens)
        if declared != actual:
            errors.append(
                f"generated artifact {path} consumer mismatch: missing={sorted(actual - declared)} "
                f"extra={sorted(declared - actual)}"
            )
        for consumer in consumers:
            if not isinstance(consumer, dict) or consumer.get("path") not in tracked_set:
                errors.append(f"generated artifact {path} has untracked consumer {consumer}")
            elif not consumer.get("role"):
                errors.append(f"generated artifact {path} consumer lacks a role: {consumer.get('path')}")
            else:
                consumer_class = classifications.get(consumer["path"], {}).get("class")
                allowed_roles = {
                    "shipping-code": {"shipping-runtime"},
                    "test-code": {"test"},
                    "importer-source": {"generator"},
                    "tool-source": {"generator", "test"},
                }
                if consumer.get("role") not in allowed_roles.get(consumer_class, set()):
                    errors.append(
                        f"generated artifact {path} consumer role disagrees with class: "
                        f"{consumer['path']} role={consumer.get('role')} class={consumer_class}"
                    )
        consumer_count += len(declared)

    for evidence in evidence_rows:
        evidence_id = evidence.get("id")
        for stale in evidence.get("staleArtifacts", []):
            path = _pointer_path(str(stale.get("path", ""))) if isinstance(stale, dict) else ""
            if path.startswith(private_prefix):
                continue
            rule = classifications.get(path)
            if rule is None:
                errors.append(f"evidence {evidence_id} has unclassified stale artifact {path}")
            elif classes.get(rule.get("class"), {}).get("currentEvidenceEligible", False):
                errors.append(f"evidence {evidence_id} marks an eligible class stale: {path}")
            elif path not in generated_set:
                errors.append(f"tracked stale artifact is not in generated-artifact map: {path}")
    for item in items:
        source_ids = item.get("sourceEvidence", [])
        for evidence_id in source_ids:
            if not isinstance(evidence_id, str) or "/" in evidence_id or "\\" in evidence_id:
                errors.append(f"work item {item.get('id')} uses a raw evidence path: {evidence_id}")
            elif evidence_id not in evidence_by_id:
                errors.append(f"work item {item.get('id')} cites unknown evidence {evidence_id}")
        levels = set(item.get("acceptance", {}).get("requiredLevels", []))
        if item.get("status") == "complete" and levels - {"L0"}:
            stale_ids = [
                evidence_id for evidence_id in source_ids
                if evidence_by_id.get(evidence_id, {}).get("type") in historical_types
            ]
            if stale_ids:
                errors.append(
                    f"completed non-L0 work item {item.get('id')} cites historical diagnostics: {stale_ids}"
                )

    runtime = manifest.get("runtimeAuthority", {})
    project_path = root / str(runtime.get("project", "game/project.godot"))
    try:
        project_text = project_path.read_text(encoding="utf-8-sig")
        settings, actual_autoloads = _project_settings(project_text)
    except OSError as exc:
        project_text, settings, actual_autoloads = "", {}, {}
        errors.append(f"cannot read Godot project: {exc}")
    expected_settings = {
        "config/name": runtime.get("name"),
        "config/description": runtime.get("description"),
        "run/main_scene": runtime.get("mainScene"),
    }
    for key, value in expected_settings.items():
        if not value or settings.get(key) != value:
            errors.append(f"Godot project setting {key} does not match runtime authority")
    shipping = runtime.get("shippingChain", [])
    shipping_paths = [row.get("path") for row in shipping if isinstance(row, dict)]
    if _duplicates([value for value in shipping_paths if isinstance(value, str)]):
        errors.append("shipping chain contains duplicate paths")
    for row in shipping:
        path = row.get("path") if isinstance(row, dict) else None
        if path not in tracked_set or not row.get("role"):
            errors.append(f"shipping-chain entry is unresolved or has no role: {row}")
        elif classifications.get(path, {}).get("class") not in {"shipping-code", "developer-entrypoint"}:
            errors.append(f"shipping-chain path has a non-shipping class: {path}")
    links = runtime.get("links", [])
    if not isinstance(links, list):
        links = []
        errors.append("shipping-chain links must be an array")
    expected_edges = list(zip(shipping_paths, shipping_paths[1:]))
    declared_edges = [
        (link.get("from"), link.get("to"))
        for link in links
        if isinstance(link, dict)
    ]
    if declared_edges != expected_edges:
        errors.append(
            f"shipping-chain links are not complete adjacent edges: "
            f"actual={declared_edges} expected={expected_edges}"
        )
    for link in links:
        path = link.get("from") if isinstance(link, dict) else None
        target_path = link.get("to") if isinstance(link, dict) else None
        token = link.get("token") if isinstance(link, dict) else None
        if not isinstance(link, dict) or set(link) != {"from", "to", "token"}:
            errors.append(f"shipping-chain link has invalid keys: {link}")
            continue
        if path not in shipping_paths or target_path not in shipping_paths:
            errors.append(f"shipping-chain link leaves the declared chain: {path} -> {target_path}")
            continue
        if shipping_paths.index(str(target_path)) != shipping_paths.index(str(path)) + 1:
            errors.append(f"shipping-chain link is not adjacent: {path} -> {target_path}")
        expected_token = (
            '--path "%ROOT%game"'
            if path == "run_game.bat" and target_path == "game/project.godot"
            else "res://" + str(target_path).removeprefix("game/")
        )
        if token != expected_token:
            errors.append(f"shipping-chain token does not identify its target: {path} -> {target_path}")
        else:
            try:
                link_text = (root / str(path)).read_text(
                    encoding="utf-8-sig", errors="replace"
                )
                present = (
                    _batch_launches_godot_project(link_text)
                    if Path(str(path)).suffix.casefold() == ".bat"
                    else token in _source_literals(str(path), link_text)
                )
            except (OSError, SyntaxError):
                present = False
            if not present:
                errors.append(f"shipping-chain link is not executable: {path} -> {target_path}")

    expected_autoloads: dict[str, str] = {}
    for row in runtime.get("autoloads", []):
        if not isinstance(row, dict) or not row.get("name") or row.get("path") not in tracked_set:
            errors.append(f"autoload map entry is unresolved: {row}")
            continue
        if classifications.get(row["path"], {}).get("class") != "shipping-code":
            errors.append(f"autoload path is not shipping code: {row['path']}")
        expected_autoloads[row["name"]] = "*res://" + row["path"].removeprefix("game/")
    if actual_autoloads != expected_autoloads:
        errors.append(
            f"Godot autoload map mismatch: actual={actual_autoloads} expected={expected_autoloads}"
        )

    startup_debt = runtime.get("startupAssetDebt", [])
    debt_keys = [row.get("projectKey") for row in startup_debt if isinstance(row, dict)]
    if _duplicates([value for value in debt_keys if isinstance(value, str)]):
        errors.append("startup asset debt contains duplicate project keys")
    for row in startup_debt:
        key = row.get("projectKey") if isinstance(row, dict) else None
        resource = row.get("resource") if isinstance(row, dict) else None
        if settings.get(str(key)) != resource:
            errors.append(f"startup asset debt does not match project setting: {key}")
            continue
        tracked_path = "game/" + str(resource).removeprefix("res://")
        if classifications.get(tracked_path, {}).get("class") != "public-asset-debt":
            errors.append(f"startup asset is not explicitly quarantined: {tracked_path}")
        owner = row.get("ownerWorkItem")
        if not isinstance(owner, str) or owner not in item_by_id or not row.get("reason"):
            errors.append(f"startup asset debt lacks owner/reason: {key}")
        else:
            candidates = {
                str(value).replace("\\", "/")
                for value in item_by_id[owner].get("ownership", {}).get("candidatePaths", [])
            }
            if tracked_path not in candidates:
                errors.append(f"startup asset {tracked_path} lacks an exact candidate path in owner {owner}")
            collisions = _assigned_collisions(tracked_path, items, owner)
            if collisions:
                errors.append(f"startup asset {tracked_path} collides with assigned lanes: {collisions}")
    for key in ("boot_splash/image", "config/icon"):
        if key in settings and key not in debt_keys:
            errors.append(f"project startup asset is not declared: {key}")
    for row in runtime.get("nonShipping", []):
        path = row.get("path") if isinstance(row, dict) else None
        if not isinstance(path, str) or not any(
            candidate == path or candidate.startswith(path + "/") for candidate in tracked
        ) or not row.get("admission"):
            errors.append(f"non-shipping boundary is unresolved or lacks admission: {row}")

    for boundary in manifest.get("generatedBoundaries", []):
        path = boundary.get("path") if isinstance(boundary, dict) else None
        if not isinstance(path, str) or not _portable(path) or not boundary.get("kind"):
            errors.append(f"invalid generated boundary: {boundary}")
            continue
        if boundary.get("tracked") is not False:
            errors.append(f"private generated boundary must pin tracked=false: {path}")
        if any(_root_matches(candidate, path) for candidate in tracked):
            errors.append(f"private generated boundary contains tracked files: {path}")

    for debt in manifest.get("runtimeDebt", []):
        path = debt.get("path") if isinstance(debt, dict) else None
        owner = debt.get("ownerWorkItem") if isinstance(debt, dict) else None
        tokens = debt.get("tokens", []) if isinstance(debt, dict) else []
        debt_keys = {"id", "path", "tokens", "ownerWorkItem", "disposition"}
        if not isinstance(debt, dict) or set(debt) != debt_keys:
            errors.append(f"runtime debt has invalid keys: {debt}")
            continue
        if (
            path not in tracked_set
            or not isinstance(owner, str)
            or owner not in item_by_id
            or not debt.get("disposition")
            or not isinstance(tokens, list)
            or not tokens
            or any(not isinstance(token, str) or not token for token in tokens)
        ):
            errors.append(f"runtime debt is unresolved: {debt}")
            continue
        try:
            text = (root / path).read_text(encoding="utf-8-sig", errors="replace")
            token_drift = (
                any(not _batch_assigns(text, token) for token in tokens)
                if Path(str(path)).suffix.casefold() == ".bat"
                else any(token not in _source_literals(str(path), text) for token in tokens)
            )
        except (OSError, SyntaxError):
            token_drift = True
        if not tokens or token_drift:
            errors.append(f"runtime debt tokens drifted: {debt.get('id')}")
        candidates = [
            str(value).replace("\\", "/")
            for value in item_by_id[owner].get("ownership", {}).get("candidatePaths", [])
        ]
        if path not in candidates:
            errors.append(f"runtime debt {debt.get('id')} lacks an exact candidate path in owner {owner}")
        collisions = _assigned_collisions(path, items, owner)
        if collisions:
            errors.append(f"runtime debt {debt.get('id')} collides with assigned lanes: {collisions}")

    conflict = manifest.get("highConflict", {})
    threshold = conflict.get("thresholdLines")
    extensions = conflict.get("extensions", [])
    threshold_valid = isinstance(threshold, int) and threshold >= 1000
    if not threshold_valid:
        errors.append("high-conflict threshold must be an integer of at least 1000 lines")
        threshold = sys.maxsize
    if not isinstance(extensions, list) or not extensions:
        errors.append("high-conflict extensions must be a non-empty array")
        extensions = []
    actual_conflicts: dict[str, int] = {}
    for relative in tracked:
        if Path(relative).suffix not in extensions:
            continue
        count = _line_count(root / relative)
        if count >= threshold:
            actual_conflicts[relative] = count
    conflict_rows = conflict.get("files", [])
    if not isinstance(conflict_rows, list):
        conflict_rows = []
        errors.append("high-conflict files must be an array")
    conflict_paths = [row.get("path") for row in conflict_rows if isinstance(row, dict)]
    if _duplicates([value for value in conflict_paths if isinstance(value, str)]):
        errors.append("high-conflict inventory contains duplicate paths")
    declared_conflicts = {value for value in conflict_paths if isinstance(value, str)}
    if declared_conflicts != set(actual_conflicts):
        errors.append(
            "high-conflict inventory mismatch: missing="
            f"{sorted(set(actual_conflicts) - declared_conflicts)} extra="
            f"{sorted(declared_conflicts - set(actual_conflicts))}"
        )
    for row in conflict_rows:
        if not isinstance(row, dict):
            errors.append("high-conflict entries must be objects")
            continue
        expected_keys = {"path", "ownerWorkItem", "seams", "focusedTests"}
        if set(row) != expected_keys:
            errors.append(
                f"high-conflict entry keys differ for {row.get('path')}: "
                f"missing={sorted(expected_keys - set(row))} extra={sorted(set(row) - expected_keys)}"
            )
        path = row.get("path")
        owner = row.get("ownerWorkItem")
        seams = row.get("seams", [])
        tests = row.get("focusedTests", [])
        if not isinstance(owner, str) or owner not in item_by_id:
            errors.append(f"high-conflict path {path} has invalid single owner {owner}")
        else:
            candidates = [
                str(value).replace("\\", "/")
                for value in item_by_id[owner].get("ownership", {}).get("candidatePaths", [])
            ]
            if path not in candidates:
                errors.append(f"high-conflict path {path} lacks an exact candidate path in owner {owner}")
            collisions = _assigned_collisions(str(path), items, owner)
            if collisions:
                errors.append(f"high-conflict path {path} collides with assigned lanes: {collisions}")
        if not isinstance(seams, list) or len(seams) < 2 or any(not seam for seam in seams):
            errors.append(f"high-conflict path {path} requires at least two named seams")
        if not isinstance(tests, list) or not tests:
            errors.append(f"high-conflict path {path} has no focused test route")
        for test in tests:
            if not isinstance(test, str) or any(char in test for char in "*?["):
                errors.append(f"high-conflict path {path} uses a non-exact test route: {test}")
            elif test not in tracked_set or classifications.get(test, {}).get("class") != "test-code":
                errors.append(f"high-conflict path {path} has an unresolved test route: {test}")

    naming = manifest.get("assetNaming", {})
    required_naming = {
        "retailSourceKeys", "convertedPrivatePayloads", "publicAuthoredAssets",
        "aliases", "currentDebtRoots", "ownerWorkItem",
    }
    missing_naming = sorted(key for key in required_naming if not naming.get(key))
    if missing_naming:
        errors.append(f"asset naming policy is incomplete: {missing_naming}")
    if naming.get("ownerWorkItem") not in item_by_id:
        errors.append("asset naming debt has no valid owner work item")
    current_debt_roots = naming.get("currentDebtRoots", [])
    path_rule_debt_roots = {
        root_path
        for rule in rules
        if isinstance(rule, dict) and rule.get("class") == "public-asset-debt"
        for root_path in rule.get("roots", [])
    }
    if set(current_debt_roots) != path_rule_debt_roots:
        errors.append(
            "asset naming debt roots disagree with path classes: "
            f"naming={sorted(current_debt_roots)} rules={sorted(path_rule_debt_roots)}"
        )
    for root_path in current_debt_roots:
        matched = [
            path for path, rule in classifications.items()
            if _root_matches(path, root_path) and rule.get("class") == "public-asset-debt"
        ]
        if not matched:
            errors.append(f"asset debt root has no quarantined tracked content: {root_path}")

    digest = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
    top_level = {path.split("/", 1)[0] for path in tracked}
    game_data = {
        "game/data/" + path.removeprefix("game/data/").split("/", 1)[0]
        for path in tracked if path.startswith("game/data/")
    }
    receipt = {
        "schema": "openbfme.repository-boundaries.receipt",
        "schemaVersion": 2,
        "revision": _git(root, "rev-parse", "HEAD").strip(),
        "target": {"productScopeContract": SCOPE_ID, "retailBaselineId": BASELINE_ID},
        "manifestSha256": digest,
        "status": "pass" if not errors else "fail",
        "counts": {
            "trackedFiles": len(tracked),
            "classifiedFiles": len(classifications),
            "topLevelPaths": len(top_level),
            "gameDataRoots": len(game_data),
            "pathRules": len(rules),
            "generatedArtifacts": len(generated),
            "generatedConsumers": consumer_count,
            "shippingChainEntries": len(shipping_paths),
            "autoloads": len(expected_autoloads),
            "highConflictFiles": len(actual_conflicts),
        },
        "highConflictLineCounts": actual_conflicts,
        "violations": errors,
    }
    return errors, receipt


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate without mutating tracked state")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    if not args.check:
        parser.error("--check is required")
    root = args.root.resolve()
    manifest = args.manifest or root / "config/repository-boundaries.json"
    output = args.output or root / "workspace/logs/P0-CODE-MAP-001/repository-boundaries.json"
    if not manifest.is_absolute():
        manifest = root / manifest
    if not output.is_absolute():
        output = root / output
    try:
        errors, receipt = validate(root, manifest)
    except (OSError, ValueError, RuntimeError, DuplicateKeyError) as exc:
        print(f"REPOSITORY_BOUNDARIES FAIL fatal={exc}", file=sys.stderr)
        return 1
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if errors:
        for error in errors:
            print(f"REPOSITORY_BOUNDARIES FAIL {error}", file=sys.stderr)
        return 1
    counts = receipt["counts"]
    print(
        "REPOSITORY_BOUNDARIES PASS "
        f"tracked={counts['trackedFiles']} classified={counts['classifiedFiles']} "
        f"rules={counts['pathRules']} consumers={counts['generatedConsumers']} "
        f"autoloads={counts['autoloads']} high_conflict={counts['highConflictFiles']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
