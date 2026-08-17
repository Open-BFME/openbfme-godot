# Contributing

Small, tested changes beat large rewrites. Public repo is source-only alpha.

## Read first

1. [README.md](README.md)
2. [DIRECTION.md](DIRECTION.md)
3. [STATUS.md](STATUS.md)
4. The guide for the area you touch (`docs/`)

## Never commit

- Retail BFME/BFME2/RotWK archives or extracts
- Converted models, textures, maps, audio, packs under `workspace/`
- Secrets, personal absolute paths, private keys
- Agent-only instruction dumps meant for private workflows

Public fixtures must be project-authored. If retail or a secret lands in a
commit, stop and report - do not "fix forward" while it remains in history.

```bat
powershell -File tools\export-scan.ps1
```

## PR shape

- One observable outcome
- Named source or oracle when claiming parity
- Explicit non-goals
- Smallest command that can disprove the change
- No silent generic fallbacks inventing retail behavior

Example checks:

```bat
powershell -File tools\gate-rotwk-systems.ps1 -SkipLiveRetail
run_importer_tests.bat
run_retail_slice.bat --test
```

Report what you ran. Warnings, leaked paths, or weakened asserts are failures.

## Product rules (short)

- Parity baseline: **RotWK 2.01** (systems-first).
- BFME2 is base/comparison, not a second full product freeze.
- Integration owner owns pack selection / final publish gates.

Details: [docs/CONTENT_PIPELINE.md](docs/CONTENT_PIPELINE.md),
[docs/VERIFICATION.md](docs/VERIFICATION.md).
