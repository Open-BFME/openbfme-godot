"""Date generated evidence against the code that measured it.

Two wrong numbers in one session came from stored reports that looked
authoritative and were not: the report predated a decoder fix, and every
consumer republished its figures without any way to notice.  This module is
the guard.  A generated report carries a *measurement provenance* block; a
consumer that republishes a stored report's figures verifies that block
against the code currently on disk and either surfaces a loud verdict or
refuses outright.

The datum that matters is not "which commit was HEAD" -- an artifact must not
be declared stale merely because unrelated code moved -- but "have the bytes
of the measuring code changed".  So the primary identity is a *measurement
fingerprint*: the SHA-256 over the names and file hashes of the measuring
module's import closure inside :mod:`openbfme_importer`.  Editing any module
in that closure (even a comment) changes the fingerprint; editing anything
outside it does not.  The Git commit and worktree cleanliness are recorded as
human-facing context only, via the exact-root helpers in :mod:`.tools`
(``git rev-parse`` walks upward, so a non-repository directory would otherwise
borrow an enclosing checkout's identity; see ``git_revision_at_exact_root``).
A missing repository degrades to a recorded ``null`` commit -- the
fingerprint still dates the report.

Fail-closed rules:

* a provenance block that is missing, malformed, or dates a different
  measurement raises :class:`MeasurementProvenanceError` -- an undatable
  report must never quietly pass as fresh;
* an import that cannot be resolved to a package source file raises rather
  than silently shrinking the closure.

This module itself (and anything only it imports) is deliberately excluded
from every closure: stamping provenance must not make evidence stale.  The
one blind spot that leaves: a change to the closure *walker* alters computed
fingerprints and will read as staleness of all prior reports.  That is a
loud false positive, never a silent pass.
"""

from __future__ import annotations

import ast
from collections.abc import Mapping
import hashlib
import json
from pathlib import Path


MEASUREMENT_PROVENANCE_SCHEMA = "openbfme.measurement-provenance"
MEASUREMENT_PROVENANCE_SCHEMA_VERSION = 0

_PACKAGE = "openbfme_importer"
# The guard's own machinery is never part of a measurement closure: stamping
# provenance is not measuring, and including it would let a guard edit mark
# every stored report stale.
_EXCLUDED_MODULES = frozenset({"measurement_provenance"})


class MeasurementProvenanceError(ValueError):
    """Raised when measurement provenance cannot be computed or trusted."""


def _canonical_sha256(value: object) -> str:
    encoded = json.dumps(
        value,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdef" for character in value)
    )


def _short_module_name(root_module: str) -> str:
    if not isinstance(root_module, str) or not root_module:
        raise MeasurementProvenanceError("measurement module must be a name")
    name = root_module
    if name.startswith(_PACKAGE + "."):
        name = name[len(_PACKAGE) + 1 :]
    if name == _PACKAGE or not name.isidentifier():
        raise MeasurementProvenanceError(
            f"measurement module name is not a package module: {root_module!r}"
        )
    if name in _EXCLUDED_MODULES:
        raise MeasurementProvenanceError(
            "the provenance machinery is not a measurement and has no closure"
        )
    return name


def _module_imports(source: str, filename: str) -> set[str]:
    """Return every in-package module a source file imports, at any depth.

    Function-local (lazy) imports count: they affect behaviour exactly like
    top-level ones.  Anything that cannot be resolved within the flat
    :mod:`openbfme_importer` package raises rather than being dropped.
    """

    try:
        tree = ast.parse(source, filename=filename)
    except SyntaxError as exc:
        raise MeasurementProvenanceError(
            f"measurement module does not parse: {filename}"
        ) from exc
    found: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                if alias.name == _PACKAGE:
                    raise MeasurementProvenanceError(
                        f"bare package import cannot be fingerprinted: {filename}"
                    )
                if alias.name.startswith(_PACKAGE + "."):
                    found.add(alias.name.split(".")[1])
        elif isinstance(node, ast.ImportFrom):
            if node.level >= 2:
                raise MeasurementProvenanceError(
                    f"parent-relative import cannot be fingerprinted: {filename}"
                )
            if node.level == 1:
                if node.module is None:
                    found.update(alias.name for alias in node.names)
                else:
                    found.add(node.module.split(".")[0])
            elif node.module == _PACKAGE:
                found.update(alias.name for alias in node.names)
            elif node.module is not None and node.module.startswith(_PACKAGE + "."):
                found.add(node.module.split(".")[1])
    return found


def measurement_module_closure(
    root_module: str,
    *,
    package_root: Path | str | None = None,
) -> tuple[tuple[str, str], ...]:
    """Return ``(module, source_sha256)`` for the measurement import closure.

    The walk starts at ``root_module`` and follows every in-package import
    transitively, hashing each module's exact source bytes.  The result is
    sorted and deterministic.  A module that cannot be resolved to a source
    file fails the walk; nothing is silently skipped except the provenance
    machinery itself.
    """

    name = _short_module_name(root_module)
    package_dir = (
        Path(__file__).resolve().parent
        if package_root is None
        else Path(package_root)
    )
    pending = [name]
    hashes: dict[str, str] = {}
    while pending:
        module = pending.pop()
        if module in hashes or module in _EXCLUDED_MODULES:
            continue
        path = package_dir / f"{module}.py"
        if not path.is_file():
            raise MeasurementProvenanceError(
                f"measurement module has no source file: {module!r}"
            )
        data = path.read_bytes()
        hashes[module] = hashlib.sha256(data).hexdigest()
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise MeasurementProvenanceError(
                f"measurement module is not UTF-8: {module!r}"
            ) from exc
        pending.extend(
            child
            for child in sorted(_module_imports(text, str(path)))
            if child not in hashes
        )
    return tuple(sorted(hashes.items()))


def measurement_fingerprint(
    root_module: str,
    *,
    package_root: Path | str | None = None,
) -> dict[str, object]:
    """Fingerprint the code that performs one measurement, right now."""

    name = _short_module_name(root_module)
    closure = measurement_module_closure(name, package_root=package_root)
    modules = [
        {"module": module, "sha256": digest} for module, digest in closure
    ]
    fingerprint = _canonical_sha256(
        {
            "schema": "openbfme.measurement-fingerprint",
            "schemaVersion": MEASUREMENT_PROVENANCE_SCHEMA_VERSION,
            "rootModule": f"{_PACKAGE}.{name}",
            "modules": modules,
        }
    )
    return {
        "rootModule": f"{_PACKAGE}.{name}",
        "moduleCount": len(modules),
        "modules": modules,
        "fingerprintSha256": fingerprint,
    }


def measurement_provenance(
    root_module: str,
    *,
    package_root: Path | str | None = None,
) -> dict[str, object]:
    """Return the JSON-ready provenance block a generated report must carry.

    The fingerprint is authoritative; the Git fields are human context.  The
    commit is read only when this package sits at the exact repository root
    (borrowed identities are worse than none) and degrades to ``None``.
    """

    from .tools import (
        git_revision_at_exact_root,
        git_worktree_clean_at_exact_root,
    )

    fingerprint = measurement_fingerprint(
        root_module, package_root=package_root
    )
    repository = Path(__file__).resolve().parents[2]
    return {
        "schema": MEASUREMENT_PROVENANCE_SCHEMA,
        "schemaVersion": MEASUREMENT_PROVENANCE_SCHEMA_VERSION,
        **fingerprint,
        "gitCommit": git_revision_at_exact_root(repository),
        "gitWorktreeClean": git_worktree_clean_at_exact_root(repository),
    }


def measurement_fingerprint_verdict(
    recorded: object,
    root_module: str,
    *,
    package_root: Path | str | None = None,
) -> dict[str, object]:
    """Date a stored report's provenance against the code on disk.

    Returns a JSON-ready verdict whose ``status`` is ``"fresh"`` when the
    recorded fingerprint matches the current one and ``"stale"`` otherwise,
    with ``staleModules`` naming exactly which measurement modules moved.
    A provenance block that is missing, malformed, or dates a *different*
    measurement raises :class:`MeasurementProvenanceError`: the caller must
    not republish figures it cannot date.
    """

    name = _short_module_name(root_module)
    if not isinstance(recorded, Mapping):
        raise MeasurementProvenanceError(
            "report carries no measurement provenance and cannot be dated"
        )
    recorded_fingerprint = recorded.get("fingerprintSha256")
    if not _is_sha256(recorded_fingerprint):
        raise MeasurementProvenanceError(
            "report provenance lacks a fingerprintSha256 and cannot be dated"
        )
    expected_root = f"{_PACKAGE}.{name}"
    if recorded.get("rootModule") != expected_root:
        raise MeasurementProvenanceError(
            "report provenance dates a different measurement: "
            f"{recorded.get('rootModule')!r} is not {expected_root!r}"
        )
    current = measurement_fingerprint(name, package_root=package_root)

    recorded_modules: dict[str, str] = {}
    modules = recorded.get("modules")
    if isinstance(modules, list):
        for row in modules:
            if (
                isinstance(row, Mapping)
                and isinstance(row.get("module"), str)
                and _is_sha256(row.get("sha256"))
            ):
                recorded_modules[row["module"]] = row["sha256"]
    current_modules = {
        row["module"]: row["sha256"] for row in current["modules"]
    }
    if recorded_modules:
        stale_modules = sorted(
            module
            for module in set(recorded_modules) | set(current_modules)
            if recorded_modules.get(module) != current_modules.get(module)
        )
    else:
        # The report dated itself with a fingerprint but no module inventory:
        # a mismatch is still loud, just unattributed.
        stale_modules = []
    status = (
        "fresh"
        if recorded_fingerprint == current["fingerprintSha256"]
        else "stale"
    )
    git_commit = recorded.get("gitCommit")
    return {
        "status": status,
        "rootModule": expected_root,
        "recordedFingerprintSha256": recorded_fingerprint,
        "currentFingerprintSha256": current["fingerprintSha256"],
        "recordedGitCommit": git_commit if isinstance(git_commit, str) else None,
        "staleModules": stale_modules if status == "stale" else [],
    }


__all__ = [
    "MEASUREMENT_PROVENANCE_SCHEMA",
    "MEASUREMENT_PROVENANCE_SCHEMA_VERSION",
    "MeasurementProvenanceError",
    "measurement_fingerprint",
    "measurement_fingerprint_verdict",
    "measurement_module_closure",
    "measurement_provenance",
]
