#!/usr/bin/env python
"""Real-data proof for the composed playable-structure runtime document.

Compiles one retail structure end-to-end from the private effective-assets
cache (descriptor -> visual closure -> pack recipe -> lifecycle evidence ->
runtime document) twice from scratch, proves both passes are byte-identical,
and prints the composed lifecycle summary.  Reads private retail data; writes
nothing.

Usage:
  python tools/prove-structure-runtime.py [ObjectId ...]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "importer"))

from openbfme_importer.playable_structure_compiler import (  # noqa: E402
    compile_playable_structure_descriptor,
)
from openbfme_importer.playable_structure_lifecycle_evidence import (  # noqa: E402
    compile_structure_lifecycle_evidence,
)
from openbfme_importer.playable_structure_pack_compiler import (  # noqa: E402
    compile_structure_visual_recipe,
    compose_structure_runtime_document,
)
from openbfme_importer.playable_unit_compiler import (  # noqa: E402
    prepare_playable_unit_compiler,
)
from openbfme_importer.playable_unit_import import _source_documents  # noqa: E402
from openbfme_importer.retail_visual_closure import (  # noqa: E402
    build_retail_visual_closure,
)

DEFAULT_ROOT = (
    Path(__file__).resolve().parents[1]
    / ".private"
    / "retail-work"
    / "cache"
    / "effective-assets"
)


def _compose(
    object_id: str,
    root: Path,
    *,
    engine_spawned: tuple[str, ...] = (),
    wall_templates: tuple[str, ...] = (),
) -> dict[str, object]:
    documents = _source_documents(root)
    prepared = prepare_playable_unit_compiler(documents)
    descriptor = compile_playable_structure_descriptor(
        object_id,
        documents,
        prepared=prepared,
        engine_spawned_roots=engine_spawned,
        wall_template_roots=wall_templates,
    )
    closure = build_retail_visual_closure(root, [object_id])
    recipe = compile_structure_visual_recipe(object_id, closure)
    evidence = compile_structure_lifecycle_evidence(
        object_id, documents, prepared=prepared
    )
    return compose_structure_runtime_document(descriptor, recipe, evidence)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("objects", nargs="*", default=["GondorBarracks"])
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument(
        "--engine-spawned",
        action="append",
        default=[],
        help="Object id admitted under the engine-spawned-composite policy",
    )
    parser.add_argument(
        "--wall-template",
        action="append",
        default=[],
        help="Object id admitted under the wall-template policy",
    )
    args = parser.parse_args()
    root = args.root.resolve()
    if not root.is_dir():
        print(f"PROOF FAIL effective assets root missing: {root}")
        return 1
    failures = 0
    for object_id in args.objects or ["GondorBarracks"]:
        try:
            first = _compose(
                object_id,
                root,
                engine_spawned=tuple(args.engine_spawned),
                wall_templates=tuple(args.wall_template),
            )
            second = _compose(
                object_id,
                root,
                engine_spawned=tuple(args.engine_spawned),
                wall_templates=tuple(args.wall_template),
            )
        except Exception as exc:  # noqa: BLE001 - proof surface
            print(f"PROOF FAIL {object_id}: {type(exc).__name__}: {exc}")
            failures += 1
            continue
        if first != second:
            print(f"PROOF FAIL {object_id}: composition is not deterministic")
            failures += 1
            continue
        lifecycle = first["registration"]["presentation"]["buildingLifecycle"]
        facts = lifecycle["simulationFacts"]
        summary = {
            "objectId": first["objectId"],
            "runtimeObjectId": lifecycle["objectId"],
            "runtimeSha256": first["runtimeSha256"],
            "phases": [
                {
                    "phase": row["phase"],
                    "visual": row["visual"].get(
                        "glb", row["visual"].get("sourceIdentifier")
                    ),
                    "fallback": row["visual"].get("visualFallback"),
                    "animation": row["animation"],
                }
                for row in lifecycle["phases"]
            ],
            "bib": (lifecycle["bib"] or {}).get("visual", {}).get("glb"),
            "audioEvents": lifecycle["audioEvents"],
            "enteringStateFx": lifecycle["effects"]["enteringStateFx"],
            "collapseUpdateFx": lifecycle["effects"]["collapseUpdateFx"],
            "particleAttachmentCount": len(
                lifecycle["effects"]["particleAttachments"]
            ),
            "simulationFacts": {
                "maximumHealth": facts["maximumHealth"],
                "damageStateRule": facts.get(
                    "damageStateRule", facts.get("damageStateRuleStatus")
                ),
                "construction": facts["construction"],
                "collapseModule": facts["collapse"].get("module"),
                "postRubble": facts["postRubble"],
            },
            "compositionExclusions": lifecycle["compositionExclusions"],
        }
        print(f"PROOF OK {object_id} deterministic composed runtime")
        print(json.dumps(summary, indent=2, sort_keys=True))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
