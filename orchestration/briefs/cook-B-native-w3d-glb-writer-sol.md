# Cook speed B — native Python W3D→GLB writer, retire Blender from the cook (Sol, multi-lane program)

Repo: C:\Users\Jonathan\Desktop\open-bfme (may be run in a SEPARATE CLONE on the
VM — importer-only Python; needs the retail extract + pinned interpreter +
existing DDC to backtest). Read AGENTS.md (rules 1-10). Claim row Q25 in
orchestration/queue.md (owner=sol-cook-B, phase noted). Standard lane rules.
No pack builds/selection changes in phases 1-2. Blender is LGPL-adjacent
tooling (OpenSAGE.BlenderPlugin): REFERENCE ONLY, never copy code — write from
the W3D format and the repo's own decoders.

## Why (measured 2026-08-17)
The cook's cost is Blender: 30-170 s per model (batch of 8 per process),
~11k models, 7.95 GB cache; a cold full cook is 6-8 h and any adapter edit
re-pays it. The repo already carries ~29k lines of pure-Python W3D chunk
decoders used for validation (w3d_geometry_streams.py, w3d_animation_streams.py,
w3d_auxiliary_streams.py, w3d_emitter_streams.py, w3d_metadata.py,
w3d_prepared_root.py, w3d_secondary_skin.py, w3d_texture_closure.py …) plus GLB
validators (w3d_glb_validation.py, native_backtest.py). Missing: the writer.
A native writer runs in milliseconds per model → full cold cook in ~10-15 min,
deterministic, no Blender attestation, and the pack/seal/pin architecture is
untouched because the OUTPUT (GLB + provenance) stays the same.

## Program shape (each phase its own brief/report/verify; this file is the charter)
**Phase 1 — static meshes (no skin, no animation).** Structures, props,
projectiles. `importer/openbfme_importer/w3d_native_glb.py`: read the W3D
(via existing decoders/`w3d_index.py`), emit GLB 2.0 (JSON + BIN, hand-rolled
or `pygltflib` if already pinned in the venv — check; do not add unpinned
deps): meshes with positions/normals/uv0, triangle indices, per-mesh material
→ texture binding using the same texture-name resolution the Blender path uses
(w3d_texture_closure.py), hierarchy pivots as nodes with the same names the
Blender export produces (this is what the runtime binds to — see game/src for
`ParticleSysBone`, bone-name lookups; names/transforms must match).
Backtest: for a fixed corpus of N static models already in the DDC (pick ≥50
across factions), compare native vs Blender GLB with the EXISTING validators
plus a new structural diff (node names + hierarchy, mesh count, vertex count,
index count, material/texture bindings, bounding boxes within 1e-4). Byte-
identity is NOT the bar (exporters differ); semantic identity per the diff is.
Gate the writer behind `OPENBFME_W3D_WRITER=native|blender` (default blender)
so nothing ships until Phase 3.
**Phase 2 — skinned + animated.** Skeleton (HIERARCHY), vertex influences,
inverse bind matrices, animation channels (ANIMATION / COMPRESSED_ANIMATION
incl. adaptive-delta), the timing/animation-name conventions the runtime
expects (see how retail_battalion.gd / animation_state_select.gd address
clips). Backtest: same structural diff + per-clip sampled joint transforms
vs Blender output at k frames within tolerance; the runtime's animation-state
runners must pass unchanged on packs cooked with native output.
**Phase 3 — flip the default, retire Blender.** OPENBFME_W3D_WRITER default
native; Blender path kept for one release as `--writer blender` fallback;
cache key adds `writer=native/vN`; remove Blender tree/plugin attestation
from the default path (keep for fallback); a full 7-faction cook, timed;
recook checklist fields verified; then a normal Q14-style select + dist.

## Definition of Done — Phase 1 (verbatim outputs)
1. `pytest importer/tests/test_w3d_native_glb.py` green: unit tests on 3 tiny
   models + the ≥50-model backtest (structural diff all PASS; list any
   tolerated deltas by name); FULL importer suite (sequential) → exactly the
   6 Q6 names, 0 errors.
2. Timing table: native ms/model vs Blender s/model on the same corpus.
3. Determinism: converting the corpus twice yields byte-identical GLBs.
4. Runtime smoke: cook ONE structure-heavy pack (e.g. Men) with
   OPENBFME_W3D_WRITER=native into a staging dir (no publish), run
   playable_structure_runtime_consumer_runner / fortress surface men runner /
   retail_spellbook_runner against it (OPENBFME_CONTENT pointed at staging) —
   same pass counts as the Blender-cooked pack; a headless boot shows models
   render (structure count, no missing-mesh diagnostics).
5. gate-hygiene PASS; git status clean; commits `feat(importer):`/`test(...)`;
   report orchestration/reports/cook-B-phase1.md; add queue rows for Phase 2/3.
