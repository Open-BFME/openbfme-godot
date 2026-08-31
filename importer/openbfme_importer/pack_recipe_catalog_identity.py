"""Fail-closed alignment between pack recipes and InstallCatalog archives.

Convert may discover physical leaves on an effective-assets tree that no
catalog archive ships (orphan extract debris, stale host files). Pack cook
resolves resources only through :meth:`InstallCatalog.resolve_exact`. Emitting
those orphans as required recipe patterns produces the mid-pack failure
``required profile resources did not resolve``.

This module is the shared gate: every required resource pattern on a compiled
pack recipe must archive-resolve when a catalog is available.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
import re
from typing import Protocol


class _CatalogResolve(Protocol):
    def resolve_exact(self, virtual_path: str) -> object | None: ...


class PackRecipeCatalogIdentityError(ValueError):
    """A pack recipe names required patterns the catalog cannot resolve."""


_SHA256 = re.compile(r"^[0-9a-f]{64}$")


def audit_pack_target_identity(
    pack: Mapping[str, object], *, baseline_id: str, catalog_sha256: str,
) -> dict[str, object]:
    """Classify one immutable pack manifest against the exact target identity."""

    observed = {
        "baselineId": pack.get("sourceBaselineId"),
        "catalogSha256": pack.get("sourceCatalogIdentitySha256"),
        "recipeSha256": pack.get("sourceRecipeSha256"),
    }
    failures: list[str] = []
    if observed["baselineId"] != baseline_id:
        failures.append("missing-or-mismatched-source-baseline")
    if observed["catalogSha256"] != catalog_sha256:
        failures.append("missing-or-mismatched-source-catalog")
    recipe = observed["recipeSha256"]
    if not isinstance(recipe, str) or _SHA256.fullmatch(recipe) is None:
        failures.append("missing-or-invalid-source-recipe")
    return {"matchesTarget": not failures, "observed": observed, "failures": failures}


def missing_required_catalog_patterns(
    recipe: Mapping[str, object],
    catalog: _CatalogResolve,
) -> list[tuple[str, str]]:
    """Return ``(resourceId, pattern)`` rows that fail catalog resolve."""

    resources = recipe.get("resources")
    if not isinstance(resources, list):
        raise PackRecipeCatalogIdentityError("pack recipe resources are invalid")
    missing: list[tuple[str, str]] = []
    for row in resources:
        if not isinstance(row, Mapping):
            raise PackRecipeCatalogIdentityError("pack recipe resource row is invalid")
        if row.get("required") is False:
            continue
        resource_id = str(row.get("id") or "")
        patterns = row.get("patterns")
        if not isinstance(patterns, list) or not patterns:
            raise PackRecipeCatalogIdentityError(
                f"pack recipe resource has no patterns: {resource_id or '<unknown>'}"
            )
        for pattern in patterns:
            if not isinstance(pattern, str) or not pattern.strip():
                raise PackRecipeCatalogIdentityError(
                    f"pack recipe resource pattern is invalid: {resource_id or '<unknown>'}"
                )
            if catalog.resolve_exact(pattern) is None:
                missing.append((resource_id, pattern))
    return missing


def assert_pack_recipe_catalog_identity(
    recipe: Mapping[str, object],
    catalog: _CatalogResolve | None,
    *,
    object_id: str = "",
) -> None:
    """Raise when any required recipe pattern is absent from the catalog.

    ``catalog is None`` skips the gate (legacy/test callers without a catalog).
    """

    if catalog is None:
        return
    missing = missing_required_catalog_patterns(recipe, catalog)
    if not missing:
        return
    sample = "; ".join(f"{rid}:{path}" for rid, path in missing[:8])
    more = "" if len(missing) <= 8 else f" (+{len(missing) - 8} more)"
    owner = f" for {object_id}" if object_id else ""
    raise PackRecipeCatalogIdentityError(
        f"pack recipe{owner} has {len(missing)} required pattern(s) not in "
        f"catalog archives (effective-assets orphans cannot pack): {sample}{more}"
    )


def filter_virtual_paths_to_catalog(
    paths: Sequence[str],
    catalog: _CatalogResolve | None,
) -> tuple[str, ...]:
    """Keep only virtual paths that catalog-resolve (identity filter)."""

    if catalog is None:
        return tuple(paths)
    return tuple(path for path in paths if catalog.resolve_exact(path) is not None)


__all__ = [
    "PackRecipeCatalogIdentityError",
    "audit_pack_target_identity",
    "assert_pack_recipe_catalog_identity",
    "filter_virtual_paths_to_catalog",
    "missing_required_catalog_patterns",
]
