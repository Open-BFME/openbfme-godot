# Importer agent rules

This subtree owns retail discovery, extraction, conversion, provenance, and
private pack construction. Follow the root `AGENTS.md` and
`docs/CONTENT_PIPELINE.md`.

- Retail inputs, caches, converted outputs, reports, and logs stay below the
  assigned `.private/scratch/jobs/<task-id>` or canonical owner-only private root.
- Workers must not publish selection, mutate the canonical completion profile,
  modify pinned tool trees, or reuse another task's job root.
- Keep converter changes deterministic, resumable, provenance-complete, and
  fail-closed. Do not rewrite the importer for style.
- Do not copy donor in-memory types into the OpenBFME pack schema.
- Use the packet's focused pytest selection. Do not run the full retail pipeline
  or canonical private build as a worker.
- A profile, source, schema, tool, or recipe change invalidates dependent cache
  evidence unless the existing cache identity proves otherwise.
