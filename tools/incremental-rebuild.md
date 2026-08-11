# Incremental faction rebuilds

The faction object cache combines these identities:

- the full archive `catalog_identity_sha256` safety backstop;
- the current hand-curated `sourceDocuments` closure emitted by each descriptor
  compiler;
- a family-scoped, transitive Python-module identity for unit, structure, or
  spellbook compilation, including the cache payload writer and every Blender
  adapter;
- the whole faction graph, prepared numeric defines, policy roots, and every
  effective-assets manifest row outside `data/ini/**`.

If source provenance is absent, a declared compiler module is missing, Python
dependency discovery is dynamic, or a family is unknown, identity expands to
the full document/compiler corpus. Doubt therefore costs time rather than
serving stale output.

The honest measured saving is compiler-only: family scoping stops a compiler
edit in one lane from evicting unrelated lanes. Before review hardening, the
unit/structure/spellbook identities selected 57/63/60 of 162 importer modules.
The review fix adds `faction_import.py` plus three `importer/blender/*.py`
adapters to every lane, so the current live identities select 61/67/64 of 165
source files. This is not a broad content-rebuild saving claim.

`catalog_identity_sha256` still dominates content edits: any archive INI change
changes the catalog identity and evicts every object. It must remain in the key
because `sourceDocuments` is not a complete dependency graph today. There are
four hand-curated closure implementations; structures omit at least
`gamedata.ini` and `commandset.ini`, and `_source_rows` drops requested paths
that are absent from its indexed documents instead of broadening the closure.
The explicitly named follow-up is **INCREMENTAL-CLOSURE-COMPLETION**: make every
compiler emit a mechanically complete, fail-closed consumed-document closure,
then prove it before considering removal of the catalog backstop.

## Review fixes

- Every family identity now directly hashes `faction_import.py`, which produces
  the cached row and artifact envelope. A guard accounts for every module from
  the former all-family salt in every lane or a commented lane exclusion.
- Every `importer/blender/*.py` adapter is hashed into every family identity.
- Effective-assets hashing excludes only `data/ini/**`; root, script, language,
  and map INIs remain protected by the durable manifest component.
- Prepared numeric defines and the whole faction graph are key components, so
  undeclared `gamedata.ini` dependencies and mapped-image rows cannot return a
  stale recipe/runtime.
- `--reconvert-only` fails when its patterns match zero W3D asset ids; clearing
  compiler identity memoization clears both memo layers.
- `-SingleBuild` terminal PASS markers carry
  `attested=false mode=single-build`; default cold A/B markers carry
  `attested=true mode=cold-a-b`.
- The committed retail module census was regenerated because
  `incremental_rebuild.py` grows the live importer consumer closure from 65 to
  66 files.

## Code-only validation

No retail build or publish was run in this lane. With the pinned Python 3.12
environment and BFME2 install binding:

- the five touched suites passed: 96 tests and 53 subtests;
- the regenerated module-census suite passed: 29 passed, 1 skipped (the
  default-state-root retail regeneration check; explicit regeneration against
  the integration state root completed successfully);
- the 46-file related sweep reported 992 passed, 28 skipped, and 139 subtests
  passed, with only the two documented pre-existing
  `test_w3d_chunk_backlog.py` provenance failures;
- the full importer suite reported 2674 passed, 174 skipped, and 964 subtests
  passed, with the same two failures and two Pillow deprecation warnings.

## Operator surface

Dry-run current coverage and cache state without converting:

```powershell
$python = '<pinned-python>'
$env:PYTHONPATH = '<worktree>\importer'
& $python tools\openbfme_import.py --json --state-root '<state-root>' rebuild-status --faction men --coverage-root '<coverage-root>' --assets-root '<effective-assets>'
```

Force only selected W3D asset ids cold. Patterns are case-insensitive shell
patterns and repeatable. Provenance records `reconversion.scope=partial`, the
exact patterns, and `fullCorpusReconverted=false`.

```powershell
& $python tools\openbfme_import.py --state-root '<state-root>' build --game bfme2 --install '<bfme2-install>' --profile '<profile.json>' --reconvert-only '*uruk*' --no-publish
```

The release gate remains cold A/B by default. Developer iteration can request
one build explicitly; both the console and pack provenance state that the
result is not reproducibility-attested.

```powershell
tools\gate-retail.ps1 -Install '<bfme2-install>' -SkipPrivateSelection -SingleBuild
```

## Real-content validation for the integration owner

These commands intentionally were not run in the code-only lane:

```powershell
$repo = '<integrated-open-bfme-checkout>'
$python = '<pinned-python>'
$state = '<private-state-root>'
$install = Join-Path $state 'editions\rotwk\layered-install\layer-1-bfme2'
$env:PYTHONPATH = Join-Path $repo 'importer'
$env:BFME2_INSTALL = $install

# Proof 1: establish current coverage and record every object key/rebuild reason.
& $python (Join-Path $repo 'tools\openbfme_import.py') --json --state-root $state rebuild-status --faction men --coverage-root (Join-Path $state 'reports\faction-import') --assets-root (Join-Path $state 'cache\effective-assets')

# Proof 2: build cold with the object cache disabled and retain a recursive
# SHA-256 inventory of every produced pack byte. Do not publish or select it.
$env:OPENBFME_NO_OBJECT_CACHE = '1'
& $python (Join-Path $repo 'tools\openbfme_import.py') --json --state-root $state build --game bfme2 --install $install --profile (Join-Path $state 'profiles\men-fords-v0-complete.generated.json') --single-build --no-publish
$env:OPENBFME_NO_OBJECT_CACHE = $null

# Proof 3: run once with the post-fix cache enabled to populate it, then repeat
# warm, require cacheHits > 0 on that second cache-enabled run, and prove the
# warm run's complete pack-byte inventory is byte-identical to Proof 2 (not
# merely the coverage aggregate or bundle id).
& $python (Join-Path $repo 'tools\openbfme_import.py') --json --state-root $state build --game bfme2 --install $install --profile (Join-Path $state 'profiles\men-fords-v0-complete.generated.json') --single-build --no-publish
& $python (Join-Path $repo 'tools\openbfme_import.py') --json --state-root $state build --game bfme2 --install $install --profile (Join-Path $state 'profiles\men-fords-v0-complete.generated.json') --single-build --no-publish

# Proof 4: on disposable source/package copies, mutate faction_import.py, each
# Blender adapter, an out-of-data/ini map.ini row, a numeric define, and a
# consumed mappedImages row in turn; require the affected object keys to move
# and require zero stale cache hits. Restore each copy before the next proof.

# Proof 5: verify a matching scoped reconversion records its exact pattern and
# partial/non-attested provenance, while a deliberately unmatched pattern exits
# non-zero before any converter runs.
& $python (Join-Path $repo 'tools\openbfme_import.py') --json --state-root $state build --game bfme2 --install $install --profile (Join-Path $state 'profiles\men-fords-v0-complete.generated.json') --reconvert-only '*uruk*' --single-build --no-publish

# Release proof remains the full cold A/B path (omit -SingleBuild).
& (Join-Path $repo 'tools\gate-retail.ps1') -Install $install -SkipPrivateSelection
```

After the scoped proof, inspect
`<state-root>\packs\<pack-id>\provenance\manifest.json` and require the
recorded pattern, partial reconversion scope, and `attested=false`. Preserve the
cold and warm recursive SHA-256 inventories as evidence. The final release
claim comes only from the default gate's equal cold A/B bundle hashes.
