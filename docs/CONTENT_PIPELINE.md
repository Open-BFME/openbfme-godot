# OpenBFME content pipeline

> **Owner:** Importer and content-pack integration owner
> **Owns:** Retail extraction, conversion, caching, provenance, pack assembly, publication, and runtime-loading contracts.
> **Does not own:** Gameplay semantics, simulation protocol, visual parity approval, mod load-order policy, or public-release approval.
> **Last verified commit:** `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
> **Update trigger:** A source format, converter, pack schema, tool pin, publication rule, or runtime mount contract changes.
> **Validation:** `run_importer_tests.bat`, `run_retail_pack_tests.bat`, and the generated pack provenance/audit reports.

## Contract

The importer is the only component allowed to understand BFME2 retail containers and
source formats. It coordinates Python, Blender and pinned external format tools, then
emits a versioned runtime-native pack. Runtime code must not read the retail install,
BIG archives, W3D files, source maps, importer caches, Blender, OpenSAGE types, or
absolute source paths.

The proven importer is production infrastructure. Preserve its observable behavior,
tests, cache identities, resumability, and performance characteristics. Refactor it
only for a bounded defect or measured bottleneck with equivalent output evidence.

## Containment

All retail inputs and derived retail payloads stay in the ignored private workspace:

```text
.private/retail-work/
  catalog/                 metadata-only effective-view indexes
  cache/sources/           verified extracted source entries
  jobs/                    isolated converter work roots
  tools/                   pinned external tools and tool manifest
  packs/                   transactional build outputs
  profiles/                generated private build profiles
  reports/                 plans, diagnostics, census and provenance

.private/content-packs/
  selection.json           selected immutable private pack
  <pack-id>/<bundle-id>/   self-contained published bundle
```

Retail payload bytes must not enter Git, repository-authored fixtures, logs, support
bundles, screenshots intended for distribution, public exports, or network traffic.
Repository tests use legal-safe synthetic fixtures. Virtual retail filenames and
digests may appear only as non-payload provenance metadata in contained reports.

## Deterministic, resumable flow

The supported operator entry point is:

```bat
run_importer.bat <BFME2>
```

The underlying flow is:

1. Bootstrap or attest the pinned private toolchain.
2. Diagnose the user-owned retail installation without modifying it.
3. Index the archive overlay and compute the effective source view.
4. Census the requested dependency closure.
5. Plan from metadata before extracting payloads.
6. Extract exact source entries into a content-addressed cache.
7. Convert in isolated job roots using bounded inputs and outputs.
8. Assemble into a staging directory.
9. Audit schema, provenance, containment, limits and completeness.
10. Publish atomically to an immutable bundle directory.
11. Update `selection.json` atomically only when publication is authorized.

Interrupted work must resume from verified cache entries and completed deterministic
conversion outputs. A failed build or publication must leave the previous immutable
selection usable. Unchanged inputs, recipes and tool identities must produce identical
bundle bytes; a changed recipe or tool identity mints a new provenance identity even
when its semantic output is equivalent.

The current cold-build benchmark and its measurement environment are volatile status,
so they live only in `STATUS.md` or generated benchmark reports. The established
baseline must be protected: compare like-for-like cold, warm and resumed runs, and do
not accept a material regression without a recorded cause and integration-owner
approval.

## Source selection and precedence

The catalog records every archive entry needed to reproduce retail overlay
selection. Selection must be deterministic and include:

- canonical virtual path and case handling;
- archive identity and overlay precedence;
- source-entry size and digest;
- duplicate and case-collision decisions;
- inheritance and INI override provenance; and
- the root query or dependency edge that made the entry reachable.

Unknown reference fields are evidence gaps, not permission to omit a dependency.
Converters fail closed on malformed, ambiguous, unsupported, escaping, oversized, or
unattested inputs. Silent generated substitutes are forbidden in private parity mode.

## Conversion boundaries

The established converter families cover bounded W3D model/hierarchy/animation work,
textures and atlas crops, audio, fonts/localization, particles, and SAGE map facts.
Each converter must:

- accept a declared, bounded source set;
- write only inside its isolated private job root;
- avoid source timestamps and absolute paths in canonical output;
- record source, recipe and tool provenance;
- make lossy substitutions explicit and reviewable;
- reject missing mandatory semantic evidence; and
- emit runtime-native files rather than retail source formats.

Map conversion preserves exact source facts before presentation simplification:
height, passability, water, roads, starts, waypoints, terrain layers, object placements,
scripts and triggers are separate typed outputs. A rendered approximation must not
replace authoritative map facts.

## Pack assembly and loading

A private retail pack is self-contained, immutable, non-redistributable, and identified
by its canonical bundle digest. Its manifest declares safe relative paths, data policy,
limits, source/profile identity, recipe/tool provenance, and conversion capabilities.
The published pack must not require the retail installation or importer toolchain at
runtime.

Private parity mode is strict:

- mount exactly the integration-owner-selected retail pack;
- require the expected pack/profile identity;
- resolve required definitions and assets from that pack;
- reject missing or escaping paths;
- reject incomplete required capabilities; and
- never fill a missing retail requirement from the loose legacy or synthetic base
  content.

Loose repository and user packs remain a separate development/modding lane described in
`MODDING.md`. Passing in that lane is not BFME2 parity evidence.

### Runtime selection-source precedence

`ModLoader.list_pack_roots` resolves the active content selection from exactly one
source, in this order:

1. **External override** — `OPENBFME_CONTENT` (developer/CI, ephemeral).
2. **Repo workspace** — `<repo>/.private/content-packs/selection.json`, detected
   automatically for non-exported runs when the durable cache settings are at their
   defaults (`openbfme/content/workspace_content_root` overrides the location).
3. **Durable user cache** — `user://content-packs/selection.json`.

The workspace outranks the durable cache so an editor launch without env setup can
never silently play a stale durable copy of the same ruleset. A workspace that exists
but cannot be loaded (missing or unusable `selection.json`) falls back to the durable
cache with a loud recorded diagnostic naming the stale risk. The winning source is
reported at boot as `[ModLoader] content source=<external|workspace|durable> active=...`
and exposed as `ModLoader.active_content_source`.

Installs that cannot see a repo workspace (exported builds, other machines) are kept
fresh by publishing the workspace selection into the durable cache with
`tools/publish-durable-pack.ps1` (copies the selected bundles and writes
`selection.json` last, so a partial publish is never selectable). This copies
retail-derived bytes only into the local user cache, never into the repository or a
distributable artifact.

## Provenance and retention

Canonical provenance covers every source entry, converter recipe, pinned tool,
conversion output and assembled file. It must be semantic enough to explain why a file
exists and how to reproduce it, while excluding retail absolute paths and timestamps.

Retain the selected immutable bundle, its source effective view, profile, plan,
provenance, tool manifest and verification reports. Cleanup is dry-run first and must
never remove the selected bundle or evidence needed to reproduce it.

## Change procedure

1. Define one observable pipeline outcome and narrow path lock.
2. Run the smallest relevant importer test or audit first.
3. Build with `--no-publish` while investigating.
4. Compare source, semantic output, provenance and timing to the prior identity.
5. Run pack/runtime containment checks.
6. Let only the integration owner publish or change the selected pack.

The final M2 decision remains owned by `VERIFICATION.md`; importer checks alone cannot
declare the slice complete.
