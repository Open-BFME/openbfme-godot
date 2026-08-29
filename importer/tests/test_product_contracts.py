from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools" / "check-product-contracts.py"


def _checker_module():
    spec = importlib.util.spec_from_file_location("openbfme_contract_checker", CHECKER)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _documents():
    product = json.loads(
        (ROOT / "contracts" / "rotwk-202-v9.7.7-product-scope.json").read_text(
            encoding="utf-8"
        )
    )
    modding = json.loads(
        (ROOT / "contracts" / "openbfme-modding-contract.json").read_text(
            encoding="utf-8"
        )
    )
    return product, modding


def test_tracked_product_contracts_are_bound_and_fail_closed() -> None:
    result = subprocess.run(
        [sys.executable, str(CHECKER), "--check"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout.startswith("PRODUCT_CONTRACTS PASS ")


def test_contract_checker_rejects_validly_redigested_nested_policy_deletions() -> None:
    checker = _checker_module()
    product, modding = _documents()

    mutations = []

    no_skirmish_evidence = copy.deepcopy(product)
    next(
        row for row in no_skirmish_evidence["product_domains"]
        if row["id"] == "retail-skirmish"
    )["required_evidence_lanes"] = []
    mutations.append((no_skirmish_evidence, copy.deepcopy(modding)))

    no_visual_fields = copy.deepcopy(product)
    next(
        row for row in no_visual_fields["evidence_lanes"]
        if row["id"] == "visual-oracle"
    )["required_fields"] = []
    mutations.append((no_visual_fields, copy.deepcopy(modding)))

    no_presentation_category = copy.deepcopy(modding)
    del no_presentation_category["pack_categories"]["presentation"]
    mutations.append((copy.deepcopy(product), no_presentation_category))

    no_resource_limits = copy.deepcopy(modding)
    no_resource_limits["resource_safety"]["limits_required"] = []
    mutations.append((copy.deepcopy(product), no_resource_limits))

    for mutated_product, mutated_modding in mutations:
        mutated_modding["policy_digest"]["value"] = checker._policy_digest(
            mutated_modding
        )
        mod_ref = next(
            row["policy_contract"] for row in mutated_product["product_domains"]
            if row["id"] == "openbfme-modern-modding"
        )
        mod_ref["policy_digest"] = mutated_modding["policy_digest"]["value"]
        mutated_product["policy_digest"]["value"] = checker._policy_digest(
            mutated_product
        )
        with pytest.raises(ValueError):
            checker.validate_documents(mutated_product, mutated_modding)
