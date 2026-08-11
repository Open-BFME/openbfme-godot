# Incremental faction rebuilds

The faction object cache uses two independent identities:

- the exact `sourceDocuments` closure emitted by each descriptor compiler;
- a family-scoped, transitive Python-module identity for unit, structure, or
  spellbook compilation.

If source provenance is absent, a declared compiler module is missing, Python
dependency discovery is dynamic, or a family is unknown, identity expands to
the full document/compiler corpus. Doubt therefore costs time rather than
serving stale output. Non-INI effective assets retain a separate manifest hash,
so a model or texture change still invalidates converted recipes.

## Operator surface

Dry-run current coverage and cache state without converting:

```powershell
$python = 'C:\Users\Jonathan\Desktop\open-bfme\.private\retail-work\tools\python-3.12-env\Scripts\python.exe'
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
$python = 'C:\Users\Jonathan\Desktop\open-bfme\.private\retail-work\tools\python-3.12-env\Scripts\python.exe'
$state = 'C:\Users\Jonathan\Desktop\open-bfme\.private\retail-work'
$install = Join-Path $state 'editions\rotwk\layered-install\layer-1-bfme2'
$env:PYTHONPATH = Join-Path $repo 'importer'
$env:BFME2_INSTALL = $install

# Establish current coverage, then inspect the next conversion without writing.
& $python (Join-Path $repo 'tools\openbfme_import.py') --json --state-root $state rebuild-status --faction men --coverage-root (Join-Path $state 'reports\faction-import') --assets-root (Join-Path $state 'cache\effective-assets')

# Developer proof: no publish or selection change; one W3D feature is forced.
& $python (Join-Path $repo 'tools\openbfme_import.py') --json --state-root $state build --game bfme2 --install $install --profile (Join-Path $state 'profiles\men-fords-v0-complete.generated.json') --reconvert-only '*uruk*' --single-build --no-publish

# Release proof remains the full cold A/B path (omit -SingleBuild).
& (Join-Path $repo 'tools\gate-retail.ps1') -Install $install -SkipPrivateSelection
```

After the scoped developer proof, inspect
`<state-root>\packs\<pack-id>\provenance\manifest.json` and require the
recorded pattern, partial reconversion scope, and `attested=false`. The final
release claim comes only from the default gate's equal cold A/B bundle hashes.
