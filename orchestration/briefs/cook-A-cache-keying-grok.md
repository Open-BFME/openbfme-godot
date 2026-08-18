# Cook speed A — stop output-neutral tool edits from evicting the conversion caches (Grok)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md (rules 1-10). Claim
row Q24 in orchestration/queue.md (owner=grok-cook-A). Standard lane rules:
explicit-path git, no sweeps, logs under workspace/logs/, sequential pytest.
DO NOT run this lane while a cook is in progress (check `tasklist` for
python/blender from tools/rotwk_full_content.py; check queue Q14 status).
No pack builds/selection changes.

## Diagnosis (measured 2026-08-17, orchestrator deep-dive)
- W3D→GLB cache key (importer/openbfme_importer/pipeline.py:7132
  `_w3d_conversion_cache_key`, adapter identity built at ~7118-7130) hashes the
  SOURCE BYTES of importer/blender/w3d_to_glb.py + w3d_multi_to_glb.py. Commit
  21b357a (log-capture cap 1→2 MiB, zero output-byte effect) evicted the entire
  cache: workspace/retail-work/cache/converted = 7.95 GB / 11,382 files, of
  which 1,331 re-minted today and 4,258 yesterday (a w3d_to_glb.py edit on
  08-16 did the same). Blender is 30-170 s per model → this is the "40 min per
  faction" recook cost.
- Object cache (importer/openbfme_importer/faction_object_cache.py:26
  `_COMPILER_SALT_MODULES`, ~20 modules) hashes compiler source bytes → 0 hits
  in 110 objects across runs. Its own work is seconds; it was slow only because
  it waited on Blender above.
- Archive attestation (pipeline.py:4683 `_attest_source_archives`) re-hashes
  5.64 GB / 213 `.big` files per invocation with an in-memory-only cache
  (`_archive_attest_cache`), ~10-15 invocations per full cook.
- Blender tree attest (bootstrap.py `_attest_blender_portable_tree` +
  `_purge_python_caches`) walks/hashes 0.97 GB / 5,518 files per run.
- Nobody records publish stage timings; publication receipts have none.

## Deliverable
1. **W3D cache: semantic version, not source bytes.** Add
   `W3D_ADAPTER_OUTPUT_VERSION = "<int>"` constants to importer/blender/
   w3d_to_glb.py and w3d_multi_to_glb.py (one shared constant if the multi
   adapter imports the single one — follow the actual structure). Replace the
   `adapter_bundle_sha256` component of the cache identity with that version
   (keep blender_tree_sha256, plugin attestation, source_hashes, logical as
   they are). Keep provenance recording the real adapter file sha256 for audit
   — it just no longer keys the cache.
   Guard (failing-first): `importer/tests/test_w3d_adapter_output_version.py`
   pins (a) the current adapter file sha256s alongside the version, and (b) a
   golden GLB byte-sha256 for 2-3 tiny retail W3D models already used by
   existing tests (find them: grep importer/tests for a fixture w3d / the
   multi-job tests). The test FAILS if an adapter file's sha256 changed AND
   (the version was not bumped AND golden bytes changed) → message "bump
   W3D_ADAPTER_OUTPUT_VERSION". If bytes are identical, it PASSES and prints
   "adapter edit is output-neutral; cache preserved". If Blender is unavailable
   in the test env, the golden check must SKIP loudly, never pass silently.
2. **Object cache: same principle.** Replace `_compiler_source_salt()` source
   hashing with an explicit `FACTION_OBJECT_CACHE_COMPILER_VERSION` in
   faction_object_cache.py plus a pinned-sha test of the salted modules that
   fails with "bump the version or confirm output-neutral" — but ONLY after
   measuring: run a Men convert twice with the new key and report the hit
   rate; if descriptor recompute is < 60 s per faction with a warm W3D cache,
   you may instead simply leave the object cache as-is and document that it
   is cheap now (report either way with numbers).
3. **Persist archive attestation**: cache `(path, size, mtime_ns) → sha256`
   in a JSON under workspace/retail-work/cache/ (state-root owned, atomic
   write, schema+version field); on hit skip the read. Attestation stays
   exact — a stat change re-hashes. Same for the Blender tree: cache the
   directory-tree digest keyed by a stat-signature of the tree; a mismatch
   re-hashes. Both keyed caches are invalidated by their own schema version.
4. **Instrument**: publish-faction-to-slice and pipeline.build emit stage
   wall-times (extract/attest, convert-assets w3d/media with hit/miss counts,
   bundle loop, hashing, audit, digest, publish copies) into the publication
   receipt and the progress log. rotwk_full_content.py prints a per-faction
   summary table at the end.
5. **Launch hygiene**: run_rotwk_full_content.bat / tools/rotwk_full_content.py
   document (and, if simple, implement via a `-Detached` switch) launching
   through a Scheduled Task or `Start-Process` outside the caller's job object,
   because a dying launch shell reaped the cook twice on 2026-08-17.

## Definition of Done (verbatim outputs)
1. New tests green; FULL importer suite (sequential) → exactly the 6 Q6 names,
   0 errors (log workspace/logs/cook-A-importer-full.txt).
2. Proof of the fix: make a whitespace/comment-only edit to
   w3d_multi_to_glb.py in a scratch commit (or `git stash`-free equivalent —
   edit, measure, revert by re-editing), run `import-faction --faction men
   --convert` twice on the pinned interpreter: report W3D cache
   hits/misses/forced both runs (must be all hits, zero Blender launches — check
   `tasklist` for blender during the run) and wall time. Then revert the edit
   by targeted edit (not git restore).
3. Archive attest: second invocation reads 0 bytes of `.big` (log the cache
   hit count) and stage timing shows the extract/attest stage < 5 s warm.
4. check_pack_addresses PASS; gate-hygiene PASS; git status clean; commits
   `perf(importer):` / `test(importer):`, explicit paths.
5. Report orchestration/reports/cook-A-cache-keying.md with before/after
   timings table and the new stage-timing receipt sample.
