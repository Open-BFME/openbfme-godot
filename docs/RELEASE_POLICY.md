# OpenBFME release and distribution policy

> **Owner:** Release integration owner
> **Owns:** Private-content containment, code-only export, client/importer/server packaging, release provenance, update/rollback, and distribution prerequisites.
> **Does not own:** Legal advice, gameplay parity approval, mod licensing decisions for third parties, or network simulation design.
> **Last verified commit:** `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
> **Update trigger:** A package boundary, export scan, updater, supported platform, redistribution rule, or release prerequisite changes.
> **Validation:** Code-only export scan, provenance manifest audit, clean-machine bootstrap, and package-specific smoke tests.

## Status

OpenBFME is currently a private compatibility build, not a public retail-content
distribution. This policy is an engineering containment contract, not a legal-status
finding. Public branding, licensing and distribution review still require explicit
owner review before release.

## Content lanes

### Private retail lane

The user's BFME2 1.06 installation, extracted source entries, decoded media, converted
assets, runtime retail packs, oracle captures and importer work products remain only
below:

```text
.private/retail-work
.private/content-packs
```

They are non-redistributable, gitignored, excluded from exported builds and never
served to clients. A private pack declares `redistributable: false`. The runtime may
mount it locally, but the client, listen server and dedicated server do not transfer
its payloads.

### Repository-authored lane

Original, generated and legal-safe fixtures may be committed and distributed when
their provenance and applicable rights are recorded. These assets may support tests,
bootstrap and non-parity fallback modes. They must never silently replace a required
retail asset in strict parity mode.

### Third-party licensed lane

Every included dependency or asset requires a `THIRD_PARTY.md` ledger entry with
source, use, license, integration form and pin/provenance authority. Inclusion also
requires compliance with its actual license terms; a ledger entry is not approval by
itself.

## Public deliverable

The intended public deliverable is code-only. It may include repository-authored and
approved third-party material, but no retail or converted retail payload. A user with a
lawfully owned installation runs the importer locally to create a private pack.

Before any shareable source archive or binary:

- build from a clean export root that never contains `.private`;
- reject retail virtual payloads, known metadata, source paths and known payload
  digests at the export boundary;
- scan archives, installers, symbols, logs, screenshots, fixtures and support bundles;
- verify no importer cache or selected private pack was copied;
- verify no donor/OpenSAGE runtime dependency was introduced accidentally;
- audit the release provenance manifest for every shipped file; and
- run the firewall's negative fixtures/self-test so a broken scanner cannot pass.

Retail filenames and digests may be used as bounded non-payload provenance or scan
signatures when necessary; raw or decoded content may not.

## Package boundaries

Release packaging is separated into:

- **Client:** Godot presentation, local simulation client, UI/audio and mod support.
- **Importer/launcher:** local discovery, doctor, resumable conversion, pack validation,
  safe selection and launch. It does not bundle retail payloads.
- **Dedicated server:** headless `MatchHost`, simulation and administration. It does not
  require or distribute presentation packs.

The same authoritative match implementation serves local, listen and dedicated modes.
Windows client support and Windows/Linux server support require cross-platform replay
digest evidence before being advertised.

## Self-hosted multiplayer

OpenBFME does not require Steam, mandatory accounts, ranked infrastructure or a central
match service. Supported discovery may include direct IP/hostname, LAN discovery and an
optional community server list. Servers may use passwords, allowlists, configuration
files and CLI overrides.

Self-hosted games are unranked and host-trusted. Transport authentication, validation
and rate limiting are required, but this is not a promise of competitive anti-cheat or
confidential transport. A VPN is an acceptable operator choice until a measured secure
transport is qualified.

Simulation compatibility uses canonical simulation/map/plugin digests. Presentation
packs are not transferred by the server and may differ only within the boundary defined
in `MODDING.md`.

## Release prerequisites

A package is releasable only when:

- its applicable milestone gate passes on the packaged revision;
- code-only containment and provenance audits pass;
- clean-machine bootstrap and launch succeed;
- configuration and secrets are absent from the artifact;
- licenses/notices for included third parties are present;
- installer/uninstaller and rollback behavior are tested;
- diagnostics are sanitized; and
- known limitations are recorded in `STATUS.md`.

M2 private completion is governed solely by
`run_m2_acceptance.bat -IntegrationOwnerPublish`; it is not by itself authorization for
a public release.

## Updates, rollback and support

The production updater target is signed, immutable and rollback-capable. An update is
installed to a new version directory, verified before selection, and leaves the prior
known-good version recoverable. Pack updates follow the same immutable-selection rule.

Safe mode starts with the last-known-good engine and pack set while disabling optional
mods. Support bundles include engine/protocol versions, sanitized configuration,
content identities, failure markers and performance summaries. They exclude secrets,
retail payloads, absolute retail paths, private oracle images and full private pack
manifests when those could reveal contained data.

## Prohibited release shortcuts

- Do not copy a working private checkout as a release artifact.
- Do not distribute preconverted retail packs.
- Do not make the server a retail-content download path.
- Do not treat `.gitignore` as the export firewall.
- Do not substitute a successful private gate for license/provenance review.
- Do not publish from a dirty or identity-ambiguous tree.
