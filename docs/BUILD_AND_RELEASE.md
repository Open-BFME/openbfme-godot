# Build and release OpenBFME for Windows

The `windows release` workflow in `.github/workflows/release.yml` is the
authoritative distributable-build path. It produces code-only Windows packages.
It never packages a retail BFME II install or a converted retail content pack.

Operator cadence (playtest weekly tags, stable cuts, prerequisites): see
[RELEASE_CYCLE.md](RELEASE_CYCLE.md). Helper script:
`tools/release/Start-PlaytestRelease.ps1`.

Linux: the launcher is WPF win-x64; run the **self-contained** build under Wine
(see [LAUNCHER_LINUX_WINE.md](LAUNCHER_LINUX_WINE.md)).

## Release assets

A published GitHub Release contains exactly these five assets:

- `OpenBFME-<version>-windows-x64.zip`: the Godot game executable and PCK.
- `OpenBFME-Launcher-<version>-windows-x64.zip`: the self-contained WPF
  launcher, bundled importer source, pinned Python runtime, and bundle inventory.
- `release-manifest.json`: repository, version, channel, commit, package names,
  package URLs, compressed and expanded sizes, and SHA-256 hashes.
- `release-manifest.json.sig`: a base64-encoded detached RSA/SHA-256 PKCS#1 v1.5
  signature over the exact manifest bytes.
- `SHA256SUMS.txt`: SHA-256 hashes for both ZIP files.

The GitHub Actions build artifact has a sixth, internal distinction:
`openbfme-<version>-windows-unsigned` is produced by the ungated build job and
does not contain `release-manifest.json.sig`. The final
`openbfme-<version>-windows` Actions artifact is created only by the gated
signing job.

## What the workflow does

The workflow has two entry points:

- `workflow_dispatch` performs a validation build. By default it stops after the
  unsigned Actions artifact and never requests the signing key.
- A protected, signed, annotated tag matching `v*` performs the complete
  build, signing, packaged-VM acceptance, and publication path.

The jobs run in this order:

1. `build` checks out the exact commit with full history, resolves the release
   target through `tools/release_source.py`, and requires that it equal
   `${{ github.repository }}`. This prevents a manifest from naming one
   repository while the workflow publishes to another.
2. A tag build asks the GitHub API to verify that the tag is annotated and
   cryptographically signed. It then proves the tagged commit is an ancestor of
   the repository's default branch.
3. Source gates run the importer, engine, launcher, release-tool,
   export-firewall, reproducibility-comparator, dependency-audit, and launcher
   protocol checks.
4. The job downloads the exact Godot 4.7 editor and templates named in the
   workflow and checks both archives against fixed SHA-512 values before use.
5. `Build-CodeOnlyExport.ps1` stages `game/` outside the checkout while
   excluding `game/data/base`, `.godot`, captures, screenshots, and import
   metadata. Godot imports, exports, and launches the code-only game headlessly;
   logged warnings or errors fail the job even when Godot exits zero.
6. `dotnet publish` creates a self-contained, single-file, `win-x64` WPF
   launcher. The job archives the importer sources from the exact commit,
   writes `release-identity.json`, constructs the shipped Python runtime, writes
   `openbfme-bundle-inventory.json`, and runs the launcher headlessly.
7. Both ZIPs pass `Test-ReleaseArtifact.ps1`, which rejects unsafe or duplicate
   archive paths, retail-format files, private state, game packs, agent
   instructions, credentials, private keys, developer paths, links, Python
   startup customization, bytecode, and oversized opaque entries.
8. The build job creates the unsigned manifest and checksums, uploads the
   unsigned Actions artifact, and records GitHub build-provenance attestations
   for both ZIPs, the manifest, and the checksum file. This job has no signing
   environment and no signing secret.
9. `sign` can run only for a protected ref and either a tag build or an explicit
   protected-ref manual acceptance run. It enters the `release-signing`
   environment, downloads the unsigned artifact, signs the manifest, and
   verifies the new signature against `tools/release/release-manifest-public.pem`
   before uploading the final signed artifact.
10. `windows-vm-acceptance` downloads that signed artifact on the dedicated
    runner. It verifies the signature, verifies both ZIP hashes against both
    metadata files, scans both ZIPs again, runs the packaged launcher twice from
    empty state against BFME II, compares the complete generated packs
    byte-for-byte and by required asset-family counts, and smoke-launches the
    packaged game with the selected pack.
11. `publish` enters the separate `production-release` environment only after
    build, signing, and VM acceptance pass. Its `contents: write` token invokes
    `gh release create` with `--verify-tag`; prerelease versions receive
    `--prerelease`.

## Security gates: YAML versus GitHub settings

The YAML enforces all of the following:

- the build job cannot reference `OPENBFME_RELEASE_SIGNING_KEY`;
- the signing secret is referenced exactly once, on the signing step;
- signing, VM acceptance, and publication require `github.ref_protected`;
- ordinary manual validation creates a useful unsigned artifact without signing;
- the signing job declares `environment: release-signing`;
- the publication job declares `environment: production-release`;
- only the publication job receives `contents: write`;
- publication depends on the successful signed artifact and Windows VM result.

Those declarations are not sufficient on their own. A repository writer can
edit workflow YAML on an unprotected branch. Before the first release, the
owner must configure the following controls in GitHub's web UI:

1. Open **Repository Settings > Rules > Rulesets**. Create an active branch
   ruleset for the default branch (currently `main`) that requires reviewed pull
   requests and prevents unreviewed direct changes to the release workflow.
2. In **Repository Settings > Rules > Rulesets**, create an active tag ruleset
   covering `v*`. Restrict who can create or update those tags, and prevent tag
   deletion or non-fast-forward changes. The pushed release tag must make
   `github.ref_protected` evaluate to `true`.
3. Open **Repository Settings > Environments > New environment** and create
   `release-signing`. Add required reviewers, enable prevention of self-review,
   disallow administrator bypass, and choose selected deployment branches and
   tags: the protected default branch plus protected `v*` tags. Under that
   environment's **Environment secrets**, create
   `OPENBFME_RELEASE_SIGNING_KEY` containing the complete private PEM.
4. Delete any repository-level or organization-level
   `OPENBFME_RELEASE_SIGNING_KEY`. If a copy remains at either broader scope, an
   untrusted workflow can request that broader secret without entering the
   signing environment.
5. Under **Repository Settings > Environments**, create `production-release`.
   Add required reviewers, enable prevention of self-review, disallow
   administrator bypass, and choose selected deployment tags matching protected
   `v*`. This gate controls the job that receives `contents: write` and creates
   the GitHub Release.

An environment with no required reviewer and no deployment branch/tag
restriction is only a name. Do not approve a release until both environments
show the configured protection rules.

The configured release target is currently described as a private repository.
GitHub plan availability matters: GitHub documents required reviewers on private
repositories as unavailable on Free, Pro, and Team plans. If the environment UI
does not offer required reviewers for this private repository, this design does
not meet the required signing-key boundary. Move to a plan/repository visibility
that supports the protection or use an independently reviewed external signing
service; do not cut a release with an unreviewed signing environment.

GitHub supplies `GITHUB_TOKEN`; no owner-created token or PAT is required.

## Create or validate the manifest signing key

`Sign-ReleaseManifest.ps1` requires an unencrypted PKCS#8 PEM beginning with
`-----BEGIN PRIVATE KEY-----`. The launcher verifies RSA/SHA-256 PKCS#1 v1.5
signatures. The tracked public key is 3072-bit and is pinned in two places:

- `tools/release/release-manifest-public.pem`;
- `ProductionPublicKey` in
  `launcher/OpenBFME.Launcher/ManifestSignature.cs`.

The launcher test fails if those two public copies differ.

Important: a newly generated private key cannot be made to match the public key
already in the repository. If the owner has the existing corresponding private
key, validate it as shown below. If that private key does not exist, generate a
new pair, replace both public-key pins, run the launcher tests, review and commit
that rotation, and only then cut the first tag. The current pipeline cannot sign
a valid manifest without the private half of the pinned key.

Generate a new pair outside the checkout:

```powershell
$OpenSsl = "C:\Program Files\Git\usr\bin\openssl.exe"
$SecureRoot = "D:\OpenBFME-release-key"
New-Item -ItemType Directory -Path $SecureRoot
& $OpenSsl genpkey `
  -algorithm RSA `
  -pkeyopt rsa_keygen_bits:3072 `
  -out (Join-Path $SecureRoot "openbfme-release-signing-private.pem")
& $OpenSsl pkey `
  -in (Join-Path $SecureRoot "openbfme-release-signing-private.pem") `
  -pubout `
  -out (Join-Path $SecureRoot "release-manifest-public.pem")
```

Never create the private file inside the repository, commit it, attach it to a
release, or paste it into an issue or Actions log. Keep a protected offline
backup.

For a key rotation, copy the generated public PEM to
`tools/release/release-manifest-public.pem`, then replace the complete
`ProductionPublicKey` PEM value in
`launcher/OpenBFME.Launcher/ManifestSignature.cs` with those exact bytes. The
two copies must be identical after normalizing CRLF to LF. The private PEM never
goes into either file.

Validate an existing private key against the tracked public key:

```powershell
$OpenSsl = "C:\Program Files\Git\usr\bin\openssl.exe"
$PrivateKey = "D:\OpenBFME-release-key\openbfme-release-signing-private.pem"
$DerivedPublic = Join-Path $env:TEMP "openbfme-derived-public.pem"
& $OpenSsl pkey -in $PrivateKey -pubout -out $DerivedPublic
if ($LASTEXITCODE -ne 0) { throw "The private signing key is invalid." }
$derived = (Get-Content -LiteralPath $DerivedPublic -Raw).Replace("`r`n", "`n").Trim()
$tracked = (Get-Content -LiteralPath tools\release\release-manifest-public.pem -Raw).
  Replace("`r`n", "`n").Trim()
Remove-Item -LiteralPath $DerivedPublic -Force
if ($derived -cne $tracked) { throw "The private key does not match the pinned release key." }
dotnet run `
  --project launcher\OpenBFME.Launcher.Tests\OpenBFME.Launcher.Tests.csproj `
  --configuration Release
```

After validation, paste the complete private PEM into the
`OPENBFME_RELEASE_SIGNING_KEY` secret under the `release-signing` environment.

## Dedicated Windows acceptance runner

Open **Repository Settings > Actions > Runners** to register one online
self-hosted Windows runner. The `windows-vm-acceptance` job requires all of
these labels:

```text
self-hosted
windows
x64
openbfme-release-vm
```

The runner account must be able to read a lawful BFME II 1.06 installation.
Define `BFME2_RETAIL_PATH` in the runner service's environment as the installation
directory. If the runner is installed as a Windows service, restart the service
after adding or changing the variable so the worker process inherits it. The job
fails immediately when the variable is missing or the directory is unavailable.

Do not place retail files in the checkout, an Actions artifact, a runner
diagnostic upload, or a GitHub Release. The acceptance receipt contains counts,
hashes, and release identity, not retail payload bytes.

## Python versions

The contributor bootstrap and shipped-player runtime have separate destination
trees but share one interpreter identity:

- both start from the hash-pinned `python-build-standalone` 3.12.13+20260718
  distribution provisioned by `tools/Install-PinnedPython.ps1`;
- contributor setup keeps it under `workspace/retail-work/tools/`;
- the Windows release workflow installs the same archive under
  `RUNNER_TEMP`, creates a fresh venv from it, and passes that venv interpreter
  to `New-PinnedPythonRuntime.ps1`;
- `New-PinnedPythonRuntime.ps1` does not trust the version string alone. It
  checks the version, launcher hash, base DLL hash, bounded runtime-tree hash,
  dependency hashes, startup behavior, and final bundle inventory.

The earlier contributor 3.12.13 versus shipped-player 3.12.10 skew was a real
defect after `bootstrap.py` adopted the 3.12.13 runtime hashes: the launcher
would reject the 3.12.10 bundle during `bootstrap-tools`. The release lane now
uses 3.12.13 as well. `actions/setup-python` remains only on the VM acceptance
runner as a 3.12.13 standard-library host for
`compare_import_bundles.py`; it is not copied into the launcher.

## Local preflight

From PowerShell in the repository root:

```powershell
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass `
  -File tools\release\Test-ReleaseTools.ps1
dotnet run `
  --project launcher\OpenBFME.Launcher.Tests\OpenBFME.Launcher.Tests.csproj `
  --configuration Release
python -m unittest tools.release.test_compare_import_bundles -v
```

The workflow is the packaging source of truth. Local checks do not exercise
GitHub environment protections, GitHub tag verification, provenance issuance,
the hosted build image, or the dedicated retail VM, and do not authorize
publication.

## Cut a release

After all owner setup and milestone approval are complete:

```powershell
git tag -s v0.1.0 -m "OpenBFME 0.1.0"
git push origin v0.1.0
```

The tag must point to a commit on the protected default-branch ancestry.
Versions with a suffix, such as `v0.1.0-playtest.1`, are published as GitHub
prereleases and receive the `playtest` manifest channel. Versions without a
suffix receive the `stable` channel.

The launcher executables are not Authenticode-signed by this workflow. Windows
may display an unknown-publisher warning. Manifest signing protects update
integrity but is not a substitute for Windows publisher identity.
