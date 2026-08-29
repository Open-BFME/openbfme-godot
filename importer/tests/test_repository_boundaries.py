from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
CHECKER_PATH = ROOT / "tools" / "check-repository-boundaries.py"
DESCRIPTION = (
    "Exact clean-room Godot port of The Lord of the Rings: The Battle for "
    "Middle-earth II - The Rise of the Witch-king, Patch 2.02 v9.7.7"
)


def _load_checker():
    spec = importlib.util.spec_from_file_location("check_repository_boundaries", CHECKER_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def _fixture(tmp_path: Path, monkeypatch):
    checker = _load_checker()
    root = tmp_path
    tracked = [
        "AGENTS.md",
        "DIRECTION.md",
        "PLAN.md",
        "README.md",
        "config/repository-boundaries.json",
        "contracts/rotwk-202-v9.7.7-baseline.json",
        "contracts/rotwk-202-v9.7.7-product-scope.json",
        "game/data/snapshot.json",
        "game/icon.png",
        "game/project.godot",
        "game/scenes/startup.tscn",
        "game/src/main.gd",
        "game/src/next.gd",
        "game/tests/main_runner.gd",
        "orchestration/work-items.json",
        "run_game.bat",
        "tools/helper.py",
    ]
    for relative in tracked:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
    for relative in ("AGENTS.md", "DIRECTION.md", "PLAN.md", "README.md"):
        (root / relative).write_text(relative + "\n", encoding="utf-8")
    (root / "run_game.bat").write_text(
        'set "OLD_ROUTE=workspace"\n'
        '"%OPENBFME_GODOT%" --path "%ROOT%game"\n',
        encoding="utf-8",
    )
    (root / "tools/helper.py").write_text(
        '"""res://data/snapshot.json is only a Python docstring."""\n'
        "# game/data/snapshot.json is only a Python comment.\n",
        encoding="utf-8",
    )
    (root / "game/src/main.gd").write_text(
        'const SNAPSHOT := "res://data/snapshot.json"\n'
        'const NEXT := "res://src/next.gd"\n'
        + "pass\n" * 998,
        encoding="utf-8",
    )
    (root / "game/src/next.gd").write_text("extends Node\n", encoding="utf-8")
    (root / "game/scenes/startup.tscn").write_text(
        '[gd_scene load_steps=2 format=3]\n\n'
        '[ext_resource path="res://src/main.gd" type="Script" id="1"]\n\n'
        '[node name="Startup" type="Node"]\n'
        'script = ExtResource("1")\n',
        encoding="utf-8",
    )
    (root / "game/tests/main_runner.gd").write_text("pass\n", encoding="utf-8")
    (root / "game/icon.png").write_bytes(b"fixture")
    (root / "game/project.godot").write_text(
        '[application]\n\n'
        'config/name="OpenBFME"\n'
        f'config/description="{DESCRIPTION}"\n'
        'run/main_scene="res://scenes/startup.tscn"\n'
        'config/icon="res://icon.png"\n\n'
        '[autoload]\n\nMain="*res://src/main.gd"\n',
        encoding="utf-8",
    )
    _write_json(
        root / "contracts/rotwk-202-v9.7.7-product-scope.json",
        {"contract_id": "openbfme.rotwk-202-v9.7.7.product-scope"},
    )
    _write_json(
        root / "contracts/rotwk-202-v9.7.7-baseline.json",
        {"baselineId": "rotwk-202-v9.7.7-en"},
    )
    ledger = {
        "evidenceSources": [
            {
                "id": "E-SCOPE",
                "type": "tracked-contract",
                "path": "contracts/rotwk-202-v9.7.7-product-scope.json",
            },
            {
                "id": "E-OLD",
                "type": "sanitized-red-baseline",
                "staleArtifacts": [{"path": "game/data/snapshot.json"}],
            },
            {
                "id": "E-RECEIPT",
                "type": "sanitized-read-only-audit-receipt",
                "inputEvidence": ["E-SCOPE"],
            },
        ],
        "workItems": [
            {
                "id": "P0-ASSET-HYGIENE-001",
                "status": "blocked",
                "sourceEvidence": ["E-SCOPE"],
                "acceptance": {"requiredLevels": ["L0"]},
                "ownership": {"candidatePaths": ["game/icon.png"]},
            },
            {
                "id": "P0-DEFAULTS-001",
                "status": "blocked",
                "sourceEvidence": ["E-OLD"],
                "acceptance": {"requiredLevels": ["L0"]},
                "ownership": {"candidatePaths": ["run_game.bat"]},
            },
            {
                "id": "P1-SIM-001",
                "status": "blocked",
                "sourceEvidence": ["E-OLD"],
                "acceptance": {"requiredLevels": ["L1"]},
                "ownership": {
                    "candidatePaths": ["game/src/main.gd", "game/data/snapshot.json"]
                },
            },
        ],
    }
    _write_json(root / "orchestration/work-items.json", ledger)
    classes = [
        "automation-config",
        "canonical-product-contract",
        "developer-entrypoint",
        "documentation",
        "historical-derived-evidence",
        "live-work-ledger",
        "public-asset-debt",
        "repository-policy",
        "shipping-code",
        "test-code",
        "tool-source",
    ]
    manifest = {
        "schema": "openbfme.repository-boundaries",
        "schemaVersion": 2,
        "authority": {
            "derivedFrom": [
                "AGENTS.md",
                "contracts/rotwk-202-v9.7.7-product-scope.json",
                "contracts/rotwk-202-v9.7.7-baseline.json",
                "DIRECTION.md",
                "PLAN.md",
                "orchestration/work-items.json",
            ],
            "rule": checker.AUTHORITY_RULE,
        },
        "target": {
            "productScopeContract": "openbfme.rotwk-202-v9.7.7.product-scope",
            "retailBaselineId": "rotwk-202-v9.7.7-en",
            "engine": "Godot 4.7",
        },
        "classDefinitions": [
            {"id": value, "tracked": True, "currentEvidenceEligible": value == "canonical-product-contract"}
            for value in classes
        ],
        "pathRules": [
            {"id": "policy", "class": "repository-policy", "roots": ["AGENTS.md"]},
            {"id": "docs", "class": "documentation", "roots": ["README.md"]},
            {"id": "config", "class": "automation-config", "roots": ["config/"]},
            {"id": "canonical", "class": "canonical-product-contract", "roots": ["DIRECTION.md", "PLAN.md", "contracts/"]},
            {
                "id": "shipping",
                "class": "shipping-code",
                "roots": ["game/project.godot", "game/scenes/", "game/src/"],
            },
            {"id": "tests", "class": "test-code", "roots": ["game/tests/"]},
            {"id": "history", "class": "historical-derived-evidence", "roots": ["game/data/snapshot.json"]},
            {"id": "asset-debt", "class": "public-asset-debt", "roots": ["game/icon.png"]},
            {"id": "ledger", "class": "live-work-ledger", "roots": ["orchestration/work-items.json"]},
            {"id": "entrypoint", "class": "developer-entrypoint", "roots": ["run_game.bat"]},
            {"id": "tools", "class": "tool-source", "roots": ["tools/"]},
        ],
        "runtimeAuthority": {
            "project": "game/project.godot",
            "name": "OpenBFME",
            "description": DESCRIPTION,
            "mainScene": "res://scenes/startup.tscn",
            "shippingChain": [
                {"path": "run_game.bat", "role": "launch"},
                {"path": "game/project.godot", "role": "project"},
                {"path": "game/scenes/startup.tscn", "role": "startup-scene"},
                {"path": "game/src/main.gd", "role": "simulation"},
                {"path": "game/src/next.gd", "role": "simulation-dependency"},
            ],
            "links": [
                {
                    "from": "run_game.bat",
                    "to": "game/project.godot",
                    "token": '--path "%ROOT%game"',
                },
                {
                    "from": "game/project.godot",
                    "to": "game/scenes/startup.tscn",
                    "token": "res://scenes/startup.tscn",
                },
                {
                    "from": "game/scenes/startup.tscn",
                    "to": "game/src/main.gd",
                    "token": "res://src/main.gd",
                },
                {
                    "from": "game/src/main.gd",
                    "to": "game/src/next.gd",
                    "token": "res://src/next.gd",
                },
            ],
            "autoloads": [{"name": "Main", "path": "game/src/main.gd"}],
            "startupAssetDebt": [
                {
                    "projectKey": "config/icon",
                    "resource": "res://icon.png",
                    "ownerWorkItem": "P0-ASSET-HYGIENE-001",
                    "reason": "fixture debt",
                }
            ],
            "nonShipping": [],
        },
        "generatedBoundaries": [
            {"path": "workspace/", "tracked": False, "kind": "private output"}
        ],
        "generatedArtifacts": [
            {
                "path": "game/data/snapshot.json",
                "ownerWorkItem": "P1-SIM-001",
                "replacement": "regenerate",
                "referenceTokens": [
                    "game/data/snapshot.json",
                    "res://data/snapshot.json",
                ],
                "operationalConsumers": [
                    {"path": "game/src/main.gd", "role": "shipping-runtime"},
                ],
            }
        ],
        "evidencePolicy": {
            "currentTypes": ["tracked-contract", "sanitized-read-only-audit-receipt"],
            "historicalDiagnosticTypes": ["sanitized-red-baseline"],
            "privatePrefix": "workspace/",
        },
        "runtimeDebt": [
            {
                "id": "old-route",
                "path": "run_game.bat",
                "tokens": ["OLD_ROUTE"],
                "ownerWorkItem": "P0-DEFAULTS-001",
                "disposition": "remove",
            }
        ],
        "highConflict": {
            "thresholdLines": 1000,
            "extensions": [".gd"],
            "files": [
                {
                    "path": "game/src/main.gd",
                    "ownerWorkItem": "P1-SIM-001",
                    "seams": ["commands", "ticks"],
                    "focusedTests": ["game/tests/main_runner.gd"],
                }
            ],
        },
        "assetNaming": {
            "retailSourceKeys": "verbatim",
            "convertedPrivatePayloads": "content addressed",
            "publicAuthoredAssets": "attested",
            "aliases": "manifest only",
            "currentDebtRoots": ["game/icon.png"],
            "ownerWorkItem": "P0-ASSET-HYGIENE-001",
        },
    }
    manifest_path = root / "config/repository-boundaries.json"
    _write_json(manifest_path, manifest)
    monkeypatch.setattr(checker, "_tracked_files", lambda _root: sorted(tracked))
    monkeypatch.setattr(checker, "_git", lambda _root, *_args: "fixture-revision\n")
    return checker, root, tracked, ledger, manifest, manifest_path


def test_tracked_repository_boundary_contract_passes(tmp_path: Path) -> None:
    completed = subprocess.run(
        [sys.executable, str(CHECKER_PATH), "--check", "--output", str(tmp_path / "receipt.json")],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    assert completed.returncode == 0, completed.stderr
    assert completed.stderr == ""
    assert completed.stdout.startswith("REPOSITORY_BOUNDARIES PASS tracked=")


def test_minimal_boundary_contract_passes(tmp_path: Path, monkeypatch) -> None:
    checker, root, _tracked, _ledger, _manifest, path = _fixture(tmp_path, monkeypatch)
    errors, receipt = checker.validate(root, path)
    assert errors == []
    assert receipt["status"] == "pass"
    assert receipt["counts"]["classifiedFiles"] == receipt["counts"]["trackedFiles"]
    (root / "game/tests/main_runner.gd").write_text(
        "# snapshot.json is only prose, not a consumer\n", encoding="utf-8"
    )
    errors, _receipt = checker.validate(root, path)
    assert errors == []
    assert checker._source_literals(
        "helper.py",
        '"""res://data/snapshot.json is only a Python docstring."""\n'
        "# game/data/snapshot.json is only a Python comment.\n",
    ) == set()


def test_boundary_mutations_fail_closed(tmp_path: Path, monkeypatch) -> None:
    def reject(case: str, fragment: str, mutate) -> None:
        checker, root, tracked, ledger, manifest, path = _fixture(tmp_path / case, monkeypatch)
        mutate(root, tracked, ledger, manifest)
        _write_json(root / "orchestration/work-items.json", ledger)
        _write_json(path, manifest)
        errors, receipt = checker.validate(root, path)
        assert any(fragment in error for error in errors), errors
        assert receipt["status"] == "fail"

    reject(
        "undefined-class",
        "uses undefined class",
        lambda _root, _tracked, _ledger, manifest: manifest["classDefinitions"].__setitem__(
            slice(None),
            [row for row in manifest["classDefinitions"] if row["id"] != "historical-derived-evidence"],
        ),
    )
    reject(
        "overlap",
        "overlapping classes",
        lambda _root, _tracked, _ledger, manifest: manifest["pathRules"].append(
            {"id": "second-shipping", "class": "shipping-code", "roots": ["game/src/"]}
        ),
    )
    reject(
        "evidence-indirection",
        "points at ineligible class",
        lambda _root, _tracked, ledger, _manifest: ledger["evidenceSources"][0].__setitem__(
            "path", "game/data/snapshot.json"
        ),
    )
    reject(
        "missing-evidence-root",
        "has no classified source root",
        lambda _root, _tracked, ledger, _manifest: ledger["evidenceSources"][0].pop(
            "path"
        ),
    )
    reject(
        "duplicate-evidence-id",
        "duplicate evidence-source ids",
        lambda _root, _tracked, ledger, _manifest: ledger["evidenceSources"].append(
            copy.deepcopy(ledger["evidenceSources"][0])
        ),
    )
    reject(
        "workspace-current-root",
        "cannot root itself in private workspace",
        lambda _root, _tracked, ledger, _manifest: (
            ledger["evidenceSources"][0].pop("path"),
            ledger["evidenceSources"][0].__setitem__(
                "sourcePointers", ["workspace/private/current.json"]
            ),
        ),
    )
    reject(
        "historical-root",
        "has no classified source root",
        lambda _root, _tracked, ledger, _manifest: ledger["evidenceSources"][2].__setitem__(
            "inputEvidence", ["E-OLD"]
        ),
    )
    reject(
        "historical-completion",
        "completed non-L0 work item",
        lambda _root, _tracked, ledger, _manifest: ledger["workItems"][2].__setitem__(
            "status", "complete"
        ),
    )
    reject(
        "wildcard-test",
        "non-exact test route",
        lambda _root, _tracked, _ledger, manifest: manifest["highConflict"]["files"][0].__setitem__(
            "focusedTests", ["game/tests/*_runner.gd"]
        ),
    )
    reject(
        "dual-owner",
        "invalid single owner",
        lambda _root, _tracked, _ledger, manifest: manifest["highConflict"]["files"][0].__setitem__(
            "ownerWorkItem", ["P1-SIM-001", "P0-DEFAULTS-001"]
        ),
    )
    reject(
        "assigned-collision",
        "collides with assigned lanes",
        lambda _root, _tracked, ledger, _manifest: ledger["workItems"].append(
            {
                "id": "P0-COLLISION-001",
                "status": "in-progress",
                "sourceEvidence": [],
                "acceptance": {"requiredLevels": ["L0"]},
                "ownership": {
                    "state": "assigned",
                    "ownedPaths": ["game/src/main.gd"],
                    "candidatePaths": ["game/src/main.gd"],
                },
            }
        ),
    )
    reject(
        "directory-candidate",
        "lacks an exact candidate path",
        lambda _root, _tracked, ledger, _manifest: ledger["workItems"][2][
            "ownership"
        ].__setitem__("candidatePaths", ["game/src/"]),
    )
    reject(
        "authority-disclaimer-drift",
        "canonical-authority disclaimer drifted",
        lambda _root, _tracked, _ledger, manifest: manifest["authority"].__setitem__(
            "rule",
            "This is an enforced ownership map derived from canonical state, not a "
            "competing product or completion authority. Extra prose is not accepted.",
        ),
    )
    reject(
        "generated-directory-candidate-only",
        "generated artifact game/data/snapshot.json lacks an exact candidate path",
        lambda _root, _tracked, ledger, _manifest: ledger["workItems"][2][
            "ownership"
        ].__setitem__("candidatePaths", ["game/src/main.gd", "game/data/"]),
    )
    reject(
        "startup-directory-candidate",
        "startup asset game/icon.png lacks an exact candidate path",
        lambda _root, _tracked, ledger, _manifest: ledger["workItems"][0][
            "ownership"
        ].__setitem__("candidatePaths", ["game/"]),
    )
    reject(
        "missing-chain-edge",
        "links are not complete adjacent edges",
        lambda _root, _tracked, _ledger, manifest: manifest["runtimeAuthority"][
            "links"
        ].pop(),
    )
    reject(
        "comment-only-chain-edge",
        "shipping-chain link is not executable",
        lambda root, _tracked, _ledger, _manifest: (root / "run_game.bat").write_text(
            'set "OLD_ROUTE=workspace"\nREM --path "%ROOT%game"\n', encoding="utf-8"
        ),
    )
    reject(
        "echo-only-chain-edge",
        "shipping-chain link is not executable",
        lambda root, _tracked, _ledger, _manifest: (root / "run_game.bat").write_text(
            'set "OLD_ROUTE=workspace"\necho --path "%ROOT%game"\n', encoding="utf-8"
        ),
    )
    reject(
        "project-comment-chain-edge",
        "shipping-chain link is not executable",
        lambda root, _tracked, _ledger, _manifest: (root / "game/project.godot").write_text(
            (root / "game/project.godot").read_text(encoding="utf-8").replace(
                'run/main_scene="res://scenes/startup.tscn"',
                'run/main_scene="res://scenes/other.tscn"\n; "res://scenes/startup.tscn"',
            ),
            encoding="utf-8",
        ),
    )
    reject(
        "scene-comment-chain-edge",
        "shipping-chain link is not executable",
        lambda root, _tracked, _ledger, _manifest: (
            root / "game/scenes/startup.tscn"
        ).write_text(
            (root / "game/scenes/startup.tscn").read_text(encoding="utf-8").replace(
                '[ext_resource path="res://src/main.gd" type="Script" id="1"]',
                '; "res://src/main.gd"\n'
                '[ext_resource path="res://src/other.gd" type="Script" id="1"]',
            ),
            encoding="utf-8",
        ),
    )
    reject(
        "gdscript-comment-chain-edge",
        "shipping-chain link is not executable",
        lambda root, _tracked, _ledger, _manifest: (root / "game/src/main.gd").write_text(
            (root / "game/src/main.gd").read_text(encoding="utf-8").replace(
                'const NEXT := "res://src/next.gd"',
                'const NEXT := "res://src/other.gd"\n# "res://src/next.gd"',
            ),
            encoding="utf-8",
        ),
    )
    reject(
        "comment-only-runtime-debt",
        "runtime debt tokens drifted",
        lambda root, _tracked, _ledger, _manifest: (root / "run_game.bat").write_text(
            'REM set "OLD_ROUTE=workspace"\n'
            '"%OPENBFME_GODOT%" --path "%ROOT%game"\n',
            encoding="utf-8",
        ),
    )
    reject(
        "autoload-drift",
        "Godot autoload map mismatch",
        lambda root, _tracked, _ledger, _manifest: (root / "game/project.godot").write_text(
            (root / "game/project.godot").read_text(encoding="utf-8").replace(
                'Main="*res://src/main.gd"', 'Other="*res://src/main.gd"'
            ),
            encoding="utf-8",
        ),
    )
    reject(
        "consumer-drift",
        "consumer mismatch",
        lambda root, _tracked, _ledger, _manifest: (root / "game/tests/main_runner.gd").write_text(
            'const UNDECLARED := "res://data/snapshot.json"\n', encoding="utf-8"
        ),
    )
    reject(
        "echo-only-runtime-debt",
        "runtime debt tokens drifted",
        lambda root, _tracked, _ledger, _manifest: (root / "run_game.bat").write_text(
            'echo set "OLD_ROUTE=workspace"\n'
            '"%OPENBFME_GODOT%" --path "%ROOT%game"\n',
            encoding="utf-8",
        ),
    )
    reject(
        "python-composed-path-consumer",
        "consumer mismatch",
        lambda root, _tracked, _ledger, _manifest: (root / "tools/helper.py").write_text(
            "from pathlib import Path\n"
            'SNAPSHOT = Path("res://data") / "snapshot.json"\n',
            encoding="utf-8",
        ),
    )
    reject(
        "python-multi-argument-path-consumer",
        "consumer mismatch",
        lambda root, _tracked, _ledger, _manifest: (root / "tools/helper.py").write_text(
            "from pathlib import Path\n"
            'SNAPSHOT = Path("res://data", "snapshot.json")\n',
            encoding="utf-8",
        ),
    )
    reject(
        "python-res-root-path-consumer",
        "consumer mismatch",
        lambda root, _tracked, _ledger, _manifest: (root / "tools/helper.py").write_text(
            "from pathlib import Path\n"
            'SNAPSHOT = Path("res://") / "data/snapshot.json"\n',
            encoding="utf-8",
        ),
    )
    reject(
        "python-res-root-multi-argument-path-consumer",
        "consumer mismatch",
        lambda root, _tracked, _ledger, _manifest: (root / "tools/helper.py").write_text(
            "from pathlib import Path\n"
            'SNAPSHOT = Path("res://", "data/snapshot.json")\n',
            encoding="utf-8",
        ),
    )
    reject(
        "python-path-alias-consumer",
        "consumer mismatch",
        lambda root, _tracked, _ledger, _manifest: (root / "tools/helper.py").write_text(
            "from pathlib import PurePosixPath as P\n"
            'SNAPSHOT = P("res://") / "data" / "snapshot.json"\n',
            encoding="utf-8",
        ),
    )
    reject(
        "python-os-path-join-consumer",
        "consumer mismatch",
        lambda root, _tracked, _ledger, _manifest: (root / "tools/helper.py").write_text(
            "import os\n"
            'SNAPSHOT = os.path.join("res://data", "snapshot.json")\n',
            encoding="utf-8",
        ),
    )
    reject(
        "python-joinpath-consumer",
        "consumer mismatch",
        lambda root, _tracked, _ledger, _manifest: (root / "tools/helper.py").write_text(
            "from pathlib import Path\n"
            'SNAPSHOT = Path("res://").joinpath("data", "snapshot.json")\n',
            encoding="utf-8",
        ),
    )
    reject(
        "python-concatenated-path-consumer",
        "consumer mismatch",
        lambda root, _tracked, _ledger, _manifest: (root / "tools/helper.py").write_text(
            'SNAPSHOT = "res://data/" + "snapshot.json"\n',
            encoding="utf-8",
        ),
    )
    reject(
        "missing-tracked-executable-source",
        "cannot read tracked executable text: tools/helper.py",
        lambda root, _tracked, _ledger, _manifest: (root / "tools/helper.py").unlink(),
    )
    reject(
        "unparsable-tracked-python-source",
        "cannot parse tracked Python executable text: tools/helper.py",
        lambda root, _tracked, _ledger, _manifest: (root / "tools/helper.py").write_text(
            'SNAPSHOT = Path("res://data") /\n', encoding="utf-8"
        ),
    )
    reject(
        "bare-reference-token",
        "invalid reference tokens",
        lambda _root, _tracked, _ledger, manifest: manifest["generatedArtifacts"][0][
            "referenceTokens"
        ].append("snapshot.json"),
    )
    reject(
        "asset-debt-drift",
        "asset naming debt roots disagree",
        lambda _root, _tracked, _ledger, manifest: manifest["assetNaming"].__setitem__(
            "currentDebtRoots", []
        ),
    )
    reject(
        "unclassified-path",
        "tracked path has no class",
        lambda root, tracked, _ledger, _manifest: (
            (root / "mystery.txt").write_text("unowned\n", encoding="utf-8"),
            tracked.append("mystery.txt"),
        ),
    )
    reject(
        "approximate-target",
        "Godot project setting config/description",
        lambda root, _tracked, _ledger, _manifest: (root / "game/project.godot").write_text(
            (root / "game/project.godot").read_text(encoding="utf-8").replace(
                DESCRIPTION, "inspired RTS"
            ),
            encoding="utf-8",
        ),
    )
