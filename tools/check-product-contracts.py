"""Fail-closed validation for tracked OpenBFME product policy contracts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
PRODUCT_PATH = ROOT / "contracts" / "rotwk-201-product-scope.json"
MODDING_PATH = ROOT / "contracts" / "openbfme-modding-contract.json"
# Historical BFME2-only policy (superseded): contracts/bfme2-106-product-scope.json


def _load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path.name}: root must be an object")
    return value


def _policy_digest(document: dict[str, Any]) -> str:
    envelope = dict(document)
    digest = dict(envelope.get("policy_digest", {}))
    digest.pop("value", None)
    envelope["policy_digest"] = digest
    canonical = json.dumps(
        envelope, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def _assert_keys(document: dict[str, Any], expected: set[str], label: str) -> None:
    actual = set(document)
    if actual != expected:
        raise ValueError(
            f"{label}: top-level keys differ; missing={sorted(expected - actual)} "
            f"unknown={sorted(actual - expected)}"
        )


def _ids(rows: Any, label: str) -> set[str]:
    if not isinstance(rows, list) or not rows:
        raise ValueError(f"{label}: expected a non-empty array")
    values = [row.get("id") for row in rows if isinstance(row, dict)]
    if len(values) != len(rows) or any(not isinstance(value, str) for value in values):
        raise ValueError(f"{label}: every row must have a string id")
    if len(set(values)) != len(values):
        raise ValueError(f"{label}: duplicate id")
    return set(values)


def validate_documents(
    product: dict[str, Any], modding: dict[str, Any]
) -> tuple[str, str]:
    _assert_keys(
        product,
        {
            "schema_version", "contract_id", "implementation_status", "game",
            "patch", "policy_digest", "classification_policy", "product_domains",
            "root_discovery_queries", "evidence_lanes", "evidence_record_contract",
            "claim_profiles", "completion_policy", "explicit_exclusions",
            "generated_outputs",
        },
        "product contract",
    )
    _assert_keys(
        modding,
        {
            "schema_version", "contract_id", "authority", "implementation_status",
            "policy_digest", "compatibility_model", "pack_categories", "manifest",
            "resolution", "server_policy", "resource_safety", "validation",
            "dedicated_server",
        },
        "modding contract",
    )

    product_digest = _policy_digest(product)
    modding_digest = _policy_digest(modding)
    if product.get("policy_digest", {}).get("value") != product_digest:
        raise ValueError("product contract: policy digest mismatch")
    if modding.get("policy_digest", {}).get("value") != modding_digest:
        raise ValueError("modding contract: policy digest mismatch")

    domains = _ids(product["product_domains"], "product domains")
    roots = _ids(product["root_discovery_queries"], "root discovery queries")
    lanes = _ids(product["evidence_lanes"], "evidence lanes")
    claims = _ids(product["claim_profiles"], "claim profiles")
    if "rotwk-201-complete" not in claims:
        raise ValueError("product contract: missing complete claim profile")

    for domain in product["product_domains"]:
        if domain.get("authority") == "retail" and not domain.get(
            "required_root_queries"
        ):
            raise ValueError(f"domain {domain['id']}: retail roots must not be empty")
        if not domain.get("required_evidence_lanes"):
            raise ValueError(f"domain {domain['id']}: evidence lanes must not be empty")
        unknown_roots = set(domain.get("required_root_queries", [])) - roots
        unknown_lanes = set(domain.get("required_evidence_lanes", [])) - lanes
        if unknown_roots or unknown_lanes:
            raise ValueError(
                f"domain {domain['id']}: unknown roots={sorted(unknown_roots)} "
                f"lanes={sorted(unknown_lanes)}"
            )

    for query in product["root_discovery_queries"]:
        for field in ("source_authority", "operation", "unknown_handling"):
            if not isinstance(query.get(field), str) or not query[field].strip():
                raise ValueError(f"root query {query['id']}: missing {field}")
    for lane in product["evidence_lanes"]:
        fields = lane.get("required_fields")
        if not isinstance(fields, list) or not fields or not all(
            isinstance(field, str) and field for field in fields
        ):
            raise ValueError(f"evidence lane {lane['id']}: required_fields is empty")

    retail_domains = {
        row["id"] for row in product["product_domains"]
        if row.get("authority") == "retail"
    }
    complete = next(
        row for row in product["claim_profiles"]
        if row["id"] == "rotwk-201-complete"
    )
    if set(complete.get("required_domains", [])) != retail_domains:
        raise ValueError("complete claim must require every retail domain")
    if "deferred" not in complete.get("blocking_rule", ""):
        raise ValueError("complete claim must be blocked by deferred retail domains")
    for claim in product["claim_profiles"]:
        required_domains = claim.get("required_domains")
        if not isinstance(required_domains, list) or not required_domains:
            raise ValueError(f"claim {claim['id']}: required domains must not be empty")
        unknown_domains = set(required_domains) - domains
        if unknown_domains:
            raise ValueError(
                f"claim {claim['id']}: unknown domains={sorted(unknown_domains)}"
            )

    mod_ref = next(
        row["policy_contract"] for row in product["product_domains"]
        if row["id"] == "openbfme-modern-modding"
    )
    if (
        mod_ref.get("contract_id") != modding.get("contract_id")
        or mod_ref.get("schema_version") != modding.get("schema_version")
        or mod_ref.get("policy_digest") != modding_digest
    ):
        raise ValueError("product contract: modding policy binding mismatch")

    assignment_values = set(
        product["classification_policy"].get("assignment_classification_values", [])
    )
    if assignment_values != {
        "scalar", "reference", "collection-reference", "opaque-unresolved"
    }:
        raise ValueError("product contract: assignment taxonomy is incomplete")

    if product["generated_outputs"].get("claim_status") != (
        "blocked-until-generator-validator-and-current-reports-exist"
    ):
        raise ValueError("product contract: completeness claim is not fail-closed")

    required_manifest = set(modding["manifest"].get("required_fields", []))
    for field in {
        "conflicts", "provenance", "license", "redistribution_policy", "code_plugins"
    }:
        if field not in required_manifest:
            raise ValueError(f"modding contract: manifest omits {field}")
    if "mixed-map" not in modding["manifest"].get("category_values", []):
        raise ValueError("modding contract: mixed-map category is missing")
    if modding["server_policy"].get("code_plugins") != "forbidden in modding v1":
        raise ValueError("modding contract: executable plugins are not forbidden")

    lock_fields = set(
        modding["compatibility_model"]["simulation_lockfile"]["required_fields"]
    )
    required_lock_fields = {
        "simulation_protocol_revision", "simulation_schema_revision",
        "canonicalizer_revision", "resolved_total_order", "packs",
        "deterministic_configuration", "map_simulation_digest",
        "authoritative_plugins",
    }
    if lock_fields != required_lock_fields:
        raise ValueError("modding contract: compatibility lockfile preimage is incomplete")

    categories = modding.get("pack_categories")
    if not isinstance(categories, dict) or set(categories) != {
        "simulation", "presentation", "map"
    }:
        raise ValueError("modding contract: pack categories are incomplete")
    for category_name in ("simulation", "presentation"):
        category = categories[category_name]
        for field in ("may_define", "must_not_define"):
            values = category.get(field)
            if not isinstance(values, list) or not values:
                raise ValueError(
                    f"modding contract: {category_name}.{field} must not be empty"
                )
    for field in ("simulation_portion", "presentation_portion"):
        values = categories["map"].get(field)
        if not isinstance(values, list) or not values:
            raise ValueError(f"modding contract: map.{field} must not be empty")

    safety_limits = set(modding["resource_safety"].get("limits_required", []))
    required_safety_limits = {
        "archive_bytes", "expanded_bytes", "entry_count", "per_entry_bytes",
        "compression_ratio", "nesting_depth",
    }
    if safety_limits != required_safety_limits:
        raise ValueError("modding contract: resource safety limits are incomplete")
    for field in ("path_rule", "executable_rule", "acquisition"):
        if not modding["resource_safety"].get(field):
            raise ValueError(f"modding contract: resource safety omits {field}")

    if modding["server_policy"].get("authoritative_inputs") != (
        "simulation content and the simulation portion of mixed maps"
    ):
        raise ValueError("modding contract: server authoritative inputs changed")
    for transfer in ("presentation_payload_transfer", "retail_payload_transfer"):
        if modding["server_policy"].get(transfer) != "forbidden":
            raise ValueError(f"modding contract: {transfer} must be forbidden")

    return product_digest, modding_digest


def validate() -> tuple[str, str]:
    return validate_documents(_load(PRODUCT_PATH), _load(MODDING_PATH))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="validate without writing")
    parser.parse_args()
    product_digest, modding_digest = validate()
    print(
        "PRODUCT_CONTRACTS PASS "
        f"product_policy_sha256={product_digest} modding_policy_sha256={modding_digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
