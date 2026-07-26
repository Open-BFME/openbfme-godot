# Launcher and Windows releases

OpenBFME Windows releases are code-only. The release contains the game,
launcher, importer, and conversion runtime, but no EA retail files and no
converted retail content. A playtester supplies a locally installed copy of
BFME II 1.06; the converted pack remains on that PC under the launcher's private
install state.

## Playtester instructions

1. Open the GitHub Release supplied by the playtest owner.
2. Download `OpenBFME-Launcher-<version>-windows-x64.zip`. Optionally download
   `SHA256SUMS.txt` and compare the ZIP with:

   ```powershell
   Get-FileHash .\OpenBFME-Launcher-<version>-windows-x64.zip -Algorithm SHA256
   ```

3. Extract the complete ZIP to a new folder. Do not run the executable from
   inside the archive.
4. Run `OpenBFME.Launcher.exe`. Windows may show an unknown-publisher warning
   because this first release lane does not Authenticode-sign the executable.
5. The launcher checks the configured GitHub release feed, verifies the detached
   manifest signature before parsing it, downloads both packages into a new
   immutable version directory, verifies package and installed-file hashes, and
   selects the new version only after verification succeeds.
6. Let the launcher detect BFME II, or use **Browse** to select the folder that
   contains the retail game's `game.dat`.
7. Choose **Import BFME II**. The first import verifies or downloads the pinned
   Blender, OpenSAGE, and FFmpeg tools and converts the required Men/Fords data
   locally. The launcher includes its own pinned Python runtime.
8. When conversion and selection complete, choose **Play OpenBFME**.

The initial download may be large and the first retail conversion may take
time. A failed download, signature check, package hash, inventory check, import,
or installed-file verification does not replace the selected version or select
an incomplete content pack.

## Release trust model

The launcher embeds the host, repository, and default channel from
`config/release-source.json` at build time. There is no owner/name fallback in
launcher code. A release build also resolves that file and fails when its
repository differs from `${{ github.repository }}`.

`release-manifest.json.sig` is a base64 detached RSA/SHA-256 PKCS#1 v1.5
signature. The launcher verifies it against the public key embedded in
`ManifestSignature.cs`; the same public key is stored in
`tools/release/release-manifest-public.pem` for CI and VM verification.
Only after signature verification does the launcher accept the manifest's
repository, channel, version, commit, package names, URLs, sizes, and SHA-256
hashes.

The updater installs into `%LOCALAPPDATA%\OpenBFME` by default, keeps immutable
version directories, rejects automatic downgrades, and preserves the previous
verified version for rollback.

## Launcher flags

```text
--channel stable|playtest|nightly
--manifest-url <approved GitHub HTTPS URL>
--install-root <directory>
--no-update
--verify-only
--headless
--import-bfme2 --bfme2-path <directory>
--import-rotwk --rotwk-path <directory>
```

Unknown or duplicate flags fail closed. Manifest and package URLs must use the
configured GitHub host and repository release path; redirects are restricted to
the approved GitHub release-asset hosts.

The current milestone and release acceptance are BFME II Men-versus-Men on Fords
of Isen II. The launcher exposes a RotWK import surface, but RotWK is not part of
this release milestone and is not accepted by the Windows release VM.

## Channel limitation before the first release

The launcher currently gives every selected channel the same GitHub
`releases/latest/download/release-manifest.json` default. GitHub's `latest`
release route does not select prereleases, while suffixed tags are published as
prereleases with a `playtest` manifest. Therefore:

- a normal stable tag such as `v0.1.0` can be discovered by the default feed;
- a playtest prerelease such as `v0.1.0-playtest.1` is not automatically
  discoverable through the default `latest` URL;
- a prerelease playtester must be launched with `--channel playtest` and an
  explicit manifest URL under that immutable GitHub Release, or the launcher
  feed implementation must be fixed before relying on automatic prerelease
  updates.

This is a real first-playtest limitation, not a documentation convention.

## What each release publishes

Every published GitHub Release contains:

- `OpenBFME-<version>-windows-x64.zip`;
- `OpenBFME-Launcher-<version>-windows-x64.zip`;
- `release-manifest.json`;
- `release-manifest.json.sig`;
- `SHA256SUMS.txt`.

The game ZIP is what the launcher installs and launches. The launcher ZIP is the
bootstrap download for a new playtester and the self-update package for existing
installations. The manifest and signature drive updates. `SHA256SUMS.txt` gives
people an independent package checksum surface.

## Maintainer release path

An ordinary manual workflow run builds, scans, attests, and uploads an unsigned
Actions artifact. It does not access the signing key. A manual run requests
signing only when `run_acceptance` is explicitly enabled on a protected ref.

A protected `v*` tag runs the complete sequence:

```text
build unsigned packages and metadata
  -> release-signing environment approval
  -> sign and verify the manifest
  -> clean Windows BFME II VM acceptance
  -> production-release environment approval
  -> gh release create --verify-tag
```

The `release-signing` environment blocks access to the private manifest key.
The `production-release` environment blocks the job with `contents: write`.
The Windows VM acceptance dependency blocks publication until the packaged
launcher has produced two byte-identical complete packs and the packaged game
has launched against the selected pack.

The YAML requires protected refs and declares both environments, but repository
owners must configure required reviewers, prevention of self-review, and
administrator-bypass prevention plus deployment branch/tag restrictions in
GitHub. The signing key must exist only as the
`OPENBFME_RELEASE_SIGNING_KEY` secret on the `release-signing` environment. An
environment name without those UI protection rules is not a security boundary.
If the repository's GitHub plan does not offer required reviewers for its
private visibility, the owner must not release from this design until that
capability exists or an independently reviewed external signing service replaces
it.

Complete first-release setup, including RSA key creation/validation, branch and
tag protection, both environment configurations, and the self-hosted runner
labels `[self-hosted, windows, x64, openbfme-release-vm]` plus
`BFME2_RETAIL_PATH`, is in `docs/BUILD_AND_RELEASE.md`.

## Failure and recovery

- A manifest signature mismatch stops update processing before the manifest is
  trusted.
- A repository, channel, URL, size, package hash, archive path, or installed
  inventory mismatch stops installation before selection changes.
- A failed import does not select an incomplete pack.
- **Roll back** selects the previous verified engine version.
- Launcher diagnostics redact absolute retail paths where they leave the
  packaged acceptance boundary.

Campaigns and War of the Ring are not supported by this release system.
